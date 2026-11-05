# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""FreeDOS OEM boot-sector helper for authenticated nonbootable Windows 98 media.

No Microsoft files are downloaded. The caller must authenticate the ISO first.
See docs/FREEDOS-BOOT.md for provenance and the two-boot protocol.
"""
from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import tempfile

import win98_iso

BOOT_DIRECTORY = Path(__file__).resolve().parent / 'freedos_boot'
BOOT_SHA256 = '16fc322080d3363e920edd677824d3f5c0d880620decd78d0fba7ed6b1ccfcf2'
MIN_FREE = 8 * 1024**3
BOOT_FILES = ('WINBOOT.SYS', 'COMMAND.COM', 'HIMEM.SYS', 'OAKCDROM.SYS',
              'MSCDEX.EXE', 'FDISK.EXE')


def _space(path):
    if shutil.disk_usage(path).free < MIN_FREE:
        raise RuntimeError('FreeDOS bootstrap requires at least 8 GiB free space')


def boot_sector():
    data = (BOOT_DIRECTORY / 'win98-fat12.bin').read_bytes()
    if len(data) != 512 or hashlib.sha256(data).hexdigest() != BOOT_SHA256:
        raise RuntimeError('FreeDOS OEM boot sector SHA256 mismatch')
    return data


def _run(*args):
    # Never print arguments or file contents: staged answers contain a private key.
    try:
        result = subprocess.run(tuple(map(str, args)), stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, stdin=subprocess.DEVNULL, timeout=60)
    except subprocess.TimeoutExpired:
        raise RuntimeError(f'FreeDOS bootstrap command timed out: {args[0]}') from None
    if result.returncode:
        raise RuntimeError(f'FreeDOS bootstrap command failed: {args[0]}')
    return result.stdout


def _private_write(path, data):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, 'wb') as output:
        output.write(data)
    os.chmod(path, 0o600)


def _copy(volume, data, destination, scratch):
    fd, name = tempfile.mkstemp(prefix='boot-', dir=scratch)
    try:
        with os.fdopen(fd, 'wb') as output:
            output.write(data)
        _run('mcopy', '-o', '-i', volume, name, destination)
    finally:
        Path(name).unlink(missing_ok=True)


def _dos(text):
    return text.strip().replace('\n', '\r\n').encode('ascii') + b'\r\n'


def extract_boot_files(iso):
    """Select the original SE setup boot files, not duplicate installed copies."""
    files = win98_iso.setup_files(Path(iso))
    if 'BASE5.CAB' not in files:
        raise RuntimeError('Windows 98 setup boot cabinet BASE5.CAB is missing')
    # Select non-spanning records in BASE5 rather than duplicate CAB sets.
    with Path(iso).open('rb') as image:
        lba, size, flags = files['BASE5.CAB']
        if flags & 2 or not 0 < size <= 16 * 1024 * 1024:
            raise RuntimeError('Invalid Windows 98 boot cabinet')
        data = win98_iso._read(image, lba * win98_iso.SECTOR, size)
    info = win98_iso._cabinet(data, size, 'BASE5.CAB')
    with tempfile.TemporaryDirectory(prefix='neo98-boot-cab-') as directory:
        _space(directory)
        cabinet = Path(directory) / 'BASE5.CAB'
        _private_write(cabinet, data)
        result = {}
        for name in BOOT_FILES:
            matches = [(size, folder) for item, size, folder in info['files'] if item == name]
            if len(matches) != 1 or matches[0][1] >= 0xfffd:
                raise RuntimeError(f'Missing or ambiguous setup boot file {name}')
            result[name] = win98_iso._extract(cabinet, name, matches[0][0])
        # EBD.CAB spans BASE4/BASE5. The existing bounded extractor follows
        # its continuation and checks all lengths before returning the bytes.
        ebd = win98_iso._content_files(iso, files, ['EBD.CAB'])['EBD.CAB']
        ebd_path = Path(directory) / 'EBD.CAB'
        _private_write(ebd_path, ebd)
        info = win98_iso._cabinet(ebd, len(ebd), 'EBD.CAB')
        for name in ('SYS.COM', 'RESTART.COM'):
            matches = [(size, folder) for item, size, folder in info['files'] if item == name]
            if len(matches) != 1 or matches[0][1] >= 0xfffd:
                raise RuntimeError(f'Missing or ambiguous emergency boot file {name}')
            result[name] = win98_iso._extract(ebd_path, name, matches[0][0])
        return result


def prepare_bootstrap(iso, disk, floppy, answer, scratch, *, cache_dir=None,
                      partition_offset=1048576):
    """Stage two boots on an existing private FAT32 disk and fresh helper floppy.

    Caller creates NEO98 directory (stage_finish), authenticates ISO, and runs
    QEMU once with floppy, then without it after SYS_OK and a clean reboot.
    """
    scratch = Path(scratch)
    _space(scratch)
    originals = extract_boot_files(iso)
    # cache_dir is retained in the API; the tiny GPL boot sector is bundled,
    # so this path performs no downloads and writes nothing to the app bundle.
    sector = boot_sector()
    boot_path = scratch / 'freedos-oem.bin'
    _private_write(boot_path, sector)
    try:
        _private_write(floppy, b'')
        _run('mformat', '-C', '-f', '1440', '-B', boot_path, '-i', floppy, '::')
    finally:
        boot_path.unlink(missing_ok=True)
    for name, destination in (('WINBOOT.SYS', 'IO.SYS'), ('COMMAND.COM', 'COMMAND.COM'),
                              ('SYS.COM', 'SYS.COM'), ('FDISK.EXE', 'FDISK.EXE'),
                              ('RESTART.COM', 'RESTART.COM')):
        _copy(floppy, originals[name], '::' + destination, scratch)
    _copy(floppy, _dos('[Options]\nBootGUI=0\nLogo=0\nBootDelay=0'), '::MSDOS.SYS', scratch)
    _copy(floppy, _dos(r'SHELL=A:\COMMAND.COM A:\ /E:2048 /P'), '::CONFIG.SYS', scratch)
    _copy(floppy, _dos(r'''
@ECHO OFF
ECHO NEO98_DOS_BOOT>COM1
A:\FDISK.EXE /MBR
IF ERRORLEVEL 1 GOTO MBRFAIL
A:\SYS.COM A: C:
IF ERRORLEVEL 1 GOTO SYSFAIL
ECHO NEO98_SYS_OK>COM1
A:\RESTART.COM
GOTO END
:MBRFAIL
ECHO NEO98_MBR_FAIL>COM1
GOTO END
:SYSFAIL
ECHO NEO98_SYS_FAIL>COM1
:END
'''), '::AUTOEXEC.BAT', scratch)
    volume = f'{disk}@@{partition_offset}'
    for name in ('WINBOOT.SYS', 'COMMAND.COM', 'HIMEM.SYS', 'OAKCDROM.SYS', 'MSCDEX.EXE'):
        content = originals[name]
        destination = {'WINBOOT.SYS': '::IO.SYS', 'COMMAND.COM': '::COMMAND.COM'}.get(name, f'::/Neo98/{name}')
        _copy(volume, content, destination, scratch)
    _copy(volume, _dos('[Options]\nBootGUI=0\nLogo=0\nBootDelay=0\n'), '::MSDOS.SYS', scratch)
    _copy(volume, _dos(r'''
DEVICE=C:\NEO98\HIMEM.SYS /TESTMEM:OFF
DEVICE=C:\NEO98\OAKCDROM.SYS /D:OEMCD001
FILES=40
BUFFERS=20
LASTDRIVE=Z
SHELL=C:\COMMAND.COM C:\ /E:2048 /P
'''), '::CONFIG.SYS', scratch)
    _copy(volume, _dos(r'''
@ECHO OFF
CALL C:\NEO98\INSTALL.BAT
'''), '::AUTOEXEC.BAT', scratch)
    _copy(volume, _dos(r'''
@ECHO OFF
PATH=C:\;C:\NEO98;D:\WIN98
ECHO NEO98_MSDOS_BOOT>COM1
C:\NEO98\MSCDEX.EXE /D:OEMCD001 /L:D
IF ERRORLEVEL 1 GOTO CDFAIL
IF NOT EXIST D:\WIN98\SETUP.EXE GOTO CDFAIL
ECHO NEO98_CD_OK>COM1
REM Setup must own subsequent boots, including after its first restart.
DEL C:\AUTOEXEC.BAT
ECHO NEO98_SETUP_START>COM1
D:\WIN98\SETUP.EXE C:\NEO98\MSBATCH.INF /IS /IE /IM
ECHO NEO98_SETUP_RETURNED>COM1
GOTO END
:CDFAIL
ECHO NEO98_CD_FAIL>COM1
:END
'''), '::/Neo98/INSTALL.BAT', scratch)
    _copy(volume, answer, '::/Neo98/MSBATCH.INF', scratch)
