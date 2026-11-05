#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Prove that no two API names known to the sources share a 32-bit API ID.

Application::Dispatch and every family switch compare only the ID of a name
(src/api_name.h, FNV-1a). That is exact as long as all names that can reach
the native dispatcher have distinct IDs. This test collects them from
  - the specs table (every import that can receive a native gate),
  - API(Name) and CRT("Name") case labels,
  - API_IS(variable, "Name") and API_ID("Name") comparisons,
  - internal calls of the form SomethingApi("Name",...),
and fails on the first collision.

It also reads every switch over an API ID and reports a statement that no case
label can reach: an `if ` line left between two cases compiles, never
runs and would silently drop an API.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATTERNS = [re.compile(r'\bAPI\(\s*(\w+)\s*\)'),
            re.compile(r'\bCRT\(\s*"([^"]+)"\s*\)'),
            re.compile(r'\bAPI_IS\(\s*\w+\s*,\s*"([^"]+)"\s*\)'),
            re.compile(r'\bAPI_ID\(\s*"([^"]+)"\s*\)'),
            re.compile(r'\b\w*Api\(\s*"([^"]+)"')]
SPEC = re.compile(r'\{\s*"[A-Z0-9]+\.DLL"\s*,\s*"([^"]+)"\s*,')


def api_id(name):
    value = 2166136261
    for byte in name.encode('latin-1'):
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def collect():
    names = {}
    for path in sorted((ROOT / 'src').iterdir()):
        if path.suffix not in ('.cpp', '.h', '.inc'):
            continue
        text = path.read_text(encoding='latin-1')
        for pattern in PATTERNS + [SPEC]:
            for name in pattern.findall(text):
                if name not in ('n', 'literal'):  # the macro definitions themselves
                    names.setdefault(name, path.name)
    return names


def blank_literals(text):
    """The text with strings, character literals and comments blanked, same length."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c in '"\'':
            out.append(' ')
            i += 1
            while i < n and text[i] != c:
                if text[i] == '\\':
                    out.append(' ')
                    i += 1
                out.append('\n' if text[i] == '\n' else ' ')
                i += 1
            out.append(' ')
            i += 1
        elif text.startswith('//', i):
            while i < n and text[i] != '\n':
                out.append(' ')
                i += 1
        elif text.startswith('/*', i):
            j = text.find('*/', i) + 2
            out.append(''.join('\n' if ch == '\n' else ' ' for ch in text[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def statements(plain, begin, end):
    """Offsets of the statements directly inside the braces plain[begin]..plain[end]."""
    i = begin + 1
    while i < end:
        while i < end and plain[i] in ' \t\n':
            i += 1
        if i >= end:
            break
        start = i
        if plain[i] == '#':
            while i < end and plain[i] != '\n':
                i += 1
            yield start, i
            continue
        parens = braces = 0
        while i < end:
            c = plain[i]
            if c == '(':
                parens += 1
            elif c == ')':
                parens -= 1
            elif c == '{':
                braces += 1
            elif c == '}':
                braces -= 1
                if not braces and not parens:
                    follow = re.match(r'\s*(else\b|;|\(|,|\))', plain[i + 1:end])
                    if not follow:
                        i += 1
                        break
                    if follow[1] == ';':
                        i += 1 + follow.end()
                        break
            elif c == ';' and not parens and not braces and not re.match(r'\s*else\b', plain[i + 1:end]):
                i += 1
                break
            i += 1
        yield start, i


LABEL = re.compile(r'\s*(API\(\w+\)|API_CASE\("[^"]+"\)|CRT\("[^"]+"\)|case [^:]+:|default\s*:)')


def unreachable_statements():
    found = []
    for path in sorted((ROOT / 'src').iterdir()):
        if path.suffix not in ('.cpp', '.h', '.inc'):
            continue
        text = path.read_text(encoding='latin-1')
        plain = blank_literals(text)
        assert len(plain) == len(text), path
        for match in re.finditer(r'switch\s*\(\s*\w+\.id\s*\)\s*\{', plain):
            begin = end = match.end() - 1
            depth = 0
            while True:
                depth += (plain[end] == '{') - (plain[end] == '}')
                if not depth:
                    break
                end += 1
            previous = None
            for start, stop in statements(plain, begin, end):
                body = text[start:stop]
                if body.startswith('#'):
                    continue
                labelled = False
                while (label := LABEL.match(body)):
                    body, labelled = body[label.end():], True
                body = body.strip()
                if not labelled and previous is not None and re.match(r'(return\b|break\b|\{)', previous):
                    found.append(f'{path.name}:{text.count(chr(10), 0, start) + 1}: {body[:80]!r}')
                if body:
                    previous = body
    return found


def main():
    assert api_id('') == 2166136261 and api_id('a') == 0xe40c292c and api_id('CreateFileA') == api_id('CreateFileA')
    names = collect()
    assert len(names) > 1000, len(names)
    seen = {}
    collisions = []
    for name, origin in sorted(names.items()):
        key = api_id(name)
        if key in seen:
            collisions.append((name, origin, seen[key], names[seen[key]], key))
        seen[key] = name
    for name, origin, other, other_origin, key in collisions:
        print(f'API ID collision {key:08x}: {name} ({origin}) and {other} ({other_origin})')
    if collisions:
        print('Change the FNV offset basis in src/api_name.h and in this test, then rebuild.')
        sys.exit(1)
    orphans = unreachable_statements()
    for orphan in orphans:
        print('Statement without a case label after a finished case:', orphan)
    if orphans:
        sys.exit(1)
    print(f'API IDS PASS {len(names)} names, no collision, no unreachable statement in an API switch')


if __name__ == '__main__':
    main()
