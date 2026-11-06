#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Critical sections under contention, with the guest code and without it.

Builds tools/critical_guest_fixture.c, links tools/test_critical_guest.cpp
against the sanitized runtime objects and our own Unicorn build, and runs the
fixture twice: with the fast gates, so EnterCriticalSection and
LeaveCriticalSection run as guest code, and without,
so every call goes through the host. Four threads take one section two levels
deep and compute inside it without any API call, so the time slice of the port
ends inside the section, between sections and in front of the gates. Both runs
must count every round exactly once and see no second owner, and the guest run
must really have used both ways: the guest code for the uncontended calls and
the ordinary entry of the import for waiting and waking.

    build/analysis-venv/bin/python tools/test_critical_guest.py
"""
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/critical-guest'
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import neo98  # noqa: E402
import native_app  # noqa: E402

IMPORTS = {'KERNEL32': {'InitializeCriticalSection': 4, 'DeleteCriticalSection': 4,
                        'EnterCriticalSection': 4, 'LeaveCriticalSection': 4, 'CreateThread': 24,
                        'WaitForSingleObject': 8, 'CloseHandle': 4, 'GetCurrentThreadId': 0,
                        'GetLastError': 0, 'SetLastError': 4, 'Sleep': 4, 'ExitProcess': 4,
                        'VirtualAlloc': 16}}
THREADS, ROUNDS = 4, 20000


def run(name, command, env, check=True):
    p = subprocess.run(list(map(str, command)), env=env, capture_output=True, text=True)
    (OUT / (name + '.log')).write_text(p.stdout + p.stderr)
    if check and p.returncode:
        print(p.stdout + p.stderr)
        p.check_returncode()
    return p


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    dlltool = shutil.which('llvm-dlltool', path=env['PATH'])
    libraries = []
    for dll, names in IMPORTS.items():
        definition, library = OUT / (dll + '.def'), OUT / (dll + '.lib')
        definition.write_text('LIBRARY ' + dll + '.DLL\nEXPORTS\n' +
                              ''.join(f'{n}@{b}\n' for n, b in names.items()))
        run(dll, [dlltool, '-m', 'i386', '-k', '-d', definition, '-l', library], env)
        libraries.append(library)
    obj, exe = OUT / 'fixture.obj', OUT / 'CRITICAL.EXE'
    run('fixture', ['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                    '-fno-stack-protector', '-c', ROOT / 'tools/critical_guest_fixture.c', '-o', obj], env)
    run('fixture-link', ['lld-link', '/entry:mainCRTStartup', '/subsystem:console', '/timestamp:0',
                         '/nodefaultlib', '/machine:x86', '/out:' + str(exe), obj, *libraries], env)
    objects = Path(native_app.object_directory())
    names = [('fiber_x86_64.S' if __import__('platform').machine() == 'x86_64' else 'fiber_aarch64.S')] + [n + '.cpp' for n in native_app.runtime_sources()]
    library = native_app.unicorn_library()
    binary = OUT / 'test-critical-guest'
    run('runner-link', ['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                        *native_app.engine_flags(), ROOT / 'tools/test_critical_guest.cpp', *[objects / (n + '.o') for n in names], *native_app.engine_link_extra(),
                        *native_app.unicorn_link(library), '-o', binary], env)
    font = ROOT / 'build/generated/notepad-fixedsys.fnt'
    results, problems = {}, []
    for mode in ('fast', 'slow'):
        p = run('guest-' + mode, [binary, exe, font, mode], env, check=False)
        line = re.search(r'^CRITICAL mode=.*$', p.stdout, re.M)
        if p.returncode or not line:
            status = re.search(r'^CRITICAL status=.*$', p.stdout, re.M)
            problems.append(f'{mode}: ' + (status[0] if status else (p.stdout + p.stderr)[-2000:]))
            continue
        values = dict(part.split('=') for part in line[0].split()[1:])
        results[mode] = values
        if int(values['counter']) != THREADS * ROUNDS or int(values['collisions']):
            problems.append(f"{mode}: counter {values['counter']} collisions {values['collisions']}")
    if 'fast' in results:
        fast = results['fast']
        # At least the quiet section, six thousand calls nobody else wants.
        # The native CPU of Linux x64 runs the guest bodies without a host
        # entry and so without a count: there the host sees next to none of
        # the calls the run without fast gates makes.
        if native_app.native_x64():
            if 'slow' in results and int(fast['fast']) + int(fast['slow']) * 100 > int(results['slow']['slow']):
                problems.append(f"the host answered {fast['slow']} calls of the fast run")
        elif int(fast['guest']) < 6000:
            problems.append('the guest code answered only ' + fast['guest'] + ' calls')
        # The slice ended inside the section, so threads waited and were woken.
        if int(fast['enter_host']) < 2 or int(fast['leave_host']) < 2:
            problems.append(f"no contention reached the host: enter {fast['enter_host']} leave {fast['leave_host']}")
    if 'slow' in results and int(results['slow']['guest']):
        problems.append('the run without fast gates used the guest code')
    # The same work on both ways: every call the guest code answered is one the
    # other run made through the host, and a call that went on from the guest
    # code to the host is counted once.
    if len(results) == 2 and not native_app.native_x64():
        fast, slow = results['fast'], results['slow']
        # The main thread of the second phase makes as many rounds as it gets
        # slices, two calls each; everything else is the same count.
        total = int(fast['guest']) + int(fast['fast']) + int(fast['slow']) - 2 * int(fast['long_rounds'])
        other = int(slow['slow']) - 2 * int(slow['long_rounds'])
        if total != other:
            problems.append(f"call totals differ: {total} against {other}")
    if problems:
        print('\n'.join('FAIL ' + p for p in problems))
        raise SystemExit(1)
    fast = results['fast']
    in_guest = (f"{int(fast['fast']) + int(fast['slow'])} host entries in the fast run" if native_app.native_x64()
                else f"{fast['guest']} calls in guest code")
    summary = (f"CRITICAL {THREADS * ROUNDS} rounds on both ways, {in_guest}, "
               f"{fast['enter_host']} entries waited, {fast['leave_host']} leaves through the host")
    print(summary)
    (OUT / 'results.json').write_text(json.dumps({
        'status': 'PASS', 'result': summary, 'runs': results,
        'guest_sha256': hashlib.sha256(exe.read_bytes()).hexdigest()}, indent=1) + '\n')


if __name__ == '__main__':
    main()
