#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A small program costs the host a few megabytes, and gives them back.

Every Windows process reserves far more than it touches: 32 MiB of heap
arena, 2 MiB of stack, a megabyte of gates, and tables in the host with room
for 128 threads and 16384 regions. The host backed all of it for every
process, so the smallest program with a window cost 46 MB and 64 of them
3.1 GB, and what a process had freed stayed with the host until it ended.

tools/process_cost_fixture.c on neo98-kms over a private copy of drive C:
starts programs in stages and holds them. While a stage is held this reads
what the host process has resident (/proc/PID/smaps_rollup) and what each of
its memory files holds (the blocks of every memfd: one file of private pages
for every Windows process, the shared blocks of the session; the resident
count leaves a page of a file out when it is mapped nowhere).

The test: 32 of the smallest program with a window are started, ended,
started again, ended, and started a third time. Checked, in bytes:

  one of the 32 programs costs less than 8 MB (it was 46)
  the file of private pages of such a program holds less than 4 MB (it was 35)
  after the programs ended, the host is back within 16 MB of where it was
  before them, and the second round leaves no more than the first
  the third 32 cost what the first did

And what a process costs at every change from one process to the next, the
calls that map its runs of pages (one for every run of pages with one
protection): a program that calls into 32 separate pages it allocated as
data has no more runs at its end than one that only wrote to them. Every
readable page is executable on the native processor; while it became so page
by page, at the first jump into it, each such page split a run in three.

    build/analysis-venv/bin/python tools/test_process_cost_host.py [--no-build] [--kms KMS]

With --table the numbers of a scenario are printed instead, stage by stage:

  small     the fixture alone, then 1, 8 and 63 of the smallest program
  again     the stages of the test
  shell     the shell (EXPLORER.EXE as the desktop), then ten small programs
            of the installation beside it (Notepad, Calculator, Paint)

    build/analysis-venv/bin/python tools/test_process_cost_host.py --no-build --table small shell [--detail]
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
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/process-cost'
MB = 1 << 20
PROGRAMS = 32
SMALL = ['NOTEPAD.EXE', 'CALC.EXE', 'NOTEPAD.EXE C:\\WINDOWS\\WIN.INI', 'CALC.EXE', 'PBRUSH.EXE', 'NOTEPAD.EXE',
         'CALC.EXE', 'NOTEPAD.EXE C:\\WINDOWS\\SYSTEM.INI', 'CALC.EXE', 'NOTEPAD.EXE']
# A stage is the list of command lines it starts, or END: every program
# started so far is ended first.
END = '==='
SCENARIOS = {
    'small': [[], ['C:\\PCOST.EXE child'], ['C:\\PCOST.EXE child'] * 7, ['C:\\PCOST.EXE child'] * 55],
    'again': [[], ['C:\\PCOST.EXE child'] * 32, END, ['C:\\PCOST.EXE child'] * 32, END, ['C:\\PCOST.EXE child'] * 32],
    'shell': [['C:\\WINDOWS\\EXPLORER.EXE'], ['C:\\WINDOWS\\' + name for name in SMALL]],
}


def rollup(pid):
    values = {}
    for line in Path(f'/proc/{pid}/smaps_rollup').read_text().splitlines()[1:]:
        key, _, rest = line.partition(':')
        values[key] = int(rest.split()[0]) * 1024
    return values


def memory_files(pid):
    """The memory files of the process by name: what each holds in bytes."""
    found = {}
    seen = set()
    for entry in Path(f'/proc/{pid}/fd').iterdir():
        try:
            target = os.readlink(entry)
            if not target.startswith('/memfd:'):
                continue
            status = os.stat(entry)
        except OSError:
            continue
        if status.st_ino in seen:
            continue
        seen.add(status.st_ino)
        found.setdefault(target[len('/memfd:'):].replace(' (deleted)', ''), []).append(status.st_blocks * 512)
    return found


def mappings(pid):
    """The resident bytes of the host's mappings, summed by what is mapped."""
    found, name = {}, ''
    for line in Path(f'/proc/{pid}/smaps').read_text().splitlines():
        fields = line.split()
        if fields and '-' in fields[0] and not fields[0].endswith(':'):
            name = fields[5] if len(fields) > 5 else '[anonymous]'
            if name.startswith('/memfd:'):
                name = name.replace(' (deleted)', '')
            elif name.startswith('/') and not name.startswith('/dev/'):
                name = '[program and libraries]'
        elif fields and fields[0] == 'Rss:':
            found[name] = found.get(name, 0) + int(fields[1]) * 1024
    return found


def measure(pid, detail=False):
    r = rollup(pid)
    files = memory_files(pid)
    return {'resident': r.get('Rss', 0), 'anonymous': r.get('Pss_Anon', 0), 'shmem': r.get('Pss_Shmem', 0),
            'file': r.get('Pss_File', 0), 'files': files, 'mappings': mappings(pid) if detail else {}}


def megabytes(value):
    return f'{value / 1048576:8.1f}'


def prepare(drive):
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    pristine = win98_c.use_pristine()
    subprocess.run(['cp', '-a', '--reflink=auto', str(pristine), str(drive)], check=True)
    if win98_c.journal(pristine).is_file():
        shutil.copy2(win98_c.journal(pristine), win98_c.journal(drive))
    flags = ['-Os', '-nostartfiles', '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32']
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/process_cost_fixture.c'), *flags, '-o',
                    str(drive / 'PCOST.EXE')], check=True)


def run(kms, drive, stages, settle, log_path, detail=False):
    """One session: the measurement of every stage, the last after all ended."""
    for stale in drive.glob('PCOST.[MG]*'):
        stale.unlink()
    (drive / 'PCOST.LST').write_text(''.join(
        (END if stage == END else '\r\n'.join(stage + ['---'])) + '\r\n' for stage in stages))
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:800x600')
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD', 'NEO98_HOST_INPUT'):
        env.pop(variable, None)
    measured = {}
    with log_path.open('w') as log:
        process = subprocess.Popen([str(kms), '--drive-c', str(drive), '--desktop', '800x600', '--exe', 'C:\\PCOST.EXE',
                                    '--command', 'PCOST.EXE C:\\PCOST.LST', '--time-limit', '900'],
                                   cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 900
        while process.poll() is None and time.monotonic() < deadline and len(measured) <= len(stages):
            for marker in drive.glob('PCOST.M*'):
                stage = int(marker.name.split('.M')[1])
                if stage in measured:
                    continue
                time.sleep(settle)
                measured[stage] = measure(process.pid, detail)
                (drive / f'PCOST.G{stage}').write_bytes(b'')
            time.sleep(0.05)
        try:
            process.wait(timeout=60)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
    return measured


def report(name, stages, measured):
    print(f'{name}: MB of the host; "private files" are the files of private pages, one for every Windows process')
    print(f'{"stage":34}{"resident":>9}{"anon":>9}{"in files":>9}   private files: count, smallest, median, largest, sum')
    count = 1
    for stage in sorted(measured):
        m = measured[stage]
        if stage <= len(stages) and stages[stage - 1] != END:
            count += len(stages[stage - 1])
            label = f'{count} processes'
        else:
            count = 1
            label = 'all ended but the fixture'
        private = sorted(m['files'].get('neo98-direct', []))
        other = sum(sum(sizes) for key, sizes in m['files'].items() if key != 'neo98-direct')
        middle = private[len(private) // 2] if private else 0
        print(f'{label:34}{megabytes(m["resident"])}{megabytes(m["anonymous"])}{megabytes(m["shmem"] + m["file"])}   '
              f'{len(private):3}{megabytes(private[0] if private else 0)}{megabytes(middle)}'
              f'{megabytes(private[-1] if private else 0)}{megabytes(sum(private))}   other files{megabytes(other)}',
              flush=True)
        if m['mappings']:
            print('    files by name: ' + ', '.join(f'{key} {len(sizes)} x, {sum(sizes) / 1048576:.1f}'
                                                    for key, sizes in sorted(m['files'].items())))
            print('    resident by mapping: ' + ', '.join(
                f'{key} {value / 1048576:.1f}' for key, value in sorted(m['mappings'].items(), key=lambda item: -item[1])[:8]))


def runs_of(kms, drive, mode):
    """The runs of pages of the fixture in `mode` at its end, as the native
    processor reports them (NEO98_NATIVE_STATS), and its exit code."""
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:800x600',
               NEO98_NATIVE_STATS='1')
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD', 'NEO98_HOST_INPUT'):
        env.pop(variable, None)
    done = subprocess.run([str(kms), '--drive-c', str(drive), '--desktop', '800x600', '--exe', 'C:\\PCOST.EXE',
                           '--command', f'PCOST.EXE {mode}', '--time-limit', '120'], cwd=ROOT, env=env,
                          capture_output=True, text=True, timeout=180)
    (WORK / f'{mode}.log').write_text(done.stdout + done.stderr)
    found = [int(line.rsplit('runs=', 1)[1]) for line in (done.stdout + done.stderr).splitlines()
             if line.startswith('DIRECT STATS') and 'runs=' in line]
    return max(found, default=0), done.returncode


def held(m):
    """What a stage costs the host: resident pages, and the pages of memory
    files that are mapped nowhere at the moment (the prepared gates of the
    processes whose turn it is not)."""
    gates = sum(m['files'].get('neo98-gates', []))
    return m['resident'] + gates


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--kms', type=Path, default=BINARY)
    parser.add_argument('--settle', type=float, default=2.0, help='seconds a stage is left alone before it is read')
    parser.add_argument('--table', nargs='+', choices=list(SCENARIOS), help='print these scenarios instead of testing')
    parser.add_argument('--detail', action='store_true', help='with --table: the memory files by name and the largest mappings')
    args = parser.parse_args()
    if not native_app.native_x64():
        raise SystemExit('neo98-kms is a Linux x64 host.')
    if not args.no_build:
        native_app.build_kms_host(vulkan=False)
    drive = WORK / 'c'
    prepare(drive)
    try:
        if args.table:
            for name in args.table:
                report(name, SCENARIOS[name], run(args.kms, drive, SCENARIOS[name], args.settle, WORK / f'{name}.log',
                                                  args.detail))
            return
        stages = SCENARIOS['again']
        m = run(args.kms, drive, stages, args.settle, WORK / 'again.log')
        plain, plain_code = runs_of(args.kms, drive, 'plain')
        jump, jump_code = runs_of(args.kms, drive, 'jump')
    finally:
        shutil.rmtree(drive, ignore_errors=True)
        win98_c.journal(drive).unlink(missing_ok=True)
        Path(str(drive) + '.clock').unlink(missing_ok=True)
    report('again', stages, m)
    if len(m) < 6:
        raise SystemExit(f'only {len(m)} of 6 stages were reached; see {WORK}/again.log')
    alone, first, ended, second, ended_again, third = (held(m[i]) for i in range(1, 7))
    each = (first - alone) / PROGRAMS
    largest = sorted(m[2]['files'].get('neo98-direct', [0]))[len(m[2]['files'].get('neo98-direct', [0])) // 2]
    checks = {
        f'one small program costs {each / MB:.1f} MB of the host, less than 8':
            0 < each < 8 * MB,
        f'the file of private pages of such a program holds {largest / MB:.1f} MB, less than 4':
            0 < largest < 4 * MB,
        f'after {PROGRAMS} programs ended the host holds {(ended - alone) / MB:.1f} MB more than before them, less than 16':
            ended - alone < 16 * MB,
        f'a second round leaves {(ended_again - ended) / MB:.1f} MB more than the first, less than 4':
            ended_again - ended < 4 * MB,
        f'the third {PROGRAMS} cost {(third - ended_again) / MB:.1f} MB, the first {(first - alone) / MB:.1f}: '
        f'within 16 of each other': abs((third - ended_again) - (first - alone)) < 16 * MB,
        f'a program that calls into 32 pages of data has {jump} runs of pages at its end, one that only writes them '
        f'{plain} (exit {jump_code} and {plain_code})': plain > 0 and jump_code == 0 and plain_code == 0 and jump <= plain + 1,
        f'no file of private pages is left of an ended program: {len(m[3]["files"].get("neo98-direct", []))} files '
        f'with the fixture alone, {len(m[1]["files"].get("neo98-direct", []))} before':
            len(m[3]['files'].get('neo98-direct', [])) == len(m[1]['files'].get('neo98-direct', [])),
    }
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'process cost host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
