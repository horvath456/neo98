#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the macOS setup assistant with dummy video. Check missing-tool diagnostics and invalid keys, selecting an existing installation, completing setup from a private installed-image copy, and canceling incomplete setup. Installation cases use local ISO/setup assets and skip when absent. Product keys must not appear in logs or files. Results: build/setup-assistant/results.json."""
from __future__ import annotations

import json
import hashlib
import os
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
import win98_cabs  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
from test_mac_app_host import clone_machine, run_host  # noqa: E402

KEY = 'QQQQQ-' 'WWWWW-' 'EEEEE-' 'RRRRR-' 'TTTTT'


def expected_checksums(pristine, iso):
    """Original installed files plus exact CD sources added by today's setup.

    Older pristine checksum lists predate the source-cabinet copy step.
    Compare those sources against the actual ISO, without changing the
    immutable reference or excluding any installed Windows files.
    """
    expected = dict(win98_c.checksums(pristine))
    with iso.open('rb') as image:
        for name, offset, size in win98_cabs.iso_files(iso):
            image.seek(offset)
            digest = hashlib.sha256()
            left = size
            while left:
                chunk = image.read(min(left, 1 << 20))
                if not chunk:
                    raise RuntimeError('Truncated test source ISO')
                digest.update(chunk)
                left -= len(chunk)
            expected['/'.join(win98_cabs.TARGET) + '/' + name] = digest.hexdigest()
    return expected


def main():
    out = neo98.output_directory('setup-assistant')
    for name in os.listdir(out):
        if name != 'results.json':
            path = out / name
            shutil.rmtree(path) if path.is_dir() and not path.is_symlink() else path.unlink()
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    pristine = win98_c.use_pristine()
    checks, notes = {}, []

    def check(name, condition):
        checks[name] = bool(condition)

    def environment(settings, **more):
        (out / 'tmp').mkdir(exist_ok=True)
        return dict({'HOME': str(Path.home()), 'SDL_VIDEODRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy',
                     'NEO98_SETTINGS_DIR': str(settings), 'NEO98_LOG_DIR': str(out / 'logs'),
                     'TMPDIR': str(out / 'tmp'), 'NEO98_HOST_INPUT_ONCE': '1'}, **more)

    def wizard(log):
        return [line[len('NEO98 WIZARD '):] for line in log.splitlines() if line.startswith('NEO98 WIZARD ')]

    # --- 1. Missing tools, malformed key ----------------------------------
    settings = out / 'settings-1'
    empty = out / 'no-tools'
    empty.mkdir()
    dummy_iso = out / 'not-an-iso.iso'
    dummy_iso.write_bytes(b'\0' * 4096)
    target = out / 'machine-1'
    steps = ['check', f'iso={dummy_iso}', f'folder={target}', f'key={KEY}', 'install', 'check', 'quit']
    code, log = run_host([str(binary)], environment(settings, NEO98_HOST_WIZARD=';'.join(steps),
                                                    NEO98_SETUP_TOOL_DIRS=str(empty)), out / 'tools.log', 120)
    lines = wizard(log)
    check('assistant appears without a machine folder', any(line.startswith('shown settings=') for line in lines))
    check('missing tools are named with the brew line',
          sum(line.startswith('tools missing: qemu-system-i386 mformat mcopy mdel mmd mdir cabextract tesseract; '
                              'brew install qemu mtools cabextract tesseract') for line in lines) >= 3)
    check('Installieren refused without the tools', 'install refused: tools' in lines)
    check('nothing created, nothing started',
          not target.exists() and not (settings / 'settings.ini').exists() and 'NEO98 HOST drive C:' not in log
          and code == 0 and lines[-1] == 'quit')
    tools_here = all(any(Path(d, tool).exists() for d in ('/opt/homebrew/bin', '/usr/local/bin'))
                     for tool in ('qemu-system-i386', 'mformat', 'cabextract', 'tesseract'))
    if tools_here:
        steps = [f'iso={dummy_iso}', f'folder={target}', 'key=ABCDE-12345', 'install', 'quit']
        code, log = run_host([str(binary)], environment(settings, NEO98_HOST_WIZARD=';'.join(steps)),
                             out / 'key.log', 120)
        check('malformed product key refused before setup starts',
              'install refused: key' in wizard(log) and 'install started' not in log and not target.exists())
    else:
        notes.append('QEMU and the other tools are not installed: the product key check was skipped')

    # --- 2. An existing folder ---------------------------------------------
    settings = out / 'settings-2'
    machine = clone_machine(out / 'machine-2')
    program = machine / 'C' / 'WINDOWS' / 'NOTEPAD.EXE'
    steps = [f'existing={empty}', f'open={program}', f'existing={machine}']
    other = out / 'machine-other'
    code, log = run_host([str(binary)],
                         environment(settings, NEO98_HOST_WIZARD=';'.join(steps),
                                     NEO98_HOST_INPUT=f'0:wait=Progman;1:menu=settings;1:menu=settings={other};1:menu=quit'),
                         out / 'existing.log')
    lines = wizard(log)
    check('folder without Windows 98 refused', f'existing {empty} refused' in lines)
    check('machine folder accepted and written to settings.ini',
          f'existing {machine} accepted' in lines and f'ready machine={machine}' in lines
          and f'NEO98 HOST app settings={settings / "settings.ini"} machine={machine} ' in log)
    check('host starts from it', f'NEO98 HOST drive C: {machine / "C"}' in log
          and 'NEO98 HOST input wait=Progman reached' in log and code == 0)
    check('a program double clicked during the assistant starts with the machine',
          f'open {program} kept' in lines and f'NEO98 HOST open {program} -> C:\\WINDOWS\\NOTEPAD.EXE' in log)
    check('Settings shows the machine', f'NEO98 HOST preferences Maschinenordner: {machine}' in log
          and f'NEO98 HOST preferences Laufwerk C: {machine / "C"}' in log)
    check('another machine folder chosen in Settings is kept for the next start',
          f'NEO98 HOST machine {other} ready=0 restart=0' in log
          and f'machine = {other}\n' in (settings / 'settings.ini').read_text())
    shutil.rmtree(machine, ignore_errors=True)

    # --- 3. and 4. Installieren with a finished disk image -----------------
    iso = Path(os.environ.get('NEO98_WIN98_ISO') or Path.home() / 'Downloads/Windows 98 Second Edition.iso')
    record = win98_c.record(pristine)
    installed = win98_c.live_folder().parent / 'setup-win98' / record['iso']['sha256'][:16]
    if not iso.is_file() or iso.stat().st_size != record['iso']['size'] or not (installed / 'disk.raw').is_file():
        notes.append(f'SKIP install runs: {iso} or {installed / "disk.raw"} is missing')
    else:
        def stage(settings):
            work = settings / 'setup' / installed.name
            work.mkdir(parents=True)
            subprocess.run(['cp', '-c', str(installed / 'disk.raw'), str(work / 'disk.raw')], check=True)
            (work / 'state.json').write_text(json.dumps({'iso_sha256': record['iso']['sha256'],
                                                         'status': 'complete'}) + '\n')
            return work

        # 4 first: cancel during the copy.
        settings = out / 'settings-4'
        work = stage(settings)
        target = out / 'machine-4'
        steps = [f'iso={iso}', f'folder={target}', 'cancel-on=copying the installation', 'install', 'quit']
        code, log = run_host([str(binary)], environment(settings, NEO98_HOST_WIZARD=';'.join(steps)),
                             out / 'cancel.log', 600)
        lines = wizard(log)
        check('Cancel stops setup', 'cancel' in lines and 'status setup: cancelled' in lines
              and 'finished code=2' in lines and lines[-1] == 'quit' and code == 0)
        check('Cancel removes the unfinished folder and journal',
              not (target / 'C').exists() and not (target / 'C.registry').exists()
              and not (settings / 'settings.ini').exists())
        check('the finished installation stays for the next attempt', (work / 'disk.raw').is_file())
        shutil.rmtree(settings, ignore_errors=True)

        # 3: the bundle, sandboxed, through to the desktop.
        app = mac_app.build_app(build=False, target=out / 'Neo98.app')
        settings = out / 'settings-3'
        work = stage(settings)
        target = out / 'machine-3'
        main_checkout = (ROOT / 'build/analysis-venv').resolve().parents[1]
        profile = out / 'bundle.sb'
        profile.write_text('(version 1)\n(allow default)\n'
                           + ''.join(f'(deny file-read-data (subpath "{path}"))\n'
                                     for path in sorted({str(ROOT), str(main_checkout), '/opt/homebrew', '/usr/local'}))
                           + f'(allow file-read-data (subpath "{out}"))\n')
        steps = [f'iso={iso}', f'key={KEY}', f'folder={target}', 'install']
        code, log = run_host(['/usr/bin/sandbox-exec', '-f', str(profile), str(app / 'Contents/MacOS/Neo98')],
                             environment(settings, NEO98_HOST_WIZARD=';'.join(steps),
                                         NEO98_HOST_INPUT='0:wait=Shell_TrayWnd;1:idle=1000;1:menu=quit'),
                             out / 'install.log')
        lines = wizard(log)
        status = [line[len('status '):] for line in lines if line.startswith('status ')]
        check('status lines of setup are shown',
              status[:3] == ['setup: checking the ISO', f'setup: copying the installation into {target / "C"}',
                             'setup: recording checksums'] and 'finished code=0' in lines)
        check('drive C: and the journal are in the chosen folder',
              (target / 'C' / win98_c.MARKER).is_file() and (target / 'C.registry').is_file()
              and f'ready machine={target}' in lines)
        expected, actual = expected_checksums(pristine, iso), win98_c.checksums(target / 'C')
        check('original Windows files and exact ISO source files match', actual == expected)
        if actual != expected:
            print('Checksum differences:', sorted(name for name in set(actual) | set(expected)
                                                 if actual.get(name) != expected.get(name)))
        check('only what a user needs: no snapshot, no generated assets',
              not Path(str(target / 'C') + win98_c.PRISTINE_SUFFIX).exists()
              and 'importing the registry' not in log)
        check('the disk image is gone after the copy', not work.exists())
        check('the desktop starts at the end', f'NEO98 HOST drive C: {target / "C"}' in log
              and 'NEO98 HOST input wait=Shell_TrayWnd reached' in log and code == 0)
        leaked = [str(path) for path in [out / 'install.log', *settings.rglob('*'), *(out / 'logs').rglob('*')]
                  if path.is_file() and KEY.encode() in path.read_bytes()]
        check('the product key is in no log and no file', not leaked and KEY not in log)
        check('no __pycache__ in the bundle', not list(app.rglob('__pycache__')))
        subprocess.run(['/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/'
                        'Support/lsregister', '-u', str(app)], capture_output=True)
        for name in ('machine-3', 'Neo98.app', 'settings-3'):
            shutil.rmtree(out / name, ignore_errors=True)
    shutil.rmtree(out / 'logs', ignore_errors=True)

    (out / 'results.json').write_text(json.dumps({'checks': checks, 'notes': notes}, indent=2, ensure_ascii=False) + '\n')
    for note in notes:
        print(note)
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    failed = [name for name, okay in checks.items() if not okay]
    if failed:
        print(f'SETUP ASSISTANT FAIL: {len(failed)} of {len(checks)} checks; logs in {out}')
        return 1
    print(f'SETUP ASSISTANT PASS: {len(checks)} checks; {out / "results.json"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
