#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Failed user installations retain useful logs after cleanup."""
import contextlib
import io
import os
import signal
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import setup_win98 as setup


class SetupFailuresTest(unittest.TestCase):
    def test_failure_preserves_logs_and_reports_tail(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            work = root / 'work/run'
            work.mkdir(parents=True)
            qemu = work / 'qemu.log'
            qemu.write_text('\n'.join(f'diagnostic {i}' for i in range(50)) + '\njit buffer: Invalid argument\n')
            (work / 'serial.log').write_text('guest output')
            output = io.StringIO()
            with patch.dict(os.environ, XDG_STATE_HOME=str(root / 'state')), \
                 patch.object(sys, 'platform', 'linux'), \
                 patch.object(setup, 'setup', side_effect=RuntimeError(f'Setup stopped; see {qemu}')), \
                 patch.object(setup.win98_c, 'ready', return_value=False), \
                 patch.object(signal, 'signal'), \
                 contextlib.redirect_stdout(output):
                result = setup.main(['--user', '--target', str(root / 'target'), '--work', str(work.parent)])
            self.assertEqual(result, 1)
            self.assertFalse(work.exists())
            saved = list((root / 'state/neo98/logs').glob('*/qemu.log'))
            self.assertEqual(len(saved), 1)
            self.assertTrue(saved[0].with_name('serial.log').is_file())
            message = output.getvalue()
            self.assertIn(str(saved[0]), message)
            self.assertNotIn(str(qemu), message)
            self.assertNotIn('diagnostic 10\n', message)
            self.assertIn('diagnostic 11\n', message)
            self.assertIn('jit buffer: Invalid argument', message.splitlines()[-1])
            self.assertIn(str(saved[0]), message.splitlines()[-1])

    def test_macos_log_location_and_unique_runs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            work = root / 'work'
            work.mkdir()
            (work / 'qemu.log').write_text('failure')
            with patch.object(sys, 'platform', 'darwin'), patch.object(Path, 'home', return_value=root):
                first = setup.preserve_failure_logs(work)
                second = setup.preserve_failure_logs(work)
            self.assertNotEqual(first, second)
            self.assertEqual(next(iter(first.values())).parent.parent, root / 'Library/Logs/Neo98')

    def test_copy_failure_keeps_original_logs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            work = root / 'work/run'
            work.mkdir(parents=True)
            log = work / 'qemu.log'
            log.write_text('failure')
            output = io.StringIO()
            with patch.object(setup, 'setup', side_effect=RuntimeError(f'Setup stopped; see {log}')), \
                 patch.object(setup.win98_c, 'ready', return_value=False), \
                 patch.object(setup, 'preserve_failure_logs', side_effect=OSError('full disk')), \
                 contextlib.redirect_stdout(output):
                result = setup.main(['--user', '--target', str(root / 'target'), '--work', str(work.parent)])
            self.assertEqual(result, 1)
            self.assertTrue(log.is_file())
            self.assertIn('Logs kept at ' + str(work), output.getvalue())


if __name__ == '__main__':
    unittest.main()
