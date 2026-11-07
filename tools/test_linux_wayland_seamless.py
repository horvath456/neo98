#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native Wayland windows, complete occluded content and restored Windows UI."""
import json
import os
from pathlib import Path
import subprocess
import sys
import time
from PIL import Image, ImageChops
from native_app import ROOT
from linux_x11_test import X11
import win98_c


def main():
    if not os.environ.get('WAYLAND_DISPLAY'):
        print('SKIP: run this test inside a Wayland session')
        return 77
    source = win98_c.use_pristine()
    out = ROOT / 'build/native-linux/wayland-seamless'
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    # The shared state test builds the profile helper without proprietary files.
    helper = ROOT / 'build/native-linux/windows-states/create'
    assert helper.exists(), 'Run tools/test_linux_windows_states.py --only notepad first'
    machine = out / ('machine-' + str(os.getpid()))
    subprocess.run(['cp','-a','--reflink=auto',str(source),str(machine)],check=True)
    profile = Path(subprocess.check_output([str(helper),str(machine),str(binary)],text=True).strip())
    steps = [
        '2:wait=Notepad','3:native-click=Notepad|100,100','3.3:native-key=Notepad|41',
        '3.5:native-ctrl=Notepad|41','3.7:native-ctrl=Notepad|43',f'4:pbdump={out / "typed.txt"}',
        f'4.2:native-shot=Notepad|{out / "before.png"}',
        '5:native-click=Notepad|19,32',f'5.4:native-shot=#32768|{out / "menu.png"}',
        '5.8:native-key=Notepad|1b',
        '6:native-click=Notepad|19,32','6.3:native-click=#32768|40,44','7:wait=#32770',
        f'7.5:native-shot=Notepad|{out / "covered.png"}',
        f'7.7:native-shot=#32770|{out / "dialog.png"}',
        '8:menu=state-save,Wayland dialog','12:menu=state-load','13:pause=0',
        f'16:native-shot=#32770|{out / "restored-dialog.png"}',
        '16.5:native-key=#32770|1b','17:native-size=Notepad|640,420',
        '18:native-click=Notepad|100,100','18.3:native-key=Notepad|42',
        '18.5:native-ctrl=Notepad|41','18.7:native-ctrl=Notepad|43',f'19:pbdump={out / "restored.txt"}',
        f'19.5:native-shot=Notepad|{out / "resized.png"}',
        '20:menu=seamless','21:menu=seamless','22:menu=quit',
    ]
    for name in ('typed.txt','restored.txt','menu.png','dialog.png','restored-dialog.png','resized.png'):
        (out / name).unlink(missing_ok=True)
    env = dict(os.environ,SDL_VIDEODRIVER='wayland',GDK_BACKEND='wayland',SDL_AUDIODRIVER='dummy',
               NEO98_CLIPBOARD='private',NEO98_KEYMAP='US',NEO98_HOST_INPUT_ONCE='1',
               NEO98_HOST_INPUT=';'.join(steps),NEO98_SEAMLESS_DUMP=str(out / 'windows.txt'),
               WAYLAND_DEBUG='1',GDK_SCALE='1',GDK_DPI_SCALE='1')
    for name in ('NEO98_SETTINGS_FILE','NEO98_LOG_TO_FILE'): env.pop(name,None)
    x = X11()
    with (out / 'test.log').open('w') as stream:
        p = subprocess.Popen([str(binary),'--game-machine',str(profile),'--seamless','--seamless-no-taskbar',
                              '--time-limit','45'],cwd=ROOT,env=env,stdout=stream,stderr=stream)
        try:
            time.sleep(3)
            assert not x.windows(p.pid), 'Native Wayland created an X11 window'
            result = p.wait(timeout=65)
        finally:
            if p.poll() is None: p.terminate();p.wait(timeout=15)
    text = (out / 'test.log').read_text()
    assert result == 0, text[-9000:]
    assert 'state loaded id=' in text, text[-9000:]
    assert (out / 'typed.txt').read_text()=='a'
    assert (out / 'restored.txt').read_text()=='ab'
    for name in ('menu.png','dialog.png','restored-dialog.png','resized.png'): assert (out / name).stat().st_size>500
    before=Image.open(out / 'before.png').convert('RGB')
    covered=Image.open(out / 'covered.png').convert('RGB')
    # Ignore the active caption and caret. The dialog must not cut holes
    # in the document image, even though it covers it in guest coordinates.
    assert ImageChops.difference(before.crop((200,80,640,440)),covered.crop((200,80,640,440))).getbbox() is None
    assert Image.open(out / 'resized.png').size==(640,420)
    assert 'CRITICAL' not in text and 'protocol error' not in text, text[-9000:]
    assert 'xdg_popup' in text and 'xdg_toplevel' in text
    assert not win98_c.pristine_problems(source)
    (out / 'results.json').write_text(json.dumps({'native_wayland':True,'no_x11':True,'gtk_input':True,
        'complete_occluded_window':True,'guest_popup':True,'restored_dialog':True,'resize':True,
        'desktop_toggle':True,'source_unchanged':True},indent=2)+'\n')
    print('LINUX WAYLAND SEAMLESS PASS: input, occlusion, popup, Windows state, resize, toggle, no X11')


if __name__=='__main__': sys.exit(main())
