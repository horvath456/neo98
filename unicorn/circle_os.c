// SPDX-License-Identifier: GPL-2.0-or-later
// Unicorn's OS allocation interface for Circle. Guest permissions are handled
// by Unicorn's soft MMU; these allocations are private kernel backing storage.
#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "../src/jit_memory.h"

void *qemu_try_memalign(size_t alignment, size_t size)
{
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    if ((alignment & (alignment - 1)) || size > SIZE_MAX - alignment - sizeof(void *)) return NULL;
    void *base = malloc(size + alignment + sizeof(void *));
    if (!base) return NULL;
    uintptr_t address = ((uintptr_t)base + sizeof(void *) + alignment - 1) & ~(alignment - 1);
    ((void **)address)[-1] = base;
    return (void *)address;
}
void *qemu_oom_check(void *pointer) { if (!pointer) abort(); return pointer; }
void *qemu_memalign(size_t alignment, size_t size) { return qemu_oom_check(qemu_try_memalign(alignment, size)); }
void qemu_vfree(void *pointer) { if (pointer) free(((void **)pointer)[-1]); }
void *qemu_anon_ram_alloc(struct uc_struct *uc, size_t size, uint64_t *alignment)
{
    (void)uc;
    if (alignment) *alignment = NEO98_HOST_PAGE_BYTES;
    void *pointer = qemu_try_memalign(NEO98_HOST_PAGE_BYTES, size);
    if (pointer) memset(pointer, 0, size);
    return pointer;
}
void qemu_anon_ram_free(struct uc_struct *uc, void *pointer, size_t size)
{ (void)uc; (void)size; qemu_vfree(pointer); }
int qemu_madvise(void *address, size_t length, int advice)
{ (void)address; (void)length; (void)advice; errno = ENOSYS; return -1; }
int qemu_mprotect_none(void *address, size_t length)
{
    // This final 64 KiB page is unmapped when the boot adapter builds tables.
    if (neo98_jit_is_guard(address, length)) return 0;
    errno = ENOSYS; return -1;
}

// uc_emu_start rejects wall-clock timeouts before reaching these functions.
// Execution is bounded by the instruction-count parameter instead.
int qemu_thread_create(struct uc_struct *uc, QemuThread *thread, const char *name,
                       void *(*start)(void *), void *argument, int mode)
{ (void)uc; (void)thread; (void)name; (void)start; (void)argument; (void)mode; errno = ENOSYS; return -1; }
void *qemu_thread_join(QemuThread *thread) { (void)thread; abort(); }
void qemu_thread_exit(struct uc_struct *uc, void *result) { (void)uc; (void)result; abort(); }
