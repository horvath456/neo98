#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Host-menu PIF editing without a running DOS game, including read-only media."""
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc', 'nasm'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
import win98_c


def main():
    out = ROOT / 'build/dos-settings-host-test'
    out.mkdir(parents=True, exist_ok=True)
    fixture = out / 'FIXTURE.EXE'
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/dos_settings_host_fixture.c'),
                    '-nostartfiles', '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32',
                    '-o', str(fixture)], check=True)
    source = win98_c.use_pristine()
    binary = ROOT / 'build/native-app/neo98-host'

    def run(name, readonly, actions, code="b8004ccd21", open_settings=True, failure_action="", missing_core=False, expect_sheet=True, live_dos=False):
        case = out / name
        shutil.rmtree(case, ignore_errors=True)
        case.mkdir()
        drive = case / 'C'
        drive.mkdir()
        programs = case / 'programs'
        programs.mkdir()
        program = programs / 'TEST.COM'
        program.write_bytes(bytes.fromhex(code))
        script = ('1:menu=dos-settings-file=' + str(program) + ';' if open_settings else '') + actions + ';7:menu=quit'
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   NEO98_HOST_INPUT=script, NEO98_HOST_MENU='', NEO98_HOST_DEADLINE='12',
                   NEO98_DOS_FAILURE_ACTION=failure_action,
                   NEO98_SETTINGS_DIR=str(case / 'settings'))
        if missing_core:
            env['NEO98_DOSBOX_CORE'] = str(case / 'missing-core.dylib')
        command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                   '--system-tree', str(source), '--drive-c', str(drive), '--no-registry-file',
                   '--drive-readonly' if readonly else '--drive', 'E=' + str(programs),
                   '--drive-readonly', 'F=' + str(out), '--exe', 'F:\\FIXTURE.EXE', '--seamless', '--quiet']
        result = subprocess.run(command, env=env, cwd=ROOT, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=22)
        (case / 'host.log').write_text(result.stdout)
        assert result.returncode == 0, result.stdout[-2500:]
        if expect_sheet:
            assert 'PIFPAGES file=E:\\TEST.COM pages=' in result.stdout, result.stdout[-2500:]
            if live_dos:
                # The active DOS window owns this sheet on its own fiber;
                # RequestDosSettings does not enqueue the host-side request.
                assert 'DOSVM toolbar button=Properties' in result.stdout, result.stdout[-2500:]
                assert 'title=TEST Properties' in result.stdout, result.stdout[-2500:]
                assert 'modal dialog end result=0' in result.stdout, result.stdout[-2500:]
            else:
                assert 'HOST DOS SETTINGS E:\\TEST.COM result=0' in result.stdout or 'HOST DOS SETTINGS E:\\TEST.COM result=1' in result.stdout, result.stdout[-2500:]
        return case, result.stdout, list(programs.glob('*.PIF')) + list(programs.glob('*.pif'))

    case, log, pifs = run('cancel', False, '2:windows=1;3:button=Cancel')
    assert not pifs and not list((case / 'C').rglob('*.PIF')), 'Cancel created a PIF'
    assert 'title=E:\\TEST.COM Properties' in log, 'No seamless property window'
    case, log, pifs = run('save', False, '1.5:type=Configured;2:button=OK')
    assert len(pifs) == 1 and pifs[0].stat().st_size >= 0x171, log[-2500:]
    assert b'Configured' in pifs[0].read_bytes(), 'The edited settings were not saved'
    assert not list((case / 'C').rglob('*.PIF')), 'PIF was stored away from the program'
    case, log, pifs = run('readonly', True, '1.5:type=Configured;2:button=OK;3:key=0d;4:button=Cancel')
    assert not pifs and not list((case / 'C').rglob('*.PIF')), 'Read-only settings created a PIF'
    assert 'PIFPAGES apply page=0 ok=0' in log, log[-2500:]
    assert 'HOST DOS SETTINGS E:\\TEST.COM result=0' in log, 'Failed save closed the sheet as successful'
    case, log, pifs = run('active', False,
                          '1:guestopen=E:\\TEST.COM;3:menu=dos-settings;4:windows=1;5:button=Cancel',
                          code='ebfe', open_settings=False, live_dos=True)
    assert 'DOSVM' in log and not pifs, log[-2500:]
    case, log, pifs = run('failed', False,
                          '1:guestopen=E:\\TEST.COM;5:windows=1;6:button=Cancel',
                          code='b8014ccd21', open_settings=False, failure_action='settings')
    assert 'NEO98 HOST DOS failure E:\\TEST.COM:' in log, log[-2500:]
    assert not pifs, 'Opening settings from a DOS failure created a PIF'
    case, log, pifs = run('startup-failed', False,
                          '1:guestopen=E:\\TEST.COM;5:windows=1;6:button=Cancel',
                          open_settings=False, failure_action='settings', missing_core=True)
    assert 'NEO98 HOST DOS failure E:\\TEST.COM:' in log and not pifs, log[-2500:]
    retry = out / 'RETRY.COM'
    subprocess.run(['nasm', '-f', 'bin', str(ROOT / 'tools/dos_retry_fixture.asm'), '-o', str(retry)], check=True)
    case, log, pifs = run('retry', False, '1:guestopen=E:\\TEST.COM', code=retry.read_bytes().hex(),
                          open_settings=False, failure_action='retry', expect_sheet=False)
    assert log.count('DOSVM start command=') == 2, log[-2500:]
    assert 'DOSVM finished code=1' in log and 'DOSVM finished code=0' in log, log[-2500:]
    assert not pifs, 'Retry created a PIF'
    print('DOS SETTINGS PASS: seamless sheet, cancel, save, read-only, active game, exit error, failed startup, retry')


if __name__ == '__main__':
    main()
