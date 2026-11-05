// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/gdi.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace Gdi;
#define CHECK(x)                                                                                             \
    do                                                                                                       \
    {                                                                                                        \
        if (!(x))                                                                                            \
        {                                                                                                    \
            std::fprintf(stderr, "Native bitmap check line %d: %s\n", __LINE__, #x);                         \
            return 1;                                                                                        \
        }                                                                                                    \
    } while (0)
struct Memory final : BitmapIO
{
    std::vector<unsigned char> bytes = std::vector<unsigned char>(0x100000);
    unsigned error = 0, faults = 0, reads = 0, writes = 0;
    bool Range(unsigned p, unsigned n) const
    {
        return p >= 0x1000 && p - 0x1000 < bytes.size() && n <= bytes.size() - (p - 0x1000);
    }
    bool Read(unsigned p, void *out, unsigned n) override
    {
        ++reads;
        if (!Range(p, n))
            return false;
        std::memcpy(out, bytes.data() + p - 0x1000, n);
        return true;
    }
    bool Write(unsigned p, const void *in, unsigned n) override
    {
        ++writes;
        if (!Range(p, n))
            return false;
        std::memcpy(bytes.data() + p - 0x1000, in, n);
        return true;
    }
    bool Probe(unsigned p, bool) override { return Range(p, 1); }
    void Error(unsigned e) override { error = e; }
    void Fault(const char *) override { ++faults; }
    unsigned Call(const char *name, unsigned a = 0, unsigned b = 0, unsigned c = 0, unsigned d = 0,
                  unsigned e = 0)
    {
        const unsigned args[] = {a, b, c, d, e};
        return BitmapApi(name, args, *this);
    }
};
int main()
{
    Memory m;
    CHECK(!LiveObjects());
    for (unsigned i = 0; i < 16; i++)
        m.bytes[i] = i & 1 ? 0 : 0xf0;
    m.error = 0x12345678;
    const auto bitmap = m.Call("CreateBitmap", 8, 8, 1, 1, 0x1000);
    CHECK(bitmap && m.error == 0x12345678);
    unsigned info[6];
    CHECK(BitmapInfo(bitmap, info) && info[0] == 0 && info[1] == 8 && info[2] == 8 && info[3] == 2 &&
          info[4] == 0x10001 && !info[5]);
    CHECK(m.Call("GetObjectA", bitmap, 0, 0) == 84);
    memset(m.bytes.data() + 0x3000, 0xa5, 100);
    CHECK(m.Call("GetObjectA", bitmap, 100, 0x4000) == 84 && m.bytes[0x3000 + 84] == 0xa5);
    CHECK(!memcmp(m.bytes.data() + 0x3000, info, 24));
    for (unsigned i = 24; i < 84; i++)
        CHECK(!m.bytes[0x3000 + i]);
    CHECK(!m.Call("GetObjectA", bitmap, 0, 0x4000) && m.error == 87);
    CHECK(m.Call("GetBitmapBits", bitmap, 100, 0x2000) == 16 &&
          !memcmp(m.bytes.data(), m.bytes.data() + 0x1000, 16));
    CHECK(!m.Call("GetBitmapBits", bitmap, 0, 0x2000) && m.error == 87);
    CHECK(!m.Call("GetBitmapBits", bitmap, 16, 0xfffffff0) && m.error == 87);
    const auto dc = CreateCompatibleDC();
    const auto old = SelectObject(dc, bitmap);
    CHECK(old && SaveDC(dc) == 1 && DeleteObject(bitmap) && ObjectExists(bitmap));
    CHECK(SelectObject(dc, old) == bitmap && DeleteObject(bitmap) && ObjectExists(bitmap));
    CHECK(RestoreDC(dc, -1) && SelectObject(dc, old) == bitmap && DeleteObject(bitmap) &&
          !ObjectExists(bitmap));
    CHECK(DeleteDC(dc));
    // The public native handle exceeds WORD after enough slot reuses. The ABI
    // boundary must translate it to the original local ID before narrowing.
    unsigned highest = 0;
    for (unsigned i = 0; i < 270; i++)
    {
        const auto b = m.Call("CreateBitmap", 8, 8, 1, 1, 0x1000);
        CHECK(b && m.Call("GetBitmapBits", b, 16, 0x2000) == 16);
        if (b > highest)
            highest = b;
        CHECK(DeleteObject(b));
    }
    CHECK(highest > 0xffff);
    unsigned record[] = {7, 8, 8, 999, 0x10001, 0x1000};
    memcpy(m.bytes.data() + 0x3000, record, sizeof record);
    const auto indirect = m.Call("CreateBitmapIndirect", 0x4000);
    CHECK(indirect && BitmapInfo(indirect, info) && info[0] == 0 && info[3] == 2);
    const auto pattern = CreatePatternBrush(indirect);
    CHECK(pattern);
    memset(m.bytes.data(), 0, 16);
    CHECK(m.Call("SetBitmapBits", indirect, 16, 0x1000) == 16);
    CHECK(DeleteObject(indirect));
    unsigned pixels[16 * 16] = {};
    const auto surface = CreateSurfaceDC({reinterpret_cast<unsigned char *>(pixels), 16, 16, 16 * 4, 32});
    CHECK(surface && SelectObject(surface, pattern));
    SetTextColor(surface, RGB(255, 0, 0));
    SetBkColor(surface, RGB(0, 0, 255));
    CHECK(PatBlt(surface, 0, 0, 16, 16, PATCOPY));
    for (unsigned y = 0; y < 16; y++)
        for (unsigned x = 0; x < 16; x++)
            CHECK(pixels[y * 16 + x] == ((x & 7) < 4 ? 0x0000ff : 0xff0000));
    SetTextColor(surface, RGB(0, 255, 0));
    SetBkColor(surface, RGB(255, 255, 255));
    const RECT area = {0, 0, 16, 16};
    CHECK(FillRect(surface, &area, pattern));
    for (unsigned y = 0; y < 16; y++)
        for (unsigned x = 0; x < 16; x++)
            CHECK(pixels[y * 16 + x] == ((x & 7) < 4 ? 0xffffff : 0x00ff00));
    CHECK(!DeleteObject(pattern) && DeleteDC(surface) && DeleteObject(pattern));
    // No eager scan of a null source, even when the original allocator rejects
    // the request. Quota rejection still goes through the thunk's error path.
    const auto reads = m.reads;
    CHECK(!m.Call("CreateBitmap", 32767, 32767, 1, 32, 0) && m.error == 87 && m.reads == reads);
    CHECK(!m.Call("CreateBitmap", 0x8000, 8, 1, 1, 0) && m.error == 87);
    const auto empty = m.Call("CreateBitmap", 0, 8, 1, 1, 0);
    CHECK(empty && BitmapInfo(empty, info) && info[1] == 1 && info[2] == 1 && DeleteObject(empty) &&
          ObjectExists(empty));
    CHECK(m.Call("CreateBitmap", 0x80000000, 8, 1, 1, 0) == empty);
    // 30-byte rows leave 16-byte gaps after every 2184 rows in original storage.
    constexpr unsigned width = 240, height = 3000, stride = 30, size = height * stride;
    for (unsigned i = 0; i < size; i++)
        m.bytes[i] = (i * 37u + i / stride) & 255;
    const auto src = m.Call("CreateBitmap", width, height, 1, 1, 0x1000);
    const auto dst = m.Call("CreateBitmap", width, height, 1, 1, 0);
    CHECK(src && dst);
    CHECK(m.Call("GetBitmapBits", src, size, 0x30000) == size &&
          !memcmp(m.bytes.data(), m.bytes.data() + 0x2f000, size));
    const auto sd = CreateCompatibleDC(), dd = CreateCompatibleDC();
    CHECK(sd && dd && SelectObject(sd, src) && SelectObject(dd, dst));
    CHECK(BitBlt(dd, 0, 0, width, height, sd, 0, 0, SRCCOPY));
    CHECK(m.Call("GetBitmapBits", dst, size, 0x50000) == size &&
          !memcmp(m.bytes.data(), m.bytes.data() + 0x4f000, size));
    // A later SetBitmapBits must invalidate the native linear drawing view.
    memset(m.bytes.data(), 0xa5, size);
    CHECK(m.Call("SetBitmapBits", src, size, 0x1000) == size);
    CHECK(BitBlt(dd, 0, 0, width, height, sd, 0, 0, SRCCOPY));
    CHECK(m.Call("GetBitmapBits", dst, size, 0x50000) == size &&
          !memcmp(m.bytes.data(), m.bytes.data() + 0x4f000, size));
    CHECK(DeleteObject(src) && ObjectExists(src));
    CHECK(DeleteDC(sd) && DeleteDC(dd) && DeleteObject(src) && DeleteObject(dst));
    CHECK(!m.faults);
    ShutdownBitmaps();
    CHECK(!LiveObjects());
    // Invalid indirect pointers stop at the native fault boundary and leave no
    // descriptor/global allocation behind, including a fresh stock bootstrap.
    CHECK(!m.Call("CreateBitmapIndirect", 0xfffffff0) && m.faults == 1);
    ShutdownBitmaps();
    CHECK(!LiveObjects());
    // A pattern brush outlives the bitmap it was built from and holds the
    // store open. A teardown while such a brush is alive has to complete when
    // the brush goes, or the stock bitmap stays behind for good.
    for (unsigned i = 0; i < 16; ++i) m.bytes[i] = i & 1 ? 0 : 0x55;
    const auto halftone = m.Call("CreateBitmap", 8, 8, 1, 1, 0x1000);
    CHECK(halftone);
    const auto halftone_brush = CreatePatternBrush(halftone);
    CHECK(halftone_brush && DeleteObject(halftone) && !ObjectExists(halftone));
    ShutdownBitmaps();
    CHECK(LiveObjects() == 2); // The brush and the stock bitmap it keeps alive.
    CHECK(DeleteObject(halftone_brush) && !ObjectExists(halftone_brush));
    CHECK(m.faults == 1 && !LiveObjects());
    ShutdownBitmaps();
    CHECK(!LiveObjects());
    std::puts("NATIVE BITMAP PASS: original DDB lifecycle, WORD/native handles, saved selection, thunk "
              "errors, 64-KiB gaps and drawing synchronization");
}
