#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Unpack the PostScript printer driver of the Win98 SE CD into build/printer-driver.

The files are the [PSCRIPT] section of MSPRINT.INF for the model "Apple LaserWriter Plus"
(APPLE380.SPD): PSCRIPT.DRV, PSCRIPT.HLP, PSCRIPT.INI, TESTPS.TXT, APPLE380.SPD, FONTS.MFM,
ICONLIB.DLL and PSMON.DLL; MSPRINT.INF itself is in the tree (WINDOWS\\INF). They are in the one cabinet set of the
WIN98 directory of the CD, which runs from BASE4.CAB over the DRIVER, NET and WIN98_nn cabinets;
cabextract follows the set, so the whole directory is unpacked for the extraction and removed
again. Nothing of the CD is checked in; the files only go to build/.

    build/analysis-venv/bin/python tools/import_printer_driver.py [--iso PATH]
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/printer-driver'
DEFAULT_ISO = Path.home() / 'Downloads/Windows 98 Second Edition.iso'
# Name and SHA-256 of every file, read from the Win98 SE CD on 23 September 2026.
FILES = {
    'PSCRIPT.DRV': 'f25606f5756e1f0373d9882e8cea58d6f600e823329f58c79addf84b05af5323',
    'PSCRIPT.HLP': 'f04d02d6bffd8efae631672829278c857b9ab00a6984fdc261d2fe3b13d4b912',
    'PSCRIPT.INI': '24504b2ad45c5e7f845cba1d90d6331311e51862c2b515a61f1301de0c467805',
    'TESTPS.TXT': 'ebcea08c1ef7e6146ad74fec109e682e5c12a99e45c9d6e5f9b43a2309f7a0b2',
    'APPLE380.SPD': '3c39824824da32db93ac4cf9a62be15d6896ba63252418a340c003fc9bf91289',
    'FONTS.MFM': '4e8fa2d66eca983f0e14c9338e6f81a06998a490c865d96abe6616f12fe68296',
    'ICONLIB.DLL': '19361e16c8fe6bf08fb309bf111b11d3f78472aff31f907184eda5afeb324794',
    'PSMON.DLL': '980de80f16d9d296d1580bda8a3284675d6d8b2f724ddc220b146675e943341e',
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--iso', type=Path, default=DEFAULT_ISO)
    args = parser.parse_args()
    if not args.iso.is_file():
        raise SystemExit(f'The Win98 SE CD image is not at {args.iso}; pass --iso.')
    cabinets = OUT / 'cabinets'
    files = OUT / 'files'
    cabinets.mkdir(parents=True, exist_ok=True)
    files.mkdir(parents=True, exist_ok=True)
    subprocess.run(['bsdtar', '-xf', str(args.iso), '-C', str(cabinets), '--strip-components', '1',
                    '--include', 'win98/*.CAB', '--exclude', 'win98/WIN98_OL.CAB',
                    '--exclude', 'win98/PRECOPY*.CAB'], check=True)
    for path in cabinets.iterdir():
        path.chmod(0o644)
    for name in FILES:
        subprocess.run(['cabextract', '-q', '-F', name.lower(), '-d', str(files),
                        str(cabinets / 'BASE4.CAB')], check=True, capture_output=True)
    shutil.rmtree(cabinets)
    manifest = {}
    for name in FILES:
        found = [p for p in files.iterdir() if p.name.upper() == name]
        if not found:
            raise SystemExit(f'{name} is missing from the cabinet set')
        path = found[0]
        if path.name != name:
            path = path.rename(files / name)
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if FILES[name] and FILES[name] != digest:
            raise SystemExit(f'{name} has SHA-256 {digest}, expected {FILES[name]}')
        manifest[name] = {'bytes': path.stat().st_size, 'sha256': digest}
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=1, sort_keys=True) + '\n')
    for name, entry in manifest.items():
        print(f"{name:14} {entry['bytes']:7} {entry['sha256']}")


if __name__ == '__main__':
    main()
