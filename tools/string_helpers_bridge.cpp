// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_strings.h"
namespace
{
using U = unsigned;
using Read = bool (*)(U, void *, U);
using Write = bool (*)(U, const void *, U);
using Resize = U (*)(U, U, U);
using Free = bool (*)(U, U);
using Compare = int (*)(U, U, U, U);
using Service = U (*)(U, U, U, U, U);
struct Bridge final : Win98Strings::Environment
{
    ::Read read;
    ::Write write;
    ::Resize resize;
    ::Free free;
    ::Compare compare;
    Service service;
    Bridge(::Read r, ::Write w, ::Resize a, ::Free f, ::Compare c, Service s)
        : read(r), write(w), resize(a), free(f), compare(c), service(s)
    {
    }
    bool Read(U a, void *p, U n) override { return read(a, p, n); }
    bool Write(U a, const void *p, U n) override { return write(a, p, n); }
    U Resize(U h, U a, U n) override { return resize(h, a, n); }
    bool Free(U h, U a) override { return free(h, a); }
    int Compare(U p, U a, U b, U c) override { return compare(p, a, b, c); }
    bool LeadByte(U c) override { return service(0, c, 0, 0, 0); }
    U Next(U p) override { return service(1, p, 0, 0, 0); }
    bool Readable(U p) override { return service(2, p, 0, 0, 0); }
    U ToWide(U flags, U p, U out, U n) override { return service(3, flags, p, out, n); }
    U Local(U n) override { return service(4, n, 0, 0, 0); }
    void ReleaseLocal(U p) override { service(5, p, 0, 0, 0); }
};
} // namespace
extern "C" unsigned helper_call(Read r, Write w, Resize a, Free f, Compare c, Service s, unsigned ordinal,
                                const unsigned *args)
{
    Bridge m(r, w, a, f, c, s);
    return Win98Strings::Helper(m, ordinal, args);
}
