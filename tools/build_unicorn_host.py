#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the pinned Unicorn engine for the ARM64 host, with the NEO98 patches.

The C++ runners used to link against the `libunicorn.2.dylib` of the pip
package. That copy cannot be changed, and guest CPU profiles put most of the guest's time inside Unicorn itself. This script builds the
same pinned version from `third_party/unicorn`, so a patch there reaches the
host.

It never edits the pinned tree. It clones the tree
into `build/unicorn-host/source` (an APFS block clone where the file system
offers one) and writes the patched files into the clone. Upstream stays byte for
byte what `third_party/unicorn/neo98-import.json` records.

The build is skipped when the stamp file matches: same patched sources, same
compiler, same CMake arguments. Python tools that `import unicorn` keep the pip
package; only the C++ link changes.

    build/analysis-venv/bin/python tools/build_unicorn_host.py [--force]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'third_party/unicorn'
OUT = ROOT / 'build/unicorn-host'
LIBRARY_NAME = 'libunicorn.2.dylib' if sys.platform == 'darwin' else 'libunicorn.so.2'
CMAKE_ARGUMENTS = ['-DCMAKE_BUILD_TYPE=Release', '-DUNICORN_ARCH=x86', '-DBUILD_SHARED_LIBS=ON',
                   '-DUNICORN_BUILD_TESTS=OFF', '-DUNICORN_INSTALL=OFF',
                   f'-DCMAKE_C_FLAGS=-ffile-prefix-map={ROOT}=.',
                   f'-DCMAKE_CXX_FLAGS=-ffile-prefix-map={ROOT}=.']


def patch_uc_c(text):
    """Two changes in uc.c.

    The pinned tree carries only the i386 target, because that is the one this
    port runs. uc.c includes every target's unicorn.h, so the other includes
    have to go.

    The counters measure how often the guest
    leaves the fast store path, how often that costs a mapping lookup, and
    whether the translation cache overflows. uc_neo98_counters reads them.
    """
    stripped = re.sub(r'#include "qemu/target/(?!i386/)[^\n]+\n', '', text)
    assert stripped != text, 'uc.c has no foreign target includes'
    marker = '\nstatic void clear_deleted_hooks(uc_engine *uc);'
    assert stripped.count(marker) == 1
    return stripped.replace(marker, '''
/* NEO98: measurement counters. */
uint64_t neo98_uc_counter[NEO98_UC_COUNTERS];

UNICORN_EXPORT
void uc_neo98_counters(uint64_t *out, size_t count)
{
    size_t i;
    for (i = 0; i < count; i++) {
        out[i] = i < NEO98_UC_COUNTERS ? neo98_uc_counter[i] : 0;
    }
}
''' + marker)


def patch_uc_priv_h(text):
    """The counter names live beside the engine, so every patched file sees them."""
    marker = '\nstruct uc_struct {'
    assert text.count(marker) == 1
    return text.replace(marker, '''
/* NEO98 measurement counters. */
#define NEO98_UC_STORE_SLOW 0
#define NEO98_UC_NOTDIRTY 1
#define NEO98_UC_MAP_LOOKUP 2
#define NEO98_UC_MAP_HIT 3
#define NEO98_UC_TB_FLUSH 4
#define NEO98_UC_CODE_BYTES 5
#define NEO98_UC_TB_INVALIDATE 6
#define NEO98_UC_COUNTERS 8
extern uint64_t neo98_uc_counter[NEO98_UC_COUNTERS];
''' + marker)


def patch_uc_priv_h_cache(text):
    """The cache lives in the engine, because every process here has its own."""
    anchor = '\nstruct uc_struct {'
    assert text.count(anchor) == 1
    text = text.replace(anchor, '''
#define NEO98_UC_MAP_CACHE 2048
struct neo98_map_entry {
    uint64_t page;
    uint64_t generation;
    struct MemoryRegion *mr;
};
''' + anchor)
    marker = '''    bool thread_executable_entry;
    bool current_executable;
    bool skip_sync_pc_on_exit;
};'''
    assert text.count(marker) == 1
    return text.replace(marker, '''    bool thread_executable_entry;
    bool current_executable;
    bool skip_sync_pc_on_exit;
    /* NEO98 mapping cache. */
    uint64_t neo98_map_generation;
    struct neo98_map_entry neo98_map_cache[NEO98_UC_MAP_CACHE];
};''')


def patch_cputlb_c(text):
    """Count the slow store path and the mapping lookups it pays for."""
    marker = '''    // Load the latest memory mapping.
    paddr = entry->paddr | (addr & ~TARGET_PAGE_MASK);
    mr = uc->memory_mapping(uc, paddr);
'''
    assert text.count(marker) == 1, text.count(marker)
    text = text.replace(marker, '''    // Load the latest memory mapping.
    neo98_uc_counter[NEO98_UC_STORE_SLOW]++;
    paddr = entry->paddr | (addr & ~TARGET_PAGE_MASK);
    mr = uc->memory_mapping(uc, paddr);
''')
    marker = '''    ram_addr_t ram_addr = mem_vaddr + iotlbentry->addr;
    MemoryRegion *mr = cpu->uc->memory_mapping(cpu->uc, tlbe->paddr | (mem_vaddr & ~TARGET_PAGE_MASK));
'''
    assert text.count(marker) == 1
    return text.replace(marker, '''    ram_addr_t ram_addr = mem_vaddr + iotlbentry->addr;
    MemoryRegion *mr;
    neo98_uc_counter[NEO98_UC_NOTDIRTY]++;
    mr = cpu->uc->memory_mapping(cpu->uc, tlbe->paddr | (mem_vaddr & ~TARGET_PAGE_MASK));
''')


def patch_exec_all_h(text):
    """Let a page that carries no code leave the slow store path again.

    `notdirty_write` calls `tlb_set_dirty` for a page without translated code,
    and `tb_gen_code` calls `tlb_reset_dirty_by_vaddr` when it translates from
    one. The pair keeps TLB_NOTDIRTY exactly where self-modifying code needs it
    and lets every other page take the fast store path after its first write.

    Both calls ask `uc_mem_hook_installed` first, and that function answers yes
    for nine hook kinds. Six of them only ever fire when an access fails: the
    three unmapped kinds and the three protection kinds. A failing access never
    reaches a valid TLB entry, it goes through `tlb_fill`, so the state of the
    dirty bit cannot hide such a hook. Only UC_HOOK_MEM_READ,
    UC_HOOK_MEM_READ_AFTER and UC_HOOK_MEM_WRITE really need every access to
    take the slow path, because they see accesses that succeed.

    NEO98 has a UC_HOOK_MEM_INVALID over the whole address space for its fault
    reports (src/x86_app.cpp, MemoryFaultHook), so before this patch the answer
    was always yes and the two calls above never ran. See

    """
    marker = '''    if (HOOK_EXISTS_BOUNDED(uc, UC_HOOK_MEM_FETCH_UNMAPPED, paddr))
        return true;
    if (HOOK_EXISTS_BOUNDED(uc, UC_HOOK_MEM_READ_UNMAPPED, paddr))
        return true;
    if (HOOK_EXISTS_BOUNDED(uc, UC_HOOK_MEM_READ, paddr))
        return true;
    if (HOOK_EXISTS_BOUNDED(uc, UC_HOOK_MEM_READ_PROT, paddr))
        return true;
    if (HOOK_EXISTS_BOUNDED(uc, UC_HOOK_MEM_FETCH_PROT, paddr))
        return true;
    if (HOOK_EXISTS_BOUNDED(uc, UC_HOOK_MEM_READ_AFTER, paddr))
        return true;
    if (HOOK_EXISTS_BOUNDED(uc, UC_HOOK_MEM_WRITE, paddr))
        return true;
    if (HOOK_EXISTS_BOUNDED(uc, UC_HOOK_MEM_WRITE_UNMAPPED, paddr))
        return true;
    if (HOOK_EXISTS_BOUNDED(uc, UC_HOOK_MEM_WRITE_PROT, paddr))
        return true;
    return false;'''
    assert text.count(marker) == 1
    return text.replace(marker, '''    /* NEO98: only the hooks that see a successful access force the slow
       path. The unmapped and protection kinds fire from tlb_fill and cannot
       be missed by a clean TLB entry. */
    if (HOOK_EXISTS_BOUNDED(uc, UC_HOOK_MEM_READ, paddr))
        return true;
    if (HOOK_EXISTS_BOUNDED(uc, UC_HOOK_MEM_READ_AFTER, paddr))
        return true;
    if (HOOK_EXISTS_BOUNDED(uc, UC_HOOK_MEM_WRITE, paddr))
        return true;
    return false;''')


def patch_cputlb_c_notdirty(text):
    """Ask the memory region, not the guest page table, whether a page runs code.

    `notdirty_write` clears TLB_NOTDIRTY only for a page whose TLB entry has
    `addr_code == -1`. That test comes from QEMU, where the guest page table
    says what may be executed. Unicorn keeps that answer in the permissions of
    the memory region instead, and the i386 target fills every TLB entry with
    PAGE_READ | PAGE_WRITE | PAGE_EXEC while paging is off
    (qemu/target/i386/excp_helper.c), which this port is. So `addr_code` is
    never -1 here and the bit never came off.

    The region is the right question: Unicorn refuses to translate code out of a
    region without UC_PROT_EXEC (load_helper, the code_read branch), so no
    translated block can lie on such a page and a write there cannot make one
    stale. A page that gets the right later gets TLB_NOTDIRTY back from
    `tlb_reset_dirty_by_vaddr` in `tb_gen_code`, before the first block of that
    page is linked.
    """
    marker = '''    if (!(!mr || (tlbe->addr_write != -1 && mr->priority < cpu->uc->snapshot_level)) &&
            !(tlbe->addr_code != -1) &&'''
    assert text.count(marker) == 1
    return text.replace(marker, '''    if (!(!mr || (tlbe->addr_write != -1 && mr->priority < cpu->uc->snapshot_level)) &&
            !(mr->perms & UC_PROT_EXEC) &&''')


def patch_exec_all_h_codeclean(text):
    """The declaration of the question notdirty_write asks after its invalidation."""
    marker = 'void tlb_set_dirty(CPUState *cpu, target_ulong vaddr);\n'
    assert text.count(marker) == 1
    return text.replace(marker, marker + '''/* NEO98: whether a page still carries translated code. */
bool neo98_page_holds_code(struct uc_struct *uc, tb_page_addr_t addr);
''')


def patch_translate_all_c_codeclean(text):
    """Answer whether a page still carries translated code.

    QEMU keeps that answer in the dirty bitmap: `tb_invalidate_phys_page_range`
    calls `tlb_unprotect_code` when the last block of a page goes, and
    `notdirty_write` then lets the page leave the slow store path. Unicorn
    removed the bitmap, so `tlb_unprotect_code` does nothing. The page
    descriptor has the same answer in `first_tb`.
    """
    marker = '/* user-mode: call with mmap_lock held */\nvoid tb_check_watchpoint('
    assert text.count(marker) == 1
    return text.replace(marker, '''/* NEO98: the page descriptor knows whether any block still starts or ends on
   the page. notdirty_write asks after its invalidation. */
bool neo98_page_holds_code(struct uc_struct *uc, tb_page_addr_t addr)
{
    PageDesc *p = page_find(uc, addr >> TARGET_PAGE_BITS);
    return p && p->first_tb;
}

''' + marker)


def patch_cputlb_c_codeclean(text):
    """Let an executable page without translated code leave the slow store path.

    After the `notdirty` group an executable page keeps TLB_NOTDIRTY for good,
    because its region still says UC_PROT_EXEC after the last block of the page
    is gone. Blitz3D builds its pixel routines in a local variable, so the page
    at the top of the main thread's stack gets the execute right, and every
    push after that took `store_helper`, `notdirty_write` and
    `tb_invalidate_phys_page_fast`: 600 million times a minute in Wonderland. Now the page leaves the slow path once its last block is
    gone, as in QEMU, and `tb_gen_code` puts it back with
    `tlb_reset_dirty_by_vaddr` before the next block of the page is linked. That
    range runs from the start of the block's page to the block's last byte, so
    it also covers the second page of a block that crosses a page boundary.
    """
    marker = '''    if (mr && (mr->perms & UC_PROT_EXEC) != 0) {
        struct page_collection *pages
            = page_collection_lock(cpu->uc, ram_addr, ram_addr + size);
        tb_invalidate_phys_page_fast(cpu->uc, pages, ram_addr, size, retaddr);
        page_collection_unlock(pages);
    }
'''
    assert text.count(marker) == 1
    text = text.replace(marker, '''    bool code = false;
    if (mr && (mr->perms & UC_PROT_EXEC) != 0) {
        struct page_collection *pages
            = page_collection_lock(cpu->uc, ram_addr, ram_addr + size);
        tb_invalidate_phys_page_fast(cpu->uc, pages, ram_addr, size, retaddr);
        page_collection_unlock(pages);
        code = neo98_page_holds_code(cpu->uc, ram_addr);
    }
''')
    marker = '''            !(mr->perms & UC_PROT_EXEC) &&
            !uc_mem_hook_installed('''
    assert text.count(marker) == 1, 'the notdirty group applies first'
    return text.replace(marker, '''            !code &&
            !uc_mem_hook_installed(''')


def patch_unicorn_common_h(text):
    """A page cache in front of find_memory_mapping.

    Every store that leaves the fast path asks which region an address belongs
    to, and notdirty_write asks a second time. The answer costs a walk of the
    dispatch tree of the address space, and the one MRU slot that walk keeps
    holds a single section, so a guest that writes across two mappings misses it
    every time. In guest execution profiles this walk is the largest single item.

    A mapping belongs to a whole guest page, because Unicorn refuses a map that
    is not page aligned. The cache therefore keys on the page and holds the
    region of that page, including a page with no region at all. It is emptied
    by a generation counter that memory_region_transaction_commit raises, so
    every map, unmap, move and copy-on-write invalidates it at once.
    """
    marker = '''MemoryRegion *find_memory_mapping(struct uc_struct *uc, hwaddr address)
{
    hwaddr xlat = 0;
    hwaddr len = 1;
    MemoryRegion *mr = address_space_translate(&uc->address_space_memory, address, &xlat, &len, false, MEMTXATTRS_UNSPECIFIED);

    if (mr == &uc->io_mem_unassigned) {
        return NULL;
    }
    return mr;
}'''
    assert text.count(marker) == 1
    text = text.replace(marker, '''MemoryRegion *find_memory_mapping(struct uc_struct *uc, hwaddr address)
{
    hwaddr xlat = 0;
    hwaddr len = 1;
    MemoryRegion *mr;
    hwaddr page = address & ~(hwaddr)(TARGET_PAGE_SIZE - 1);
    unsigned slot = (unsigned)((page >> TARGET_PAGE_BITS) & (NEO98_UC_MAP_CACHE - 1));

    neo98_uc_counter[NEO98_UC_MAP_LOOKUP]++;
    if (uc->neo98_map_cache[slot].generation == uc->neo98_map_generation &&
        uc->neo98_map_cache[slot].page == page) {
        neo98_uc_counter[NEO98_UC_MAP_HIT]++;
        return uc->neo98_map_cache[slot].mr;
    }
    mr = address_space_translate(&uc->address_space_memory, address, &xlat, &len, false, MEMTXATTRS_UNSPECIFIED);
    if (mr == &uc->io_mem_unassigned) {
        mr = NULL;
    }
    uc->neo98_map_cache[slot].page = page;
    uc->neo98_map_cache[slot].mr = mr;
    uc->neo98_map_cache[slot].generation = uc->neo98_map_generation;
    return mr;
}''')
    marker = '    uc->memory_mapping = find_memory_mapping;'
    assert text.count(marker) == 1
    return text.replace(marker, marker + '''
    /* Generation zero is the state a fresh engine has, so start above it and
       no cache entry of the zeroed array counts as an answer. */
    uc->neo98_map_generation = 1;''')


def patch_memory_c(text):
    """Empty the mapping cache whenever the memory topology really changed."""
    marker = '''        mr->uc->memory_region_update_pending = false;
        MEMORY_LISTENER_CALL_GLOBAL(mr->uc, commit, Forward);'''
    assert text.count(marker) == 1
    return text.replace(marker, '''        mr->uc->memory_region_update_pending = false;
        mr->uc->neo98_map_generation++;
        MEMORY_LISTENER_CALL_GLOBAL(mr->uc, commit, Forward);''')


def patch_translate_all_c(text):
    """The translation cache: how full it got and how often it was flushed."""
    marker = '    gen_code_buf = tcg_ctx->code_gen_ptr;'
    assert text.count(marker) == 1
    text = text.replace(marker, marker + '''
    {
        uint64_t used = (uint64_t)((uintptr_t)gen_code_buf - (uintptr_t)tcg_ctx->code_gen_buffer);
        if (used > neo98_uc_counter[NEO98_UC_CODE_BYTES]) {
            neo98_uc_counter[NEO98_UC_CODE_BYTES] = used;
        }
        neo98_uc_counter[NEO98_UC_TB_FLUSH] = tcg_ctx->tb_ctx.tb_flush_count;
    }''')
    marker = 'void tb_invalidate_phys_page_fast(struct uc_struct *uc, struct page_collection *pages,'
    assert text.count(marker) == 1
    body = text.index('{', text.index(marker))
    return text[:body + 1] + '\n    neo98_uc_counter[NEO98_UC_TB_INVALIDATE]++;' + text[body + 1:]


def patch_tcg_apple_jit_h(text):
    """The W^X switch of Apple silicon, on the two paths that pay for it.

    `assert_executable` reads the system register S3_6_c15_c1_5, and the read
    starts with `isb sy`, a pipeline barrier. The read happens before the
    `assert` that uses it, so a release build with NDEBUG keeps the barrier and
    drops only the comparison. Every hook callback and every switch runs it.

    `JIT_CALLBACK_GUARD` calls `pthread_jit_write_protect_np` after every hook
    callback, whether or not the callback changed anything. NEO98 calls a block
    hook before each translated block, so this is one call per block. The guard
    also left `uc->current_executable` holding what the callback set while the
    thread was switched back, so engine and thread disagreed afterwards; the
    version here keeps the two together.
    """
    marker = '''static inline void assert_executable(bool executable) {
    if (!pthread_jit_write_protect_supported_np()) {
        return;
    }
    uint64_t v = read_sprr_perm();

    if (!v) {
        assert(executable == thread_executable());
    }
}'''
    assert text.count(marker) == 1
    text = text.replace(marker, '''static inline void assert_executable(bool executable) {
#ifndef NDEBUG
    if (!pthread_jit_write_protect_supported_np()) {
        return;
    }
    uint64_t v = read_sprr_perm();

    if (!v) {
        assert(executable == thread_executable());
    }
#else
    (void)executable;
#endif
}''')
    marker = '''#define JIT_CALLBACK_GUARD(x)                       \\
{                                                   \\
    bool executable = uc->current_executable;       \\
    assert_executable(executable);                  \\
    x;                                              \\
    jit_write_protect(executable);                  \\
}                                                   \\
'''
    assert text.count(marker) == 1
    text = text.replace(marker, '''#define JIT_CALLBACK_GUARD(x)                       \\
{                                                   \\
    bool executable = uc->current_executable;       \\
    assert_executable(executable);                  \\
    x;                                              \\
    if (uc->current_executable != executable) {     \\
        jit_write_protect(executable);              \\
        uc->current_executable = executable;        \\
    }                                               \\
}                                                   \\
''')
    marker = '''#define JIT_CALLBACK_GUARD_VAR(var, x)                  \\
{                                                       \\
    bool executable = uc->current_executable;           \\
    assert_executable(executable);                      \\
    var = x;                                            \\
    jit_write_protect(executable);                      \\
}                                                       \\
'''
    assert text.count(marker) == 1
    return text.replace(marker, '''#define JIT_CALLBACK_GUARD_VAR(var, x)                  \\
{                                                       \\
    bool executable = uc->current_executable;           \\
    assert_executable(executable);                      \\
    var = x;                                            \\
    if (uc->current_executable != executable) {         \\
        jit_write_protect(executable);                  \\
        uc->current_executable = executable;            \\
    }                                                   \\
}                                                       \\
''')


GEN_TB_START = '''static inline void gen_tb_start(TCGContext *tcg_ctx, TranslationBlock *tb)
{
    TCGv_ptr puc = tcg_const_ptr(tcg_ctx, tcg_ctx->uc);
    TCGv_i32 tmp = tcg_const_i32(tcg_ctx, 0);
'''


def patch_gen_icount_h_exitcheck(text):
    """Test the stop request in the translated code, as QEMU does.

    Unicorn calls `helper_check_exit_request` at the start of every translated
    block, chained entries included. The helper has no flags, so TCG also treats
    it as a call that may read and write every global. The profile of
    Yetisports put 20 percent of all samples there.

    QEMU reads `icount_decr` and branches when it is negative; `cpu_exit` sets
    its high half to -1. Here the translated code does the same read and calls
    the unchanged helper only when the value is negative, so the exit itself
    keeps Unicorn's path (`cpu_loop_exit_restore`, `skip_sync_pc_on_exit`).
    Unicorn's comment against a branch here is about the delay slot flag of MIPS
    and SPARC; with that flag the old code stays. At the start of a block no
    global is loaded yet, so the end of the basic block syncs nothing.
    """
    assert text.count(GEN_TB_START) == 1
    return text.replace(GEN_TB_START, GEN_TB_START.replace('''    TCGv_ptr puc = tcg_const_ptr(tcg_ctx, tcg_ctx->uc);
    TCGv_i32 tmp = tcg_const_i32(tcg_ctx, 0);
''', '''    TCGv_ptr puc;
    TCGv_i32 tmp;
    /* NEO98: the test in the translated code, see tools/build_unicorn_host.py. */
    if (tcg_ctx->delay_slot_flag == NULL) {
        TCGLabel *run = gen_new_label(tcg_ctx);
        TCGv_i32 count = tcg_temp_new_i32(tcg_ctx);
        tcg_gen_ld_i32(tcg_ctx, count, tcg_ctx->cpu_env,
                       offsetof(ArchCPU, neg.icount_decr.u32) - offsetof(ArchCPU, env));
        /* NEO98 budget */
        tcg_gen_brcondi_i32(tcg_ctx, TCG_COND_GE, count, 0, run);
        tcg_temp_free_i32(tcg_ctx, count);
        puc = tcg_const_ptr(tcg_ctx, tcg_ctx->uc);
        tmp = tcg_const_i32(tcg_ctx, 0);
        gen_helper_check_exit_request(tcg_ctx, puc, tmp);
        tcg_temp_free_i32(tcg_ctx, tmp);
        tcg_temp_free_ptr(tcg_ctx, puc);
        gen_set_label(tcg_ctx, run);
        return;
    }
    puc = tcg_const_ptr(tcg_ctx, tcg_ctx->uc);
    tmp = tcg_const_i32(tcg_ctx, 0);
'''))


def patch_gen_icount_h_budget(text):
    """Count blocks down in the translated code instead of in a block hook.

    NEO98 bounded every run with a UC_HOOK_BLOCK that counted blocks and
    stopped after 50,000. The hook costs a PC store, a helper call and the
    callback before every block: 8.7 percent of the Yetisports profile.

    The counter lives beside `icount_decr` in the negative offset state of the
    CPU. The translated code decrements it and folds it into the same sign test
    as the stop request, so the fast path is one more load, subtract, store and
    or. Only a negative value takes the slow path, and there
    `helper_neo98_tb_slow` calls the callback of `uc_neo98_budget_hook` before
    it tests the stop request, which is the order the block hook had. The
    callback may set a new budget with `uc_neo98_budget`, one block included,
    without any change to translated code.
    """
    assert '/* NEO98 budget */' in text, 'the budget patch builds on exitcheck'
    text = text.replace('        /* NEO98 budget */\n', '''        {
            TCGv_i32 budget = tcg_temp_new_i32(tcg_ctx);
            const int where = (int)(offsetof(ArchCPU, neg.neo98_budget) - offsetof(ArchCPU, env));
            tcg_gen_ld_i32(tcg_ctx, budget, tcg_ctx->cpu_env, where);
            tcg_gen_subi_i32(tcg_ctx, budget, budget, 1);
            tcg_gen_st_i32(tcg_ctx, budget, tcg_ctx->cpu_env, where);
            tcg_gen_or_i32(tcg_ctx, count, count, budget);
            tcg_temp_free_i32(tcg_ctx, budget);
        }
''')
    marker = '''        puc = tcg_const_ptr(tcg_ctx, tcg_ctx->uc);
        tmp = tcg_const_i32(tcg_ctx, 0);
        gen_helper_check_exit_request(tcg_ctx, puc, tmp);
        tcg_temp_free_i32(tcg_ctx, tmp);
        tcg_temp_free_ptr(tcg_ctx, puc);
        gen_set_label(tcg_ctx, run);'''
    assert text.count(marker) == 1
    return text.replace(marker, '''        {
            TCGv_i64 pc = tcg_const_i64(tcg_ctx, tb->pc);
            puc = tcg_const_ptr(tcg_ctx, tcg_ctx->uc);
            gen_helper_neo98_tb_slow(tcg_ctx, puc, pc);
            tcg_temp_free_ptr(tcg_ctx, puc);
            tcg_temp_free_i64(tcg_ctx, pc);
        }
        gen_set_label(tcg_ctx, run);''')


def patch_cpu_defs_h_budget(text):
    """The block budget, next to `icount_decr` so the offset from env is small."""
    marker = '''    IcountDecr icount_decr;
} CPUNegativeOffsetState;'''
    assert text.count(marker) == 1
    return text.replace(marker, '''    IcountDecr icount_decr;
    /* NEO98: blocks left before the budget callback, tools/build_unicorn_host.py. */
    int32_t neo98_budget;
} CPUNegativeOffsetState;''')


def patch_tcg_runtime_h_budget(text):
    marker = 'DEF_HELPER_2(check_exit_request, void, ptr, i32)'
    assert text.count(marker) == 1
    return text.replace(marker, marker + '\nDEF_HELPER_2(neo98_tb_slow, void, ptr, i64)')


def patch_tcg_runtime_c_budget(text):
    """The slow path of the block start, and the setter of the budget.

    The exit half repeats `helper_check_exit_request`, because
    `cpu_loop_exit_restore` needs the return address of this very helper.
    """
    marker = 'void HELPER(check_exit_request)(void *p, uint32_t in_delay_slot) {'
    assert text.count(marker) == 1
    return text.replace(marker, '''/* NEO98: the budget of translated blocks. */
UNICORN_EXPORT void uc_neo98_budget(uc_engine *uc, int32_t blocks);
UNICORN_EXPORT
void uc_neo98_budget(uc_engine *uc, int32_t blocks)
{
    if (uc->cpu) {
        cpu_neg(uc->cpu)->neo98_budget = blocks;
    }
}

void HELPER(neo98_tb_slow)(void *p, uint64_t pc)
{
    uc_engine *uc = p;
    CPUState *cpu = uc->cpu;

    if (cpu_neg(cpu)->neo98_budget < 0) {
        cpu_neg(cpu)->neo98_budget = INT32_MAX;
        if (uc->neo98_budget_callback && !uc->stop_request) {
            JIT_CALLBACK_GUARD(uc->neo98_budget_callback(uc, pc, 0, uc->neo98_budget_data));
        }
    }
    if (cpu_loop_exit_requested(cpu)) {
        if (uc->nested_level == 1) {
            tb_exec_unlock(uc);
        }
        cpu->tcg_exit_req = 0;
        if (uc->skip_sync_pc_on_exit) {
            uc->skip_sync_pc_on_exit = false;
            cpu_loop_exit(cpu);
        } else {
            cpu_loop_exit_restore(cpu, GETPC());
        }
    }
}

''' + marker)


def patch_uc_priv_h_budget(text):
    marker = '    bool skip_sync_pc_on_exit;\n'
    assert text.count(marker) == 1
    return text.replace(marker, marker + '''    /* NEO98 block budget, tools/build_unicorn_host.py. */
    uc_cb_hookcode_t neo98_budget_callback;
    void *neo98_budget_data;
''')


def patch_uc_c_budget(text):
    marker = '\nstatic void clear_deleted_hooks(uc_engine *uc);'
    assert text.count(marker) == 1
    return text.replace(marker, '''
/* NEO98: the callback of the block budget, see uc_neo98_budget. */
UNICORN_EXPORT void uc_neo98_budget_hook(uc_engine *uc, uc_cb_hookcode_t callback, void *data);
UNICORN_EXPORT
void uc_neo98_budget_hook(uc_engine *uc, uc_cb_hookcode_t callback, void *data)
{
    uc->neo98_budget_callback = callback;
    uc->neo98_budget_data = data;
}
''' + marker)


def patch_misc_helper_c_tscpause(text):
    """RDTSC and RDTSCP stand through a pause of the machine : the counter is the engine's clock in nanoseconds less the sum of
    the pauses, which uc_neo98_tsc_pause sets. One load on the way."""
    marker = '        val = cpu_get_tsc(env) + env->tsc_offset;\n'
    assert text.count(marker) == 2
    text = text.replace(marker, '        val = cpu_get_tsc(env) + env->tsc_offset - neo98_uc_tsc_pause;\n')
    head = '\nvoid helper_rdtsc(CPUX86State *env)\n'
    assert text.count(head) == 1
    return text.replace(head, '\n/* NEO98: tools/build_unicorn_host.py, "tscpause". */\nextern uint64_t neo98_uc_tsc_pause;\n' + head)


def patch_uc_c_tscpause(text):
    marker = '\nstatic void clear_deleted_hooks(uc_engine *uc);'
    assert text.count(marker) == 1
    return text.replace(marker, '''
/* NEO98: nanoseconds RDTSC stays behind the clock, the pauses of the machine. */
uint64_t neo98_uc_tsc_pause;
UNICORN_EXPORT void uc_neo98_tsc_pause(uint64_t nanoseconds);
UNICORN_EXPORT
void uc_neo98_tsc_pause(uint64_t nanoseconds)
{
    neo98_uc_tsc_pause = nanoseconds;
}
''' + marker)


def patch_tcg_runtime_c_registers(text):
    """Hand out where the general registers and EIP of the CPU live.

    Every uc_reg_read and uc_reg_write goes through UC_INIT, and on Apple
    silicon that asks the thread for its W^X state and restores it afterwards.
    An ordinary API call of the port reads and writes twelve registers that way,
    and Pinball spent 5.3 percent of its samples there.
    In UC_MODE_32 the eight general registers are plain reads of `env->regs`
    and zero-extending writes; EIP is a plain read. A write of EIP also makes
    the engine leave the running block, so the port keeps writing EIP through
    uc_reg_write. The pointers stay valid as long as the engine does.
    """
    return text + """
/* NEO98: direct access to the general registers, tools/build_unicorn_host.py. */
UNICORN_EXPORT uint64_t *uc_neo98_x86_registers(uc_engine *uc, uint64_t **eip);
UNICORN_EXPORT
uint64_t *uc_neo98_x86_registers(uc_engine *uc, uint64_t **eip)
{
    CPUX86State *env;
    QEMU_BUILD_BUG_ON(sizeof(target_ulong) != sizeof(uint64_t));
    if (!uc->cpu) {
        return NULL;
    }
    env = uc->cpu->env_ptr;
    *eip = (uint64_t *)&env->eip;
    return (uint64_t *)env->regs;
}
"""


def patch_translator_c_temps(text):
    """End a block before the translator runs out of temporaries.

    The i386 front end hands many x87 helpers a fresh `tcg_const_i32` for the
    register number and never frees it (fxch, fmul st(i), fstp and the rest
    leave one to three temporaries each). The temporaries only come back when
    the next block starts. TCGContext keeps TCG_MAX_TEMPS (512) of them, and
    `tcg_temp_alloc` checks the bound with a debug assertion only; the array is
    followed by the heads of the op lists, so the 513th temporary zeroes them
    and the next emitted op writes through a null pointer. A straight run of a
    few hundred x87 instructions does that: the matrix code of the KIDS200
    OpenGL games leaks about two per instruction. The block now
    ends, like one whose op buffer is full, while 64 temporaries are left;
    the next block starts with all of them free again.
    """
    marker = '        if (tcg_op_buf_full(tcg_ctx) || db->num_insns >= db->max_insns) {'
    assert text.count(marker) == 1
    return text.replace(marker, '''        /* NEO98: temporaries, tools/build_unicorn_host.py. */
        if (tcg_op_buf_full(tcg_ctx) || db->num_insns >= db->max_insns ||
            tcg_ctx->nb_temps > TCG_MAX_TEMPS - 64) {''')



def patch_cpu_exec_c_exception(text):
    """A delivered x86 interrupt is complete even without QEMU's IDT path."""
    marker = "        cpu->exception_index = -1;\n    }\n\n    *ret = EXCP_INTERRUPT;"
    assert text.count(marker) == 1
    return text.replace(marker, """#if defined(TARGET_I386) || defined(TARGET_X86_64)
        /* NEO98: the hook replaces do_interrupt, which normally clears
         * old_exception. Leaving #DE pending turns the next independent
         * divide fault into a spurious double fault. */
        ((CPUArchState *)cpu->env_ptr)->old_exception = -1;
#endif
""" + marker)


# Each group is one change with one reason, and a group may touch several files.
# The groups apply in this order; `--without` leaves single groups out so a
# measurement can say what one of them is worth. "build" is not optional: the
# pinned tree carries one target and uc.c would not compile without it.
PATCHES = {
    'build': {'uc.c': patch_uc_c, 'include/uc_priv.h': patch_uc_priv_h},
    'measure': {'qemu/accel/tcg/cputlb.c': patch_cputlb_c,
                'qemu/accel/tcg/translate-all.c': patch_translate_all_c},
    'writeprotect': {'qemu/include/tcg/tcg-apple-jit.h': patch_tcg_apple_jit_h},
    'mapcache': {'qemu/unicorn_common.h': patch_unicorn_common_h,
                 'include/uc_priv.h': patch_uc_priv_h_cache,
                 'qemu/softmmu/memory.c': patch_memory_c},
    'notdirty': {'qemu/include/exec/exec-all.h': patch_exec_all_h,
                 'qemu/accel/tcg/cputlb.c': patch_cputlb_c_notdirty},
    'codeclean': {'qemu/include/exec/exec-all.h': patch_exec_all_h_codeclean,
                  'qemu/accel/tcg/translate-all.c': patch_translate_all_c_codeclean,
                  'qemu/accel/tcg/cputlb.c': patch_cputlb_c_codeclean},
    'exitcheck': {'qemu/include/exec/gen-icount.h': patch_gen_icount_h_exitcheck},
    'budget': {'qemu/include/exec/gen-icount.h': patch_gen_icount_h_budget,
               'qemu/include/exec/cpu-defs.h': patch_cpu_defs_h_budget,
               'qemu/accel/tcg/tcg-runtime.h': patch_tcg_runtime_h_budget,
               'qemu/accel/tcg/tcg-runtime.c': patch_tcg_runtime_c_budget,
               'include/uc_priv.h': patch_uc_priv_h_budget,
               'uc.c': patch_uc_c_budget},
    'registers': {'qemu/accel/tcg/tcg-runtime.c': patch_tcg_runtime_c_registers},
    'temps': {'qemu/accel/tcg/translator.c': patch_translator_c_temps},
    'exceptions': {'qemu/accel/tcg/cpu-exec.c': patch_cpu_exec_c_exception},
    'tscpause': {'qemu/target/i386/misc_helper.c': patch_misc_helper_c_tscpause,
                 'uc.c': patch_uc_c_tscpause},
}


def patched_sources(without=()):
    """The patched text of every file this build changes, keyed by relative path.

    `without` leaves single patches out. A measurement that has to say what one
    change is worth builds the library twice and compares; the stamp carries the
    selection, so the next ordinary build puts the full library back.
    """
    unknown = set(without) - (set(PATCHES) - {'build'})
    if unknown:
        raise SystemExit('unknown or mandatory patch group: ' + ', '.join(sorted(unknown)))
    texts = {}
    for group, files in PATCHES.items():
        if group in without:
            continue
        for name, function in files.items():
            texts[name] = function(texts.get(name) or (SOURCE / name).read_text())
    return texts


def identity(patched):
    """What the stamp compares: patched text, CMake arguments and the compiler."""
    digest = hashlib.sha256()
    for name in sorted(patched):
        digest.update(name.encode())
        digest.update(hashlib.sha256(patched[name].encode()).digest())
    digest.update(json.dumps(CMAKE_ARGUMENTS).encode())
    for tool in ('cc', 'cmake'):
        try:
            digest.update(subprocess.check_output([tool, '--version'], text=True,
                                                  stderr=subprocess.DEVNULL).encode())
        except Exception:
            digest.update(b'unknown ' + tool.encode())
    # The pinned tree itself: a re-import must rebuild even if the patched files
    # happen to produce the same text.
    digest.update((SOURCE / 'neo98-import.json').read_bytes())
    return digest.hexdigest()


def clone_tree(target):
    shutil.rmtree(target, ignore_errors=True)
    target.parent.mkdir(parents=True, exist_ok=True)
    # The resolved path: where third_party/unicorn is a link (a worktree that
    # shares main's imports), cp -R would copy the link, and the patches would
    # then be written into the pinned source itself.
    origin = SOURCE.resolve()
    if subprocess.run(['cp', '-Rc', str(origin), str(target)], capture_output=True).returncode:
        subprocess.run(['cp', '-R', str(origin), str(target)], check=True, capture_output=True)
    if target.is_symlink() or target.resolve() == origin:
        raise SystemExit(f'{target} is not a copy of {origin}; refusing to patch the pinned source')


def library(force=False, without=()):
    """The built library, rebuilt when the stamp no longer matches."""
    from unicorn_dependencies import ensure
    ensure('unicorn')
    patched = patched_sources(without)
    stamp_path, result = OUT / 'stamp.json', OUT / 'lib' / LIBRARY_NAME
    want = identity(patched)
    if not force and result.exists():
        try:
            if json.loads(stamp_path.read_text())['identity'] == want:
                return result
        except (OSError, ValueError, KeyError):
            pass
    source, build = OUT / 'source', OUT / 'build'
    clone_tree(source)
    for name, text in patched.items():
        (source / name).write_text(text)
    shutil.rmtree(build, ignore_errors=True)
    environment = dict(os.environ)
    subprocess.run(['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja', *CMAKE_ARGUMENTS],
                   check=True, env=environment, cwd=ROOT)
    # The machine has 8 GB: two unbounded QEMU builds at once (two tests
    # starting together) helped exhaust memory and swap on 2026-10-24.
    jobs = os.environ.get('NEO98_NATIVE_JOBS', '4')
    subprocess.run(['cmake', '--build', str(build), '--parallel', jobs], check=True, env=environment, cwd=ROOT)
    result.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(build / LIBRARY_NAME, result)
    if sys.platform == 'darwin':
        # The runners link with -rpath on this directory, so the library has to
        # name itself the way the loader will look it up.
        subprocess.run(['install_name_tool', '-id', '@rpath/' + LIBRARY_NAME, str(result)], check=True)
        subprocess.run(['codesign', '--force', '--sign', '-', str(result)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    stamp_path.write_text(json.dumps({'identity': want, 'version': json.loads(
        (SOURCE / 'neo98-import.json').read_text())['version'], 'patched': sorted(patched),
        'without': sorted(without)}, indent=1) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--force', action='store_true', help='rebuild even when the stamp matches')
    parser.add_argument('--without', default='', help='comma separated patches to leave out, for A/B measurements')
    arguments = parser.parse_args()
    print(library(force=arguments.force,
                  without=tuple(n for n in arguments.without.split(',') if n)))
