// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace SystemVm
{
// Win98 SE's resident KERNEL data window. Only the native flags and the
// instruction decoder's static tables are exposed; no unrelocated code
// pointers or boot-time handles are copied into the native KERNEL.
constexpr unsigned KernelBase = 0x1c9a0;
constexpr unsigned KernelFirst = 0x1c000, KernelEnd = 0x20000;
constexpr unsigned WinFlagsOffset = 0x13cc, WinFlags = 0x2c29;
constexpr unsigned DecoderFirst = 0x1ed4, DecoderEnd = 0x1f32;
}
