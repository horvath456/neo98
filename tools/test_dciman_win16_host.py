#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""resolve DCI exports, access the screen through Win16 driver callbacks."""
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['nasm'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
sys.path[:0] = [str(Path(__file__).resolve().parent),str(Path(__file__).resolve().parents[1])]
import neo98
import win16_fixture
import win98_c
from native_app import ROOT, build_host

def main():
    out=ROOT/'build/dciman-win16-host';out.mkdir(parents=True,exist_ok=True)
    root=win98_c.use_pristine()
    program=out/'DCI16.EXE'
    win16_fixture.assemble(ROOT/'tools/dciman_win16_fixture.asm',program,'DCI16',neo98.tool_environment())
    binary=ROOT/'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    if '--host' in sys.argv: binary=Path(sys.argv[sys.argv.index('--host')+1]).resolve()
    private=out/'c'
    if private.exists():shutil.rmtree(private)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R',str(root.resolve()),str(private)],check=True)
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NEO98_HOST_DEADLINE='25',
             NEO98_SETTINGS_DIR=str(out/'settings'),NEO98_LOG_DIR=str(out/'logs'))
    with (out/'host.log').open('w') as log:
        p=subprocess.Popen([binary,program,ROOT/'build/generated/notepad-fixedsys.fnt','--drive-c',private,
            '--system-tree',private,'--no-registry-file'],env=env,stdout=log,stderr=subprocess.STDOUT)
        try: code=p.wait(timeout=45)
        except subprocess.TimeoutExpired:
            p.terminate();p.wait(timeout=20);raise
    result=(private/'DCI16.OUT').read_bytes() if (private/'DCI16.OUT').exists() else b''
    (out/'result.bin').write_bytes(result)
    shutil.rmtree(private)
    assert code==0 and len(result)==42,(code,result,(out/'host.log').read_text()[-2000:])
    resolved,provider,answer,lo,hi,balance,offscreen,overlay,enum,*surface=struct.unpack('<21H',result)
    assert (offscreen,overlay,enum)==(0xfffc,)*3,(offscreen,overlay,enum)
    assert resolved==1 and answer==0 and hi!=0 and balance==0,(resolved,provider,answer,lo,hi,balance)
    width,height,stride,depth,selector,begin,pixel,end,destroy,expired,public_begin,public_end=surface
    assert width>=320 and height>=200 and stride>=width and depth in (8,16,24,32),surface
    assert (public_begin,public_end)==(0,0),surface
    assert selector and (begin,pixel,end,destroy,expired)==(0,0x5a,0,0,0xffff),surface
    print('DCIMAN Win16 PASS: named exports, screen offset beyond 64 KiB, driver callbacks, lifetime, Pascal stack balanced')

if __name__=='__main__':main()
