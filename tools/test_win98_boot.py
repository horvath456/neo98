#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Unit checks; opt-in headless media smoke with NEO98_BOOT_TEST_ISO=/path.iso."""
from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import tempfile
import time
import unittest
from unittest.mock import patch

import win98_boot as boot


class BootTests(unittest.TestCase):
    def test_pinned_sector_and_win9x_entry(self):
        sector = boot.boot_sector()
        self.assertEqual(sector[-2:], b'\x55\xaa')
        self.assertEqual(sector[0x1f1:0x1fc], b'IO      SYS')
        self.assertIn(b'\xea\x00\x02\x70\x00', sector)  # JMP

    def test_corrupt_asset_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, 'win98-fat12.bin').write_bytes(b'\0' * 512)
            with patch.object(boot, 'BOOT_DIRECTORY', Path(directory)):
                with self.assertRaisesRegex(RuntimeError, 'SHA256'):
                    boot.boot_sector()

    @unittest.skipUnless(shutil.which('nasm'), 'NASM needed to reproduce asset')
    def test_reproducible_official_source(self):
        expected = {
            'oemboot.asm': 'e1d63e42c9b54bdb151f81f23e0c4739bd7ebc327ae64838d98e953bb7283c0d',
            'magic.mac': '8723d23e082ba5ee5c5f4fd1ad22c441097f84b5328fd079b2004090186b1090',
            'COPYING': '0f1e68d9b4a580cdcca4c5f4e3b8046f7a8759da05edd109cff081bc484b3c4b',
        }
        for name, digest in expected.items():
            self.assertEqual(hashlib.sha256((boot.BOOT_DIRECTORY / name).read_bytes()).hexdigest(), digest)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'boot.bin'
            subprocess.run(['nasm', '-DISFAT12', '-DWINBOOT', '-f', 'bin',
                            '-I' + str(boot.BOOT_DIRECTORY) + '/',
                            str(boot.BOOT_DIRECTORY / 'oemboot.asm'), '-o', str(output)], check=True)
            self.assertEqual(output.read_bytes(), boot.boot_sector())

    def test_command_timeout_is_sanitized_runtime_error(self):
        failure = subprocess.TimeoutExpired(['mcopy', 'PRIVATE ARGUMENT'], 60,
                                            output=b'PRIVATE OUTPUT', stderr=b'PRIVATE ERROR')
        with patch.object(boot.subprocess, 'run', side_effect=failure):
            with self.assertRaises(RuntimeError) as caught:
                boot._run('mcopy', 'PRIVATE ARGUMENT')
        self.assertEqual(str(caught.exception), 'FreeDOS bootstrap command timed out: mcopy')
        self.assertTrue(caught.exception.__suppress_context__)

    def test_disk_floor(self):
        with patch.object(boot.shutil, 'disk_usage', return_value=shutil._ntuple_diskusage(20, 13, 7)):
            with self.assertRaisesRegex(RuntimeError, '8 GiB'):
                boot._space('.')

    def test_staging_protocol_and_private_answer(self):
        # Stub only media extraction and mtools; check the actual generated files.
        copied = {}
        originals = {name: name.encode() for name in boot.BOOT_FILES + ('SYS.COM', 'RESTART.COM')}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with patch.object(boot, 'extract_boot_files', return_value=originals), \
                 patch.object(boot, '_run'), patch.object(boot, '_space'), \
                 patch.object(boot, '_copy', side_effect=lambda volume, data, dest, scratch: copied.__setitem__((str(volume), dest), data)):
                boot.prepare_bootstrap(root / 'source.iso', root / 'disk', root / 'floppy',
                                       b'PRIVATE ANSWER TEST', root)
            answer_copies = [key for key, data in copied.items() if b'PRIVATE ANSWER TEST' in data]
            self.assertEqual(answer_copies, [(str(root / 'disk') + '@@1048576', '::/Neo98/MSBATCH.INF')])
            first = copied[(str(root / 'floppy'), '::AUTOEXEC.BAT')]
            self.assertIn(b'A:\\SYS.COM A: C:\r\n', first)
            self.assertLess(first.index(b'NEO98_SYS_OK'), first.index(b'A:\\RESTART.COM'))
            second = copied[(str(root / 'disk') + '@@1048576', '::/Neo98/INSTALL.BAT')]
            self.assertIn(b'C:\\NEO98\\MSBATCH.INF /IS /IE /IM', second)
            self.assertLess(second.index(b'DEL C:\\AUTOEXEC.BAT'), second.index(b'NEO98_SETUP_START'))
            self.assertFalse(list(root.glob('boot-*')))
            self.assertEqual((root / 'floppy').stat().st_mode & 0o777, 0o600)

    @unittest.skipUnless(os.environ.get('NEO98_BOOT_TEST_ISO'), 'Set NEO98_BOOT_TEST_ISO for real media smoke')
    def test_real_media_two_boots(self):
        # No product key is needed to prove Microsoft DOS and Setup initialize.
        import setup_win98 as setup
        iso = Path(os.environ['NEO98_BOOT_TEST_ISO']).resolve()
        with tempfile.TemporaryDirectory(prefix='neo98-boot-smoke-') as directory:
            work = Path(directory)
            boot._space(work)
            disk, floppy = work / 'disk.img', work / 'boot.img'
            setup.prepare_disk(disk)
            setup.stage_finish(disk, work)
            boot.prepare_bootstrap(iso, disk, floppy, b'[Setup]\r\nExpress=1\r\n', work)
            for stage in range(2):
                serial, monitor = work / f'{stage}.log', work / f'{stage}.sock'
                args = setup.qemu_args(iso, disk, floppy if stage == 0 else None, serial, monitor)
                process = subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                           stderr=subprocess.DEVNULL,
                                           env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy'))
                text = ''
                try:
                    deadline = time.monotonic() + 180
                    while time.monotonic() < deadline:
                        boot._space(work)
                        text = serial.read_text(errors='replace') if serial.exists() else ''
                        self.assertNotIn('_FAIL', text)
                        self.assertNotIn('NEO98_SETUP_RETURNED', text)
                        if process.poll() is not None or (stage == 1 and 'NEO98_SETUP_START' in text):
                            break
                        time.sleep(0.5)
                    if stage == 0:
                        self.assertEqual(process.poll(), 0, text)
                        self.assertIn('NEO98_DOS_BOOT', text)
                        self.assertIn('NEO98_SYS_OK', text)
                    else:
                        self.assertIn('NEO98_MSDOS_BOOT', text)
                        self.assertIn('NEO98_CD_OK', text)
                        self.assertIn('NEO98_SETUP_START', text)
                        # Wait for an immediate loader failure, not full installation.
                        time.sleep(5)
                        self.assertIsNone(process.poll())
                        self.assertNotIn('NEO98_SETUP_RETURNED', serial.read_text(errors='replace'))
                finally:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()


if __name__ == '__main__':
    unittest.main()
