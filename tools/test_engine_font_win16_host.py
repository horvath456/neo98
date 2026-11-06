#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""resolve EngineDeleteFont and release its auxiliary global block."""
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
ROOT=next(p for p in Path(__file__).resolve().parents if (p/'neo98.py').exists())
sys.path[:0] = [str(ROOT/'tools'),str(ROOT)]
import neo98
import win16_fixture
import win98_c
from native_app import ROOT, build_host

def main():
    out=ROOT/'build/engine-font-win16-host';out.mkdir(parents=True,exist_ok=True)
    root=win98_c.use_pristine()
    program=out/'FONT16.EXE'
    win16_fixture.assemble(Path(__file__).with_name('engine_font_win16_fixture.asm'),program,'FONT16',neo98.tool_environment(),modules=('KERNEL','USER','GDI'))
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
    result=(private/'FONT16.OUT').read_bytes() if (private/'FONT16.OUT').exists() else b''
    (out/'result.bin').write_bytes(result)
    engine=(private/'ENGINE.OUT').read_bytes() if (private/'ENGINE.OUT').exists() else b''
    (out/'engine.bin').write_bytes(engine)
    shutil.rmtree(private)
    assert code==0 and len(result)==22,(code,result,(out/'host.log').read_text()[-2000:])
    values=struct.unpack('<3H2I4H',result)
    assert values==(1,1,0x23,0x55667788,0x11223344,0,0,1,0),values
    assert len(engine)==2118,(len(engine),(out/'host.log').read_text()[-2500:])
    need,invalid,realized,widthresult,contextresult,glyphbytes=struct.unpack_from('<5HI',engine)
    assert need==0xd3 and invalid==0 and realized&1 and widthresult and contextresult!=0xffff and 0<glyphbytes<=512,(need,invalid,realized,widthresult,contextresult,glyphbytes)
    font=engine[14:270];xform=engine[270:302];widths=struct.unpack_from('<95H',engine,302)
    assert struct.unpack_from('<H',font)[0]==3 and font[0x16:0x18]==xform[:2]
    assert min(widths)>0 and max(widths)>min(widths)
    assert any(engine[516:516+glyphbytes]),'glyph A is blank'
    normal=struct.unpack_from('<6i',engine,492)
    rotated=[]
    for at in (1028,1570):
        context,size=struct.unpack_from('<HI',engine,at)
        gm=struct.unpack_from('<6i',engine,at+6)
        assert context!=0xffff and 0<size<=512 and any(engine[at+30:at+30+size]),(context,size,gm)
        rotated.append(gm)
    assert normal[4]>0 and normal[5]==0 and rotated[0][4]==0 and rotated[0][5]>0,(normal,rotated)
    assert rotated[1][4]>0 and rotated[1][4]==rotated[1][5],rotated
    assert struct.unpack_from('<3H',engine,2112)==(widths[33],widths[0],widths[55])
    print('EngineGetCharWidthStr matches the range table PASS')
    print('Engine angles 0, 450 and 900: bitmap and advance PASS')
    print('Engine realization, width table, context and glyph bitmap PASS')
    print('EngineDeleteFont Win16 PASS: named export, packed fields, auxiliary GlobalFree, repeat, stack')

if __name__=='__main__':main()
