#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A later cache manifest must not hide objects linked into a frozen host."""
import json
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import state_layout


class FrozenHostObjects(unittest.TestCase):
    def test_invalid_binary_or_object_cannot_supply_an_empty_layout(self):
        with tempfile.TemporaryDirectory() as folder:
            invalid = Path(folder) / 'invalid.o'
            invalid.write_bytes(b'not a Mach-O object\n')
            for reader in (state_layout.source_objects, state_layout.symbols, state_layout.parse_dwarf):
                with self.subTest(reader=reader.__name__):
                    with self.assertRaises(subprocess.CalledProcessError):
                        reader(invalid)

    def test_old_host_uses_its_debug_map_after_an_incremental_rebuild(self):
        with tempfile.TemporaryDirectory(prefix='state layout ') as folder:
            root = Path(folder)
            source = root / 'src/registry.cpp'
            source.parent.mkdir()
            source.touch()
            cache = root / 'build/native-app/object-cache-fast'
            objects = cache / 'objects'
            objects.mkdir(parents=True)
            old, new = objects / 'old.o', objects / 'new.o'
            old.touch(); new.touch()
            entries = cache / 'entries'
            entries.mkdir()
            (entries / 'registry.json').write_text(json.dumps({
                'source': str(source), 'object_sha256': 'new'}))
            debug_map = (f'0000000000000000 - 00 0000 SO {source.parent}/\n'
                         f'0000000000000000 - 00 0000 SO {source.name}\n'
                         f'0000000000000000 - 00 0001 OSO {old}\n')
            with patch.object(state_layout, 'ROOT', root), \
                    patch.object(state_layout.subprocess, 'run', return_value=SimpleNamespace(stdout=debug_map)):
                self.assertEqual(state_layout.source_objects(root / 'frozen-host'), {source: old})
                old.unlink()
                with self.assertRaisesRegex(SystemExit, 'linked object is missing'):
                    state_layout.source_objects(root / 'frozen-host')


if __name__ == '__main__':
    unittest.main()
