// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// GDI.528 GDIParametersInfo and the TrueType switches GDI
// reads at start. USER hands SPI_GETFONTSMOOTHING and
// SPI_SETFONTSMOOTHING to this entry.
namespace Win98GdiParameters
{
// GDI DS:34h. Windows 98 starts it at 1; bit 8 TTOnly, 2000h TTIfCollisions,
// 4000h TTIfNoFontname and 10h FontSmoothing come from WIN.INI [TrueType].
struct State
{
    unsigned short flags = 1;
};
inline State &System()
{
    static State state;
    return state;
}
// every key through GetProfileInt with default 0; the value is
// negated and masked, so only its low bits decide (FontSmoothing=32 is off).
template <class ProfileInt> void Boot(State &state, ProfileInt profile_int)
{
    state.flags = 1;
    state.flags |= unsigned(-profile_int("TTOnly")) & 8;
    state.flags |= unsigned(-profile_int("TTIfCollisions")) & 0x2000;
    state.flags |= unsigned(-profile_int("TTIfNoFontname")) & 0x4000;
    state.flags |= unsigned(-profile_int("FontSmoothing")) & 0x10;
}
// 4Ah writes the WORD flags & 10h, 4Bh sets or clears the bit and
// with SPIF_UPDATEINIFILE writes "1" or "0" to [TrueType] FontSmoothing. Both answer 1; every other action answers 0.
template <class WriteProfile>
int Query(State &state, unsigned action, unsigned parameter, unsigned flags, unsigned short &value,
          WriteProfile write_profile)
{
    if (action == 0x4a)
    {
        value = state.flags & 0x10;
        return 1;
    }
    if (action != 0x4b)
        return 0;
    if (parameter & 0xffff)
        state.flags |= 0x10;
    else
        state.flags &= ~0x10u;
    if (flags & 1)
        write_profile((parameter & 0xffff) ? "1" : "0");
    return 1;
}
} // namespace Win98GdiParameters
