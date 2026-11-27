#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The sizes of an MS-DOS window: the TrueType face Windows has, and Auto in the work area.

PIFMGR.DLL of Windows 95 and of Windows 98 is the same code here: the TrueType
face of MS-DOS windows is "Courier New" unless Windows has its extension
(COOL.DLL, which Plus! brought to Windows 95 and Windows 98 has), which
replaces the name with the value Font of ...\\CurrentVersion\\MS-DOS Emulation
in the registry, "Lucida Console" on Windows 98. The cell heights are those
of TTInitialSizes in SYSTEM.INI or the eight PIFMGR carries, and the cell of
each is the one Windows measured and keeps in [TTFontDimenCache].

The MS-DOS window drew every TrueType size with LUCON.TTF and listed
sixteen sizes with the widths of that face. A Windows without that file (a
Windows 95 without Plus!) was offered sizes it could not draw: "9 x 15" came
out in the raster size of the nearest height, 12 x 16. And "Auto" took the
largest size that fits the screen less 28 pixels for a taskbar, whatever
the work area was.

tools/dos_font_fixture.c on neo98-kms over a private copy of drive C:
starts the MS-DOS prompt, reads the sizes of the box in its toolbar, chooses
the TrueType size 15 pixels high, then Auto, then Auto in a work area 300
pixels high. Twice:

  with Windows 98 as installed: sixteen TrueType sizes with the cells of
  [TTFontDimenCache], 9 x 15 among them and 720 pixels wide when chosen,
  drawn with Lucida Console
  as a Windows 95 without Plus! has it (no COOL.DLL, no TTInitialSizes, and
  the cells that Windows measures with Courier New): eight sizes, 7 x 15
  among them and 560 pixels wide when chosen, drawn with Courier New

and both times Auto is the largest raster size whose window fits the work
area, in the one of the screen and in the smaller one.

    build/analysis-venv/bin/python tools/test_dos_font_host.py [--no-build] [--keep] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
if __name__ == '__main__':
    import shutil as _shutil
    if not _shutil.which('i686-w64-mingw32-gcc'):  # the fixture is built with it; docs/BUILDING.md
        print('SKIP: needs i686-w64-mingw32-gcc')
        _sys.exit(77)
import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/dos-font'
# What a Windows 95 measures for the eight heights with Courier New and
# writes into SYSTEM.INI.
COURIER = {12: (5, 12), 13: (6, 12), 14: (7, 14), 15: (7, 15), 16: (8, 16), 18: (10, 18), 20: (10, 20), 22: (12, 22)}


def section(text, name):
    found = re.search(r'(?im)^\[' + re.escape(name) + r'\]\s*$(.*?)(?=^\[|\Z)', text, re.S)
    return dict(line.split('=', 1) for line in found.group(1).splitlines() if '=' in line) if found else {}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--keep', action='store_true', help='keep the copy of the drive')
    parser.add_argument('--kms', type=Path, default=BINARY)
    args = parser.parse_args()
    if not native_app.native_x64():
        raise SystemExit('neo98-kms is a Linux x64 host.')
    if not args.no_build:
        native_app.build_kms_host(vulkan=False)
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    drive = WORK / 'c'
    pristine = win98_c.use_pristine()
    subprocess.run(['cp', '-a', '--reflink=auto', str(pristine), str(drive)], check=True)
    if win98_c.journal(pristine).is_file():
        shutil.copy2(win98_c.journal(pristine), win98_c.journal(drive))
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/dos_font_fixture.c'), '-nostartfiles',
                    '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32',
                    '-o', str(drive / 'DOSFONT.EXE')], check=True)
    system_ini = win98_c.path('C:\\WINDOWS\\SYSTEM.INI', drive)
    logs = []

    def session(name):
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:800x600')
        for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD', 'NEO98_HOST_INPUT'):
            env.pop(variable, None)
        written = win98_c.path('C:\\DOSFONT.TXT', drive)
        if written:
            written.unlink()
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '800x600',
                                 '--exe', 'C:\\DOSFONT.EXE', '--time-limit', '150'], cwd=ROOT, env=env,
                                capture_output=True, text=True, timeout=200)
        logs.append(f'---- {name}: {result.returncode}\n{result.stdout}{result.stderr}')
        written = win98_c.path('C:\\DOSFONT.TXT', drive)
        found = {}
        for line in (written.read_text().splitlines() if written else ()):
            key, _, value = line.partition('=')
            found[key] = int(value)
        face = re.findall(r'DOSVM truetype face="([^"]*)" ready=(\d)', result.stdout + result.stderr)
        sizes = [found.get(f'size {i}', 0) for i in range(found.get('sizes', 0))]
        raster = [(s & 0xff, s >> 8 & 0x7f) for s in sizes if s != 0xffff and not s & 0x8000]
        truetype = [(s & 0xff, s >> 8 & 0x7f) for s in sizes if s != 0xffff and s & 0x8000]
        return found, face[-1] if face else ('', '0'), raster, truetype

    def checks_of(name, found, face, raster, truetype, cells, family, chosen):
        strip = found.get('truetype height', 0) - 25 * 15
        frame = (found.get('frame width', 0), found.get('frame height', 0))

        def auto(work_height):
            fit = [(w, h) for w, h in raster if 80 * w <= found.get('work width', 0) - frame[0]
                   and 25 * h + strip <= work_height - frame[1]]
            w, h = max(fit, default=(0, 0))
            return 80 * w, 25 * h + strip
        return {
            f'{name}: the MS-DOS window has its size box, with Auto first and raster sizes ({len(raster)})':
                found.get('box') == 1 and found.get('size 0') == 0xffff and len(raster) >= 4,
            f'{name}: the TrueType sizes are the cells Windows keeps for its heights: {truetype}':
                truetype == cells,
            f'{name}: the size 15 high is {chosen[0]} x 15 and its picture {80 * chosen[0]} pixels wide, 25 rows high '
            f'above a strip: {found.get("truetype width")} x {found.get("truetype height")}':
                chosen in truetype and found.get('truetype width') == 80 * chosen[0] and 0 <= strip <= 40,
            f'{name}: it is drawn with {family}: {face}': face == (family, '1'),
            f'{name}: Auto is the largest raster size that fits the work area of '
            f'{found.get("work width")} x {found.get("work height")}: '
            f'{found.get("auto width")} x {found.get("auto height")}, expected {auto(found.get("work height", 0))}':
                (found.get('auto width'), found.get('auto height')) == auto(found.get('work height', 0)) != (0, strip),
            f'{name}: and in a work area 300 pixels high: {found.get("auto small width")} x '
            f'{found.get("auto small height")}, expected {auto(300)}':
                found.get('work area set') == 1
                and (found.get('auto small width'), found.get('auto small height')) == auto(300) != (0, strip)
                and auto(300) != auto(found.get('work height', 0)),
        }

    checks = {}
    try:
        text = system_ini.read_text(errors='replace')
        heights = [int(h) for h in section(text, 'NonWindowsApp').get('TTInitialSizes', '').split()]
        cache = {int(key.split()[1]): tuple(int(v) for v in value.split())
                 for key, value in section(text, 'TTFontDimenCache').items()}
        cells = []
        for height in heights:
            cell = cache.get(height, ((height * 3 + 2) // 5, height))
            if cell not in cells:
                cells.append(cell)
        checks[f'Windows 98 keeps sixteen TrueType heights in SYSTEM.INI, 9 x 15 among their cells: {cells}'] = \
            len(heights) == 16 and (9, 15) in cells
        checks.update(checks_of('Windows 98', *session('windows 98'), cells, 'Lucida Console', (9, 15)))
        # As a Windows 95 without Plus! has it.
        for extension in ('C:\\WINDOWS\\SYSTEM\\COOL.DLL', 'C:\\WINDOWS\\COOL.DLL'):
            found = win98_c.path(extension, drive)
            if found:
                found.unlink()
        text = re.sub(r'(?im)^TTInitialSizes=.*\r?\n', '', text)
        text = re.sub(r'(?ims)^\[TTFontDimenCache\]\s*$.*?(?=^\[|\Z)', '', text)
        text += '\r\n[TTFontDimenCache]\r\n' + ''.join(f'0 {h}={w} {high}\r\n' for h, (w, high) in COURIER.items())
        system_ini.write_bytes(text.encode('latin-1'))
        cells = []
        for cell in COURIER.values():
            if cell not in cells:
                cells.append(cell)
        checks.update(checks_of('without the extension', *session('no extension'), cells, 'Courier New', (7, 15)))
    finally:
        (WORK / 'log.txt').write_text('\n'.join(logs))
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
            Path(str(drive) + '.clock').unlink(missing_ok=True)
    stops = [line for line in '\n'.join(logs).splitlines()
             if 'no native provider' in line or 'CRASHREPORT' in line or 'not ported' in line]
    checks[f'no process stopped at something missing: {stops[:1]}'] = not stops
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'dos font host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
