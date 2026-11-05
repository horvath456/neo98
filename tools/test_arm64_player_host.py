#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The sample player (sdk/samples/player) as an ARM64 program in the host.

Without a screen (SDL's dummy video driver) and without sound (its dummy
audio driver). ffmpeg writes two short films, a Cinepak AVI with PCM sound and
an MPEG-1 program stream with layer II sound, and decodes them itself as the
reference. In a clone of the pristine drive C: the player then

- decodes the AVI without a window (/bench): every frame has the pixels ffmpeg
  decodes, the sound is the same bytes, a seek lands on a key frame;
- does the same built without NEON: the same pixels;
- decodes the MPEG-1 film: the frames are within a few steps of ffmpeg's (the
  two decoders round differently), the sound within a few steps too;
- plays both films once in its window (/play): every frame is decoded in
  about the time the film lasts, the sound device took the blocks.

The test is skipped when ffmpeg is not installed.

    build/analysis-venv/bin/python tools/test_arm64_player_host.py [--no-build]

Results in build/arm64-player-host/results.json. docs/ARM64-APPS.md.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import time
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
import win98_c  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import arm64_run  # noqa: E402
import arm64_sdk  # noqa: E402
import sdk_dependencies  # noqa: E402

PLAYER = ROOT / 'sdk/samples/player'
# The decoders are part of the SDK library (sdk/lib: movie.h); the sample is the window around them.
SOURCES = ['player.c']


def build_player(output, extra=()):
    """PLAYER.EXE from sdk/samples/player; pl_mpeg comes from third_party."""
    sources = [PLAYER / name for name in SOURCES]
    flags = list(extra)
    if extra:
        # A variant of the decoders (without NEON): compiled with the program
        # in place of the ones of the library.
        sources += [ROOT / 'sdk/lib' / name for name in ('movie.c', 'avi.c', 'mpeg.c', 'cinepak.c', 'color.c')]
        flags += [f'-I{sdk_dependencies.ensure("pl_mpeg")}', '-Wno-unused-function']
    return arm64_sdk.build(sources, output, gui=True, extra_cflags=flags)


def ffmpeg():
    return shutil.which('ffmpeg') or ('/opt/homebrew/bin/ffmpeg' if Path('/opt/homebrew/bin/ffmpeg').exists() else None)


def make_films(tool, folder, seconds=3):
    """A Cinepak AVI and an MPEG-1 program stream of a moving test picture
    with a tone. Returns their paths."""
    quiet = [tool, '-hide_banner', '-loglevel', 'error', '-y']
    avi, mpg = folder / 'T.AVI', folder / 'T.MPG'
    subprocess.run([*quiet, '-f', 'lavfi', '-i', f'testsrc=size=160x120:rate=15:duration={seconds}', '-f', 'lavfi', '-i',
                    f'sine=frequency=440:sample_rate=22050:duration={seconds}', '-c:v', 'cinepak', '-q:v', '4', '-c:a',
                    'pcm_s16le', '-shortest', str(avi)], check=True)
    subprocess.run([*quiet, '-f', 'lavfi', '-i', f'testsrc=size=320x240:rate=25:duration={seconds}', '-f', 'lavfi', '-i',
                    f'sine=frequency=440:sample_rate=44100:duration={seconds}', '-c:v', 'mpeg1video', '-b:v', '1000k',
                    '-c:a', 'mp2', '-ac', '2', '-f', 'mpeg', '-shortest', str(mpg)], check=True)
    return avi, mpg


def reference(tool, film, width, height):
    """What ffmpeg decodes: per frame the checksum of its pixels (blue,
    green, red, zero) and their mean colour; the sound as 16-bit samples."""
    quiet = [tool, '-hide_banner', '-loglevel', 'error', '-y', '-i', str(film)]
    raw = subprocess.run([*quiet, '-pix_fmt', 'bgr0', '-f', 'rawvideo', '-'], check=True, capture_output=True).stdout
    sound = subprocess.run([*quiet, '-f', 's16le', '-'], check=True, capture_output=True).stdout
    size = width * height * 4
    frames = []
    for at in range(0, len(raw) - size + 1, size):
        frame = bytearray(raw[at:at + size])
        frame[3::4] = bytes(width * height)  # the fourth byte is nobody's
        count = width * height
        frames.append((zlib.crc32(bytes(frame)), sum(frame[0::4]) // count, sum(frame[1::4]) // count, sum(frame[2::4]) // count))
    return frames, sound


def run_host(binary, out, drive_c, command, name, result, seconds=180, steps=None, program='PLAYER.EXE'):
    """Starts the host with the player as its program and waits until the
    player has written `result` to the end and has left; then the host, which
    goes on as a machine without a program, is ended. Returns whether the
    player left with exit code 0."""
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_KEYMAP', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    env['NEO98_SETTINGS_DIR'] = str(out / 'settings')
    if steps:
        env['NEO98_HOST_INPUT'] = ';'.join(steps)
    line = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree', '--exe',
            f'C:\\{program}', '--command', command, '--drive-c', str(drive_c), '--no-registry-file',
            '--crash-report-dir', str(out / 'reports')]
    log = out / f'{name}.log'
    deadline = time.monotonic() + seconds
    with log.open('w') as stream:
        arm64_run.sync()
        process = subprocess.Popen(line, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        try:
            while process.poll() is None and time.monotonic() < deadline:
                time.sleep(0.2)
                if 'NEO98 HOST exited code=' in log.read_text(errors='replace'):
                    break
        finally:
            arm64_run.end(process)
    text = log.read_text(errors='replace')
    written = next((p for p in drive_c.iterdir() if p.name.upper() == result.upper()), None)
    return 'NEO98 HOST exited code=0 ' in text and written is not None and 'done=1' in written.read_text(errors='replace')


def read_result(drive_c, name):
    """The result file of the player: key=value lines and `frame` lines."""
    path = next((p for p in drive_c.iterdir() if p.name.upper() == name.upper()), None)
    values, frames = {}, []
    if not path:
        return values, frames
    for line in path.read_text(errors='replace').split('\n'):
        line = line.strip()
        if line.startswith('frame '):
            parts = line.split()
            frames.append((int(parts[2], 16), int(parts[3]), int(parts[4]), int(parts[5]), float(parts[6])))
        elif '=' in line:
            key, value = line.split('=', 1)
            values[key] = value
    return values, frames


def sound_crc(drive_values):
    return int(drive_values.get('sound_crc', '0'), 16)


def main():
    tool = ffmpeg()
    if not tool:
        print('ARM64 PLAYER SKIP: ffmpeg is not installed, no test films')
        raise SystemExit(77)
    out = neo98.output_directory('arm64-player-host')
    for name in ('drive-c', 'reports', 'films', 'settings'):
        shutil.rmtree(out / name, ignore_errors=True)
    (out / 'films').mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    pristine = win98_c.use_pristine()
    drive_c = out / 'drive-c'
    subprocess.run(['cp', '-c', '-R', str(pristine) + '/', str(drive_c)], check=True)
    (drive_c / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)

    avi, mpg = make_films(tool, out / 'films')
    shutil.copy(avi, drive_c / 'T.AVI')
    shutil.copy(mpg, drive_c / 'T.MPG')
    avi_frames, avi_sound = reference(tool, avi, 160, 120)
    mpg_frames, mpg_sound = reference(tool, mpg, 320, 240)
    build_player(drive_c / 'PLAYER.EXE')
    build_player(drive_c / 'PLAYERC.EXE', ['-DPLAYER_NO_NEON'])

    codes = {}
    codes['avi'] = run_host(binary, out, drive_c, 'PLAYER.EXE /bench C:\\T.AVI C:\\AVI.TXT', 'bench-avi', 'AVI.TXT')
    codes['mpg'] = run_host(binary, out, drive_c, 'PLAYER.EXE /bench C:\\T.MPG C:\\MPG.TXT', 'bench-mpg', 'MPG.TXT')
    shutil.copy(drive_c / 'PLAYERC.EXE', drive_c / 'PLAYER.EXE')
    codes['avi-c'] = run_host(binary, out, drive_c, 'PLAYER.EXE /bench C:\\T.AVI C:\\AVIC.TXT', 'bench-avi-c', 'AVIC.TXT')
    codes['mpg-c'] = run_host(binary, out, drive_c, 'PLAYER.EXE /bench C:\\T.MPG C:\\MPGC.TXT', 'bench-mpg-c', 'MPGC.TXT')
    build_player(drive_c / 'PLAYER.EXE')
    shot = out / 'player.ppm'
    shot.unlink(missing_ok=True)
    codes['play-avi'] = run_host(binary, out, drive_c, 'PLAYER.EXE /play C:\\T.AVI C:\\PAVI.TXT', 'play-avi', 'PAVI.TXT',
                                 steps=['0:wait=Neo98Player', f'1.2:shot={shot}'])
    codes['play-mpg'] = run_host(binary, out, drive_c, 'PLAYER.EXE /play C:\\T.MPG C:\\PMPG.TXT', 'play-mpg', 'PMPG.TXT')

    # The same film through MCI command strings (sdk/lib/moviemci.h).
    arm64_sdk.build([ROOT / 'tools/arm64_moviemci_fixture.c'], drive_c / 'MCIFILM.EXE', gui=False)
    codes['mci'] = run_host(binary, out, drive_c, 'MCIFILM.EXE C:\\T.AVI C:\\MCI.TXT', 'mci', 'MCI.TXT', program='MCIFILM.EXE')
    mci, _ = read_result(drive_c, 'MCI.TXT')

    a, a_frames = read_result(drive_c, 'AVI.TXT')
    m, m_frames = read_result(drive_c, 'MPG.TXT')
    ac, ac_frames = read_result(drive_c, 'AVIC.TXT')
    mc, mc_frames = read_result(drive_c, 'MPGC.TXT')
    pa, _ = read_result(drive_c, 'PAVI.TXT')
    pm, _ = read_result(drive_c, 'PMPG.TXT')

    def close(mine, theirs, steps):
        return len(mine) == len(theirs) and all(
            abs(x[1] - y[1]) <= steps and abs(x[2] - y[2]) <= steps and abs(x[3] - y[3]) <= steps for x, y in zip(mine, theirs))

    def played(values, frames, seconds):
        try:
            decoded, presented, wall = int(values['decoded']), int(values['presented']), float(values['wall_ms'])
            blocks = int(values['sound_blocks'])
        except (KeyError, ValueError):
            return False
        # Every frame decoded; shown at the pace of the film (a late frame is
        # skipped on the screen, not in the decoder); the sound went out.
        return (decoded == frames and presented >= frames * 0.8 and seconds * 900 <= wall <= seconds * 1000 + 2500 and
                blocks >= seconds * 10 - 2)

    # The screen while the film plays: the test picture has many colours, the
    # bar of the player is the grey of a button face.
    colours, grey = set(), 0
    if shot.exists():
        # The host writes a PPM (P6): three text fields, then red, green, blue.
        data = shot.read_bytes()
        fields = data.split(b'\n', 3)
        if len(fields) == 4 and fields[0] == b'P6':
            body = fields[3]
            pixels = list(zip(body[0::3], body[1::3], body[2::3]))
            colours = set(pixels)
            grey = pixels.count((192, 192, 192))
    mpeg_sound = int(m.get('sound_frames', 0))
    checks = {
        'the player writes its result and leaves with exit code 0 in every run': all(codes.values()),
        'AVI: the header (Cinepak, 160x120, 15 frames per second, PCM 22050 Hz mono)':
            (a.get('video'), a.get('audio'), a.get('width'), a.get('height'), a.get('fps'), a.get('audio_rate'),
             a.get('audio_channels')) == ('Cinepak', 'PCM', '160', '120', '15.000', '22050', '1'),
        'AVI: every frame has exactly the pixels ffmpeg decodes':
            len(a_frames) == len(avi_frames) > 0 and [f[0] for f in a_frames] == [f[0] for f in avi_frames],
        'AVI: the sound is the same bytes':
            int(a.get('sound_frames', 0)) * 2 == len(avi_sound) and sound_crc(a) == zlib.crc32(avi_sound),
        'AVI: a seek to the middle lands on a frame before it': 0 <= float(a.get('seek_half', -1)) <= 1.5,
        'AVI: the build without NEON decodes the same pixels':
            ac.get('frames_crc') == a.get('frames_crc') and ac.get('done') == '1',
        'MPEG-1: the header (320x240, 25 frames per second, layer II 44100 Hz)':
            (m.get('video'), m.get('width'), m.get('height'), m.get('fps'), m.get('audio_rate')) ==
            ('MPEG-1', '320', '240', '25.000', '44100'),
        'MPEG-1: every frame is within a few steps of what ffmpeg decodes': close(m_frames, mpg_frames, 3),
        'MPEG-1: the sound has the length ffmpeg decodes': mpeg_sound * 4 == len(mpg_sound),
        'MPEG-1: the build without NEON converts to the same pixels':
            mc.get('frames_crc') == m.get('frames_crc') and mc.get('done') == '1',
        'AVI: played in the window at the pace of the film, with sound': played(pa, len(avi_frames), 3),
        'MPEG-1: played in the window at the pace of the film, with sound': played(pm, len(mpg_frames), 3),
        'the window shows the film and the bar of the player': len(colours) > 200 and grey > 3000,
        'MCI strings: open, length in frames and milliseconds, source size, a child window':
            (mci.get('open'), mci.get('frames'), mci.get('format'), mci.get('length'), mci.get('source'), mci.get('child'),
             mci.get('put'), mci.get('mode_open')) == ('0', '45', '0', '3000', '0 0 160 120', '1', '0', 'stopped'),
        'MCI strings: play shows the window and the position follows the film':
            mci.get('play') == '0' and mci.get('mode_playing') == 'playing' and mci.get('visible') == '1' and
            900 <= int(mci.get('position_playing', 0)) <= 1900,
        'MCI strings: pause holds the position, seek moves it, a place beyond the end is refused':
            (mci.get('pause'), mci.get('paused_still'), mci.get('mode_paused'), mci.get('seek'), mci.get('position_seek')) ==
            ('0', '1', 'paused', '0', '500') and mci.get('seek_beyond') == '282',
        'MCI strings: the film stops at its end; close; a foreign command goes to WINMM':
            (mci.get('mode_end'), mci.get('position_end'), mci.get('close'), mci.get('foreign')) == ('stopped', '3000', '0', '1'),
    }
    measured = {name: {key: values.get(key) for key in ('decoded', 'presented', 'late', 'decode_ms', 'present_ms', 'wall_ms',
                                                         'sound_blocks')}
                for name, values in (('bench-avi', a), ('bench-mpg', m), ('bench-avi-c', ac), ('bench-mpg-c', mc),
                                     ('play-avi', pa), ('play-mpg', pm))}
    detail = {'mci': mci, 'colours': len(colours), 'grey': grey,
              'mpeg_frames': [len(m_frames), len(mpg_frames)], 'mpeg_sound': [mpeg_sound * 4, len(mpg_sound)],
              'mpeg_first': [m_frames[:3], mpg_frames[:3]]}
    shutil.rmtree(drive_c, ignore_errors=True)
    shutil.rmtree(out / 'films', ignore_errors=True)
    (out / 'results.json').write_text(json.dumps({'codes': codes, 'measured': measured, 'checks': checks}, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'ARM64 PLAYER FAIL: codes={codes} detail={detail}; logs in {out}')
        return 1
    print(f'ARM64 PLAYER PASS checks={len(checks)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
