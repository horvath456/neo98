#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check GTK clipboard text, file paths and DIB images with real Win32 calls."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
from test_host_clipboard import build_fixture, png, fixture_output, dump, dump_image
from native_app import ROOT
import win98_c

def main():
    win98_c.use_pristine()
    out = ROOT / 'build/native-linux/clipboard'
    out.mkdir(parents=True, exist_ok=True)
    prog = out / 'programs'; prog.mkdir(exist_ok=True)
    exe = build_fixture(out)
    for name in ('CLIPGET1','CLIPGET2','CLIPGET3','CLIPSET','CLIPUNI','CLIPLATE','CLIPDIB','CLIPDROP','CLIPCUT'):
        shutil.copyfile(exe, prog / (name + '.EXE'))
        (prog / (name + '.OUT')).unlink(missing_ok=True)
    text = 'Grüße aus Linux €\nZeile 2 日本\rdritte\r\nvierte'
    (out / 'text.txt').write_text(text)
    guest = 'Grüße aus Win98 €\r\nZeile 2'
    wide = 'Ωmega 日本\r\nx'
    (prog / 'CLIPSET.IN').write_bytes(guest.encode('cp1252'))
    (prog / 'CLIPUNI.IN').write_bytes(wide.encode('utf-16-le'))
    (prog / 'CLIPLATE.IN').write_bytes('spät gerendert'.encode('cp1252'))
    (prog / 'CLIPDROP.IN').write_bytes(b'C:\\WINDOWS\\NOTEPAD.EXE\r\nC:\\PROGRA~1\\ACCESS~1\\WORDPAD.EXE\r\nC:\\NICHTDA.TXT\r\n')
    (prog / 'CLIPCUT.IN').write_bytes(b'C:\\WINDOWS\\WIN.INI\r\n')
    pixels = [(255,0,0),(0,255,0),(0,0,255),(255,255,255),(0,0,0),(18,52,86)]
    (out / 'image.png').write_bytes(png(3,2,pixels))
    table = [(0,0,0),(255,0,0),(0,128,255),(255,255,0)]
    dib = struct.pack('<IiiHHIIiiII',40,3,2,1,8,0,8,0,0,4,0)
    dib += b''.join(bytes((b,g,r,0)) for r,g,b in table)+bytes((3,2,1,0,1,2,3,0))
    (prog / 'CLIPDIB.IN').write_bytes(dib)
    outside = out / 'host-files'; outside.mkdir(exist_ok=True)
    letter = outside / 'Brief an Müller.txt'; letter.write_text('test')
    folder = outside / 'Ordner mit Inhalt'; folder.mkdir(exist_ok=True)
    (folder / 'innen.txt').write_text('test')
    steps = ['0:wait=Notepad']
    clock = 1
    def step(action, after=1):
        nonlocal clock
        clock += after; steps.append(f'{clock}:{action}')
    def run(name):
        step(f'guestopen=D:\\{name}.EXE')
        step(f'waitfile={prog / (name + ".OUT")}',0)
    step(f'pbtextfile={out / "text.txt"}'); run('CLIPGET1')
    step(f'pbfiles={letter}|{folder}',2); run('CLIPGET2')
    step(f'pbimage={out / "image.png"}',2); run('CLIPGET3')
    for name,kind in [('CLIPSET','set'),('CLIPUNI','uni'),('CLIPLATE','late'),('CLIPDIB','dib'),('CLIPDROP','drop'),('CLIPCUT','cut')]:
        run(name); step(f'pbdescribe={out / (kind + ".txt")}',1)
    step('menu=quit',2)
    env = dict(os.environ,NEO98_CLIPBOARD='private',NEO98_HOST_INPUT=';'.join(steps),SDL_AUDIODRIVER='dummy')
    for name in ('NEO98_GPU','NEO98_SETTINGS_FILE'): env.pop(name,None)
    command = [str(ROOT / 'build/native-app/neo98-host'),'--exe',r'C:\WINDOWS\NOTEPAD.EXE',
               '--ram-drive','--no-registry-file','--drive',f'D={prog}']
    with (out / 'host.log').open('w') as stream:
        run_result = subprocess.run(command,cwd=ROOT,env=env,stdout=stream,stderr=stream,text=True,timeout=65)
    log = (out / 'host.log').read_text()
    assert run_result.returncode == 0, log[-3000:]
    assert 'CRITICAL' not in log and 'WARNING' not in log, log[-3000:]
    got = fixture_output(prog / 'CLIPGET1.OUT')
    normalized = text.replace('\r\n','\n').replace('\r','\n').replace('\n','\r\n')
    assert bytes.fromhex(got['unicode'][0]).decode('utf-16-le') == normalized, got
    assert bytes.fromhex(got['text'][0]).decode('cp1252') == normalized.replace('日本','??'), got
    files = [bytes.fromhex(p).decode('cp1252') for p in fixture_output(prog / 'CLIPGET2.OUT')['drop']]
    assert files == [r'F:\Brief an Müller.txt',r'F:\Ordner mit Inhalt'], files
    raw = bytes.fromhex(fixture_output(prog / 'CLIPGET3.OUT')['dib'][0])
    assert struct.unpack_from('<IiiHH',raw) == (40,3,2,1,24), raw.hex()
    assert [tuple(raw[40+y*12+x*3:43+y*12+x*3][::-1]) for y in (1,0) for x in range(3)] == pixels
    assert dump(out / 'set.txt')[0] == guest.replace('\r\n','\n')
    assert dump(out / 'uni.txt')[0] == wide.replace('\r\n','\n')
    assert dump(out / 'late.txt')[0] == 'spät gerendert'
    assert dump_image(out / 'dib.txt') == (3,2,[table[i] for i in (1,2,3,3,2,1)])
    files = dump(out / 'drop.txt')[1]
    assert len(files) == 2 and all(Path(path).is_file() for path in files), files
    assert Path(files[0]).name.upper() == 'NOTEPAD.EXE' and Path(files[1]).name.upper() == 'WORDPAD.EXE', files
    assert len(dump(out / 'cut.txt')[1]) == 1
    assert not win98_c.pristine_problems(win98_c.FOLDER)
    (out / 'results.json').write_text(json.dumps({'text_both_directions':True,'unicode':True,'delayed_text':True,
        'files_both_directions':True,'short_names':True,'png_to_dib':True,'palette_dib_to_png':True},indent=2)+'\n')
    print('LINUX CLIPBOARD PASS: text, Unicode, delayed rendering, files, short names and images')

if __name__ == '__main__': main()
