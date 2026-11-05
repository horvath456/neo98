# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
import json
from pathlib import Path
import tempfile
import unittest
from compare_oem_bitmaps import compare, read_run
from wine_conformance import digest


class BitmapComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def fixture(self, name, changed=None, omit=None, binary='same-exe'):
        folder = self.root / name
        folder.mkdir()
        lines = []
        for bitmap in range(0x7fde, 0x8000):
            for variant in range(3):
                key = (bitmap, variant)
                if key == omit:
                    continue
                value = 'absent error=50' if key == changed else 'size=13,13 stride=2 planes=1 bpp=1 hash=2516f91e'
                lines.append(f'oem_bitmaps.c:20: OEM id={bitmap:04x} variant={variant} {value}')
        log = folder / 'TEST.EXE.1.LOG'
        log.write_text('\n'.join(lines) + '\n')
        (folder / 'manifest.json').write_text(json.dumps({'groups': [{'name': 'oem_bitmaps', 'sha256': binary}]}))
        (folder / 'results.json').write_text(json.dumps({'groups': {'oem_bitmaps': {
            'status': 'PASS', 'processes': [{'log': log.name, 'sha256': digest(log)}]}}}))
        return folder

    def test_all_variants_match(self):
        report = compare(self.fixture('reference'), self.fixture('candidate'))
        self.assertEqual(report['status'], 'PASS')
        self.assertEqual(len(report['checks']), 102)

    def test_missing_bitmap_is_a_failure_even_when_probe_assertions_pass(self):
        report = compare(self.fixture('reference'), self.fixture('candidate', changed=(0x7fe3, 0)))
        self.assertEqual(report['status'], 'FAIL')
        self.assertEqual(report['checks']['7fe3/0']['status'], 'FAIL')

    def test_truncated_case_set_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'missing or unexpected'):
            read_run(self.fixture('truncated', omit=(0x7fe3, 0)))

    def test_different_executables_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'different bitmap EXEs'):
            compare(self.fixture('reference'), self.fixture('candidate', binary='changed-exe'))

    def test_changed_log_is_rejected(self):
        folder = self.fixture('changed')
        with (folder / 'TEST.EXE.1.LOG').open('a') as log:
            log.write('oem_bitmaps.c:20: OEM id=7fe3 variant=0 absent error=50\n')
        with self.assertRaisesRegex(ValueError, 'log hash changed'):
            read_run(folder)


if __name__ == '__main__':
    unittest.main()
