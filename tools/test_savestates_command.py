#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The command line for the states of a game machine that does not run; tools/test_savestates.py runs this.

No machine starts here: a game machine is made from a clone of the pristine
drive C:, its layer is changed by hand, and the host is asked to list, save,
load, rename and delete.
"""
from __future__ import annotations

import fcntl
import hashlib
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import disk_room  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
from test_mac_app_host import clone_machine  # noqa: E402

GAME = 'C:\\WINDOWS\\CALC.EXE'


def checksums(path):
    listing = {}
    for entry in sorted(path.rglob('*')):
        name = str(entry.relative_to(path))
        if entry.is_dir():
            listing[name] = 'directory'
        else:
            listing[name] = hashlib.sha256(entry.read_bytes()).hexdigest() + f' {entry.stat().st_mtime_ns}'
    return listing


def states(text):
    """The lines of a list as dictionaries."""
    found = []
    for line in text.splitlines():
        match = re.match(r'NEO98 GAME MACHINE STATE id=(\S+) kind=(\S+) game=(\S+) parent=(\S+) depth=(\d+) current=(\d) '
                         r'usable=(\d) damaged=(\d) time=(\d+) layers=(\S+) machine=(\d+) name="(.*)" reason="(.*)"$', line)
        if match:
            keys = ('id', 'kind', 'game', 'parent', 'depth', 'current', 'usable', 'damaged', 'time', 'layers', 'machine',
                    'name', 'reason')
            found.append(dict(zip(keys, match.groups())))
    return found


def run(out, build=True):
    work = out / 'command'
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    binary = build_host() if build else ROOT / 'build/native-app/neo98-host'
    machine = clone_machine(work / 'machine')
    settings_dir = work / 'settings'
    settings_dir.mkdir()
    (work / 'tmp').mkdir()
    (settings_dir / 'settings.ini').write_text(f'# Test\nmachine = {machine}\nscale = 1\n')
    environment = {'HOME': str(Path.home()), 'SDL_VIDEODRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy',
                   'NEO98_SETTINGS_DIR': str(settings_dir), 'NEO98_LOG_DIR': str(work / 'logs'), 'TMPDIR': str(work / 'tmp'),
                   'NEO98_REVISION': 'test-1', 'NEO98_DOS_CORE_ID': 'kern-a', 'PATH': os.environ.get('PATH', '')}
    log = work / 'commands.log'
    log.write_text('')
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)
        if not condition:
            print(f'  FAIL {name}', flush=True)

    def host(*arguments, **extra):
        result = subprocess.run([str(binary), *arguments], cwd='/', env=dict(environment, **extra), capture_output=True,
                                text=True, errors='replace', stdin=subprocess.DEVNULL, timeout=120)
        text = result.stdout + result.stderr
        with log.open('a') as handle:
            handle.write(f'$ {" ".join(arguments)}\n{text}exit {result.returncode}\n\n')
        return result.returncode, text

    code, text = host('--game-machine-create', GAME, '--game-machine-name', 'Zustandsspiel')
    game = machine / 'Spielmaschinen' / 'Zustandsspiel'
    layer = game / 'C-Schicht'
    check('the game machine is made', code == 0 and layer.is_dir())
    code, text = host('--game-machine-states', 'Zustandsspiel')
    check('a new game machine has no states', code == 0 and states(text) == [])
    code, text = host('--game-machine-states', 'Gibtesnicht')
    check('a game machine that is not there', code == 15 and 'no game machine named' in text)

    # The first state: the files as the game machine was made.
    (layer / 'SPIELE').mkdir()
    (layer / 'SPIELE' / 'STAND.DAT').write_text('eins')
    first_files = checksums(layer)
    code, text = host('--game-machine-state-save', 'Zustandsspiel', '--state-name', 'Erster Stand')
    saved = re.search(r'NEO98 GAME MACHINE STATE saved id=(\S+)', text)
    check('a state is saved', code == 0 and saved)
    first = saved[1] if saved else ''
    code, text = host('--game-machine-states', 'Zustandsspiel')
    listed = states(text)
    check('it is listed: its name, files alone, current, usable',
          len(listed) == 1 and listed[0]['id'] == first and listed[0]['name'] == 'Erster Stand' and listed[0]['game'] == 'off'
          and listed[0]['kind'] == 'manual' and listed[0]['current'] == '1' and listed[0]['usable'] == '1'
          and listed[0]['layers'] == 'C' and listed[0]['parent'] == '-')
    state = game / 'Zustände' / first
    check('the folder of the state holds the copy of the layer and the description',
          checksums(state / 'C-Schicht') == first_files and (state / 'zustand.ini').is_file()
          and (state / 'C-Schicht.registry').is_file() == (game / 'C-Schicht.registry').is_file())

    # The game goes on, a second state.
    (layer / 'SPIELE' / 'STAND.DAT').write_text('two, and longer')
    (layer / 'SPIELE' / 'NEU.DAT').write_text('neu')
    (game / 'D-Schicht').mkdir()
    (game / 'D-Schicht' / 'AUF-D.TXT').write_text('auf d')
    second_files = checksums(layer)
    code, text = host('--game-machine-state-save', 'Zustandsspiel', '--state-name', 'Zweiter Stand')
    second = (re.search(r'saved id=(\S+)', text) or ['', ''])[1]
    check('a second state', code == 0 and second and second != first)
    (layer / 'SPIELE' / 'STAND.DAT').write_text('drei')

    # Loading the first: an automatic state before, the files of the first after.
    code, text = host('--game-machine-state-load', 'Zustandsspiel', '--state', first)
    loaded = re.search(r'NEO98 GAME MACHINE STATE loaded id=(\S+) before=(\S+)', text)
    check('the first state is loaded', code == 0 and loaded and loaded[1] == first)
    before = loaded[2] if loaded else ''
    check('the layer is the copy of the first state', checksums(layer) == first_files)
    check('the layer of the second drive is gone with it', not (game / 'D-Schicht').exists())
    check('nothing of the load is left', not (game / '.laden').exists())
    code, text = host('--game-machine-states', 'Zustandsspiel')
    listed = {entry['id']: entry for entry in states(text)}
    check('three states: the two and the one from before the load',
          len(listed) == 3 and listed.get(before, {}).get('kind') == 'auto' and listed[before]['parent'] == second
          and listed[before]['name'].startswith('Before loading') and listed[second]['parent'] == first)
    check('the machine stands on the first', listed[first]['current'] == '1' and listed[second]['current'] == '0')
    check('the automatic state holds what was written last',
          (game / 'Zustände' / before / 'C-Schicht/SPIELE/STAND.DAT').read_text() == 'drei'
          and (game / 'Zustände' / before / 'D-Schicht/AUF-D.TXT').read_text() == 'auf d')
    # A third state now comes from the first: a second branch.
    (layer / 'SPIELE' / 'STAND.DAT').write_text('anderer Weg')
    code, text = host('--game-machine-state-save', 'Zustandsspiel')
    third = (re.search(r'saved id=(\S+)', text) or ['', ''])[1]
    code, text = host('--game-machine-states', 'Zustandsspiel')
    order = [(entry['id'], entry['depth']) for entry in states(text)]
    check('the list follows the tree', order == [(first, '0'), (second, '1'), (before, '2'), (third, '1')])
    check('a state without a name is named by its time', [e for e in states(text) if e['id'] == third][0]['name'].startswith('State from '))
    # Back to the second, across the tree.
    code, text = host('--game-machine-state-load', 'Zustandsspiel', '--state', second)
    check('the second state is loaded across the tree', code == 0 and checksums(layer) == second_files
          and (game / 'D-Schicht' / 'AUF-D.TXT').read_text() == 'auf d')

    # Names.
    code, text = host('--game-machine-state-rename', 'Zustandsspiel', '--state', before, '--state-name', 'Doch behalten')
    check('an automatic state is renamed', code == 0 and 'renamed' in text)
    code, text = host('--game-machine-states', 'Zustandsspiel')
    listed = {entry['id']: entry for entry in states(text)}
    check('and is the person\'s then', listed[before]['kind'] == 'manual' and listed[before]['name'] == 'Doch behalten')
    code, text = host('--game-machine-state-rename', 'Zustandsspiel', '--state', first, '--state-name', '   ')
    check('an empty name is refused', code == 15 and 'name is empty' in text)
    code, text = host('--game-machine-state-rename', 'Zustandsspiel', '--state', 'nichts', '--state-name', 'x')
    check('a state that is not there is not renamed', code == 15)

    # Automatic states: the newest five stay.
    for _ in range(7):
        host('--game-machine-state-load', 'Zustandsspiel', '--state', first)
    code, text = host('--game-machine-states', 'Zustandsspiel')
    listed = states(text)
    automatic = [entry for entry in listed if entry['kind'] == 'auto']
    check('after many loads five automatic states are left', len(automatic) == 5)
    check('the named ones are all still there', {first, second, third, before} <= {entry['id'] for entry in listed})

    # Deleting in the middle of the tree.
    code, text = host('--game-machine-state-delete', 'Zustandsspiel', '--state', second)
    check('a state in the middle is deleted', code == 0 and not (game / 'Zustände' / second).exists())
    code, text = host('--game-machine-states', 'Zustandsspiel')
    listed = {entry['id']: entry for entry in states(text)}
    check('what came from it hangs on its parent', listed[before]['parent'] == first and second not in listed)
    code, text = host('--game-machine-state-delete', 'Zustandsspiel', '--state', second)
    check('deleting it again fails', code == 15)
    code, text = host('--game-machine-state-load', 'Zustandsspiel', '--state', second)
    check('loading a state that is gone fails and changes nothing', code == 15 and 'does not exist' in text)

    # A state with a machine state is bound to the engine of its supplier (not
    # to the revision of NEO98) and to the files
    # of the normal drive. No machine ran here, so one is put together: the
    # description of a DOS state with a file of the normal drive in it.
    made = game / 'Zustände' / 'gemacht'
    shutil.copytree(game / 'Zustände' / first, made)
    (made / 'Maschine.bin').write_bytes(b'0123456789')
    calc = machine / 'C' / 'WINDOWS' / 'CALC.EXE'
    info = calc.stat()
    text = (made / 'zustand.ini').read_text()
    text = text.replace('game = off', 'game = dos').replace('machine.size = 0', 'machine.size = 10')
    text = re.sub(r'^engine = .*$', 'engine = dos-1-kern-a', text, flags=re.M)
    text = text.replace('end = 1', f'file = C\t{info.st_size}\t{info.st_mtime_ns // 10**9}.{info.st_mtime_ns % 10**9:09d}\tWINDOWS/CALC.EXE\nend = 1')
    (made / 'zustand.ini').write_text(text)
    code, text = host('--game-machine-states', 'Zustandsspiel')
    listed = {entry['id']: entry for entry in states(text)}
    check('a state with a machine state of this engine and these files fits', listed['gemacht']['usable'] == '1')
    code, text = host('--game-machine-states', 'Zustandsspiel', NEO98_REVISION='test-2')
    check('another revision of NEO98 with the same DOS machine: it still fits',
          {entry['id']: entry for entry in states(text)}['gemacht']['usable'] == '1')
    code, text = host('--game-machine-states', 'Zustandsspiel', NEO98_DOS_CORE_ID='kern-b')
    listed = {entry['id']: entry for entry in states(text)}
    check('another DOS machine: refused, with both engines in the reason',
          listed['gemacht']['usable'] == '0' and 'dos-1-kern-a' in listed['gemacht']['reason'] and 'dos-1-kern-b' in listed['gemacht']['reason']
          and 'DOS-Maschine' in listed['gemacht']['reason'])
    check('the states of the files alone still fit', listed[first]['usable'] == '1')
    layer_now = checksums(layer)
    count = len(listed)
    code, text = host('--game-machine-state-load', 'Zustandsspiel', '--state', 'gemacht', NEO98_DOS_CORE_ID='kern-b')
    check('loading it is refused with the reason, and nothing changed',
          code == 15 and 'different version' in text and checksums(layer) == layer_now)
    code, text = host('--game-machine-states', 'Zustandsspiel')
    check('no automatic state was made for the refused load', len(states(text)) == count)
    # The normal machine patches the program.
    os.utime(calc, ns=(info.st_atime_ns, info.st_mtime_ns + 2 * 10**9))
    code, text = host('--game-machine-states', 'Zustandsspiel')
    listed = {entry['id']: entry for entry in states(text)}
    check('a changed file of the normal drive: refused, the file is named',
          listed['gemacht']['usable'] == '0' and 'C:\\WINDOWS\\CALC.EXE' in listed['gemacht']['reason'])
    os.utime(calc, ns=(info.st_atime_ns, info.st_mtime_ns))
    # A folder that is no whole state.
    (made / 'zustand.ini').write_text((made / 'zustand.ini').read_text().split('layers')[0])
    code, text = host('--game-machine-states', 'Zustandsspiel')
    listed = {entry['id']: entry for entry in states(text)}
    check('a state with half a description is listed as damaged', listed['gemacht']['damaged'] == '1' and listed['gemacht']['usable'] == '0')
    code, text = host('--game-machine-state-delete', 'Zustandsspiel', '--state', 'gemacht')
    check('and can be deleted', code == 0 and not made.exists())

    # While the game machine runs its own host does this.
    with open(str(layer) + '.host-lock', 'a+') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        code, text = host('--game-machine-state-save', 'Zustandsspiel')
        check('saving from outside is refused while the game machine runs', code == 14 and 'running' in text)
        code, text = host('--game-machine-state-load', 'Zustandsspiel', '--state', first)
        check('loading from outside too', code == 14)
        code, text = host('--game-machine-states', 'Zustandsspiel')
        check('the list is there all the same', code == 0 and len(states(text)) == count - 1)
    code, text = host('--game-machine-state-save', 'Zustandsspiel')
    check('afterwards saving works again', code == 0)

    # The game machine is deleted with its states.
    code, text = host('--game-machine-delete', 'Zustandsspiel')
    check('deleting the game machine takes its states along', code == 0 and not game.exists())
    passed = sum(1 for value in checks.values() if value)
    disk_room.tidy(work)
    return {'passed': passed, 'failed': len(checks) - passed, 'checks': checks}


if __name__ == '__main__':
    result = run(ROOT / 'build/savestates', build='--no-build' not in sys.argv)
    print(f"command: {result['passed']} passed, {result['failed']} failed")
    sys.exit(1 if result['failed'] else 0)
