#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""ISO recognition, source compatibility and the packaged setup entry point."""
from __future__ import annotations

import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import mac_app
import setup_win98 as setup


class Acceptance(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.iso = self.root / 'another.iso'
        self.iso.write_bytes(b'original image')
        self.known = dict(name='Known SE', size=self.iso.stat().st_size,
                          sha256=hashlib.sha256(self.iso.read_bytes()).hexdigest(), profile='english-se')

    def test_known_image_keeps_the_hash_fast_path(self):
        with patch.object(setup.win98_c, 'supported_isos', return_value=[self.known]), \
                patch.object(setup.win98_iso, 'content_record') as content:
            result = setup.check_iso(self.iso)
        self.assertEqual(result['validation'], 'sha256')
        self.assertEqual(result['profile'], 'english-se')
        content.assert_not_called()
        self.assertNotIn('validation', self.known)

    def test_another_image_is_checked_by_content(self):
        self.iso.write_bytes(b'another image!')  # same size, different digest
        digest = hashlib.sha256(self.iso.read_bytes()).hexdigest()
        result = dict(self.known, sha256=digest, validation='content')
        with patch.object(setup.win98_c, 'supported_isos', return_value=[self.known]), \
                patch.object(setup.win98_iso, 'content_record', return_value=result) as content:
            self.assertEqual(setup.check_iso(self.iso), result)
        content.assert_called_once_with(self.iso, digest=digest)

    def test_different_size_does_not_need_an_image_hash_pin(self):
        self.iso.write_bytes(b'a differently arranged image')
        with patch.object(setup.win98_c, 'supported_isos', return_value=[self.known]), \
                patch.object(setup.win98_iso, 'content_record', return_value={'profile': 'english-se'}) as content:
            self.assertEqual(setup.check_iso(self.iso), {'profile': 'english-se'})
        content.assert_called_once_with(self.iso, digest=None)

    def test_an_older_installation_can_use_the_same_content_profile(self):
        installed = {'iso': {'sha256': self.known['sha256']}}
        other = {'sha256': 'b' * 64, 'profile': 'english-se'}
        with patch.object(setup.win98_c, 'record', return_value=installed), \
                patch.object(setup.win98_c, 'supported_isos', return_value=[self.known]):
            setup.check_source_iso(self.root, other)

    def test_unknown_old_source_can_only_use_the_exact_image(self):
        installed = {'iso': {'sha256': 'a' * 64}}
        with patch.object(setup.win98_c, 'record', return_value=installed), \
                patch.object(setup.win98_c, 'supported_isos', return_value=[]):
            setup.check_source_iso(self.root, {'sha256': 'a' * 64})
            with self.assertRaisesRegex(RuntimeError, 'original ISO'):
                setup.check_source_iso(self.root, {'sha256': 'b' * 64, 'profile': 'english-se'})

    def test_wrong_source_is_refused_before_any_completion_changes(self):
        installed = {'iso': {'sha256': 'a' * 64, 'profile': 'english-se'}}
        other = {'sha256': 'b' * 64, 'profile': 'another-language'}
        with patch.object(setup, 'check_iso', return_value=other), \
                patch.object(setup.win98_c, 'record', return_value=installed), \
                patch.object(setup.win98_c, 'supported_isos', return_value=[]), \
                patch.object(setup.win98_time_zone, 'apply') as zone, \
                patch.object(setup.win98_glide_driver, 'apply') as glide, \
                patch.object(setup.win98_cabs, 'apply') as cabinets:
            with self.assertRaisesRegex(RuntimeError, 'existing Windows installation'):
                setup.complete(self.root, self.iso)
        zone.assert_not_called()
        glide.assert_not_called()
        cabinets.assert_not_called()

    def test_unsupported_image_never_starts_installation_or_clears_assets(self):
        target = self.root / 'machine'
        with patch.object(setup, 'check_iso', side_effect=RuntimeError('incompatible system files')), \
                patch.object(setup, 'read_product_key') as key, \
                patch.object(setup, 'require_commands') as commands, \
                patch.object(setup, 'clear_caches') as caches, \
                patch.object(setup, 'run_setup') as qemu:
            with self.assertRaisesRegex(RuntimeError, 'incompatible system files'):
                setup.setup(self.iso, folder=target, work_root=self.root / 'work')
        for action in (key, commands, caches, qemu):
            action.assert_not_called()
        self.assertFalse(target.exists())

    def test_packaged_setup_imports_without_the_checkout_or_python_packages(self):
        scripts = self.root / 'setup'
        scripts.mkdir()
        source = Path(setup.__file__).parent
        for name in mac_app.SETUP_FILES:
            shutil.copyfile(source / name, scripts / name)
        for name in mac_app.SETUP_DIRECTORIES:
            shutil.copytree(source / name, scripts / name)
        program = '/usr/bin/python3'
        if not Path(program).is_file():
            program = shutil.which('python3')
        result = subprocess.run([program, '-I', '-c',
                                 'import sys; sys.path.insert(0, sys.argv[1]); '
                                 'import setup_win98, win98_boot; '
                                 'from pathlib import Path; '
                                 'assert (Path(win98_boot.__file__).parent / "freedos_boot/win98-fat12.bin").is_file(); '
                                 'assert (Path(win98_boot.__file__).parent / "freedos_boot/COPYING").is_file(); '
                                 'print("setup imported")', str(scripts)],
                                cwd=self.root, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.strip(), 'setup imported')


class BootLayout(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.iso = Path(self.temporary.name) / 'boot.iso'
        self.floppy = bytearray(1440 * 1024)
        self.floppy[510:512] = b'\x55\xaa'
        self.image = bytearray(22 * setup.CD_SECTOR + len(self.floppy))
        for block, kind in ((16, 1), (17, 0), (18, 255)):
            self.image[block * 2048:block * 2048 + 7] = bytes([kind]) + b'CD001\x01'
        struct.pack_into('<I', self.image, 16 * 2048 + 80, len(self.image) // 2048)
        record = 17 * 2048
        self.image[record + 7:record + 39] = b'EL TORITO SPECIFICATION'.ljust(32, b' ')
        struct.pack_into('<I', self.image, record + 71, 20)
        catalog = 20 * 2048
        self.image[catalog] = 1
        self.image[catalog + 30:catalog + 32] = b'\x55\xaa'
        struct.pack_into('<H', self.image, catalog + 28,
                         -sum(struct.unpack_from('<16H', self.image, catalog)) & 0xffff)
        self.image[catalog + 32:catalog + 34] = b'\x88\x02'
        struct.pack_into('<I', self.image, catalog + 40, 22)
        self.image[22 * 2048:] = self.floppy

    def inspect(self):
        self.iso.write_bytes(self.image)
        # The content reader has its own malformed-ISO tests. These tests
        # exercise boot-layout validation independently of CAB contents.
        with patch.object(setup.win98_iso, 'setup_files', return_value={}):
            return setup.inspect_iso(self.iso)

    def test_supported_floppy_layout_is_preserved(self):
        self.assertEqual(self.inspect(), self.floppy)

    def test_boot_record_can_precede_the_primary_descriptor(self):
        primary = self.image[16 * 2048:17 * 2048]
        boot = self.image[17 * 2048:18 * 2048]
        self.image[16 * 2048:17 * 2048] = boot
        self.image[17 * 2048:18 * 2048] = primary
        self.assertEqual(self.inspect(), self.floppy)

    def test_catalog_cannot_point_past_the_declared_volume(self):
        struct.pack_into('<I', self.image, 17 * 2048 + 71, len(self.image) // 2048)
        with self.assertRaisesRegex(RuntimeError, 'catalog lies outside'):
            self.inspect()

    def test_boot_image_cannot_point_past_the_declared_volume(self):
        struct.pack_into('<I', self.image, 20 * 2048 + 40, 23)
        with self.assertRaisesRegex(RuntimeError, 'boot image lies outside'):
            self.inspect()

    def test_bad_boot_catalog_checksum_is_rejected(self):
        self.image[20 * 2048 + 4] = 1
        with self.assertRaisesRegex(RuntimeError, 'Invalid x86 El Torito'):
            self.inspect()


if __name__ == '__main__':
    unittest.main()
