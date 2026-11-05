#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Select one installation ISO from a file, ZIP or 7z without mounting it."""
from contextlib import contextmanager
from pathlib import Path, PurePosixPath
import selectors
import shutil
import stat
import struct
import subprocess
import tempfile
import time
import zipfile

MAX_ISO = 1 << 30
MAX_ENTRIES = 10000
MAX_LISTING = 2 << 20
MAX_ZIP_DIRECTORY = 8 << 20


def _zip_directory(stream):
    """Bound central metadata before ZipFile allocates its entry objects."""
    stream.seek(0, 2)
    size = stream.tell()
    tail_size = min(size, 65535 + 22)
    stream.seek(size - tail_size)
    tail = stream.read(tail_size)
    position = tail.rfind(b'PK\x05\x06')
    if position < 0 or position + 22 > len(tail):
        raise RuntimeError('The ZIP file is damaged.')
    end = size - tail_size + position
    _, disk, directory_disk, on_disk, count, length, offset, comment = struct.unpack_from('<4s4H2LH', tail, position)
    if position + 22 + comment != len(tail) or disk or directory_disk or on_disk != count:
        raise RuntimeError('Multi-volume or damaged ZIP file.')
    directory_end = end
    stream.seek(max(0, end - 20))
    locator = stream.read(20)
    if locator[:4] == b'PK\x06\x07':
        _, zip64_disk, record_offset, disks = struct.unpack('<4sLQL', locator)
        if zip64_disk or disks != 1 or record_offset + 56 != end - 20:
            raise RuntimeError('Invalid or multi-volume ZIP64 archive.')
        stream.seek(record_offset)
        record = stream.read(56)
        if len(record) != 56:
            raise RuntimeError('Truncated ZIP64 archive.')
        signature, record_size, _, _, disk, directory_disk, on_disk, count, length, offset = struct.unpack('<4sQ2H2L4Q', record)
        if signature != b'PK\x06\x06' or record_size != 44 or disk or directory_disk or on_disk != count:
            raise RuntimeError('Invalid or multi-volume ZIP64 archive.')
        directory_end = record_offset
    elif count == 0xffff or length == 0xffffffff or offset == 0xffffffff:
        raise RuntimeError('Missing ZIP64 archive metadata.')
    if count > MAX_ENTRIES or length > MAX_ZIP_DIRECTORY:
        raise RuntimeError('The ZIP file has too many entries or oversized file metadata.')
    start = directory_end - length
    if start < 0 or offset > start:
        raise RuntimeError('Invalid ZIP directory.')
    stream.seek(start)
    remaining, found = length, 0
    while remaining:
        header = stream.read(min(46, remaining))
        if len(header) != 46 or header[:4] != b'PK\x01\x02':
            raise RuntimeError('Invalid ZIP directory.')
        name, extra, comment = struct.unpack_from('<3H', header, 28)
        entry_size = 46 + name + extra + comment
        if entry_size > remaining or struct.unpack_from('<H', header, 34)[0] != 0:
            raise RuntimeError('Invalid or multi-volume ZIP entry.')
        stream.seek(entry_size - 46, 1)
        remaining -= entry_size
        found += 1
        if found > MAX_ENTRIES:
            raise RuntimeError('The ZIP file has too many entries.')
    if found != count:
        raise RuntimeError('Inconsistent ZIP file count.')


@contextmanager
def _zip(source):
    with Path(source).open('rb') as stream:
        _zip_directory(stream)
        with zipfile.ZipFile(stream) as archive:
            yield archive


def _name(name):
    path = PurePosixPath(name.replace('\\', '/'))
    if (not name or path.is_absolute() or '..' in path.parts or ':' in name
            or any(ord(c) < 32 for c in name)):
        raise RuntimeError('The archive contains an unsafe path.')
    return path


def _select(entries):
    if len(entries) > MAX_ENTRIES:
        raise RuntimeError('The archive contains too many entries.')
    images = []
    for name, size, regular in entries:
        path = _name(name)
        if path.suffix.lower() == '.iso':
            if not regular or not 0 < size <= MAX_ISO:
                raise RuntimeError('The ISO in the archive is invalid or larger than 1 GiB.')
            images.append((name, size))
    if len(images) != 1:
        raise RuntimeError('The archive must contain exactly one installation ISO.')
    return images[0]


def zip_contains_iso(source, *, machine_files=()):
    """Distinguish an installation ZIP from an existing-machine ZIP."""
    try:
        with _zip(source) as archive:
            names = {_name(item.filename).as_posix() for item in archive.infolist()}
            # An installed machine may contain game CD images. Its complete
            # Windows tree takes precedence over embedded ISO files.
            if machine_files:
                first = machine_files[0]
                for name in names:
                    if name.endswith(first):
                        prefix = name[:-len(first)]
                        if all(prefix + required in names for required in machine_files):
                            return False
            return any(PurePosixPath(name).suffix.lower() == '.iso' for name in names)
    except zipfile.BadZipFile as error:
        raise RuntimeError('The ZIP file is damaged.') from error


def _sevenzip():
    program = next((shutil.which(name) for name in ('7zz', '7z', '7za') if shutil.which(name)), None)
    if not program:
        raise RuntimeError('7z files require 7-Zip. Install 7zz or 7z, or extract the ISO first.')
    return program


def _pipe(command, limit, *, output=None, timeout=30):
    """Bound a helper's output and time, including a stalled decoder."""
    process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    data, errors, written = bytearray(), bytearray(), 0
    deadline = time.monotonic() + timeout
    try:
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ, 'out')
            selector.register(process.stderr, selectors.EVENT_READ, 'err')
            while selector.get_map():
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise RuntimeError('Reading the 7z archive timed out.')
                for key, _ in selector.select(min(remaining, .2)):
                    chunk = key.fileobj.read1(65536)
                    if not chunk:
                        selector.unregister(key.fileobj)
                    elif key.data == 'err':
                        errors.extend(chunk)
                        if len(errors) > 65536:
                            raise RuntimeError('Too many errors while reading the 7z archive.')
                    else:
                        written += len(chunk)
                        if written > limit:
                            raise RuntimeError('The 7z archive exceeds its declared size.')
                        if output is None:
                            data.extend(chunk)
                        else:
                            output.write(chunk)
        try:
            status = process.wait(timeout=max(.001, deadline - time.monotonic()))
        except subprocess.TimeoutExpired as error:
            raise RuntimeError('Reading the 7z archive timed out.') from error
        if status:
            raise RuntimeError('Could not read the 7z archive. '
                               'Damaged and encrypted archives are not supported.')
        return bytes(data) if output is None else written
    finally:
        if process.poll() is None:
            process.kill()
        process.wait()
        process.stdout.close()
        process.stderr.close()


def _sevenzip_image(source):
    program = _sevenzip()
    listing = _pipe([program, 'l', '-slt', '-ba', '-sccUTF-8', '--', str(source)], MAX_LISTING)
    entries = []
    try:
        for block in listing.decode('utf-8').strip().split('\n\n'):
            fields = {}
            for line in block.splitlines():
                if ' = ' in line:
                    key, value = line.split(' = ', 1)
                    if key in fields:
                        raise ValueError('Duplicate archive metadata')
                    fields[key] = value
            if 'Path' not in fields:
                continue
            regular = (fields.get('Folder') != '+' and fields.get('Encrypted') != '+'
                       and not fields.get('Symbolic Link') and not fields.get('Hard Link')
                       and not fields.get('Attributes', '').strip().startswith('l'))
            entries.append((fields['Path'], int(fields.get('Size', '0')), regular))
    except (UnicodeDecodeError, ValueError, IndexError) as error:
        raise RuntimeError('Invalid 7z archive metadata.') from error
    return program, _select(entries)


def _space(size):
    if shutil.disk_usage(tempfile.gettempdir()).free < size + (64 << 20):
        raise RuntimeError('Not enough free disk space to extract the installation ISO.')


@contextmanager
def installation_iso(source):
    """Yield a source ISO for the entire setup; remove extracted data on exit."""
    source = Path(source).expanduser().resolve()
    if not source.is_file():
        raise RuntimeError(f'Installation file not found: {source}')
    suffix = source.suffix.lower()
    if suffix == '.iso':
        yield source
        return
    if suffix not in ('.zip', '.7z'):
        raise RuntimeError('Choose a Windows 98 SE ISO or a ZIP/7z archive containing exactly one ISO.')
    print('setup: validate installation archive and extract ISO', flush=True)
    try:
        with tempfile.TemporaryDirectory(prefix='neo98-install-source-') as temporary:
            iso = Path(temporary) / 'installation.iso'
            if suffix == '.zip':
                with _zip(source) as archive:
                    entries = [(item.filename, item.file_size,
                                not item.is_dir() and not stat.S_ISLNK(item.external_attr >> 16)
                                and not (item.flag_bits & 1)) for item in archive.infolist()]
                    name, size = _select(entries)
                    _space(size)
                    with archive.open(name) as stream, iso.open('xb') as out:
                        written = 0
                        while True:
                            chunk = stream.read(1 << 20)
                            if not chunk:
                                break
                            written += len(chunk)
                            if written > size:
                                raise RuntimeError('The ZIP exceeds the declared ISO size.')
                            out.write(chunk)
            else:
                program, (name, size) = _sevenzip_image(source)
                _space(size)
                with iso.open('xb') as out:
                    written = _pipe([program, 'x', '-so', '-y', '-bd', '-spd', '--', str(source), name],
                                    size, output=out, timeout=600)
            if written != size:
                raise RuntimeError('The installation ISO in the archive is truncated.')
            yield iso
    except (zipfile.BadZipFile, NotImplementedError) as error:
        raise RuntimeError('The ZIP file is damaged or uses an unsupported format.') from error
