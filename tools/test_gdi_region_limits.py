#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""provider allocation boundaries, under serial.py."""
from pathlib import Path
import os, subprocess, shutil
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/user-gdi-limits'
def main():
    OUT.mkdir(exist_ok=True);binary=OUT/'region-limits'
    try:
        for name,command in [('build',['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','tools/test_gdi_region_limits.cpp',*[f'src/{s}.cpp' for s in ('dib_text','dibeng','win98_rect')],'-o',str(binary)]),('run',[str(binary)])]:
            result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,env=dict(os.environ,UBSAN_OPTIONS='halt_on_error=1'))
            (OUT/f'6116-{name}.log').write_text(result.stdout+result.stderr)
            if name=='run' or result.returncode:print(result.stdout+result.stderr,end='')
            result.check_returncode()
    finally:shutil.rmtree(str(binary)+'.dSYM',ignore_errors=True)
if __name__=='__main__':main()
