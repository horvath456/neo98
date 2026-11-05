// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_find.h"
namespace
{
using U = unsigned;
struct Bridge final : Win98Find::Environment
{
    using ReadFn = bool (*)(U, void *, U);
    using WriteFn = bool (*)(U, const void *, U);
    using AllocFn = U (*)(U, U, U);
    using FreeFn = bool (*)(U, U);
    using ServiceFn = U (*)(const char *, const U *, U);
    using HookFn = U (*)(U, U, U, U, U);
    using LiteralFn = U (*)(const char *);
    ReadFn read;
    WriteFn write;
    AllocFn alloc;
    FreeFn free;
    ServiceFn service;
    HookFn hook;
    LiteralFn literal;
    Bridge(ReadFn r, WriteFn w, AllocFn a, FreeFn f, ServiceFn s, HookFn h, LiteralFn l)
        : read(r), write(w), alloc(a), free(f), service(s), hook(h), literal(l)
    {
    }
    bool Read(U a, void *p, U n) override { return read(a, p, n); }
    bool Write(U a, const void *p, U n) override { return write(a, p, n); }
    U Resize(U h, U a, U n) override { return alloc(h, a, n); }
    bool Free(U h, U a) override { return free(h, a); }
    int Compare(U, U, U, U) override { return 0; }
    U Service(const char *n, const U *a, U c) override { return service(n, a, c); }
    U Hook(U p, U h, U m, U w, U l) override { return hook(p, h, m, w, l); }
    U Literal(const char *t) override { return literal(t); }
};
} // namespace
extern "C" unsigned find_call(Bridge::ReadFn r, Bridge::WriteFn w, Bridge::AllocFn a, Bridge::FreeFn f,
                              Bridge::ServiceFn service, Bridge::HookFn hook, Bridge::LiteralFn literal,
                              Win98Find::State *state, unsigned operation, const unsigned *args)
{
    Bridge e(r, w, a, f, service, hook, literal);
    return operation == 0 ? Win98Find::Validate(e, *state, args[0], args[1] != 0)
                          : Win98Find::Dialog(e, *state, args[0], args[1], args[2], args[3]);
}
