#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run WMF checks on both x86 engines; --full also checks EMF playback.

Build the host first. Run under the shared test lock when using a worktree.
"""
if __name__ == '__main__':
    import sys as _sys
    if _sys.platform != 'darwin':
        print('SKIP: compares the FEX and Unicorn engines of macOS')  # macOS only; skipped elsewhere
        _sys.exit(77)
import argparse
import os
import re
import arm64_run
from ui_clipboard_test_support import ROOT, fixture, machine, host
from gdi_metafile_components import IMPORTS


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--full', action='store_true', help='also run the EMF checks')
    parser.add_argument('--scaled-text', action='store_true', help='compatibility option; --full always checks scaled text')
    args = parser.parse_args()
    out = ROOT / 'build/metafile-host'
    exe = fixture(out / 'fixture', ROOT / 'tools/gdi_metafile_fixture.c', IMPORTS,
                  defines=['-DGDI_METAFILE_STANDALONE', *([] if args.full else ['-DGDI_METAFILE_WMF_ONLY'])])
    for cpu in ('fex', 'unicorn'):
        os.environ['NEO98_CPU'] = cpu
        with machine(out / cpu) as drive:
            arm64_run.ensure_idle()
            arm64_run.sync()
            log = host(out / cpu, drive, r'E:\TEST.EXE', seconds=15,
                       extra=['--drive-readonly', 'E=' + str(exe.parent)])
            assert re.search(r'NEO98 HOST exited code=0\b', log), log[-3000:]
        print(f'PASS {cpu}: metafile recording, enumeration, deletion in callback and playback')


if __name__ == '__main__':
    main()
