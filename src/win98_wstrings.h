// SPDX-License-Identifier: GPL-2.0-or-later
// Win98 SE KERNEL32 Unicode string loops and their access-fault result.
#pragma once
namespace Win98WStrings
{
using U = unsigned;
// Providers: bool Read16(U address, U &value), bool Write16(U address, U value),
// Error(U). Checked WORD accesses adapt the original SEH boundary to native ARM.
template <class E> U Fault(E &e)
{
    e.Error(87); // F1/F2 wrapper selectors return zero.
    return 0;
}
// Public, inner.
template <class E> U Length(E &e, U source)
{
    U cursor = source, value;
    for (;;)
    {
        if (!e.Read16(cursor, value)) return Fault(e);
        if (!value) break;
        cursor += 2;
    }
    const U distance = cursor - source;
    return (distance >> 1) | (distance & 0x80000000u); // Original SAR EAX,1.
}
// Public, inner.
template <class E> U Copy(E &e, U destination, U source)
{
    U cursor = destination, value;
    do
    {
        if (!e.Read16(source, value)) return Fault(e);
        if (!e.Write16(cursor, value)) return Fault(e);
        cursor += 2;
        source += 2;
    } while (value);
    return destination;
}
// Public, inner.
template <class E> U Cat(E &e, U destination, U source)
{
    U cursor = destination, value;
    for (;;)
    {
        if (!e.Read16(cursor, value)) return Fault(e);
        if (!value) break;
        cursor += 2;
    }
    do
    {
        if (!e.Read16(source, value)) return Fault(e);
        if (!e.Write16(cursor, value)) return Fault(e);
        cursor += 2;
        source += 2;
    } while (value);
    return destination;
}
// lstrcpynW : CL=3 -> unsupported dispatcher. No pointer access.
template <class E> U UnsupportedCopyN(E &e, U, U, U)
{
    e.Error(120);
    return 0;
}
} // namespace Win98WStrings
