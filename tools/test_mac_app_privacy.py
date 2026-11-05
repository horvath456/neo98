#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""padding is no installation fingerprint; diverse samples still detect leaks."""
import tempfile
import unittest
from pathlib import Path
from test_mac_app_host import registry_windows, files_holding


class RegistryProbeTest(unittest.TestCase):
    def test_padding_and_short_sections(self):
        for data in (b'', b'\0' * 1024, b'ABCD' * 256, bytes(range(63))):
            self.assertEqual(list(registry_windows(data)), [])

    def test_quarter_padding_skipped_and_leak_detected(self):
        payload = bytes(range(64))
        data = b'\0' * 768 + b'AB' * 64 + payload + b'\0' * 64
        samples = list(registry_windows(data))
        self.assertTrue(samples)
        for at, value in samples:
            self.assertEqual(value, data[at:at + 64])
            self.assertEqual(len(value), 64)
            self.assertGreaterEqual(len(set(value)), 16)
        wanted = [(str(at), value) for at, value in samples]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'library').write_bytes(b'\0' * 2048)
            self.assertEqual(files_holding(root, wanted), [])
            (root / 'leak').write_bytes(samples[0][1])
            self.assertIn((wanted[0][0], 'leak'), files_holding(root, wanted))


if __name__ == '__main__':
    unittest.main()
