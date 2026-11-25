#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Display Properties of Windows 98 applies a scheme and keeps its fonts.

The page Appearance applies a scheme with SystemParametersInfo:
SPI_SETNONCLIENTMETRICS with the five fonts of the window frame, and the
icon font. USER writes each font of these structures into WIN.INI with a
second writer that narrows the LOGFONT of 32 bits first; the port read the
face of the wide font where the narrow one has it and wrote empty names:
"CaptionFont=" and the others in [WindowMetrics].

On neo98-kms over a private copy of drive C:, with the picture of a 1024x768
desktop in memory: Display Properties opens on Appearance, Down chooses the
scheme after "Windows Standard", Apply applies it. Then WIN.INI names a face
for every font of [WindowMetrics] and for IconFont of [Desktop].

    build/analysis-venv/bin/python tools/test_appearance_scheme_host.py [--no-build] [--keep] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/appearance-scheme'
APPLY = (421, 481)
FONTS = ('CaptionFont', 'SmCaptionFont', 'MenuFont', 'StatusFont', 'MessageFont')


def section(text, name):
    """The keys of one section of an INI file, the first of each name."""
    values, inside = {}, False
    for line in text.splitlines():
        line = line.strip()
        if line.startswith('['):
            inside = line[1:].split(']')[0].lower() == name.lower()
        elif inside and '=' in line:
            key, value = line.split('=', 1)
            values.setdefault(key.strip().lower(), value.strip())
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--keep', action='store_true', help='keep the copy of the drive')
    parser.add_argument('--kms', type=Path, default=BINARY)
    args = parser.parse_args()
    if not native_app.native_x64():
        raise SystemExit('neo98-kms is a Linux x64 host.')
    if not args.no_build:
        native_app.build_kms_host()
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    drive = WORK / 'c'
    pristine = win98_c.use_pristine()
    subprocess.run(['cp', '-a', '--reflink=auto', str(pristine), str(drive)], check=True)
    if win98_c.journal(pristine).is_file():
        shutil.copy2(win98_c.journal(pristine), win98_c.journal(drive))
    steps = ['0:wait=#32770', '3:key=28', '5:shot={out}/scheme.png', f'6:click={APPLY[0]},{APPLY[1]}',
             '14:shot={out}/applied.png', '15:quit=1']
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps).replace('{out}', str(WORK)))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    try:
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '1024x768',
                                 '--exe', 'C:\\WINDOWS\\RUNDLL32.EXE',
                                 '--command', 'rundll32.exe shell32.dll,Control_RunDLL desk.cpl,,2',
                                 '--time-limit', '40'], cwd=ROOT, env=env, capture_output=True, text=True,
                                timeout=130)
        ini = win98_c.path('C:\\WINDOWS\\WIN.INI', drive)
        text = ini.read_text(errors='replace') if ini else ''
    finally:
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
    log = result.stdout + result.stderr
    (WORK / 'log.txt').write_text(log)
    stops = [line for line in log.splitlines()
             if 'no native provider' in line or 'CRASHREPORT' in line or 'not ported' in line]
    metrics, desktop = section(text, 'WindowMetrics'), section(text, 'Desktop')
    faces = {name: metrics.get(name.lower()) for name in FONTS}
    checks = {
        'Display Properties applies the scheme without a stop':
            not stops and (WORK / 'applied.png').is_file() and bool(metrics),
        f'WIN.INI [WindowMetrics] names the face of every font: {faces}':
            all(face and face[0].isascii() and face[0].isalpha() for face in faces.values()),
        f'WIN.INI [Desktop] names the icon font: {desktop.get("iconfont")!r}': bool(desktop.get('iconfont')),
    }
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    if stops:
        print('    stop:', stops[0])
    print(f'Pictures and log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'appearance scheme host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
