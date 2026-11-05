// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_printf.h"
namespace
{
using U = unsigned;
struct Bridge final : Win98Arrays::Memory
{
    using ReadFn = bool (*)(U, void *, U);
    using WriteFn = bool (*)(U, const void *, U);
    using ResizeFn = U (*)(U, U, U);
    using FreeFn = bool (*)(U, U);
    using CompareFn = int (*)(U, U, U, U);
    ReadFn read;
    WriteFn write;
    ResizeFn resize;
    FreeFn free;
    CompareFn compare;
    Bridge(ReadFn r, WriteFn w, ResizeFn a, FreeFn f, CompareFn c)
        : read(r), write(w), resize(a), free(f), compare(c)
    {
    }
    bool Read(U a, void *p, U n) override { return read(a, p, n); }
    bool Write(U a, const void *p, U n) override { return write(a, p, n); }
    U Resize(U h, U a, U n) override { return resize(h, a, n); }
    bool Free(U h, U a) override { return free(h, a); }
    int Compare(U p, U a, U b, U c) override { return compare(p, a, b, c); }
};
// The code page table the original walks lives in memory the loader hands to
// the process, so the test supplies the same ranges through this callback.
using NarrowFn = U (*)(U, unsigned char *);
NarrowFn narrow_hook = nullptr;
struct Page final : Win98Printf::Ansi
{
    U Narrow(U character, unsigned char *bytes) override
    {
        return narrow_hook ? narrow_hook(character, bytes) : 0;
    }
};
} // namespace
extern "C" void printf_narrowing(NarrowFn f) { narrow_hook = f; }
extern "C" unsigned array_call(Bridge::ReadFn r, Bridge::WriteFn w, Bridge::ResizeFn a, Bridge::FreeFn f,
                               Bridge::CompareFn c, unsigned ordinal, const unsigned *args)
{
    Bridge memory(r, w, a, f, c);
    Page ansi;
    bool supported = false;
    return Win98Printf::Format(memory, args[0], args[1], args[2], supported, &ansi);
}
