// SPDX-License-Identifier: GPL-2.0-or-later
// Host bridge for the original GDI object-ownership comparison. It exposes the
// implemented cores in src/win98_gdi_owner.h over a flat arena that mirrors
// the 16-bit GDI heap selector, so tools/test_gdi_owner_original.py can run the
// unchanged GDI.EXE code against them.
#include "../src/win98_gdi_owner.h"
#include <stdio.h>
#include <string.h>

namespace {
struct Arena {
    unsigned char *bytes;
    unsigned size;
    unsigned HandleBase() const { return 0x10000; }
    unsigned Read8(unsigned at) const { return at < size ? bytes[at] : 0; }
    unsigned Read16(unsigned at) const { return at + 1 < size ? bytes[at] | (bytes[at + 1] << 8) : 0; }
    unsigned Read32(unsigned at) const
    {
        return at + 3 < size ? bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16) |
                                   (unsigned(bytes[at + 3]) << 24)
                             : 0;
    }
    void Write16(unsigned at, unsigned value)
    {
        if (at + 1 >= size) return;
        bytes[at] = (unsigned char)value;
        bytes[at + 1] = (unsigned char)(value >> 8);
    }
    unsigned External(unsigned, unsigned &object) { object = 0; return 0; }
};
} // namespace

extern "C" unsigned gdi_owner_call(unsigned char *bytes, unsigned size, unsigned op, unsigned handle,
                                   unsigned argument)
{
    Arena arena{bytes, size};
    switch (op)
    {
    case 0: return Win98GdiOwner::IsGdiObject(arena, handle);
    case 1: return Win98GdiOwner::ExchangeOwnerAt(arena, handle, argument);
    case 2: return Win98GdiOwner::MakePrivateAt(arena, handle, argument != 0);
    case 3: return Win98GdiOwner::Signature(arena, handle);
    case 4: return Win98GdiOwner::DeleteRefused(argument, Win98GdiOwner::DeleteMask) ? 1 : 0;
    case 5: return Win98GdiOwner::DeleteRefused(argument, Win98GdiOwner::SysDeleteMask) ? 1 : 0;
    case 6: return Win98GdiOwner::ClearFlags(argument);
    case 7: return Win98GdiOwner::OwnerGate(argument) ? 1 : 0;
    case 8: return Win98GdiOwner::DeleteGate(argument) ? 1 : 0;
    default: return 0;
    }
}

#ifdef GDI_OWNER_STANDALONE
// Replays the sparse corpus the comparison wrote, so the same cases run under
// AddressSanitizer and UndefinedBehaviorSanitizer.
static bool ReadPairs(FILE *file, unsigned count, unsigned char *arena, unsigned size)
{
    for (unsigned i = 0; i < count; ++i)
    {
        unsigned offset = 0;
        unsigned char value = 0;
        if (fread(&offset, 4, 1, file) != 1 || fread(&value, 1, 1, file) != 1) return false;
        if (offset >= size) return false;
        arena[offset] = value;
    }
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    FILE *file = fopen(argv[1], "rb");
    if (!file) return 2;
    const unsigned size = 0x20000;
    unsigned char *arena = new unsigned char[size];
    unsigned char *expected = new unsigned char[size];
    unsigned header[5];
    unsigned long cases = 0;
    int status = 0;
    while (fread(header, sizeof header, 1, file) == 1)
    {
        memset(arena, 0, size);
        if (!ReadPairs(file, header[4], arena, size)) { status = 3; break; }
        memcpy(expected, arena, size);
        unsigned changed = 0;
        if (fread(&changed, 4, 1, file) != 1) { status = 3; break; }
        if (!ReadPairs(file, changed, expected, size)) { status = 3; break; }
        const unsigned got = gdi_owner_call(arena, size, header[0], header[1], header[2]);
        if (got != header[3] || memcmp(arena, expected, size))
        {
            fprintf(stderr, "case %lu mismatch op=%u handle=%08x\n", cases, header[0], header[1]);
            status = 1;
            break;
        }
        ++cases;
    }
    delete[] arena;
    delete[] expected;
    fclose(file);
    if (!status) printf("GDI OWNER SANITIZER PASS cases=%lu\n", cases);
    return status;
}
#endif
