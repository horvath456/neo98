# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
import unittest

from external_conformance import combine_test_entries


class MultipleEntryTests(unittest.TestCase):
    def test_keeps_assertion_lines_and_runs_both_original_entries(self):
        source = 'START_TEST(apiA)\n{\n    ok(1, "ANSI");\n}\nSTART_TEST(apiW)\n{\n    ok(0, "Unicode");\n}\n'
        result = combine_test_entries(source, ['apiA', 'apiW'], 'api')
        for assertion in ('    ok(1, "ANSI");', '    ok(0, "Unicode");'):
            self.assertEqual(source.splitlines().index(assertion), result.splitlines().index(assertion))
            self.assertEqual(result.count(assertion), 1)
        self.assertIn('START_TEST(api) { func_apiA(); func_apiW(); }', result)
        self.assertEqual(result.count('START_TEST('), 1)

    def test_rejects_missing_reordered_or_duplicate_entries(self):
        for source in ('START_TEST(apiA) {}',
                       'START_TEST(apiW) {} START_TEST(apiA) {}',
                       'START_TEST(apiA) {} START_TEST(apiA) {}'):
            with self.assertRaises(RuntimeError):
                combine_test_entries(source, ['apiA', 'apiW'], 'api')


if __name__ == '__main__':
    unittest.main()
