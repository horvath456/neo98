// SPDX-License-Identifier: GPL-2.0-or-later
// KERNEL32 character classification.
#pragma once
#include "win98_system_locale.h"
namespace Win98CharacterType {
using U = unsigned;
template<class E> U Decode(E &e, U locale, U source) {
    const U first = e.Read8(source);
    if (e.Multibyte(locale) && e.LocaleLead(locale, first))
        return (first << 8) | e.Read8(source + 1);
    return first;
}
template<class E> U Query(E &e, U requested, U type, U source, U count, U output) {
    const U system = e.SystemLocale();
    const U locale = Win98SystemLocale::ResolveLocale(requested, system, [&] { return e.UserLocale(); });
    const U data = locale == system ? e.SystemTable()
                                   : (locale & 0xfff00000u) ? 0 : e.LookupLocale(locale);
    auto invalid = [&]() -> U { e.Error(87); return 0; };
    if (!data || !source || !count || !output || source == output)
        return invalid();
    if (int(count) <= -1)
        count = e.Length(source) + 1;
    if (type != 1 && type != 2 && type != 4)
        return invalid();
    if (!e.Multibyte(data)) {
        for (U i = 0; int(i) < int(count); ++i)
            e.Write16(output + i * 2, e.TypeWord(data, type, e.Read8(source + i)));
        return 1;
    }
    const U end = source + count;
    for (U i = 0; int(i) < int(count) && source < end; ++i) {
        // The original preflight consults ACP, not the requested locale's table.
        if (e.SystemLead(e.Read8(source)) && source + 1 >= end)
            return invalid();
        const U character = Decode(e, data, source);
        U slot = e.TypeWord(data, type, character >> 8);
        slot = e.TypeWord(data, type, slot + ((character >> 4) & 15));
        e.Write16(output + i * 2, e.TypeWord(data, type, slot + (character & 15)));
        source += character < 256 ? 1 : 2;
    }
    return 1;
}
} // namespace Win98CharacterType
