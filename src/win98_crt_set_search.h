// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98CrtSetSearch {
using U = unsigned;
// The CRT character-set searches construct the same 256-bit set.
// keep=true is strspn; keep=false is strcspn or strpbrk.
template<class E> U ByteSpan(E &e, U text, U set, bool keep) {
    U bits[8] = {};
    for (U c; (c = e.Read8(set++)) != 0;) bits[c >> 5] |= U(1) << (c & 31);
    U p = text;
    for (U c; (c = e.Read8(p)) != 0; ++p)
        if (bool(bits[c >> 5] & (U(1) << (c & 31))) != keep) break;
    return p - text;
}
// Multibyte set searches share this scan.
// A dangling lead byte in the SET breaks at that nonzero byte, even without
// matching TEXT. Preserve this original malformed-input behavior.
template<class E> bool Contains(E &e, U text, U set) {
    while (U c = e.Read8(set)) {
        if (e.LeadByte(c)) {
            if (c == e.Read8(text) && e.Read8(set + 1) == e.Read8(text + 1)) break;
            if (!e.Read8(set + 1)) break;
            ++set;
        } else if (c == e.Read8(text)) break;
        ++set;
    }
    return e.Read8(set) != 0;
}
// _mbspbrk, _mbscspn, _mbsspn and _mbsspnp share
// the multibyte character-set scan.
// Count results are byte offsets. Pointer forms return zero at the terminator.
template<class E> U Search(E &e, U text, U set, bool keep, bool pointer) {
    U p = text;
    if (!e.Multibyte()) p += ByteSpan(e, text, set, keep);
    else {
        e.Lock(25);
        while (e.Read8(p)) {
            if (Contains(e, p, set) != keep) break;
            if (e.LeadByte(e.Read8(p)) && !e.Read8(++p)) break;
            ++p;
        }
        e.Unlock(25);
    }
    return pointer ? (e.Read8(p) ? p : 0) : p - text;
}
}
