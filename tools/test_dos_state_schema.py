#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""DOS archive compatibility rejects changes beyond the reviewed additions.

    build/analysis-venv/bin/python tools/test_dos_state_schema.py
"""
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import build_dosbox_pure as core

# The identity covers the compiler flags, and those differ per platform
# (-fPIC on Linux). Each value changes only on a reviewed archive change.
REVIEWED = {'darwin': 'a6a7f92a2d2f', 'linux': 'ccf60c88d32e'}[sys.platform]


class ArchiveIdentity(unittest.TestCase):
    def test_adc_sources_have_their_own_archive_key(self):
        self.assertEqual(core.state_identity(), REVIEWED)

    def test_adc_changes_reject_the_previous_archive(self):
        self.assertNotEqual(core.state_identity(), 'bc134fbd7d23')

    def test_an_adc_event_change_changes_the_key(self):
        with tempfile.TemporaryDirectory() as folder:
            helper = Path(folder) / 'adc.inc'
            helper.write_text(core.EXTRA['neo98_adc.inc'].read_text() + '\nstatic unsigned probe;\n')
            with patch.dict(core.EXTRA, {'neo98_adc.inc': helper}):
                self.assertNotEqual(core.state_identity(), REVIEWED)

    def test_a_native_field_in_the_bios_patch_changes_the_key(self):
        original = core.PATCHES['src/ints/bios.cpp']
        changed = lambda text: original(text) + '\nstruct Neo98BiosProbe { unsigned field; };\n'
        with patch.dict(core.PATCHES, {'src/ints/bios.cpp': changed}):
            self.assertNotEqual(core.state_identity(), REVIEWED)

    def test_a_native_field_in_the_vbe_patch_changes_the_key(self):
        original = core.PATCHES['src/ints/int10_vesa.cpp']
        changed = lambda text: original(text) + '\nstruct Neo98VbeProbe { unsigned field; };\n'
        with patch.dict(core.PATCHES, {'src/ints/int10_vesa.cpp': changed}):
            self.assertNotEqual(core.state_identity(), REVIEWED)

    def test_a_console_field_beyond_the_resume_patch_changes_the_key(self):
        original = core.PATCHES['src/dos/dev_con.h']
        changed = lambda text: original(text).replace('class device_CON : public DOS_Device {',
                                  'class device_CON : public DOS_Device {\nunsigned neo98_probe;')
        with patch.dict(core.PATCHES, {'src/dos/dev_con.h': changed}):
            self.assertNotEqual(core.state_identity(), REVIEWED)

    def test_an_unreviewed_resume_helper_field_changes_the_key(self):
        with tempfile.TemporaryDirectory() as folder:
            helper = Path(folder) / 'resume.h'
            helper.write_text(core.EXTRA['neo98_resume.h'].read_text().replace('unsigned tag;', 'unsigned tag; unsigned probe;'))
            with patch.dict(core.EXTRA, {'neo98_resume.h': helper}):
                self.assertNotEqual(core.state_identity(), REVIEWED)

    def test_an_unreviewed_cpu_loop_change_changes_the_key(self):
        original = core.PATCHES['src/dosbox.cpp']
        changed = lambda text: original(text) + '\nstatic unsigned neo98_cpu_loop_probe;\n'
        with patch.dict(core.PATCHES, {'src/dosbox.cpp': changed}):
            self.assertNotEqual(core.state_identity(), REVIEWED)

    def test_an_unreviewed_paging_field_changes_the_key(self):
        original = core.PATCHES['src/cpu/paging.cpp']
        changed = lambda text: original(text) + '\nstatic unsigned neo98_paging_probe;\n'
        with patch.dict(core.PATCHES, {'src/cpu/paging.cpp': changed}):
            self.assertNotEqual(core.state_identity(), REVIEWED)

    def test_an_unreviewed_dynrec_field_changes_the_key(self):
        original = core.PATCHES['src/cpu/core_dynrec.cpp']
        changed = lambda text: original(text) + '\nstatic unsigned neo98_dynrec_probe;\n'
        with patch.dict(core.PATCHES, {'src/cpu/core_dynrec.cpp': changed}):
            self.assertNotEqual(core.state_identity(), REVIEWED)

    def test_an_unreviewed_unwind_helper_changes_the_key(self):
        with tempfile.TemporaryDirectory() as folder:
            helper = Path(folder) / 'unwind.h'
            helper.write_text(core.EXTRA['neo98_dynrec_unwind.h'].read_text() + '\nstatic unsigned probe;\n')
            with patch.dict(core.EXTRA, {'neo98_dynrec_unwind.h': helper}):
                self.assertNotEqual(core.state_identity(), REVIEWED)

    def test_changed_generated_frame_requires_an_unwind_review(self):
        name = 'src/cpu/core_dynrec/risc_armv8le.h'
        original = core.PATCHES[name]
        changed = lambda text: original(text).replace('cache_addd( 0xa9bd7bfd )', 'cache_addd( 0xa9bc7bfd )')
        with patch.dict(core.PATCHES, {name: changed}):
            with self.assertRaisesRegex(AssertionError, 'unwind ABI changed'):
                core.patched_sources()

    def test_an_added_generated_frame_instruction_requires_an_unwind_review(self):
        name = 'src/cpu/core_dynrec/risc_armv8le.h'
        original = core.PATCHES[name]
        changed = lambda text: original(text).replace('static void gen_return_function(void) {',
            'static void gen_return_function(void) {\ncache_addd(0xd10043ff);')
        with patch.dict(core.PATCHES, {name: changed}):
            with self.assertRaisesRegex(AssertionError, 'unwind ABI changed'):
                core.patched_sources()


if __name__ == '__main__':
    unittest.main()
