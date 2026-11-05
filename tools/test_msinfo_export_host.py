#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Export all original MSINFO pages, including its 16-bit USER driver/version calls."""
import json
import shutil
import sys
from pathlib import Path
sys.path[:0] = [str(Path(__file__).resolve().parent), str(Path(__file__).resolve().parents[1])]
import win98_c
from native_app import ROOT, build_host
from test_device_manager_host import host, failures
from test_msinfo_host import PROGRAM


def main():
    out = ROOT / 'build/msinfo-export-host'
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    pristine = win98_c.use_pristine()
    steps = ['0:wait=|Microsoft System Information', '3:click=130,153', '4:key=27',
             '5:key=28', '6:key=28', '7:key=28', '8:key=28', '11:keydown=12',
             '11.1:key=46', '11.2:keyup=12', '12:key=45', '14:type=C:\\MSINFO.TXT',
             '15:key=0d', '60:tops=1']
    code, text, run = host(binary, out, 'original', PROGRAM, 'MSINFO32.EXE', steps, 65,
                           pristine, win98_c.journal(pristine), keep=True)
    report = ''
    try:
        file = run / 'c/MSINFO.TXT'
        if file.exists():
            shutil.copyfile(file, out / 'MSINFO.TXT')
            report = file.read_text(encoding='cp1252')
    finally:
        shutil.rmtree(run / 'c', ignore_errors=True)
    checks = {
        'the export finishes through Software Updates': all(s in report for s in
            ['[System Information]', '[Hardware Resources]', '[Audio Codecs]', '[Video Codecs]',
             '[Registry]', '[Software Updates]', 'Year 2000 Update']),
        'the export includes native hardware and codec versions': all(s in report for s in
            ['NEO98 Sound Card', 'NEO98 Ethernet Adapter', '[IRQs]', '[Memory]', '4.3.0.1998', 'Cinepak']),
        'the original control reaches GetNextDriver': 'WIN16 USER.257' in text,
        'the original control reaches UserSeeUserDo version request': 'WIN16 USER.216' in text,
        'no fatal call or crash': code == 0 and not failures(text) and 'CRASHREPORT' not in text,
    }
    for name, yes in checks.items(): print(('ok  ' if yes else 'FAIL'), name, flush=True)
    (out / 'results.json').write_text(json.dumps(checks, indent=2)+'\n')
    print(f'msinfo export: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1

if __name__ == '__main__':
    raise SystemExit(main())
