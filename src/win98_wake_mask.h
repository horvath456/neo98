// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// The change bits a GetMessage or PeekMessage consumes, as Win98 USER computes
// them from the message filter.
//
// The common retrieval of USER.EXE (entered by PeekMessage and
// GetMessage) takes a zero maximum as "everything" (: mask 217Fh, the
// maximum becomes FFFFh); otherwise CalcWakeMask builds the mask.
// It starts at 207Fh, or at the PM_QS_* bits of the high flags word when the
// caller gave any (an input bit brings 2007h, a post or timer bit both 18h),
// and removes each input kind whose messages the filter cannot reach:
//
// QS_MOUSEMOVE 02h 0200h or 00A0h ( per message)
// QS_MOUSEBUTTON 04h 00A1h..00A9h or 0201h..020Ah ( per range)
// QS_KEY 01h 0100h..0108h
// QS_PAINT 20h 000Fh
// QS_TIMER 10h 0113h or 0118h
//
// A minimum of 0023h (WM_QUEUESYNC) keeps all three input kinds. A filter
// whose minimum lies above its maximum is the inverted form; Windows 98 takes a
// message below the maximum or above the minimum, a range that starts
// at or below the minimum or ends at or above the maximum. then clears
// the change bits of the mask except QS_SENDMESSAGE (40h): posted messages
// are always consumed, QS_ALLPOSTMESSAGE (100h) only by an unfiltered
// retrieval. Filter values are the 16-bit words USER sees.
//
// The port does not filter by the PM_QS_* bits (the retrieval takes every
// kind the range allows), so `qs` is zero there; the oracle test
// tools/test_wake_mask_original.py compares all three inputs with USER.EXE.
namespace Win98WakeMask {
constexpr unsigned SendMessage = 0x40;

inline unsigned Compute(unsigned minimum, unsigned maximum, unsigned qs = 0)
{
    minimum &= 0xffff;
    maximum &= 0xffff;
    qs &= 0xffff;
    if (!maximum)
        return 0x217fu & ~SendMessage;
    unsigned mask = 0x207f;
    if (qs)
    {
        if (qs & 7) qs |= 0x2007;
        if (qs & 0x18) qs |= 0x18;
        mask = qs;
    }
    const bool inverted = minimum > maximum;
    auto single = [&](unsigned m) { return inverted ? (m < maximum || m > minimum) : (m >= minimum && m <= maximum); };
    auto range = [&](unsigned first, unsigned last) {
        return inverted ? (first <= minimum || last >= maximum) : (last >= minimum && first <= maximum);
    };
    if (!single(0x200) && !single(0xa0)) mask &= ~0x2u;
    if (!range(0xa1, 0xa9) && !range(0x201, 0x20a)) mask &= ~0x4u;
    if (!range(0x100, 0x108)) mask &= ~0x1u;
    if (!single(0xf)) mask &= ~0x20u;
    if (!single(0x113) && !single(0x118)) mask &= ~0x10u;
    if (minimum == 0x23) mask |= 7;
    return mask & ~SendMessage;
}
} // namespace Win98WakeMask
