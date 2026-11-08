#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Import verified, pinned host dependencies without a full toolchain."""
from pathlib import Path, PurePosixPath
import hashlib
import json
import shutil
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
PINS = json.loads((ROOT / 'unicorn/dependencies.json').read_text())


# Where each pin is imported to under third_party/.
DESTINATIONS = {'unicorn': 'unicorn', 'dosbox-pure': 'dosbox-pure', 'stb': 'stb'}


def selected(name, parts):
    """Return a relative output path, or None. Never extract archive links."""
    if name == 'stb':
        # The DOS TrueType rasterizer, the Linux media image decoder and
        # their licence; nothing else of the collection.
        if parts in (('stb_truetype.h',), ('stb_image.h',), ('LICENSE',)): return PurePosixPath(*parts)
        return None
    if name == 'dosbox-pure':
        # The libretro core and the DOSBox sources it is built from. The CI
        # descriptions, the pictures of the README and the Visual Studio
        # projects are not part of the build.
        if parts[0] in ('.github', 'images', '.gitlab-ci.yml'): return None
        if parts[-1].endswith(('.sln', '.vcxproj', '.vcxproj.filters')): return None
        return PurePosixPath(*parts)
    if name == 'unicorn':
        if parts[0] in ('bindings', 'tests', 'samples', 'docs', '.github', '.gitlab', 'msvc'): return None
        if len(parts) > 2 and parts[:2] == ('qemu', 'target') and parts[2] != 'i386': return None
        other_tcg = ('arm', 'i386', 'mips', 'ppc', 'riscv', 's390', 'sparc', 'loongarch64')
        if len(parts) > 2 and parts[:2] == ('qemu', 'tcg') and parts[2] in other_tcg: return None
        return PurePosixPath(*parts)
    return None


def ensure(name):
    pin = PINS[name]
    destination = ROOT / 'third_party' / DESTINATIONS[name]
    manifest = destination / 'neo98-import.json'
    if manifest.exists():
        previous = json.loads(manifest.read_text())
        if previous['archive_sha256'] != pin['sha256']: raise RuntimeError(f'{name} dependency pin changed')
        for relative, digest in previous['files'].items():
            path = destination / relative
            if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
                raise RuntimeError(f'{name} dependency differs: {path}')
        if name != 'stb' or 'stb_image.h' in previous['files']:
            return previous
    destination.parent.mkdir(parents=True, exist_ok=True)
    print(f'Importing {name}: {pin["version"]}', flush=True)
    digest = hashlib.sha256()

    class HashedStream:
        def __init__(self, response): self.response = response
        def read(self, size):
            data = self.response.read(size); digest.update(data); return data

    with tempfile.TemporaryDirectory(prefix=f'neo98-{name}-', dir=destination.parent) as directory:
        temporary = Path(directory)
        files = {}
        with urllib.request.urlopen(pin['url'], timeout=60) as response:
            stream = HashedStream(response)
            with tarfile.open(fileobj=stream, mode='r|*') as archive:
                for member in archive:
                    parts = PurePosixPath(member.name).parts
                    if not member.isfile() or len(parts) < 2: continue
                    if member.name.startswith('/') or '..' in parts: raise ValueError('Unsafe dependency archive path')
                    relative = selected(name, parts[1:])
                    if relative is None: continue
                    data = archive.extractfile(member).read()
                    output = temporary / relative
                    output.parent.mkdir(parents=True, exist_ok=True); output.write_bytes(data)
                    files[str(relative)] = hashlib.sha256(data).hexdigest()
            while stream.read(1024 * 1024): pass
        if digest.hexdigest() != pin['sha256']: raise RuntimeError(f'{name} archive checksum mismatch')
        # Merge only our selected files. Keep unrelated existing audit files.
        destination.mkdir(exist_ok=True)
        for relative in files:
            target = destination / relative; target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(temporary / relative, target)
        result = {'version': pin['version'], 'url': pin['url'], 'archive_sha256': pin['sha256'], 'files': files}
        manifest.write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    for name in PINS: ensure(name)
