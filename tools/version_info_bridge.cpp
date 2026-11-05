// SPDX-License-Identifier: GPL-2.0-or-later
// Host entry points into the implementation of src/win98_version_info.h, so
// tools/test_version_info_original.py can put the same input through the
// original 16-bit VER.DLL under Unicorn and through this code.
#include "../src/win98_version_info.h"

extern "C" int version_query(unsigned char *bytes, unsigned size, const char *sub, unsigned *offset,
                             unsigned *length)
{
    Win98VersionInfo::BufferMemory block(bytes, size);
    unsigned value = 0, value_length = 0;
    if (!Win98VersionInfo::QueryValue(block, Win98VersionInfo::BufferMemory::Base, sub, value, value_length))
        return 0;
    *offset = value - Win98VersionInfo::BufferMemory::Base;
    *length = value_length;
    return 1;
}
// over a whole block, the way GetFileVersionInfo runs it.
extern "C" unsigned version_convert(unsigned char *bytes, unsigned size, unsigned limit)
{
    Win98VersionInfo::BufferMemory block(bytes, size);
    const unsigned base = Win98VersionInfo::BufferMemory::Base;
    return Win98VersionInfo::Convert(block, base, base, base + limit);
}
extern "C" unsigned version_language(unsigned language, char *out, unsigned size)
{
    unsigned char bytes[1024] = {};
    Win98VersionInfo::BufferMemory block(bytes, sizeof bytes);
    const unsigned result =
        Win98VersionInfo::LanguageName(block, language, Win98VersionInfo::BufferMemory::Base, size);
    for (unsigned i = 0; i < size && i < sizeof bytes; ++i) out[i] = (char)bytes[i];
    return result;
}
