#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Batch normal/delay PE imports and transitive original DLL dependencies.

Registrations are not compatibility results. Dynamic symbol names still need
runtime coverage. Supply DLL directories in guest search order.
"""
import argparse
import json
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pefile
import dll_routes
from native_api_catalog import read_catalog

ROOT = Path(__file__).resolve().parents[1]


def inventory(exes, directories, forced_native):
    native, data = read_catalog(ROOT)
    aliases = ROOT/'build/generated/native_module_exports.h'
    if aliases.exists():
        for dll, name, ordinal in re.findall(r'\{"([^"]+)", "([^"]+)", (\d+)\}', aliases.read_text()):
            if (dll, name) in native:
                native[dll, 'ordinal_'+ordinal] = native[dll, name]
    # A DLL counts as native when the route table says so, or when the caller
    # forced it with --native-module; every other one is walked as the original
    # even if our catalogue still carries an implementation.
    native_modules = {dll for dll, _ in native if dll_routes.native(dll)}
    # These are real failure contracts extracted from Win98, not missing APIs.
    source = (ROOT/'src/x86_app.cpp').read_text()
    tables = dict((table, dll) for dll, table in re.findall(
        r'if\s*\(\s*item\.argc\s*==\s*0xffffffff\s*&&\s*Equal\(\s*dll\s*,\s*"([^"]+)"\s*\)\s*\)\s*'
        r'for\s*\(\s*const auto\s*&entry\s*:\s*(\w+)\s*\)', source))
    failures = set()
    for path in [*ROOT.glob('src/*exports.h'), *ROOT.glob('build/generated/*unsupported.h')]:
        text = path.read_text()
        for table, dll in tables.items():
            match = re.search(r'\b'+table+r'\[\]\s*=\s*\{(.*?)\};', text, re.S)
            if match:
                for name, ordinal in re.findall(r'\{\s*(?:"([^"]+)"|nullptr)\s*,\s*(\d+)', match[1]):
                    failures.add((dll, 'ordinal_'+ordinal))
                    if name:
                        failures.add((dll, name))
    cache, rows, modules, seen = {}, [], {}, set()

    def read(path):
        if path not in cache:
            pe = pefile.PE(str(path), fast_load=True)
            pe.parse_data_directories(directories=[0, 1, 13])
            cache[path] = pe
        return cache[path]

    def resolve(app, importer, dll, symbol, kind, chain=()):
        row = dict(app=str(app), importer=str(importer), dll=dll, symbol=symbol, kind=kind)
        rows.append(row)
        if dll in native_modules or dll in forced_native:
            if (dll, symbol) in native or (dll == 'MSVCRT.DLL' and symbol in data):
                row['status'] = 'native-registration'
            elif (dll, symbol) in failures:
                row['status'] = 'original-failure-contract'
            else:
                row['status'] = 'missing-native-export'
            return
        path = None
        for directory in dict.fromkeys([app.parent, importer.parent, *directories]):
            matches = sorted(p for p in directory.iterdir() if p.is_file() and p.name.upper() == dll) if directory.is_dir() else []
            if matches:
                path = matches[0].resolve()
                break
        if path is None:
            row['status'] = 'missing-dll'
            return
        exports = getattr(getattr(read(path), 'DIRECTORY_ENTRY_EXPORT', None), 'symbols', [])
        entry = next((e for e in exports if (e.name.decode('ascii') if e.name else '') == symbol
                      or 'ordinal_'+str(e.ordinal) == symbol), None)
        row.update(provider=str(path), status='original-export' if entry else 'missing-original-export')
        visit(app, path)
        if entry and entry.forwarder:
            forward = entry.forwarder.decode('ascii')
            row['forwarder'] = forward
            if (path, symbol) in chain:
                row['status'] = 'forwarder-cycle'
                return
            target, name = forward.rsplit('.', 1)
            resolve(app, path, target.upper() + ('' if target.upper().endswith('.DLL') else '.DLL'),
                    'ordinal_'+name[1:] if name.startswith('#') else name,
                    'forwarder', (*chain, (path, symbol)))

    def visit(app, path):
        if (app, path) in seen:
            return
        seen.add((app, path))
        pe = read(path)
        modules[str(path)] = dict(machine=pe.FILE_HEADER.Machine, subsystem=pe.OPTIONAL_HEADER.Subsystem)
        modules[str(path)]['sections'] = [s.Name.rstrip(b'\0').decode('ascii', errors='replace') for s in pe.sections]
        modules[str(path)]['packed_upx'] = any(s.startswith('UPX') for s in modules[str(path)]['sections'])
        for attr, kind in [('DIRECTORY_ENTRY_IMPORT', 'import'), ('DIRECTORY_ENTRY_DELAY_IMPORT', 'delay-import')]:
            for descriptor in getattr(pe, attr, []):
                for entry in descriptor.imports:
                    resolve(app, path, descriptor.dll.decode('ascii').upper(),
                            entry.name.decode('ascii') if entry.name else 'ordinal_'+str(entry.ordinal), kind)

    try:
        for exe in exes:
            visit(exe.resolve(), exe.resolve())
        gaps = {}
        for row in rows:
            if row['status'].startswith('missing') or row['status'] == 'forwarder-cycle':
                gap = gaps.setdefault(row['dll']+'!'+row['symbol'], dict(status=row['status'], apps=[], importers=[]))
                for field, value in [('apps', row['app']), ('importers', row['importer'])]:
                    if value not in gap[field]:
                        gap[field].append(value)
        return dict(modules=modules, imports=rows, gaps=dict(sorted(gaps.items())),
                    limitations=['Registrations do not prove semantic compatibility.',
                                 'Dynamic loads require runtime coverage.',
                                 'DLL directories must match the guest deployment.'])
    finally:
        for pe in cache.values():
            pe.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', nargs='+', type=Path)
    parser.add_argument('--dll-dir', action='append', type=Path, default=[])
    parser.add_argument('--native-module', action='append', default=[])
    parser.add_argument('--runtime-report', action='append', type=Path, default=[],
                        help='merge dynamic lookups from a probe_application --trace-calls JSON report')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = inventory(args.exe, args.dll_dir, {s.upper() for s in args.native_module})
    for path in args.runtime_report:
        observed = json.loads(path.read_text())
        for entry in observed.get('dynamic_imports', []):
            dll = entry['module'].replace('\\', '/').rsplit('/', 1)[-1].upper()
            report['imports'].append(dict(app=observed['exe'], importer=entry['module'], dll=dll,
                symbol=entry['symbol'], kind='dynamic-observation',
                status='diagnostic-stub-observation' if entry['diagnostic_stubs_enabled'] else
                       'runtime-resolved' if entry['resolved'] else 'runtime-unresolved'))
            if not entry['resolved']:
                report['gaps'].setdefault(dll+'!'+entry['symbol'], dict(status='runtime-unresolved',
                    apps=[observed['exe']], importers=[entry['module']]))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    print(f"{len(report['modules'])} PE modules, {len(report['imports'])} import edges, {len(report['gaps'])} distinct gaps")
    for name, gap in report['gaps'].items():
        print(f"{name}: {gap['status']} ({len(gap['apps'])} apps)")
    for path, module in report['modules'].items():
        if module['packed_upx']:
            print(f"Packed UPX image: {path}; unpacked imports require additional analysis")


if __name__ == '__main__':
    main()
