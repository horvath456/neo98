#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""install the original Plus! 98 CD on disposable Windows clones.

Run through a shared serial test lock. NEO98_PLUS98_MEDIA names an already unpacked
CD directory; this test never opens or mounts an image. NEO98_PLUS98_KEY_FILE
can name a private file containing the CD's ten-digit product key. Otherwise
the test reuses the key fields in the existing Plus! registration journal.
Neither the key nor the installed Windows tree is kept in the test artifacts.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import arm64_run
import win98_c
from native_app import build_host


class Script:
    def __init__(self):
        self.steps = []
        self.time = 0.0

    def add(self, action, pause=1.0):
        self.steps.append(f'{self.time:.2f}:{action}')
        self.time += pause
        return self

    def key(self, digits):
        # Do not put a key in a type= action: the host logs those verbatim.
        for digit in digits:
            self.add(f'key={ord(digit):x}', 0.15)
        self.time += 0.5
        return self


def redact(text, key):
    # tops=2 also includes hidden controls from earlier wizard pages.
    text = re.sub(r'(class=Edit title=)(?:' + key[:3] + '|' + key[3:] + r')$',
                  r'\1[redacted]', text, flags=re.M)
    text = re.sub(r'\b\d{5}-\d{3}-\d{7}-\d{5}\b', '[redacted registration]', text)
    for value in (key, key[:3] + '-' + key[3:], key[3:]):
        text = text.replace(value, '[redacted]')
    return text


def registered_key(journal):
    """Read the existing Plus! key fields, without generating any key digits.

    Original setup.exe 409A70..409B78 builds ProductId as product code,
    input controls 44Eh and 44Fh, then a random five-digit suffix.
    """
    if not journal.is_file():
        return None
    data = journal.read_bytes()
    if not data.startswith((b'NEO98REG1', bytes([65, 82, 77, 57, 56]) + b'REG1')):
        return None
    offset, value = 9, None
    target = r'hkey_local_machine\software\microsoft\plus!98'

    def text():
        nonlocal offset
        size, = struct.unpack_from('<H', data, offset)
        offset += 2
        result = data[offset:offset + size].decode('latin1')
        offset += size
        return result

    while offset < len(data):
        kind = data[offset]
        offset += 1
        if kind not in (1, 2, 3, 4):
            raise ValueError('invalid registry journal record')
        path = text().lower()
        name = text().lower() if kind >= 3 else ''
        if kind == 2 and (target == path or target.startswith(path + '\\')):
            value = None
        if kind == 4 and path == target and name == 'productid':
            value = None
        if kind == 3:
            value_type, size = struct.unpack_from('<II', data, offset)
            offset += 8
            payload = data[offset:offset + size]
            offset += size
            if path == target and name == 'productid':
                value = payload.rstrip(b'\0') if value_type == 1 else None
    match = re.fullmatch(rb'\d{5}-(\d{3})-(\d{7})-\d{5}', value or b'')
    return (match[1] + match[2]).decode('ascii') if match else None


def clone(source, target, journal):
    check_space()
    subprocess.run(['cp', '-c', '-R', str(source.resolve()), str(target)], check=True)
    if journal.is_file():
        shutil.copyfile(journal, Path(str(target) + '.registry'))


def check_space():
    if shutil.disk_usage('/System/Volumes/Data').free < 6 * 1024**3:
        raise RuntimeError('STOP: fewer than 6 GiB free')


def run_host(binary, drive, media, out, script, key, deadline=600):
    check_space()
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_DEADLINE=str(deadline), NEO98_HOST_INPUT=';'.join(script.steps),
               NEO98_HOST_INPUT_ONCE='1', NEO98_SETTINGS_DIR=str(out / 'settings'),
               NEO98_LOG_DIR=str(out / 'logs'))
    for name in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_TRACE_CALLS',
                 'NEO98_TRACE_CALLS_FROM', 'NEO98_HOST_TRACE_EIP', 'NEO98_LOG_TO_FILE'):
        env.pop(name, None)
    command = [str(binary), '-', r'C:\WINDOWS\FONTS\VGAFIX.FON',
               '--exe', r'C:\WINDOWS\EXPLORER.EXE', '--quiet',
               '--system-tree', str(drive), '--drive-c', str(drive),
               '--registry', str(drive) + '.registry', '--cdrom', str(media),
               '--desktop', '800x600',
               '--crash-report-dir', str(out / 'reports')]
    arm64_run.ensure_idle()
    arm64_run.sync()
    lines = []
    with (out / 'host.log').open('w') as log:
        process = subprocess.Popen(command, env=env, cwd=out, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, errors='replace')

        def read_log():
            for line in process.stdout:
                clean = redact(line, key)
                lines.append(clean)
                log.write(clean)
                log.flush()

        reader = threading.Thread(target=read_log, daemon=True)
        reader.start()
        timed_out = False
        try:
            code = process.wait(deadline + 120)
        except subprocess.TimeoutExpired:
            timed_out = True
            code = arm64_run.end(process)
        except KeyboardInterrupt:
            arm64_run.end(process)
            raise
        finally:
            if process.poll() is not None:
                reader.join()
    text = ''.join(lines)
    # Reboot reports can include the key-bearing registry or window state.
    # The checked log and screenshots are the retained evidence.
    shutil.rmtree(out / 'reports', ignore_errors=True)
    return code, text, timed_out


def welcome(script, key):
    script.add('wait=Shell_TrayWnd').add('idle=1500')
    script.add(r'guestopen=D:\setup.exe').add('wait=|Microsoft Plus! 98 Setup')
    script.add('waitchild=Static|Welcome to the Microsoft Plus! 98 Setup wizard.')
    script.add('button=Next >').add('waitchild=Static|License Agreement')
    script.add('button=I accept the Agreement.').add('button=Next >')
    script.add('waitchild=Static|User Information')
    script.add('click=321,405').key(key[:3])
    script.add('click=376,405').key(key[3:])
    script.add('button=Next >', 2)
    return script


def installation(key, custom, out):
    script = welcome(Script(), key)
    script.add('waitchild=Static|VirusScan').add('button=Scan system for viruses')
    script.add('button=Next >').add('waitchild=Static|Setup Options')
    script.add('button=Custom' if custom else 'button=Complete').add('button=Next >')
    if custom:
        script.add('waitchild=Static|Components to Install')
        # The component tree's last row is VirusScan on the original CD.
        script.add('click=336,385').add(f'shot={out}/custom-components.ppm')
        script.add('button=Next >')
    script.add('waitchild=Static|Start Copying Files').add('tops=2')
    script.add('button=Next >')
    if not custom:
        script.add('wait=|McAfee Virus Shield').add('tops=2')
        script.add(f'shot={out}/mcafee-before.ppm').add('button=OK')
    script.add('waitchild=Static|Plus! 98 Setup is complete').add('tops=2')
    script.add(f'shot={out}/complete.ppm')
    script.add('button=Configure your Desktop Theme.').add('button=Run the Maintenance wizard.')
    script.add('button=Finish', 3).add('tops=2')
    if custom:
        # Without VirusScan the original setup reports success with OK and
        # does not request a restart. Do not inject a reboot on its behalf.
        script.add('button=OK', 3)
    else:
        script.add('button=Yes', 10)
        script.add('wait=|McAfee Virus Shield').add('tops=2').add('button=OK')
    script.add('wait=Shell_TrayWnd').add('tops=2')
    script.add(f'shot={out}/desktop.ppm')
    return script


def components(script, out):
    for name, program, window in (
        ('themes', r'C:\Program Files\PLUS!\Themes.exe', '|Desktop Themes'),
        ('deluxecd', r'C:\Program Files\PLUS!\DeluxeCD\deluxecd.exe', 'MMFRAME_MAIN'),
        ('spider', r'C:\Program Files\PLUS!\Spider\spider.exe', '|Spider'),
        ('maintenance', r'C:\WINDOWS\TUNEUP.EXE', '|Maintenance Wizard'),
    ):
        script.add('guestopen=' + program).add('wait=' + window)
        script.add('tops=2').add(f'shot={out}/{name}.ppm')
        # has its own close/reopen regression in
        # test_plus98_deluxecd_close_host.py; this test checks setup and launch.
        if name != 'deluxecd':
            script.add('close=*', 2)
    script.add(r'guestopen=C:\WINDOWS\NOTEPAD.EXE').add('wait=Notepad')
    script.add('type=Plus 98 session remains responsive.').add('tops=2')
    script.add(f'shot={out}/responsive.ppm').add('menu=quit')
    return script


def check_run(code, text, timed_out, custom, drive):
    assert code == 0 and not timed_out and 'menu-script quit ok' in text, 'host did not finish through its menu'
    assert 'timed out' not in text and 'NEO98 HOST deadline' not in text, 'a wizard or program was not reached'
    assert not re.search(r'input button=.* hwnd=0\b', text), 'a requested button was absent or disabled'
    assert 'Plus! 98 Setup is complete' in text, 'installer did not reach completion'
    assert 'Setup could not restart your computer' not in text
    assert 'NEO98 HOST failed' not in text and 'FAULT BOX' not in text
    assert 'NEO98 HOST imported Windows setup completed' not in text, 'setup completion was not persisted by the first logon'
    assert 'Runonce: InSetup' not in text, 'the imported Windows setup state blocked component registration'
    assert 'No components registered' not in text, 'Deluxe CD components were not registered'
    reboot = 'NEO98 HOST exitwindows flags=00000002 code=67'
    warning = 'MessageBox caption=McAfee Virus Shield text=Unable to load device drivers.'
    if custom:
        assert warning not in text, 'VirusScan was installed by the Custom scenario'
        assert 'NEO98 HOST exitwindows' not in text, 'Custom unexpectedly requested a reboot'
        assert text.count('NEO98 HOST drive C:') == 1, 'Custom session unexpectedly restarted'
        after = text.split('NEO98 HOST input button=Finish', 1)[1]
    else:
        assert reboot in text, 'no installer-requested EWX_REBOOT'
        assert text.count('NEO98 HOST drive C:') == 2, 'host did not re-execute exactly once'
        before, after = text.split(reboot, 1)
        assert warning in before and warning in after, 'McAfee warning was not checked before and after the reboot'
    final_windows = text.rsplit('NEO98 HOST tops foreground=', 1)[1]
    for closed in ('McAfee Virus Shield', 'Microsoft Plus! 98 Setup'):
        assert not re.search(r'top .*visible=1.*title=' + re.escape(closed), final_windows), closed + ' stayed open'
    for title in ('Desktop Themes', 'Spider', 'Maintenance Wizard'):
        assert re.search(r'top .*visible=1.*title=' + re.escape(title), after), title
    assert re.search(r'top .*visible=1.*class=MMFRAME_MAIN', after), 'Deluxe CD Player'
    assert 'class=Edit title=Plus 98 session remains responsive.' in after
    programs = [r'Program Files\PLUS!\Themes.exe', r'Program Files\PLUS!\DeluxeCD\deluxecd.exe',
                r'Program Files\PLUS!\Spider\spider.exe', r'WINDOWS\TUNEUP.EXE']
    for program in programs:
        assert win98_c.path(program, drive), f'installed file missing: {program}'
    virus = win98_c.path(r'Program Files\PLUS!\Viruscan\vshwin32.exe', drive)
    assert bool(virus) == (not custom), 'VirusScan files disagree with the chosen installation type'
    return {'setup_complete': True, 'installer_reboot': not custom, 'shell_restarted': not custom,
            'virus_scan_installed': bool(virus), 'mcafee_dialog_dismissed': not custom,
            'programs_opened': programs, 'session_responsive': True,
            'deluxecd_close': 'covered by test_plus98_deluxecd_close_host.py'}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--only', choices=('typical', 'custom'))
    parser.add_argument('--source', type=Path, help='fresh Win98 C: tree; default is the original setup snapshot')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/plus98-setup-host')
    args = parser.parse_args()
    medium = os.environ.get('NEO98_PLUS98_MEDIA')
    if not medium:
        print('PLUS98 SETUP SKIP: set NEO98_PLUS98_MEDIA to the unpacked original CD directory')
        raise SystemExit(77)
    media = Path(medium).expanduser().resolve()
    for name in ('setup.exe', 'plus98/setup.exe', 'plus98/PLUS98.CAB'):
        if not (media / name).is_file():
            raise SystemExit(f'PLUS98 SETUP FAIL: missing {name} in NEO98_PLUS98_MEDIA')
    key_file = os.environ.get('NEO98_PLUS98_KEY_FILE')
    key = (re.sub(r'[\s-]', '', Path(key_file).read_text()) if key_file else
           registered_key(win98_c.journal(win98_c.live_folder())))
    if not key:
        print('PLUS98 SETUP SKIP: no existing Plus! registration; set NEO98_PLUS98_KEY_FILE to the original CD key file')
        raise SystemExit(77)
    if not re.fullmatch(r'[0-9]{10}', key):
        raise SystemExit('PLUS98 SETUP FAIL: the private key file must contain ten digits')
    source = (args.source or win98_c.pristine_folder()).resolve()
    if not win98_c.path(r'WINDOWS\EXPLORER.EXE', source):
        raise SystemExit('PLUS98 SETUP FAIL: a configured Win98 setup tree is required')
    if win98_c.path(r'Program Files\PLUS!\Setup!98\setup.exe', source):
        raise SystemExit('PLUS98 SETUP FAIL: --source must not already have Plus! 98 installed')
    arm64_run.ensure_idle()
    binary = ROOT / 'build/native-app/neo98-host'
    if not args.no_build:
        os.environ['NEO98_NATIVE_JOBS'] = '2'
        binary = build_host()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    scratch = ROOT / 'build/fix-plus98-scratch'
    scratch.mkdir(parents=True, exist_ok=True)
    results = {}
    for name in ('typical', 'custom'):
        if args.only and name != args.only:
            continue
        run = out / name
        shutil.rmtree(run, ignore_errors=True)
        run.mkdir()
        temporary = Path(tempfile.mkdtemp(prefix=name + '-', dir=scratch))
        keep_running = False
        try:
            drive = temporary / 'c'
            clone(source, drive, win98_c.journal(source))
            # The pristine snapshot predates Windows' first logon (Active
            # Setup). Complete that session and flush its registry before
            # installing Plus!, just as on an already configured user C:.
            initial = Script().add('wait=Shell_TrayWnd').add('idle=1500')
            initial.add('menu=quit')
            code, text, timed_out = run_host(binary, drive, media, run / 'first-logon', initial, key, 300)
            assert code == 0 and not timed_out and 'menu-script quit ok' in text, 'Windows first logon did not finish'
            assert 'timed out' not in text and 'NEO98 HOST deadline' not in text, 'Windows first logon timed out'
            assert 'FAULT BOX' not in text and 'NEO98 HOST failed' not in text, 'Windows first logon failed'
            script = components(installation(key, name == 'custom', run), run)
            code, text, timed_out = run_host(binary, drive, media, run, script, key)
            try:
                results[name] = check_run(code, text, timed_out, name == 'custom', drive)
            except AssertionError as error:
                # A failed navigation might have left the key page visible.
                for picture in run.glob('*.ppm'):
                    picture.unlink()
                (run / 'results.json').write_text(json.dumps({'passed': False, 'reason': str(error)}, indent=2) + '\n')
                raise AssertionError(f'{name}: {error}; see {run / "host.log"}') from None
            (run / 'results.json').write_text(json.dumps(results[name], indent=2) + '\n')
        except arm64_run.Stuck:
            keep_running = True
            raise
        finally:
            if not keep_running:
                shutil.rmtree(temporary)
        print(f'PLUS98 SETUP PASS: {name}', flush=True)
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
