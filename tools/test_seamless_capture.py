#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check seamless capture selection without opening windows or taking screenshots.

Run under the shared test lock. --source accepts an earlier capture helper.
"""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'tools/capture_host_windows.swift')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        print('SKIP: ScreenCaptureKit requires macOS')
        return 0
    out = ROOT / 'build/seamless-capture'
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'filter-test'
    subprocess.run(['swiftc', '-parse-as-library', '-D', 'CAPTURE_FILTER_TEST', str(args.source),
                    str(ROOT / 'tools/test_seamless_capture.swift'), '-o', str(binary)], check=True)
    return subprocess.run([str(binary)], check=False).returncode

if __name__ == '__main__':
    sys.exit(main())
