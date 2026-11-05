#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build and run the descendant-invalidation regression: showing a window invalidates its visible descendants."""
from pathlib import Path
import json
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/show-children-paint"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sources = ["gdi", "dib_text", "dibeng", "win98_rect", "user", "user_draw",
               "text_edit", "win98_dialog", "win98_parameters"]
    binary = OUT / "show-children-paint-native"
    command = ["clang++", "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined",
               "-fno-omit-frame-pointer", str(ROOT / "tools/test_show_children_paint_native.cpp")]
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
        "scope": "ShowWindow of a hidden window invalidates every visible descendant, also one that set WS_VISIBLE through SetWindowLong in WM_CREATE.",
    }, indent=2) + "\n")


if __name__ == "__main__":
    main()
