#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""wallpaper pixels, clipping, SPI replacement and process lifetime."""
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
import native_app
from ui_clipboard_test_support import fixture


def main():
    out = ROOT / 'build/desktop-wallpaper-host'
    out.mkdir(parents=True, exist_ok=True)
    env = dict(neo98.tool_environment(), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_DEADLINE='40', NEO98_SETTINGS_DIR=str(out/'settings'), NEO98_LOG_DIR=str(out/'logs'))
    imports = {'KERNEL32': ['ExitProcess@4', 'CreateFileA@28', 'WriteFile@20', 'CloseHandle@4',
                           'GetModuleFileNameA@12', 'GetCommandLineA@0', 'CreateProcessA@40',
                           'WaitForSingleObject@8', 'GetExitCodeProcess@8'],
               'ADVAPI32': ['RegCreateKeyA@12', 'RegSetValueExA@24', 'RegCloseKey@4'],
               'USER32': ['SystemParametersInfoA@16', 'PaintDesktop@4', 'GetSystemMetrics@4', 'GetSysColor@4'],
               'GDI32': ['CreateCompatibleDC@4', 'CreateBitmap@20', 'SelectObject@8', 'DeleteObject@4',
                         'DeleteDC@4', 'GetPixel@12', 'SetPixel@16', 'SaveDC@4', 'RestoreDC@8',
                         'IntersectClipRect@20']}
    names = {dll: {name.rsplit('@', 1)[0]: int(name.rsplit('@', 1)[1]) for name in entries}
             for dll, entries in imports.items()}
    program = fixture(out/'fixture', ROOT/'tools/desktop_wallpaper_fixture.c', names)
    host = ROOT/'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    if '--host' in sys.argv: host = Path(sys.argv[sys.argv.index('--host')+1]).resolve()
    results = []
    for cpu in native_app.test_engines():
        if subprocess.run(__import__('test_support').other_hosts_command(), capture_output=True).returncode == 0:
            __import__('test_support').skip('exclusive host access is unavailable: another neo98-host is active')
        if shutil.disk_usage(ROOT).free < 6*1024**3:
            raise SystemExit('STOP: fewer than 6 GiB free')
        private = out / ('c-'+cpu)
        if private.exists(): shutil.rmtree(private)
        subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(Path(os.environ.get('NEO98_WIN98_ROOT') or __import__('pathlib').Path(__import__('os').environ.get('NEO98_WIN98_ROOT') or ROOT / 'build/win98-c')).resolve()), str(private)], check=True)
        shutil.copyfile(program, private/'WALLTEST.EXE')
        with (out/(cpu+'.log')).open('w') as log:
            p = subprocess.Popen([host, '-', ROOT/'build/generated/notepad-fixedsys.fnt',
                '--exe', 'C:\\WALLTEST.EXE', '--system-tree', private, '--drive-c', private,
                '--no-registry-file'], env=dict(env, NEO98_CPU=cpu), stdout=log, stderr=subprocess.STDOUT)
            try: code = p.wait(timeout=65)
            except subprocess.TimeoutExpired:
                p.terminate(); p.wait(timeout=20); raise
        text = (out/(cpu+'.log')).read_text(errors='replace')
        shutil.rmtree(private)
        assert code == 0 and 'NEO98 HOST exited code=0' in text, (cpu, code, text[-3500:])
        assert 'CRASHREPORT' not in text, text[-3500:]
        results.append({'cpu': cpu, 'status': 'PASS'})
        print(cpu, 'wallpaper PASS', flush=True)
    (out/'results.json').write_text(json.dumps({'host_sha256': hashlib.sha256(host.read_bytes()).hexdigest(),
        'revision': neo98.source_revision(),
        'tests': results}, indent=2)+'\n')


if __name__ == '__main__': main()
