#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the tests that need no Windows installation and print one table.

    python3 tools/test_public.py                  every test, 4 at a time
    python3 tools/test_public.py --only savestate tests whose file name contains 'savestate'
    python3 tools/test_public.py --list           the selected tests, without running them

This is the suite CI runs. Drive C: is deliberately missing: a test that
needs Windows stops with the setup hint, and is counted as NEEDS-WIN98, not as
a failure. No Microsoft file is read, copied or downloaded.

tools/public_tests.json lists, per platform, the tests known to fail there
and why. They are not started (KNOWN in the table); --include-known runs them
too and reports those that pass now, so the list can shrink. The run fails on
any other failure. A failure that took longer than 20 seconds in a parallel
run is repeated once on its own, because some tests share build caches.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / 'tools'
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(TOOLS))
import neo98  # noqa: E402

EXPECTATIONS = TOOLS / 'public_tests.json'
WINDOWS_DATA = 'Runtime objects need generated Windows data'
# The runner itself, and tests that boot a kernel in QEMU.
EXCLUDED = {'test_public.py', 'test_explorer_ui.py'}
# Tests whose required --source is the Windows folder itself.
NEEDS_FOLDER = {'test_release_data_host.py', 'test_seamless_exposure_host.py', 'test_release_smoke.py'}
# Drive C: itself, or headers and assets generated from its files.
NEEDS_WINDOWS = re.compile(r'No Windows 98 installation at|Run setup first|'
                           r"build/generated/[\w./-]+'? file not found|"
                           r'Runtime objects need generated Windows data|Could not open drive C:', re.I)


def platform_key():
    machine = platform.machine().lower()
    machine = {'x86_64': 'x64', 'amd64': 'x64', 'aarch64': 'arm64'}.get(machine, machine)
    system = {'darwin': 'macos'}.get(sys.platform, 'linux' if sys.platform.startswith('linux') else sys.platform)
    return f'{system}-{machine}'


def scripts():
    return [path for path in sorted(TOOLS.glob('test_*.py')) if path.name not in EXCLUDED]


def arguments_for(script, objects, out_root):
    """Pass only the options a script declares (as the private host suite does)."""
    text = script.read_text()
    arguments = []
    if "'--out'" in text or '"--out"' in text:
        out = out_root / script.stem
        out.mkdir(parents=True, exist_ok=True)
        arguments += ['--out', str(out)]
    if isinstance(objects, Path) and ("'--objects'" in text or '"--objects"' in text):
        arguments += ['--objects', str(objects)]
    if "'--binary'" in text or '"--binary"' in text:
        arguments += ['--binary', str(ROOT / 'build/native-app/neo98-host')]
    if "'--rebuild-affected'" in text:
        arguments += ['--rebuild-affected']
    return arguments


def run(python, script, objects, out_root, timeout, no_windows):
    started = time.monotonic()
    command = [str(python), str(script), *arguments_for(script, objects, out_root)]
    text = script.read_text()
    if isinstance(objects, str) and ("'--objects'" in text or '"--objects"' in text):
        status = 'NEEDS-WIN98' if objects == WINDOWS_DATA else 'FAIL'
        return {'test': script.name, 'status': status, 'exit_code': None, 'seconds': 0.0,
                'reason': objects, 'log': ''}
    worker = f'{os.getpid()}-{threading.get_ident()}'
    settings = out_root / f'settings-{worker}'
    logs = out_root / f'runtime-logs-{worker}'
    for private in (settings, logs):
        shutil.rmtree(private, ignore_errors=True)
        private.mkdir(parents=True)
    env = neo98.tool_environment()
    env.update(NEO98_WIN98_ROOT=str(no_windows), NEO98_SETTINGS_DIR=str(settings),
               NEO98_LOG_DIR=str(logs), SDL_AUDIODRIVER='dummy',
               SDL_VIDEODRIVER=os.environ.get('SDL_VIDEODRIVER', 'dummy'))
    process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, errors='replace', start_new_session=True)
    try:
        output, _ = process.communicate(timeout=timeout)
        code = process.returncode
    except subprocess.TimeoutExpired:
        # Only this test's process group. A test that ignores SIGTERM must not
        # hold up the run: on Linux it is killed; on macOS, where an ARM64 host
        # is never sent SIGKILL, it is left behind. A child that left the
        # group may keep the pipe open, so reading stops after a minute too.
        os.killpg(process.pid, signal.SIGTERM)
        output = ''
        try:
            output, _ = process.communicate(timeout=60)
        except subprocess.TimeoutExpired:
            if sys.platform.startswith('linux'):
                os.killpg(process.pid, signal.SIGKILL)
                try:
                    output, _ = process.communicate(timeout=30)
                except subprocess.TimeoutExpired:
                    pass
            process.stdout.close()
        output = (output or '') + f'\nTimed out after {timeout} s'
        code = None
    shutil.rmtree(settings, ignore_errors=True)
    shutil.rmtree(logs, ignore_errors=True)
    log = out_root / (script.stem + '.log')
    log.write_text(output)
    if code == 0:
        status = 'PASS'
    elif code == 77:
        status = 'SKIP'
    elif NEEDS_WINDOWS.search(output) or script.name in NEEDS_FOLDER:
        status = 'NEEDS-WIN98'
    else:
        status = 'FAIL'
    lines = [line for line in output.splitlines() if line.strip()]
    reason = '' if status == 'PASS' else (lines[-1][:160] if lines else 'no output')
    return {'test': script.name, 'status': status, 'exit_code': code,
            'seconds': round(time.monotonic() - started, 1), 'reason': reason,
            'log': str(log.relative_to(ROOT))}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--jobs', type=int, default=4, help='tests at the same time (default 4)')
    parser.add_argument('--timeout', type=int, default=300, help='seconds per test (default 300)')
    parser.add_argument('--include-known', action='store_true', help='also run the known failures')
    parser.add_argument('--only', action='append', default=[], help='substring of a test file name; repeatable')
    parser.add_argument('--list', action='store_true', help='print the selected tests and stop')
    parser.add_argument('--platform', default=platform_key(), help='key in tools/public_tests.json')
    parser.add_argument('--no-rerun', action='store_true', help='do not repeat failures one at a time')
    options = parser.parse_args(argv)

    selected = [s for s in scripts() if not options.only or any(o in s.name for o in options.only)]
    if options.list:
        print('\n'.join(s.name for s in selected))
        return 0
    if not selected:
        raise SystemExit('No test matches --only')
    expectations = json.loads(EXPECTATIONS.read_text()) if EXPECTATIONS.exists() else {}
    known = expectations.get(options.platform, {}).get('known_failures', {})

    out_root = neo98.output_directory('public-tests')
    no_windows = out_root / 'no-windows'  # never created: drive C: is absent on purpose
    python = neo98.analysis_python()
    try:
        from native_app import object_directory
        objects = object_directory()
    except Exception as error:  # noqa: BLE001 - reported per test below
        from check_release_embedded import FORBIDDEN as WINDOWS_DERIVED
        missing = [name for name in WINDOWS_DERIVED if not (ROOT / 'build/generated' / name).exists()]
        objects = WINDOWS_DATA if missing else f'Runtime objects failed to build: {error}'[:300]
        print(objects, flush=True)
    print(f'Public tests on {options.platform}: {len(selected)} tests, {options.jobs} at a time', flush=True)

    def one(script):
        if script.name in known and not options.include_known:
            return {'test': script.name, 'status': 'KNOWN', 'exit_code': None, 'seconds': 0.0,
                    'reason': known[script.name], 'log': ''}
        result = run(python, script, objects, out_root, options.timeout, no_windows)
        print(f'  {result["status"]:11} {result["seconds"]:7.1f}s  {result["test"]}', flush=True)
        return result

    with ThreadPoolExecutor(max_workers=max(1, options.jobs)) as workers:
        results = list(workers.map(one, selected))
    if options.jobs > 1 and not options.no_rerun:
        # A failure within seconds is not a race over a shared cache.
        again = [r for r in results if r['status'] == 'FAIL' and r['seconds'] > 20]
        if again:
            print(f'Repeating {len(again)} slow failures one at a time', flush=True)
        for index, result in enumerate(results):
            if result in again:
                results[index] = one(TOOLS / result['test'])

    counts = {}
    for result in results:
        counts[result['status']] = counts.get(result['status'], 0) + 1
    unexpected = [r for r in results if r['status'] == 'FAIL' and r['test'] not in known]
    fixed = [r for r in results if r['status'] not in ('FAIL', 'KNOWN') and r['test'] in known]
    for result in sorted(results, key=lambda r: r['test']):
        if result['status'] == 'FAIL':
            mark = 'known' if result['test'] in known else 'NEW'
            print(f'FAIL ({mark}) {result["test"]}: {result["reason"]}\n    log: {result["log"]}')
    for result in fixed:
        print(f'Known failure passes now, remove it from tools/public_tests.json: {result["test"]}')
    summary = ', '.join(f'{n} {s}' for s, n in sorted(counts.items()))
    print(f'\n{len(results)} tests: {summary}; {len(unexpected)} unexpected failures')
    report = {'platform': options.platform, 'counts': counts, 'tests': results,
              'unexpected': [r['test'] for r in unexpected], 'fixed': [r['test'] for r in fixed]}
    (out_root / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Results: {(out_root / "results.json").relative_to(ROOT)}')
    return 1 if unexpected else 0


if __name__ == '__main__':
    raise SystemExit(main())
