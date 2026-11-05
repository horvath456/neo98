#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Print the Windows 98 printer test page through the native host.

Install Apple LaserWriter Plus on LPT1 in a private copy of the setup drive.
Invoke RUNDLL_PrintTestPage from the imported MSPRINT2.DLL. Verify the success
dialog, a one-page PostScript job, expected text and logo polygons. When
Ghostscript is available, also check the PDF text and rendered logo.

    python3 tools/test_print_test_page_host.py [--no-build] [--keep]
"""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import print_pdf  # noqa: E402
import win98_c  # noqa: E402
from test_device_manager_host import failures, host  # noqa: E402

PRINTER = 'Apple LaserWriter Plus'
COMMAND = 'RUNDLL32.EXE MSPRINT2.DLL,RUNDLL_PrintTestPage ' + PRINTER
# Lines of the page (MSPRINT2 strings 3209 to 3230) as they come back from the PDF.
TEXT = ['Windows 98', 'Printer Test Page', 'Congratulations!',
        'If you can read this information, you have correctly installed your',
        'Printer name:', PRINTER, 'Port name:', 'LPT1:', 'Driver name:', 'PSCRIPT.DRV',
        'Data file:', 'APPLE380.SPD', 'Files used by this driver:', 'This is the end of the printer test page.']
FAILURE = 'Windows was unable to print the test page'
COMPLETED = 'Printer test page completed!'


def logo_pixels(pdf, scratch):
    """Dark pixels of the rendered page at 20 dpi where the logo stands: one inch from the
    top left corner of the printable area, one inch wide. The header text "Windows 98" begins
    right of it (twice LOGPIXELSX), so a page with text only has none."""
    image = Path(scratch) / 'page.pgm'
    subprocess.run([print_pdf.ghostscript(), '-q', '-dSAFER', '-dBATCH', '-dNOPAUSE', '-sDEVICE=pgmraw', '-r20',
                    '-sOutputFile=' + str(image), str(pdf)], check=True, capture_output=True)
    data = image.read_bytes()
    header = re.match(rb'P5\s+(?:#[^\n]*\n\s*)*(\d+)\s+(\d+)\s+\d+\s', data)
    width, height = int(header[1]), int(header[2])
    body = data[header.end():]
    return sum(1 for y in range(height // 6) for x in range(width // 5) if body[y * width + x] < 128)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n', 1)[0])
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--keep', action='store_true', help='keep the run folder with its clone of C:')
    arguments = parser.parse_args()
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('print-test-page-host')
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host' if arguments.no_build else build_host()
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)
        print(('ok  ' if condition else 'FAIL'), name, flush=True)

    def prepare(run):
        from test_support import require_printer_inputs
        require_printer_inputs()
        subprocess.run([sys.executable, str(ROOT / 'tools/install_printer.py'), '--drive-c', str(run / 'c'),
                        '--registry', str(run / 'c.registry'), '--tree', str(run / 'c')], check=True,
                       stdout=subprocess.DEVNULL)

    # The test page prints at once; the answer is the dialog or the message box after it.
    steps = ['0:wait=#32770|' + PRINTER, '6.0:tops=2', '6.5:button=Yes', '9.0:tops=1']
    code, text, run = host(binary, out, 'test-page', 'C:\\WINDOWS\\RUNDLL32.EXE', COMMAND, steps, 60, pristine,
                           win98_c.journal(pristine), prepare=prepare, extra=['--printer-output', '{out}/printed'],
                           keep=arguments.keep)
    shown = re.findall(r'^NEO98 HOST (?:top|child) id=\w+ visible=1 .*? title=(.*)$', text, re.M)
    boxes = re.findall(r'^MessageBox caption=.*? text=(.*)$', text, re.M)
    check('the test page ends in "Printer test page completed!"', COMPLETED in shown)
    check('no "unable to print the test page"', not any(FAILURE in line for line in shown + boxes))
    check('no crash or call the host does not carry', not failures(text) and 'CRASHREPORT' not in text)
    jobs = re.findall(r'PRINTER_JOB bytes=(\d+) file=(\S+) pdf=(\S+) tool=(\S+)', text)
    check('one job reaches LPT1', len(jobs) == 1 and Path(jobs[0][1]).is_file())
    if jobs and Path(jobs[0][1]).is_file():
        job = Path(jobs[0][1]).read_bytes().decode('latin-1')
        page = job.split('%%EndPageSetup', 1)[-1].split('%%PageTrailer', 1)[0]
        check('the job is PostScript of one page', job.lstrip('\x04').startswith('%!PS-Adobe')
              and re.findall(r'^%%Pages: (\d+)', job, re.M) == ['1'])
        # PSCRIPT sets a text run as "A" and a polygon as "x y N M dx dy -... C" with
        # its fill; an empty page had neither.
        shown_text = re.findall(r'\(((?:[^()\\]|\\.)*)\)A', page)
        check('the page carries the text of the test page', len(shown_text) >= 15
              and 'Congratulations!' in shown_text and 'This is the end of the printer test page.' in shown_text)
        check('the page carries the logo\'s polygons', page.count(' N M ') > 100)
        if jobs[0][2] != '-' and print_pdf.ghostscript():
            pages = print_pdf.pages(jobs[0][2], run / 'pdf-text')
            flat = ' '.join(' '.join(pages).split())
            missing = [line for line in TEXT if line not in flat]
            check('the PDF has one page with the text of the test page', len(pages) == 1 and not missing)
            if missing:
                print('    missing:', missing)
            logo = logo_pixels(jobs[0][2], run)
            check('the PDF page shows the logo at the top left', logo > 60)
            print(f'    dark pixels where the logo stands: {logo}')
            print('    PDF', jobs[0][2])
        else:
            print('skip the PDF: Ghostscript is not installed')
    print(f'{sum(checks.values())}/{len(checks)} checks pass, log {run / "log.txt"}')
    if not arguments.keep:
        shutil.rmtree(run / 'c', ignore_errors=True)
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
