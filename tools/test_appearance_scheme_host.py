#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Display Properties of Windows 98 applies a scheme and keeps its fonts.

The page Appearance applies a scheme with SystemParametersInfo:
SPI_SETNONCLIENTMETRICS with the five fonts of the window frame, and the
icon font. USER stores each font of these structures under
HKCU\\Control Panel\\Desktop\\WindowMetrics as the fifty bytes of a LOGFONT16
with the height in points, through a second writer that narrows the LOGFONT
of 32 bits first; the port read the face of the wide font where the narrow
one has it and wrote empty names. (The test first looked for the faces in
[WindowMetrics] of WIN.INI, where the port kept them then; Windows keeps
nothing of this in WIN.INI.)

On neo98-kms over a private copy of drive C:, with the picture of a 1024x768
desktop in memory: Display Properties opens on Appearance, Down chooses the
scheme after "Windows Standard", Apply applies it. Then the registry has a
face and a height in points for every font of the frame and for IconFont.
The steps wait for the sheet and for a quiet machine, not for a time.

    build/analysis-venv/bin/python tools/test_appearance_scheme_host.py [--no-build] [--keep] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
import argparse
import os
import shutil
import struct
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
# HKEY_CURRENT_USER is the key of the user under HKEY_USERS.
METRICS = '\\Control Panel\\Desktop\\WindowMetrics'


def stored(journal, wanted):
    """The values the journal of the registry sets under a key that ends in `wanted`,
    the last of each name (src/registry_persist.cpp: a record is its kind,
    the path, for a value its name, and for a set value type, size and data)."""
    data = journal.read_bytes() if journal.is_file() else b''
    values, at = {}, 9

    def text():
        nonlocal at
        length = struct.unpack_from('<H', data, at)[0]
        at += 2 + length
        return data[at - length:at].decode('cp1252')

    while at < len(data):
        kind = data[at]
        at += 1
        path = text()
        name = text() if kind >= 3 else ''
        if kind == 3:
            _, size = struct.unpack_from('<II', data, at)
            at += 8 + size
            if path.lower().endswith(wanted.lower()):
                values[name.lower()] = data[at - size:at]
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
    # Each step waits for the sheet or for a quiet machine: the scheme is
    # chosen, then applied, which broadcasts the new metrics to every window.
    steps = ['0:wait=#32770|Display Properties', '0:idle=1000', '0.3:key=28', '0.3:idle=1000',
             '0.3:shot={out}/scheme.png', f'0.6:click={APPLY[0]},{APPLY[1]}', '0.6:idle=2000',
             '0.6:shot={out}/applied.png', '0.9:quit=1']
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps).replace('{out}', str(WORK)))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    try:
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '1024x768',
                                 '--exe', 'C:\\WINDOWS\\RUNDLL32.EXE',
                                 '--command', 'rundll32.exe shell32.dll,Control_RunDLL desk.cpl,,2',
                                 '--time-limit', '120'], cwd=ROOT, env=env, capture_output=True, text=True,
                                timeout=180)
        metrics = stored(win98_c.journal(drive), METRICS)
    finally:
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
    log = result.stdout + result.stderr
    (WORK / 'log.txt').write_text(log)
    stops = [line for line in log.splitlines()
             if 'no native provider' in line or 'CRASHREPORT' in line or 'not ported' in line]

    def font(name):
        """(points, face) of a stored LOGFONT16, or None."""
        value = metrics.get(name.lower())
        if not value or len(value) != 50:
            return None
        return struct.unpack_from('<h', value, 0)[0], value[18:].split(b'\0')[0].decode('cp1252')

    faces = {name: font(name) for name in FONTS}
    timed_out = [line for line in log.splitlines() if 'timed out after' in line]
    checks = {
        f'every wait came, and the run ended before the time limit: {timed_out} {result.returncode}':
            not timed_out and result.returncode == 0,
        'Display Properties applies the scheme without a stop':
            not stops and (WORK / 'applied.png').is_file() and bool(metrics),
        f'WindowMetrics has every font with its face and its height in points: {faces}':
            all(entry and 4 <= entry[0] <= 72 and entry[1][:1].isalpha() for entry in faces.values()),
        f'WindowMetrics has the icon font: {font("IconFont")!r}':
            bool(font('IconFont')) and font('IconFont')[1][:1].isalpha(),
        f'the sizes are stored in twips: CaptionHeight {metrics.get("captionheight")!r}':
            metrics.get('captionheight', b'')[:1] == b'-',
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
