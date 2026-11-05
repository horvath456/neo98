#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The host as a Mac app: settings, machine folder, files opened from outside, the bundle.

Two runs under SDL's dummy video driver, both without a command line and with
an environment that holds nothing but HOME and the test switches (no PATH):

1. build/native-app/neo98-host reads settings.ini from NEO98_SETTINGS_DIR,
   takes drive C: and the registry journal from the machine folder named
   there and starts Explorer. Files are opened the way a double click in the
   Finder opens them (the open= step of NEO98_HOST_INPUT and File >
   Open program): a program on C:, a program and a document in a folder
   outside the drives (the folder becomes the next free drive), a DOS program,
   an ISO image (CD-ROM). Scale, network cable and D: changed in the menu are
   in settings.ini afterwards and hold over a restart. Quitting while a DOS
   program runs ends with exit code 0.
2. A copy of the bundle tools/mac_app.py builds runs inside a sandbox that
   denies reading this checkout, the main checkout, /opt/homebrew and
   /usr/local: Explorer starts, a DOS program runs in the bundled DOS machine,
   a report goes to the log folder, a restart re-executes the bundle.
3. The disk image of tools/mac_app.py holds the app and the link to
   /Applications.

The machine folder is a clone of the pristine Win98 folder, the settings and
logs live under build/mac-app; nothing outside the checkout is touched.

    build/analysis-venv/bin/python tools/test_mac_app_host.py [--no-build]

Results in build/mac-app/results.json. docs/BUILDING.md.
"""
from __future__ import annotations

import json
import plistlib
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
import mac_app  # noqa: E402
import win98_c  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402

EXTENSIONS = {'exe', 'com', 'bat', 'pif', 'lnk', 'scr', 'iso', 'cue'}


def clone_machine(machine):
    """A machine folder with a writable clone of the pristine drive C:."""
    pristine = win98_c.use_pristine()
    shutil.rmtree(machine, ignore_errors=True)
    machine.mkdir(parents=True)
    subprocess.run(['cp', '-c', '-R', str(pristine) + '/', str(machine / 'C')], check=True)
    (machine / 'C' / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    if win98_c.journal(pristine).is_file():
        subprocess.run(['cp', '-c', str(win98_c.journal(pristine)), str(machine / 'C.registry')], check=True)
    return machine


def registry_windows(data):
    """Three 64-byte samples, skipping padding and low-diversity windows.

    Start at each quarter and wrap once. A section without a sample with
    at least 16 distinct byte values contributes no probe.
    """
    used = set()
    for quarter in (1, 2, 3):
        start = len(data) * quarter // 4
        for at in (*range(start, len(data) - 63, 64), *range(0, start, 64)):
            value = data[at:at + 64]
            if len(value) == 64 and len(set(value)) >= 16 and value not in used:
                used.add(value)
                yield at, value
                break


def installation_values(drive_c):
    """Byte strings that belong to the installation in this Win98 folder: the
    values ProductKey, ProductId and DigitalProductId of its SYSTEM.DAT, and
    pieces of the registry image tools/import_registry.py makes from its
    hives. Each as (label, bytes); the bytes never reach a log."""
    import import_registry
    hives = {name: import_registry.parse(win98_c.path(f'C:\\WINDOWS\\{name}', drive_c))
             for name in ('SYSTEM.DAT', 'USER.DAT')}
    wanted = []
    version = hives['SYSTEM.DAT']['tree'].get('software\\microsoft\\windows\\currentversion')
    for name, _kind, payload in (version[3] if version else []):
        if name.lower() in ('productkey', 'productid', 'digitalproductid') and len(payload.rstrip(b'\0')) >= 8:
            payload = payload.rstrip(b'\0')
            wanted.append((name, payload))
            if name.lower() != 'digitalproductid':
                wanted.append((name + ' (UTF-16)', payload.decode('cp1252').encode('utf-16-le')))
    keys, _skipped = import_registry.select(hives)
    image = import_registry.encode(keys)
    for blob in ('names', 'payloads'):
        data = image[blob]
        for at, value in registry_windows(data):
            wanted.append((f'registry image {blob} at byte {at}', value))
    return wanted


def files_holding(root, wanted):
    """[(label, file)] for every value found in a file below root."""
    found = []
    for path in sorted(p for p in root.rglob('*') if p.is_file() and not p.is_symlink()):
        data = path.read_bytes()
        found += [(label, str(path.relative_to(root))) for label, value in wanted if value in data]
    return found


def run_host(command, environment, log_path, timeout=1500):
    with log_path.open('w') as log:
        result = subprocess.run(command, cwd='/', env=environment, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=timeout)
    return result.returncode, log_path.read_text(errors='replace')


def main():
    if '--privacy-only' in sys.argv:
        wanted = installation_values(win98_c.use_pristine())
        assert any(label.startswith('registry image') for label, _ in wanted)
        found = files_holding(ROOT / 'build/Neo98.app', wanted)
        assert (ROOT / 'build/Neo98.app/Contents/MacOS/Neo98').is_file()
        assert not found, found
        print(f'MAC APP PRIVACY PASS: {len(wanted)} nontrivial probes')
        return 0
    out = neo98.output_directory('mac-app')
    for name in ('settings', 'logs', 'outside', 'drive-d', 'iso-source', 'copy', 'tmp', 'Neo98.app', 'dmg-mount'):
        shutil.rmtree(out / name, ignore_errors=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    machine = clone_machine(out / 'machine')
    settings_dir, logs = out / 'settings', out / 'logs'
    settings_dir.mkdir()
    (out / 'tmp').mkdir()
    settings = settings_dir / 'settings.ini'
    settings.write_text(f'# Test\nmachine = {machine}\nscale = 1\n')
    outside = out / 'outside' / 'My Folder'
    outside.mkdir(parents=True)
    shutil.copyfile(win98_c.path('C:\\WINDOWS\\CALC.EXE', machine / 'C'), outside / 'CALC.EXE')
    (outside / 'Lies mich.txt').write_text('Hallo\r\n')
    edit = win98_c.path('C:\\WINDOWS\\COMMAND\\EDIT.COM', machine / 'C')
    notepad = win98_c.path('C:\\WINDOWS\\NOTEPAD.EXE', machine / 'C')
    source = out / 'iso-source' / 'NEO98APP'
    source.mkdir(parents=True)
    (source / 'README.TXT').write_text('NEO98 app test\r\n')
    iso = out / 'neo98app.iso'
    iso.unlink(missing_ok=True)
    subprocess.run(['hdiutil', 'makehybrid', '-quiet', '-iso', '-joliet', '-default-volume-name', 'NEO98APP',
                    '-o', str(iso), str(source)], check=True)
    drive_d = out / 'drive-d'
    drive_d.mkdir()
    (drive_d / 'DATEN.TXT').write_text('D:\r\n')
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)

    base = {'HOME': str(Path.home()), 'SDL_VIDEODRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy',
            'NEO98_SETTINGS_DIR': str(settings_dir), 'NEO98_LOG_DIR': str(logs), 'TMPDIR': str(out / 'tmp'),
            'NEO98_HOST_INPUT_ONCE': '1', 'NEO98_HOST_SCREEN': '2560x1600'}

    # --- 1. The host without a command line -------------------------------
    steps = [
        '0:wait=Shell_TrayWnd', '1:idle=1500', '1:menu=describe',
        f'1:open={notepad}', '0:wait=Notepad',
        f'1:open={outside / "CALC.EXE"}', '0:wait=SciCalc',
        f'1:open={outside / "Lies mich.txt"}', '0:wait=|Lies mich - Notepad',
        f'1:menu=open-program={edit}', '0:wait=|EDIT',
        f'1:open={iso}', '2:idle=1000',
        '1:menu=scale-2', '1:menu=network-online', f'1:menu=drive-d={drive_d}', '2:menu=report',
        '1:menu=reboot',
        '0:wait=Shell_TrayWnd', '1:idle=1500', '1:menu=describe',
        # Quit while a DOS program runs.
        f'1:open={edit}', '0:wait=|EDIT', '1:idle=1000', '1:menu=quit',
    ]
    code, log = run_host([str(binary)], dict(base, NEO98_HOST_INPUT=';'.join(steps)), out / 'host.log')
    starts = re.findall(r'^NEO98 HOST app settings=(\S+) machine=(\S+)', log, re.M)
    check('reads settings.ini of NEO98_SETTINGS_DIR, twice with the restart',
          starts == [(str(settings), str(machine))] * 2)
    check('drive C: is <machine>/C', log.count(f'NEO98 HOST drive C: {machine / "C"}\n') == 2)
    check('registry journal beside it', f'NEO98 HOST registry {machine / "C.registry"}' in log)
    check('registry starts from the hives of the machine folder',
          len(re.findall(r'^NEO98 HOST registry starts from hives C:\\WINDOWS\\SYSTEM.DAT C:\\WINDOWS\\USER.DAT '
                         r'keys=\d{5} values=\d{5} lan=1 error=0$', log, re.M)) == 2)
    check('host activates itself at the start and after the restart',
          log.count('NEO98 HOST activation (start): no screen') == 2)
    check('print jobs go into the machine folder', f'NEO98 HOST printer LPT1 -> {machine / "Print Output"}' in log)
    waits = re.findall(r'^NEO98 HOST input wait=(.*) (reached|timed out)', log, re.M)
    check('every wait was reached', waits and all(state == 'reached' for _, state in waits) and len(waits) == 7)
    items = [line for line in log.splitlines() if line.startswith('NEO98 HOST menu-item ')]
    check('menu has Settings', any('Neo98 > Settings … [settings] cmd+,' in line for line in items))
    check('menu has Open program', any('File > Open program … [open-program] cmd+o' in line for line in items))
    opened = re.findall(r'^HOST OPEN (.*) started error=0$', log, re.M)
    check('program on C: started by its guest path', 'C:\\WINDOWS\\NOTEPAD.EXE' in opened)
    mounted = re.search(r'^NEO98 HOST medium ([D-Z]): fixed ' + re.escape(str(outside)) + '$', log, re.M)
    letter = mounted[1] if mounted else '?'
    check('folder outside the drives mounted under a free letter', mounted)
    check('program outside started from that drive', f'{letter}:\\CALC.EXE' in opened)
    check('document opened through its association',
          f'{letter}:\\Lies mich.txt' in opened and f'NOTEPAD.EXE {letter}:\\Lies mich.txt' in log)
    check('started through START.EXE in the shell process',
          f'command="C:\\WINDOWS\\COMMAND\\START.EXE "{letter}:\\CALC.EXE""' in log)
    check('DOS program from Open program', opened.count('C:\\WINDOWS\\COMMAND\\EDIT.COM') == 2)
    check('ISO goes into the CD-ROM drive', f'NEO98 HOST open {iso} -> CD-ROM' in log
          and re.search(r'^NEO98 HOST medium [D-Z]: cdrom ', log, re.M))
    stored = dict(line.split(' = ', 1) for line in settings.read_text().splitlines() if ' = ' in line)
    check('settings.ini keeps scale, cable, CD-ROM and D:',
          stored.get('scale') == '2' and stored.get('online') == '0' and stored.get('cdrom') == str(iso)
          and stored.get('drive.D') == str(drive_d) and stored.get('machine') == str(machine))
    after = log.split('NEO98 HOST reboot report=', 1)[-1]
    check('restart keeps D:, the disc, the scale and the pulled cable',
          f'NEO98 HOST drive D: fixed {drive_d}' in after and re.search(r'NEO98 HOST drive [E-Z]: cdrom ', after)
          and 'Scale 2x [scale-2] cmd+2 (enabled)' in after and 'disconnected' in after)
    reports = re.findall(r'^NEO98 HOST report (.*)$', log, re.M)
    check('reports go to the log folder', len(reports) == 1 and reports[0].startswith(str(logs / 'crash-reports'))
          and Path(reports[0], 'report.json').exists())
    check('quit with a running DOS program ends with exit code 0', code == 0)
    check('no host failure', 'NEO98 HOST failed' not in log and 'libc++abi' not in log)

    # --- 2. The bundle ------------------------------------------------------
    app = mac_app.build_app(build=False, target=out / 'Neo98.app')
    program = app / 'Contents/MacOS/Neo98'
    info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
    declared = {ext for kind in info['CFBundleDocumentTypes'] for ext in kind['CFBundleTypeExtensions']}
    check('Info.plist names the eight extensions', declared == EXTENSIONS)
    check('exe opener, iso viewer',
          any('exe' in k['CFBundleTypeExtensions'] and k['CFBundleTypeRole'] == 'Shell' for k in info['CFBundleDocumentTypes'])
          and any('iso' in k['CFBundleTypeExtensions'] and k['CFBundleTypeRole'] == 'Viewer'
                  for k in info['CFBundleDocumentTypes']))
    binaries = [program, *sorted((app / 'Contents/Frameworks').iterdir())]
    foreign = [f'{b.name}: {name}' for b in binaries for name in mac_app.dependencies(b)
               if not name.startswith(mac_app.SYSTEM_PREFIXES) and not name.startswith('@rpath/')]
    check('no library outside the bundle and the system', not foreign)
    check('rpath is the Frameworks folder only', mac_app.rpaths(program) == ['@executable_path/../Frameworks'])
    names = {b.name for b in binaries}
    check('SDL2, SDL3, Unicorn and the DOS machine are bundled',
          {'libdosbox-pure.dylib', 'libunicorn.2.dylib'} <= names and any(n.startswith('libSDL2') for n in names)
          and (not any(n.startswith('libSDL2') and b'libSDL3.dylib' in (app / 'Contents/Frameworks' / n).read_bytes()
                       for n in names) or 'libSDL3.dylib' in names))
    resources = app / 'Contents/Resources'
    check('icon and setup files in Resources, no font of Win98',
          not (resources / 'notepad-fixedsys.fnt').exists() and (resources / 'Neo98.icns').stat().st_size > 10000
          and all((resources / 'setup/tools' / name).is_file() for name in mac_app.SETUP_FILES))
    check('ad hoc signature verifies',
          subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], capture_output=True).returncode == 0)
    # ARM64 programs need the machine of Hypervisor.framework (docs/ARM64-APPS.md).
    signed = subprocess.run(['codesign', '-d', '--entitlements', '-', str(app)], capture_output=True, text=True)
    check('the signature carries the hypervisor entitlement',
          'com.apple.security.hypervisor' in signed.stdout + signed.stderr)
    # Nothing of the installation is in the bundle: not the product
    # key, not the product id, not the registry image made from its hives.
    wanted = installation_values(machine / 'C')
    check('the folder has a product key and a product id to look for',
          {'ProductKey', 'ProductId'} <= {label for label, _ in wanted})
    held = files_holding(app, wanted)
    for label, name in held:
        print(f'bundle file {name} holds {label} of the Win98 folder', flush=True)
    check('bundle holds no ProductKey, ProductId or registry image of the installation', not held)
    check('the search finds such a value where it is',
          files_holding(machine / 'C' / 'WINDOWS', [w for w in wanted if w[0] == 'ProductId'])
          or any(value in (machine / 'C.registry').read_bytes() for label, value in wanted if label == 'ProductId'))
    copy = out / 'copy' / 'Neo98.app'
    copy.parent.mkdir()
    subprocess.run(['cp', '-R', str(app), str(copy)], check=True)
    main_checkout = (ROOT / 'build/analysis-venv').resolve().parents[1]
    denied = sorted({str(ROOT), str(main_checkout), '/opt/homebrew', '/usr/local'})
    profile = out / 'bundle.sb'
    profile.write_text('(version 1)\n(allow default)\n'
                       + ''.join(f'(deny file-read-data (subpath "{path}"))\n' for path in denied)
                       + f'(allow file-read-data (subpath "{out}"))\n')
    sandbox = ['/usr/bin/sandbox-exec', '-f', str(profile)]
    probe = subprocess.run([*sandbox, '/bin/cat', str(ROOT / 'neo98.py')], capture_output=True)
    check('the sandbox denies the checkout', probe.returncode != 0)
    settings.write_text(f'machine = {machine}\nscale = 1\n')
    steps = ['0:wait=Shell_TrayWnd', '1:idle=1500', f'1:open={edit}', '0:wait=|EDIT', '1:idle=1000',
             '1:menu=report', '1:menu=reboot', '0:wait=Shell_TrayWnd', '1:idle=1000', '1:menu=quit']
    code, log = run_host([*sandbox, str(copy / 'Contents/MacOS/Neo98')],
                         dict(base, NEO98_HOST_INPUT=';'.join(steps)), out / 'bundle.log')
    waits = re.findall(r'^NEO98 HOST input wait=(.*) (reached|timed out)', log, re.M)
    check('bundle: Explorer and the DOS program appear', len(waits) == 3 and all(s == 'reached' for _, s in waits))
    check('bundle: DOS machine loaded from Frameworks', 'the DOS machine' not in log
          and 'HOST OPEN C:\\WINDOWS\\COMMAND\\EDIT.COM started' in log)
    reports = re.findall(r'^NEO98 HOST report (.*)$', log, re.M)
    check('bundle: report in the log folder', len(reports) == 1 and Path(reports[0], 'report.json').exists())
    check('bundle: restart re-executes the program', log.count('NEO98 HOST app settings=') == 2
          and log.count('NEO98 HOST running') == 2)
    check('bundle: registry from the hives of the machine folder',
          log.count('NEO98 HOST registry starts from hives ') == 2)
    check('bundle: system font from the font file of the machine folder',
          log.count('NEO98 HOST system font C:\\WINDOWS\\FONTS\\VGAFIX.FON read from the drive') == 2)
    check('bundle: exit code 0', code == 0)
    # --- 3. The disk image ----------------------------------------------------
    dmg = mac_app.build_dmg(app=app, target=out / 'Neo98.dmg')
    mount = out / 'dmg-mount'
    mount.mkdir(exist_ok=True)
    attached = subprocess.run(['hdiutil', 'attach', '-readonly', '-nobrowse', '-noverify', '-mountpoint', str(mount),
                               str(dmg)], capture_output=True).returncode == 0
    try:
        check('disk image holds the app and the link to /Applications',
              attached and (mount / 'Neo98.app/Contents/MacOS/Neo98').is_file()
              and (mount / 'Applications').is_symlink() and str((mount / 'Applications').readlink()) == '/Applications')
        check('app in the disk image keeps its signature', attached and subprocess.run(
            ['codesign', '--verify', '--deep', '--strict', str(mount / 'Neo98.app')], capture_output=True).returncode == 0)
    finally:
        if attached:
            subprocess.run(['hdiutil', 'detach', '-force', str(mount)], capture_output=True)
    dmg.unlink(missing_ok=True)
    # The reports hold a screenshot and the log each.
    shutil.rmtree(logs / 'crash-reports', ignore_errors=True)
    # Started bundles register with macOS. Test bundles must not create
    # duplicate entries in Spotlight, the Dock or Open With.
    lsregister = ('/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/'
                  'Support/lsregister')
    for bundle in (copy, app):
        subprocess.run([lsregister, '-u', str(bundle)], capture_output=True)
    shutil.rmtree(out / 'copy', ignore_errors=True)
    shutil.rmtree(app, ignore_errors=True)

    report = {'checks': checks, 'log': str(out / 'host.log'), 'bundle_log': str(out / 'bundle.log'),
              'settings': stored}
    (out / 'results.json').write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
    failed = [name for name, okay in checks.items() if not okay]
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if failed:
        print(f'MAC APP FAIL: {len(failed)} of {len(checks)} checks; see {out / "host.log"} and {out / "bundle.log"}')
        return 1
    print(f'MAC APP PASS: {len(checks)} checks; {out / "results.json"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
