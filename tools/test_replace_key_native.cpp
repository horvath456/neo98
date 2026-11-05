// SPDX-License-Identifier: GPL-2.0-or-later
// a RegReplaceKey takes effect at the end of the session, as VMM
// Shutdown swaps the hive for its "~~R" copy there.
#include "../src/x86_app.h"
#include "system_tree_host.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

static std::vector<unsigned char> load(const char *p)
{
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

static const char *Value()
{
    auto &store = Registry::System();
    unsigned node = 0;
    bool created = false;
    if (store.Open(store.roots[2], "NEO98REPLACE", false, node, created))
        return "<no key>";
    const Registry::Value *value = nullptr;
    const char *text = !store.Query(node, "V", value) ? (const char *)value->data : "<no value>";
    store.Release(node);
    return text;
}

static int Attributes(const char *path)
{
    unsigned node = 0;
    if (Files::System().Lookup(path, node)) return -1;
    return (int)Files::System().Get(node)->attributes;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    assert(!Files::System().Seed("C:\\REPLACE.EXE", exe.data(), exe.size()));
    auto *app = new X86::Application(nullptr);
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "REPLACE.EXE", ""));
    for (unsigned n = 0; n < 40000 && app->Status() != X86::Application::Exited; ++n) {
        app->Pump(8);
        if (app->Status() == X86::Application::Failed)
            fprintf(stderr, "REPLACE KEY failed: %s\n", app->Error());
        assert(app->Status() != X86::Application::Failed);
    }
    printf("REPLACE KEY exit=%u value=%s copy=%d\n", app->ExitCode(), Value(), Attributes("C:\\WINDOWS\\SYSTEM.~~R"));
    assert(app->Status() == X86::Application::Exited && app->ExitCode() == 0);
    delete app;
    // Before the end of the session the key is unchanged and the copy waits,
    // read only and hidden.
    assert(!strcmp(Value(), "old"));
    assert(Attributes("C:\\WINDOWS\\SYSTEM.~~R") == 3);
    X86::Application::SystemExitHives();
    printf("REPLACE KEY after exit value=%s hive=%d copy=%d saved=%d\n", Value(),
           Attributes("C:\\WINDOWS\\SYSTEM.DAT"), Attributes("C:\\WINDOWS\\SYSTEM.~~R"),
           Attributes("C:\\WINDOWS\\SYSTEM.~~S"));
    assert(!strcmp(Value(), "new"));
    assert(Attributes("C:\\WINDOWS\\SYSTEM.DAT") == 3);
    assert(Attributes("C:\\WINDOWS\\SYSTEM.~~R") == -1 && Attributes("C:\\WINDOWS\\SYSTEM.~~S") == -1);
    // A second end of the session has nothing left to do.
    X86::Application::SystemExitHives();
    assert(!strcmp(Value(), "new"));
    printf("REPLACE KEY PASS\n");
    return 0;
}
