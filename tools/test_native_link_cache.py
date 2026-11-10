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

    def build(self, environment=None, **kwargs):
        with native_app.cached_runner(self.binary, [], [], 'main.cpp', [], [],
                                      {**os.environ, **(environment or {})},
                                      self.root / 'cache', **kwargs) as temporary:
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

    def entries(self):
        return len(list((self.root / 'cache/entries').glob('*.json')))

    def test_session_variables_keep_the_build(self):
        # What a headless suite worker changes from run to run: its display,
        # its session bus and the folders of its private machine.
        first = {'DISPLAY': ':101', 'DBUS_SESSION_BUS_ADDRESS': 'unix:path=/run/one',
                 'NEO98_WIN98_ROOT': '/machines/one', 'NEO98_SETTINGS_DIR': '/settings/one',
                 'NEO98_LOG_DIR': '/logs/one', 'PWD': '/somewhere'}
        second = {'DISPLAY': ':102', 'DBUS_SESSION_BUS_ADDRESS': 'unix:path=/run/two',
                  'NEO98_WIN98_ROOT': '/machines/two', 'NEO98_SETTINGS_DIR': '/settings/two',
                  'NEO98_LOG_DIR': '/logs/two', 'PWD': '/elsewhere', 'XAUTHORITY': '/run/two.auth'}
        self.assertTrue(self.build(first))
        self.assertEqual(self.entries(), 1)
        self.assertFalse(self.build(second))
        self.assertEqual(self.entries(), 1)

    def test_compiler_variables_rebuild(self):
        # A header folder the compiler reads from its environment decides
        # what an include finds, so it belongs to the identity.
        headers = self.root / 'other'
        headers.mkdir()
        self.assertTrue(self.build())
        self.assertTrue(self.build({'CPATH': str(headers)}))
        self.assertEqual(self.entries(), 2)
        self.assertFalse(self.build({'CPATH': str(headers)}))
        self.assertTrue(self.build({'CPATH': str(headers), 'MACOSX_DEPLOYMENT_TARGET': '13.0'}))

    def test_missing_sidecar_and_damaged_binary(self):
        self.assertTrue(self.build())
        self.assertTrue(self.build(sidecars=('.layout',)))
        self.binary.write_bytes(b'broken')
        self.assertTrue(self.build())
        self.assertFalse(self.build())

    def test_layout_guard_change_invalidates_link(self):
        guard = self.root / 'tools/state_layout_elf.py'
        guard.write_text('# original guard\n')
        self.assertTrue(self.build())
        self.assertFalse(self.build())
        guard.write_text('# revised guard\n')
        self.assertTrue(self.build())

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
