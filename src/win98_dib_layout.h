// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Native Win98 SE DIBENG and GDI.
// Provider addresses replace selectors. Arithmetic intentionally wraps at
// DWORD.
namespace Win98Dib {
using U = unsigned;
inline U Word(U v) { return v & 65535; }
inline bool Negative(U v) { return (v & 0x80000000u) != 0; }
// DIBENG. The bitfields arms do not inspect biPlanes.
template <class E> U Validate(E &e, U info, U maximumCompression) {
  const U compression = e.Read32(info + 16), depth = e.Read16(info + 14);
  if (compression == 3) {
    if (depth != 16 && depth != 32)
      return 0;
    const U red = e.Read32(info + 40), green = e.Read32(info + 44),
            blue = e.Read32(info + 48);
    if (depth == 16 && blue == 31) {
      if (red == 0x7c00 && green == 0x3e0)
        return 1;
      if (red == 0xf800 && green == 0x7e0)
        return 0x40;
    }
    if (depth == 32 && red == 0xff0000 && green == 0xff00 && blue == 0xff)
      return 1;
    return 0;
  }
  if (compression > maximumCompression)
    return 0;
  if (depth != 1 && depth != 4 && depth != 8 && depth != 16 && depth != 24 &&
      depth != 32)
    return 0;
  if (e.Read16(info + 12) != 1)
    return 0;
  if (compression && Negative(e.Read32(info + 8)))
    return 0;
  return 1;
}
// DIBENG. bits is a 16:16 token in the original oracle; a native
// provider may use a different address representation at this boundary.
template <class E>
void Construct(E &e, U output, U width, U height, U depth, U flags, U info,
               U bits, U scans = 0, U strideOverride = 0) {
  flags = Word(flags);
  depth = Word(depth);
  e.Write16(output + 28, flags);
  e.Write8(output + 9, depth);
  e.Write8(output + 8, 1);
  e.Write16(output + 2, width);
  e.Write32(output + 32, info);
  e.Write32(output + 22, Word(bits));
  e.Write16(output + 26, bits >> 16);
  e.Write32(output + 10, 0);
  e.Write32(output + 18, 0);
  e.Write32(output + 36, 0);
  e.Write32(output + 40, 0);
  e.Write32(output + 44, 0);
  e.Write16(output + 30, 0x400);
  e.Write16(output, 0x5250);
  if (Negative(scans)) {
    e.Write16(output + 4, height);
    return;
  }
  U stride = Word(strideOverride);
  if (!stride)
    stride = ((width * depth + 15) & 0xfffffff0u) >> 3;
  e.Write16(output + 6, stride);
  stride = (stride + 3) & 0xfffffffcu;
  if ((flags & 1) || Negative(height)) {
    if (!(flags & 1))
      height = 0 - height;
    e.Write32(output + 14, stride);
    e.Write16(output + 4, height);
    return;
  }
  e.Write32(output + 14, 0 - stride);
  e.Write16(output + 4, height);
  if (Word(scans))
    height = (height & 0xffff0000u) | Word(scans);
  e.Write32(output + 22, e.Read32(output + 22) + stride * (height - 1));
}
// DIBENG, shared by CreateDIBPDevice and the DIBBits/ToDevice arms.
template <class E>
U Prepare(E &e, U info, U output, U bits, U flags, U maximumCompression,
          U scans = 0) {
  U valid = Validate(e, info, maximumCompression);
  if (!valid)
    return 0;
  if (valid == 0x40)
    flags |= valid;
  Construct(e, output, e.Read32(info + 4), e.Read32(info + 8),
            e.Read16(info + 14), flags, info, bits, scans);
  return 1;
}
// DIBENG export300. Zero-sized accepted descriptors return zero bytes.
template <class E> U CreateDevice(E &e, U info, U output, U bits, U flags) {
  if (e.Read16(info + 14) == 1) {
    const U palette = (info & 0xffff0000u) | Word(info + e.Read32(info));
    if (!e.Read32(palette) && e.Read32(palette + 4) == 0xffffff)
      flags |= 0x800;
  }
  if (!Prepare(e, info, output, bits, flags, 0))
    return 0;
  U stride = e.Read32(output + 14);
  if (Negative(stride))
    stride = 0 - stride;
  return stride * e.Read16(output + 4);
}
// DIBENG. Original DIB transfer descriptors are larger than 48 bytes.
template <class E>
U TransferDevice(E &e, U info, U output, U bits, U scans, U compression) {
  if ((compression & 255) != 0 && (compression & 255) != 3)
    scans |= 0x80000000u;
  if (!Prepare(e, info, output, bits, 4, 2, scans))
    return 0;
  e.Write8(output + 0x42, 1);
  return 1;
}
// GDI. Expand BITMAPCOREHEADER and optionally its color table.
template <class E>
void ExpandCore(E &e, U source, U output, bool paletteIndices,
                bool copyPalette) {
  e.Write32(output, 40);
  e.Write32(output + 4, e.Read16(source + 4));
  e.Write32(output + 8, e.Read16(source + 6));
  e.Write16(output + 12, e.Read16(source + 8));
  const U depth = e.Read16(source + 10);
  e.Write16(output + 14, depth);
  for (U i = 16; i < 40; i += 2)
    e.Write16(output + i, 0);
  if (depth > 8 || !copyPalette)
    return;
  const U count = 1u << depth;
  source += 12;
  output += 40;
  for (U i = 0; i < count; ++i) {
    if (paletteIndices) {
      e.Write16(output, e.Read16(source));
      source += 2;
      output += 2;
    } else {
      const U color = e.Read16(source) | (e.Read8(source + 2) << 16);
      e.Write32(output, color);
      source += 3;
      output += 4;
    }
  }
}
// GDI. Probe is the KERNEL627 memory-range provider, returning the
// original bad-pointer result. flags4 permits a null buffer before any read.
template <class E>
U ProbeBits(E &e, U info, U bits, U flags, U scans, U widthOverride = 0) {
  flags = Word(flags);
  scans = Word(scans);
  widthOverride = Word(widthOverride);
  if ((flags & 4) && !bits)
    return 0;
  U depth, width, height, bytes;
  if (e.Read16(info) == 12) {
    depth = e.Read16(info + 10);
    width = e.Read16(info + 4);
    height = e.Read16(info + 6);
  } else {
    const U compression = e.Read32(info + 16);
    if (compression != 0 && compression != 3) {
      bytes = e.Read32(info + 20);
      return e.ProbeBits(bits, bytes ? bytes : 1, (flags & 2) != 0);
    }
    depth = e.Read16(info + 14);
    width = e.Read32(info + 4);
    height = e.Read32(info + 8);
    if (Negative(height))
      height = 0 - height;
  }
  if ((flags & 0x20) && height == 1)
    bytes = (widthOverride * depth + 7) >> 3;
  else {
    bytes = ((width * depth + 31) & 0xffffffe0u) >> 3;
    if (!(flags & 8) && scans <= height)
      height = scans;
    bytes *= height;
  }
  return e.ProbeBits(bits, bytes, (flags & 2) != 0);
}
} // namespace Win98Dib
