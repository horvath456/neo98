// SPDX-License-Identifier: GPL-2.0-or-later
// FORMAT_MESSAGE_FROM_HMODULE against a real Win98 message table.
//
// The runner starts one original program so a process exists, loads
// ESENU.DLL of the imported installation as guest code and asks
// Application::ModuleMessage for every identifier the resource carries. The
// Python side decodes the same resource on its own and compares. It also
// checks the two decisions around the lookup: an identifier the module does
// not carry answers false, and so does a handle that is no module.
#define private public
#include "../src/x86_app.h"
#undef private
#include "system_tree_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static std::vector<unsigned char> load(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return {};
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    rewind(file);
    std::vector<unsigned char> bytes(size > 0 ? size : 0);
    if (size <= 0 || fread(bytes.data(), 1, bytes.size(), file) != bytes.size()) bytes.clear();
    fclose(file);
    return bytes;
}
static void require(bool ok, const char *why)
{
    if (ok) return;
    fprintf(stderr, "FORMAT MESSAGE MODULE FAILED: %s\n", why);
    exit(1);
}
// The identifiers stand on the command line, one per argument, in decimal.
int main(int argc, char **argv)
{
    require(argc >= 5, "usage: PROGRAM FONT MODULE IDENTIFIER...");
    const auto tree = SystemTreeHost::Scan(argc, argv);
    require(SystemTreeHost::Mount(tree.root), "mount the imported Win98 tree");
    const auto exe = load(argv[1]), font = load(argv[2]);
    require(!exe.empty() && !font.empty(), "read the program and the font");
    auto *app = new X86::Application(nullptr);
    require(app->Start(exe.data(), exe.size(), font.data(), font.size(), "TASKMAN.EXE"),
            app->Error());
    const unsigned index = app->LoadModule(argv[3], 0);
    require(index < 32 && app->modules[index].pe, "load the message module as guest code");
    const unsigned handle = app->modules[index].handle;
    printf("FORMAT MESSAGE MODULE module=%s handle=%08x\n", argv[3], handle);
    char text[8192];
    for (int i = 4; i < argc; ++i)
    {
        if (!strncmp(argv[i], "--", 2)) break;
        const unsigned identifier = (unsigned)strtoul(argv[i], nullptr, 10);
        if (!app->ModuleMessage(handle, identifier, 0, text, sizeof text))
        {
            printf("FORMAT MESSAGE MISSING id=%u\n", identifier);
            continue;
        }
        printf("FORMAT MESSAGE TEXT id=%u bytes=%u text=", identifier, (unsigned)strlen(text));
        for (const auto *p = (const unsigned char *)text; *p; ++p) printf("%02x", *p);
        putchar('\n');
    }
    // An identifier outside every block and a handle that is no module both
    // answer false instead of inventing a message.
    require(!app->ModuleMessage(handle, 0x7fff0000u, 0, text, sizeof text),
            "an absent identifier answers false");
    require(!app->ModuleMessage(0x12340000u, 0, 0, text, sizeof text),
            "a handle that is no module answers false");
    puts("FORMAT MESSAGE MODULE CONTRACT ok");
    fflush(stdout);
    return 0;
}
