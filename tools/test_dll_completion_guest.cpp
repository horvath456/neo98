// SPDX-License-Identifier: GPL-2.0-or-later
// Runner for tools/dll_completion_fixture.c (branch dlls).
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <vector>

static std::vector<unsigned char> load(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    std::vector<unsigned char> data(size);
    assert(fread(data.data(), 1, data.size(), file) == data.size());
    fclose(file);
    return data;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    const auto executable = load(argv[1]);
    const auto font = load(argv[2]);
    X86::Application app(nullptr);
    // COMDLG32 runs as the original by the DLL rule; its native form answers
    // GetFileTitleW only with the override the host flag --native-module sets.
    assert(app.AddNativeModule("COMDLG32.DLL"));
    assert(app.Start(executable.data(), executable.size(), font.data(), font.size(),
                     "DLLCOMP.EXE", "DLLCOMP"));
    for (unsigned i = 0; i < 20000 && app.Status() != X86::Application::Exited &&
         app.Status() != X86::Application::Failed; ++i)
        app.Pump(32);
    const bool passed = app.Status() == X86::Application::Exited && !app.ExitCode();
    if (!passed)
        fprintf(stderr, "DLL-COMPLETION GUEST FAIL state=%u code=%u error=%s\n",
                app.Status(), app.ExitCode(), app.Error()); // the exit code names the failed check
    printf("DLL-COMPLETION GUEST %s calls=%u callbacks=%u\n",
           passed ? "PASS" : "FAIL", app.ApiCalls(), app.Callbacks());
    assert(!User::LiveWindows());
    return passed ? 0 : 1;
}
