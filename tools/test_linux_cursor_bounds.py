#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify cursor restoration after an off-screen position or desktop resize."""
import subprocess
from native_app import ROOT, build_fast
import win98_c

def main():
    win98_c.use_pristine()
    binary=build_fast('test_cursor_bounds.cpp','test-cursor-bounds')
    run=subprocess.run([str(binary),'--ram-drive','--no-registry-file'],cwd=ROOT,capture_output=True,text=True,timeout=30)
    (ROOT/'build/native-linux/cursor-bounds.log').write_text(run.stdout+run.stderr)
    assert run.returncode==0,run.stdout+run.stderr
    assert not win98_c.pristine_problems(win98_c.FOLDER)
    print(run.stdout.strip())

if __name__=='__main__':main()
