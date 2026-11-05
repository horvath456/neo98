#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build pinned Wine URLMon tests with an identical in-guest HTTP fixture."""
import argparse
import difflib
from pathlib import Path
import re
import subprocess

import conformance_campaign as campaign
import wine_conformance as wine

SOURCE_SHA256 = '7e1643dbe45e6d43f55c9687fffe9dd74f792b206170d5b72f89be0ef821879c'
URLS = {'WINE_ABOUT_URL': 'http://localhost:32198/site/about',
        'SHORT_RESPONSE_URL': 'http://localhost:32199/posttest.php'}


def adapt_source(text):
    """Keep original assertion lines and logic; change only two HTTP inputs."""
    for symbol, url in URLS.items():
        pattern = r'static const WCHAR ' + symbol + r'\[\]\s*=\s*\{[^}]+\};'
        matches = list(re.finditer(pattern, text))
        if len(matches) != 1:
            raise ValueError('Pinned URL declaration changed: ' + symbol)
        match = matches[0]
        replacement = 'static const WCHAR ' + symbol + '[] = {' + ','.join(repr(c) for c in url) + ',0};'
        replacement += '\n' * match[0].count('\n')
        text = text[:match.start()] + replacement + text[match.end():]
    before = 'extern IID IID_IBindStatusCallbackHolder;'
    if text.count(before) != 1:
        raise ValueError('Pinned GUID declaration changed')
    return text.replace(before, 'extern const IID IID_IBindStatusCallbackHolder;')


def build(args):
    import json
    out = args.out.resolve()
    if (out / 'manifest.json').exists():
        raise RuntimeError('Use a new output directory; preserve previous test binaries and results')
    archive = out / 'downloads/wine-1.0.tar.gz'
    original_archive = wine.DEFAULT_OUT / 'downloads/wine-1.0.tar.gz'
    if original_archive.exists():
        wine.clone(original_archive, archive)
    wine.build(argparse.Namespace(out=out, groups=[], timeout=args.timeout, cc=args.cc))
    manifest = json.loads((out / 'manifest.json').read_text())
    source = out / 'source'
    src = source / 'dlls/urlmon/tests/url.c'
    if wine.digest(src) != SOURCE_SHA256:
        raise RuntimeError('Wine URLMon source hash differs from the reviewed fixture input')
    group = next(g for g in campaign.inventory(source) if g['name'] == 'urlmon_url')
    generated = out / 'generated'; generated.mkdir()
    original = src.read_text()
    adapted = adapt_source(original)
    translation = generated / 'url.c'
    translation.write_text('#line 1 "url.c"\n' + adapted)
    patch = generated / 'url.patch'
    patch.write_text(''.join(difflib.unified_diff(original.splitlines(True), adapted.splitlines(True),
                                               fromfile='wine-1.0/dlls/urlmon/tests/url.c', tofile='fixture/url.c')))
    startup = generated / 'startup.c'
    startup_text = (wine.SUPPORT / 'startup.c').read_text()
    anchor = '    result = main(argc, argv);'
    if startup_text.count(anchor) != 1:
        raise RuntimeError('Startup fixture anchor changed')
    startup_text = startup_text.replace('void mainCRTStartup(void)',
        'extern int neo98_http_fixture_start(void);\nextern int neo98_http_fixture_stop(void);\n\nvoid mainCRTStartup(void)')
    startup_text = startup_text.replace(anchor,
        '    if (!neo98_http_fixture_start()) { fflush(stdout); ExitProcess(252); }\n' + anchor +
        '\n    if (!neo98_http_fixture_stop()) result = 252;')
    startup.write_text(startup_text)
    flags = manifest['flags'] + ['-D_WIN32_WINNT=0x0501', '-DWINVER=0x0501',
        '-Wno-implicit-function-declaration', '-Wno-incompatible-pointer-types', '-Wno-int-conversion']
    options, extras = campaign.legacy_sdk(source, generated, group)
    fixture = wine.SUPPORT / 'loopback_http.c'
    command = [args.cc, *flags, *options, '-include', str(wine.SUPPORT / 'wine_sdk.h'),
        '-I' + str(generated), '-idirafter', str(source / 'include'), '-DSTANDALONE',
        str(startup), str(translation), str(fixture), *extras, '-o', str(out / 'bin' / group['exe']),
        '-Wl,--start-group', '-lmsvcrt-os', '-lurlmon', '-lole32', '-luuid', '-lws2_32',
        '-lkernel32', '-luser32', '-ladvapi32', '-lmingwex', '-lgcc', '-Wl,--end-group']
    result = subprocess.run(command, capture_output=True, text=True)
    (out / 'build.log').write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError('URLMon fixture build failed; see ' + str(out / 'build.log'))
    details = wine.pe_details(out / 'bin' / group['exe'])
    if details['machine'] != 0x14c:
        raise RuntimeError('Fixture must be native Win32 x86')
    group.update(build_status='BUILT', timeout_ms=args.timeout * 1000, build_command=command,
                 sha256=wine.digest(out / 'bin' / group['exe']), translation_unit_sha256=wine.digest(translation), **details)
    group['runtime_dlls'] = ['MSHTML.DLL', 'WININET.DLL', 'SHLWAPI.DLL', 'OLEAUT32.DLL', 'SHDOCVW.DLL']
    group['input_fixture'] = {'kind': 'in-guest-loopback-http', 'urls': URLS,
        'server_sha256': wine.digest(fixture), 'startup_sha256': wine.digest(startup),
        'patch_sha256': wine.digest(patch),
        'scope': 'Only two HTTP URL constants and the SDK GUID qualifier change; upstream assertions remain intact. New binary requires its own original Win98 reference.'}
    manifest.update(groups=[group], flags=flags,
        supervisor_sha256=campaign.supervisor(out, [group], args.cc, flags),
        supervisor_source_sha256=wine.digest(wine.SUPPORT / 'supervisor.c'))
    wine.save(out / 'manifest.json', manifest)
    print('BUILT', group['name'], group['sha256'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cc', default='i686-w64-mingw32-gcc')
    parser.add_argument('--timeout', type=int, default=180)
    build(parser.parse_args())
