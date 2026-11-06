#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""the HAL mask must name the supported Direct3D shading modes."""
if __name__ == '__main__':
    import sys as _sys
    if _sys.platform != 'darwin':
        print('SKIP: builds with the Homebrew MinGW sysroot of macOS')  # macOS only; skipped elsewhere
        _sys.exit(77)
import os
from pathlib import Path
import subprocess
from native_app import ROOT
import neo98

root = Path(__file__).resolve().parents[1]
out = root / 'build/ddhal-shade-caps'
out.mkdir(parents=True, exist_ok=True)
mingw = Path(os.environ.get('NEO98_MINGW_I686',
                            '/opt/homebrew/opt/mingw-w64/toolchain-i686/i686-w64-mingw32'))
source = out / 'caps.cpp'
source.write_text('''#include "win98_ddhal.h"
#include <windows.h>
#include <d3d.h>
constexpr unsigned expected = D3DPSHADECAPS_COLORFLATRGB | D3DPSHADECAPS_COLORGOURAUDRGB |
    D3DPSHADECAPS_SPECULARFLATRGB | D3DPSHADECAPS_SPECULARGOURAUDRGB |
    D3DPSHADECAPS_ALPHAFLATBLEND | D3DPSHADECAPS_ALPHAGOURAUDBLEND |
    D3DPSHADECAPS_FOGFLAT | D3DPSHADECAPS_FOGGOURAUD;
static_assert(Win98DDHal::PrimCaps::ShadeValue == expected, "incorrect Direct3D shading capability mask");
static_assert(!(Win98DDHal::PrimCaps::ShadeValue & D3DPSHADECAPS_ALPHAPHONGBLEND), "Phong is not implemented");
''')
subprocess.run(['clang++', '--target=i686-w64-windows-gnu', f'--sysroot={mingw}', '-std=c++17',
                '-fsyntax-only', '-I' + str(root / 'src'), str(source)], env=neo98.tool_environment(), check=True)
print('PASS: HAL shading capabilities match named Direct3D flags in mingw-w64')
