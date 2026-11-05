#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""C applications get builtins; CRT sources keep real libc calls."""
from pathlib import Path
import re
import subprocess
import arm64_sdk
ROOT = Path(__file__).resolve().parents[1]

def main():
    sdk = arm64_sdk.generate()
    out = ROOT / 'build/arm64-c-builtins-test'
    out.mkdir(parents=True, exist_ok=True)
    source = out / 'copy.c'
    source.write_text('''#include <string.h>
#include <stdlib.h>
#include <errno.h>
#if __STDC_HOSTED__ != EXPECT_HOSTED
#error incorrect C compilation environment
#endif
void copy16(void *to, const void *from) { memcpy(to, from, 16); }
int oversized(void) { errno = 0; return malloc((size_t)-1) == NULL && errno == ENOMEM; }
''')
    for target in ('msvc', 'gnu'):
        asm = out / (target + '.s')
        subprocess.run([*arm64_sdk.compiler(sdk, source, target), '-DEXPECT_HOSTED=1', '-S', str(source), '-o', str(asm)], check=True)
        assert not re.search(r'\b(?:b|bl)\s+memcpy\b', asm.read_text()), asm.read_text()
        assert re.search(r'\bbl\s+malloc\b', asm.read_text()), asm.read_text()
    asm = out / 'crt.s'
    subprocess.run(['clang', *arm64_sdk.RUNTIME_CFLAGS, '-isystem', str(sdk / 'include'), '-DEXPECT_HOSTED=0', '-S', str(source), '-o', str(asm)], check=True)
    assert re.search(r'\b(?:b|bl)\s+memcpy\b', asm.read_text()), asm.read_text()
    print('ARM64 C BUILTINS PASS: hosted MSVC/MinGW programs, freestanding CRT')

if __name__ == '__main__':
    main()
