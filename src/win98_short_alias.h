// SPDX-License-Identifier: GPL-2.0-or-later
// The 8.3 alias a VFAT directory gives a long name.
//
// KERNEL32's short-path
// resolver only reads aliases; it never makes one. The alias itself is made by
// the file system, and the file system is the part we now write ourselves.
// This header holds that rule, so the RAM volume and every volume provider
// produce the same names.
// Aliases are deterministic and avoid existing names in the directory.
// tools/test_short_alias.py runs it over the 6,496 files and 691 directories of
// the imported Win98 installation and compares with the aliases the FAT
// directories actually carry.
#pragma once
namespace Win98ShortAlias
{
using U = unsigned;
inline U Fold(U c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
// Two names are the same file when they match without regard to case. VFAT
// compares long names in Unicode, so the letters of the ANSI code page above
// 127 match too: a directory written as "Zubeh\xf6r" is found as "ZUBEH\xd6R".
inline U NameFold(U c)
{
    if (c >= 0xe0 && c <= 0xfe && c != 0xf7) return c - 32;
    if (c == 0x9a || c == 0x9c || c == 0x9e) return c - 16;
    return c == 0xff ? 0x9f : Fold(c);
}
// Characters a FAT short name cannot hold. Everything else, including bytes
// above 127, is taken as it is; this port has no OEM conversion table.
inline bool Invalid(U c)
{
    if (c < 32 || c == 127)
        return true;
    const char *set = "+,;=[]\"*/:<>?\\|";
    for (const char *p = set; *p; ++p)
        if (c == (unsigned char)*p)
            return true;
    return false;
}
// True when the name already is a short name. Such an entry gets no alias; the
// directory stores it with the case flags VFAT keeps for exactly this case.
inline bool Short(const char *name)
{
    U base = 0, extension = 0, dots = 0;
    bool after = false;
    for (const char *p = name; *p; ++p)
    {
        const U c = (unsigned char)*p;
        if (c == '.')
        {
            if (++dots > 1)
                return false;
            after = true;
            continue;
        }
        if (c == ' ' || Invalid(c))
            return false;
        if (after)
            ++extension;
        else
            ++base;
    }
    return base && base <= 8 && (!dots || (extension && extension <= 3));
}
// The name without its leading periods, without spaces and embedded periods,
// folded to upper case, split into the part before and after the last period.
inline void Basis(const char *name, char (&base)[9], char (&extension)[4])
{
    while (*name == '.')
        ++name;
    const char *last = nullptr;
    for (const char *p = name; *p; ++p)
        if (*p == '.')
            last = p;
    U n = 0;
    for (const char *p = name; *p && (!last || p < last); ++p)
    {
        const U c = NameFold((unsigned char)*p);
        if (c == ' ' || c == '.')
            continue;
        if (n < 8)
            base[n++] = char(Invalid(c) ? '_' : c);
    }
    base[n] = 0;
    n = 0;
    if (last)
        for (const char *p = last + 1; *p && n < 3; ++p)
        {
            const U c = NameFold((unsigned char)*p);
            if (c == ' ' || c == '.')
                continue;
            extension[n++] = char(Invalid(c) ? '_' : c);
        }
    extension[n] = 0;
    // An empty base cannot be stored; one filler character stands in.
    if (!base[0])
    {
        base[0] = '_';
        base[1] = 0;
    }
}
// Builds candidate number `tail` (1, 2, 3,...). The numeric tail always fits:
// the base gives way to it, so a two-digit tail leaves five base characters.
inline void Candidate(const char *name, U tail, char (&out)[13])
{
    char base[9] = {}, extension[4] = {}, digits[8] = {};
    Basis(name, base, extension);
    U count = 0;
    for (U value = tail; value; value /= 10)
        digits[count++] = char('0' + value % 10);
    if (!count)
        digits[count++] = '0';
    U end = 0;
    const U room = count + 1 < 8 ? 8 - (count + 1) : 1;
    while (base[end] && end < room)
    {
        out[end] = base[end];
        ++end;
    }
    out[end++] = '~';
    while (count)
        out[end++] = digits[--count];
    if (extension[0])
    {
        out[end++] = '.';
        for (U i = 0; extension[i]; ++i)
            out[end++] = extension[i];
    }
    out[end] = 0;
}
// The environment answers whether a candidate is already taken in the target
// directory, comparing case insensitively against long names and aliases.
template <class E> inline bool Generate(E &e, const char *name, char (&out)[13])
{
    if (Short(name))
    {
        U n = 0;
        for (const char *p = name; *p && n < 12; ++p)
            out[n++] = char(NameFold((unsigned char)*p));
        out[n] = 0;
        // A name that already is a short name needs no separate alias.
        return false;
    }
    for (U tail = 1; tail < 1000000; ++tail)
    {
        Candidate(name, tail, out);
        if (!e.Taken(out))
            return true;
    }
    out[0] = 0;
    return false;
}
} // namespace Win98ShortAlias
