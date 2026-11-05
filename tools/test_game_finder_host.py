#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Game machines from the Mac side.

Everything runs under SDL's dummy video and audio drivers: no window, no
sound, no entry in LaunchServices, nothing on the desktop of the person. The
machine folder is a clone of the pristine drive C:, settings and logs lie
under build/game-finder. The dialogs are answered by the test switch
game-machine-answers (tools/host_game_menu.h).

1. The bundle: Info.plist declares the service "Open as game machine" for
   the program files, with types the same plist declares; the program holds
   the method the service names; plutil accepts the plist. A game machine
   made and started with the bundle is a program of its launcher bundle: the
   link in it leads to the host of the bundle, and macOS knows the process
   by the name of the game.
2. A program opened from the Finder (the step open=) is asked about once.
   "Normally" is remembered in settings.ini and not asked again; a name of an
   installer and a program of Windows are not asked about.
3. "As a game machine" makes one and starts it, for a program outside the
   drives with its folder as a drive of the game machine. The next double
   click starts that game machine without a question.
4. A second game machine for the same program: the double click offers the
   choice and guesses nothing.
5. The entry of the Finder (game-machine-open): a new game machine, then the
   existing one; a file that is no program is refused.
6. The checkbox switches the question off for every program, the menu
   switches it on again; both stand in settings.ini.
7. An alias of the launcher in a folder the test names.
8. A second start hands a file and a game request to the running host.
9. No host runs :
   --game-machine-open for a program with a game machine, without one (the
   name answered), with two (the choice answered) and cancelled; a file of
   the launch (NEO98_LAUNCH_OPEN, what a double click in the Finder sends)
   for a program with a game machine and for one answered "game", "cancel"
   and "normal". The start becomes the game machine: the process list holds
   one host, under the link of the launcher bundle, and no normal machine
   booted. Only "normal" boots it.
10. The icon of a program file is the icon of its game machine from the
    start.

    build/analysis-venv/bin/python tools/test_game_finder_host.py [--no-build] [--no-app]

Results in build/game-finder/results.json.
"""
from __future__ import annotations

import json
import os
import plistlib
import re
import shutil
import subprocess
import sys
import time
import unicodedata
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
import mac_app  # noqa: E402
import win98_c  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
from test_game_machine_host import _wait_file, build_guest, values  # noqa: E402
from test_mac_app_host import clone_machine  # noqa: E402
from test_single_instance_host import locked, wait_for  # noqa: E402


def stored(path):
    """name -> list of values of a settings.ini."""
    found = {}
    if path.is_file():
        for line in path.read_text().splitlines():
            if ' = ' in line and not line.startswith('#'):
                name, value = line.split(' = ', 1)
                found.setdefault(name, []).append(value)
    return found


def main():
    out = neo98.output_directory('game-finder')
    for name in ('settings', 'logs', 'tmp', 'machine', 'outside', 'desk', 'app-settings', 'app-logs', 'app-machine'):
        shutil.rmtree(out / name, ignore_errors=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    fixture = build_guest(out, neo98.tool_environment())
    machine = clone_machine(out / 'machine')
    drive_c = machine / 'C'
    games = machine / 'Spielmaschinen'
    (drive_c / 'SPIELE').mkdir()
    for name in ('GMGAME.EXE', 'SETUP.EXE', 'DIENST.EXE', 'ZWEI.EXE', 'DREI.EXE', 'SPAET.EXE', 'KALT.EXE', 'FRAGE.EXE',
                 'NEIN.EXE', 'NORM.EXE'):
        shutil.copyfile(fixture, drive_c / 'SPIELE' / name)
    (drive_c / 'SPIELE' / 'LIES.TXT').write_text('not a program\r\n')
    outside = out / 'outside' / 'Kroko Mania'
    outside.mkdir(parents=True)
    kroko = outside / 'KROKO.EXE'
    shutil.copyfile(fixture, kroko)
    desk = out / 'desk'
    desk.mkdir()
    settings_dir, logs = out / 'settings', out / 'logs'
    settings_dir.mkdir()
    (out / 'tmp').mkdir()
    settings_file = settings_dir / 'settings.ini'
    settings_file.write_text(f'# Test\nmachine = {machine}\nscale = 1\n')
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)
        if not condition:
            print(f'FAIL {name}', flush=True)

    base = {'HOME': str(Path.home()), 'SDL_VIDEODRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy',
            'NEO98_SETTINGS_DIR': str(settings_dir), 'NEO98_LOG_DIR': str(logs), 'TMPDIR': str(out / 'tmp'),
            'NEO98_HOST_SCREEN': '1100x700', 'NEO98_HOST_DEADLINE': '600'}
    command_log = out / 'commands.log'
    command_log.write_text('')

    def host(arguments, environment=None, timeout=300, program=None):
        try:
            result = subprocess.run([str(program or binary), *arguments], cwd='/', env=dict(base, **(environment or {})),
                                    capture_output=True, text=True, errors='replace', stdin=subprocess.DEVNULL,
                                    timeout=timeout)
            code, text = result.returncode, result.stdout + result.stderr
        except subprocess.TimeoutExpired as late:
            code, text = 'timeout', (late.stdout or b'').decode(errors='replace') if isinstance(late.stdout, bytes) else str(late.stdout or '')
        with command_log.open('a') as log:
            log.write(f'$ {" ".join(map(str, arguments))}\n{text}exit {code}\n\n')
        return code, text

    def game_log(name):
        return logs / 'Spielmaschinen' / name / 'host-session.log'

    # --- 1. The bundle ---------------------------------------------------------
    info = mac_app.info_plist('0.1', 'test')
    services = info.get('NSServices', [])
    service = services[0] if services else {}
    declared = ({kind['UTTypeIdentifier'] for kind in info['UTImportedTypeDeclarations']}
                | {'com.microsoft.windows-executable'})
    opened = {kind for document in info['CFBundleDocumentTypes'] for kind in document['LSItemContentTypes']}
    check('Info.plist declares one service, "Open as game machine"',
          len(services) == 1 and service.get('NSMenuItem') == {'default': 'Open as game machine'}
          and service.get('NSMessage') == 'openAsGameMachine' and service.get('NSPortName') == info['CFBundleName'])
    check('the service takes .exe and .com: types the plist declares and the app opens',
          service.get('NSSendFileTypes') == ['com.microsoft.windows-executable', 'at.andreas-horvath.neo98.com']
          and set(service.get('NSSendFileTypes', [])) <= declared and set(service.get('NSSendFileTypes', [])) <= opened)
    check('the service is on without the system settings and belongs to the Finder',
          service.get('NSRequiredContext') == {'NSApplicationIdentifier': 'com.apple.finder'})
    method = b'openAsGameMachine:userData:error:'
    check('the host holds the method the service names', method in binary.read_bytes())
    app = None
    if '--no-app' not in sys.argv:
        app = mac_app.build_app(build=False, target=out / 'Neo98.app')
        built = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
        lint = subprocess.run(['/usr/bin/plutil', '-lint', str(app / 'Contents/Info.plist')], capture_output=True, text=True)
        check('the built bundle: plutil accepts Info.plist, and it holds the service',
              lint.returncode == 0 and built.get('NSServices') == services
              and method in (app / 'Contents/MacOS/Neo98').read_bytes())
        check('the bundle is built here and not in the main checkout',
              str(app.resolve()).startswith(str(ROOT.resolve()) + '/build/game-finder/'))
        signed = subprocess.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(app)], capture_output=True)
        check('the signature of the bundle holds', signed.returncode == 0)
        # A game machine of the bundle, with a machine and settings of its own.
        app_machine = clone_machine(out / 'app-machine')
        (out / 'app-settings').mkdir()
        (out / 'app-settings' / 'settings.ini').write_text(f'# Test\nmachine = {app_machine}\nscale = 1\n')
        (app_machine / 'C' / 'SPIELE').mkdir()
        shutil.copyfile(fixture, app_machine / 'C' / 'SPIELE' / 'GMGAME.EXE')
        environment = {'NEO98_SETTINGS_DIR': str(out / 'app-settings'), 'NEO98_LOG_DIR': str(out / 'app-logs')}
        program = app / 'Contents/MacOS/Neo98'
        code, text = host(['--game-machine-create', 'C:\\SPIELE\\GMGAME.EXE', '--game-machine-name', 'Café Game'],
                          environment, program=program)
        folder = (app_machine / 'Spielmaschinen' / 'Café Game').resolve()
        launcher = folder / 'Café Game.app'
        link = launcher / 'Contents/MacOS/Café Game'
        script = launcher / 'Contents/MacOS/spielmaschine'
        check('bundle: the launcher holds a link to the host of the bundle and a script that starts the link',
              code == 0 and link.is_symlink() and os.readlink(link) == str(program.resolve())
              and script.read_text().endswith(f"exec '{link}' --game-machine '{folder}'\n") and os.access(script, os.X_OK))
        lint = subprocess.run(['/usr/bin/plutil', '-lint', str(launcher / 'Contents/Info.plist')], capture_output=True)
        check('bundle: plutil accepts the Info.plist of the launcher', lint.returncode == 0)
        # What a double click on the launcher runs: its script.
        code, text = host([], environment, program=script)
        (out / 'app-game.log').write_text(text)
        check('bundle: started by the script of its launcher, the game ran and the machine ended',
              code == 0 and 'NEO98 HOST exited code=7' in text and 'NEO98 HOST game machine ends (the game has ended)' in text)
        # The file system may spell the umlaut in two characters.
        check('bundle: macOS knows the process by the name of the game and as the launcher bundle',
              f'NEO98 HOST game machine application name=Café Game bundle={launcher}\n' in unicodedata.normalize('NFC', text))

    # --- 10. The icon of the program file ---------------------------------------
    code, text = host(['--game-machine-create', 'C:\\WINDOWS\\CALC.EXE', '--game-machine-name', 'Rechner'])
    calculator = games / 'Rechner'
    check('the icon of the program file is there before the first start, in the folder and in the launcher',
          code == 0 and re.search(r'^NEO98 GAME MACHINE icon 32x32 of .*/WINDOWS/CALC\.EXE$', text, re.M)
          and (calculator / 'Symbol.icns').read_bytes()[:4] == b'icns'
          and (calculator / 'Rechner.app/Contents/Resources/icon.icns').read_bytes() == (calculator / 'Symbol.icns').read_bytes())
    neo98_icon = ROOT / 'build/generated/Neo98.icns'
    check('and it is not the icon of NEO98',
          not neo98_icon.is_file() or neo98_icon.read_bytes() != (calculator / 'Symbol.icns').read_bytes())
    code, text = host(['--game-machine-create', 'C:\\WINDOWS\\WINFILE.EXE', '--game-machine-name', 'Dateimanager'])
    check('a 16-bit program has no icon here; its game machine is made all the same',
          code == 0 and 'GAME MACHINE icon' not in text and not (games / 'Dateimanager' / 'Symbol.icns').exists())
    for name in ('Rechner', 'Dateimanager'):
        host(['--game-machine-delete', name])

    # --- 2. to 8. The normal machine --------------------------------------------
    spiele = drive_c / 'SPIELE'
    calc = win98_c.path('C:\\WINDOWS\\CALC.EXE', drive_c)
    # Each entry: seconds after the entry before, and the step. The clock of
    # the script counts from its last wait, so the seconds add up between two.
    fixture_window = (0, 'wait=|Game machine fixture')
    script = [(0, 'wait=Shell_TrayWnd'), (1, 'idle=1500'), (1, 'menu=describe'),
              # 2. asked once, "normally"
              (1, 'menu=game-machine-answers=normal'), (1, f'open={spiele / "GMGAME.EXE"}'), fixture_window,
              (4, f'open={spiele / "GMGAME.EXE"}'), fixture_window,
              (4, f'open={spiele / "SETUP.EXE"}'), fixture_window,
              (4, f'open={calc}'), (0, 'wait=SciCalc'),
              # 3. "as a game machine", a program outside the drives
              (1, 'menu=game-machine-answers=game|Finder game'), (1, f'open={kroko}'),
              (14, f'open={kroko}'),
              # 4. a second game machine for the same program
              (14, f'menu=game-machine-create={kroko}|Second game'), (2, f'open={kroko}'),
              (1, 'menu=game-machine-answers=Second game'), (1, f'open={kroko}'),
              # 5. the entry of the Finder
              (14, 'menu=game-machine-answers=Service game'), (1, f'menu=game-machine-open={spiele / "DIENST.EXE"}'),
              (14, f'menu=game-machine-open={spiele / "DIENST.EXE"}'),
              (12, f'menu=game-machine-open={spiele / "LIES.TXT"}'),
              # 6. the checkbox and the menu
              (1, 'menu=game-machine-answers=normal+never'), (1, f'open={spiele / "ZWEI.EXE"}'), fixture_window,
              (4, 'menu=describe'), (1, f'open={spiele / "DREI.EXE"}'), fixture_window,
              (4, 'menu=game-machine-ask'), (1, 'menu=game-machine-answers=cancel'), (1, f'open={spiele / "DREI.EXE"}'),
              # 7. the alias
              (2, f'menu=game-machine-desktop=Finder game|{desk}'), (1, f'menu=game-machine-desktop=Finder game|{desk}'),
              (1, 'menu=game-machine-desktop=Finder game'), (1, 'menu=describe'),
              # 8. what a second start hands over
              (1, 'idle=100'), fixture_window, (25, 'menu=quit')]
    steps, clock = [], 0
    for seconds, step in script:
        clock = 0 if step.startswith(('wait=', 'idle=')) and not seconds else clock + seconds
        steps.append(f'{clock}:{step}')
        if step.startswith(('wait=', 'idle=')):
            clock = clock if seconds else 0
    normal_log = out / 'normal.log'
    with normal_log.open('w') as log:
        normal = subprocess.Popen([str(binary)], cwd='/', env=dict(base, NEO98_HOST_INPUT=';'.join(steps)),
                                  stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)

    def logged(pattern, timeout=240, count=1):
        return wait_for(normal_log, pattern, timeout, count)

    path = re.escape
    game, zwei, drei, dienst = spiele / 'GMGAME.EXE', spiele / 'ZWEI.EXE', spiele / 'DREI.EXE', spiele / 'DIENST.EXE'
    try:
        check('normal machine reaches the desktop', logged(r'^NEO98 HOST input wait=Shell_TrayWnd reached', 300))
        # 2.
        check('a program opened from the Finder is asked about; the answer is "normally"',
              logged(rf'^NEO98 HOST open {path(str(game.resolve()))} asked: normal$'))
        check('and it starts in the normal machine',
              logged(r'^NEO98 HOST open .*GMGAME\.EXE -> C:\\SPIELE\\GMGAME\.EXE$')
              and logged(r'^NEO98 HOST input wait=\|Game machine fixture reached', 120))
        check('the answer stands in settings.ini', stored(settings_file).get('open.normal') == [str(game.resolve())]
              and stored(settings_file).get('open.ask') == ['1'])
        check('the second time it is not asked again',
              logged(rf'^NEO98 HOST open {path(str(game.resolve()))} not asked: answered before$'))
        check('a program with the name of an installer is not asked about',
              logged(r'^NEO98 HOST open .*/SETUP\.EXE not asked: installer name$'))
        check('a program of Windows is not asked about',
              logged(r'^NEO98 HOST open .*/WINDOWS/CALC\.EXE not asked: program of Windows$')
              and logged(r'^NEO98 HOST input wait=SciCalc reached', 120))
        # 3.
        folder = (games / 'Finder game')
        check('"as a game machine": a game machine is made, for a program outside the drives on a drive of its folder',
              logged(rf'^NEO98 HOST open {path(str(kroko.resolve()))} asked: game machine$')
              and logged(r'^NEO98 HOST game machine created name=Finder game program=D:\\KROKO\.EXE '))
        check('and started at once', logged(r'^NEO98 HOST game machine start name=Finder game ok$', 30))
        check('the game machine ran the game from that drive',
              _wait_file(game_log('Finder game'), r'^NEO98 HOST game machine ends \(the game has ended\)$', 120)
              and values(folder / 'C-Schicht' / 'GMTEST' / 'SAVE.TXT').get('self', '').upper() == 'D:\\KROKO.EXE')
        text = game_log('Finder game').read_text(errors='replace') if game_log('Finder game').is_file() else ''
        # A hard disk like any other of a game machine: read where it lies,
        # written into a layer of the game machine (that a game
        # writes there: tools/test_game_machine_files_host.py).
        check('its drive D: is the folder of the program below a write layer of the game machine',
              f'NEO98 HOST drive D: layer {folder.resolve()}/D-Schicht\n' in text
              and f'NEO98 HOST drive D: below {outside.resolve()}\n' in text)
        own = stored(folder / 'settings.ini')
        check('the game machine keeps the drive and the program file in its settings',
              own.get('drive.D') == [str(outside.resolve())] and own.get('game.file') == [str(kroko.resolve())]
              and own.get('game.program') == ['D:\\KROKO.EXE'] and own.get('drives.readonly') == ['1']
              and 'open.ask' not in own and 'open.normal' not in own)
        launcher = folder.resolve() / 'Finder game.app'
        check('macOS knows the game machine by the name of the game: it runs as the program of its launcher bundle',
              f'NEO98 HOST game machine application name=Finder game bundle={launcher}\n' in text)
        check('the next double click starts the game machine without a question',
              logged(r'^NEO98 HOST game machine start name=Finder game ok$', 60, 2)
              and len(re.findall(r'KROKO\.EXE asked:', normal_log.read_text(errors='replace'))) == 1)
        # 4.
        check('a second game machine for the same program, under another name',
              logged(r'^NEO98 HOST game machine created name=Second game program=D:\\KROKO\.EXE ', 120))
        check('with two the double click guesses nothing',
              logged(rf'^NEO98 HOST game machine: 2 for {path(str(kroko.resolve()))}, none chosen$', 60))
        check('and starts the one that is chosen',
              logged(rf'^NEO98 HOST game machine: 2 for {path(str(kroko.resolve()))}, chosen Second game$', 60)
              and logged(r'^NEO98 HOST game machine start name=Second game ok$', 30)
              and _wait_file(game_log('Second game'), r'^NEO98 HOST game machine ends \(the game has ended\)$', 120))
        # 5.
        check('the entry of the Finder makes a game machine and starts it',
              logged(rf'^NEO98 HOST open as game machine {path(str(dienst.resolve()))}$', 120)
              and logged(r'^NEO98 HOST game machine created name=Service game program=C:\\SPIELE\\DIENST\.EXE ', 30)
              and logged(r'^NEO98 HOST game machine start name=Service game ok$', 30))
        check('the second time it starts the one that is there',
              logged(r'^NEO98 HOST game machine start name=Service game ok$', 60, 2)
              and len(re.findall(r'created name=Service game', normal_log.read_text(errors='replace'))) == 1)
        check('a file that is no program is refused',
              logged(r'^NEO98 HOST game machine: .*/LIES\.TXT is no program file$', 60))
        # 6.
        check('the checkbox: the answer counts and the question is off',
              logged(rf'^NEO98 HOST open {path(str(zwei.resolve()))} asked: normal, never ask again$', 120))
        check('another program then opens without a question',
              logged(r'^NEO98 HOST open .*/DREI\.EXE -> C:\\SPIELE\\DREI\.EXE$', 120)
              )
        check('the menu switches the question on again',
              logged(r'^NEO98 HOST open asks about game machines: 1$', 60)
              and 'DREI.EXE asked' not in normal_log.read_text(errors='replace').split('asks about game machines: 1')[0])
        check('cancelled, the program does not start and nothing is remembered',
              logged(rf'^NEO98 HOST open {path(str(drei.resolve()))} asked: cancelled$', 60))
        # 7.
        alias = desk / 'Finder game'
        check('an alias of the launcher in the named folder leads to the launcher',
              logged(rf'^NEO98 HOST game machine alias {path(str(alias.resolve()))} -> {path(str(launcher))}$', 60)
              and alias.is_file() and alias.read_bytes()[:4] == b'book')
        check('made again it replaces itself', logged(r'^NEO98 HOST game machine alias .* -> ', 30, 2)
              and sorted(entry.name for entry in desk.iterdir()) == ['Finder game'])
        check('without a screen nothing is put on the desktop of the person',
              logged(r'^NEO98 HOST game machine alias failed: Desktop folder not found\.$', 30))
        # 8. Now the script waits for the window of a program a second start hands over.
        check('the script waits for a second start', logged(r'^NEO98 HOST input idle=100 ', 60))
        code, text = host(['--exe', str(zwei)])
        check('a second start with a file hands it over', code == 0 and re.search(r'handed over: open .*/ZWEI\.EXE', text))
        check('the running host knows the answer for it and starts it normally',
              logged(rf'^NEO98 HOST open {path(str(zwei.resolve()))} not asked: answered before$', 30)
              and logged(r'^NEO98 HOST input wait=\|Game machine fixture reached', 120, 6))
        code, text = host(['--game-machine-open', str(dienst)])
        check('a second start with --game-machine-open hands the request over',
              code == 0 and re.search(r'handed over: game .*/DIENST\.EXE', text))
        check('the running host starts the game machine of that program',
              logged(rf'^NEO98 HOST second start: game {path(str(dienst.resolve()))}$', 30)
              and logged(r'^NEO98 HOST game machine start name=Service game ok$', 30, 3))
        check('normal machine ends in order', normal.wait(timeout=300) == 0)
    finally:
        if normal.poll() is None:
            normal.terminate()
            try:
                normal.wait(timeout=30)
            except subprocess.TimeoutExpired:
                pass
    log = normal_log.read_text(errors='replace')
    final = stored(settings_file)
    check('settings.ini: the question is on again, two programs are answered "normally"',
          final.get('open.ask') == ['1'] and final.get('open.normal') == [str(game.resolve()), str(zwei.resolve())])
    describes = log.split('NEO98 HOST menu-script describe ok')
    items = [[line for line in part.splitlines() if line.startswith('NEO98 HOST menu-item ')] for part in describes]
    prefix = 'NEO98 HOST menu-item Machine > Game machines > '
    check('menu: the question has an item with a check mark, off after the checkbox, on again after the menu',
          len(items) >= 3 and prefix + 'Ask when opening from Finder [game-machine-ask] (enabled)' in items[0]
          and prefix + 'Ask when opening from Finder [game-machine-ask]' in items[1]
          and prefix + 'Ask when opening from Finder [game-machine-ask] (enabled)' in items[2])
    check('menu: each game machine names its program and offers the alias',
          len(items) >= 3 and all(prefix + f'{name} > Program: {program} (disabled)' in items[2]
                                  and prefix + f'{name} > Create desktop launcher [game-machine-desktop]' in items[2]
                                  for name, program in (('Finder game', 'D:\\KROKO.EXE'), ('Second game', 'D:\\KROKO.EXE'),
                                                        ('Service game', 'C:\\SPIELE\\DIENST.EXE'))))
    waits = re.findall(r'^NEO98 HOST input wait=(.*) (reached|timed out)', log, re.M)
    check('every wait of the normal machine was reached', len(waits) == 8 and all(state == 'reached' for _, state in waits))
    check('no host failure', 'NEO98 HOST failed' not in log and 'libc++abi' not in log)
    code, text = host(['--game-machine-list'])
    check('the list names the three game machines with their program files',
          re.findall(r'^NEO98 GAME MACHINE name=(.*?) running=\d program=(\S+) folder=.*? file=(.*)$', text, re.M)
          == [('Finder game', 'D:\\KROKO.EXE', str(kroko.resolve())),
              ('Second game', 'D:\\KROKO.EXE', str(kroko.resolve())),
              ('Service game', 'C:\\SPIELE\\DIENST.EXE', str(dienst.resolve()))])
    check('the normal drive of the program outside got no file from the game machines',
          sorted(entry.name for entry in outside.iterdir() if not entry.name.startswith('.')) == ['KROKO.EXE'])

    # --- 9. --game-machine-open with a machine that does not run -------------------
    for name in ('Service game', 'Finder game', 'Second game'):
        for _ in range(60):
            if not locked(games / name / 'C-Schicht.host-lock'):
                break
            time.sleep(1)
    # No host runs: a start that is asked for a game machine becomes it, and
    # the normal machine does not boot. The process the test started is the
    # game machine afterwards, under the link of its launcher bundle.
    lock_c = machine / 'C.host-lock'
    runs = [0]

    def hosts():
        """The hosts of this test that run: (pid, command line)."""
        listing = subprocess.run(['/bin/ps', '-axo', 'pid=,command='], capture_output=True, text=True, errors='replace').stdout
        found = []
        for line in listing.splitlines():
            pid, _, command = line.strip().partition(' ')
            if (str(out) + '/' in command or str(binary) in command) and 'test_game_finder_host' not in command:
                found.append((int(pid), command.strip()))
        return found

    def cold(arguments, environment=None, timeout=240):
        """One start with no host running. What it printed until it ended or
        became a game machine, its exit code, and what the process list and
        the lock of the normal machine said while the game machine ran."""
        runs[0] += 1
        front_log = out / f'cold-{runs[0]}.log'
        before = lock_c.stat().st_mtime_ns if lock_c.exists() else 0
        with front_log.open('w') as log:
            process = subprocess.Popen([str(binary), *arguments], cwd='/', env=dict(base, **(environment or {})),
                                       stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        seen = {'pid': process.pid, 'command': '', 'hosts': [], 'normal_locked': None}
        try:
            end = time.time() + 90
            while time.time() < end and process.poll() is None:
                if 'this start becomes the game machine' in front_log.read_text(errors='replace'):
                    break
                time.sleep(0.2)
            became = re.search(r'^NEO98 HOST open without machine: this start becomes the game machine name=(.*) \((.*)\)$',
                               front_log.read_text(errors='replace'), re.M)
            if became and process.poll() is None:
                # The new image has its name once it runs.
                for _ in range(50):
                    seen['hosts'] = hosts()
                    seen['command'] = dict(seen['hosts']).get(process.pid, '')
                    if '--game-machine' in seen['command'] or process.poll() is not None:
                        break
                    time.sleep(0.1)
                seen['normal_locked'] = locked(lock_c)
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            code = 'timeout'
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    pass
        text = front_log.read_text(errors='replace')
        seen['lock_touched'] = (lock_c.stat().st_mtime_ns if lock_c.exists() else 0) != before
        with command_log.open('a') as log:
            log.write(f'$ (no host running) {" ".join(map(str, arguments))} {environment or ""}\n{text}'
                      f'process {seen["pid"]}: {seen["command"]}\nhosts: {seen["hosts"]}\nexit {code}\n\n')
        return code, text, seen

    def is_game_machine(text, seen, name):
        """The start became the game machine `name` and no normal machine booted."""
        folder = (games / name).resolve()
        link = folder / f'{name}.app' / 'Contents/MacOS' / name
        return (f'NEO98 HOST open without machine: this start becomes the game machine name={name} ({link})\n' in text
                and 'NEO98 HOST app settings=' not in text and 'EXPLORER' not in text
                and seen['command'] == f'{link} --game-machine {folder}'
                and [pid for pid, _ in seen['hosts']] == [seen['pid']]
                and seen['normal_locked'] is False)

    def game_ran(name, mode_file=True):
        log = game_log(name)
        return (_wait_file(log, r'^NEO98 HOST game machine ends \(the game has ended\)$', 120)
                and f'NEO98 HOST game machine application name={name} bundle={(games / name).resolve() / (name + ".app")}\n'
                in log.read_text(errors='replace')
                and len(re.findall(r'^NEO98 HOST app settings=', log.read_text(errors='replace'), re.M)) == 1
                and 'EXPLORER.EXE' not in log.read_text(errors='replace')
                and (not mode_file or values(games / name / 'C-Schicht' / 'GMTEST' / 'SAVE.TXT').get('mode') == 'game'))

    spaet, kalt, frage, nein, norm = (spiele / name for name in ('SPAET.EXE', 'KALT.EXE', 'FRAGE.EXE', 'NEIN.EXE', 'NORM.EXE'))
    check('no host of the test runs before the starts without a machine', hosts() == [])
    # (b) a program without a game machine: made under the suggested name.
    code, text, seen = cold(['--game-machine-open', str(spaet)])
    check('--game-machine-open, no host running, a program without a game machine: it is made from the machine on the disk',
          re.search(r'^NEO98 HOST game machine created name=Spaet program=C:\\SPIELE\\SPAET\.EXE ms=\d+ folder=.* '
                    r'\(machine not running, held for the copy only\)$', text, re.M))
    check('and the start becomes the game machine: one host, under the link of the launcher, no normal machine booted',
          is_game_machine(text, seen, 'Spaet'))
    check('the lock of the normal machine was held for the copy and is free while the game machine runs',
          seen['lock_touched'] and seen['normal_locked'] is False)
    check('the game ran in it and the process ended with the game', code == 0 and game_ran('Spaet') and hosts() == [])
    # (a) a program with a game machine; here the start also reads its launch
    # as a start from the Finder does.
    code, text, seen = cold(['--game-machine-open', str(spaet)], {'NEO98_LAUNCH_PROBE': '1'})
    check('--game-machine-open, a program with a game machine: the start becomes it and makes nothing',
          is_game_machine(text, seen, 'Spaet') and 'created' not in text and code == 0 and game_ran('Spaet'))
    check('the lock of the normal machine is not touched for it', not seen['lock_touched'])
    probe = re.search(r'^NEO98 HOST launch: (.*) after (\d+) ms, event (.*), 0 file\(s\), 1 as game machine$', text, re.M)
    check('the launch is read before anything boots', bool(probe))
    print(f'launch probe of a start from the test: {probe.group(0) if probe else "-"}', flush=True)
    # The name answered.
    code, text, seen = cold(['--game-machine-open', str(kalt)], {'NEO98_GAME_MACHINE_ANSWERS': 'Kaltstart'})
    check('the name of a new game machine is asked for without a machine',
          re.search(r'^NEO98 HOST game machine created name=Kaltstart program=C:\\SPIELE\\KALT\.EXE ', text, re.M)
          and is_game_machine(text, seen, 'Kaltstart') and code == 0 and game_ran('Kaltstart'))
    # (c) two game machines for one program.
    code, text = host(['--game-machine-create', 'C:\\SPIELE\\KALT.EXE', '--game-machine-name', 'Cold Two'])
    check('a second game machine for that program', code == 0)
    code, text, seen = cold(['--game-machine-open', str(kalt)])
    check('with two and no answer nothing starts and nothing boots',
          code == 0 and f'NEO98 HOST game machine: 2 for {kalt.resolve()}, none chosen\n' in text
          and 'becomes' not in text and 'NEO98 HOST app settings=' not in text and hosts() == [])
    code, text, seen = cold(['--game-machine-open', str(kalt)], {'NEO98_GAME_MACHINE_ANSWERS': 'Cold Two'})
    check('the one chosen starts',
          f'NEO98 HOST game machine: 2 for {kalt.resolve()}, chosen Cold Two\n' in text
          and is_game_machine(text, seen, 'Cold Two') and code == 0 and game_ran('Cold Two'))
    # (d) cancelled, and a file that is no program.
    code, text, seen = cold(['--game-machine-open', str(nein)], {'NEO98_GAME_MACHINE_ANSWERS': 'cancel'})
    check('the name cancelled: no game machine, no machine, the start ends',
          code == 0 and 'NEO98 HOST game machine for C:\\SPIELE\\NEIN.EXE: cancelled\n' in text
          and 'NEO98 HOST app settings=' not in text and not seen['lock_touched'] and hosts() == []
          and not any(entry.name.startswith(('Nein', '.Nein')) for entry in games.iterdir()))
    code, text, seen = cold(['--game-machine-open', str(spiele / 'LIES.TXT')])
    check('a file that is no program is refused without a machine',
          code == 0 and re.search(r'^NEO98 HOST game machine: .*/LIES\.TXT is no program file$', text, re.M)
          and 'NEO98 HOST app settings=' not in text and hosts() == [])
    # --- 9b. A double click with no host running (the file of the launch) ---------
    code, text, seen = cold([], {'NEO98_LAUNCH_OPEN': str(spaet)})
    check('a program with a game machine, opened with no host running: the start becomes it without a question',
          is_game_machine(text, seen, 'Spaet') and 'asked' not in text and code == 0 and game_ran('Spaet'))
    code, text, seen = cold([], {'NEO98_LAUNCH_OPEN': str(frage), 'NEO98_GAME_MACHINE_ANSWERS': 'game|Prompt game'})
    check('a program never asked about, answered "as a game machine": made, and the start becomes it',
          f'NEO98 HOST open {frage.resolve()} asked: game machine\n' in text
          and re.search(r'^NEO98 HOST game machine created name=Prompt game program=C:\\SPIELE\\FRAGE\.EXE ', text, re.M)
          and is_game_machine(text, seen, 'Prompt game') and code == 0 and game_ran('Prompt game'))
    code, text, seen = cold([], {'NEO98_LAUNCH_OPEN': str(nein), 'NEO98_GAME_MACHINE_ANSWERS': 'cancel'})
    check('answered "cancel": the start ends, nothing boots, nothing is remembered',
          code == 0 and f'NEO98 HOST open {nein.resolve()} asked: cancelled\n' in text
          and 'NEO98 HOST app settings=' not in text and hosts() == []
          and str(nein.resolve()) not in stored(settings_file).get('open.normal', []))
    code, text, seen = cold([], {'NEO98_LAUNCH_OPEN': str(norm), 'NEO98_GAME_MACHINE_ANSWERS': 'normal', 'NEO98_LAUNCH_PROBE': '1',
                                 'NEO98_HOST_INPUT': '0:wait=|Game machine fixture;4:menu=quit'})
    (out / 'late.log').write_text(text)
    asked = text.find(f'NEO98 HOST open {norm.resolve()} asked: normal\n')
    check('answered "normally": asked before the machine boots, then the normal machine boots and opens the program',
          code == 0 and 0 <= asked < text.find('NEO98 HOST app settings=')
          and f'NEO98 HOST open {norm.resolve()} not asked: answered before\n' in text
          and re.search(r'^NEO98 HOST open .*/NORM\.EXE -> C:\\SPIELE\\NORM\.EXE$', text, re.M)
          and re.search(r'^NEO98 HOST input wait=\|Game machine fixture reached', text, re.M)
          and 'becomes' not in text and str(norm.resolve()) in stored(settings_file).get('open.normal', []))
    probe = re.search(r'^NEO98 HOST launch: .*$', text, re.M)
    print(f'launch probe before a normal boot: {probe.group(0) if probe else "-"}', flush=True)
    for name in ('Service game', 'Finder game', 'Second game', 'Spaet', 'Kaltstart', 'Cold Two', 'Prompt game'):
        code, text = host(['--game-machine-delete', name])
        check(f'game machine {name} is deleted again', code == 0)

    failed = [name for name, okay in checks.items() if not okay]
    if not failed:
        for name in ('machine', 'tmp', 'app-machine', 'Neo98.app'):
            shutil.rmtree(out / name, ignore_errors=True)
    (out / 'results.json').write_text(json.dumps({'checks': checks, 'logs': str(out)}, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if failed:
        print(f'{len(failed)} of {len(checks)} checks failed; logs in {out}')
        return 1
    print(f'game machines from the Finder: {len(checks)} checks passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
