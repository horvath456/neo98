// SPDX-License-Identifier: GPL-2.0-or-later
// DirectDraw HEL surface DC behavior. Catalog entry 45 opens a DC and entry
// 44 releases it. The result is a word-sized HDC, not a pixel pointer; zero
// becomes DDERR_CANTCREATEDC.
// Arguments are a palette far pointer (256 four-byte entries or null), a
// DDSURFACEDESC far pointer and a reference HDC. Require DDPF_RGB. An
// eight-bit surface without an explicit palette also requires an eight-bit
// reference device. Allocation or attachment failure returns zero.
// Create a compatible memory DC and DIB section; RGB565 uses BI_BITFIELDS
// with masks f800/07e0/001f. Attach the surface dimensions, pitch, depth and
// pixel storage afterwards. Indexed surfaces use the supplied palette or the
// reference-device table. Neo98 accesses linear guest memory directly instead
// of creating a selector.
// Cache one DC by depth and RGB565 format. Reuse it when idle; DDSCAPS_OWNDC
// or an active cached DC requires a private allocation. SaveDC/RestoreDC
// applies to the cached DC. Release detaches pixels and destroys private DCs.
#pragma once
namespace Win98DDraw16Surface
{
using U = unsigned;
// DDSURFACEDESC field offsets read by this entry point.
enum : U
{
    Size = 0x6c,
    Height = 0x08,
    Width = 0x0c,
    Pitch = 0x10,
    Surface = 0x24,
    PixelFlags = 0x4c,
    BitCount = 0x54,
    RedMask = 0x58,
    Caps = 0x68,
};
// DDPF_RGB.
enum : U { Rgb = 0x40 };
// DDSCAPS_OWNDC requires a private DC.
enum : U { OwnDc = 0x00040000u };
// Derive the 565 flag from the red mask alone.
inline bool Is565(U bit_count, U red_mask) { return bit_count == 16 && red_mask == 0xf800u; }
// Require DDPF_RGB.
inline bool Describes(U pixel_flags) { return (pixel_flags & Rgb) != 0; }
// An eight-bit surface without an explicit palette requires an eight-bit
// reference device.
inline bool ReferenceFits(U bit_count, bool has_palette, U reference_depth)
{
    if (bit_count != 8 || has_palette)
        return true;
    return reference_depth == 8;
}
// The cached DC matches only when depth and the RGB565 flag are unchanged.
inline bool CacheFits(U cached_depth, bool cached_565, U depth, bool is_565)
{
    return cached_depth == depth && cached_565 == is_565;
}
// Allocate a private DC when the cache is busy or the surface requests one.
inline bool WantsOwnDc(U caps, U busy) { return busy != 0 || (caps & OwnDc) != 0; }
// Surface byte length, also used to bound guest access.
inline U Bytes(U pitch, U height) { return pitch * height; }
}
