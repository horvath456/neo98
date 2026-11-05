#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/check_release.py on small throwaway repositories."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_release  # noqa: E402


class ReleaseCheckTest(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        self.root = Path(self.work.name)
        self.git('init', '-q')
        (self.root / 'tools').mkdir()
        (self.root / 'tools/release_exclusions.json').write_text(
            json.dumps({'tools/private_compare.py': 'private'}))
        (self.root / 'neo98.py').write_text('print("Neo98")\n')
        (self.root / 'README.md').write_text('# Neo98\n\nSee [building](docs/BUILDING.md).\n')
        (self.root / 'docs').mkdir()
        (self.root / 'docs/BUILDING.md').write_text('Build it.\n')
        self.git('add', '-A')
        self.git('commit', '-q', '-m', 'base')

    def tearDown(self):
        self.work.cleanup()

    def git(self, *args):
        subprocess.run(['git', '-C', str(self.root), '-c', 'user.name=t', '-c', 'user.email=t@t',
                        *args], check=True, capture_output=True)

    def findings(self, staged=False, everything=True):
        return check_release.check(self.root, staged=staged, everything=everything)[1]

    def test_clean_tree_passes(self):
        self.assertEqual(self.findings(), [])
        self.assertEqual(self.findings(staged=True), [])

    def test_staged_content_is_checked_not_working_copy(self):
        (self.root / 'tools/sample.cpp').write_text('// Bug B' + '-1234 fixed here.\n')
        self.git('add', 'tools/sample.cpp')
        (self.root / 'tools/sample.cpp').write_text('// Clean now, but not staged.\n')
        found = self.findings(staged=True, everything=False)
        self.assertTrue(any('issue reference' in f for f in found), found)
        # The finding names the rule, never the matched text.
        self.assertFalse(any('1234' in f for f in found), found)

    def test_staged_only_checks_changes(self):
        (self.root / 'tools/new.py').write_text('x = 1\n')
        self.git('add', 'tools/new.py')
        names, _ = check_release.check(self.root, staged=True, everything=False)
        self.assertEqual(names, ['tools/new.py'])

    def test_private_paths_rejected(self):
        for name in ('BUG' + 'S.md', 'PLAN.md', 'tools/private_compare.py', 'tools/bench_x.py',
                     'tools/state_corpus/a.txt', 'docs/images/extra.png', '.github/notes.txt',
                     'workflow/serial.py'):
            with self.subTest(name=name):
                path = self.root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'text\n')
                self.git('add', name)
                self.assertTrue(self.findings(staged=True), name)
                self.git('rm', '-q', '--cached', name)
                path.unlink()

    def test_secret_shapes_and_names(self):
        for data in (b'-'.join([b'ABCDE'] * 5), bytes((65, 82, 77, 57, 56)),
                     b'/User' + b's/someone/'):
            with self.subTest(data=data):
                (self.root / 'tools/data.bin').write_bytes(b'\0' + data)
                self.git('add', 'tools/data.bin')
                found = self.findings(staged=True)
                self.assertTrue(any('forbidden pattern' in f for f in found), found)

    def test_symlink_and_imports(self):
        (self.root / 'tools/link').symlink_to('/etc/passwd')
        (self.root / 'tools/uses_private.py').write_text('import private_compare\n')
        self.git('add', 'tools/link', 'tools/uses_private.py')
        found = self.findings(staged=True)
        self.assertIn('tools/link: symbolic link', found)
        self.assertIn('tools/uses_private.py: imports an excluded tool', found)

    def test_broken_document_link(self):
        (self.root / 'docs/BUILDING.md').unlink()
        self.git('add', '-A')
        self.assertIn('README.md: missing document link docs/BUILDING.md', self.findings(staged=True))

    def test_german_prose(self):
        (self.root / 'tools/note.py').write_text('# Test f\u00fcr den Hook\n')
        self.git('add', 'tools/note.py')
        self.assertTrue(any('possible German text' in f for f in self.findings(staged=True)))


if __name__ == '__main__':
    unittest.main()
