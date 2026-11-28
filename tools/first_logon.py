#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The Win98 folder of setup after its first logon, for tests that need a desktop.

The folder setup builds has not seen a user yet: the installation ends from
`HKLM\\...\\RunOnce`, before the shell has run. The first start of
EXPLORER.EXE therefore does what Win98 does at a first logon (RunOnce, the
per-user stubs of Active Setup, README.md), then starts the programs
of the Run key, among them WELCOME.EXE. That takes minutes in a test runner,
and it happens again in every test that starts the shell over an empty
writable C: with an empty journal.

This module runs that first logon once, with the original programs in
neo98-host, the host every one of these tests starts, and keeps what it left
behind:

    build/first-logon/drive-c the files the logon wrote to C:
    build/first-logon/session.registry setup's journal plus the logon's changes

setup's journal has already taken WELCOME.EXE and the other first-boot
programs out of the Run key (tools/win98_folder.py), so the session ends with
the desktop and the taskbar. A test clones both with `clone(directory)` and
starts its own shell on the copy.

    build/analysis-venv/bin/python tools/first_logon.py [--force]
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import neo98  # noqa: E402
import win98_c  # noqa: E402

# The first logon is the 33 per-user INF sections and a dozen programs; the
# optimised host needs about a minute for it on an idle machine.
LIMIT_S = 900
# The taskbar appears when RunOnce and Active Setup are through; the programs
# of the Run key and the Internet Connection Wizard's desktop stub settle in
# the 20 seconds after it, and the host then quits, which keeps the journal.
# One wait of the host's script gives up after 240 seconds; three of them
# give the logon the time it had before.
STEPS = '0:wait=Shell_TrayWnd;0:wait=Shell_TrayWnd;0:wait=Shell_TrayWnd;20:menu=quit'


def directory():
    return neo98.output_directory('first-logon')


def stamp():
    """What the kept session depends on: setup's folder and this recipe."""
    pristine = win98_c.FOLDER
    manifest = pristine / win98_c.PRISTINE_MANIFEST
    digest = hashlib.sha256()
    digest.update(manifest.read_bytes() if manifest.is_file() else str(pristine).encode())
    digest.update(Path(__file__).read_bytes())
    return digest.hexdigest()


def build(out):
    from native_app import build_host
    host = build_host()
    drive, registry = out / 'drive-c', out / 'session.registry'
    shutil.rmtree(drive, ignore_errors=True)
    drive.mkdir(parents=True)
    (out / 'stamp.json').unlink(missing_ok=True)
    journal = win98_c.journal(win98_c.FOLDER)
    if journal.is_file():
        shutil.copyfile(journal, registry)
    else:
        registry.unlink(missing_ok=True)
    env = dict(neo98.tool_environment(), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=STEPS,
               NEO98_HOST_DEADLINE=str(LIMIT_S), NEO98_SETTINGS_DIR=str(out / 'settings'),
               NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_TRACE_CALLS_FROM', 'NEO98_TRACE_PAINT'):
        env.pop(key, None)
    result = subprocess.run(
        [str(host), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
         '--system-tree', str(win98_c.FOLDER), '--drive-c', str(drive), '--registry', str(registry),
         '--desktop', '800x600', '--exe', 'C:\\WINDOWS\\EXPLORER.EXE', '--quiet',
         '--crash-report-dir', str(out / 'reports')],
        cwd=ROOT, env=env, capture_output=True, text=True, errors='replace', timeout=LIMIT_S + 300)
    text = result.stdout + result.stderr
    (out / 'logon.log').write_text(text)
    if result.returncode or 'input wait=Shell_TrayWnd reached' not in text:
        raise SystemExit(f'first logon did not bring up the taskbar and end (exit {result.returncode}); '
                         f'see {out / "logon.log"}')
    processes = len(re.findall(r'CreateProcess application', text))
    (out / 'stamp.json').write_text(json.dumps({'stamp': stamp(), 'processes': processes}) + '\n')
    return processes


def session(force=False):
    """(drive-c, session.registry) of the kept first logon; runs it when needed."""
    from native_build import locked
    win98_c.use_pristine()
    out = directory()
    with locked(out / 'lock'):
        try:
            current = json.loads((out / 'stamp.json').read_text())['stamp'] == stamp()
        except (OSError, ValueError, KeyError):
            current = False
        # The stamp alone does not say the session is there: a cleanup of the
        # disk may have taken the files and left the stamp.
        if not (out / 'drive-c').is_dir() or not (out / 'session.registry').is_file():
            current = False
        if force or not current:
            print('first logon: running the shell once over setup\'s folder', flush=True)
            build(out)
    return out / 'drive-c', out / 'session.registry'


def clone(target):
    """A private copy for one test: (drive-c, registry) under `target`."""
    drive, registry = session()
    target = Path(target)
    target.mkdir(parents=True, exist_ok=True)
    copy_drive, copy_registry = target / 'drive-c', target / 'session.registry'
    shutil.rmtree(copy_drive, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(drive), str(copy_drive)], check=True)
    shutil.copyfile(registry, copy_registry)
    return copy_drive, copy_registry


if __name__ == '__main__':
    drive, registry = session(force='--force' in sys.argv)
    print(f'{drive.relative_to(ROOT)}  {registry.relative_to(ROOT)} ({registry.stat().st_size} bytes)')
