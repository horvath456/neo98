#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Real DOS speaker audio after 1.8 s without a consumer, under ASan/UBSan."""
import subprocess
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98
from build_dosbox_pure import core_dylib
out = ROOT / 'build/dos-audio-latency'
out.mkdir(parents=True, exist_ok=True)
# Speaker on, wait for Enter, speaker off, wait again (keep producing silence).
(out / 'TONE.COM').write_bytes(bytes.fromhex(
    'b0b6e643b8a904e64288e0e642e4610c03e661b400cd16e46124fce661b400cd16b8004ccd21'))
core = core_dylib()
binary = out / 'test-dos-audio-latency'
subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined', '-pthread',
    f'-DNEO98_DOSBOX_CORE="{core}"', str(ROOT / 'tools/test_dos_audio_latency.cpp'),
    str(ROOT / 'tools/host_dos_vm.cpp'), str(ROOT / 'src/dos_vm.cpp'), str(ROOT / 'src/sound_synth.cpp'),
                    str(ROOT / 'src/sound_microphone.cpp'), str(ROOT / 'src/sound_mixer.cpp'),
                    str(ROOT / 'src/pc_speaker.cpp'),
    '-o', str(binary)], check=True, env=neo98.tool_environment(), cwd=ROOT)
result = subprocess.run([str(binary), str(out)], capture_output=True, text=True, timeout=30)
(out / 'native.log').write_text(result.stdout + result.stderr)
print(result.stdout)
if result.returncode:
    print(result.stderr[-6000:])
raise SystemExit(result.returncode)
