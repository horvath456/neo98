// SPDX-License-Identifier: GPL-2.0-or-later
// Runner for tools/thread_exit_fixture.c. Drive D: is the directory
// with the fixture's DLL, read-only; C: is a RAM volume. The guest ends with
// exit code 0x2259 when every thread ended the Win98 way. ThreadsCreated
// counts the initial thread too.
#include "../src/x86_app.h"
#include "host_volume.h"
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
    assert(fread(data.data(), 1, size, file) == size_t(size));
    fclose(file);
    return data;
}
int main(int argc, char **argv)
{
    assert(argc >= 3);
    const auto drives = HostVolume::Scan(argc, argv);
    if (!HostVolume::Mount(drives))
    {
        fprintf(stderr, "cannot mount the drives\n");
        return 3;
    }
    auto exe = load(argv[1]), font = load(argv[2]);
    X86::Application app(nullptr);
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "TXEXIT.EXE"));
    for (unsigned i = 0; i < 20000 && app.Status() != X86::Application::Exited &&
                         app.Status() != X86::Application::Failed;
         ++i)
        app.Pump(32);
    printf("THREAD EXIT state=%u exit=%x threads=%u calls=%u callbacks=%u error=%s\n", (unsigned)app.Status(),
           app.ExitCode(), app.ThreadsCreated(), app.ApiCalls(), app.Callbacks(), app.Error());
    if (app.Status() != X86::Application::Exited || app.ExitCode() != 0x2259 || app.ThreadsCreated() != 5)
        return 1;
    printf("THREAD EXIT PASS\n");
    return 0;
}
