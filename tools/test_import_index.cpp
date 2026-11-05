// SPDX-License-Identifier: GPL-2.0-or-later
#include <new>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#define private public
#include "../src/x86_app.h"
#undef private
namespace X86 {
#include "../src/x86_import_table.inc"
}
static void require(bool okay) { if (!okay) std::abort(); }
int main()
{
    using App = X86::Application;
    App::ImportTable session, process;
    constexpr unsigned count = 12000;
    require(!session.Find(nullptr, "", count) && !session.Find("", nullptr, count));
    // More names than buckets, sparse session slots and an independent
    // process table. No CPU, filesystem or guest is needed for this index.
    for (unsigned i = 0; i < count; ++i) {
        const unsigned slot = i * 3;
        require(session.Reserve(slot));
        auto &entry = session[slot];
        std::strcpy(entry.dll, i & 1 ? "USER32.DLL" : "KERNEL32.DLL");
        std::snprintf(entry.name, sizeof entry.name, "Export%u", i);
        session.Index(slot);
        session.Index(slot); // Rebinding must not make a cycle.
        if (i % 2 == 0) {
            require(process.Reserve(slot));
            process[slot] = entry;
            process.Index(slot);
        }
    }
    const auto begin = std::chrono::steady_clock::now();
    for (unsigned round = 0; round < 20; ++round)
        for (unsigned i = 0; i < count; ++i) {
            char name[64]; std::snprintf(name, sizeof name, "Export%u", i);
            const char *dll = i & 1 ? "user32.dll" : "kernel32.dll";
            require(session.Find(dll, name, count * 3) == i * 3 + 1);
            require(!session.Find(dll, name, i * 3)); // unpublished/withdrawn slot
            require(process.Find(dll, name, count * 3) == (i % 2 == 0 ? i * 3 + 1 : 0));
            name[0] = 'e';
            require(!session.Find(dll, name, count * 3)); // export case matters
        }
    // A previously missing name becomes visible immediately when bound.
    require(!process.Find("USER32.DLL", "Export1", count * 3));
    require(process.Reserve(3)); process[3] = session[3]; process.Index(3);
    require(process.Find("USER32.dll", "Export1", count * 3) == 4);
    for (unsigned repeat = 0; repeat < 20; ++repeat) {
        App::ImportTable fresh;
        require(!fresh.Find("kernel32.dll", "Export0", count * 3));
    }
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    std::printf("IMPORT INDEX PASS: 960000 lookups, collisions, sparse slots, rebinding, case, missing-to-bound, new processes; %.3f ms\n", ms);
}
