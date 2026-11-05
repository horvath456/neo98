# SPDX-License-Identifier: GPL-2.0-or-later
"""Build DirectX comparison guests with installed headers and Win98's MSVCRT.

No MinGW startup/runtime is linked: modern MinGW startup code requires NT.
The fixture enters main through the MSVCRT shipped with Windows 98.
"""
import os
from pathlib import Path
import subprocess
from native_app import ROOT
import neo98


def build(source, output):
    root = Path(os.environ.get('NEO98_MINGW_I686',
                               '/opt/homebrew/opt/mingw-w64/toolchain-i686/i686-w64-mingw32'))
    if not (root / 'include/windows.h').is_file():
        raise RuntimeError(f'Missing i686 mingw-w64 headers at {root}; set NEO98_MINGW_I686')
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    startup = output.with_suffix('.startup.c')
    startup.write_text('''#include <windows.h>
#include <stdarg.h>
__declspec(dllimport) int __cdecl _vsnprintf(char *, unsigned, const char *, va_list);
int __ms_vsnprintf(char *s, unsigned n, const char *format, va_list args) { return _vsnprintf(s, n, format, args); }
__declspec(dllimport) int __cdecl __getmainargs(int *, char ***, char ***, int, int *);
int main(int, char **);
void __main(void) {}
void mainCRTStartup(void) {
    int argc = 0, mode = 0; char **argv = 0, **env = 0;
    if (__getmainargs(&argc, &argv, &env, 0, &mode)) ExitProcess(255);
    ExitProcess(main(argc, argv));
}
''')
    env = neo98.tool_environment()
    objects = []
    for path in (Path(source), startup):
        obj = output.parent / (output.stem + '-' + path.stem + '.obj')
        subprocess.run(['clang', '--target=i686-w64-windows-gnu', f'--sysroot={root}', '-O1',
                        '-march=i686', '-mno-sse', '-fno-stack-protector', '-fno-builtin',
                        '-D_WIN32_WINNT=0x0400', '-DWINVER=0x0410', '-D__MSVCRT_VERSION__=0x0600',
                        '-D__USE_MINGW_ANSI_STDIO=0', '-c', str(path), '-o', str(obj)], env=env, check=True)
        objects.append(obj)
    math_def = output.with_suffix('.math.def')
    math_lib = output.with_suffix('.math.lib')
    math_def.write_text('LIBRARY MSVCRT.DLL\nEXPORTS\n sin\n cos\n')
    subprocess.run(['llvm-dlltool', '-m', 'i386', '-d', str(math_def), '-l', str(math_lib)], env=env, check=True)
    builtins = sorted((root.parent / 'lib/gcc/i686-w64-mingw32').glob('*/libgcc.a'))
    if not builtins:
        raise RuntimeError('Missing i686 GCC builtins for the comparison guests')
    libraries = [math_lib, builtins[-1], *[root / 'lib' / f'lib{name}.a' for name in
                 ('ddraw', 'dsound', 'dxguid', 'winmm', 'gdi32', 'user32', 'kernel32', 'msvcrt-os')]]
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/nodefaultlib', '/machine:x86',
                    '/subsystem:console,4.0', '/osversion:4.0', '/timestamp:0',
                    '/safeseh:no', '/dynamicbase:no', '/nxcompat:no',
                    '/out:' + str(output), *map(str, objects), *map(str, libraries)], env=env, check=True)
    return output
