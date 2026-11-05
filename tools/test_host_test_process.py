#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Process EOF, a hostile child and a crash log need no guest drive."""
import os
from pathlib import Path
import sys
import shutil
import tempfile
import time
import unittest
from host_test_process import run_logged


class OwnedHostProcess(unittest.TestCase):
    def run_case(self, source, **options):
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder) / 'host.log'
            code, text = run_logged([sys.executable, '-u', '-c', source], log, **options)
            return code, text, log.stat().st_size

    def test_eof_returns_without_waiting_for_deadline(self):
        code, text, _ = self.run_case("print('EOF')", timeout=2)
        self.assertEqual(code, 0)
        self.assertEqual(text, 'EOF\n')

    def test_ignored_termination_and_inherited_stdout_are_killed(self):
        source = """import os, signal, subprocess, sys, time
signal.signal(signal.SIGTERM, signal.SIG_IGN)
child = subprocess.Popen([sys.executable, '-c', 'import signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); time.sleep(30)'])
print('GROUP', os.getpid(), 'CHILD', child.pid, flush=True)
while True: time.sleep(1)
"""
        start = time.monotonic()
        code, text, _ = self.run_case(source, timeout=0.3, stop_grace=0.05)
        self.assertEqual(code, 'timeout')
        self.assertLess(time.monotonic() - start, 3)
        group = int(text.split()[1])
        until = time.monotonic() + 2
        while time.monotonic() < until:
            try:
                os.killpg(group, 0)
            except ProcessLookupError:
                return
            time.sleep(0.01)
        self.fail('the test process group is still present')

    def test_log_flood_stops_and_preserves_bounded_beginning_and_end(self):
        source = "import os; print('BEGIN',flush=True)\nwhile True: os.write(1,b'x'*16384)"
        code, text, size = self.run_case(source, timeout=2, log_limit=128 << 10,
                                        output_limit=64 << 10, stop_grace=0.05)
        self.assertEqual(code, 'log size limit')
        self.assertLessEqual(size, 64 << 10)
        self.assertTrue(text.startswith('BEGIN\n'))
        self.assertIn('beginning and end preserved', text)
        self.assertTrue(text.endswith('xxx'))

    def test_live_callback_observes_partial_lines_once(self):
        seen = []
        def collect(line, process):
            seen.append(line)
            if line == 'ready\n':
                self.assertIsNone(process.poll())
        source = "import sys,time; print('ready',flush=True); time.sleep(.1); sys.stdout.write('tail'); sys.stdout.flush()"
        code, text, _ = self.run_case(source, timeout=2, on_line=collect)
        self.assertEqual(code, 0)
        self.assertEqual(text, 'ready\ntail')
        self.assertEqual(seen, ['ready\n', 'tail'])

    def test_disk_reserve_stops_owned_host(self):
        start = time.monotonic()
        code, _, _ = self.run_case('import time; time.sleep(30)', timeout=2,
                                  min_free_bytes=shutil.disk_usage(tempfile.gettempdir()).total + 1)
        self.assertEqual(code, 'disk reserve reached')
        self.assertLess(time.monotonic() - start, 3)


if __name__ == '__main__':
    unittest.main()
