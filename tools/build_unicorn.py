#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Cross-compile the pinned Unicorn x86 engine for the Circle AArch64 kernel."""
from pathlib import Path
import concurrent.futures
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from unicorn_dependencies import ensure
import build_unicorn_host

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'third_party/unicorn'
PORT = ROOT / 'unicorn'
BUILD = ROOT / 'build/unicorn-circle'
# The patched clone the kernel compiles from. It carries the patch groups of
# tools/build_unicorn_host.py, so the Pi runs the same engine as the Mac host,
# and the kernel's own changes below on top. A clone rather than single copied
# files, because the groups change structures in headers (CPUNegativeOffsetState,
# uc_struct) that every translation unit has to see alike.
TREE = BUILD / 'source'
# writeprotect only touches the Apple half of tcg-apple-jit.h.
HOST_GROUPS_LEFT_OUT = ('writeprotect',)
SYSROOT = ROOT / 'third_party/aarch64-newlib'


def build():
    dependencies = {name: ensure(name) for name in ('unicorn', 'newlib')}
    BUILD.mkdir(parents=True, exist_ok=True)
    # These changes are intentionally confined to generated copies of upstream
    # files. No Windows source or binaries enter this library.
    patched = build_unicorn_host.patched_sources(without=HOST_GROUPS_LEFT_OUT)
    source_text = lambda name: patched.get(name) or (SOURCE / name).read_text()
    kernel = {}
    translated = source_text('qemu/accel/tcg/translate-all.c')
    marker = '#else\nvoid free_code_gen_buffer(struct uc_struct *uc)\n'
    assert translated.count(marker) == 1
    translated = translated.replace(marker, '''#elif defined(CONFIG_NEO98_CIRCLE)
void free_code_gen_buffer(struct uc_struct *uc)
{
    extern void neo98_jit_free(void *);
    neo98_jit_free(uc->tcg_ctx->initial_buffer);
}
static inline void *alloc_code_gen_buffer(struct uc_struct *uc)
{
    extern void *neo98_jit_alloc(size_t);
    return neo98_jit_alloc(uc->tcg_ctx->code_gen_buffer_size);
}
''' + marker)
    # Report code-cache usage so the slot size stays measurable on the Pi.
    probe = '    gen_code_buf = tcg_ctx->code_gen_ptr;'
    assert translated.count(probe) == 1
    translated = translated.replace(probe, probe + '''
#ifdef CONFIG_NEO98_CIRCLE
    {
        extern void neo98_jit_record(const void *, const void *, unsigned);
        neo98_jit_record(tcg_ctx->code_gen_buffer, gen_code_buf, tcg_ctx->tb_ctx.tb_flush_count);
    }
#endif''')
    kernel['qemu/accel/tcg/translate-all.c'] = translated
    uc = source_text('uc.c')
    uc = re.sub(r'#include "qemu/target/(?!i386/)[^\n]+\n', '', uc)
    marker = 'uint64_t timeout, size_t count)\n{'
    assert uc.count(marker) == 1
    uc = uc.replace(marker, marker + '\n    if (timeout) return UC_ERR_ARG; // Circle has no watchdog thread.\n')
    kernel['uc.c'] = uc
    kernel['glib_compat/grand.c'] = source_text('glib_compat/grand.c').replace(
        '#if defined(unix)', '#if defined(CONFIG_NEO98_CIRCLE) || defined(unix)')
    random = source_text('qemu/util/guest-random.c').replace('static __thread GRand', 'static GRand')
    # Upstream's trailing-byte copy reverses the subtraction. Keep it bounded.
    random = random.replace('buf + i, &x, i - len', 'buf + i, &x, len - i')
    kernel['qemu/util/guest-random.c'] = random
    patched.update(kernel)
    thread = BUILD / 'qemu/thread-posix.h'
    thread.parent.mkdir(exist_ok=True)
    write_changed(thread, '#pragma once\nstruct QemuThread { int unavailable; };\n')
    preferred = Path('/opt/homebrew/opt/llvm/bin/clang')
    clang = os.environ.get('NEO98_CLANG') or (str(preferred) if preferred.exists() else shutil.which('clang'))
    if not clang: raise RuntimeError('Install LLVM clang, or set NEO98_CLANG')
    flags = [clang, '--target=aarch64-none-elf', f'--sysroot={SYSROOT}', '-mcpu=cortex-a53',
             '-mno-outline-atomics', '-std=gnu11', '-O2', '-g', '-ffreestanding',
             '-ffunction-sections', '-fdata-sections', '-fno-stack-protector',
             '-D_GNU_SOURCE', '-DUNICORN_HAS_X86', '-Wno-typedef-redefinition',
             '-Wno-incompatible-pointer-types', '-Wno-unused-value']
    flags += [f'-I{SYSROOT}/include', '-include', str(PORT / 'config/config-host.h'),
              '-include', str(PORT / 'compat.h')]
    includes = [BUILD, PORT / 'config', TREE / 'glib_compat', TREE / 'qemu',
                TREE / 'qemu/include', TREE / 'include', TREE / 'qemu/tcg',
                TREE / 'qemu/tcg/aarch64', TREE, TREE / 'qemu/accel/tcg']
    flags += [f'-I{p}' for p in includes]
    sources = json.loads((PORT / 'sources.json').read_text())
    sources.append({'file': str(PORT / 'circle_os.c'), 'guest': False})
    sources.append({'file': str(PORT / 'newlib_system.c'), 'guest': False})
    archive = BUILD / 'libunicorn-circle.a'
    manifest_path = BUILD / 'manifest.json'
    compiler = subprocess.check_output([clang, '--version'], text=True).splitlines()[0]
    digest = hashlib.sha256(json.dumps([flags, compiler, dependencies], sort_keys=True).encode())
    for name in sorted(patched):
        digest.update(name.encode()); digest.update(patched[name].encode())
    for path in [Path(__file__), ROOT / 'src/jit_memory.h', *sorted(p for p in PORT.rglob('*') if p.is_file())]:
        digest.update(str(path).encode()); digest.update(path.read_bytes())
    build_key = digest.hexdigest()
    if archive.exists() and manifest_path.exists():
        previous = json.loads(manifest_path.read_text())
        if previous.get('build_key') == build_key and previous['archive_sha256'] == hashlib.sha256(archive.read_bytes()).hexdigest():
            print(f'Unicorn unchanged: {archive}')
            return archive
    build_unicorn_host.clone_tree(TREE)
    for name, text in patched.items():
        (TREE / name).write_text(text)

    def compile_one(item):
        source = TREE / item['file']
        output = BUILD / (item['file'].replace('/', '_') + '.o')
        command = flags.copy()
        if item['guest']:
            command += ['-DNEED_CPU_H', '-include', 'x86_64.h',
                        f'-I{PORT}/config/x86_64-softmmu', f'-I{TREE}/qemu/target/i386']
        command += ['-c', str(source), '-o', str(output)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode: return None, result.stderr
        return output, result.stderr

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(compile_one, sources))
    (BUILD / 'compile.log').write_text(''.join(log for _, log in results))
    errors = [log for obj, log in results if obj is None]
    if errors:
        print('\n'.join(errors)[:18000]); raise RuntimeError(f'{len(errors)} source files failed')
    archive.unlink(missing_ok=True)
    subprocess.run([str(Path(clang).with_name('llvm-ar')), 'rcs', str(archive),
                    *[str(obj) for obj, _ in results]], check=True)
    manifest = {'host': 'AArch64 ELF64', 'guest': 'x86', 'backend': 'Unicorn TCG',
                'dependencies': {name: {key: value for key, value in dep.items() if key != 'files'}
                                 for name, dep in dependencies.items()},
                'archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
                'compiler': compiler, 'build_key': build_key}
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Built {archive}: {archive.stat().st_size:,} bytes')
    return archive


def write_changed(path, text):
    if not path.exists() or path.read_text() != text: path.write_text(text)


if __name__ == '__main__': build()
