// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Short path names as Windows 98's KERNEL32.DLL resolves them.
namespace Win98ShortPath
{
using U = unsigned;
struct Names
{
    unsigned char long_name[260];
    unsigned char short_name[14];
};
inline bool Separator(U byte) { return byte == '\\' || byte == '/'; }

// Output must initially contain 260 zero bytes,
// as supplied by the original caller's HeapAlloc(HEAP_ZERO_MEMORY).
// Memory providers must check allocations: the original final terminator and
// UNC-prefix loop do not enforce the allocation bound themselves.
template <class E> U Resolve(E &e, U input, U output, bool long_name = false)
{
    U cursor = input, write = output, replacement = output, ordinary = 0;
    U byte = e.Read8(cursor);
    if (byte && e.Read8(cursor + 1) == ':')
    {
        cursor += 2;
        write = replacement = output + 2;
        e.Write8(output, byte);
        e.Write8(output + 1, ':');
    }
    if (Separator(e.Read8(cursor)) && Separator(e.Read8(cursor + 1)))
    {
        cursor += 2;
        write = replacement = output + 2;
        e.Write8(output, '\\');
        e.Write8(output + 1, '\\');
        while ((byte = e.Read8(cursor)))
        {
            if (Separator(byte))
            {
                if (ordinary)
                    break;
                ++cursor;
                ++ordinary;
                e.Write8(write++, '\\');
            }
            else
            {
                ++cursor;
                e.Write8(write++, byte);
            }
            ++replacement;
        }
        if (e.Attributes(output) == ~0u)
            return 67;
        ordinary = 0;
    }
    else if (e.Read8(output) && e.Read8(output + 1) == ':' && !e.Read8(output + 2))
    {
        e.Write8(output + 2, '\\');
        if (e.Attributes(output) == ~0u)
            return e.LastError();
        e.Write8(output + 2, 0);
    }
    e.Error(161);
    for (;;)
    {
        byte = e.Read8(cursor);
        if (byte && !Separator(byte))
        {
            if (byte != '.')
                ++ordinary;
            if (static_cast<int>(write - output) >= 260)
                return 161;
            ++cursor;
            e.Write8(write++, byte);
            continue;
        }
        if (byte)
        {
            do
            {
                byte = e.Read8(++cursor);
            } while (byte && Separator(byte));
            byte = e.Read8(--cursor);
        }
        if (ordinary)
        {
            e.Write8(write, 0);
            Names names;
            U handle = e.FindFirst(output, names);
            if (handle == ~0u)
                return e.LastError();
            e.FindClose(handle);
            const unsigned char *name = !long_name && names.short_name[0]
                                            ? names.short_name : names.long_name;
            if (*name)
            {
                do
                {
                    if (static_cast<int>(replacement - output) >= 260)
                        return 161;
                    e.Write8(replacement++, *name++);
                } while (*name);
                write = replacement;
            }
        }
        if (!byte)
        {
            e.Write8(write, 0);
            return 0;
        }
        ++cursor;
        if (static_cast<int>(write - output) >= 260)
            return 161;
        ordinary = 0;
        e.Write8(write++, '\\');
        replacement = write;
    }
}

// a NUL must occur in the first 260 input bytes.
template <class E> bool TooLong(E &e, U input)
{
    for (U n = 0; n != 260; ++n)
        if (!e.Read8(input + n))
            return false;
    e.Error(206);
    return true;
}

// (short) / (long).
// Public x86 SEH probes / remain the guest-memory adapter's
// responsibility. FileApiMode and its conversion/cleanup are explicit providers.
template <class E> U Get(E &e, U input, U output, U capacity, bool long_name = false)
{
    U result = 0, path = input;
    bool converted = e.FileApiMode(1);
    if (converted)
        path = e.ConvertInput(input, 0);
    if (!TooLong(e, path))
    {
        U temporary = e.AllocateZero(260);
        if (temporary)
        {
            U error = Resolve(e, path, temporary, long_name);
            if (!error)
            {
                while (e.Read8(temporary + result))
                    ++result;
                if (output && result < capacity)
                {
                    // Original Windows 98 copies forward in DWORD groups then bytes.
                    U n = result + 1, offset = 0;
                    for (; n >= 4; n -= 4, offset += 4)
                    {
                        U value = e.Read8(temporary + offset) |
                                  (e.Read8(temporary + offset + 1) << 8) |
                                  (e.Read8(temporary + offset + 2) << 16) |
                                  (e.Read8(temporary + offset + 3) << 24);
                        for (U j = 0; j != 4; ++j)
                            e.Write8(output + offset + j, (value >> (j * 8)) & 255);
                    }
                    for (; n; --n, ++offset)
                        e.Write8(output + offset, e.Read8(temporary + offset));
                }
                else
                    ++result;
                error = e.LastError();
            }
            e.Free(temporary);
            e.Error(error);
        }
    }
    if (converted)
    {
        e.FreeInput(path, 0);
        e.ClearFileApiState(6);
    }
    return result;
}
} // namespace Win98ShortPath
