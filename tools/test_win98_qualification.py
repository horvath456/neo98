# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
import json
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path
from types import SimpleNamespace
from win98_qualification import contract_batches, fixture_arguments
from compare_probe_traces import check_trace_contracts
from wine_conformance import check_baseline


class ContractBatchTests(unittest.TestCase):
    def test_sequence_contract_requires_pinned_reference_and_propagates_failure(self):
        from wine_conformance import digest
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            for filename in ('results.json', 'manifest.json'):
                (root / filename).write_text('{}')
            baseline = root / 'baseline.json'
            baseline.write_text(json.dumps({'comparison': 'message_sequences',
                'reference_results_sha256': digest(root / 'results.json')}))
            args = SimpleNamespace(baseline=baseline, candidate=root, report=root / 'report.json')
            with patch('compare_message_sequences.compare') as compare:
                with self.assertRaises(SystemExit):
                    check_baseline(args)
                compare.assert_not_called()
            args.reference = root
            with patch('compare_message_sequences.compare', return_value={'status': 'FAIL'}):
                with self.assertRaises(SystemExit):
                    check_baseline(args)
            with patch('compare_message_sequences.compare', return_value={'status': 'PASS'}):
                check_baseline(args)
            self.assertEqual(json.loads(args.report.read_text())['status'], 'PASS')
            data = json.loads(baseline.read_text())
            data['assertion_contract'] = True
            baseline.write_text(json.dumps(data))
            with patch('wine_conformance.evaluate_baseline', return_value={
                    'status': 'FAIL', 'groups': {}, 'improvements': []}), \
                 patch('compare_message_sequences.compare', return_value={'status': 'PASS'}):
                with self.assertRaises(SystemExit):
                    check_baseline(args)
            self.assertEqual(json.loads(args.report.read_text())['status'], 'FAIL')

    def test_standalone_check_requires_pixel_reference(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            baseline = root / 'baseline.json'
            baseline.write_text(json.dumps({'trace_checks': [{'group': 'image'}]}))
            for filename in ('results.json', 'manifest.json'):
                (root / filename).write_text('{}')
            report = root / 'report.json'
            with patch('wine_conformance.evaluate_baseline', return_value={
                    'status': 'PASS', 'groups': {}, 'improvements': []}):
                with self.assertRaises(SystemExit):
                    check_baseline(SimpleNamespace(baseline=baseline, candidate=root, report=report))
            self.assertEqual(json.loads(report.read_text())['status'], 'FAIL')

    def test_pixel_failure_and_incomplete_trace_are_not_api_passes(self):
        baseline = {'groups': {'image': {}}, 'trace_checks': [
            {'group': 'image', 'prefix': 'PIXELS', 'count': 2}]}
        with patch('compare_probe_traces.compare', return_value={'status': 'FAIL'}) as compare:
            self.assertEqual(check_trace_contracts(baseline, Path('ref'), Path('actual')), [{'status': 'FAIL'}])
            compare.assert_called_once_with(Path('ref'), Path('actual'), 'image', 'PIXELS', 2)
        with patch('compare_probe_traces.compare', side_effect=ValueError('incomplete probe traces')):
            self.assertEqual(check_trace_contracts(baseline, Path('ref'), Path('actual'))[0]['status'], 'CHECK_ERROR')
        baseline['groups'] = {}
        with patch('compare_probe_traces.compare') as compare:
            self.assertEqual(check_trace_contracts(baseline, Path('ref'), Path('actual'))[0]['status'], 'CHECK_ERROR')
            compare.assert_not_called()

    def test_reference_environment_requirements(self):
        self.assertEqual(fixture_arguments(['kernel32_path', 'kernel32_drive']), ['--fixture-cdrom'])
        self.assertEqual(fixture_arguments(['riched20_editor']), ['--reference-dll', 'RICHED20.DLL'])
        self.assertEqual(fixture_arguments(['gdi32_path']), [])

    def test_shared_reference_keeps_all_contracts_and_groups(self):
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            for filename, groups in [('first', ['a', 'b']), ('second', ['b', 'c'])]:
                (folder / (filename + '.json')).write_text(json.dumps({
                    'reference_results_sha256': 'same', 'groups': dict.fromkeys(groups, {})}))
            (folder / 'review.json').write_text('{"differences": []}')
            batches = contract_batches(folder)
            self.assertEqual(batches['same']['groups'], {'a', 'b', 'c'})
            self.assertEqual(len(batches['same']['contracts']), 2)
            self.assertEqual(contract_batches(folder, ['second'])['same']['groups'], {'b', 'c'})
            with self.assertRaises(ValueError):
                contract_batches(folder, ['missing'])
            with self.assertRaises(ValueError):
                contract_batches(folder, ['review'])


if __name__ == '__main__':
    unittest.main()
