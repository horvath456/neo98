// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#define private public
#include "../src/x86_app.h"
#include "fixture_native_modules.h"
#undef private
using U = unsigned;
static std::vector<unsigned char> load(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    assert(size > 0);
    rewind(file);
    std::vector<unsigned char> bytes(size);
    assert(fread(bytes.data(), 1, bytes.size(), file) == bytes.size());
    fclose(file);
    return bytes;
}
static void value(U root, const char *path, const char *name, const char *text)
{
    auto &store = Registry::System();
    U node = 0;
    bool created = false;
    assert(!store.Open(store.roots[root], path, true, node, created));
    if (text)
        assert(!store.Set(node, name, 1, text, strlen(text) + 1));
    else
    {
        const U error = store.DeleteValue(node, name);
        assert(error == 0 || error == 2);
    }
}
static const char *machine = "SYSTEM\\CurrentControlSet\\Control\\Nls\\Locale";
static const char *personal = "Control Panel\\International";
static const char *codepage = "SYSTEM\\CurrentControlSet\\Control\\nls\\codepage";
static void reset()
{
    value(2, machine, "", nullptr);
    value(1, personal, "Locale", nullptr);
    value(1, "Control Panel\\desktop\\ResourceLocale", "", nullptr);
    value(2, codepage, "ACP", nullptr);
    value(2, codepage, "OEMCP", nullptr);
    const U error = Files::System().Remove("C:\\WINDOWS\\SYSTEM.INI", false);
    assert(error == 0 || error == 2);
}
static void check_block(X86::Application &app, U handle, U bytes)
{
    assert(handle);
    const U args[] = {app.system_locale.heap, 0, handle};
    assert(app.HeapApi("HeapSize", args) >= bytes);
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    const auto exe = load(argv[1]), font = load(argv[2]);
    for (U profile = 0; profile < 7; ++profile)
    {
        reset();
        U ansi = 1252, oem = 437, resource = 0x409;
        if (profile == 1)
        {
            value(2, machine, "", "00000407");
            value(1, personal, "Locale", "00000411");
            resource = 0x407;
        }
        else if (profile == 2)
        {
            const char ini[] = "; native NLS provider test\r\n[Intl]\r\nSystemLocale = \"00000407\"\r\n"
                               "ACP=850\r\nOEMCP=850\r\n";
            assert(!Files::System().Seed("C:\\WINDOWS\\SYSTEM.INI", ini, sizeof ini - 1));
            ansi = oem = 850;
            resource = 0x407;
        }
        else if (profile == 3)
        {
            value(2, codepage, "ACP", "932");
        }
        else if (profile == 4)
        {
            value(1, personal, "Locale", "0");
            value(1, "Control Panel\\desktop\\ResourceLocale", "", "00000411");
            resource = 0x411;
        }
        else if (profile == 5)
        {
            value(2, machine, "", "bad!");
            value(2, codepage, "ACP", "0");
            const char ini[] = "[other]\nACP=932\n[intl]\nACP=1252\nOEMCP=437\nSystemLocale=409\n";
            assert(!Files::System().Seed("C:\\WINDOWS\\SYSTEM.INI", ini, sizeof ini - 1));
        }
        else if (profile == 6)
        {
            value(2, codepage, "ACP", "437");
            value(2, codepage, "OEMCP", "850");
            ansi = 437;
            oem = 850;
        }
        auto *app = new X86::Application(nullptr);
        assert(FixtureModules::KeepNative(*app));
        const bool started = app->Start(exe.data(), exe.size(), font.data(), font.size(), "COMMON.EXE");
        if (profile == 3)
        {
            assert(!started && app->Status() == X86::Application::Failed && !app->system_locale.initialized);
            assert(strstr(app->Error(), "NLS"));
            delete app;
            continue;
        }
        if (!started)
            fprintf(stderr, "NLS profile %u: %s\n", profile, app->Error());
        assert(started && app->system_locale.initialized);
        const auto &state = app->system_locale;
        assert(state.ansi == ansi && state.oem == oem && state.system == 0x409 && state.user == 0x409);
        assert(state.resource == resource && state.heap >= 0x80000000u && state.heap < 0xc0000000u);
        check_block(*app, state.tableStorage, 0x22c);
        check_block(*app, state.codepageBuckets, 0x90);
        check_block(*app, state.localeBuckets, 0x120);
        check_block(*app, state.ansiTable, 84);
        check_block(*app, state.oemTable, 84);
        check_block(*app, state.systemTable, 68);
        check_block(*app, state.overrides, 0xb44);
        assert(app->Read32(state.tableStorage) == state.codepageBuckets);
        assert(app->Read32(state.tableStorage + 4) == state.localeBuckets);
        assert(app->Read32(state.ansiTable) == ansi && app->Read32(state.oemTable) == oem);
        assert((state.ansiTable == state.oemTable) == (ansi == oem));
        assert(state.systemTable == state.userTable && app->Read32(state.systemTable + 4) == 1252);
        assert(!state.overrideValid && !app->Read32(state.overrides + 0xb40));
        assert(app->NlsApi(312, nullptr) == ansi && app->NlsApi(406, nullptr) == oem);
        assert(app->AnsiWide(0x80) == (ansi == 1252 ? 0x20acu : 0xc7u));
        assert(app->AnsiNarrow(0xe9) == (ansi == 1252 ? 0xe9u : 0x82u));
        value(1, personal, "Locale", "\"00000407\"");
        assert(app->DefaultLocaleApi("GetUserDefaultLCID") == 0x409);
        assert(app->DefaultLocaleApi("InvalidateNLSCache") == 1);
        assert(app->DefaultLocaleApi("GetThreadLocale") == 0);
        assert(app->DefaultLocaleApi("GetUserDefaultLCID") == 0x407);
        assert(app->DefaultLocaleApi("GetThreadLocale") == 0x407);
        assert(app->DefaultLocaleApi("GetSystemDefaultLCID") == 0x409);
        const U buffer = app->Allocate(32);
        U info[] = {0x400, 1, buffer, 32};
        assert(!app->LocaleInfo(info) && app->last_error == 87);
        info[0] = 0;
        assert(!app->LocaleInfo(info));
        info[0] = 0x800;
        assert(app->LocaleInfo(info) == 5);
        char name[5];
        assert(app->Read(buffer, name, sizeof name) && !strcmp(name, "0409"));
        info[0] = 9;
        assert(app->LocaleInfo(info) == 5);
        value(1, personal, "Locale", nullptr);
        assert(app->DefaultLocaleApi("InvalidateNLSCache") == 1);
        assert(app->DefaultLocaleApi("GetUserDefaultLCID") == 0x409);
        assert(app->Status() != X86::Application::Failed);
        delete app;
    }
    reset();
    puts("SYSTEM LOCALE NATIVE STARTUP PASS: 7 profiles, actual heaps/tables, aliases and cache lifetime");
}
