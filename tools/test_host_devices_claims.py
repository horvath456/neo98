#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The hardware service leaves out the volumes the host already serves.

    python3 tools/test_host_devices_claims.py
"""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    out = ROOT / 'build/host-devices-claims'
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'test-host-devices-claims'
    subprocess.run(['clang++', '-std=c++17', '-O1', '-I', str(ROOT / 'tools'), str(ROOT / 'tools/test_host_devices_claims.cpp'),
                    '-o', str(binary)], check=True)
    with tempfile.TemporaryDirectory(dir=out) as temp:
        disc, stick = Path(temp) / 'OFFICE10', Path(temp) / 'STICK'
        disc.mkdir()
        stick.mkdir()
        return subprocess.run([str(binary), str(disc), str(stick)]).returncode


if __name__ == '__main__':
    sys.exit(main())
