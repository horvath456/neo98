# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
import unittest
import json
import tempfile
from pathlib import Path
from unittest.mock import patch
from wine_conformance import digest

from compare_message_sequences import compare_cases, parse_sequences, qualify_contract
from conformance_campaign import instrument_message_sequences


def log(checks='', context='show', site='msg.c:42'):
    return f'NEO98_SEQUENCE_BEGIN {site} {context}\n{checks}NEO98_SEQUENCE_END {site} {context}\n'


class SequenceTests(unittest.TestCase):
    def test_composite_contract_keeps_nonsequence_counts_and_pins_review(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            (root/'results.json').write_text('{}')
            proof = root/'probe.json'
            proof.write_text('{"status":"PASS"}')
            review = root/'review.json'
            reviewed = {'group': 'user32_msg', 'binary_sha256': 'exe',
                'reference_results_sha256': digest(root/'results.json'), 'site': 'msg.c:9',
                'reference': {'PASS': 1}, 'reason': 'Native dangling pointer, demonstrated by paired probe',
                'probe': str(proof), 'probe_sha256': digest(proof)}
            review.write_text(json.dumps(reviewed))
            def baseline():
                return {'reference_results_sha256': digest(root/'results.json'), 'groups': {'user32_msg': {
                    'sha256': 'exe', 'required': {'msg.c:42': {'PASS': 8}, 'msg.c:9': {'PASS': 1},
                    'msg.c:7059': {'PASS': 31}}, 'advisory': {}}}}
            with patch('compare_message_sequences.read_sequences', return_value=(
                    parse_sequences(log()), {}, {'runtime': '86box-win98'})):
                result = qualify_contract(baseline(), root, review)
                self.assertEqual(result['groups']['user32_msg']['required'], {'msg.c:7059': {'PASS': 31}})
                self.assertEqual(result['sequence_invocations'], 1)
                proof.write_text('changed')
                with self.assertRaises(ValueError):
                    qualify_contract(baseline(), root, review)

    def test_optional_assertion_counts(self):
        a = parse_sequences(log('NEO98_CHECK msg.c:42 PASS\n'*8))
        b = parse_sequences(log('NEO98_CHECK msg.c:42 PASS\n'*2))
        self.assertEqual(compare_cases(a, b)['status'], 'PASS')

    def test_empty_sequence_is_executed(self):
        self.assertEqual(parse_sequences(log())[0]['status'], 'PASS')

    def test_repeated_missing_invocation(self):
        report = compare_cases(parse_sequences(log()+log()), parse_sequences(log()))
        self.assertEqual([x['status'] for x in report['checks']], ['PASS', 'MISSING'])
        self.assertEqual(report['status'], 'FAIL')

    def test_failure_not_hidden_by_more_passes(self):
        report = compare_cases(parse_sequences(log('NEO98_CHECK msg.c:42 PASS\n')),
                               parse_sequences(log('NEO98_CHECK msg.c:42 PASS\n'*20+'NEO98_CHECK msg.c:42 FAIL\n')))
        self.assertEqual(report['status'], 'FAIL')

    def test_native_failure_and_improvement(self):
        reference = parse_sequences(log('NEO98_CHECK msg.c:42 FAIL\n'))
        self.assertEqual(compare_cases(reference, reference)['checks'][0]['status'], 'REFERENCE_FAIL')
        self.assertEqual(compare_cases(reference, parse_sequences(log()))['checks'][0]['status'], 'IMPROVEMENT')

    def test_malformed_or_incomplete(self):
        for text in ('', 'NEO98_SEQUENCE_BEGIN msg.c:42 show\n',
                     'NEO98_SEQUENCE_END msg.c:42 show\n',
                     'NEO98_SEQUENCE_BEGIN msg.c:42 show\n'+log(),
                     log().replace('END msg.c:42 show', 'END msg.c:42 hide'),
                     log('NEO98_CHECK other.c:1 PASS\n'), 'NEO98_SEQUENCE_BOGUS\n'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                parse_sequences(text)

    def test_same_site_different_context(self):
        report = compare_cases(parse_sequences(log(context='show')), parse_sequences(log(context='hide')))
        self.assertEqual(report['checks'][0]['status'], 'MISSING')
        self.assertEqual(len(report['additional_sequences']), 1)

    def test_instrumentation_preserves_lines_and_early_return(self):
        source = ('static void ok_sequence_(void)\n{\n'
                  '    static const struct message end_of_sequence = {0};\n'
                  '    if (bad) { flush_sequence(); return; }\n'
                  '    flush_sequence();\n}\n')
        result = instrument_message_sequences(source)
        self.assertEqual(result.count('\n'), source.count('\n'))
        self.assertEqual(result.count('NEO98_SEQUENCE_BEGIN'), 1)
        self.assertEqual(result.count('NEO98_SEQUENCE_END'), 2)
        with self.assertRaises(RuntimeError):
            instrument_message_sequences(source.replace('flush_sequence(); return;', 'return;'))


if __name__ == '__main__':
    unittest.main()
