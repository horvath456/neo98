#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Which scripts and which local inputs produced a generated asset.

Every generated header in build/generated/ carries a provenance record next to
it. The record names the original file the data came from and the hash of the
header itself, so a hand edit of the header is caught. It did not say which
version of the generator produced it, so a changed generator went unnoticed
until somebody deleted the record by hand.

The table below closes that: each provenance file names its generator scripts
and the local inputs they read. `stamp` writes their hashes into the record,
`changed` answers whether any of them differs today. neo98.py regenerates in
that case and still treats a header that differs while the generators are
unchanged as a hand edit, which stays an error.

The imported Win98 disk image is deliberately not listed: it is gigabytes and
is already identified by the hashes of the files taken out of it.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

# Outside a checkout (the setup files of the app bundle) the folder above tools/.
ROOT = next((p for p in Path(__file__).resolve().parents if (p / 'neo98.py').is_file()),
            Path(__file__).resolve().parents[1])

# provenance file -> (generator scripts, local inputs), both relative to ROOT.
SOURCES = {
    'provenance.json': (
        ('tools/import_win98.py',), ()),
    'registry-provenance.json': (
        ('tools/import_registry.py', 'tools/network_setup.py', 'tools/display_setup.py', 'tools/hardware_setup.py',
         'src/neo98_hardware.h'),
        ('build/win98-c/WINDOWS/INF/NETAMD.INF', 'build/win98-c/WINDOWS/INF/NETTRANS.INF')),
    'message-table-provenance.json': (
        ('tools/message_table_assets.py',), ('build/programs/KERNEL32.DLL',)),
    'winmine-provenance.json': (
        ('tools/import_winmine.py',), ()),
    'dibeng-provenance.json': (
        ('tools/import_dibeng.py',), ('build/dibeng/DIBENG.DLL',)),
    'text-provenance.json': (
        ('tools/import_text.py', 'tools/import_dibeng.py'),
        ('build/dibeng/DIBENG.DLL', 'build/win98-original/SSERIFE.FON')),
    'font-charset-provenance.json': (
        ('tools/extract_font_charset.py',), ('build/gdi-audit/GDI.EXE',)),
    'font-charset-info-provenance.json': (
        ('tools/extract_font_charset_info.py',), ('build/programs/GDI32.DLL',)),
    'marlett-provenance.json': (
        ('tools/marlett_glyphs.py', 'tools/truetype_hint.py'),
        ('build/nonclient-fidelity/fonts/MARLETT.TTF',)),
    'user-resources-provenance.json': (
        ('tools/import_user_resources.py',),
        ('build/gdi-audit/USER.EXE',)),
    'user-provenance.json': (
        ('tools/test_user_draw.py', 'tools/import_user.py', 'tools/gui_inventory.py',
         'tools/user_draw_bridge.cpp'),
        ('build/gdi-audit/USER.EXE',)),
    'frame-control-provenance.json': (
        ('tools/test_frame_control.py', 'tools/import_frame_control.py', 'tools/import_user.py',
         'tools/gui_inventory.py', 'tools/marlett_glyphs.py', 'tools/truetype_hint.py',
         'tools/frame_control_bridge.cpp'),
        ('build/gdi-audit/USER.EXE', 'build/nonclient-fidelity/fonts/MARLETT.TTF')),
    'dialog-provenance.json': (
        ('tools/import_dialog.py', 'tools/import_user.py', 'tools/gui_inventory.py'),
        ('build/gdi-audit/USER.EXE',)),
}


def _digest(relative):
    path = ROOT / relative
    if not path.is_file():
        return None
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sources(name):
    """{'generator_sha256': {...}, 'input_sha256': {...}} as they are right now.

    Files that do not exist yet are left out, so a first run stamps what it
    actually read and a later run that misses one of them counts as a change.
    """
    scripts, inputs = SOURCES[name]
    record = {}
    for key, listed in (('generator_sha256', scripts), ('input_sha256', inputs)):
        digests = ((item, _digest(item)) for item in listed)
        record[key] = {item: value for item, value in digests if value}
    return record


def stamp(record, name):
    """Add the current generator and input hashes to a provenance record."""
    record.update(sources(name))
    return record


def write(record, name, destination=None):
    """Stamp a provenance record and write it to build/generated/<name>."""
    stamp(record, name)
    target = Path(destination) if destination else ROOT / 'build/generated' / name
    target.write_text(json.dumps(record, indent=2) + '\n')
    return record


def changed(info, name):
    """True when a generator script or a local input differs from the record."""
    if not isinstance(info, dict):
        return True
    current = sources(name)
    return any(info.get(key) != value for key, value in current.items())
