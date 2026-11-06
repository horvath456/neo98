#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""independent screen DCs, Win98 cache recovery and quiet painting.

Run through a shared serial test lock. Uses a RAM drive, dummy SDL, FEX and Unicorn.
"""
if __name__ == '__main__':
    import sys as _sys
    if _sys.platform != 'darwin':
        print('SKIP: compares the FEX and Unicorn engines of macOS')  # macOS only; skipped elsewhere
        _sys.exit(77)
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
from native_app import build_host
from ui_clipboard_test_support import fixture


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', type=Path)
    parser.add_argument('--baseline', action='store_true')
    args = parser.parse_args()
    out = ROOT / 'build/screen-dc-focus'
    imports = {'KERNEL32': {'ExitProcess': 4, 'GetModuleHandleA': 4},
               'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16,
                          'GetDC': 4, 'GetWindowDC': 4, 'ReleaseDC': 8, 'SetFocus': 4,
                          'GetFocus': 0, 'SetTimer': 16, 'GetMessageA': 16,
                          'TranslateMessage': 4, 'DispatchMessageA': 4},
               'GDI32': {'GetStockObject': 4, 'SetTextColor': 8, 'GetTextColor': 4,
                         'SaveDC': 4, 'RestoreDC': 8, 'GetClipBox': 8, 'IntersectClipRect': 20,
                         'SetPixel': 16, 'GetPixel': 12}}
    exe = fixture(out / 'fixture', ROOT / 'tools/screen_dc_focus_fixture.c', imports)
    host = (args.host or build_host()).resolve()
    results = []
    for cpu in ('fex', 'unicorn'):
        suffix = cpu + ('-before' if args.baseline else '')
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_CPU=cpu,
                   NEO98_HOST_DEADLINE='15', NEO98_HOST_INPUT='12:menu=quit',
                   NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
        command = [str(host), str(exe), str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                   '--system-tree', str(__import__('pathlib').Path(__import__('os').environ.get('NEO98_WIN98_ROOT') or __import__('pathlib').Path(__import__('os').environ.get('NEO98_WIN98_ROOT') or ROOT / 'build/win98-c'))), '--ram-drive', '--no-registry-file']
        with (out / (suffix + '.log')).open('w') as log:
            process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
            try: code = process.wait(timeout=45)
            except subprocess.TimeoutExpired:
                process.terminate(); process.wait(timeout=20); raise
        output = (out / (suffix + '.log')).read_text(errors='replace')
        expected = 61001 if args.baseline else 0
        assert code == 0, (cpu, code, output[-5000:])
        assert f'NEO98 HOST exited code={expected} ' in output, (cpu, code, output[-5000:])
        results.append({'cpu': cpu, 'status': 'EXPECTED_FAILURE' if args.baseline else 'PASS',
                        'guest_exit_code': expected})
        print(cpu, results[-1]['status'], flush=True)
    (out / ('before.json' if args.baseline else 'results.json')).write_text(json.dumps({
        'host_sha256': hashlib.sha256(host.read_bytes()).hexdigest(),
        'fixture_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(), 'tests': results}, indent=2) + '\n')


if __name__ == '__main__': main()
