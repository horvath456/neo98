// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <link.h>
#include <sys/mman.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cstdint>
#include <limits>
#include "../src/linux_shared_memory.h"
#if __has_feature(address_sanitizer)
#include <sanitizer/allocator_interface.h>
#else
extern "C" size_t neo98_state_allocation_bytes(const void *);
#endif
namespace LinuxStatePlatform
{
struct Image { uint64_t slide = 0, begin = ~0ull, end = 0, writable = ~0ull; };
inline Image MainImage()
{
    Image image;
    dl_iterate_phdr(+[](dl_phdr_info *info, size_t, void *data) {
        if (info->dlpi_name && *info->dlpi_name) return 0;
        auto &image = *static_cast<Image *>(data); image.slide = info->dlpi_addr;
        for (unsigned i = 0; i < info->dlpi_phnum; ++i)
        {
            const auto &p = info->dlpi_phdr[i]; if (p.p_type != PT_LOAD) continue;
            const uint64_t begin = info->dlpi_addr + p.p_vaddr, end = begin + p.p_memsz;
            if (begin < image.begin) image.begin = begin;
            if (end > image.end) image.end = end;
            if ((p.p_flags & PF_W) && begin < image.writable) image.writable = begin;
        }
        return 1;
    }, &image);
    return image;
}
inline bool Identity(unsigned char out[16])
{
    bool found = false;
    struct Result { unsigned char *out; bool *found; } result{out,&found};
    dl_iterate_phdr(+[](dl_phdr_info *info,size_t,void *data) {
        if (info->dlpi_name && *info->dlpi_name) return 0;
        auto &r = *static_cast<Result *>(data);
        for (unsigned i = 0; i < info->dlpi_phnum; ++i)
        {
            const auto &p = info->dlpi_phdr[i]; if (p.p_type != PT_NOTE) continue;
            const auto *at = reinterpret_cast<const unsigned char *>(info->dlpi_addr + p.p_vaddr);
            const auto *end = at + p.p_memsz;
            while (size_t(end-at) >= sizeof(ElfW(Nhdr)))
            {
                const auto *note = reinterpret_cast<const ElfW(Nhdr) *>(at); at += sizeof(*note);
                const size_t names = (size_t(note->n_namesz)+3)&~size_t(3), bytes = (size_t(note->n_descsz)+3)&~size_t(3);
                if (names > size_t(end-at) || bytes > size_t(end-at)-names) break;
                if (note->n_type == NT_GNU_BUILD_ID && note->n_namesz == 4 && !memcmp(at,"GNU",4) && note->n_descsz >= 16)
                { memcpy(r.out,at+names,16); *r.found = true; return 1; }
                at += names+bytes;
            }
        }
        return 1;
    }, &result);
    return found;
}
struct Run { uint64_t begin, end; };
inline std::vector<Run> WritableRuns(uint64_t begin,uint64_t bytes)
{
    std::vector<Run> runs;
    FILE *file = fopen("/proc/self/maps","r"); if (!file) return runs;
    char line[8192];
    while (fgets(line,sizeof line,file))
    {
        unsigned long long low = 0,high = 0; char rights[5] = {};
        if (sscanf(line,"%llx-%llx %4s",&low,&high,rights) != 3 || rights[0] != 'r' || rights[1] != 'w') continue;
        if (low < begin) low = begin;
        if (high > begin+bytes) high = begin+bytes;
        if (low < high) runs.push_back({low,high});
    }
    fclose(file); return runs;
}
inline size_t AllocationSize(const void *pointer)
{
#if __has_feature(address_sanitizer)
    const size_t bytes = pointer && __sanitizer_get_ownership(pointer) ? __sanitizer_get_allocated_size(pointer) : 0;
#else
    const size_t bytes = neo98_state_allocation_bytes(pointer);
#endif
    if (bytes) return bytes;
    for (const auto &block : LinuxSharedMemory::blocks) if (block.host == pointer) return size_t(block.bytes);
    return 0;
}
inline size_t GoodSize(size_t bytes)
{
    if (bytes > std::numeric_limits<size_t>::max()-23) return bytes;
    const size_t result = ((bytes+23)&~size_t(15))-8;
    return result < 24 ? 24 : result;
}
}
