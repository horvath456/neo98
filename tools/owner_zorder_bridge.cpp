// SPDX-License-Identifier: GPL-2.0-or-later
// Bridge for the implemented USER owned-window z-order pass.
#include "../src/win98_owner_zorder.h"
using namespace Win98OwnerZOrder;
using U = unsigned;
enum { Windows = 12, Entries = 32, Trace = 512 };
struct State
{
    Node w[Windows];
    U valid, disabled, capacity;
    Entry entry[Entries];
    U count;
    U trace[Trace], used;
};
struct Provider
{
    State &s;
    void Event(U a, U b = 0, U c = 0, U d = 0)
    {
        if (s.used + 4 <= Trace) { s.trace[s.used++] = a; s.trace[s.used++] = b; s.trace[s.used++] = c; s.trace[s.used++] = d; }
    }
    U Capacity() { return s.capacity; }
    Node Read(U h) { return h < Windows ? s.w[h] : Node{}; }
    bool IsWindow(U h) { Event(1, h); return h < Windows && (s.valid & (1u << h)); }
    // The original clears a stale owner with a plain store; the effect is
    // compared through the window fields, not through the trace.
    void ClearOwner(U h) { if (h < Windows) s.w[h].owner = 0; }
    bool Disabled() { return s.disabled != 0; }
    U Count() { return s.count; }
    void SetCount(U n) { s.count = n; }
    Entry Get(U index) { return index < Entries ? s.entry[index] : Entry{}; }
    void Set(U index, const Entry &value) { if (index < Entries) s.entry[index] = value; }
    bool Append(const Entry &value)
    {
        Event(3, value.window, value.after, value.flags);
        if (s.count >= Entries) return false;
        s.entry[s.count++] = value;
        return true;
    }
};
extern "C" U owner_zorder_run(State *state, U index)
{
    Provider e{*state};
    Arrange(e, index);
    return state->count;
}
