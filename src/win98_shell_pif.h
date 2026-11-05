// SPDX-License-Identifier: GPL-2.0-or-later
// SHELL PIF-property wrappers load PIFMGR on demand and resolve
// OpenProperties, GetProperties, SetProperties, and CloseProperties. Forward
// argument blocks unchanged. Count successful opens; closing the last handle
// permits unloading. A null CloseProperties handle requests unloading. Failed
// loads return zero. Paths that skip the close call preserve the
// caller-provided result state. SetProperties is available to Win16 callers
// only.
#pragma once

namespace Win98ShellPif
{
// The PIFMGR ordinals the loader resolves, in the order it resolves them.
enum Ordinal
{
    OpenProperties = 2,
    GetProperties = 3,
    SetProperties = 4,
    CloseProperties = 9,
};

// KERNEL.95 reports a load failure as a code of 0x20 or less.
constexpr unsigned MaximumErrorCode = 0x20;
constexpr char ModuleName[] = "PIFMGR.DLL";

// Argument bytes each forwarder copies through, from its own `retf`.
constexpr unsigned OpenBytes = 0x0c;
constexpr unsigned GetBytes = 0x0e;
constexpr unsigned SetBytes = 0x0e;
constexpr unsigned CloseBytes = 0x04;

// The six shared words of the SHELL data segment.
struct State
{
    unsigned instance = 0; //
    unsigned handles = 0;  //
    unsigned open = 0;     //
    unsigned get = 0;      //
    unsigned set = 0;      //
    unsigned close = 0;    //
};

struct Environment
{
    virtual ~Environment() = default;
    // LoadLibrary takes a far pointer to the constant string,
    // so pass the name directly.
    virtual unsigned LoadModule(const char *name) = 0;
    // KERNEL.50 with 0000:ordinal in place of a name pointer. Returns the far
    // pointer the original stores in one of the four slots.
    virtual unsigned Procedure(unsigned instance, unsigned ordinal) = 0;
    // KERNEL.96.
    virtual void FreeModule(unsigned instance) = 0;
    // The far call through one slot. "bytes" is the size of the argument block
    // the forwarder copied through; the callee removes it itself.
    virtual unsigned CallProcedure(unsigned procedure, unsigned bytes) = 0;
    // The uninitialised stack word index 2 returns when it makes no far call.
    virtual unsigned StaleResult() { return 0; }
};

// Returns 1 when the four slots have been filled.
inline unsigned Load(State &s, Environment &e)
{
    if (s.instance)
        return 1;
    s.instance = e.LoadModule(ModuleName) & 0xffffu;
    if (s.instance <= MaximumErrorCode)
    {
        s.instance = 0;
        return 0;
    }
    s.open = e.Procedure(s.instance, OpenProperties);
    s.close = e.Procedure(s.instance, CloseProperties);
    s.get = e.Procedure(s.instance, GetProperties);
    s.set = e.Procedure(s.instance, SetProperties);
    return 1;
}


inline void Unload(State &s, Environment &e)
{
    if (s.handles)
        return;
    const unsigned instance = s.instance;
    if (!instance)
        return;
    s.instance = 0;
    e.FreeModule(instance);
    s.open = s.close = s.get = s.set = 0;
}

// index 5.
inline unsigned Open(State &s, Environment &e)
{
    if (!Load(s, e))
        return 0;
    s.handles = (s.handles + 1) & 0xffffu;
    const unsigned result = e.CallProcedure(s.open, OpenBytes) & 0xffffu;
    if (!result)
        s.handles = (s.handles - 1) & 0xffffu;
    return result;
}

// index 4.
inline unsigned Get(State &s, Environment &e)
{
    if (!Load(s, e))
        return 0;
    return e.CallProcedure(s.get, GetBytes) & 0xffffu;
}

// index 3.
inline unsigned Set(State &s, Environment &e)
{
    if (!Load(s, e))
        return 0;
    return e.CallProcedure(s.set, SetBytes) & 0xffffu;
}

// index 2. A null handle asks for the unload instead of a close.
inline unsigned Close(State &s, Environment &e, unsigned handle)
{
    if (!(handle & 0xffffu))
    {
        Unload(s, e);
        return e.StaleResult() & 0xffffu;
    }
    if (!s.handles)
        return e.StaleResult() & 0xffffu;
    const unsigned result = e.CallProcedure(s.close, CloseBytes) & 0xffffu;
    s.handles = (s.handles - 1) & 0xffffu;
    return result;
}
} // namespace Win98ShellPif
