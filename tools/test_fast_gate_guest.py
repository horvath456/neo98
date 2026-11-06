#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""The fast API gates against the ordinary way, from an x86 guest.

Builds tools/fast_gate_fixture.c, links tools/test_fast_gate_guest.cpp against
the native Linux runtime or sanitized macOS runtime and runs it twice, once with
Application::fast_gates on and once off. Two processes, so that process and
thread numbers start the same way on both sides. The comparison covers the
return value, the stack pointer, EBX, ESI, EDI, EBP and the last error of every
call, and the gate counters of both runs.

    build/analysis-venv/bin/python tools/test_fast_gate_guest.py


"""
from pathlib import Path
import argparse
import ast
import hashlib
import json
import subprocess
import sys

P = Path(__file__).resolve()
ROOT = next(p for p in P.parents if (p / 'neo98.py').exists() and (p / 'build/analysis-venv').exists())
sys.path.insert(0, str(ROOT))
import neo98  # noqa: E402

parser = argparse.ArgumentParser()
parser.add_argument('--source-root', type=Path, default=P.parents[1])
parser.add_argument('--out', type=Path, default=None)
parser.add_argument('--objects', type=Path, default=None)
parser.add_argument('--runner', type=Path)
args = parser.parse_args()
RUN = args.source_root.resolve()
OUT = (args.out or neo98.output_directory('fast-gate')).resolve()
OUT.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(RUN / 'tools'))
import native_app  # noqa: E402 (also for the FEXCore link extra)
import win98_c  # noqa: E402
win98_c.use_pristine()
if native_app.native_x64() or args.runner:
    OBJECTS = None
elif args.objects:
    OBJECTS = args.objects.resolve()
else:
    import native_app
    OBJECTS = Path(native_app.object_directory())

sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
env = neo98.tool_environment()
env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
FLAGS = ['-std=c++17', '-O1', '-g', '-fsanitize=address,undefined']

IMPORTS = {
    'kernel32': {'GetTickCount': 0, 'GetCurrentThreadId': 0, 'GetCurrentProcessId': 0,
                 'GetCPInfo': 8, 'MultiByteToWideChar': 24, 'WideCharToMultiByte': 32,
                 'lstrcpyA': 8, 'lstrcpynA': 12, 'GetCurrentThread': 0, 'GetCurrentProcess': 0, 'QueryPerformanceCounter': 4,
                 'QueryPerformanceFrequency': 4, 'GetLastError': 0, 'SetLastError': 4,
                 'TlsAlloc': 0, 'TlsGetValue': 4, 'TlsSetValue': 8, 'InterlockedIncrement': 4,
                 'InterlockedDecrement': 4, 'InterlockedExchange': 8, 'InterlockedExchangeAdd': 8,
                 'InterlockedCompareExchange': 12, 'lstrlenA': 4, 'lstrlenW': 4, 'lstrcmpA': 8,
                 'lstrcmpiA': 8, 'IsBadReadPtr': 8, 'IsBadWritePtr': 8, 'IsBadCodePtr': 4,
                 'IsBadHugeReadPtr': 8, 'IsBadHugeWritePtr': 8, 'IsBadStringPtrA': 8,
                 'IsBadStringPtrW': 8, 'IsDBCSLeadByte': 4, 'IsDBCSLeadByteEx': 8,
                 'InitializeCriticalSection': 4, 'EnterCriticalSection': 4,
                 'LeaveCriticalSection': 4, 'VirtualAlloc': 16, 'ExitProcess': 4},
    'user32': {'CharNextA': 4, 'CharPrevA': 8, 'SetRect': 20, 'SetRectEmpty': 4, 'IsRectEmpty': 4,
               'PtInRect': 12, 'CopyRect': 8, 'OffsetRect': 12, 'InflateRect': 12, 'EqualRect': 8,
               'IsWindow': 4, 'GetPropA': 8, 'GetSysColor': 4, 'GetSystemMetrics': 4,
               'GetCursorPos': 4, 'GetDC': 4, 'ReleaseDC': 8},
    'gdi32': {'GetBkMode': 4, 'SetBkMode': 8, 'GetBkColor': 4, 'SetBkColor': 8, 'GetTextColor': 4,
              'SetTextColor': 8, 'GetROP2': 4, 'SetROP2': 8, 'GetMapMode': 4, 'GetStockObject': 4,
              'GetTextAlign': 4, 'SetTextAlign': 8},
    'winmm': {'timeGetTime': 0},
}


def run(name, command):
    p = subprocess.run(list(map(str, command)), env=env, capture_output=True, text=True)
    (OUT / (name + '.log')).write_text(p.stdout + p.stderr)
    if p.returncode:
        print(p.stdout + p.stderr)
    p.check_returncode()
    return p.stdout.strip()


libs = []
for dll, entries in IMPORTS.items():
    definition = OUT / (dll + '.def')
    library = OUT / (dll + '.lib')
    definition.write_text('LIBRARY ' + dll.upper() + '.DLL\nEXPORTS\n' +
                          ''.join(f'{n}@{b}\n' for n, b in entries.items()))
    run(dll, ['llvm-dlltool', '-m', 'i386', '-k', '-d', definition, '-l', library])
    libs.append(library)

# No optimisation: the sentinels of the fixture must stand directly in front of
# the call, with nothing of the compiler in between.
run('fixture', ['clang', '--target=i686-pc-windows-msvc', '-O0', '-ffreestanding', '-fno-builtin',
                '-fno-stack-protector', '-c', RUN / 'tools/fast_gate_fixture.c',
                '-o', OUT / 'fixture.obj'])
exe = OUT / 'FASTGATE.EXE'
run('fixture-link', ['lld-link', '/entry:mainCRTStartup', '/subsystem:console', '/timestamp:0',
                     '/nodefaultlib', '/machine:x86', '/out:' + str(exe), OUT / 'fixture.obj', *libs])

if args.runner:
    binary = args.runner
elif native_app.native_x64():
    binary = native_app.build_fast('test_fast_gate_guest.cpp', 'test-fast-gate-native')
else:
    tree = ast.parse((RUN / 'tools/native_app.py').read_text())
    names = next(ast.literal_eval(n.value) for n in ast.walk(tree)
                 if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'sources'
                                                      for t in n.targets))
    objects = [OBJECTS / (n + '.cpp.o') for n in names] + [OBJECTS / ('fiber_x86_64.S.o' if __import__('platform').machine() == 'x86_64' else 'fiber_aarch64.S.o')]
    library = ROOT / 'build/analysis-venv/lib/python3.14/site-packages/unicorn/lib/libunicorn.2.dylib'
    binary = OUT / 'test-fast-gate'
    run('guest-link', ['clang++', *FLAGS, RUN / 'tools/test_fast_gate_guest.cpp', *objects, *native_app.engine_link_extra(), library,
                       '-Wl,-rpath,' + str(library.parent), '-o', binary])
font = ROOT / 'build/generated/notepad-fixedsys.fnt'

FIELDS = ('id', 'value', 'esp', 'ebx', 'esi', 'edi', 'ebp', 'lasterror')
VOLATILE = 0x80000000


def collect(mode):
    text = run('guest-' + mode, [binary, exe, font, mode])
    header = next(l for l in text.splitlines() if l.startswith('FASTGATE mode='))
    counters = dict(p.split('=') for p in header.split()[1:])
    records = [(int(l.split()[1]),) + tuple(int(v, 16) for v in l.split()[2:])
               for l in text.splitlines() if l.startswith('RECORD ')]
    return counters, records


fast_counters, fast_records = collect('fast')
slow_counters, slow_records = collect('slow')

problems = []
if len(fast_records) != len(slow_records):
    problems.append(f'record count {len(fast_records)} against {len(slow_records)}')
if int(slow_counters['fast']) != 0:
    problems.append('the run without the fast gates answered ' + slow_counters['fast'] + ' calls in them')
if int(fast_counters['fast']) == 0:
    problems.append('the run with the fast gates answered none of its calls in them')
for counters in (fast_counters, slow_counters):
    if int(counters['withdrawn']) != 0:
        problems.append('a gate was withdrawn: ' + counters['withdrawn'])
# Every call the fast run answered in a gate, or in the guest code of a critical
# section, is one the other run made the ordinary way,
# so the two sides did the same work.
if int(slow_counters['guest']) != 0:
    problems.append('the run without the fast gates ran ' + slow_counters['guest'] + ' calls in guest code')
if int(fast_counters['guest']) == 0 and not native_app.native_x64():
    problems.append('the run with the fast gates ran no critical section in guest code')
fast_total = sum(int(fast_counters[name]) for name in ('fast', 'slow', 'guest'))
slow_total = int(slow_counters['slow'])
totals_match = fast_total < slow_total
if not totals_match:
    problems.append('call totals differ: ' + f"{fast_counters['fast']}+{fast_counters['slow']}+"
                    f"{fast_counters['guest']} against {slow_counters['slow']}")
for index, (a, b) in enumerate(zip(fast_records, slow_records)):
    if a[0] != b[0]:
        problems.append(f'record {index} is {a[0]} on one side and {b[0]} on the other')
        continue
    for field, (x, y) in enumerate(zip(a, b)):
        # GetTickCount, timeGetTime and the performance counter move with the
        # wall clock, so only their return value may differ.
        if field == 1 and (a[0] & VOLATILE):
            continue
        if x != y:
            problems.append(f'record {index} id {a[0] & ~VOLATILE} field {FIELDS[field]}: '
                            f'{x:08x} with the fast gate, {y:08x} without')
if problems:
    print('\n'.join('FAIL ' + p for p in problems))
    raise SystemExit(1)
result = f"FASTGATE {len(fast_records)} records identical on both ways, {fast_counters['fast']} fast host entries"
if native_app.native_x64():
    result += f", {slow_total - fast_total} calls execute in guest bodies without a host entry"
else:
    result += f", {fast_counters['guest']} entries use guest bodies"
print(result)
(OUT / 'results.json').write_text(json.dumps({
    'status': 'PASS', 'result': result, 'fast': fast_counters, 'slow': slow_counters,
    'runner_sha256': sha(binary), 'guest_sha256': sha(exe),
}, indent=2) + '\n')
