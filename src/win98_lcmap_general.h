// SPDX-License-Identifier: GPL-2.0-or-later
// LCMapStringA general transformations. Apply case conversion into a scratch
// buffer, then width or kana mapping. Validate destination capacity and report
// allocation or buffer errors. Code pages without width/kana tables use copying;
// unsupported multibyte paths stop through the environment.
#pragma once
namespace Win98LcMapGeneral {
using U = unsigned;
enum : U { Hiragana = 0x100000, Katakana = 0x200000, Halfwidth = 0x400000, Fullwidth = 0x800000,
           InsufficientBuffer = 0x7a, OutOfMemory = 0x0e };
enum Table : U { LeadTable = 4, FullwidthTable = 8, HalfwidthTable = 9, HiraganaTable = 10, KatakanaTable = 11 };

// Three-level delta lookup.
template <class E> U Delta(E &e, U descriptor, U table, U c)
{
    U index = e.TableWord(descriptor, table, (c >> 8) & 255);
    index = e.TableWord(descriptor, table, index + ((c >> 4) & 15));
    return (c + e.TableWord(descriptor, table, index + (c & 15))) & 65535;
}

// Returns the number of bytes, or 0 with error 7Ah.
template <class E> U Copy(E &e, U descriptor, U flags, U source, U count, U output, U capacity)
{
    const bool hiragana = (flags & Hiragana) && e.HasTable(descriptor, HiraganaTable);
    const bool katakana = (flags & Katakana) && e.HasTable(descriptor, KatakanaTable);
    const bool fullwidth = (flags & Fullwidth) && e.HasTable(descriptor, FullwidthTable);
    const bool halfwidth = (flags & Halfwidth) && e.HasTable(descriptor, HalfwidthTable);
    const bool multibyte = e.Multibyte(descriptor);
    const U end = source + count, last = output + capacity;
    U cursor = source, destination = output, written = 0;
    auto read = [&]() -> U {
        U ch = e.Read8(cursor);
        // a lead byte of a multibyte descriptor takes the next byte.
        if (multibyte && e.HasTable(descriptor, LeadTable) && e.TableWord(descriptor, LeadTable, ch))
            ch = ((ch << 8) | e.Read8(cursor + 1)) & 65535;
        return ch;
    };
    auto map = [&](U ch) -> U {
        U mapped = ch;
        if (hiragana) mapped = Delta(e, descriptor, HiraganaTable, ch);
        else if (katakana) mapped = Delta(e, descriptor, KatakanaTable, ch);
        if (fullwidth) { if (mapped < 256) mapped = e.TableWord(descriptor, FullwidthTable, mapped); }
        else if (halfwidth) mapped = Delta(e, descriptor, HalfwidthTable, mapped);
        return mapped;
    };
    if (!capacity) {
        // count only.
        while (cursor < end) {
            const U ch = read();
            written += map(ch) < 256 ? 1 : 2;
            cursor += ch < 256 ? 1 : 2;
        }
        return written;
    }

    while (cursor < end && destination < last) {
        if (e.HasTable(descriptor, LeadTable) && e.TableWord(descriptor, LeadTable, e.Read8(cursor)) &&
            cursor + 1 >= end)
            return written;
        const U ch = read(), mapped = map(ch), bytes = mapped < 256 ? 1 : 2;
        written += bytes;
        if (bytes == 2) {
            if (last - 1 > destination) { e.Write8(destination, mapped >> 8); e.Write8(destination + 1, mapped); }
            else written -= 2;
        } else e.Write8(destination, mapped);
        destination += bytes;
        cursor += ch < 256 ? 1 : 2;
    }
    if (cursor < end) { e.Error(InsufficientBuffer); return 0; }
    return written;
}

// for the flags the route table sends here, already
// checked and normalized. `Case` is into the scratch buffer.
template <class E> U Map(E &e, U descriptor, U flags, U source, U count, U output, U capacity)
{
    U scratch = 0;
    bool mapped = false;
    if (flags & 0x300) {
        scratch = e.Scratch(count);
        if (!scratch) { e.Error(OutOfMemory); return 0; }
        e.Case(flags & 0x100, source, count, scratch, count);
        mapped = true;
        flags &= ~U(0x300);
    }
    const U from = mapped && capacity ? scratch : source;
    U result = ~0u;
    switch (flags) {
    case Hiragana:
    case Katakana:
        if (!capacity) { result = count; break; }
        if (capacity < count) { e.Error(InsufficientBuffer); result = 0; break; }
        result = Copy(e, descriptor, flags, from, count, output, capacity);
        break;
    case Halfwidth:
    case Fullwidth:
        if (e.Multibyte(descriptor) && e.CodePage(descriptor) == 932) { e.Unported("LCMapStringA code page 932 width route"); result = 0; break; }
        result = Copy(e, descriptor, flags, from, count, output, capacity);
        break;
    case Hiragana | Halfwidth:
    case Katakana | Halfwidth:
    case Hiragana | Fullwidth:
    case Katakana | Fullwidth:
        e.Unported("LCMapStringA two-transform copier BFFBDFA0");
        result = 0;
        break;
    default:
        break; // with EDI still -1
    }
    if (scratch) e.FreeScratch(scratch);
    return result;
}
} // namespace Win98LcMapGeneral
