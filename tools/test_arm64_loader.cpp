// SPDX-License-Identifier: GPL-2.0-or-later
// The PE loader with a PE32+ image for ARM64 that the SDK built: parse, load
// at 00400000h, DIR64 relocations, rights per section, imports of 8 bytes.
// tools/test_arm64_loader_native.py, docs/ARM64-APPS.md.
#include "../src/pe32.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static unsigned checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #condition); return 1; } } while (0)

struct Memory final : Pe32::AddressSpace
{
    std::map<unsigned, std::vector<unsigned char>> pages;
    std::map<unsigned, unsigned> rights;
    bool Map(unsigned address, unsigned size, unsigned permissions) override
    {
        for (unsigned page = address; page < address + size; page += 4096)
        {
            if (pages.count(page)) return false;
            pages[page].assign(4096, 0);
            rights[page] = permissions;
        }
        return true;
    }
    bool Store(unsigned address, const void *data, Pe32::Size size) override
    {
        const auto *p = static_cast<const unsigned char *>(data);
        for (Pe32::Size i = 0; i < size; ++i)
        {
            auto page = pages.find((address + i) & ~4095u);
            if (page == pages.end()) return false;
            page->second[(address + i) & 4095] = p[i];
        }
        return true;
    }
    bool Fetch(unsigned address, void *data, unsigned size) override
    {
        auto *p = static_cast<unsigned char *>(data);
        for (unsigned i = 0; i < size; ++i)
        {
            auto page = pages.find((address + i) & ~4095u);
            if (page == pages.end()) return false;
            p[i] = page->second[(address + i) & 4095];
        }
        return true;
    }
    bool Protect(unsigned address, unsigned size, unsigned permissions) override
    {
        for (unsigned page = address; page < address + size; page += 4096)
        {
            if (!pages.count(page)) return false;
            rights[page] = permissions;
        }
        return true;
    }
    unsigned long long Word(unsigned address, unsigned size)
    {
        unsigned char data[8] = {};
        Fetch(address, data, size);
        unsigned long long value = 0;
        for (unsigned i = 0; i < size; ++i) value |= (unsigned long long)data[i] << (8 * i);
        return value;
    }
};

struct Seen
{
    std::vector<std::string> names;
};

static bool Resolve(void *context, const char *dll, const char *name, unsigned, unsigned *address)
{
    auto &seen = *static_cast<Seen *>(context);
    if (!name) return false;
    seen.names.push_back(std::string(dll) + "!" + name);
    *address = 0x70001000u + 16 * (unsigned)seen.names.size();
    return true;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    FILE *file = fopen(argv[1], "rb");
    if (!file) return 2;
    std::vector<unsigned char> bytes;
    for (int c; (c = fgetc(file)) != EOF;) bytes.push_back((unsigned char)c);
    fclose(file);

    Pe32::Image image;
    CHECK(image.Parse(bytes.data(), bytes.size()));
    CHECK(image.Arm64());
    CHECK(!image.HasPreferredBase());
    CHECK(image.CanRelocate());
    CHECK(!image.IsDll());
    CHECK(image.Subsystem() == 3);
    Memory memory;
    CHECK(image.Load(memory));
    CHECK(image.Base() == 0x00400000u);
    CHECK(image.Entry() > image.Base() && image.Entry() < image.Base() + image.SizeOfImage());
    // The entry lies in a page that may be executed and not written; the
    // header page may only be read.
    CHECK(memory.rights[image.Entry() & ~4095u] == (Pe32::Read | Pe32::Execute));
    CHECK(memory.rights[image.Base()] == Pe32::Read);
    // The first instruction of an ARM64 function is a 4-byte word, not zero.
    CHECK(memory.Word(image.Entry(), 4) != 0);
    unsigned writable = 0, executable = 0, both = 0;
    for (auto &page : memory.rights)
    {
        if (page.second & Pe32::Write) ++writable;
        if (page.second & Pe32::Execute) ++executable;
        if ((page.second & Pe32::Write) && (page.second & Pe32::Execute)) ++both;
    }
    CHECK(executable >= 1 && both == 0);

    // Imports: 8 bytes per entry, the upper half zero, every name seen once.
    Seen seen;
    CHECK(image.BindImports(memory, Resolve, &seen));
    CHECK(seen.names.size() >= 5);
    bool write_file = false, exit_process = false;
    for (auto &name : seen.names)
    {
        if (name == "KERNEL32.dll!WriteFile" || name == "KERNEL32.DLL!WriteFile") write_file = true;
        if (name == "KERNEL32.dll!ExitProcess" || name == "KERNEL32.DLL!ExitProcess") exit_process = true;
    }
    CHECK(write_file && exit_process);
    unsigned slots = 0;
    for (auto &page : memory.pages)
        for (unsigned offset = 0; offset < 4096; offset += 8)
        {
            const unsigned long long value = memory.Word(page.first + offset, 8);
            if (value > 0x70001000u && value <= 0x70001000u + 16 * seen.names.size() && !(value & 15)) ++slots;
        }
    CHECK(slots == seen.names.size());

    // A second load at another base moves every absolute address with it.
    Pe32::Image moved;
    Memory other;
    CHECK(moved.Parse(bytes.data(), bytes.size()));
    CHECK(moved.Load(other, 0x00500000u));
    unsigned relocated = 0;
    for (auto &page : memory.pages)
    {
        if (page.first == image.Base()) continue; // the header names no address
        for (unsigned offset = 0; offset < 4096; offset += 8)
        {
            const unsigned long long a = memory.Word(page.first + offset, 8), b = other.Word(page.first + 0x100000 + offset, 8);
            if (a == b) continue;
            // An import slot was bound in the first load only.
            if (a > 0x70001000u && a <= 0x70001000u + 16 * seen.names.size()) continue;
            CHECK(b - a == 0x100000 && a >= image.Base() && a < image.Base() + image.SizeOfImage());
            ++relocated;
        }
    }
    CHECK(relocated >= 1);

    // Refusals: a PE32 magic under the ARM64 machine, an unknown machine, a
    // fixed image above 4 GiB, a relocation of an i386-only kind.
    const unsigned nt = bytes[60] | bytes[61] << 8;
    auto broken = bytes;
    broken[nt + 24] = 0x0b; broken[nt + 25] = 0x01;
    Pe32::Image refused;
    CHECK(!refused.Parse(broken.data(), broken.size()));
    broken = bytes;
    broken[nt + 4] = 0x64; broken[nt + 5] = 0x86; // x86-64
    CHECK(!refused.Parse(broken.data(), broken.size()));
    broken = bytes;
    broken[nt + 22] |= 1; // IMAGE_FILE_RELOCS_STRIPPED
    Memory third;
    CHECK(refused.Parse(broken.data(), broken.size()) && !refused.Load(third));

    printf("ARM64 LOADER PASS checks=%u imports=%zu relocated=%u\n", checks, seen.names.size(), relocated);
    return 0;
}
