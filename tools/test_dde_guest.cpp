// SPDX-License-Identifier: GPL-2.0-or-later
// Drives the DDEML guest fixture through the native Application runtime and
// checks the two DDEML windows the ported DdeInitialize creates.
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <cstring>
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
static unsigned CountClass(const char *wanted)
{
    unsigned windows[128] = {}, count = 0, found = 0;
    if (!User::WindowSnapshot(User::GetDesktopWindow(), windows, 128, count))
        return 0;
    for (unsigned i = 0; i < count; ++i)
    {
        char name[64] = {};
        User::GetClassName(windows[i], name, sizeof name);
        if (!strcmp(name, wanted))
            ++found;
    }
    return found;
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = Load(argv[1]), font = Load(argv[2]);
    const unsigned before = Gdi::LiveObjects();
    unsigned peak_instance = 0, peak_server = 0;
    {
        X86::Application app(nullptr);
        assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "DDE.EXE"));
        for (unsigned i = 0; i < 20000 && app.Status() != X86::Application::Exited &&
                             app.Status() != X86::Application::Failed;
             ++i)
        {
            app.Pump(32);
            const unsigned instance = CountClass("DMGClass"), server = CountClass("DMGFrame");
            if (instance > peak_instance)
                peak_instance = instance;
            if (server > peak_server)
                peak_server = server;
        }
        if (app.Status() != X86::Application::Exited || app.ExitCode())
            fprintf(stderr, "dde guest: status=%u code=%x error=%s\n", app.Status(), app.ExitCode(),
                    app.Error());
        assert(app.Status() == X86::Application::Exited && !app.ExitCode());
        // One DMGClass per instance, one DMGFrame for the server instance only:
        // the client instance is APPCMD_CLIENTONLY and gets none. The guest
        // counts them itself while both instances live (exit codes 0x3c and
        // 0x3d); between two pump steps both instances may already have come
        // and gone, so here only an upper bound holds.
        assert(peak_instance <= 2);
        assert(peak_server <= 1);
        // DdeUninitialize destroyed both instances' windows again.
        assert(CountClass("DMGClass") == 0 && CountClass("DMGFrame") == 0);
        printf("DDE GUEST PASS calls=%u callbacks=%u instances=%u servers=%u\n", app.ApiCalls(),
               app.Callbacks(), peak_instance, peak_server);
    }
    assert(Gdi::LiveObjects() == before);
    return 0;
}
