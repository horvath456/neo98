#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check that the repository contains only publishable source.

Usage:
  python3 tools/check_release.py            every tracked file in the working tree (CI)
  python3 tools/check_release.py --staged   staged changes only (pre-commit hook)
  python3 tools/check_release.py --staged --all   the whole index

Applies the same content rules as tools/export_release.py without copying
anything: forbidden names and keys, provenance annotations, binary evidence,
excluded private tools, broken document links and untranslated prose.
Findings name the file and the rule, never the matched text.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
import export_release as release  # noqa: E402
import check_release_language as language  # noqa: E402

# Top-level entries a public checkout may contain.
TOP_LEVEL = release.DIRECTORIES | {name for name in release.FILES if '/' not in name} | {
    'docs', '.github', '.githooks'}
# Raster formats a screenshot or extracted resource would use; own .ico/.svg icons are fine.
IMAGE_SUFFIXES = {'.png', '.jpg', '.jpeg', '.gif', '.bmp', '.webp', '.tif', '.tiff'}
BUILD_PARTS = {'__pycache__', '.pytest_cache', 'build', '.DS_Store', 'third_party'}
BUILD_SUFFIXES = {'.pyc', '.o', '.obj', '.dylib', '.so', '.a', '.lib'}


def git(root, *args, data=None):
    return subprocess.run(['git', '-C', str(root), *args], input=data, check=True,
                          capture_output=True).stdout


def path_violations(relative, exclusions):
    """Rules about where a file lives, independent of its content."""
    path = PurePosixPath(relative)
    parts = path.parts
    found = []
    if parts[0] not in TOP_LEVEL:
        found.append(relative + ': not a public top-level entry')
    if parts[0] == '.github' and not (len(parts) == 3 and parts[1] == 'workflows'
                                      and path.suffix in {'.yml', '.yaml'}):
        found.append(relative + ': only workflow files belong in .github')
    if any(part in BUILD_PARTS for part in parts) or path.suffix in BUILD_SUFFIXES:
        found.append(relative + ': build product or cache')
    if (relative in exclusions or relative.startswith('tools/state_corpus/')
            or path.name.startswith(('bench_', 'measure_'))):
        found.append(relative + ': private tool or evidence (tools/release_exclusions.json)')
    if path.suffix.lower() in IMAGE_SUFFIXES and relative not in release.IMAGES:
        found.append(relative + ': image not listed in export_release.IMAGES')
    return found


class Tree:
    """Tracked files of the working tree or of the index."""

    def __init__(self, root, staged):
        self.root = Path(root)
        self.staged = staged
        self.modes = {}
        if staged:
            for record in git(root, 'ls-files', '-s', '-z').split(b'\0'):
                if record:
                    meta, name = record.split(b'\t', 1)
                    mode, sha, _stage = meta.split()
                    self.modes[name.decode()] = (mode.decode(), sha.decode())
        else:
            for name in git(root, 'ls-files', '-z').split(b'\0'):
                name = name.decode()
                if name and os.path.lexists(self.root / name):
                    self.modes[name] = None
        self.directories = set()
        for name in self.modes:
            parent = PurePosixPath(name).parent
            while str(parent) != '.':
                self.directories.add(str(parent))
                parent = parent.parent

    def exists(self, relative, directory_ok):
        return relative in self.modes or (directory_ok and relative in self.directories)

    def symlink(self, relative):
        if self.staged:
            return self.modes[relative][0] == '120000'
        return (self.root / relative).is_symlink()

    def read(self, names):
        """Yield (name, bytes) for regular files, reading blobs in one batch."""
        names = [n for n in names if not self.symlink(n)]
        if not self.staged:
            for name in names:
                yield name, (self.root / name).read_bytes()
            return
        names = [n for n in names if self.modes[n][0] != '160000']  # submodules
        if not names:
            return
        request = ''.join(self.modes[n][1] + '\n' for n in names).encode()
        output = git(self.root, 'cat-file', '--batch', data=request)
        offset = 0
        for name in names:
            header_end = output.index(b'\n', offset)
            _sha, _kind, size = output[offset:header_end].split()
            start = header_end + 1
            end = start + int(size)
            yield name, output[start:end]
            offset = end + 1


def check(root, staged=False, everything=True):
    root = Path(root)
    tree = Tree(root, staged)
    if staged and not everything:
        changed = git(root, 'diff', '--cached', '--name-only', '-z', '--diff-filter=ACMRT')
        names = sorted(n for n in changed.decode().split('\0') if n in tree.modes)
    else:
        names = sorted(tree.modes)
    exclusions_file = 'tools/release_exclusions.json'
    exclusions = {}
    if tree.exists(exclusions_file, False):
        exclusions = json.loads(dict(tree.read([exclusions_file]))[exclusions_file])
    allowed_language = language.load_exceptions()
    found = []
    for name in names:
        found += path_violations(name, exclusions)
        if tree.symlink(name):
            found += release.entry_violations(name, None, tree.exists, symlink=True)
    for name, data in tree.read(names):
        found += release.entry_violations(name, data, tree.exists)
        if name.startswith('tools/') and name.endswith('.py'):
            found += release.excluded_import_violations(name, data, exclusions)
        found += [f"{hit['file']}:{hit['line']}: possible German text"
                  for hit in language.text_findings(name, data, allowed_language)]
    return names, found


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--staged', action='store_true', help='check the index instead of the working tree')
    parser.add_argument('--all', action='store_true', help='with --staged: check the whole index')
    parser.add_argument('--root', type=Path, default=TOOLS.parent)
    args = parser.parse_args()
    names, found = check(args.root, staged=args.staged, everything=args.all or not args.staged)
    for line in found:
        print(line)
    if found:
        print(f'Release check failed: {len(found)} findings in {len(names)} files.', file=sys.stderr)
        return 1
    print(f'Release check passed: {len(names)} files.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
