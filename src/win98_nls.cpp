// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_nls.h"
#include "win98_nls_composite.h"
#include "win98_nls_decompose.h"
namespace Win98Nls
{
U Table::Half(U offset) const
{
    return data && offset <= size && size - offset >= 2 ? data[offset] | U(data[offset + 1]) << 8 : 0;
}
U Table::Offset(U index) const { return (Half(index * 4) | Half(index * 4 + 2) << 16) * 2; }
U Table::Wide(U byte, bool glyphs) const
{
    U table = glyphs && Offset(3) ? Offset(3) : Offset(2);
    return Half(table + byte * 2);
}
U Table::Narrow(U character) const
{
    U table = Offset(5), first = Half(table + (character >> 8) * 2);
    U second = Half(table + (first + ((character >> 4) & 15)) * 2);
    return Half(table + (second + (character & 15)) * 2) & 255;
}
bool Table::Valid() const
{
    if (!data || size < 512 || Half(0) || Half(2))
        return false;
    for (U i = 1; i <= 5; ++i)
        if ((i == 1 || i == 2 || i == 5) && (!Offset(i) || Offset(i) >= size))
            return false;
    return Offset(1) <= size - 22 && Offset(2) <= size - 512 && Half(Offset(1)) == 1;
}
namespace
{
constexpr U Limit = 1048576;
U Error(Environment &e, U code)
{
    e.Error(code);
    return 0;
}
U Half(Environment &e, U p)
{
    unsigned short value = 0;
    e.Read(p, &value, 2);
    return value;
}
U Byte(Environment &e, U p)
{
    unsigned char value = 0;
    e.Read(p, &value, 1);
    return value;
}
void PutHalf(Environment &e, U p, U value)
{
    unsigned short word = value;
    e.Write(p, &word, 2);
}
void PutByte(Environment &e, U p, U value)
{
    unsigned char byte = value;
    e.Write(p, &byte, 1);
}
const Table *Lookup(Environment &e, U page)
{
    if (!page)
        page = e.Ansi();
    else if (page == 1)
        page = e.Oem();
    return e.CodePage(page);
}
U Length(Environment &e, U p, U count, bool wide)
{
    if (!(count & 0x80000000u))
        return count <= Limit ? count : 0;
    for (U i = 0; i < Limit; ++i)
        if (!(wide ? Half(e, p + i * 2) : Byte(e, p + i)))
            return i + 1;
    return 0;
}
U Info(Environment &e, U page, U out)
{
    if (!out)
        return Error(e, 87);
    if (page >= 60000)
    {
        if (page != 65000 && page != 65001)
            return Error(e, 87);
        e.Word(out, page == 65000 ? 5 : 3);
        PutHalf(e, out + 4, '?');
        for (U i = 6; i < 18; ++i)
            PutByte(e, out + i, 0);
        return 1;
    }
    const auto *table = Lookup(e, page);
    if (!table)
        return Error(e, 87);
    U info = table->Offset(1), fallback = table->Half(info + 2);
    e.Word(out, table->Half(info));
    PutByte(e, out + 4, fallback >> 8 ? fallback >> 8 : fallback);
    PutByte(e, out + 5, fallback >> 8 ? fallback : 0);
    for (U i = 0; i < 12; ++i)
        PutByte(e, out + 6 + i, table->data[info + 10 + i]);
    return 1; // CPINFO's two padding bytes remain untouched, as in Win98.
}
U Utf8Decode(Environment &e, U input, U count, U output, U capacity)
{
    U produced = 0, pending = 0, consumed = 0;
    // Preserve the original Win98 decoder, including partial writes and malformed-byte behavior.
    for (; consumed < count && (!capacity || produced < capacity); ++consumed)
    {
        U byte = Byte(e, input + consumed);
        if (!(byte & 128))
        {
            if (capacity)
                PutHalf(e, output + produced * 2, byte);
            ++produced;
        }
        else if (!(byte & 64))
        {
            if (pending)
            {
                --pending;
                if (capacity)
                    PutHalf(e, output + produced * 2, (Half(e, output + produced * 2) << 6) | (byte & 63));
                if (!pending)
                    ++produced;
            }
        }
        else if (!pending)
        {
            while (byte & 128)
            {
                byte = (byte * 2) & 255;
                ++pending;
            }
            if (capacity)
                PutHalf(e, output + produced * 2, byte >> pending);
            --pending;
        }
        else
        {
            pending = 0;
            ++produced;
        }
    }
    return capacity && consumed < count ? Error(e, 122) : produced;
}
U Utf8Encode(Environment &e, U input, U count, U output, U capacity)
{
    U produced = 0;
    for (U i = 0; i < count; ++i)
    {
        U ch = Half(e, input + i * 2), width = ch < 128 ? 1 : ch < 2048 ? 2 : 3;
        if (capacity && width > capacity - produced)
            return Error(e, 122);
        if (capacity)
        {
            if (width == 1)
                PutByte(e, output + produced, ch);
            else if (width == 2)
            {
                PutByte(e, output + produced, 0xc0 | ch >> 6);
                PutByte(e, output + produced + 1, 0x80 | (ch & 63));
            }
            else
            {
                PutByte(e, output + produced, 0xe0 | ch >> 12);
                PutByte(e, output + produced + 1, 0x80 | ((ch >> 6) & 63));
                PutByte(e, output + produced + 2, 0x80 | (ch & 63));
            }
        }
        produced += width;
    }
    return produced;
}
U Convert(Environment &e, const U *a, bool wide)
{
    U page = a[0], flags = a[1], input = a[2], count = a[3], output = a[4], capacity = a[5];
    bool unicode = page >= 60000;
    const Table *table = unicode ? nullptr : Lookup(e, page);
    if ((!unicode && !table) || (unicode && page != 65000 && page != 65001) || !count ||
        (capacity & 0x80000000u) || !input || (capacity && !output) ||
        (input == output && (!unicode || capacity)) || (wide && unicode && (a[6] || a[7])))
        return Error(e, 87);
    if ((unicode && flags) ||
        (!unicode && (wide ? ((!(flags & 0x200) && (flags & 0x70)) || (flags & 0xfffff98fu))
                           : ((flags & 0xfffffff0u) || ((flags & 1) && (flags & 2))))))
        return Error(e, 1004);
    count = Length(e, input, count, wide);
    if (!count)
        return Error(e, 87);
    // the SBCS-to-UTF16 path computes an end pointer in
    // DWORD arithmetic. Its plain loop halves the signed byte capacity;
    // the decomposition loop instead compares unsigned end pointers.
    if (!unicode && !wide && capacity &&
        ((!(flags & 2) && (capacity & 0x40000000u)) ||
         ((flags & 2) && output + capacity * 2 <= output)))
        return Error(e, 122);
    if (unicode && page == 65000)
    {
        e.Unsupported("NLS UTF-7 conversion is not ported");
        return 0;
    }
    if (unicode)
        return wide ? Utf8Encode(e, input, count, output, capacity)
                    : Utf8Decode(e, input, count, output, capacity);
    if (wide && (flags & 0x200))
    {
        const auto *composition = e.Unicode();
        if (!composition || !composition->Valid())
        {
            e.Unsupported("NLS Unicode composition data unavailable");
            return 0;
        }
        return Win98NlsComposite::Convert(e, *table, *composition, flags, input, count, output,
                                          capacity, a[6], a[7]);
    }
    if (!wide && (flags & 2))
    {
        const auto *decomposition = e.Decomposition();
        if (!decomposition || !decomposition->Valid())
        {
            e.Unsupported("NLS Unicode decomposition data unavailable");
            return 0;
        }
        return Win98NlsDecompose::Convert(e, *table, *decomposition, flags, input, count, output, capacity);
    }
    U amount = !capacity || count <= capacity ? count : capacity;
    if (!wide)
    {
        U info = table->Offset(1), fallback = table->Half(info + 4), fallback_byte = table->Half(info + 8);
        for (U i = 0; i < amount; ++i)
        {
            U byte = Byte(e, input + i), ch = table->Wide(byte, flags & 4);
            if (capacity)
                PutHalf(e, output + i * 2, ch);
            if ((flags & 8) && ((ch == fallback && Byte(e, input + i) != fallback_byte) ||
                                (ch > 0xdfff && ch < 0xf900)))
                return Error(e, 1113);
        }
    }
    else
    {
        U info = table->Offset(1), fallback = table->Half(info + 2), original = table->Half(info + 6);
        U replacement = a[6] ? Byte(e, a[6]) : fallback;
        if (a[7])
            e.Word(a[7], 0);
        if (capacity || a[6] || a[7])
            for (U i = 0; i < amount; ++i)
            {
                U ch = Half(e, input + i * 2), byte = table->Narrow(ch);
                if ((flags & 0x400) && table->Wide(byte) != ch)
                    byte = fallback;
                if (byte == (fallback & 255) && ch != original)
                {
                    if (a[7])
                        e.Word(a[7], 1);
                    if (capacity)
                        byte = replacement;
                }
                if (capacity)
                    PutByte(e, output + i, byte);
            }
    }
    return amount < count ? Error(e, 122) : amount;
}
} // namespace
U Call(Environment &e, U ordinal, const U *a)
{
    if (ordinal == 312)
        return e.Ansi();
    if (ordinal == 406)
        return e.Oem();
    if (ordinal == 318)
        return Info(e, a[0], a[1]);
    if (ordinal == 537 || ordinal == 538)
    {
        // / : classify the low byte through the optional lead-byte table.
        const auto *table = Lookup(e, ordinal == 537 ? 0 : a[0]);
        if (!table) return Error(e, ordinal == 537 ? 2 : 87);
        const U offset = table->Offset(4), byte = a[ordinal == 537 ? 0 : 1] & 255;
        return offset && table->Half(offset + byte * 2) != 0;
    }
    if (ordinal == 594 || ordinal == 811)
        return Convert(e, a, ordinal == 811);
    return 0;
}
} // namespace Win98Nls
