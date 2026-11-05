#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Camera pictures larger than 640x480 and the Mac's list of cameras, without a window.

tools/test_camera_frames.mm: CopyFrame (tools/host_devices/platform.h) scales a picture of
1280x720, 1920x1080, 1760x1328 or 1080x1920 down into the 640x480 frame Windows reads, keeping
its proportions, instead of dropping it; on macOS the platform of tools/host_devices/macos.mm
lists every camera AVFoundation has. No camera is opened and macOS is never asked for access.

    build/analysis-venv/bin/python tools/test_camera_frames.py
"""
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import neo98  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]


def main():
    if sys.platform != 'darwin':
        print('skipped: macOS only')
        return 0
    out = neo98.output_directory('camera-frames')
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'camera-frames'
    subprocess.run(['clang++', '-std=c++20', '-fobjc-arc', '-O1', '-g', '-I', str(ROOT / 'tools'), '-I', str(ROOT / 'src'),
                    str(ROOT / 'tools/test_camera_frames.mm'), '-o', str(binary),
                    '-framework', 'AppKit', '-framework', 'CoreWLAN', '-framework', 'CoreLocation', '-framework', 'IOKit',
                    '-framework', 'AVFoundation', '-framework', 'CoreMedia', '-framework', 'CoreVideo'],
                   env=neo98.tool_environment(), check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
    print(result.stdout + result.stderr)
    return 0 if result.returncode == 0 and 'CAMERA FRAMES PASS' in result.stdout else 1


if __name__ == '__main__':
    sys.exit(main())
