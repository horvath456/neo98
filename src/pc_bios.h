// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string.h>

namespace PcBios
{
constexpr unsigned Base = 0xf0000, Size = 0x10000;
// The existing virtual PC firmware, shared by the Win16 selectors and the
// system VM's flat BIOS window. No SMBIOS table is advertised.
inline void Page(unsigned offset, unsigned char (&bytes)[4096])
{
    memset(bytes, 0xff, sizeof bytes);
    if (offset == 0xf000)
    {
        memcpy(bytes + 0xff5, "04/23/99", 8);
        bytes[0xffe] = 0xfc;
        bytes[0xfff] = 0;
    }
}
}
