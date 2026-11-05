// SPDX-License-Identifier: GPL-2.0-or-later
// Runs the registry-import fixture as a real x86 process on the native runtime.
#include "../src/registry.h"
#include "../src/x86_app.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
static std::vector<unsigned char> load(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    auto size = ftell(file);
    rewind(file);
    std::vector<unsigned char> bytes(size);
    assert(fread(bytes.data(), 1, size, file) == size_t(size));
    fclose(file);
    return bytes;
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(!Registry::SystemInitializationError());
    auto &store = Registry::System();
    unsigned nodes = 0, values = 0;
    for (unsigned i = 0; i < store.node_capacity; ++i)
        nodes += store.nodes[i].used;
    for (unsigned i = 0; i < store.value_capacity; ++i)
        values += store.values[i].used;
    X86::Application app(nullptr);
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "REGIMP.EXE"));
    for (unsigned n = 0; n < 10000 && app.Status() != X86::Application::Exited &&
                         app.Status() != X86::Application::Failed;
         ++n)
    {
        app.Pump(32);
        if (app.Status() == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (app.Status() != X86::Application::Exited || app.ExitCode())
        fprintf(stderr, "REGISTRY IMPORT status=%u code=%u error=%s\n", app.Status(), app.ExitCode(), app.Error());
    assert(app.Status() == X86::Application::Exited && !app.ExitCode());
    printf("REGISTRY IMPORT PASS nodes=%u/%u values=%u/%u calls=%u\n", nodes, store.node_capacity, values,
           store.value_capacity, app.ApiCalls());
    return 0;
}
