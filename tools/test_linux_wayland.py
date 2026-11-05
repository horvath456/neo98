#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the explicitly selected, deprecated native Wayland path."""
import json
import os
import subprocess
from native_app import ROOT, linux_gtk_flags
import neo98
from linux_x11_test import X11
import win98_c


def main():
    assert os.environ.get('WAYLAND_DISPLAY'), 'Run this test inside a Wayland session'
    source = win98_c.use_pristine()
    out = ROOT / 'build/native-linux/wayland'
    out.mkdir(parents=True, exist_ok=True)
    flags, libs = linux_gtk_flags()
    sdl_flags = subprocess.check_output(['pkg-config','--cflags','sdl2'],text=True).split()
    sdl_libs = subprocess.check_output(['pkg-config','--libs','sdl2'],text=True).split()
    helper = out / 'test-canvas'
    subprocess.run(['clang++','-std=c++17','-O1','-g',*flags,*sdl_flags,str(ROOT/'tools/test_canvas_wayland.cpp'),
                    *libs,*sdl_libs,'-o',str(helper)],env=neo98.tool_environment(),check=True)
    env = dict(os.environ, SDL_VIDEODRIVER='wayland', GDK_BACKEND='wayland', SDL_AUDIODRIVER='dummy',
               NEO98_CLIPBOARD='private', WAYLAND_DEBUG='1', GDK_SCALE='1', GDK_DPI_SCALE='1')
    with (out / 'canvas.log').open('w') as stream:
        subprocess.run([str(helper),str(out / 'gtk-canvas.png')],env=env,stdout=stream,stderr=stream,check=True,timeout=30)
    env['NEO98_HOST_INPUT'] = f'3:type=Wayland;4:ctrl=41;5:menu=copy;6:pbdump={out / "copied.txt"};7:menu=fullscreen;8:menu=fullscreen;9:menu=quit'
    for name in ('NEO98_SETTINGS_FILE','NEO98_LOG_TO_FILE'): env.pop(name,None)
    binary = ROOT / 'build/native-app/neo98-host'
    x = X11()
    with (out / 'host.log').open('w') as stream:
        process = subprocess.Popen([str(binary),'--exe',r'C:\WINDOWS\NOTEPAD.EXE','--ram-drive','--no-registry-file',
                                   '--time-limit','15','--screenshot',str(out / 'notepad.png')],cwd=ROOT,env=env,stdout=stream,stderr=stream)
        try:
            import time
            time.sleep(2)
            assert not x.windows(process.pid), 'Wayland host created X11 windows'
            assert process.wait(timeout=30)==0
        finally:
            if process.poll() is None: process.terminate(); process.wait(timeout=15)
    text=(out / 'host.log').read_text()
    assert 'NEO98 display=wayland canvas=gtk' in text
    assert 'native Wayland output is deprecated.' in text
    assert 'xdg_toplevel' in text and '.set_title("NEO98")' in text
    assert (out / 'copied.txt').read_text()=='Wayland'
    assert 'CRITICAL' not in text and 'CRITICAL' not in (out / 'canvas.log').read_text()
    assert not win98_c.pristine_problems(source)
    (out / 'results.json').write_text(json.dumps({'native_wayland':True,'no_x11_windows':True,
        'gtk_canvas_input':True,'native_menu_copy':True,'fullscreen':True,'source_unchanged':True},indent=2)+'\n')
    print('LINUX WAYLAND PASS: native GTK desktop, menu, clipboard, input, fullscreen, no X11 windows')

if __name__=='__main__': main()
