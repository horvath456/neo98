#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""retained fErase, independent of pending WM_ERASEBKGND.

Run through a shared serial test lock with an already built host.
"""
import argparse
import hashlib
import json
import os
from ui_clipboard_test_support import ROOT, fixture, machine, host


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--baseline', action='store_true')
    args = parser.parse_args()
    out = ROOT / 'build/paint-erase'
    exe = fixture(out / 'fixture', ROOT / 'tools/paint_erase_fixture.c', {
        'KERNEL32': {'ExitProcess': 4},
        'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16,
                   'InvalidateRect': 12, 'UpdateWindow': 4, 'BeginPaint': 8,
                   'EndPaint': 8, 'GetUpdateRect': 12, 'DestroyWindow': 4}})
    results = {'host_sha256': hashlib.sha256((ROOT / 'build/native-app/neo98-host').read_bytes()).hexdigest(),
               'fixture_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(), 'tests': {}}
    for cpu in ('fex', 'unicorn'):
        os.environ['NEO98_CPU'] = cpu
        folder = out / (cpu + ('-before' if args.baseline else '-after'))
        with machine(folder) as drive:
            log = host(folder, drive, r'E:\TEST.EXE', seconds=8,
                       extra=('--drive-readonly', 'E=' + str(exe.parent)))
            expected = 102 if args.baseline else 0
            assert f'NEO98 HOST exited code={expected} ' in log, log[-3000:]
        results['tests'][cpu] = 'expected failure at retained fErase' if args.baseline else 'PASS'
        print(cpu, results['tests'][cpu], flush=True)
    (out / ('before.json' if args.baseline else 'results.json')).write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
