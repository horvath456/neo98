#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Media Center 98 in the Linux host under X11 and in the Mac host (every
part but screen and shortcut), against the test Kodi of
tools/test_kodi_server.py (NEO98_TEST_KODI) and the add-on service.neo98
running in it against a stand-in of Kodi's GUI. No real Kodi, no Netflix,
no sound.

  catalog MEDIACTR.EXE shows Kodi's top: the add-ons Netflix and YouTube
             with their pictures and the source Movies
  browse double clicks open Netflix > TV Shows > Stranger Things >
             Season 1, whose three episodes show their posters; a double
             click on the first plays it in Kodi (Player.Open)
  endless All Movies (70 films on two pages): End at the end of the first
             page loads the second, whose posters show
  dialogs Kodi's dialogs as Windows dialogs: Netflix asks who watches
             while its folder loads (a list, Down and Enter pick Kids), then
             a question (Enter: Yes), a password (typed, never in the log), a
             progress Kodi closes itself, and a message Esc cancels
  shortcut on a writable C: the host puts MEDIACTR.EXE and its shortcut
             in Programs > Accessories > Entertainment and takes NETFLIX.EXE
             of an earlier build away; the shortcut opens Media Center 98
  nonetflix on a Kodi without the Netflix add-on, Netflix still stands in
             the catalogue and says what is missing when it opens
  nokodi a computer without Kodi: the program says so
  window (Mac) the video plays inside the window: the host says so,
             and the program paints the video area in the key colour that
             the host replaces with Kodi's window (ScreenCaptureKit, which
             only a hand test with a real Kodi shows); until Kodi's first
             picture the area shows Windows' download animation instead
  compositor (Linux) the host starts the test Kodi as Kodi itself
             (NEO98_KODI), with --windowing=wayland under the host's own
             compositor, where it shows the Wayland test client
             (tools/test_wayland_client.c) in one colour; the video plays in
             the video area of the window as the host draws it (hostshot),
             an open menu covers it, a double click gives it the whole
             window and Esc takes it back, Stop shows the list again; at
             least 57 of Kodi's 60 pictures a second reach the window
  screen the screen goes to Kodi while it plays: NEO98's window leaves
             X11 when the episode starts and comes back when Kodi stops it;
             Back in Kodi pauses the video and brings NEO98 back; Show Video
             (Ctrl+Enter) gives the screen to Kodi again and the video goes
             on; Kodi quitting brings NEO98 back

    build/analysis-venv/bin/python tools/test_media_center_host.py [--no-build] [--only PART...]


"""
import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import test_kodi_server  # noqa: E402
import win98_c  # noqa: E402

OWN_C = None
PARTS = ('catalog', 'browse', 'endless', 'dialogs', 'shortcut', 'nonetflix', 'nokodi', 'window', 'compositor', 'screen')
CLASS = 'NEO98MediaCenter'
LINK = r'C:\WINDOWS\Start Menu\Programs\Accessories\Entertainment\Media Center 98.lnk'
# Where the icons of the large icon view stand on the 800x600 desktop with
# the window at its default place: the first, second and third column.
COLUMNS = (318, 443, 567)
ROW = 170


def read_png(path):
    """width, height and the RGB bytes of the host's screenshot (8-bit RGB or RGBA PNG, or PPM)."""
    import struct
    import zlib
    data = Path(path).read_bytes()
    if data.startswith(b'P6'):
        # The Mac host writes its screenshots as binary PPM whatever the name.
        fields = data.split(maxsplit=4)
        return int(fields[1]), int(fields[2]), fields[4][:int(fields[1]) * int(fields[2]) * 3]
    at, chunks, width = 8, b'', 0
    while at < len(data):
        size, kind = struct.unpack('>I4s', data[at:at + 8])
        body = data[at + 8:at + 8 + size]
        if kind == b'IHDR':
            width, height, depth, colour = struct.unpack('>IIBB', body[:10])
            channels = {2: 3, 6: 4}[colour]
        elif kind == b'IDAT':
            chunks += body
        at += 12 + size
    raw, stride, rows, previous = zlib.decompress(chunks), width * channels, [], bytearray(width * channels)
    for y in range(height):
        kind, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b, c = previous[i], previous[i - channels] if i >= channels else 0
            if kind == 1: line[i] = (line[i] + a) & 255
            elif kind == 2: line[i] = (line[i] + b) & 255
            elif kind == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif kind == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line[i] for i in range(stride) if channels == 3 or i % 4 != 3))
        previous = line
    return width, height, b''.join(rows)


def red(path):
    """Pixels of the red test poster (0xd01010)."""
    width, height, pixels = read_png(path)
    return sum(1 for i in range(0, len(pixels), 3) if pixels[i] == 0xd0 and pixels[i + 1] == 0x10 and pixels[i + 2] == 0x10)


def host(out, name, steps, program, kodi, drive=None, command=None, seconds=40, window=False, profiles=False, driver=None, start_delay=0.1):
    """One run of the GUI host: (exit code, log text, calls of the test Kodi).
    driver(server) runs on a thread of its own while the host runs."""
    run = out / name
    shutil.rmtree(run, ignore_errors=True)
    run.mkdir(parents=True)
    server = None
    env = dict(os.environ, NEO98_CLIPBOARD='private', NEO98_KEYMAP='US', SDL_AUDIODRIVER='dummy', GDK_SCALE='1',
               GDK_DPI_SCALE='1', NEO98_SETTINGS_FILE=str(run / 'settings.ini'),
               NEO98_HOST_INPUT=';'.join(steps).replace('{out}', str(run)))
    # The video inside the window (Mac) only where a part asks for it.
    if not window:
        env['NEO98_MEDIA_WINDOW'] = '0'
    # Keys never reach a real Kodi on this computer (the test Kodi has no
    # EventServer): a port nobody listens on. Media Center's full screen
    # leaves the Mac window alone.
    env['NEO98_KODI_EVENTS_PORT'] = '9'
    env['NEO98_MEDIA_HOST_FULLSCREEN'] = '0'
    for variable in ('SDL_VIDEODRIVER', 'GDK_BACKEND', 'NEO98_GPU', 'NEO98_LOG_TO_FILE', 'NEO98_KODI', 'NEO98_KODI_PORT', 'NEO98_KODI_HELPER_PORT'):
        env.pop(variable, None)
    if kodi == 'none':
        env['NEO98_TEST_KODI'] = 'none'
    elif kodi == 'program':
        # The host starts the test Kodi itself, as it would start Kodi, on
        # ports of its own; started under the host's compositor it shows the
        # Wayland test client as its window.
        import socket
        from test_compositor_host import CLIENT
        ports = []
        for _ in range(2):
            with socket.socket() as held:
                held.bind(('127.0.0.1', 0))
                ports.append(held.getsockname()[1])
        env.update(NEO98_KODI=str(ROOT / 'tools/test_kodi_server.py'), NEO98_KODI_PORT=str(ports[0]), NEO98_KODI_HELPER_PORT=str(ports[1]),
                   NEO98_TEST_KODI_LOG=str(run / 'calls.jsonl'), NEO98_TEST_WAYLAND_CLIENT=str(CLIENT),
                   NEO98_TEST_WAYLAND_COLOUR='10c0c0', NEO98_MEDIA_FPS='1')
        env.pop('NEO98_TEST_KODI', None)
    else:
        server = test_kodi_server.start(run / 'calls.jsonl', netflix=kodi == 'netflix', profiles=profiles, start_delay=start_delay)
        env['NEO98_TEST_KODI'] = str(server.port)
        env['NEO98_KODI_HELPER_PORT'] = str(server.helper)
    # Nothing of the user's Kodi: no folder to put the add-on in.
    env['NEO98_KODI_HOME'] = str(run / 'no-kodi-home')
    # A C: of its own: a host on build/win98-c would hand the program to the
    # user's running NEO98 instead of starting.
    drive = drive or OWN_C
    line = [str(ROOT / 'build/native-app/neo98-host')]
    if sys.platform == 'darwin':
        # Positional arguments select the development CLI instead of the app wizard.
        line += ['-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree', str(drive)]
        env.update(NEO98_HOST_DEADLINE=str(seconds), NEO98_SETTINGS_DIR=str(run / 'settings'))
    else:
        line += ['--time-limit', str(seconds)]
    line += ['--exe', program]
    line += ['--drive-c', str(drive), '--registry', str(drive) + '.registry']
    if command:
        line += ['--command', command]
    try:
        if driver:
            import threading
            threading.Thread(target=driver, args=(server,), daemon=True).start()
        with (run / 'log.txt').open('w') as log:
            code = subprocess.run(line, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=seconds + 60).returncode
    finally:
        if server:
            server.stop()
    calls = [json.loads(l) for l in (run / 'calls.jsonl').read_text().splitlines()] if (run / 'calls.jsonl').exists() else []
    return code, (run / 'log.txt').read_text(errors='replace'), calls, run


def dialogs(server):
    """The dialogs after the choice of profile, each once the one before is answered."""
    import time
    gui = server.kodi.gui

    def answered(count, seconds=30):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if len([1 for _ in answers(server)]) >= count:
                return True
            time.sleep(0.1)
        return False
    if not answered(1, 40):
        return
    gui.open({'kind': 'yesno', 'heading': 'Continue watching?', 'text': 'Stranger Things, Chapter Two'})
    if not answered(2):
        return
    gui.open({'kind': 'input', 'heading': 'Enter your password', 'hidden': True})
    if not answered(3):
        return
    gui.open({'kind': 'progress', 'heading': 'Signing in', 'text': 'Please wait', 'cancel': True, 'percent': 40})
    time.sleep(3)
    gui.close({'closed': True})
    gui.open({'kind': 'ok', 'heading': 'Signed in', 'text': 'Welcome back.'})


def answers(server):
    """The answers the add-on gave to Kodi's dialogs, in order."""
    path = server.kodi.log_path
    return [json.loads(l)['params'] for l in Path(path).read_text().splitlines() if json.loads(l)['method'] == 'Dialog.Answer'] if path and Path(path).exists() else []


def folders(text):
    """The folders the provider read, in order."""
    return [l.split(' ')[4] if len(l.split(' ')) > 4 and l.split(' ')[4].startswith(('plugin:', '/')) else ''
            for l in text.splitlines() if l.startswith('NEO98 HOST media folder ')]


def stand_in(x):
    """A window of the class Kodi, as Kodi's own window would be, below NEO98's."""
    import ctypes as C

    class Hint(C.Structure):
        _fields_ = [('name', C.c_char_p), ('klass', C.c_char_p)]
    x.x.XCreateSimpleWindow.argtypes = [C.c_void_p, C.c_ulong, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_uint, C.c_ulong, C.c_ulong]
    x.x.XCreateSimpleWindow.restype = C.c_ulong
    x.x.XSetClassHint.argtypes = [C.c_void_p, C.c_ulong, C.POINTER(Hint)]
    x.x.XStoreName.argtypes = [C.c_void_p, C.c_ulong, C.c_char_p]
    x.x.XDestroyWindow.argtypes = [C.c_void_p, C.c_ulong]
    window = x.x.XCreateSimpleWindow(x.display, x.root, 40, 40, 640, 360, 0, 0, 0)
    x.x.XSetClassHint(x.display, window, C.byref(Hint(b'kodi', b'Kodi')))
    x.x.XStoreName(x.display, window, b'Kodi')
    x.x.XMapRaised(x.display, window)
    x.x.XFlush(x.display)
    return window


def screen(out, check):
    """The screen handoff, watched in X11 while the host runs."""
    import time
    from linux_x11_test import X11
    run = out / 'screen'
    shutil.rmtree(run, ignore_errors=True)
    run.mkdir(parents=True)
    server = test_kodi_server.start(run / 'calls.jsonl')
    first = f'{COLUMNS[0]},{ROW}'
    steps = [f'0:wait={CLASS}', f'3:dblclick={first}', f'5:dblclick={COLUMNS[1]},{ROW}', f'7:dblclick={first}', f'9:dblclick={first}',
             f'11.5:dblclick={first}', f'20:dblclick={first}', '28:shot={out}/paused.png', '30:ctrl=d', '45:menu=quit']
    env = dict(os.environ, NEO98_CLIPBOARD='private', NEO98_KEYMAP='US', SDL_AUDIODRIVER='dummy', GDK_SCALE='1', GDK_DPI_SCALE='1',
               NEO98_SETTINGS_FILE=str(run / 'settings.ini'), NEO98_HOST_INPUT=';'.join(steps).replace('{out}', str(run)), NEO98_TEST_KODI=str(server.port))
    for variable in ('SDL_VIDEODRIVER', 'GDK_BACKEND', 'NEO98_GPU', 'NEO98_LOG_TO_FILE', 'NEO98_KODI', 'NEO98_KODI_PORT'):
        env.pop(variable, None)
    x = X11()
    kodi_window = stand_in(x)
    log = (run / 'log.txt').open('w')
    process = subprocess.Popen([str(ROOT / 'build/native-app/neo98-host'), '--drive-c', str(OWN_C), '--registry', str(OWN_C) + '.registry', '--exe',
                                r'C:\WINDOWS\MEDIACTR.EXE', '--time-limit', '60'], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)

    def shown():
        return any(w['title'] == 'NEO98' for w in x.windows(process.pid))

    def until(condition, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline and process.poll() is None:
            if condition():
                return True
            time.sleep(0.05)
        return condition()

    def calls():
        path = run / 'calls.jsonl'
        return [json.loads(l)['method'] for l in path.read_text().splitlines()] if path.exists() else []

    try:
        check('screen: NEO98 shows its window', until(shown, 20))
        check('screen: the window leaves the screen when the episode plays', until(lambda: 'Player.Open' in calls() and not shown(), 25))
        check("screen: and Kodi's window becomes the active one",
              until(lambda: x.property(x.root, '_NET_ACTIVE_WINDOW') == [kodi_window], 5))
        test_kodi_server.call(server.port, 'NEO98Test.Stop')
        check('screen: Kodi stops the video, NEO98 has the screen again', until(shown, 5))
        check('screen: the second play gives Kodi the screen again', until(lambda: calls().count('Player.Open') == 2 and not shown(), 15))
        # Back counts only once the provider has seen the fullscreen video;
        # before that Kodi may still show a dialog of the add-on.
        def polled():
            made = calls()
            return made[len(made) - made[::-1].index('Player.Open'):].count('GUI.GetProperties') >= 2
        until(polled, 10)
        pauses = calls().count('Player.PlayPause')
        test_kodi_server.call(server.port, 'NEO98Test.Back')
        check('screen: Back in Kodi brings NEO98 back', until(shown, 6))
        check('screen: and pauses the video', until(lambda: calls().count('Player.PlayPause') == pauses + 1 and server.kodi.speed == 0, 3))
        check('screen: Show Video gives Kodi the screen and the video goes on',
              until(lambda: not shown() and 'GUI.SetFullscreen' in calls(), 15) and until(lambda: server.kodi.speed == 1, 3))
        test_kodi_server.call(server.port, 'NEO98Test.Quit')
        check('screen: Kodi quits while it plays, NEO98 has the screen again', until(shown, 6))
        process.wait(timeout=40)
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=15)
        log.close()
        server.stop()
        x.x.XDestroyWindow(x.display, kodi_window)
        x.x.XFlush(x.display)
    text = (run / 'log.txt').read_text(errors='replace')
    check('screen: the host says why', 'NEO98 HOST media back from Kodi' in text and 'Kodi was closed. Click Retry' in text)
    check('screen: the host ends cleanly', process.returncode == 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--only', nargs='+', choices=PARTS)
    args = parser.parse_args()
    mac = sys.platform == 'darwin'
    if not mac and not native_app.native_x64():
        raise SystemExit('This test drives the Linux x64 host under X11 or the Mac host.')
    if not args.no_build:
        native_app.build_host(vulkan=not mac, host_media=True)
    pristine = win98_c.use_pristine()
    out = ROOT / 'build/media-center-host'
    out.mkdir(parents=True, exist_ok=True)
    global OWN_C
    OWN_C = out / 'own-c'
    shutil.rmtree(OWN_C, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(pristine), str(OWN_C)] if mac else ['cp', '-a', '--reflink=auto', str(pristine), str(OWN_C)], check=True)
    shutil.copy(win98_c.journal(pristine), str(OWN_C) + '.registry')
    results, failed = {}, []

    def check(name, condition):
        results[name] = bool(condition)
        print(('PASS ' if condition else 'FAIL ') + name, flush=True)
        if not condition:
            failed.append(name)

    wait = f'0:wait={CLASS}'
    # The Mac host does not log its input steps; there the program shows by
    # its first question to the provider. The screen part needs X11; shortcut
    # starts Media Center 98 through START.EXE, and the Mac host does not end when
    # START.EXE does.
    program_open = (lambda text: 'NEO98 HOST media ' in text) if mac else (lambda text: f'NEO98 Linux input wait={CLASS}' in text)
    for part in args.only or [p for p in PARTS if not (mac and p in ('screen', 'shortcut', 'compositor')) and not (not mac and p == 'window')]:
        if part == 'catalog':
            code, text, calls, run = host(out, part, [wait, '4:shot={out}/top.png', '5:menu=quit'], r'C:\WINDOWS\MEDIACTR.EXE', 'netflix')
            check('catalog: the window of Media Center 98 opens', program_open(text))
            check('catalog: it reads Kodi\'s top folder', folders(text)[:1] == [''])
            check('catalog: Netflix and YouTube show their pictures', (run / 'top.png').exists() and red(run / 'top.png') >= 2 * 90 * 90)
            check('catalog: the host ends cleanly', code == 0)
        elif part == 'browse':
            steps = [wait, f'3:dblclick={COLUMNS[0]},{ROW}', f'5:dblclick={COLUMNS[1]},{ROW}', f'7:dblclick={COLUMNS[0]},{ROW}',
                     f'9:dblclick={COLUMNS[0]},{ROW}', '11:shot={out}/season.png', f'11.5:dblclick={COLUMNS[0]},{ROW}', '14:menu=quit']
            code, text, calls, run = host(out, part, steps, r'C:\WINDOWS\MEDIACTR.EXE', 'netflix')
            seen = folders(text)
            check('browse: Netflix, TV Shows, Stranger Things and Season 1 open in turn',
                  seen[1:5] == ['plugin://plugin.video.netflix/', 'plugin://plugin.video.netflix/directory/tvshows/',
                                'plugin://plugin.video.netflix/directory/show/strangerthings/',
                                'plugin://plugin.video.netflix/directory/season/strangerthings/1/'])
            check('browse: the three episodes show their posters', (run / 'season.png').exists() and red(run / 'season.png') >= 3 * 90 * 90)
            opened = [c['params'] for c in calls if c['method'] == 'Player.Open']
            check('browse: a double click plays the first episode in Kodi',
                  opened == [{'item': {'file': 'plugin://plugin.video.netflix/play/80057281'}}])
        elif part == 'endless':
            steps = [wait, f'3:dblclick={COLUMNS[0]},{ROW}', f'5:dblclick={COLUMNS[2]},{ROW}', f'8:click={COLUMNS[0]},{ROW}', '9:key=23',
                     '13:key=23', '17:shot={out}/end.png', '18:menu=quit']
            code, text, calls, run = host(out, part, steps, r'C:\WINDOWS\MEDIACTR.EXE', 'netflix')
            check('endless: at the end of the first page the next one loads',
                  'folder plugin://plugin.video.netflix/directory/long/?page=2&page_token=CAIQAA state=1 entries=30' in text)
            w, h, p = read_png(run / 'end.png') if (run / 'end.png').exists() else (0, 0, b'')
            check('endless: the posters at the end show', sum(1 for i in range(0, len(p), 3) if p[i:i + 3] == b'\x10\xa0\x30') >= 3 * 90 * 90)
        elif part == 'dialogs':
            profiles = (f'0:wait={CLASS};3:dblclick={COLUMNS[0]},{ROW};4:wait=|Who\'s watching?;5:shot={{out}}/select.png;'
                        '5.5:key=28;5.6:key=0d;6:wait=|Continue watching?;6.1:key=0d;7:wait=|Enter your password;7.1:type=hunter2;'
                        '7.2:key=0d;8:wait=|Signing in;8.1:shot={out}/progress.png;9:wait=|Signed in;9.1:key=1b;12:menu=quit').split(';')
            code, text, calls, run = host(out, part, profiles, r'C:\WINDOWS\MEDIACTR.EXE', 'netflix', seconds=90, profiles=True, driver=dialogs)
            given = [c['params'] for c in calls if c['method'] == 'Dialog.Answer']
            check("dialogs: Netflix's choice of profile comes up and Kids is picked", given[:1] == [{'item': 1, 'label': 'Kids'}])
            check('dialogs: and then the Netflix folder loads',
                  'NEO98 HOST media folder plugin://plugin.video.netflix/ state=1 entries=3' in text)
            check('dialogs: Enter answers the question with Yes', given[1:2] == [{'button': 1, 'label': 'Yes'}])
            check('dialogs: the password goes to Kodi', given[2:3] == [{'text': 'hunter2'}])
            # The script's own line of the keys it types aside.
            check('dialogs: and never into the log', not any('hunter2' in l for l in text.splitlines() if not l.startswith(('NEO98 HOST input ', 'NEO98 Linux input '))))
            check('dialogs: the progress goes when Kodi closes it, then Esc cancels the message', given[3:5] == [{'closed': True}, {'cancel': True}])
            check('dialogs: the host ends cleanly', code == 0)
        elif part == 'shortcut':
            drive = out / 'shortcut-c'
            shutil.rmtree(drive, ignore_errors=True)
            subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(pristine), str(drive)] if mac else ['cp', '-a', '--reflink=auto', str(pristine), str(drive)], check=True)
            shutil.copy(win98_c.journal(pristine), str(drive) + '.registry')
            # What an earlier build left: NETFLIX.EXE.
            (drive / 'WINDOWS/NETFLIX.EXE').write_bytes(b'MZ')
            code, text, calls, run = host(out, part, [wait, '5:menu=quit'], r'C:\WINDOWS\COMMAND\START.EXE', 'netflix',
                                          drive=drive, command=f'START "{LINK}"')
            entertainment = drive / 'WINDOWS/Start Menu/Programs/Accessories/Entertainment'
            check('shortcut: the host puts MEDIACTR.EXE into C:\\WINDOWS', (drive / 'WINDOWS/MEDIACTR.EXE').exists())
            check('shortcut: and its shortcut into Programs > Accessories > Entertainment', (entertainment / 'Media Center 98.lnk').exists())
            check('shortcut: NETFLIX.EXE of an earlier build is gone', not (drive / 'WINDOWS/NETFLIX.EXE').exists())
            check('shortcut: the shortcut opens Media Center 98', program_open(text) and folders(text)[:1] == [''])
            # Deleted by the user, a shortcut stays deleted.
            (entertainment / 'Media Center 98.lnk').unlink()
            code, text, calls, run = host(out, part + '-again', ['3:menu=quit'], r'C:\WINDOWS\NOTEPAD.EXE', 'netflix', drive=drive)
            check('shortcut: a deleted shortcut stays deleted', not (entertainment / 'Media Center 98.lnk').exists())
            shutil.rmtree(drive, ignore_errors=True)
            Path(str(drive) + '.registry').unlink(missing_ok=True)
        elif part == 'nonetflix':
            code, text, calls, run = host(out, part, [wait, f'3:dblclick={COLUMNS[0]},{ROW}', '6:shot={out}/missing.png', '7:menu=quit'],
                                          r'C:\WINDOWS\MEDIACTR.EXE', 'plain')
            check('nonetflix: Netflix stands first in the catalogue, and opened it fails with the reason',
                  'folder plugin://plugin.video.netflix/ state=2 entries=0 The Netflix add-on is not installed in Kodi.' in text)
            check('nonetflix: and no picture shows', (run / 'missing.png').exists() and red(run / 'missing.png') == 0)
        elif part == 'screen':
            # The handoff is watched in a real X11 session of the host.
            if not os.environ.get('DISPLAY'):
                print('SKIP screen: needs an X11 display, and DISPLAY is not set', flush=True)
                continue
            screen(out, check)
        elif part == 'window':
            steps = [wait, f'3:dblclick={COLUMNS[0]},{ROW}', f'5:dblclick={COLUMNS[1]},{ROW}', f'7:dblclick={COLUMNS[0]},{ROW}',
                     f'9:dblclick={COLUMNS[0]},{ROW}', f'11.5:dblclick={COLUMNS[0]},{ROW}', '13.5:shot={out}/opening.png',
                     '20:shot={out}/playing.png', '21:click=440,534', '23:shot={out}/seeked.png', '24:dblclick=515,300',
                     '26:shot={out}/full.png', '27:key=1b', '29:shot={out}/back.png', '29.5:click=684,534', '30.5:click=725,534',
                     '32:shot={out}/volume.png', '33:click=172,252', '38:shot={out}/browse.png', '39:click=140,108',
                     '41:shot={out}/nowplaying.png', '42:menu=quit']
            # Kodi's first picture comes 5 s after the start; the last action is at 42 s.
            code, text, calls, run = host(out, part, steps, r'C:\WINDOWS\MEDIACTR.EXE', 'netflix', window=True, start_delay=5, seconds=60)
            if 'NEO98 HOST media screen window' not in text and 'NEO98 HOST media screen kodi' in text:
                print('SKIP window: macOS does not let this process record the screen', flush=True)
                continue
            check('window: the video plays inside the window', 'NEO98 HOST media screen window' in text)
            width, height, pixels = read_png(run / 'playing.png') if (run / 'playing.png').exists() else (0, 0, b'')
            key = sum(1 for i in range(0, len(pixels), 3) if pixels[i:i + 3] == b'\x10\x00\x10')
            check('window: the video area is painted in the key colour', key >= 300 * 200)
            sought = [c['params']['value']['time'] for c in calls if c['method'] == 'Player.Seek' and 'time' in c['params'].get('value', {})]
            at = [t['hours'] * 3600 + t['minutes'] * 60 + t['seconds'] for t in sought]
            check('window: a click on the timeline jumps there (about the middle of 49 minutes)', len(at) >= 1 and 900 <= at[0] <= 2100)
            width, height, pixels = read_png(run / 'full.png') if (run / 'full.png').exists() else (0, 0, b'')
            key = sum(1 for i in range(0, len(pixels), 3) if pixels[i:i + 3] == b'\x10\x00\x10')
            check("window: a double click gives the video Media Center's whole screen, Kodi stays hidden",
                  'NEO98 HOST media full screen on' in text and key >= len(pixels) // 3 * 9 // 10 and 'screen kodi' not in text)
            check('window: Esc goes back to the window', 'NEO98 HOST media full screen off' in text)
            check('window: the speaker button mutes Kodi', {'mute': True} in [c['params'] for c in calls if c['method'] == 'Application.SetMute'])
            volumes = [c['params']['volume'] for c in calls if c['method'] == 'Application.SetVolume']
            check('window: a click on the volume slider sets a low volume there', bool(volumes) and volumes[-1] <= 40)
            def colour(name, rgb):
                w, h, p = read_png(run / name) if (run / name).exists() else (0, 0, b'')
                return sum(1 for i in range(0, len(p), 3) if p[i:i + 3] == rgb)
            check('window: a folder chosen while the video plays shows its posters, the video waits',
                  colour('browse.png', b'\x10\x00\x10') == 0 and colour('browse.png', b'\x10\xa0\x30') >= 3 * 90 * 90)
            check('window: Now Playing brings the video back', colour('nowplaying.png', b'\x10\x00\x10') >= 300 * 200)
            width, height, pixels = read_png(run / 'opening.png') if (run / 'opening.png').exists() else (0, 0, b'')
            face = sum(1 for i in range(0, len(pixels), 3) if pixels[i:i + 3] == b'\xc0\xc0\xc0')
            check("window: until Kodi's first picture the area shows Windows' animation, not Kodi",
                  pixels and not any(pixels[i:i + 3] == b'\x10\x00\x10' for i in range(0, len(pixels), 3)) and face >= 300 * 200
                  and 'NEO98 HOST media picture after ' in text)
        elif part == 'compositor':
            from test_compositor_host import build_client
            build_client()
            steps = [wait, f'3:dblclick={COLUMNS[0]},{ROW}', f'5:dblclick={COLUMNS[1]},{ROW}', f'7:dblclick={COLUMNS[0]},{ROW}',
                     f'9:dblclick={COLUMNS[0]},{ROW}', f'11:dblclick={COLUMNS[0]},{ROW}', '16:hostshot={out}/window.png',
                     '16.5:click=161,87', '17.5:hostshot={out}/menu.png', '18:key=1b', '18.5:key=1b', '19:dblclick=470,300',
                     '21:hostshot={out}/whole.png', '22:key=1b', '23.5:hostshot={out}/back.png', '24:ctrl=53',
                     '26:hostshot={out}/stopped.png', '27:menu=quit']
            code, text, calls, run = host(out, part, steps, r'C:\WINDOWS\MEDIACTR.EXE', 'program', window=True)
            check('compositor: the host starts Kodi under its compositor',
                  "starting Kodi: " in text and "under NEO98's compositor" in text and 'compositor client connected' in text)
            check('compositor: the video plays inside the window', 'NEO98 HOST media screen window' in text and 'screen kodi' not in text)

            def video(name, box=None):
                """(count, left, top, right, bottom) of the test client's colour in a hostshot, in guest pixels, and the shot's scale."""
                if not (run / name).exists():
                    return (0, 0, 0, 0, 0), 1
                width, height, pixels = read_png(run / name)
                scale = width / 800
                x0, y0, x1, y1 = [int(v * scale) for v in box] if box else (0, 0, width, height)
                count, left, top, right, bottom = 0, width, height, -1, -1
                for y in range(y0, y1):
                    row = y * width * 3
                    for x in range(x0, x1):
                        if pixels[row + x * 3:row + x * 3 + 3] == b'\x10\xc0\xc0':
                            count += 1
                            left, top, right, bottom = min(left, x), min(top, y), max(right, x), max(bottom, y)
                return (count, left / scale, top / scale, right / scale, bottom / scale), scale
            (count, left, top, right, bottom), scale = video('window.png')
            check(f'compositor: the video shows in the video area only ({count} pixels, {left:.0f},{top:.0f} to {right:.0f},{bottom:.0f})',
                  count > 100000 * scale * scale / 1.44 and count < 800 * 600 * scale * scale / 2 and left > 150)
            check('compositor: the tree beside it shows no video', video('window.png', (10, 100, left - 140, 400))[0][0] == 0)
            under = video('menu.png', (left + 2, top + 4, left + 20, top + 14))[0][0]
            beside = video('menu.png', (left + 250, top + 4, left + 270, top + 14))[0][0]
            check(f'compositor: an open menu covers the video ({under} under it, {beside} beside it)', under == 0 and beside > 50)
            # The test video is 16:9: the whole width of the 4:3 window, bars above and below.
            (whole, w_left, _, w_right, _), scale = video('whole.png')
            check(f'compositor: a double click gives the video the whole window ({whole} pixels, {w_left:.0f} to {w_right:.0f})',
                  'NEO98 HOST media full screen on' in text and w_left <= 1 and w_right >= 798 and whole >= 800 * 450 * scale * scale * 0.95)
            back = video('back.png')[0]
            check(f'compositor: Esc goes back to the window ({back[1]:.0f},{back[2]:.0f})',
                  'NEO98 HOST media full screen off' in text and abs(back[1] - left) <= 2 and abs(back[2] - top) <= 2)
            check('compositor: Stop shows the list again', (run / 'stopped.png').exists() and video('stopped.png')[0][0] == 0)
            rates = sorted(int(l.split('=')[1]) for l in text.splitlines() if l.startswith('NEO98 HOST video fps='))
            median = rates[len(rates) // 2] if rates else 0
            check(f"compositor: Kodi's pictures reach the window at least 57 times a second ({median} in the middle of {rates})", median >= 57)
            check('compositor: the host ends cleanly and Kodi with it', code == 0 and 'compositor client left' in text)
        elif part == 'nokodi':
            code, text, calls, run = host(out, part, [wait, '4:menu=quit'], r'C:\WINDOWS\MEDIACTR.EXE', 'none')
            check('nokodi: the program opens and the host says Kodi is missing',
                  program_open(text) and 'NEO98 HOST media state=1 Kodi is not installed on this ' + ('Mac.' if mac else 'computer.') in text)
    assert not win98_c.pristine_problems(pristine), 'The test changed the pristine C:'
    (out / 'results.json').write_text(json.dumps(results, indent=1) + '\n')
    if failed:
        raise SystemExit(f'MEDIA CENTER FAIL: {len(failed)} of {len(results)}, see {out}')
    print(f'MEDIA CENTER PASS: {len(results)} checks')


if __name__ == '__main__':
    main()
