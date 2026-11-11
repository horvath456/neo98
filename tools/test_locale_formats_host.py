#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The format enumerations answer from the record of the locale asked about.

EnumTimeFormats, EnumDateFormats and EnumCalendarInfo served English (United
States) only and refused every other locale, so a German installation could
not list its own date and time formats. The lists now come from the locale
record of LOCALE.NLS, for every locale of code page 1252.

tools/locale_formats_fixture.c asks for the German and the English lists and
for those of the system locale; the exit code names the first step that
failed.

    build/analysis-venv/bin/python tools/test_locale_formats_host.py [host binary]
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98
IMPORTS = {
    'KERNEL32': {'ExitProcess': 4, 'EnumTimeFormatsA': 12, 'EnumDateFormatsA': 12, 'EnumCalendarInfoA': 16,
                 'GetSystemDefaultLCID': 0, 'GetLastError': 0},
}


def run(binary, out, drive, engine):
    runtime = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   NEO98_HOST_INPUT='', NEO98_HOST_MENU='12:quit', NEO98_HOST_DEADLINE='40')
    runtime.pop('NEO98_CPU', None)
    if engine:
        runtime['NEO98_CPU'] = engine
    result = subprocess.run([str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                             '--system-tree', '--exe', 'E:\\LOCFMT.EXE', '--drive-readonly',
                             'E=' + str(out / 'e'), '--drive-c', str(drive), '--no-registry-file', '--quiet',
                             '--crash-report-dir', str(out / 'reports')],
                            cwd=ROOT, env=runtime, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, errors='replace', timeout=120)
    (out / f'host-{engine or "default"}.log').write_text(result.stdout)
    okay = result.returncode == 0 and 'exited code=0' in result.stdout
    if not okay:
        print('\n'.join(line for line in result.stdout.splitlines()[-400:]
                        if 'exited' in line or 'EXCEPTION' in line or 'Fail' in line)[-1500:])
    return okay


def main():
    out = neo98.output_directory('locale-formats')
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
    obj, exe = out / 'fixture.obj', out / 'e' / 'LOCFMT.EXE'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding',
                    '-fno-stack-protector', '-fno-builtin', '-c', str(ROOT / 'tools/locale_formats_fixture.c'),
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
    checks = {f'the format lists are those of the locale ({engine or "default engine"})': run(binary, out, drive, engine)
              for engine in ('',)}
    shutil.rmtree(drive, ignore_errors=True)
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'LOCALE FORMATS FAIL; see {out}')
        return 1
    print(f'LOCALE FORMATS PASS: {len(checks)} checks')
    return 0


if __name__ == '__main__':
    sys.exit(main())
