// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Full original GDI32 CreateDIBSection and
// CreateDIBitmap. Win16 thunks and mapped memory are providers.
namespace Win98DibPublic {
using U = unsigned;
template <class E>
U CreateSection(E &e, U dc, U info, U usage, U output, U section, U offset) {
  if (!output)
    output = e.ScratchWord();
  if (offset & 3)
    return 0;
  if (section) {
    U base = e.MapView(section, 2, 0, offset & 0xffff0000u, 0);
    e.Write32(output, base);
    if (!base)
      return 0;
    e.Write32(output, base + (offset & 65535));
  }
  const U bitmap =
      e.CreateSection16(dc, info, usage, output, section, offset, 1);
  if (!bitmap) {
    if (section) {
      const U base = e.Read32(output) - (offset & 65535);
      e.Write32(output, base);
      e.UnmapView(base);
    }
    e.Write32(output, 0);
  }
  return bitmap;
}
template <class E>
U CreateBitmap(E &e, U dc, U header, U flags, U bits, U info, U usage) {
  if (!(flags & 2))
    return e.CreateBitmap16(dc, header, flags, bits, info, usage);
  U bitmap = CreateSection(e, dc, info, usage, 0, 0, 0);
  if (bitmap && (flags & 4))
    e.SetDibits(dc, bitmap, 0, e.Read32(info + 8), bits, info, usage);
  return bitmap;
}
} // namespace Win98DibPublic
