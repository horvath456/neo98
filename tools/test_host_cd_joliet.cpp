// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_cd_image.h"
#include <cassert>
#include <cstdio>

int main(int argc, char **argv)
{
    assert(argc == 3);
    HostCdImage::Image image;
    std::string error;
    assert(image.Open(argv[1], error));
    Volume::Entry entry;
    unsigned done = 0;
    unsigned char bytes[4096];
    if (!strcmp(argv[2], "doom"))
    {
        assert(image.Find("E:\\Doom 3.msi", entry));
        assert(!strcmp(entry.name, "Doom 3.msi"));
        assert(image.Find("E:\\ISScript9.Msi", entry));
        assert(!image.Read("E:\\Doom 3.msi", 0, bytes, 8, done) && done == 8);
        const unsigned char ole[] = {0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1};
        assert(!memcmp(bytes, ole, 8));
        puts("HOST DOOM JOLIET PASS");
        return 0;
    }
    if (!strcmp(argv[2], "primary"))
    {
        assert(image.Find("E:\\DOOM3~1.MSI", entry));
        assert(!image.Find("E:\\Doom 3.msi", entry));
        assert(!strcmp(image.Label(), "PRIMARY"));
        puts("HOST JOLIET PRIMARY FALLBACK PASS");
        return 0;
    }
    assert(!strcmp(image.Label(), "Joliet CD"));
    assert(image.Find("E:\\dOoM 3.MsI", entry));
    assert(!strcmp(entry.name, "Doom 3.msi") && entry.size == 7000);
    assert(entry.attributes == 1);
    assert(!image.Find("E:\\DOOM3~1.MSI", entry)); // one chosen tree
    assert(image.Find("E:\\Setup Files", entry) && (entry.attributes & 16));
    assert(image.Find("E:\\Setup Files\\Gr\xfc\xdf" "e.txt", entry));
    assert(image.Find("E:\\Setup Files\\Read Me.txt", entry));
    assert(!image.Read("E:\\Setup Files\\Read Me.txt", 0, bytes, sizeof bytes, done));
    assert(done == 5 && !memcmp(bytes, "hello", 5));
    assert(!image.Read("E:\\Doom 3.msi", 2037, bytes, sizeof bytes, done) && done == sizeof bytes);
    for (unsigned i = 0; i < done; ++i) assert(bytes[i] == ((i + 2037) * 17) % 256);
    assert(!image.Read("E:\\Doom 3.msi", 6997, bytes, 10, done) && done == 3);
    assert(!image.Read("E:\\Doom 3.msi", 7000, bytes, 10, done) && !done);
    assert(image.Child("E:\\", 0, entry) && !strcmp(entry.name, "Doom 3.msi"));
    assert(image.Child("E:\\", 1, entry) && !strcmp(entry.name, "Setup Files"));
    assert(!image.Child("E:\\", 2, entry)); // no dot or malformed entries
    assert(image.Child("E:\\Setup Files", 0, entry) && !strcmp(entry.name, "Gr\xfc\xdf" "e.txt"));
    assert(image.Child("E:\\Setup Files", 1, entry) && !strcmp(entry.name, "Read Me.txt"));
    assert(!image.Child("E:\\Setup Files", 2, entry));
    assert(image.Write("E:\\Doom 3.msi", 0, bytes, 1, done) == 19 && !done);
    puts("HOST JOLIET PASS");
}
