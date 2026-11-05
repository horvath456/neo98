#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The scrollWheel events of the Mac through the host's own way, without a visible window.

tools/test_host_scroll_native.mm makes real scroll events (CGEvents with
phases and momentum, as NSEvents for an SDL window that is never shown),
lets the host's monitor (tools/host_scroll_watch.mm) and SDL see them and
puts every SDL_MOUSEWHEEL through HostWheelFromSdl (tools/host_wheel.h), the
function tools/neo98_host.cpp calls. This script builds and runs it.

It needs the window server of a logged-in session (SDL's Cocoa driver); in
a session without one it says so and is skipped.

    build/analysis-venv/bin/python tools/test_host_scroll_native.py
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT  # noqa: E402


def main():
    out = neo98.output_directory('host-scroll-native')
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'test-host-scroll'
    cflags = subprocess.check_output(['sdl2-config', '--cflags'], text=True).split()
    libs = subprocess.check_output(['sdl2-config', '--libs'], text=True).split()
    subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-fobjc-arc', '-I', str(ROOT / 'tools'), *cflags,
                    str(ROOT / 'tools/test_host_scroll_native.mm'), str(ROOT / 'tools/host_scroll_watch.mm'),
                    '-o', str(binary), *libs, '-framework', 'AppKit', '-framework', 'CoreGraphics'], check=True)
    env = dict(os.environ)
    env.pop('SDL_VIDEODRIVER', None)   # the real driver; the window is never shown
    env['SDL_AUDIODRIVER'] = 'dummy'
    try:
        run = subprocess.run([str(binary)], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        print('host scroll native: no answer within two minutes')
        return 1
    (out / 'log.txt').write_text(run.stdout)
    print(run.stdout, end='')
    if run.returncode == 77:
        print('host scroll native: skipped, this session has no window server for SDL')
        return 0
    # The premise of the hint the host sets: without it no momentum event reaches the application.
    without = subprocess.run([str(binary), '--no-momentum'], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             text=True, timeout=120)
    print(without.stdout.strip().splitlines()[-1] if without.stdout.strip() else 'no answer without the hint')
    print(('ok  ' if without.returncode == 0 else 'FAIL'),
          'without SDL_MAC_SCROLL_MOMENTUM the Mac keeps the momentum to itself (why the host sets the hint)')
    return run.returncode or without.returncode


if __name__ == '__main__':
    raise SystemExit(main())
