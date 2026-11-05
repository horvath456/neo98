#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The FreeDOS-to-Microsoft-DOS handoff permits exactly one extra initial boot."""
from pathlib import Path
import contextlib
import io
import os
import signal
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

import setup_win98 as setup


class BootstrapSequence(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)
        self.disk = self.work / 'disk.raw'
        self.disk.touch()
        self.floppy = self.work / 'boot.img'
        self.floppy.touch()

    def sequence(self, events, *, bootstrap):
        calls = []

        def command(iso, disk, floppy, serial, monitor):
            index = len(calls)
            calls.append(floppy)
            body = '\n'.join(events[index]) + '\n'
            return [sys.executable, '-c', 'import pathlib,sys; pathlib.Path(sys.argv[1]).write_text(sys.argv[2])',
                    str(serial), body]

        with patch.object(setup, 'qemu_args', command), patch.object(setup.SetupPreview, 'frame'):
            setup.run_setup(None, self.disk, self.floppy, self.work, 10, bootstrap=bootstrap)
        return calls

    def test_bootstrap_handoff_runs_second_boot_without_floppy(self):
        calls = self.sequence([
            ['NEO98_DOS_BOOT', 'NEO98_SYS_OK'],
            ['NEO98_CD_OK', 'NEO98_SETUP_START', 'NEO98_SETUP_COMPLETE'],
        ], bootstrap=True)
        self.assertEqual(calls, [self.floppy, None])

    def test_regular_installation_does_not_gain_an_unexpected_reboot(self):
        with self.assertRaisesRegex(RuntimeError, 'before Setup completed'):
            self.sequence([['NEO98_DOS_BOOT', 'NEO98_SYS_OK']], bootstrap=False)

    def test_bootstrap_requires_a_successful_system_transfer(self):
        with self.assertRaisesRegex(RuntimeError, 'before Setup completed'):
            self.sequence([['NEO98_DOS_BOOT']], bootstrap=True)

    def test_handoff_is_allowed_only_once(self):
        with self.assertRaisesRegex(RuntimeError, 'before Setup completed'):
            self.sequence([
                ['NEO98_DOS_BOOT', 'NEO98_SYS_OK'],
                ['NEO98_DOS_BOOT', 'NEO98_SYS_OK'],
            ], bootstrap=True)

    def test_guest_bootstrap_failure_stops_instead_of_rebooting(self):
        with self.assertRaisesRegex(RuntimeError, 'NEO98_SYS_FAIL'):
            self.sequence([['NEO98_DOS_BOOT', 'NEO98_SYS_FAIL']], bootstrap=True)


class BootSelection(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.iso = self.root / 'source.iso'
        self.iso.write_bytes(b'fixture')
        self.record = {'sha256': 'a' * 64, 'profile': 'english-se'}

    def install(self, inspection):
        helper = Mock()
        key = 'QQQQQ-' 'WWWWW-' 'EEEEE-' 'RRRRR-' 'TTTTT'
        with patch.object(setup.win98_c, 'ready', return_value=False), \
                patch.object(setup, 'check_iso', return_value=self.record), \
                patch.object(setup, 'require_commands'), \
                patch.object(setup, 'inspect_iso', **inspection), \
                patch.object(setup, 'check_boot_files') as check, \
                patch.object(setup, 'prepare_disk'), patch.object(setup, 'stage_finish'), \
                patch.object(setup, 'prepare_floppy') as native, \
                patch.object(setup, 'run_setup') as run, patch.object(setup, 'validate_disk'), \
                patch.object(setup, 'import_folder'), \
                patch.dict(sys.modules, {'win98_boot': SimpleNamespace(prepare_bootstrap=helper)}):
            result = setup.setup(self.iso, folder=self.root / 'C', work_root=self.root / 'work',
                                 product_key=key, user=True)
        return result, helper, native, run, check

    def test_nonbootable_full_image_uses_freedos_and_original_answer(self):
        result, helper, native, run, check = self.install({'side_effect': setup.NoBootImage('no catalog')})
        self.assertEqual(result, 0)
        helper.assert_called_once()
        native.assert_not_called()
        check.assert_not_called()
        self.assertTrue(run.call_args.kwargs['bootstrap'])
        self.assertEqual(helper.call_args.args[0], self.iso.resolve())
        self.assertEqual(helper.call_args.kwargs['partition_offset'], setup.PARTITION_OFFSET)
        self.assertEqual(helper.call_args.kwargs['cache_dir'], self.root / 'work/boot-cache')
        self.assertIn(b'[BatchSetup]', helper.call_args.args[3])

    def test_bootable_source_keeps_its_original_dos_floppy(self):
        result, helper, native, run, check = self.install({'return_value': b'original boot floppy'})
        self.assertEqual(result, 0)
        helper.assert_not_called()
        native.assert_called_once()
        check.assert_called_once()
        self.assertFalse(run.call_args.kwargs['bootstrap'])

    def test_damaged_boot_image_is_not_silently_replaced(self):
        with self.assertRaisesRegex(RuntimeError, 'invalid boot checksum'):
            self.install({'side_effect': RuntimeError('invalid boot checksum')})
        self.assertFalse((self.root / 'C').exists())

    def test_main_removes_unfinished_disk_after_a_bootstrap_timeout(self):
        import win98_boot
        target = self.root / 'C'
        work = self.root / 'install'
        partial = work / ('a' * 16)

        def stalled(*args, **kwargs):
            partial.mkdir(parents=True)
            (partial / 'disk.raw').write_bytes(b'PRIVATE ANSWER FIXTURE')
            win98_boot._run('mcopy', 'private staging file')

        previous = signal.getsignal(signal.SIGTERM)
        try:
            with patch.dict(os.environ), patch.object(setup, 'setup', side_effect=stalled), \
                    patch.object(win98_boot.subprocess, 'run', side_effect=subprocess.TimeoutExpired('mcopy', 60,
                                                                                               output=b'PRIVATE ANSWER FIXTURE')), \
                    contextlib.redirect_stdout(io.StringIO()) as output:
                result = setup.main(['--user', '--iso', str(self.iso), '--target', str(target), '--work', str(work)])
        finally:
            signal.signal(signal.SIGTERM, previous)
        self.assertEqual(result, 1)
        self.assertFalse(partial.exists())
        self.assertNotIn('PRIVATE ANSWER FIXTURE', output.getvalue())


if __name__ == '__main__':
    unittest.main()
