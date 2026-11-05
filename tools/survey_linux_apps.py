#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Survey installed Windows GUI programs on isolated Linux game machines.

Each program gets ten seconds before saving, then loads in a fresh host. This
checks title/menu states, not gameplay. Installers and console helpers are excluded.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time
from native_app import ROOT
import neo98
import win98_c


def inventory(root):
    selected, excluded = [], []
    for path in sorted(root.rglob('*')):
        if path.suffix.lower() != '.exe' or not path.is_file(): continue
        relative = str(path.relative_to(root))
        reason = ''
        if re.search(r'setup|unins|unwise|autorun|instal', path.stem, re.I): reason = 'installer'
        try:
            with path.open('rb') as stream:
                header = stream.read(64)
                stream.seek(struct.unpack_from('<I', header, 60)[0]); image = stream.read(96)
            if image[:4] == b'PE\0\0':
                if struct.unpack_from('<H', image, 24 + 68)[0] != 2: reason = reason or 'console/helper'
            elif image[:2] != b'NE' or image[54] != 2: reason = reason or 'DOS/non-Windows'
        except (OSError, struct.error, IndexError): reason = 'invalid executable header'
        (excluded if reason else selected).append({'path': relative, **({'reason': reason} if reason else {})})
    return selected, excluded


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path)
    parser.add_argument('--runner', type=Path, default=ROOT/'build/native-app/neo98-host')
    parser.add_argument('--output', type=Path, default=ROOT/'build/native-linux/app-survey')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--limit', type=int, default=0)
    parser.add_argument('--match', default='')
    parser.add_argument('--list', action='store_true')
    parser.add_argument('--resume', action='store_true', help='continue the same frozen host and inventory')
    args = parser.parse_args()
    root = args.root.resolve() if args.root else win98_c.use_pristine()
    programs, excluded = inventory(root)
    programs = [p for p in programs if args.match.lower() in p['path'].lower()]
    if args.limit: programs = programs[:args.limit]
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    (out/'inventory.json').write_text(json.dumps({'root': str(root), 'selected': programs, 'excluded': excluded}, indent=2)+'\n')
    print(f'Selected {len(programs)} Windows GUI programs; excluded {len(excluded)} installers/console/DOS helpers', flush=True)
    if args.list: return
    source = args.runner.resolve(); binary = out/'neo98-host'
    if not args.resume:
        shutil.copy2(source, binary); shutil.copy2(str(source)+'.layout', str(binary)+'.layout')
    if not args.resume: (out/'host.json').write_text(json.dumps({'source': str(source), 'sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'layout_sha256': hashlib.sha256(Path(str(binary)+'.layout').read_bytes()).hexdigest(), 'root':str(root),
        'programs':len(programs),'jobs':args.jobs,'seconds_before_save':10,'started_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())},indent=2)+'\n')
    cpp = out/'create.cpp'; helper = out/'create'
    cpp.write_text('''#include "tools/host_game_machine.h"
int main(int argc,char **argv) {
 HostGameMachine::Request make; make.machine=argv[1]; make.program=argv[3]; make.name=argv[4];
 HostGameMachine::Result result;
 if(!HostGameMachine::Create(make,argv[2],result)) { fprintf(stderr,"%s\\n",result.error.c_str()); return 1; }
 puts(result.folder.c_str());
}''')
    subprocess.run(['clang++','-std=c++17','-O1','-I'+str(ROOT),str(cpp),'-o',str(helper)], env=neo98.tool_environment(),check=True)
    def run(program):
        result = dict(program); key = hashlib.sha256(program['path'].encode()).hexdigest()[:16]
        folder = out/key; folder.mkdir(exist_ok=True)
        profile = None
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', GDK_BACKEND='x11', SDL_AUDIODRIVER='dummy',
            NEO98_GPU='vulkan', NEO98_NATIVE_MODE='direct', NEO98_CLIPBOARD='private', NEO98_LOG_TO_FILE='0',
            NEO98_DOSBOX_CORE=str(ROOT/'build/dosbox-pure-host/lib/libdosbox-pure.so'), XDG_DATA_HOME=str(folder/'data'))
        for name in ('NEO98_HOST_INPUT','NEO98_HOST_INPUT_ONCE','NEO98_HOST_MENU','NEO98_SETTINGS_FILE', 'NEO98_TRACE_CPU'):
            env.pop(name,None)
        try:
            guest = 'C:\\'+program['path'].replace('/','\\')
            profile = Path(subprocess.check_output([str(helper),str(root),str(binary),guest,'LinuxSurvey-'+key+'-'+str(os.getpid())],text=True).strip())
            env['NEO98_HOST_INPUT'] = f'10:menu=state-save,Survey;12:shot={folder/"saved.png"};14:menu=quit'
            start = time.monotonic()
            with (folder/'save.log').open('w') as log:
                saved = subprocess.run([str(binary),'--game-machine',str(profile),'--no-seamless','--no-pause-unfocused','--offline','--time-limit','55'],env=env,cwd=ROOT,stdout=log,stderr=log,timeout=70)
            text = (folder/'save.log').read_text(errors='replace')
            result['save_exit'] = saved.returncode
            state = re.search(r'NEO98 Linux state saved id=(\S+) kind=manual', text)
            result['saved'] = bool(state)
            result['errors_before_save'] = [line[:500] for line in text.splitlines() if re.search(r'(failed|refused|fatal|unsupported)',line,re.I)][-8:]
            result['save_run_seconds'] = time.monotonic()-start
            shot = folder/'saved.png'
            if shot.exists():
                from PIL import Image
                image=Image.open(shot).convert('RGB')
                result['drew'] = len(image.resize((160,120)).getcolors(19200) or [])>4
            if not state: return result
            env['NEO98_HOST_INPUT'] = f'2:shot={folder/"loaded.png"};5:shot={folder/"later.png"};6:menu=quit'
            with (folder/'load.log').open('w') as log:
                loaded = subprocess.run([str(binary),'--game-machine',str(profile),'--load-state',state[1], '--no-seamless','--no-pause-unfocused','--offline','--time-limit','55'],env=env,cwd=ROOT,stdout=log,stderr=log,timeout=70)
            text=(folder/'load.log').read_text(errors='replace')
            result['load_exit']=loaded.returncode
            result['loaded']='NEO98 Linux state loaded id=' in text
            result['errors_after_load']=[line[:500] for line in text.splitlines() if re.search(r'(failed|refused|fatal|unsupported)',line,re.I)][-8:]
            result['same_picture_after_two_seconds']=shot.exists() and (folder/'loaded.png').exists() and shot.read_bytes()==(folder/'loaded.png').read_bytes()
            result['picture_changes_after_load']=(folder/'loaded.png').exists() and (folder/'later.png').exists() and (folder/'loaded.png').read_bytes()!=(folder/'later.png').read_bytes()
            return result
        except Exception as error:
            result['error']=repr(error); return result
        finally:
            if profile is not None: shutil.rmtree(profile,ignore_errors=True)
    total=len(programs)
    results=json.loads((out/'results.json').read_text()) if args.resume else []
    done={r['path'] for r in results}; programs=[p for p in programs if p['path'] not in done]
    print(f'Continuing {len(results)} completed, {len(programs)} remaining' ,flush=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        for future in as_completed([executor.submit(run,p) for p in programs]):
            result=future.result(); results.append(result)
            temporary=out/'results.new'; temporary.write_text(json.dumps(sorted(results,key=lambda r:r['path']),indent=2)+'\n'); temporary.replace(out/'results.json')
            print(f'{len(results)}/{total} {result["path"]}: saved={result.get("saved",False)} loaded={result.get("loaded",False)} error={result.get("error","")}',flush=True)
    summary={'selected':total,'completed':len(results),'drew':sum(r.get('drew',False) for r in results),
        'saved':sum(r.get('saved',False) for r in results),'loaded':sum(r.get('loaded',False) for r in results),
        'load_exit_zero':sum(r.get('loaded',False) and r.get('load_exit')==0 for r in results),
        'loaded_with_same_exit':sum(bool(r.get('loaded')) and r.get('load_exit')==r.get('save_exit') for r in results),
        'test_errors':sum('error' in r for r in results), 'same_picture_is_not_required_for_running_animations':True}
    assert not win98_c.pristine_problems(root)
    summary['source_unchanged']=True
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n'); print(json.dumps(summary),flush=True)

if __name__=='__main__': main()
