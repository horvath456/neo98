#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""OK in the Network applet commits without asking for the installation disk.

The COMCTL32 of Windows 95 and 98 answers 1 for every OK of a property sheet
whose pages all accept PSN_APPLY, whether a page reported a change or not;
NETDI then commits the configuration, and the commit installs NET.INF's
NET_ANYNET.add again (Windows 98 more of NET.INF), so SETUPX copies files
from the source path, here the cabinets on the drive. The SETUPX of Windows
95 keeps the names of its copy queue as local atoms of its own data
segment; while the port kept those atoms outside the segment every name
SETUPX read was empty, no file was found in a cabinet and SETUPX asked for
the CD (docs/NETWORK-APPLET.md, docs/WIN16-ATOMS.md). The SETUPX of Windows
98 uses no atoms.

A changed computer description makes the Identification page set
DI_NEEDRESTART on the first component (NETDI's PSN_APPLY), and after the
commit NETDI asks to restart (RestartDialog, EW_RESTARTWINDOWS). An OK
without a change asks nothing.

"Primary Network Logon" of the page Configuration shows the client whose
class key has Ndi\\Default, "Windows Logon" without one, and OK stores that
choice in HKLM\\Network\\Logon (PrimaryProvider, LMLogon). The drive's mark
and its PrimaryProvider name the same client; the port took the mark away on
every drive and so turned a Windows 95 with Client for Microsoft Networks
into Windows Logon at the first OK (docs/NETWORK-APPLET.md).

neo98-kms over one private copy of drive C:, RUNDLL32 with NETCPL.CPL, and
tools/network_logon_fixture.c, which reads the registry and the box:

  at the first start the box shows the client PrimaryProvider names, or
  Windows Logon for an empty one
  the description changed and OK: no disk question of SETUPX, a file copied
  from the cabinets, the description stored, the restart question
  OK without a change: no disk question, no restart question, the applet
  ends
  after both: the box shows the same, PrimaryProvider and LMLogon are as
  they were (an LMLogon the drive did not have is written as zero)

    build/analysis-venv/bin/python tools/test_network_identification_host.py [--no-build] [--keep] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
if __name__ == '__main__':
    import shutil as _shutil
    if not _shutil.which('i686-w64-mingw32-gcc'):  # the fixture is built with it; docs/BUILDING.md
        print('SKIP: needs i686-w64-mingw32-gcc')
        _sys.exit(77)
import argparse
import os
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/network-identification'
COMMAND = 'RUNDLL32.EXE SHELL32.DLL,Control_RunDLL NETCPL.CPL'
# The Identification tab and the end of the description field of the sheet
# at its place on an 800x600 desktop.
TAB, FIELD = '185,95', '300,296'
RESTART = 'System Settings Change'


def journal_values(path):
    """The values the registry journal of a drive sets, (key, name) to the
    last data, from record kinds 3 (set value) on."""
    data = path.read_bytes() if path.is_file() else b''
    values, at = {}, len(b'NEO98REG1')

    def counted(at):
        size = struct.unpack_from('<H', data, at)[0]
        return data[at + 2:at + 2 + size].decode('latin-1'), at + 2 + size
    while at < len(data):
        kind = data[at]
        key, at = counted(at + 1)
        if kind in (3, 4):
            name, at = counted(at)
            if kind == 3:
                _, size = struct.unpack_from('<II', data, at)
                values[(key.lower(), name.lower())] = data[at + 8:at + 8 + size]
                at += 8 + size
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--keep', action='store_true', help='keep the copy of the drive')
    parser.add_argument('--kms', type=Path, default=BINARY)
    args = parser.parse_args()
    if not native_app.native_x64():
        raise SystemExit('neo98-kms is a Linux x64 host.')
    if not args.no_build:
        native_app.build_kms_host(vulkan=False)
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    drive = WORK / 'c'
    pristine = win98_c.use_pristine()
    subprocess.run(['cp', '-a', '--reflink=auto', str(pristine), str(drive)], check=True)
    if win98_c.journal(pristine).is_file():
        shutil.copy2(win98_c.journal(pristine), win98_c.journal(drive))
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/network_logon_fixture.c'), '-nostartfiles',
                    '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32', '-ladvapi32',
                    '-o', str(drive / 'NETLOGON.EXE')], check=True)
    logs = []

    def session(name, steps, exe='C:\\WINDOWS\\RUNDLL32.EXE', command=COMMAND):
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:800x600',
                   NEO98_HOST_INPUT=';'.join(steps))
        for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
            env.pop(variable, None)
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '800x600', '--verbose',
                                 '--exe', exe, '--command', command,
                                 '--time-limit', '60'], cwd=ROOT, env=env, capture_output=True, text=True,
                                errors='replace', timeout=150)
        text = result.stdout + result.stderr
        logs.append(f'---- {name}: {result.returncode}\n{text}')
        return result.returncode, text

    def logon(name):
        """What tools/network_logon_fixture.c read: the registry and the box."""
        written = win98_c.path('C:\\NETLOGON.TXT', drive)
        if written:
            written.unlink()
        session(name, (), 'C:\\NETLOGON.EXE', 'NETLOGON.EXE read')
        written = win98_c.path('C:\\NETLOGON.TXT', drive)
        return dict(line.split('=', 1) for line in (written.read_text().splitlines() if written else ()) if '=' in line)

    def system_files():
        folder = win98_c.path('C:\\WINDOWS', drive)
        return {p: p.stat().st_mtime for p in folder.rglob('*') if p.is_file()}

    try:
        first = logon('the logon at the first start')
        before = system_files()
        started = time.time()
        changed_code, changed = session('the description changed', [
            '0:wait=#32770|Network', '0:idle=2500', f'1:click={TAB}', f'2:click={FIELD}', '2.5:key=23',
            '3:type= Phone', '4:key=0d', f'4:wait=#32770|{RESTART}', '4:idle=500', '5:key=4e'])
        after = system_files()
        copied = sorted(p.name for p, mtime in after.items()
                        if mtime >= started - 2 and p.suffix.lower() in ('.vxd', '.dos', '.exe', '.sys', '.dll')
                        and before.get(p, 0) != mtime)
        values = journal_values(win98_c.journal(drive))
        comment = values.get(('hkey_local_machine\\system\\currentcontrolset\\services\\vxd\\vnetsup', 'comment'), b'')
        same_code, same = session('OK without a change', [
            '0:wait=#32770|Network', '0:idle=2500', '1:key=0d'])
        last = logon('the logon after both')
    finally:
        (WORK / 'log.txt').write_text('\n'.join(logs))
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
            Path(str(drive) + '.clock').unlink(missing_ok=True)
    disk = '__Win95SetupDiskQuery'
    checks = {
        'the description changed and OK: no disk question of SETUPX': disk not in changed,
        f'a file of the network copied from the cabinets: {", ".join(copied) or "none"}': bool(copied),
        f'the description stored: {comment.rstrip(bytes(1)).decode("latin-1")!r}':
            comment.rstrip(bytes(1)).endswith(b' Phone'),
        'the restart question (RestartDialog, EW_RESTARTWINDOWS)':
            f'MessageBox caption={RESTART}' in changed and 'SHELL.157 #157 arg=0042' in changed,
        'and the applet ends after No': changed_code == 0,
        'OK without a change: no disk question': disk not in same,
        'no restart question, the applet ends': f'MessageBox caption={RESTART}' not in same and same_code == 0,
        f'at the first start the box shows {first.get("logon")!r}, the client PrimaryProvider '
        f'{first.get("primary")!r} names: {first.get("expected")!r} (marked: {first.get("marked")!r})':
            bool(first.get('logon')) and first.get('logon') == first.get('expected'),
        f'after both the box shows {last.get("logon")!r}': bool(last.get('logon')) and
            last.get('logon') == first.get('logon'),
        f'PrimaryProvider and LMLogon as they were: {first.get("primary")!r} {first.get("lmlogon")!r}, after '
        f'{last.get("primary")!r} {last.get("lmlogon")!r}':
            'primary' in first and last.get('primary') == first.get('primary') and
            (last.get('lmlogon') == first.get('lmlogon') or
             # NETDI writes LMLogon itself; the port's network setup of
             # Windows 98 adds the client without it, and zero is what an
             # absent value means (no logon to a Windows NT domain).
             (first.get('lmlogon') == '(none)' and last.get('lmlogon', '').strip('0') == '' and
              bool(last.get('lmlogon')))),
    }
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'network identification host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
