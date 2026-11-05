#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise file associations, opening decisions and per-program dock actions."""
import json
from contextlib import contextmanager
import os
import re
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
from linux_x11_test import X11
from native_app import ROOT
import win98_c


def wait(check, seconds=15):
    until=time.monotonic()+seconds
    while time.monotonic()<until:
        value=check()
        if value: return value
        time.sleep(.08)
    raise AssertionError('Desktop condition did not arrive')



@contextmanager
def game_cleanup(folder):
    try: yield
    finally:
        for entry in Path('/proc').iterdir():
            if not entry.name.isdigit(): continue
            try:
                command=(entry/'cmdline').read_bytes()
                if str(folder).encode() not in command or b'--game-machine\0' not in command: continue
                pid=int(entry.name);os.kill(pid,15)
                try:wait(lambda:not entry.exists() or (entry/'stat').read_text().split()[2]=='Z',5)
                except AssertionError:os.kill(pid,9)
            except (PermissionError,FileNotFoundError,ProcessLookupError):pass


def main():
    source=win98_c.use_pristine(); binary=ROOT/'build/native-app/neo98-host'
    out=ROOT/'build/native-linux/desktop';out.mkdir(parents=True,exist_ok=True)
    x=X11(); reports={}
    (out/'results.json').unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(prefix='neo98 desktop $ % ',dir=out) as temporary, game_cleanup(Path(temporary)):
        folder=Path(temporary); drive=folder/'C'
        subprocess.run(['cp','-a','--reflink=auto',str(source),str(drive)],check=True)
        settings=folder/'settings.ini';settings.write_text('open.ask = 1\n')
        env=dict(os.environ,NEO98_WIN98_ROOT=str(drive),NEO98_SETTINGS_FILE=str(settings),
            XDG_DATA_HOME=str(folder/'data'),SDL_AUDIODRIVER='dummy',NEO98_CLIPBOARD='private',NEO98_LOG_TO_FILE='0')
        for name in ('SDL_VIDEODRIVER','GDK_BACKEND','NEO98_HOST_INPUT','NEO98_HOST_MENU','NEO98_HOST_INPUT_ONCE'):
            env.pop(name,None)
        from gi.repository import Gio
        context=Gio.AppLaunchContext()
        for key,value in env.items():context.setenv(key,value)
        copied=folder/'Notepad $ % ä.exe';shutil.copy2(drive/'WINDOWS/NOTEPAD.EXE',copied)
        def launch(name,*options):
            log=(out/(name+'.log')).open('w')
            current=dict(env)
            if name=='open-normal': current['NEO98_HOST_INPUT']=f'2:wait=Notepad;3:type=hello;4:ctrl=41;5:ctrl=43;6:pbdump={out/"opened.txt"}'
            p=subprocess.Popen([str(binary),'--drive-c',str(drive),'--no-registry-file','--no-pause-unfocused','--time-limit','45',*options],env=current,cwd=ROOT,stdout=log,stderr=log)
            return p,log
        def stop(p,log):
            if p.poll() is None: p.terminate()
            try: p.wait(timeout=15)
            except subprocess.TimeoutExpired: p.kill(); p.wait(timeout=10)
            log.close()
        p,log=launch('open-normal','--exe',r'C:\WINDOWS\CALC.EXE','--open-host',copied.as_uri())
        try:
            question=wait(lambda: next((w for w in x.windows(p.pid) if w['title']=='Open program'),None))
            x.capture(question['id'],out/'open-question.png')
            x.activate(question['id']);x.chord('Return')
            wait(lambda: 'open.normal = '+str(copied) in settings.read_text())
            wait(lambda:(out/'opened.txt').exists() and (out/'opened.txt').read_text()=='hello')
            # A second launch hands this host an external file without starting
            # a second machine or asking again about the remembered file.
            second=subprocess.run([str(binary),'--drive-c',str(drive),'--open-host',str(copied)],env=env,cwd=ROOT,capture_output=True,timeout=15)
            assert second.returncode==0,second.stderr
            time.sleep(1)
            assert not any(w['title']=='Open program' for w in x.windows(p.pid))
            reports['open']={'file_uri':True,'remembered_normal':True,'external_folder':True,'single_instance':True}
        finally:stop(p,log)
        settings.write_text('open.ask = 1\nseamless = 0\n')
        p,log=launch('open-game','--exe',r'C:\WINDOWS\CALC.EXE','--open-host',str(copied),'--seamless')
        try:
            question=wait(lambda:next((w for w in x.windows(p.pid) if w['title']=='Open program'),None))
            x.activate(question['id']);x.chord('Escape')
            wait(lambda:not any(w['title']=='Open program' for w in x.windows(p.pid)))
            assert 'open.normal' not in settings.read_text()
            subprocess.run([str(binary),'--drive-c',str(drive),'--open-host',str(copied)],env=env,check=True,timeout=15)
            question=wait(lambda:next((w for w in x.windows(p.pid) if w['title']=='Open program'),None))
            x.click(question['x']+22,question['y']+question['height']-95)
            # The rightmost response creates a game machine.
            x.click(question['x']+question['width']-95,question['y']+question['height']-22)
            wait(lambda:'NEO98 game machine created name=' in (out/'open-game.log').read_text())
            assert 'open.ask = 0' in settings.read_text()
            game=Path(re.search(r'NEO98 game machine created name=.* folder=(.*)',(out/'open-game.log').read_text())[1])
            lock=Path(str(game/'C-Schicht')+'.host-lock')
            game_pid=wait(lambda:int(lock.read_text().strip()) if lock.exists() and lock.read_text().strip() else None)
            def guest_windows():
                return [w for w in x.windows(game_pid) if 'Notepad' in w['title'] and b'neo98-program-' in x.property(w['id'],'WM_CLASS')]
            wait(guest_windows)
            subprocess.run([str(binary),'--drive-c',str(drive),'--open-host',str(copied)],env=env,check=True,timeout=15)
            time.sleep(1)
            assert len(guest_windows())==1,guest_windows()
            assert not any(w['title']=='Open program' for w in x.windows(p.pid))
            entries=list((folder/'data/applications').glob('neo98-program-*.desktop'))
            game_entry=next(path for path in entries if '--game-machine' in path.read_text())
            subprocess.run(['desktop-file-validate',str(game_entry)],check=True)
            app=Gio.DesktopAppInfo.new_from_filename(str(game_entry));assert app
            app.launch_action('Activate',context);time.sleep(.6)
            assert len(guest_windows())==1,guest_windows()
            app.launch_action('Quit',context)
            wait(lambda:not Path(f'/proc/{game_pid}').exists() or Path(f'/proc/{game_pid}/stat').read_text().split()[2]=='Z')
            app.launch_action('Activate',context)
            game_pid=wait(lambda:int(lock.read_text().strip()) if lock.exists() and lock.read_text().strip() and int(lock.read_text().strip())!=game_pid else None)
            wait(guest_windows);assert len(guest_windows())==1,guest_windows()
            app.launch_action('Quit',context)
            wait(lambda:not Path(f'/proc/{game_pid}').exists() or Path(f'/proc/{game_pid}/stat').read_text().split()[2]=='Z')
            reports['game_choice']={'cancel':True,'never_ask_persisted':True,'existing_game_reused':True,'unicode_dock':True,'pinned_game_reopened':True}
        finally:stop(p,log)
        # Stop only the game instances this test created, before removing C:.
        for lock in folder.rglob('*.host-lock'):
            try:
                pid=int(lock.read_text().strip())
                command=Path(f'/proc/{pid}/cmdline').read_bytes()
                if b'--game-machine' in command and str(folder).encode() in command:
                    os.kill(pid,15)
                    wait(lambda:not Path(f'/proc/{pid}').exists() or Path(f'/proc/{pid}/stat').read_text().split()[2]=='Z')
            except (ValueError,FileNotFoundError,ProcessLookupError):pass
        settings.write_text('open.ask = 0\nseamless = 1\n')
        p,log=launch('dock','--exe',r'C:\WINDOWS\NOTEPAD.EXE','--seamless')
        try:
            window=wait(lambda:next((w for w in x.windows(p.pid) if 'Notepad' in w['title']),None))
            entries=list((folder/'data/applications').glob('neo98-program-*.desktop'))
            entry=next(path for path in entries if 'NOTEPAD.EXE' in path.read_text().upper())
            subprocess.run(['desktop-file-validate',str(entry)],check=True)
            assert entry.stem.encode() in x.property(window['id'],'WM_CLASS')
            x.x.XIconifyWindow(x.display,window['id'],0);x.x.XFlush(x.display);time.sleep(.4)
            # Run the generated.desktop actions through GLib's actual parser.
            from gi.repository import Gio
            desktop=Gio.DesktopAppInfo.new_from_filename(str(entry));assert desktop
            desktop.launch_action('Activate',context)
            wait(lambda:'NEO98 dock activate found=1' in (out/'dock.log').read_text())
            wait(lambda:any(w['id']==window['id'] for w in x.windows(p.pid)))
            desktop.launch_action('Quit',context)
            p.wait(timeout=15);assert p.returncode==0
            reports['dock']={'desktop_valid':True,'class_matches':True,'activate_restores':True,'quit':True}
        finally:stop(p,log)
        # Installing handlers advertises Open With support without changing defaults.
        bundle=folder/'NEO98.AppDir';bundle.mkdir();(bundle/'AppRun').write_text('#!/bin/sh\nexit 0\n');(bundle/'AppRun').chmod(0o755)
        subprocess.run(['python3',str(ROOT/'tools/linux_desktop_install.py'),str(bundle)],env=env,check=True)
        entry=folder/'data/applications/neo98.desktop'
        subprocess.run(['desktop-file-validate',str(entry)],check=True)
        assert '--open %f' in entry.read_text() and 'MimeType=' in entry.read_text()
        batch=folder/'test.bat';batch.write_bytes(b'@echo off\r\n')
        pif=folder/'test.pif';pif.write_bytes(bytes(545))
        samples=[copied,source/'WINDOWS/WINVER.EXE',source/'WINDOWS/COMMAND.COM',batch,pif]
        types=[]
        for sample in samples:
            if not sample.exists():continue
            mime=subprocess.check_output(['xdg-mime','query','filetype',str(sample)],env=env,text=True).strip()
            supported=Gio.DesktopAppInfo.new_from_filename(str(entry)).get_supported_types()
            assert any(Gio.content_type_is_a(mime,declared) for declared in supported),(sample,mime,supported)
            types.append(mime)
        reports['mime_types']=types
        reports['file_associations']=True
    assert not win98_c.pristine_problems(source)
    (out/'results.json').write_text(json.dumps(reports,indent=2)+'\n')
    print('LINUX DESKTOP PASS: file handlers, opening decisions, remembered choices, dock activate/quit')

if __name__=='__main__':main()
