// SPDX-License-Identifier: GPL-2.0-or-later
#define private public
#include "../src/x86_app.h"
#undef private
#include <cassert>
#include <cstdio>
#include <vector>
static constexpr unsigned MiB = 1024 * 1024;
static std::vector<unsigned char> load(const char *path)
{
    FILE *f = fopen(path, "rb"); assert(f);
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    std::vector<unsigned char> bytes(n);
    assert(fread(bytes.data(), 1, n, f) == size_t(n)); fclose(f);
    return bytes;
}
static void block_extension(X86::Application &app)
{
    assert(app.virtual_memory.Available(app.heap_end, 65536));
    assert(app.Map(app.heap_end, 65536, 3));
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    for (unsigned scenario = 0; scenario < 4; ++scenario)
    {
        X86::Application app(nullptr);
        assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "HEAPGROW.EXE"));
        // Loader mappings can already sit just above the initial heap.
        // Give each case a segment with a known free extension, then occupy
        // that extension explicitly in the fragmentation cases.
        const unsigned segment = app.virtual_memory.FindPrivate(64 * MiB);
        assert(segment && app.Map(segment, 32 * MiB, 3));
        app.next_alloc = segment;
        app.heap_end = segment + 32 * MiB;
        app.heap_total += 32 * MiB;
        const unsigned old = app.Allocate(MiB);
        assert(old);
        app.Write32(old, 0x12345678);
        app.Write32(old + MiB - 4, 0x87654321);
        const unsigned tail_start = app.next_alloc, tail_size = app.heap_end - app.next_alloc;
        const unsigned total = app.heap_total;
        if (scenario) block_extension(app);
        if (scenario < 2)
        {
            const unsigned args[] = {0, 32 * MiB};
            const unsigned large = app.Api("GlobalAlloc", args);
            assert(large && app.AllocationSize(large) == 32 * MiB);
            app.Write32(large, 0xaabbccdd);
            app.Write32(large + 32 * MiB - 4, 0x11223344);
            assert(app.Read32(large) == 0xaabbccdd && app.Read32(large + 32 * MiB - 4) == 0x11223344);
            assert(app.Read32(old) == 0x12345678 && app.Read32(old + MiB - 4) == 0x87654321);
            if (!scenario)
            {
                assert(large == tail_start && app.heap_total == total + 16 * MiB);
            }
            else
            {
                assert(large != tail_start && app.heap_total == total + 32 * MiB);
                // The abandoned tail remains reusable and must not alias the
                // allocation record used for the new segment.
                assert(app.Allocate(tail_size) == tail_start);
                assert(app.Read32(large) == 0xaabbccdd);
            }
        }
        else
        {
            unsigned args[] = {old, 40 * MiB, 0};
            assert(!app.Api("LocalReAlloc", args));
            assert(app.heap_total == total && app.next_alloc == tail_start);
            assert(app.AllocationSize(old) == MiB && app.Read32(old) == 0x12345678);
            if (scenario == 2)
            {
                args[2] = 2; // LMEM_MOVEABLE permits a new address.
                const unsigned moved = app.Api("LocalReAlloc", args);
                assert(moved && moved != old && app.AllocationSize(moved) == 40 * MiB);
                assert(app.Read32(moved) == 0x12345678 && app.Read32(moved + MiB - 4) == 0x87654321);
                assert(!app.AllocationSize(old));
            }
            else
            {
                // Only 16 MiB of quota remains: enough for the deficit,
                // insufficient for the complete 32 MiB replacement segment.
                app.heap_total = app.HeapArenaLimit - 16 * MiB;
                const unsigned before = app.heap_total;
                assert(!app.Allocate(32 * MiB));
                assert(app.heap_total == before && app.next_alloc == tail_start);
                assert(app.Read32(old) == 0x12345678);
            }
        }
        assert(app.Status() != X86::Application::Failed);
    }
    puts("HEAP GROWTH PASS: contiguous, fragmented, tail reuse, fixed/movable realloc, quota failure");
}
