// SPDX-License-Identifier: GPL-2.0-or-later
#define private public
#include "../src/x86_app.h"
#undef private
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
static std::vector<unsigned char> load(const char *p) {
  FILE *f = fopen(p, "rb");
  assert(f);
  fseek(f, 0, SEEK_END);
  auto n = ftell(f);
  rewind(f);
  std::vector<unsigned char> b(n);
  assert(fread(b.data(), 1, n, f) == size_t(n));
  fclose(f);
  return b;
}
int main(int argc, char **argv) {
  assert(argc == 3);
  auto exe = load(argv[1]), font = load(argv[2]);
  for (unsigned round = 0; round < 1; ++round) {
    unsigned before = Gdi::LiveObjects();
    {
      X86::Application app(nullptr);
      assert(app.Start(exe.data(), exe.size(), font.data(), font.size(),
                       "GDIPATHS.EXE"));
      for (unsigned n = 0;
           n < 10000 && app.Status() != X86::Application::Exited &&
           app.Status() != X86::Application::Failed;
           ++n) {
        app.Pump(32);
        if (app.Status() == X86::Application::Idle)
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      if (app.Status() != X86::Application::Exited || app.ExitCode())
        fprintf(stderr, "GDIPATHS status=%u code=%u error=%s\n", app.Status(),
                app.ExitCode(), app.Error());
      assert(app.Status() == X86::Application::Exited && !app.ExitCode());
      // The same path on a window DC: the compositor has to see the fill.
      auto dc = User::GetDC(User::GetDesktopWindow());
      assert(dc);
      User::Render();
      assert(!User::Dirty());
      unsigned one[] = {dc};
      assert(app.GdiApi("BeginPath", one));
      unsigned move[] = {dc, 12, 13, 0};
      assert(app.GdiApi("MoveToEx", move));
      const int corners[3][2] = {{28, 13}, {28, 29}, {12, 29}};
      for (const auto &corner : corners) {
        unsigned line[] = {dc, unsigned(corner[0]), unsigned(corner[1])};
        assert(app.GdiApi("LineTo", line));
      }
      assert(app.GdiApi("CloseFigure", one));
      assert(app.GdiApi("EndPath", one));
      const unsigned brush = Gdi::CreateSolidBrush(Gdi::RGB(0, 0, 0));
      assert(brush);
      const unsigned previous = Gdi::SelectObject(dc, brush);
      assert(previous);
      assert(app.GdiApi("FillPath", one) && User::Dirty());
      // FillPath drops the path, so a second one answers FALSE.
      assert(!app.GdiApi("FillPath", one));
      assert(Gdi::SelectObject(dc, previous) == brush);
      assert(Gdi::DeleteObject(brush));
      assert(User::ReleaseDC(User::GetDesktopWindow(), dc));
      printf("GDI paths guest PASS round=%u calls=%u callbacks=%u\n", round,
             app.ApiCalls(), app.Callbacks());
    }
    assert(Gdi::LiveObjects() == before);
  }
  return 0;
}
