#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The sounds of the session: "Start Windows" and "Exit Windows".

Under Win98 MSGSRV32.EXE plays both. When Windows starts it logs on, plays
`SystemStart` of the sound scheme asynchronously and starts the shell of
SYSTEM.INI; on ExitWindows USER sends it 508h once every
window agreed, it plays `SystemExit` to the end and USER waits for that before
WM_ENDSESSION. Both are
SND_ALIAS | SND_NODEFAULT: an empty or missing `.Current` plays nothing.

Four runs of the host, silent (SDL's dummy device) and recorded with
NEO98_AUDIO_TAP:

- `start`: the machine starts with Explorer over the first logon, and the
  scheme names `C:\\WINDOWS\\media\\The Microsoft Sound.wav` for SystemStart.
  The device plays the samples of that file, as long as the file lasts.
- `start-none`: the same with the default value of the scheme's `.Current`
  key explicitly removed from the private registry. Nothing sounds.
- `exit`: a program alone (no Windows start, so no startup sound) calls
  ExitWindowsEx(EWX_SHUTDOWN) with `LOGOFF.WAV` for SystemExit. The whole
  file is played before the host ends.
- `exit-empty`: the same with an empty value. Nothing sounds.

    build/analysis-venv/bin/python tools/test_startup_sound_host.py [--no-build | --binary HOST] [--only start,exit,...]

Results in build/startup-sound-host/results.json.
"""
from __future__ import annotations

import array
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import neo98  # noqa: E402
import first_logon  # noqa: E402
import win98_c  # noqa: E402
from win98_folder import journal_record  # noqa: E402
from native_app import build_host  # noqa: E402

SCHEME = 'HKEY_USERS\\.Default\\AppEvents\\Schemes\\Apps\\.Default\\%s\\.Current'
START_WAV = 'C:\\WINDOWS\\media\\The Microsoft Sound.wav'
EXIT_WAV = 'C:\\WINDOWS\\media\\LOGOFF.WAV'
RATE = 44100

EXIT_SOURCE = r'''
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U ExitWindowsEx(U, U);
API void ExitProcess(U);
void mainCRTStartup(void) {
 ExitWindowsEx(1, 0);
 ExitProcess(0);
}
'''


def set_value(path, name, text):
    """A `set value` record of the registry journal (src/registry_persist.cpp)."""
    out = bytearray([3])
    for part in (path, name):
        raw = part.encode('cp1252')
        out += struct.pack('<H', len(raw)) + raw
    data = text.encode('cp1252') + b'\0'
    return bytes(out) + struct.pack('<II', 1, len(data)) + data


def with_records(source, target, records):
    """A copy of the journal `source` with `records` after its own."""
    body = source.read_bytes() if source and source.is_file() else b'NEO98REG1'
    target.write_bytes(body + b''.join(records))


def wave(guest_path):
    """The left channel of a PCM file of C:, and its rate."""
    host = win98_c.FOLDER / guest_path[3:].replace('\\', '/')
    if not host.exists():
        for candidate in host.parent.iterdir():
            if candidate.name.lower() == host.name.lower():
                host = candidate
    data = host.read_bytes()
    at, rate, channels, samples = 12, 0, 0, None
    while at + 8 <= len(data):
        chunk, size = data[at:at + 4], struct.unpack_from('<I', data, at + 4)[0]
        if chunk == b'fmt ':
            _, channels, rate = struct.unpack_from('<HHI', data, at + 8)
        elif chunk == b'data':
            samples = array.array('h')
            samples.frombytes(data[at + 8:at + 8 + size])
        at += 8 + size + (size & 1)
    return samples[0::channels], rate


def tap_left(tap):
    if not tap.exists():
        return array.array('h')
    samples = array.array('h')
    samples.frombytes(tap.read_bytes()[: tap.stat().st_size // 4 * 4])
    return samples[0::2]


def sounding(left, floor=64):
    """First and last frame louder than `floor`, or None."""
    loud = [i for i in range(0, len(left), 16) if abs(left[i]) > floor]
    return (loud[0], loud[-1]) if loud else None


def envelope(samples, rate, window=0.02):
    """The loudness of each `window` seconds: the mean absolute sample."""
    size = max(1, int(rate * window))
    return [sum(abs(v) for v in samples[at:at + size]) / size for at in range(0, len(samples) - size + 1, size)]


def correlation(tap, reference, rate):
    """How well the loudness of the file over time matches the loudness of
    what the device played, at the best offset (normalised, 1 is the same
    shape). The mixer brings the file to the rate of the device, so the
    samples themselves do not line up one to one; their course does."""
    played, wanted = envelope(tap, RATE), envelope(reference, rate)
    if not any(played) or not any(wanted):
        return 0.0
    mean = sum(wanted) / len(wanted)
    wanted = [v - mean for v in wanted]
    norm_w = sum(v * v for v in wanted) ** 0.5
    best = 0.0
    for lag in range(0, len(played) - len(wanted) + 1):
        part = played[lag:lag + len(wanted)]
        average = sum(part) / len(part)
        part = [v - average for v in part]
        norm_p = sum(v * v for v in part) ** 0.5
        if norm_p and norm_w:
            best = max(best, sum(a * b for a, b in zip(part, wanted)) / (norm_p * norm_w))
    return best


def exit_program(out, env):
    definition, library = out / 'USER32.def', out / 'USER32.lib'
    definition.write_text('LIBRARY USER32.DLL\nEXPORTS\nExitWindowsEx@8\n')
    subprocess.run(['llvm-dlltool', '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)], env=env,
                   check=True)
    kernel_definition, kernel_library = out / 'KERNEL32.def', out / 'KERNEL32.lib'
    kernel_definition.write_text('LIBRARY KERNEL32.DLL\nEXPORTS\nExitProcess@4\n')
    subprocess.run(['llvm-dlltool', '-m', 'i386', '-k', '-d', str(kernel_definition), '-l', str(kernel_library)],
                   env=env, check=True)
    (out / 'endexit.c').write_text(EXIT_SOURCE)
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-stack-protector',
                    '-c', str(out / 'endexit.c'), '-o', str(out / 'endexit.obj')], env=env, check=True)
    exe = out / 'ENDEXIT.EXE'
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/timestamp:0', '/nodefaultlib',
                    '/machine:x86', '/out:' + str(exe), str(out / 'endexit.obj'), str(library),
                    str(kernel_library)], env=env, check=True)
    return exe


def run(binary, out, name, env, explorer, records, program=None):
    work = out / name
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    if explorer:
        drive, registry = first_logon.clone(work)
        with_records(registry, registry, records)
    else:
        drive, registry = work / 'drive-c', work / 'session.registry'
        drive.mkdir()
        with_records(win98_c.journal(win98_c.FOLDER), registry, records)
        shutil.copyfile(program, drive / program.name)
    tap = work / 'audio.tap'
    steps = ['0:wait=Shell_TrayWnd', '12:menu=quit'] if explorer else []
    run_env = dict(env, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_AUDIO_TAP=str(tap),
                   NEO98_HOST_STATS='1', NEO98_HOST_DEADLINE='120', NEO98_HOST_INPUT=';'.join(steps))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_TRACE_CALLS'):
        run_env.pop(key, None)
    exe = 'C:\\WINDOWS\\EXPLORER.EXE' if explorer else 'C:\\' + program.name
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
               '--exe', exe, '--quiet', '--drive-c', str(drive), '--registry', str(registry),
               '--crash-report-dir', str(work / 'reports')]
    log = work / 'host.log'
    with log.open('w') as stream:
        try:
            code = subprocess.run(command, cwd=ROOT, env=run_env, stdout=stream, stderr=subprocess.STDOUT,
                                  stdin=subprocess.DEVNULL, timeout=240).returncode
        except subprocess.TimeoutExpired:
            code = 'timeout'
    text = log.read_text(errors='replace')
    shutil.rmtree(drive, ignore_errors=True)
    stats = re.findall(r'^NEO98 HOST stats t=[\d.]+ presents=\d+ sound=(\d+) audible=(\d+)', text, re.M)
    left = tap_left(tap)
    span = sounding(left)
    return {'log': str(log), 'code': code, 'text': text, 'left': left,
            'sounding_seconds': round((span[1] - span[0]) / RATE, 2) if span else 0.0,
            'audible_frames': int(stats[-1][1]) if stats else 0}


def main():
    out = neo98.output_directory('startup-sound-host')
    env = neo98.tool_environment()
    win98_c.use_pristine()
    env.update({key: os.environ[key] for key in ('NEO98_WIN98_ROOT', 'NEO98_WIN98_LIVE_ROOT', 'NEO98_WIN98_PRISTINE')})
    only = sys.argv[sys.argv.index('--only') + 1].split(',') if '--only' in sys.argv else \
        ['start', 'start-none', 'exit', 'exit-empty']
    if '--binary' in sys.argv:
        binary = Path(sys.argv[sys.argv.index('--binary') + 1])
    else:
        binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    program = exit_program(out, env) if any(name.startswith('exit') for name in only) else None
    cases = {
        'start': (True, [set_value(SCHEME % 'SystemStart', '', START_WAV)], START_WAV),
        'start-none': (True, [journal_record(4, SCHEME % 'SystemStart', '')], None),
        'exit': (False, [set_value(SCHEME % 'SystemExit', '', EXIT_WAV)], EXIT_WAV),
        'exit-empty': (False, [set_value(SCHEME % 'SystemExit', '', '')], None),
    }
    results, failed = {}, False
    for name in only:
        explorer, records, sound = cases[name]
        result = run(binary, out, name, env, explorer, records, program)
        text = result['text']
        alias = 'SystemStart' if explorer else 'SystemExit'
        request = re.search(r'SOUND session name=%s file=(.*?) bytes=(\d+) wait=(\d) result=(\d+)' % alias, text)
        checks = {'host ended': result['code'] == 0,
                  'the session asked for its sound once': len(re.findall('SOUND session name=' + alias, text)) == 1}
        if explorer:
            checks['the desktop came up'] = 'wait=Shell_TrayWnd reached' in text
            checks['no exit sound without ExitWindows'] = 'SOUND session name=SystemExit' not in text
        else:
            checks['ExitWindowsEx ended the machine'] = 'NEO98 HOST exitwindows flags=00000001' in text
            checks['no startup sound for a program alone'] = 'SOUND session name=SystemStart' not in text
            checks['the exit sound is synchronous'] = bool(request) and request.group(3) == '1'
        numbers = {'sounding_seconds': result['sounding_seconds'], 'audible_frames': result['audible_frames']}
        if sound:
            reference, rate = wave(sound)
            seconds = len(reference) / rate
            numbers['file_seconds'] = round(seconds, 2)
            numbers['correlation'] = round(correlation(result['left'], reference, rate), 3)
            checks['the scheme named the file'] = bool(request) and request.group(1) == sound and \
                request.group(4) == '0'
            numbers['peak'] = max((abs(v) for v in result['left']), default=0)
            numbers['file_peak'] = max(abs(v) for v in reference)
            checks['the device played the samples of the file'] = numbers['correlation'] > 0.95 and \
                abs(numbers['peak'] - numbers['file_peak']) <= numbers['file_peak'] // 20
            checks['as long as the file lasts'] = abs(result['sounding_seconds'] - seconds) < 0.6
        else:
            checks['nothing was played'] = (not request or request.group(4) != '0') and \
                result['sounding_seconds'] == 0.0 and result['audible_frames'] == 0
        ok = all(checks.values())
        failed |= not ok
        results[name] = {'log': result['log'], 'checks': checks, 'numbers': numbers, 'pass': ok}
        print(f"{name}: {'PASS' if ok else 'FAIL'} {numbers}")
        for check, passed in checks.items():
            if not passed:
                print(f'  failed: {check}')
    (out / 'results.json').write_text(json.dumps(results, indent=1) + '\n')
    print(f"Startup sound host {'FAIL' if failed else 'PASS'}: {out / 'results.json'}")
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
