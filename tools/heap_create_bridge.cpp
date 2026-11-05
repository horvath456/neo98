// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_heap_create.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
using U = unsigned;
[[maybe_unused]] constexpr U ConfigWords = 12;
constexpr U OutputWords = 256, SnapshotBytes = 4 * 4096;
struct Environment
{
    const U *c;
    U *out, commits = 0, events = 0, reserved = 0, reserved_bytes = 0;
    Win98HeapCreate::State state;
    std::map<U, std::array<unsigned char, 4096>> pages;
    Environment(const U *config, U *output) : c(config), out(output)
    {
        state.private_head = c[8];
        state.critical_sections_ready = c[7] != 0;
        out[1] = c[9];
    }
    void Log(U kind, U a = 0, U b = 0, U d = 0, U f = 0)
    {
        assert(events < 40);
        U *at = out + 32 + events++ * 5;
        at[0] = kind; at[1] = a; at[2] = b; at[3] = d; at[4] = f;
    }
    void Seed(U page)
    {
        if (!pages.count(page)) pages[page].fill(0xcc);
    }
    U Read8(U address)
    {
        auto i = pages.find(address >> 12);
        assert(i != pages.end());
        return i->second[address & 4095];
    }
    U Read32(U a) { return Read8(a) | Read8(a + 1) << 8 | Read8(a + 2) << 16 | Read8(a + 3) << 24; }
    void Write8(U address, U value)
    {
        auto i = pages.find(address >> 12);
        assert(i != pages.end());
        i->second[address & 4095] = value;
    }
    void Write16(U a, U v) { Write8(a, v); Write8(a + 1, v >> 8); }
    void Write32(U a, U v) { for (U i = 0; i < 4; ++i) Write8(a + i, v >> (i * 8)); }
    void Error(U code) { Log(9, code); out[1] = code; }
    U Reserve(U page, U count, U flags)
    {
        Log(1, page, count, flags);
        if (c[5]) return ~0u;
        reserved = c[4] ? c[4] : page == 0x80060000 ? 0x81000000u : 0x04000000u;
        reserved_bytes = count << 12;
        return reserved;
    }
    U Commit(U page, U count, U flags)
    {
        Log(2, page, count, flags);
        if (++commits == c[6]) { Error(8); return 0; }
        for (U i = 0; i < count; ++i) Seed(page + i);
        return 1;
    }
    void Decommit(U page, U count, U flags)
    {
        Log(4, page, count, flags);
        for (U i = 0; i < count; ++i) pages.erase(page + i);
    }
    void Release(U base, U flags)
    {
        Log(5, base, flags);
        const U bytes = c[0] == 5 ? Read32(base) : reserved_bytes;
        for (auto i = pages.begin(); i != pages.end();)
            if (i->first >= base >> 12 && i->first - (base >> 12) < bytes >> 12)
                i = pages.erase(i);
            else ++i;
    }
    void InitializeCriticalSection(U a)
    {
        Log(6, a);
        for (U i = 0; i < 24; ++i) Write8(a + i, 0);
        Write32(a + 4, 0x12345678);
    }
    void InitializeEarlyLock(U a) { Log(7, a); Write32(a, 0x11111111); }
    void MakeCriticalSectionGlobal(U a) { Log(8, a); }
    void EnterKernel() { Log(10); }
    void LeaveKernel() { Log(11); }
    U ValidateHeap(U heap, U block, U flags)
    {
        Log(12, heap, block, flags);
        if (c[5]) { Error(87); return 0; }
        return 1;
    }
    void UnlockHeap(U heap, U flags) { Log(13, heap, flags); }
    U PrimaryHeap() { return c[10] ? c[4] : 0; }
    void ClosePrimaryLock(U a) { Log(14, a); }
    void DeleteHeapLock(U a) { Log(15, a); }
    U InitializeHeap(U heap, U base, U size, U flags)
    {
        Log(3, heap, base, size, flags);
        if (c[10]) return 0;
        if (c[0] == 2) return Win98HeapCreate::Initialize(*this, state, heap, base, size, flags);
        Seed(base >> 12);
        return base;
    }
    void Run()
    {
        const U base = c[4] ? c[4] : 0x81000000;
        if (c[0] == 1)
            out[0] = Win98HeapCreate::Initialize(*this, state, base, base, c[2], c[1]);
        else if (c[0] == 3)
        {
            out[0] = Win98HeapCreate::Initialize(*this, state, base, base, 4096, c[1] & ~0x100u);
            if (out[0]) out[0] = Win98HeapCreate::Initialize(*this, state, base, base + 0x200000, c[2], c[1] | 0x100);
        }
        else if (c[0] == 4)
        {
            out[0] = Win98HeapCreate::Initialize(*this, state, base, base, c[2], c[1]);
            if (out[0])
            {
                const U block = base + 0x74, bytes = Read32(block) & 0x0ffffffc;
                const U previous = Read32(block + 8), next = Read32(block + 4);
                Write32(previous + 4, next); Write32(next + 8, previous);
                Write32(block, c[11] | 0xa0000000);
                if (((block + c[11]) >> 12) != (block >> 12))
                    Commit((block + c[11]) >> 12, 1, c[1]);
                Win98HeapCreate::InsertFree(*this, base, block + c[11], bytes - c[11], 0);
                Win98HeapCreate::InsertFree(*this, base, block, c[11], c[1] & 0x200);
            }
        }
        else if (c[0] == 5)
        {
            out[0] = Win98HeapCreate::Initialize(*this, state, base, base, 4096, c[1]);
            if (c[2])
            {
                const U extension = base + 0x200000;
                Win98HeapCreate::Initialize(*this, state, base, extension, c[2], c[1] | 0x100);
                Write32(base + 4, extension); Write32(extension + 4, 0);
            }
            Write32(base + 0x4c, base + 0x50); Write8(base + 0x50, c[11] & 2 ? 4 : 0);
            if (base < 0x7fffffff)
            {
                Write32(base + 0x48, state.private_head);
                if (c[11] & 1)
                {
                    state.private_head = base - 0x10000;
                    Seed(state.private_head >> 12); Write32(state.private_head + 0x48, base);
                }
                else state.private_head = base;
            }
            out[0] = Win98HeapCreate::Destroy(*this, state, base);
        }
        else
            out[0] = Win98HeapCreate::Create(*this, state, c[1], c[2], c[3]);
        out[2] = state.private_head;
        out[3] = events;
        out[4] = pages.size();
    }
};
extern "C" void heap_create(const U *config, U *output, unsigned char *snapshot)
{
    std::memset(output, 0, OutputWords * sizeof(U));
    std::memset(snapshot, 0, SnapshotBytes);
    Environment e(config, output);
    e.Run();
    assert(e.pages.size() <= 4);
    U i = 0;
    for (const auto &page : e.pages)
    {
        output[8 + i] = page.first;
        std::memcpy(snapshot + i++ * 4096, page.second.data(), 4096);
    }
}
#ifdef HEAP_CREATE_MAIN
int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *input = std::fopen(argv[1], "rb"); assert(input);
    U config[ConfigWords], expected[OutputWords], actual[OutputWords], count = 0;
    unsigned char bytes[SnapshotBytes], result[SnapshotBytes];
    while (std::fread(config, sizeof config, 1, input))
    {
        assert(std::fread(expected, sizeof expected, 1, input) == 1);
        assert(std::fread(bytes, sizeof bytes, 1, input) == 1);
        heap_create(config, actual, result);
        assert(!std::memcmp(expected, actual, sizeof actual));
        assert(!std::memcmp(bytes, result, sizeof result));
        ++count;
    }
    assert(std::feof(input)); std::fclose(input);
    std::printf("HEAP CREATE NATIVE PASS %u original cases, complete committed-page snapshots\n", count);
}
#endif
