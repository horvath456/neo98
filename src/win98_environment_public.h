// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_environment.h"
namespace Win98Environment
{
// KERNEL32 public argument probes. ProbeRead/ProbeWrite stand in for a faultable
// byte read / ADD byte,0; the original common SEH handler returns zero/error87.
template<class E> bool ProbeText(E &e, U p)
{
    U count = ~0u, value;
    do { if (!e.ProbeRead(p++, value)) return false; } while (value && --count);
    return true;
}
template<class E> U GetPublic(E &e, U name, U output, U capacity)
{
    bool valid = ProbeText(e, name);
    if (valid && capacity)
    {
        U p = output + capacity - 1;
        do { if (!e.ProbeWrite(p)) { valid = false; break; } p -= 4096; } while (p > output);
        if (valid) valid = e.ProbeWrite(output);
    }
    if (!valid) { e.Error(87); return 0; }
    return Get(e, name, output, capacity);
}
template<class E> U SetPublic(E &e, U name, U value)
{
    U ignored;
    if ((name && !e.ProbeRead(name, ignored)) || (value && !e.ProbeRead(value, ignored)))
    { e.Error(87); return 0; }
    return Set(e, name, value);
}
template<class E> U ExpandPublic(E &e, U source, U output, U capacity)
{
    if (!ProbeText(e, source) || (output && !e.ProbeWrite(output))) { e.Error(87); return 0; }
    return Expand(e, source, output, capacity);
}
} // namespace Win98Environment
