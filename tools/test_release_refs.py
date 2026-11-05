#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Release metadata and setup messages, using only temporary fixtures."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import sys
import types
with patch.dict(sys.modules, {'smoke_matrix': types.ModuleType('smoke_matrix')}):
    import import_census
import setup_win98


class ReleaseRefsTest(unittest.TestCase):
    def test_census_preserves_worklist_in_output_report(self):
        with tempfile.TemporaryDirectory() as work:
            document = Path(work) / 'census.md'
            self.assertEqual(import_census.worklist(document), [])
            document.write_text(import_census.WORKLIST_BEGIN + '\nKeep this item.\n' + import_census.WORKLIST_END)
            self.assertIn('Keep this item.', import_census.worklist(document))

    def test_setup_time_zone_message(self):
        with tempfile.TemporaryDirectory() as work:
            folder = Path(work)
            (folder / setup_win98.win98_c.MARKER).write_text(json.dumps({'cabs': setup_win98.win98_cabs.VERSION}))
            output = io.StringIO()
            with patch.object(setup_win98.win98_time_zone, 'apply', return_value=True), \
                 patch.object(setup_win98.win98_glide_driver, 'apply', return_value=False), \
                 patch.object(setup_win98.win98_c, 'pristine', return_value=folder), \
                 contextlib.redirect_stdout(output):
                setup_win98.complete(folder)
            self.assertIn('setup: time zone ', output.getvalue())
            self.assertIn(' added (', output.getvalue())


if __name__ == '__main__':
    unittest.main()
