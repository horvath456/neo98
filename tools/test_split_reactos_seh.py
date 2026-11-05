# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
import unittest
from split_reactos_seh import select_entry


class SEHSelectionTests(unittest.TestCase):
    def test_ms_cases_preserve_guards_assertions_and_lines(self):
        body = '\n'.join('run_test(seh%04d);' % n for n in range(1, 59))
        source = 'START_TEST(ms_seh)\n{\n#ifdef _USE_NATIVE_SEH\n'+body+'\n#endif\n}\n'
        cases, result = select_entry(source, 'ms_seh')
        self.assertEqual(len(cases), 58)
        self.assertEqual(source.count('\n'), result.count('\n'))
        self.assertIn('#ifdef _USE_NATIVE_SEH', result)
        self.assertEqual(result.count('run_test('), 58)
        self.assertIn('if (argc < 3 || !strcmp(argv[2], "seh0058")) run_test(seh0058);', result)

    def test_pseh_keeps_array_and_exact_assertion(self):
        entries = '\n'.join('USE_TEST(test_case_%d),' % n for n in range(104))
        assertion = 'ok(call_test(testsuite[i].func), "%s failed\\n", testsuite[i].name);'
        source = 'START_TEST(pseh)\n{\n'+entries+'\n'+assertion+'\n}\n'
        cases, result = select_entry(source, 'pseh')
        self.assertEqual(len(cases), 104)
        self.assertIn(entries, result)
        self.assertIn(assertion, result)
        self.assertEqual(source.count('\n'), result.count('\n'))

    def test_unknown_or_changed_dispatch_is_rejected(self):
        for name in ('other', 'ms_seh', 'pseh'):
            with self.assertRaises(ValueError):
                select_entry('START_TEST('+name+') { dangerous(); }', name)


if __name__ == '__main__':
    unittest.main()
