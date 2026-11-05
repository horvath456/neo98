#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""What the machine calls itself outside the Device Manager.

Win98 was installed once on another machine (QEMU: a Cirrus Logic display
adapter, SeaBIOS, an AMD PCnet card). Nothing a person reads may name it.
tools/test_device_manager_host.py checks the windows around the devices;
this test checks the rest, over a clone of setup's C: without a window:

1. SYSTEM.INI as a file: after the first start [boot.description] names the
   display adapter of the port; a second start over the same folder changes
   nothing (the host says when it wrote). WIN.INI, MSDOS.SYS, CONFIG.SYS and
   AUTOEXEC.BAT name nothing of the installation machine.
2. System Properties, General and Performance: the processor line and the
   memory. The memory is the figure GlobalMemoryStatus gives, in whole
   megabytes, on both pages (GetFreeSpace(GFS_PHYSICALRAMSIZE)).
3. The registry as REGEDIT exports it, every key and value: the installation
   machine is named only by the records of installed files and INF files
   (the files are there) and the INF section names of the network card's
   model; none of them is shown anywhere.
4. A DOS program (tools/bios_names_dos_fixture.asm): the identification and
   version of the BIOS in the ROM and the vendor, product and revision of
   VBE name the port, and "IBM COMPATIBLE" stays.

    build/analysis-venv/bin/python tools/test_machine_names_host.py [--no-build]
"""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import hardware_setup  # noqa: E402
import win98_c  # noqa: E402
from test_device_manager_host import STALE, failures, host, said  # noqa: E402

RUNDLL = 'C:\\WINDOWS\\RUNDLL32.EXE'
GENERAL = 'RUNDLL32.EXE shell32.dll,Control_RunDLL sysdm.cpl,,0'
WROTE = 'NEO98 HOST SYSTEM.INI names the display adapter of the port'
# Where the registry may name the installation machine: records of files and
# INF files that are in the folder, and the INF sections of the card's model.
RECORDS = ('\\Setup\\SetupX\\INF\\OEM Name]', '\\Setup\\SetupX\\Catalogs]', '\\Setup\\SetupX\\Cert\\',
           '\\Control\\InstalledFiles]', '\\Services\\Class\\Net\\0001]', '\\Services\\Class\\Net\\0001\\Ndi\\Install]')


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('machine-names-host')
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    hardware = hardware_setup.names()
    journal = win98_c.journal(pristine)
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)
        print(('ok  ' if condition else 'FAIL'), name, flush=True)

    # 1 and 2: the first start, with System Properties.
    steps = ['0:wait=|System Properties', '4:tops=2', '6:click=346,96', '8:tops=2']
    code, text, first = host(binary, out, 'first', RUNDLL, GENERAL, steps, 14, pristine, journal, keep=True)
    folder = first / 'c'
    ini = (win98_c.path('C:\\WINDOWS\\SYSTEM.INI', folder) or Path('/nonexistent'))
    raw = ini.read_bytes() if ini.is_file() else b''
    before = (win98_c.path('C:\\WINDOWS\\SYSTEM.INI', pristine)).read_bytes()
    check('setup\'s SYSTEM.INI names the display adapter of the installation machine (the premise)',
          STALE.search(before.decode('cp1252')) is not None)
    check('after the first start the file names the display adapter of the port',
          f'display.drv={hardware["DisplayName"]}\r\n'.encode() in raw and WROTE in text)
    check('nothing else of the file changed',
          re.sub(rb'(?m)^display\.drv=(?!pnpdrvr).*\r\n', b'', raw) == re.sub(rb'(?m)^display\.drv=(?!pnpdrvr).*\r\n', b'', before))
    stale = []
    for name in ('C:\\WINDOWS\\SYSTEM.INI', 'C:\\WINDOWS\\WIN.INI', 'C:\\MSDOS.SYS', 'C:\\CONFIG.SYS', 'C:\\AUTOEXEC.BAT'):
        file = win98_c.path(name, folder)
        if file and STALE.search(file.read_bytes().decode('cp1252', errors='replace')):
            stale.append(name)
    check('SYSTEM.INI, WIN.INI, MSDOS.SYS, CONFIG.SYS and AUTOEXEC.BAT name nothing of the installation machine', not stale)
    lines = said(text)
    memory = [line for line in lines if re.search(r'MB( of)? RAM$', line)]
    check('System Properties shows the processor as the CPU reports itself',
          any(re.match(r'x86 Family \d+ Model \d+ Stepping \d+$', line) for line in lines))
    check('General and Performance show the same memory, 1024.0 MB, as GlobalMemoryStatus has it',
          [re.sub(r'\s*MB( of)? RAM$', '', line) for line in memory] == ['1024.0', '1024.0'])
    check('System Properties names nothing of the installation machine', not [line for line in lines if STALE.search(line)])
    check('no call the host does not carry (System Properties)', not failures(text))

    # The second start over the folder the first one left: nothing is written.
    code, text, second = host(binary, out, 'second', RUNDLL, GENERAL, ['0:wait=|System Properties', '3:tops=1'], 10,
                              folder, first / 'c.registry', keep=True)
    again = win98_c.path('C:\\WINDOWS\\SYSTEM.INI', second / 'c')
    check('a second start writes nothing', WROTE not in text and again and again.read_bytes() == raw)
    shutil.rmtree(first / 'c', ignore_errors=True)
    shutil.rmtree(second / 'c', ignore_errors=True)

    # 3: the registry as a whole.
    code, text, run = host(binary, out, 'registry', 'C:\\WINDOWS\\REGEDIT.EXE', 'REGEDIT.EXE /E C:\\ALL.REG',
                           ['45:tops=1'], 60, pristine, journal, keep=True)
    export = win98_c.path('C:\\ALL.REG', run / 'c')
    exported = export.read_bytes().decode('cp1252', errors='replace') if export else ''
    shutil.rmtree(run / 'c', ignore_errors=True)
    key, found, keys = '', [], 0
    for line in exported.splitlines():
        if line.startswith('['):
            key, keys = line, keys + 1
        if STALE.search(line) and not any(record in key for record in RECORDS):
            found.append((key, line[:100]))
    check(f'REGEDIT exported the registry ({keys} keys)', keys > 10000)
    check('no key or value outside the records of installed files names the installation machine', keys and not found)
    for key, line in found[:8]:
        print('    ', key, '|', line)

    # 4: the DOS machine.
    program = out / 'BIOSID.COM'
    subprocess.run(['nasm', '-f', 'bin', str(ROOT / 'tools/bios_names_dos_fixture.asm'), '-o', str(program)],
                   env=neo98.tool_environment(), check=True)
    code, text, run = host(binary, out, 'dos', 'C:\\BIOSID.COM', 'BIOSID.COM', ['15:tops=1'], 25, pristine, journal,
                           prepare=lambda run: shutil.copy(program, run / 'c/BIOSID.COM'), keep=True)
    written = win98_c.path('C:\\BIOSID.TXT', run / 'c')
    names = written.read_bytes().decode('cp437').split('\r\n') if written else []
    shutil.rmtree(run / 'c', ignore_errors=True)
    check('the DOS program read the BIOS and VBE names', len(names) >= 6)
    check('the BIOS of the DOS machine is an IBM compatible one of the port',
          names[:2] == [f'IBM COMPATIBLE 486 BIOS COPYRIGHT {hardware["Maker"]}.', f'{hardware["Maker"]} BIOS v1.0'])
    check('VBE names the port as vendor and its display adapter as product',
          names[3:5] == [hardware['Maker'], hardware['DisplayName']])
    check('no name of the DOS machine says what it is made of',
          names and not [name for name in names if re.search('dosbox|qemu|bochs', name, re.I)])
    print(f'machine names host: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
