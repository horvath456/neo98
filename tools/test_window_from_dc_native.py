#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build and run the native USER/GDI WindowFromDC lifecycle test."""
from pathlib import Path
import json
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/window-from-dc"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sources = ["gdi", "dib_text", "dibeng", "win98_rect", "user", "user_draw",
               "text_edit", "win98_dialog", "win98_parameters"]
    binary = OUT / "window-from-dc-native"
    command = ["clang++", "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined",
               "-fno-omit-frame-pointer", str(ROOT / "tools/test_window_from_dc_native.cpp")]
    command += [str(ROOT / "src" / (name + ".cpp")) for name in sources]
    command += ["-o", str(binary)]
    result = subprocess.run(command, capture_output=True, text=True)
    (OUT / "native-build.log").write_text(result.stdout + result.stderr)
    result.check_returncode()
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    (OUT / "native-test.log").write_text(result.stdout + result.stderr)
    print(result.stdout + result.stderr, end="")
    result.check_returncode()
    (OUT / "native-results.json").write_text(json.dumps({
        "status": "PASS",
        "asan_ubsan": "PASS",
        "scope": ("USER-owned canvas, paint, and shared screen DC association; "
                   "memory/display DC exclusion; release, destruction, and handle "
                   "generation invalidation."),
    }, indent=2) + "\n")


if __name__ == "__main__":
    main()
