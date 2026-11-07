#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""a foreign window locks DirectDraw during mode-change repositioning."""
if __name__ == '__main__':
    import sys as _sys
    if _sys.platform != 'darwin':
        print('SKIP: compares the FEX and Unicorn engines of macOS')  # macOS only; skipped elsewhere
        _sys.exit(77)
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
sys.path[:0] = [str(Path(__file__).resolve().parent), str(Path(__file__).resolve().parents[1])]
import neo98
from native_app import ROOT, build_host
from ui_clipboard_test_support import fixture


def main():
    out = ROOT / 'build/ddraw-reposition-host'
    out.mkdir(parents=True, exist_ok=True)
    env = dict(neo98.tool_environment(), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT='5:wstate=chains;6:tops=1', NEO98_HOST_DEADLINE='40', NEO98_SETTINGS_DIR=str(out/'settings'), NEO98_LOG_DIR=str(out/'logs'))
    imports = {'KERNEL32': ['ExitProcess@4', 'GetModuleHandleA@4', 'GetCommandLineA@0',
        'GetModuleFileNameA@12', 'CreateProcessA@40', 'Sleep@4', 'GetTickCount@0',
        'WaitForSingleObject@8', 'GetExitCodeProcess@8'],
        'USER32': ['RegisterClassA@4', 'CreateWindowExA@48', 'DefWindowProcA@16',
        'GetMessageA@16', 'DispatchMessageA@4', 'FindWindowA@8', 'SendMessageA@16',
        'MoveWindow@24', 'SetForegroundWindow@4', 'ChangeDisplaySettingsA@8'], 'DDRAW': ['DirectDrawCreate@12']}
    names = {dll: {name.rsplit('@', 1)[0]: int(name.rsplit('@', 1)[1]) for name in entries}
             for dll, entries in imports.items()}
    program = fixture(out/'fixture', ROOT/'tools/ddraw_reposition_fixture.c', names)
    host = ROOT/'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    if '--host' in sys.argv: host = Path(sys.argv[sys.argv.index('--host')+1]).resolve()
    results = []
    for cpu in ('fex', 'unicorn'):
        if subprocess.run(__import__('test_support').other_hosts_command(), capture_output=True).returncode == 0:
            __import__('test_support').skip('exclusive host access is unavailable: another neo98-host is active')
        if shutil.disk_usage(ROOT).free < 6*1024**3:
            raise SystemExit('STOP: fewer than 6 GiB free')
        private = out / ('c-'+cpu)
        if private.exists(): shutil.rmtree(private)
        subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(Path(os.environ.get('NEO98_WIN98_ROOT') or __import__('pathlib').Path(__import__('os').environ.get('NEO98_WIN98_ROOT') or ROOT / 'build/win98-c')).resolve()), str(private)], check=True)
        shutil.copyfile(program, private/'DDREPOS.EXE')
        with (out/(cpu+'.log')).open('w') as log:
            p = subprocess.Popen([host, '-', ROOT/'build/generated/notepad-fixedsys.fnt',
                '--exe', 'C:\\DDREPOS.EXE', '--system-tree', private, '--drive-c', private,
                '--no-registry-file'], env=dict(env, NEO98_CPU=cpu), stdout=log, stderr=subprocess.STDOUT)
            try: code = p.wait(timeout=65)
            except subprocess.TimeoutExpired:
                p.terminate(); p.wait(timeout=20); raise
        text = (out/(cpu+'.log')).read_text(errors='replace')
        shutil.rmtree(private)
        assert code == 0 and 'NEO98 HOST exited code=0' in text, (cpu, code, text[-3500:])
        assert 'CRASHREPORT' not in text, text[-3500:]
        results.append({'cpu': cpu, 'status': 'PASS'})
        print(cpu, 'DirectDraw reposition PASS', flush=True)
    (out/'results.json').write_text(json.dumps({'host_sha256': hashlib.sha256(host.read_bytes()).hexdigest(),
        'revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
        'tests': results}, indent=2)+'\n')


if __name__ == '__main__': main()
