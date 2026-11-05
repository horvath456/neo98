#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A Kodi for the tests of the host media provider: JSON-RPC over TCP on
localhost with a fixed catalogue, and the posters over HTTP. No real Kodi,
no Netflix, no video and no sound.

The catalogue: the add-ons Netflix and YouTube and the source Movies;
Netflix > My List (two films), TV Shows > Stranger Things (Season 1 with
three episodes, Season 2) and "[B]The Crown[/B] [COLOR red]New[/COLOR]",
All Movies (70 films with posters on two pages, the first ending in "Next
page (2)"); a folder that fails and one that is empty. With
netflix=False Kodi has neither the Netflix add-on nor Widevine.

The player answers Player.Open, PlayPause, Seek, Stop, GetProperties and
GetActivePlayers and sends Kodi's notifications (Player.OnPlay, OnAVStart,
OnPause, OnResume, OnSeek, OnStop) to every connection. GUI.GetProperties
reports the fullscreen video while it plays. Methods of the tests:
NEO98Test.Back (the user leaves the video in Kodi, it keeps playing),
NEO98Test.Stop (the user stops it in Kodi), NEO98Test.Quit (Kodi quits).
Every request is logged as one JSON line.

Kodi's dialogs: the real add-on service.neo98
(tools/host_media/kodi_addon/service.neo98/service.py) runs here against a
stand-in of Kodi's GUI (the modules xbmc and xbmcgui), on `server.helper`
for NEO98_KODI_HELPER_PORT. NEO98Test.Dialog opens a dialog (kind ok, yesno,
select, input, progress; heading, text, buttons, choices, hidden),
NEO98Test.CloseDialog closes it as Kodi would; the answer the add-on gives
is logged as Dialog.Answer. With profiles=True the Netflix folder first asks
"Who's watching?" (a list) and waits for the answer, as the real add-on
does.

In a test: `server = start(log, netflix=True)`, then `server.port` for
NEO98_TEST_KODI, `server.stop` at the end. As a program it stands in
for Kodi's binary (NEO98_KODI): it listens on NEO98_KODI_PORT after
NEO98_TEST_KODI_DELAY seconds, logs to NEO98_TEST_KODI_LOG and ends with
Application.Quit.
"""
import http.server
import importlib.util
import json
import os
import socket
import socketserver
import struct
import sys
import threading
import time
import types
import zlib
from pathlib import Path

COLOURS = {'red': (0xd0, 0x10, 0x10), 'blue': (0x10, 0x30, 0xd0), 'green': (0x10, 0xa0, 0x30), 'yellow': (0xe0, 0xc0, 0x10)}


def png(rgb, width=100, height=100):
    """A PNG of one colour."""
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    rows = b''.join(b'\0' + bytes(rgb) * width for _ in range(height))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


NETFLIX = 'plugin://plugin.video.netflix/'
SERVICE = Path(__file__).resolve().parent / 'host_media/kodi_addon/service.neo98/service.py'
PROFILES = ['Anna', 'Kids', 'Guest']


class Gui:
    """Kodi's GUI as far as service.neo98 reads and drives it: one dialog
    at a time, its labels by control id, and what closes it."""
    WINDOWS = {'yesno': 10100, 'progress': 10101, 'input': 10103, 'number': 10109, 'select': 12000, 'ok': 12002}

    def __init__(self, log):
        self.log = log
        self.lock = threading.Lock()
        self.window, self.labels, self.shown, self.items, self.focus, self.percent = 0, {}, set(), [], 0, 0
        self.closed = threading.Event()
        self.closed.set()
        self.result = None
        # A video plays; the properties of the home window.
        self.video, self.properties = False, {}

    def open(self, spec):
        """A dialog as Kodi draws it: ids of DialogConfirm, DialogSelect, DialogKeyboard, DialogProgress."""
        kind = spec.get('kind', 'ok')
        window = self.WINDOWS[kind]
        labels = {1: spec.get('heading', ''), 9: spec.get('text', '')}
        buttons = spec.get('buttons') or {'ok': ['OK'], 'yesno': ['No', 'Yes']}.get(kind, [])
        shown = set()
        # Kodi 21's OK dialog shows its one button as 11.
        for i, label in enumerate(buttons):
            labels[11 if kind == 'ok' else 10 + i] = label
            shown.add(11 if kind == 'ok' else 10 + i)
        if kind == 'input':
            labels[311] = spec.get('heading', '')
            labels[312] = '*' * len(spec.get('input', '')) if spec.get('hidden') else spec.get('input', '')
        if kind == 'progress' and spec.get('cancel'):
            shown.add(10)
        if kind == 'select':
            shown.add(3)
        with self.lock:
            self.window, self.labels, self.shown = window, labels, shown
            self.items, self.focus, self.percent, self.result = list(spec.get('choices', [])), 0, int(spec.get('percent', 0)), None
            self.closed.clear()

    def close(self, result):
        with self.lock:
            if not self.window:
                return
            self.window, self.result = 0, result
        self.log('Dialog.Answer', result)
        self.closed.set()

    def info(self, label):
        with self.lock:
            if label.startswith('Control.GetLabel('):
                return self.labels.get(int(label[17:-1]), '')
            if label == 'System.ProgressBar':
                return str(self.percent) if self.window == 10101 else ''
            if label.startswith('Container(') and label.endswith(').NumItems'):
                return str(len(self.items))
            if '.ListItemAbsolute(' in label:
                index = int(label.split('ListItemAbsolute(')[1].split(')')[0])
                return self.items[index] if index < len(self.items) else ''
        return ''

    def visible(self, condition):
        with self.lock:
            if condition == 'Player.HasVideo':
                return self.video
            return condition.startswith('Control.IsVisible(') and int(condition[18:-1]) in self.shown

    def builtin(self, command):
        name, _, rest = command.partition('(')
        arguments = [a.strip() for a in rest.rstrip(')').split(',')] if rest else []
        if name in ('SetProperty', 'ClearProperty'):
            with self.lock:
                if name == 'SetProperty':
                    self.properties[arguments[0]] = arguments[1]
                else:
                    self.properties.pop(arguments[0], None)
            return
        if name == 'SetFocus':
            with self.lock:
                self.focus = int(arguments[1])
        elif name == 'SendClick':
            window, control = int(arguments[0]), int(arguments[1])
            # Kodi's list ignores a click message: it carries no action.
            if window == 12002 and control == 11:
                self.close({'button': 0, 'label': self.labels.get(control, '')})
            elif window in (10100, 10101):
                self.close({'button': control - 10, 'label': self.labels.get(control, '')})
        elif name == 'Action' and arguments[:1] == ['Back']:
            self.close({'cancel': True})
        elif name == 'Action' and arguments == ['Select', '12000'] and self.window == 12000:
            self.close({'item': self.focus, 'label': self.items[self.focus] if self.focus < len(self.items) else ''})

    def jsonrpc(self, text):
        request = json.loads(text)
        if request.get('method') == 'Input.SendText' and self.window in (10103, 10109):
            self.close({'text': request['params']['text']})
        return json.dumps({'jsonrpc': '2.0', 'id': request.get('id'), 'result': 'OK'})


def helper(gui, port=0):
    """service.neo98 against the stand-in GUI: (its listener, its stop event)."""
    xbmc, xbmcgui = types.ModuleType('xbmc'), types.ModuleType('xbmcgui')
    xbmc.getInfoLabel, xbmc.getCondVisibility = gui.info, gui.visible
    xbmc.executebuiltin, xbmc.executeJSONRPC = gui.builtin, gui.jsonrpc
    xbmc.LOGWARNING, xbmc.log = 2, lambda *args: None

    # No xbmcgui.Window: control objects of Kodi's dialogs crash Kodi.
    xbmcgui.getCurrentWindowDialogId = lambda: gui.window
    saved = {name: sys.modules.get(name) for name in ('xbmc', 'xbmcgui')}
    sys.modules.update(xbmc=xbmc, xbmcgui=xbmcgui)
    try:
        spec = importlib.util.spec_from_file_location('neo98_kodi_service', SERVICE)
        service = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(service)
    finally:
        for name, module in saved.items():
            if module is None:
                sys.modules.pop(name, None)
            else:
                sys.modules[name] = module
    dialogs, stopping, listener = service.Dialogs(), threading.Event(), service.listen(port)

    def poll():
        while not stopping.wait(0.05):
            dialogs.poll()
    for target, arguments in ((service.serve, (dialogs, listener, stopping)), (poll, ())):
        threading.Thread(target=target, args=arguments, daemon=True).start()
    return listener, stopping
EPISODES = [('80057281', 'Chapter One: The Vanishing of Will Byers', 2940),
            ('80077209', 'Chapter Two: The Weirdo on Maple Street', 3300),
            ('80077210', 'Chapter Three: Holly, Jolly', 3120)]


class Kodi:
    def __init__(self, log=None, netflix=True, profiles=False, start_delay=0.1):
        self.log_path, self.netflix, self.profiles, self.start_delay = log, netflix, profiles, start_delay
        self.gui = Gui(self.log)
        self.lock = threading.Lock()
        self.clients = []
        self.playing = None    # (file, title, total)
        self.speed = 0
        self.position = 0.0
        self.since = 0.0
        self.fullscreen = False
        self.volume, self.muted = 80, False
        self.quit = threading.Event()
        self.art_port = 0

    def art(self, name):
        url = f'http://127.0.0.1:{self.art_port}/poster/{name}.png'
        return 'image://' + ''.join(c if c.isalnum() or c in '-_.~' else '%%%02x' % ord(c) for c in url) + '/'

    def log(self, method, params):
        if self.log_path:
            with self.lock, open(self.log_path, 'a') as f:
                f.write(json.dumps({'method': method, 'params': params}) + '\n')

    def broadcast(self, method, data):
        message = json.dumps({'jsonrpc': '2.0', 'method': method, 'params': {'data': data, 'sender': 'xbmc'}}).encode()
        for client in list(self.clients):
            try:
                client.sendall(message)
            except OSError:
                pass

    def time(self):
        if self.playing and self.speed:
            return min(self.position + time.monotonic() - self.since, self.playing[2])
        return self.position

    @staticmethod
    def clock(seconds):
        s = int(seconds)
        return {'hours': s // 3600, 'minutes': s // 60 % 60, 'seconds': s % 60, 'milliseconds': 0}

    def item(self, label, file, kind='unknown', folder=True, art=None, plot='', title=None):
        entry = {'label': label, 'file': file, 'filetype': 'directory' if folder else 'file', 'type': kind,
                 'title': title or label, 'plot': plot, 'thumbnail': '', 'art': {}}
        if art:
            entry['art'] = {'poster': self.art(art)}
        return entry

    def directory(self, path):
        """Files.GetDirectory: the files, or None for Kodi's error."""
        n = NETFLIX + 'directory/'
        if path == 'special://home/cdm/':
            return [{'label': 'libwidevinecdm.so', 'file': 'special://home/cdm/libwidevinecdm.so', 'filetype': 'file'}] if self.netflix else []
        if path.startswith(NETFLIX) and not self.netflix:
            return None
        if path == NETFLIX:
            # The add-on asks who watches before its first folder; Kodi's
            # JSON-RPC waits meanwhile.
            if self.profiles:
                self.profiles = False
                self.gui.open({'kind': 'select', 'heading': "Who's watching?", 'choices': PROFILES})
                if not self.gui.closed.wait(60) or not self.gui.result or self.gui.result.get('cancel'):
                    return []
            return [self.item('My List', n + 'mylist/'), self.item('TV Shows', n + 'tvshows/'), self.item('All Movies', n + 'long/')]
        if path == n + 'mylist/':
            return [self.item('Glass Onion', NETFLIX + 'play/81458416', 'movie', False, 'yellow', 'A tech billionaire invites friends to his island.'),
                    self.item('Extraction', NETFLIX + 'play/80230399', 'movie', False, 'green', 'A mercenary on a rescue mission.')]
        if path == n + 'tvshows/':
            return [self.item('Stranger Things', n + 'show/strangerthings/', 'tvshow', True, 'red',
                              'When a young boy vanishes, a small town uncovers a mystery in Hawkins, Indiana.'),
                    self.item('[B]The Crown[/B] [COLOR red]New[/COLOR]', n + 'show/thecrown/', 'tvshow', True, 'blue', 'The reign of Queen Elizabeth II.')]
        if path == n + 'show/strangerthings/':
            return [self.item('Season 1', n + 'season/strangerthings/1/', 'season', True, 'red'),
                    self.item('Season 2', n + 'season/strangerthings/2/', 'season', True, 'red')]
        if path == n + 'season/strangerthings/1/':
            return [self.item(f'{i + 1}. {title}', NETFLIX + 'play/' + video, 'episode', False, 'red', title=title)
                    for i, (video, title, _) in enumerate(EPISODES)]
        # 70 films with posters on two pages, as YouTube pages its lists.
        if path == n + 'long/':
            return [self.item(f'Item {i + 1}', NETFLIX + f'play/long{i + 1}', 'movie', False, 'green') for i in range(40)] + \
                   [self.item('Next page (2)', n + 'long/?page=2&page_token=CAIQAA')]
        if path == n + 'long/?page=2&page_token=CAIQAA':
            return [self.item(f'Item {i + 41}', NETFLIX + f'play/long{i + 41}', 'movie', False, 'green') for i in range(30)]
        if path == n + 'empty/':
            return []
        if path == 'plugin://plugin.video.youtube/':
            return [self.item('Search', 'plugin://plugin.video.youtube/search/'), self.item('Popular', 'plugin://plugin.video.youtube/popular/')]
        if path == '/media/movies/':
            return [self.item('Big Buck Bunny.mkv', '/media/movies/Big Buck Bunny.mkv', 'movie', False)]
        return None

    def call(self, method, params):
        """The result of a request, or (code, message) for Kodi's error."""
        self.log(method, params)
        if method == 'JSONRPC.Ping':
            return 'pong'
        if method == 'Application.GetProperties':
            return {'name': 'Kodi', 'version': {'major': 21, 'minor': 1, 'revision': 'test', 'tag': 'stable'}, 'volume': self.volume, 'muted': self.muted}
        if method in ('Application.SetVolume', 'Application.SetMute'):
            if method == 'Application.SetVolume':
                self.volume = int(params.get('volume', self.volume))
            else:
                self.muted = bool(params.get('mute'))
            self.broadcast('Application.OnVolumeChanged', {'volume': self.volume, 'muted': self.muted})
            return self.volume if method == 'Application.SetVolume' else self.muted
        if method == 'Application.Quit':
            self.quit.set()
            return 'OK'
        if method == 'Addons.GetAddonDetails':
            known = {'inputstream.adaptive', 'script.module.inputstreamhelper'} | ({'plugin.video.netflix'} if self.netflix else set())
            if params.get('addonid') not in known:
                return (-32602, 'Invalid params.')
            return {'addon': {'addonid': params['addonid'], 'enabled': True, 'type': 'xbmc.python.pluginsource'}}
        if method == 'Addons.GetAddons':
            addons = [{'addonid': 'plugin.video.youtube', 'name': 'YouTube', 'thumbnail': self.art('red'), 'summary': 'Watch videos.'}]
            if self.netflix:
                addons.insert(0, {'addonid': 'plugin.video.netflix', 'name': 'Netflix', 'thumbnail': self.art('red'), 'summary': 'Netflix VOD Services Add-on'})
            return {'addons': addons, 'limits': {'start': 0, 'end': len(addons), 'total': len(addons)}}
        if method == 'Files.GetSources':
            return {'sources': [{'label': 'Movies', 'file': '/media/movies/'}], 'limits': {'start': 0, 'end': 1, 'total': 1}}
        if method == 'Files.GetDirectory':
            files = self.directory(params.get('directory', ''))
            if files is None:
                return (-32602, 'Invalid params.')
            return {'files': files, 'limits': {'start': 0, 'end': len(files), 'total': len(files)}}
        if method == 'Player.Open':
            file = params.get('item', {}).get('file', '')
            if 'missing' in file:
                return (-32602, 'Invalid params.')
            title, total = next(((t, d) for v, t, d in EPISODES if file.endswith('/' + v)), (file.rsplit('/', 1)[-1], 600))
            with self.lock:
                self.playing, self.speed, self.position, self.since, self.fullscreen = (file, title, total), 1, 0.0, time.monotonic(), True
            item = {'title': title, 'type': 'episode'}
            # OnAVStart: the first picture, start_delay seconds after the start.
            threading.Timer(0.1, lambda: self.broadcast('Player.OnPlay', {'item': item, 'player': {'playerid': 1, 'speed': 1}})).start()
            threading.Timer(max(self.start_delay, 0.1), lambda: self.broadcast('Player.OnAVStart', {'item': item, 'player': {'playerid': 1, 'speed': 1}})).start()
            return 'OK'
        if method == 'Player.GetActivePlayers':
            return [{'playerid': 1, 'playertype': 'internal', 'type': 'video'}] if self.playing else []
        if method == 'GUI.GetProperties':
            return {'fullscreen': bool(self.playing) and self.fullscreen, 'currentwindow': {'id': 12005 if self.fullscreen else 10000, 'label': ''}}
        if method == 'GUI.SetFullscreen':
            self.fullscreen = bool(params.get('fullscreen'))
            return self.fullscreen
        if method.startswith('Player.') and not self.playing:
            return (-32100, 'Failed to execute method.')
        if method == 'Player.GetProperties':
            return {'time': self.clock(self.time()), 'totaltime': self.clock(self.playing[2]), 'speed': self.speed}
        if method == 'Player.PlayPause':
            with self.lock:
                self.position, self.since = self.time(), time.monotonic()
                self.speed = 0 if self.speed else 1
            self.broadcast('Player.OnPause' if not self.speed else 'Player.OnResume', {'item': {'title': self.playing[1]}, 'player': {'playerid': 1, 'speed': self.speed}})
            return {'speed': self.speed}
        if method == 'Player.Seek':
            value = params.get('value', {})
            with self.lock:
                if 'time' in value:
                    t = value['time']
                    target = t.get('hours', 0) * 3600 + t.get('minutes', 0) * 60 + t.get('seconds', 0)
                else:
                    target = self.time() + value.get('seconds', 0)
                self.position = max(0.0, min(target, self.playing[2]))
                self.since = time.monotonic()
            self.broadcast('Player.OnSeek', {'item': {'title': self.playing[1]}, 'player': {'playerid': 1, 'speed': self.speed}})
            return {'time': self.clock(self.position), 'totaltime': self.clock(self.playing[2]), 'percentage': 0}
        if method in ('Player.Stop', 'NEO98Test.Stop'):
            self.stop_playing()
            return 'OK'
        return (-32601, 'Method not found.')

    def stop_playing(self):
        if not self.playing:
            return
        title = self.playing[1]
        with self.lock:
            self.playing, self.speed, self.position, self.fullscreen = None, 0, 0.0, False
        self.broadcast('Player.OnStop', {'end': False, 'item': {'title': title}})

    def test_call(self, method, params=None):
        if method == 'NEO98Test.Dialog':
            self.gui.open(params or {})
        elif method == 'NEO98Test.CloseDialog':
            self.gui.close({'closed': True})
        elif method == 'NEO98Test.Back':
            self.fullscreen = False
        elif method == 'NEO98Test.Quit':
            self.broadcast('System.OnQuit', {'exitcode': 0})
            self.quit.set()
        return 'OK'

    def serve(self, connection):
        self.clients.append(connection)
        buffer = b''
        try:
            while not self.quit.is_set():
                data = connection.recv(65536)
                if not data:
                    break
                buffer += data
                decoder = json.JSONDecoder()
                while buffer.strip():
                    text = buffer.decode(errors='replace').lstrip()
                    try:
                        request, end = decoder.raw_decode(text)
                    except ValueError:
                        break
                    buffer = text[end:].encode()
                    method, params = request.get('method', ''), request.get('params') or {}
                    result = (self.test_call(method, params) if method in ('NEO98Test.Back', 'NEO98Test.Quit', 'NEO98Test.Dialog', 'NEO98Test.CloseDialog')
                              else self.call(method, params))
                    if 'id' not in request:
                        continue
                    reply = {'jsonrpc': '2.0', 'id': request['id']}
                    if isinstance(result, tuple):
                        reply['error'] = {'code': result[0], 'message': result[1]}
                    else:
                        reply['result'] = result
                    connection.sendall(json.dumps(reply).encode())
        except OSError:
            pass
        finally:
            self.clients.remove(connection)
            connection.close()


class Server:
    """The JSON-RPC port and the HTTP port of the posters, each on a thread."""

    def __init__(self, log=None, netflix=True, port=0, profiles=False, start_delay=0.1, helper_port=0):
        self.kodi = Kodi(log, netflix, profiles, start_delay)
        kodi = self.kodi
        self.helper_listener, self.helper_stopping = helper(kodi.gui, helper_port)
        self.helper = self.helper_listener.getsockname()[1]

        class Pictures(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                name = self.path.rsplit('/', 1)[-1].removesuffix('.png')
                if self.path.startswith('/poster/') and name in COLOURS:
                    body = png(COLOURS[name])
                    self.send_response(200)
                    self.send_header('Content-Type', 'image/png')
                    self.send_header('Content-Length', str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                else:
                    self.send_error(404)

            def log_message(self, *args):
                pass

        self.http = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Pictures)
        kodi.art_port = self.http.server_address[1]

        class Rpc(socketserver.BaseRequestHandler):
            def handle(self):
                kodi.serve(self.request)

        socketserver.ThreadingTCPServer.allow_reuse_address = True
        self.rpc = socketserver.ThreadingTCPServer(('127.0.0.1', port), Rpc)
        self.rpc.daemon_threads = True
        self.port = self.rpc.server_address[1]
        self.threads = [threading.Thread(target=s.serve_forever, daemon=True) for s in (self.http, self.rpc)]
        for t in self.threads:
            t.start()

    def stop(self):
        self.kodi.quit.set()
        for client in list(self.kodi.clients):
            try:
                client.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        self.rpc.shutdown()
        self.http.shutdown()
        self.rpc.server_close()
        self.http.server_close()
        self.helper_stopping.set()
        self.kodi.gui.close({'closed': True})
        self.helper_listener.close()


def start(log=None, netflix=True, profiles=False, start_delay=0.1):
    if log and os.path.exists(log):
        os.remove(log)
    return Server(str(log) if log else None, netflix, profiles=profiles, start_delay=start_delay)


def call(port, method, params=None):
    """One request to a running server, as another program would send it."""
    with socket.create_connection(('127.0.0.1', port), timeout=5) as s:
        s.sendall(json.dumps({'jsonrpc': '2.0', 'id': 1, 'method': method, 'params': params or {}}).encode())
        text, decoder = '', json.JSONDecoder()
        while True:
            text += s.recv(65536).decode()
            # Notifications to every connection may come before the answer.
            while text.strip():
                try:
                    reply, end = decoder.raw_decode(text.lstrip())
                except ValueError:
                    break
                text = text.lstrip()[end:]
                if reply.get('id') == 1:
                    return reply


def main():
    # Started under NEO98's compositor (neo98-kms): the Wayland test client
    # (tools/test_wayland_client.c) stands in for Kodi's window.
    window = None
    if '--windowing=wayland' in sys.argv[1:] and os.environ.get('NEO98_TEST_WAYLAND_CLIENT'):
        import subprocess
        window = subprocess.Popen([os.environ['NEO98_TEST_WAYLAND_CLIENT'], '--colour', os.environ.get('NEO98_TEST_WAYLAND_COLOUR', 'd01010')],
                                  stdin=subprocess.PIPE, stdout=subprocess.DEVNULL)
    time.sleep(float(os.environ.get('NEO98_TEST_KODI_DELAY', '0')))
    server = Server(os.environ.get('NEO98_TEST_KODI_LOG'), os.environ.get('NEO98_TEST_KODI_NETFLIX', '1') == '1',
                    int(os.environ.get('NEO98_KODI_PORT', '9090')), start_delay=float(os.environ.get('NEO98_TEST_KODI_START', '0.1')),
                    helper_port=int(os.environ.get('NEO98_KODI_HELPER_PORT', '9779')))
    print(f'test Kodi on port {server.port}', file=sys.stderr, flush=True)
    server.kodi.quit.wait()
    time.sleep(0.2)
    server.stop()
    if window:
        window.stdin.close()
        window.terminate()
        window.wait(timeout=10)


if __name__ == '__main__':
    main()
