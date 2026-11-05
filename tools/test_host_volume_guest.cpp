// SPDX-License-Identifier: GPL-2.0-or-later
// Runner for tools/host_volume_fixture.c. It mounts the drives the test
// prepared, starts the guest once and reports its exit code. The test calls it
// twice over the same directories.
#include "../src/x86_app.h"
#include "host_volume.h"
#include "registry_file.h"
#include <cassert>
#include <cstdio>
#include <cstring>
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
    assert(argc >= 4);
    const char *pass = argv[3];
    const auto drives = HostVolume::Scan(argc, argv);
    if (!HostVolume::Mount(drives))
    {
        fprintf(stderr, "cannot mount the drives\n");
        return 3;
    }
    if (RegistryFile::Attach(argc, argv, nullptr))
        printf("HOST VOLUME GUEST registry %s\n",
               static_cast<RegistryFile::File *>(Registry::Attached())->Path());
    auto exe = load(argv[1]), font = load(argv[2]);
    char command[64];
    snprintf(command, sizeof command, "HOSTVOL.EXE %s", pass);
    X86::Application app(nullptr);
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "HOSTVOL.EXE", command));
    for (unsigned i = 0; i < 20000 && app.Status() != X86::Application::Exited &&
                         app.Status() != X86::Application::Failed;
         ++i)
        app.Pump(32);
    if (app.Status() != X86::Application::Exited || app.ExitCode())
        fprintf(stderr, "host volume pass %s state=%u exit=%u calls=%u error=%s\n", pass,
                (unsigned)app.Status(), app.ExitCode(), app.ApiCalls(), app.Error());
    assert(app.Status() == X86::Application::Exited && !app.ExitCode());
    Files::System().Flush();
    printf("HOST VOLUME GUEST PASS pass=%s calls=%u\n", pass, app.ApiCalls());
    return 0;
}
