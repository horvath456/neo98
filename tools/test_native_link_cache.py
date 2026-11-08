#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Exercise the real compiler/link cache with a small runner."""
from concurrent.futures import ThreadPoolExecutor
import os
import shutil
from pathlib import Path
import tempfile
import struct
import unittest
from unittest.mock import patch

import native_app


class LinkCacheTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        (self.root / 'tools').mkdir()
        shutil.copyfile(native_app.ROOT / 'tools/arm64_hypervisor.entitlements',
                        self.root / 'tools/arm64_hypervisor.entitlements')
        (self.root / 'src/state_rules').mkdir(parents=True)
        self.header = self.root / 'tools/value.h'
        self.header.write_text('#define VALUE 1\n')
        (self.root / 'tools/main.cpp').write_text('#include "value.h"\nint main() { return VALUE; }\n')
        self.binary = self.root / 'runner'
        self.patch = patch.object(native_app, 'ROOT', self.root)
        self.patch.start()
        self.addCleanup(self.patch.stop)

    def build(self, **kwargs):
        with native_app.cached_runner(self.binary, [], [], 'main.cpp', [], [],
                                      dict(os.environ), self.root / 'cache', **kwargs) as temporary:
            if temporary is not None:
                os.replace(temporary, self.binary)
            return temporary is not None

    def test_reuse_and_header_change(self):
        self.assertTrue(self.build())
        before = self.binary.stat().st_mtime_ns
        self.assertFalse(self.build())
        self.assertEqual(before, self.binary.stat().st_mtime_ns)
        self.header.write_text('#define VALUE 2\n')
        self.assertTrue(self.build())
        self.assertNotEqual(before, self.binary.stat().st_mtime_ns)

    def test_missing_sidecar_and_damaged_binary(self):
        self.assertTrue(self.build())
        self.assertTrue(self.build(sidecars=('.layout',)))
        self.binary.write_bytes(b'broken')
        self.assertTrue(self.build())
        self.assertFalse(self.build())

    def test_tool_path_is_idempotent(self):
        with patch.dict(os.environ, {'PATH': '/usr/bin:/bin'}), patch('shutil.which', return_value='/usr/bin/tool'):
            first = native_app.neo98.tool_environment()['PATH']
            os.environ['PATH'] = first
            self.assertEqual(first, native_app.neo98.tool_environment()['PATH'])

    def test_layout_retained_by_image_identity(self):
        layout = self.root / 'runner.layout'
        identity = bytes(range(16))
        contents = struct.pack('<8sIIIII', b'A98LAY04', 0, 0, 0, 0, 0) + identity + bytes(4)
        layout.write_bytes(contents)
        native_app.retain_layout(layout)
        layout.unlink()
        retained = self.root / '.state-layouts' / (identity.hex() + '.layout')
        self.assertEqual(retained.read_bytes(), contents)

    def test_concurrent_builds_publish_once(self):
        with ThreadPoolExecutor(max_workers=2) as pool:
            rebuilt = list(pool.map(lambda _: self.build(), range(2)))
        self.assertEqual(sorted(rebuilt), [False, True])


if __name__ == '__main__':
    unittest.main()
