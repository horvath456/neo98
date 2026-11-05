// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_kernel.h"
namespace Win98
{
unsigned HandleCount(unsigned requested) { return requested > 256 ? 256 : requested; }
unsigned UnsupportedResult(unsigned encoding)
{
    switch ((encoding >> 4) & 15)
    {
    case 15:
        return 0xffffffff;
    case 14:
        return 120;
    case 13:
        return 50;
    case 2:
        // MMSYSERR_NOTSUPPORTED, the answer of the WINMM contracts.
        return 8;
    case 1:
        return 1;
    default:
        return 0;
    }
}
unsigned Version(unsigned flags)
{
    if (flags & 0x200000)
        return 0xa03;
    return flags & 0x80000000 ? 0xc0000004 : 0xc0000a04;
}
} // namespace Win98
