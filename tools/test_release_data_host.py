#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run through serial.py against an already built release host and private C:."""
import argparse
import os
from pathlib import Path
import sys
import subprocess
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parent))
from ui_clipboard_test_support import ROOT, fixture
import native_app
from test_release_smoke import run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--host', type=Path, default=ROOT/'build/native-app/neo98-host')
    parser.add_argument('--output', type=Path, default=ROOT/'build/release-data-host')
    parser.add_argument('--skip-serialui', action='store_true')
    parser.add_argument('--engine', choices=('fex','unicorn','native'), action='append')
    args = parser.parse_args()
    out = args.output; out.mkdir(exist_ok=True)
    imports = {
        'KERNEL32': dict(ExitProcess=4, FormatMessageA=28, MultiByteToWideChar=24, GlobalAlloc=8, GlobalLock=4, GlobalUnlock=4, GetDefaultCommConfigA=12, CommConfigDialogA=12, GetLastError=0),
        'USER32': dict(OpenClipboard=4, EmptyClipboard=0, SetClipboardData=8, GetClipboardData=4, CloseClipboard=0,
                       LoadCursorA=8, LoadIconA=8, CreateWindowExA=48, GetSystemMenu=8, GetMenuItemCount=4, GetMenuStringA=20, DestroyWindow=4, MessageBoxA=16, TrackPopupMenu=28),
    }
    exe = fixture(out / 'fixture', ROOT / 'tools/release_data_fixture.c', imports,
                  defines=['-DRELEASE_DATA_SKIP_SERIALUI'] if args.skip_serialui else [])
    # The engines of macOS; Linux x64 runs x86 code on the native CPU only.
    for engine in (args.engine or native_app.test_engines()):
        case = out / engine; case.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='machine-', dir=case) as temporary:
            drive = Path(temporary) / 'c'
            subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(args.source.resolve()), str(drive)], check=True)
            assert not drive.is_symlink()
            env = dict(os.environ, NEO98_CPU=engine, SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy',
                       NEO98_HOST_PASTEBOARD='unique', NEO98_SETTINGS_DIR=str(case/'settings'),
                       NEO98_LOG_DIR=str(case/'logs'), NEO98_LOG_TO_FILE='0', NEO98_HOST_DEADLINE='90',
                       NEO98_HOST_INPUT=f'0:wait=#32770|Neo98 system message;1:shot={case}/message.ppm;2:button=OK;3:wait=#32768;4:shot={case}/menu.ppm;5:key=1b;6:wait=#32770|COM1 Properties;7:idle=800;8:shot={case}/serialui.ppm;9:button=Cancel')
            text = run([args.host.resolve(), '-', r'C:\WINDOWS\FONTS\VGAFIX.FON', '--quiet', '--offline',
                        '--system-tree', drive, '--drive-c', drive, '--no-registry-file', '--drive-readonly', 'E='+str(exe.parent),
                        '--exe', r'E:\TEST.EXE'], env, case/'host.log')
            assert 'NEO98 HOST exited code=0' in text, text[-1200:]
            suffix = '' if args.skip_serialui else ', SERIALUI'
            print(f'PASS {engine}: FormatMessage, CP1252, cursor, icon, system menu, clipboard{suffix}', flush=True)

if __name__ == '__main__': main()
