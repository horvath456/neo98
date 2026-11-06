#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The bits of a DIB section are the same memory in every process.

Win98 keeps the bits of a DIB section in the shared arena. COMCTL32 relies on
it: the images of an image list are a DIB section, the system image list of
the shell belongs to every process that loads SHELL32, and ImageList_Draw
blends a selected icon straight in the bytes GetObject names. With the bits
in the memory of the process that created the section, Explorer faulted in
COMCTL32 after the dialog "Open With" (rundll32) had grown the
list.

tools/dib_section_shared_fixture.c starts itself a second time. Each of the
two processes creates a section; the other reads and writes it through
GetObject and through the pointer alone, and both look at the result, the
parent also through GetPixel. The exit code names the first step that failed.

    build/analysis-venv/bin/python tools/test_dib_section_shared_host.py [host binary]
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98
import arm64_run
IMPORTS = {
    'KERNEL32': {'ExitProcess': 4, 'GetModuleFileNameA': 12, 'CreateProcessA': 40, 'CreateFileMappingA': 24,
                 'OpenFileMappingA': 12, 'MapViewOfFile': 20, 'CreateEventA': 16, 'SetEvent': 4,
                 'WaitForSingleObject': 8, 'GetExitCodeProcess': 8},
    'GDI32': {'CreateDIBSection': 24, 'GetObjectA': 12, 'CreateCompatibleDC': 4, 'SelectObject': 8, 'GetPixel': 12},
}


def run(binary, out, drive, engine):
    runtime = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   NEO98_HOST_INPUT='', NEO98_HOST_MENU='12:quit', NEO98_HOST_DEADLINE='40')
    runtime.pop('NEO98_CPU', None)
    if engine:
        runtime['NEO98_CPU'] = engine
    result = arm64_run.run([str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                             '--system-tree', '--exe', 'E:\\DIBSHARE.EXE', '--drive-readonly',
                             'E=' + str(out / 'e'), '--drive-c', str(drive), '--no-registry-file', '--quiet',
                             '--crash-report-dir', str(out / 'reports')],
                            cwd=ROOT, env=runtime, timeout=120)
    log = result.stdout + result.stderr
    (out / f'host-{engine or "default"}.log').write_text(log)
    okay = not result.timed_out and result.returncode == 0 and 'exited code=0' in log
    if not okay:
        print('\n'.join(line for line in log.splitlines()[-400:]
                        if 'exited' in line or 'EXCEPTION' in line or 'Fail' in line)[-1500:])
    return okay


def main():
    out = neo98.output_directory('dib-section-shared')
    (out / 'e').mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    dlltool = shutil.which('llvm-dlltool', path=env['PATH'])
    assert dlltool
    libraries = []
    for dll, names in IMPORTS.items():
        definition, library = out / f'{dll}.def', out / f'{dll}.lib'
        definition.write_text(f'LIBRARY {dll}.DLL\nEXPORTS\n' +
                              ''.join(f'{name}@{size}\n' for name, size in names.items()))
        subprocess.run([dlltool, '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)],
                       env=env, check=True)
        libraries.append(str(library))
    obj, exe = out / 'fixture.obj', out / 'e' / 'DIBSHARE.EXE'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding',
                    '-fno-stack-protector', '-fno-builtin', '-c', str(ROOT / 'tools/dib_section_shared_fixture.c'),
                    '-o', str(obj)], env=env, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/nodefaultlib',
                    '/machine:x86', '/timestamp:0', '/out:' + str(exe), str(obj), *libraries],
                   env=env, check=True)
    binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'build/native-app/neo98-host'
    import win98_c
    win98_c.use_pristine()
    drive = out / 'drive-c'
    shutil.rmtree(drive, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive)], check=True)
    # Both engines: FEXCore shares host pages of 16 KiB, Unicorn guest pages.
    # Linux x64 has one engine, the native CPU.
    engines = ('',) if sys.platform == 'linux' else ('', 'unicorn')
    checks = {f'two processes share both sections ({engine or "default engine"})': run(binary, out, drive, engine)
              for engine in engines}
    shutil.rmtree(drive, ignore_errors=True)
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'DIB SECTION SHARED FAIL; see {out}')
        return 1
    print(f'DIB SECTION SHARED PASS: {len(checks)} checks')
    return 0


if __name__ == '__main__':
    sys.exit(main())
