#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Copies of the Win98 folder with the DirectSound of another DirectX.

The native DSOUND.VXD (src/x86_dsound_vxd.cpp) answers every DSOUND.DLL of its
interface. The Win98 folder carries 4.06 (DirectX 6.1) and the DirectX 9 copy
of tools/dx9_tree.py carries 4.09; the DirectSound tests measure the versions
in between on a copy that has only the three DirectSound files replaced:

    dx7 4.07.00.0700 out of dx70eng.exe, on the folder of setup
    dx80a 4.08.00.0400 out of DX80A.EXE of the program collection, on the
            DirectX 9 copy
    dx81 4.08.01.0881 out of the DirectX 8.1 package of October 2001, on
            the DirectX 9 copy

The packages of DirectX 7.0 and 8.1 are the original Microsoft files as
archive.org mirrors them. They are fetched once into build/directx-packages
and checked against the pinned size and SHA-256; nothing of them is in the
repository and nothing goes into the Win98 folder. Without network, package
or cabextract the answer is None and the caller skips.

    build/analysis-venv/bin/python tools/dx_sound_tree.py dx7
"""
from pathlib import Path
import hashlib
import shutil
import subprocess
import sys
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parent))
import win98_c      # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
PACKAGES = ROOT / 'build/directx-packages'
FILES = ('dsound.dll', 'dsound3d.dll', 'dsound.vxd')
from external_paths import external_path
VERSIONS = {
    'dx7': {
        'file': 'dx70eng.exe', 'base': 'setup', 'dsound': '4.07.00.0700',
        'url': 'https://archive.org/download/dx70eng_202501/dx70eng.exe',
        'size': 6311904, 'sha256': '7c2e73a4eb06a61c61c7fa141b9d34838feaa7f155fa62f5793218fef76f0742',
    },
    'dx80a': {
        'file': 'DX80A.EXE', 'base': 'dx9', 'dsound': '4.08.00.0400',
        'local': 'GAMES/KIDS50/System/DX80A.EXE',
    },
    'dx81': {
        'file': 'Microsoft_DirectX-8.1_2001Oct.exe', 'base': 'dx9', 'dsound': '4.08.01.0881',
        'url': 'https://archive.org/download/MS_DirectX/Microsoft_DirectX-8.1_2001Oct.exe',
        'size': 12144784, 'sha256': 'd1762981f497a623b2f3e725c0916b96e716bd4a8845c5a4cc7f764b1a5cc5f7',
    },
}


def _sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as data:
        for chunk in iter(lambda: data.read(1 << 20), b''):
            digest.update(chunk)
    return digest.hexdigest()


def package(name):
    """The package file of one version, fetched and verified, or None."""
    pin = VERSIONS[name]
    if 'local' in pin:
        path = external_path('NEO98_COLLECTION') / pin['local']
        return path if path.is_file() else None
    path = PACKAGES / pin['file']
    if path.is_file() and path.stat().st_size == pin['size'] and _sha256(path) == pin['sha256']:
        return path
    PACKAGES.mkdir(parents=True, exist_ok=True)
    try:
        with urllib.request.urlopen(pin['url'], timeout=300) as response:
            data = response.read()
    except OSError as error:
        print(f'{pin["file"]}: {error}', flush=True)
        return None
    if len(data) != pin['size'] or hashlib.sha256(data).hexdigest() != pin['sha256']:
        raise SystemExit(f'{pin["url"]} is not the pinned file ({len(data)} bytes)')
    path.write_bytes(data)
    return path


def tree(name):
    """The copy of the Win98 folder for one version, built on first use, or None with a note."""
    pin = VERSIONS[name]
    target = ROOT / 'build/dx-sound-tree' / name / 'win98-c'
    marker = target / '.neo98-dx-sound'
    if marker.is_file() and marker.read_text().strip() == pin['dsound']:
        return target
    if not shutil.which('cabextract'):
        print('DirectSound tree skipped: cabextract missing', flush=True)
        return None
    source = package(name)
    if source is None:
        print(f'DirectSound tree {name} skipped: no package', flush=True)
        return None
    if pin['base'] == 'dx9':
        import dx9_tree
        base = dx9_tree.tree()
    else:
        base = win98_c.pristine()
    if base is None:
        return None
    if target.exists():
        subprocess.run(['chmod', '-R', 'u+w', str(target)], check=True)
        shutil.rmtree(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['cp', '-c', '-R', str(Path(base).resolve()), str(target)], check=True)
    scratch = target.parent / 'files'
    shutil.rmtree(scratch, ignore_errors=True)
    subprocess.run(['cabextract', '-q', '-d', str(scratch), '-F', '*sound*', str(source)], check=True)
    for file_name in FILES:
        destination = win98_c.path(f'WINDOWS/SYSTEM/{file_name}', target)
        destination.chmod(0o644)
        shutil.copyfile(scratch / file_name, destination)
    marker.write_text(pin['dsound'] + '\n')
    print(f'DirectSound tree {target}: DSOUND.DLL {pin["dsound"]}', flush=True)
    return target


def select(arguments):
    """The tree a test asked for with --dx7, --dx8 or --dx81: (wanted, path)."""
    for flag, name in (('--dx7', 'dx7'), ('--dx8', 'dx80a'), ('--dx81', 'dx81')):
        if flag in arguments:
            return True, tree(name)
    return False, None


if __name__ == '__main__':
    sys.exit(0 if all(tree(name) for name in (sys.argv[1:] or VERSIONS)) else 1)
