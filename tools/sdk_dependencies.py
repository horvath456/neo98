#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Free code the SDK for ARM64 applications builds programs with.

sdk/dependencies.json pins each archive by URL and SHA-256. ensure(name)
imports the selected files into third_party/<name>, once, and writes
neo98-import.json there with origin, licence and the hash of every file; a
later call verifies the files against it. The pattern is the one of
tools/unicorn_dependencies.py.

    musl-math musl (MIT): <math.h> and the algorithms of the C library of sdk/crt
                (printf, scanf, numbers, strings, wide and multibyte
                characters, classes of characters, dates, sorting, the
                allocator and its lock)
    pl_mpeg MPEG-1 decoder (MIT) of the sample player
    libcxx, libcxxabi, libunwind
                the C++ standard library and its runtime (Apache-2.0 with the
                LLVM exception), from the release archives of LLVM
    tzdata IANA timezone/leap data and its own compact-data scripts
    compiler-rt compiler helpers for libc++ filesystem, chrono and print (128-bit division)
    llvm-libc the headers of LLVM libc one source of libc++ includes; no
                release archive holds them alone, so the pin lists the files
                (sdk/llvm-libc-files.json: path and SHA-256 below one address)

    build/analysis-venv/bin/python tools/sdk_dependencies.py

docs/ARM64-APPS.md.
"""
from pathlib import Path, PurePosixPath
import hashlib
import io
import json
import shutil
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
PINS = json.loads((ROOT / 'sdk/dependencies.json').read_text())

# Functions of musl's math directory the runtime leaves out: families that are
# not in the C standard (Bessel functions, exp10, scalb, significand, finite),
# and the helpers of an 80-bit or 128-bit long double. long double is double
# under Windows on ARM64; the l variants are kept and compile to a call of the
# double function.
MUSL_SKIP_PREFIXES = ('__invtrigl', '__polevll', 'exp10', 'j0', 'j1', 'jn', 'scalb.', 'scalbf.', 'significand', 'finite')


# The C library beyond <math.h>: everything that is an algorithm comes from
# musl, unchanged. What is left out of each directory needs Linux (system
# calls, file descriptors, time zone files, locale files) and has its
# counterpart over KERNEL32 in sdk/crt/src, or is not offered at all.
MUSL_STDIO = """__lockfile __overflow __stdio_exit __toread __towrite __uflow clearerr fclose feof ferror fflush fgetc fgetpos fgets fgetwc
fgetws fileno fprintf fputc fputs fputwc fputws fread fscanf fseek fsetpos ftell fwide fwprintf fwrite fwscanf getc
getchar getwc getwchar ofl ofl_add perror printf putc putchar puts putwc putwchar rewind scanf setbuf setvbuf snprintf
sprintf sscanf stderr stdin stdout swprintf swscanf ungetc ungetwc vfprintf vfscanf vfwprintf vfwscanf vprintf vscanf
vsnprintf vsprintf vsscanf vswprintf vswscanf vwprintf vwscanf wprintf wscanf""".split()
MUSL_STRING = """memchr memrchr stpcpy stpncpy strcat strchr strchrnul strcmp strcpy strcspn strdup strlen strncat strncmp
strncpy strndup strnlen strpbrk strrchr strspn strstr strtok strtok_r wcpcpy wcpncpy wcscat wcschr wcscmp wcscpy wcscspn
wcsdup wcslen wcsncat wcsncmp wcsncpy wcsnlen wcspbrk wcsrchr wcsspn wcsstr wcstok wmemchr wmemcmp wmemcpy wmemmove
wmemset""".split()
MUSL_OTHER = {
    'stdlib': 'abs atof atoi atol atoll bsearch div imaxabs imaxdiv labs ldiv llabs lldiv qsort qsort_nr strtod strtol wcstod wcstol'.split(),
    'multibyte': 'btowc internal mblen mbrlen mbrtowc mbsinit mbsnrtowcs mbsrtowcs mbstowcs mbtowc wcrtomb wcsnrtombs wcsrtombs wcstombs wctob wctomb'.split(),
    'locale': '__lctrans c_locale langinfo localeconv strcoll strxfrm wcscoll wcsxfrm'.split(),
    'time': '__month_to_secs __secs_to_tm __tm_to_secs __year_to_secs asctime asctime_r difftime gmtime gmtime_r strftime timegm wcsftime'.split(),
    'internal': 'floatscan intscan shgetc'.split(),
}
# musl's mallocng allocator, imported byte for byte. The Windows page and
# mapping adapter is sdk/crt/src/mman_windows.c; allocator policy stays musl's.
MUSL_MALLOCNG = ('malloc.c', 'free.c', 'realloc.c', 'aligned_alloc.c', 'malloc_usable_size.c')
MUSL_OTHER['malloc'] = ['calloc', 'replaced']
MUSL_OTHER['thread'] = ['__lock', '__wait']
# Not musl's atomic.h, pthread_impl.h, syscall.h, lock.h, dynlink.h and the
# headers of Linux (sys/mman.h, elf.h): sdk/crt/musl-shim stands for them.
MUSL_HEADERS = {('src', 'internal', name) for name in ('libm.h', 'floatscan.h', 'intscan.h', 'shgetc.h', 'stdio_impl.h', 'libc.h',
                                                       'locale_impl.h')}
MUSL_HEADERS |= {('arch', 'aarch64', 'fp_arch.h'), ('src', 'stdio', 'getc.h'),
                 ('src', 'stdio', 'putc.h'), ('src', 'multibyte', 'internal.h'), ('src', 'time', 'time_impl.h'),
                 ('include', 'langinfo.h'), ('include', 'nl_types.h')}
# Of src/ctype: not the tables of glibc's interface and not the widths of
# terminal cells.
MUSL_CTYPE_SKIP = ('__ctype_b', '__ctype_to', 'wcwidth', 'wcswidth')


def musl_math(parts):
    if parts in (('COPYRIGHT',), ('include', 'math.h')):
        return PurePosixPath(*parts)
    if parts in MUSL_HEADERS:
        return PurePosixPath('src', parts[-1])
    if len(parts) == 4 and parts[:3] == ('src', 'malloc', 'mallocng'):
        if parts[3] in MUSL_MALLOCNG:
            return PurePosixPath('src', 'mallocng_' + parts[3])
        return PurePosixPath('src', parts[3]) if parts[3] in ('meta.h', 'glue.h') else None
    if len(parts) == 3 and parts[:2] == ('src', 'math') and parts[2].endswith(('.c', '.h')):
        name = parts[2]
        if name.startswith(MUSL_SKIP_PREFIXES):
            return None
        return PurePosixPath('src', name)
    if len(parts) == 3 and parts[0] == 'src' and parts[2].endswith(('.c', '.h')):
        directory, stem = parts[1], parts[2].rsplit('.', 1)[0]
        if directory == 'ctype':
            return None if stem.startswith(MUSL_CTYPE_SKIP) else PurePosixPath('src', parts[2])
        wanted = MUSL_STDIO if directory == 'stdio' else MUSL_STRING if directory == 'string' else MUSL_OTHER.get(directory, ())
        if parts[2].endswith('.c') and stem in wanted:
            # multibyte/internal.c lies beside the header of the same name.
            return PurePosixPath('src', 'multibyte_internal.c' if (directory, stem) == ('multibyte', 'internal') else parts[2])
    return None


def select(name, members):
    """{relative path: bytes} of the files to import, from {parts: bytes}."""
    if name == 'pl_mpeg':
        return {PurePosixPath(*parts): data for parts, data in members.items() if parts in (('pl_mpeg.h',), ('README.md',))}
    files = {}
    for parts, data in members.items():
        relative = musl_math(parts)
        if relative is not None:
            if relative in files:
                raise RuntimeError(f'{name}: two files of the archive would become {relative}')
            files[relative] = data
    # The variants for aarch64 replace the generic files of the same name.
    for parts, data in members.items():
        if len(parts) == 4 and parts[:3] == ('src', 'math', 'aarch64') and parts[3].endswith('.c'):
            files[PurePosixPath('src', parts[3])] = data
    return files


def llvm_runtime(parts):
    """Headers, sources and licence of one runtime of LLVM (libc++, libc++abi,
    libunwind); tests, documentation and build files stay behind."""
    if parts[0] in ('include', 'src') or parts in (('LICENSE.TXT',), ('CREDITS.TXT',)):
        return PurePosixPath(*parts)
    # What libc++ installs as <__assertion_handler>.
    if parts == ('vendor', 'llvm', 'default_assertion_handler.in'):
        return PurePosixPath(*parts)
    return None


def fetch(url):
    with urllib.request.urlopen(url, timeout=300) as response:
        return response.read()


def archive_files(name, pin):
    """{relative path: bytes} of the files to import from the archive of a pin."""
    archive_bytes = fetch(pin['url'])
    if hashlib.sha256(archive_bytes).hexdigest() != pin['sha256']:
        raise RuntimeError(f'{name}: archive checksum mismatch')
    members = {}
    with tarfile.open(fileobj=io.BytesIO(archive_bytes), mode='r:*') as archive:
        if name == 'tzdata':
            names = ('africa', 'antarctica', 'asia', 'australasia', 'europe', 'northamerica', 'southamerica',
                     'etcetera', 'factory', 'backward', 'ziguard.awk', 'zishrink.awk', 'leap-seconds.list', 'version', 'LICENSE')
            return {PurePosixPath(path): archive.extractfile(path).read() for path in names}
        for member in archive:
            parts = PurePosixPath(member.name).parts
            if not member.isfile() or len(parts) < 2:
                continue
            if member.name.startswith('/') or '..' in parts:
                raise ValueError('Unsafe dependency archive path')
            # A runtime of LLVM has thousands of test files: only what is
            # taken is read.
            if name in LLVM_RUNTIMES and llvm_runtime(parts[1:]) is None:
                continue
            members[parts[1:]] = archive.extractfile(member).read()
    if name in LLVM_RUNTIMES:
        return {llvm_runtime(parts): data for parts, data in members.items()}
    return select(name, members)


def listed_files(name, pin):
    """A pin that names single files below a base address, each with its hash."""
    listed = json.loads((ROOT / 'sdk' / pin['files']).read_text())
    files = {}
    for relative, digest in listed.items():
        if relative.startswith('/') or '..' in PurePosixPath(relative).parts:
            raise ValueError('Unsafe dependency path')
        data = fetch(pin['base'] + relative)
        if hashlib.sha256(data).hexdigest() != digest:
            raise RuntimeError(f'{name}: checksum mismatch for {relative}')
        files[PurePosixPath(relative)] = data
    return files


def identity(pin):
    """What a manifest must repeat for its import to be the pinned one."""
    if 'files' in pin:
        return hashlib.sha256((ROOT / 'sdk' / pin['files']).read_bytes()).hexdigest()
    return pin['sha256']


LLVM_RUNTIMES = ('libcxx', 'libcxxabi', 'libunwind')


def ensure(name):
    pin = PINS[name]
    destination = ROOT / 'third_party' / name
    manifest = destination / 'neo98-import.json'
    if manifest.exists():
        previous = json.loads(manifest.read_text())
        # selection: which files of the archive are taken; it changes when
        # the importer takes more of the same archive.
        if previous['archive_sha256'] != identity(pin) or previous.get('selection', 1) != pin.get('selection', 1):
            raise RuntimeError(f'{name}: the pin changed; remove {destination} to import again')
        # A large import is checked by its list, not file by file, on every
        # build; `python tools/sdk_dependencies.py --verify` reads every file.
        if not VERIFY and len(previous['files']) > 400:
            return destination
        for relative, digest in previous['files'].items():
            path = destination / relative
            if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
                raise RuntimeError(f'{name} differs from its import: {path}')
        return destination
    print(f'Importing {name}: {pin["version"]}', flush=True)
    files = listed_files(name, pin) if 'files' in pin else archive_files(name, pin)
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Another process may import at the same time: build beside, then rename.
    with tempfile.TemporaryDirectory(prefix=f'neo98-{name}-', dir=destination.parent) as directory:
        temporary = Path(directory) / name
        for relative, data in files.items():
            output = temporary / relative
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(data)
        result = {'version': pin['version'], 'url': pin.get('url') or pin['base'], 'archive_sha256': identity(pin),
                  'license': pin['license'], 'use': pin['use'], 'changes': 'none', 'selection': pin.get('selection', 1),
                  'files': {str(relative): hashlib.sha256(data).hexdigest() for relative, data in sorted(files.items())}}
        (temporary / 'neo98-import.json').write_text(json.dumps(result, indent=2) + '\n')
        try:
            temporary.rename(destination)
        except OSError:
            if not manifest.exists():
                raise
    return destination


VERIFY = False

if __name__ == '__main__':
    import sys
    VERIFY = '--verify' in sys.argv
    for dependency in PINS:
        print(ensure(dependency))
