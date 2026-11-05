#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build local release assets (macOS: Neo98.dmg, Linux: Neo98-linux-x64.tar.gz)
and complete pinned GPL source archives.

Run from the release checkout. No upload or GitHub operation is performed.
"""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))


def add_bytes(archive, name, data, mode=0o644):
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mode = mode
    archive.addfile(info, io.BytesIO(data))


def source_assets(output):
    from export_release import export
    from build_unicorn_host import patched_sources as unicorn_patches
    from build_dosbox_pure import patched_sources as dos_patches
    from unicorn_dependencies import ensure, PINS
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='release-source-', dir=output) as temporary:
        tree = Path(temporary) / 'neo98'
        export(ROOT, tree)
        with tarfile.open(output / 'Neo98-source.tar.gz', 'w:gz') as archive:
            for path in sorted(tree.rglob('*')):
                if path.is_file():
                    add_bytes(archive, 'neo98/' + path.relative_to(tree).as_posix(), path.read_bytes(),
                              0o755 if os.access(path, os.X_OK) else 0o644)
    for name, patches in [('unicorn', unicorn_patches), ('dosbox-pure', dos_patches)]:
        ensure(name)
        pin = PINS[name]
        cache = ROOT / 'build/release-downloads' / (pin['sha256'] + '.tar.gz')
        cache.parent.mkdir(parents=True, exist_ok=True)
        if not cache.exists():
            with urllib.request.urlopen(pin['url'], timeout=120) as response:
                data = response.read()
            if hashlib.sha256(data).hexdigest() != pin['sha256']:
                raise RuntimeError('Source archive checksum mismatch: ' + name)
            cache.write_bytes(data)
        data = cache.read_bytes()
        if hashlib.sha256(data).hexdigest() != pin['sha256']:
            raise RuntimeError('Cached source checksum mismatch: ' + name)
        with tarfile.open(output / (name + '-source.tar.gz'), 'w:gz') as archive:
            add_bytes(archive, name + '/upstream.tar.gz', data)
            add_bytes(archive, name + '/pin.json', (json.dumps(pin, indent=2)+'\n').encode())
            for path, text in sorted(patches().items()):
                add_bytes(archive, name + '/modified/' + path, text.encode())
            builder = 'tools/build_' + ('unicorn_host' if name == 'unicorn' else 'dosbox_pure') + '.py'
            instructions = (
                f'This archive holds the source code of {name} as it is built into Neo98.\n\n'
                'upstream.tar.gz is the unchanged upstream release. pin.json has its download\n'
                'address and SHA-256 checksum. The modified folder contains the files Neo98\n'
                'changes, in their changed form.\n\n'
                'To build it the way Neo98 does, unpack Neo98-source.tar.gz from the same\n'
                f'release and run python3 neo98.py app. {builder} applies the changes and\n'
                'sets the build flags. docs/BUILDING.md lists what you need to install.\n')
            add_bytes(archive, name + '/BUILD.txt', instructions.encode())
    # The setup toolkit's programs, built unchanged from these archives
    # (tools/setup_toolkit.py, tools/setup_toolkit.json).
    from setup_toolkit import fetch, PINS as TOOLKIT
    for name in ('qemu', 'mtools', 'cabextract'):
        archive = fetch(name)
        suffix = ''.join(archive.suffixes[-2:])
        shutil.copy2(archive, output / f'{name}-{TOOLKIT[name]["version"]}-source{suffix}')
    # FreeDOS boot sources, macros, license and build recipe are also in Neo98-source.
    with tarfile.open(output / 'freedos-boot-source.tar.gz', 'w:gz') as archive:
        for path in sorted((ROOT / 'tools/freedos_boot').rglob('*')):
            if path.is_file(): add_bytes(archive, 'freedos-boot/' + path.relative_to(ROOT / 'tools/freedos_boot').as_posix(), path.read_bytes())
        add_bytes(archive, 'freedos-boot/BUILD.md', (ROOT / 'docs/FREEDOS-BOOT.md').read_bytes())


def notarize(path, profile):
    """Have Apple's notary service check a signed app or disk image, then staple the ticket."""
    path = Path(path)
    with tempfile.TemporaryDirectory(prefix='notarize-') as temporary:
        upload = path
        if path.suffix == '.app':
            upload = Path(temporary) / (path.stem + '.zip')
            subprocess.run(['ditto', '-c', '-k', '--keepParent', str(path), str(upload)], check=True)
        result = subprocess.run(['xcrun', 'notarytool', 'submit', str(upload), '--keychain-profile', profile,
                                 '--wait', '--timeout', '45m', '--output-format', 'json'],
                                check=True, capture_output=True, text=True)
    answer = json.loads(result.stdout)
    if answer.get('status') != 'Accepted':
        log = subprocess.run(['xcrun', 'notarytool', 'log', answer.get('id', ''), '--keychain-profile', profile],
                             capture_output=True, text=True).stdout
        raise RuntimeError(f'Notarization of {path.name} ended with {answer.get("status")}:\n{log}')
    subprocess.run(['xcrun', 'stapler', 'staple', '-q', str(path)], check=True)
    print(f'{path.name}: notarized and stapled')


def checksums(output):
    files = sorted(p for p in output.iterdir() if p.is_file() and p.name != 'SHA256SUMS')
    (output / 'SHA256SUMS').write_text(''.join(
        hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.name + '\n' for p in files))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/release-assets')
    parser.add_argument('--sources-only', action='store_true')
    parser.add_argument('--no-sources', action='store_true', help='only the package (the other platform publishes the sources)')
    parser.add_argument('--checksums-only', action='store_true', help='Refresh after notarization/stapling')
    parser.add_argument('--notarize', metavar='PROFILE',
                        help='notarize the app and the disk image with this notarytool keychain profile; '
                             'NEO98_SIGN_IDENTITY must name a Developer ID Application certificate')
    args = parser.parse_args()
    if args.checksums_only:
        checksums(args.output); return
    if args.output.exists() and any(args.output.iterdir()):
        parser.error('Output must be empty; move previous release assets first')
    args.output.mkdir(parents=True, exist_ok=True)
    if not args.sources_only:
        # Releases never inherit the builder's optional Media Center setting.
        os.environ['NEO98_WITH_HOST_MEDIA'] = '0'
        import setup_toolkit
        if not (setup_toolkit.OUT / 'manifest.json').is_file():
            setup_toolkit.build()
        else:
            setup_toolkit.check(setup_toolkit.OUT)
    if not args.sources_only and sys.platform.startswith('linux'):
        from linux_app import package
        from check_release_bundle import check as check_bundle
        appdir = package(release=True)
        violations = check_bundle(appdir)
        if violations: raise RuntimeError('Release bundle check failed: ' + ', '.join(violations))
        shutil.copy2(ROOT / 'build/Neo98-linux-x64.tar.gz', args.output / 'Neo98-linux-x64.tar.gz')
    elif not args.sources_only:
        if sys.platform != 'darwin': parser.error('Binary packaging requires macOS arm64 or Linux x64')
        from mac_app import build_app, build_dmg, signing_identity
        if args.notarize and signing_identity() == '-':
            parser.error('--notarize needs NEO98_SIGN_IDENTITY set to a Developer ID Application certificate')
        app = build_app()
        from check_release_embedded import check_dependencies
        manifest = ROOT / 'build/native-app/neo98-host.dependencies.json'
        problems = check_dependencies(json.loads(manifest.read_text()))
        if problems: raise RuntimeError('Forbidden release dependencies: ' + ', '.join(problems))
        from check_release_bundle import check as check_bundle
        violations = check_bundle(app)
        if violations: raise RuntimeError('Release bundle check failed: ' + ', '.join(violations))
        frameworks = app / 'Contents/Frameworks'
        allowed = {'libSDL2-2.0.0.dylib', 'libSDL3.dylib', 'libunicorn.2.dylib', 'libdosbox-pure.dylib'}
        unexpected = {p.name for p in frameworks.iterdir()} - allowed
        if unexpected: raise RuntimeError('Review licenses/source coverage for bundled libraries: ' + ', '.join(sorted(unexpected)))
        if args.notarize:
            notarize(app, args.notarize)
        dmg = build_dmg(build=False, app=app)
        if args.notarize:
            subprocess.run(['codesign', '--force', '--sign', signing_identity(), '--timestamp', str(dmg)], check=True)
            notarize(dmg, args.notarize)
        shutil.copy2(dmg, args.output / 'Neo98.dmg')
    if not args.no_sources:
        source_assets(args.output)
    checksums(args.output)
    print('Release assets: ' + str(args.output))


if __name__ == '__main__': main()
