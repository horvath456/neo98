// SPDX-License-Identifier: GPL-2.0-or-later
// Original Win98 SBCS MB_COMPOSITE branch.
#pragma once
namespace Win98NlsDecompose
{
using U = unsigned;
class UnicodeTable
{
    const unsigned char *data = nullptr;
    U Half(U at) const { return data[at] | U(data[at + 1]) << 8; }

  public:
    // 's decomposition pointer is UNICODE.NLS + 2002h. Native bounds
    // checks cover every three-level trie entry and each pair of leaf WORDs.
    bool Load(const unsigned char *bytes, U size)
    {
        data = nullptr;
        if (!bytes || size < 0x2002)
            return false;
        data = bytes;
        const U end = ((Half(0x2000) + 0x1000) & 0xffff) * 2;
        if (end < 0x2202 || end > size)
            return data = nullptr, false;
        const U words = (end - 0x2002) / 2;
        for (U page = 0; page < 256; ++page)
        {
            const U first = Half(0x2002 + page * 2);
            if (first > words || words - first < 16)
                return data = nullptr, false;
            for (U nibble = 0; nibble < 16; ++nibble)
            {
                const U second = Half(0x2002 + (first + nibble) * 2);
                if (second > words || words - second < 32)
                    return data = nullptr, false;
            }
        }
        return true;
    }
    bool Valid() const { return data != nullptr; }
    // The first leaf WORD is the mark, the second the base character.
    // Both words are published even if either zero makes the lookup fail.
    bool Pair(U character, U &mark, U &base) const
    {
        if (!data)
            return false;
        character &= 0xffff;
        const U first = Half(0x2002 + (character >> 8) * 2);
        const U second = Half(0x2002 + (first + ((character >> 4) & 15)) * 2);
        const U leaf = 0x2002 + (second + (character & 15) * 2) * 2;
        mark = Half(leaf);
        base = Half(leaf + 2);
        return mark && base;
    }
};

// M.Read16/Write16 access a WORD at a guest or scratch
// address. The existing character stays composed if no extra WORD fits.
template <class M> U Expand(M &m, const UnicodeTable &unicode, U output, U end)
{
    U mark = 0, base = 0;
    if (!unicode.Pair(m.Read16(output), mark, base))
        return 1;
    U cursor = output, added = 0;
    do
    {
        if (end - 2 <= cursor)
            break;
        cursor += 2;
        for (U at = cursor, remaining = added; remaining; --remaining, at -= 2)
            m.Write16(at, m.Read16(at - 2));
        ++added;
        m.Write16(output, base);
        m.Write16(output + 2, mark);
    } while (unicode.Pair(base, mark, base));
    return added + 1;
}

// E.Read(address,out,bytes), Write(address,data,bytes), Error(code).
// Table is the existing Win98Nls::Table. Public argument/flag validation and
// negative input-length resolution remain in MultiByteToWideChar's caller.
// Preconditions: a validated SBCS codepage, flags containing MB_COMPOSITE.
template <class E, class Table>
U Convert(E &e, const Table &table, const UnicodeTable &unicode, U flags, U input, U count, U output,
          U capacity)
{
    struct Guest
    {
        E &e;
        U Read16(U at)
        {
            unsigned short value = 0;
            e.Read(at, &value, 2);
            return value;
        }
        void Write16(U at, U value)
        {
            const unsigned short word = value;
            e.Write(at, &word, 2);
        }
    } guest{e};
    struct Scratch
    {
        unsigned short words[5] = {};
        U Read16(U at) { return words[at / 2]; }
        void Write16(U at, U value) { words[at / 2] = value; }
    } scratch;
    auto byte = [&](U at)
    {
        unsigned char value = 0;
        e.Read(at, &value, 1);
        return U(value);
    };
    const U info = table.Offset(1), fallback = table.Half(info + 4), fallback_byte = table.Half(info + 8);
    const U end = output + capacity * 2;
    U produced = 0;
    for (U i = 0; i < count; ++i)
    {
        if (capacity && output + produced * 2 >= end)
        {
            e.Error(122);
            return 0;
        }
        const U source = input + i, ch = table.Wide(byte(source), flags & 4);
        // /Windows 98 write the mapped WORD before validation/decomposition.
        if (capacity)
            guest.Write16(output + produced * 2, ch);
        else
            scratch.Write16(0, ch);
        if ((flags & 8) && ((ch == fallback && byte(source) != fallback_byte) ||
                            (ch >= 0xe000 && ch <= 0xf8ff)))
        {
            e.Error(1113);
            return 0;
        }
        // Count mode uses the original five-WORD temporary buffer.
        produced += capacity ? Expand(guest, unicode, output + produced * 2, end)
                             : Expand(scratch, unicode, 0, 10);
    }
    return produced;
}
} // namespace Win98NlsDecompose
