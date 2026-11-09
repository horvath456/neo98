// SPDX-License-Identifier: GPL-2.0-or-later
// Back host service pages with a file so native guest processes map the
// same physical bytes, including allocations made after their fork.
#pragma once
#if defined(__linux__) && defined(__x86_64__)
#include <cstdint>
#include <linux/memfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>
namespace LinuxSharedMemory
{
struct Block { unsigned char *host; uint64_t bytes; int fd; };
inline std::vector<Block> blocks;
inline void *Allocate(uint64_t bytes, void *reserved = nullptr, int protection = PROT_READ | PROT_WRITE)
{
    if (!bytes || bytes > (1ull << 32)) return nullptr;
    bytes = (bytes + 4095) & ~4095ull;
    const int fd = syscall(SYS_memfd_create, "neo98-shared", MFD_CLOEXEC);
    if (fd < 0) return nullptr;
    if (ftruncate(fd, bytes)) { close(fd); return nullptr; }
    void *host = mmap(reserved, bytes, protection, MAP_SHARED | (reserved ? MAP_FIXED : 0), fd, 0);
    if (host == MAP_FAILED) { close(fd); return nullptr; }
    blocks.push_back({static_cast<unsigned char *>(host), bytes, fd});
    return host;
}
// Saved service blocks may require more than page alignment. Keep them
// file-backed on reload too, so all guest views still alias the same bytes.
inline void *AllocateAligned(uint64_t bytes, unsigned alignment)
{
    if (alignment <= 12) return Allocate(bytes);
    if (alignment > 16 || !bytes || (bytes & 4095) || bytes > (1ull << 32)) return nullptr;
    const uint64_t boundary = 1ull << alignment, total = bytes + boundary;
    void *reservation = mmap(nullptr, total, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (reservation == MAP_FAILED) return nullptr;
    const auto begin = reinterpret_cast<uintptr_t>(reservation);
    const auto aligned = (begin + boundary - 1) & ~(boundary - 1);
    void *result = Allocate(bytes, reinterpret_cast<void *>(aligned));
    if (!result) { munmap(reservation, total); return nullptr; }
    if (aligned > begin) munmap(reservation, aligned - begin);
    munmap(reinterpret_cast<void *>(aligned + bytes), begin + total - aligned - bytes);
    return result;
}
inline bool Find(const void *host, uint64_t bytes, int &fd, uint64_t &offset)
{
    const auto pointer = reinterpret_cast<uintptr_t>(host);
    for (const auto &block : blocks)
    {
        const auto base = reinterpret_cast<uintptr_t>(block.host);
        if (pointer >= base && pointer - base <= block.bytes && bytes <= block.bytes - (pointer - base))
        { fd = block.fd; offset = pointer - base; return !(offset & 4095); }
    }
    return false;
}
inline bool Free(void *host)
{
    for (size_t i = 0; i < blocks.size(); ++i) if (blocks[i].host == host)
    {
        munmap(host, blocks[i].bytes); close(blocks[i].fd);
        blocks.erase(blocks.begin() + i); return true;
    }
    return false;
}
}
#endif
