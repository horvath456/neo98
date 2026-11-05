#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build Wine 1.0's host IDL compiler from the verified conformance archive."""
import argparse
import json
import platform
import shutil
import subprocess
import tarfile
from pathlib import Path

import wine_conformance as wine


def build(archive, out):
    archive, out = Path(archive).resolve(), Path(out).resolve()
    pin = json.loads((wine.SUPPORT/'pin.json').read_text())
    if wine.digest(archive) != pin['sha256']:
        raise RuntimeError('Wine IDL compiler archive does not match the source pin')
    script_hash = wine.digest(Path(__file__))
    binary, report = out/'widl', out/'build.json'
    if report.is_file() and binary.is_file():
        saved = json.loads(report.read_text())
        if saved.get('builder_sha256') == script_hash and saved.get('archive_sha256') == pin['sha256'] and saved.get('binary_sha256') == wine.digest(binary):
            return binary
    out.mkdir(parents=True, exist_ok=True)
    source = out/'source'
    if source.exists():
        if source.is_symlink():
            raise RuntimeError('Refusing a symlink for extracted compiler sources')
        shutil.rmtree(source)
    source.mkdir()
    with tarfile.open(archive) as tar:
        for member in tar:
            parts = Path(member.name).parts[1:]
            relative = '/'.join(parts)
            if not member.isfile() or not parts or '..' in parts or not relative.startswith(('include/', 'tools/widl/', 'libs/wpp/')):
                continue
            target = source.joinpath(*parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            with tar.extractfile(member) as src, target.open('wb') as dst:
                shutil.copyfileobj(src, dst)
    # These Unix host declarations are used only to build the compiler. They
    # do not select the Windows target ABI or enter the generated guest EXE.
    features = ('UNISTD_H GETOPT_H GETOPT_LONG MODE_T OFF_T PID_T SIZE_T SSIZE_T '
                'FSBLKCNT_T FSFILCNT_T FSTATVFS STATVFS STRUCT_STATVFS_F_BLOCKS '
                'STRUCT_OPTION_NAME FFS FTRUNCATE FUTIMES GETPAGESIZE ISINF LSTAT '
                'MEMMOVE PCLOSE POPEN PREAD PWRITE READLINK SNPRINTF STRCASECMP '
                'STRDUP STRERROR STRNCASECMP STRTOLL STRTOULL USLEEP VSNPRINTF').split()
    config = out/'config.h'
    config.write_text('#define __WINE_CONFIG_H\n#define PACKAGE_VERSION "1.0"\n'+
                      ''.join('#define HAVE_'+name+' 1\n' for name in features))
    commands = []
    for prefix, folder, lexer in (('parser', source/'tools/widl', 'parser'), ('ppy', source/'libs/wpp', 'ppl')):
        commands += [['bison', '-p', prefix+'_', '-d', '-o', str(out/(prefix+'.tab.c')), str(folder/(prefix+'.y'))],
                     ['flex', '-o'+str(out/(lexer+'.yy.c')), str(folder/(lexer+'.l'))]]
    # Wine 1.0 predates ARM64 host declarations. On Apple Silicon build an
    # x86_64 host tool, which runs through Rosetta, rather than pretending the
    # ARM64 compiler has x86 predefined types.
    architecture = ['-arch', 'x86_64'] if platform.system() == 'Darwin' else []
    if platform.system() != 'Darwin' and platform.machine() not in ('x86_64', 'amd64', 'i386', 'i686'):
        raise RuntimeError('Legacy WIDL needs an x86 host compiler on this platform')
    files = sorted((source/'tools/widl').glob('*.c')) + [source/'libs/wpp/preproc.c', source/'libs/wpp/wpp.c']
    files += [out/name for name in ('parser.tab.c', 'ppy.tab.c', 'parser.yy.c', 'ppl.yy.c')]
    commands.append(['clang', *architecture, '-std=gnu89', '-O1', '-Wno-implicit-function-declaration',
                     '-I'+str(out), '-I'+str(source/'include'), '-I'+str(source/'tools/widl'),
                     '-I'+str(source/'libs/wpp'), *map(str, files), '-o', str(binary)])
    with (out/'build.log').open('w') as log:
        for command in commands:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    version = subprocess.check_output([str(binary), '-V'], text=True)
    if 'version 1.0' not in version:
        raise RuntimeError('Unexpected historical IDL compiler version')
    wine.save(report, {'archive_sha256': pin['sha256'], 'builder_sha256': script_hash,
                      'binary_sha256': wine.digest(binary), 'config_sha256': wine.digest(config),
                      'commands': commands, 'version': version,
                      'host_compiler': subprocess.check_output(['clang', '--version'], text=True)})
    return binary


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    print(build(args.archive, args.out))
