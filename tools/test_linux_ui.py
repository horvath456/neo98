#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise native menus, X11 windows, keyboard focus and persisted view settings."""
import ctypes as C
import json
import os
from pathlib import Path
import re
import subprocess
import time
from linux_x11_test import X11, U, I, P
from native_app import ROOT
import win98_c

def wait(check, seconds=12):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        found = check()
        if found: return found
        time.sleep(.05)
    raise AssertionError('UI condition did not arrive')

def main():
    win98_c.use_pristine()
    out = ROOT / 'build/native-linux/ui'; out.mkdir(parents=True,exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    x = X11(); x.require_input(); reports = {}
    def launch(name,steps,*args,settings=None):
        env = dict(os.environ, NEO98_CLIPBOARD='private',NEO98_KEYMAP='US',SDL_AUDIODRIVER='dummy',
                   NEO98_HOST_INPUT=steps,NEO98_MENU_TRACE='1',NEO98_SEAMLESS_DUMP=str(out/(name+'.windows')))
        env.update(GDK_SCALE='1',GDK_DPI_SCALE='1')
        # Exercise the default selection, including when WAYLAND_DISPLAY is set.
        for variable in ('SDL_VIDEODRIVER','GDK_BACKEND','NEO98_GPU','NEO98_SETTINGS_FILE','NEO98_LOG_TO_FILE'): env.pop(variable,None)
        if settings: env['NEO98_SETTINGS_FILE'] = str(settings)
        stream = (out / (name + '.log')).open('w')
        process = subprocess.Popen([str(binary),'--ram-drive','--no-registry-file',*args],cwd=ROOT,env=env,stdout=stream,stderr=stream)
        return process,stream
    def finish(process,stream):
        if process.poll() is None: process.terminate()
        try: process.wait(timeout=15)
        finally: stream.close()

    process,stream = launch('desktop',f'10:pbdump={out / "native-copy.txt"};11:menu=quit',
                             '--exe',r'C:\WINDOWS\NOTEPAD.EXE')
    try:
        child = wait(lambda: next((w for w in x.windows(process.pid) if 'Linux x64' in w['title']),None))
        shell = next(w for w in x.windows(process.pid) if w['title']=='Neo98')
        assert child['x']==shell['x'] and shell['y'] < child['y'] < shell['y']+60, (shell,child)
        x.click(child['x']+120,child['y']+140)
        wait(lambda: x.focus()==child['id'])
        for key in 'abc': x.chord(key); time.sleep(.08)
        x.chord('Control_L','a'); time.sleep(.2)
        x.click(shell['x']+60,shell['y']+12)
        popup = wait(lambda: next((w for w in x.windows(process.pid) if w['id'] not in (child['id'],shell['id'])),None))
        x.capture(popup['id'],out / 'edit-menu.png')
        assert x.focus() in [w['id'] for w in x.windows(process.pid)], 'Menu does not own the keyboard'
        x.chord('Down'); x.chord('Down'); x.chord('Return')
        time.sleep(.3); x.capture(shell['id'],out / 'desktop.png')
        process.wait(timeout=20)
        assert process.returncode==0
        assert (out / 'native-copy.txt').read_text()=='abc', 'Native menu copy lost the guest selection'
        reports['desktop'] = {'default_backend':'x11','wayland_session':bool(os.environ.get('WAYLAND_DISPLAY')),
                              'embedded':True,'real_keyboard':True,'native_menu_copy':True}
    finally: finish(process,stream)

    settings = out / 'settings.ini'; settings.unlink(missing_ok=True)
    steps = f'4:menu=fullscreen;5:menu=fullscreen;6:menu=seamless-windows;7:menu=wheel-focus;8:menu=network-listen;9:menu=quit'
    process,stream = launch('seamless',steps,'--exe',r'C:\WINDOWS\NOTEPAD.EXE','--seamless',settings=settings)
    try:
        native = wait(lambda: next((w for w in x.windows(process.pid) if 'Notepad' in w['title']),None))
        assert (native['width'],native['height'])==(672,480),native
        x.capture(native['id'],out / 'seamless.png')
        shape = C.CDLL('libXext.so.6')
        shape.XShapeGetRectangles.argtypes=[P,U,I,C.POINTER(I),C.POINTER(I)]
        shape.XShapeGetRectangles.restype=P
        count,order=I(),I(); rectangles=shape.XShapeGetRectangles(x.display,native['id'],0,C.byref(count),C.byref(order))
        assert count.value>0,'Native window has no guest region'
        x.x.XFree(rectangles)
        x.click(native['x']+100,native['y']+100); wait(lambda: x.focus()==native['id'])
        x.chord('a'); time.sleep(.2); x.capture(native['id'],out / 'seamless-typed.png')
        assert (out / 'seamless.png').read_bytes()!=(out / 'seamless-typed.png').read_bytes()
        process.wait(timeout=20); assert process.returncode==0
        text=settings.read_text()
        for value in ('seamless = 1','seamless.windows = 0','fullscreen = 0','wheel.focus = 1','net.listen = any'):
            assert value in text,(value,text)
        log=(out / 'seamless.log').read_text()
        assert 'NEO98 seamless=0' in log and 'NEO98 seamless=1' in log
        reports['seamless']={'native_window':True,'shape':True,'real_keyboard':True,'fullscreen_return':True,'settings_saved':True}
    finally: finish(process,stream)
    process,stream=launch('settings-reload','2:menu=network-listen;3:menu=quit','--exe',r'C:\WINDOWS\NOTEPAD.EXE',settings=settings)
    try:
        layer=wait(lambda: next((w for w in x.windows(process.pid) if w['title']=='Neo98' and w['width']>=1200),None))
        assert layer['width']>=1200,layer
        process.wait(timeout=10);assert process.returncode==0
        assert 'NEO98 network listen=any' in (out/'settings-reload.log').read_text()
        assert 'net.listen = 127.0.0.1' in settings.read_text()
        reports['settings_reload']={'seamless_layer':True,'network_listen':True}
    finally: finish(process,stream)
    process,stream=launch('native-minimize','10:menu=quit','--exe',r'C:\WINDOWS\NOTEPAD.EXE','--seamless')
    try:
        native=wait(lambda:next((w for w in x.windows(process.pid) if 'Notepad' in w['title']),None))
        description=out/'native-minimize.windows'
        wait(lambda:description.exists())
        x.x.XIconifyWindow(x.display,native['id'],0);x.x.XFlush(x.display)
        wait(lambda:'minimized=1' in description.read_text())
        x.activate(native['id'])
        wait(lambda:'minimized=0' in description.read_text())
        wait(lambda:any(w['id']==native['id'] for w in x.windows(process.pid)))
        # The guest caption's close button reaches WM_CLOSE through the
        # native window's input translation. This blank document needs no prompt.
        x.click(native['x']+native['width']-12,native['y']+11)
        process.wait(timeout=10);assert process.returncode==0
        reports['native_minimize']={'minimized':True,'restored':True,'guest_caption_close':True}
    finally:finish(process,stream)
    for path in out.glob('*.log'):
        assert 'CRITICAL' not in path.read_text(),path
    assert not win98_c.pristine_problems(win98_c.FOLDER)
    (out / 'results.json').write_text(json.dumps(reports,indent=2)+'\n')
    print('LINUX UI PASS: native menus, keyboard, seamless windows, fullscreen and saved settings')

if __name__=='__main__': main()
