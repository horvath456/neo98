#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Shared DLL sections expire with their last module owner, including passive peers.

Uses the existing host; run through a shared serial test lock.
"""
from pathlib import Path
import argparse
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98


def build(out, env):
    import pefile
    out.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'tools/session_section_lifetime.c'
    dll = out / 'SECTLIFE.DLL'
    common = ['i686-w64-mingw32-gcc', str(source), '-O1', '-nostartfiles']
    subprocess.run([*common, '-DBUILD_DLL', '-shared', '-Wl,--entry,_DllMain@12',
                    '-Wl,--image-base,0xb6000000', '-o', str(dll)], env=env, check=True)
    pe = pefile.PE(str(dll))
    section = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.shared')
    section.Characteristics |= 0x10000000  # IMAGE_SCN_MEM_SHARED
    pe.write(str(dll))
    pe.close()
    exe = out / 'SECTLIFE.EXE'
    subprocess.run([*common, '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32',
                    '-lkernel32', '-o', str(exe)], env=env, check=True)
    return exe


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cpu', choices=['fex', 'unicorn'], default='fex')
    args = parser.parse_args()
    out = ROOT / 'build/session-section-lifetime' / args.cpu
    env = neo98.tool_environment()
    build(out, env)
    drive = out / 'drive-c'
    drive.mkdir(exist_ok=True)
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_CPU=args.cpu,
               NEO98_HOST_INPUT='', NEO98_HOST_MENU='25:quit', NEO98_HOST_DEADLINE='30',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    run = subprocess.run([str(ROOT / 'build/native-app/neo98-host'), '-',
                          str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                          '--exe', 'E:\\SECTLIFE.EXE', '--drive-readonly', 'E=' + str(out),
                          '--drive-c', str(drive), '--no-registry-file', '--quiet'],
                         env=env, capture_output=True, text=True)
    log = run.stdout + run.stderr
    (out / 'host.log').write_text(log)
    assert run.returncode == 0, log[-3000:]
    assert 'SESSION SECTION LIFETIME PASS' in log, log[-3000:]
    assert 'NEO98 HOST exited code=0' in log, log[-3000:]
    print('SESSION SECTION LIFETIME PASS (' + args.cpu + '): passive peers, two owners, reload, process exit')


if __name__ == '__main__':
    main()
