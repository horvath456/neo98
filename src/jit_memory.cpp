// SPDX-License-Identifier: GPL-2.0-or-later
// NEO98's allocation policy, independent of Circle. The current boot adapter
// installs these ranges as 64 KiB pages before enabling the MMU.
#include "jit_memory.h"

extern "C" {
alignas(NEO98_HOST_PAGE_BYTES) unsigned char neo98_jit_pool[NEO98_JIT_BYTES];
static bool busy[NEO98_JIT_SLOTS];
static unsigned live_slots, peak_slots, peak_flushes;
static unsigned long peak_code_bytes;
void *neo98_jit_alloc(unsigned long size)
{
    if (size > NEO98_JIT_SLOT_BYTES) return nullptr;
    for (unsigned slot = 0; slot < NEO98_JIT_SLOTS; ++slot)
    {
        if (busy[slot]) continue;
        busy[slot] = true;
        if (++live_slots > peak_slots) peak_slots = live_slots;
        return neo98_jit_pool + (unsigned long)slot * NEO98_JIT_SLOT_BYTES;
    }
    return nullptr;
}
void neo98_jit_free(void *pointer)
{
    if (!pointer) return;
    const auto offset = (unsigned long)pointer - (unsigned long)neo98_jit_pool;
    const unsigned slot = (unsigned)(offset / NEO98_JIT_SLOT_BYTES);
    if (offset >= NEO98_JIT_BYTES || offset % NEO98_JIT_SLOT_BYTES || !busy[slot]) __builtin_trap();
    busy[slot] = false;
    --live_slots;
}
unsigned neo98_jit_free_slots(void)
{
    return NEO98_JIT_SLOTS - live_slots;
}
unsigned neo98_boot_page_kind(unsigned long address)
{
    const auto start = (unsigned long)neo98_jit_pool;
    if (address < start || address >= start + NEO98_JIT_BYTES) return 0;
    const auto inside = (address - start) % NEO98_JIT_SLOT_BYTES;
    return inside == NEO98_JIT_SLOT_BYTES - NEO98_HOST_PAGE_BYTES ? 2 : 1;
}
int neo98_jit_is_guard(void *address, unsigned long size)
{
    return size == NEO98_HOST_PAGE_BYTES && neo98_boot_page_kind((unsigned long)address) == 2;
}
void neo98_jit_statistics(unsigned *live, unsigned *slots, unsigned long *code_bytes, unsigned *flushes)
{
    if (live) *live = live_slots;
    if (slots) *slots = peak_slots;
    if (code_bytes) *code_bytes = peak_code_bytes;
    if (flushes) *flushes = peak_flushes;
}
void neo98_jit_record(const void *buffer, const void *pointer, unsigned flushes)
{
    const auto used = (unsigned long)pointer - (unsigned long)buffer;
    if (used <= NEO98_JIT_SLOT_BYTES && used > peak_code_bytes) peak_code_bytes = used;
    if (flushes > peak_flushes) peak_flushes = flushes;
}
}
