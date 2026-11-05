// SPDX-License-Identifier: GPL-2.0-or-later
// USER32 character conversion.
#pragma once
namespace Win98UserCharacters {
using U = unsigned;
template<class E> U Predicate(E &e, U operation, U character) {
    character &= 255;
    if (operation == 3 && !character) return 0;
    U type = 0;
    if (!e.Type(character, type)) return 0;
    const U masks[] = {2, 1, 0x104, 0x100};
    return !!(type & masks[operation]);
}
template<class E> U Next(E &e, U source, bool extended, U page) {
    auto invalid = [&]() -> U { e.Error(87); return 0; };
    if (e.BadRead(source)) return invalid();
    if (extended && e.Lead(page & 65535, e.Read8(source))) {
        ++source;
        if (e.BadRead(source)) return invalid();
    }
    if (e.Read8(source)) {
        ++source;
        if (e.BadRead(source)) return invalid();
    }
    return source;
}
template<class E> U Previous(E &e, U start, U current, bool extended, U page) {
    if (!extended) {
        if (e.BadRead(start) || e.BadRead(current)) { e.Error(87); return 0; }
        return current > start ? current - 1 : current;
    }
    if (current <= start) return current;
    --current;
    const U previous = current - 1;
    U cursor = previous, odd = 0;
    while (cursor >= start && e.Lead(page & 65535, e.Read8(cursor))) {
        odd ^= 1;
        --cursor;
    }
    return odd ? previous : current;
}
} // namespace Win98UserCharacters
