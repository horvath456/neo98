#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Find likely German prose in an already exported tree.

This is a heuristic, not a language detector. Review every finding. Exceptions
match exact line hashes in specific files, so adding an exception for a guest
text expectation cannot conceal a new untranslated comment in that file.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re

# Escapes keep the detector's own source English.
GERMAN = re.compile(
    r'[\u00e4\u00f6\u00fc\u00c4\u00d6\u00dc\u00df]|'
    r'\b(?:[uU]nd|[nN]icht|[wW]ird|[wW]erden|[fF]uer|[fF]\xfcr|[kK]ein[e]|[kK]ein[e]n|[eE]ine[n]|[eE]inem|[dD]ieser|[dD]ieses|[kK]onnte|[fF]ehlgeschlagen|[lL]aesst|[zZ]urueck|[oO]effnen|[dD]er|[dD]ie|[dD]as|[iI]st|[sS]ind|[mM]it|[oO]hne|[aA]uch|[wW]ieder|[bB]eim|[bB]eide|[oO]de[r]|[eE]ines|[eE]iner|[sS]teht|[bB]leibt|[lL]iest|[gG]ibt|[nN]immt|[mM]uss|[eE]ine|[sS]ein|[sS]ein[e]|[sS]ein[e]n|[aA]lle|[hH]ier|[nN]ur|[nN]och|[nN]ach|[uU]eber|[aA]us|[zZ]um|[zZ]ur|[vV]or|[vV]om|[dD]ann|[wW]enn|[kK]ein|[eE]rst|[zZ]wei|[dD]rei|[dD]urch|[uU]nter|[dD]iese|[dD]iesen|[hH]atte|[aA]bsturzbericht|[vV]ollbild|[aA]uswerfen|[nN]eustart|[bB]eenden|[eE]instellungen|[sS]peichern|[aA]bbrechen|[uU]mbenennen|[eE]igenschaften|[vV]orhandenen|[dD]atei|[fF]ehler|[oO]rdne[r]|[fF]enster|[bB]ereits|[nN]achgetragen|[aA]usgabe|[zZ]eitzone|[tT]reiber|[nN]etzwerk|[dD]ruckausgabe|[oO]ffen|[lL]eerer|[sS]imulationen|[aA]ufrufe|[rR]ahmen|[lL]okal\w*|[uU]nbekannt\w*|[lL]aufwerk\w*|[bB]ericht\w*|[gG]rund\b|[aA]enderung\w*)\b')
DEFAULT_EXCEPTIONS = Path(__file__).with_name('release_language_exceptions.json')


def load_exceptions():
    return json.loads(DEFAULT_EXCEPTIONS.read_text())


def text_findings(name, data, allowed):
    """Findings for one file; data is its raw bytes."""
    if b'\0' in data:
        return []
    try:
        text = data.decode('utf-8')
    except UnicodeDecodeError:
        return []
    result = []
    if GERMAN.search(name):
        result.append({'file': name, 'line': 0, 'sha256': hashlib.sha256(name.encode()).hexdigest()})
    for number, line in enumerate(text.splitlines(), 1):
        if not GERMAN.search(line):
            continue
        digest = hashlib.sha256(line.encode()).hexdigest()
        if digest not in allowed.get(name, {}):
            # Never reproduce text that could contain a secret.
            result.append({'file': name, 'line': number, 'sha256': digest})
    return result


def findings(root, exceptions=None):
    root = Path(root)
    allowed = exceptions if exceptions is not None else load_exceptions()
    result = []
    for path in sorted(root.rglob('*')):
        if not path.is_file() or path.is_symlink():
            continue
        result += text_findings(path.relative_to(root).as_posix(), path.read_bytes(), allowed)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tree', type=Path)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    result = findings(args.tree)
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        for hit in result:
            print(f"{hit['file']}:{hit['line']}: possible German text")
        print(f'{len(result)} language findings')
    return bool(result)


if __name__ == '__main__':
    raise SystemExit(main())
