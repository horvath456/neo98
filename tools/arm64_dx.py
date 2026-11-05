#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""DirectX for ARM64 programs: the part of the SDK under sdk/dx.

tools/arm64_sdk.py calls in here when it generates the SDK:

    headers(include) ddraw.h, dsound.h and obj.h into build/sdk/include;
                            the interfaces are written from sdk/dx/*.itf
    libraries(sdk,...) dxguid.lib, and the ARM64 system DLLs
                            build/sdk/system/DDRAW.DLL and DSOUND.DLL with
                            their import libraries ddraw.lib and dsound.lib

and tools/native_app.py takes the two DLLs into every runner:

    system_modules build/generated/arm64_system_modules.inc

An interface list names, per interface, its IID and its methods in the order
of the method table. The same list gives the declaration for C (a table of
function pointers), the one for C++ (pure virtual functions) and the call
macros of both.
"""
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DX = ROOT / 'sdk/dx'
# The ARM64 system DLLs: name, sources, what it links besides the SDK.
MODULES = {
    'DDRAW.DLL': {'sources': 'ddraw', 'define': 'NEO98_DDRAW_BUILD'},
    'DSOUND.DLL': {'sources': 'dsound', 'define': 'NEO98_DSOUND_BUILD'},
}


class DxError(Exception):
    pass


def parse_interfaces(path):
    """[('guid', name, text)] and [('interface', name, guid, [(result, method, parameters)])] in source order."""
    items, current = [], None
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.split('#', 1)[0].strip()
        if not line:
            continue
        if m := re.fullmatch(r'guid\s+(\w+)\s+\{([0-9A-Fa-f-]+)\}', line):
            items.append(('guid', m[1], m[2]))
        elif m := re.fullmatch(r'interface\s+(\w+)\s+\{([0-9A-Fa-f-]+)\}', line):
            current = ('interface', m[1], m[2], [])
            items.append(current)
        elif m := re.fullmatch(r'(\w+)\s+(\w+)\((.*)\)', line):
            if not current:
                raise DxError(f'{path.name}:{number}: method before any interface')
            current[3].append((m[1], m[2], m[3].strip()))
        else:
            raise DxError(f'{path.name}:{number}: cannot read {line!r}')
    return items


def guid_fields(text):
    """'6C14DB80-A733-11CE-A521-0020AF0BE560' as the eleven numbers of DEFINE_GUID."""
    parts = text.split('-')
    if [len(p) for p in parts] != [8, 4, 4, 4, 12]:
        raise DxError(f'not a GUID: {text}')
    tail = parts[3] + parts[4]
    return [f'0x{parts[0].upper()}', f'0x{parts[1].upper()}', f'0x{parts[2].upper()}',
            *[f'0x{tail[i:i + 2].upper()}' for i in range(0, 16, 2)]]


def interface_text(items):
    out = []
    for item in items:
        if item[0] == 'guid':
            out.append(f'DEFINE_GUID({item[1]}, {", ".join(guid_fields(item[2]))});')
    out.append('')
    for item in items:
        if item[0] != 'interface':
            continue
        _, name, guid, methods = item
        out.append(f'DEFINE_GUID(IID_{name}, {", ".join(guid_fields(guid))});')
        out += ['#undef INTERFACE', f'#define INTERFACE {name}', f'DECLARE_INTERFACE_({name}, IUnknown)', '{',
                '    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppvObject) PURE;',
                '    STDMETHOD_(ULONG, AddRef)(THIS) PURE;', '    STDMETHOD_(ULONG, Release)(THIS) PURE;']
        for result, method, parameters in methods:
            head = f'STDMETHOD({method})' if result == 'HRESULT' else f'STDMETHOD_({result}, {method})'
            out.append(f'    {head}({"THIS_ " + parameters if parameters else "THIS"}) PURE;')
        out += ['};', '#undef INTERFACE']
        every = [('HRESULT', 'QueryInterface', 'REFIID riid, void **ppvObject'), ('ULONG', 'AddRef', ''),
                 ('ULONG', 'Release', ''), *methods]
        letters = 'abcdefghijklmnop'
        c_lines, cxx_lines = [], []
        for _, method, parameters in every:
            count = len([p for p in parameters.split(',') if p.strip()]) if parameters else 0
            names = ', '.join(letters[:count])
            c_lines.append(f'#define {name}_{method}(p{", " + names if names else ""}) '
                           f'(p)->lpVtbl->{method}(p{", " + names if names else ""})')
            cxx_lines.append(f'#define {name}_{method}(p{", " + names if names else ""}) (p)->{method}({names})')
        out += ['#if !defined(__cplusplus) || defined(CINTERFACE)', *c_lines, '#else', *cxx_lines, '#endif', '']
    return '\n'.join(out)


def expand(text):
    """A header with its interface lists written out."""
    def replace(match):
        return interface_text(parse_interfaces(DX / match[1]))
    return re.sub(r'/\*@interfaces (\S+)@\*/', replace, text)


def sources():
    """Every file the generated parts depend on, for the stamp of the SDK."""
    return [Path(__file__), *sorted(p for p in DX.rglob('*') if p.is_file())]


def headers(include):
    """The headers of sdk/dx/include into the include directory of the SDK."""
    written = []
    for path in sorted((DX / 'include').glob('*.h')):
        text = expand(path.read_text())
        target = include / path.name
        if not target.exists() or target.read_text() != text:
            target.write_text(text)
        written.append(target)
    # The names of the Windows headers these stand for.
    for name in ('unknwn.h', 'guiddef.h', 'ole2.h', 'initguid.h'):
        target = include / name
        text = '/* Generated by tools/arm64_dx.py. */\n' + ('#ifndef INITGUID\n#define INITGUID\n#endif\n' if name == 'initguid.h' else '') + \
            '#include <objbase.h>\n'
        if not target.exists() or target.read_text() != text:
            target.write_text(text)
    return written


def libraries(sdk, cflags, tool):
    """dxguid.lib, the system DLLs and their import libraries. `cflags` are
    the compiler options of the SDK, `tool` finds lld-link."""
    include, lib, system = sdk / 'include', sdk / 'lib', sdk / 'system'
    system.mkdir(parents=True, exist_ok=True)
    lld = tool('lld-link')
    work = lib / 'dx.obj.d'
    work.mkdir(parents=True, exist_ok=True)

    def compile_one(source, flags=()):
        obj = work / (source.parent.name + '.' + source.stem + '.obj')
        subprocess.run(['clang', *cflags, *flags, '-isystem', str(include), f'-I{DX}', '-c', str(source), '-o', str(obj)], check=True)
        return obj

    guid = compile_one(DX / 'dxguid.c')
    subprocess.run([lld, '/lib', '/machine:arm64', f'/out:{lib / "dxguid.lib"}', str(guid)], check=True, capture_output=True)
    libs = [lib / 'start.lib', lib / 'dxguid.lib', lib / 'crt.lib', lib / 'kernel32.lib', lib / 'user32.lib', lib / 'gdi32.lib',
            lib / 'winmm.lib', lib / 'neo98dx.lib']
    for name, module in MODULES.items():
        folder = DX / module['sources']
        objects = [compile_one(source, [f'-D{module["define"]}']) for source in sorted(folder.glob('*.c'))]
        stem = name[:-4].lower()
        subprocess.run([lld, '/machine:arm64', '/nodefaultlib', '/dll', '/entry:_DllMainCRTStartup', f'/def:{folder / (stem + ".def")}',
                        f'/implib:{lib / (stem + ".lib")}', *map(str, objects), *map(str, libs), f'/out:{system / name}'],
                       check=True, capture_output=True)
    for path in work.glob('*.obj'):
        path.unlink()
    work.rmdir()
    return [system / name for name in MODULES]


def system_modules():
    """build/generated/arm64_system_modules.inc for tools/host_arm64_modules.cpp:
    the bytes of the ARM64 system DLLs. Without lld-link, or when the DLLs do
    not build, the table is empty and the runner has no such modules."""
    import arm64_sdk
    out = ROOT / 'build/generated/arm64_system_modules.inc'
    lines = ['// Generated by tools/arm64_dx.py. Do not edit.']
    table = []
    try:
        sdk = arm64_sdk.generate()
        for name in MODULES:
            data = (sdk / 'system' / name).read_bytes()
            symbol = 'arm64_module_' + name[:-4].lower()
            lines.append(f'static const unsigned char {symbol}[] = {{')
            for at in range(0, len(data), 32):
                lines.append('    ' + ','.join(str(b) for b in data[at:at + 32]) + ',')
            lines.append('};')
            table.append(f'    {{"{name}", {symbol}, sizeof {symbol}}},')
    except arm64_sdk.ApiError as error:
        # No linker for ARM64 programs on this machine: a runner without the
        # modules. Any other failure is a failure of the build.
        if 'not found' not in str(error):
            raise
        lines = [lines[0], f'// The system DLLs were not built: {str(error).splitlines()[0]}']
        table = []
    lines.append('static const struct { const char *name; const unsigned char *bytes; unsigned size; } arm64_system_module_table[] = {')
    lines += table or ['    {nullptr, nullptr, 0},']
    lines.append('};')
    text = '\n'.join(lines) + '\n'
    out.parent.mkdir(parents=True, exist_ok=True)
    if not out.exists() or out.read_text() != text:
        out.write_text(text)
    return out


if __name__ == '__main__':
    import sys
    for name in sys.argv[1:] or ['ddraw.h', 'dsound.h']:
        sys.stdout.write(expand((DX / 'include' / name).read_text()))
