// SPDX-License-Identifier: GPL-2.0-or-later
// Original GDI software-DDB GetObject and its GDI32 conversion.
#pragma once
namespace Win98BitmapQuery
{
using U = unsigned;
// GDI, software-DDB branch for the thunk's fixed count 54h.
// Caller validates a local bitmap handle and excludes DIBSECTION flag 0004h.
// The original locks primary storage directly; it does not call LockBitmap.
template<class E> U ReadDdb(E &e, U handle, unsigned char (&out)[84])
{
    const U object = e.Object(handle);
    const U storage = e.Read16(object + 0x0e);
    const U block = e.GlobalLock(storage);
    if (!block) return 0;
    for (U i = 0; i < 10; ++i) out[i] = static_cast<unsigned char>(e.Read8(block + i));
    for (U i = 10; i < 84; ++i) out[i] = 0;
    e.GlobalUnlock(storage);
    return 84;
}
inline U Word(const unsigned char *p) { return U(p[0]) | (U(p[1]) << 8); }
inline U Dword(const unsigned char *p) { return Word(p) | (Word(p + 2) << 16); }
inline U SignedWord(U value) { return (value & 0x8000) ? value | 0xffff0000u : value; }
inline U SignedByte(U value) { return (value & 0x80) ? value | 0xff00u : value; }
inline void Put(unsigned char *p, U value, U bytes)
{
    for (U i = 0; i < bytes; ++i) p[i] = static_cast<unsigned char>(value >> (i * 8));
}
// GDI32 GetObjectA -> GDI, bitmap branch only.
// GetBitmap16 supplies the original public GetObject(WORD handle,54h,temp)
// validation and query. For a software DDB it delegates to ReadDdb above.
// MapPointer represents KERNEL.357 MapSL for a non-null returned bits pointer.
// WriteGuest8 supplies protected guest writes. The thunk itself performs no
// output mapping. Other GDI object kinds have different conversion branches.
template<class E> U Get(E &e, U handle, U count, U output)
{
    unsigned char small[84]{};
    if (!e.GetBitmap16(handle & 0xffffu, small)) { e.Error(87); return 0; }
    if (!output) return 84;
    unsigned char expanded[84];
    for (U i = 0; i < 4; ++i) Put(expanded + i * 4, SignedWord(Word(small + i * 2)), 4);
    Put(expanded + 16, SignedByte(small[8]), 2);
    Put(expanded + 18, SignedByte(small[9]), 2);
    const U pointer = Dword(small + 10);
    Put(expanded + 20, pointer ? e.MapPointer(pointer) : 0, 4);
    for (U i = 0; i < 60; ++i) expanded[24 + i] = small[14 + i];
    const U bytes = count < 84 ? count : 84;
    for (U i = 0; i < bytes; ++i) e.WriteGuest8(output + i, expanded[i]);
    if (!bytes) e.Error(87);
    return bytes;
}
} // namespace Win98BitmapQuery
