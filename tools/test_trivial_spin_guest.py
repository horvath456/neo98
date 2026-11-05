#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""An API-only guest loop must yield and keep TLS isolated between threads."""
from pathlib import Path
import os
import re
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
import neo98
from native_app import build_fast
from ui_clipboard_test_support import fixture
import win98_c
win98_c.use_pristine()
out = ROOT / 'build/trivial-spin'
out.mkdir(parents=True, exist_ok=True)
exe = fixture(out, ROOT / 'tools/trivial_spin_fixture.c', {'KERNEL32': {
    'CreateThread': 24, 'WaitForSingleObject': 8, 'Sleep': 4, 'ExitProcess': 4,
    'GetLastError': 0, 'SetLastError': 4, 'TlsGetValue': 4, 'TlsSetValue': 8}})
runner = build_fast('probe_application.cpp', 'probe-trivial-spin')
for mode, extra in [('direct', []), ('gated', ['--no-fast-gates']), ('traced', ['--trace-calls'])]:
    path = out / (mode + '.log')
    with path.open('w') as log:
        p = subprocess.Popen([str(runner), str(exe), str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                              '--time-limit', '15', *extra], env=neo98.tool_environment(),
                             stdout=log, stderr=subprocess.STDOUT)
        try: code = p.wait(timeout=45)
        except subprocess.TimeoutExpired:
            p.terminate(); p.wait()
            raise AssertionError('Guest did not yield; terminated own runner')
    text = path.read_text(errors='replace')
    assert code == 0 and re.search(r'PROBE started=1 state=2 code=0 ', text), text[-2000:]
    assert 'PROBE_STOP reason=exited' in text
    print('PASS', mode, 'API-loop preemption and thread-local storage')
