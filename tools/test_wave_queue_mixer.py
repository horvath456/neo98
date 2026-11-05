#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the PCM/queue regression under ASan/UBSan, through serial.py."""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
assert shutil.disk_usage(ROOT).free >= 8.5 * 1024**3
out = ROOT / 'build/wave-queue-mixer'
out.mkdir(exist_ok=True)
flags = ['-std=c++17', '-O1', '-g', '-fsanitize=address,undefined']
objects = []
# Separate compile/link steps avoid a temporary dSYM bundle on macOS.
for source, name in [('src/sound_mixer.cpp', 'mixer'), ('tools/test_wave_queue_mixer.cpp', 'test')]:
    obj = out / (name + '.o')
    subprocess.run(['clang++', *flags, '-c', str(ROOT / source), '-o', str(obj)],
                   check=True, stdin=subprocess.DEVNULL)
    objects.append(str(obj))
subprocess.run(['clang++', *flags, *objects, '-o', str(out / 'test')],
               check=True, stdin=subprocess.DEVNULL)
subprocess.run([str(out / 'test')], check=True, stdin=subprocess.DEVNULL)
