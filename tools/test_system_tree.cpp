// SPDX-License-Identifier: GPL-2.0-or-later
// The Win98 module search, the layered file view and the MISSING diagnostics,
// on a fixture tree that tools/test_system_tree.py lays out. Nothing here reads
// the local Win98 installation; the DLLs are the own fixtures of
// tools/build_module_fixture.py.
#define private public
#include "../src/x86_app.h"
#undef private
#include "system_tree_host.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::vector<std::string> lines;
static void log(const char *text)
{
    lines.push_back(text);
    if (!strncmp(text, "MISSING", 7))
        printf("TREE LOG %s\n", text);
}
static bool logged(const char *prefix)
{
    for (const auto &line : lines)
        if (!strncmp(line.c_str(), prefix, strlen(prefix)))
            return true;
    return false;
}
static std::vector<unsigned char> load(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    long n = ftell(file);
    rewind(file);
    std::vector<unsigned char> bytes(n);
    assert(fread(bytes.data(), 1, n, file) == (unsigned)n);
    fclose(file);
    return bytes;
}
static void check(bool ok, const char *what)
{
    if (ok)
    {
        printf("TREE PASS %s\n", what);
        fflush(stdout);
        return;
    }
    fprintf(stderr, "TREE FAILED %s\n", what);
    exit(1);
}
static bool same(const char *a, const char *b) { return !strcasecmp(a, b); }

int main(int argc, char **argv)
{
    // argv: <fixture root> <font>
    assert(argc >= 3);
    check(SystemTreeHost::Mount(argv[1]), "mount the fixture tree");

    // 1. Layered file view. The tree entries are visible, the RAM volume wins,
    // a write lands in RAM and the file on the host stays as it was.
    auto &store = Files::System();
    Files::Session session(store);
    unsigned node = Files::Invalid;
    check(!store.Lookup("C:\\WINDOWS\\SYSTEM\\SEARCH.DLL", node) && store.Get(node), "tree file visible");
    check(store.Get(node)->size > 0 && !store.Get(node)->data, "tree file has metadata before its bytes");
    check(store.Get(node)->times[2].date != 0, "tree file keeps its write timestamp");
    unsigned alias_node = Files::Invalid;
    check(!store.Lookup("C:\\WINDOWS\\SYSTEM\\SEARCH~1.DLL", alias_node) && alias_node == node,
          "8.3 alias resolves to the same entry");
    unsigned long_node = Files::Invalid;
    check(!store.Lookup("C:\\PROGRA~1\\LONGNA~1.DLL", long_node) &&
              same(store.Get(long_node)->name, "Long Name Library.DLL"),
          "a path of 8.3 aliases reaches a long name");

    unsigned handle = 0, status = 0, done = 0;
    unsigned char head[2] = {};
    check(!session.Open("C:\\WINDOWS\\SYSTEM\\SEARCH.DLL", 1, 1, 3, 0, handle, status) &&
              !session.Read(handle, head, 2, done) && done == 2 && head[0] == 'M' && head[1] == 'Z',
          "tree file bytes arrive on first read");
    session.Close(handle);

    const unsigned before = store.Get(node)->size;
    check(!session.Open("C:\\WINDOWS\\SYSTEM\\SEARCH.DLL", 3, 0, 3, 0, handle, status) &&
              !session.Write(handle, "NEO98", 5, done) && done == 5,
          "a write to a tree file is accepted");
    session.Close(handle);
    check(store.Get(node)->size == before && !store.Get(node)->lower, "the written file became a RAM file");
    auto disk = load((std::string(argv[1]) + "/WINDOWS/SYSTEM/SEARCH.DLL").c_str());
    check(disk.size() == before && disk[0] == 'M' && disk[1] == 'Z', "the host file is untouched");

    // Shadowing and enumeration: a RAM file hides a tree file of the same name,
    // and one directory listing shows both sources exactly once.
    check(!store.Seed("C:\\WINDOWS\\SYSTEM\\SHADOW.TXT", "ram", 3), "seed a RAM file into a tree directory");
    unsigned directory = Files::Invalid;
    check(!store.Lookup("C:\\WINDOWS\\SYSTEM", directory), "system directory");
    unsigned found = 0, search_entries = 0, shadow_entries = 0;
    for (unsigned index = 0, child = 0; !store.Enumerate(directory, index, child); ++index)
    {
        ++found;
        if (same(store.Get(child)->name, "SEARCH.DLL")) ++search_entries;
        if (same(store.Get(child)->name, "SHADOW.TXT")) ++shadow_entries;
    }
    check(found >= 4 && search_entries == 1 && shadow_entries == 1,
          "one listing over both layers, no entry twice");

    // Removing a tree entry keeps it removed.
    check(!store.Remove("C:\\WINDOWS\\SYSTEM\\SPARE.DLL", false), "remove a tree file");
    unsigned gone = Files::Invalid;
    check(store.Lookup("C:\\WINDOWS\\SYSTEM\\SPARE.DLL", gone) == 2, "the removed tree file stays removed");

    // 2. The module search order.
    auto font = load(argv[2]);
    auto exe = load((std::string(argv[1]) + "/WINDOWS/MYPROG/MODULE.EXE").c_str());
    auto *app = new X86::Application(log);
    check(app->Start(exe.data(), exe.size(), font.data(), font.size(), "C:\\WINDOWS\\MYPROG\\MODULE.EXE"),
          "start the fixture program out of its own directory");

    char path[260] = {}, searched[600] = {};
    app->SearchModuleFile("SEARCH.DLL", path, searched, sizeof searched);
    printf("TREE SEARCH name=SEARCH.DLL path=%s searched=%s\n", path, searched);
    check(same(path, "C:\\WINDOWS\\MYPROG\\SEARCH.DLL"), "the program's own directory comes first");
    check(same(searched, "C:\\WINDOWS\\MYPROG"),
          "the searched list stops at the directory that answered");

    check(!app->files.SetDirectory("C:\\WORK"), "current directory");
    check(!store.Remove("C:\\WINDOWS\\MYPROG\\SEARCH.DLL", false), "hide the program-directory copy");
    app->SearchModuleFile("SEARCH.DLL", path, searched, sizeof searched);
    check(same(path, "C:\\WORK\\SEARCH.DLL"), "the current directory comes second");

    check(!store.Remove("C:\\WORK\\SEARCH.DLL", false), "hide the current-directory copy");
    app->SearchModuleFile("SEARCH.DLL", path, searched, sizeof searched);
    check(same(path, "C:\\WINDOWS\\SYSTEM\\SEARCH.DLL"), "the system directory comes third");

    check(!store.Remove("C:\\WINDOWS\\SYSTEM\\SEARCH.DLL", false), "hide the system copy");
    app->SearchModuleFile("SEARCH.DLL", path, searched, sizeof searched);
    check(same(path, "C:\\WINDOWS\\SEARCH.DLL"), "the windows directory comes fourth");

    // KnownDLLs: VERSION.DLL is in the installation's
    // System\CurrentControlSet\Control\SessionManager\KnownDLLs, so a copy
    // beside the program cannot displace the system one.
    check(app->KnownModule("VERSION.DLL"), "VERSION.DLL is a known DLL");
    check(!app->KnownModule("SEARCH.DLL"), "SEARCH.DLL is not a known DLL");
    app->SearchModuleFile("VERSION.DLL", path, searched, sizeof searched);
    check(same(path, "C:\\WINDOWS\\SYSTEM\\VERSION.DLL"),
          "a known DLL comes from the system directory, not from beside the program");
    check(!strchr(searched, ';'), "a known DLL searches nowhere else");

    // PATH comes last. C:\EXTRA is not one of the four fixed directories.
    app->Api("SetEnvironmentVariableA",
             (const unsigned[]){app->String("PATH"), app->String("C:\\WINDOWS;C:\\WINDOWS\\SYSTEM;C:\\EXTRA")});
    app->SearchModuleFile("ONPATH.DLL", path, searched, sizeof searched);
    check(same(path, "C:\\EXTRA\\ONPATH.DLL"), "PATH directories come last");

    // 3. LoadLibrary with and without path and extension, and through the alias.
    const unsigned first = app->Api("LoadLibraryA", (const unsigned[]){app->String("ALPHA.DLL")});
    check(first != 0, "LoadLibrary with extension");
    check(app->Api("LoadLibraryA", (const unsigned[]){app->String("ALPHA")}) == first,
          "LoadLibrary without extension finds the loaded module");
    check(app->Api("LoadLibraryA", (const unsigned[]){app->String("C:\\WINDOWS\\SYSTEM\\ALPHA.DLL")}) == first,
          "LoadLibrary with a full path finds the loaded module");
    const unsigned aliased = app->Api("LoadLibraryA", (const unsigned[]){app->String("C:\\PROGRA~1\\GAMMA.DLL")});
    check(aliased != 0 && aliased != first, "LoadLibrary through the 8.3 alias of a directory");
    char name[260] = {};
    const unsigned buffer = app->Allocate(260);
    app->Api("GetModuleFileNameA", (const unsigned[]){aliased, buffer, 260});
    strcpy(name, app->ReadString(buffer));
    check(strstr(name, "GAMMA.DLL") != nullptr, "GetModuleFileName reports the module loaded by alias");

    // 4. The MISSING diagnostics.
    check(!app->Api("LoadLibraryA", (const unsigned[]){app->String("NOSUCH.DLL")}), "an absent DLL fails");
    check(logged("MISSING DLL NOSUCH.DLL needed by C:\\WINDOWS\\MYPROG\\MODULE.EXE, searched: C:\\WINDOWS\\MYPROG;"),
          "MISSING DLL names the module and every directory searched");
    check(!app->Api("LoadLibraryA", (const unsigned[]){app->String("BROKEN.DLL")}), "a DLL with a bad import fails");
    check(logged("MISSING IMPORT MODULE.EXE!ReporX needed by C:\\WINDOWS\\SYSTEM\\BROKEN.DLL"),
          "MISSING IMPORT names the export and the module that wanted it");

    printf("SYSTEM TREE CONTRACT root=%s\n", SystemTreeHost::MountedRoot());
    fflush(stdout);
    delete app;
    return 0;
}
