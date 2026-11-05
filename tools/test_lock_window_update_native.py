#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build and run the focused native LockWindowUpdate provider test."""
from pathlib import Path
import json
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/lock-window-update"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sources = ["gdi", "dib_text", "dibeng", "win98_rect", "user", "user_draw",
               "text_edit", "win98_dialog", "win98_parameters"]
    binary = OUT / "lock-window-update-native"
    command = ["clang++", "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined",
               "-fno-omit-frame-pointer", str(ROOT / "tools/test_lock_window_update_native.cpp")]
    command += [str(ROOT / "src" / (name + ".cpp")) for name in sources]
    command += ["-o", str(binary)]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    (OUT / "native-build.log").write_text(result.stdout + result.stderr)
    result.check_returncode()
    result = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, text=True)
    (OUT / "native-test.log").write_text(result.stdout + result.stderr)
    print(result.stdout + result.stderr, end="")
    result.check_returncode()
    (OUT / "native-results.json").write_text(json.dumps({
        "status": "PASS",
        "asan_ubsan": "PASS",
        "scope": "LockWindowUpdate ownership, locked subtree clip, unrelated drawing, redraw on unlock, and teardown.",
    }, indent=2) + "\n")


if __name__ == "__main__":
    main()
