#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Resume a DOS game with its original Windows property sheet open."""
import json
import os
from pathlib import Path
import subprocess
import time
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(1, str(Path(__file__).resolve().parent))
import neo98
from native_app import ROOT
from linux_x11_test import X11
import win98_c


def main():
    root = win98_c.use_pristine(); out = ROOT / 'build/native-linux/dos-settings-test'; out.mkdir(parents=True, exist_ok=True)
    programs = out / 'programs'; programs.mkdir(exist_ok=True)
    (programs / 'HELLO.COM').write_bytes(bytes.fromhex('b400cd16b8004ccd21'))
    helper_source = out / 'create.cpp'
    helper_source.write_text(r'''
#include "tools/host_game_machine.h"
int main(int argc,char **argv) {
    HostGameMachine::Request make; make.machine=argv[1]; make.program="D:\\HELLO.COM";
    make.name="LinuxDOSSettings-"+std::to_string(getpid()); make.source.drives[3]=argv[2]; make.source.seamless=true;
    HostGameMachine::Result result;
    if(!HostGameMachine::Create(make,argv[3],result)) { fprintf(stderr,"%s\n",result.error.c_str()); return 1; }
    puts(result.folder.c_str());
}
''')
    helper = out / 'create'; binary = ROOT / 'build/native-app/neo98-host'
    subprocess.run(['clang++', '-std=c++17', '-O1', '-I'+str(ROOT), str(helper_source), '-o', str(helper)],
        env=neo98.tool_environment(), check=True)
    profile = Path(subprocess.check_output([str(helper), str(root), str(programs), str(binary)], text=True).strip())
    env = dict(os.environ, SDL_VIDEODRIVER='x11', SDL_AUDIODRIVER='dummy', NEO98_CLIPBOARD='private',
        NEO98_HOST_INPUT_ONCE='1', NEO98_HOST_INPUT='4:menu=dos-settings;6:menu=state-save,PIF;10:menu=state-load;18:menu=quit')
    for name in ('NEO98_SETTINGS_FILE', 'NEO98_LOG_TO_FILE'): env.pop(name, None)
    x = X11(); log = out / 'host.log'
    with log.open('w') as stream:
        process = subprocess.Popen([str(binary), '--game-machine', str(profile), '--time-limit', '30', '--verbose'],
            cwd=ROOT, env=env, stdout=stream, stderr=stream)
        try:
            deadline = time.monotonic() + 14; properties = None
            while time.monotonic() < deadline:
                properties = next((w for w in x.windows(process.pid) if 'HELLO Properties' in w['title']), None)
                if properties: break
                time.sleep(.1)
            assert properties, log.read_text()[-3000:]
            x.capture(properties['id'], out / 'original-properties.png')
            deadline = time.monotonic()+25
            while time.monotonic()<deadline:
                if 'state loaded id=' in log.read_text(): break
                if process.poll() is not None: break
                time.sleep(.1)
            assert 'state loaded id=' in log.read_text(),log.read_text()[-4000:]
            deadline=time.monotonic()+3; properties=None
            while time.monotonic()<deadline:
                properties=next((w for w in x.windows(process.pid) if 'HELLO Properties' in w['title']),None)
                if properties: break
                time.sleep(.05)
            assert properties,log.read_text()[-4000:]
            x.capture(properties['id'],out / 'restored-properties.png')
            x.click(properties['x']+238,properties['y']+415)
            deadline=time.monotonic()+5
            while time.monotonic()<deadline:
                if not any('HELLO Properties' in w['title'] for w in x.windows(process.pid)): break
                time.sleep(.1)
            assert not any('HELLO Properties' in w['title'] for w in x.windows(process.pid)),log.read_text()[-4000:]
            assert process.wait(timeout=25)==0,log.read_text()[-4000:]
        finally:
            if process.poll() is None: process.terminate(); process.wait(timeout=15)
    assert 'game=dos-ui' in log.read_text(),log.read_text()[-4000:]
    assert 'windows=3' in log.read_text(), 'Restored sheet did not return to the DOS window'
    assert 'modal dialog end result=0' in log.read_text(),log.read_text()[-4000:]
    assert not win98_c.pristine_problems(root)
    (out / 'results.json').write_text(json.dumps({'original_property_sheet': True, 'first_dos_process': True,
        'seamless_window': True, 'combined_dos_and_property_sheet_resumed': True, 'source_unchanged': True}, indent=2)+'\n')
    print('LINUX DOS SETTINGS PASS: original PIF sheet, first DOS process, seamless window, combined state resumed')

if __name__ == '__main__': main()
