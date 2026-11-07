#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""reload DirectDraw after DDHELP cleans up an orphan DirectSound device.

Run under a shared serial test lock; --system-tree accepts a private Win98/DX9 clone.
--clean is the control that releases DirectSound before exit. Win98 can also
terminate DDHELP after its stale DDRAW callback; the next client must still work.
See the development history for the original-system trace.
No Doom files are needed. This uses the already-built host.
"""
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)
import argparse
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
import native_app
import neo98


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', type=Path, default=ROOT / 'build/native-app/neo98-host')
    parser.add_argument('--system-tree', type=Path)
    parser.add_argument('--cpu', choices=native_app.ENGINE_NAMES, default=native_app.test_engines()[0])
    parser.add_argument('--clean', action='store_true')
    args = parser.parse_args()
    out = ROOT / 'build/ddraw-unload' / (args.cpu + ('-clean' if args.clean else '-orphan'))
    out.mkdir(parents=True, exist_ok=True)
    exe = out / 'DDUNLOAD.EXE'
    env = neo98.tool_environment()
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/ddraw_unload_fixture.c'),
                    '-O1', '-nostartfiles', '-Wl,-e,_mainCRTStartup', '-mwindows',
                    '-lgdi32', '-luser32', '-lkernel32', '-o', str(exe)], check=True, env=env)
    drive = out / 'drive-c'
    drive.mkdir(exist_ok=True)
    source = args.system_tree
    if source is None:
        import dx9_tree
        source = dx9_tree.require_tree()
        if source is None:
            print('DDRAW UNLOAD SKIP: no DirectX 9 system tree')
            return
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT='',
               NEO98_HOST_MENU='45:quit', NEO98_HOST_DEADLINE='60', NEO98_CPU=args.cpu,
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    command = [str(args.host.resolve()), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', str(source), '--exe', 'E:\\DDUNLOAD.EXE',
               '--drive-readonly', 'E=' + str(out), '--drive-c', str(drive),
               '--no-registry-file', '--quiet']
    if args.clean:
        command += ['--command', 'E:\\DDUNLOAD.EXE +']
    # The host deadline handles shutdown without SIGKILL.
    result = subprocess.run(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, errors='replace')
    (out / 'host.log').write_text(result.stdout)
    assert result.returncode == 0, result.stdout[-2200:]
    assert result.stdout.count('DDUNLOAD child pixels PASS') == 3, result.stdout[-2200:]
    assert 'DDUNLOAD done' in result.stdout, result.stdout[-2200:]
    assert 'NEO98 HOST exited code=0' in result.stdout, result.stdout[-2200:]
    faults = re.findall(r'EXCEPTION [^\r\n]*', result.stdout)
    # The same cleanup exception leads to TerminateProcess(..., 5) at
    # DDHELP!0100385F on Win98. Passive mappings must not retain the dead
    # helper's DLL state; three successful clients exercise its replacement.
    assert all(not args.clean and line.startswith('EXCEPTION c0000005 trap 14 at baad8506 ')
               for line in faults), faults
    noexec = re.findall(r'NoExec instruction in entry block: ([0-9A-Fa-f]+)', result.stdout)
    assert all(not args.clean and address.upper() == 'BAAD8506' for address in noexec), noexec
    assert len(noexec) <= len(faults)
    print('DDRAW UNLOAD PASS (' + args.cpu + '): three processes draw and unload; subsequent clients work')


if __name__ == '__main__':
    main()
