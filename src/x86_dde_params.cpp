// SPDX-License-Identifier: GPL-2.0-or-later
// The four USER32 exports that pack and unpack a DDE message lParam:
// PackDDElParam, UnpackDDElParam, FreeDDElParam and ReuseDDElParam. All four
// are flat thunks into USER.EXE's DDE code, which keeps the packed pairs in
// one system wide pool, because a DDE message travels between processes. The
// store below is that pool: one table for the whole native runtime, not one
// per Application.
#include "x86_app.h"
namespace X86 {
namespace {
// The DDE messages. Only four of them carry a packed pair; WM_DDE_EXECUTE
// carries the data handle itself and the remaining three carry two words.
constexpr unsigned DdeInitiate = 0x3e0, DdeTerminate = 0x3e1, DdeAdvise = 0x3e2;
constexpr unsigned DdeUnadvise = 0x3e3, DdeAck = 0x3e4, DdeData = 0x3e5;
constexpr unsigned DdeRequest = 0x3e6, DdePoke = 0x3e7, DdeExecute = 0x3e8;
bool Packed(unsigned message)
{
    return message == DdeAck || message == DdeAdvise || message == DdeData || message == DdePoke;
}
// The pool. A handle is the tag 'DD' in the high word and the slot index plus
// one in the low word, so a freed slot is never mistaken for a live one and a
// packed lParam never looks like the MAKELONG form of a small window handle.
constexpr unsigned PoolTag = 0x44440000, PoolCapacity = 256;
struct Entry { bool used = false; unsigned low = 0, high = 0; };
Entry pool[PoolCapacity];
unsigned Handle(unsigned index) { return PoolTag | (index + 1); }
Entry *Find(unsigned parameter)
{
    if ((parameter & 0xffff0000u) != PoolTag) return nullptr;
    const unsigned index = (parameter & 65535) - 1;
    if (index >= PoolCapacity || !pool[index].used) return nullptr;
    return &pool[index];
}
unsigned AllocatePair(unsigned low, unsigned high)
{
    for (unsigned i = 0; i < PoolCapacity; ++i)
        if (!pool[i].used)
        {
            pool[i] = {true, low, high};
            return Handle(i);
        }
    return 0;
}
} // namespace

unsigned Application::DdeParameterApi(ApiName name, const unsigned *a)
{
    switch (name.id)
    {
    API_CASE("PackDDElParam")
    {
        if (a[0] == DdeExecute) return a[2];
        if (!Packed(a[0]))
        {
            if (a[0] != DdeInitiate && a[0] != DdeTerminate && a[0] != DdeUnadvise &&
                a[0] != DdeRequest)
            {
                // The original pool only knows the DDE messages. A foreign
                // message would silently get the MAKELONG form; say so instead.
                last_error = 87;
                return 0;
            }
            return (a[1] & 65535) | (a[2] << 16);
        }
        const unsigned handle = AllocatePair(a[1], a[2]);
        if (!handle) last_error = 8;
        return handle;
    }
    API_CASE("UnpackDDElParam")
    {
        unsigned low = 0, high = 0;
        bool okay = true;
        if (a[0] == DdeExecute) high = a[1];
        else if (!Packed(a[0])) { low = a[1] & 65535; high = a[1] >> 16; }
        else if (const auto *entry = Find(a[1])) { low = entry->low; high = entry->high; }
        else okay = false;
        if (a[2] && !Store(a[2], &low, 4)) return 0;
        if (a[3] && !Store(a[3], &high, 4)) return 0;
        return okay ? 1u : 0u;
    }
    API_CASE("FreeDDElParam")
    {
        if (!Packed(a[0])) return 1;
        auto *entry = Find(a[1]);
        if (!entry) return 0;
        *entry = {};
        return 1;
    }
    API_CASE("ReuseDDElParam")
    {
        // The pair travels from the incoming message to the outgoing one. When
        // both messages pack, the slot stays where it is; otherwise the old
        // slot goes back into the pool and the new form is built from scratch.
        auto *entry = Find(a[0]);
        if (Packed(a[1]) && Packed(a[2]))
        {
            if (!entry) return 0;
            entry->low = a[3];
            entry->high = a[4];
            return a[0];
        }
        if (Packed(a[1]))
        {
            if (!entry) return 0;
            *entry = {};
        }
        const unsigned arguments[] = {a[2], a[3], a[4]};
        return DdeParameterApi("PackDDElParam", arguments);
    }
    default:
        break;
    }
    Fail(name);
    return 0;
}
} // namespace X86
