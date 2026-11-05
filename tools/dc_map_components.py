#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Verify original GDI coordinate code, public thunks and native raster integration."""
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98


def test():
    out = ROOT/'build/gdi-coordinate'
    out.mkdir(parents=True, exist_ok=True)
    reports = {}
    for name, report in (('dc_map', 'core-results.json'),
                         ('dc_map_public', 'public-results.json'),
                         ('dc_map_native', 'native-results.json')):
        result = subprocess.run([str(neo98.analysis_python()), str(ROOT/'tools'/f'test_{name}.py')],
                                capture_output=True, text=True, env=neo98.tool_environment())
        (out/f'component-{name}.log').write_text(result.stdout + result.stderr)
        if result.returncode:
            print(result.stdout + result.stderr)
        result.check_returncode()
        reports[name] = json.loads((out/report).read_text())
        print(name + ': PASS', flush=True)
    return reports


if __name__ == '__main__':
    print(json.dumps(test(), indent=2))
