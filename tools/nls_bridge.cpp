// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_nls.h"
namespace
{
unsigned ansi = 1252, oem = 437, thread = 0;
Win98Nls::Table tables[3];
const unsigned pages[] = {1252, 437, 850};
using U = unsigned;
struct Bridge final : Win98Nls::Environment
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
    U Ansi() override { return ansi; }
    U Oem() override { return oem; }
    const Win98Nls::Table *CodePage(U page) override
    {
        for (U i = 0; i < 3; ++i)
            if (pages[i] == page)
                return &tables[i];
        return nullptr;
    }
    void Error(U error) override { Word(thread + 0x68, error); }
    void Unsupported(const char *) override { Error(120); }
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
    return Win98Nls::Call(memory, ordinal, args);
}

extern "C" unsigned nls_table(unsigned index, const unsigned char *data, unsigned size)
{
    if (index >= 3)
        return 0;
    tables[index] = {data, size};
    return tables[index].Valid();
}
extern "C" void nls_state(unsigned acp, unsigned ocp, unsigned t)
{
    ansi = acp;
    oem = ocp;
    thread = t;
}
