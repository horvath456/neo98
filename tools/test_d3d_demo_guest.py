#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""The Direct3D demo (tools/d3d_demo.c) as a test of what it shows.

Seven runs of DEMO3D.EXE in the optimised probe runner, each checked at the
host image:

- the fixed picture ("still") on the transform-and-lighting device and on the
  HAL device, where D3DIM transforms and lights: below the text overlay the
  two may differ by at most a few levels per channel;
- the cube map reflection and the bump map against the same picture
  without them, and the W buffer against the Z buffer;
- the floor's transform and material out of a state block;
- the same picture with a gamma ramp of 150 percent through
  IDirectDrawGammaControl: the scene comes out brighter;
- the demo in a window on the desktop, blitted through a clipper: the client
  area shows the scene, the desktop around the window keeps its colour.


"""
import sys
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import d3d_demo  # noqa: E402

# DirectX 6.1 of the ISO installation is not what this test measures; it runs on
# the DirectX 9 copy of the Win98 folder (tools/dx9_tree.py) and skips without it.
import dx9_tree  # noqa: E402
import os  # noqa: E402
parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
parser.add_argument('--runner', type=Path)
parser.add_argument('--system-tree', type=Path)
args = parser.parse_args()
_DX9 = args.system_tree or dx9_tree.tree()
if _DX9 is None:
    print('SKIP: no DirectX 9 tree')
    raise SystemExit(0)
os.environ['NEO98_WIN98_ROOT'] = str(_DX9)

DESKTOP = (0x00, 0x80, 0x80)


def frame():
    data = (d3d_demo.OUT / 'demo.ppm').read_bytes()
    assert data.startswith(b'P6\n800 600\n255\n'), 'no 800x600 frame'
    return data[len(b'P6\n800 600\n255\n'):]


def run(*arguments):
    d3d_demo.measure(exe, list(arguments), 10, runner=args.runner, system_tree=_DX9)
    log = (d3d_demo.OUT / 'demo.log').read_text()
    return frame(), log


def mean(pixels, top):
    part = pixels[800 * 3 * top:]
    return sum(part) / len(part)


exe = d3d_demo.build()
tnl, log = run('still')
assert 'D3DDEMO device TnL HAL' in log, 'the transform-and-lighting device was not taken'
for feature in ('cube map', 'bump map', 'palette alpha', 'Z buffer D24S8', 'floor DXT1', 'gamma control'):
    assert 'D3DDEMO ' + feature in log, f'the driver refused: {feature}'
# The cube map and the bump map change the picture; the plain run leaves
# them out. The W buffer orders the same scene as the Z buffer.
plain, log = run('still', 'plain')
changed = sum(1 for i in range(800 * 3 * 110, len(tnl), 3) if max(abs(tnl[i + k] - plain[i + k]) for k in range(3)) > 24)
assert changed > 20000, f'the cube map and the bump map changed only {changed} pixels'
wbuf, log = run('still', 'wbuffer')
different = sum(1 for i in range(800 * 3 * 110, len(tnl), 3) if max(abs(tnl[i + k] - wbuf[i + k]) for k in range(3)) > 24)
assert different < 3000, f'the W buffer changed {different} pixels of the same scene'
# A state block that holds the floor's world matrix and material draws the
# same picture as setting both directly.
blocked, log = run('still', 'stateblock')
assert 'D3DDEMO state block' in log and 'failed' not in log, 'the state block run stopped'
block_diff = sum(1 for i in range(800 * 3 * 110, len(tnl)) if abs(tnl[i] - blocked[i]) > 8)
assert block_diff == 0, f'the state block changed {block_diff} channel values'
hal, log = run('still', 'hal')
assert 'D3DDEMO device HAL' in log, 'the HAL device was not taken'
# Below the text overlay (the first 110 host rows) both devices draw the same.
worst, differing = 0, 0
for i in range(800 * 3 * 110, len(tnl)):
    d = abs(tnl[i] - hal[i])
    worst = max(worst, d)
    differing += d > 8
# A handful of single pixels may land on the other side of a floor tile's
# edge: the bump map shifts the floor's coordinates, and the two devices
# compute them in different arithmetic (D3DIM in x87, the GPU in float).
assert differing < 100, f'{differing} channel values differ by more than 8 between the devices (worst {worst})'
bright, log = run('still', 'gamma=150')
assert 'D3DDEMO gamma control' in log, 'no IDirectDrawGammaControl on the primary'
assert mean(bright, 150) > mean(tnl, 150) * 1.2, f'gamma 150 did not brighten: {mean(bright, 150):.1f} against {mean(tnl, 150):.1f}'
window, log = run('window')
assert 'D3DDEMO windowed' in log and 'failed' not in log, 'the windowed demo stopped'


def pixel(pixels, x, y):
    at = (y * 800 + x) * 3
    return tuple(pixels[at:at + 3])


# Outside the window: the desktop. Inside the client area (the window stands
# at 40,30 with a 480x360 client): the scene, not the desktop colour.
assert pixel(window, 700, 550) == DESKTOP, f'the desktop around the window changed: {pixel(window, 700, 550)}'
inside = sum(pixel(window, x, y) != DESKTOP for x in range(80, 500, 20) for y in range(200, 400, 20))
assert inside > 150, f'only {inside} sampled client pixels show the scene'
print(f'D3DDEMO PASS devices agree (worst {worst}), cube and bump map change {changed} pixels, W buffer {different}, gamma brightens, the window shows the scene')
