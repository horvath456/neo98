// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Initial boot configuration. Guest pages retain x86's 4 KiB granularity.
#define NEO98_HOST_PAGE_BYTES 65536
// One slot backs one Unicorn engine. The last host page of a slot stays
// unmapped so TCG's region guard page keeps working.
#define NEO98_JIT_SLOTS 4
#define NEO98_JIT_SLOT_BYTES (2 * 1024 * 1024)
#define NEO98_JIT_BYTES (NEO98_JIT_SLOTS * NEO98_JIT_SLOT_BYTES)
// The translation cache one engine may fill. On the Pi the cache is a slot of
// the fixed pool above, so the slot size is the limit. A host with an MMU lets
// Unicorn take the buffer from mmap and has no such limit, and a program that
// translates more code than fits flushes the whole cache and translates the
// same blocks again.
#if defined(__APPLE__) || defined(__linux__)
#define NEO98_JIT_CACHE_BYTES (64 * 1024 * 1024)
#else
#define NEO98_JIT_CACHE_BYTES NEO98_JIT_SLOT_BYTES
#endif
#ifdef __cplusplus
extern "C" {
#endif
void *neo98_jit_alloc(unsigned long size);
void neo98_jit_free(void *pointer);
// 0: platform default; 1: kernel executable cache; 2: unmapped guard.
unsigned neo98_boot_page_kind(unsigned long address);
int neo98_jit_is_guard(void *address, unsigned long size);
// Live slots, highest number ever live, largest observed code bytes in one
// slot and the largest translation-buffer flush count of any engine.
void neo98_jit_statistics(unsigned *live, unsigned *peak_slots,
                          unsigned long *peak_code_bytes, unsigned *flushes);
// Called by the patched TCG buffer allocator for every generated block.
void neo98_jit_record(const void *buffer, const void *pointer, unsigned flushes);
// Slots a further uc_open could still take. Unicorn aborts when its buffer
// allocation fails, so callers ask before opening an engine.
unsigned neo98_jit_free_slots(void);
#ifdef __cplusplus
}
#endif
