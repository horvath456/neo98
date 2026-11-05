// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98Crt
{
// MSVCRT _EH_prolog installs an exception registration frame. Dispatch and
// unwinding are separate.
struct Prolog
{
    unsigned frame[4], sp, bp, ax, ip, handler;
};
Prolog ExceptionProlog(unsigned sp, unsigned return_address, unsigned eax, unsigned ebp, unsigned previous);
// MSVCRT _controlfp/_control87 and their x87/CRT bit conversions.
unsigned DecodeControl(unsigned word);
unsigned EncodeControl(unsigned control);
struct ControlResult
{
    unsigned value, word;
};
ControlResult Control(unsigned word, unsigned requested, unsigned mask, bool control87);
// __dllonexit grows the callback table and reports allocation failure.
// The owning DLL executes this table during detach; this routine only registers entries.
template <class Environment>
unsigned DllOnExit(Environment &e, unsigned function, unsigned start, unsigned end)
{
    e.Lock();
    const unsigned size = e.Size(e.Read(start));
    const unsigned base = e.Read(start), used = e.Read(end) - base;
    if (size <= used)
    {
        const unsigned grown = e.Resize(base, size + 16);
        if (!grown)
        {
            e.Unlock();
            return 0;
        }
        // SAR 2 followed by a scaled LEA retains the original DWORD rounding.
        const unsigned offset = (e.Read(end) - e.Read(start)) & ~3u;
        e.Write(end, grown + offset);
        e.Write(start, grown);
    }
    e.Write(e.Read(end), function);
    e.Write(end, e.Read(end) + 4);
    e.Unlock();
    return function;
}
// Process _onexit uses a growable callback table.
// Unlike __dllonexit, this checks that four more bytes fit, and queries size again before growing.
template <class Environment> unsigned OnExit(Environment &e, unsigned function, unsigned start, unsigned end)
{
    e.Lock();
    const unsigned size = e.Size(e.Read(start));
    unsigned slot = e.Read(end);
    if (size < slot - e.Read(start) + 4)
    {
        const unsigned grown = e.Resize(e.Read(start), e.Size(e.Read(start)) + 16);
        if (!grown)
        {
            e.Unlock();
            return 0;
        }
        slot = grown + ((e.Read(end) - e.Read(start)) & ~3u);
        e.Write(start, grown);
        e.Write(end, slot);
    }
    e.Write(slot, function);
    e.Write(end, e.Read(end) + 4);
    e.Unlock();
    return function;
}
// Traverse registered callbacks; CRT stream and terminator tables are separate.
template <class Environment> void Cleanup(Environment &e, unsigned start, unsigned end)
{
    const unsigned begin = e.Read(start);
    if (!begin)
        return;
    unsigned cursor = e.Read(end) - 4;
    if (cursor < begin)
        return;
    do
    {
        const unsigned function = e.Read(cursor);
        if (function && !e.Invoke(function))
            return;
        cursor -= 4;
    } while (cursor >= e.Read(start));
}
} // namespace Win98Crt
