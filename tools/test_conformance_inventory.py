# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
import json
from pathlib import Path
import tempfile
import unittest

from conformance_inventory import inventory


class InventoryTests(unittest.TestCase):
    def test_stale_hash_skips_and_invalid_harness_do_not_complete_coverage(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for source in ('wine', 'reactos', 'sdl12'):
                folder = root/'catalog'/source
                folder.mkdir(parents=True)
                groups = [{'name': 'case', 'sha256': 'new', 'build_status': 'BUILT'}] if source == 'wine' else []
                (folder/'catalog.json').write_text(json.dumps({'source_pin': {}, 'groups': groups}))
            def result(folder, runtime, sha, status):
                folder.mkdir(parents=True)
                (folder/'manifest.json').write_text(json.dumps({'groups': [{'name': 'case', 'sha256': sha}]}))
                (folder/'results.json').write_text(json.dumps({'runtime': runtime, 'suite_done': True,
                    'groups': {'case': {'status': status}}}))
            result(root/'runs/old/reference', '86box-win98', 'old', 'PASS')
            result(root/'runs/invalid/reference', '86box-win98', 'new', 'PASS')
            (root/'runs/invalid/invalid-harness.json').write_text('{"reason":"bad harness"}')
            report = inventory(root/'catalog', root/'runs', None)
            self.assertEqual(report['without_reference_attempt'], ['case'])
            result(root/'runs/current/reference', '86box-win98', 'new', 'FAIL')
            result(root/'runs/current/neo98', 'neo98', 'new', 'SKIP')
            report = inventory(root/'catalog', root/'runs', None)
            self.assertEqual(report['without_reference_attempt'], [])
            self.assertEqual(report['completed_reference_without_neo98_attempt'], ['case'])


if __name__ == '__main__':
    unittest.main()
