#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Boot a game profile and verify that its files and registry stay in its layers."""
import json
import os
from pathlib import Path
import subprocess
from native_app import ROOT
from test_host_clipboard import build_fixture
import neo98
import win98_c

def main():
    root=win98_c.use_pristine(); out=ROOT/'build/native-linux/game-test'; out.mkdir(exist_ok=True)
    prog=out/'programs'; prog.mkdir(exist_ok=True)
    (prog/'CLIPGET.EXE').write_bytes(build_fixture(out).read_bytes())
    assert not (prog/'CLIPGET.OUT').exists()
    source=out/'create.cpp'
    source.write_text(r'''
#include "tools/host_game_machine.h"
#include <cstdio>
int main(int argc, char **argv) {
    if(argc!=4)return 2;
    HostGameMachine::Request make;
    make.machine=argv[1];make.program="D:\\CLIPGET.EXE";
    make.name="LinuxTest-"+std::to_string(getpid());
    make.source.drives[3]=argv[2];make.source.seamless=false;
    HostGameMachine::Result result;
    if(!HostGameMachine::Create(make,argv[3],result)){
        fprintf(stderr,"%s\n",result.error.c_str());return 1;
    }
    puts(result.folder.c_str());return 0;
}
''')
    helper=out/'create'; binary=ROOT/'build/native-app/neo98-host'
    subprocess.run(['clang++','-std=c++17','-O1','-I'+str(ROOT),str(source),'-o',str(helper)],check=True,env=neo98.tool_environment())
    folder=Path(subprocess.check_output([str(helper),str(root),str(prog),str(binary)],text=True).strip())
    (out/'folder.txt').write_text(str(folder))
    env=dict(os.environ,NEO98_CLIPBOARD='private',NEO98_HOST_INPUT='2:menu=quit',NEO98_TRACE_CPU='1')
    for name in ('NEO98_LOG_TO_FILE','NEO98_SETTINGS_FILE'):env.pop(name,None)
    with (out/'host.log').open('w') as log:
        run=subprocess.run([str(binary),'--game-machine',str(folder),'--time-limit','15'],cwd=ROOT,env=env,
                           stdout=log,stderr=log,timeout=30)
    assert run.returncode==0,(out/'host.log').read_text()[-3000:]
    assert (folder/'D-Schicht/CLIPGET.OUT').is_file()
    assert not (prog/'CLIPGET.OUT').exists()
    assert (folder/'C-Schicht.registry').is_file(),'Registry was not saved beside the profile layer'
    assert (folder/(folder.name+'.desktop')).is_file()
    subprocess.run(['desktop-file-validate',str(folder/(folder.name+'.desktop'))],check=True)
    assert (folder/'neo98-host').resolve()==binary
    assert 'drive.D = '+str(prog) in (folder/'settings.ini').read_text()
    assert not win98_c.pristine_problems(root)
    (out/'results.json').write_text(json.dumps({'windows_profile_boot':True,'isolated_write_layer':True,
        'registry_in_profile':True,'linux_desktop_launcher':True,'source_unchanged':True},indent=2)+'\n')
    print('LINUX GAME MACHINE PASS: Windows boot, file and registry isolation, desktop launcher')

if __name__=='__main__':main()
