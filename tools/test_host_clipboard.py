#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The clipboard between the Mac and the guest, both ways, text and files.

build/native-app/neo98-host runs Explorer from a clone of the pristine Win98
folder under SDL's dummy video and audio drivers. Its pasteboard is a private
one (NEO98_HOST_PASTEBOARD=unique, NSPasteboard pasteboardWithUniqueName): the
clipboard of the person at this Mac is neither read nor written. The steps
pbtext=, pbfiles= and pbdump= of NEO98_HOST_INPUT copy onto that pasteboard as
another Mac program would and write out what it holds.

1. Mac to guest, text: a guest program (tools/host_clipboard_fixture.c) finds
   CF_TEXT, CF_UNICODETEXT, CF_OEMTEXT and CF_LOCALE; the text is Windows-1252
   with CR LF, and a character the page lacks is narrowed as Win98 narrows it.
2. Mac to guest, files: CF_HDROP with guest paths on a drive mounted for the
   folder, "Preferred DropEffect" 5.
3. Guest to Mac, text: CF_TEXT, CF_UNICODETEXT alone, and a format set with a
   null handle that the program renders on WM_RENDERFORMAT.
4. Guest to Mac, files: CF_HDROP with long names and 8.3 aliases becomes file
   URLs of the Mac files behind drive C:; a cut is offered as a copy.
5. Notepad: typed text copied with Edit > Kopieren (Cmd+C) reaches the
   pasteboard, text of the Mac pasted with Edit > Paste (Cmd+V)
   reaches the edit control; the Command key held meanwhile opens no Start
   menu.
6. Explorer: files copied on the Mac are pasted into a folder window and
   arrive in the folder; files copied in the folder window become file URLs.
8. Pictures: a PNG of the Mac is CF_DIB in the guest (24 bits, bottom up); a
   CF_DIB of the guest with a colour table is PNG on the pasteboard.
7. DOS window: Paste of the toolbar types the Mac's text into a DOS program
   that reads the keyboard through DOS; Mark and Copy put the text of the DOS
   screen on the pasteboard.

    build/analysis-venv/bin/python tools/test_host_clipboard.py [--no-build]

Results in build/host-clipboard/results.json. docs/BUILDING.md.
"""
from __future__ import annotations

import json
import struct
import zlib
import unicodedata
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
import win98_c  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
from test_mac_app_host import clone_machine, run_host  # noqa: E402

IMPORTS = {
    'KERNEL32': {'ExitProcess': 4, 'GetModuleFileNameA': 12, 'CreateFileA': 28, 'ReadFile': 20, 'WriteFile': 20,
                 'CloseHandle': 4, 'GlobalAlloc': 8, 'GlobalLock': 4, 'GlobalUnlock': 4, 'GlobalSize': 4,
                 'GetModuleHandleA': 4},
    'USER32': {'OpenClipboard': 4, 'CloseClipboard': 0, 'EmptyClipboard': 0, 'GetClipboardData': 4,
               'SetClipboardData': 8, 'EnumClipboardFormats': 4, 'RegisterClipboardFormatA': 4, 'RegisterClassA': 4,
               'CreateWindowExA': 48, 'GetMessageA': 16, 'DispatchMessageA': 4, 'DefWindowProcA': 16, 'SetTimer': 16,
               'PostQuitMessage': 4},
}
MAC_TEXT = 'Grüße vom Mac – 5 €\nzweite Zeile\rdritte\r\nĀ 日本'
# Windows-1252 of the guest: CR LF line ends; U+0100 has a best fit (A), the
# two ideographs none (the default character).
MAC_TEXT_ANSI = 'Grüße vom Mac – 5 €\r\nzweite Zeile\r\ndritte\r\n'.encode('cp1252') + b'A ??'
MAC_TEXT_WIDE = 'Grüße vom Mac – 5 €\r\nzweite Zeile\r\ndritte\r\nĀ 日本'.encode('utf-16-le')
GUEST_TEXT = 'Grüße aus Win98 – 5 €\r\nZeile 2'
GUEST_WIDE = 'Ωmega 日本\r\nx'
LATE_TEXT = 'spät gerendert'
TYPED = 'Hallo Mac'
PASTED = 'Vom Mac eingefügt: äöü ß €'
DOS_TEXT = 'HALLO DOS 123'


def png(width, height, pixels):
    """A PNG file of 8-bit RGB; `pixels` are (r, g, b) from the top left."""
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + b''.join(bytes(p) for p in pixels[y * width:(y + 1) * width]) for y in range(height))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
            + chunk(b'sRGB', b'\0') + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def close(a, b):
    return len(a) == len(b) and all(abs(x - y) <= 2 for p, q in zip(a, b) for x, y in zip(p, q))


def dump_image(path):
    """The picture pbdump= wrote: (width, height, [(r, g, b),...]) or None."""
    if path.is_file():
        for line in path.read_text().splitlines():
            parts = line.split()
            if parts and parts[0] == 'image':
                return int(parts[1]), int(parts[2]), [tuple(bytes.fromhex(v)) for v in parts[3:]]
    return None


def build_fixture(out):
    """CLIPTEST.EXE from tools/host_clipboard_fixture.c, a Win32 GUI program
    without a C runtime."""
    environment = neo98.tool_environment()
    work = out / 'fixture'
    work.mkdir(exist_ok=True)
    libraries = []
    for dll, items in IMPORTS.items():
        definition = work / f'{dll}.def'
        definition.write_text(f'LIBRARY {dll}.DLL\nEXPORTS\n' + '\n'.join(f'{n}@{c}' for n, c in items.items()) + '\n')
        library = definition.with_suffix('.lib')
        subprocess.run(['llvm-dlltool', '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)],
                       env=environment, check=True)
        libraries.append(library)
    obj = work / 'fixture.obj'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-stack-protector',
                    '-fno-builtin', '-c', str(ROOT / 'tools/host_clipboard_fixture.c'), '-o', str(obj)],
                   env=environment, check=True)
    exe = work / 'CLIPTEST.EXE'
    subprocess.run(['lld-link', '/machine:x86', '/entry:fixture_entry', '/subsystem:windows,4.0', '/nodefaultlib',
                    '/fixed', '/safeseh:no', '/timestamp:0', str(obj), *[str(x) for x in libraries],
                    '/out:' + str(exe)], env=environment, check=True)
    return exe


def dump(path):
    """What pbdump= wrote: the text and the files of the pasteboard."""
    text, files = None, []
    if path.is_file():
        for line in path.read_text().splitlines():
            kind, _, value = line.partition(' ')
            if kind == 'text':
                text = bytes.fromhex(value).decode('utf-8')
            elif kind == 'file':
                files.append(unicodedata.normalize('NFC', value))
    return text, files


def fixture_output(path):
    lines = {}
    if path.is_file():
        for line in path.read_bytes().decode('latin-1').splitlines():
            key, _, value = line.partition(' ')
            lines.setdefault(key, []).append(value)
    return lines


def main():
    out = neo98.output_directory('host-clipboard')
    for name in ('settings', 'logs', 'tmp', 'prog', 'mac', 'ziel', 'fixture'):
        shutil.rmtree(out / name, ignore_errors=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    exe = build_fixture(out)
    machine = clone_machine(out / 'machine')
    settings_dir, logs = out / 'settings', out / 'logs'
    settings_dir.mkdir()
    (out / 'tmp').mkdir()
    (settings_dir / 'settings.ini').write_text(f'# Test\nmachine = {machine}\nscale = 1\n')
    prog, mac, ziel = out / 'prog', out / 'mac', out / 'ziel'
    for folder in (prog, mac / 'Ordner mit Inhalt', ziel):
        folder.mkdir(parents=True)
    for name in ('CLIPGET1', 'CLIPGET2', 'CLIPGET3', 'CLIPSET', 'CLIPUNI', 'CLIPDIB', 'CLIPLATE', 'CLIPDROP', 'CLIPCUT'):
        shutil.copyfile(exe, prog / f'{name}.EXE')
    (prog / 'CLIPSET.IN').write_bytes(GUEST_TEXT.encode('cp1252'))
    (prog / 'CLIPUNI.IN').write_bytes(GUEST_WIDE.encode('utf-16-le'))
    (prog / 'CLIPLATE.IN').write_bytes(LATE_TEXT.encode('cp1252'))
    guest_files = 'C:\\WINDOWS\\NOTEPAD.EXE\r\nC:\\PROGRA~1\\ACCESS~1\\WORDPAD.EXE\r\nC:\\NICHTDA.TXT\r\n'
    (prog / 'CLIPDROP.IN').write_bytes(guest_files.encode('cp1252'))
    (prog / 'CLIPCUT.IN').write_bytes(b'C:\\WINDOWS\\WIN.INI\r\n')
    letter_file = mac / 'Brief an Müller.txt'
    letter_file.write_bytes('Sehr geehrter Herr Müller\r\n'.encode('cp1252'))
    (mac / 'Ordner mit Inhalt' / 'innen.txt').write_bytes(b'innen\r\n')
    (out / 'mac-text.txt').write_text(MAC_TEXT, encoding='utf-8', newline='')
    (out / 'mac-paste.txt').write_text(PASTED, encoding='utf-8', newline='')
    (out / 'mac-dos.txt').write_text(DOS_TEXT, encoding='utf-8', newline='')
    # Pictures. The Mac's: 3x2, a different colour each. The guest's: 3x2 with
    # a colour table of four, bottom row first, rows padded to four bytes.
    mac_pixels = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255), (0, 0, 0), (18, 52, 86)]
    (out / 'mac-image.png').write_bytes(png(3, 2, mac_pixels))
    table = [(0, 0, 0), (255, 0, 0), (0, 128, 255), (255, 255, 0)]
    guest_dib = (struct.pack('<IiiHHIIiiII', 40, 3, 2, 1, 8, 0, 8, 0, 0, 4, 0)
                 + b''.join(bytes((b, g, r, 0)) for r, g, b in table) + bytes((3, 2, 1, 0)) + bytes((1, 2, 3, 0)))
    guest_pixels = [table[1], table[2], table[3], table[3], table[2], table[1]]
    (prog / 'CLIPDIB.IN').write_bytes(guest_dib)
    # A DOS program that echoes its keys until Esc: mov ah,1; int 21h; cmp al,27; jne; ret.
    keys = machine / 'C' / 'KEYS.COM'
    keys.write_bytes(bytes.fromhex('b401cd213c1b75f8c3'))
    (out / 'mac-files.txt').write_text(f'{letter_file}\n{mac / "Ordner mit Inhalt"}\n', encoding='utf-8')
    notepad = win98_c.path('C:\\WINDOWS\\NOTEPAD.EXE', machine / 'C')
    dumps = {name: out / f'dump-{name}.txt' for name in ('set', 'uni', 'late', 'drop', 'cut', 'notepad', 'round',
                                                         'explorer', 'explorer-cut', 'dos', 'dib')}
    for path in dumps.values():
        path.unlink(missing_ok=True)

    steps, clock = [], [0]

    def step(action, after=0):
        clock[0] += after
        steps.append(f'{clock[0]}:{action}')

    def settle(milliseconds=700, after=2):
        step(f'idle={milliseconds}', after)

    def wait(target):
        steps.append(f'0:wait={target}')
        clock[0] = 0

    def run(name, after=1):
        step(f'open={prog / (name + ".EXE")}', after)
        settle()

    wait('Shell_TrayWnd')
    step('idle=1500', 1)
    step('menu=describe', 1)
    # 7. The DOS window, first: it is the first window of the session, so its
    # frame is at 64,55 with a border of 3 and a caption of 19. The strip of
    # buttons is 28 high, Mark, Copy and Paste are 23 wide from 74 on; the
    # text screen below is 80x25 cells of 12x16.
    left, top = 64 + 3, 55 + 3 + 19
    step(f'open={keys}', 1)
    wait('|KEYS')
    step('idle=1000', 1)
    step('tops=0', 1)
    step(f'pbtext={out / "mac-dos.txt"}', 1)
    step(f'click={left + 74 + 2 * 23 + 11},{top + 14}', 2)     # Paste
    settle(700, 2)
    step(f'click={left + 74 + 11},{top + 14}', 1)              # Mark
    step(f'down={left + 2},{top + 28 + 2}', 1)
    step(f'move={left + 500},{top + 28 + 200}', 1)
    step(f'move={left + 957},{top + 28 + 397}', 1)
    step(f'up={left + 957},{top + 28 + 397}', 1)
    step(f'click={left + 74 + 23 + 11},{top + 14}', 1)         # Copy
    settle(500, 1)
    step(f'pbdump={dumps["dos"]}', 1)
    # 1. Mac to guest, text.
    step(f'pbtext={out / "mac-text.txt"}', 1)
    run('CLIPGET1')
    # 2. Mac to guest, files.
    step(f'pbfiles={out / "mac-files.txt"}', 1)
    run('CLIPGET2')
    # 3. Guest to Mac, text.
    run('CLIPSET')
    step(f'pbdump={dumps["set"]}', 1)
    run('CLIPUNI')
    step(f'pbdump={dumps["uni"]}', 1)
    step(f'open={prog / "CLIPLATE.EXE"}', 1)
    settle(700, 5)
    step(f'pbdump={dumps["late"]}', 1)
    # 4. Guest to Mac, files.
    run('CLIPDROP')
    step(f'pbdump={dumps["drop"]}', 1)
    run('CLIPCUT')
    step(f'pbdump={dumps["cut"]}', 1)
    # 8. Pictures.
    step(f'pbimage={out / "mac-image.png"}', 1)
    run('CLIPGET3')
    run('CLIPDIB')
    step(f'pbdump={dumps["dib"]}', 1)
    # 5. Notepad. Alt+E, A is Edit > Select All.
    step(f'open={notepad}', 1)
    wait('Notepad')
    step('idle=700', 1)
    step(f'type={TYPED}', 1)
    select_all = ['keydown=12', 'key=45', 'keyup=12', 'key=41']
    for key in select_all:
        step(key, 1)
    step('cmd=1', 1)
    step('menu=shortcut=cmd+c', 1)   # the key itself, matched by AppKit against the menu
    step('cmd=0', 1)
    settle(500, 1)
    step('tops=1', 1)
    step(f'pbdump={dumps["notepad"]}', 1)
    step(f'pbtext={out / "mac-paste.txt"}', 1)
    step('menu=edit-paste', 2)   # the selection is replaced
    settle(500, 1)
    for key in select_all:
        step(key, 1)
    step('menu=edit-cut', 1)
    settle(500, 1)
    step(f'pbdump={dumps["round"]}', 1)
    # 6. Explorer: paste the Mac's files into a folder window, copy them there.
    step(f'pbfiles={out / "mac-files.txt"}', 1)
    step(f'open={ziel}', 1)
    wait('CabinetWClass')
    step('idle=1000', 1)
    step('menu=edit-paste', 1)
    settle(1500, 3)
    for key in ('keydown=11', 'key=41', 'keyup=11'):   # Ctrl+A
        step(key, 1)
    step('menu=edit-copy', 1)
    settle(700, 2)
    step(f'pbdump={dumps["explorer"]}', 1)
    step('menu=edit-cut', 1)
    settle(700, 2)
    step(f'pbdump={dumps["explorer-cut"]}', 1)
    step('menu=quit', 1)

    environment = {'HOME': str(Path.home()), 'SDL_VIDEODRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy',
                   'NEO98_SETTINGS_DIR': str(settings_dir), 'NEO98_LOG_DIR': str(logs), 'TMPDIR': str(out / 'tmp'),
                   'NEO98_HOST_SCREEN': '2560x1600', 'NEO98_HOST_PASTEBOARD': 'unique',
                   'NEO98_HOST_INPUT': ';'.join(steps)}
    code, log = run_host([str(binary)], environment, out / 'host.log', timeout=900)
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)

    board = re.search(r'^NEO98 HOST clipboard pasteboard=(.*)$', log, re.M)
    check('the pasteboard is a private one, not the Mac\'s', board and board[1] and 'general' not in board[1].lower())
    waits = re.findall(r'^NEO98 HOST input wait=(.*) (reached|timed out)', log, re.M)
    check('every wait was reached', len(waits) == 4 and all(state == 'reached' for _, state in waits))
    check('every pasteboard step ran', 'refused' not in ''.join(re.findall(r'^NEO98 HOST input pb.*$', log, re.M))
          and len(re.findall(r'^NEO98 HOST input pb', log, re.M)) == 17)
    check('menu has Edit with Cmd+X, Cmd+C, Cmd+V',
          all(f'Edit > {title} [{name}] cmd+{key}' in log for title, name, key in
              (('Cut', 'edit-cut', 'x'), ('Copy', 'edit-copy', 'c'), ('Paste', 'edit-paste', 'v'))))

    # 1. Mac to guest, text.
    got = fixture_output(prog / 'CLIPGET1.OUT')
    formats = got.get('formats', [''])[0].split()
    check('Mac text: CF_TEXT, CF_UNICODETEXT, CF_OEMTEXT and CF_LOCALE are on the clipboard',
          {'1', '13', '7', '16'} <= set(formats) and '15' not in formats)
    check('Mac text: CF_TEXT is Windows-1252 with CR LF, best fit and default character',
          got.get('text') == [MAC_TEXT_ANSI.hex()])
    check('Mac text: CF_UNICODETEXT keeps every character', got.get('unicode') == [MAC_TEXT_WIDE.hex()])
    oem = bytes.fromhex(got.get('oem', [''])[0])
    check('Mac text: CF_OEMTEXT is the OEM page of the same text',
          oem.startswith(b'Gr\x81\xe1e vom Mac') and len(oem) == len(MAC_TEXT_ANSI))
    check('Mac text: CF_LOCALE', len(got.get('locale', [''])[0]) == 8)
    check('Mac text: fetched when the program read it',
          re.search(r'HOST CLIPBOARD offer taken kinds=1 .*\n(?:.*\n)*?.*HOST CLIPBOARD render format=1 ', log))

    # 2. Mac to guest, files.
    got = fixture_output(prog / 'CLIPGET2.OUT')
    formats = got.get('formats', [''])[0].split()
    mounted = re.search(r'^NEO98 HOST medium ([D-Z]): fixed ' + re.escape(str(mac)) + '$', log, re.M)
    letter = mounted[1] if mounted else '?'
    check('Mac files: CF_HDROP and Preferred DropEffect are on the clipboard', {'15', 'effect'} <= set(formats))
    check('Mac files: the folder of the files is a drive of the guest', mounted)
    names = [bytes.fromhex(value).decode('cp1252') for value in got.get('drop', [])]
    check('Mac files: CF_HDROP names the guest paths, ANSI',
          got.get('dropfiles') == ['wide=0']
          and names == [f'{letter}:\\Brief an Müller.txt', f'{letter}:\\Ordner mit Inhalt'])
    check('Mac files: Preferred DropEffect is copy and link, as Explorer\'s Copy', got.get('effect') == ['5'])
    check('Mac files: the names are the text', '1' in formats
          and bytes.fromhex(got.get('text', [''])[0]).decode('cp1252') == 'Brief an Müller.txt\r\nOrdner mit Inhalt')

    # 3. Guest to Mac, text.
    check('guest CF_TEXT is the string of the pasteboard', dump(dumps['set'])[0] == GUEST_TEXT.replace('\r\n', '\n'))
    check('guest CF_UNICODETEXT alone is the string of the pasteboard',
          dump(dumps['uni'])[0] == GUEST_WIDE.replace('\r\n', '\n'))
    late = fixture_output(prog / 'CLIPLATE.OUT')
    check('a format set with a null handle is rendered on WM_RENDERFORMAT',
          'offered' in late and late.get('render') == ['1'] and dump(dumps['late'])[0] == LATE_TEXT)

    # 4. Guest to Mac, files.
    c = machine / 'C'
    text, files = dump(dumps['drop'])
    check('guest CF_HDROP: file URLs of the Mac files, 8.3 aliases resolved, a missing file left out',
          files == [str(c / 'WINDOWS/NOTEPAD.EXE'), str(c / 'Program Files/Accessories/WORDPAD.EXE')])
    check('guest CF_HDROP: a file not on this Mac is named in the log', 'C:\\NICHTDA.TXT -> (not on this Mac)' in log)
    text, files = dump(dumps['cut'])
    check('guest cut: offered to the Mac as a copy', files == [str(c / 'WINDOWS/WIN.INI')]
          and re.search(r'^NEO98 HOST clipboard to mac text=0 files=1 effect=2 ', log, re.M)
          and (c / 'WINDOWS/WIN.INI').is_file())

    # 5. Notepad.
    check('Notepad: Cmd+C puts the typed text on the pasteboard', dump(dumps['notepad'])[0] == TYPED)
    check('Notepad: Cmd+V pastes the Mac\'s text, Cmd+X cuts it back out', dump(dumps['round'])[0] == PASTED
          and len(re.findall(r'^NEO98 HOST clipboard to mac text=[1-9]', log, re.M)) == 6)
    check('the edit keys reached the guest as Ctrl+C, Ctrl+V, Ctrl+X',
          [m for m in re.findall(r'^NEO98 HOST edit key ctrl\+(.)$', log, re.M)] == ['c', 'v', 'x', 'v', 'c', 'x'])
    tops = re.findall(r'^NEO98 HOST tops foreground=.*$(?:\n^NEO98 HOST (?:top|child) .*$)*', log, re.M)
    check('the Command key held during Cmd+C opens no Start menu',
          len(tops) == 2 and 'class=Notepad' in tops[1] and not re.search(r'visible=1 .* class=BaseBar', tops[1]))

    # 6. Explorer.
    pasted = ziel / letter_file.name
    check('Explorer: Paste copies the Mac\'s file into the folder',
          pasted.is_file() and pasted.read_bytes() == letter_file.read_bytes() and letter_file.is_file())
    check('Explorer: Paste copies the Mac\'s folder with its content',
          (ziel / 'Ordner mit Inhalt' / 'innen.txt').is_file())
    text, files = dump(dumps['explorer'])
    check('Explorer: Kopieren puts the files of the folder window on the pasteboard as file URLs',
          sorted(files) == sorted([str(pasted), str(ziel / 'Ordner mit Inhalt')]))
    text, files = dump(dumps['explorer-cut'])
    check('Explorer: Ausschneiden offers the same files as a copy and removes nothing',
          sorted(files) == sorted([str(pasted), str(ziel / 'Ordner mit Inhalt')]) and pasted.is_file())

    # 8. Pictures.
    got = fixture_output(prog / 'CLIPGET3.OUT')
    formats = got.get('formats', [''])[0].split()
    expected = struct.pack('<IiiHHIIiiII', 40, 3, 2, 1, 24, 0, 24, 3780, 3780, 0, 0) + b''.join(
        b''.join(bytes((b, g, r)) for r, g, b in mac_pixels[y * 3:y * 3 + 3]) + b'\0\0\0' for y in (1, 0))
    found = bytes.fromhex(got.get('dib', [''])[0])
    check('Mac picture: CF_DIB and the CF_BITMAP USER offers with it are on the clipboard', {'8', '2'} <= set(formats))
    check('Mac picture: CF_DIB is 24 bits a pixel, bottom row first, the colours of the PNG',
          found[:40] == expected[:40] and len(found) == len(expected)
          and all(abs(x - y) <= 2 for x, y in zip(found[40:], expected[40:])))
    image = dump_image(dumps['dib'])
    check('guest CF_DIB with a colour table is the picture of the pasteboard',
          image and image[:2] == (3, 2) and close(image[2], guest_pixels))

    # 7. DOS window.
    frame = re.search(r'^NEO98 HOST top id=\w+ visible=1 frame=64,55,1030,508 .* class=tty title=KEYS', log, re.M)
    check('DOS window: the frame the clicks assume', frame)
    check('DOS window: Paste, Mark and Copy of the toolbar were hit',
          re.findall(r'DOSVM toolbar button=(\w+)', log) == ['Paste', 'Mark', 'Copy'])
    check('DOS window: Paste types the Mac\'s text, Mark and Copy put the screen on the pasteboard',
          DOS_TEXT in (dump(dumps['dos'])[0] or ''))

    echoes = len(re.findall(r'^NEO98 HOST clipboard from mac ', log, re.M))
    check('the host does not offer its own writes back to the guest', echoes == 7)
    check('host ended with exit code 0', code == 0)
    check('no host failure', 'NEO98 HOST failed' not in log and 'libc++abi' not in log)

    failed = [name for name, okay in checks.items() if not okay]
    (out / 'results.json').write_text(json.dumps({'status': 'FAIL' if failed else 'PASS', 'checks': checks},
                                                 indent=2, ensure_ascii=False) + '\n')
    for name, okay in checks.items():
        print(('PASS ' if okay else 'FAIL ') + name)
    for name in ('settings', 'tmp', 'fixture'):
        shutil.rmtree(out / name, ignore_errors=True)
    if not failed:
        shutil.rmtree(out / 'machine', ignore_errors=True)
    print('PASS host clipboard' if not failed else f'FAIL host clipboard: {len(failed)} checks')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
