// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
static std::vector<unsigned char> load(const char *name)
{
    FILE *file = fopen(name, "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    rewind(file);
    std::vector<unsigned char> data(size);
    assert(fread(data.data(), 1, size, file) == static_cast<unsigned long>(size));
    fclose(file);
    return data;
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    const auto exe = load(argv[1]), font = load(argv[2]);
    for (unsigned mode = 0; mode < 5; ++mode)
    {
        auto *app = new X86::Application(nullptr);
        const char command[] = {char('0' + mode), 0};
        assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "VMEM.EXE", command));
        for (unsigned i = 0; i < 10000 && app->Status() != X86::Application::Exited &&
                             app->Status() != X86::Application::Failed;
             ++i)
            app->Pump();
        printf("VIRTUAL APP mode=%u state=%u code=%u calls=%u error=%s\n", mode, app->Status(),
               app->ExitCode(), app->ApiCalls(), app->Error());
        fflush(stdout);
        if (!mode)
            assert(app->Status() == X86::Application::Exited && !app->ExitCode());
        else
        {
            assert(app->Status() == X86::Application::Failed);
            if (mode == 2)
                assert(strstr(app->Error(), "write violates page protection"));
            else
                assert(strstr(app->Error(), mode == 1   ? "WRITE_PROT"
                                            : mode == 3 ? "READ_UNMAPPED"
                                                        : "FETCH_PROT"));
        }
        delete app;
        assert(!User::LiveWindows() && !Gdi::LiveObjects());
    }
}
