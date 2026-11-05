// SPDX-License-Identifier: GPL-2.0-or-later
// USER system visibility must survive application clip removal.
#include "gdi.h"
#include "user.h"
#include "gdi_truetype.h"
#include "win98_frame_control.h"
#include <cstdlib>
#include <cassert>
#include <cstdio>
#include <cstring>
// This bitmap-only provider test must never enter the TrueType driver.
namespace GdiTrueType { bool AbcWidths(const Font &, unsigned, unsigned, int *) { std::abort(); } }
struct IO : Gdi::BitmapIO {
  unsigned char data[4096]{};
  unsigned error = 0;
  bool Read(unsigned p, void *d, unsigned n) override {
    if (!p || p > 4096 || n > 4096 - p)
      return false;
    memcpy(d, data + p, n);
    return true;
  }
  bool Write(unsigned p, const void *d, unsigned n) override {
    if (!p || p > 4096 || n > 4096 - p)
      return false;
    memcpy(data + p, d, n);
    return true;
  }
  bool Probe(unsigned p, bool) override { return p && p < 4096; }
  void Error(unsigned e) override { error = e; }
  void Fault(const char *s) override {
    fprintf(stderr, "fault %s\n", s);
    assert(false);
  }
  unsigned Region(const char *n, unsigned a = 0, unsigned b = 0, unsigned c = 0,
                  unsigned d = 0) {
    unsigned v[] = {a, b, c, d, 0};
    return Gdi::RegionApi(n, v, *this);
  }
} io;
#ifdef USER_CLIP_OOM
extern "C" void region_test_failure(unsigned);
extern "C" unsigned region_test_allocations();
#endif
unsigned failures = 0, output = 0, owner_calls = 0, red = 0;
void check(bool okay, const char *n) {
  printf("%s: %s\n", n, okay ? "PASS" : "FAIL");
  failures += !okay;
}
void owner(void *, unsigned, unsigned, Gdi::HDC dc, const Gdi::RECT &,
           unsigned) {
  ++owner_calls;
  check(io.Region("GetClipRgn", dc, output) == 0,
        "owner-draw has no application clip");
  assert(io.Region("SelectClipRgn", dc, 0));
  const Gdi::RECT huge = {-1000, -1000, 1000, 1000};
  assert(Gdi::FillRect(dc, &huge, red));
}
// the colour question of an EDIT, answered like DefWindowProc
// (COLOR_3DFACE for WM_CTLCOLORSTATIC, COLOR_WINDOW for WM_CTLCOLOREDIT),
// except for control 13, whose parent paints it green with blue text.
unsigned face_brush = 0, window_brush = 0, green_brush = 0, edit_questions = 0,
         static_questions = 0;
unsigned edit_color(void *, unsigned, unsigned control, Gdi::HDC dc,
                    unsigned message) {
  edit_questions += message == 0x133 || message == 0x138;
  static_questions += message == 0x138;
  if (User::ControlID(control) == 13) {
    Gdi::SetTextColor(dc, Gdi::RGB(0, 0, 255));
    Gdi::SetBkColor(dc, Gdi::RGB(0, 255, 0));
    return green_brush;
  }
  Gdi::SetTextColor(dc, 0);
  if (message == 0x138) {
    Gdi::SetBkColor(dc, Gdi::RGB(192, 192, 192));
    return face_brush;
  }
  Gdi::SetBkColor(dc, Gdi::RGB(255, 255, 255));
  return window_brush;
}
// This harness has no guest message loop. Consume the parent's WM_PAINT
// before serving its controls, as USER does for a parent without
// WS_CLIPCHILDREN. BeginPaint also settles newly created child geometry.
void paint_parent(User::HWND h) {
  User::Paint paint{};
  assert(User::BeginPaint(h, paint));
  assert(User::EndPaint(h, paint));
}
int main() {
  assert(User::Initialize());
  assert(User::RegisterClass("RegionProvider", 1, 0, 0));
  const auto parent = User::CreateWindow("RegionProvider", "", 0x90000000u, 40,
                                         60, 80, 50, 0, 0, 0);
  assert(parent);
  User::CompleteCreation(parent);
  output = io.Region("CreateRectRgn", 0, 0, 0, 0);
  assert(output);
  auto dc = User::GetDC(parent);
  assert(dc);
  check(io.Region("GetClipRgn", dc, output) == 0,
        "retained GetDC has no application clip");
  assert(User::ReleaseDC(parent, dc));
  User::Paint paint{};
  dc = User::BeginPaint(parent, paint);
  assert(dc);
  check(io.Region("GetClipRgn", dc, output) == 0,
        "BeginPaint has no application clip");
  assert(User::EndPaint(parent, paint));
  dc = User::GetDC(0);
  assert(dc);
  assert(Gdi::SetDeviceOrigin(dc, 30, 40));
  const auto shape = io.Region("CreateRectRgn", 0, 0, 10, 10);
  assert(shape);
  assert(io.Region("SelectClipRgn", dc, shape));
  auto nested = User::GetDC(0);
  assert(nested);
  check(io.Region("GetClipRgn", nested, output) == 0,
        "nested screen GetDC resets application clip");
  unsigned args[] = {nested, 128};
  assert(Gdi::MapApi("GetDCOrgEx", args, io));
  int origin[2];
  memcpy(origin, io.data + 128, 8);
  check(!origin[0] && !origin[1], "nested screen GetDC resets device origin");
  assert(User::ReleaseDC(0, nested));
  check(io.Region("GetClipRgn", dc, output) == 1 &&
            io.Region("EqualRgn", output, shape),
        "nested release restores original application clip");
  assert(User::ReleaseDC(0, dc));
  assert(Gdi::DeleteObject(shape));
  const auto button = User::CreateWindow("BUTTON", "", 0x5000000bu, 20, 10, 20,
                                         15, parent, 1, 0);
  assert(button);
  User::CompleteCreation(button);
  red = Gdi::CreateSolidBrush(255);
  assert(red);
  User::SetControlCallbacks(nullptr, owner, nullptr);
  // A control paints on its owning thread after the parent has handled
  // WM_PAINT. Render publishes pixels and cannot dispatch that message.
  paint_parent(parent);
  User::ServeControlPaints();
  User::Render();
  check(owner_calls > 0, "owner draw callback ran");
  const unsigned *pixels = reinterpret_cast<const unsigned *>(User::Pixels());
  unsigned red_count = 0;
  for (unsigned i = 0; i < 800 * 600; ++i)
    red_count += pixels[i] == 0xff0000;
  check(red_count == 300, "owner clear-appclip still bounded to 20x15 control");
  printf("red_pixels=%u\n", red_count);
  User::SetControlCallbacks(nullptr, nullptr, nullptr);
  // an EDIT without the focus hides its selection; ES_NOHIDESEL
  // (0100h) keeps it. Both select all of their text, a third window has the
  // focus, and the selection colour counts only in the one with the style.
  {
    const auto dialog = User::CreateWindow("RegionProvider", "", 0x90000000u,
                                           100, 300, 120, 100, 0, 0, 0);
    assert(dialog);
    User::CompleteCreation(dialog);
    const auto hidden =
        User::CreateWindow("EDIT", "", 0x50000080u, 0, 0, 120, 20, dialog, 0, 0);
    const auto kept =
        User::CreateWindow("EDIT", "", 0x50000180u, 0, 40, 120, 20, dialog, 0, 0);
    const auto other =
        User::CreateWindow("EDIT", "", 0x50000080u, 0, 80, 120, 20, dialog, 0, 0);
    assert(hidden && kept && other);
    const User::HWND edits[] = {hidden, kept, other};
    for (auto h : edits) {
      User::CompleteCreation(h);
      assert(User::SetWindowText(h, "Selection"));
      User::EditMessage(h, 0xb1, 0, ~0u); // EM_SETSEL all
    }
    User::SetFocus(other);
    paint_parent(dialog);
    User::ServeControlPaints();
    User::Render();
    const unsigned *screen = reinterpret_cast<const unsigned *>(User::Pixels());
    auto navy = [&](int top) {
      unsigned n = 0;
      for (int y = top; y < top + 20; ++y)
        for (int x = 100; x < 220; ++x)
          n += (screen[y * 800 + x] & 0xffffff) == 0x000080;
      return n;
    };
    check(navy(300) == 0, "an EDIT without the focus hides its selection");
    check(navy(340) > 0, "ES_NOHIDESEL keeps the selection without the focus");
    check(navy(380) > 0, "the EDIT with the focus shows its selection");
    assert(User::DestroyWindow(dialog));
  }
  // USER. A read-only (ES_READONLY 0800h) or disabled EDIT
  // of a 4.0 program asks its parent with WM_CTLCOLORSTATIC and stands on
  // the dialog face; an ordinary one asks WM_CTLCOLOREDIT and is white; a
  // disabled one writes in COLOR_GRAYTEXT; the brush of the parent wins.
  {
    User::ConfigureVersion(0x400);
    face_brush = Gdi::CreateSolidBrush(Gdi::RGB(192, 192, 192));
    window_brush = Gdi::CreateSolidBrush(Gdi::RGB(255, 255, 255));
    green_brush = Gdi::CreateSolidBrush(Gdi::RGB(0, 255, 0));
    assert(face_brush && window_brush && green_brush);
    const auto dialog = User::CreateWindow("RegionProvider", "", 0x90000000u,
                                           300, 100, 300, 200, 0, 0, 0);
    assert(dialog);
    User::CompleteCreation(dialog);
    const unsigned styles[] = {0x50000880u, 0x50000080u, 0x58000080u,
                               0x50000080u};
    User::HWND fields[4] = {};
    for (unsigned i = 0; i < 4; ++i) {
      fields[i] = User::CreateWindow("EDIT", "", styles[i], 10, 10 + 40 * i,
                                     200, 20, dialog, 10 + i, 0);
      assert(fields[i]);
      User::CompleteCreation(fields[i]);
      assert(User::SetWindowText(fields[i], "MMMMMMMM"));
    }
    User::SetControlCallbacks(nullptr, nullptr, edit_color);
    paint_parent(dialog);
    User::ServeControlPaints();
    User::Render();
    const unsigned *screen = reinterpret_cast<const unsigned *>(User::Pixels());
    auto count = [&](User::HWND h, unsigned colour) {
      Gdi::RECT r = {};
      assert(User::ClientScreenRect(h, &r));
      unsigned n = 0;
      for (int y = r.top; y < r.bottom; ++y)
        for (int x = r.left; x < r.right; ++x)
          n += (screen[y * 800 + x] & 0xffffff) == colour;
      return n;
    };
    printf("edit colour questions=%u static=%u\n", edit_questions,
           static_questions);
    check(edit_questions == 4 && static_questions == 2,
          "read-only and disabled EDITs ask WM_CTLCOLORSTATIC, the others "
          "WM_CTLCOLOREDIT");
    check(count(fields[0], 0xc0c0c0) > 3000 && count(fields[0], 0xffffff) == 0,
          "a read-only EDIT stands on the dialog face");
    check(count(fields[1], 0xffffff) > 3000,
          "an ordinary EDIT is white");
    check(count(fields[2], 0xc0c0c0) > 3000 && count(fields[2], 0x808080) > 0,
          "a disabled EDIT is grey with COLOR_GRAYTEXT text");
    check(count(fields[3], 0x00ff00) > 3000 && count(fields[3], 0x0000ff) > 0,
          "the parent's brush and text colour win");
    User::SetControlCallbacks(nullptr, nullptr, nullptr);
    assert(User::DestroyWindow(dialog));
    const unsigned brushes[] = {face_brush, window_brush, green_brush};
    for (auto brush : brushes)
      assert(Gdi::DeleteObject(brush));
  }
  // an EDIT in front of a group box keeps its frame. The old
  // per-control surface copy erased that frame behind a transparent group.
  {
    const auto box = User::CreateWindow("RegionProvider", "", 0x90000000u, 300,
                                        350, 200, 120, 0, 0, 0);
    assert(box);
    User::CompleteCreation(box);
    const auto field = User::CreateWindow("EDIT", "", 0x50800000u, 40, 50, 80,
                                          20, box, 20, 0);
    const auto group = User::CreateWindow("BUTTON", "Time", 0x50000007u, 5, 5,
                                          190, 110, box, 21, 0);
    assert(field && group);
    User::CompleteCreation(field);
    User::CompleteCreation(group);
    User::SetExtendedStyle(field, 0x200);
    paint_parent(box);
    User::ServeControlPaints();
    User::Render();
    const unsigned *screen = reinterpret_cast<const unsigned *>(User::Pixels());
    Gdi::RECT r = {};
    assert(User::GetWindowRect(field, &r));
    const auto *colors = Win98Frame::SystemColors();
    unsigned frame = 0;
    for (int x = r.left + 2; x < r.right - 2; ++x)
      frame += (screen[r.top * 800 + x] & 0xffffff) == colors[16] &&
               (screen[(r.top + 2) * 800 + x] & 0xffffff) == colors[21];
    printf("edit frame pixels=%u\n", frame);
    check(frame + 8 >= unsigned(r.right - r.left),
          "an EDIT in front of a group box keeps its frame");
    assert(User::DestroyWindow(box));
  }
  const auto tiny =
      User::CreateWindow("EDIT", "", 0x50000004u, 2, 2, 4, 4, parent, 2, 0);
  assert(tiny);
  User::CompleteCreation(tiny);
  paint_parent(parent);
  User::ServeControlPaints();
  User::Render();
  check(!User::Dirty(), "small control inset is an empty valid system clip");

#ifdef USER_CLIP_OOM
  unsigned rejected = 0;
  for (unsigned fail = 1; fail <= 2; ++fail) {
    User::Render();
    auto base = User::GetDC(0);
    assert(base);
    const auto saved_shape = io.Region("CreateRectRgn", 100, 110, 120, 130);
    assert(saved_shape);
    assert(io.Region("SelectClipRgn", base, saved_shape));
    region_test_failure(fail);
    auto child = User::GetDC(button);
    region_test_failure(0);
    if (child) {
      check(io.Region("GetClipRgn", child, output) == 0,
            "OOM surviving child lease starts without appclip");
      assert(User::ReleaseDC(button, child));
    } else
      ++rejected;
    check(io.Region("GetClipRgn", base, output) == 1 &&
              io.Region("EqualRgn", output, saved_shape),
          "OOM child acquisition preserves caller saved clip");
    assert(User::ReleaseDC(0, base) && Gdi::DeleteObject(saved_shape));
  }
  check(rejected == 2, "native allocation failure rejects GetDC");
  printf("rejected_GetDC=%u\n", rejected);
#endif
  assert(Gdi::DeleteObject(output) && Gdi::DeleteObject(red));
  User::Shutdown();
  Gdi::ShutdownBitmaps();
  check(!Gdi::LiveObjects(), "all GDI objects released");
#ifdef USER_CLIP_OOM
  check(!region_test_allocations(), "no retained native region allocations");
#endif
  return failures ? 1 : 0;
}
