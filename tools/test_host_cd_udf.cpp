// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_cd_image.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
// With a supplied hybrid disc, exercise the public CD provider including
// a file crossing sector boundaries and the complete long-name directory.
int main(int argc, char **argv)
{
    assert(argc == 3);
    HostCdImage::Image image;
    std::string error;
    assert(image.Open(argv[1], error));
    Volume::Entry entry;
    if (!strcmp(argv[2], "fallback")) {
        assert(image.Find("D:\\ONLYISO.TXT", entry));
        assert(!image.Find("D:\\Games\\Install All Games.exe", entry));
        puts("HOST ISO FALLBACK PASS"); return 0;
    }
    assert(image.Find("D:\\Games\\Install All Games.exe", entry));
    assert(entry.size > 4096);
    unsigned char bytes[4096]; unsigned done = 0;
    assert(!image.Read("D:\\Games\\Install All Games.exe", 0, bytes, sizeof bytes, done) && done == sizeof bytes);
    assert(bytes[0] == 'M' && bytes[1] == 'Z');
    assert(image.Child("D:\\Games", 0, entry));
    assert(!strcmp(entry.name, "Install All Games.exe"));
    assert(!image.Child("D:\\Games", 1, entry));
    assert(!image.Find("D:\\Games\\Install Colony.exe", entry));
    assert(!image.Read("D:\\Games\\Install All Games.exe", 2037, bytes, sizeof bytes, done));
    std::ifstream source(argv[2], std::ios::binary);
    std::vector<unsigned char> expected((std::istreambuf_iterator<char>(source)), {});
    assert(expected.size() >= 2037 + sizeof bytes);
    assert(!memcmp(bytes, expected.data() + 2037, sizeof bytes));
    assert(!image.Read("D:\\notes.txt", 0, bytes, sizeof bytes, done) && done == 12);
    assert(!memcmp(bytes, "Inline notes", 12));
    assert(!image.Read("D:\\long.bin", 2037, bytes, 1000, done) && done == 1000);
    assert(!memcmp(bytes, expected.data() + 2037, 1000));
    assert(image.Write("D:\\Games\\Install All Games.exe", 0, bytes, 1, done) == 19);
    puts("HOST UDF PASS");
}
