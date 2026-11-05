#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Evidence export stays bounded and never retains secret-bearing long lines."""
import gzip
import os
from pathlib import Path
import sys
import tempfile
import tracemalloc
import unittest
from unittest.mock import Mock, patch
sys.path.insert(0, str(Path(__file__).resolve().parent))
import game_repertoire_logs as logs

class ExportTests(unittest.TestCase):
    def test_redaction_overlong_lines_and_compression_budget(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder); source=root/'host.log'; dest=root/'out'; dest.mkdir()
            with source.open('w') as f:
                f.write('start\npassword=private\n' + 'x'*65530 + 'product key=private\n')
                for _ in range(5000):
                    f.write(os.urandom(200).hex()+'\n')
                f.write('NEO98 HOST exited\n')
            tracemalloc.start()
            try:
                logs.export_log(source,dest,root,True)
                self.assertLess(tracemalloc.get_traced_memory()[1], 2*1024**2)
            finally:
                tracemalloc.stop()
            self.assertFalse((dest/'sanitized.log.gz').exists())
            text=(dest/'excerpt.log').read_text()
            self.assertNotIn('private',text)
            self.assertIn('NEO98 HOST exited',text)
            self.assertIn('Omitted overlong lines: 1',text)

    def test_small_log_and_reserve_stop(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder); source=root/'host.log'; dest=root/'out'; dest.mkdir()
            source.write_text('start\nserial number=private\nNEO98 HOST exited\n')
            logs.export_log(source,dest,root,True)
            self.assertEqual(gzip.decompress((dest/'sanitized.log.gz').read_bytes()),b'start\nNEO98 HOST exited\n')
            with patch('game_repertoire.shutil.disk_usage',return_value=Mock(free=5*1024**3)):
                with self.assertRaisesRegex(RuntimeError,'result processing'):
                    logs.export_log(source,dest,root,True)

if __name__=='__main__':unittest.main()
