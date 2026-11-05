// SPDX-License-Identifier: GPL-2.0-or-later
// String routines that behave like those of COMCTL32 on Windows 98.
#include "win98_strings.h"
namespace Win98Strings
{
using U = unsigned;
static U Integer(Win98Arrays::Memory &m, U source)
{
    unsigned short c = 0;
    if (!m.Read(source, &c, 2))
        return 0;
    bool negative = c == '-';
    if (negative)
        source += 2;
    U value = 0;
    for (U i = 0; i < 0x100000; ++i, source += 2)
    {
        if (!m.Read(source, &c, 2))
            return 0;
        if (c < '0' || c > '9')
            return negative ? 0u - value : value;
        value = value * 10 + c - '0'; // Original x86 arithmetic wraps at 32 bits.
    }
    return 0;
}
U Helper(Environment &m, U ordinal, const U *a)
{
    if (ordinal == 71)
        return m.Resize(1, 0, a[0]);
    if (ordinal == 72)
        return m.Resize(1, a[0], a[1]);
    if (ordinal == 73)
        return m.Free(1, a[0]);
    if (ordinal == 365)
        return Integer(m, a[0]);
    if (ordinal == 350)
    {
        U source = a[0];
        for (U i = 0; i < 0x100000; ++i)
        {
            unsigned char first = 0;
            if (!m.Read(source, &first, 1) || !first)
                return 0; // Searching for NUL does not return the terminator.
            unsigned short pair = 0;
            if (!m.Read(source, &pair, 2))
                return 0;
            if (first == (a[1] & 255) && (!m.LeadByte(first) || pair == (a[1] & 65535)))
                return source;
            source = m.Next(source);
        }
        return 0;
    }
    if (ordinal == 357)
    {
        U source = a[0];
        if (!source)
            return 0;
        if (source == 0xffffffff)
            return Integer(m, source); // The original sentinel reaches the parser unchanged.
        if (!m.Readable(source))
            return 0;
        U count = m.ToWide(0, source, 0, 0);
        if (!count)
            count = 1;
        U wide = m.Local(count * 2);
        if (!wide)
            return 0;
        U value = m.ToWide(1, source, wide, count) ? Integer(m, wide) : 0;
        m.ReleaseLocal(wide);
        return value;
    }
    return 0;
}
U Call(Win98Arrays::Memory &m, U ordinal, const U *a)
{
    if (ordinal < 233 || ordinal > 236)
        return 0;
    U unit = ordinal >= 235 ? 2 : 1, source = (ordinal & 1) ? a[0] : a[1], length = 0;
    if (source)
        for (;; ++length)
        {
            U character = 0;
            if (length >= 0x100000 || !m.Read(source + length * unit, &character, unit))
                return 0;
            if (!character)
                break;
        }
    if (ordinal & 1)
    { // GetPtr returns the number of units INCLUDING the terminator.
        if (!source)
        {
            U zero = 0;
            if ((int)a[2] > 0)
                m.Write(a[1], &zero, unit);
            return 0;
        }
        if (a[1] && a[2])
        {
            if ((int)a[2] < 0)
                return 0;
            if (a[2] <= length)
                length = a[2] - 1;
            m.Move(a[1], source, length * unit);
            U zero = 0;
            m.Write(a[1] + length * unit, &zero, unit);
        }
        return length + 1;
    }
    U copy = 0;
    if (source)
    {
        copy = m.Resize(1, 0, (length + 1) * unit);
        if (!copy)
            return 0;
        m.Move(copy, source, (length + 1) * unit);
    }
    // The native adapter serializes this exchange; there is no guest callback here.
    U old = m.Word(a[0]);
    m.Word(a[0], copy);
    if (old)
        m.Free(1, old);
    return 1;
}
} // namespace Win98Strings
