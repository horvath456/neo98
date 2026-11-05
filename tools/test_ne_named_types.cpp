// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_ne_image.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main()
{
    unsigned char file[1024]{};
    auto word = [&](unsigned at, unsigned value) { file[at] = value; file[at + 1] = value >> 8; };
    file[0] = 'M'; file[1] = 'Z'; file[0x3c] = 0x80;
    file[0x80] = 'N'; file[0x81] = 'E'; word(0xa4, 0x40);
    const unsigned table = 0xc0;
    word(table + 2, 0x60); word(table + 4, 2);
    word(table + 10, 0x300); word(table + 12, 4); word(table + 16, 0x70);
    word(table + 22, 0x310); word(table + 24, 4); word(table + 28, 0x8007);
    word(table + 34, 0x800a); word(table + 36, 1);
    word(table + 42, 0x320); word(table + 44, 4); word(table + 48, 0x70);
    file[table + 0x60] = 11; memcpy(file + table + 0x61, "ZIPRESOURCE", 11);
    file[table + 0x70] = 7; memcpy(file + table + 0x71, "ZIPDATA", 7);
    Win98Ne::Image image;
    unsigned error = 0, offset = 0, size = 0;
    assert(image.Parse(file, sizeof file, error));
    assert(image.ResourceAt(0, 0, "zipdata", offset, size, "zipresource") && offset == 0x300 && size == 4);
    assert(image.ResourceAt(0, 0x8007, nullptr, offset, size, "ZIPRESOURCE") && offset == 0x310);
    assert(image.ResourceAt(0x800a, 0, "ZIPDATA", offset, size) && offset == 0x320);
    assert(!image.ResourceAt(0, 0, "ZIPDATA", offset, size, "OTHER"));
    assert(!image.ResourceAt(0, 0, "OTHER", offset, size, "ZIPRESOURCE"));
    assert(!image.ResourceAt(0, 0x8008, nullptr, offset, size, "ZIPRESOURCE"));
    assert(!image.ResourceAt(0, 0, "ZIPDATA", offset, size));
    // A counted type string outside the file is rejected.
    word(table + 2, 1023 - table); file[1023] = 12;
    assert(!image.ResourceAt(0, 0, "ZIPDATA", offset, size, "ZIPRESOURCE"));
    puts("PASS NE named resource types: strings, ordinals, case, missing and malformed keys");
}
