#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""What is missing at our native edge, answered statically over a Win98 tree.

Takes EXE or DLL files and directories, resolves normal and delay imports
recursively through the ORIGINAL DLLs of an extracted Win98 volume
(build/win98-c) and decides for every imported symbol whether our ARM64
port can satisfy it. The decision follows the one route table in
src/dll_routes.h (read through tools/dll_routes.py):

  native / native-today
      The symbol must exist in our native catalogue (src/x86_app.cpp ApiSpec,
      the ordinal aliases of build/generated/native_module_exports.h, the CRT
      and locale data exports) or in one of the original failure contracts we
      reproduce on purpose. Anything else is `missing-native-export` and is
      THE gap at our native edge: a real application asks for it and no
      provider answers.
  native16
      A 16-bit NE module we implement behind Win16 ordinals. Not a gap.
  original
      The DLL runs as original x86 code. The symbol is looked up in the export
      table of the DLL in the tree, forwarders are chased (cycle guarded) and
      the imports of that DLL are walked in turn. A symbol the original DLL
      does not export is `missing-original-export`, a DLL that is not in the
      tree at all is `missing-dll`.

--native-module NAME forces one DLL onto its native route, the same way
Application::AddNativeModule does for a process at runtime. That is the test
override of the DLL rule.

JSON schema (version 1), stable and reproducible: keys are sorted, no
timestamps, paths are relative to the repository root where possible and
otherwise written with a leading `~/`.

  version       1
  inputs        [ {argument, kind: 'file'|'directory'|'missing', path, exes} ]
                `argument` is the command line word, `path` its resolved and
                shortened form, `exes` the number of images taken from it.
  route_table   { 'KERNEL32.DLL': {route, reason, forced_native: bool} }
                The effective table of this run, including the DLLs that only
                --native-module named.
  modules       { path: {machine, subsystem, sections, packed_upx, imports,
                         role: 'input'|'library', skipped?: reason} }
                Every file that was opened. `skipped` names why a file was not
                analysed: 'not-a-pe', 'ne-16bit-module', 'le-module',
                'upx-packed' or 'pe-parse-error: ...'. A skipped module has no
                import edges in the result. A UPX packed DLL still answers
                export lookups, only its own imports stay unknown.
  gaps          { 'DLL!Symbol': {status, exes, importers, count} }
                status is one of 'missing-native-export',
                'missing-original-export', 'missing-dll', 'forwarder-cycle'.
                `exes` are the input images affected, `importers` the modules
                that carry the import, `count` is len(exes).
  top           [ {symbol, status, count} ] sorted by count descending then
                symbol ascending, cut to --top.
  limitations   [ str ] what this static answer does not cover.
"""
import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import pefile

import dll_routes
from native_api_catalog import read_catalog

ROOT = Path(__file__).resolve().parents[1]
HOME = Path.home()

GAP_STATUS = ('missing-native-export', 'missing-original-export', 'missing-dll', 'forwarder-cycle')

LIMITATIONS = [
    'A registration is not a compatibility result: a symbol our catalogue names may still behave differently.',
    'Only the import tables are read. LoadLibrary/GetProcAddress names need runtime coverage (probe_application --trace-calls).',
    'UPX packed and 16-bit NE images are reported as skipped; their real imports only appear at runtime.',
    'Resource-only and data DLLs without an export table look like missing-original-export for every symbol asked of them.',
    'The DLL search order is the Win98 one for a clean tree; SetDllDirectory, KnownDLLs and side-by-side copies are not modelled.',
]


def show(path):
    """A stable, machine independent spelling of a path."""
    path = Path(path)
    for base, prefix in ((ROOT, ''), (HOME, '~/')):
        try:
            return prefix + path.relative_to(base).as_posix()
        except ValueError:
            pass
    return path.as_posix()


def guest_path(argument, tree):
    """Map a guest path such as C:\\WINDOWS into the extracted tree."""
    text = str(argument)
    if re.fullmatch(r'[A-Za-z]:[\\/].*', text) or text.startswith('\\'):
        parts = [p for p in re.split(r'[\\/]+', text)[1:] if p]
        here = tree
        for part in parts:
            matches = [p for p in here.iterdir() if p.name.upper() == part.upper()] if here.is_dir() else []
            if not matches:
                return here / part
            here = sorted(matches)[0]
        return here
    return Path(text).expanduser()


class Catalog:
    """Our native providers and the failure contracts we reproduce."""

    def __init__(self, root=ROOT):
        self.specs, self.data = read_catalog(root)
        aliases = root / 'build/generated/native_module_exports.h'
        if aliases.exists():
            for dll, name, ordinal in re.findall(r'\{"([^"]+)", "([^"]+)", (\d+)\}', aliases.read_text()):
                if (dll, name) in self.specs:
                    self.specs[dll, 'ordinal_' + ordinal] = self.specs[dll, name]
        self.failures = set()
        source = (root / 'src/x86_app.cpp').read_text()
        tables = dict((table, dll) for dll, table in re.findall(
            # The dispatcher spells these blocks with and without spaces, so the
            # pattern stays whitespace tolerant; otherwise a compactly written
            # table looks like a gap although the contract is already reproduced.
            r'if\s*\(\s*item\.argc\s*==\s*0xffffffff\s*&&\s*Equal\(\s*dll\s*,\s*"([^"]+)"\s*\)\s*\)\s*'
            r'for\s*\(\s*const auto\s*&entry\s*:\s*(\w+)\s*\)', source))
        for path in [*root.glob('src/*exports.h'), *root.glob('build/generated/*unsupported.h')]:
            text = path.read_text()
            for table, dll in tables.items():
                match = re.search(r'\b' + table + r'\[\]\s*=\s*\{(.*?)\};', text, re.S)
                if match:
                    for name, ordinal in re.findall(r'\{\s*(?:"([^"]+)"|nullptr)\s*,\s*(\d+)', match[1]):
                        self.failures.add((dll, 'ordinal_' + ordinal))
                        if name:
                            self.failures.add((dll, name))

    def status(self, dll, symbol):
        if (dll, symbol) in self.specs or (dll == 'MSVCRT.DLL' and symbol in self.data):
            return 'native-registration'
        if (dll, symbol) in self.failures:
            return 'original-failure-contract'
        return 'missing-native-export'


class Tree:
    """Case insensitive lookups and cached PE parses over the Win98 tree."""

    def __init__(self, tree):
        self.tree = tree
        self.search = [d for d in (tree / 'WINDOWS/SYSTEM', tree / 'WINDOWS') if d.is_dir()]
        if not self.search:
            self.search = [d for d in (guest_path('C:\\WINDOWS\\SYSTEM', tree), guest_path('C:\\WINDOWS', tree)) if d.is_dir()]
        self.listings = {}
        self.pes = {}
        self.modules = {}
        self.imports = {}
        self.exports = {}
        self.located = {}

    def listing(self, directory):
        directory = Path(directory)
        key = str(directory)
        if key not in self.listings:
            table = {}
            try:
                for entry in sorted(directory.iterdir()):
                    if entry.is_file():
                        table.setdefault(entry.name.upper(), entry)
            except OSError:
                pass
            self.listings[key] = table
        return self.listings[key]

    def locate(self, dll, directories):
        key = (dll, tuple(str(d) for d in directories))
        if key not in self.located:
            found = None
            for directory in directories:
                found = self.listing(directory).get(dll)
                if found is not None:
                    break
            self.located[key] = found
        return self.located[key]

    def kind(self, path):
        """'pe', 'ne-16bit-module', 'le-module' or 'not-a-pe' from the headers."""
        try:
            with open(path, 'rb') as handle:
                head = handle.read(0x40)
                if len(head) < 0x40 or head[:2] not in (b'MZ', b'ZM'):
                    return 'not-a-pe'
                offset = int.from_bytes(head[0x3c:0x40], 'little')
                handle.seek(offset)
                signature = handle.read(4)
        except OSError:
            return 'not-a-pe'
        if signature[:4] == b'PE\0\0':
            return 'pe'
        if signature[:2] == b'NE':
            return 'ne-16bit-module'
        if signature[:2] in (b'LE', b'LX'):
            return 'le-module'
        return 'not-a-pe'

    def module(self, path, role='library'):
        """Open a file once. Returns the modules record; `skipped` means unusable."""
        path = Path(path)
        key = show(path)
        record = self.modules.get(key)
        if record is not None:
            if role == 'input':
                record['role'] = 'input'
            return record
        record = {'role': role}
        self.modules[key] = record
        kind = self.kind(path)
        if kind != 'pe':
            record['skipped'] = kind
            return record
        try:
            pe = pefile.PE(str(path), fast_load=True)
            pe.parse_data_directories(directories=[0, 1, 13])
        except Exception as error:  # pefile raises a whole family here
            record['skipped'] = 'pe-parse-error: ' + str(error).strip().replace('\n', ' ')[:120]
            return record
        self.pes[key] = pe
        record['machine'] = int(pe.FILE_HEADER.Machine)
        record['subsystem'] = int(pe.OPTIONAL_HEADER.Subsystem)
        record['sections'] = [s.Name.rstrip(b'\0').decode('ascii', errors='replace') for s in pe.sections]
        record['packed_upx'] = any(s.startswith('UPX') for s in record['sections'])
        edges = []
        for attribute, kind in (('DIRECTORY_ENTRY_IMPORT', 'import'), ('DIRECTORY_ENTRY_DELAY_IMPORT', 'delay-import')):
            for descriptor in getattr(pe, attribute, []):
                try:
                    dll = descriptor.dll.decode('ascii', errors='replace').upper()
                except Exception:
                    continue
                if not dll.endswith('.DLL') and '.' not in dll:
                    dll += '.DLL'
                for entry in descriptor.imports:
                    name = entry.name.decode('ascii', errors='replace') if entry.name else 'ordinal_%d' % (entry.ordinal or 0)
                    edges.append((dll, name, kind))
        record['imports'] = len(edges)
        self.imports[key] = edges
        if record['packed_upx']:
            record['skipped'] = 'upx-packed'
            self.imports[key] = []
        return record

    def readable(self, path):
        """True when the file parsed as a PE, so its export table is usable."""
        return show(path) in self.pes

    def export_table(self, path):
        """name -> forwarder or None, with ordinal_N aliases."""
        key = show(path)
        if key not in self.exports:
            table = {}
            pe = self.pes.get(key)
            for symbol in getattr(getattr(pe, 'DIRECTORY_ENTRY_EXPORT', None), 'symbols', []):
                forwarder = symbol.forwarder.decode('ascii', errors='replace') if symbol.forwarder else None
                if symbol.name:
                    table[symbol.name.decode('ascii', errors='replace')] = forwarder
                table['ordinal_%d' % symbol.ordinal] = forwarder
            self.exports[key] = table
        return self.exports[key]

    def close(self):
        for pe in self.pes.values():
            pe.close()
        self.pes.clear()


class Scan:
    def __init__(self, tree, catalog, forced_native):
        self.tree = tree
        self.catalog = catalog
        self.forced = forced_native
        self.gaps = {}
        self.exe_count = 0

    def route(self, dll):
        return 'native' if dll in self.forced else dll_routes.route(dll)

    def record(self, key, status, exe, importer):
        gap = self.gaps.setdefault(key, {'status': status, 'exes': set(), 'importers': set()})
        gap['exes'].add(exe)
        gap['importers'].add(importer)

    def run(self, exe, argument_dir):
        """Walk one input image and everything it pulls in."""
        exe = Path(exe)
        record = self.tree.module(exe, role='input')
        name = show(exe)
        if 'skipped' in record:
            return False
        self.exe_count += 1
        search_tail = [argument_dir, *self.tree.search]
        walked = set()
        pending = [exe]
        while pending:
            module = pending.pop()
            key = show(module)
            if key in walked:
                continue
            walked.add(key)
            for dll, symbol, _kind in self.tree.imports.get(key, ()):
                target = self.resolve(name, module, dll, symbol, search_tail, walked, pending, ())
                if target is not None and show(target) not in walked:
                    pending.append(target)
        return True

    def resolve(self, exe, importer, dll, symbol, search_tail, walked, pending, chain):
        """Classify one import edge. Returns the original DLL to walk next."""
        route = self.route(dll)
        if route in ('native', 'native-today'):
            status = self.catalog.status(dll, symbol)
            if status == 'missing-native-export':
                self.record(dll + '!' + symbol, status, exe, show(importer))
            return None
        if route == 'native16':
            return None
        directories = [Path(importer).parent, *search_tail]
        path = self.tree.locate(dll, directories)
        if path is None:
            self.record(dll + '!' + symbol, 'missing-dll', exe, show(importer))
            return None
        self.tree.module(path)
        if not self.tree.readable(path):
            # A 16-bit NE, an LE driver or a broken file cannot answer a 32-bit
            # import. A UPX packed DLL still carries its real export table, so
            # it is resolved here and only its own imports stay unknown.
            self.record(dll + '!' + symbol, 'missing-original-export', exe, show(importer))
            return None
        forwarder = self.tree.export_table(path)
        if symbol not in forwarder:
            self.record(dll + '!' + symbol, 'missing-original-export', exe, show(importer))
            return path
        target = forwarder[symbol]
        if target:
            if (show(path), symbol) in chain:
                self.record(dll + '!' + symbol, 'forwarder-cycle', exe, show(importer))
                return path
            module, name = target.rsplit('.', 1)
            module = module.upper()
            if not module.endswith('.DLL'):
                module += '.DLL'
            following = self.resolve(exe, path, module,
                                     'ordinal_' + name[1:] if name.startswith('#') else name,
                                     search_tail, walked, pending, (*chain, (show(path), symbol)))
            if following is not None and show(following) not in walked:
                pending.append(following)
        return path


def gather(argument, tree):
    """Resolve one command line word into (kind, base directory, [images])."""
    path = guest_path(argument, tree)
    if path.is_dir():
        images = sorted((p for p in path.rglob('*') if p.is_file() and p.suffix.upper() == '.EXE'),
                        key=lambda p: str(p).upper())
        return 'directory', path, images
    if path.is_file():
        return 'file', path.parent, [path]
    return 'missing', path.parent, []


def report(arguments, tree_root, forced_native, top_n, recursive):
    tree = Tree(tree_root)
    catalog = Catalog()
    scan = Scan(tree, catalog, forced_native)
    inputs = []
    for argument in arguments:
        kind, base, images = gather(argument, tree_root)
        if kind == 'directory' and not recursive:
            images = [p for p in images if p.parent == base]
        taken = 0
        for image in images:
            if scan.run(image, base):
                taken += 1
        text = str(argument)
        for prefix, replacement in ((str(ROOT) + '/', ''), (str(HOME) + '/', '~/')):
            if text.startswith(prefix):
                text = replacement + text[len(prefix):]
        inputs.append({'argument': text, 'kind': kind,
                       'path': show(base if kind == 'directory' else images[0] if images else base),
                       'exes': taken})
    table = {}
    for dll, entry in dll_routes.ROUTES.items():
        table[dll] = {'route': 'native' if dll in forced_native else entry['route'],
                      'reason': entry['reason'], 'forced_native': dll in forced_native}
    for dll in sorted(forced_native):
        table.setdefault(dll, {'route': 'native', 'reason': '--native-module on the command line',
                               'forced_native': True})
    gaps = {}
    for key in sorted(scan.gaps):
        gap = scan.gaps[key]
        gaps[key] = {'status': gap['status'], 'exes': sorted(gap['exes']),
                     'importers': sorted(gap['importers']), 'count': len(gap['exes'])}
    order = sorted(gaps.items(), key=lambda item: (-item[1]['count'], item[0]))
    top = [{'symbol': name, 'status': gap['status'], 'count': gap['count']}
           for name, gap in (order[:top_n] if top_n else order)]
    result = {'version': 1, 'inputs': inputs, 'route_table': dict(sorted(table.items())),
              'modules': dict(sorted(tree.modules.items())), 'gaps': gaps, 'top': top,
              'limitations': LIMITATIONS}
    tree.close()
    return result, scan.exe_count, show(tree_root)


def text_report(result, exes, tree_name):
    lines = []
    skipped = {}
    for name, module in result['modules'].items():
        if 'skipped' in module:
            skipped.setdefault(module['skipped'].split(':')[0], []).append(name)
    lines.append(f'Unimplemented imports at our native edge, tree {tree_name}')
    lines.append('Inputs: ' + ', '.join(f"{i['argument']} ({i['exes']} EXE)" for i in result['inputs']))
    lines.append(f"{exes} EXEs scanned, {len(result['modules'])} modules opened, "
                 f"{sum(len(v) for v in skipped.values())} skipped, {len(result['gaps'])} distinct gaps")
    for reason in sorted(skipped):
        lines.append(f'  skipped {reason}: {len(skipped[reason])}')
    lines.append('')
    lines.append('COUNT  STATUS                    DLL!SYMBOL')
    for name, gap in sorted(result['gaps'].items(), key=lambda item: (-item[1]['count'], item[0])):
        lines.append(f"{gap['count']:5d}  {gap['status']:<24}  {name}")
    per_dll = {}
    for name, gap in result['gaps'].items():
        entry = per_dll.setdefault(name.split('!', 1)[0], {'gaps': 0, 'exes': set(), 'status': {}})
        entry['gaps'] += 1
        entry['exes'].update(gap['exes'])
        entry['status'][gap['status']] = entry['status'].get(gap['status'], 0) + 1
    lines.append('')
    lines.append('GAPS   EXES   DLL                       BY STATUS')
    for dll, entry in sorted(per_dll.items(), key=lambda item: (-item[1]['gaps'], item[0])):
        detail = ', '.join(f'{k}={v}' for k, v in sorted(entry['status'].items()))
        lines.append(f"{entry['gaps']:5d}  {len(entry['exes']):5d}  {dll:<24}  {detail}")
    lines.append('')
    lines.append('Limitations:')
    lines.extend('  - ' + line for line in result['limitations'])
    return '\n'.join(lines) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('input', nargs='+', help='EXE/DLL file, directory, or a guest path such as C:\\WINDOWS')
    parser.add_argument('--tree', type=Path, default=ROOT / 'build/win98-c',
                        help='the extracted Win98 volume (default build/win98-c)')
    parser.add_argument('--json', dest='json_path', type=Path)
    parser.add_argument('--text', dest='text_path', type=Path)
    parser.add_argument('--recursive', dest='recursive', action='store_true', default=True,
                        help='walk directory arguments recursively (default)')
    parser.add_argument('--no-recursive', dest='recursive', action='store_false')
    parser.add_argument('--top', type=int, default=40, help='entries in the `top` list, 0 for all')
    parser.add_argument('--native-module', action='append', default=[],
                        help='force this DLL onto its native route, like Application::AddNativeModule')
    args = parser.parse_args(argv)

    tree_root = args.tree.expanduser().resolve()
    if not tree_root.is_dir():
        parser.error(f'The Win98 tree is not a directory: {tree_root}')
    forced = {name.upper() if name.upper().endswith('.DLL') else name.upper() + '.DLL'
              for name in args.native_module}
    result, exes, tree_name = report(args.input, tree_root, forced, args.top, args.recursive)
    text = text_report(result, exes, tree_name)
    if args.json_path:
        args.json_path.parent.mkdir(parents=True, exist_ok=True)
        args.json_path.write_text(json.dumps(result, indent=2, sort_keys=False) + '\n')
    if args.text_path:
        args.text_path.parent.mkdir(parents=True, exist_ok=True)
        args.text_path.write_text(text)
    if not args.json_path and not args.text_path:
        sys.stdout.write(text)
    else:
        print(f'{exes} EXEs, {len(result["modules"])} modules, {len(result["gaps"])} distinct gaps')
    return 0


if __name__ == '__main__':
    sys.exit(main())
