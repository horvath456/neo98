// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/user_draw.h"
#include "../build/generated/user_vectors.h"
#include <cstdio>
#include <cstring>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "USER/GDI check failed at %d: %s\n", __LINE__, #x); return 1; } } while (0)

static unsigned Crc(const unsigned char *bytes, unsigned count)
{
    unsigned crc = ~0u;
    for (unsigned i = 0; i < count; ++i) {
        crc ^= bytes[i];
        for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    return ~crc;
}

int main()
{
    constexpr unsigned width = 96, height = 80, size = width * height * 4;
    unsigned char storage[size + 128];
    std::memset(storage, 0xa5, sizeof storage);
    auto *pixels = storage + 64;
    auto dc = Gdi::CreateSurfaceDC({pixels, width, height, width * 4, 32});
    auto brush = Gdi::CreateSolidBrush(0x123456);
    CHECK(dc && brush);
    auto previous = Gdi::SelectObject(dc, brush);
    CHECK(previous);
    Gdi::SetTextColor(dc, 0x394857); Gdi::SetBkColor(dc, 0x657483);
    const unsigned live = Gdi::LiveObjects();
    const unsigned edges = sizeof UserEdgeVectors / sizeof UserEdgeVectors[0];
    const unsigned buttons = sizeof UserButtonVectors / sizeof UserButtonVectors[0];
    unsigned count = 0;
    for (unsigned i = 0; i < edges + buttons; ++i) {
        const bool button = i >= edges;
        const auto &v = button ? UserButtonVectors[i - edges] : UserEdgeVectors[i];
        // The public adapter currently uses a 32-bit display and one-pixel borders.
        if (v[6] != 32 || v[7] != 1 || v[8] != 1) continue;
        for (unsigned p = 0; p < size; p += 4) {
            pixels[p] = 0x56; pixels[p + 1] = 0x34; pixels[p + 2] = 0x12; pixels[p + 3] = 0;
        }
        Gdi::RECT r = {(int)v[0], (int)v[1], (int)v[2], (int)v[3]};
        const bool result = button ? Win98Draw::ButtonFrame(dc, &r, v[4], v[5]) : Win98Draw::DrawEdge(dc, &r, v[4], v[5]);
        if (result != bool(v[13]) || r.left != (int)v[9] || r.top != (int)v[10] ||
            r.right != (int)v[11] || r.bottom != (int)v[12] || Crc(pixels, size) != v[14]) {
            std::fprintf(stderr, "Original USER vector %u differs through native GDI\n", i);
            return 1;
        }
        CHECK(Gdi::LiveObjects() == live);
        for (unsigned b = 0; b < 64; ++b) CHECK(storage[b] == 0xa5 && storage[size + 64 + b] == 0xa5);
        ++count;
    }
    // Pattern realization must stay anchored to the destination surface when
    // the window origin moves, and obey the DC clip without touching its state.
    std::memset(pixels, 0x12, size);
    CHECK(Gdi::SetViewportOrgEx(dc, 3, 2));
    CHECK(Gdi::IntersectClipRect(dc, 17, 18, 30, 33));
    Gdi::RECT r = {10, 10, 70, 55};
    CHECK(Win98Draw::ButtonFrame(dc, &r, 0x400, 0x3000));
    CHECK(r.left == 12 && r.top == 12 && r.right == 68 && r.bottom == 53);
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
        unsigned expected = 0x12121212;
        if (x >= 20 && x < 33 && y >= 20 && y < 35) expected = (x + y) & 1 ? 0xffffff : 0xc0c0c0;
        unsigned actual; std::memcpy(&actual, pixels + (y * width + x) * 4, 4);
        CHECK(actual == expected);
    }
    CHECK(Gdi::SetTextColor(dc, 0) == 0x394857 && Gdi::SetBkColor(dc, 0) == 0x657483);
    CHECK(Gdi::SelectObject(dc, previous) == brush && Gdi::DeleteObject(brush));
    CHECK(Gdi::DeleteDC(dc) && Gdi::LiveObjects() == 0);
    std::printf("Native USER/GDI: %u original vectors, pattern origin, clipping, DC state, guards and cleanup passed\n", count);
}
