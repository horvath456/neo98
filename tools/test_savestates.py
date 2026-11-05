#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The saved states of a game machine.

store (the default): tools/test_savestate_store.cpp, the states on disk
without a machine: the folder, the tree, automatic states, names, what a
state fits to, a layer saved, changed and loaded byte for byte, and every
write cut short. It runs twice, with block clones and with
NEO98_COPY_PLAIN=1 (a medium that cannot clone).

command: the command line of the host for the states of a game machine that
does not run, on a clone of the pristine drive C:.

    build/analysis-venv/bin/python tools/test_savestates.py [--only store,command] [--no-build]

Results in build/savestates/results.json.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import FLAGS, ROOT  # noqa: E402

OUT = ROOT / 'build/savestates'


def build_store():
    binary = OUT / 'test-savestate-store'
    subprocess.run(['clang++', *FLAGS, '-I' + str(ROOT / 'src'), str(ROOT / 'tools/test_savestate_store.cpp'),
                    str(ROOT / 'src/files.cpp'), str(ROOT / 'src/system_tree.cpp'), str(ROOT / 'src/volume.cpp'),
                    '-lz', '-o', str(binary)], cwd=ROOT, env=neo98.tool_environment(), check=True)
    shutil.rmtree(str(binary) + '.dSYM', ignore_errors=True)
    return binary


def run_store(binary, plain):
    work = OUT / ('store-plain' if plain else 'store-clone')
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    env = dict(os.environ)
    env.pop('NEO98_SAVESTATE_STOP', None)
    if plain:
        env['NEO98_COPY_PLAIN'] = '1'
    else:
        env.pop('NEO98_COPY_PLAIN', None)
    result = subprocess.run([str(binary), str(work)], capture_output=True, text=True, errors='replace', env=env)
    summary = re.search(r'savestate store: (\d+) passed, (\d+) failed', result.stdout)
    failures = [line for line in result.stdout.splitlines() if line.startswith('FAIL')]
    for line in failures:
        print('  ' + line)
    if not summary:
        print(result.stdout[-2000:], result.stderr[-4000:])
    report = {'copies': 'plain' if plain else 'clone', 'passed': int(summary[1]) if summary else 0,
              'failed': int(summary[2]) if summary else -1, 'failures': failures, 'exit': result.returncode}
    if not report['failed'] and not result.returncode:
        shutil.rmtree(work, ignore_errors=True)
    else:
        print(f'  folders in {work}')
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--only', default='store,command')
    parser.add_argument('--no-build', action='store_true')
    args = parser.parse_args()
    only = set(args.only.split(','))
    OUT.mkdir(parents=True, exist_ok=True)
    report = {}
    bad = 0
    if 'store' in only:
        binary = OUT / 'test-savestate-store' if args.no_build else build_store()
        report['store'] = [run_store(binary, False), run_store(binary, True)]
        for run in report['store']:
            print(f"store ({run['copies']}): {run['passed']} passed, {run['failed']} failed")
            bad += 1 if run['failed'] or run['exit'] else 0
    if 'command' in only:
        from test_savestates_command import run as run_command
        report['command'] = run_command(OUT, build=not args.no_build)
        print(f"command: {report['command']['passed']} passed, {report['command']['failed']} failed")
        bad += 1 if report['command']['failed'] else 0
    (OUT / 'results.json').write_text(json.dumps(report, indent=1, ensure_ascii=False) + '\n')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
