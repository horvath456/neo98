// SPDX-License-Identifier: GPL-2.0-or-later
#include "bitmap_bridge.cpp"
#include "../src/win98_pattern_brush.h"
struct PatternEnvironment : BitmapEnvironment
{
    U pattern_failure = 0, pattern_locals = 0;
    std::vector<unsigned char> snapshot;
    PatternEnvironment() : BitmapEnvironment(0, 0x400, 0x1234) {}
    U LocalAllocate(U flags, U bytes)
    {
        ++pattern_locals;
        const bool fail = (pattern_failure == 1 && pattern_locals == 1) ||
                          (pattern_failure == 3 && pattern_locals == 2);
        const U handle = fail ? 0 : 0x1002 + locals * 4;
        Event(7, flags, bytes, handle);
        if (!handle) return 0;
        const U body = 0x2000 + locals++ * 0x40;
        Write16(0x2f0000 + handle, body); Write16(0x2f0000 + handle + 2, 0);
        for (U i = 0; i < bytes; ++i) Write8(0x2f0000 + body + i, 0xcc);
        return handle;
    }
    U GlobalLock(U handle)
    {
        ++locks;
        const U address = pattern_failure == 4 && locks == 4 ? 0 : Global(handle).address;
        Event(4, handle, address); return address;
    }
    U LockBitmap(U h) { return Win98Bitmap::Lock(*this, h); }
    void UnlockBitmap(U h) { Win98Bitmap::Unlock(*this, h); }
    U CreateBitmap(U p)
    {
        return Win98Bitmap::Create(*this, state, {Read16(p), Read16(p + 2), Read16(p + 4),
              Read16(p + 6), Read8(p + 8), Read8(p + 9), ReadPointer(p + 10)});
    }
    U SetBitmapBits(U h, U count, U bits) { return Win98Bitmap::SetBits(*this, h, count, bits); }
    U DriverCopyBitmap(U, U, U) { std::abort(); }
    bool BitmapDeviceMatchesScreen(U) { std::abort(); }
    U CreateScreenBitmap(U, U) { std::abort(); }
    bool DibSectionPattern(U, U) { Event(16); return false; }
    U HeapFloor() const { return 0x100; }
    void InvalidBitmap(U h) { Event(15, h, 0x6066); }
    U CallerModule() { Event(14, 0x4242); return 0x4242; }
    U Run(const U *a)
    {
        switch (a[0])
        {
            case 0: return Win98Bitmap::Create(*this, state, {0, a[1], a[2], a[5], a[3], a[4], a[6]});
            case 1: return Win98Bitmap::SetBits(*this, a[1], a[2], a[3]);
            case 2: return Win98Bitmap::GetBits(*this, a[1], a[2], a[3]);
            case 3: return LockBitmap(a[1]);
            case 4: UnlockBitmap(a[1]); return 0;
            case 5: return Win98Bitmap::Destroy(*this, state, a[1]);
            case 6: Write16(Object(a[1]) + a[2], a[3]); return 0;
            case 7: Write8(a[1], a[2]); return 0;
            case 8: pattern_failure = a[1]; mode = a[1] == 2 ? 1 : 0;
                    pattern_locals = allocations = locks = 0; return 0;
            case 9: return Win98PatternBrush::Create(*this, state.serial, a[1]);
            case 10: return Win98PatternBrush::PublicCreate(*this, state.serial, a[1]);
            case 11: return Win98PatternBrush::CopyBitmap(*this, a[1], a[2]);
            case 12: return Win98PatternBrush::UnrealizedSoftwareDestroy(*this, a[1]);
            default: std::abort();
        }
    }
    void Word(U value) { for (U i = 0; i < 4; ++i) snapshot.push_back(value >> (8 * i)); }
    void Snapshot()
    {
        snapshot.clear(); Word(state.empty_bitmap); Word(state.empty_storage); Word(state.serial);
        Word(events.size()); for (U value : events) Word(value);
        snapshot.insert(snapshot.end(), memory.begin() + 0x2f1000, memory.begin() + 0x2f3000);
        Word(globals.size());
        for (const auto &block : globals)
        {
            Word(block.handle); Word(block.address); Word(block.bytes); Word(block.freed);
            snapshot.insert(snapshot.end(), memory.begin() + block.address, memory.begin() + block.address + block.bytes);
        }
    }
};
struct MonoEnvironment
{
    unsigned char memory[1024]{};
    U Read8(U p) { assert(p < sizeof memory); return memory[p]; }
    U Read16(U p) { return Read8(p) | Read8(p + 1) << 8; }
    U ReadPointer(U p) { return Read16(p) | Read16(p + 2) << 16; }
    void Write8(U p, U v) { assert(p < sizeof memory); memory[p] = v; }
    void Write16(U p, U v) { Write8(p, v); Write8(p + 1, v >> 8); }
    void Write32(U p, U v) { Write16(p, v); Write16(p + 2, v >> 16); }
};
extern "C" {
void *pattern_new() { return new PatternEnvironment; }
void pattern_delete(void *p) { delete static_cast<PatternEnvironment *>(p); }
U pattern_run(void *p, const U *a) { return static_cast<PatternEnvironment *>(p)->Run(a); }
const unsigned char *pattern_snapshot(void *p, U *n)
{ auto &e = *static_cast<PatternEnvironment *>(p); e.Snapshot(); *n = e.snapshot.size(); return e.snapshot.data(); }
void pattern_mono(const unsigned char *input, U stride, U x, U y, unsigned char *output)
{
    MonoEnvironment e;
    for (U i = 0; i < 64; ++i) e.Write8(0x200 + i, input[i]);
    e.Write16(0x100 + 6, stride); e.Write32(0x100 + 10, 0x200);
    for (U i = 0; i < 64; ++i) e.Write8(0x300 + i, 0xa0 + i);
    Win98PatternBrush::RealizeMono8(e, 0x100, 0x300, x, y);
    for (U i = 0; i < 64; ++i) output[i] = e.Read8(0x300 + i);
}
U pattern_color(U bit, U text, U background)
{ return Win98PatternBrush::MonoColor(bit, text, background); }
}
#ifdef PATTERN_BRUSH_STANDALONE
int main(int argc, char **argv)
{
    assert(argc == 3);
    FILE *f = std::fopen(argv[1], "rb"); assert(f);
    U count; assert(std::fread(&count, 4, 1, f) == 1);
    for (U i = 0; i < count; ++i)
    {
        PatternEnvironment e; U steps; assert(std::fread(&steps, 4, 1, f) == 1);
        for (U j = 0; j < steps; ++j)
        {
            U a[8], expected, bytes;
            assert(std::fread(a, 4, 8, f) == 8);
            assert(std::fread(&expected, 4, 1, f) == 1);
            assert(std::fread(&bytes, 4, 1, f) == 1);
            std::vector<unsigned char> original(bytes);
            assert(std::fread(original.data(), 1, bytes, f) == bytes);
            assert(e.Run(a) == expected); e.Snapshot(); assert(e.snapshot == original);
        }
    }
    assert(std::fgetc(f) == EOF); std::fclose(f);
    f = std::fopen(argv[2], "rb"); assert(f);
    U mono = 0, parameters[3]; unsigned char input[64], expected[64], output[64];
    while (std::fread(parameters, sizeof parameters, 1, f) == 1)
    {
        assert(std::fread(input, 64, 1, f) == 1 && std::fread(expected, 64, 1, f) == 1);
        pattern_mono(input, parameters[0], parameters[1], parameters[2], output);
        for (U i = 0; i < 64; ++i) assert(output[i] == expected[i]);
        ++mono;
    }
    assert(!std::ferror(f)); std::fclose(f);
    std::printf("PATTERN BRUSH ASAN/UBSAN PASS: %u sequences, %u monochrome realizations\n", count, mono);
}
#endif
