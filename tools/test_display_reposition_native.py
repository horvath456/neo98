#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""USER places the top-level windows anew after a display mode change.

tools/display_reposition_fixture.c opens a sizable window at the bottom right
of 800x600, a tool window and a maximized window, changes to 640x480 with
ChangeDisplaySettings(flags 0) and back with ChangeDisplaySettings(NULL, 0).
USER moves the sizable window in proportion both times (,
MulDiv with rounding, checked against the original by
tools/test_display_reposition_original.py) and leaves the tool window and the
maximized window alone.

    build/analysis-venv/bin/python tools/test_display_reposition_native.py
"""
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import neo98  # noqa: E402
import win98_c  # noqa: E402
from native_app import build_fast  # noqa: E402

IMPORTS = {
    'KERNEL32': {'OutputDebugStringA': 4, 'ExitProcess': 4, 'GetModuleHandleA': 4},
    'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16, 'ShowWindow': 8,
               'GetWindowRect': 8, 'PeekMessageA': 20, 'DispatchMessageA': 4, 'EnumDisplaySettingsA': 12,
               'ChangeDisplaySettingsA': 8},
}
# 800x600 -> 640x480: left 480 + MulDiv(480, -160, 800) = 385, top 380 +
# MulDiv(380, -120, 600) = 305; back: 385 + MulDiv(385, 160, 640) = 481, 305 +
# MulDiv(305, 120, 480) = 381. The size stays 250x150.
SIZABLE = {'before': (480, 380, 730, 530), 'small': (385, 305, 635, 455), 'back': (481, 381, 731, 531)}


def build_fixture(out, env):
    dlltool = shutil.which('llvm-dlltool', path=env['PATH'])
    libraries = []
    for dll, functions in IMPORTS.items():
        definition, library = out / (dll + '.def'), out / (dll + '.lib')
        definition.write_text(f'LIBRARY {dll}.DLL\nEXPORTS\n' +
                              ''.join(f'{name}@{size}\n' for name, size in functions.items()))
        subprocess.run([dlltool, '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)], env=env, check=True)
        libraries.append(str(library))
    obj, exe = out / 'fixture.obj', out / 'REPOS.EXE'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                    '-fno-stack-protector', '-c', str(ROOT / 'tools/display_reposition_fixture.c'), '-o', str(obj)],
                   env=env, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/timestamp:0', '/nodefaultlib',
                    '/machine:x86', '/out:' + str(exe), str(obj), *libraries], env=env, check=True)
    return exe


def main():
    win98_c.use_pristine()
    out = neo98.output_directory('display-reposition-native')
    out.mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    exe = build_fixture(out, env)
    runner = build_fast('probe_application.cpp', 'probe-smoke')
    drive = out / 'drive-c'
    shutil.rmtree(drive, ignore_errors=True)
    result = subprocess.run([str(runner), str(exe), str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                             '--system-tree', '--drive-c', str(drive), '--time-limit', '60'],
                            env=env, capture_output=True, text=True, timeout=600)
    text = result.stdout + result.stderr
    (out / 'runner.log').write_text(text)
    seen = {}
    for stage, name, *values in re.findall(r'REPOS (\w+) (\w+) (-?\d+) (-?\d+) (-?\d+) (-?\d+)', text):
        seen[(stage, name)] = tuple(int(v) for v in values)
    summary = next((l for l in text.splitlines() if l.startswith('PROBE started=')), '')
    failures = []
    for stage, want in SIZABLE.items():
        if seen.get((stage, 'sizable')) != want:
            failures.append(f'sizable {stage}: {seen.get((stage, "sizable"))}, expected {want}')
    for name in ('tool', 'maximized'):
        before = seen.get(('before', name))
        for stage in ('small', 'back'):
            if before is None or seen.get((stage, name)) != before:
                failures.append(f'{name} {stage}: {seen.get((stage, name))}, expected {before}')
    if ' code=0 ' not in summary + ' ':
        failures.append(f'fixture did not end with 0: {summary}')
    report = {'summary': summary, 'rectangles': {f'{s} {n}': v for (s, n), v in seen.items()}, 'failures': failures}
    (out / 'results.json').write_text(json.dumps(report, indent=1) + '\n')
    for failure in failures:
        print('REPOSITION FAIL', failure)
    if failures:
        raise SystemExit(f'{len(failures)} checks failed; see {out}/runner.log')
    print('DISPLAY REPOSITION NATIVE PASS', json.dumps(report['rectangles']))


if __name__ == '__main__':
    main()
