/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// Native handle, immutable clip ownership and raster integration tests.
#ifdef REGION_OVERLAY
#include "gdi.h"
#else
#include "../src/gdi.h"
#endif
#include <cstdio>
#include <cstring>
#include <vector>
#ifdef REGION_FAIL_TEST
extern "C" void region_test_failure(unsigned);
extern "C" unsigned region_test_allocations();
#endif
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::fprintf(stderr, "region native failed line %d: %s\n", __LINE__,     \
                   #x);                                                        \
      return 1;                                                                \
    }                                                                          \
  } while (0)
struct IO : Gdi::BitmapIO {
  unsigned char memory[65536]{};
  unsigned error = 0, faults = 0;
  const char *active = nullptr;
  bool Read(unsigned p, void *d, unsigned n) override {
    if (!p || p > sizeof memory || n > sizeof memory - p)
      return false;
    memcpy(d, memory + p, n);
    return true;
  }
  bool Write(unsigned p, const void *d, unsigned n) override {
    if (!p || p > sizeof memory || n > sizeof memory - p)
      return false;
    memcpy(memory + p, d, n);
    return true;
  }
  bool Probe(unsigned p, bool) override { return p && p < sizeof memory; }
  void Error(unsigned n) override { error = n; }
  void Fault(const char *s) override {
    ++faults;
    std::fprintf(stderr, "native region fault during %s: %s\n", active, s);
  }
  unsigned Call(const char *n, unsigned a = 0, unsigned b = 0, unsigned c = 0,
                unsigned d = 0, unsigned e = 0) {
    active = n;
    unsigned v[] = {a, b, c, d, e};
    return Gdi::RegionApi(n, v, *this);
  }
  unsigned Create(int l, int t, int r, int b) {
    return Call("CreateRectRgn", l, t, r, b);
  }
};
int main() {
  IO io;
  auto a = io.Create(0, 0, 100, 100), b = io.Create(20, 20, 80, 80),
       r = io.Create(0, 0, 0, 0);
  CHECK(a && b && r);
  io.error = 0x12345678;
  CHECK(io.Call("GetClipRgn", 0, r) == ~0u && io.error == 6);
  io.error = 0x12345678;
  CHECK(io.Call("GetClipRgn", 0, 0) == ~0u && io.error == 6);
  CHECK(io.Call("CombineRgn", r, a, b, 4) == 3);
  CHECK(io.Call("GetRegionData", r, 0, 0) == 96);
  CHECK(io.Call("GetRegionData", r, 95, 0xffffffff) == 96);
  CHECK(io.Call("GetRegionData", r, 96, 4096) == 96);
  CHECK(io.Call("PtInRegion", r, 5, 5));
  CHECK(!io.Call("PtInRegion", r, 50, 50));
  CHECK(Gdi::DeleteObject(a) && Gdi::DeleteObject(b) && Gdi::DeleteObject(r));
  CHECK(!io.Call("PtInRegion", a, 5, 5));
  // ExtCreateRegion accepts cbRgnData=1 on Win98, but still needs readable
  // rectangles. Exercise scaling and a non-axis-aligned transform separately.
  unsigned input[] = {33, 1, 1, 16, 0, 0, 8, 4, 0, 0, 8, 4};
  CHECK(io.Write(512, input, sizeof input));
  auto imported = io.Call("ExtCreateRegion", 0, 1, 512);
  CHECK(imported && io.Call("PtInRegion", imported, 7, 3));
  CHECK(Gdi::DeleteObject(imported));
  float xform[] = {.5f, 0, 0, .5f, 20, 40};
  CHECK(io.Write(768, xform, sizeof xform));
  imported = io.Call("ExtCreateRegion", 768, 1, 512);
  auto expected = io.Create(20, 40, 24, 42);
  CHECK(imported && io.Call("EqualRgn", imported, expected));
  CHECK(Gdi::DeleteObject(imported) && Gdi::DeleteObject(expected));
  xform[0] = 1; xform[1] = 1; xform[3] = 1; xform[4] = 0; xform[5] = 0;
  CHECK(io.Write(768, xform, sizeof xform));
  imported = io.Call("ExtCreateRegion", 768, sizeof input, 512);
  CHECK(imported && io.Call("PtInRegion", imported, 3, 4));
  CHECK(!io.Call("PtInRegion", imported, 3, 0));
  CHECK(Gdi::DeleteObject(imported));
  input[2] = 10000000;
  CHECK(io.Write(512, input, sizeof input));
  CHECK(!io.Call("ExtCreateRegion", 768, sizeof input, 512));
  unsigned pixels[400 * 12]{};
  auto dc = Gdi::CreateSurfaceDC(
      {reinterpret_cast<unsigned char *>(pixels), 400, 12, 1600, 32});
  CHECK(dc);
  io.error = 0x12345678;
  CHECK(io.Call("GetClipRgn", dc, 0) == ~0u && io.error == 6);
  auto untouched = io.Create(0, 0, 0, 0);
  CHECK(untouched);
  io.error = 0x12345678;
  CHECK(io.Call("GetClipRgn", dc, untouched) == 0 && io.error == 0x12345678);
  CHECK(Gdi::DeleteObject(untouched));
  auto brush = Gdi::CreateSolidBrush(Gdi::RGB(255, 0, 0));
  CHECK(brush);
  auto old = Gdi::SelectObject(dc, brush);
  CHECK(old);
  unsigned data[8 + 100 * 4] = {32, 1, 100, 100 * 16, 0, 0, 299, 10};
  for (unsigned i = 0; i < 100; ++i) {
    data[8 + i * 4] = i * 3;
    data[9 + i * 4] = 0;
    data[10 + i * 4] = i * 3 + 1;
    data[11 + i * 4] = 10;
  }
  CHECK(io.Write(1024, data, sizeof data));
  auto bars = io.Call("ExtCreateRegion", 0, sizeof data, 1024);
  CHECK(bars);
  CHECK(io.Call("ExtSelectClipRgn", dc, bars, 0x10005) == 3);
  CHECK(Gdi::SaveDC(dc) == 1);
  CHECK(io.Call("SetRectRgn", bars, 0, 0, 2, 2));
  CHECK(Gdi::DeleteObject(bars));
  CHECK(Gdi::PatBlt(dc, 0, 0, 400, 12, Gdi::PATCOPY));
  unsigned painted = 0;
  for (auto v : pixels)
    painted += v != 0;
  CHECK(painted == 1000);
  CHECK(io.Call("SelectClipRgn", dc, 0) == 2);
  CHECK(Gdi::RestoreDC(dc, -1));
  memset(pixels, 0, sizeof pixels);
  CHECK(Gdi::PatBlt(dc, 0, 0, 400, 12, Gdi::PATCOPY));
  painted = 0;
  for (auto v : pixels)
    painted += v != 0;
  CHECK(painted == 1000);
  auto snapshot = io.Create(0, 0, 0, 0);
  CHECK(snapshot);
  CHECK(io.Call("GetClipRgn", dc, snapshot) == 1);
  CHECK(io.Call("GetRegionData", snapshot, 0, 0) == sizeof data);
  CHECK(Gdi::SaveDC(dc) == 1);
  CHECK(Gdi::ExcludeClipRect(dc, 0, 0, 150, 12) == 3);
  CHECK(io.Call("GetClipRgn", dc, snapshot) == 1);
  CHECK(io.Call("GetRegionData", snapshot, 0, 0) == 32 + 50 * 16);
  CHECK(Gdi::RestoreDC(dc, -1));
  CHECK(io.Call("OffsetClipRgn", dc, 1, 1) == 3);
  CHECK(io.Call("GetClipRgn", dc, snapshot) == 1);
  CHECK(io.Call("PtInRegion", snapshot, 1, 1));
  CHECK(!io.Call("PtInRegion", snapshot, 0, 0));
  CHECK(Gdi::DeleteObject(snapshot));
  CHECK(Gdi::SelectObject(dc, old) == brush);
  CHECK(Gdi::DeleteObject(brush) && Gdi::DeleteDC(dc));
  CHECK(!io.faults);
  Gdi::ShutdownBitmaps();
  CHECK(Gdi::LiveObjects() == 0);
  // Device origin and native canvas origin are separate coordinate systems.
  for (bool local : {false, true}) {
    unsigned shifted_pixels[160 * 120]{};
    auto shifted = Gdi::CreateSurfaceDC(
        {reinterpret_cast<unsigned char *>(shifted_pixels), 160, 120, 640, 32});
    CHECK(shifted);
    CHECK(Gdi::SetDeviceOrigin(shifted, 40, 60, local));
    auto shape = io.Create(0, 0, 10, 10), out = io.Create(0, 0, 0, 0);
    CHECK(shape && out);
    Gdi::RECT visible = {2, 2, 8, 8};
    CHECK(Gdi::IntersectClipRegion(shifted, &visible, 1) == 2);
    CHECK(io.Call("GetClipRgn", shifted, out) == 0);
    CHECK(io.Call("SelectClipRgn", shifted, shape) == 2);
    CHECK(io.Call("GetClipRgn", shifted, out) == 1 &&
          io.Call("EqualRgn", shape, out));
    CHECK(Gdi::SaveDC(shifted));
    CHECK(io.Call("SelectClipRgn", shifted, 0) == 2);
    CHECK(io.Call("GetClipRgn", shifted, out) == 0);
    auto red = Gdi::CreateSolidBrush(255);
    Gdi::RECT all = {0, 0, 100, 100};
    CHECK(red && Gdi::FillRect(shifted, &all, red));
    unsigned count = 0;
    for (unsigned y = 0; y < 120; ++y)
      for (unsigned x = 0; x < 160; ++x) {
        bool hit = shifted_pixels[y * 160 + x] != 0;
        count += hit;
        if (hit)
          CHECK(x >= (local ? 2 : 42) && x < (local ? 8 : 48) &&
                y >= (local ? 2 : 62) && y < (local ? 8 : 68));
      }
    CHECK(count == 36);
    Gdi::RECT smaller = {4, 4, 6, 6};
    CHECK(Gdi::IntersectClipRegion(shifted, &smaller, 1) == 2);
    CHECK(Gdi::RestoreDC(shifted, -1));
    CHECK(io.Call("GetClipRgn", shifted, out) == 1 &&
          io.Call("EqualRgn", shape, out));
    unsigned map[] = {shifted, 8};
    CHECK(Gdi::MapApi("SetMapMode", map, io) == 1);
    unsigned ext[] = {shifted, 1, 1, 0};
    CHECK(Gdi::MapApi("SetWindowExtEx", ext, io));
    ext[1] = 2;
    ext[2] = 3;
    CHECK(Gdi::MapApi("SetViewportExtEx", ext, io));
    unsigned origin[] = {shifted, 7, 11, 0};
    CHECK(Gdi::MapApi("SetWindowOrgEx", origin, io));
    origin[1] = 13;
    origin[2] = 17;
    CHECK(Gdi::MapApi("SetViewportOrgEx", origin, io));
    CHECK(io.Call("OffsetClipRgn", shifted, 3, 2));
    CHECK(io.Call("GetClipRgn", shifted, out) == 1);
    CHECK(io.Call("GetRgnBox", out, 4096) == 2);
    int bounds[4];
    CHECK(io.Read(4096, bounds, 16));
    CHECK(bounds[0] == 6 && bounds[1] == 6 && bounds[2] == 16 &&
          bounds[3] == 16);
    CHECK(Gdi::DeleteObject(red) && Gdi::DeleteObject(shape) &&
          Gdi::DeleteObject(out) && Gdi::DeleteDC(shifted));
    CHECK(!io.faults);
    Gdi::ShutdownBitmaps();
  }
  // Selecting a larger bitmap must not retain the default 1x1 bitmap bounds.
  {
    auto changing = Gdi::CreateCompatibleDC(0);
    auto shape = io.Create(2, 2, 18, 8), out = io.Create(0, 0, 0, 0);
    CHECK(changing && shape && out);
    CHECK(io.Call("SelectClipRgn", changing, shape) == 1);
    unsigned *large_pixels = nullptr, *small_pixels = nullptr;
    Gdi::BITMAPINFO info = {{40, 24, -12, 1, 32, 0, 0, 0, 0, 0, 0}};
    auto large = Gdi::CreateDIBSection(
        changing, &info, 0, reinterpret_cast<void **>(&large_pixels));
    info.bmiHeader.biWidth = 6;
    info.bmiHeader.biHeight = -6;
    auto small = Gdi::CreateDIBSection(
        changing, &info, 0, reinterpret_cast<void **>(&small_pixels));
    auto red = Gdi::CreateSolidBrush(255);
    CHECK(large && small && red);
    auto original = Gdi::SelectObject(changing, large);
    CHECK(original && Gdi::SaveDC(changing));
    Gdi::RECT all = {0, 0, 24, 12};
    CHECK(Gdi::FillRect(changing, &all, red));
    unsigned count = 0;
    for (unsigned y = 0; y < 12; ++y)
      for (unsigned x = 0; x < 24; ++x) {
        const bool hit = large_pixels[y * 24 + x] != 0;
        CHECK(hit == (x >= 2 && x < 18 && y >= 2 && y < 8));
        count += hit;
      }
    CHECK(count == 96);
    CHECK(Gdi::SelectObject(changing, small) == large);
    CHECK(Gdi::FillRect(changing, &all, red));
    count = 0;
    for (unsigned y = 0; y < 6; ++y)
      for (unsigned x = 0; x < 6; ++x) {
        const bool hit = small_pixels[y * 6 + x] != 0;
        CHECK(hit == (x >= 2 && y >= 2));
        count += hit;
      }
    CHECK(count == 16);
    CHECK(Gdi::RestoreDC(changing, -1));
    CHECK(io.Call("GetClipRgn", changing, out) == 1 &&
          io.Call("EqualRgn", shape, out));
    memset(large_pixels, 0, 24 * 12 * sizeof(unsigned));
    CHECK(Gdi::FillRect(changing, &all, red));
    count = 0;
    for (unsigned i = 0; i < 24 * 12; ++i)
      count += large_pixels[i] != 0;
    CHECK(count == 96);
    CHECK(Gdi::SelectObject(changing, original) == large);
    CHECK(Gdi::DeleteObject(large) && Gdi::DeleteObject(small) &&
          Gdi::DeleteObject(red) && Gdi::DeleteObject(shape) &&
          Gdi::DeleteObject(out) && Gdi::DeleteDC(changing));
    Gdi::ShutdownBitmaps();
    CHECK(!io.faults && !Gdi::LiveObjects());
  }
// Checked native allocation boundaries: failed operations keep the prior clip.
#ifdef REGION_FAIL_TEST
  {
    for (unsigned fail = 1; fail <= 14; ++fail) {
      auto h = io.Create(0, 0, 80, 10), old_clip = io.Create(10, 1, 30, 4),
           q = io.Create(0, 0, 0, 0);
      CHECK(h && old_clip && q);
      auto dest = Gdi::CreateSurfaceDC(
          {reinterpret_cast<unsigned char *>(pixels), 400, 12, 1600, 32});
      CHECK(dest);
      CHECK(io.Call("SelectClipRgn", dest, old_clip) == 2);
      CHECK(Gdi::SaveDC(dest));
      region_test_failure(fail);
      unsigned selected = io.Call("SelectClipRgn", dest, h);
      region_test_failure(0);
      CHECK(io.Call("GetClipRgn", dest, q) == 1);
      CHECK(io.Call("EqualRgn", q, selected ? h : old_clip));
      CHECK(Gdi::RestoreDC(dest, -1));
      CHECK(io.Call("GetClipRgn", dest, q) == 1);
      CHECK(io.Call("EqualRgn", q, old_clip));
      CHECK(Gdi::DeleteDC(dest));
      CHECK(Gdi::DeleteObject(h) && Gdi::DeleteObject(old_clip) &&
            Gdi::DeleteObject(q));
      CHECK(!io.faults);
      Gdi::ShutdownBitmaps();
      CHECK(Gdi::LiveObjects() == 0);
      CHECK(region_test_allocations() == 0);
    }
    for (unsigned fail = 1; fail <= 30; ++fail) {
      region_test_failure(fail);
      auto result = io.Call("ExtCreateRegion", 0, sizeof data, 1024);
      region_test_failure(0);
      if (result)
        CHECK(Gdi::DeleteObject(result));
      CHECK(!io.faults);
      Gdi::ShutdownBitmaps();
      CHECK(Gdi::LiveObjects() == 0);
      CHECK(region_test_allocations() == 0);
    }
    std::puts("Native region allocation failures PASS: 44 injected failure "
              "positions, no retained allocations after teardown");
  }
#endif
  std::puts("Native regions PASS: 100 disjoint clip rectangles, object "
            "recycling, SaveDC/RestoreDC and raster");
}
