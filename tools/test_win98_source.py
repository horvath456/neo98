#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Installation archive selection, containment, cleanup and Linux routing."""
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import linux_setup
import setup_win98
import win98_source as source


class Sources(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.payload = b'ISO fixture without licensed Windows data'

    def zip(self, names=('folder/Windows 98.ISO',)):
        path = self.root / 'media.ZIP'
        with zipfile.ZipFile(path, 'w') as archive:
            for name in names:
                archive.writestr(name, self.payload)
            archive.writestr('cover.jpg', b'cover')
        return path

    def test_direct_iso_is_neither_copied_nor_removed(self):
        path = self.root / 'Windows 98.ISO'
        path.write_bytes(self.payload)
        with source.installation_iso(path) as iso:
            self.assertEqual(iso, path.resolve())
        self.assertEqual(path.read_bytes(), self.payload)

    def test_nested_zip_extracts_only_iso_and_removes_temporary_copy(self):
        path = self.zip()
        before = path.read_bytes()
        with source.installation_iso(path) as iso:
            self.assertEqual(iso.read_bytes(), self.payload)
            self.assertEqual(list(iso.parent.iterdir()), [iso])
        self.assertFalse(iso.parent.exists())
        self.assertEqual(path.read_bytes(), before)

    def test_temporary_iso_is_removed_after_installer_failure(self):
        with self.assertRaisesRegex(RuntimeError, 'failed setup'):
            with source.installation_iso(self.zip()) as iso:
                raise RuntimeError('failed setup')
        self.assertFalse(iso.parent.exists())

    def test_temporary_iso_is_removed_on_cancellation(self):
        with self.assertRaises(KeyboardInterrupt):
            with source.installation_iso(self.zip()) as iso:
                raise KeyboardInterrupt()
        self.assertFalse(iso.parent.exists())

    def test_archive_requires_one_iso(self):
        for names in ((), ('one.iso', 'two.iso'), ('same.iso', 'folder/other.ISO')):
            with self.subTest(names=names), self.assertRaisesRegex(RuntimeError, 'exactly one'):
                with source.installation_iso(self.zip(names)):
                    self.fail('Ambiguous archive was accepted')

    def test_archive_paths_cannot_escape(self):
        for name in ('../escape.iso', '/escape.iso', 'C:/escape.iso', '..\\escape.iso'):
            with self.subTest(name=name), self.assertRaisesRegex(RuntimeError, 'path'):
                with source.installation_iso(self.zip((name,))):
                    self.fail('Unsafe archive path was accepted')
        self.assertFalse((self.root.parent / 'escape.iso').exists())

    def test_symlink_iso_is_rejected(self):
        path = self.root / 'link.zip'
        info = zipfile.ZipInfo('link.iso')
        info.create_system = 3
        info.external_attr = (stat.S_IFLNK | 0o777) << 16
        with zipfile.ZipFile(path, 'w') as archive:
            archive.writestr(info, 'outside.iso')
        with self.assertRaises(RuntimeError), source.installation_iso(path):
            self.fail('Symlink was accepted')

    def test_limited_disk_space_refuses_extraction(self):
        with patch.object(source.shutil, 'disk_usage', return_value=shutil._ntuple_diskusage(100, 99, 1)):
            with self.assertRaisesRegex(RuntimeError, 'disk space'), source.installation_iso(self.zip()):
                self.fail('Extraction started without free space')

    def test_zip_is_classified_by_contents(self):
        self.assertTrue(source.zip_contains_iso(self.zip()))
        self.assertFalse(source.zip_contains_iso(self.zip(('machine/C/WINDOWS/EXPLORER.EXE',))))

    def test_corrupt_zip_has_an_installer_error(self):
        path = self.root / 'broken.zip'
        path.write_bytes(b'broken')
        with self.assertRaisesRegex(RuntimeError, 'damaged'), source.installation_iso(path):
            self.fail('Corrupt ZIP was accepted')

    @unittest.skipUnless(any(shutil.which(p) for p in ('7zz', '7z', '7za')), '7-Zip is unavailable')
    def test_real_7z_stream_preserves_bytes_and_cleans_up(self):
        folder = self.root / 'nested'; folder.mkdir()
        (folder / 'Windows [98].ISO').write_bytes(self.payload)
        (folder / 'unrelated.txt').write_text('keep this outside extraction')
        archive = self.root / 'media.7z'
        subprocess.run([source._sevenzip(), 'a', '-bd', str(archive), 'nested'], cwd=self.root,
                       check=True, capture_output=True, timeout=20)
        with source.installation_iso(archive) as iso:
            self.assertEqual(iso.read_bytes(), self.payload)
            self.assertEqual(list(iso.parent.iterdir()), [iso])
        self.assertFalse(iso.parent.exists())

    def test_helper_output_limit_stops_decoder(self):
        command = [sys.executable, '-c', 'import sys; sys.stdout.buffer.write(b"x" * 65536)']
        with self.assertRaisesRegex(RuntimeError, 'size'):
            source._pipe(command, 4)

    def test_stalled_helper_times_out(self):
        command = [sys.executable, '-c', 'import time; time.sleep(20)']
        with self.assertRaisesRegex(RuntimeError, 'timed out'):
            source._pipe(command, 4, timeout=.1)

    def test_linux_auto_routes_iso_zip_to_installer_with_hidden_key(self):
        archive = self.zip()
        with patch.object(setup_win98, 'main', return_value=0) as install, \
                patch.object(linux_setup, 'import_machine') as machine:
            self.assertEqual(linux_setup.main(['auto', str(archive), '--target', str(self.root / 'C')]), 0)
        machine.assert_not_called()
        command = install.call_args.args[0]
        self.assertIn('--key-stdin', command)
        self.assertIn(str(archive), command)

    def test_linux_auto_keeps_existing_machine_zip_import(self):
        archive = self.zip(('C/WINDOWS/EXPLORER.EXE',))
        with patch.object(setup_win98, 'main') as install, \
                patch.object(linux_setup, 'import_machine') as machine:
            self.assertEqual(linux_setup.main(['auto', str(archive), '--target', str(self.root / 'C')]), 0)
        install.assert_not_called()
        machine.assert_called_once()

    def test_linux_machine_zip_with_a_game_iso_is_imported(self):
        names = tuple('machine/C/' + name for name in linux_setup.REQUIRED)
        archive = self.zip(names + ('machine/C/My Documents/game.iso',))
        with patch.object(setup_win98, 'main') as install, \
                patch.object(linux_setup, 'import_machine') as machine:
            self.assertEqual(linux_setup.main(['auto', str(archive), '--target', str(self.root / 'C')]), 0)
        install.assert_not_called()
        machine.assert_called_once()

    def test_zip_metadata_limits_precede_zipfile_allocation(self):
        for field, value in ((10, source.MAX_ENTRIES + 1), (12, source.MAX_ZIP_DIRECTORY + 1)):
            archive = self.zip()
            data = bytearray(archive.read_bytes())
            end = data.rfind(b'PK\x05\x06')
            struct.pack_into('<H' if field == 10 else '<I', data, end + field, value)
            if field == 10:
                struct.pack_into('<H', data, end + 8, value)
            archive.write_bytes(data)
            with self.subTest(field=field), patch.object(source.zipfile, 'ZipFile', side_effect=AssertionError('allocated')):
                with self.assertRaisesRegex(RuntimeError, 'too many entries|oversized file metadata'):
                    source.zip_contains_iso(archive)

    def test_central_directory_count_cannot_lie(self):
        archive = self.zip()
        data = bytearray(archive.read_bytes())
        end = data.rfind(b'PK\x05\x06')
        struct.pack_into('<HH', data, end + 8, 0, 0)
        archive.write_bytes(data)
        with patch.object(source.zipfile, 'ZipFile', side_effect=AssertionError('allocated')):
            with self.assertRaisesRegex(RuntimeError, 'file count'):
                source.zip_contains_iso(archive)

    def zip64(self, *, count=None, length=None):
        archive = self.zip()
        data = bytearray(archive.read_bytes())
        end = data.rfind(b'PK\x05\x06')
        _, _, _, _, actual_count, actual_size, offset, _ = struct.unpack_from('<4s4H2LH', data, end)
        count = actual_count if count is None else count
        length = actual_size if length is None else length
        record = struct.pack('<4sQ2H2L4Q', b'PK\x06\x06', 44, 45, 45, 0, 0, count, count, length, offset)
        locator = struct.pack('<4sLQL', b'PK\x06\x07', 0, end, 1)
        footer = bytearray(data[end:])
        struct.pack_into('<HHII', footer, 8, 0xffff, 0xffff, 0xffffffff, 0xffffffff)
        archive.write_bytes(data[:end] + record + locator + footer)
        return archive

    def test_small_zip64_archive_is_supported(self):
        with source.installation_iso(self.zip64()) as iso:
            self.assertEqual(iso.read_bytes(), self.payload)

    def test_zip64_metadata_limits_precede_allocation(self):
        for arguments in ({'count': source.MAX_ENTRIES + 1}, {'length': source.MAX_ZIP_DIRECTORY + 1}):
            archive = self.zip64(**arguments)
            with self.subTest(arguments=arguments), patch.object(source.zipfile, 'ZipFile', side_effect=AssertionError('allocated')):
                with self.assertRaisesRegex(RuntimeError, 'too many entries|oversized file metadata'):
                    source.zip_contains_iso(archive)


if __name__ == '__main__':
    unittest.main()
