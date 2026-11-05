#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Small ISO9660/CAB fixtures exercise validation without copyrighted payloads."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile
import unittest
from unittest.mock import patch

import win98_iso as w


def both(value, width):
    return value.to_bytes(width, 'little') + value.to_bytes(width, 'big')


def record(name, lba, size, flags=0):
    name = name if isinstance(name, bytes) else name.encode('ascii')
    data = bytearray(33 + len(name) + (len(name) % 2 == 0))
    data[0] = len(data)
    data[2:10] = both(lba, 4)
    data[10:18] = both(size, 4)
    data[25] = flags
    data[28:32] = both(1, 2)
    data[32] = len(name)
    data[33:33 + len(name)] = name
    return data


def iso_bytes(entries):
    """One PVD, a root directory and a WIN98 directory; no boot image."""
    image = bytearray(22 * w.SECTOR)
    root = record('WIN98', 21, w.SECTOR, 2)
    image[20*w.SECTOR:20*w.SECTOR+len(root)] = root
    directory = bytearray()
    for name, payload in entries:
        lba = len(image) // w.SECTOR
        image.extend(payload)
        image.extend(bytes(-len(image) % w.SECTOR))
        directory.extend(record(name + ';1', lba, len(payload)))
    assert len(directory) <= w.SECTOR
    image[21*w.SECTOR:21*w.SECTOR+len(directory)] = directory
    pvd = bytearray(w.SECTOR)
    pvd[:7] = b'\x01CD001\x01'
    pvd[80:88] = both(len(image)//w.SECTOR, 4)
    pvd[120:124] = both(1, 2)
    pvd[124:128] = both(1, 2)
    pvd[128:132] = both(w.SECTOR, 2)
    root = record(b'\0', 20, w.SECTOR, 2)
    pvd[156:156+len(root)] = root
    image[16*w.SECTOR:17*w.SECTOR] = pvd
    image[17*w.SECTOR:17*w.SECTOR+7] = b'\xffCD001\x01'
    return image


def cabinet(entries, *, next_name=None, folder=0):
    """A real, uncompressed Microsoft CAB with one CFDATA block."""
    links = (next_name.encode('ascii') + b'\0disk\0') if next_name else b''
    file_table, payload = bytearray(), bytearray()
    for name, data in entries:
        file_table.extend(struct.pack('<IIHHHH', len(data), len(payload), folder, 0, 0, 0))
        file_table.extend(name.encode('ascii') + b'\0')
        payload.extend(data)
    offset = 36 + len(links) + 8
    block_offset = offset + len(file_table)
    header = bytearray(36)
    header[:4] = b'MSCF'
    struct.pack_into('<I', header, 8, block_offset + 8 + len(payload))
    struct.pack_into('<I', header, 16, offset)
    header[24:26] = b'\x03\x01'
    struct.pack_into('<5H', header, 26, 1, len(entries), 2 if next_name else 0, 123, 0)
    return (header + links + struct.pack('<IHH', block_offset, 1, 0) + file_table
            + struct.pack('<IHH', 0, len(payload), len(payload)) + payload)


def metadata(version='4,10,0,2222', language='EN', product='1'):
    return f'[data]\nProductType={product}\n[Strings]\nLANG="{language}"\nOS_VERSION="{version}"\n'.encode()


class IsoValidationTests(unittest.TestCase):
    def setUp(self):
        if shutil.disk_usage(tempfile.gettempdir()).free < w.MIN_FREE_SPACE + (1 << 20):
            self.skipTest('8 GiB free-space floor')
        self.tmp = tempfile.TemporaryDirectory(prefix='test-win98-iso-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.iso = self.root / 'unknown-layout.iso'
        profile = json.loads(w.MANIFEST.read_text())['content_profiles'][0]
        self.payloads = [(name, ('fixture for ' + name).encode()) for name in profile['files']]
        profile['files'] = {name: {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
                            for name, data in self.payloads}
        self.manifest = self.root / 'profiles.json'
        self.manifest.write_text(json.dumps({'content_profiles': [profile]}))
        self.addCleanup(patch.stopall)
        patch.object(w, 'MANIFEST', self.manifest).start()

    def make(self, *, inf=None, payloads=None, extra=(), cab=None):
        entries = [('SETUP.EXE', b'setup'), ('SMARTDRV.EXE', b'smartdrv'),
                   ('PRECOPY1.CAB', cabinet([('SETUPPP.INF', metadata() if inf is None else inf)])),
                   ('WIN98_21.CAB', cabinet(self.payloads if payloads is None else payloads) if cab is None else cab)]
        self.iso.write_bytes(iso_bytes(entries + list(extra)))
        return self.iso

    def validate(self):
        if not shutil.which('cabextract'):
            self.skipTest('cabextract is required for real CAB integration')
        return w.content_record(self.iso)

    def test_unknown_full_iso_real_cabextract(self):
        for product in ('1', '9'):
            with self.subTest(product=product):
                self.make(inf=metadata(product=product))
                result = self.validate()
                self.assertEqual(result['validation'], 'content')
                self.assertEqual(result['media_type'], 'full')
                self.assertEqual(result['product_type'], product)
                self.assertEqual(result['sha256'], hashlib.sha256(self.iso.read_bytes()).hexdigest())

    def test_wrong_critical_file(self):
        self.make(payloads=[(name, b'x'*len(data)) for name, data in self.payloads])
        with self.assertRaisesRegex(w.CompatibilityError, 'Incompatible critical'):
            self.validate()

    def test_missing_critical_file(self):
        self.make(payloads=self.payloads[1:])
        with self.assertRaisesRegex(w.CompatibilityError, 'Missing required setup file'):
            self.validate()

    def test_first_edition(self):
        self.make(inf=metadata(version='4,10,0,1998'))
        with self.assertRaisesRegex(w.CompatibilityError, 'OS_VERSION'):
            self.validate()

    def test_other_language(self):
        self.make(inf=metadata(language='DE'))
        with self.assertRaisesRegex(w.CompatibilityError, 'language'):
            self.validate()

    def test_upgrade_despite_matching_critical_files(self):
        self.make(inf=metadata(product='2'))
        with self.assertRaisesRegex(w.CompatibilityError, 'Upgrade media'):
            self.validate()

    def test_unknown_or_missing_sku(self):
        for product in ('', '3', '99'):
            with self.subTest(product=product):
                self.make(inf=metadata(product=product))
                with self.assertRaisesRegex(w.CompatibilityError, 'ProductType'):
                    self.validate()

    def test_duplicate_metadata(self):
        for inf in (metadata()+b'LANG=EN\n', metadata()+b'[DATA]\nproducttype=2\n'):
            with self.assertRaisesRegex(w.CompatibilityError, 'Duplicate'):
                w._metadata(inf)

    def test_product_type_must_be_in_data_section(self):
        self.assertIsNone(w._metadata(b'[Strings]\nProductType=1\n')[2])

    def test_comments_quotes_and_case_in_metadata(self):
        self.assertEqual(w._metadata(b'[data]\n producttype = "9" ; full\n[strings]\nlang="EN"\nos_version=4,10,0,2222'),
                         ('4,10,0,2222', 'EN', '9'))

    def test_missing_cab_chain_member(self):
        self.make(cab=cabinet(self.payloads, next_name='WIN98_22.CAB'))
        with self.assertRaisesRegex(w.CompatibilityError, 'Missing cabinet chain member'):
            self.validate()

    def test_missing_file_continuation(self):
        self.make(cab=cabinet(self.payloads, folder=0xfffe))
        with self.assertRaisesRegex(w.CompatibilityError, 'Missing cabinet continuation'):
            self.validate()

    def test_duplicate_cabinet_payload(self):
        self.make(payloads=self.payloads + [self.payloads[0]])
        with self.assertRaisesRegex(w.CompatibilityError, 'Duplicate or broken'):
            self.validate()

    def test_disagreeing_loose_file(self):
        self.make(extra=[(self.payloads[0][0], b'different')])
        with self.assertRaisesRegex(w.CompatibilityError, 'copies disagree'):
            self.validate()

    def test_matching_loose_copy(self):
        self.make(extra=[self.payloads[0]])
        self.assertEqual(self.validate()['media_type'], 'full')

    def test_minimal_setup_user_is_not_installed_user(self):
        self.make(extra=[('MINI.CAB', cabinet([('USER.EXE', b'minimal setup user')]))])
        self.assertEqual(self.validate()['media_type'], 'full')

    def test_duplicate_iso_name(self):
        self.make(extra=[('setup.exe', b'other')])
        with self.assertRaisesRegex(w.CompatibilityError, 'Duplicate ISO9660'):
            w.setup_files(self.iso)

    def mutate_record(self, offset, data):
        image = bytearray(self.iso.read_bytes())
        image[21*w.SECTOR+offset:21*w.SECTOR+offset+len(data)] = data
        self.iso.write_bytes(image)

    def test_extent_outside_volume(self):
        self.make()
        self.mutate_record(2, both(1000000, 4))
        with self.assertRaisesRegex(w.CompatibilityError, 'extent exceeds'):
            w.setup_files(self.iso)

    def test_extent_length_outside_volume(self):
        self.make()
        self.mutate_record(10, both(0xffffffff, 4))
        with self.assertRaisesRegex(w.CompatibilityError, 'extent exceeds'):
            w.setup_files(self.iso)

    def test_multi_extent_rejected(self):
        self.make()
        self.mutate_record(25, b'\x80')
        with self.assertRaisesRegex(w.CompatibilityError, 'multi-extent'):
            w.setup_files(self.iso)

    def test_byte_order_disagreement(self):
        self.make()
        self.mutate_record(2, b'\xff')
        with self.assertRaisesRegex(w.CompatibilityError, 'byte-order'):
            w.setup_files(self.iso)

    def test_truncated_volume(self):
        self.make()
        self.iso.write_bytes(self.iso.read_bytes()[:-1])
        with self.assertRaisesRegex(w.CompatibilityError, 'volume size'):
            w.setup_files(self.iso)

    def test_directory_in_place_of_required_file(self):
        self.make()
        self.mutate_record(25, b'\x02')
        with self.assertRaisesRegex(w.CompatibilityError, 'normal file SETUP.EXE'):
            w.setup_files(self.iso)

    def test_low_disk_fails_before_copy(self):
        self.make()
        usage = shutil.disk_usage(self.root)._replace(free=w.MIN_FREE_SPACE)
        with patch.object(w.shutil, 'disk_usage', return_value=usage):
            with self.assertRaisesRegex(w.CompatibilityError, 'free space'):
                self.validate()

    def test_free_space_rechecked_before_copy(self):
        self.make()
        usage = shutil.disk_usage(self.root)
        with patch.object(w.shutil, 'disk_usage',
                          side_effect=[usage, usage._replace(free=w.MIN_FREE_SPACE)]):
            with self.assertRaisesRegex(w.CompatibilityError, 'free space'):
                self.validate()

    def test_truncated_cabinet_data(self):
        self.make(cab=cabinet(self.payloads)[:-1])
        with self.assertRaisesRegex(w.CompatibilityError, 'Truncated or invalid cabinet'):
            self.validate()

    def test_extraction_output_bound(self):
        if not shutil.which('cabextract'):
            self.skipTest('cabextract unavailable')
        cab = self.root / 'test.cab'
        cab.write_bytes(cabinet([('TEST.DLL', b'123456')]))
        with self.assertRaisesRegex(w.CompatibilityError, 'exceeded limits'):
            w._extract(cab, 'TEST.DLL', 5)

    def test_invalid_cabinet(self):
        self.make(cab=b'not a cabinet')
        with self.assertRaisesRegex(w.CompatibilityError, 'Invalid cabinet header'):
            self.validate()


if __name__ == '__main__':
    unittest.main()
