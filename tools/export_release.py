#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Export release sources into a new, empty directory; reject private content.

Usage: python3 tools/export_release.py DESTINATION [--source CHECKOUT]
Only Git-tracked files; no build products, history, historical evidence or external symlinks are copied.
The checks scan file names and bytes, including binary files.
"""
from __future__ import annotations
import argparse
import ast
import json
import io
import tokenize
import os
from pathlib import Path, PurePosixPath
import posixpath
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DIRECTORIES = {'src', 'tools', 'sdk', 'fex', 'unicorn', 'platform', 'LICENSES'}
FILES = {'CMakeLists.txt', 'neo98.py', 'Run.command', 'COPYING', 'README.md',
         'ARCHITECTURE.md', '.gitignore', '.gitattributes', 'circle-revision.txt',
         'THIRD_PARTY.md', 'NOTICE', 'NOTICE.md', '.github/workflows/release.yml'}
DOCS = {'docs/BUILDING.md', 'docs/ARM64-APPS.md', 'docs/FREEDOS-BOOT.md'}
# Screenshots and the icon shown by README.md. Every other image is rejected.
IMAGES = {'docs/images/icon.png', 'docs/images/desktop.png', 'docs/images/setup.png',
          'docs/images/setup-progress.png', 'docs/images/games.jpg', 'docs/images/seamless.png'}
PATTERNS = [re.compile(p, re.I) for p in (
    rb'andreashorvat[h]', rb'/User[s]/', rb'a[r]m98', rb'pico_it[x]', rb'win95-phon[e]',
    rb'\b[A-Z0-9]{5}(?:-[A-Z0-9]{5}){4}\b', # retail product keys
    rb'\b[0-9]{3}-[0-9]{7}\b', rb'\b[0-9]{5}-OEM-[0-9]{7}-[0-9]{5}\b')]


# Context limits these checks to provenance annotations. Ordinary hexadecimal
# error codes, flag values and executable operands remain valid source text.
TRACE_PATTERNS = {name: re.compile(pattern, re.I) for name, pattern in {
    'named VMM code location': rb'\b(?:VMM[0-9]+|SYSE|RARE|THCR):[0-9a-f]{4,8}\b',
    'module offset list': rb'\b[A-Z][A-Z0-9]*(?:\.(?:DLL|EXE|DRV))\s*[,;:]\s*(?:0x)?[0-9a-f]{4,8}(?:\s*[,;]\s*(?:0x)?[0-9a-f]{4,8})+\b',
    'module segment offsets': rb'\b[A-Z][A-Z0-9]*\.(?:DLL|EXE|DRV)[^\n]{0,60}\bsegment\s+[0-9]+[^\n]{0,30}\boffsets?\s+(?:0x)?[0-9a-f]{4,8}\b',
    'issue reference': rb'\bB[-][0-9]{3,5}\b',
    'segmented system address': rb'\b(?:USER|GDI|KERNEL|KRNL386|DIBENG|DDRAW|DCIMAN|SHELL)(?:16|32)?(?:\.(?:EXE|DLL|DRV))?\s*(?:at\s+)?[0-9a-f]{1,4}:(?:0x)?[0-9a-f]{3,8}\b',
    'segmented address annotation': rb'(?m)(?://|/\*|^[ \t]*#(?!define\b|include\b|if\b|elif\b|endif\b|pragma\b))[^\n]*?(?<![\w.])[0-9]{1,2}:(?:0x)?[0-9a-f]{4,8}\b',
    'system code address annotation': rb'(?m)(?://|/\*|^[ \t]*#(?!define\b|include\b|if\b|elif\b|endif\b|pragma\b))(?![^\n]*\b(?:flags?|mask|HRESULT|error code)\b)[^\n]*\b(?:0x)?(?:BF[0-9a-f]|7F[0-9a-f]|BE[AB])(?!FFFFF\b)[0-9a-f]{5}\b',
    'named original address': rb'\b(?:KERNEL32|USER32|GDI32|SHELL32)(?:\.DLL)?\s+(?:at|@)\s+(?:0x)?[0-9a-f]{8}\b',
    'module location annotation': rb'\b[A-Z][A-Z0-9]*(?:\.(?:DLL|EXE|DRV))\s+(?:at\s+)?(?:0x[0-9a-f]{5,8}|[0-9a-f]{8})\b',
    'library code location annotation': rb'(?m)(?://|/\*|^[ \t]*#(?!define\b|include\b|if\b|elif\b|endif\b|pragma\b))(?![^\n]*\b(?:flags?|mask|HRESULT|error code|pseudo-handle|tokens?|native USER)\b)[^\n]*\b(?:0x)?(?:70|77|78|7E|7F)(?!F{6}\b)[0-9a-f]{6}\b',
    'original instruction claim': rb'(?:matches|follows|copies)\s+the\s+original\s+instruction\s+sequence',
    'original code evidence': rb'\b(?:original|Win98|Windows\s+98)\s+(?:code\s+)?disassembl(?:y|ies)',
    'disassembly provenance claim': rb'\b(?:reconstructed|derived|ported|copied)\s+from\s+(?:the\s+)?disassembl(?:y|ies)\b|\bsee\s+[^\n]{0,60}\bdisassembl(?:y|ies)\b',
    'original instruction excerpt': rb'(?m)(?://|/\*|^[ \t]*#(?!define\b|include\b|if\b|elif\b|endif\b|pragma\b))[^\n]*\boriginal\b[^\n]*\b(?:xor\s+eax|mov\s+[a-z]{2,3},|push\s+ebp|retf\s+0x)',
    'comparison log reference': rb'\b86Box[^\n]{0,100}\b(?:logs?|transcripts?)\b',
    'private bug report': rb'docs/bug[s]/|\bBUG[S]\.md\b',
}.items()}


PROSE_LOCATION = re.compile(
    r'(?<![\w.])[0-9]{1,2}:(?:0x)?[0-9a-f]{4,8}\b|'
    r'\b(?:0x)?(?:7[0-9a-e][0-9a-f]{6}|BF[0-9a-f]{6})\b', re.I)
C_PROSE = re.compile(
    r'R"([^\s()\\]{0,16})\(.*?\)\1"|"(?:\\.|[^"\\])*"|'
    r"'(?:\\.|[^'\\])*'|//[^\n]*|/\*.*?\*/", re.S)


def prose_locations(path, data):
    """Check annotations separately from executable addresses and constants."""
    try:
        text = data.decode('utf-8')
    except UnicodeDecodeError:
        return False
    prose = []
    if path.suffix == '.py':
        try:
            tree = ast.parse(text)
        except SyntaxError:
            return False
        for node in ast.walk(tree):
            if isinstance(node, (ast.Module, ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                doc = ast.get_docstring(node)
                if doc:
                    prose.append(doc)
    elif path.suffix in {'.c', '.cpp', '.h', '.hpp', '.inc', '.mm'}:
        prose = [m.group() for m in C_PROSE.finditer(text)
                 if m.group().startswith(('//', '/*'))]
    elif path.suffix == '.md':
        prose = [text]
    for block in prose:
        for line in block.splitlines():
            if re.search(r'\b(?:flags?|mask|HRESULT|error code|pseudo-handle|tokens?|native USER)\b', line, re.I):
                continue
            if PROSE_LOCATION.search(line):
                return True
    return False


COMMENT_DAMAGE = re.compile(
    r'^[.,;:](?:[ ,;:]*)(?=\s|$)|,,|(?<![\w)])\(\s*\)|'
    r'\w[ \t]+[.,](?=\s|$)')


def comment_prose_violations(path, relative, data):
    """Catch punctuation left behind by removed annotations, only in prose."""
    try:
        text = data.decode('utf-8')
    except UnicodeDecodeError:
        return []
    blocks = []
    if path.suffix == '.py':
        try:
            blocks.extend((token.start[0], token.string[1:])
                          for token in tokenize.generate_tokens(io.StringIO(text).readline)
                          if token.type == tokenize.COMMENT)
            tree = ast.parse(text)
            for node in ast.walk(tree):
                if isinstance(node, (ast.Module, ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                    doc = ast.get_docstring(node, clean=False)
                    if doc is not None:
                        blocks.append((node.body[0].lineno, doc))
        except (SyntaxError, tokenize.TokenError):
            return []
    elif path.suffix in {'.c', '.cpp', '.h', '.hpp', '.inc', '.mm'}:
        for match in C_PROSE.finditer(text):
            value = match.group()
            if value.startswith(('//', '/*')):
                blocks.append((text.count('\n', 0, match.start()) + 1,
                               value[2:-2] if value.startswith('/*') else value[2:]))
    elif path.suffix in {'.s', '.S'}:
        blocks = [(number, line.lstrip()[1:])
                  for number, line in enumerate(text.splitlines(), 1)
                  if line.lstrip().startswith('#')]
    problems = []
    for first, block in blocks:
        fenced = False
        for offset, line in enumerate(block.splitlines()):
            line = re.sub(r'^\s*\* ?', '', line).strip()
            if line.startswith('```'):
                fenced = not fenced
                continue
            if fenced:
                continue
            # Literal examples and function calls are not English punctuation.
            line = re.sub(r'`[^`]*`|"(?:\\.|[^"\\])*"', 'example', line)
            if COMMENT_DAMAGE.search(line):
                problems.append(f'{relative}:{first + offset}: broken comment prose')
    return problems


DOC_REFERENCE = re.compile(rb"\bdocs/[A-Za-z0-9_./-]+\.md\b")


def analysis_violations(path, relative, data):
    """Keep internal binary-analysis scripts and provenance out of releases."""
    problems = []
    script = path.suffix.lower() in {'.java', '.py', '.idc'}
    if script and (path.suffix.lower() == '.idc' or
                   re.search(r'(?:ghid[r]a|ida[_-]script|^Export(?:All|CommonControls|Dibeng|Gui|Winmine)\.java$)', path.name, re.I) or
                   re.search(rb'(?:ghid[r]a\.(?:app|program)|Ghid[r]aScript|analyzeHead[l]ess|'
                             rb'(?m:^\s*(?:from|import)\s+(?:ida[a-z_]*|id[c])\b))', data)):
        problems.append(relative + ': internal analysis script')
    if relative.startswith('LICENSES/'):
        return problems
    try:
        text = data.decode('utf-8')
    except UnicodeDecodeError:
        return problems
    # Compiler/JIT instruction decoders are legitimate. Reject prose about
    # analysing Windows binaries, in either word order and across line breaks.
    action = r'\b(?:decompi[l](?:e[ds]?|ing|ation)|disassemb[l](?:e[ds]?|ing|y|ies)|reverse[- ]engineer(?:ing|ed)?)\b'
    windows = (r'\b(?:Windows(?:\s+98)?|Win9[58]|original|USER(?:32)?|GDI(?:32)?|'
               r'KERNEL(?:32)?|DIBENG|WINMINE|[A-Z0-9_]+\.(?:DLL|EXE|DRV|VXD))\b')
    near = r'[\s\S]{0,160}?'
    blocks = [text]
    if path.suffix == '.py':
        try:
            tokens = list(tokenize.generate_tokens(io.StringIO(text).readline))
            blocks = []
            comments = []
            for token in tokens:
                if token.type == tokenize.COMMENT:
                    comments.append(token.string)
                elif token.type == tokenize.STRING:
                    blocks.append(token.string)
                elif token.type not in {tokenize.NL, tokenize.NEWLINE, tokenize.INDENT, tokenize.DEDENT}:
                    if comments:
                        blocks.append('\n'.join(comments))
                        comments = []
            if comments:
                blocks.append('\n'.join(comments))
        except (tokenize.TokenError, IndentationError):
            pass
    if any(re.search(action + near + windows + '|' + windows + near + action, block, re.I)
           for block in blocks):
        problems.append(relative + ': Windows binary analysis provenance')
    return problems


def entry_violations(relative, data, exists, symlink=False):
    """Check one tree entry by name and content, without touching the disk.

    data is None for a directory. exists(relative_path, directory_ok) tells
    whether a file (or, with directory_ok, also a directory) is in the tree.
    """
    path = PurePosixPath(relative)
    if symlink:
        return [relative + ': symbolic link']
    found = []
    if data is not None:
        if len(data) > 2 * 1024 * 1024:
            found.append(relative + ': file exceeds source size limit')
        if (path.suffix.lower() in {'.exe', '.dll', '.dat', '.xz', '.zip', '.png', '.jpg', '.ppm', '.log', '.ttf', '.fon', '.nls', '.cab', '.dmp', '.state'}
                and relative not in IMAGES):
            found.append(relative + ': binary, snapshot or evidence file')
    content = relative.encode() + (b'\n' + data if data is not None else b'')
    for line in content.splitlines():
        if b'Andreas ' + b'Horvath' in line and b'opyright' not in line:
            found.append(relative + ': personal name outside copyright notice')
    if data is not None:
        for match in sorted(set(DOC_REFERENCE.findall(content))):
            target = match.decode('ascii')
            if not exists(target, False):
                found.append(f'{relative}: missing document link {target}')
    for index, pattern in enumerate(PATTERNS):
        if pattern.search(content):
            # Do not echo a possible secret into build logs.
            found.append(f'{relative}: forbidden pattern {index + 1}')
    if data is not None:
        found += analysis_violations(path, relative, data)
        found += comment_prose_violations(path, relative, data)
    for label, pattern in TRACE_PATTERNS.items():
        if pattern.search(content):
            found.append(f'{relative}: {label}')
    if data is not None and prose_locations(path, data):
        found.append(f'{relative}: code location in prose')
    if data is not None and path.suffix == '.md':
        text = data.decode('utf-8', errors='replace')
        for link in re.findall(r'\]\(([^)]+)\)', text):
            target = link.split('#', 1)[0]
            if not target or re.match(r'[a-z]+:', target):
                continue
            if not exists(posixpath.normpath(posixpath.join(str(path.parent), target)), True):
                found.append(relative + ': missing document link ' + target)
    return found


def violations(root):
    root = Path(root)
    def exists(relative, directory_ok):
        target = root / relative
        return target.exists() if directory_ok else target.is_file()
    found = []
    for path in sorted(root.rglob('*')):
        relative = path.relative_to(root).as_posix()
        symlink = path.is_symlink()
        data = path.read_bytes() if path.is_file() and not symlink else None
        found += entry_violations(relative, data, exists, symlink)
    return found


def excluded_import_violations(relative, data, exclusions):
    """A tool must not outlive an excluded helper it imports."""
    excluded_modules = {Path(name).stem for name in exclusions if name.endswith('.py')}
    try:
        tree = ast.parse(data.decode())
    except (SyntaxError, UnicodeDecodeError, ValueError):
        return []
    for node in ast.walk(tree):
        modules = ([alias.name.split('.')[0] for alias in node.names] if isinstance(node, ast.Import)
                   else [node.module.split('.')[0]] if isinstance(node, ast.ImportFrom) and node.module else [])
        if any(name in excluded_modules for name in modules):
            return [relative + ': imports an excluded tool']
    return []


def export(source, destination):
    source, destination = source.resolve(), destination.resolve()
    if source == destination or source.is_relative_to(destination):
        raise ValueError('Destination must not contain or replace the source tree')
    if destination.exists() and any(destination.iterdir()):
        raise ValueError('Destination must be empty; existing files are never removed')
    exclusions = json.loads((source / 'tools/release_exclusions.json').read_text())
    destination.mkdir(parents=True, exist_ok=True)
    copied = 0
    paths = []
    # Only tracked files: ignored build outputs (dependency files, kernels,
    # maps) in a working checkout must never reach the release.
    listing = subprocess.run(['git', '-C', str(source), 'ls-files', '-z'],
                             check=True, capture_output=True).stdout
    paths = [source / name for name in listing.decode().split('\0') if name]
    for path in sorted(paths):
        relative = path.relative_to(source)
        parts = relative.parts
        name = relative.as_posix()
        if parts[0] not in DIRECTORIES and name not in FILES and name not in DOCS and name not in IMAGES:
            continue
        if any(p in {'.git', '__pycache__', '.pytest_cache', 'build', '.DS_Store'} for p in parts):
            continue
        if name.startswith('tools/state_corpus/') or name in exclusions or path.name.startswith(('bench_', 'measure_')):
            continue
        if (path.suffix.lower() == '.md' and name not in FILES and name not in DOCS
                and parts[0] != 'LICENSES'
                and not path.name.upper().startswith(('LICENSE', 'COPYING', 'NOTICE', 'THIRD_PARTY', 'THIRD-PARTY'))):
            continue
        if path.is_symlink():
            raise ValueError(f'Release source contains a symbolic link: {name}')
        if not path.is_file() or path.suffix in {'.pyc', '.o', '.dylib', '.a'}:
            continue
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        copied += 1
    # A source file that imports a local tool must not outlive its excluded helper.
    problems = []
    for path in sorted((destination / 'tools').rglob('*.py')):
        problems += excluded_import_violations(path.relative_to(destination).as_posix(),
                                               path.read_bytes(), exclusions)
    problems += violations(destination)
    if problems:
        raise ValueError('Export rejected:\n' + '\n'.join(problems))
    return copied


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--source', type=Path, default=ROOT)
    parser.add_argument('--binary', type=Path, help='Also reject embedded Windows fixture bytes')
    args = parser.parse_args()
    try:
        if args.binary:
            sys.path.insert(0, str(Path(__file__).resolve().parent))
            from check_release_embedded import check, check_dependencies
            evidence = check(args.binary, args.source)
            dependencies = Path(str(args.binary) + '.dependencies.json')
            problems = check_dependencies(json.loads(dependencies.read_text()))
            if problems or any(r['full_match_in_host'] for r in evidence):
                raise ValueError('Release binary contains forbidden Windows data or includes')
        count = export(args.source, args.destination)
    except (ValueError, OSError) as error:
        parser.exit(1, f'{error}\n')
    print(f'Exported and checked {count} files')


if __name__ == '__main__':
    main()
