#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Play both original Half-Life intros without Escape and measure frame pacing.

An initialized installation and registry are cloned privately. Screenshots are
matched against ffmpeg-decoded source frames, so advancing audio/MCI position
alone cannot pass. Every changed host frame is captured to detect stalls that
average progress misses. License values are neither inspected nor printed.
"""
import argparse
from test_support import ExternalInputParser
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess

from PIL import Image, ImageChops, ImageStat

ROOT = Path(__file__).resolve().parents[1]
SIZE = (64, 48)


def reference(ffmpeg, path):
    data = subprocess.check_output([ffmpeg, '-v', 'error', '-i', str(path),
        '-vf', 'scale=64:48', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-an', '-'])
    stride = SIZE[0] * SIZE[1] * 3
    return [Image.frombytes('RGB', SIZE, data[i:i + stride]) for i in range(0, len(data), stride)]


def match(path, frames):
    picture = Image.open(path).convert('RGB').resize(SIZE)
    differences = [sum(ImageStat.Stat(ImageChops.difference(picture, frame)).mean) / 3
                   for frame in frames]
    best = min(range(len(frames)), key=differences.__getitem__)
    return {'frame': best, 'rgb_error': round(differences[best], 3)}


def frame_pacing(log, frames, start=4, end=16):
    """Measure visible movie changes, not timer ticks or repeated presents.

    Sierra moves throughout this interval. Exclude its static opening/closing
    images, and deduplicate source frames so a blinking cursor cannot pass.
    """
    seen = []
    for timestamp, path in re.findall(r'^FRAMELOG t=([\d.]+) frame=\d+ file=(.+)$', log, re.M):
        t = float(timestamp)
        if not start <= t <= end:
            continue
        picture = match(Path(path), frames)
        assert picture['rgb_error'] < 5, (t, picture)
        if not seen or picture['frame'] != seen[-1]['frame']:
            seen.append({'time': t, **picture})
    assert seen, 'No changed host frames captured during the Sierra intro'
    # Include both boundaries so a freeze at the start/end cannot disappear.
    times = [start] + [item['time'] for item in seen] + [end]
    gaps = sorted(b - a for a, b in zip(times, times[1:]))
    return {'start': start, 'end': end, 'changed_frames': len(seen),
            'unique_source_frames': len({item['frame'] for item in seen}),
            'median_gap_ms': round(statistics.median(gaps) * 1000, 3),
            'p95_gap_ms': round(gaps[int((len(gaps) - 1) * .95)] * 1000, 3),
            'max_gap_ms': round(max(gaps) * 1000, 3), 'frames': seen}


def check_pacing(pacing):
    # At 15 fps this 12-second moving passage has about 180 frames. Allow
    # scheduling jitter on a shared host, but reject repeated half-second holds.
    summary = {key: value for key, value in pacing.items() if key != 'frames'}
    # Adjacent source frames can match out of order near the fade. Require
    # distinct source pictures as well, so repeated matches do not inflate it.
    assert pacing['unique_source_frames'] >= 150 and pacing['max_gap_ms'] <= 250, summary


def main():
    parser = ExternalInputParser(description=__doc__)
    parser.add_argument('--drive-c', required=True, type=Path)
    parser.add_argument('--registry', required=True, type=Path)
    parser.add_argument('--host', type=Path, default=ROOT / 'build/native-app/neo98-host')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/half-life-intro-test')
    parser.add_argument('--audio', choices=['dummy', 'coreaudio'], default='dummy')
    args = parser.parse_args()
    ffmpeg = shutil.which('ffmpeg') or '/opt/homebrew/bin/ffmpeg'
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out / 'results.json').unlink(missing_ok=True)
    (out / 'pacing.json').unlink(missing_ok=True)
    for previous in out.glob('frame-*.ppm'):
        previous.unlink()
    presentation_frames = out / 'presented'
    presentation_frames.mkdir(exist_ok=True)
    for previous in presentation_frames.glob('f*.ppm'):
        previous.unlink()
    drive, registry = out / 'c', out / 'c.registry'
    assert args.drive_c.resolve() != drive and args.registry.resolve() != registry
    shutil.rmtree(drive, ignore_errors=True)
    subprocess.run(['cp', '-cR', str(args.drive_c.resolve()), str(drive)], check=True)
    shutil.copyfile(args.registry, registry)
    samples = [4, 6, 8, 10, 12, 14, 16]
    steps = [f'{t}:shot={out}/frame-{t}.ppm' for t in samples + [20, 22, 34]] + ['35:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER=args.audio,
               NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_DEADLINE='45', NEO98_HOST_SCREEN='1024x768',
               NEO98_FRAME_LOG=str(presentation_frames),
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_TRACE_CALLS_FROM',
                'NEO98_TRACE_PAINT', 'NEO98_FEX_DEBUG', 'NEO98_FEX_DUMP_CODE', 'NEO98_PROFILE_MOVIE'):
        env.pop(key, None)
    try:
        with (out / 'host.log').open('w') as log:
            result = subprocess.run([str(args.host.resolve()), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(registry),
                '--exe', r'C:\SIERRA\Half-Life\hl.exe', '--directory', r'C:\SIERRA\Half-Life',
                '--quiet', '--crash-report-dir', str(out / 'reports')],
                env=env, stdout=log, stderr=subprocess.STDOUT, timeout=60)
        log = (out / 'host.log').read_text(errors='replace')
        assert result.returncode == 0 and 'NEO98 HOST failed' not in log, log[-2000:]
        media = drive / 'SIERRA/Half-Life/valve/media'
        frames = reference(ffmpeg, media / 'sierra.avi')
        pacing = frame_pacing(log, frames)
        (out / 'pacing.json').write_text(json.dumps(pacing, indent=2) + '\n')
        check_pacing(pacing)
        seen = {t: match(out / f'frame-{t}.ppm', frames) for t in samples}
        assert all(v['rgb_error'] < 5 for v in seen.values()), seen
        rates = [(seen[b]['frame'] - seen[a]['frame']) / (b - a) for a, b in zip(samples, samples[1:])]
        average = (seen[samples[-1]]['frame'] - seen[samples[0]]['frame']) / (samples[-1] - samples[0])
        # The host is shared with other apps. Allow a short scheduling stall,
        # while requiring near-real-time progress across the whole sample and
        # rejecting the old multi-second freezes in every two-second window.
        assert 13 <= average <= 16 and all(10 <= rate <= 17 for rate in rates), (average, rates, seen)
        valve = reference(ffmpeg, media / 'valve.avi')
        valve_seen = {t: match(out / f'frame-{t}.ppm', valve) for t in (20, 22)}
        assert all(v['rgb_error'] < 5 for v in valve_seen.values()), valve_seen
        assert 24 <= valve_seen[22]['frame'] - valve_seen[20]['frame'] <= 34, valve_seen
        menu = Image.open(out / 'frame-34.ppm').convert('RGB').resize((640, 480))
        orange = sum(n for n, (r, g, b) in menu.crop((50, 170, 230, 455)).getcolors(640 * 480)
                     if r > 150 and g > 70 and b < 100)
        assert orange > 1000, ('main menu did not appear without Escape', orange)
        for t, name in ((8, 'sierra'), (22, 'valve'), (34, 'menu')):
            Image.open(out / f'frame-{t}.ppm').save(out / (name + '.png'))
        report = {'status': 'PASS', 'host': str(args.host.resolve()), 'audio': args.audio,
                  'sierra_pacing': {key: value for key, value in pacing.items() if key != 'frames'},
                  'sierra_frames': seen, 'sierra_progress_fps': rates, 'sierra_average_progress_fps': average,
                  'valve_frames': valve_seen,
                  'menu_without_escape': True,
                  'limits': ['Frame changes measured at the host presentation boundary with SDL dummy video.',
                             'Physical monitor scanout and gameplay frame pacing not measured.',
                             'Audio quality and lip synchronization not assessed.']}
        (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
        print('HALF-LIFE INTRO PASS:', pacing['changed_frames'], 'Sierra frame changes in 12 seconds;',
              'maximum gap', pacing['max_gap_ms'], 'ms; Valve and main menu without Escape')
    finally:
        shutil.rmtree(drive, ignore_errors=True)
        registry.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
