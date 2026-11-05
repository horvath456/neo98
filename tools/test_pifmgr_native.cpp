// SPDX-License-Identifier: GPL-2.0-or-later
// Runs tools/pifmgr_fixture.c on the host runtime with the Win98 tree as C:
// and a host directory as D:.
// test-pifmgr FIXTURE.EXE FONT --system-tree --drive D=DIR
#define private public
#include "../src/x86_app.h"
#undef private
#include "host_volume.h"
#include "system_tree_host.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
using App = X86::Application;
static std::vector<unsigned char> load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    std::vector<unsigned char> b(n > 0 ? n : 0);
    if (n > 0 && fread(b.data(), 1, b.size(), f) != b.size()) b.clear();
    fclose(f);
    return b;
}
static void logger(const char *s) { puts(s); fflush(stdout); }
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    const auto tree_options = SystemTreeHost::Scan(argc, argv);
    if (!tree_options.tree || !SystemTreeHost::Mount(tree_options.root)) { fprintf(stderr, "the imported Win98 tree is needed\n"); return 2; }
    const auto drive_options = HostVolume::Scan(argc, argv);
    if (!HostVolume::Mount(drive_options)) { fprintf(stderr, "cannot mount the host drives\n"); return 2; }
    auto exe = load(argv[1]), font = load(argv[2]);
    if (exe.empty() || font.empty()) return 3;
    Files::System().Seed("C:\\WINDOWS\\PIFMGRFX.EXE", exe.data(), exe.size());
    App app(logger);
    if (!app.Start(exe.data(), exe.size(), font.data(), font.size(), "PIFMGRFX.EXE", nullptr)) { printf("PIFMGR start failed: %s\n", app.Error()); return 3; }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (app.Status() != App::Exited && app.Status() != App::Failed && std::chrono::steady_clock::now() < deadline) app.Pump(64);
    printf("PIFMGR state=%u exit=%u calls=%u error=%s\n", app.Status(), app.ExitCode(), app.ApiCalls(), app.Error());
    fflush(stdout);
    return app.Status() == App::Exited && app.ExitCode() == 0 ? 0 : 4;
}
