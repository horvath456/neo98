#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""NEO98's Kodi add-on service.neo98 against the stand-in of Kodi's GUI in
tools/test_kodi_server.py: it reports each kind of dialog (OK, Yes/No with
a third button, a list, a password, a progress) with its words, keeps the
serial of a progress while it moves, and answers each dialog through the
built-in function Kodi would run. No Kodi, no NEO98.

    python3 tools/test_kodi_helper.py


"""
import json
import socket
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_kodi_server  # noqa: E402


class Client:
    def __init__(self, port):
        self.socket = socket.create_connection(('127.0.0.1', port), timeout=5)
        self.buffer = b''

    def ask(self, request):
        self.socket.sendall(json.dumps(request).encode() + b'\n')
        while b'\n' not in self.buffer:
            self.buffer += self.socket.recv(65536)
        line, self.buffer = self.buffer.split(b'\n', 1)
        return json.loads(line)

    def dialog(self, serial_after=0, seconds=3):
        """The dialog once the add-on has read a new one."""
        deadline = time.monotonic() + seconds
        while True:
            reply = self.ask({'get': 1})
            if reply['serial'] > serial_after or time.monotonic() > deadline:
                return reply
            time.sleep(0.05)


def main():
    failed = []

    def check(name, condition):
        print(('PASS ' if condition else 'FAIL ') + name, flush=True)
        if not condition:
            failed.append(name)

    server = test_kodi_server.start()
    gui = server.kodi.gui
    client = Client(server.helper)
    try:
        check('no dialog: serial 0', client.ask({'get': 1}) == {'serial': 0})

        gui.open({'kind': 'ok', 'heading': 'Netflix', 'text': 'Your session has expired.'})
        d = client.dialog()
        check('ok: kind, heading, text, button', (d['kind'], d['heading'], d['text'], d['buttons']) ==
              ('ok', 'Netflix', 'Your session has expired.', ['OK']))
        check('ok: a stale serial is refused', client.ask({'answer': {'serial': d['serial'] + 7, 'button': 0}}) == {'ok': False})
        check('ok: the answer is taken', client.ask({'answer': {'serial': d['serial'], 'button': 0}}) == {'ok': True})
        gui.closed.wait(2)
        check('ok: Kodi clicks OK', gui.result == {'button': 0, 'label': 'OK'})
        last = d['serial']

        gui.open({'kind': 'yesno', 'heading': 'YouTube', 'text': 'Sign in now?', 'buttons': ['Later', 'Sign in', 'Never']})
        d = client.dialog(last)
        check('yesno: a new serial and three buttons', d['serial'] > last and d['buttons'] == ['Later', 'Sign in', 'Never'])
        client.ask({'answer': {'serial': d['serial'], 'button': 1}})
        gui.closed.wait(2)
        check('yesno: Kodi clicks Yes', gui.result == {'button': 1, 'label': 'Sign in'})
        last = d['serial']

        gui.open({'kind': 'select', 'heading': "Who's watching?", 'choices': test_kodi_server.PROFILES})
        d = client.dialog(last)
        check('select: the choices', d['kind'] == 'select' and d['choices'] == test_kodi_server.PROFILES)
        client.ask({'answer': {'serial': d['serial'], 'item': 2}})
        gui.closed.wait(2)
        check('select: Kodi picks the third', gui.result == {'item': 2, 'label': 'Guest'})
        last = d['serial']

        gui.open({'kind': 'input', 'heading': 'Enter your password', 'hidden': True})
        d = client.dialog(last)
        check('input: a password is hidden', d['kind'] == 'input' and 'hidden' in d['flags'] and d['input'] == '')
        client.ask({'answer': {'serial': d['serial'], 'text': 'hunter2'}})
        gui.closed.wait(2)
        check('input: Kodi gets the text', gui.result == {'text': 'hunter2'})
        last = d['serial']

        gui.open({'kind': 'input', 'heading': 'Search', 'input': 'stranger'})
        d = client.dialog(last)
        check('input: a plain one starts with its text', 'hidden' not in d['flags'] and d['input'] == 'stranger')
        client.ask({'answer': {'serial': d['serial'], 'cancel': True}})
        gui.closed.wait(2)
        check('input: cancel goes Back', gui.result == {'cancel': True})
        last = d['serial']

        gui.open({'kind': 'progress', 'heading': 'Signing in', 'text': 'Please wait', 'cancel': True, 'percent': 10})
        d = client.dialog(last)
        check('progress: percent and cancel', d['kind'] == 'progress' and d['percent'] == 10 and 'cancel' in d['flags'])
        with gui.lock:
            gui.labels[9], gui.percent = 'Almost there', 80
        time.sleep(0.3)
        moved = client.ask({'get': 1})
        check('progress: moves under the same serial', moved['serial'] == d['serial'] and moved['percent'] == 80 and moved['text'] == 'Almost there')
        gui.close({'closed': True})
        time.sleep(0.3)
        check('progress: gone when Kodi closes it', client.ask({'get': 1}) == {'serial': 0})

        client.ask({'get': 1})
        time.sleep(0.4)
        check("overlays: Kodi's overlays show while no video plays", 'neo98.video' not in gui.properties)
        with gui.lock:
            gui.video = True
        client.ask({'get': 1})
        time.sleep(0.4)
        check('overlays: hidden while a video plays and NEO98 asks', gui.properties.get('neo98.video') == '1')
        time.sleep(2.3)
        check('overlays: back once NEO98 stops asking', 'neo98.video' not in gui.properties)
    finally:
        client.socket.close()
        server.stop()
    if failed:
        raise SystemExit(f'KODI HELPER FAIL: {len(failed)}')
    print('KODI HELPER PASS')


if __name__ == '__main__':
    main()
