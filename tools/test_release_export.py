#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('export_release', Path(__file__).with_name('export_release.py'))
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ExportTest(unittest.TestCase):
    def test_export_and_rejection(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work) / 'source'; root.mkdir()
            (root / 'tools').mkdir()
            (root / 'tools/release_exclusions.json').write_text('{}')
            (root / 'tools/state_corpus').mkdir()
            (root / 'tools/state_corpus/private.xz').write_bytes(b'private')
            (root / 'neo98.py').write_text('print("Neo98")')
            (root / 'docs' / 'bugs').mkdir(parents=True)
            (root / 'docs' / 'bugs' / 'private.txt').write_text('private evidence')
            git = ['git', '-C', str(root), '-c', 'user.name=t', '-c', 'user.email=t@t']
            subprocess.run(git[:3] + ['init', '-q'], check=True)
            subprocess.run(git + ['add', '-A'], check=True)
            (root / 'tools/untracked.d').write_text('build output, not tracked')
            target = Path(work) / 'export'
            release.export(root, target)
            self.assertTrue((target / 'neo98.py').exists())
            self.assertFalse((target / 'docs').exists())
            self.assertFalse((target / '.git').exists())
            self.assertFalse((target / 'tools/state_corpus').exists())
            self.assertFalse((target / 'tools/untracked.d').exists())
            with self.assertRaises(ValueError): release.export(root, target)
            for data in (bytes((65, 82, 77, 57, 56)), b'/User' + b's/name',
                         b'pico_' + b'itx', b'win95-' + b'phone', b'-'.join([b'ABCDE'] * 5)):
                (target / 'bad.bin').write_bytes(b'\0' + data)
                self.assertTrue(release.violations(target))
            (target / 'bad.bin').unlink()
            (root / 'tools/link').symlink_to('/etc/passwd')
            subprocess.run(git + ['add', 'tools/link'], check=True)
            with self.assertRaises(ValueError): release.export(root, Path(work) / 'bad-export')


    def test_comment_prose_damage(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            for name, prefix, suffix in (
                ('sample.cpp', '// ', ''), ('sample.h', '/*\n * ', '\n */'),
                ('sample.py', '# ', ''), ('sample.py', '\"\"\"', '\"\"\"'),
                ('sample.s', '# ', ''),
            ):
                for prose in ('. Removed reference.', ', The callback.',
                              ': An image.', 'Two,, entries.', 'The call () returns.',
                              'The address .', 'The value , then the next.'):
                    with self.subTest(name=name, prose=prose, prefix=prefix):
                        sample = root / name
                        sample.write_text(prefix + prose + suffix + '\n')
                        self.assertTrue(any('broken comment prose' in finding
                                            for finding in release.violations(root)))
                        sample.unlink()

    def test_comment_prose_ignores_code_and_literals(self):
        samples = {
            'sample.cpp': r'''const char *s = "// , Not a comment,, () .";
const char *raw = R"tag(/* , Not prose,, () . */)tag";
void run() { for (;;) {} } // Calls run() and operator().
#define EMPTY()
// A function pointer: void (*)().
// Literal examples: `a,,b`, ` () ` and "text .".
// ... more entries follow.
// The pointer is looked at. It points into, and then past, the block.
/* A block comment. */
''',
            'sample.py': r'''value = "# , Not a comment,, () ."
quotient = 8 // 2
empty = ()
def run():
    """Calls run() and preserves the tuple `()`.

    ```
    example,, () .
    ```
    """
    return "text ."
# Literal examples: "a,,b" and ` () `.
''',
        }
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            for name, text in samples.items():
                (root / name).write_text(text)
            self.assertEqual(release.violations(root), [])

    def test_provenance_patterns(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            sample = root / 'sample.cpp'
            for annotation in (
                'USER ' + '6:' + '7DA2', 'KERNEL32 at ' + 'BFF' + '8038D',
                '// ' + 'BFF' + '8038D', 'B-' + '1234',
                'matches the original ' + 'instruction sequence',
                'original ' + 'disassembly', '86' + 'Box comparison ' + 'log',
                'docs/' + 'bugs/report.md',
                '// GDI ' + '2:' + '0x1234', '// ' + '7FC' + 'B1234',
                '// Orig' + 'inal ' + 'xor ' + 'e' + 'ax,eax; retf 0x4',
                '// MSVCRT.DLL ' + '0x7800' + '1234',
                '// ' + '7800' + '1234',
                'ported from the ' + 'disassembly',
                '// USER.EXE, ' + '254A, 5DEA, 5E50',
                '// VMM' + '14:' + '202C..20E3',
                '// SY' + 'SE:' + '0298',
                '// DIBENG.DLL segment 3, offsets ' + '0D0A..0D39',
            ):
                with self.subTest(annotation=annotation):
                    sample.write_text(annotation)
                    self.assertTrue(release.violations(root))
            sample.write_text('constexpr auto error = 0x80004005; // HRESULT\n'
                              'constexpr auto flags = 0xffffffff; // flags\n'
                              '// 0x80000000 is the high bit\n'
                              '// flags: ' + 'BFF' + '8038D' + '\n'
                              '#define FEX_CALL 0x7f010001\n'
                              '// Maximum signed value: 0x7fffffff\n'
                              'value = index//6; mask = 0x7fffffff\n'
                              'unsigned value = 0xbff8038d;\n')
            self.assertEqual(release.violations(root), [])
            python = root / 'sample.py'
            python.write_text('"""USER ' + '6:' + '1234' + '"""\n')
            self.assertTrue(release.violations(root))
            python.write_text('address = 0x78001234\n')
            sample.write_text('// Native window-procedure token 0x71000030\n')
            self.assertEqual(release.violations(root), [])

    def test_doc_references_in_all_exported_files(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            target = 'docs/' + 'public.md'
            for name, content in (
                ('sample.cpp', '// See ' + target + '#section'),
                ('sample.py', '"""Read ' + target + '"""'),
                ('sample.json', '{"document": "' + target + '"}'),
                ('sample.bin', 'prefix\0' + target),
            ):
                with self.subTest(name=name):
                    sample = root / name
                    sample.write_text(content)
                    self.assertIn(name + ': missing document link ' + target,
                                  release.violations(root))
                    (root / 'docs').mkdir(exist_ok=True)
                    (root / target).write_text('Public documentation.')
                    self.assertEqual(release.violations(root), [])
                    (root / target).unlink()
                    sample.unlink()
            # A directory with the same name is not a document.
            (root / target).mkdir()
            (root / 'sample.cpp').write_text('// ' + target)
            self.assertTrue(release.violations(root))

    def test_language_vocabulary_and_names(self):
        from check_release_language import findings
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            for word in ('Zeit' + 'zone', 'na' + 'chgetragen', 'Netz' + 'werk',
                         'Druckaus' + 'gabe', 'Date' + 'i', 'Feh' + 'ler',
                         'Ord' + 'ner', 'Fens' + 'ter', 'ber' + 'eits', 'Trei' + 'ber'):
                (root / 'sample.py').write_text('# ' + word)
                self.assertEqual(len(findings(root, {})), 1, word)
            (root / 'sample.py').unlink()
            (root / ('Ord' + 'ner.txt')).write_text('English text')
            self.assertEqual(findings(root, {})[0]['line'], 0)

    def test_internal_analysis_rejected(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            sample = root / 'sample.py'
            for text in (
                'import ' + 'ida' + 'api',
                'from ' + 'id' + 'c import get_name',
                '# decomp' + 'iling Windows 98 binaries',
                '# Windows 98 binary decomp' + 'ilation',
                '# disassemb' + 'ling USER.EXE',
                '# DIBENG.DLL\n# disassemb' + 'ly output',
                '# Reverse enginee' + 'ring Windows binaries',
            ):
                with self.subTest(text=text):
                    sample.write_text(text)
                    self.assertTrue(release.violations(root))
            sample.write_text('# Compiler instruction decoder for JIT diagnostics\n')
            self.assertEqual(release.violations(root), [])
            java = root / 'Export.java'
            java.write_text('import ghid' + 'ra.app.script.Ghid' + 'raScript;')
            self.assertTrue(release.violations(root))
            java.unlink()
            java = root / ('Export' + 'Dibeng.java'); java.write_text('// script')
            self.assertTrue(release.violations(root))
            java.unlink()
            idc = root / 'analysis.idc'; idc.write_text('// script')
            self.assertTrue(release.violations(root))
            idc.unlink()
            named = root / ('ghid' + 'ra_export.py'); named.write_text('# script')
            self.assertTrue(release.violations(root))
            named.unlink()
            license_dir = root / 'LICENSES'; license_dir.mkdir()
            (license_dir / 'vendor.txt').write_text('Windows: no decomp' + 'ilation permitted.')
            self.assertEqual(release.violations(root), [])

    def test_release_cli_and_resource_dependencies(self):
        import ast
        import contextlib
        import io
        import json
        import sys
        checkout = Path(__file__).resolve().parents[1]
        sys.path.insert(0, str(checkout))
        import neo98
        from unittest.mock import patch
        with patch.object(neo98, 'ROOT', Path('/nonexistent-release-checkout')):
            with self.assertRaisesRegex(RuntimeError, 'unavailable in the public release'):
                neo98.winmine_assets()
            for command in ('import-winmine', 'import-dibeng', 'import-text', 'import-user'):
                with self.subTest(command=command), patch.object(sys, 'argv', ['neo98.py', command]), contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit) as error:
                        neo98.main()
                    self.assertEqual(error.exception.code, 2)
        text = (checkout / 'neo98.py').read_text()
        self.assertNotIn('ghid' + 'ra', text.lower())
        excluded = json.loads((checkout / 'tools/release_exclusions.json').read_text())
        for name in ('import_dibeng.py', 'import_text.py', 'import_winmine.py',
                     'api_port_evidence.json', 'api_inventory.py', 'stub_inventory.py',
                     'wine_assembly_inventory.py', 'flat_validation_bridge.cpp'):
            self.assertIn('tools/' + name, excluded)
        for name in ('native_app.py', 'native_build.py', 'setup_win98.py', 'mac_app.py',
                     'import_registry.py', 'import_start_menu.py', 'import_user_resources.py',
                     'serialui_assets.py', 'extract_font_charset.py', 'extract_font_charset_info.py'):
            self.assertNotIn('tools/' + name, excluded)
        for name in ('import_user_resources.py', 'serialui_assets.py'):
            tree = ast.parse((checkout / 'tools' / name).read_text())
            for node in ast.walk(tree):
                if isinstance(node, ast.ImportFrom) and node.module:
                    self.assertNotIn('tools/' + node.module + '.py', excluded)

    def test_resource_only_segment_reader(self):
        import hashlib
        import struct
        import sys
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from import_user_resources import strip_table, STRIP_SEGMENT, STRIP_OFFSET, STRIP_ENTRIES
        data = bytearray(16384)
        base, table, sector, shift = 64, 64, 512, 4
        struct.pack_into('<I', data, 60, base)
        struct.pack_into('<H', data, base + 0x22, table)
        struct.pack_into('<H', data, base + 0x32, shift)
        struct.pack_into('<HH', data, base + table + (STRIP_SEGMENT - 1) * 8, sector, 4096)
        entries = [(index, index + 1) for index in range(STRIP_ENTRIES)]
        raw = b''.join(struct.pack('<HH', *entry) for entry in entries)
        start = (sector << shift) + STRIP_OFFSET
        data[start:start + len(raw)] = raw
        self.assertEqual(strip_table(bytes(data)), (entries, hashlib.sha256(raw).hexdigest()))

    def test_dll_route_behavior_preserved(self):
        import sys
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from dll_routes import read_table, route
        table = read_table()
        self.assertEqual(route('USER32.DLL'), 'native')
        self.assertEqual(route('GDI32.DLL'), 'native')
        self.assertEqual(route('SHELL32.DLL'), 'original')
        self.assertEqual(route('SERIALUI.DLL'), 'native16')
        self.assertTrue(all(row['reason'] for row in table.values()))

    def test_bundle_privacy(self):
        import sys
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from check_release_bundle import check
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            (root / 'library').write_bytes(b'assertion at /Use' + b'rs/private/source.c')
            self.assertTrue(check(root))
            (root / 'library').write_bytes(b'assertion at ./source.c; special://home/addons/skin')
            self.assertEqual(check(root), [])

    def test_binary_and_link_guard(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            (root / 'README.md').write_text('[Missing](missing.md)')
            self.assertTrue(release.violations(root))
            (root / 'README.md').write_text('[Local](target.md)')
            (root / 'target.md').write_text('Local documentation.')
            self.assertEqual(release.violations(root), [])
            (root / 'snapshot.xz').write_bytes(b'compressed state')
            self.assertTrue(release.violations(root))

    def test_language_exceptions_are_line_specific(self):
        import hashlib
        import sys
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from check_release_language import findings
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            sample = root / 'guest_test.py'
            guest = 'expected = "Gr\u00fc\u00dfe"'
            sample.write_text(guest + '\n')
            self.assertEqual(len(findings(root, {})), 1)
            allowed = {'guest_test.py': {hashlib.sha256(guest.encode()).hexdigest(): 'Guest text'}}
            self.assertEqual(findings(root, allowed), [])
            sample.write_text(guest + '\n# ' + 'ni' + 'cht' + ' translated\n')
            self.assertEqual(len(findings(root, allowed)), 1)


if __name__ == '__main__': unittest.main()
