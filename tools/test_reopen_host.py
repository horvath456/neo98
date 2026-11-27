#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Opening and closing the same thing fifty times leaves nothing behind.

A program that plays films all day opens and closes the AVI driver for every
one of them. Windows 98 serves that with 16-bit code in the process of the
program (MCIAVI.DRV, MSVIDEO.DLL, AVIFILE.DLL, the codecs), which takes its
memory from the global heap of KRNL386: one selector of the local descriptor
table for every block, one for every 64 KiB of a block larger than that. What
a close gives back has to be gone: a selector that is free names no
descriptor any more, and the next block takes its place in the table.

tools/reopen_fixture.c on neo98-kms over a private copy of drive C:, fifty
rounds each of

  avi    a film through MCI (uncompressed frames larger than 64 KiB with PCM
         sound, written here), opened, played to its end, closed
  cvid   the same with a Cinepak film, when ffmpeg is there to write one
  wave   a sound through the wave device of MCI
  sound  the same sound through sndPlaySound
  exec   a 16-bit program of the installation started and ended (WINVER.EXE)

and 70000 rounds of

  thread a thread started, waited for and closed: more threads than there
         are thread identities, which are words

After a round the fixture counts the descriptors of its local descriptor
table with LAR. Checked for each: every round answered without error; the
count after the last round is the one after the second (the first round
loads what stays loaded, the second shows what a round costs when it does);
and the memory of the host (resident pages, /proc/PID/smaps_rollup) after the
last round is within a few megabytes of the one after the second.

    build/analysis-venv/bin/python tools/test_reopen_host.py [--no-build] [--keep] [--rounds N] [mode ...]
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
import math
import os
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/reopen'
MODES = ('avi', 'cvid', 'wave', 'sound', 'exec', 'thread')
THREADS = 70000
# What the host may grow by between the second round and the last: the
# allocator of the host keeps what it once took, and a round of fifty leaves
# its marks in the logs and caches of the host. A leak of one film's frames
# per round would be twenty times this.
MEMORY_SLACK = 6 << 20


def chunk(name, data):
    return name + struct.pack('<I', len(data)) + data + (b'\0' if len(data) & 1 else b'')


def riff_list(kind, data):
    return chunk(b'LIST', kind + data)


def write_wave(path, milliseconds=120, rate=11025):
    samples = bytes(128 + int(60 * math.sin(i * 2 * math.pi * 440 / rate)) for i in range(rate * milliseconds // 1000))
    fmt = struct.pack('<HHIIHH', 1, 1, rate, rate, 1, 8)
    path.write_bytes(b'RIFF' + struct.pack('<I', 4 + 8 + len(fmt) + 8 + len(samples) + (len(samples) & 1)) + b'WAVE'
                     + chunk(b'fmt ', fmt) + chunk(b'data', samples))


def write_film(path, width=320, height=240, frames=6, rate=15, sound_rate=11025):
    """An AVI of uncompressed 8-bit frames (each larger than 64 KiB) with PCM
    sound, interleaved, with the old index: what MCIAVI plays without a codec."""
    palette = b''.join(bytes((i, 255 - i, (i * 7) & 255, 0)) for i in range(256))
    header = struct.pack('<IiiHHIIiiII', 40, width, height, 1, 8, 0, width * height, 0, 0, 256, 0) + palette
    per_frame = sound_rate // rate
    video_header = struct.pack('<4s4sIHHIIIIIIIIhhhh', b'vids', b'DIB ', 0, 0, 0, 0, 1, rate, 0, frames,
                               width * height, 0xffffffff, 0, 0, 0, width, height)
    sound_format = struct.pack('<HHIIHH', 1, 1, sound_rate, sound_rate, 1, 8)
    sound_header = struct.pack('<4s4sIHHIIIIIIIIhhhh', b'auds', b'\0\0\0\0', 0, 0, 0, 0, 1, sound_rate, 0,
                               frames * per_frame, per_frame, 0xffffffff, 1, 0, 0, 0, 0)
    main = struct.pack('<IIIIIIIIIIIIII', 1000000 // rate, width * height * rate + sound_rate, 0, 0x110, frames, 0, 2,
                       width * height, width, height, 0, 0, 0, 0)
    headers = riff_list(b'hdrl', chunk(b'avih', main)
                        + riff_list(b'strl', chunk(b'strh', video_header) + chunk(b'strf', header))
                        + riff_list(b'strl', chunk(b'strh', sound_header) + chunk(b'strf', sound_format)))
    movie, index = b'movi', b''
    for frame in range(frames):
        picture = bytes(((x + frame * 16) ^ y) & 255 for y in range(height) for x in range(width))
        sound = bytes(128 + int(50 * math.sin((frame * per_frame + i) * 2 * math.pi * 330 / sound_rate))
                      for i in range(per_frame))
        for name, data in ((b'00db', picture), (b'01wb', sound)):
            index += struct.pack('<4sIII', name, 0x10, len(movie), len(data))
            movie += chunk(name, data)
    body = b'AVI ' + headers + chunk(b'LIST', movie) + chunk(b'idx1', index)
    path.write_bytes(b'RIFF' + struct.pack('<I', len(body)) + body)


def write_cinepak(path):
    tool = shutil.which('ffmpeg')
    if not tool:
        return False
    done = subprocess.run([tool, '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i',
                           'testsrc=size=320x240:rate=15:duration=0.4', '-f', 'lavfi', '-i',
                           'sine=frequency=440:sample_rate=22050:duration=0.4', '-c:v', 'cinepak', '-q:v', '4',
                           '-c:a', 'pcm_s16le', '-shortest', str(path)])
    return done.returncode == 0 and path.is_file()


def resident(pid):
    for line in Path(f'/proc/{pid}/smaps_rollup').read_text().splitlines():
        if line.startswith('Rss:'):
            return int(line.split()[1]) * 1024
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--keep', action='store_true', help='keep the copy of the drive')
    parser.add_argument('--kms', type=Path, default=BINARY)
    parser.add_argument('--rounds', type=int, default=50)
    parser.add_argument('modes', nargs='*', choices=[*MODES, []], default=[])
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
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/reopen_fixture.c'), '-nostartfiles',
                    '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows', '-lwinmm', '-luser32', '-lkernel32',
                    '-o', str(drive / 'REOPEN.EXE')], check=True)
    write_film(drive / 'FILM.AVI')
    write_wave(drive / 'SOUND.WAV')
    have_cinepak = write_cinepak(drive / 'CVID.AVI')
    targets = {'avi': 'C:\\FILM.AVI', 'cvid': 'C:\\CVID.AVI', 'wave': 'C:\\SOUND.WAV', 'sound': 'C:\\SOUND.WAV',
               'exec': 'C:\\WINDOWS\\WINVER.EXE', 'thread': '-'}
    logs, checks = [], {}

    def session(mode, rounds):
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:640x480')
        for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD', 'NEO98_HOST_INPUT'):
            env.pop(variable, None)
        for stale in drive.glob('REOPEN.[TMG]*'):
            stale.unlink()
        name = 'avi' if mode == 'cvid' else mode
        memory = {}
        with (WORK / f'{mode}.log').open('w') as log:
            process = subprocess.Popen([str(args.kms), '--drive-c', str(drive), '--desktop', '640x480',
                                        '--exe', 'C:\\REOPEN.EXE', '--command',
                                        f'REOPEN.EXE {name} {targets[mode]} {rounds}', '--time-limit', '560'],
                                       cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 600
            while process.poll() is None and time.monotonic() < deadline:
                for marker in drive.glob('REOPEN.M*'):
                    round_number = int(marker.name.split('.M')[1])
                    if round_number not in memory:
                        memory[round_number] = resident(process.pid)
                        (drive / f'REOPEN.G{round_number}').write_bytes(b'')
                time.sleep(0.02)
            if process.poll() is None:
                process.kill()
            code = process.wait()
        written = win98_c.path('C:\\REOPEN.TXT', drive)
        found = {}
        for line in (written.read_text().splitlines() if written else ()):
            key, _, value = line.partition('=')
            found[key] = int(value)
        logs.append(f'---- {mode}: {code}\n' + (WORK / f'{mode}.log').read_text(errors='replace'))
        return found, memory, code

    try:
        for mode in (args.modes or MODES):
            if mode == 'cvid' and not have_cinepak:
                print('skip cvid: no ffmpeg to write a Cinepak film', flush=True)
                continue
            rounds = THREADS if mode == 'thread' else args.rounds
            found, memory, code = session(mode, rounds)
            counts = [found.get(str(i)) for i in (1, 2, 3, rounds)]
            second, last = found.get('2'), found.get(str(rounds))
            grown = memory.get(rounds, 0) - memory.get(2, 0)
            print(f'{mode}: descriptors before {found.get("before")}, after rounds 1, 2, 3 ... {rounds}: '
                  f'{counts[:3]} ... {counts[-1:]}; host memory after rounds 1, 2, {rounds}: '
                  + ', '.join(f'{memory.get(i, 0) / 1048576:.1f} MB' for i in (1, 2, rounds)), flush=True)
            checks[f'{mode}: {rounds} rounds without an error (errors {found.get("errors")}, the first '
                   f'{found.get("first error")}, exit {code})'] = \
                found.get('done') == 1 and found.get('errors') == 0 and code == 0
            checks[f'{mode}: the descriptors after round {rounds} are those after round 2 ({second} and {last})'] = \
                second is not None and second == last
            checks[f'{mode}: the memory of the host grew by {grown / 1048576:.1f} MB from round 2 to round {rounds}'] = \
                2 in memory and rounds in memory and grown <= MEMORY_SLACK
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
    print(f'reopen host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
