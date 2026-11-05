// SPDX-License-Identifier: GPL-2.0-or-later
// Windows 98 path parsing, normalization, and comparison behavior.
#include "win98_paths.h"
namespace Win98Paths
{
using U = unsigned;
using Memory = Win98Arrays::Memory;
namespace
{
U Byte(Memory &m, U p)
{
    unsigned char c = 0;
    m.Read(p, &c, 1);
    return c;
}
void Byte(Memory &m, U p, U value)
{
    unsigned char c = value;
    m.Write(p, &c, 1);
}
U Length(Memory &m, U p)
{
    U n = 0;
    while (n < 65536 && Byte(m, p + n))
        ++n;
    return n;
}
void Copy(Memory &m, U out, U in, U size)
{
    if (!size)
        return;
    U n = 0;
    while (n + 1 < size)
    {
        U c = Byte(m, in + n);
        Byte(m, out + n++, c);
        if (!c)
            return;
    }
    Byte(m, out + n, 0);
}
bool UNC(Memory &m, U p) { return p && Byte(m, p) == '\\' && Byte(m, p + 1) == '\\'; }
bool Relative(Memory &m, U p) { return !p || !Byte(m, p) || (Byte(m, p) != '\\' && Byte(m, p + 1) != ':'); }
bool Root(Memory &m, U p)
{
    if (!p || !Byte(m, p))
        return false;
    if (Byte(m, p + 1) == ':' && Byte(m, p + 2) == '\\' && !Byte(m, p + 3))
        return true;
    if (Byte(m, p) != '\\')
        return false;
    if (!Byte(m, p + 1))
        return true;
    if (Byte(m, p + 1) != '\\')
        return false;
    U separators = 0;
    for (U n = 2; n < 65536 && Byte(m, p + n); ++n)
        if (Byte(m, p + n) == '\\' && ++separators > 1)
            return false;
    return true;
}
U Remove(Memory &m, U p)
{
    if (!p)
        return 0;
    U cut = p;
    for (U n = 0; n < 65536 && Byte(m, p + n); ++n)
        if (Byte(m, p + n) == '\\')
            cut = p + n;
        else if (Byte(m, p + n) == ':')
        {
            if (Byte(m, p + n + 1) == '\\')
                ++n;
            cut = p + n + 1;
        }
    if (!Byte(m, cut))
        return 0;
    if ((cut == p && Byte(m, cut) == '\\') || (cut == p + 1 && UNC(m, p)))
    {
        if (!Byte(m, cut + 1))
            return 0;
        ++cut;
    }
    Byte(m, cut, 0);
    return 1;
}
U Strip(Memory &m, U p)
{
    if (!p)
        return 0;
    while (!Root(m, p))
        if (!Remove(m, p))
            return 0;
    return 1;
}
U Add(Memory &m, U p)
{
    if (!p)
        return 0;
    U n = Length(m, p);
    if (n >= 259)
        return 0;
    if (n && Byte(m, p + n - 1) != '\\')
    {
        Byte(m, p + n++, '\\');
        Byte(m, p + n, 0);
    }
    return p + n;
}
U Canonicalize(Memory &m, U out, U in, U &error)
{
    if (!out || !in)
    {
        error = 87;
        return 0;
    }
    bool unc = UNC(m, in);
    U end = out;
    for (U steps = 0; steps < 65536 && Byte(m, in); ++steps)
    {
        U n = 0;
        while (n < 65536 && Byte(m, in + n) && Byte(m, in + n) != '\\')
            ++n;
        if (!n && Byte(m, in) == '\\')
        {
            Byte(m, end++, '\\');
            ++in;
        }
        else if (n == 1 && Byte(m, in) == '.')
        {
            if (!Byte(m, in + 1))
            {
                --end;
                ++in;
            }
            else
                in += 2;
        }
        else if (n == 2 && Byte(m, in) == '.' && Byte(m, in + 1) == '.')
        {
            if (!Root(m, out))
            {
                U previous = out;
                for (U p = out; p < end - 1 && p - out < 65536; ++p)
                    if (Byte(m, p) == '\\')
                        previous = p;
                end = previous;
            }
            else if (Byte(m, in + 2) == '\\')
                ++in;
            in += 2;
        }
        else
        {
            Copy(m, end, in, n + 1);
            end += n;
            in += n;
        }
        Byte(m, end, 0);
    }
    // Original leaves an empty input's existing output alone before this repair.
    if (!Byte(m, out))
    {
        Byte(m, out, '\\');
        Byte(m, out + 1, 0);
    }
    if (Byte(m, out + 1) == ':' && !Byte(m, out + 2))
    {
        Byte(m, out + 2, '\\');
        Byte(m, out + 3, 0);
    }
    if (unc && Byte(m, out) == '\\' && !Byte(m, out + 1))
    {
        Byte(m, out + 1, '\\');
        Byte(m, out + 2, 0);
    }
    return 1;
}
// Original PathCombine uses a 260-byte stack buffer. Keep it outside guest RAM.
struct Temporary final : Memory
{
    static constexpr U Base = 0xfffff000;
    Memory &parent;
    unsigned char bytes[260] = {};
    explicit Temporary(Memory &m) : parent(m) {}
    bool Read(U p, void *d, U n) override
    {
        if (p < Base)
            return parent.Read(p, d, n);
        if (p - Base > sizeof bytes || n > sizeof bytes - (p - Base))
            return false;
        for (U i = 0; i < n; ++i)
            ((unsigned char *)d)[i] = bytes[p - Base + i];
        return true;
    }
    bool Write(U p, const void *d, U n) override
    {
        if (p < Base)
            return parent.Write(p, d, n);
        if (p - Base > sizeof bytes || n > sizeof bytes - (p - Base))
            return false;
        for (U i = 0; i < n; ++i)
            bytes[p - Base + i] = ((const unsigned char *)d)[i];
        return true;
    }
    U Resize(U, U, U) override { return 0; }
    bool Free(U, U) override { return false; }
    int Compare(U, U, U, U) override { return 0; }
};
U Combine(Memory &parent, U out, U dir, U file, U &error)
{
    if (!out || (!dir && !file))
        return out;
    // Reject the original null-source lstrcpyn fault path deterministically.
    if (!dir && file && !Byte(parent, file))
    {
        error = 87;
        return 0;
    }
    Temporary m(parent);
    U buf = Temporary::Base;
    if (!file || !Byte(m, file) || !dir)
        Copy(m, buf, (!file || !Byte(m, file)) ? dir : file, 260);
    else if (!Byte(m, dir) || !Relative(m, file))
    {
        if (!Byte(m, dir) || Byte(m, file) != '\\' || UNC(m, file))
            Copy(m, buf, file, 260);
        else
        {
            Copy(m, buf, dir, 260);
            Strip(m, buf);
            U end = Add(m, buf);
            if (!end)
                return 0;
            Copy(m, end, file + 1, buf + 259 - end);
        }
    }
    else
    {
        Copy(m, buf, dir, 260);
        U end = Add(m, buf);
        if (!end)
            return 0;
        U n = Length(m, file);
        if (Length(m, buf) + n >= 260)
            return 0;
        Copy(m, end, file, n + 1);
    }
    Canonicalize(m, out, buf, error);
    return out;
}
} // namespace
U Call(Memory &m, U ordinal, const U *a, U &error)
{
    U p = a[0];
    switch (ordinal)
    {
    case 451:
        return Add(m, p);
    case 459:
        return Canonicalize(m, p, a[1], error);
    case 461:
        return Combine(m, p, a[1], a[2], error);
    case 473:
    case 475:
    {
        if (!p)
            return 0;
        U found = ordinal == 475 ? p : 0;
        for (U n = 0; n < 65536; ++n, ++p)
        {
            U c = Byte(m, p);
            if (!c)
                break;
            if (ordinal == 473)
            {
                if (c == '.')
                    found = p;
                else if (c == ' ' || c == '\\')
                    found = 0;
            }
            else if (c == '\\' || c == ':' || c == '/')
            {
                U next = Byte(m, p + 1);
                if (next && next != '\\' && next != '/')
                    found = p + 1;
            }
        }
        return found ? found : p;
    }
    case 503:
        return Relative(m, p);
    case 505:
        return Root(m, p);
    case 511:
        return UNC(m, p);
    case 539:
        return Remove(m, p);
    case 547:
        if (!p)
            return 0;
        if (UNC(m, p))
        {
            U separators = 0;
            for (U n = 2; n < 65536 && Byte(m, p + n); ++n)
                if (Byte(m, p + n) == '\\' && ++separators == 2)
                    return p + n + 1;
        }
        else if (Byte(m, p + 1) == ':' && Byte(m, p + 2) == '\\')
            return p + 3;
        return 0;
    case 551:
        return Strip(m, p);
    default:
        return 0;
    }
}
namespace
{
//  specialized to the original GetFileTitle call's flags (1, 0, 0).
int TitleOffset(Memory &m, U start)
{
    U p = start;
    if (!Byte(m, p))
        return -10;
    if (Byte(m, p + 1) == ':')
    {
        U drive = Byte(m, p) | 32;
        if (drive < 'a' || drive > 'z')
            return -2;
        p += 2;
    }
    int separators = 0;
    bool unc = false;
    if (Byte(m, p) == '\\' || Byte(m, p) == '/')
    {
        U old = p++;
        if (Byte(m, p) == '.')
        {
            ++p;
            if (Byte(m, p) == '\\' || Byte(m, p) == '/')
                ++p;
            else if (!Byte(m, p))
                return -1;
            else
                --p;
        }
        else if (Byte(m, p) == '\\' && Byte(m, old) == '\\')
        {
            ++p;
            separators = -1;
            unc = true;
        }
        else if (Byte(m, p) == '/')
            return -6;
    }
    else if (Byte(m, p) == '.')
    {
        ++p;
        if (Byte(m, p) == '.')
            ++p;
        if (!Byte(m, p))
            return -1;
        if (Byte(m, p) != '\\' && Byte(m, p) != '/')
            return 0;
        ++p;
    }
    if (!Byte(m, p))
        return -1;
    U stem = 0, offset = 0;
    bool extension = false, wildcard = false;
    for (U steps = 0; steps < 65536 && Byte(m, p); ++steps)
    {
        U c = Byte(m, p);
        if (c < 32 || c == '"' || c == ':' || c == '<' || c == '|')
            return -5;
        if (c == '\\' || c == '/')
        {
            ++separators;
            if (wildcard)
                return -11;
            if (!stem)
                return -6;
            ++p;
            if (!separators && !Byte(m, p))
                return -13;
            stem = 0;
            extension = false;
        }
        else if (c == '.')
        {
            if (!stem)
            {
                offset = p - start;
                ++p;
                if (Byte(m, p) == '.')
                    ++p;
                if (!Byte(m, p))
                    return -1;
                ++p;
                stem = 1;
            }
            else
            {
                ++p;
                extension = true;
            }
        }
        else
        {
            if (c == '*' || c == '?')
                wildcard = true;
            if (!extension && ++stem == 1)
                offset = p - start;
            ++p;
        }
    }
    if (separators == -1)
        return -13;
    if ((unc && !separators) || !stem)
        return -1;
    return (short)offset;
}
} // namespace
U FileTitle(Memory &m, const U *a, U &dialog_error)
{
    U copy = 0, length = a[0] ? Length(m, a[0]) : 0;
    if (a[0])
    {
        copy = m.Resize(1, 0, length + 1);
        if (!copy)
        {
            dialog_error = 9;
            return 0; // Original reports allocation failure through CommDlgExtendedError.
        }
        Copy(m, copy, a[0], length + 1);
    }
    int result = !copy || !Byte(m, copy) ? -10 : TitleOffset(m, copy);
    if (result >= 0)
    {
        U title = copy + result, n = Length(m, title);
        result = (short)(n + 1);
        if (result <= (int)(a[2] & 65535))
        {
            result = 0;
            for (U i = 0; i < n; ++i)
                if (Byte(m, title + i) == '*' || Byte(m, title + i) == '?')
                    result = -12;
            if (!result)
            {
                // Null output with a sufficient capacity faults in the original.
                if (!a[1])
                    result = -10;
                else
                {
                    Copy(m, a[1], title, n + 1);
                    U end = a[1] + n - 1;
                    while (Byte(m, end) == ' ')
                        Byte(m, end--, 0);
                }
            }
        }
    }
    if (copy)
        m.Free(1, copy);
    return (U)(int)(short)result;
}
} // namespace Win98Paths
