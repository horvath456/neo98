// SPDX-License-Identifier: GPL-2.0-or-later
// KERNEL32 pointer-probe bodies from the pinned local Win98 image.
// ReadByte/AddByte return false on a memory fault. That boundary replaces
// x86 exception dispatch; these algorithms preserve the original probe order.
#pragma once
namespace Win98PointerProbe
{
using U = unsigned;
struct State
{
    unsigned char read_sum = 0; // Original, written only after success.
    unsigned char write_add = 0; // Original, read before every ADD.
};

// IsBadReadPtr and IsBadHugeReadPtr share.
template<class E> U Read(E &e, State &s, U pointer, U bytes)
{
    if (!bytes) return 0;
    if (!pointer) return 1;
    U last, first;
    if (!e.ReadByte(pointer + bytes - 1, last) || !e.ReadByte(pointer, first)) return 1;
    U sum = (last + first) & 255;
    if (bytes > 0x1001)
    {
        U count = (bytes - 2) >> 12;
        do
        {
            pointer += 0x1000;
            U value;
            if (!e.ReadByte(pointer, value)) return 1;
            sum = (sum + value) & 255;
        } while (--count);
    }
    s.read_sum = sum;
    return 0;
}

// IsBadWritePtr and IsBadHugeWritePtr share.
// Unlike the read family, the first byte is touched before the last byte.
template<class E> U Write(E &e, State &s, U pointer, U bytes)
{
    if (!bytes) return 0;
    if (!pointer) return 1;
    if (!e.AddByte(pointer, s.write_add) || !e.AddByte(pointer + bytes - 1, s.write_add)) return 1;
    if (bytes > 0x1001)
    {
        U count = (bytes - 2) >> 12;
        do
        {
            pointer += 0x1000;
            if (!e.AddByte(pointer, s.write_add)) return 1;
        } while (--count);
    }
    return 0;
}

// IsBadCodePtr Windows 98 uses the read body with a length of one.
// It does not independently test an execute permission bit.
template<class E> U Code(E &e, State &s, U pointer) { return Read(e, s, pointer, 1); }

// IsBadStringPtrA, including its bounded REPNE SCASB.
template<class E> U String(E &e, U pointer, U maximum)
{
    if (!maximum) return 0;
    if (!pointer) return 1;
    do
    {
        U value;
        if (!e.ReadByte(pointer++, value)) return 1;
        if (!value) break;
    } while (--maximum);
    return 0;
}

// Windows 98 IsBadStringPtrW returns zero without reading memory or changing
// LastError. Internal native validation must use a real probe.
inline U WideString(U, U) { return 0; }
} // namespace Win98PointerProbe
