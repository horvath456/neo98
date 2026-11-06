#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The check matrix of PLAN.md step 6: start every program of the collection.

The matrix answers what the static import analysis cannot: exports fetched
through LoadLibrary and GetProcAddress, and bugs in APIs we already have. It
needs no per program maintenance. It reads the collection out of directories,
starts every program that is worth starting for at most 20 seconds in the host
without ASan, several at a time, and writes one line per program into
build/smoke-matrix/results.json.

    build/analysis-venv/bin/python tools/smoke_matrix.py
    build/analysis-venv/bin/python tools/smoke_matrix.py --list
    build/analysis-venv/bin/python tools/smoke_matrix.py --only SKIFREE --jobs 2

The report groups programs by observed startup result and records their calls.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import unicodedata
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

ROOT = Path(__file__).resolve().parents[1]
HOME = Path.home()

import programs_drive
import win98_c
# The matrix compares runs with each other, so it starts from setup's C:, not
# from the user's machine.
win98_c.use_pristine()

# The collection is mounted whole, so the guest sees the layout the reference system
# had: D:\GAMES and D:\Programme. tools/programs_drive.py.
COLLECTION = programs_drive.COLLECTION
GAMES = programs_drive.GAMES
WINDOWS_TREE = win98_c.FOLDER / 'WINDOWS'

# The programs a smoke run must not start. Every rule names the reason that
# lands in results.json, and every rule is decided from the file alone, so the
# matrix stays free of per program maintenance.
#
# An uninstaller deletes the installation it finds, a setup program writes one,
# and both would work on the drive overlay of their own run and tell us nothing
# about the port. A 16-bit NE image is PLAN.md step 9 and the loader rejects it
# today. Everything else is started.
NAME_RULES = (
    # A stem is the file name without its extension, in upper case.
    (re.compile(r'^(UNINS|UNWISE|UNSETUP|ISUN|DEISL|_?ISDEL|UNREG)'), 'filter:uninstaller'),
    (re.compile(r'SETUP'), 'filter:setup'),
    (re.compile(r'^(INSTALL|INST16|INST32|ACMSETUP|MSIEXEC|WEXTRACT|RUNONCE|WININIT)'), 'filter:setup'),
)

# An installation program whose file name is the name of the game it installs
# gets past NAME_RULES. It still names itself in its own version information,
# so this rule needs no per program maintenance either. An Inno Setup stub
# unpacks a second stage into C:\TEMP, starts it and waits modally on its
# message box; 88 of them were the largest hanging spot of the matrix and told
# nothing about the port.
VERSION_RULES = (
    (re.compile(r'inno setup'), 'filter:inno-setup'),
    (re.compile(r'\b(uninstall|deinstall)'), 'filter:uninstaller'),
)


def nfc(text):
    return unicodedata.normalize('NFC', text)


def show(path):
    """A stable spelling of a path, the one tools/unimplemented_imports.py uses."""
    path = Path(path)
    for base, prefix in ((ROOT, ''), (HOME, '~/')):
        try:
            return prefix + path.relative_to(base).as_posix()
        except ValueError:
            pass
    return path.as_posix()


def image_kind(path):
    """'pe32', 'ne16', or the reason why this file is not a program we can start.

    Since the third part of PLAN.md step 9 the runner starts New Executables as
    well, so a 16-bit program of the tree is a run like any other and its class
    says what happened. A DOS MZ without an NE, LE or PE header stays skipped:
    nothing of NEO98 runs real mode.
    """
    try:
        with open(path, 'rb') as file:
            head = file.read(0x40)
            if len(head) < 0x40 or head[:2] != b'MZ':
                return 'skip:not-a-pe'
            offset = int.from_bytes(head[0x3c:0x40], 'little')
            if offset <= 0 or offset > 0x10000000:
                return 'skip:not-a-pe'
            file.seek(offset)
            signature = file.read(6)
    except OSError as error:
        return f'skip:unreadable ({error.strerror})'
    if signature[:2] == b'NE':
        return 'ne16'
    if signature[:2] == b'LE' or signature[:2] == b'LX':
        return 'skip:le-module'
    if signature[:4] != b'PE\0\0':
        return 'skip:not-a-pe'
    machine = int.from_bytes(signature[4:6], 'little')
    if machine != 0x14c:
        return f'skip:machine-{machine:04x}'
    return 'pe32'


def filtered(name):
    """The filter rule that keeps a program out of the run, or None."""
    stem = Path(name).stem.upper()
    for pattern, reason in NAME_RULES:
        if pattern.search(stem):
            return reason
    return None


def version_strings(path):
    """The VS_VERSIONINFO strings of an image in lower case, empty where there are none."""
    try:
        import pefile
        image = pefile.PE(str(path), fast_load=True)
    except Exception:
        return {}
    values = {}
    try:
        image.parse_data_directories(
            directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_RESOURCE']])
        for info in getattr(image, 'FileInfo', []):
            for entry in info if isinstance(info, list) else [info]:
                for table in getattr(entry, 'StringTable', []):
                    for key, value in table.entries.items():
                        values[key.decode('latin1', 'replace').lower()] = \
                            value.decode('latin1', 'replace').strip().lower()
    except Exception:
        values = {}
    finally:
        image.close()
    return values


def filtered_by_version(path):
    """The filter rule the program's own version information decides, or None."""
    text = ' '.join(version_strings(path).values())
    for pattern, reason in VERSION_RULES:
        if pattern.search(text):
            return reason
    return None


def collect(directory, collection, drive, guest_root):
    """Every EXE of one directory, in the order and with the reach of
    tools/unimplemented_imports.py, each one already judged.

    `drive` is the letter the run mounts the collection under, `guest_root` the
    guest path of `directory` on that drive.
    """
    directory = Path(directory)
    programs = []
    if not directory.is_dir():
        return programs
    images = sorted((p for p in directory.rglob('*') if p.is_file() and p.suffix.upper() == '.EXE'),
                    key=lambda p: str(p).upper())
    for image in images:
        relative = image.relative_to(directory)
        # macOS hands out a decomposed name; the guest and the identity of the
        # program use the composed form, because only that has a byte in the
        # ANSI code page. The host path keeps the spelling the disk uses.
        guest = nfc(guest_root + '\\' + str(relative).replace('/', '\\'))
        entry = {'id': nfc(f'{collection}/{relative.as_posix()}'),
                 'name': image.name,
                 'collection': collection,
                 'path': show(image),
                 'guest': guest,
                 'guest_directory': guest.rsplit('\\', 1)[0],
                 'drive': drive,
                 # The folder a run clones into its own drive overlay: the top
                 # level folder of the collection the program lives in, so its
                 # data files keep their relative paths.
                 'folder': relative.parts[0] if len(relative.parts) > 1 else None,
                 'skip': None}
        reason = filtered(image.name)
        if not reason:
            kind = image_kind(image)
            reason = kind if kind not in ('pe32', 'ne16') else filtered_by_version(image)
        entry['skip'] = reason
        programs.append(entry)
    return programs


def collection_arguments(names):
    """The directories of the collection, as (name, directory, drive, guest root)."""
    known = {'games': (GAMES, 'D', programs_drive.GAMES_PATH),
             'windows': (WINDOWS_TREE, 'C', 'C:\\WINDOWS')}
    return [(name, *known[name]) for name in names]


def discover(names):
    programs = []
    for name, directory, drive, guest_root in collection_arguments(names):
        programs += collect(directory, name, drive, guest_root)
    return programs


def describe(programs, names):
    """One line per collection over exactly the programs this run takes."""
    summary = []
    for name, directory, _, _ in collection_arguments(names):
        found = [p for p in programs if p['collection'] == name]
        summary.append({'collection': name, 'path': show(directory), 'exes': len(found),
                        'run': sum(1 for p in found if not p['skip']),
                        'skipped': sum(1 for p in found if p['skip'])})
    return summary


# The six classes of PLAN.md step 6, from the best outcome to the worst. The
# comparison with the last run reads regression and progress out of this order.
CLASSES = ('window-content', 'window-empty', 'missing-api', 'crash', 'timeout', 'skipped')
RANK = {'window-content': 4, 'window-empty': 3, 'missing-api': 2, 'crash': 1, 'timeout': 1,
        'skipped': 0}

FONT = ROOT / 'build/generated/notepad-fixedsys.fnt'
# The matrix runs the probe without ASan and UBSan, as PLAN.md step 6 asks. The
# sanitized build of the same source stays the one every test uses; this one is
# five times faster and does not abort on a host bug the matrix wants to see as
# the guest sees it. tools/native_app.py links it out of the same objects the
# SDL host uses.
RUNNER = ROOT / 'build/native-app/probe-smoke'


def build_runner():
    from native_app import build_fast
    return build_fast('probe_application.cpp', RUNNER.name)


def slug(text):
    return re.sub(r'[^A-Za-z0-9._-]+', '_', text)


def unhex(text):
    try:
        return bytes.fromhex(text).decode('cp1252', 'replace')
    except ValueError:
        return ''


def write_png(pixels, width, height, path):
    """The screenshot as a PNG, out of the probe's PPM. Nobody opens a PPM."""
    raw = b''.join(b'\x00' + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))

    def chunk(kind, data):
        body = kind + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body))

    path.write_bytes(b'\x89PNG\r\n\x1a\n'
                     + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def convert_screenshot(source, target):
    if not source.exists():
        return None
    data = source.read_bytes()
    parts = data.split(b'\n', 3)
    if len(parts) < 4 or parts[0] != b'P6':
        return None
    width, height = (int(n) for n in parts[1].split())
    write_png(parts[3], width, height, target)
    return show(target)


def clone(source, target):
    """A copy that costs nothing on APFS. -c clones the blocks; without it the
    run would either write into the collection or copy gigabytes."""
    if subprocess.run(['cp', '-Rc', str(source), str(target)], capture_output=True).returncode:
        subprocess.run(['cp', '-R', str(source), str(target)], check=True, capture_output=True)
    compose_names(target)


def compose_names(root):
    """Normalize copied filenames to composed Unicode. macOS decomposes accented characters; the guest byte mapping cannot represent standalone combining marks. Rename only this run's private copy."""
    for directory, folders, files in os.walk(root, topdown=False):
        for name in folders + files:
            composed = unicodedata.normalize('NFC', name)
            if composed != name:
                os.rename(os.path.join(directory, name), os.path.join(directory, composed))
    composed = unicodedata.normalize('NFC', root.name)
    if composed != root.name:
        root.rename(root.with_name(composed))


def ansi(path):
    """Encode a guest path using one ANSI byte per character. Normalize decomposed host names first so accented characters become representable bytes rather than UTF-8 sequences passed through argv."""
    return unicodedata.normalize('NFC', path).encode('cp1252', 'replace')


def click_script(program):
    """The optional input script beside the program note, docs/apps/<name>/."""
    stem = Path(program['name']).stem.lower()
    for name in (slug(program['id']).lower(), stem):
        candidate = ROOT / 'docs/apps' / name / 'click-script.txt'
        if candidate.exists():
            return candidate
    return None


# Set by main from --execute-everything: the runner then gives every readable
# page the execute right at once, as before Only for
# the comparison of one change; an ordinary run leaves it empty.
RUNNER_OPTIONS = []


def build_command(program, scratch, reports, shot, limit, runner=RUNNER):
    drive_c = scratch / 'c'
    drive_c.mkdir(parents=True, exist_ok=True)
    command = [str(runner), str(host_path(program)), str(FONT), *RUNNER_OPTIONS, '--system-tree',
               '--exe', ansi(program['guest']),
               '--drive-c', str(drive_c),
               '--registry', str(scratch / 'registry.bin'),
               '--current-directory', ansi(program['guest_directory']),
               '--time-limit', str(limit),
               '--crash-report-dir', str(reports),
               '--screenshot', str(shot)]
    if program['drive'] != 'C':
        overlay = scratch / 'd'
        overlay.mkdir(parents=True, exist_ok=True)
        # The overlay holds a clone of the program's own collection folder, so
        # a run that writes a save game or a configuration file leaves the
        # collection alone and does not see what another run wrote.
        games = overlay / 'GAMES'
        games.mkdir(parents=True, exist_ok=True)
        if program['folder']:
            clone(GAMES / program['folder'], games / program['folder'])
        else:
            clone(host_path(program), games / program['name'])
        command += ['--drive', f"{program['drive']}={overlay}"]
    script = click_script(program)
    if script:
        command += ['--click-script', str(script)]
    return command


def host_path(program):
    text = program['path']
    return (HOME / text[2:]) if text.startswith('~/') else (ROOT / text)


WINDOW = re.compile(r'^PROBE_WINDOW (.*)$', re.M)
SUMMARY = re.compile(r'^PROBE started=(\d+) state=(\d+) code=(\d+) calls=(\d+) callbacks=(\d+) '
                     r'windows=(\d+) error=(.*)$', re.M)
STOP = re.compile(r'^PROBE_STOP reason=(\S+) calls=(\d+)$', re.M)
MISSING = re.compile(r'^MISSING IMPORT (\S+)!(\S+) needed by (.*)$', re.M)
NO_DLL = re.compile(r'^DLL dependency load failed (\S+) error=(\d+)$', re.M)
UNIMPLEMENTED = re.compile(r'Unimplemented (?:API|export) (\S+)!(\S+)')
REPORT_LINE = re.compile(r'^CRASHREPORT (\S+)$', re.M)


def readable(text):
    """Text a person can read, not the bytes of an icon or bitmap control."""
    return len(text) >= 3 and all(c == '\t' or ' ' <= c <= '\u00ff' for c in text) and \
        sum(c.isalnum() or c.isspace() for c in text) * 2 >= len(text)


def parse_windows(log):
    """The visible top level windows with the size of their client area and the
    number of colours the desktop shows inside it, and the text of a dialog.

    A program that only manages to put up its own error box counts as a window
    with content, because that is what it is. The text of that box is the
    finding, so it is read out of the Static children and kept in the row.
    """
    all_windows = []
    for text in WINDOW.findall(log):
        fields = dict(part.split('=', 1) for part in text.split(' ') if '=' in part)
        if 'hwnd' not in fields:
            continue
        all_windows.append({'hwnd': int(fields['hwnd']), 'parent': int(fields.get('parent', 0)),
                            'visible': fields.get('visible') == '1',
                            'class': unhex(fields.get('class', '')),
                            'text': unhex(fields.get('text', '')),
                            'width': int(fields.get('client_w', 0)),
                            'height': int(fields.get('client_h', 0)),
                            'visible_pixels': int(fields.get('visible_pixels', 0)),
                            'colours': int(fields['colours'].rstrip('+')) if 'colours' in fields else None})
    windows = []
    for window in all_windows:
        if not window['visible'] or window['parent'] or window['colours'] is None:
            continue
        if window['class'] == '#32770':
            # Win98 draws the text of a message box with DrawText and not into a
            # Static control, so the caption is often all there is. A Static
            # with readable text adds to it; an icon control carries bytes that
            # are not text and stays out.
            texts = [w['text'] for w in all_windows
                     if w['parent'] == window['hwnd'] and w['class'] == 'Static' and readable(w['text'])]
            window['message'] = ' '.join([window['text']] + texts).strip() or '(untitled)'
        windows.append({k: v for k, v in window.items() if k not in ('parent', 'visible')})
    return windows


def read_crash_report(directory):
    """Module plus offset of the suspect and the last API call it made."""
    path = Path(directory) / 'report.json'
    if not path.exists():
        return {}
    try:
        report = json.loads(path.read_text())
    except (OSError, ValueError):
        return {}
    suspect = report.get('suspect', {})
    out = {'report': show(path.parent), 'where': suspect.get('eip_text', ''),
           'why': suspect.get('why', ''), 'block': suspect.get('block_text', ''),
           'hang': report.get('hang', False)}
    index, thread = suspect.get('process_index'), suspect.get('thread_index')
    calls = []
    if index is not None and index < len(report.get('processes', [])):
        process = report['processes'][index]
        out['error'] = process.get('error', '')
        if thread is not None and thread < len(process.get('threads', [])):
            calls = process['threads'][thread].get('api_calls', [])
    if calls:
        last = calls[-1]
        out['last_api'] = f"{last.get('dll', '?')}!{last.get('name', '?')}"
        out['last_returned'] = last.get('returned', False)
    return out


def classify(program, log, exit_code, reports):
    """One of the six classes of PLAN.md step 6, with what it took to decide."""
    result = {'class': 'crash', 'detail': '', 'api_calls': 0, 'windows': []}
    summary, stop = SUMMARY.search(log), STOP.search(log)
    missing = [f'{dll}!{name}' for dll, name, _ in MISSING.findall(log)]
    missing_dlls = sorted({dll for dll, _ in NO_DLL.findall(log)})
    reports_found = REPORT_LINE.findall(log)
    if reports_found:
        result.update(read_crash_report(reports_found[-1]))
    if summary:
        result['started'] = summary[1] == '1'
        result['state'] = int(summary[2])
        result['exit_code'] = int(summary[3])
        result['api_calls'] = int(summary[4])
        result['error'] = summary[7].strip()
    result['windows'] = parse_windows(log)
    visible_content = [w for w in result['windows'] if w['colours'] > 1]
    result['visual_class'] = ('window-content' if visible_content else
                              'window-empty' if result['windows'] else 'no-window')
    result['stop'] = stop[1] if stop else 'no-summary'
    if missing:
        result['missing'] = sorted(set(missing))
    if missing_dlls:
        result['missing_dlls'] = missing_dlls
    error = result.get('error', '')
    unimplemented = UNIMPLEMENTED.search(error) or UNIMPLEMENTED.search(log)
    if exit_code == 3:
        result['class'] = 'skipped'
        result['detail'] = 'skip:missing-test-file'
        return result
    if exit_code not in (0, None) and not summary:
        result['class'] = 'crash'
        result['detail'] = f'runner exit {exit_code}'
        return result
    if unimplemented and (not summary or result.get('state') == 3):
        result['class'] = 'missing-api'
        result['api'] = f'{unimplemented[1]}!{unimplemented[2]}'
        result['detail'] = result['api']
        result.setdefault('missing', []).append(result['api'])
        result['missing'] = sorted(set(result['missing']))
        return result
    if result.get('state') == 3 and (missing or missing_dlls):
        # The loader could not bind the import table. The names are the gap.
        result['class'] = 'missing-api'
        result['api'] = (missing + [d + '!(not loaded)' for d in missing_dlls])[0]
        result['detail'] = error or result['api']
        return result
    if result['stop'] == 'time-limit':
        result['class'] = 'timeout'
        result['detail'] = result.get('where', '') or 'no report'
        return result
    if result.get('state') == 3 or not result.get('started'):
        result['class'] = 'crash'
        result['detail'] = error or 'start failed'
        return result
    content = visible_content
    result['class'] = 'window-content' if content else 'window-empty'
    if content and all(w.get('message') for w in content):
        # Every window the run put up is a dialog. Its text is the finding.
        result['dialog'] = content[0]['message']
        result['detail'] = 'Dialog: ' + result['dialog'][:100]
    elif content:
        result['detail'] = f"{content[0]['class']} {content[0]['colours']} colours"
    else:
        result['detail'] = f"{len(result['windows'])} visible top level windows"
    return result


def run_one(program, options):
    """One program, in its own scratch, drive overlay and registry journal."""
    identity = slug(program['id'])
    row = {k: program[k] for k in ('id', 'name', 'collection', 'path', 'guest')}
    if program['skip']:
        row.update({'class': 'skipped', 'detail': program['skip'], 'seconds': 0.0})
        return row
    scratch = options.out / 'runs' / identity
    shutil.rmtree(scratch, ignore_errors=True)
    scratch.mkdir(parents=True)
    reports = options.out / 'reports' / identity
    shot = scratch / 'shot.ppm'
    command = build_command(program, scratch, reports, shot, options.time_limit, options.runner)
    started = time.monotonic()
    try:
        finished = subprocess.run(command, capture_output=True, text=True, errors='replace',
                                  cwd=ROOT, timeout=options.time_limit + 40)
        log, code = finished.stdout + finished.stderr, finished.returncode
    except subprocess.TimeoutExpired as expired:
        log = (expired.stdout or b'').decode('utf-8', 'replace') if isinstance(expired.stdout, bytes) \
            else (expired.stdout or '')
        code, log = None, log + '\nPROBE_STOP reason=time-limit calls=0\n'
    seconds = round(time.monotonic() - started, 2)
    (options.out / 'logs').mkdir(parents=True, exist_ok=True)
    log_path = options.out / 'logs' / (identity + '.log')
    log_path.write_text(log)
    (options.out / 'shots').mkdir(parents=True, exist_ok=True)
    picture = convert_screenshot(shot, options.out / 'shots' / (identity + '.png'))
    row.update(classify(program, log, code, reports))
    row['seconds'] = seconds
    row['runner_exit'] = code
    row['log'] = show(log_path)
    if picture:
        row['screenshot'] = picture
    row['command'] = ' '.join(c.decode('cp1252') if isinstance(c, bytes) else c for c in command)
    # What a entry quotes: this rebuilds the scratch, the overlay and
    # the registry journal of the run, and keeps them.
    row['reproduce'] = ('build/analysis-venv/bin/python tools/smoke_matrix.py --only '
                        f"'^{re.escape(program['id'])}$' --jobs 1 --keep-scratch")
    if not options.keep_scratch:
        shutil.rmtree(scratch, ignore_errors=True)
    return row


def run_all(programs, options):
    rows, done = [], 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, options.jobs)) as pool:
        futures = {pool.submit(run_one, p, options): p for p in programs}
        for future in concurrent.futures.as_completed(futures):
            row = future.result()
            rows.append(row)
            done += 1
            print(f"[{done}/{len(programs)}] {row['id']:<55} {row['class']:<15} {row['detail'][:60]}",
                  flush=True)
    rows.sort(key=lambda r: r['id'].upper())
    return rows


def revision():
    try:
        text = subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD'], cwd=ROOT, text=True)
        return text.strip()
    except (OSError, subprocess.CalledProcessError):
        return 'unknown'


def compare(previous, current):
    """Regression and progress against the last run, per class.

    A step down the order of CLASSES is a regression, a step up is progress.
    A program that only appeared or vanished from the collection is neither.
    """
    before = {row['id']: row for row in previous.get('programs', [])}
    after = {row['id']: row for row in current['programs']}
    out = {'baseline_revision': previous.get('revision', 'unknown'),
           'regressions': [], 'progress': [], 'liveness_changes': [],
           'added': [], 'removed': []}
    for identity, row in sorted(after.items()):
        old = before.get(identity)
        if not old:
            out['added'].append({'id': identity, 'class': row['class']})
            continue
        if old['class'] == row['class']:
            continue
        old_visual = old.get('visual_class')
        if not old_visual and old['class'] in ('window-content', 'window-empty'):
            old_visual = old['class']
        if not old_visual and 'windows' in old and all(
                isinstance(w.get('colours'), int) for w in old['windows']):
            old_visual = ('window-content' if any(w['colours'] > 1 for w in old['windows'])
                          else 'window-empty' if old['windows'] else 'no-window')
        if old_visual and old_visual == row.get('visual_class') and \
                'timeout' in (old['class'], row['class']):
            out['liveness_changes'].append({
                'id': identity, 'from': old['class'], 'to': row['class'],
                'visual_class': old_visual, 'detail': row.get('detail', '')})
            continue
        change = {'id': identity, 'from': old['class'], 'to': row['class'],
                  'detail': row.get('detail', '')}
        if RANK[row['class']] < RANK[old['class']]:
            out['regressions'].append(change)
        else:
            out['progress'].append(change)
    for identity in sorted(set(before) - set(after)):
        out['removed'].append({'id': identity, 'class': before[identity]['class']})
    return out


def top_missing(rows):
    """The missing APIs, with the number of programs each one stops."""
    counts = {}
    for row in rows:
        for name in set(row.get('missing', [])):
            counts.setdefault(name, set()).add(row['id'])
        for name in set(row.get('missing_dlls', [])):
            counts.setdefault(name + '!(DLL fehlt)', set()).add(row['id'])
    return sorted(((name, sorted(ids)) for name, ids in counts.items()),
                  key=lambda item: (-len(item[1]), item[0]))


def top_crash_sites(rows):
    """The crash and hang sites, by module plus offset."""
    counts = {}
    for row in rows:
        if row['class'] not in ('crash', 'timeout'):
            continue
        where = row.get('where') or row.get('detail') or 'unknown'
        counts.setdefault(where, set()).add(row['id'])
    return sorted(((where, sorted(ids)) for where, ids in counts.items()),
                  key=lambda item: (-len(item[1]), item[0]))


def top_dialogs(rows):
    """The error boxes the programs put up, grouped by their wording."""
    counts = {}
    for row in rows:
        if not row.get('dialog'):
            continue
        # The wording without the numbers and the program name, so the same
        # fault from twenty programs lands in one line.
        key = re.sub(r'[0-9A-F]{4,}|[0-9]+', '#', row['dialog'])
        key = re.sub(r'\s+', ' ', key).strip()
        counts.setdefault(key, []).append(row['id'])
    return sorted(((key, sorted(ids)) for key, ids in counts.items()),
                  key=lambda item: (-len(item[1]), item[0]))


def summary_markdown(result, difference):
    lines = ['# Smoke matrix: latest results', '',
             f"Revision {result['revision']}, time limit {result['time_limit']} seconds, "
             f"{result['jobs']} concurrent programs.", '']
    for line in result['collections']:
        lines.append(f"- {line['collection']}: {line['path']}, {line['exes']} EXE, "
                     f"{line['run']} started, {line['skipped']} skipped")
    lines += ['', '## Classes', '', '| Class | Programs |', '| --- | ---: |']
    for name in CLASSES:
        lines.append(f"| {name} | {result['counts'][name]} |")
    lines.append(f"| **Total** | **{len(result['programs'])}** |")

    missing = top_missing(result['programs'])
    lines += ['', '## Most frequent missing APIs', '', '| Export | Programs | Example |',
              '| --- | ---: | --- |']
    for name, ids in missing[:25]:
        lines.append(f'| {name} | {len(ids)} | {ids[0]} |')
    if not missing:
        lines.append('| (none) | 0 | |')

    sites = top_crash_sites(result['programs'])
    lines += ['', '## Most frequent crash and hang locations', '',
              '| Module + offset | Programs | Example |', '| --- | ---: | --- |']
    for where, ids in sites[:25]:
        lines.append(f'| `{where}` | {len(ids)} | {ids[0]} |')
    if not sites:
        lines.append('| (none) | 0 | |')

    dialogs = top_dialogs(result['programs'])
    lines += ['', '## Most frequent dialog-only results', '',
              '| Text | Programs | Example |', '| --- | ---: | --- |']
    for text, ids in dialogs[:25]:
        lines.append(f"| {text[:140].replace('|', '/')} | {len(ids)} | {ids[0]} |")
    if not dialogs:
        lines.append('| (none) | 0 | |')

    if difference:
        lines += ['', '## Changes since the previous run', '',
                  f"Baseline revision {difference['baseline_revision']}.", '',
                  f"Regressions {len(difference['regressions'])}, "
                  f"Improvements {len(difference['progress'])}, "
                  f"changed liveness observations {len(difference.get('liveness_changes', []))}, "
                  f"added {len(difference['added'])}, removed {len(difference['removed'])}.", '']
        for title, key in (('Regressions', 'regressions'), ('Improvements', 'progress')):
            if not difference[key]:
                continue
            lines += [f'### {title}', '']
            for change in difference[key]:
                lines.append(f"- {change['id']}: {change['from']} -> {change['to']} "
                             f"({change['detail']})")
            lines.append('')
        if difference.get('liveness_changes'):
            lines += ['### Changed liveness observations', '',
                      'The visual category is unchanged, but timeout behavior changed and needs review.', '']
            for change in difference['liveness_changes']:
                lines.append(f"- {change['id']}: {change['from']} -> {change['to']}, "
                             f"visible {change['visual_class']} ({change['detail']})")
            lines.append('')
    lines += ['', 'Counts describe the observed calls in this run.', '']
    return '\n'.join(lines)


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--collection', action='append', choices=('games', 'windows'),
                        help='which collection to run, repeatable (default: both)')
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 4,
                        help='programs running at the same time (default: number of cores)')
    parser.add_argument('--time-limit', type=int, default=20, help='seconds per program')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/smoke-matrix')
    parser.add_argument('--baseline', type=Path,
                        help='the run to compare against (default: <out>/previous.json)')
    parser.add_argument('--only', help='regular expression on the program id, for one program')
    parser.add_argument('--limit', type=int, help='stop after this many programs, for a trial run')
    parser.add_argument('--list', action='store_true', help='only print the collection and the filter')
    parser.add_argument('--keep-scratch', action='store_true',
                        help='keep the drive overlay of every run instead of removing it')
    parser.add_argument('--no-compare', action='store_true', help='skip the comparison and its exit code')
    parser.add_argument('--build', action='store_true', help='relink the runner before the run')
    parser.add_argument('--no-fast-gates', action='store_true',
                        help='send every API call through the ordinary stop of uc_emu_start, as '
                             'before fast-gate dispatch')
    parser.add_argument('--execute-everything', action='store_true',
                        help='give every readable guest page the execute right at once, as before '
                             '')
    parser.add_argument('--census', action='store_true',
                        help='MEASURING MODE for tools/import_census.py: a call of an export the '
                             'port does not implement writes a line and returns the answer of an '
                             'unimplemented Win98 export instead of stopping the run, so one start '
                             'shows every such call.')
    return parser.parse_args(argv)


def select(programs, only, limit):
    if only:
        pattern = re.compile(only, re.I)
        programs = [p for p in programs if pattern.search(p['id'])]
    if limit:
        programs = programs[:limit]
    return programs


def main(argv=None):
    args = parse_arguments(argv)
    names = args.collection or ['games', 'windows']
    programs = select(discover(names), args.only, args.limit)
    summary = describe(programs, names)
    if args.list:
        for entry in programs:
            print(f"{entry['id']:<60} {entry['skip'] or 'run'}")
        for line in summary:
            print(f"{line['collection']:<10} {line['path']:<50} {line['exes']:4d} EXE, "
                  f"{line['run']:4d} to run, {line['skipped']:4d} skipped")
        return 0
    run_revision = revision()
    if not RUNNER.exists() or args.build:
        print(f'building {show(RUNNER)} without sanitizers', flush=True)
        build_runner()
    args.out.mkdir(parents=True, exist_ok=True)
    # Other tests relink probe-smoke too. Keep this run on one executable even
    # when another build finishes while the matrix is still starting programs.
    if args.execute_everything:
        RUNNER_OPTIONS.append('--execute-everything')
    if args.no_fast_gates:
        RUNNER_OPTIONS.append('--no-fast-gates')
    if args.census:
        RUNNER_OPTIONS.append('--census')
    args.runner = args.out.resolve() / 'probe-smoke'
    shutil.copy2(RUNNER, args.runner)
    runner_sha256 = hashlib.sha256(args.runner.read_bytes()).hexdigest()
    # The results of the last run become the baseline of this one, so a plain
    # call compares against the run before it without an argument.
    results_path, previous_path = args.out / 'results.json', args.out / 'previous.json'
    if not args.baseline and results_path.exists():
        shutil.move(str(results_path), str(previous_path))
    baseline_path = args.baseline or previous_path
    rows = run_all(programs, args)
    counts = {name: sum(1 for r in rows if r['class'] == name) for name in CLASSES}
    result = {'version': 1, 'revision': run_revision, 'runner_sha256': runner_sha256,
              'time_limit': args.time_limit,
              'jobs': args.jobs, 'collections': summary, 'counts': counts, 'programs': rows}
    difference = None
    if not args.no_compare and baseline_path.exists():
        difference = compare(json.loads(baseline_path.read_text()), result)
        result['comparison'] = {'baseline': show(baseline_path), **difference}
    results_path.write_text(json.dumps(result, indent=1, ensure_ascii=True) + '\n')
    (args.out / 'summary.md').write_text(summary_markdown(result, difference))
    for name in CLASSES:
        print(f'{counts[name]:5d}  {name}')
    print(f'{show(results_path)}, {show(args.out / "summary.md")}')
    if difference:
        print(f"Regressions {len(difference['regressions'])}, "
              f"Improvements {len(difference['progress'])}, added {len(difference['added'])}, "
              f"removed {len(difference['removed'])}")
        for change in difference['regressions']:
            print(f"  REGRESSION {change['id']}: {change['from']} -> {change['to']}")
        for change in difference['progress']:
            print(f"  PROGRESS  {change['id']}: {change['from']} -> {change['to']}")
        if difference['regressions']:
            return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
