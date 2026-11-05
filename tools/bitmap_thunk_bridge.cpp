// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_bitmap_thunk.h"
#include <assert.h>
#include <stdio.h>
using U = unsigned;
enum : U { InputWords = 16, OutputWords = 163 };
struct Environment
{
    const U *input;
    U *output, events = 0;
    unsigned char bitmap[24]{};
    Environment(const U *in, U *out) : input(in), output(out)
    {
        Store(0, in[1], 4); Store(4, in[2], 4); Store(8, in[3], 4);
        Store(12, in[4], 4); Store(16, in[5], 2); Store(18, in[6], 2);
        Store(20, in[7], 4); output[1] = 0xa5a5;
    }
    void Store(U offset, U value, U size)
    { for (U i = 0; i < size; ++i) bitmap[offset + i] = static_cast<unsigned char>(value >> (i * 8)); }
    void Event(U op, U a = 0, U b = 0, U c = 0, U d = 0, U e = 0, U f = 0, U g = 0)
    {
        assert(events < 20);
        const U values[8]{op, a, b, c, d, e, f, g};
        for (U i = 0; i < 8; ++i) output[3 + events * 8 + i] = values[i];
        output[2] = ++events;
    }
    U Read(U pointer, U size)
    {
        assert(pointer >= 0x310000 && pointer - 0x310000 <= 24 - size);
        const U offset = pointer - 0x310000;
        U value = 0;
        for (U i = 0; i < size; ++i) value |= U(bitmap[offset + i]) << (i * 8);
        Event(1, offset, size, value); return value;
    }
    U Read16(U pointer) { return Read(pointer, 2); }
    U Read32(U pointer) { return Read(pointer, 4); }
    bool MapBits(U pointer, U bytes, U &mapped, U &cookie)
    {
        const bool null = !pointer || !bytes;
        const bool failed = !null && (input[8] || bytes > 0xffff0000u);
        mapped = null || failed ? 0 : (49 * 8) << 16;
        cookie = null || failed ? 0 : ((49 * 8) << 16) | 1;
        Event(2, pointer, bytes, mapped, cookie, failed); return !failed;
    }
    U Result()
    { if (input[11]) output[1] = input[10]; return input[9]; }
    U CreateBitmap16(const Win98BitmapThunk::Bitmap16 &b)
    { Event(3, b.type, b.width, b.height, b.stride, b.planes, b.bpp, b.bits); return Result(); }
    U GetBitmapBits16(U handle, U count, U bits)
    { Event(6, handle, count, bits); return Result(); }
    U SetBitmapBits16(U handle, U count, U bits)
    { Event(7, handle, count, bits); return Result(); }
    void UnmapBits(U cookie) { Event(4, cookie); }
    void Error(U error) { output[1] = error; Event(5, error); }
};
extern "C" void bitmap_thunk(const U *input, U *output)
{
    for (U i = 0; i < OutputWords; ++i) output[i] = 0;
    Environment e(input, output);
    switch (input[0])
    {
        case 0: output[0] = Win98BitmapThunk::Create(e, input[2], input[3], input[5], input[6], input[7]); break;
        case 1: output[0] = Win98BitmapThunk::Indirect(e, 0x310000); break;
        case 2: output[0] = Win98BitmapThunk::GetBits(e, input[12], input[13], input[7]); break;
        case 3: output[0] = Win98BitmapThunk::SetBits(e, input[12], input[13], input[7]); break;
        default: assert(false);
    }
}
#ifdef BITMAP_THUNK_MAIN
int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb"); assert(file);
    U input[InputWords], expected[OutputWords], actual[OutputWords], count = 0;
    while (fread(input, sizeof input, 1, file) == 1)
    {
        assert(fread(expected, sizeof expected, 1, file) == 1);
        bitmap_thunk(input, actual);
        for (U i = 0; i < OutputWords; ++i) assert(expected[i] == actual[i]);
        ++count;
    }
    assert(!ferror(file)); fclose(file);
    printf("BITMAP THUNK ASAN/UBSAN PASS: %u calls\n", count);
}
#endif
