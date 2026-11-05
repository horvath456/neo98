#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Media Center 98 against a real Kodi, by hand only: Kodi's own dialogs
come up as Windows dialogs and the answers typed there reach Kodi.

Kodi runs with a HOME of its own (build/media-center-real-kodi/home: its
profile, its log under Library/Logs), windowed, minimized and muted, on the
ports 19090 (JSON-RPC), 19777 (EventServer) and 19779 (service.neo98), so
the user's Kodi and profile stay untouched even while that Kodi runs. The
host puts service.neo98 into that profile and turns it on; a script run in
Kodi then shows a question, a list, a password, a progress and a message
(xbmcgui.Dialog), and the host's input answers them: Yes, the second entry,
"hunter2", OK. The script writes what Kodi got. Needs Kodi.app (macOS) or
kodi in PATH (Linux); never part of a suite.

    build/analysis-venv/bin/python tools/test_media_center_real_kodi.py [--no-build]


"""
import argparse
import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402

OUT = ROOT / 'build/media-center-real-kodi'
RPC, EVENTS, HELPER = 19090, 19777, 19779
SCRIPT = '''import json, sys, time
import xbmcgui
d = xbmcgui.Dialog()
r = {}
r['yesno'] = d.yesno('Continue watching?', 'Stranger Things, Chapter Two')
r['select'] = d.select("Who's watching?", ['Anna', 'Kids', 'Guest'])
r['password'] = d.input('Enter your password', option=xbmcgui.ALPHANUM_HIDE_INPUT)
p = xbmcgui.DialogProgress()
p.create('Signing in', 'Please wait')
for i in range(12):
    p.update(i * 8, 'Step %d of 12' % (i + 1))
    time.sleep(0.4)
p.close()
r['ok'] = d.ok('Signed in', 'Welcome back.')
with open(sys.argv[1], 'w') as f:
    json.dump(r, f)
'''
SETTINGS = f'''<settings version="2">
    <setting id="videoscreen.screenmode">WINDOW</setting>
    <setting id="window.width">480</setting>
    <setting id="window.height">270</setting>
    <setting id="services.esenabled">true</setting>
    <setting id="services.esport">{EVENTS}</setting>
    <setting id="lookandfeel.soundskin"></setting>
    <setting id="audiooutput.guisoundmode">0</setting>
    <setting id="general.addonupdates">2</setting>
    <setting id="general.addonnotifications">false</setting>
</settings>
'''


def builtin(function):
    """A built-in function of Kodi through its EventServer (HELO, then ACTION)."""
    def packet(kind, payload):
        return b'XBMC' + bytes([2, 0]) + struct.pack('!HIIHI', kind, 1, 1, len(payload), 0x4154) + b'\0' * 10 + payload
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as u:
        u.sendto(packet(0x01, b'NEO98 test\0\0' + struct.pack('!HII', 0, 0, 0)), ('127.0.0.1', EVENTS))
        time.sleep(0.3)
        u.sendto(packet(0x0A, b'\x01' + function.encode() + b'\0'), ('127.0.0.1', EVENTS))


def rpc(method, params=None):
    with socket.create_connection(('127.0.0.1', RPC), 10) as s:
        s.sendall(json.dumps({'jsonrpc': '2.0', 'id': 1, 'method': method, 'params': params or {}}).encode())
        text, decoder = '', json.JSONDecoder()
        while True:
            text += s.recv(65536).decode()
            try:
                reply, _ = decoder.raw_decode(text.lstrip())
            except ValueError:
                continue
            if reply.get('id') == 1:
                return reply
            text = ''


def listening(port):
    try:
        socket.create_connection(('127.0.0.1', port), 0.5).close()
        return True
    except OSError:
        return False


def kodi_program():
    if sys.platform == 'darwin':
        for app in (Path('/Applications/Kodi.app'), Path.home() / 'Applications/Kodi.app'):
            if (app / 'Contents/MacOS/Kodi').exists():
                return str(app / 'Contents/MacOS/Kodi')
    return shutil.which('kodi')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    args = parser.parse_args()
    program = kodi_program()
    if not program:
        raise SystemExit('No Kodi on this computer.')
    if any(listening(p) for p in (RPC, HELPER)):
        raise SystemExit(f'Port {RPC} or {HELPER} is taken: is a test Kodi still running?')
    if not args.no_build:
        native_app.build_host(vulkan=sys.platform != 'darwin', host_media=True)
    home = OUT / 'home'
    profile = home / ('Library/Application Support/Kodi' if sys.platform == 'darwin' else '.kodi')
    (profile / 'userdata').mkdir(parents=True, exist_ok=True)
    if not (profile / 'userdata/guisettings.xml').exists():
        (profile / 'userdata/guisettings.xml').write_text(SETTINGS)
    (profile / 'userdata/advancedsettings.xml').write_text(f'<advancedsettings version="1.0">\n    <jsonrpc><tcpport>{RPC}</tcpport></jsonrpc>\n</advancedsettings>\n')
    run = OUT / 'run'
    shutil.rmtree(run, ignore_errors=True)
    run.mkdir(parents=True)
    (run / 'dialogs.py').write_text(SCRIPT)
    result = run / 'result.json'
    kodi = subprocess.Popen([program], env=dict(os.environ, HOME=str(home), NEO98_KODI_HELPER_PORT=str(HELPER)),
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(60):
            try:
                rpc('Application.SetMute', {'mute': True})
                break
            except OSError:
                time.sleep(1)
        builtin('Minimize')

        def start_dialogs():
            # Once the host has turned service.neo98 on.
            for _ in range(240):
                if listening(HELPER):
                    time.sleep(1)
                    builtin(f'RunScript({run / "dialogs.py"},{result})')
                    return
                time.sleep(0.25)
        threading.Thread(target=start_dialogs, daemon=True).start()
        steps = ['0:wait=NEO98MediaCenter', '1:wait=|Continue watching?', '2.5:key=0d', "3:wait=|Who's watching?", '4.5:key=28',
                 '4.6:key=0d', '5:wait=|Enter your password', '6.5:type=hunter2', '6.6:key=0d', '7:wait=|Signing in',
                 '8:shot={o}/progress.png', '9:wait=|Signed in', '10.5:key=0d', '13:menu=quit']
        env = dict(os.environ, NEO98_CLIPBOARD='private', NEO98_KEYMAP='US', SDL_AUDIODRIVER='dummy', NEO98_MEDIA_WINDOW='0',
                   NEO98_SETTINGS_FILE=str(run / 'settings.ini'), NEO98_HOST_INPUT=';'.join(steps).replace('{o}', str(run)),
                   NEO98_KODI_PORT=str(RPC), NEO98_KODI_EVENTS_PORT=str(EVENTS), NEO98_KODI_HELPER_PORT=str(HELPER),
                   NEO98_KODI_HOME=str(profile), NEO98_KODI=str(run / 'no-other-kodi'), NEO98_YTDLP='none')
        for variable in ('NEO98_TEST_KODI', 'SDL_VIDEODRIVER'):
            env.pop(variable, None)
        # A C: of its own, so the run never goes to the user's running NEO98.
        pristine = win98_c.use_pristine()
        own = run / 'c'
        subprocess.run(['cp', '-c', '-R', str(pristine), str(own)] if sys.platform == 'darwin' else ['cp', '-a', '--reflink=auto', str(pristine), str(own)], check=True)
        shutil.copy(win98_c.journal(pristine), str(own) + '.registry')
        line = [str(ROOT / 'build/native-app/neo98-host')]
        if sys.platform == 'darwin':
            line += ['-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree', str(own)]
            env.update(NEO98_HOST_DEADLINE='150', NEO98_SETTINGS_DIR=str(run / 'settings'))
        line += ['--exe', r'C:\WINDOWS\MEDIACTR.EXE', '--drive-c', str(own), '--registry', str(own) + '.registry']
        with (run / 'log.txt').open('w') as log:
            try:
                code = subprocess.run(line, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=180).returncode
            except subprocess.TimeoutExpired:
                code = 'timeout'
    finally:
        # Out of any dialog the script still waits in, then Kodi quits.
        for _ in range(6):
            builtin('Action(Back)')
            time.sleep(0.4)
        try:
            rpc('Application.Quit')
            kodi.wait(30)
        except (OSError, subprocess.TimeoutExpired):
            kodi.kill()
    got = json.loads(result.read_text()) if result.exists() else None
    text = (run / 'log.txt').read_text(errors='replace')
    checks = {
        'the host turned service.neo98 on': 'NEO98 HOST media turned on service.neo98' in text or 'NEO98 HOST media dialog 1 ' in text,
        'Kodi got Yes, Kids, the password and OK': got == {'yesno': True, 'select': 1, 'password': 'hunter2', 'ok': True},
        'the password is not in the log': not any('hunter2' in l for l in text.splitlines() if not l.startswith('NEO98 HOST input ')),
        'the host ended cleanly': code == 0,
        'Kodi ended cleanly': kodi.returncode == 0,
    }
    for name, passed in checks.items():
        print(('PASS ' if passed else 'FAIL ') + name)
    if not all(checks.values()):
        raise SystemExit(f'REAL KODI FAIL, see {run} and {home}')
    print('REAL KODI PASS')


if __name__ == '__main__':
    main()
