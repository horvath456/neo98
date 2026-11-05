// SPDX-License-Identifier: GPL-2.0-or-later
#include "gdi.h"
#include "user.h"
#include "gdi_truetype.h"
#include <cstdlib>
#include <cassert>
#include <cstdio>
#include <initializer_list>
static unsigned failures;
// This bitmap-only provider test must never enter the TrueType driver.
namespace GdiTrueType { bool AbcWidths(const Font &, unsigned, unsigned, int *) { std::abort(); } }
static void check(bool okay,const char *name) { printf("%s %s\n",okay?"PASS":"FAIL",name); failures+=!okay; }
int main() {
  assert(User::Initialize());
  const auto red=Gdi::CreateSolidBrush(0xff);
  // A new guest-visible framebuffer must retain transparent control backgrounds,
  // both when installing its allocator and when changing colour depth.
  {
    const Gdi::RECT sample{600, 400, 610, 410};
    auto dc = User::GetDC(0);
    assert(Gdi::FillRect(dc, &sample, red));
    assert(User::ReleaseDC(0, dc));
    User::SetScreenLeaseProvider(nullptr,
      [](void *, unsigned bytes, unsigned &guest, unsigned char *&host) {
        guest = 0x12340000; host = new unsigned char[bytes]; return true;
      },
      [](void *, unsigned, unsigned char *host) { delete[] host; });
    for (int depth : {32, 8, 16, 24, 32}) {
      assert(User::SetScreenMode(800, 600, depth));
      dc = User::GetDC(0);
      check(Gdi::GetPixel(dc, 605, 405) == 0xff, "framebuffer replacement preserves a red pixel");
      unsigned pixels[] = {0x000000, 0xffffff, 0xff0000, 0x000080};
      assert(Gdi::CopySurface(dc, 620, 410,
                            {reinterpret_cast<unsigned char *>(pixels), 4, 1, 16, 32}));
      check(Gdi::GetPixel(dc, 620, 410) == 0 && Gdi::GetPixel(dc, 621, 410) == 0xffffff &&
            Gdi::GetPixel(dc, 622, 410) == 0xff,
            "32-bit popup pixels reach every desktop format");
      assert(User::ReleaseDC(0, dc));
    }
  }
  assert(Gdi::DeleteObject(red));
  User::Shutdown(); Gdi::ShutdownBitmaps();
  check(!Gdi::LiveObjects(), "temporary framebuffer DCs released");
  return failures?1:0;
}
