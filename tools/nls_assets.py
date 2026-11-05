#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Extract original local Win98 NLS tables for the native KERNEL32 conversion code."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import win98_c  # noqa: E402
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))


def generate():
    from neo98 import win98_assets
    source = win98_assets()['source']
    out = ROOT / 'build/nls'
    out.mkdir(exist_ok=True)
    text = ['// Original local Win98 NLS tables. Do not redistribute.']
    records = []
    for page in (1252, 437, 850, 866):
        name = f'CP_{page}.NLS'
        file = out / name
        if not file.exists():
            win98_c.copy(f'::/WINDOWS/SYSTEM/{name}', str(file))
        data = file.read_bytes()
        text.append(f'static const unsigned char OriginalCodePage{page}[] = {{')
        text.extend(','.join(str(b) for b in data[i:i+24]) + ',' for i in range(0, len(data), 24))
        text.append('};')
        records.append({'codepage': page, 'file': name, 'bytes': len(data),
                        'sha256': hashlib.sha256(data).hexdigest()})
    header = ROOT / 'build/generated/nls_tables.h'
    contents = '\n'.join(text) + '\n'
    if not header.exists() or header.read_text() != contents:
        header.write_text(contents)
    (out / 'provenance.json').write_text(json.dumps(records, indent=2) + '\n')
    name = 'UNICODE.NLS'
    file = out / name
    if not file.exists():
        win98_c.copy(f'::/WINDOWS/SYSTEM/{name}', str(file))
    data = file.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != '3d8d27915d9e0d7d9e5ae9fe31038239818f57a48471a933d4c5fc86d8c3492d':
        raise ValueError('Unexpected original UNICODE.NLS; audit this version before importing')
    text = ['// Original local Win98 Unicode data. Do not redistribute.',
            'static const unsigned char OriginalUnicodeComposition[] = {']
    text.extend(','.join(str(b) for b in data[i:i+24]) + ',' for i in range(0, len(data), 24))
    text.append('};')
    header = ROOT / 'build/generated/nls_unicode.h'
    contents = '\n'.join(text) + '\n'
    if not header.exists() or header.read_text() != contents:
        header.write_text(contents)
    (out / 'unicode-provenance.json').write_text(json.dumps(
        {'file': name, 'bytes': len(data), 'sha256': digest,
         'source': source, 'consumer': 'native original SBCS Unicode composition and decomposition'}, indent=2) + '\n')
    return records

if __name__ == '__main__':
    generate()
