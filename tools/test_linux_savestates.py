#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the DOS state and disk layers through the Linux GUI host."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(1, str(Path(__file__).resolve().parent))
import neo98
from native_app import ROOT
import win98_c


def ini(path):
    values = {}
    for line in path.read_text().splitlines():
        if ' = ' in line:
            key, value = line.split(' = ', 1)
            values.setdefault(key, []).append(value)
    return values


def main():
    pristine = win98_c.use_pristine()
    out = ROOT / 'build/native-linux/savestates'
    out.mkdir(parents=True, exist_ok=True)
    source = out / ('source-' + str(os.getpid()))
    subprocess.run(['cp', '-a', '--reflink=auto', str(pristine), str(source)], check=True)
    (source / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    game = source / 'SSGAME'
    for part in ('SUB', 'OTHER', 'FIND'):
        (game / part).mkdir(parents=True)
    for number in range(1, 6):
        (game / 'FIND' / f'F{number}.TXT').write_text(f'file {number}')
    env = neo98.tool_environment()
    env['PATH'] = str(ROOT / 'build/toolchain/usr/bin') + os.pathsep + env.get('PATH', '')
    subprocess.run(['nasm', '-f', 'bin', str(ROOT / 'tools/savestate_dos_fixture.asm'), '-o', str(game / 'SSGAME.COM')], env=env, check=True)
    helper_source = out / 'create.cpp'
    helper_source.write_text(r'''
#include "tools/host_game_machine.h"
int main(int argc, char **argv) {
    HostGameMachine::Request make; make.machine=argv[1]; make.program="C:\\SSGAME\\SSGAME.COM";
    make.name="LinuxStates"; make.source.seamless=false;
    if(argc>3) { make.program="D:\\SSGAME.COM"; make.name="LinuxDStates"; make.source.drives[3]=argv[3]; }
    HostGameMachine::Result result;
    if(!HostGameMachine::Create(make,argv[2],result)) { fprintf(stderr,"%s\n",result.error.c_str()); return 1; }
    puts(result.folder.c_str());
}
''')
    helper = out / 'create'
    subprocess.run(['clang++', '-std=c++17', '-O1', '-I'+str(ROOT), str(helper_source), '-o', str(helper)], env=env, check=True)
    binary = ROOT / 'build/native-app/neo98-host'
    folder = Path(subprocess.check_output([str(helper), str(source), str(binary)], text=True).strip())
    layer = folder / 'C-Schicht'
    env = dict(os.environ, SDL_VIDEODRIVER='x11', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT_ONCE='1', NEO98_CLIPBOARD='private',
               NEO98_LOG_DIR=str(out / 'logs'), NEO98_HOST_INPUT=
               '5:key=41;5.2:key=41;5.4:key=41;6:menu=state-save,A;9:key=41;9.2:key=44;9.4:key=43;9.6:key=46;10:key=57;11:menu=state-load;15:pause=0;16:key=57;17:key=41;18:key=57;19:menu=quit')
    for key in ('NEO98_LOG_TO_FILE', 'NEO98_SETTINGS_FILE'):
        env.pop(key, None)
    log = out / 'basic.log'
    with log.open('w') as stream:
        result = subprocess.run([str(binary), '--game-machine', str(folder), '--time-limit', '40'], cwd=ROOT,
                                env=env, stdout=stream, stderr=stream, timeout=70)
    text = log.read_text()
    assert result.returncode == 0, text[-5000:]
    assert 'state loaded id=' in text, text[-5000:]
    reports = sorted((layer / 'SSGAME/SUB').glob('R*.TXT'))
    assert len(reports) == 2, [(str(p), p.read_text()) for p in layer.rglob('R*.TXT')]
    assert reports[0].read_text().startswith('score=03 pos=000F keep=1 data=f'), reports[0].read_text()
    assert reports[1].read_text().startswith('score=04 pos=0014 keep=1 data=f'), reports[1].read_text()
    assert not (layer / 'SSGAME/OTHER/R0.TXT').exists()
    states = [p for p in (folder / 'Zustände').iterdir() if p.is_dir() and not p.name.startswith('.')]
    manual = next(p for p in states if ini(p / 'zustand.ini')['kind'] == ['manual'])
    automatic = next(p for p in states if ini(p / 'zustand.ini')['kind'] == ['auto'])
    assert (manual / 'Bild.png').is_file()
    assert (automatic / 'SSGAME').exists() is False
    assert (automatic / 'C-Schicht/SSGAME/OTHER/R0.TXT').read_text().startswith('score=04 pos=0014 keep=0 data=s')
    assert (manual / 'C-Schicht/SSGAME/KEEP.TXT').read_text() == 'keep'
    assert not (game / 'LOG.TXT').exists(), 'Original drive was changed'
    # A corrupted CPU image must be refused before swapping any disk layer.
    machine_file = manual / 'Maschine.bin'; original = machine_file.read_bytes()
    before = {str(p.relative_to(layer)): hashlib.sha256(p.read_bytes()).hexdigest() for p in layer.rglob('*') if p.is_file()}
    machine_file.write_bytes(bytes([original[0] ^ 1]) + original[1:])
    try:
        with (out / 'corrupt.log').open('w') as stream:
            failed = subprocess.run([str(binary), '--game-machine', str(folder), '--load-state', manual.name],
                cwd=ROOT, env=env, stdout=stream, stderr=stream, timeout=30)
        assert failed.returncode == 1
        assert 'damaged' in (out / 'corrupt.log').read_text()
        assert before == {str(p.relative_to(layer)): hashlib.sha256(p.read_bytes()).hexdigest() for p in layer.rglob('*') if p.is_file()}
    finally: machine_file.write_bytes(original)
    # DOS must read the original D: through its profile's write layer.
    d_folder = Path(subprocess.check_output([str(helper), str(source), str(binary), str(game)], text=True).strip())
    d_env = dict(env, NEO98_HOST_INPUT='4:key=57;5:menu=state-save,D;8:menu=quit')
    with (out / 'drive-d.log').open('w') as stream:
        drive_d = subprocess.run([str(binary), '--game-machine', str(d_folder), '--time-limit', '20'],
            cwd=ROOT, env=d_env, stdout=stream, stderr=stream, timeout=30)
    assert drive_d.returncode == 0, (out / 'drive-d.log').read_text()[-3000:]
    assert (d_folder / 'D-Schicht/R0.TXT').is_file()
    assert not (game / 'R0.TXT').exists()
    assert 'state saved id=' in (out / 'drive-d.log').read_text()
    # Capture the actual GTK list, restricted to this process's windows.
    from linux_x11_test import X11
    x = X11()
    with (out / 'list.log').open('w') as stream:
        process = subprocess.Popen([str(binary), '--game-machine', str(folder), '--time-limit', '15'],
            cwd=ROOT, env=dict(env, NEO98_HOST_INPUT='4:menu=states;5:menu=state-save,List;9:menu=quit'), stdout=stream, stderr=stream)
        try:
            deadline = time.monotonic() + 12
            while time.monotonic() < deadline:
                view = next((w for w in x.windows(process.pid) if 'States' in w['title']), None)
                if view: break
                time.sleep(.1)
            assert view, 'GTK state list did not appear'
            x.capture(view['id'], out / 'state-list.png')
            assert process.wait(timeout=20) == 0
            assert 'state saved id=' in (out / 'list.log').read_text()
        finally:
            if process.poll() is None: process.terminate(); process.wait(timeout=15)
    assert not win98_c.pristine_problems(pristine)
    (out / 'results.json').write_text(json.dumps({'profile': str(folder), 'ram_restored': True,
        'open_file_position_restored': True, 'directory_restored': True, 'deleted_file_restored': True,
        'automatic_backup': True, 'picture_saved': True, 'source_unchanged': True,
        'corrupt_machine_refused_before_restore': True, 'dos_reads_original_drive_d': True,
        'gtk_state_list': True, 'save_while_list_paused': True}, indent=2) + '\n')
    print('LINUX SAVESTATES PASS: RAM, open file, directory, disk layers, automatic backup, picture')

if __name__ == '__main__':
    main()
