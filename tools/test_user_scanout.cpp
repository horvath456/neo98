// SPDX-License-Identifier: GPL-2.0-or-later
#include "gdi.h"
#include "user.h"
#include "gdi_truetype.h"
#include <cstdlib>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
// This pixel fixture never measures an outline font.
namespace GdiTrueType {
bool AbcWidths(const Font &, unsigned, unsigned, int *) { std::abort(); }
}
struct Scanout
{
    unsigned reads = 0;
    unsigned short color = 0x4210;
    bool ready = true;
    static bool Access(void *context, const unsigned char *bits, bool read)
    {
        auto &s = *static_cast<Scanout *>(context);
        if (!read) return s.ready;
        ++s.reads;
        if (bits) for (int y = 0; y < 480; ++y) for (int x = 0; x < 640; ++x)
            memcpy(const_cast<unsigned char *>(bits) + y * 1280 + x * 2, &s.color, 2);
        return true;
    }
};
int main()
{
    assert(User::Initialize());
    assert(User::SetScreenMode(640, 480, 16));
    User::ShowPointer(false); User::SetPointerShape(0);
    Scanout source;
    User::SetHostScanoutProvider(&source, Scanout::Access);
    assert(User::DeferScreenRead());
    User::ScreenChanged();
    assert(source.reads == 0);
    const auto *frame = reinterpret_cast<const unsigned *>(User::Pixels());
    assert(source.reads == 1 && (frame[0] & 0xffffff) == 0x848484);
    User::Pixels(); assert(source.reads == 1);
    User::ScreenChanged(); source.color = 0x7c00;
    frame = reinterpret_cast<const unsigned *>(User::Pixels());
    assert(source.reads == 2 && (frame[0] & 0xffffff) == 0xff0000);
    // Gamma, guest pointer and overlays use the established CPU compositor.
    unsigned short gamma[768];
    for (unsigned c = 0; c < 3; ++c) for (unsigned v = 0; v < 256; ++v) gamma[c * 256 + v] = v * 128;
    User::SetGammaRamp(gamma); assert(!User::DeferScreenRead());
    frame = reinterpret_cast<const unsigned *>(User::Pixels());
    assert((frame[0] & 0xffffff) == 0x7f0000);
    for (unsigned c = 0; c < 3; ++c) for (unsigned v = 0; v < 256; ++v) gamma[c * 256 + v] = v * 257;
    User::SetGammaRamp(gamma); assert(User::DeferScreenRead());
    User::SetPointerShape(1); assert(!User::DeferScreenRead()); User::SetPointerShape(0);
    unsigned overlay[4]{}; User::SetOverlay(overlay, 2, 2);
    assert(!User::DeferScreenRead()); User::ClearOverlay();
    // Settling a flip must materialize the current buffer before copying it.
    std::vector<unsigned char> back(640 * 480 * 2, 0);
    User::SetScanout(back.data(), 1280); User::ScreenChanged();
    source.color = 0x001f; User::SettleScanout();
    unsigned short pixel = 0; memcpy(&pixel, User::ScreenBits(), 2); assert(pixel == 0x001f);
    // Detaching a host provider leaves the last picture readable.
    source.color = 0x03e0; User::ScreenChanged();
    User::SetHostScanoutProvider(nullptr, nullptr);
    frame = reinterpret_cast<const unsigned *>(User::Pixels());
    assert((frame[0] & 0xffffff) == 0x00ff00);
    // CPU scanout must see unannounced writes, including a store AFTER the
    // scan tick but BEFORE Pixels, and a changed flipped buffer.
    auto *raw = const_cast<unsigned char *>(User::ScreenBits());
    unsigned short direct = 0x7c00;
    memcpy(raw, &direct, 2);
    User::ScreenChanged(); User::ScanoutScreen();
    direct = 0x001f; memcpy(raw, &direct, 2);
    frame = reinterpret_cast<const unsigned *>(User::Pixels());
    assert((frame[0] & 0xffffff) == 0x0000ff);
    direct = 0x03e0; memcpy(raw, &direct, 2);
    assert(User::ScanoutScreen());
    frame = reinterpret_cast<const unsigned *>(User::Pixels());
    assert((frame[0] & 0xffffff) == 0x00ff00);
    User::SetScanout(back.data(), 1280);
    User::ScreenChanged(); User::ScanoutScreen();
    direct = 0x7c00; memcpy(back.data(), &direct, 2);
    frame = reinterpret_cast<const unsigned *>(User::Pixels());
    assert((frame[0] & 0xffffff) == 0xff0000);
    User::SettleScanout();
    // Cursor composition also waits for the latest primary, and never
    // writes its pixels into the guest's direct screen memory.
    static unsigned cursor_draws=0;
    User::SetCursorProvider(nullptr, +[](void *,Gdi::HDC dc,int x,int y,unsigned){
        ++cursor_draws;
        const auto brush=Gdi::CreateSolidBrush(Gdi::RGB(255,255,255));
        const Gdi::RECT rect{x,y,x+1,y+1};
        const bool ok=Gdi::FillRect(dc,&rect,brush);
        Gdi::DeleteObject(brush);return ok;
    });
    User::Mouse(0,0,false,false); User::ShowPointer(true); User::SetPointerShape(1);
    cursor_draws=0;
    User::ScreenChanged(); User::ScanoutScreen();
    direct=0x001f;memcpy(raw,&direct,2);
    assert(cursor_draws==0);
    frame=reinterpret_cast<const unsigned *>(User::Pixels());
    assert(cursor_draws==1 && (frame[0]&0xffffff)==0xffffff);
    memcpy(&pixel,raw,2);assert(pixel==0x001f);
    User::ScreenChanged();User::SetPointerShape(0);
    frame=reinterpret_cast<const unsigned *>(User::Pixels());
    assert((frame[0]&0xffffff)==0x0000ff);
    User::ShowPointer(false);User::SetCursorProvider(nullptr,nullptr);
    // GDI supplies the access notification before reading or changing bytes.
    static unsigned char pixels[16]{};
    static unsigned reads = 0, writes = 0;
    Gdi::SetSurfaceAccessSink(+[](const unsigned char *bits, bool write) {
        if (bits != pixels) return;
        if (write) ++writes; else ++reads;
    });
    auto dc = Gdi::CreateSurfaceDC({pixels, 2, 2, 8, 32}); assert(dc);
    const auto brush = Gdi::CreateSolidBrush(Gdi::RGB(255, 0, 0));
    const Gdi::RECT rect{0, 0, 1, 1}; assert(Gdi::FillRect(dc, &rect, brush));
    assert(writes && Gdi::GetPixel(dc, 0, 0) == Gdi::RGB(255, 0, 0) && reads);
    Gdi::SetSurfaceAccessSink(nullptr); assert(Gdi::DeleteDC(dc) && Gdi::DeleteObject(brush));
    User::Shutdown(); Gdi::ShutdownBitmaps();
    puts("USER SCANOUT PASS: deferred reads, screenshot pixels, gamma, cursor, overlay, settle, detach, GDI access");
}
