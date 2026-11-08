#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""real TSP initialization and a subsequent display broadcast."""
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
sys.path[:0] = [str(Path(__file__).resolve().parent), str(Path(__file__).resolve().parents[1])]
import neo98
from native_app import ROOT, build_host
import native_app
from ui_clipboard_test_support import fixture


def main():
    out = ROOT / 'build/tapi-provider-host'
    out.mkdir(parents=True, exist_ok=True)
    env = dict(neo98.tool_environment(), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_DEADLINE='40', NEO98_SETTINGS_DIR=str(out/'settings'), NEO98_LOG_DIR=str(out/'logs'))
    imports = {'KERNEL32': ['ExitProcess@4', 'LoadLibraryA@4', 'GetProcAddress@8', 'FreeLibrary@4'],
               'USER32': ['SendMessageA@16']}
    names = {dll: {name.rsplit('@', 1)[0]: int(name.rsplit('@', 1)[1]) for name in entries}
             for dll, entries in imports.items()}
    program = fixture(out/'fixture', ROOT/'tools/tapi_provider_fixture.c', names)
    host = ROOT/'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    if '--host' in sys.argv: host = Path(sys.argv[sys.argv.index('--host')+1]).resolve()
    results = []
    # The engines of macOS; Linux x64 runs x86 code on the native CPU only.
    engines = native_app.test_engines()
    for cpu, empty in [(engine, empty) for empty in (False, True) for engine in engines]:
        case = cpu + ('-empty' if empty else '')
        if subprocess.run(__import__('test_support').other_hosts_command(), capture_output=True).returncode == 0:
            __import__('test_support').skip('exclusive host access is unavailable: another neo98-host is active')
        if shutil.disk_usage(ROOT).free < 6*1024**3:
            raise SystemExit('STOP: fewer than 6 GiB free')
        private = out / ('c-'+case)
        if private.exists(): shutil.rmtree(private)
        subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(Path(os.environ.get('NEO98_WIN98_ROOT') or __import__('pathlib').Path(__import__('os').environ.get('NEO98_WIN98_ROOT') or ROOT / 'build/win98-c')).resolve()), str(private)], check=True)
        shutil.copyfile(program, private/'TAPITEST.EXE')
        if empty:
            ini = private/'WINDOWS/TELEPHON.INI'
            data, changed = re.subn(rb'(?im)^NumProviders=[^\r\n]*', b'NumProviders=0', ini.read_bytes())
            assert changed == 1
            ini.write_bytes(data)
        with (out/(case+'.log')).open('w') as log:
            p = subprocess.Popen([host, '-', ROOT/'build/generated/notepad-fixedsys.fnt',
                '--exe', 'C:\\TAPITEST.EXE', '--system-tree', private, '--drive-c', private,
                '--no-registry-file'], env=dict(env, NEO98_CPU=cpu), stdout=log, stderr=subprocess.STDOUT)
            try: code = p.wait(timeout=65)
            except subprocess.TimeoutExpired:
                p.terminate(); p.wait(timeout=20); raise
        text = (out/(case+'.log')).read_text(errors='replace')
        shutil.rmtree(private)
        expected = 4 if empty else 0
        assert f'NEO98 HOST exited code={expected} ' in text, (case, code, text[-3500:])
        if not empty:
            assert code == 0 and 'TSP provider id=1 ' in text and 'TSP provider id=2 ' in text, text[-3500:]
        assert 'CRASHREPORT' not in text, text[-3500:]
        results.append({'cpu': cpu, 'empty_provider_list': empty, 'guest_exit': expected, 'status': 'PASS'})
        print(case, 'TAPI provider PASS', flush=True)
    (out/'results.json').write_text(json.dumps({'host_sha256': hashlib.sha256(host.read_bytes()).hexdigest(),
        'revision': neo98.source_revision(),
        'tests': results}, indent=2)+'\n')


if __name__ == '__main__': main()
