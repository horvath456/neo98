#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""ddraw.h, dsound.h and the Direct3D headers of the NEO98 SDK against the
headers of mingw-w64.

The headers under sdk/dx/include are own files of the SDK. A program that
builds against them has to build against the Windows headers as well, and
has to mean the same bytes: this test reads out of the SDK headers

- the size of every structure and the offset of every field,
- the value of every constant and of every member of an enumeration,
- the place of every method in its method table and the size of the table,

by compiling probes with the compiler of the SDK, and has the compiler of
mingw-w64 assert each of them against its own ddraw.h, dsound.h and d3d.h. The
GUIDs are compared as text, DEFINE_GUID against DEFINE_GUID. Then the two
fixtures of tools/test_arm64_dx_native.py are checked against the headers of
mingw-w64, and a C++ program against those of the SDK.

Skipped (and said so) when mingw-w64 is not installed. No ARM64 program runs.

    build/analysis-venv/bin/python tools/test_arm64_dx_headers.py


"""
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import arm64_dx  # noqa: E402
import arm64_sdk  # noqa: E402

OUT = ROOT / 'build/arm64-dx-headers'
HEADERS = ('ddraw.h', 'dsound.h', 'd3dtypes.h', 'd3dcaps.h', 'd3d.h')
INTERFACES = ('ddraw.itf', 'dsound.itf', 'd3d.itf')
# Names the Windows headers of DirectX 7 had and later ones dropped or changed,
# or that the header of mingw-w64 does not carry; each with its reason.
ONLY_HERE = {
    'DIRECTDRAW_VERSION': 'the version a program asks for, not a fact of the interface',
    'DIRECTSOUND_VERSION': 'the version a program asks for, not a fact of the interface',
    'DSBUFFERDESC1': 'checked by its size below; mingw-w64 has it only under DIRECTSOUND_VERSION >= 0x0700',
    'DIRECT3D_VERSION': 'the version a program asks for, not a fact of the interface',
    'D3DLIGHT_RANGE_MAX': 'a call of sqrt, not a constant expression',
    'D3DLIGHT_ALL_FLAGS': 'of the DirectX 7 header; mingw-w64 does not carry it',
}
# Fields the witness cannot name: mingw-w64 takes D3DVECTOR from dsound.h when
# that comes first, where it has x, y and z only. The offsets of x, y and z
# are checked, and the second names share them.
ONLY_HERE_FIELDS = {
    ('D3DVECTOR', 'dvX'), ('D3DVECTOR', 'dvY'), ('D3DVECTOR', 'dvZ'),
}
checks = 0


def expect(condition, text, detail=''):
    global checks
    checks += 1
    if not condition:
        print(f'FAIL {text}\n{detail}')
        sys.exit(1)


def header_text(name):
    return arm64_dx.expand((arm64_dx.DX / 'include' / name).read_text())


def constants(text):
    """The object-like macros of a header that stand for a number."""
    names = []
    for name, value in re.findall(r'^#define (\w+)[ \t]+(.+?)\s*(?:/\*.*)?$', text, re.M):
        if name.startswith(('NEO98_', 'LPDD', 'LPDS', '_FAC', 'DirectDraw', 'DirectSound', 'INTERFACE')) or name in ONLY_HERE:
            continue
        if re.fullmatch(r'[A-Za-z_]\w*', value) and not value.startswith(('S_', 'E_', 'CLASS_E', 'CO_E')):
            continue   # another name, not a value
        if value.startswith('\\'):
            continue
        names.append(name)
    # A definition that runs over several lines.
    names += [name for name in re.findall(r'^#define (\w+) \\$', text, re.M) if not name.startswith('MAKEFOURCC')]
    return names


def structures(text):
    """{name: [field]} of the typedef'd structures, the members of anonymous unions included."""
    found = {}
    for body, name in re.findall(r'typedef struct \w+\n\{\n(.*?)\n\} (\w+)[,;]', text, re.S):
        fields, depth, named = [], 0, 0
        for line in body.split('\n'):
            line = line.strip()
            if line == '{':
                depth += 1
                continue
            if line.startswith('}'):
                depth -= 1
                # `} MultiSampleCaps;` closes a named member: a field of its own.
                member = re.fullmatch(r'\} (\w+);', line)
                if member:
                    fields.append(member[1])
                    named -= 1
                continue
            if line in ('union', 'struct'):
                continue
            match = re.fullmatch(r'[\w ]+?[ *]+(\w+)(?:\[[^\]]+\])?;', line)
            if match and not named:
                fields.append(match[1])
        found[name] = fields
    return found


def enumerators(text):
    """The members of the typedef'd enumerations."""
    names = []
    for body in re.findall(r'typedef enum \w+\n\{\n(.*?)\n\} \w+;', text, re.S):
        names += re.findall(r'^\s*(\w+) = ', body, re.M)
    return names


def named_members(text):
    """Fields that lie inside a named nested struct: not reachable from the outer type."""
    inner = set()
    for block in re.findall(r'struct\n\s*\{\n(.*?)\n\s*\} \w+;', text, re.S):
        inner |= set(re.findall(r'(\w+);', block))
    return inner


def probe_values(expressions, headers):
    """The value of every expression under the headers of the SDK, by its compiler."""
    sdk = arm64_sdk.generate()
    lines = ['#include <windows.h>', '#include <stddef.h>', *[f'#include <{h}>' for h in headers]]
    lines += [f'const long long probe_{i} = (long long)({expression});' for i, expression in enumerate(expressions)]
    source = OUT / 'probe.c'
    source.write_text('\n'.join(lines) + '\n')
    result = subprocess.run(['clang', *arm64_sdk.CFLAGS, '-O0', '-isystem', str(sdk / 'include'), '-S', '-emit-llvm', '-o', '-', str(source)],
                            capture_output=True, text=True)
    expect(result.returncode == 0, 'the probes compile against the headers of the SDK', result.stderr[:3000])
    values = dict(re.findall(r'@probe_(\d+) = [^\n]*? i64 (-?\d+)', result.stdout))
    expect(len(values) == len(expressions), f'every probe has a value: {len(values)} of {len(expressions)}')
    return [int(values[str(i)]) for i in range(len(expressions))]


def guids(text):
    found = {}
    for name, body in re.findall(r'DEFINE_GUID\(\s*(\w+)\s*,([^)]*)\)', text):
        try:
            found[name] = tuple(int(part.strip().rstrip('L'), 0) for part in body.split(','))
        except ValueError:
            continue
    return found


def main():
    shutil.rmtree(OUT, ignore_errors=True)
    OUT.mkdir(parents=True)
    sdk = arm64_sdk.generate()
    text = {name: header_text(name) for name in HEADERS}
    both = '\n'.join(text.values())
    unknown = header_text('objbase.h')

    expressions, labels = [], []
    records = structures(both)
    inner = named_members(both)
    for name, fields in records.items():
        expressions.append(f'sizeof({name})')
        labels.append(f'sizeof({name})')
        if name in ONLY_HERE:
            continue
        for field in fields:
            if field in inner or (name, field) in ONLY_HERE_FIELDS:
                continue
            expressions.append(f'offsetof({name}, {field})')
            labels.append(f'offsetof({name}, {field})')
    names = constants(both) + [n for n in constants(unknown) if n.startswith(('S_', 'E_', 'CLASS_E', 'CO_E', 'REGDB_E'))]
    members = enumerators(both)
    names += members
    for name in names:
        expressions.append(name)
        labels.append(name)
    methods = 0
    interfaces = [item for path in INTERFACES for item in arm64_dx.parse_interfaces(arm64_dx.DX / path)
                  if item[0] == 'interface']
    for _, name, _, listed in interfaces:
        expressions.append(f'sizeof(struct {name}Vtbl)')
        labels.append(f'sizeof(struct {name}Vtbl)')
        for method in ['QueryInterface', 'AddRef', 'Release', *[m[1] for m in listed]]:
            expressions.append(f'offsetof(struct {name}Vtbl, {method})')
            labels.append(f'offsetof(struct {name}Vtbl, {method})')
            methods += 1
    values = probe_values(expressions, HEADERS)
    expect(len(records) >= 60 and len(names) >= 1000 and methods >= 650 and len(members) >= 250,
           f'the probes cover the headers: {len(records)} structures, {len(names)} constants ({len(members)} of them members of '
           f'enumerations), {methods} methods')

    compiler = shutil.which('x86_64-w64-mingw32-gcc')
    if not compiler:
        print(f'ARM64 DX HEADERS SKIP mingw-w64 is not installed (probes={len(expressions)})')
        return
    # The same facts, asserted by the compiler of mingw-w64 against its headers.
    witness = ['#include <windows.h>', '#include <stddef.h>', '#include <ddraw.h>', '#include <dsound.h>', '#include <d3d.h>']
    for label, expression, value in zip(labels, expressions, values):
        if label == 'sizeof(DSBUFFERDESC1)':
            # The description before DirectX 7 is the present one without its GUID.
            witness.append(f'_Static_assert(offsetof(DSBUFFERDESC, guid3DAlgorithm) == {value}, "{label}");')
            continue
        if label in members:
            # An enumeration is an int for the compiler of the SDK and may be
            # unsigned for the other: the 32 bits are the fact.
            witness.append(f'_Static_assert((unsigned)({expression}) == {value & 0xffffffff}u, "{label}");')
            continue
        witness.append(f'_Static_assert((long long)({expression}) == {value}LL, "{label}");')
    source = OUT / 'witness.c'
    source.write_text('\n'.join(witness) + '\n')
    result = subprocess.run([compiler, '-fsyntax-only', '-fmax-errors=0', str(source)], capture_output=True, text=True)
    wrong = sorted(set(re.findall(r'static assertion failed: "([^"]+)"', result.stderr)))
    missing = sorted(set(re.findall(r"error: '(\w+)' undeclared", result.stderr)) |
                     set(re.findall(r"has no member named '(\w+)'", result.stderr)))
    expect(not wrong, f'mingw-w64 agrees with every size, offset and value: {wrong[:20]}')
    expect(not missing, f'mingw-w64 knows every name: {missing[:20]}', result.stderr[:1500])
    expect(result.returncode == 0, 'the witness compiles', result.stderr[:3000])

    # The GUIDs, text against text.
    include = Path(compiler).resolve().parents[1] / 'toolchain-x86_64/x86_64-w64-mingw32/include'
    if not (include / 'ddraw.h').exists():
        include = Path('/opt/homebrew/opt/mingw-w64/toolchain-x86_64/x86_64-w64-mingw32/include')
    theirs = {}
    for name in ('ddraw.h', 'dsound.h', 'd3d.h', 'unknwn.h', 'unknwnbase.h'):
        if (include / name).exists():
            theirs.update(guids((include / name).read_text(errors='replace')))
    ours = guids(both)
    expect(len(ours) >= 45, f'the headers define their GUIDs: {len(ours)}')
    different = sorted(name for name, value in ours.items() if name in theirs and theirs[name] != value)
    absent = sorted(name for name in ours if name not in theirs)
    expect(not different, f'every GUID has the bytes mingw-w64 gives it: {different}')
    expect(not absent, f'mingw-w64 defines every GUID of the SDK headers: {absent}')

    # A program for the headers of the SDK is a program for those of Windows.
    for fixture in ('arm64_ddraw_fixture.c', 'arm64_dsound_fixture.c'):
        result = subprocess.run([compiler, '-fsyntax-only', '-Wall', str(ROOT / 'tools' / fixture)], capture_output=True, text=True)
        expect(result.returncode == 0, f'{fixture} compiles against the headers of mingw-w64', result.stderr[:3000])
    # And C++ reads the same headers: methods as member functions.
    cxx = OUT / 'methods.cpp'
    cxx.write_text('''#include <windows.h>
#include <ddraw.h>
#include <dsound.h>
HRESULT Draw(IDirectDraw7 *dd, IDirectDrawSurface7 *surface, IDirectDrawSurface *old, RECT *rect)
{
    DDSURFACEDESC2 d = {};
    d.dwSize = sizeof d;
    if (FAILED(dd->SetCooperativeLevel(nullptr, DDSCL_NORMAL))) return E_FAIL;
    if (surface->IsLost() == DDERR_SURFACELOST) surface->Restore();
    old->Blt(rect, old, nullptr, DDBLT_WAIT, nullptr);
    return surface->Lock(nullptr, &d, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR, nullptr);
}
HRESULT Sound(IDirectSound *ds, IDirectSoundBuffer *buffer)
{
    DWORD play = 0, write = 0;
    DSBUFFERDESC description = {};
    description.dwSize = sizeof description;
    if (FAILED(ds->SetCooperativeLevel(nullptr, DSSCL_PRIORITY))) return DSERR_GENERIC;
    return buffer->GetCurrentPosition(&play, &write);
}
bool Same(REFIID a) { return a == IID_IDirectDraw7 || IsEqualGUID(a, IID_IDirectSound); }
''')
    result = subprocess.run(['clang++', *arm64_sdk.CFLAGS, *arm64_sdk.CXXFLAGS, '-isystem', str(sdk / 'include'), '-fsyntax-only', str(cxx)],
                            capture_output=True, text=True)
    expect(result.returncode == 0, 'a C++ program compiles against the headers of the SDK', result.stderr[:3000])
    result = subprocess.run([compiler.replace('gcc', 'g++'), '-fsyntax-only', str(cxx)], capture_output=True, text=True)
    expect(result.returncode == 0, 'and against those of mingw-w64', result.stderr[:3000])
    shutil.rmtree(OUT, ignore_errors=True)
    print(f'ARM64 DX HEADERS PASS checks={checks} structures={len(records)} constants={len(names)} methods={methods} guids={len(ours)}')


if __name__ == '__main__':
    main()
