// SPDX-License-Identifier: GPL-2.0-or-later
// Runner for the modeless-dialog fixture: a modeless dialog that ends itself from
// the WM_CREATE of its own first control.
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
    {
        X86::Application app(nullptr);
        assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "YIELDDLG.EXE"));
        for (unsigned i = 0; i < 20000 && app.Status() != X86::Application::Exited &&
                             app.Status() != X86::Application::Failed; ++i)
            app.Pump(32);
        if (app.Status() != X86::Application::Exited || app.ExitCode())
            fprintf(stderr, "yield dialog guest: status=%u code=%u error=%s\n",
                    app.Status(), app.ExitCode(), app.Error());
        assert(app.Status() == X86::Application::Exited && !app.ExitCode());
        printf("YIELD DIALOG GUEST PASS calls=%u callbacks=%u\n", app.ApiCalls(), app.Callbacks());
    }
    assert(Gdi::LiveObjects() == before);
    return 0;
}
