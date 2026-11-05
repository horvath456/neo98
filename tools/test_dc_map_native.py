#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Isolated GDI/USER integration test. No shared generators or product edits."""
from pathlib import Path
import argparse,json,shutil,subprocess
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/gdi-coordinate'
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--patch',type=Path);args=parser.parse_args()
    runtime=OUT/'native-runtime'
    if runtime.exists():shutil.rmtree(runtime)
    runtime.mkdir(parents=True)
    shutil.copytree(ROOT/'src',runtime/'src',ignore=shutil.ignore_patterns('*.o','*.d','*.img','*.elf','*.map','*.lst'))
    (runtime/'sdk').symlink_to(ROOT/'sdk',target_is_directory=True)
    (runtime/'build').symlink_to(ROOT/'build',target_is_directory=True)
    if args.patch:
        subprocess.run(['git','init','-q'],cwd=runtime,check=True)
        subprocess.run(['git','apply','-p2',str(args.patch.resolve())],cwd=runtime,check=True)
    (runtime/'tools').mkdir()
    shutil.copy2(ROOT/'tools/test_dc_map_native.cpp',runtime/'tools/test_dc_map_native.cpp')
    binary=OUT/'dc-map-native-asan'
    sources=['gdi','dib_text','dibeng','win98_rect','user','user_draw','text_edit','win98_dialog','win98_parameters']
    command=['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
             str(runtime/'tools/test_dc_map_native.cpp'),*[str(runtime/'src'/f'{s}.cpp') for s in sources],'-o',str(binary)]
    result=subprocess.run(command,capture_output=True,text=True);(OUT/'native-build.log').write_text(result.stdout+result.stderr)
    if result.returncode:print(result.stdout+result.stderr)
    result.check_returncode()
    result=subprocess.run([str(binary)],capture_output=True,text=True);(OUT/'native-test.log').write_text(result.stdout+result.stderr)
    print(result.stdout+result.stderr,end='');result.check_returncode()
    (OUT/'native-results.json').write_text(json.dumps({'ASan_UBSan':'PASS','shared_generators':False,
        'scope':'Actual GDI object state/raster/clip and USER retained canvas, including move and Save/Restore; x86 guest fixture is separate.'},indent=2)+'\n')
if __name__=='__main__':main()
