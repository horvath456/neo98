// SPDX-License-Identifier: GPL-2.0-or-later
// Original Win98 SBCS composite conversion.
#pragma once
namespace Win98NlsComposite
{
using U = unsigned;
class UnicodeTable
{
    const unsigned char *data = nullptr;
    U size = 0, base_table = 0, mark_table = 0, matrix = 0, rows = 0, columns = 0;
    U Half(U at) const { return data[at] | U(data[at + 1]) << 8; }
    bool Span(U at, U bytes) const { return at <= size && bytes <= size - at; }
    U Lookup(U table, U ch) const
    {
        const U first = Half(table + (ch >> 8) * 2);
        const U second = Half(table + (first + ((ch >> 4) & 15)) * 2);
        return Half(table + (second + (ch & 15)) * 2);
    }
    bool ValidTrie(U at, U words, U maximum) const
    {
        if (words < 256 || !Span(at, words * 2))
            return false;
        for (U page = 0; page < 256; ++page)
        {
            const U first = Half(at + page * 2);
            if (first > words || words - first < 16)
                return false;
            for (U nibble = 0; nibble < 16; ++nibble)
            {
                const U second = Half(at + (first + nibble) * 2);
                if (second > words || words - second < 16)
                    return false;
                for (U low = 0; low < 16; ++low)
                    if (Half(at + (second + low) * 2) > maximum)
                        return false;
            }
        }
        return true;
    }

  public:
    // 's original layout, with native bounds checks.
    // The caller owns immutable bytes for the lifetime of this table.
    bool Load(const unsigned char *bytes, U length)
    {
        *this = {};
        if (!bytes || length < 0x2002)
            return false;
        data = bytes;
        size = length;
        const U header = ((Half(0x2000) + 0x1000) & 0xffff) * 2;
        if (!Span(header, 6))
            return *this = {}, false;
        rows = data[header + 4];
        columns = data[header + 5];
        base_table = header + 6;
        mark_table = base_table + Half(header) * 2;
        matrix = mark_table + Half(header + 2) * 2;
        if (!rows || !columns || !Span(matrix, rows * columns * 2) ||
            !ValidTrie(base_table, Half(header), rows) || !ValidTrie(mark_table, Half(header + 2), columns))
            return *this = {}, false;
        return true;
    }
    bool Valid() const { return data != nullptr; }
    bool NonSpacing(U ch) const { return data[(ch & 0xffff) >> 3] & (1u << (ch & 7)); }
    // A missing row/column yields zero.
    U Compose(U base, U mark) const
    {
        const U row = Lookup(base_table, base & 0xffff);
        if (!row)
            return 0;
        const U column = Lookup(mark_table, mark & 0xffff);
        return column ? Half(matrix + ((row - 1) * columns + column - 1) * 2) : 0;
    }
};

// Providers: Read(address,out,bytes), Write(address,data,bytes), Error(code).
// Table supplies Offset/Half/Narrow/Wide as Win98Nls::Table does.
// Public argument/flag validation, codepage selection and negative-count length
// resolution stay with WideCharToMultiByte. Count here is UTF-16 code units.
// Preconditions: valid original UnicodeTable, SBCS codepage, WC_COMPOSITECHECK.
template <class E, class Table>
U Convert(E &e, const Table &table, const UnicodeTable &unicode, U flags, U input, U count, U output,
          U capacity, U default_character, U used_default)
{
    const U info = table.Offset(1), fallback = table.Half(info + 2), original = table.Half(info + 6);
    unsigned char replacement = fallback;
    if (default_character)
        e.Read(default_character, &replacement, 1);
    if (used_default)
    {
        const U zero = 0;
        e.Write(used_default, &zero, 4);
    }
    auto mark_used = [&]()
    {
        if (used_default)
        {
            const U one = 1;
            e.Write(used_default, &one, 4);
        }
    };
    auto read_half = [&](U at)
    {
        unsigned short value = 0;
        e.Read(at, &value, 2);
        return U(value);
    };
    auto put = [&](U at, U byte)
    {
        if (capacity)
        {
            const unsigned char value = byte;
            e.Write(at, &value, 1);
        }
    };
    auto mapped = [&](U ch, U destination, U original_source)
    {
        U byte = table.Narrow(ch);
        put(destination, byte);
        // The original stores before rereading an ordinary source character.
        // Partial input/output overlap can change these subsequent comparisons.
        if ((flags & 0x400) && table.Wide(byte) != (original_source ? read_half(original_source) : ch))
        {
            byte = fallback & 255;
            put(destination, byte);
        }
        if (byte == (fallback & 255) && (original_source ? read_half(original_source) : ch) != original)
        {
            mark_used();
            if (replacement != fallback)
                put(destination, replacement);
        }
    };
    U produced = 0;
    // SBCS count/write branches. In the original count
    // mode a stack byte receives writes and flag 80000000h prevents decrement.
    for (U i = 0; i < count; ++i)
    {
        if (capacity && produced >= capacity)
        {
            e.Error(122);
            return 0;
        }
        const U source = input + i * 2, ch = read_half(source), destination = output + produced;
        // previous means the previous INPUT code unit.
        if (!unicode.NonSpacing(ch))
        {
            mapped(ch, destination, source);
            ++produced;
            continue;
        }
        if (produced)
        {
            const U composed = unicode.Compose(read_half(source - 2), ch);
            if (composed)
            {
                mapped(composed, destination - 1, 0);
                continue;
            }
        }
        if (flags & 0x10)
            continue;
        if (flags & 0x40)
        {
            mark_used();
            put(produced ? destination - 1 : destination, replacement);
            if (!produced)
                ++produced;
            continue;
        }
        mapped(ch, destination, source);
        ++produced;
    }
    return produced;
}
} // namespace Win98NlsComposite
