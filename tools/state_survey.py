#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Saved states across the program collection.

For every Windows program of the collection (tools/programs_drive.py) two runs
of the real host without a screen and without sound:

  1. the program starts and runs ten seconds with NEO98_STATE_PROBE=1, which
     asks after every turn of the host whether the machine could be saved;
     then the machine is paused, a picture taken and the state saved;
  2. a new host loads the state, takes a picture before the machine goes on,
     lets it run four seconds and takes another.

Drive C: is a clone of the Win98 folder with DirectX 9.0c (tools/dx9_tree.py)
when that package is there, as a machine with games has it; else the folder of
setup with its DirectX 6.1, on which the programs that ask for a later one end
in their own message box.

What is recorded per program: whether it came up, how often it could have
been saved and the reasons it could not, size and time of the save, whether
the scan found a pointer to memory of the host, the time of the load, whether
the first picture after the load is the saved one, whether the loaded machine
failed, and whether its picture still changes.

Nobody plays: the programs stand in their title or menu, which is where most
of them wait for a click. What a state of a level in play needs is not
measured here. --poke presses Enter, clicks the middle of the screen, presses
the space bar and clicks twice more before the save and once more after the
load: blind input that takes many programs out of their title, into a game,
a menu or a dialog, whatever comes.

    build/analysis-venv/bin/python tools/state_survey.py [--jobs 3] [--kinds ddraw,d3d7,d3d8] [--limit N]
    build/analysis-venv/bin/python tools/state_survey.py --report # the table of the last run
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import disk_room  # noqa: E402
import dx9_tree  # noqa: E402
import programs_drive  # noqa: E402
import state_frames  # noqa: E402
import state_shared_pages  # noqa: E402
import win98_c  # noqa: E402
from host_test_process import run_logged  # noqa: E402
win98_c.use_pristine()

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/state-survey'
RUN_SECONDS = 10.0
KEEP = False
POKE = False
NAME = 'results'
RESERVE_GIB = 8.0


def programs():
    """Every Windows program of the collection with its kind."""
    import pefile
    result = []
    for exe in sorted(programs_drive.GAMES.rglob('*')):
        if exe.suffix.lower() != '.exe' or not exe.is_file():
            continue
        head = exe.open('rb').read(0x1000)
        if head[:2] != b'MZ':
            continue
        offset = int.from_bytes(head[0x3c:0x40], 'little')
        signature = head[offset:offset + 2] if offset + 2 <= len(head) else b''
        if signature == b'NE':
            result.append({'path': str(exe.relative_to(programs_drive.COLLECTION)), 'kind': 'win16'})
            continue
        if signature != b'PE':
            continue
        try:
            pe = pefile.PE(str(exe), fast_load=True)
            pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
            dlls = {entry.dll.decode().upper() for entry in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', [])}
        except Exception:  # noqa: BLE001 - a broken file is no program
            continue
        kind = ('d3d8' if 'D3D8.DLL' in dlls or 'D3D9.DLL' in dlls
                else 'opengl' if 'OPENGL32.DLL' in dlls
                else 'd3d7' if 'DDRAW.DLL' in dlls and 'D3DXOF.DLL' in dlls
                else 'ddraw' if 'DDRAW.DLL' in dlls
                else 'gdi')
        result.append({'path': str(exe.relative_to(programs_drive.COLLECTION)), 'kind': kind,
                       'sound': 'dsound' if 'DSOUND.DLL' in dlls else 'winmm' if 'WINMM.DLL' in dlls else '',
                       'network': bool(dlls & {'WSOCK32.DLL', 'WS2_32.DLL', 'WININET.DLL'})})
    return result


def host(binary, folder, name, drive_c, drive_d, steps, options, program=None, probe=False, seconds=40, on_line=None):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps),
               NEO98_HOST_DEADLINE=str(seconds))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_TRACE_CALLS', 'NEO98_CPU', 'NEO98_STATE_IMAGE',
                'NEO98_STATE_EXEC', 'NEO98_STATE_PROBE'):
        env.pop(key, None)
    if probe:
        env['NEO98_STATE_PROBE'] = '1'
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
               *(['--exe', program] if program else []), '--quiet', '--drive-c', str(drive_c),
               '--drive', f'D={drive_d}', '--no-registry-file', '--crash-report-dir', str(folder / 'reports'), *options]
    log = folder / f'{name}.log'
    result = run_logged(command, log, cwd=ROOT, env=env, timeout=seconds + 40,
                        on_line=on_line, min_free_bytes=int(1.5 * 2**30))
    if result[0] == 'disk reserve reached':
        raise RuntimeError('disk reserve reached; this program was not measured')
    return result


def clone(source, target):
    shutil.rmtree(target, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(source), str(target)], check=True)


def plain(picture):
    """A picture of one colour (but for the pointer): nothing was drawn."""
    body = picture.split(b'\n', 3)[-1]
    return len(set(body[i:i + 3] for i in range(0, min(len(body), 1440000), 3 * 97))) < 4


def survey_one(binary, folder, drive_c, drive_d, program):
    record = dict(program)
    guest = 'D:\\' + program['path'].replace('/', '\\')
    state = folder / 'machine.state'
    shots = {key: folder / f'{key}.ppm' for key in ('saved', 'settled', 'loaded', 'later')}
    for path in (state, *shots.values()):
        path.unlink(missing_ok=True)
    shutil.rmtree(folder / 'reports', ignore_errors=True)
    at = RUN_SECONDS
    saved_c, saved_d = folder / 'saved-c', folder / 'saved-d'
    captured = False
    def snapshot(line, process):
        nonlocal captured
        if captured or not line.startswith('NEO98 HOST state saved '):
            return
        # Stop only our host while copying: script deadlines must not let
        # the guest run or tear down its files midway through the snapshot.
        os.kill(process.pid, signal.SIGSTOP)
        try:
            clone(drive_c, saved_c)
            clone(drive_d, saved_d)
            captured = True
        finally:
            os.kill(process.pid, signal.SIGCONT)
    poke = ['4:keydown=0d', '4.2:keyup=0d', '5:down=400,300', '5.2:up=400,300', '6:keydown=20', '6.2:keyup=20',
            '7:down=300,250', '7.2:up=300,250', '8.5:down=500,350', '8.7:up=500,350'] if POKE else []
    code, text = host(binary, folder, 'save', drive_c, drive_d,
                      [*poke, f'{at}:pause=1', f'{at + 0.1}:shot={shots["saved"]}', f'{at + 0.15}:wstate=chains',
                       f'{at + 0.2}:wstate=scan',
                       f'{at + 0.3}:wstate=save', f'{at + 2.3}:shot={shots["settled"]}', f'{at + 2.5}:pause=0',
                       f'{at + 3.0}:menu=quit'],
                      ['--state-save', str(state)], program=guest, probe=True, on_line=snapshot)
    record['save_exit'] = code
    failed = re.search(r'^NEO98 HOST failed .*?error=(.*)$', text, re.M)
    record['failed_before_save'] = failed.group(1)[:160] if failed else None
    probe = re.search(r'^NEO98 HOST state probe asked=(\d+) yes=(\d+)', text, re.M)
    if probe:
        record['probe_asked'], record['probe_yes'] = int(probe.group(1)), int(probe.group(2))
        record['probe_reasons'] = [[int(count), reason[:150]] for count, reason in
                                   re.findall(r'^NEO98 HOST state probe no=(\d+) \([\d.]+%\) (.*)$', text, re.M)]
    saved = re.search(r'^NEO98 HOST state saved \S+ bytes=(\d+) ms=([\d.]+) turns=(\d+)', text, re.M)
    refused = re.search(r'^NEO98 HOST state save (?:refused|failed)[^:]*: (.*)$', text, re.M)
    scan = re.search(r'^NEO98 STATE scan suspects=(\d+)', text, re.M)
    record['scan_host_pointers'] = int(scan.group(1)) if scan else None
    # Deduplicate rejected fields; each needs a state rule or review.
    record['refusals'] = [re.sub(r' x\d+(: .*)?$', '', line)[:400] for line in re.findall(r'^NEO98 STATE refused: (.*)$', text, re.M)][:40]
    record['refused'] = refused.group(1)[:200] if refused else None
    # The host functions each waiting thread stood in at that moment
    # (MachineState::Chains): what a state would have to take up again.
    record['chains'] = [line[:1500] for line in re.findall(r'^NEO98 STATE chain (.*)$', text, re.M)][:24]
    record['long_transient_frames'] = state_frames.long_frames(text)
    picture = shots['saved'].read_bytes() if shots['saved'].exists() else b''
    record['drew'] = bool(picture) and not plain(picture)
    if not saved:
        record['saved'] = False
        return record
    record['saved'] = True
    if not captured:
        raise RuntimeError('the saved state has no matching drive snapshot')
    record['state_bytes'], record['save_ms'], record['save_turns'] = int(saved.group(1)), float(saved.group(2)), \
        int(saved.group(3))
    # Shared guest pages that reach past their block map foreign heap after a load.
    record['shared_runs_too_long'] = len(state_shared_pages.check(state)[2])
    code, text = host(binary, folder, 'load', saved_c, saved_d,
                      [f'0.4:shot={shots["loaded"]}', '0.6:pause=0',
                       *(['2:down=400,300', '2.2:up=400,300'] if POKE else []), f'4.6:shot={shots["later"]}',
                       '5.0:menu=quit'],
                      ['--state-load', str(state)])
    record['load_exit'] = code
    loaded = re.search(r'^NEO98 HOST state loaded \S+ ms=([\d.]+)', text, re.M)
    record['loaded'] = bool(loaded)
    record['load_ms'] = float(loaded.group(1)) if loaded else None
    load_error = re.search(r'^NEO98 HOST state load (?:refused|failed): (.*)$', text, re.M)
    record['load_error'] = load_error.group(1)[:200] if load_error else None
    failed = re.search(r'^NEO98 HOST failed .*?error=(.*)$', text, re.M)
    record['failed_after_load'] = failed.group(1)[:200] if failed else None
    reports = folder / 'reports'
    record['crash_reports_after_load'] = len(list(reports.glob('*'))) if reports.exists() else 0
    after = shots['loaded'].read_bytes() if shots['loaded'].exists() else b''
    later = shots['later'].read_bytes() if shots['later'].exists() else b''
    # The window of a paused host may show the frame before the last one;
    # the second picture of the standing machine is that frame (tools/test_state_host.py, game).
    settled = shots['settled'].read_bytes() if shots['settled'].exists() else b''
    record['same_picture'] = bool(after) and after in (picture, settled)
    record['picture_moves_after_load'] = bool(later) and later != after
    # A state that did not load is kept for a look at it (--keep).
    if KEEP and (not record['loaded'] or code != 0) and len(list(OUT.glob('failed-*.state'))) < 6:
        kept = OUT / ('failed-' + re.sub(r'[^A-Za-z0-9]+', '-', program['path']))
        shutil.copyfile(state, str(kept) + '.state')
        shutil.copyfile(folder / 'load.log', str(kept) + '.log')
    state.unlink(missing_ok=True)
    for path in (saved_c, saved_d):
        shutil.rmtree(path, ignore_errors=True)
    for path in shots.values():
        path.unlink(missing_ok=True)
    return record


def worker(index, binary, queue, results, lock, total, tree):
    folder = OUT / f'job{index}'
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    drive_d = folder / 'drive-d'
    # The clones of the collection and of the Win98 folder go however the job ends.
    try:
        while True:
            with lock:
                if not queue:
                    break
                if shutil.disk_usage(OUT).free < RESERVE_GIB * 2**30:
                    print('state survey stopped before the next program: disk reserve reached', flush=True)
                    break
                program = queue.pop(0)
            drive_c = folder / 'drive-c'
            started = time.time()
            for log in (folder / 'save.log', folder / 'load.log'):
                log.unlink(missing_ok=True)
            try:
                # Programs may change their own data. Each original run
                # starts from the collection; loads use the save's snapshot.
                clone(tree, drive_c)
                clone(programs_drive.COLLECTION, drive_d)
                record = survey_one(binary, folder, drive_c, drive_d, program)
            except Exception as error:  # noqa: BLE001 - one program must not end the survey
                record = dict(program, error=repr(error))
            finally:
                for path in (folder / 'saved-c', folder / 'saved-d'):
                    shutil.rmtree(path, ignore_errors=True)
            if (record.get('error') or not record.get('saved') or not record.get('loaded') or
                    record.get('save_exit') != 0 or record.get('load_exit') != 0 or
                    record.get('failed_after_load') or record.get('crash_reports_after_load') or
                    not record.get('same_picture')):
                # Keep bounded evidence before the worker folder is reused.
                # A full survey must retain the reason for each failed case.
                key = re.sub(r'[^A-Za-z0-9]+', '-', program['path'])[:100]
                key += '-' + hashlib.sha256(program['path'].encode()).hexdigest()[:12]
                evidence = OUT / f'{NAME}-failures' / key
                evidence.mkdir(parents=True, exist_ok=True)
                for log in (folder / 'save.log', folder / 'load.log'):
                    if not log.exists():
                        continue
                    with log.open('rb') as source:
                        data = source.read(128 << 10)
                        if log.stat().st_size > len(data):
                            source.seek(-(64 << 10), 2)
                            data = data[:64 << 10] + b'\n[survey evidence: beginning and end]\n' + source.read(64 << 10)
                    (evidence / log.name).write_bytes(data)
                record['log_directory'] = str(evidence.relative_to(OUT))
            record['seconds'] = round(time.time() - started, 1)
            with lock:
                results.append(record)
                (OUT / f'{NAME}.json').write_text(json.dumps(results, indent=1))
                print(f'[{len(results)}/{total}] {program["kind"]:6} {program["path"]}: '
                      f'saved={record.get("saved")} loaded={record.get("loaded")} same={record.get("same_picture")} '
                      f'{record.get("refused") or record.get("failed_after_load") or ""}', flush=True)
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def report(results):
    kinds = collections.OrderedDict()
    for record in results:
        kinds.setdefault(record['kind'], []).append(record)
    print('| Kind | Programs | Draw | Saved | Loaded | Same image | Continues without errors | '
          'Median save ms | Median load ms | Median MiB |')
    print('|---|---|---|---|---|---|---|---|---|---|')

    def median(values):
        values = sorted(v for v in values if v is not None)
        return values[len(values) // 2] if values else None
    for kind, records in kinds.items():
        saved = [r for r in records if r.get('saved')]
        good = [r for r in saved if r.get('loaded') and not r.get('failed_after_load')
                and not r.get('crash_reports_after_load') and r.get('load_exit') == 0]
        print('| %s | %d | %d | %d | %d | %d | %d | %s | %s | %s |' % (
            kind, len(records), sum(1 for r in records if r.get('drew')), len(saved),
            sum(1 for r in saved if r.get('loaded')), sum(1 for r in saved if r.get('same_picture')), len(good),
            median(r.get('save_ms') for r in saved), median(r.get('load_ms') for r in saved),
            round(median(r.get('state_bytes') for r in saved) / 1048576, 1) if saved else None))
    reasons = collections.Counter()
    for record in results:
        if record.get('saved'):
            continue
        reason = record.get('refused') or ('the program failed: ' + record['failed_before_save']
                                           if record.get('failed_before_save') else 'no answer')
        # The reason without the name of the program and the number of the thread.
        reason = re.sub(r'^.*?(thread \d+ )', '', reason)
        reason = re.sub(r'^[A-Z]:\\\S+: ', '', reason)
        reasons[reason] += 1
    print('\nNot saved, grouped by reason:')
    for reason, count in reasons.most_common(25):
        print(f'  {count:4}  {reason}')
    after = collections.Counter()
    for record in results:
        if record.get('saved') and (record.get('failed_after_load') or record.get('load_error')
                                    or not record.get('loaded')):
            after[(record.get('load_error') or record.get('failed_after_load') or 'the host ended')[:110]] += 1
    print('\nSaved, but failed after loading:')
    for reason, count in after.most_common(25):
        print(f'  {count:4}  {reason}')
    # Collect rejected fields across programs for state-rule review.
    fields = collections.Counter()
    for record in results:
        for line in set(re.sub(r'\[\d+\]', '[]', text) for text in record.get('refusals') or []):
            fields[line[:260]] += 1
    print('\nFields rejected by the writer (programs, location):')
    for line, count in fields.most_common(40):
        print(f'  {count:4}  {line}')
    asked = sum(r.get('probe_asked', 0) for r in results)
    yes = sum(r.get('probe_yes', 0) for r in results)
    if asked:
        print(f'\nSaveable in {yes} of {asked} host passes ({100.0 * yes / asked:.1f} %)')
    never = [r for r in results if r.get('probe_asked') and not r.get('probe_yes')]
    print(f'Programs that could not be saved in any run: {len(never)}')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--jobs', type=int, default=3)
    parser.add_argument('--reserve-gib', type=float, default=8.0,
                        help='free-space reserve before each program; below 8 requires --jobs 1, minimum 3')
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/native-app/neo98-host',
                        help='the host and matching .layout to freeze for this survey')
    parser.add_argument('--kinds', default='ddraw,d3d7,d3d8,opengl,gdi,win16')
    parser.add_argument('--limit', type=int, default=0)
    parser.add_argument('--match', default='')
    parser.add_argument('--report', action='store_true')
    parser.add_argument('--again', action='store_true',
                        help='only the programs the results of --name do not have as saved and running; they replace their records')
    parser.add_argument('--keep', action='store_true', help='keep the state and the log of a load that failed')
    parser.add_argument('--name', default='results', help='the file of the results under build/state-survey')
    parser.add_argument('--poke', action='store_true', help='blind input before the save and after the load')
    arguments = parser.parse_args()
    if arguments.jobs < 1 or arguments.reserve_gib < 3 or (arguments.reserve_gib < 8 and arguments.jobs != 1):
        parser.error('use at least one job; a reserve below 8 GiB requires one job and at least 3 GiB')
    global KEEP, NAME, POKE, RESERVE_GIB
    RESERVE_GIB = arguments.reserve_gib
    KEEP, NAME, POKE = arguments.keep, arguments.name, arguments.poke
    OUT.mkdir(parents=True, exist_ok=True)
    if not arguments.report:
        disk_room.require(RESERVE_GIB)
    if arguments.report:
        report(json.loads((OUT / f'{arguments.name}.json').read_text()))
        return 0
    if not programs_drive.available():
        print('state survey skipped: the program collection is not there', flush=True)
        return 0
    kinds = arguments.kinds.split(',')
    queue = [p for p in programs() if p['kind'] in kinds and arguments.match.lower() in p['path'].lower()]
    earlier = []
    if arguments.again:
        earlier = json.loads((OUT / f'{arguments.name}.json').read_text())
        good = {r['path'] for r in earlier if r.get('saved') and r.get('loaded') and r.get('load_exit') == 0
                and not r.get('failed_after_load') and not r.get('crash_reports_after_load')}
        queue = [p for p in queue if p['path'] not in good]
    queue.sort(key=lambda p: kinds.index(p['kind']))
    if arguments.limit:
        # An even pick across each kind, not the first ones of the alphabet.
        picked = []
        for kind in kinds:
            of_kind = [p for p in queue if p['kind'] == kind]
            step = max(1, len(of_kind) // arguments.limit)
            picked += of_kind[::step][:arguments.limit]
        queue = picked
    # A copy of the host of its own: a build during the survey would give the
    # host that loads another identity than the one that saved.
    binary = OUT / 'neo98-host'
    source_binary = arguments.binary.resolve()
    shutil.copyfile(source_binary, binary)
    shutil.copymode(source_binary, binary)
    # With the table of its fields, which belongs to that very program.
    shutil.copyfile(str(source_binary) + '.layout', str(binary) + '.layout')
    tree = dx9_tree.tree() or win98_c.FOLDER.resolve()
    (OUT / f'{NAME}-host.json').write_text(json.dumps({
        'binary': str(source_binary),
        'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'layout_sha256': hashlib.sha256(Path(str(binary) + '.layout').read_bytes()).hexdigest(),
        'started_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
        'programs': len(queue), 'jobs': arguments.jobs, 'poke': POKE,
        'reserve_gib': RESERVE_GIB, 'active_host_stop_reserve_gib': 1.5,
        'run_seconds': RUN_SECONDS, 'drive_c': str(tree),
        'drive_d': str(programs_drive.COLLECTION),
        'drives_at_save': True,
    }, indent=2) + '\n')
    print(f'drive C: from {tree}', flush=True)
    results, lock, total = [], threading.Lock(), len(queue)
    threads = [threading.Thread(target=worker, args=(i, binary, queue, results, lock, total, tree))
               for i in range(arguments.jobs)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    complete = len(results) == total and not any(r.get('error') for r in results)
    if arguments.again:
        # The records of this run in the place of the earlier ones.
        fresh = {r['path']: r for r in results}
        results = [fresh.pop(r['path'], r) for r in earlier] + list(fresh.values())
        (OUT / f'{NAME}.json').write_text(json.dumps(results, indent=1))
    report(results)
    if not complete:
        print(f'state survey incomplete: {len(results)} of {total} selected programs were measured', file=sys.stderr)
    return 0 if complete else 1


if __name__ == '__main__':
    sys.exit(main())
