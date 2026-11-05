# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
import json
from pathlib import Path
import tempfile
import unittest
from compare_marlett_pixels import CELLS, compare, fnv
from wine_conformance import digest


class MarlettComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def fixture(self, name, changed=False, missing=False, font=b'font', height=13):
        folder = self.root / name
        folder.mkdir()
        lines = [f'test.c:1: MARLETT_FONT size=13 face=Marlett height={height} ascent={height} descent=0',
                 f'test.c:2: MARLETT_FILE bytes={len(font)} fnv={fnv(font):08x}']
        for code in CELLS[:-1] if missing else CELLS:
            rows = [0] * 32
            if changed and code == 'f':
                rows[0] = 1
            lines.append(f'test.c:3: MARLETT_PIXELS size=13 code={code} rows=' + ''.join(f'{r:08x} ' for r in rows))
        log = folder / 'TEST.LOG'
        log.write_text('\n'.join(lines) + '\n')
        (folder / 'manifest.json').write_text(json.dumps({'groups': [{'name': 'marlett_pixels', 'sha256': 'same-exe'}]}))
        (folder / 'results.json').write_text(json.dumps({'groups': {'marlett_pixels': {
            'status': 'PASS', 'processes': [{'log': log.name, 'sha256': digest(log)}]}}}))
        return folder

    def test_matching_matrix(self):
        self.assertEqual(compare(self.fixture('ref'), [13], self.fixture('arm'))['status'], 'PASS')

    def test_pixel_difference_despite_all_api_assertions_passing(self):
        report = compare(self.fixture('ref'), [13], self.fixture('arm', changed=True))
        self.assertEqual(report['status'], 'FAIL')
        self.assertEqual(report['checks']['13/f']['status'], 'FAIL')

    def test_missing_glyph_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'incomplete glyph matrix'):
            compare(self.fixture('ref'), [13], self.fixture('arm', missing=True))

    def test_metric_difference_is_reported_even_with_matching_pixels(self):
        report = compare(self.fixture('ref'), [13], self.fixture('arm', height=14))
        self.assertEqual(report['status'], 'FAIL')
        self.assertEqual(report['metrics']['13']['status'], 'FAIL')
        self.assertTrue(all(c['status'] == 'PASS' for c in report['checks'].values()))

    def test_different_font_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'different EXE or font bytes'):
            compare(self.fixture('ref'), [13], self.fixture('arm', font=b'other font'))

    def test_modified_log_is_rejected(self):
        candidate = self.fixture('arm')
        (candidate / 'TEST.LOG').write_text('changed')
        with self.assertRaisesRegex(ValueError, 'changed log'):
            compare(self.fixture('ref'), [13], candidate)
