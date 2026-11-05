#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Report relative Markdown links in the repository that do not resolve."""
import os, re, subprocess, sys, urllib.parse
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
LINK = re.compile(r'\]\(([^)\s]+)\)')
files = subprocess.run(['git', '-C', str(ROOT), 'ls-files', '-co', '--exclude-standard', '*.md'],
                       capture_output=True, text=True).stdout.split()
md_bad, other_bad = [], []
for f in files:
    if f.startswith(('third_party/', 'fex/', 'unicorn/', 'platform/', 'build/')):
        continue
    p = ROOT / f
    if not p.is_file():
        continue
    fence = False
    for n, line in enumerate(p.read_text().split('\n'), 1):
        if line.lstrip().startswith('```'):
            fence = not fence
        if fence:
            continue
        for m in LINK.finditer(line):
            t = m.group(1)
            if re.match(r'^[a-z]+:', t) or t.startswith(('#', '<')):
                continue
            path = urllib.parse.unquote(t.split('#')[0])
            if not path:
                continue
            full = os.path.normpath(os.path.join(ROOT, os.path.dirname(f), path))
            if not os.path.exists(full):
                (md_bad if path.endswith('.md') else other_bad).append(f'{f}:{n}: {t}')
print('broken .md links:', len(md_bad))
for b in md_bad:
    print(' ', b)
print('broken other links:', len(other_bad))
if '-v' in sys.argv:
    for b in other_bad:
        print(' ', b)
sys.exit(1 if md_bad else 0)
