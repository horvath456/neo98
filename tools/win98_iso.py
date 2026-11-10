#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded ISO9660 inspection and Windows 98 SE content validation.

Only cabextract is required beyond Python's standard library. Profiles pin the
original files used by the native runtime; matching setup metadata alone does
not establish compatibility. No image is mounted or modified.
"""
from pathlib import Path
import hashlib
import json
import re
import selectors
import shutil
import struct
import subprocess
import tempfile
import time

SECTOR = 2048
MAX_DIRECTORY = 4 << 20
MAX_CAB_INDEX = 2 << 20
MAX_CAB_TOTAL = 256 << 20
MAX_FILE = 16 << 20
EXTRACT_TIMEOUT = 30
MIN_FREE_SPACE = 8 << 30
MANIFEST = Path(__file__).with_name('win98_isos.json')
SAFE_NAME = re.compile(r'[A-Z0-9_][A-Z0-9_.-]{0,126}\Z')


class CompatibilityError(RuntimeError):
    """The source image cannot be validated against a supported profile."""


def _fail(message):
    raise CompatibilityError(message)


def _read(image, offset, size):
    image.seek(offset)
    data = image.read(size)
    if len(data) != size:
        _fail('Truncated ISO or cabinet data')
    return data


def _both(data, offset, width):
    a = int.from_bytes(data[offset:offset + width], 'little')
    b = int.from_bytes(data[offset + width:offset + 2 * width], 'big')
    if a != b:
        _fail('Invalid ISO9660 byte-order fields')
    return a


def setup_files(iso: Path) -> dict:
    """Return uppercase WIN98 names mapped to (LBA, size, flags)."""
    with Path(iso).open('rb') as image:
        image.seek(0, 2)
        image_size = image.tell()
        pvd = None
        for sector in range(16, 80):
            descriptor = _read(image, sector * SECTOR, SECTOR)
            if descriptor[1:7] != b'CD001\x01':
                _fail('Invalid ISO9660 volume descriptor')
            if descriptor[0] == 1:
                if pvd is not None:
                    _fail('Duplicate ISO9660 primary volume descriptor')
                pvd = descriptor
            elif descriptor[0] == 255:
                break
        else:
            _fail('Missing ISO9660 descriptor terminator')
        if pvd is None or _both(pvd, 128, 2) != SECTOR:
            _fail('Missing or invalid ISO9660 primary volume')
        limit = _both(pvd, 80, 4) * SECTOR
        if limit > image_size or limit < (sector + 1) * SECTOR:
            _fail('Truncated or invalid ISO9660 volume size')
        if _both(pvd, 120, 2) != 1 or _both(pvd, 124, 2) != 1:
            _fail('Multi-volume ISO9660 images are unsupported')

        def record(data):
            if len(data) < 34 or data[0] != len(data):
                _fail('Malformed ISO9660 directory record')
            n = data[32]
            if not n or 33 + n + (n % 2 == 0) > len(data):
                _fail('Malformed ISO9660 filename record')
            lba, size = _both(data, 2, 4), _both(data, 10, 4)
            flags = data[25]
            if data[1] or data[26] or data[27] or flags & ~3:
                _fail('Unsupported ISO9660 extended, interleaved or multi-extent file')
            if _both(data, 28, 2) != 1:
                _fail('Invalid ISO9660 volume sequence')
            if lba * SECTOR > limit or size > limit - lba * SECTOR:
                _fail('ISO9660 extent exceeds the declared volume')
            raw = data[33:33 + n]
            if raw in (b'\0', b'\1'):
                return None, (lba, size, flags)
            try:
                name = raw.decode('ascii').upper()
            except UnicodeDecodeError:
                _fail('Non-ASCII ISO9660 filename')
            if name.endswith(';1'):
                name = name[:-2]
            if not SAFE_NAME.fullmatch(name) or name in ('.', '..') or '..' in name:
                _fail(f'Unsafe ISO9660 filename: {raw!r}')
            return name, (lba, size, flags)

        def directory(entry):
            lba, size, flags = entry
            if not flags & 2 or size > MAX_DIRECTORY or not size:
                _fail('Invalid or oversized ISO9660 directory')
            data = _read(image, lba * SECTOR, size)
            result = {}
            at = 0
            while at < size:
                length = data[at]
                if not length:
                    end = min((at // SECTOR + 1) * SECTOR, size)
                    if any(data[at:end]):
                        _fail('Malformed ISO9660 directory padding')
                    at = end
                    continue
                if length > SECTOR - at % SECTOR or at + length > size:
                    _fail('Truncated ISO9660 directory record')
                name, item = record(data[at:at + length])
                if name is not None:
                    if name in result:
                        _fail(f'Duplicate ISO9660 filename: {name}')
                    result[name] = item
                at += length
            return result

        _, root = record(pvd[156:156 + pvd[156]])
        entries = directory(root)
        if 'WIN98' not in entries:
            _fail('ISO has no WIN98 setup directory')
        files = directory(entries['WIN98'])
        for name in ('SETUP.EXE', 'SMARTDRV.EXE'):
            if name not in files or files[name][2] & 2 or not files[name][1]:
                _fail(f'WIN98 is missing a normal file {name}')
        return files


def _cabinet(data, size, name):
    """Read the bounded CAB index, without decompressing any file."""
    if len(data) < 36 or data[:4] != b'MSCF':
        _fail(f'Invalid cabinet header: {name}')
    total, offset = struct.unpack_from('<I4xI', data, 8)
    folders, count, flags, set_id, number = struct.unpack_from('<5H', data, 26)
    if total != size or data[24:26] != b'\x03\x01' or flags & ~7:
        _fail(f'Truncated or invalid cabinet: {name}')
    at, folder_reserve = 36, 0

    def need(n):
        if at + n > len(data):
            _fail(f'Truncated or oversized cabinet index: {name}')

    def string():
        nonlocal at
        end = data.find(b'\0', at, min(at + 256, len(data)))
        if end < 0:
            _fail(f'Invalid cabinet name in {name}')
        raw = data[at:end]
        at = end + 1
        return raw

    if flags & 4:
        need(4)
        reserve, folder_reserve, _ = struct.unpack_from('<HBB', data, at)
        at += 4
        need(reserve)
        at += reserve
    links = []
    for bit in (1, 2):
        link = None
        if flags & bit:
            try:
                link = string().decode('ascii').upper()
            except UnicodeDecodeError:
                _fail(f'Invalid cabinet chain name in {name}')
            if not SAFE_NAME.fullmatch(link) or '..' in link or not link.endswith('.CAB'):
                _fail(f'Unsafe cabinet chain name in {name}')
            string()  # Disk label, never used as a path.
        links.append(link)
    need(folders * (8 + folder_reserve))
    if offset < at + folders * (8 + folder_reserve) or offset > len(data):
        _fail(f'Invalid cabinet file table: {name}')
    at = offset
    files = []
    for _ in range(count):
        need(16)
        length, _, folder = struct.unpack_from('<IIH', data, at)
        if folder >= folders and folder not in (0xfffd, 0xfffe, 0xffff):
            _fail(f'Invalid cabinet folder in {name}')
        at += 16
        raw = string()
        # Noncritical cabinets can contain subdirectories. They are never
        # extracted; only exact safe profile basenames reach cabextract.
        files.append((raw.decode('cp1252').upper(), length, folder))
    return {'previous': links[0], 'next': links[1], 'set': set_id,
            'number': number, 'files': files}


def _cabinet_index(image, files):
    result = {}
    total = 0
    for name, (lba, size, flags) in files.items():
        if name.endswith('.CAB'):
            if flags & 2:
                _fail(f'Cabinet is a directory: {name}')
            total += size
            if total > MAX_CAB_TOTAL or len(result) >= 256:
                _fail('Setup cabinets exceed the validation disk limit')
            result[name] = _cabinet(_read(image, lba * SECTOR, min(size, MAX_CAB_INDEX)), size, name)
    for name, info in result.items():
        for direction, step in (('previous', -1), ('next', 1)):
            other = info[direction]
            if other is None:
                continue
            if other not in result:
                _fail(f'Missing cabinet chain member {other}, required by {name}')
            peer = result[other]
            # Original SE media has backward links that skip one cabinet,
            # and CATALOG3 links back into the separate PRECOPY set.
            # Require monotonic numbering, not reciprocal links or Set IDs.
            if ((step == 1 and peer['number'] != info['number'] + 1) or
                    (step == -1 and peer['number'] >= info['number'])):
                _fail(f'Inconsistent cabinet chain: {name}, {other}')
    return result


def _extract(cabinet, filename, expected_size):
    """Pipe one selected file with bounded memory, diagnostics and runtime."""
    if not SAFE_NAME.fullmatch(filename) or not 0 < expected_size <= MAX_FILE:
        _fail(f'Invalid extraction request for {filename}')
    try:
        process = subprocess.Popen(['cabextract', '-q', '-p', '-F', filename, str(cabinet)],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   cwd=cabinet.parent)
    except FileNotFoundError:
        _fail('cabextract is required to validate this ISO')
    output, errors = bytearray(), bytearray()
    deadline = time.monotonic() + EXTRACT_TIMEOUT
    try:
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ, output)
            selector.register(process.stderr, selectors.EVENT_READ, errors)
            while selector.get_map():
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    _fail(f'Cabinet extraction timed out for {filename}')
                for key, _ in selector.select(min(remaining, .2)):
                    chunk = key.fileobj.read1(65536)
                    if not chunk:
                        selector.unregister(key.fileobj)
                        continue
                    key.data.extend(chunk)
                    if len(output) > expected_size or len(errors) > 65536:
                        _fail(f'Cabinet extraction exceeded limits for {filename}')
            try:
                status = process.wait(timeout=max(.001, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                _fail(f'Cabinet extraction timed out for {filename}')
        if status or len(output) != expected_size:
            detail = errors.decode('utf-8', 'replace').strip()[:500]
            _fail(f'Cannot extract {filename}: truncated/missing cabinet data. {detail}')
        return bytes(output)
    finally:
        if process.poll() is None:
            process.kill()
        process.wait()
        process.stdout.close()
        process.stderr.close()


def _sources(index, filename):
    # MINI.CAB holds the separate minimal setup environment, including a
    # different USER.EXE. It is not a source for installed system files.
    matches = [(name, size, folder) for name, info in index.items()
               for item, size, folder in info['files']
               if item == filename and name != 'MINI.CAB']
    if not matches:
        return None
    if len({index[name]['set'] for name, _, _ in matches}) != 1:
        _fail(f'Duplicate cabinet file: {filename}')
    matches.sort(key=lambda item: index[item[0]]['number'])
    if len(matches) > 1:
        expected = [0xfffe] + [0xffff] * (len(matches) - 2) + [0xfffd]
        if ([m[2] for m in matches] != expected or
                len({m[1] for m in matches}) != 1 or
                any(index[a[0]]['next'] != b[0] for a, b in zip(matches, matches[1:]))):
            _fail(f'Duplicate or broken cabinet continuation: {filename}')
    elif matches[0][2] >= 0xfffd:
        _fail(f'Missing cabinet continuation for {filename}')
    return matches[0][0], matches[0][1]


def _content_files(iso, files, wanted):
    with Path(iso).open('rb') as image:
        index = _cabinet_index(image, files)
        sources = {name: _sources(index, name) for name in wanted}
        needed = set()
        pending = [source[0] for source in sources.values() if source]
        while pending:
            name = pending.pop()
            if name in needed:
                continue
            needed.add(name)
            pending.extend(index[name][d] for d in ('previous', 'next') if index[name][d])
        required = sum(files[name][1] for name in needed)
        # Bundled setup scripts live inside the signed application. Keep
        # temporary cabinet copies in the user's temporary directory.
        if shutil.disk_usage(tempfile.gettempdir()).free < required + MIN_FREE_SPACE:
            _fail('Not enough free space for private cabinet validation')
        with tempfile.TemporaryDirectory(prefix='win98-iso-') as tmp:
            target = Path(tmp)
            for name in sorted(needed):
                lba, size, _ = files[name]
                if shutil.disk_usage(target).free < size + MIN_FREE_SPACE:
                    _fail('Not enough free space for private cabinet validation')
                image.seek(lba * SECTOR)
                with (target / name).open('wb') as out:
                    left = size
                    while left:
                        chunk = image.read(min(left, 1 << 20))
                        if not chunk:
                            _fail(f'Truncated cabinet {name}')
                        out.write(chunk)
                        left -= len(chunk)
            result = {}
            for name in wanted:
                direct = None
                if name in files:
                    lba, size, flags = files[name]
                    if flags & 2 or not 0 < size <= MAX_FILE:
                        _fail(f'Invalid setup file: {name}')
                    direct = _read(image, lba * SECTOR, size)
                source = sources[name]
                cabinet = _extract(target / source[0], name, source[1]) if source else None
                if direct is not None and cabinet is not None and direct != cabinet:
                    _fail(f'Loose and cabinet copies disagree for {name}')
                if direct is None and cabinet is None:
                    _fail(f'Missing required setup file {name}')
                result[name] = direct if direct is not None else cabinet
            return result


def _metadata(data):
    section, values = '', {}
    for line in data.decode('cp1252').splitlines():
        line = line.split(';', 1)[0].strip()
        if line.startswith('[') and line.endswith(']'):
            section = line[1:-1].strip().upper()
        elif '=' in line:
            key, value = line.split('=', 1)
            key = key.strip().upper()
            if ((section == 'STRINGS' and key in ('OS_VERSION', 'LANG')) or
                    (section == 'DATA' and key == 'PRODUCTTYPE')):
                if key in values:
                    _fail(f'Duplicate SETUPPP.INF {key}')
                values[key] = value.strip().strip('"')
    return values.get('OS_VERSION'), values.get('LANG'), values.get('PRODUCTTYPE')


def content_record(iso: Path, *, digest=None) -> dict:
    """Validate unknown media against the pinned SE files of its language and return provenance."""
    iso = Path(iso)
    profiles = json.loads(MANIFEST.read_text())['content_profiles']
    files = setup_files(iso)
    metadata = _content_files(iso, files, ['SETUPPP.INF'])
    version, language, product_type = _metadata(metadata['SETUPPP.INF'])
    languages = sorted({p['language'] for p in profiles})
    if language not in languages:
        _fail(f'Unsupported SETUPPP.INF language {language!r}; expected ' + ' or '.join(languages))
    candidates = [p for p in profiles if p['os_version'] == version and p['language'] == language]
    if not candidates:
        _fail(f'Unsupported SETUPPP.INF OS_VERSION {version!r}; expected Windows 98 SE 4,10,0,2222')
    if any(product_type in p.get('upgrade_product_types', []) for p in candidates):
        _fail('Windows 98 SE Upgrade media is unsupported; use validated Full install media')
    candidates = [p for p in candidates if product_type in p.get('full_product_types', [])]
    if not candidates:
        _fail(f'Unsupported SETUPPP.INF ProductType {product_type!r}; expected validated Full install media')
    wanted = sorted({name for profile in candidates for name in profile['files']})
    contents = _content_files(iso, files, wanted)
    mismatches = []
    for profile in candidates:
        mismatch = next((name for name, expected in profile['files'].items()
                         if len(contents[name]) != expected['size'] or
                         hashlib.sha256(contents[name]).hexdigest() != expected['sha256']), None)
        if mismatch:
            mismatches.append(mismatch)
            continue
        if digest is None:
            hasher = hashlib.sha256()
            with iso.open('rb') as image:
                for chunk in iter(lambda: image.read(1 << 20), b''):
                    hasher.update(chunk)
            digest = hasher.hexdigest()
        if not isinstance(digest, str) or not re.fullmatch('[0-9a-f]{64}', digest):
            _fail('Invalid ISO SHA-256 digest')
        return {'name': profile['name'], 'size': iso.stat().st_size,
                'sha256': digest, 'profile': profile['id'], 'language': language,
                'os_version': version, 'product_type': product_type,
                'media_type': 'full', 'validation': 'content'}
    _fail('Incompatible critical setup file: ' + ', '.join(mismatches))
