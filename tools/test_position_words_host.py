#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare MoveWindow/SetWindowPos/DeferWindowPos WORD packing with real Win98.

Run through serial.py; requires an explicitly selected private Windows tree
and POSWORD.TXT from the unchanged fixture on Win98. No live C: is used.
"""
import argparse
from test_support import ExternalInputParser
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT/'tools')]
import neo98
import win98_c
from game_repertoire import wait_host


def main():
    parser = ExternalInputParser(description=__doc__)
    parser.add_argument('--system-root', required=True, type=Path)
    parser.add_argument('--reference', required=True, type=Path)
    parser.add_argument('--label', default='after', choices=['before','after'])
    parser.add_argument('--expect-match', action='store_true')
    args = parser.parse_args()
    source = args.system_root.resolve()
    assert source != (ROOT/'build/win98-c').resolve() and not args.system_root.is_symlink()
    assert win98_c.legacy_read(source/'.neo98-index').is_file()
    if subprocess.run(['pgrep','-x','neo98-host'],capture_output=True).returncode == 0:
        __import__('test_support').skip('exclusive host access is unavailable: another neo98-host is active')
    assert shutil.disk_usage('/System/Volumes/Data').free >= 8 * 1024**3
    out = ROOT/'build/position-words'
    out.mkdir(exist_ok=True)
    exe = out/'POSWORD.EXE'
    subprocess.run(['i686-w64-mingw32-gcc','-O1','-ffreestanding','-fno-builtin',
        '-nostdlib','-Wl,--entry,_mainCRTStartup','-Wl,--subsystem,windows:4.0',
        str(ROOT/'tools/position_words_fixture.c'),'-o',str(exe),'-luser32','-lkernel32'],
        env=neo98.tool_environment(),check=True)
    expected = args.reference.read_text().splitlines()
    assert len(expected) == 21
    records = []
    for cpu in ('fex','unicorn'):
        folder = tempfile.mkdtemp(prefix='private-',dir=out)
        process = None
        try:
            drive = Path(folder)/'c'
            subprocess.run(['cp','-cR',str(source),str(drive)],check=True)
            assert not drive.is_symlink() and drive.resolve()!=source
            shutil.copyfile(exe,drive/'POSWORD.EXE')
            env = dict(os.environ, SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',
                NEO98_CPU=cpu,NEO98_WIN98_ROOT=str(drive),NEO98_HOST_DEADLINE='25',
                NEO98_SETTINGS_DIR=str(Path(folder)/'settings'),NEO98_LOG_DIR=str(Path(folder)/'logs'))
            env.pop('NEO98_HOST_INPUT',None)
            log = out/(args.label+'-'+cpu+'.log')
            with log.open('w') as stream:
                process = subprocess.Popen([str(ROOT/'build/native-app/neo98-host'),'-',
                    r'C:\WINDOWS\FONTS\VGAFIX.FON','--exe',r'C:\POSWORD.EXE','--quiet',
                    '--system-tree',str(drive),'--drive-c',str(drive),'--no-registry-file'],
                    env=env,stdout=stream,stderr=subprocess.STDOUT)
                code,timed_out = wait_host(process,25,out)
            assert code==0 and not timed_out, log
            result = (drive/'POSWORD.TXT').read_text().splitlines()
            (out/(args.label+'-'+cpu+'.txt')).write_text('\n'.join(result)+'\n')
            mismatches = [{'line':i+1,'win98':e,'neo98':g}
                          for i,(e,g) in enumerate(zip(expected,result)) if e!=g]
            match = len(result)==len(expected) and not mismatches
            records.append({'cpu':cpu,'match':match,'lines':len(result),'mismatches':mismatches})
            print(cpu, 'PASS' if match else 'DIFF',len(mismatches),'of',len(expected),'lines',flush=True)
        finally:
            if process is None or process.poll() is not None:
                shutil.rmtree(folder)
    (out/(args.label+'.json')).write_text(json.dumps({
        'host_sha256':hashlib.sha256((ROOT/'build/native-app/neo98-host').read_bytes()).hexdigest(),
        'fixture_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
        'fixture_source_sha256':hashlib.sha256((ROOT/'tools/position_words_fixture.c').read_bytes()).hexdigest(),
        'reference_sha256':hashlib.sha256(args.reference.read_bytes()).hexdigest(),'tests':records},indent=2)+'\n')
    if args.label == 'after' or args.expect_match:
        assert all(r['match'] for r in records), records


if __name__ == '__main__':
    main()
