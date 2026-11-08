// SPDX-License-Identifier: GPL-2.0-or-later
// Exact live allocation bases. State readers never ask glibc to inspect an
// arbitrary pointer; the registry itself uses glibc without interposition.
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <malloc.h>
#include <pthread.h>
#include <algorithm>
#include <cstring>
#include "../src/linux_shared_memory.h"
extern "C" {
void *__libc_malloc(size_t);
void *__libc_calloc(size_t,size_t);
void *__libc_realloc(void *,size_t);
void __libc_free(void *);
void *__libc_memalign(size_t,size_t);
}
namespace {
struct Entry { const void *base; size_t bytes; Entry *next; };
constexpr size_t Buckets = 16384;
Entry *entries[Buckets] = {};
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
// GTK's image loaders and the native CPU can fork after audio and other
// worker threads have started. The child must not inherit a registry lock
// held by a thread that no longer exists there.
void BeforeFork() { pthread_mutex_lock(&lock); }
void AfterFork() { pthread_mutex_unlock(&lock); }
__attribute__((constructor)) void ForkHandlers()
{
    pthread_atfork(BeforeFork, AfterFork, AfterFork);
}
size_t Bucket(const void *p) { return (reinterpret_cast<uintptr_t>(p) >> 4) % Buckets; }
void Remove(const void *p)
{
    if (!p) return;
    pthread_mutex_lock(&lock);
    Entry **at = &entries[Bucket(p)];
    while (*at && (*at)->base != p) at = &(*at)->next;
    if (*at) { Entry *old = *at; *at = old->next; __libc_free(old); }
    pthread_mutex_unlock(&lock);
}
void Add(void *p,size_t bytes)
{
    if (!p) return;
    const int error = errno;
    auto *entry = static_cast<Entry *>(__libc_malloc(sizeof(Entry)));
    if (!entry) { errno = error; return; }
    entry->base = p; entry->bytes = bytes;
    pthread_mutex_lock(&lock);
    const size_t bucket = Bucket(p); entry->next = entries[bucket]; entries[bucket] = entry;
    pthread_mutex_unlock(&lock); errno = error;
}
}
extern "C" size_t neo98_state_allocation_bytes(const void *p)
{
    if (!p) return 0;
    pthread_mutex_lock(&lock);
    Entry *entry = entries[Bucket(p)];
    while (entry && entry->base != p) entry = entry->next;
    const size_t bytes = entry ? entry->bytes : 0;
    pthread_mutex_unlock(&lock); return bytes;
}
extern "C" void *malloc(size_t bytes) { void *p = __libc_malloc(bytes); Add(p,bytes); return p; }
extern "C" void *calloc(size_t count,size_t bytes) { void *p = __libc_calloc(count,bytes); Add(p,count*bytes); return p; }
extern "C" void free(void *p)
{
    const int error = errno;
    if (p && !(reinterpret_cast<uintptr_t>(p)&4095) && LinuxSharedMemory::Free(p)) { errno = error; return; }
    Remove(p); __libc_free(p); errno = error;
}
extern "C" void *realloc(void *p,size_t bytes)
{
    // Restored page-aligned arrays may back guest aliases. Ordinary delete
    // and realloc retain their original ownership contract for these arrays.
    if (p && !(reinterpret_cast<uintptr_t>(p)&4095))
        for (const auto &block : LinuxSharedMemory::blocks) if (block.host == p)
        {
            if (!bytes) { LinuxSharedMemory::Free(p); return nullptr; }
            void *result = __libc_malloc(bytes);
            if (!result) return nullptr;
            memcpy(result,p,std::min(bytes,size_t(block.bytes))); Add(result,bytes);
            LinuxSharedMemory::Free(p); return result;
        }
    const size_t old_bytes = neo98_state_allocation_bytes(p);
    Remove(p);
    void *result = __libc_realloc(p,bytes);
    const int error = errno;
    Add(result ? result : (bytes ? p : nullptr),result ? bytes : old_bytes);
    errno = error;
    return result;
}
extern "C" int posix_memalign(void **out,size_t alignment,size_t bytes)
{
    if (alignment < sizeof(void *) || alignment % sizeof(void *) || (alignment & (alignment-1))) return EINVAL;
    const int error = errno; void *p = __libc_memalign(alignment,bytes); errno = error;
    if (!p) return ENOMEM;
    Add(p,bytes); *out = p; return 0;
}
extern "C" void *aligned_alloc(size_t alignment,size_t bytes)
{ void *p = __libc_memalign(alignment,bytes); Add(p,bytes); return p; }
