// SPDX-License-Identifier: GPL-2.0-or-later
// Host bridge for tools/test_kernel_ordinals_b_original.py: the header-only
// implementations, callable through ctypes.
#include "../src/win98_kernel_ordinals_b.h"
namespace O = Win98KernelOrdinalsB;
extern "C"
{
unsigned long long kob_large(unsigned ordinal, unsigned a, unsigned b, unsigned c, unsigned d, unsigned e)
{
    O::Pair r{0, 0};
    switch (ordinal)
    {
    case 72: r = O::LargeIntegerAdd(a, b, c, d); break;
    case 73: r = O::EnlargedIntegerMultiply(a, b); break;
    case 74: r = O::EnlargedUnsignedMultiply(a, b); break;
    case 77: r = O::ExtendedMagicDivide(a, b, c, d, e); break;
    case 78: r = O::ExtendedIntegerMultiply(a, b, c); break;
    case 79: r = O::LargeIntegerShiftLeft(a, b, c); break;
    case 80: r = O::LargeIntegerShiftRight(a, b, c); break;
    case 81: r = O::LargeIntegerArithmeticShift(a, b, c); break;
    case 82: r = O::LargeIntegerNegate(a, b); break;
    case 83: r = O::LargeIntegerSubtract(a, b, c, d); break;
    case 84: r = O::ConvertLongToLargeInteger(a); break;
    case 85: r = O::ConvertUlongToLargeInteger(a); break;
    }
    return ((unsigned long long)r.high << 32) | r.low;
}
// 75: returns 1 when the divide succeeds and fills quotient and remainder.
unsigned kob_divide75(unsigned low, unsigned high, unsigned divisor, unsigned *quotient, unsigned *remainder)
{
    return O::EnlargedUnsignedDivide(low, high, divisor, *quotient, *remainder) ? 1 : 0;
}
// 76: returns the 64-bit result; *stored says whether the remainder is written.
unsigned long long kob_divide76(unsigned low, unsigned high, unsigned divisor, unsigned *remainder, unsigned *stored)
{
    bool store = false;
    unsigned rem = 0;
    const auto r = O::ExtendedLargeIntegerDivide(low, high, divisor, store, rem);
    *stored = store;
    if (store)
        *remainder = rem;
    return ((unsigned long long)r.high << 32) | r.low;
}
unsigned kob_wcstol(const unsigned short *text, unsigned *end_offset, unsigned base, unsigned want_end)
{
    const unsigned short *end = nullptr;
    const unsigned v = O::WideToLong(text, want_end ? &end : nullptr, base);
    *end_offset = want_end ? unsigned(end - text) : 0xffffffffu;
    return v;
}
int kob_strchr(const char *s, unsigned ch) { const char *p = O::NarrowChar(s, ch); return p ? int(p - s) : -1; }
int kob_strrchr(const char *s, unsigned ch) { const char *p = O::NarrowLastChar(s, ch); return p ? int(p - s) : -1; }
unsigned kob_strcspn(const char *s, const char *set) { return O::NarrowComplementSpan(s, set); }
int kob_strstr(const char *s, const char *n) { const char *p = O::NarrowFind(s, n); return p ? int(p - s) : -1; }
void kob_strncpy(char *d, const char *s, unsigned n) { O::NarrowCopyN(d, s, n); }
int kob_wcschr(const unsigned short *s, unsigned ch) { const unsigned short *p = O::WideChar(s, ch); return p ? int(p - s) : -1; }
int kob_wcscmp(const unsigned short *a, const unsigned short *b) { return O::WideCompare(a, b); }
int kob_wcsncmp(const unsigned short *a, const unsigned short *b, unsigned n) { return O::WideCompareN(a, b, n); }
void kob_wcsncpy(unsigned short *d, const unsigned short *s, unsigned n) { O::WideCopyN(d, s, n); }
struct EmptyLdt final : O::DescriptorSource
{
    bool Descriptor(unsigned, unsigned char (&)[8]) override { return false; }
};
unsigned kob_vdm_pointer(unsigned vp, unsigned protected_mode)
{
    EmptyLdt ldt;
    return O::VdmPointer(ldt, vp, protected_mode);
}
unsigned kob_descriptor(unsigned vp)
{
    EmptyLdt ldt;
    unsigned char bytes[8];
    return O::Descriptor(ldt, vp, bytes);
}
struct NoTasks final : O::TaskSource
{
    unsigned error = 0;
    unsigned TaskProcess(unsigned) override { return 0; }
    unsigned ProcessTask(unsigned) override { return 0; }
    void Error(unsigned value) override { error = value; }
};
unsigned kob_handle(unsigned ordinal, unsigned handle, unsigned type, unsigned *error)
{
    NoTasks t;
    const unsigned r = ordinal == 57 ? O::Handle32(t, handle, type) : O::Handle16(t, handle, type);
    *error = t.error;
    return r;
}
unsigned kob_slot(unsigned ordinal, unsigned char *mask, unsigned slot, unsigned *error)
{
    unsigned e = 0;
    const unsigned r = ordinal == 102 ? O::SlotAlloc(*mask, e) : O::SlotFree(*mask, slot, e);
    *error = e;
    return r;
}
}
