#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Empty Recycle Bin after a host that did not end in order.

SHELL32 keeps the number of files in the Recycle Bin of a drive in
HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\explorer\\BitBucket, value
"D" (BFF...  writes it after every deletion,  reads it); the
entries themselves are in D:\\RECYCLED\\INFO2. When the count is zero, the
File menu greys "Empty Recycle Bin"  and the link of the web view
(recycle.htt, InvokeVerb) does nothing. The host wrote the registry journal
only when it ended in order; after a crash or a forced end the count was
lost while INFO2 kept the files.

First run: tools/recycle_fixture.c deletes D:\\VICTIM.TXT into the bin, and
the host is killed a few seconds later. Second run: Explorer with the same
registry file and D:, Recycle Bin, the "Empty Recycle Bin" link: the
confirmation must appear.

    build/analysis-venv/bin/python tools/test_recycle_empty_host.py [--no-build]

Results in build/recycle-empty/results.json.
"""
from __future__ import annotations

import json
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's

IMPORTS = {'KERNEL32': ['ExitProcess@4', 'CreateFileA@28', 'WriteFile@20', 'CloseHandle@4'],
           'SHELL32': ['SHFileOperationA@4', 'SHQueryRecycleBinA@8'],
           'USER32': ['wsprintfA']}


def pixels(path):
    magic, size, depth, body = Path(path).read_bytes().split(b'\n', 3)
    return body, int(size.split()[0])


def at(image, x, y):
    body, width = image
    i = (y * width + x) * 3
    return body[i:i + 3].hex()


def fixture(out):
    env = neo98.tool_environment()
    dlltool = shutil.which('llvm-dlltool', path=env['PATH'])
    libs = []
    for dll, names in IMPORTS.items():
        definition = out / f'{dll}.def'
        definition.write_text(f'LIBRARY {dll}.DLL\nEXPORTS\n' + ''.join(n + '\n' for n in names))
        lib = out / f'{dll}.lib'
        subprocess.run([dlltool, '-m', 'i386', '-k', '-d', str(definition), '-l', str(lib)], env=env, check=True)
        libs.append(str(lib))
    obj, exe = out / 'recycle.obj', out / 'RECYCLE.EXE'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                    '-fno-stack-protector', '-c', str(ROOT / 'tools/recycle_fixture.c'), '-o', str(obj)],
                   env=env, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:console', '/timestamp:0', '/nodefaultlib',
                    '/machine:x86', '/out:' + str(exe), str(obj), *libs], env=env, check=True)
    return exe


def main():
    out = neo98.output_directory('recycle-empty')
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)

    exe = fixture(out)
    drive_c, drive_d, registry = out / 'drive-c', out / 'drive-d', out / 'session.registry'
    for path in (drive_c, drive_d):
        shutil.rmtree(path, ignore_errors=True)
    registry.unlink(missing_ok=True)
    subprocess.run(['cp', '-c', '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    drive_d.mkdir()
    shutil.copy(exe, drive_d / 'RECYCLE.EXE')
    shot = out / 'confirm.ppm'
    shot.unlink(missing_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    base = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
            '--drive-c', str(drive_c), '--drive', f'D={drive_d}', '--registry', str(registry), '--quiet',
            '--crash-report-dir', str(out / 'reports')]
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_INPUT', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    # First session: delete, then end the host without the orderly shutdown.
    with (out / 'delete.log').open('w') as log:
        host = subprocess.Popen([*base, '--exe', 'D:\\RECYCLE.EXE'], cwd=ROOT, env=env, stdout=log,
                                stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        deadline = time.time() + 120
        while time.time() < deadline and not (drive_d / 'BBRESULT.TXT').exists():
            time.sleep(0.5)
        time.sleep(8)
        host.send_signal(signal.SIGKILL)
        host.wait()
    result_text = (drive_d / 'BBRESULT.TXT').read_text() if (drive_d / 'BBRESULT.TXT').exists() else ''
    check('file deleted into the bin', 'op=0' in result_text and 'items=1' in result_text)
    check('INFO2 written', (drive_d / 'RECYCLED' / 'INFO2').exists())
    # Second session: Explorer, Recycle Bin, the link of the web view.
    # The script waits for the guest, not for the clock (NEO98_HOST_INPUT of
    # tools/neo98_host.cpp), so a loaded machine only makes it slower.
    steps = ['0:wait=Shell_TrayWnd', '0:idle=2000', '1:dblclick=37,245', '1:wait=CabinetWClass',
             '1:idle=3000', '2:move=130,451', '3:click=130,451', '3:wait=#32770|Confirm File Delete',
             '3:idle=1000', f'5:shot={shot}', '6:menu=quit']
    env2 = dict(env, NEO98_HOST_INPUT=';'.join(steps))
    with (out / 'explorer.log').open('w') as log:
        result = subprocess.run([*base, '--exe', 'C:\\WINDOWS\\EXPLORER.EXE'], cwd=ROOT, env=env2, stdout=log,
                                stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=300)
    check('screenshot written', shot.exists())
    if shot.exists():
        image = pixels(shot)
        # "Confirm File Delete" over the folder window: its caption row at
        # y 209..222 runs from x 192 to 607.
        check('confirmation of Empty Recycle Bin shown',
              at(image, 250, 215) == '000080' and at(image, 550, 215) == '000080')
    check('host ended', result.returncode == 0)
    report = {'checks': checks, 'delete_result': result_text.strip(), 'exit_code': result.returncode}
    (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    failed = [name for name, okay in checks.items() if not okay]
    if failed:
        print(f'RECYCLE EMPTY FAIL: {len(failed)} of {len(checks)} checks; see {out}')
        return 1
    print(f'RECYCLE EMPTY PASS: {len(checks)} checks; {out / "results.json"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
