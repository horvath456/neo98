#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Original EYEDOG/MSIAV controls: hardware resources and codec versions.

The API trace records text painted by the original COMCTL32 controls, including
owner-drawn resource rows. Runs on a private C: clone, without a visible window.
"""
import json
import re
import sys
from pathlib import Path
sys.path[:0] = [str(Path(__file__).resolve().parent), str(Path(__file__).resolve().parents[1])]
import neo98
import win98_c
from native_app import ROOT, build_host
from test_msinfo_host import PROGRAM
from test_device_manager_host import host, failures, resources


def main():
    out = ROOT / 'build/msinfo-resources-host'
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    pristine = win98_c.use_pristine()
    steps = ['0:wait=|Microsoft System Information', '3:click=130,153', '4:key=6a']
    at = 6
    for label in ['conflicts', 'dma', 'forced', 'io', 'irq', 'memory']:
        steps += [f'{at}:key=28', f'{at+3}:shot={{out}}/{label}.ppm']
        if label == 'io':
            steps += [f'{at+2.2}:move=500,300', f'{at+2.4}:wheel=-8', f'{at+3.2}:shot={{out}}/io-bottom.ppm']
        at += 8
    steps += [f'{at}:click=130,265', f'{at+1}:key=27', f'{at+2}:key=28', f'{at+3}:key=27',
              f'{at+4}:key=28', f'{at+7}:shot={{out}}/audio.ppm', f'{at+8}:key=28',
              f'{at+11}:shot={{out}}/video.ppm', f'{at+12}:tops=1']
    code, text, run = host(binary, out, 'original', PROGRAM, 'MSINFO32.EXE', steps, at+15,
                           pristine, win98_c.journal(pristine), environment={'NEO98_TRACE_TEXT':'1'})
    painted = re.findall(r'^APITEXT \d+ (.*)$', text, re.M)
    checks = {}
    def check(name, yes):
        checks[name] = bool(yes)
        print(('ok  ' if yes else 'FAIL'), name, flush=True)
    check('HWINFOD publishes the snapshot requested by EYEDOG', 'HWINFOD code=3e8 instance=2 success=1' in text)
    for kind, label in [('D','DMA'), ('P','I/O'), ('Q','IRQ'), ('M','Memory')]:
        expected = {(first,last) for rows in resources().values() for k,first,last in rows if k == kind}
        if kind in 'DQ':
            missing = [first for first,last in expected if not any(re.fullmatch(r'0*'+str(first), s.strip()) for s in painted)]
        else:
            missing = [(first,last) for first,last in expected if not any(
                re.search(rf'(?:0?x)?0*{first:X}\s*-\s*(?:0?x)?0*{last:X}', s, re.I) for s in painted)]
        check(f'{label}: every range from DeviceResources is painted', not missing)
        if missing: print('missing:', missing)
    check('the sound, network and display devices are painted', all(any(name in s for s in painted)
          for name in ['NEO98 Sound Card','NEO98 Ethernet Adapter','NEO98 Display Adapter']))
    check('Audio Codecs paints the original codec and its version',
          any('IMA ADPCM' in s for s in painted) and '4.3.0.1998' in painted)
    check('Video Codecs paints Cinepak and its version', any('Cinepak' in s for s in painted) and '1.10.0.6' in painted)
    check('no exception, crash or unsupported call', code == 0 and not failures(text) and 'CRASHREPORT' not in text
          and not re.search(r'EXCEPTION|SEH handler frame abandoned',text))
    (out/'results.json').write_text(json.dumps({'checks':checks},indent=2)+'\n')
    print(f'msinfo resources: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1

if __name__ == '__main__':
    raise SystemExit(main())
