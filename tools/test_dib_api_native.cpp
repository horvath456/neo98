// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdlib>
static int fail_after = -1;
static void *DibTestMalloc(size_t n) {
  if (fail_after == 0) {
    fail_after = -1;
    return nullptr;
  }
  if (fail_after > 0)
    --fail_after;
  return ::malloc(n);
}
#define malloc DibTestMalloc
#include "gdi.cpp"
#undef malloc
#include <cstdio>
#include <vector>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "DIB native line %d: %s\n", __LINE__, #x);               \
      return 1;                                                                \
    }                                                                          \
  } while (0)
struct Memory final : Gdi::DibIO {
  unsigned char data[0x40000]{};
  struct Block {
    Gdi::DibLease lease;
    bool used = false;
  } blocks[128];
  unsigned error = 0, allocations = 0, frees = 0;
  bool fail_lease = false, fail_output = false;
  unsigned char *Pointer(unsigned p, unsigned n) {
    if (p >= 0x10000 && p - 0x10000 <= sizeof data &&
        n <= sizeof data - (p - 0x10000))
      return data + (p - 0x10000);
    for (auto &b : blocks)
      if (b.used && p >= b.lease.guest && p - b.lease.guest <= b.lease.bytes &&
          n <= b.lease.bytes - (p - b.lease.guest))
        return b.lease.host + (p - b.lease.guest);
    return nullptr;
  }
  bool Read(unsigned p, void *out, unsigned n) override {
    auto *v = Pointer(p, n);
    if (!v)
      return false;
    memcpy(out, v, n);
    return true;
  }
  bool Write(unsigned p, const void *in, unsigned n) override {
    auto *v = Pointer(p, n);
    if (!v || (fail_output && p == 0x10100))
      return false;
    memcpy(v, in, n);
    return true;
  }
  bool Probe(unsigned p, bool) override { return Pointer(p, 1); }
  void Error(unsigned n) override { error = n; }
  void Fault(const char *s) override {
    fprintf(stderr, "DIB fault: %s\n", s);
    abort();
  }
  static void Release(void *context, const Gdi::DibLease &lease) {
    auto &m = *static_cast<Memory *>(context);
    for (auto &b : m.blocks)
      if (b.used && b.lease.guest == lease.guest) {
        b.used = false;
        free(lease.host);
        ++m.frees;
        return;
      }
    abort();
  }
  bool AllocateDib(unsigned bytes, Gdi::DibLease &lease) override {
    if (fail_lease)
      return false;
    for (unsigned i = 0; i < 128; ++i)
      if (!blocks[i].used) {
        auto *p = static_cast<unsigned char *>(calloc(1, bytes));
        if (!p)
          return false;
        lease = {0x80000000 + i * 0x100000, bytes, p, this, Release};
        blocks[i] = {lease, true};
        ++allocations;
        return true;
      }
    return false;
  }
  unsigned Call(const char *name, std::initializer_list<unsigned> list) {
    unsigned a[13]{};
    unsigned i = 0;
    for (auto n : list)
      a[i++] = n;
    return Gdi::DibApi(name, a, *this);
  }
  void W(unsigned p, unsigned v) { memcpy(data + (p - 0x10000), &v, 4); }
  unsigned R(unsigned p) {
    unsigned n = 0;
    Read(p, &n, 4);
    return n;
  }
  void Header(unsigned depth, unsigned width = 4, int height = 3,
              unsigned compression = 0) {
    memset(data, 0, 0x100);
    W(0x10000, 40);
    W(0x10004, width);
    W(0x10008, unsigned(height));
    W(0x1000c, 1 | (depth << 16));
    W(0x10010, compression);
  }
};
static int MenuMasks() {
  using namespace Gdi;
  Memory m;
  const unsigned initial = LiveObjects();
  // Original COMCTL32 paints its menu icons with DSPDxax (00E20746):
  // mask bits choose between the existing image and the background brush.
  // A monochrome DDB has DC colours, not a DIB colour table of its own.
  for (unsigned depth : {4u, 8u, 16u, 24u, 32u}) {
    m.Header(depth, 8, -1);
    if (depth <= 8) {
      const unsigned count = 1u << depth;
      for (unsigned i = 0; i < count; ++i) {
        const unsigned gray = i * 255 / (count - 1);
        m.W(0x10028 + i * 4, gray * 0x010101);
      }
    }
    const unsigned image = m.Call("CreateDIBSection", {0, 0x10000, 0, 0x10480, 0, 0});
    CHECK(image);
    const unsigned target = CreateCompatibleDC();
    const unsigned previous = SelectObject(target, image);
    unsigned char mask_bits[4] = {0x55, 0, 0, 0};
    const unsigned mask = CreateSurfaceDC({mask_bits, 8, 1, 4, 1});
    const unsigned brush = CreateSolidBrush(0xc0c0c0);
    const unsigned previous_brush = SelectObject(target, brush);
    CHECK(target && previous && mask && brush && previous_brush);
    CHECK(SetTextColor(target, 0) != ~0u);
    CHECK(SetBkColor(target, 0xffffff) != ~0u);
    auto set_pixel = [&](int x, unsigned color) {
      unsigned args[] = {target, unsigned(x), 0, color};
      return PixelApi("SetPixel", args, m);
    };
    const unsigned background = set_pixel(0, 0xc0c0c0);
    const unsigned foreground = set_pixel(0, 0x404040);
    CHECK(background != ~0u && foreground != ~0u);
    for (int x = 0; x < 8; ++x) CHECK(set_pixel(x, 0x404040) == foreground);
    CHECK(BitBlt(target, 0, 0, 8, 1, mask, 0, 0, 0x00e20746));
    for (int x = 0; x < 8; ++x) CHECK(GetPixel(target, x, 0) == (x & 1 ? background : foreground));
    CHECK(SelectObject(target, previous_brush) == brush && DeleteObject(brush));
    CHECK(SelectObject(target, previous) == image && DeleteObject(image));
    CHECK(DeleteDC(mask) && DeleteDC(target));
  }
  ShutdownBitmaps();
  CHECK(LiveObjects() == initial && m.allocations == m.frees);
  return 0;
}
static int IndexedSetDibits() {
  using namespace Gdi;
  Memory m;
  const unsigned initial = LiveObjects();
  for (unsigned depth : {4u, 8u}) {
    // Golf's original OLEAUT32 loader creates a 400x295, 8-bit section,
    // then supplies the BMP's complete colour table and bottom-up pixels.
    m.Header(depth, 400, 295);
    const unsigned entries = 1u << depth;
    for (unsigned i = 0; i < entries; ++i)
      m.W(0x10028 + 4 * i, i * 0x010101);
    const unsigned image = m.Call("CreateDIBSection", {0, 0x10000, 0, 0x10480, 0, 0});
    CHECK(image);
    const unsigned bits = m.R(0x10480), stride = 400 * depth / 8;
    for (unsigned row = 0; row < 295; ++row)
      for (unsigned x = 0; x < stride; ++x)
        m.data[0x1000 + row * stride + x] = (row + x) & 255;
    const unsigned dc = CreateCompatibleDC();
    CHECK(dc);
    CHECK(m.Call("SetDIBits", {dc, image, 0, 295, 0x11000, 0x10000, 0}) == 295);
    CHECK(!memcmp(m.Pointer(bits, stride * 295), m.data + 0x1000, stride * 295));
    memset(m.data + 0x28, 0, entries * 4);
    memset(m.data + 0x1000, 0, stride * 295);
    CHECK(m.Call("GetDIBits", {dc, image, 0, 295, 0x11000, 0x10000, 0}) == 295);
    CHECK(m.R(0x10020) == entries);
    CHECK(!memcmp(m.Pointer(bits, stride * 295), m.data + 0x1000, stride * 295));
    for (unsigned i = 0; i < entries; ++i)
      CHECK(m.R(0x10028 + 4 * i) == i * 0x010101);
    // A different source table maps colours into the target table. A
    // partial transfer leaves all other scan lines and the table unchanged.
    m.W(0x1002c, 0x020202);
    m.W(0x10030, 0x010101);
    memset(m.data + 0x1000, depth == 8 ? 1 : 0x12, stride);
    CHECK(m.Call("SetDIBits", {dc, image, 7, 1, 0x11000, 0x10000, 0}) == 1);
    const auto *stored = m.Pointer(bits, stride * 295);
    for (unsigned x = 0; x < stride; ++x)
      CHECK(stored[7 * stride + x] == (depth == 8 ? 2 : 0x21));
    CHECK(stored[6 * stride] == 6 && stored[8 * stride] == 8);
    const unsigned previous = SelectObject(dc, image);
    CHECK(previous && GetPixel(dc, 0, 295 - 8) == 0x020202);
    CHECK(SelectObject(dc, previous) == image);
    CHECK(DeleteObject(image) && DeleteDC(dc));
  }
  ShutdownBitmaps();
  CHECK(LiveObjects() == initial && m.allocations == m.frees);
  return 0;
}
int main() {
  using namespace Gdi;
  CHECK(MenuMasks() == 0);
  CHECK(IndexedSetDibits() == 0);
  Memory m;
  const unsigned initial = LiveObjects();
  m.Header(32);
  m.fail_lease = true;
  CHECK(!m.Call("CreateDIBSection", {0, 0x10000, 0, 0x10100, 0, 0}));
  CHECK(LiveObjects() == initial && m.error == 8 && m.R(0x10100) == 0);
  m.fail_lease = false;
  fail_after = 0;
  CHECK(!m.Call("CreateDIBSection", {0, 0x10000, 0, 0x10100, 0, 0}));
  CHECK(LiveObjects() == initial && m.allocations == 0);
  m.fail_output = true;
  CHECK(!m.Call("CreateDIBSection", {0, 0x10000, 0, 0x10100, 0, 0}));
  CHECK(m.allocations == m.frees && LiveObjects() == initial);
  m.fail_output = false;
  unsigned section = m.Call("CreateDIBSection", {0, 0x10000, 0, 0x10100, 0, 0});
  CHECK(section);
  unsigned bits = m.R(0x10100);
  CHECK(bits && m.Pointer(bits, 48));
  // The exposed pointer is physical bottom row; the selected DC sees logical
  // top row.
  unsigned physical[] = {0xff112233, 0xff445566, 0xff778899, 0xffaabbcc,
                         0x01020304, 0x02030405, 0x03040506, 0x04050607,
                         0x11224466, 0x22446688, 0x336688aa, 0x4488aacc};
  CHECK(m.Write(bits, physical, sizeof physical));
  unsigned dc = CreateCompatibleDC();
  CHECK(dc);
  unsigned old = SelectObject(dc, section);
  CHECK(old);
  CHECK(GetPixel(dc, 0, 0) == 0x00664422);
  CHECK(GetPixel(dc, 3, 2) == 0x00ccbbaa);
  CHECK(SaveDC(dc) == 1);
  CHECK(!DeleteObject(section));
  CHECK(SelectObject(dc, old) == section);
  CHECK(!DeleteObject(section));
  CHECK(RestoreDC(dc, -1));
  CHECK(SelectObject(dc, old) == section);
  CHECK(m.Call("GetDIBits", {0, section, 0, 3, 0x11000, 0x10000, 0}) == 3);
  CHECK(!memcmp(m.data + 0x1000, physical, sizeof physical));
  // One bottom scan is replaced; the other two rows and high source bytes
  // survive.
  unsigned line[] = {0xaaff0000, 0xaa00ff00, 0xaa0000ff, 0xaaffffff};
  memcpy(m.data + 0x1000, line, sizeof line);
  CHECK(m.Call("SetDIBits", {0, section, 0, 1, 0x11000, 0x10000, 0}) == 1);
  CHECK(!memcmp(m.Pointer(bits, 16), line, 16));
  CHECK(!memcmp(m.Pointer(bits + 16, 32), physical + 4, 32));
  // Query and partial GetObject use the same shared bits lease.
  m.Header(0);
  CHECK(m.Call("GetDIBits", {0, section, 0, 0, 0, 0x10000, 0}) == 3);
  CHECK(m.R(0x10004) == 4 && m.R(0x10008) == 3 && m.R(0x10014) == 48);
  CHECK(DibGetObject(section, 84, 0x11000, m) == 84 && m.R(0x11014) == bits);
  CHECK(DeleteObject(section));
  CHECK(!m.Pointer(bits, 1) && m.allocations == m.frees);
  CHECK(DeleteDC(dc));
  // Resource DIB with a nontrivial 4-bit palette becomes a real original DDB.
  alignas(4) unsigned char resource[40 + 64 + 8]{};
  unsigned *r = reinterpret_cast<unsigned *>(resource);
  r[0] = 40;
  r[1] = 2;
  r[2] = 2;
  r[3] = 1 | (4u << 16);
  r[10 + 1] = 0xff0000;
  r[10 + 2] = 0x00ff00;
  r[10 + 3] = 0x0000ff;
  r[10 + 4] = 0xffffff;
  resource[104] = 0x12;
  resource[108] = 0x34;
  unsigned image = CreateResourceBitmap(resource, sizeof resource, m);
  CHECK(image && HasOriginalBitmap(image));
  dc = CreateCompatibleDC();
  CHECK(dc);
  old = SelectObject(dc, image);
  CHECK(GetPixel(dc, 0, 0) == 0xff0000 && GetPixel(dc, 1, 0) == 0xffffff);
  CHECK(GetPixel(dc, 0, 1) == 0xff && GetPixel(dc, 1, 1) == 0xff00);
  CHECK(SelectObject(dc, old) == image);
  CHECK(DeleteObject(image));
  CHECK(DeleteDC(dc));
  CHECK(!CreateResourceBitmap(resource, sizeof resource - 1, m) &&
        m.error == 87);
  // Draw through an original region clip into a translated retained surface.
  unsigned target[64]{};
  DibEng::Bitmap view{reinterpret_cast<unsigned char *>(target), 8, 8, 32, 32};
  dc = CreateSurfaceDC(view);
  CHECK(dc);
  CHECK(SetDeviceOrigin(dc, 2, 3, false));
  CHECK(IntersectClipRect(dc, 0, 0, 2, 2) == 2);
  m.Header(32, 2, 2);
  unsigned input[] = {0x00112233, 0x00445566, 0x00778899, 0x00aabbcc};
  memcpy(m.data + 0x1000, input, sizeof input);
  CHECK(m.Call("StretchDIBits", {dc, 0, 0, 2, 2, 0, 0, 2, 2, 0x11000, 0x10000,
                                 0, SRCCOPY}) == 2);
  CHECK(target[3 * 8 + 2] == 0x778899 && target[3 * 8 + 3] == 0xaabbcc &&
        target[4 * 8 + 2] == 0x112233 && target[4 * 8 + 3] == 0x445566);
  CHECK(target[0] == 0 && target[3 * 8 + 4] == 0);
  // A stretch with another raster operation samples like the SRCCOPY one and
  // combines each sample with the old pixel; it used to answer 50.
  {
    unsigned before[4][4];
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 4; ++x)
        before[y][x] = target[(3 + y) * 8 + 2 + x];
    CHECK(m.Call("StretchDIBits",
                 {dc, 0, 0, 4, 4, 0, 0, 2, 2, 0x11000, 0x10000, 0, SRCINVERT}) == 2);
    // The clip of 2 by 2 keeps the upper left quarter; there every target
    // pixel samples source (0,0), the top row of the bottom-up input.
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 4; ++x)
        CHECK(target[(3 + y) * 8 + 2 + x] ==
              (x < 2 && y < 2 ? before[y][x] ^ input[2] : before[y][x]));
  }
  CHECK(DeleteDC(dc));
  // Table operations apply to the selected section, not an arbitrary bitmap.
  m.Header(4, 2, -2);
  for (unsigned i = 0; i < 16; ++i)
    m.W(0x10028 + i * 4, i * 0x010101);
  section = m.Call("CreateDIBSection", {0, 0x10000, 0, 0x10100, 0, 0});
  CHECK(section);
  dc = CreateCompatibleDC();
  old = SelectObject(dc, section);
  m.W(0x11000, 0xaa123456);
  CHECK(m.Call("SetDIBColorTable", {dc, 3, 1, 0x11000}) == 1);
  CHECK(m.Call("GetDIBColorTable", {dc, 3, 1, 0x11100}) == 1 &&
        m.R(0x11100) == 0x123456);
  unsigned palette_bits = m.R(0x10100);
  unsigned char indices[] = {0x34, 0, 0, 0, 0x43, 0, 0, 0};
  CHECK(m.Write(palette_bits, indices, sizeof indices));
  CHECK(GetPixel(dc, 0, 0) == 0x563412);
  unsigned colored[4]{};
  DibEng::Bitmap colored_view{reinterpret_cast<unsigned char *>(colored), 2, 2,
                              8, 32};
  unsigned color_dc = CreateSurfaceDC(colored_view);
  CHECK(color_dc && BitBlt(color_dc, 0, 0, 2, 2, dc, 0, 0, SRCCOPY));
  CHECK((colored[0] & 0xffffff) == 0x123456 &&
        (colored[1] & 0xffffff) == 0x040404);
  CHECK(DeleteDC(color_dc));
  CHECK(SelectObject(dc, old) == section);
  CHECK(DeleteObject(section));
  CHECK(DeleteDC(dc));
  // The public 32-bit constructor leaves an unaligned-offset output alone.
  m.Header(32);
  m.W(0x10100, 0x12345678);
  CHECK(!m.Call("CreateDIBSection", {0, 0x10000, 0, 0x10100, 0, 1}) &&
        m.R(0x10100) == 0x12345678);
  CHECK(!m.Call("CreateDIBSection", {0, 0x10000, 0, 0xfffffffc, 0, 0}) &&
        m.error == 87);
  CHECK(!m.Call("CreateDIBSection", {0, 0x10000, 0, 0x10100, 1, 0}) &&
        m.R(0x10100) == 0 && m.error == 50);
  unsigned created = m.Call("CreateDIBitmap", {0, 0x10000, 6, 0, 0x10000, 0});
  CHECK(created && m.error == 87);
  CHECK(DeleteObject(created));
  // The original one-scan ToDevice probe accepts an unpadded 24-bit pixel.
  unsigned tight_pixel = 0;
  DibEng::Bitmap tight_view{reinterpret_cast<unsigned char *>(&tight_pixel), 1,
                            1, 4, 32};
  unsigned tight_dc = CreateSurfaceDC(tight_view);
  m.Header(24, 1, 1);
  m.data[65533] = 0x12;
  m.data[65534] = 0x34;
  m.data[65535] = 0x56;
  CHECK(m.Call("SetDIBitsToDevice",
               {tight_dc, 0, 0, 1, 1, 0, 0, 0, 1, 0x1fffd, 0x10000, 0}) == 1);
  CHECK((tight_pixel & 0xffffff) == 0x563412);
  CHECK(DeleteDC(tight_dc));
  // Narrow, tall atlases are valid DIBs: Half-Life's buttons use 5382 rows.
  // Copy a strip far below row 4096, in both bitmap orientations.
  unsigned atlas_pixels[4]{};
  const unsigned atlas_dc = CreateSurfaceDC({reinterpret_cast<unsigned char *>(atlas_pixels), 2, 2, 8, 32});
  for (int height : {5382, -5382, 32767, -32767}) {
    const unsigned rows = unsigned(height < 0 ? -height : height);
    m.Header(8, 4, height);
    for (unsigned i = 0; i < 256; ++i) m.W(0x10028 + i * 4, i == 1 ? 0x00ff00 : 0xff0000);
    memset(m.data + 0x2000, 0, rows * 4);
    const unsigned strip = rows - 100;
    const unsigned memory_row = height > 0 ? strip : rows - strip - 2;
    memset(m.data + 0x2000 + memory_row * 4, 1, 8);
    CHECK(m.Call("SetDIBitsToDevice", {atlas_dc, 0, 0, 2, 2, 1, strip, 0, rows, 0x12000, 0x10000, 0}) == rows);
    for (auto pixel : atlas_pixels) CHECK((pixel & 0xffffff) == 0x00ff00);
  }
  m.Header(8, 4, 32768);
  CHECK(!m.Call("SetDIBitsToDevice", {atlas_dc, 0, 0, 2, 2, 0, 0, 0, 32768, 0x12000, 0x10000, 0}) && m.error == 87);
  m.Header(32, 32767, 32767);
  CHECK(!m.Call("SetDIBitsToDevice", {atlas_dc, 0, 0, 2, 2, 0, 0, 0, 32767, 0x12000, 0x10000, 0}) && m.error == 87);
  CHECK(DeleteDC(atlas_dc));
  // Full handle table rolls back a successfully allocated page lease.
  std::vector<unsigned> held;
  while (auto h = CreateSolidBrush(0))
    held.push_back(h);
  unsigned before = m.allocations - m.frees;
  m.Header(32);
  CHECK(!m.Call("CreateDIBSection", {0, 0x10000, 0, 0x10100, 0, 0}));
  CHECK(m.allocations - m.frees == before);
  for (auto h : held)
    CHECK(DeleteObject(h));
  for (unsigned i = 0; i < 100; ++i) {
    m.Header(32, 17, (i & 1) ? 11 : -11);
    unsigned h = m.Call("CreateDIBSection", {0, 0x10000, 0, 0x10100, 0, 0});
    CHECK(h && DeleteObject(h));
  }
  ShutdownBitmaps();
  CHECK(LiveObjects() == initial && m.allocations == m.frees && dib_bytes == 0);
  puts("DIB native PASS: shared bits, orientation, partial scans, original DDB "
       "resources, clipping, palette, lifetime, OOM");
  return 0;
}
