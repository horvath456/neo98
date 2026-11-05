#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""System Properties shows the NEO98 logo under "Manufactured and supported by:".

The host writes C:\\WINDOWS\\SYSTEM\\OEMLOGO.BMP beside OEMINFO.INI at the start
(src/neo98_oem_logo.h, ProfileFiles SyncOemLogo). SYSDM.CPL clips the picture to
180 by 114 pixels; the teal square of the icon must stand whole in it, 110
pixels high. Over a clone of setup's C:, without a window.

    build/analysis-venv/bin/python tools/test_oem_logo_host.py [--no-build]
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
from test_device_manager_host import failures, host  # noqa: E402
from test_wheel_host import Steps  # noqa: E402

# The logo's place on the General page of the sheet at 64,55.
AREA = (100, 310, 290, 455)


def teal(run):
    data = (run / 'sys.ppm').read_bytes()
    parts = data.split(b'\n', 3)
    width = int(parts[1].split()[0])
    xs, ys = [], []
    left, top, right, bottom = AREA
    for y in range(top, bottom):
        for x in range(left, right):
            r, g, b = parts[3][(y * width + x) * 3:(y * width + x) * 3 + 3]
            if r < 20 and 90 < g < 160 and 90 < b < 160:
                xs.append(x)
                ys.append(y)
    return (max(xs) - min(xs) + 1, max(ys) - min(ys) + 1) if xs else (0, 0)


def main():
    pristine = win98_c.use_pristine()
    journal = win98_c.journal(pristine)
    out = neo98.output_directory('oem-logo-host')
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    steps = Steps('#32770|System Properties', 3.0)
    steps.add('tops=1', 0.5).add('shot={out}/sys.ppm')
    code, text, run = host(binary, out, 'sysdm', 'C:\\WINDOWS\\RUNDLL32.EXE',
                           'RUNDLL32.EXE SHELL32.DLL,Control_RunDLL SYSDM.CPL', steps.steps,
                           int(steps.time) + 15, pristine, journal)
    width, height = teal(run) if (run / 'sys.ppm').is_file() else (0, 0)
    checks = {
        'the host writes OEMLOGO.BMP': 'NEO98 HOST OEMLOGO.BMP shows the NEO98 icon' in text,
        'System Properties shows the whole teal square, 110 pixels': 105 <= width <= 115 and 105 <= height <= 115,
        'no crash or call the host does not carry': code == 0 and not failures(text) and 'CRASHREPORT' not in text,
    }
    for name, ok in checks.items():
        print(('ok  ' if ok else 'FAIL'), name)
    print(f'{sum(checks.values())}/{len(checks)} checks pass; teal {width}x{height}; pictures in {run}')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
