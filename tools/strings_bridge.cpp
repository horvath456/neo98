// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_strings.h"
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
} // namespace
extern "C" unsigned array_call(Bridge::ReadFn r, Bridge::WriteFn w, Bridge::ResizeFn a, Bridge::FreeFn f,
                               Bridge::CompareFn c, unsigned ordinal, const unsigned *args)
{
    Bridge memory(r, w, a, f, c);
    return Win98Strings::Call(memory, ordinal, args);
}
