#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for the release data gate, using synthetic fixtures only."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_release_embedded import arrays, check, check_dependencies

class ReleaseEmbedded(unittest.TestCase):
    def test_integer_widths(self):
        self.assertEqual(arrays('inline constexpr unsigned short Words[2]={0x1234,255};')['Words'], b'\x34\x12\xff\0')

    def test_fail_closed_and_binary_match(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            fixture = bytes(range(32))
            header = root / 'fixture.h'
            header.write_text('static const unsigned char Fixture[]={' + ','.join(map(str, fixture)) + '};')
            manifest = root / 'fingerprints.json'
            manifest.write_text(json.dumps([dict(header='fixture.h', symbol='Fixture', bytes=32, sha256=hashlib.sha256(fixture).hexdigest())]))
            binary = root / 'host'; binary.write_bytes(b'HOST' + fixture)
            self.assertTrue(check(binary, root, manifest)[0]['full_match_in_host'])
            binary.write_bytes(b'HOST with no fixture')
            self.assertFalse(check(binary, root, manifest)[0]['full_match_in_host'])
            header.write_text('')
            with self.assertRaises(ValueError): check(binary, root, manifest)
            header.unlink()
            with self.assertRaises(FileNotFoundError): check(binary, root, manifest)

    def test_linked_dependency_records(self):
        from native_app import release_dependencies
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); cache = root / 'cache'; (cache / 'entries').mkdir(parents=True)
            entry = cache / 'entries/object.json'
            entry.write_text(json.dumps(dict(object_sha256='abc', dependencies={'src/windows_runtime_data.h': 'hash'})))
            release_dependencies([root / 'abc.o'], cache, root / 'host')
            self.assertEqual(json.loads((root / 'host.dependencies.json').read_text()), ['src/windows_runtime_data.h'])
            with self.assertRaises(RuntimeError): release_dependencies([root / 'missing.o'], cache, root / 'host')
            entry.write_text(json.dumps(dict(object_sha256='abc', dependencies={'build/generated/nls_tables.h': 'hash'})))
            with self.assertRaises(RuntimeError): release_dependencies([root / 'abc.o'], cache, root / 'host')

    def test_entries_of_this_build_win(self):
        # A fixture include can leave the object bytes unchanged. The entry of
        # the other build must not make the release check fail.
        from native_app import release_dependencies
        import native_build
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); cache = root / 'cache'; (cache / 'entries').mkdir(parents=True)
            release = cache / 'entries/release.json'
            release.write_text(json.dumps(dict(object_sha256='abc', dependencies={'src/windows_runtime_data.h': 'hash'})))
            (cache / 'entries/development.json').write_text(json.dumps(
                dict(object_sha256='abc', dependencies={'build/generated/gdi_palette_tables.h': 'hash'})))
            obj = root / 'abc.o'
            with self.assertRaises(RuntimeError): release_dependencies([obj], cache, root / 'host')
            native_build.SELECTED_ENTRIES[str(obj)] = {str(release)}
            try:
                release_dependencies([obj], cache, root / 'host')
            finally:
                native_build.SELECTED_ENTRIES.pop(str(obj), None)
            self.assertEqual(json.loads((root / 'host.dependencies.json').read_text()), ['src/windows_runtime_data.h'])

    def test_release_dependencies(self):
        self.assertEqual(len(check_dependencies(['build/generated/nls_tables.h', 'build/serialui-assets/templates.h'])), 2)
        self.assertEqual(check_dependencies(['src/windows_runtime_data.h']), [])

if __name__ == '__main__': unittest.main()
