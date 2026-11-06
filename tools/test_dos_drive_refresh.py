#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""New DOS sessions see drives mounted while an earlier session runs."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import neo98
from build_dosbox_pure import core_dylib

out = ROOT / 'build/dos-drive-refresh'
out.mkdir(parents=True, exist_ok=True)
first, second = out / 'first', out / 'second'
first.mkdir(exist_ok=True)
second.mkdir(exist_ok=True)
(first / 'WAIT.COM').write_bytes(bytes.fromhex('b400cd16b8074ccd21'))
(first / 'DONE.COM').write_bytes(bytes.fromhex('b82a4ccd21'))
(second / 'DONE.COM').write_bytes(bytes.fromhex('b82b4ccd21'))
core = core_dylib()
binary = out / 'test-dos-drive-refresh'
flags = ['-std=c++17', '-O1', '-g', '-fsanitize=address,undefined', '-pthread', f'-DNEO98_DOSBOX_CORE="{core}"']
subprocess.run(['clang++', *flags, str(ROOT / 'tools/test_dos_drive_refresh.cpp'),
                str(ROOT / 'tools/host_dos_vm.cpp'), str(ROOT / 'src/dos_vm.cpp'), str(ROOT / 'src/sound_synth.cpp'),
                    str(ROOT / 'src/sound_microphone.cpp'), str(ROOT / 'src/sound_mixer.cpp'),
                    str(ROOT / 'src/pc_speaker.cpp'),
                '-o', str(binary)], check=True, env=neo98.tool_environment(), cwd=ROOT)
result = subprocess.run([str(binary), str(first), str(second)], cwd=ROOT, capture_output=True, text=True, timeout=90)
(out / 'native.log').write_text(result.stdout + result.stderr)
print(result.stdout)
if result.returncode:
    print(result.stderr[-6000:])
assert result.returncode == 0, result.returncode
