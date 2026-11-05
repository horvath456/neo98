// SPDX-License-Identifier: GPL-2.0-or-later
// KERNEL32 ordinal exports 61–122 implement large-integer operations, WOW
// pointer/handle helpers, reserved thread slots, and CRT-style strings.
// Native providers are in src/x86_kernel_ordinals_b.cpp.
#pragma once
namespace Win98KernelOrdinalsB
{
using U = unsigned;
using ULL = unsigned long long;
struct Pair
{
    U low, high;
};
inline Pair Split(ULL v)
{
    return {U(v), U(v >> 32)};
}
inline ULL Join(U low, U high)
{
    return (ULL(high) << 32) | low;
}

// ---- Large integers -------------------------------------------------------
// 72: add / adc.
inline Pair LargeIntegerAdd(U al, U ah, U bl, U bh)
{
    return Split(Join(al, ah) + Join(bl, bh));
}
// 83: sub / sbb.
inline Pair LargeIntegerSubtract(U al, U ah, U bl, U bh)
{
    return Split(Join(al, ah) - Join(bl, bh));
}
// 73: imul dword.
inline Pair EnlargedIntegerMultiply(U a, U b)
{
    return Split(ULL((long long)(int)a * (long long)(int)b));
}
// 74: mul dword.
inline Pair EnlargedUnsignedMultiply(U a, U b)
{
    return Split(ULL(a) * b);
}
// 75: div dword with EDX:EAX = high:low. The divide faults when the
// divisor is zero or the quotient does not fit 32 bits; false reports that.
// On success EAX holds the quotient, EDX the remainder, and the remainder is
// also stored when the pointer is not null.
inline bool EnlargedUnsignedDivide(U low, U high, U divisor, U &quotient, U &remainder)
{
    if (!divisor || high >= divisor)
        return false;
    const ULL n = Join(low, high);
    quotient = U(n / divisor);
    remainder = U(n % divisor);
    return true;
}
// 76: 64 shift-subtract rounds. A zero divisor returns the dividend
// unchanged and skips the remainder store.
inline Pair ExtendedLargeIntegerDivide(U low, U high, U divisor, bool &store, U &remainder)
{
    store = false;
    if (!divisor)
        return {low, high};
    const ULL n = Join(low, high);
    remainder = U(n % divisor);
    store = true;
    return Split(n / divisor);
}
// 77: |dividend| * magic as a 128-bit product, keep the high 64
// bits, shift right by the byte count in steps of 31, restore the sign.
inline Pair ExtendedMagicDivide(U low, U high, U magic_low, U magic_high, U shift)
{
    ULL dividend = Join(low, high);
    const bool negative = (high & 0x80000000u) != 0;
    if (negative)
        dividend = 0 - dividend;
    const unsigned __int128 product = (unsigned __int128)dividend * Join(magic_low, magic_high);
    ULL v = ULL(product >> 64);
    unsigned char count = (unsigned char)shift;
    while (count > 0x1f)
    {
        count = (unsigned char)(count - 0x1f);
        v >>= 31;
    }
    v >>= count;
    if (negative)
        v = 0 - v;
    return Split(v);
}
// 78: signed 64 x signed 32, low 64 bits of the product.
inline Pair ExtendedIntegerMultiply(U low, U high, U multiplier)
{
    ULL a = Join(low, high);
    U b = multiplier;
    const bool negative = ((high ^ multiplier) & 0x80000000u) != 0;
    if (high & 0x80000000u)
        a = 0 - a;
    if (b & 0x80000000u)
        b = 0 - b;
    ULL r = (ULL(b) * U(a)) + (ULL(U(ULL(b) * U(a >> 32))) << 32);
    if (negative)
        r = 0 - r;
    return Split(r);
}
// Shift helpers mask the count to six bits;
// 32 and above move the other half through a shift by the low five bits.
inline Pair LargeIntegerShiftLeft(U low, U high, U count)
{
    const U c = count & 0x3f;
    if (c < 32)
        return c ? Pair{low << c, (high << c) | (low >> (32 - c))} : Pair{low, high};
    return {0, low << (c & 31)};
}
inline Pair LargeIntegerShiftRight(U low, U high, U count)
{
    const U c = count & 0x3f;
    if (c < 32)
        return c ? Pair{(low >> c) | (high << (32 - c)), high >> c} : Pair{low, high};
    return {high >> (c & 31), 0};
}
inline Pair LargeIntegerArithmeticShift(U low, U high, U count)
{
    const U c = count & 0x3f;
    if (c < 32)
        return c ? Pair{(low >> c) | (high << (32 - c)), U((int)high >> c)} : Pair{low, high};
    const U v = U((int)high >> (c & 31));
    return {v, (v & 0x80000000u) ? 0xffffffffu : 0};
}
// 82: neg edx / neg eax / sbb edx, 0.
inline Pair LargeIntegerNegate(U low, U high)
{
    return Split(0 - Join(low, high));
}
// The signed division helper sign-extends the high dividend; the unsigned
// helper clears it.
inline Pair ConvertLongToLargeInteger(U v)
{
    return {v, (v & 0x80000000u) ? 0xffffffffu : 0};
}
inline Pair ConvertUlongToLargeInteger(U v)
{
    return {v, 0};
}

// ---- WOW pointers and descriptors -----------------------------------------
// The protected-mode branch reads the LDT and checks its limit.
// The provider answers false when the
// selector lies beyond that limit. NEO98 has no LDT, so its provider always
// answers false; the real-mode arithmetic and every rejection are original.
struct DescriptorSource
{
    virtual ~DescriptorSource() = default;
    // Eight descriptor bytes for a selector whose index bits are already set
    // to selector | 7; false when the selector is beyond the table limit.
    virtual bool Descriptor(U selector, unsigned char (&bytes)[8]) = 0;
};
// shared by 56 and the protected branch of 68.
inline U VdmPointer(DescriptorSource &ldt, U vp, U protected_mode)
{
    const U selector = vp >> 16, offset = vp & 0xffff;
    if (!protected_mode)
        return (selector << 4) + offset; //
    if (!(selector & 4) || !(selector & ~7u))
        return 0; // and 0x378d: GDT selectors and null both fail
    unsigned char d[8];
    if (!ldt.Descriptor((selector & ~7u) | 7, d))
        return 0; // beyond the LDT limit
    if (!(d[5] & 0x80))
        return 0; // not present
    if (!(d[5] & 0x10))
        return 0; // a system descriptor
    const U base = U(d[2]) | (U(d[3]) << 8) | (U(d[4]) << 16) | (U(d[7]) << 24);
    return base + offset;
}
// ordinal 70: copies the raw descriptor; no present check.
inline U Descriptor(DescriptorSource &ldt, U vp, unsigned char (&out)[8])
{
    const U selector = vp >> 16;
    if (!selector || !(selector & 4))
        return 0;
    if (!ldt.Descriptor(selector | 7, out))
        return 0;
    return 1;
}
// and 0x2f65e, ordinals 57 and 58. WOW_TYPE_HTASK is 13; type 3
// is rejected with error 50. Every other type is a zero or sign-free extension
// of the low word. The task database lookups are providers.
struct TaskSource
{
    virtual ~TaskSource() = default;
    virtual U TaskProcess(U htask16) = 0;   // MapSL(htask << 16), then the process id
    virtual U ProcessTask(U process_id) = 0; // the task word behind the process data
    virtual void Error(U value) = 0;
};
constexpr U NotSupported = 50, HandleTask = 13, HandleRejected = 3;
inline U Handle32(TaskSource &s, U handle16, U type)
{
    if (type == HandleTask && (handle16 & 0xffff))
        return s.TaskProcess(handle16 & 0xffff);
    if (type == HandleRejected)
    {
        s.Error(NotSupported);
        return 0;
    }
    return handle16 & 0xffff;
}
inline U Handle16(TaskSource &s, U handle32, U type)
{
    if (type == HandleTask && handle32)
        return s.ProcessTask(handle32) & 0xffff;
    if (type == HandleRejected)
    {
        s.Error(NotSupported);
        return 0;
    }
    return handle32 & 0xffff;
}

// ---- Reserved thread slots 0x50..0x57 ---------------------------------------
// An eight-bit mask tracks allocated slots. A failed
// allocation sets error 259 and still adds 0x50 to its -1 sentinel, returning
// 0x4f; that quirk is kept. Freeing an unallocated or out-of-range slot sets
// error 87 and returns zero.
constexpr U SlotBase = 0x50, SlotCount = 8, NoMoreItems = 259, InvalidParameter = 87;
inline U SlotAlloc(unsigned char &mask, U &error)
{
    for (U i = 0; i < SlotCount; ++i)
        if (!(mask & (1u << i)))
        {
            mask |= (unsigned char)(1u << i);
            return SlotBase + i;
        }
    error = NoMoreItems;
    return SlotBase - 1;
}
inline U SlotFree(unsigned char &mask, U slot, U &error)
{
    const U index = slot - SlotBase;
    if (index >= SlotCount || !(mask & (1u << index)))
    {
        error = InvalidParameter;
        return 0;
    }
    mask &= (unsigned char)~(1u << index);
    return 1;
}

// ---- String helpers ---------------------------------------------------------
using W = unsigned short;
// 105: wcstol. Flags at [ebp-4]: 2 negative, 4 overflow, 8 digits
// seen; bit 1 (unsigned) is tested but never set by this entry.
inline U WideToLong(const W *text, const W **end, U base)
{
    const W *p = text;
    U flags = 0;
    W c = *p++;
    while (c == ' ' || (c >= 9 && c <= 13))
        c = *p++;
    if (c == '-')
    {
        flags |= 2;
        c = *p++;
    }
    else if (c == '+')
        c = *p++;
    if ((int)base < 0 || base == 1 || (int)base > 36)
    {
        if (end)
            *end = text;
        return 0;
    }
    if (base == 0)
    {
        if (c != '0')
            base = 10;
        else if (*p == 'x' || *p == 'X')
            base = 16;
        else
            base = 8;
    }
    if (base == 16 && c == '0' && (*p == 'x' || *p == 'X'))
    {
        ++p;
        c = *p++;
    }
    const U limit = 0xffffffffu / base, spare = 0xffffffffu % base;
    U value = 0;
    for (;;)
    {
        U digit;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
            digit = U(c | 0x20) - 0x57;
        else
            break;
        if (digit >= base)
            break;
        flags |= 8;
        if (value < limit || (value == limit && spare >= digit))
            value = value * base + digit;
        else
            flags |= 4;
        c = *p++;
    }
    --p;
    if (!(flags & 8))
    {
        if (end)
            *end = text;
        return 0;
    }
    if (!(flags & 4))
    {
        if (flags & 2)
        {
            if (value > 0x80000000u)
                flags |= 4;
        }
        else if (value > 0x7fffffffu)
            flags |= 4;
    }
    if (flags & 4)
        value = (flags & 1) ? 0xffffffffu : (flags & 2) ? 0x80000000u : 0x7fffffffu;
    if (end)
        *end = p;
    return (flags & 2) ? 0 - value : value;
}
// 108, body : strchr, finds the terminator for a zero character.
inline const char *NarrowChar(const char *s, U ch)
{
    const unsigned char c = (unsigned char)ch;
    for (;; ++s)
    {
        if ((unsigned char)*s == c)
            return s;
        if (!*s)
            return nullptr;
    }
}
// 109, body : strcspn over a 256-bit set. The scan loop continues
// with jae, that is while the character's bit is clear, so the count covers
// the leading characters that are not in the set.
inline U NarrowComplementSpan(const char *s, const char *set)
{
    U bits[8] = {};
    for (const unsigned char *q = (const unsigned char *)set; *q; ++q)
        bits[*q >> 5] |= 1u << (*q & 31);
    U n = 0;
    for (const unsigned char *q = (const unsigned char *)s; *q && !(bits[*q >> 5] & (1u << (*q & 31))); ++q)
        ++n;
    return n;
}
// 110, body : strncpy with zero fill.
inline char *NarrowCopyN(char *d, const char *s, U n)
{
    char *start = d;
    while (n)
    {
        const char c = *s++;
        if (!c)
            break;
        *d++ = c;
        --n;
    }
    while (n--)
        *d++ = 0;
    return start;
}
// 111, body : strrchr through a backward scan from the terminator.
inline const char *NarrowLastChar(const char *s, U ch)
{
    const unsigned char c = (unsigned char)ch;
    const char *p = s;
    while (*p)
        ++p;
    for (;; --p)
    {
        if ((unsigned char)*p == c)
            return p;
        if (p == s)
            return nullptr;
    }
}
// 112, body : strstr; an empty needle answers the haystack.
inline const char *NarrowFind(const char *hay, const char *needle)
{
    for (;; ++hay)
    {
        const char *h = hay, *n = needle;
        for (;;)
        {
            if (!*n)
                return hay;
            if (!*h)
                return nullptr;
            if (*h != *n)
                break;
            ++h;
            ++n;
        }
    }
}
// 113, body : wcschr, finds the terminator for a zero character.
inline const W *WideChar(const W *s, U ch)
{
    const W c = (W)ch;
    while (*s && *s != c)
        ++s;
    return *s == c ? s : nullptr;
}
// 114, body : wcscmp normalised to -1, 0, 1.
inline int WideCompare(const W *a, const W *b)
{
    int d = int(*a) - int(*b);
    while (!d && *b)
    {
        ++a;
        ++b;
        d = int(*a) - int(*b);
    }
    return d < 0 ? -1 : d > 0 ? 1 : 0;
}
// 115, body : wcsncmp returning the raw difference.
inline int WideCompareN(const W *a, const W *b, U n)
{
    if (!n)
        return 0;
    while (--n && *a && *b == *a)
    {
        ++a;
        ++b;
    }
    return int(*a) - int(*b);
}
// 116, body : wcsncpy with zero fill.
inline W *WideCopyN(W *d, const W *s, U n)
{
    W *start = d;
    while (n)
    {
        const W c = *s++;
        *d++ = c;
        --n;
        if (!c)
            break;
    }
    while (n--)
        *d++ = 0;
    return start;
}
} // namespace Win98KernelOrdinalsB
