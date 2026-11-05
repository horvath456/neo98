#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Python view of the one DLL route table in src/dll_routes.h.

The C++ header is the single source of truth. This module parses it so no
second table exists in the repository.

    ROUTES dict 'KERNEL32.DLL' -> {'route':..., 'reason':...}
    route(leaf) 'native' | 'native-today' | 'native16' | 'original'
    native(leaf) True for 'native' and 'native-today'

A DLL the table does not name runs as the original, so route answers
'original' for it.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'src/dll_routes.h'

# Route::Native -> the name PLAN.md and the JSON reports use.
NAMES = {'Native': 'native', 'NativeToday': 'native-today',
         'Native16': 'native16', 'Original': 'original'}

ENTRY = re.compile(r'\{\s*"([^"]+)"\s*,\s*Route::(\w+)\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}')


class RouteTableError(RuntimeError):
    """src/dll_routes.h does not have the documented one-line entry format."""


def read_table(header: Path = HEADER):
    """Parse the Table initialiser of the header into an ordered dict."""
    if not header.is_file():
        raise RouteTableError(f'The DLL route table is missing: {header}')
    text = header.read_text()
    match = re.search(r'Entry\s+Table\[\]\s*=\s*\{(.*?)\n\};', text, re.S)
    if not match:
        raise RouteTableError(f'No `Entry Table[] = {{...}};` initialiser in {header}')
    body = match.group(1)
    table = {}
    for line in body.splitlines():
        code = line.split('//', 1)[0].strip()
        if not code:
            continue
        entry = ENTRY.fullmatch(code.rstrip(','))
        if not entry:
            raise RouteTableError(
                f'{header}: entry is not one line of the form '
                f'{{"NAME.DLL", Route::X, "reason"}}: {line.strip()!r}')
        dll, route, reason = entry.group(1).upper(), entry.group(2), entry.group(3)
        if route not in NAMES:
            raise RouteTableError(f'{header}: unknown Route::{route} for {dll}')
        if dll in table:
            raise RouteTableError(f'{header}: duplicate entry for {dll}')
        table[dll] = {'route': NAMES[route], 'reason': reason}
    if not table:
        raise RouteTableError(f'{header}: the route table is empty')
    # The header must also declare every route the enum names, otherwise the
    # parse silently dropped a form we do not recognise.
    declared = set(re.findall(r'^\s*(\w+),\s*//', text, re.M))
    if not set(NAMES) <= declared:
        raise RouteTableError(f'{header}: enum Route no longer declares {sorted(set(NAMES) - declared)}')
    return table


ROUTES = read_table()


def route(dll_leaf_name: str) -> str:
    """Route of one DLL leaf name; 'original' when the table does not name it."""
    return ROUTES.get(str(dll_leaf_name).upper(), {}).get('route', 'original')


def native(dll_leaf_name: str) -> bool:
    """True when our loader resolves the DLL to a native ARM64 provider."""
    return route(dll_leaf_name) in ('native', 'native-today')


def main():
    width = max(len(dll) for dll in ROUTES)
    routes = max(len(entry['route']) for entry in ROUTES.values())
    print(f'{"DLL".ljust(width)}  {"ROUTE".ljust(routes)}  REASON')
    for dll, entry in sorted(ROUTES.items()):
        print(f'{dll.ljust(width)}  {entry["route"].ljust(routes)}  {entry["reason"]}')
    print(f'\n{len(ROUTES)} entries from {HEADER.relative_to(ROOT)}; '
          'every DLL not named here runs as the original.')


if __name__ == '__main__':
    sys.exit(main())
