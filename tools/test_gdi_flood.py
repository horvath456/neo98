#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/gdi-flood"
OUT.mkdir(parents=True, exist_ok=True)
binary = OUT / "gdi-flood-fixture"
subprocess.run([
    "clang++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
    str(ROOT / "tools/gdi_flood_fixture.cpp"), "-o", str(binary),
], check=True)
subprocess.run([str(binary)], check=True)
print("GDI FloodFill traversal: PASS (unchanged ROP, patterned output, concave region)")
