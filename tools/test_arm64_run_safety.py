#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""exercise process control with mocks, never crash a real host."""
import contextlib
import subprocess
import unittest
from unittest.mock import Mock, patch
import arm64_run

class ProcessSafety(unittest.TestCase):
    def test_existing_host_is_not_started_or_signalled(self):
        with patch.object(arm64_run.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, '123\n', '')), \
             patch.object(arm64_run.subprocess, 'Popen') as spawn:
            with self.assertRaisesRegex(RuntimeError, 'another neo98-host is active: 123'):
                arm64_run.run(['unused'], 1)
            spawn.assert_not_called()

    def check_run(self, responses, stuck=False):
        process = Mock(returncode=0, pid=123)
        process.communicate.side_effect = responses
        with patch.object(arm64_run, 'ensure_idle'), patch.object(arm64_run, 'sync'), \
             patch.object(arm64_run, 'lock', return_value=contextlib.nullcontext()), \
             patch.object(arm64_run.subprocess, 'Popen', return_value=process):
            if stuck:
                with self.assertRaises(arm64_run.Stuck): arm64_run.run(['unused'], 1)
                result = None
            else:
                result = arm64_run.run(['unused'], 1)
        process.kill.assert_not_called()
        return process, result

    def test_normal_exit_needs_no_signal(self):
        process, result = self.check_run([('done', '')])
        process.terminate.assert_not_called()
        self.assertFalse(result.timed_out)

    def test_timeout_requests_orderly_exit(self):
        process, result = self.check_run([subprocess.TimeoutExpired('unused', 1), ('ended', '')])
        process.terminate.assert_called_once()
        self.assertTrue(result.timed_out)

    def test_stuck_process_is_left_alive(self):
        process, _ = self.check_run([subprocess.TimeoutExpired('unused', 1), subprocess.TimeoutExpired('unused', 120)], True)
        process.terminate.assert_called_once()

if __name__ == '__main__':
    unittest.main()
