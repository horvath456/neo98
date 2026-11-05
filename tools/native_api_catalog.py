# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Read native function and data registrations without running build generators."""
from pathlib import Path
import re

SPEC = re.compile(r'\{\s*"([A-Z0-9]+\.DLL)"\s*,\s*"([^"]+)"\s*,\s*([\da-fA-FxXuU\s|]+)\}')


def read_catalog(root: Path):
    app = (root / "src/x86_app.cpp").read_text()
    body = app.split("const ApiSpec specs[] =", 1)[1].split("};", 1)[0]
    specs = {}
    for dll, name, encoded in SPEC.findall(body):
        key = (dll, name)
        value = 0
        for constant in encoded.split("|"):
            value |= int(constant.strip().rstrip("uU"), 0)
        if key in specs:
            raise ValueError(f"Duplicate ApiSpec: {dll}!{name}")
        specs[key] = value
    if not specs or len(specs) != len(re.findall(r'\{\s*"', body)):
        raise ValueError("Unparsed ApiSpec expression; update the catalog reader")
    # NativeProcedure publishes this legacy variable itself. The other data
    # symbols are declared by the actual locale and startup providers below.
    if '!strcmp(name, "_adjust_fdiv")' not in app:
        raise ValueError("The direct CRT data publisher changed; update the catalog reader")
    data = {"_adjust_fdiv"}
    for path, method in (("src/x86_locale.cpp", "LocaleExport"), ("src/x86_args.cpp", "ArgumentsExport"), ("src/x86_crt_time.inc", "CrtTimeExport")):
        body = (root / path).read_text().split(f"Application::{method}(", 1)[1].split("\n}", 1)[0]
        names = re.findall(r'!strcmp\(name,\s*"([^"]+)"\)', body)
        if not names:
            raise ValueError(f"No data exports parsed from {method}")
        data.update(names)
    return specs, data
