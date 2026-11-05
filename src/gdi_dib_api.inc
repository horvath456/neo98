// SPDX-License-Identifier: GPL-2.0-or-later
// Native DIB driver/object provider. Original descriptor, color and public
// constructor decisions are in win98_dib_*.h; ICM/RLE/palette realization fail.
namespace {
constexpr unsigned DibLimit = 128u << 20;
unsigned dib_bytes = 0;
struct DibFormat {
  unsigned char info[1080]{}, device[68]{};
  unsigned width = 0, height = 0, stride = 0, bytes = 0, depth = 0, colors = 0,
           palette = 40;
  // 1 for BI_RLE8 and 2 for BI_RLE4; the geometry above describes the expanded
  // image, not the wire image.
  unsigned rle = 0;
  bool bottom_up = false, rgb565 = false;
  // For DIB_PAL_COLORS on indexed targets, xlate[source_index] gives the
  // destination palette slot. Without translation, the blitter resolves the
  // nearest color. See gdi_dib_palette.inc.
  bool translate = false;
  unsigned short xlate[256]{};
  unsigned Read8(unsigned p) const {
    if (p >= 0x100 && p < 0x100 + sizeof info)
      return info[p - 0x100];
    if (p >= 0x1000 && p < 0x1000 + sizeof device)
      return device[p - 0x1000];
    return 0;
  }
  // Whole fields inside one of the two buffers are copied at once; the byte
  // accessors keep the answers at the edges (zero outside, writes dropped).
  unsigned char *Field(unsigned p, unsigned n) {
    if (p >= 0x100 && p - 0x100 + n <= sizeof info) return info + (p - 0x100);
    if (p >= 0x1000 && p - 0x1000 + n <= sizeof device) return device + (p - 0x1000);
    return nullptr;
  }
  const unsigned char *Field(unsigned p, unsigned n) const {
    return const_cast<DibFormat *>(this)->Field(p, n);
  }
  unsigned Read16(unsigned p) const {
    if (const unsigned char *f = Field(p, 2)) return f[0] | (unsigned)f[1] << 8;
    return Read8(p) | (Read8(p + 1) << 8);
  }
  unsigned Read32(unsigned p) const {
    if (const unsigned char *f = Field(p, 4))
      return f[0] | (unsigned)f[1] << 8 | (unsigned)f[2] << 16 | (unsigned)f[3] << 24;
    return Read16(p) | (Read16(p + 2) << 16);
  }
  void Write8(unsigned p, unsigned v) {
    if (p >= 0x100 && p < 0x100 + sizeof info)
      info[p - 0x100] = v;
    else if (p >= 0x1000 && p < 0x1000 + sizeof device)
      device[p - 0x1000] = v;
  }
  void Write16(unsigned p, unsigned v) {
    if (unsigned char *f = Field(p, 2)) { f[0] = v; f[1] = v >> 8; return; }
    Write8(p, v);
    Write8(p + 1, v >> 8);
  }
  void Write32(unsigned p, unsigned v) {
    if (unsigned char *f = Field(p, 4)) { f[0] = v; f[1] = v >> 8; f[2] = v >> 16; f[3] = v >> 24; return; }
    Write16(p, v);
    Write16(p + 2, v >> 16);
  }
};
struct DibRecord {
  DibFormat format;
  DibLease lease;
  // Only the colour table of a device bitmap on a palettised display
  // (AttachDisplayColors): the object stays a DDB for GetObject.
  bool colours_only = false;
};
bool DeviceSurface(const Object &o) { return o.dib && o.dib->colours_only; }
void DestroyDib(DibRecord *record) {
  if (!record)
    return;
  dib_bytes -= record->lease.bytes;
  if (record->lease.release)
    record->lease.release(record->lease.context, record->lease);
  record->~DibRecord();
  free(record);
}
struct CoreInfoProvider {
  const unsigned char *source;
  DibFormat &target;
  unsigned Read8(unsigned p) { return source[p - 0x2000]; }
  unsigned Read16(unsigned p) { return Read8(p) | (Read8(p + 1) << 8); }
  unsigned Read32(unsigned p) { return Read16(p) | (Read16(p + 2) << 16); }
  void Write16(unsigned p, unsigned v) { target.Write16(p, v); }
  void Write32(unsigned p, unsigned v) { target.Write32(p, v); }
};
#include "gdi_dib_palette.inc"
bool ReadDibFormat(BitmapIO &io, unsigned address, unsigned usage, DibFormat &f,
                   unsigned *wire_bytes = nullptr, bool read_palette = true,
                   unsigned get_masks = 0, Context* palette_context = nullptr, bool constructing = false) {
  usage &= 65535;
  if (usage > 1) {
    io.Error(50);
    return false;
  }
  unsigned char header[12];
  if (!address || !io.Read(address, header, sizeof header)) {
    io.Error(87);
    return false;
  }
  const unsigned size = unsigned(header[0]) | (unsigned(header[1]) << 8) |
                        (unsigned(header[2]) << 16) |
                        (unsigned(header[3]) << 24);
  if (size == 12) {
    const unsigned depth = unsigned(header[10]) | (unsigned(header[11]) << 8);
    if (depth != 1 && depth != 4 && depth != 8 && depth != 16 && depth != 24 &&
        depth != 32) {
      io.Error(87);
      return false;
    }
    unsigned char original[12 + 256 * 3];
    unsigned n = 12 + (read_palette && depth <= 8 ? (1u << depth) * (usage?2:3) : 0);
    if (!io.Read(address, original, n)) {
      io.Error(87);
      return false;
    }
    CoreInfoProvider e{original, f};
    Win98Dib::ExpandCore(e, 0x2000, 0x100, usage==1, read_palette);
    if (wire_bytes)
      *wire_bytes = n;
  } else if (size == 40 || size == 108 || size == 124) {
    if (!io.Read(address, f.info, 40)) {
      io.Error(87);
      return false;
    }
    const unsigned depth = f.Read16(0x10e), compression = f.Read32(0x110),
                   used = f.Read32(0x120);
    // BI_RLE8 pairs with eight bits and BI_RLE4 with four; DIBENG accepts a
    // compression up to 2 in its transfer descriptor and GDI's source
    // builder limits the depth. Any other pairing keeps error 50.
    const bool rle =
        (compression == 1 && depth == 8) || (compression == 2 && depth == 4);
    if (compression != 0 && compression != 3 && !rle) {
      io.Error(50);
      return false;
    }
    f.rle = rle ? compression : 0;
    if (depth != 1 && depth != 4 && depth != 8 && depth != 16 && depth != 24 &&
        depth != 32) {
      io.Error(87);
      return false;
    }
    f.colors = depth <= 8 ? (used ? used : 1u << depth) : 0;
    if (f.colors > (depth <= 8 ? 1u << depth : 0)) {
      io.Error(87);
      return false;
    }
    f.palette = compression == 3 ? 52 : 40;
    const unsigned n = f.palette + (read_palette ? f.colors * (usage?2:4) : 0);
    if (n > sizeof f.info || !io.Read(address, f.info, n)) {
      io.Error(87);
      return false;
    }
    if (compression == 3 && get_masks) {
      const unsigned depth = f.Read16(0x10e);
      if (depth == 16) {
        f.Write32(0x128, get_masks == 2 ? 0xf800 : 0x7c00);
        f.Write32(0x12c, get_masks == 2 ? 0x7e0 : 0x3e0);
        f.Write32(0x130, 31);
      } else if (depth == 32) {
        f.Write32(0x128, 0xff0000);
        f.Write32(0x12c, 0xff00);
        f.Write32(0x130, 255);
      }
    }
    if (wire_bytes)
      *wire_bytes = n;
  } else {
    io.Error(size == 108 || size == 124 ? 50 : 87);
    return false;
  }
  f.depth = f.Read16(0x10e);
  if(size==12)f.colors=f.depth<=8?1u<<f.depth:0;
  if(usage&&read_palette&&!ResolveDibPalette(io,f,palette_context,constructing))return false;
  f.width = f.Read32(0x104);
  const unsigned raw_height = f.Read32(0x108);
  f.bottom_up = !Win98Dib::Negative(raw_height);
  f.height = f.bottom_up ? raw_height : 0 - raw_height;
  // The native renderer has bounded surfaces; original wrapping arithmetic is
  // still executed by CreateDevice and independently oracle-tested. The width
  // and height use the positive 16-bit range of the DIB engine. OPENGL32
  // builds a 16384-pixel span; Half-Life uses a 5382-row button
  // atlas. The byte limit below still bounds the total allocation.
  if (!f.width || f.width > 32767 || !f.height || f.height > 32767 ||
      f.Read16(0x10c) != 1) {
    io.Error(87);
    return false;
  }
  if (f.rle) {
    // DIBENG refuses a compressed descriptor, so there is no device to ask for
    // the stride; Validate also rules out a compressed top down DIB.
    if (!f.bottom_up) {
      io.Error(87);
      return false;
    }
    f.stride = ((f.width * f.depth + 31) & ~31u) >> 3;
    f.bytes = f.stride * f.height;
    f.rgb565 = false;
    if (!f.bytes || f.bytes > DibLimit) {
      io.Error(87);
      return false;
    }
    return true;
  }
  f.bytes = Win98Dib::CreateDevice(f, 0x100, 0x1000, 0, 4);
  f.stride = f.Read32(0x100e);
  if (Win98Dib::Negative(f.stride))
    f.stride = 0 - f.stride;
  f.rgb565 = (f.Read16(0x101c) & 0x40) != 0;
  if (!f.bytes || f.bytes > DibLimit || f.bytes != f.stride * f.height) {
    io.Error(87);
    return false;
  }
  if (size == 12)
    f.colors = f.depth <= 8 ? 1u << f.depth : 0;
  return true;
}
unsigned DibBufferBytes(DibFormat &format, unsigned flags, unsigned scans,
                        unsigned width = 0) {
  struct SizeProvider {
    DibFormat &format;
    unsigned bytes = 0;
    unsigned Read16(unsigned p) { return format.Read16(p); }
    unsigned Read32(unsigned p) { return format.Read32(p); }
    unsigned ProbeBits(unsigned, unsigned n, bool) {
      bytes = n;
      return 0;
    }
  } e{format};
  Win98Dib::ProbeBits(e, 0x100, 1, flags, scans, width);
  return e.bytes;
}
DibEng::Bitmap DibView(DibFormat &format, unsigned char *bits, unsigned rows) {
  return {bits + (format.bottom_up ? (rows - 1) * format.stride : 0),
          int(format.width), int(rows),
          format.bottom_up ? -int(format.stride) : int(format.stride),
          int(format.depth)};
}
unsigned DibColor(DibFormat &format, const DibEng::Bitmap &bitmap, int x,
                  int y) {
  return Win98DibColor::ToColor(format, 0x100 + format.palette, format.depth,
                                format.rgb565, DibEng::ReadPixel(bitmap, x, y));
}
unsigned DibEncode(unsigned color, unsigned depth, bool rgb565,
                   bool palette_source = false) {
  if (depth == 1) {
    unsigned sum = (color & 255) + ((color >> 16) & 255) + ((color >> 8) & 255);
    return sum >= 383;
  }
  if (depth == 16) {
    if (palette_source)
      return Win98DibColor::FromColor16(color, rgb565);
    const unsigned r = (color & 255) >> 3,
                   g = ((color >> 8) & 255) >> (rgb565 ? 2 : 3),
                   b = ((color >> 16) & 255) >> 3;
    return (r << (rgb565 ? 11 : 10)) | (g << 5) | b;
  }
  return Win98DibColor::Swap(color);
}
unsigned DibConvert(DibFormat &source_format, const DibEng::Bitmap &source,
                    int x, int y, unsigned depth, bool rgb565) {
  if (source.depth <= 8) {
    unsigned value =
        DibEncode(DibColor(source_format, source, x, y), depth, rgb565, true);
    if (source.depth == 4 && depth == 32 && !(x & 1)) {
      const unsigned index =
          source.bits[DibEng::Offset(y) * source.stride + x / 2] & 15;
      value |=
          (source_format.Read32(0x100 + source_format.palette + index * 4) &
           255)
          << 24;
    }
    return value;
  }
  const unsigned value = DibEng::ReadPixel(source, x, y);
  if (depth == 1)
    return Win98DibRaster::Monochrome(value, source.depth, source_format.rgb565,
                                      unsigned(x));
  return Win98DibRaster::Convert(value, source.depth, source_format.rgb565,
                                 depth, rgb565);
}
// coverage, when present, has the geometry of source and answers one for every
// pixel a run length stream actually wrote. An uncovered pixel leaves the
// target untouched, the way a delta or a short end of bitmap does under Win98.
bool CopyDibPixels(DibFormat &source_format, const DibEng::Bitmap &source,
                   DibEng::Bitmap &target, int dx, int dy, int sx, int sy,
                   int w, int h, bool target565 = false,
                   const DibEng::Bitmap *coverage = nullptr) {
  if (target.depth == 4 || target.depth == 8)
    return false;
  unsigned accumulator = 0;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      if (dx + x < 0 || dy + y < 0 || dx + x >= target.width ||
          dy + y >= target.height || sx + x < 0 || sy + y < 0 ||
          sx + x >= source.width || sy + y >= source.height)
        continue;
      if (coverage && !DibEng::ReadPixel(*coverage, sx + x, sy + y))
        continue;
      unsigned value = DibConvert(source_format, source, sx + x, sy + y,
                                  target.depth, target565);
      if (source.depth == 24 && target.depth == 32)
        value = Win98DibRaster::Load24(accumulator, value, unsigned(sx + x));
      accumulator = value;
      DibEng::WritePixel(target, dx + x, dy + y, value);
    }
  return true;
}
// mono_key: COLORREF of the source DC's background when the target is
// monochrome. GDI realises that colour on the source surface first: a
// palettised DIB yields the nearest colour-table index, direct colour the
// exact RGB. Pixels equal to it become 1, all others 0. COMCTL32 depends on
// both halves: ImageList_AddMasked remaps the colour table so the mask entry
// is white and blits with the mask colour as background, the toolbar blits a
// colour image into its mono bitmap with the background set to white.
// A DIB drawn onto a palettized device goes through the logical palette of
// the DC. GDI builds the translation when it hands a DIB to the driver:
// Windows 98 finds for every colour of the table the nearest entry of the DC's
// palette and takes the slot it is realized on; for a source of more
// than eight bits Windows 98 builds a 32K inverse table over the same palette, indexed by five bits per channel. With the default palette that
// is the twenty static colours. DeviceTranslation holds that translation.
struct DeviceTranslation {
  const Win98GdiPalette::Palette *palette = nullptr;
  unsigned mask = 255;
  unsigned short slots[256]{};
  unsigned Slot(unsigned color) const {
    unsigned index = Win98GdiPalette::Nearest(*palette, color & 0xffffff);
    if (index >= palette->count) index = 0;
    return palette->entries[index].mapping[1] & mask;
  }
  void Table(DibFormat &format, unsigned depth) {
    const unsigned entries = 1u << depth;
    for (unsigned i = 0; i < entries; ++i)
      slots[i] = (unsigned short)Slot(Win98DibColor::ToColor(format, 0x100 + format.palette, format.depth, format.rgb565, i));
  }
  // the colour is looked up by its upper five bits per channel.
  unsigned Inverse(unsigned color) {
    static unsigned char cache[32768];
    static bool known[32768];
    static const Win98GdiPalette::Palette *owner = nullptr;
    static unsigned signature = 0;
    unsigned sum = palette->count;
    for (unsigned i = 0; i < palette->count; ++i)
      sum = sum * 31 + palette->entries[i].color + palette->entries[i].mapping[1] * 7;
    if (owner != palette || signature != sum) { memset(known, 0, sizeof known); owner = palette; signature = sum; }
    const unsigned r = (color & 255) >> 3, g = ((color >> 8) & 255) >> 3, b = ((color >> 16) & 255) >> 3;
    const unsigned key = (r << 10) | (g << 5) | b;
    if (!known[key]) {
      cache[key] = (unsigned char)Slot((r << 3 | r >> 2) | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2) << 16);
      known[key] = true;
    }
    return cache[key];
  }
};
bool BlitDib(Context &context, DibFormat &format, const DibEng::Bitmap &source,
             const RECT &area, int sx, int sy, unsigned rop,
             const unsigned *mono_key = nullptr,
             const DibEng::Bitmap *coverage = nullptr, bool source_is_dib = true) {
  auto *object = Find(context.current.bitmap, BitmapObject);
  auto &target = object->bitmap;
  // A palettised DIB section as the target: DIBENG realises every source
  // colour as the nearest entry of the target's colour table and runs the
  // raster operation on the indices. COMCTL32's image lists are such DIBs
  // (an 8-bit halftone section), and every toolbar strip is copied into one.
  const bool palettised_target = (target.depth == 4 || target.depth == 8 ||
      (target.depth == 1 && !mono_key)) && object->dib;
  if ((target.depth == 4 || target.depth == 8) && !palettised_target)
    return false;
  // RealizeBrush supplies physical pixels for every supported target depth,
  // including palette indices. Pattern ROPs do not require a 32-bit target.
  // An operation without a pattern does not look at the brush: SRCINVERT
  // onto a DC that holds the hollow brush draws (the last step of Delphi's
  // TransparentStretchBlt on a canvas with bsClear).
  unsigned pattern[64];
  DibEng::Brush brush{};
  if (DibEng::UsesPattern(rop) &&
      !RealizeBrush(*Find(context.current.brush, BrushObject), context.current, pattern, brush))
    return false;
  const bool target565 = object->dib && object->dib->format.rgb565;
  // The target's colour table, read once, and a small cache of resolved
  // colours: a strip of icons repeats a few dozen colours thousands of times.
  unsigned table[256], table_entries = 0;
  // A direct-mapped cache over the full colour: gradients and photos would
  // exhaust a small list and fall back to the linear search per pixel.
  static unsigned cache_color[4096], cache_index[4096];
  static bool cache_valid[4096];
  static const DibFormat *cache_owner = nullptr;
  static unsigned cache_signature = 0;
  if (palettised_target) {
    auto &f = object->dib->format;
    table_entries = f.colors ? f.colors : 1u << target.depth;
    if (table_entries > 256) table_entries = 256;
    unsigned signature = table_entries;
    for (unsigned i = 0; i < table_entries; ++i) {
      table[i] = Win98DibColor::ToColor(f, 0x100 + f.palette, f.depth, f.rgb565, i) & 0xffffff;
      signature = signature * 31 + table[i];
    }
    // The table of a surface changes under it (a realized palette), so the
    // cache belongs to the colours, not only to the surface.
    if (cache_owner != &f || cache_signature != signature) {
      memset(cache_valid, 0, sizeof cache_valid);
      cache_owner = &f;
      cache_signature = signature;
    }
  }
  // A DIB onto a surface of the palettized display: the DC's palette decides
  // (DeviceTranslation above). A device surface as the source is the same
  // device and keeps its slots; a DIB_PAL_COLORS source has its translation.
  DeviceTranslation device;
  const bool device_target = palettised_target && object->dib->colours_only && PaletteDisplay() &&
                             source_is_dib && !format.translate;
  bool device_table = false, device_inverse = false;
  if (device_target) {
    auto *palette = Find(context.current.palette, PaletteObject);
    if (palette && palette->palette.count && palette->palette.entries) {
      device.palette = &palette->palette;
      device.mask = (1u << target.depth) - 1;
      if (source.depth <= 8) { device.Table(format, unsigned(source.depth)); device_table = true; }
      else device_inverse = true;
    }
  }
  auto nearest_index = [&](unsigned color) {
    color &= 0xffffff;
    const unsigned slot = (color ^ (color >> 12)) & 4095;
    if (cache_valid[slot] && cache_color[slot] == color) return cache_index[slot];
    unsigned best = ~0u, index = 0;
    for (unsigned i = 0; i < table_entries; ++i) {
      const unsigned c = table[i];
      if (c == color) { index = i; break; }
      const int dr = int(c & 255) - int(color & 255), dg = int((c >> 8) & 255) - int((color >> 8) & 255),
                db = int((c >> 16) & 255) - int((color >> 16) & 255);
      const unsigned d = unsigned(dr * dr + dg * dg + db * db);
      if (d < best) { best = d; index = i; }
    }
    cache_valid[slot] = true; cache_color[slot] = color; cache_index[slot] = index;
    return index;
  };
  auto swap = [](unsigned v) { return ((v & 255) << 16) | (v & 0xff00) | ((v >> 16) & 255); };
  // The key realised on a palettised source: the colour-table index nearest
  // to the background colour.
  unsigned key_index = 0;
  if (mono_key && target.depth == 1 && source.depth <= 8) {
    const unsigned entries = format.colors ? format.colors : 1u << source.depth;
    unsigned best = ~0u;
    for (unsigned i = 0; i < entries && i < 256; ++i) {
      const unsigned c = Win98DibColor::ToColor(format, 0x100 + format.palette, format.depth, format.rgb565, i);
      const int dr = int(c & 255) - int(*mono_key & 255), dg = int((c >> 8) & 255) - int((*mono_key >> 8) & 255),
                db = int((c >> 16) & 255) - int((*mono_key >> 16) & 255);
      const unsigned d = unsigned(dr * dr + dg * dg + db * db);
      if (d < best) { best = d; key_index = i; }
    }
  }
  RECT bounded, bounds{0, 0, target.width, target.height};
  if (!Intersect(bounded, area, bounds))
    return true;
  // A palettised source has at most 256 colours, and the colour table does not
  // change during the blit: its colours (DibColor) and their encodings in the
  // target (DibConvert) are looked up once per index, not read out of the
  // colour table for every pixel. Same values as the per-pixel calls.
  // Filled on first use: a sprite of a few pixels needs a few entries only.
  unsigned source_colors[256], source_encoded[256];
  unsigned char source_known[256] = {};
  const bool source_lookup = source.depth == 1 || source.depth == 4 || source.depth == 8;
  const bool encoded_lookup = source_lookup && !(source.depth == 4 && target.depth == 32);
  auto source_color = [&](unsigned index) {
    index &= 255;
    if (!(source_known[index] & 1)) {
      source_colors[index] = Win98DibColor::ToColor(format, 0x100 + format.palette, format.depth, format.rgb565, index);
      source_known[index] |= 1;
    }
    return source_colors[index];
  };
  auto source_encoding = [&](unsigned index) {
    index &= 255;
    if (!(source_known[index] & 2)) {
      source_encoded[index] = DibEncode(source_color(index), target.depth, target565, true);
      source_known[index] |= 2;
    }
    return source_encoded[index];
  };
  const unsigned rop_index = (rop >> 16) & 255;
  for (unsigned i = 0; i < context.current.clips; ++i) {
    RECT clip;
    if (!Intersect(clip, bounded, ClipAt(context.current, i)))
      continue;
    unsigned accumulator = 0;
    for (int y = clip.top; y < clip.bottom; ++y)
      for (int x = clip.left; x < clip.right; ++x) {
        const int px = sx + x - area.left, py = sy + y - area.top;
        if (px < 0 || py < 0 || px >= source.width || py >= source.height)
          continue;
        if (coverage && !DibEng::ReadPixel(*coverage, px, py))
          continue;
        unsigned pixel;
        if (device_table)
          pixel = device.slots[DibEng::ReadPixel(source, px, py) & 255];
        else if (device_inverse)
          pixel = device.Inverse(swap(Win98DibRaster::To32(DibEng::ReadPixel(source, px, py), source.depth, format.rgb565)));
        else if (palettised_target && format.translate &&
            (source.depth == 4 || source.depth == 8)) {
          // Write the translated index directly without a color search. An
          // identity mapping has the same effect as omitting translation.
          pixel = format.xlate[DibEng::ReadPixel(source, px, py) & 255];
        } else if (palettised_target && target.depth == 1 && source.depth > 8) {
          // The direct-colour DIBENG conversion uses its monochrome
          // intensity threshold, even when the section has a colour table.
          pixel = DibConvert(format, source, px, py, 1, false);
        } else if (palettised_target) {
          unsigned color;
          if (source.depth == 1 && !source_is_dib)
            color = swap(DibEng::ReadPixel(source, px, py) ? context.current.text_mode.background
                                                            : context.current.text_mode.foreground);
          else if (source_lookup)
            color = source_color(DibEng::ReadPixel(source, px, py));
          else if (source.depth <= 8)
            color = DibColor(format, source, px, py);
          else
            color = swap(Win98DibRaster::To32(DibEng::ReadPixel(source, px, py), source.depth, format.rgb565));
          pixel = nearest_index(color);
        } else if (target.depth == 1 && mono_key) {
          if (source.depth <= 8)
            pixel = DibEng::ReadPixel(source, px, py) == key_index;
          else {
            const unsigned v = Win98DibRaster::To32(DibEng::ReadPixel(source, px, py), source.depth, format.rgb565);
            const unsigned color = ((v & 255) << 16) | (v & 0xff00) | ((v >> 16) & 255);
            pixel = (color & 0xffffff) == (*mono_key & 0xffffff);
          }
        } else if (encoded_lookup)
          pixel = source_encoding(DibEng::ReadPixel(source, px, py));
        else
          pixel = DibConvert(format, source, px, py, target.depth, target565);
        if (source.depth == 24 && target.depth == 32)
          pixel = Win98DibRaster::Load24(accumulator, pixel, unsigned(px));
        if (rop_index == 0xcc) {
          // SRCCOPY: the operation is the source itself.
          accumulator = pixel;
          DibEng::WritePixel(target, x, y, accumulator);
          continue;
        }
        const unsigned pat =
            brush.pattern ? brush.pattern[(y & 7) * 8 + (x & 7)] : brush.color;
        accumulator = DibEng::ApplyRop(rop_index, pixel,
                                       DibEng::ReadPixel(target, x, y), pat);
        DibEng::WritePixel(target, x, y, accumulator);
      }
  }
  return true;
}
// A DIB written into a bitmap of the palettized display (SetDIBits,
// CreateDIBitmap with CBM_INIT): the same translation through the DC's
// palette as a DIB drawn onto the screen (DeviceTranslation), because GDI
// builds it the same way for both before the driver copies.
bool CopyDibToDevice(DibFormat &format, const DibEng::Bitmap &source, DibEng::Bitmap &target,
                     int dx, int dy, int sx, int sy, int w, int h, unsigned palette,
                     const DibEng::Bitmap *coverage = nullptr) {
  auto *object = Find(palette, PaletteObject);
  if (!object || !object->palette.count || !object->palette.entries) return false;
  DeviceTranslation t;
  t.palette = &object->palette;
  t.mask = (1u << target.depth) - 1;
  const bool table = source.depth <= 8 && !format.translate;
  if (table) t.Table(format, unsigned(source.depth));
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      if (dx + x < 0 || dy + y < 0 || dx + x >= target.width || dy + y >= target.height ||
          sx + x < 0 || sy + y < 0 || sx + x >= source.width || sy + y >= source.height)
        continue;
      if (coverage && !DibEng::ReadPixel(*coverage, sx + x, sy + y)) continue;
      const unsigned value = DibEng::ReadPixel(source, sx + x, sy + y);
      unsigned pixel;
      if (source.depth <= 8 && format.translate) pixel = format.xlate[value & 255] & t.mask;
      else if (table) pixel = t.slots[value & 255];
      else {
        const unsigned v = Win98DibRaster::To32(value, unsigned(source.depth), format.rgb565);
        pixel = t.Inverse(((v & 255) << 16) | (v & 0xff00) | ((v >> 16) & 255));
      }
      DibEng::WritePixel(target, dx + x, dy + y, pixel);
    }
  return true;
}
struct DibTemporary {
  unsigned char *data = nullptr;
  ~DibTemporary() { free(data); }
  bool Allocate(unsigned n) {
    data = static_cast<unsigned char *>(malloc(n));
    return data != nullptr;
  }
};
// The expanded scanlines of a BI_RLE4 or BI_RLE8 wire image and the coverage
// its runs leave behind. Both carry the wire row order, so the views below are
// built exactly like the view of an uncompressed bottom up DIB.
struct DibRleImage {
  DibTemporary pixels, cover;
  DibEng::Bitmap view{}, coverage{};
  bool Build(BitmapIO &io, DibFormat &format, const unsigned char *stream,
             unsigned bytes) {
    const unsigned area = format.width * format.height;
    if (!pixels.Allocate(format.bytes) || !cover.Allocate(area)) {
      io.Error(8);
      return false;
    }
    memset(pixels.data, 0, format.bytes);
    memset(cover.data, 0, area);
    const Win98DibRle::Target target{pixels.data, cover.data, format.width,
                                     format.height, format.stride,
                                     format.depth};
    if (!Win98DibRle::Decode(stream, bytes, target)) {
      io.Error(50);
      return false;
    }
    view = DibView(format, pixels.data, format.height);
    coverage = {cover.data + (format.height - 1) * format.width,
                int(format.width), int(format.height), -int(format.width), 8};
    return true;
  }
};
unsigned CreateDibDdb(BitmapIO &io, unsigned dc, unsigned header,
                      unsigned flags, unsigned bits, unsigned info,
                      unsigned usage) {
  if ((usage & 65535) > 1) {
    io.Error(87);
    return 0;
  }
  // Win98 probes a supplied BITMAPINFO even without CBM_INIT. A null
  // descriptor is allowed on that path, an inaccessible one is not.
  if (info && !io.Probe(info, false)) return 0;
  if (dc && !DC(dc)) {
    io.Error(87);
    return 0;
  }
  DibFormat geometry;
  if (!ReadDibFormat(io, header, usage, geometry, nullptr, false))
    return 0;
  const auto *context = DC(dc);
  // Without a DC the bitmap is one of the display, as for a resource bitmap.
  const unsigned depth =
      context ? Find(context->current.bitmap, BitmapObject)->bitmap.depth
              : PaletteDisplay() ? unsigned(display_depth) : 32;
  // A palettized target is a bitmap of the palettized display: its pixels are
  // slots of the system palette. Any other one has no table to write into.
  const bool device = (depth == 4 || depth == 8) && PaletteDisplay() && int(depth) == display_depth;
  if ((depth == 4 || depth == 8) && !device) {
    io.Error(50);
    return 0;
  }
  DibFormat source;
  DibTemporary input;
  DibRleImage expanded;
  if (flags & 4) {
    if (!ReadDibFormat(io, info, usage, source,nullptr,true,0,DC(dc)))
      return 0;
    // A compressed DDB initializer needs a nonempty declared wire image.
    // The one-byte probing fallback is not a successful image initializer.
    if (source.rle && !source.Read32(0x114)) return 0;
    const unsigned wire = DibBufferBytes(source, 8, 0);
    if (!input.Allocate(source.rle ? wire : source.bytes)) {
      io.Error(8);
      return 0;
    }
    if (!bits || !io.Read(bits, input.data, wire)) {
      io.Error(87);
      return 0;
    }
    if (source.rle && !expanded.Build(io, source, input.data, wire))
      return 0;
  }
  const unsigned args[] = {geometry.width, geometry.height, 1, depth, 0};
  const unsigned bitmap = BitmapApi("CreateBitmap", args, io);
  if (!bitmap)
    return 0;
  if (device) AttachDisplayColors(bitmap);
  if (flags & 4 && device) {
    auto *object = Find(bitmap, BitmapObject);
    BitmapAccess access(object, true);
    auto view = source.rle ? expanded.view : DibView(source, input.data, source.height);
    if (!CopyDibToDevice(source, view, object->bitmap, 0, 0, 0, 0, int(source.width), int(source.height),
                         context ? context->current.palette : DefaultPalette(),
                         source.rle ? &expanded.coverage : nullptr)) {
      DeleteObject(bitmap);
      io.Error(50);
      return 0;
    }
  } else if (flags & 4) {
    auto *object = Find(bitmap, BitmapObject);
    BitmapAccess access(object, true);
    auto view = source.rle ? expanded.view
                           : DibView(source, input.data, source.height);
    if (!CopyDibPixels(source, view, object->bitmap, 0, 0, 0, 0,
                       int(source.width), int(source.height), false,
                       source.rle ? &expanded.coverage : nullptr)) {
      DeleteObject(bitmap);
      io.Error(50);
      return 0;
    }
  }
  return bitmap;
}
unsigned TransferDib(BitmapIO &io, unsigned dc, unsigned bitmap, unsigned start,
                     unsigned count, unsigned bits, unsigned info,
                     unsigned usage, bool get) {
  usage &= 65535;
  auto *object = Find(bitmap, BitmapObject);
  if(get&&usage){io.Error(50);return 0;}
  if (!DC(dc)) { io.Error(87); return 0; }
  if (!object) {
    io.Error(87);
    return 0;
  }
  unsigned char header[40]{};
  if (!info || !io.Read(info, header, 12)) {
    io.Error(87);
    return 0;
  }
  const unsigned size = unsigned(header[0]) | (unsigned(header[1]) << 8) |
                        (unsigned(header[2]) << 16) |
                        (unsigned(header[3]) << 24);
  if (size == 40 && !io.Read(info, header, 40)) {
    io.Error(87);
    return 0;
  }
  if (get && size == 40 && !header[14] && !header[15]) {
    if (bits) return 0; // Win98 descriptor queries require a null pixel buffer.
    if (usage) {
      io.Error(50);
      return 0;
    }
    DibFormat query;
    query.Write32(0x100, 40);
    query.Write32(0x104, object->bitmap.width);
    query.Write32(0x108, object->bitmap.height);
    query.Write16(0x10c, 1);
    query.Write16(0x10e, object->bitmap.depth);
    // A bitmap of a 16-bit display answers BI_BITFIELDS; the second call
    // then fills in its masks. SDL 1.2 tells 555 from 565 this way and
    // takes 565 for anything else (Trampolin of KIDS50).
    if (object->bitmap.depth == 16 || object->bitmap.depth == 32)
      query.Write32(0x110, 3);
    const unsigned stride =
        ((unsigned(object->bitmap.width) * object->bitmap.depth + 31) & ~31u) >>
        3;
    query.Write32(0x114, stride * object->bitmap.height);
    if (!io.Write(info, query.info, 40)) {
      io.Error(87);
      return 0;
    }
    return object->bitmap.height;
  }
  // A descriptor-only query above works for every DDB. Once a bit depth is
  // supplied, the device conversion requires a monochrome or matching DDB.
  if (get && !object->dib && object->bitmap.depth != 1 &&
      object->bitmap.depth != display_depth) return 0;
  DibFormat format;
  if (!ReadDibFormat(io, info, usage, format, nullptr, true,
                     get ? (object->dib && object->dib->format.rgb565 ? 2 : 1)
                         : 0,DC(dc)))
    return 0;
  // GetDIBits into a run length header: the original refuses it outright when
  // lpvBits is null and otherwise runs the DIB engine's
  // compressor, which is not implemented here.
  if (get && format.rle) {
    io.Error(50);
    return 0;
  }
  const bool same_indexed = get && object->dib && format.depth <= 8 &&
      format.depth == unsigned(object->bitmap.depth);
  if (same_indexed) {
    const auto &table = object->dib->format;
    format.colors = 1u << format.depth;
    format.Write32(0x120, object->dib->colours_only ? 0 : format.colors);
    for (unsigned i = 0; i < format.colors; ++i)
      format.Write32(0x100 + format.palette + i * 4,
          i < table.colors ? table.Read32(0x100 + table.palette + i * 4) & 0xffffff : 0);
  }
  if (get && format.depth == 1 && !same_indexed) {
    format.colors = 2;
    format.Write32(0x120, 2);
    format.Write32(0x100 + format.palette, 0);
    format.Write32(0x104 + format.palette, 0xffffff);
  }
  start &= 65535;
  count &= 65535;
  if (format.width != unsigned(object->bitmap.width) ||
      format.height > unsigned(object->bitmap.height) ||
      start >= format.height || start & 0x8000) {
    io.Error(87);
    return 0;
  }
  if (count > format.height - start)
    count = format.height - start;
  if (get && !bits) {
    format.Write32(0x114, format.bytes);
    if (!io.Write(info, size == 12 ? header : format.info,
                  size == 12 ? 12 : format.palette + format.colors * 4))
      return 0;
    return format.height;
  }
  if (!count)
    return 0;
  if (!bits) {
    io.Error(87);
    return 0;
  }
  // Palettized output needs the original logical palette realization kernel.
  // GetDIBits into a run length header is the DIB engine's compressor; only its refusal for a null bits pointer is
  // implemented above, the compressor itself stays a boundary.
  // A bitmap of the palettized display holds slots of the system palette. Its
  // pixels go out unchanged into a DIB of the same depth, whose table is the
  // system palette, and a DIB comes in through the DC's palette
  // (CopyDibToDevice). Other palettized conversions stay a boundary.
  const bool device = PaletteDisplay() && object->dib && object->dib->colours_only &&
                      (object->bitmap.depth == 1 || object->bitmap.depth == 4 || object->bitmap.depth == 8);
  const bool device_get = get && device && format.depth == unsigned(object->bitmap.depth);
  const bool indexed_section = object->dib && !object->dib->colours_only &&
                               (object->bitmap.depth == 1 || object->bitmap.depth == 4 || object->bitmap.depth == 8);
  const bool indexed_get = get && indexed_section && format.depth == unsigned(object->bitmap.depth);
  const bool palette_get = device_get || indexed_get;
  if ((get && (format.depth == 4 || format.depth == 8) && !palette_get) ||
      (!get && (object->bitmap.depth == 4 || object->bitmap.depth == 8) && !device && !indexed_section)) {
    io.Error(50);
    return 0;
  }
  if (palette_get) {
    const auto &table = object->dib->format;
    const unsigned entries = 1u << format.depth;
    format.colors = entries;
    format.Write32(0x120, indexed_get ? entries : 0);
    for (unsigned i = 0; i < entries; ++i)
      format.Write32(0x100 + format.palette + i * 4, i < table.colors ? table.Read32(0x100 + table.palette + i * 4) : 0);
  }
  const unsigned bytes = DibBufferBytes(format, get ? 0x46 : 0x40, count);
  DibTemporary buffer;
  if (!buffer.Allocate(bytes)) {
    io.Error(8);
    return 0;
  }
  if (!io.Read(bits, buffer.data, bytes)) {
    io.Error(87);
    return 0;
  }
  // A run length stream carries the whole image, so it is expanded once and the
  // requested band is taken out of the expansion.
  DibRleImage expanded;
  if (format.rle && !expanded.Build(io, format, buffer.data, bytes))
    return 0;
  BitmapAccess access(object, !get);
  auto transfer = DibView(format, buffer.data, count);
  const int y = int(format.height - start - count);
  if (!get && device) {
    auto *context = DC(dc);
    if (!CopyDibToDevice(format, format.rle ? expanded.view : transfer, object->bitmap, 0, y, 0,
                         format.rle ? y : 0, int(format.width), int(count),
                         context ? context->current.palette : DefaultPalette(),
                         format.rle ? &expanded.coverage : nullptr)) {
      io.Error(50);
      return 0;
    }
  } else if (palette_get) {
    for (unsigned row = 0; row < count; ++row)
      for (unsigned x = 0; x < format.width; ++x)
        DibEng::WritePixel(transfer, int(x), int(row), DibEng::ReadPixel(object->bitmap, int(x), y + int(row)));
    if (!io.Write(bits, buffer.data, bytes)) {
      io.Error(87);
      return 0;
    }
    if (size != 12 && !io.Write(info, format.info, format.palette + format.colors * 4))
      return 0;
  } else if (!get && indexed_section) {
    // SetDIBits writes the bitmap independently of the caller's selected
    // surface, mapping and clipping. Use the same DIB colour-table
    // translation as BlitDib, with the bitmap's full bounds.
    // Original OLEAUT32 uses this path for Golf '98's 8-bit splash picture.
    Context destination;
    destination.current.bitmap = bitmap;
    destination.current.clip[0] = {0, 0, object->bitmap.width, object->bitmap.height};
    const RECT band{0, y, int(format.width), y + int(count)};
    if (!BlitDib(destination, format, format.rle ? expanded.view : transfer,
                 band, 0, format.rle ? y : 0, SRCCOPY, nullptr,
                 format.rle ? &expanded.coverage : nullptr)) {
      io.Error(50);
      return 0;
    }
  } else if (!get) {
    if (!CopyDibPixels(format, format.rle ? expanded.view : transfer,
                       object->bitmap, 0, y, 0, format.rle ? y : 0,
                       int(format.width), int(count),
                       object->dib && object->dib->format.rgb565,
                       format.rle ? &expanded.coverage : nullptr)) {
      io.Error(50);
      return 0;
    }
  } else {
    DibFormat source;
    if (object->dib)
      source = object->dib->format;
    else {
      source.depth = object->bitmap.depth;
      source.palette = 40;
      if (source.depth <= 8) {
        source.Write32(0x128, 0);
        source.Write32(0x12c, 0xffffff);
      }
    }
    if (!CopyDibPixels(source, object->bitmap, transfer, 0, 0, 0, y,
                       int(format.width), int(count), format.rgb565)) {
      io.Error(50);
      return 0;
    }
    if (!io.Write(bits, buffer.data, bytes)) {
      io.Error(87);
      return 0;
    }
    if (size != 12) {
      if (!io.Write(info, format.info,
                    format.palette + (format.depth == 1 ? 8 : 0)))
        return 0;
    }
  }
  return count;
}
bool ScaleDib(Context &,DibFormat &,const DibEng::Bitmap &,RECT,RECT,unsigned,bool,bool,unsigned *);
bool StretchThroughSource(Context &,DibFormat &,const DibEng::Bitmap &,RECT,RECT,unsigned,const DibEng::Bitmap *,
                          unsigned *,bool=true);
unsigned DrawDib(BitmapIO &io, unsigned dc, int x, int y, int dw, int dh,
                 int sx, int sy, int sw, int sh, unsigned bits, unsigned info,
                 unsigned usage, unsigned rop, unsigned start = 0,
                 unsigned scans = ~0u) {
  auto *context = DC(dc);
  if (!context) {
    io.Error(87);
    return 0;
  }
  DibFormat format;
  if (!ReadDibFormat(io, info, usage, format,nullptr,true,0,DC(dc)))
    return 0;
  dw = short(dw);
  dh = short(dh);
  sw = short(sw);
  sh = short(sh);
  sx = short(sx);
  sy = short(sy);
  x = short(x);
  y = short(y);
  if (dw < 0 || dh < 0 || sw < 0 || sh < 0) {
    io.Error(50);
    return 0;
  }
  if (!dw || !dh || !sw || !sh)
    return 1;
  if (!(context->current.mapping.transform_flags & 1)) {
    io.Error(50);
    return 0;
  }
  unsigned rows = format.height;
  if (scans != ~0u) {
    start &= 65535;
    scans &= 65535;
    if (start >= format.height)
      return 0;
    rows = scans < format.height - start ? scans : format.height - start;
    if (!rows)
      return 0;
  }
  // A run length stream is always the whole image: its wire size is
  // biSizeImage and no band of it can be read on its own.
  if (format.rle)
    rows = format.height;
  const unsigned input_bytes =
      DibBufferBytes(format, scans == ~0u ? 8 : 0x60, rows, unsigned(sw));
  DibTemporary input;
  if (!input.Allocate(format.rle ? input_bytes : format.stride * rows)) {
    io.Error(8);
    return 0;
  }
  if (scans != ~0u && format.height == 1 &&
      (sx < 0 || unsigned(sx + sw) > format.width ||
       unsigned(sx + sw) * format.depth > input_bytes * 8)) {
    io.Error(87);
    return 0;
  }
  // An unscaled copy of an uncompressed DIB only looks at the source rows of
  // its rectangle, the way DIBENG does. Reading the whole image instead was
  // the largest single item of the Pinball profile: a game that blits a small
  // part of a large back buffer paid for every row of it.
  // Both ends of the image are still probed, so a buffer that is too short
  // fails as before.
  const int wanted_y = (scans == ~0u ? int(format.height) : int(start + rows)) - sy - sh;
  const bool partial = !format.rle && dw == sw && dh == sh && input_bytes >= format.stride * rows && bits;
  if (partial) {
    const int first = wanted_y < 0 ? 0 : wanted_y;
    const int last = wanted_y + sh > int(rows) ? int(rows) : wanted_y + sh;
    if (!io.Probe(bits, false) || !io.Probe(bits + input_bytes - 1, false)) {
      io.Error(87);
      return 0;
    }
    if (first < last) {
      const unsigned memory_row = format.bottom_up ? rows - unsigned(last) : unsigned(first);
      const unsigned offset = memory_row * format.stride, bytes = unsigned(last - first) * format.stride;
      if (!io.Read(bits + offset, input.data + offset, bytes)) {
        io.Error(87);
        return 0;
      }
    }
  } else {
    memset(input.data, 0, format.rle ? input_bytes : format.stride * rows);
    if (!bits || !io.Read(bits, input.data, input_bytes)) {
      io.Error(87);
      return 0;
    }
  }
  DibRleImage expanded;
  if (format.rle && !expanded.Build(io, format, input.data, input_bytes))
    return 0;
  auto source = format.rle ? expanded.view : DibView(format, input.data, rows);
  auto *object = Find(context->current.bitmap, BitmapObject);
  BitmapAccess access(object, true);
  auto &target = object->bitmap;
  // A palettised DIB section is a target DIBENG can serve, because its colour
  // table says which index each source colour becomes; BlitDib below realises
  // exactly that. Only a palettised DDB, which has no table in this store, and
  // a pattern operation outside 32 bits stay out. COMCTL32's CreateMappedBitmap
  // fills its 4-bit image strip with StretchDIBits and gets a black strip
  // otherwise.
  if (((target.depth == 4 || target.depth == 8) && !object->dib) ||
      (DibEng::UsesPattern(rop) && target.depth != 32)) {
    io.Error(50);
    return 0;
  }
  const int source_y =
      (scans == ~0u || format.rle ? int(format.height) : int(start + rows)) -
      sy - sh;
  const auto origin = DevicePoint(context->current, x, y);
  if (rop & 0xff000000) {
    io.Error(50);
    return 0;
  }
  const RECT area{origin.x, origin.y, origin.x + dw, origin.y + dh};
  if(dw!=sw||dh!=sh){
    unsigned failure=50;
    // The direct scale serves one 16- or 32-bit format into the same one
    // with SRCCOPY; everything else goes through the source's own format
    // and the converting blit.
    const bool direct=!format.rle&&rop==SRCCOPY&&source.depth==target.depth&&
                      (source.depth==32||(source.depth==16&&format.rgb565==bool(object->dib&&object->dib->format.rgb565)));
    const RECT source_area{sx,source_y,sx+sw,source_y+sh};
    if(!(direct?ScaleDib(*context,format,source,area,source_area,rop,false,false,&failure)
               :StretchThroughSource(*context,format,source,area,source_area,rop,
                                     format.rle?&expanded.coverage:nullptr,&failure))){io.Error(failure);return 0;}
    return unsigned(sh);
  }
  if (!BlitDib(*context, format, source, area, sx, source_y, rop, nullptr,
               format.rle ? &expanded.coverage : nullptr)) {
    io.Error(50);
    return 0;
  }
  return scans == ~0u ? unsigned(sh) : rows;
}
struct DibConstructor {
  DibIO &io;
  unsigned temporary = 0;
  bool scratch_active = false;
  unsigned ScratchWord() {
    scratch_active = true;
    return 0xfffffffcu;
  }
  bool IsScratch(unsigned p) const {
    return scratch_active && p == 0xfffffffcu;
  }
  unsigned Read32(unsigned p) {
    if (IsScratch(p))
      return temporary;
    unsigned value = 0;
    if (!io.Read(p, &value, 4))
      io.Error(87);
    return value;
  }
  void Write32(unsigned p, unsigned v) {
    if (IsScratch(p))
      temporary = v;
    else if (!io.Write(p, &v, 4))
      io.Error(87);
  }
  unsigned MapView(unsigned, unsigned, unsigned, unsigned, unsigned) {
    io.Error(50);
    return 0;
  }
  void UnmapView(unsigned) {}
  unsigned CreateSection16(unsigned dc, unsigned info, unsigned usage,
                           unsigned output, unsigned section, unsigned,
                           unsigned) {
    if (section) {
      io.Error(50);
      return 0;
    }
    if (dc && !DC(dc)) {
      io.Error(87);
      return 0;
    }
    if (!IsScratch(output) && !io.Probe(output, true)) {
      io.Error(87);
      return 0;
    }
    DibFormat format;
    if (!ReadDibFormat(io, info, usage, format,nullptr,true,0,DC(dc),true))
      return 0;
    if (format.rle) return 0;
    const unsigned rounded = (format.bytes + 4095) & ~4095u;
    if (rounded > DibLimit - dib_bytes) {
      io.Error(8);
      return 0;
    }
    void *storage = malloc(sizeof(DibRecord));
    if (!storage) {
      io.Error(8);
      return 0;
    }
    auto *record = new (storage) DibRecord{};
    record->format = format;
    record->format.Write32(0x114, format.bytes);
    if(!usage)record->format.Write32(0x120, format.colors);
    if (!io.AllocateDib(rounded, record->lease)) {
      record->~DibRecord();
      free(record);
      io.Error(8);
      return 0;
    }
    if (!record->lease.host || !record->lease.guest ||
        record->lease.bytes != rounded || !record->lease.release) {
      if (record->lease.release)
        record->lease.release(record->lease.context, record->lease);
      record->~DibRecord();
      free(record);
      io.Error(8);
      return 0;
    }
    memset(record->lease.host, 0, rounded);
    dib_bytes += rounded;
    const unsigned handle = Allocate(BitmapObject);
    if (!handle) {
      DestroyDib(record);
      io.Error(8);
      return 0;
    }
    auto *object = Find(handle, BitmapObject);
    object->dib = record;
    object->bitmap = DibView(record->format, record->lease.host, format.height);
    const unsigned pointer = record->lease.guest;
    if (!IsScratch(output) && !io.Write(output, &pointer, 4)) {
      Free(handle);
      io.Error(87);
      return 0;
    }
    if (IsScratch(output))
      temporary = pointer;
    return handle;
  }
  unsigned CreateBitmap16(unsigned dc, unsigned header, unsigned flags,
                          unsigned bits, unsigned info, unsigned usage) {
    return CreateDibDdb(io, dc, header, flags, bits, info, usage);
  }
  unsigned SetDibits(unsigned dc, unsigned bitmap, unsigned start,
                     unsigned count, unsigned bits, unsigned info,
                     unsigned usage) {
    return TransferDib(io, dc, bitmap, start, count, bits, info, usage, false);
  }
};
} // namespace
bool IsDibApi(ApiName name){switch(name.id){API_CASE("CreateDIBitmap") API_CASE("CreateDIBSection") API_CASE("GetDIBits") API_CASE("SetDIBits") API_CASE("SetDIBitsToDevice") API_CASE("StretchDIBits") API_CASE("GetDIBColorTable") API_CASE("SetDIBColorTable") return true;default:return false;}}
unsigned DibApi(ApiName name, const unsigned *a, DibIO &io) {
  switch (name.id)
  {
  API_CASE("CreateDIBSection") {
    DibConstructor e{io};
    return Win98DibPublic::CreateSection(e, a[0], a[1], a[2], a[3], a[4], a[5]);
  }
  API_CASE("CreateDIBitmap") {
    // CBM_CREATDIB with no BITMAPINFO fails without changing LastError.
    if ((a[2] & 2) && !a[4]) return 0;
    DibConstructor e{io};
    return Win98DibPublic::CreateBitmap(e, a[0], a[1], a[2], a[3], a[4], a[5]);
  }
  API_CASE("GetDIBits") API_CASE("SetDIBits")
    return TransferDib(io, a[0], a[1], a[2], a[3], a[4], a[5], a[6],
                       name[0] == 'G');
  API_CASE("StretchDIBits")
    return DrawDib(io, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8],
                   a[9], a[10], a[11], a[12]);
  API_CASE("SetDIBitsToDevice")
    return DrawDib(io, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[3], a[4],
                   a[9], a[10], a[11], SRCCOPY, a[7], a[8]);
  default:
      break;
  }
  auto *context = DC(a[0]);
  auto *object =
      context ? Find(context->current.bitmap, BitmapObject) : nullptr;
  if (!object) { io.Error(87); return 0; }
  if (!object->dib || object->dib->format.depth > 8) return 0;
  auto &format = object->dib->format;
  unsigned start = a[1] & 65535, count = a[2] & 65535;
  // Get reports the initialized table; Set can extend it up to the depth's
  // capacity. Win98's three-entry 8-bit DIB grows to four after Set(0,4).
  const unsigned capacity = name[0] == 'G' ? format.colors : 1u << format.depth;
  if (start >= capacity)
    return 0;
  if (count > capacity - start)
    count = capacity - start;
  unsigned char temporary[1024];
  if (count && !a[3]) {
    io.Error(87);
    return 0;
  }
  if (name[0] == 'G') {
    if (!io.Write(a[3], format.info + format.palette + start * 4, count * 4))
      return 0;
  } else {
    if (!io.Read(a[3], temporary, count * 4))
      return 0;
    for (unsigned i = 0; i < count; ++i)
      temporary[i * 4 + 3] = 0;
    memcpy(format.info + format.palette + start * 4, temporary, count * 4);
    if (count && start + count > format.colors) {
      format.colors = start + count;
      format.Write32(0x120, format.colors);
    }
  }
  return count;
}
bool HasDibBitmap(unsigned h) {
  auto *object = Find(h, BitmapObject);
  return object && object->dib && !object->dib->colours_only;
}
unsigned DibBitmapBits(unsigned handle, unsigned count, unsigned buffer, bool write, BitmapIO &io) {
  auto *object = Find(handle, BitmapObject);
  if (!object || !object->dib || object->dib->colours_only) return 0;
  const auto &f = object->dib->format;
  const unsigned stride = ((f.width * f.depth + 15) & ~15u) >> 3;
  const unsigned total = stride * f.height;
  if (!buffer) return write ? 0 : total;
  if (count > total) count = total;
  for (unsigned done = 0; done < count;) {
    const unsigned row = done / stride, offset = done % stride;
    const unsigned amount = count - done < stride - offset ? count - done : stride - offset;
    auto *bits = object->dib->lease.host + row * f.stride + offset;
    if (!(write ? io.Read(buffer + done, bits, amount) : io.Write(buffer + done, bits, amount))) {
      io.Error(87); return 0;
    }
    done += amount;
  }
  return count;
}
unsigned DibGetObject(unsigned handle, unsigned count, unsigned output,
                      BitmapIO &io) {
  auto *object = Find(handle, BitmapObject);
  if (!object || !object->dib) {
    io.Error(87);
    return 0;
  }
  struct Query {
    Object &object;
    BitmapIO &io;
    bool failed = false;
    unsigned GetBitmap16(unsigned, unsigned char (&small)[84]) {
      const auto &f = object.dib->format;
      Win98BitmapQuery::Put(small, 0, 2);
      Win98BitmapQuery::Put(small + 2, f.width, 2);
      Win98BitmapQuery::Put(small + 4, f.height, 2);
      Win98BitmapQuery::Put(small + 6, f.stride,
                            2);
      small[8] = 1;
      small[9] = f.depth;
      Win98BitmapQuery::Put(small + 10, 1, 4);
      memcpy(small + 14, f.info, 40);
      if (f.Read32(0x110) == 3)
        memcpy(small + 54, f.info + 40, 12);
      return 84;
    }
    unsigned MapPointer(unsigned) { return object.dib->lease.guest; }
    void WriteGuest8(unsigned p, unsigned v) {
      unsigned char byte = v;
      if (!io.Write(p, &byte, 1)) {
        failed = true;
        io.Error(87);
      }
    }
    void Error(unsigned e) { io.Error(e); }
  } query{*object, io};
  const unsigned result = Win98BitmapQuery::Get(query, handle, count, output);
  return query.failed ? 0 : result;
}

HBITMAP CreateResourceBitmap(const unsigned char *resource, unsigned bytes,
                             BitmapIO &errors) {
  struct ResourceMemory final : BitmapIO {
    const unsigned char *resource;
    unsigned bytes;
    BitmapIO &errors;
    ResourceMemory(const unsigned char *r, unsigned n, BitmapIO &e)
        : resource(r), bytes(n), errors(e) {}
    bool Read(unsigned p, void *out, unsigned n) override {
      if (p < 0x10000 || p - 0x10000 > bytes || n > bytes - (p - 0x10000))
        return false;
      memcpy(out, resource + (p - 0x10000), n);
      return true;
    }
    bool Write(unsigned, const void *, unsigned) override { return false; }
    bool Probe(unsigned p, bool write) override {
      return !write && p >= 0x10000 && p - 0x10000 < bytes;
    }
    void Error(unsigned e) override { errors.Error(e); }
    void Fault(const char *s) override { errors.Fault(s); }
  } memory(resource, bytes, errors);
  DibFormat format;
  unsigned header_bytes = 0;
  if (!resource || !ReadDibFormat(memory, 0x10000, 0, format, &header_bytes))
    return 0;
  // GDI (Win98Dib::ProbeBits) measures a compressed descriptor by
  // biSizeImage, not by the expanded raster: an RT_BITMAP with BI_RLE4 or
  // BI_RLE8 carries only the coded bytes. format.bytes is the expanded size
  // and would reject every packed resource of that kind (SHDOC401 bitmap 309,
  // the background preview of the Display applet).
  const unsigned wire = format.rle ? DibBufferBytes(format, 8, 0) : format.bytes;
  if (header_bytes > bytes || wire > bytes - header_bytes) {
    errors.Error(87);
    return 0;
  }
  return CreateDibDdb(memory, 0, 0x10000, 4, 0x10000 + header_bytes, 0x10000,
                      0);
}
