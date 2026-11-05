#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Fail if a release embeds a locally extracted Windows array or includes one.

Fixtures remain local. The committed manifest contains only lengths and hashes.
Run after building, with --root pointing at the local comparison fixture tree.
A missing or changed fixture is an error, never a skipped check.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN = {
    'dibeng_rops.h', 'dibeng_vectors.h', 'text_vectors.h', 'user_vectors.h',
    'frame_control_vectors.h', 'win98_vectors.h', 'dialog_reference.h',
    'win98_message_table.h', 'nls_tables.h', 'nls_unicode.h', 'user_resources.h',
    'user_menus.h', 'atom_upper.h', 'font_charset_tables.h', 'font_charset_info.h',
    'computer_name_tables.h', 'system_assets.h', 'notepad_program.h',
    'notepad-fixedsys.fnt', 'text_fonts.h', 'marlett_glyphs.h', 'win98_font.h',
    'winmine_bitmaps.h', 'win98_registry_data.h', 'unicorn_user32.h',
    'gdi_palette_tables.h', 'gdi_object_tables.h', 'win98_timezone_registry_data.h', 'winmm_messages.h', 'crt_locale_data.h',
}
FORBIDDEN_PATHS = {'serialui-assets/templates.h', 'property-sheet-assets/templates.h'}

def check_dependencies(paths):
    problems = []
    for path in paths:
        path = Path(path)
        if path.name in FORBIDDEN or any(path.as_posix().endswith(p) for p in FORBIDDEN_PATHS):
            problems.append('Forbidden release input: ' + path.name)
    return problems


def arrays(text):
    text = re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)
    result = {}
    pattern = r'(?:static\s+const|inline\s+(?:const|constexpr)|constexpr)\s+unsigned(\s+char|\s+short)?\s+(\w+)\s*(?:\[[^\]]*\])+\s*=\s*\{(.*?)\};'
    for kind, name, body in re.findall(pattern, text, re.S):
        width = {'char': 1, 'short': 2, '': 4}[kind.strip()]
        values = re.findall(r'0x[\da-fA-F]+|\b\d+', body)
        result[name] = b''.join(int(v, 0 if v.startswith('0x') else 10).to_bytes(width, 'little') for v in values)
    return result


def check(binary, root=ROOT, manifest=None):
    records = json.loads((manifest or ROOT / 'tools/release_embedded_fingerprints.json').read_text())
    data = Path(binary).read_bytes()
    if not data: raise ValueError('Empty release binary')
    cache, results = {}, []
    for record in records:
        header = record['header']
        if header not in cache: cache[header] = arrays((root / header).read_text())
        value = cache[header].get(record['symbol'])
        if value is None or len(value) != record['bytes'] or hashlib.sha256(value).hexdigest() != record['sha256']:
            raise ValueError('Missing or changed comparison fixture: ' + header + ':' + record['symbol'])
        offset = data.find(value)
        results.append({**record, 'full_match_in_host': offset >= 0, 'file_offset': offset if offset >= 0 else None})
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--dependencies', type=Path, help='JSON list of actual linked compiler dependencies')
    args = parser.parse_args()
    try:
        result = check(args.binary, args.root)
        problems = check_dependencies(json.loads(args.dependencies.read_text())) if args.dependencies else []
        hits = sum(r['full_match_in_host'] for r in result)
        if args.output: args.output.write_text(json.dumps(result, indent=2) + '\n')
        print(f'{len(result)} arrays checked; {hits} embedded matches; {len(problems)} forbidden includes')
        for problem in problems: print(problem)
        return int(bool(hits or problems))
    except (ValueError, OSError) as error:
        parser.exit(1, str(error) + '\n')

if __name__ == '__main__':
    raise SystemExit(main())
