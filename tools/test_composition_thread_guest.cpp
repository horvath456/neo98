// SPDX-License-Identifier: GPL-2.0-or-later
// Verify window paint callbacks stay on the owning thread even when another
// thread requests repainting and becomes idle.
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <vector>
static std::vector<unsigned char> Load(const char *name)
{
    FILE *f = fopen(name, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    std::vector<unsigned char> v(n);
    assert(fread(v.data(), 1, n, f) == size_t(n));
    fclose(f);
    return v;
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = Load(argv[1]), font = Load(argv[2]);
    const unsigned before = Gdi::LiveObjects();
    unsigned code = 0, calls = 0, callbacks = 0;
    {
        X86::Application app(nullptr);
        assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "COMPTHRD.EXE"));
        for (unsigned i = 0; i < 40000 && app.Status() != X86::Application::Exited &&
                             app.Status() != X86::Application::Failed; ++i)
        {
            app.Pump(32);
            if (User::FindWindow("NEO98CompositionThread", "Cursor thread binding"))
                app.Mouse(450, 100, false, false);
            // Compose during execution as the host does. Composition must not
            // invoke guest callbacks.
            User::Render();
        }
        if (app.Status() != X86::Application::Exited)
            fprintf(stderr, "composition thread guest: status=%u error=%s\n", app.Status(), app.Error());
        assert(app.Status() == X86::Application::Exited);
        code = app.ExitCode(); calls = app.ApiCalls(); callbacks = app.Callbacks();
    }
    if (code) fprintf(stderr, "composition thread guest: check %u failed\n", code);
    assert(!code);
    assert(Gdi::LiveObjects() == before);
    printf("COMPOSITION THREAD GUEST PASS calls=%u callbacks=%u\n", calls, callbacks);
    return 0;
}
