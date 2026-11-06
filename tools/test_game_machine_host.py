#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The game machine (tools/host_game_machine.h).

Everything runs under SDL's dummy video and audio drivers with a machine
folder that is a clone of the pristine drive C:, settings and logs under
build/game-machine. The guest is tools/game_machine_fixture.c, installed in
the normal machine as C:\\SPIELE\\GMGAME.EXE before any game machine exists.

1. The command line makes game machines from the machine that does not run:
   an empty write layer with the registry of this moment (hives and journal),
   settings.ini, the launcher bundle. Nothing of drive C: is copied.
2. The write layer. The game reads files of the normal drive through it,
   among them one that was added and one that was changed after the game
   machine was made; it writes to such files, deletes and renames files and
   directories, lists a directory (no name and no 8.3 alias twice), maps,
   locks and watches files. Two files stay open while the test deletes them
   in the normal drive. Before and after the run the normal drive is the
   same, file for file by SHA-256. The second start finds everything as the
   first left it, and a file the game wrote hides a later change of the same
   file in the normal drive.
3. A game machine runs its game without a shell: what the game writes (a
   file, a registry value) is in the game machine and not in the normal
   machine, the value is still there in the second run, and the machine ends
   by itself with the game.
4. A game whose first process starts a second one and leaves (CreateProcess,
   and ShellExecute without Explorer) runs until the second has ended.
5. A game that faults shows the error box of Win98; after it the machine ends
   and its report lies in its own log folder.
6. A DOS program and a 16-bit Windows program in a game machine write into
   the layer.
7. The normal machine with Explorer and game machines beside it: the menu
   Machine > Game machines makes one for a program by path and one for the
   program in front, starts one (a host process of its own with its own
   lock), refuses to delete it while it runs and deletes it afterwards. A
   second start of a running game machine brings it to the front. A game
   machine pauses and goes on; a file opened in it goes to the normal
   machine.
8. The normal machine never saw the game's registry value.

    build/analysis-venv/bin/python tools/test_game_machine_host.py [--no-build]

Results in build/game-machine/results.json.
"""
from __future__ import annotations
if __name__ == '__main__':
    import sys as _sys
    if _sys.platform != 'darwin':
        # The write layer itself is the same code on Linux and runs in
        # tools/test_linux_game_machine.py.
        print('SKIP: the command line and the launcher bundles of game machines are the macOS host\'s; '
              'the Linux host makes game machines in its dialog')  # macOS only; skipped elsewhere
        _sys.exit(77)

import hashlib
import json
import os
import plistlib
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
import win98_c  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
from test_mac_app_host import clone_machine  # noqa: E402
from test_single_instance_host import locked, wait_for  # noqa: E402

IMPORTS = {
    'USER32': ['RegisterClassA@4', 'CreateWindowExA@48', 'DefWindowProcA@16', 'GetMessageA@16', 'DispatchMessageA@4',
               'ShowWindow@8', 'SetTimer@16', 'PostQuitMessage@4', 'FindWindowA@8', 'wsprintfA'],
    'GDI32': ['GetStockObject@4'],
    'KERNEL32': ['GetModuleHandleA@4', 'GetModuleFileNameA@12', 'GetCommandLineA@0', 'GetCurrentDirectoryA@8',
                 'CreateDirectoryA@8', 'CreateFileA@28', 'WriteFile@20', 'CloseHandle@4', 'GetFileAttributesA@4',
                 'CreateProcessA@40', 'ExitProcess@4', 'GetTickCount@0', 'DeleteFileA@4', 'MoveFileA@8',
                 'RemoveDirectoryA@4', 'SetFileAttributesA@8', 'GetShortPathNameA@12', 'FindFirstFileA@8',
                 'FindNextFileA@8', 'FindClose@4', 'ReadFile@20', 'SetFilePointer@16', 'Sleep@4',
                 'GetVolumeInformationA@32', 'GetDiskFreeSpaceA@20', 'CreateFileMappingA@24', 'MapViewOfFile@20',
                 'UnmapViewOfFile@4', 'LockFile@20', 'UnlockFile@20', 'FindFirstChangeNotificationA@12',
                 'FindCloseChangeNotification@4', 'WaitForSingleObject@8', 'WritePrivateProfileStringA@16',
                 'GetPrivateProfileStringA@24', 'WriteProfileStringA@12', 'GetLastError@0'],
    'ADVAPI32': ['RegCreateKeyExA@36', 'RegQueryValueExA@24', 'RegSetValueExA@24', 'RegCloseKey@4'],
    'SHELL32': ['ShellExecuteA@24'],
}
GAME = 'C:\\SPIELE\\GMGAME.EXE'


def build_guest(out, env):
    libraries = []
    for dll, names in IMPORTS.items():
        definition, library = out / f'{dll}.def', out / f'{dll}.lib'
        definition.write_text(f'LIBRARY {dll}.DLL\nEXPORTS\n' + ''.join(name + '\n' for name in names))
        subprocess.run(['llvm-dlltool', '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)],
                       env=env, check=True)
        libraries.append(str(library))
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                    '-fno-stack-protector', '-c', str(ROOT / 'tools/game_machine_fixture.c'), '-o',
                    str(out / 'fixture.obj')], env=env, check=True)
    program = out / 'GMGAME.EXE'
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/nodefaultlib', '/machine:x86',
                    '/timestamp:0', '/out:' + str(program), str(out / 'fixture.obj'), *libraries], env=env, check=True)
    return program


def values(path):
    """key=value lines of a file the fixture wrote; empty when it is not there."""
    if not path.is_file():
        return {}
    return dict(line.split('=', 1) for line in path.read_text(errors='replace').splitlines() if '=' in line)


def checksums(path):
    """Every file below path with its SHA-256, the sidecars included, and every directory."""
    listing = {}
    for entry in sorted(path.rglob('*')):
        name = str(entry.relative_to(path))
        if entry.is_symlink():
            listing[name] = 'link ' + os.readlink(entry)
        elif entry.is_dir():
            listing[name] = 'directory'
        else:
            digest = hashlib.sha256()
            with entry.open('rb') as handle:
                for block in iter(lambda: handle.read(1 << 20), b''):
                    digest.update(block)
            listing[name] = digest.hexdigest()
    return listing


def sidecar(directory):
    """name -> flags of the records of a directory's.neo98-index."""
    records = {}
    index = directory / '.neo98-index'
    if index.is_file():
        for line in index.read_text(errors='replace').splitlines():
            fields = line.split('\t')
            if not line.startswith('#') and len(fields) == 5:
                records[fields[4]] = int(fields[0])
    return records


# A DOS program: creates C:\GMBASE\DOS.TXT with the text DOS and ends.
DOS_PROGRAM = (bytes.fromhex('b43c31c9ba2001cd21721089c3b440b90300ba3201cd21b43ecd21b8004ccd21')
               + b'C:\\GMBASE\\DOS.TXT\0' + b'DOS')


def main():
    out = neo98.output_directory('game-machine')
    for name in ('settings', 'logs', 'tmp', 'machine', 'outside'):
        shutil.rmtree(out / name, ignore_errors=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    fixture = build_guest(out, neo98.tool_environment())
    machine = clone_machine(out / 'machine')
    drive_c = machine / 'C'
    (drive_c / 'SPIELE').mkdir()
    shutil.copyfile(fixture, drive_c / 'SPIELE' / 'GMGAME.EXE')
    settings_dir, logs = out / 'settings', out / 'logs'
    settings_dir.mkdir()
    (out / 'tmp').mkdir()
    # The seamless view, which a user turns on in View > Seamless.
    (settings_dir / 'settings.ini').write_text(f'# Test\nmachine = {machine}\nscale = 1\nseamless = 1\n')
    games = machine / 'Spielmaschinen'
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)
        if not condition:
            print(f'FAIL {name}', flush=True)

    base_environment = {'HOME': str(Path.home()), 'SDL_VIDEODRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy',
            'NEO98_SETTINGS_DIR': str(settings_dir), 'NEO98_LOG_DIR': str(logs), 'TMPDIR': str(out / 'tmp'),
            'NEO98_HOST_SCREEN': '1100x700', 'NEO98_HOST_DEADLINE': '600'}
    command_log = out / 'commands.log'
    command_log.write_text('')

    def host(arguments, environment=None, timeout=300):
        """One start of the host that ends by itself: exit code and output."""
        try:
            result = subprocess.run([str(binary), *arguments], cwd='/', env=dict(base_environment, **(environment or {})),
                                    capture_output=True, text=True, errors='replace', stdin=subprocess.DEVNULL,
                                    timeout=timeout)
            code, text = result.returncode, result.stdout + result.stderr
        except subprocess.TimeoutExpired as late:
            code, text = 'timeout', (late.stdout or b'').decode(errors='replace') if isinstance(late.stdout, bytes) else str(late.stdout or '')
        with command_log.open('a') as log:
            log.write(f'$ {" ".join(arguments)}\n{text}exit {code}\n\n')
        return code, text

    # The normal machine makes the files the layer test works on, so they
    # have their records in the sidecars like anything a program installed.
    code, text = host(['--exe', GAME, '--command', f'"{GAME}" prepare'])
    (out / 'prepare.log').write_text(text)
    base = drive_c / 'GMBASE'
    prepared = values(drive_c / 'GMTEST' / 'PREPARE.TXT')
    check('the normal machine prepared C:\\GMBASE', code == 0 and (base / 'BIG.BIN').stat().st_size == 5 * 1024 * 1024
          and sidecar(base).get('Long Name One.txt') == 0)
    (drive_c / 'SPIELE' / 'DOSW.COM').write_bytes(DOS_PROGRAM)

    # --- 1. Made from the command line ---------------------------------------
    started = time.time()
    code, text = host(['--game-machine-create', GAME, '--game-machine-name', 'Testspiel'])
    took = time.time() - started
    game = games / 'Testspiel'
    layer = game / 'C-Schicht'
    check('create ends with 0 and names the folder',
          code == 0 and f'NEO98 GAME MACHINE created name=Testspiel program={GAME} folder={game.resolve()}' in text)
    check('creating took less than five seconds', took < 5)
    check('the layer holds the two hives and nothing else of the drive',
          sorted(str(f.relative_to(layer)) for f in layer.rglob('*') if f.is_file())
          == ['.neo98-volume', 'WINDOWS/.neo98-index', 'WINDOWS/SYSTEM.DAT', 'WINDOWS/USER.DAT'])
    check('the hives are those of the normal machine, with their records of the sidecar',
          (layer / 'WINDOWS/SYSTEM.DAT').read_bytes() == (drive_c / 'WINDOWS/SYSTEM.DAT').read_bytes()
          and (layer / 'WINDOWS/USER.DAT').read_bytes() == (drive_c / 'WINDOWS/USER.DAT').read_bytes()
          and set(sidecar(layer / 'WINDOWS')) == {'SYSTEM.DAT', 'USER.DAT'})
    check('and the registry journal, when the machine has one',
          (game / 'C-Schicht.registry').is_file() == (machine / 'C.registry').is_file()
          and (not (machine / 'C.registry').is_file()
               or (game / 'C-Schicht.registry').read_bytes() == (machine / 'C.registry').read_bytes()))
    check('the game machines do not lie in drive C:', games.parent == machine and drive_c not in games.parents)
    stored = dict(line.split(' = ', 1) for line in (game / 'settings.ini').read_text().splitlines() if ' = ' in line)
    check('settings.ini of the game machine names the program and the source',
          stored.get('game.program') == GAME and stored.get('game.name') == 'Testspiel'
          and stored.get('game.source') == str(machine.resolve()) and stored.get('machine') == str(game.resolve())
          and stored.get('seamless') == '1' and stored.get('seamless.dock') == '0' and stored.get('drives.readonly') == '1')
    launcher = game / 'Testspiel.app'
    script = launcher / 'Contents/MacOS/spielmaschine'
    info = plistlib.loads((launcher / 'Contents/Info.plist').read_bytes()) if (launcher / 'Contents/Info.plist').is_file() else {}
    link = launcher.resolve() / 'Contents/MacOS/Testspiel'
    check('launcher bundle starts this host with the game machine, through its link to the host',
          script.is_file() and os.access(script, os.X_OK)
          and f"exec '{link}' --game-machine '{game.resolve()}'" in script.read_text()
          and link.is_symlink() and os.readlink(link) == str(binary.resolve())
          and info.get('CFBundleExecutable') == 'spielmaschine' and info.get('CFBundleName') == 'Testspiel')
    code, text = host(['--game-machine-create', GAME, '--game-machine-name', 'Testspiel'])
    check('a second one of the same name is refused', code == 15 and 'already exists' in text)
    code, text = host(['--game-machine-create', 'C:\\SPIELE\\FEHLT.EXE'])
    check('a program that is not there is refused', code == 15 and 'on any of the machine' in text)
    for name, arguments in (('Starter', 'launcher'), ('Shellstarter', 'shell'), ('Absturz', 'crash'),
                            ('Dauerlauf', 'stay'), ('Pausenspiel', 'stay'), ('Schicht', 'layer1')):
        code, text = host(['--game-machine-create', str(drive_c / 'SPIELE' / 'GMGAME.EXE'), '--game-machine-name', name,
                           '--game-machine-arguments', arguments])
        check(f'game machine {name} made from the file of this Mac', code == 0 and f'program={GAME} ' in text)
    code, text = host(['--game-machine-create', 'C:\\SPIELE\\DOSW.COM', '--game-machine-name', 'Dosspiel'])
    check('game machine for a DOS program', code == 0)
    code, text = host(['--game-machine-create', 'C:\\WINDOWS\\WINFILE.EXE', '--game-machine-name', 'Dateimanager'])
    check('game machine for a 16-bit program', code == 0)
    code, text = host(['--game-machine-list'])
    listed = re.findall(r'^NEO98 GAME MACHINE name=(\S+) running=0 program=', text, re.M)
    check('the list names all nine', listed == ['Absturz', 'Dateimanager', 'Dauerlauf', 'Dosspiel', 'Pausenspiel', 'Schicht',
                                                'Shellstarter', 'Starter', 'Testspiel'])

    # Files that come to the normal drive after the game machines were made.
    (drive_c / 'GMTEST' / 'SOURCE.TXT').write_text('from the regular machine\r\n')
    (base / 'ADDED.TXT').write_text('added later')
    (base / 'CHANGE.TXT').write_text('new content')

    # --- 2. The write layer ---------------------------------------------------
    folder = games / 'Schicht'
    upper = folder / 'C-Schicht'
    before = checksums(drive_c)
    with (out / 'Schicht-1.log').open('w') as log:
        process = subprocess.Popen([str(binary), '--game-machine', 'Schicht'], cwd='/', env=base_environment, stdout=log,
                                   stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
    # The game holds two files of the normal drive open; they go away there.
    held = _wait_file(upper / 'GMTEST' / 'HOLD.TXT', r'hold', 240)
    if held:
        (base / 'OPEN.TXT').unlink()
        (base / 'BIG2.BIN').unlink()
    try:
        code = process.wait(timeout=300)
    except subprocess.TimeoutExpired:
        process.terminate()
        code = 'timeout'
    text = (out / 'Schicht-1.log').read_text(errors='replace')
    after = checksums(drive_c)
    for name in ('GMBASE/OPEN.TXT', 'GMBASE/BIG2.BIN'):
        before.pop(name, None)
    one = values(upper / 'GMTEST' / 'LAYER1.TXT')
    (out / 'LAYER1.TXT').write_text(json.dumps(one, indent=1))
    check('the layer run ended by itself', code == 0 and held and 'NEO98 HOST game machine ends (the game has ended)' in text)
    check('drive C: is the normal drive below the layer',
          f'NEO98 HOST drive C: {upper.resolve()}\n' in text)
    check('the normal drive is unchanged, file for file (SHA-256, sidecars included)', before == after and len(after) > 3000)
    check('a file of the normal drive is read through the layer', one.get('read') == 'base read')
    check('a file added to the normal drive after creation is visible', one.get('added') == 'added later')
    check('a file changed in the normal drive after creation shows the new content', one.get('changed') == 'new content')
    check('a file of 5 MiB of the normal drive reads right at its start, middle and end', one.get('big_read') == '1')
    check('a mapping of a file of the normal drive', one.get('map_read') == '1')
    check('writing to a file of the normal drive', one.get('write_open') == '1' and one.get('write') == 'GAME write 0123456789 and more')
    check('a writable mapping of the file in the layer', one.get('map_write') == 'GAME Write 0123456789 and more')
    check('a range lock keeps another handle from reading', one.get('lock') == '1' and one.get('lock_read') == '33'
          and one.get('unlock') == '1')
    check('writing into the 5 MiB file changes five bytes of it', one.get('big_write') == '1'
          and (upper / 'GMBASE' / 'BIG.BIN').stat().st_size == 5 * 1024 * 1024)
    check('the written files lie in the layer', (upper / 'GMBASE' / 'WRITE.TXT').read_text() == 'GAME Write 0123456789 and more'
          and (base / 'WRITE.TXT').read_text() == 'base write 0123456789')
    check('deleting a file of the normal drive', one.get('delete') == '1' and one.get('delete_gone') == '1'
          and (base / 'DELETE.TXT').is_file())
    check('renaming a file of the normal drive', one.get('rename') == '1' and one.get('renamed') == 'base rename'
          and one.get('rename_gone') == '1' and (base / 'RENAME.TXT').is_file())
    check('a directory of the normal drive with a file in it is not removed', one.get('deldir_full') == '145')
    check('emptied, it goes, and made again it is empty', one.get('deldir') == '1' and one.get('deldir_gone') == '1'
          and one.get('deldir_again') == '1')
    check('renaming a directory of the normal drive takes its files along',
          one.get('movedir') == '1' and one.get('movedir_a') == 'moved a' and one.get('movedir_b') == 'moved b'
          and one.get('movedir_gone') == '1' and (base / 'MOVEDIR' / 'SUB' / 'B.TXT').is_file())
    check('attributes of a file of the normal drive', one.get('attributes') == '1')
    check('a new long name gets an alias the normal drive does not have',
          one.get('alias_one') == 'C:\\GMBASE\\LONGNA~1.TXT' and one.get('alias_two') == 'C:\\GMBASE\\LONGNA~2.TXT'
          and one.get('alias_one_reads') == 'one')
    check('change notification on a directory of the normal drive', one.get('notify') == '1')
    check('profile file of the normal drive: old key read, new key written',
          one.get('ini_write') == '1' and one.get('ini_old') == 'v' and one.get('ini_new') == 'yes'
          and 'game' not in (base / 'BASE.INI').read_text())
    check('WIN.INI is written in the layer', one.get('winini') == '1'
          and 'NEO98GameMachine' in (upper / 'WINDOWS' / 'WIN.INI').read_text(errors='replace')
          and 'NEO98GameMachine' not in (drive_c / 'WINDOWS' / 'WIN.INI').read_text(errors='replace'))
    listed_names = sorted(entry.split('/')[0] for entry in one.get('names', '').split(';') if entry)
    check('the listing merges both sides: sixteen entries, none twice',
          one.get('twice') == '0' and one.get('count') == '16'
          and listed_names == sorted(['READ.TXT', 'WRITE.TXT', 'RENAMED.TXT', 'ATTR.TXT', 'Long Name One.txt',
                                      'Long Name Two.txt', 'OPEN.TXT', 'CHANGE.TXT', 'SHADOW.TXT', 'BASE.INI', 'DELDIR',
                                      'MOVED', 'BIG.BIN', 'BIG2.BIN', 'ADDED.TXT', 'NOTIFY.TXT']))
    check('serial number and free space as for the normal drive',
          one.get('serial') == prepared.get('serial') and one.get('serial') not in (None, '0') and one.get('space') == '1')
    check('files deleted in the normal drive while the game holds them open stay readable',
          one.get('held_before') == '1' and one.get('held_after') == ' open, read while it goes away'
          and one.get('held_big_after') == '1')
    records = sidecar(upper / 'GMBASE')
    check('the tombstones are records of the layer\'s sidecar',
          records.get('DELETE.TXT') == 1 and records.get('RENAME.TXT') == 1 and records.get('MOVEDIR') == 1
          and records.get('DELDIR') == 10 and records.get('RENAMED.TXT') == 0)
    # The second start. Meanwhile the normal drive gets one more file, and
    # a file the game wrote changes there.
    settings_file = folder / 'settings.ini'
    settings_file.write_text(settings_file.read_text().replace('game.arguments = layer1', 'game.arguments = layer2'))
    (base / 'ADDED2.TXT').write_text('added between the runs')
    (base / 'SHADOW.TXT').write_text('base changed later')
    before = checksums(drive_c)
    code, text = host(['--game-machine', 'Schicht'])
    (out / 'Schicht-2.log').write_text(text)
    two = values(upper / 'GMTEST' / 'LAYER2.TXT')
    (out / 'LAYER2.TXT').write_text(json.dumps(two, indent=1))
    check('second start: the normal drive is unchanged again', code == 0 and checksums(drive_c) == before)
    check('second start: what was written is still there',
          two.get('write') == 'GAME Write 0123456789 and more' and two.get('big') == '1' and two.get('ini_new') == 'yes'
          and two.get('read') == 'base read')
    check('second start: deleted and renamed files stay so',
          two.get('delete_gone') == '1' and two.get('rename_gone') == '1' and two.get('renamed') == 'base rename')
    check('second start: the directory made again holds only the new file',
          two.get('deldir') == '1' and two.get('deldir_new') == 'new')
    check('second start: the renamed directory is where it went',
          two.get('movedir_gone') == '1' and two.get('movedir_a') == 'moved a' and two.get('movedir_b') == 'moved b')
    check('second start: attributes and aliases are the same',
          two.get('attributes') == '3' and two.get('alias_one') == one.get('alias_one') and two.get('alias_two') == one.get('alias_two'))
    check('second start: a file added meanwhile is visible, files deleted there are gone',
          two.get('added2') == 'added between the runs' and two.get('held_gone') == '1')
    check('second start: a file the game wrote hides the later change in the normal drive', two.get('shadow') == 'game shadow')
    check('second start: fifteen entries, none twice, same serial number',
          two.get('twice') == '0' and two.get('count') == '15' and two.get('serial') == one.get('serial'))

    # --- 3. The game runs in its machine -------------------------------------
    started = time.time()
    code, text = host(['--game-machine', 'Testspiel'])
    took = time.time() - started
    (out / 'game-1.log').write_text(text)
    save = values(layer / 'GMTEST' / 'SAVE.TXT')
    check('game machine names itself and reads its own settings',
          f'NEO98 HOST app settings={game.resolve()}/settings.ini machine={game.resolve()}' in text
          and f'NEO98 HOST game machine name=Testspiel program={GAME}' in text)
    check('the layer and its journal are the game machine\'s',
          f'NEO98 HOST drive C: {layer.resolve()}\n' in text and f'NEO98 HOST registry {game.resolve()}/C-Schicht.registry' in text)
    check('the game ran: exit code 7, the machine ended by itself',
          code == 0 and 'NEO98 HOST exited code=7' in text and 'NEO98 HOST game machine ends (the game has ended)' in text)
    check('and without the deadline', 'deadline' not in text and took < 120)
    check('the game wrote its file in the game machine', save.get('mode') == 'game' and save.get('self', '').upper() == GAME)
    check('the game started in its own folder', save.get('directory', '').upper() == 'C:\\SPIELE')
    check('the game wrote its registry values', save.get('registry') == '0,0' and save.get('score') == '1')
    check('no shell runs in the game machine', save.get('tray') == '0')
    check('the file is not in the normal machine', not (drive_c / 'GMTEST' / 'SAVE.TXT').exists())
    check('a later file of the normal machine is visible in the game machine', save.get('source_marker') == '1')
    check('the lock of the game machine is free again',
          (game / 'C-Schicht.host-lock').exists() and not locked(game / 'C-Schicht.host-lock'))
    check('the normal machine was not locked', not locked(machine / 'C.host-lock'))
    check('its log folder is its own', 'log=stdout' in text and not (logs / 'host-session.log').exists())
    code, text = host(['--game-machine', str(game)])
    (out / 'game-2.log').write_text(text)
    save = values(layer / 'GMTEST' / 'SAVE.TXT')
    check('second run, by folder: the registry value of the first is still there', code == 0 and save.get('score') == '2')

    # --- 4. A launcher that leaves first -------------------------------------
    for name, record, way in (('Starter', 'LAUNCH.TXT', 'CreateProcess'), ('Shellstarter', 'SHELL.TXT', 'ShellExecute')):
        folder = games / name
        code, text = host(['--game-machine', name])
        (out / f'{name}.log').write_text(text)
        first, child = values(folder / 'C-Schicht' / 'GMTEST' / record), values(folder / 'C-Schicht' / 'GMTEST' / 'CHILD.TXT')
        check(f'{way}: the first process started the second',
              first.get('result') not in (None, '0') and (way == 'CreateProcess' or int(first.get('result', '0')) > 32))
        check(f'{way}: the machine went on after the first process left',
              re.search(r'^NEO98 HOST game machine: first process exited code=3, 1 more running$', text, re.M))
        check(f'{way}: the second process ran to its end, three seconds later',
              child.get('mode') == 'child' and int(child.get('tick', '0')) - int(first.get('tick', '0')) >= 2900)
        check(f'{way}: then the machine ended by itself',
              code == 0 and 'NEO98 HOST game machine ends (the game has ended)' in text and 'deadline' not in text)

    # --- 5. A game that faults -----------------------------------------------
    folder = games / 'Absturz'
    code, text = host(['--game-machine', 'Absturz'],
                      {'NEO98_HOST_INPUT': '0:wait=#32770|Gmgame;1:idle=500;1:key=0d'})
    (out / 'Absturz.log').write_text(text)
    reports = sorted((logs / 'Spielmaschinen' / 'Absturz' / 'crash-reports').glob('*/report.json'))
    check('the fault shows the error box of Win98', 'NEO98 HOST input wait=#32770|Gmgame reached' in text)
    check('after the box the machine ends by itself',
          code == 0 and 'NEO98 HOST game machine ends' in text and 'deadline' not in text
          and values(folder / 'C-Schicht' / 'GMTEST' / 'CRASH.TXT').get('mode') == 'crash')
    check('the crash report lies in the log folder of the game machine', len(reports) == 1)

    # --- 6. A DOS program and a 16-bit program ---------------------------------
    upper = games / 'Dosspiel' / 'C-Schicht'
    before = checksums(drive_c)
    # In a game machine the window of a DOS program goes with the program;
    # nobody has to close a "Finished" window. More of DOS
    # programs in tools/test_game_machine_files_host.py.
    code, text = host(['--game-machine', 'Dosspiel'], timeout=240)
    (out / 'Dosspiel.log').write_text(text)
    check('the DOS program ran and the machine ended by itself, without a window to close',
          code == 0 and 'NEO98 HOST game machine ends' in text and 'deadline' not in text)
    check('the DOS program wrote its file into the layer',
          (upper / 'GMBASE' / 'DOS.TXT').is_file() and (upper / 'GMBASE' / 'DOS.TXT').read_bytes() == b'DOS'
          and not (drive_c / 'GMBASE' / 'DOS.TXT').exists())
    upper = games / 'Dateimanager' / 'C-Schicht'
    code, text = host(['--game-machine', 'Dateimanager'],
                      {'NEO98_HOST_INPUT': '0:wait=|File Manager;2:idle=500;1:close=File Manager;'
                                           '0:wait=|Exit Windows;1:key=0d'}, timeout=400)
    (out / 'Dateimanager.log').write_text(text)
    # File Manager as the first program of a machine is its shell (as with
    # shell=winfile.exe): closing it asks to exit Windows (a log off), and
    # that ends a game machine instead of bringing it up again.
    check('the 16-bit File Manager ran and the machine ended when it was closed',
          code == 0 and 'wait=|File Manager reached' in text and 'wait=|Exit Windows reached' in text
          and text.count('NEO98 HOST exitwindows') == 1 and 'NEO98 HOST reboot' not in text and 'deadline' not in text)
    check('the 16-bit program wrote WINFILE.INI into the layer',
          (upper / 'WINDOWS' / 'WINFILE.INI').is_file() and not (drive_c / 'WINDOWS' / 'WINFILE.INI').exists())
    check('neither changed the normal drive', checksums(drive_c) == before)

    # --- 7. The normal machine with game machines beside it ------------------
    calc = win98_c.path('C:\\WINDOWS\\CALC.EXE', drive_c)
    steps = ['0:wait=Shell_TrayWnd', '1:idle=1500', '1:menu=describe',
             f'1:menu=game-machine-create={GAME}|Menu game',
             f'1:open={calc}', '0:wait=SciCalc', '3:menu=describe', '1:menu=game-machine-create-front=Rechner',
             '1:menu=game-machine-start=Dauerlauf', '12:menu=game-machine-delete=Dauerlauf',
             '0:wait=Notepad',  # opened in a game machine, handed over to this one
             '45:menu=game-machine-delete=Dauerlauf', '1:menu=game-machine-reveal=Menu game',
             '1:menu=game-machine-delete=Menu game', '1:menu=describe', '1:menu=quit']
    normal_log = out / 'normal.log'
    stay, pause = games / 'Dauerlauf', games / 'Pausenspiel'
    stay_log = logs / 'Spielmaschinen' / 'Dauerlauf' / 'host-session.log'
    with normal_log.open('w') as log:
        normal = subprocess.Popen([str(binary)], cwd='/', env=dict(base_environment, NEO98_HOST_INPUT=';'.join(steps)),
                                  stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
    paused = None
    try:
        check('normal machine reaches the desktop', wait_for(normal_log, r'^NEO98 HOST input wait=Shell_TrayWnd reached'))
        code, text = host(['--game-machine-create', GAME, '--game-machine-name', 'Nebenher'])
        check('the command line does not clone a machine that runs', code == 14 and 'the machine is running' in text)
        check('menu makes a game machine while the machine runs',
              wait_for(normal_log, r'^NEO98 HOST game machine created name=Menu game program=C:\\SPIELE\\GMGAME.EXE ', 120))
        check('menu makes one for the program in front, named by the test',
              wait_for(normal_log, r'^NEO98 HOST game machine created name=Rechner program=C:\\WINDOWS\\CALC.EXE ', 300))
        check('menu starts a game machine', wait_for(normal_log, r'^NEO98 HOST game machine start name=Dauerlauf ok$', 60))
        check('it runs as a host of its own with its own log',
              wait_for(normal_log, r'.', 1) and _wait_file(stay_log, r'^NEO98 HOST running ', 120))
        check('started from the menu, macOS knows it by the name of the game and as its launcher bundle',
              f'NEO98 HOST game machine application name=Dauerlauf bundle={stay.resolve()}/Dauerlauf.app\n'
              in stay_log.read_text(errors='replace'))
        check('both locks are held, by two processes',
              locked(machine / 'C.host-lock') and locked(stay / 'C-Schicht.host-lock')
              and (machine / 'C.host-lock').read_text().strip() == str(normal.pid)
              and (stay / 'C-Schicht.host-lock').read_text().strip() not in ('', str(normal.pid)))
        code, text = host(['--game-machine', 'Dauerlauf'])
        check('a second start of the running game machine hands over and ends',
              code == 0 and re.search(r'runs in process \d+; handed over: front', text))
        check('the running game machine comes to the front', _wait_file(stay_log, r'^NEO98 HOST second start: front$', 30))
        code, text = host(['--game-machine-list'])
        check('the list says which one runs', re.search(r'^NEO98 GAME MACHINE name=Dauerlauf running=1 ', text, re.M)
              and re.search(r'^NEO98 GAME MACHINE name=Testspiel running=0 ', text, re.M))
        # A third machine, started here so that it has a script: pause, and a
        # file opened in it.
        notepad = out / 'outside' / 'NOTEPAD.EXE'
        notepad.parent.mkdir()
        shutil.copyfile(win98_c.path('C:\\WINDOWS\\NOTEPAD.EXE', drive_c), notepad)
        pause_log = out / 'Pausenspiel.log'
        with pause_log.open('w') as log:
            paused = subprocess.Popen([str(binary), '--game-machine', 'Pausenspiel'], cwd='/',
                                      env=dict(base_environment, NEO98_HOST_INPUT=';'.join(
                                          ['0:wait=|Game machine stays open', '1:menu=describe', '1:menu=pause', '3:menu=pause',
                                           f'1:open={notepad}', '3:menu=quit'])),
                                      stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        check('the third machine ends on Quit', paused.wait(timeout=300) == 0)
        text = pause_log.read_text(errors='replace')
        check('three machines ran side by side', 'wait=|Game machine stays open reached' in text and normal.poll() is None)
        check('Pause stops and continues a game machine',
              re.search(r'^NEO98 HOST pause on \(menu\)', text, re.M) and re.search(r'^NEO98 HOST pause off \(menu\) stood=[1-3]\.', text, re.M)
              and 'NEO98 HOST title Pausenspiel [Pause]' in text)
        own_items = [line for line in text.splitlines() if line.startswith('NEO98 HOST menu-item ')]
        check('a game machine has the menu of the machine but none for game machines',
              any('Machine > Pause [pause]' in line for line in own_items)
              and not any('game-machine' in line for line in own_items))
        check('a file opened in a game machine is handed to the normal machine',
              re.search(r'^NEO98 HOST game machine: open .*NOTEPAD\.EXE handed to the normal machine ', text, re.M)
              and wait_for(normal_log, r'^NEO98 HOST input wait=Notepad reached', 120))
        check('delete is refused while the game machine runs',
              wait_for(normal_log, r'^NEO98 HOST game machine delete failed: The game machine is still running', 5) and stay.is_dir())
        check('the started game machine ended by itself with its game',
              _wait_file(stay_log, r'^NEO98 HOST game machine ends \(the game has ended\)$', 120)
              and values(stay / 'C-Schicht' / 'GMTEST' / 'END.TXT').get('mode') == 'stay')
        check('menu deletes game machines',
              wait_for(normal_log, r'^NEO98 HOST game machine deleted name=Menu game ', 200)
              and not stay.exists() and not (games / 'Menu game').exists())
        check('normal machine ends in order', normal.wait(timeout=300) == 0)
    finally:
        for process in (normal, paused):
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    pass
    log = normal_log.read_text(errors='replace')
    items = [line for line in log.splitlines() if line.startswith('NEO98 HOST menu-item ')]
    check('menu: Machine > Game machines with both ways to make one',
          any('Machine > Game machines > New game machine for a program … [game-machine-create]' in line for line in items)
          and any('New game machine for the foreground program [game-machine-create-front] (disabled)' in line for line in items)
          and any('New game machine for "CALC.EXE" … [game-machine-create-front]' in line and '(disabled)' not in line for line in items))
    check('menu: each game machine with Start, Show in Finder, Delete',
          all(any(f'Machine > Game machines > Testspiel > {title} [{name}]' in line for line in items)
              for title, name in (('Start', 'game-machine-start'), ('Show in Finder', 'game-machine-reveal'),
                                  ('Delete …', 'game-machine-delete'))))
    # A program of Windows in a game machine: its window comes up without a
    # shell, its icon becomes the icon of the machine, closing it ends the machine.
    calculator = games / 'Rechner'
    check('made from the menu, the game machine has the icon of its program file at once',
          re.search(r'^NEO98 HOST game machine icon 32x32 from the file .*/WINDOWS/CALC\.EXE$', log, re.M)
          and (calculator / 'Symbol.icns').is_file()
          and (calculator / 'Rechner.app/Contents/Resources/icon.icns').read_bytes() == (calculator / 'Symbol.icns').read_bytes())
    # Without it (a program whose file has no icon) the first window gives one.
    (calculator / 'Symbol.icns').unlink(missing_ok=True)
    code, text = host(['--game-machine', 'Rechner'],
                      {'NEO98_HOST_INPUT': '0:wait=SciCalc;2:idle=500;1:close=Calculator;0:close=Rechner'})
    (out / 'Rechner.log').write_text(text)
    check('Calc runs in its game machine and the machine ends when its window is closed',
          code == 0 and 'NEO98 HOST input wait=SciCalc reached' in text and 'NEO98 HOST exited code=0' in text
          and 'NEO98 HOST game machine ends (the game has ended)' in text and 'deadline' not in text)
    check('without an icon from the file, the icon of the first window is the icon of the machine and of the launcher',
          re.search(r'^NEO98 HOST game machine icon 32x32 of C:\\WINDOWS\\CALC.EXE written$', text, re.M)
          and (calculator / 'Symbol.icns').read_bytes()[:4] == b'icns'
          and (calculator / 'Rechner.app/Contents/Resources/icon.icns').read_bytes() == (calculator / 'Symbol.icns').read_bytes())
    code, text = host(['--game-machine-create', 'C:\\WINDOWS\\CALC.EXE'])
    named = re.search(r'^NEO98 GAME MACHINE created name=(.*) program=C:\\WINDOWS\\CALC.EXE ', text, re.M)
    check('without a name the version resource of the program names the game machine',
          code == 0 and named and named[1] == 'Windows Calculator application file'
          and (games / named[1] / 'settings.ini').is_file())
    code, text = host(['--game-machine-delete', 'Windows Calculator application file'])
    check('and that one is deleted again', code == 0 and not (games / 'Windows Calculator application file').exists())
    waits = re.findall(r'^NEO98 HOST input wait=(.*) (reached|timed out)', log, re.M)
    check('every wait of the normal machine was reached', len(waits) == 3 and all(state == 'reached' for _, state in waits))
    check('no host failure', 'NEO98 HOST failed' not in log and 'libc++abi' not in log)
    code, text = host(['--game-machine-list'])
    check('nine game machines are left and no lock is held', re.findall(r'^NEO98 GAME MACHINE name=(\S+) running=0 ', text, re.M)
          == ['Absturz', 'Dateimanager', 'Dosspiel', 'Pausenspiel', 'Rechner', 'Schicht', 'Shellstarter', 'Starter', 'Testspiel']
          and not locked(machine / 'C.host-lock'))

    # --- 8. The normal machine never saw the game ----------------------------
    code, text = host(['--exe', GAME])
    (out / 'normal-game.log').write_text(text)
    save = values(drive_c / 'GMTEST' / 'SAVE.TXT')
    check('the game in the normal machine starts its count at one', code == 0 and save.get('score') == '1'
          and save.get('source_marker') == '1')
    check('and the game machine keeps its own', values(layer / 'GMTEST' / 'SAVE.TXT').get('score') == '2')
    code, text = host(['--game-machine-delete', 'Testspiel'])
    check('the command line deletes a game machine', code == 0 and not game.exists())
    code, text = host(['--game-machine-delete', 'Testspiel'])
    check('and says when there is none', code == 15 and 'No game machine named' in text)

    failed = [name for name, okay in checks.items() if not okay]
    if not failed:
        for name in ('machine', 'tmp'):
            shutil.rmtree(out / name, ignore_errors=True)
    (out / 'results.json').write_text(json.dumps({'checks': checks, 'logs': str(out)}, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if failed:
        print(f'{len(failed)} of {len(checks)} checks failed; logs in {out}')
        return 1
    print(f'game machine: {len(checks)} checks passed')
    return 0


def _wait_file(path, pattern, timeout):
    """wait_for for a log that may not exist yet."""
    end = time.time() + timeout
    while time.time() < end:
        if path.is_file() and re.search(pattern, path.read_text(errors='replace'), re.M):
            return True
        time.sleep(0.5)
    return False


if __name__ == '__main__':
    sys.exit(main())
