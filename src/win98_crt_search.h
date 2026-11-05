// SPDX-License-Identifier: GPL-2.0-or-later
// Original Win98 MSVCRT string search.
#pragma once
namespace Win98CrtSearch
{
using U = unsigned;
static_assert(sizeof(U) == 4, "Original DWORD arithmetic");
// Providers: Read8/Read32(address), Multibyte, LeadByte(byte), Lock/Unlock(id).
// Read32 preserves the original aligned reads, including up to three bytes past
// a terminator within its aligned word. Providers enforce guest accessibility.
// Multibyte represents CRT global7803A9BC, LeadByte table7803A9C1[byte]&4.

// strchr scans for a byte or the string terminator.
template<class E> U Find(E &e, U string, U character)
{
    const U byte = character & 255;
    U p = string;
    while (p & 3)
    {
        const U value = e.Read8(p++);
        if (value == byte) return p - 1;
        if (!value) return 0;
    }
    const U pattern = byte * 0x01010101u;
    for (;;)
    {
        const U word = e.Read32(p), sum = word + 0x7efefeffu;
        const U different = word ^ pattern;
        const U possible_match = (~different ^ (different + 0x7efefeffu)) & 0x81010100u;
        const U possible_zero = (~word ^ sum) & 0x81010100u;
        p += 4;
        if (possible_match)
        {
            // The original deliberately reloads the same DWORD for byte tests.
            const U check = e.Read32(p - 4);
            for (U i = 0; i < 4; ++i)
            {
                const U value = (check >> (i * 8)) & 255;
                if (value == byte) return p - 4 + i;
                if (!value) return 0;
            }
        }
        else if (possible_zero && ((possible_zero & 0x01010100u) || !(sum & 0x80000000u)))
            return 0;
    }
}
// strrchr scans forward to the terminator, then backward by byte.
template<class E> U FindLast(E &e, U string, U character)
{
    U p = string;
    while (e.Read8(p++)) {}
    U count = p - string;
    --p;
    const U byte = character & 255;
    while (count)
    {
        const U value = e.Read8(p--);
        --count;
        if (value == byte) break;
    }
    ++p;
    return e.Read8(p) == byte ? p : 0; // Original repeats the final byte check.
}
// _mbschr uses strchr for SBCS input.
template<class E> U MultiFind(E &e, U string, U character)
{
    if (!e.Multibyte()) return Find(e, string, character);
    e.Lock(25);
    U p = string;
    for (;;)
    {
        const U first = e.Read8(p);
        if (!first) { e.Unlock(25); return character == 0 ? p : 0; }
        if (e.LeadByte(first))
        {
            const U second = e.Read8(p + 1);
            ++p;
            if (!second) { e.Unlock(25); return 0; }
            if (character == ((first << 8) | second)) { e.Unlock(25); return p - 1; }
        }
        else if (character == first) { e.Unlock(25); return p; }
        ++p;
    }
}
// The malformed final lead-byte behavior of _mbsrchr below is
// present in the original: with no prior match it returns the trailing NUL.
template<class E> U MultiFindLast(E &e, U string, U character)
{
    if (!e.Multibyte()) return FindLast(e, string, character);
    e.Lock(25);
    U p = string, found = 0;
    for (;;)
    {
        const U first = e.Read8(p);
        U last = first;
        if (e.LeadByte(first))
        {
            last = e.Read8(p + 1);
            ++p;
            if (!last) { if (!found) found = p; }
            else if (character == ((first << 8) | last)) found = p - 1;
        }
        else if (character == first) found = p;
        ++p;
        if (!last) break;
    }
    e.Unlock(25);
    return found;
}
} // namespace Win98CrtSearch
