#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""The SDK for ARM64 applications: signature source, header, libraries.

Checks, without running a program:
  - the generator refuses a signature whose slots differ from specs;
  - the layouts it computes are the ones of Win32 and Win64 for known structs;
  - the compiler agrees: sizeof and offsetof of every converted struct in the
    generated header equal the Win64 layout of the table;
  - a program built with sdk-build is an ARM64 PE that Windows on ARM can
    load as far as the file says: machine, subsystem, relocations, imports by
    name only, from the three DLLs only, every name an export the pinned
    Win98 DLL has too (or one of the Win64-only *LongPtr names).
docs/ARM64-APPS.md.
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import arm64_sdk  # noqa: E402

SCRATCH = ROOT / 'build/arm64-sdk-test'
# Win32 and Win64 sizes from the documented layouts of the Windows headers.
KNOWN = {'WNDCLASSA': (40, 72), 'WNDCLASSEXA': (48, 80), 'MSG': (28, 48), 'PAINTSTRUCT': (64, 72),
         'CREATESTRUCTA': (48, 80), 'WINDOWPOS': (28, 40), 'STARTUPINFOA': (68, 104),
         'PROCESS_INFORMATION': (16, 24), 'MEMORY_BASIC_INFORMATION': (28, 48), 'SYSTEM_INFO': (36, 48),
         'DRAWITEMSTRUCT': (48, 64), 'MEASUREITEMSTRUCT': (24, 32), 'COPYDATASTRUCT': (12, 24),
         'SECURITY_ATTRIBUTES': (12, 24), 'MEMORYSTATUS': (32, 56), 'RECT': (16, 16), 'LOGFONTA': (60, 60),
         'WIN32_FIND_DATAA': (320, 320), 'CRITICAL_SECTION': (24, 40), 'MINMAXINFO': (40, 40),
         'TEXTMETRICA': (56, 56), 'BITMAPINFOHEADER': (40, 40), 'OVERLAPPED': (20, 32)}
WIN64_ONLY = {'GetWindowLongPtrA', 'SetWindowLongPtrA', 'GetClassLongPtrA', 'SetClassLongPtrA'}
checks = 0


def expect(condition, text):
    global checks
    checks += 1
    if not condition:
        print(f'FAIL {text}')
        sys.exit(1)


def readobj(*arguments):
    return subprocess.run([arm64_sdk.tool('llvm-readobj'), *arguments], check=True, capture_output=True, text=True).stdout


# The DLLs sdk/api describes.
DLLS = ('kernel32', 'user32', 'gdi32', 'winmm', 'advapi32', 'neo98touch')


def independent_witness(model, described):
    # The optional NEO98 extension is ours, not an export of Windows.
    described = described - {f['name'] for f in model.functions if f['dll'] == 'NEO98TOUCH.DLL'}
    compilers = {False: shutil.which('i686-w64-mingw32-gcc'), True: shutil.which('x86_64-w64-mingw32-gcc')}
    if not all(compilers.values()):
        return 'not installed, skipped'
    # Structs this port describes more narrowly than the headers do are left
    # to the size check: a field the headers wrap in a union has another name.
    for wide, compiler in compilers.items():
        lines = ['#include <windows.h>', '#include <stddef.h>']
        for name, record in model.structs.items():
            if record.get('synthetic'):
                continue
            size, _, flat = arm64_sdk.layout(model, name, wide)
            lines.append(f'_Static_assert(sizeof({name}) == {size}, "sizeof {name}");')
            fields = record['fields']
            if name in ('OVERLAPPED', 'SYSTEM_INFO', 'LARGE_INTEGER'):
                continue
            if all('group' not in f and not f['count'] and not f.get('sizeof') and (f['pointer'] or f['type'] in model.types) for f in fields):
                for field, (offset, _, _, _) in zip(fields, flat):
                    lines.append(f'_Static_assert(offsetof({name}, {field["name"]}) == {offset}, "{name}.{field["name"]}");')
        source = SCRATCH / f'witness{64 if wide else 32}.c'
        source.write_text('\n'.join(lines) + '\n')
        result = subprocess.run([compiler, '-fsyntax-only', str(source)], capture_output=True, text=True)
        expect(result.returncode == 0, f'mingw-w64 agrees with the {"Win64" if wide else "Win32"} layouts:\n' + result.stderr[:3000])
    library = Path(compilers[True]).resolve().parents[1] / 'x86_64-w64-mingw32/lib'
    if not (library / 'libuser32.a').exists():
        library = Path('/opt/homebrew/opt/mingw-w64/toolchain-x86_64/x86_64-w64-mingw32/lib')
    exported = set()
    for dll in DLLS:
        if dll == 'neo98touch': continue
        symbols = subprocess.run([arm64_sdk.tool('llvm-nm'), str(library / f'lib{dll}.a')], capture_output=True, text=True).stdout
        exported |= {line.split('__imp_', 1)[1].strip() for line in symbols.splitlines() if '__imp_' in line}
    missing = sorted(described - exported)
    expect(not missing, f'every described function is an export of 64-bit Windows by mingw-w64: missing {missing}')
    for sample in ('hello.c', 'files.c', 'window.c'):
        result = subprocess.run([compilers[True], '-fsyntax-only', '-Wall', str(ROOT / 'sdk/samples' / sample)],
                                capture_output=True, text=True)
        expect(result.returncode == 0, f'{sample} compiles against the headers of mingw-w64:\n' + result.stderr[:2000])
    exception_records(Path(compilers[True]).resolve().parents[1] / 'x86_64-w64-mingw32/include')
    return 'layouts, exports and samples agree'


# The records of exceptions and unwinding of Windows on ARM64, which
# sdk/api/exceptions.h writes out by hand: (type, size, {field: offset}).
EXCEPTION_RECORDS = [
    ('CONTEXT', 0x390, {'ContextFlags': 0, 'Cpsr': 4, 'X': 8, 'X19': 0xa0, 'Fp': 0xf0, 'Lr': 0xf8, 'Sp': 0x100,
                        'Pc': 0x108, 'V': 0x110, 'Fpcr': 0x310, 'Fpsr': 0x314, 'Bcr': 0x318, 'Bvr': 0x338,
                        'Wcr': 0x378, 'Wvr': 0x380}),
    ('EXCEPTION_RECORD', 0x98, {'ExceptionCode': 0, 'ExceptionFlags': 4, 'ExceptionRecord': 8, 'ExceptionAddress': 0x10,
                                'NumberParameters': 0x18, 'ExceptionInformation': 0x20}),
    ('DISPATCHER_CONTEXT', 0x58, {'ControlPc': 0, 'ImageBase': 8, 'FunctionEntry': 0x10, 'EstablisherFrame': 0x18,
                                  'TargetPc': 0x20, 'ContextRecord': 0x28, 'LanguageHandler': 0x30, 'HandlerData': 0x38,
                                  'HistoryTable': 0x40, 'ScopeIndex': 0x48, 'ControlPcIsUnwound': 0x4c,
                                  'NonVolatileRegisters': 0x50}),
    ('RUNTIME_FUNCTION', 8, {'BeginAddress': 0, 'UnwindData': 4}),
    ('EXCEPTION_POINTERS', 16, {'ExceptionRecord': 0, 'ContextRecord': 8}),
    ('NEON128', 16, {'Low': 0, 'High': 8}),
    ('UNWIND_HISTORY_TABLE', 0xd8, {'Count': 0, 'LowAddress': 8, 'HighAddress': 0x10, 'Entry': 0x18}),
]


def exception_records(mingw_include):
    """The same assertions against the header of the SDK and against the
    headers of mingw-w64, both read by clang for Windows on ARM64 (the
    compilers of mingw-w64 from Homebrew are for x86)."""
    lines = ['#include <windows.h>', '#include <stddef.h>']
    for name, size, fields in EXCEPTION_RECORDS:
        lines.append(f'_Static_assert(sizeof({name}) == {size}, "sizeof({name})");')
        lines += [f'_Static_assert(offsetof({name}, {field}) == {offset}, "{name}.{field}");' for field, offset in fields.items()]
    for name, value in (('CONTEXT_FULL', 0x400007), ('EXCEPTION_UNWINDING', 2), ('EXCEPTION_TARGET_UNWIND', 0x20),
                        ('EXCEPTION_COLLIDED_UNWIND', 0x40), ('STATUS_ACCESS_VIOLATION', 0xc0000005),
                        ('EXCEPTION_MAXIMUM_PARAMETERS', 15)):
        lines.append(f'_Static_assert((unsigned){name} == {value}u, "{name}");')
    source = SCRATCH / 'exception_records.c'
    source.write_text('\n'.join(lines) + '\n')
    own = subprocess.run(['clang', '--target=aarch64-pc-windows-msvc', '-ffreestanding', '-nostdlibinc', '-fsyntax-only',
                          '-isystem', str(arm64_sdk.SDK / 'include'), str(source)], capture_output=True, text=True)
    expect(own.returncode == 0, 'the records of exceptions in the header of the SDK have the layout of Windows on ARM64:\n'
           + own.stderr[:3000])
    if (mingw_include / 'windows.h').exists():
        theirs = subprocess.run(['clang', '--target=aarch64-w64-windows-gnu', '-nostdlibinc', '-fsyntax-only', '-w',
                                 '-isystem', str(mingw_include), str(source)], capture_output=True, text=True)
        expect(theirs.returncode == 0, 'mingw-w64 has the same layout for the records of exceptions:\n' + theirs.stderr[:3000])


def main():
    shutil.rmtree(SCRATCH, ignore_errors=True)
    SCRATCH.mkdir(parents=True)
    model = arm64_sdk.load()
    expect(len(model.functions) >= 300, 'the signature source describes at least 300 functions')

    # A wrong signature does not pass.
    paths = sorted(arm64_sdk.API.glob('*.api'))
    with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
        wrong = Path(directory) / '99-wrong.api'
        wrong.write_text('dll KERNEL32;\nBOOL CloseHandle(HANDLE a, HANDLE b);\n')
        refused = False
        try:
            arm64_sdk.check(arm64_sdk.parse([*paths, wrong]))
        except arm64_sdk.ApiError as error:
            refused = 'CloseHandle has 2 slots' in str(error)
        expect(refused, 'a signature with the wrong number of slots is refused')
        wrong.write_text('dll KERNEL32;\nBOOL NoSuchExport(HANDLE a);\n')
        refused = False
        try:
            arm64_sdk.check(arm64_sdk.parse([*paths, wrong]))
        except arm64_sdk.ApiError as error:
            refused = 'not connected' in str(error)
        expect(refused, 'a function the runtime does not provide is refused')
        wrong.write_text('dll USER32;\nBOOL GetMessageA(MSG* m, HWND h, UINT a, UINT b);\n')
        refused = False
        try:
            arm64_sdk.table(arm64_sdk.parse([*paths, wrong]))
        except arm64_sdk.ApiError as error:
            refused = 'needs in, out or inout' in str(error)
        expect(refused, 'a pointer to a differing struct without a direction is refused')

    for name, (size32, size64) in KNOWN.items():
        got = (arm64_sdk.layout(model, name, False)[0], arm64_sdk.layout(model, name, True)[0])
        expect(got == (size32, size64), f'{name}: layout {got}, Windows has {(size32, size64)}')

    # The compiler's view of the generated header against the table.
    sdk = arm64_sdk.generate(force=True)
    lines = ['#include <windows.h>']
    for name, record in model.structs.items():
        if record.get('synthetic'):
            continue
        size, _, _ = arm64_sdk.layout(model, name, True)
        lines.append(f'_Static_assert(sizeof({name}) == {size}, "{name}");')
    for name in model.structs:
        if model.structs[name].get('synthetic'):
            continue
        program = arm64_sdk.struct_program(model, name)
        if not program:
            continue
        # Each pointer-sized field of a converted struct sits where the table says.
        flat = arm64_sdk.layout(model, name, True)[2]
        fields = model.structs[name]['fields']
        simple = all('group' not in f and not f['count'] and (f['pointer'] or f['type'] in model.types) for f in fields)
        if simple:
            for field, (offset, _, _, _) in zip(fields, flat):
                lines.append(f'_Static_assert(__builtin_offsetof({name}, {field["name"]}) == {offset}, "{name}.{field["name"]}");')
    lines.append('int probe;')
    source = SCRATCH / 'layout.c'
    source.write_text('\n'.join(lines) + '\n')
    result = subprocess.run(['clang', *arm64_sdk.CFLAGS, f'-I{sdk / "include"}', '-c', str(source), '-o',
                             str(SCRATCH / 'layout.obj')], capture_output=True, text=True)
    expect(result.returncode == 0, 'the header has the Win64 layout of the table:\n' + result.stderr[:2000])

    # A built program, as a file.
    console = arm64_sdk.build([ROOT / 'sdk/samples/hello.c'], SCRATCH / 'hello.exe')
    headers = readobj('--file-headers', str(console))
    expect('IMAGE_FILE_MACHINE_ARM64' in headers, 'machine is ARM64')
    expect('Format: COFF-ARM64' in headers, 'format is COFF-ARM64')
    expect('IMAGE_SUBSYSTEM_WINDOWS_CUI' in headers, 'a program with main is a console program')
    expect('IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE' in headers, 'the image can be relocated')
    expect('PE32+' in headers or 'Magic: 0x20B' in headers, 'optional header is PE32+')
    relocations = readobj('--coff-basereloc', str(console))
    kinds = {line.split()[-1] for line in relocations.splitlines() if 'Type:' in line}
    expect(kinds <= {'DIR64', 'ABSOLUTE'}, f'only DIR64 relocations, got {sorted(kinds)}')
    imports = readobj('--coff-imports', str(console))
    dlls = {line.split()[-1].upper() for line in imports.splitlines() if line.strip().startswith('Name:')}
    expect(dlls and dlls <= {'KERNEL32.DLL', 'USER32.DLL', 'GDI32.DLL'}, f'imports only from the three DLLs: {sorted(dlls)}')
    names = [line.split()[1] for line in imports.splitlines() if line.strip().startswith('Symbol:')]
    expect(names and all(not n.isdigit() for n in names), 'every import is by name')
    specs = arm64_sdk.read_specs()
    known = {name for (_, name) in specs}
    for name in names:
        expect(name in known or name in WIN64_ONLY, f'import {name} is an export of the Win98 DLL')

    # Every name the libraries offer is one the table can convert, and the
    # other way round.
    offered = set()
    for dll in DLLS:
        text = (sdk / 'lib' / f'{dll}.def').read_text().split('EXPORTS', 1)[1]
        offered |= {line.strip() for line in text.splitlines() if line.strip()}
    described = {f['name'] for f in model.functions}
    # The driver below the ARM64 DirectX DLLs is a DLL of the port, not one of
    # Windows; programs do not import it.
    own = {f['name'] for f in model.functions if f['dll'] == 'NEO98DX.DLL'}
    expect(offered == described - own, 'the import libraries offer exactly the described functions')
    table = arm64_sdk.TABLE.read_text()
    expect(all(f'"{name}"' in table for name in described), 'the table holds every described function')

    # An independent witness, when it is installed: the headers and import
    # libraries of mingw-w64 (Homebrew). Its compilers give the Win32 and the
    # Win64 layout of every struct of sdk/api, its 64-bit import libraries
    # name the exports of 64-bit Windows, and its headers compile the samples.
    witness = independent_witness(model, described - own)
    shutil.rmtree(SCRATCH, ignore_errors=True)
    print(f'ARM64 SDK PASS checks={checks} functions={len(model.functions)} mingw-w64={witness}')


if __name__ == '__main__':
    main()
