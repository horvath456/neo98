// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_bitmap.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
using U = unsigned;
struct BitmapEnvironment
{
    struct Block { U handle, address, bytes; bool freed = false; };
    std::vector<unsigned char> memory = std::vector<unsigned char>(0x2000000);
    std::vector<U> events;
    std::vector<Block> globals;
    Win98Bitmap::State state;
    U mode, version, allocations = 0, locks = 0, locals = 0;
    BitmapEnvironment(U failure, U expected, U stock) : mode(failure), version(expected)
    {
        state = {stock, 0, 123};
        for (U i = 0; i < 0x180000; ++i) memory[0x400000 + i] = (i * 37 + (i >> 8)) & 255;
        for (U i = 0; i < 0x180000; ++i) memory[0x600000 + i] = 0x77;
    }
    void Check(U p, U n) const { assert(p <= memory.size() && n <= memory.size() - p); }
    U Read8(U p) const { Check(p, 1); return memory[p]; }
    U Read16(U p) const { return Read8(p) | Read8(p + 1) << 8; }
    U Read32(U p) const { return Read16(p) | Read16(p + 2) << 16; }
    void Write8(U p, U v) { Check(p, 1); memory[p] = v; }
    void Write16(U p, U v) { Write8(p, v); Write8(p + 1, v >> 8); }
    void Write32(U p, U v) { Write16(p, v); Write16(p + 2, v >> 16); }
    U Address(U raw) const
    {
        if (!raw) return 0;
        const U segment = (raw >> 16) / 8, offset = raw & 65535;
        if (segment == 64) return 0x400000 + offset;
        if (segment == 65) return 0x600000 + offset;
        if (segment == 57) return 0x390000 + offset;
        if (segment >= 81 && segment < 81 + globals.size()) return 0x800000 + (segment - 81) * 0x100000 + offset;
        assert(false); return 0;
    }
    U Pointer(U address) const
    {
        if (!address) return 0;
        U segment = 0, offset = 0;
        if (address >= 0x800000) { segment = 81 + (address - 0x800000) / 0x100000; offset = (address - 0x800000) % 0x100000; }
        else if (address >= 0x600000) { segment = 65; offset = address - 0x600000; }
        else if (address >= 0x400000) { segment = 64; offset = address - 0x400000; }
        else { segment = 57; offset = address - 0x390000; }
        assert(offset <= 65535); return (segment * 8 << 16) | offset;
    }
    U ReadPointer(U p) const { return Address(Read32(p)); }
    void WritePointer(U p, U v) { Write32(p, Pointer(v)); }
    void Event(U kind, U a = 0, U b = 0, U c = 0) { events.insert(events.end(), {kind, a, b, c}); }
    U GetCurrentTask() { Event(1); return 1; }
    U GetExpectedVersion(U task) { Event(2, task); return version; }
    U GlobalAllocate(U flags, U bytes)
    {
        ++allocations;
        if ((mode == 1 && allocations == 1) || (mode == 2 && allocations == 2) || bytes > 0x100000)
        { Event(3, flags, bytes); return 0; }
        const U handle = 0x5001 + globals.size(), address = 0x800000 + globals.size() * 0x100000;
        Check(address, bytes); globals.push_back({handle, address, bytes});
        for (U i = 0; i < bytes; ++i) memory[address + i] = 0xa5;
        Event(3, flags, bytes, handle); return handle;
    }
    Block &Global(U handle) { assert(handle >= 0x5001 && handle < 0x5001 + globals.size()); return globals[handle - 0x5001]; }
    U GlobalLock(U handle)
    {
        ++locks; auto &block = Global(handle);
        const U p = (mode >= 4 && mode <= 6 && locks == mode - 3) ? 0 : block.address;
        Event(4, handle, p); return p;
    }
    void GlobalUnlock(U handle) { Global(handle); Event(5, handle); }
    void GlobalFree(U handle)
    {
        auto &block = Global(handle); assert(!block.freed); block.freed = true;
        for (U i = 0; i < block.bytes; ++i) memory[block.address + i] = 0xdd;
        Event(6, handle);
    }
    U GlobalSmartPageLock(U handle) { Event(9, handle); return mode == 7 ? 0 : 1; }
    U LocalAllocate(U flags, U bytes)
    {
        const U handle = mode == 3 ? 0 : 0x1002 + locals * 4;
        Event(7, flags, bytes, handle);
        if (!handle) return 0;
        const U body = 0x2000 + locals++ * 0x40;
        Write16(0x2f0000 + handle, body); Write16(0x2f0000 + handle + 2, 0);
        for (U i = 0; i < bytes; ++i) Write8(0x2f0000 + body + i, 0xcc);
        return handle;
    }
    U Object(U handle) const { return 0x2f0000 + Read16(0x2f0000 + handle); }
    void MarkLocal(U handle, U mask) { const U p = 0x2f0000 + handle + 2; Write8(p, Read8(p) | mask); }
    void LocalFree(U handle) { Write16(Object(handle) + 2, 0); Event(8, handle); }
    void ProbeBits(U bits, U count) { Event(10, bits, count); }
    bool DeviceCapable(U) { return false; }
    Win98Bitmap::DeviceResult RealizeBitmap(U, const Win98Bitmap::Bitmap &, U) { std::abort(); }
    U DeviceHandle(U) { std::abort(); } U DeviceTable(U) { std::abort(); }
    U DriverBits(U, U, U, U, U) { std::abort(); }
    void DestroyDevice(U, U, U) { std::abort(); }
    void FreeSelector(U handle) { Event(11, handle); }
    void DibSectionCleanup(U) { std::abort(); }
    void ArithmeticFault() { Event(13); }
    U Run(const U *a)
    {
        switch (a[0])
        {
        case 0: return Win98Bitmap::Create(*this, state, {0, a[1], a[2], a[5], a[3], a[4], a[6]});
        case 1: return Win98Bitmap::SetBits(*this, a[1], a[2], a[3]);
        case 2: return Win98Bitmap::GetBits(*this, a[1], a[2], a[3]);
        case 3: return Win98Bitmap::Lock(*this, a[1]);
        case 4: Win98Bitmap::Unlock(*this, a[1]); return 0;
        case 5: return Win98Bitmap::Destroy(*this, state, a[1]);
        case 6: Write16(Object(a[1]) + a[2], a[3]); return 0;
        case 7: Write8(a[1], a[2]); return 0;
        case 8: mode = a[1]; allocations = locks = 0; return 0;
        default: std::abort();
        }
    }
};
extern "C" {
void *bitmap_new(U failure, U version, U stock) { return new BitmapEnvironment(failure, version, stock); }
void bitmap_delete(void *p) { delete static_cast<BitmapEnvironment *>(p); }
U bitmap_run(void *p, const U *a) { return static_cast<BitmapEnvironment *>(p)->Run(a); }
const unsigned char *bitmap_memory(void *p) { return static_cast<BitmapEnvironment *>(p)->memory.data(); }
const U *bitmap_events(void *p, U *n) { auto &v = static_cast<BitmapEnvironment *>(p)->events; *n = v.size(); return v.data(); }
void bitmap_state(void *p, U *out) { auto &s = static_cast<BitmapEnvironment *>(p)->state; out[0] = s.empty_bitmap; out[1] = s.empty_storage; out[2] = s.serial; }
}
#ifdef BITMAP_STANDALONE
int main(int argc, char **argv)
{
    assert(argc == 2); FILE *f = std::fopen(argv[1], "rb"); assert(f);
    U cases; assert(std::fread(&cases, 4, 1, f) == 1);
    for (U c = 0; c < cases; ++c)
    {
        U h[4]; assert(std::fread(h, 4, 4, f) == 4); BitmapEnvironment e(h[0], h[1], h[2]);
        for (U i = 0; i < h[3]; ++i) { U a[8]; assert(std::fread(a, 4, 8, f) == 8); e.Run(a); }
    }
    assert(std::fgetc(f) == EOF); std::fclose(f);
    std::printf("bitmap native ASan/UBSan: %u sequences passed\n", cases);
}
#endif
