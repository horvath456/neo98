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
  assert(argc == 5);
  auto exe = load(argv[1]), font = load(argv[2]);
  for (unsigned round = 0; round < 1; ++round) {
    unsigned before = Gdi::LiveObjects();
    {
      X86::Application app(nullptr);
      assert(app.Start(exe.data(), exe.size(), font.data(), font.size(),
                       "WMFTEST.EXE"));
      for (unsigned n = 0;
           n < 10000 && app.Status() != X86::Application::Exited &&
           app.Status() != X86::Application::Failed;
           ++n) {
        app.Pump(32);
        if (app.Status() == X86::Application::Idle)
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      if (app.Status() != X86::Application::Exited || app.ExitCode())
        fprintf(stderr, "WMFTEST status=%u code=%u error=%s\n", app.Status(),
                app.ExitCode(), app.Error());
      assert(app.Status() == X86::Application::Exited && !app.ExitCode());
      auto dc = User::GetDC(User::GetDesktopWindow());
      assert(dc);
      User::Render();
      assert(!User::Dirty());
      // Metafile playback draws through a window DC and must mark the surface
      // changed for presentation.
      const unsigned nothing[] = {0};
      const unsigned meta = app.GdiApi("CreateMetaFileA", nothing);
      assert(meta);
      const unsigned rectangle[] = {meta, 4, 4, 40, 30};
      assert(app.GdiApi("Rectangle", rectangle));
      const unsigned close[] = {meta};
      const unsigned file = app.GdiApi("CloseMetaFile", close);
      assert(file);
      const unsigned play[] = {dc, file};
      assert(app.GdiApi("PlayMetaFile", play) && User::Dirty());
      // Export the bytes so the runner can independently validate the record
      // format.
      const unsigned measure[] = {file, 0, 0};
      const unsigned bytes = app.GdiApi("GetMetaFileBitsEx", measure);
      assert(bytes >= 18);
      std::vector<unsigned char> image(bytes);
      const unsigned buffer = app.Allocate(bytes);
      assert(buffer);
      const unsigned fetch[] = {file, bytes, buffer};
      assert(app.GdiApi("GetMetaFileBitsEx", fetch) == bytes);
      assert(app.Read(buffer, image.data(), bytes));
      app.Free(buffer);
      FILE *out = fopen(argv[3], "wb");
      assert(out && fwrite(image.data(), 1, bytes, out) == bytes);
      fclose(out);
      const unsigned drop[] = {file};
      assert(app.GdiApi("DeleteMetaFile", drop));
      // Repeat with an enhanced metafile.
      const unsigned reference[] = {0, 0, 0, 0};
      const unsigned enhanced = app.GdiApi("CreateEnhMetaFileA", reference);
      assert(enhanced);
      const unsigned box[] = {enhanced, 4, 4, 40, 30};
      assert(app.GdiApi("Rectangle", box));
      const unsigned finish[] = {enhanced};
      const unsigned emf = app.GdiApi("CloseEnhMetaFile", finish);
      assert(emf);
      const unsigned emf_size[] = {emf, 0, 0};
      const unsigned emf_bytes = app.GdiApi("GetEnhMetaFileBits", emf_size);
      assert(emf_bytes >= 88);
      std::vector<unsigned char> enhanced_image(emf_bytes);
      const unsigned emf_buffer = app.Allocate(emf_bytes);
      assert(emf_buffer);
      const unsigned emf_fetch[] = {emf, emf_bytes, emf_buffer};
      assert(app.GdiApi("GetEnhMetaFileBits", emf_fetch) == emf_bytes);
      assert(app.Read(emf_buffer, enhanced_image.data(), emf_bytes));
      app.Free(emf_buffer);
      FILE *emf_out = fopen(argv[4], "wb");
      assert(emf_out &&
             fwrite(enhanced_image.data(), 1, emf_bytes, emf_out) == emf_bytes);
      fclose(emf_out);
      const unsigned emf_play[] = {dc, emf, 0};
      assert(app.GdiApi("PlayEnhMetaFile", emf_play) && User::Dirty());
      const unsigned emf_drop[] = {emf};
      assert(app.GdiApi("DeleteEnhMetaFile", emf_drop));
      assert(User::ReleaseDC(User::GetDesktopWindow(), dc));
      printf("GDI metafile guest PASS round=%u calls=%u callbacks=%u\n", round,
             app.ApiCalls(), app.Callbacks());
    }
    assert(Gdi::LiveObjects() == before);
  }
  return 0;
}
