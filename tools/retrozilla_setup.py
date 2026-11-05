#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""RetroZilla 2.3 in the guest, installed by its own installer.

HTTPS for the Win98 programs of this port goes through a browser that brings
a TLS of its own, not through a proxy. RetroZilla is
a Mozilla Suite for Windows 9x with an updated NSS. This tool

1. downloads retrozilla-2.3-win32-installer.exe from the project's official
   GitHub release into build/downloads/retrozilla-2.3/ (not into the
   repository) and checks its SHA-256,
2. runs that installer in the guest from a read-only drive D: onto a writable
   drive C: with a registry file, and clicks through its pages the way a
   user does (Next, Accept, Next, Next, Install),
3. keeps the result in build/retrozilla/installed/ (drive-c/ and
   registry.dat) with a stamp, so later runs copy it instead of installing
   again.

    build/analysis-venv/bin/python tools/retrozilla_setup.py [--force]

Other scripts import `installed` and `copy_to(directory)`.
"""
import hashlib
import json
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))

RELEASE = 'https://github.com/rn10950/RetroZilla/releases/download/2.3-release/retrozilla-2.3-win32-installer.exe'
SHA256 = '8d8644d28e607ca1552c2f726b7e18cb455152c1148f774437b37ff940b104f9'
DOWNLOADS = ROOT / 'build/downloads/retrozilla-2.3'
INSTALLER = DOWNLOADS / 'retrozilla-2.3-win32-installer.exe'
BASE = ROOT / 'build/retrozilla/installed'
EXE = 'Program Files/RetroZilla/RetroZilla.exe'
# The installer's pages at their default place: Next and Accept and Install
# share the button at 485,456.
CLICKS = 'wait 20000\n' + 'point 485 456\nwait 4000\n' * 5 + 'wait 300000\n'


def digest(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def download():
    if INSTALLER.is_file() and digest(INSTALLER) == SHA256:
        return INSTALLER
    DOWNLOADS.mkdir(parents=True, exist_ok=True)
    partial = INSTALLER.with_suffix('.part')
    with urllib.request.urlopen(RELEASE, timeout=120) as response, open(partial, 'wb') as out:
        shutil.copyfileobj(response, out)
    if digest(partial) != SHA256:
        partial.unlink()
        raise SystemExit(f'{RELEASE}: unexpected SHA-256')
    partial.rename(INSTALLER)
    return INSTALLER


def installed():
    stamp = BASE / 'stamp.json'
    if not stamp.is_file() or not (BASE / 'drive-c' / EXE).is_file():
        return False
    return json.loads(stamp.read_text()).get('sha256') == SHA256


def install(runner=None):
    import neo98
    from native_app import build_fast
    installer = download()
    runner = runner or build_fast('probe_application.cpp', 'probe-smoke')
    shutil.rmtree(BASE, ignore_errors=True)
    (BASE / 'drive-c').mkdir(parents=True)
    script = BASE / 'click.txt'
    script.write_text(CLICKS)
    result = subprocess.run([str(runner), str(installer), str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                             '--system-tree', '--exe', 'D:\\' + installer.name,
                             '--drive-c', str(BASE / 'drive-c'), '--registry', str(BASE / 'registry.dat'),
                             '--drive-readonly', f'D={DOWNLOADS}', '--host-clock', '--no-idle-stop',
                             '--time-limit', '360', '--click-script', str(script)],
                            env=neo98.tool_environment(), capture_output=True, text=True, timeout=3600)
    (BASE / 'install.log').write_text(result.stdout + result.stderr)
    if not (BASE / 'drive-c' / EXE).is_file():
        raise SystemExit(f'RetroZilla did not install; see {BASE / "install.log"}')
    (BASE / 'stamp.json').write_text(json.dumps({'sha256': SHA256, 'release': RELEASE}) + '\n')
    return BASE


def copy_to(directory, runner=None):
    """A fresh copy of the installed state: directory/drive-c and directory/registry.dat."""
    if not installed():
        install(runner)
    directory = Path(directory)
    shutil.rmtree(directory, ignore_errors=True)
    directory.mkdir(parents=True)
    shutil.copytree(BASE / 'drive-c', directory / 'drive-c', symlinks=True)
    shutil.copy2(BASE / 'registry.dat', directory / 'registry.dat')
    return directory


def main():
    if '--force' in sys.argv or not installed():
        install()
    print(f'RETROZILLA installed in {BASE}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
