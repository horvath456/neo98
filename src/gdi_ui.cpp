// SPDX-License-Identifier: GPL-2.0-or-later
// Small ARM window/composition adapter, with native window behavior.
#include "kernel.h"
#include "user_draw.h"
#include <circle/string.h>
#include <circle/util.h>

namespace {
void Fill(Gdi::HDC dc, Gdi::RECT r, unsigned color)
{
    const auto brush = Gdi::CreateSolidBrush(Gdi::RGB(color >> 16, color >> 8, color));
    Gdi::FillRect(dc, &r, brush); Gdi::DeleteObject(brush);
}
void Label(Gdi::HDC dc, int x, int y, const char *text, unsigned color = 0)
{
    Gdi::SetTextColor(dc, Gdi::RGB(color >> 16, color >> 8, color));
    Gdi::SetBkMode(dc, Gdi::TRANSPARENT);
    Gdi::TextOutA(dc, x, y, text, strlen(text));
}
void Edge(Gdi::HDC dc, Gdi::RECT r)
{
    Win98Draw::DrawEdge(dc,&r,5,15);
}
}

bool Kernel::InitializeGdi()
{
    screen_dc = Gdi::CreateSurfaceDC({reinterpret_cast<unsigned char *>(surface.pixels), 800, 600, surface.stride * 4, 32});
    if (!screen_dc) return false;
    gdi_windows[0] = {52, 80, 428, 306, 0, 0};
    gdi_windows[1] = {294, 252, 422, 272, 0, 0};
    gdi_windows[2] = {56, 80, 688, 454, 0, 0};
    for (unsigned i = 0; i < 6; ++i) { editor_fonts[i] = Gdi::CreateFontResource(i); if (!editor_fonts[i]) return false; }
    for (int i = 0; i < 3; ++i) {
        auto &w = gdi_windows[i];
        w.dc = Gdi::CreateCompatibleDC(screen_dc);
        Gdi::BITMAPINFO info = {{40, w.width, -w.height, 1, 32, 0, 0, 0, 0, 0, 0}};
        void *bits;
        w.bitmap = Gdi::CreateDIBSection(w.dc, &info, 0, &bits);
        if (!w.dc || !w.bitmap || !Gdi::SelectObject(w.dc, w.bitmap)) return false;
        if (i < 2) GdiWindowContent(i);
    }
    editor.Reset("Windows 98 text, on ARM.\n\n"
                 "This window draws through the implemented Windows 98 text engine. "
                 "The six MS Sans Serif sizes come from the original font file.\n\n"
                 "Click in this document and type. Select text with the mouse or Shift + arrows. "
                 "Try changing the font size while text is selected.\n\n"
                 "Ctrl+A selects all. Ctrl+C, Ctrl+X and Ctrl+V use the local clipboard. "
                 "Ctrl+Z undoes the last edit. Nothing is saved to disk.\n\n"
                 "The quick brown fox jumps over the lazy dog. 0123456789\n"
                 "Wide: WWWMMM   Narrow: iii...   Accents: \xe4 \xf6 \xfc \xc4 \xd6 \xdc \xdf\n\n"
                 "Escape opens the graphics desktop. E brings this document back.");
    EditorMetrics(); EditorContent();
    return true;
}

void Kernel::GdiWindowContent(int index)
{
    if (index == 2) { EditorContent(); return; }
    auto &w = gdi_windows[index]; const auto dc = w.dc;
    Fill(dc, {0, 0, w.width, w.height}, 0xc0c0c0); Edge(dc, {0, 0, w.width, w.height});
    const bool active = gdi_order[1] == index;
    Fill(dc, {4, 4, w.width - 4, 27}, active ? 0x000080 : 0x808080);
    Label(dc, 11, 9, index ? "Clipping and window contents" : "Windows 98 raster operations", 0xffffff);
    Fill(dc, {w.width - 25, 7, w.width - 7, 24}, 0xc0c0c0);
    Edge(dc, {w.width - 25, 7, w.width - 7, 24}); Label(dc, w.width - 20, 8, "_");
    if (!index) {
        Label(dc, 16, 40, "Source bitmap"); Label(dc, 166, 40, "Destination");
        for (int y = 0; y < 80; y += 10) for (int x = 0; x < 128; x += 16)
            Fill(dc, {16 + x, 61 + y, 32 + x, 71 + y}, ((x / 16 + y / 10) & 1) ? 0x00a0e0 : 0xffd040);
        Fill(dc, {166, 61, 394, 181}, 0x306090);
        Fill(dc, {194, 73, 270, 164}, 0xe06070);
        Fill(dc, {295, 83, 382, 142}, 0x60c080);
        static const unsigned rops[] = {Gdi::SRCCOPY, Gdi::SRCINVERT, Gdi::SRCAND, Gdi::SRCPAINT, 0x00960169};
        static const char *names[] = {"SRCCOPY", "SRCINVERT / XOR", "SRCAND", "SRCPAINT / OR", "Source XOR destination XOR brush"};
        auto brush = Gdi::CreateSolidBrush(Gdi::RGB(0x80, 0x30, 0xc0));
        auto old = Gdi::SelectObject(dc, brush);
        Gdi::BitBlt(dc, 182, 81, 128, 80, dc, 16, 61, rops[gdi_operation]);
        Gdi::SelectObject(dc, old); Gdi::DeleteObject(brush);
        Label(dc, 16, 198, names[gdi_operation]);
        Label(dc, 16, 223, "Space: change raster operation");
        Label(dc, 16, 249, "Original DIBENG raster logic, compiled for ARM64.");
        CString results; results.Format("%u original-driver comparisons passed.", DibEng::OriginalTestCount());
        Label(dc, 16, 274, results, 0x006000);
    } else {
        Label(dc, 16, 40, "A saved clipping region leaves this hole untouched.");
        Fill(dc, {16, 64, 405, 199}, 0xffffff);
        Gdi::SaveDC(dc);
        Gdi::IntersectClipRect(dc, 16, 64, 405, 199);
        Gdi::ExcludeClipRect(dc, 134, 94, 286, 169);
        for (int y = 64; y < 199; y += 12)
            Fill(dc, {16, y, 405, y + 6}, ((y / 12) & 1) ? 0x000080 : 0x008080);
        Gdi::RestoreDC(dc, -1);
        Label(dc, 159, 110, "Excluded region");
        Label(dc, 153, 140, "SaveDC / RestoreDC");
        Label(dc, 16, 215, "Drag either title bar. Covered pixels return intact.");
        Label(dc, 16, 239, "S: scroll this bitmap with an overlapping BitBlt");
        if (gdi_scroll) Gdi::BitBlt(dc, 21, 69, 379, 125, dc, 16, 64, Gdi::SRCCOPY);
    }
}

void Kernel::GdiDamage(Rect r)
{
    Rect clipped;
    const Rect bounds = {0, 0, 800, 600};
    if (!Win98::IntersectRect(&clipped, &r, &bounds)) return;
    if (!gdi_dirty) gdi_damage = clipped;
    else {
        if (clipped.left < gdi_damage.left) gdi_damage.left = clipped.left;
        if (clipped.top < gdi_damage.top) gdi_damage.top = clipped.top;
        if (clipped.right > gdi_damage.right) gdi_damage.right = clipped.right;
        if (clipped.bottom > gdi_damage.bottom) gdi_damage.bottom = clipped.bottom;
    }
    gdi_dirty = true;
}

void Kernel::PaintGdi()
{
    const unsigned started = CTimer::GetClockTicks();
    const Rect damage = gdi_damage;
    Gdi::SaveDC(screen_dc);
    Gdi::IntersectClipRect(screen_dc, damage.left, damage.top, damage.right, damage.bottom);
    Fill(screen_dc, {0, 0, 800, 600}, 0x008080);
    Label(screen_dc, 20, 18, "NEO98 / Windows graphics subsystem", 0xffffff);
    Label(screen_dc, 20, 40, editor_view ? "Text editor    /    Escape: graphics desktop" : "E: Text editor    M: Minesweeper    D: Diagnostics    Q: Halt", 0xffffff);
    const int layers = editor_view ? 3 : 2;
    for (int layer = 0; layer < layers; ++layer) {
        const auto &w = gdi_windows[layer == 2 ? 2 : gdi_order[layer]];
        Gdi::SaveDC(screen_dc);
        // The ARM window adapter supplies visibility. This is not yet USER's
        // message dispatch/invalidation implementation.
        for (int above = layer + 1; above < layers; ++above) {
            const auto &front = gdi_windows[above == 2 ? 2 : gdi_order[above]];
            Gdi::ExcludeClipRect(screen_dc, front.x, front.y, front.x + front.width, front.y + front.height);
        }
        Gdi::BitBlt(screen_dc, w.x, w.y, w.width, w.height, w.dc, 0, 0, Gdi::SRCCOPY);
        Gdi::RestoreDC(screen_dc, -1);
    }
    Fill(screen_dc, {0, 562, 800, 600}, 0xc0c0c0);
    Fill(screen_dc, {0, 562, 800, 564}, 0xffffff);
    Edge(screen_dc, {6, 568, 107, 595}); Label(screen_dc, 24, 575, "NEO98");
    Label(screen_dc, 125, 575, editor_view ? "NEO98 Edit - MS Sans Serif" : "GDI: bitmap contexts, clipping, raster operations");
    Label(screen_dc, 639, 575, "AArch64");
    Gdi::RestoreDC(screen_dc, -1);
    Present(damage);
    gdi_dirty = false;
    CString line;
    line.Format("NEO98 GDIFRAME pixels=%u elapsed_us=%u rect=%d,%d,%d,%d objects=%u",
                (damage.right - damage.left) * (damage.bottom - damage.top), CTimer::GetClockTicks() - started,
                damage.left, damage.top, damage.right, damage.bottom, Gdi::LiveObjects());
    Log(line);
}

void Kernel::GdiInput(unsigned down, unsigned up, int wheel)
{
    if (editor_view) { EditorInput(down, up, wheel); return; }
    if (down & MOUSE_BUTTON_LEFT) {
        for (int layer = 1; layer >= 0; --layer) {
            const int index = gdi_order[layer]; auto &w = gdi_windows[index];
            if (mouse_x < w.x || mouse_y < w.y || mouse_x >= w.x + w.width || mouse_y >= w.y + w.height) continue;
            gdi_order[0] = 1 - index; gdi_order[1] = index;
            for (int i = 0; i < 2; ++i) {
                GdiWindowContent(i);
                const auto &changed = gdi_windows[i];
                GdiDamage({changed.x, changed.y, changed.x + changed.width, changed.y + changed.height});
            }
            if (mouse_y < w.y + 28) { gdi_drag = index; gdi_drag_x = mouse_x - w.x; gdi_drag_y = mouse_y - w.y; }
            break;
        }
    }
    if (up & MOUSE_BUTTON_LEFT) { if (gdi_drag >= 0) Log("NEO98 GDI drag=end"); gdi_drag = -1; }
    if (gdi_drag >= 0 && (mouse_buttons & MOUSE_BUTTON_LEFT)) {
        auto &w = gdi_windows[gdi_drag];
        int x = mouse_x - gdi_drag_x, y = mouse_y - gdi_drag_y;
        if (x < 0) x = 0; if (x > 800 - w.width) x = 800 - w.width;
        if (y < 60) y = 60; if (y > 554 - w.height) y = 554 - w.height;
        if (x == w.x && y == w.y) return;
        GdiDamage({w.x, w.y, w.x + w.width, w.y + w.height});
        w.x = x; w.y = y;
        GdiDamage({w.x, w.y, w.x + w.width, w.y + w.height});
        CString line; line.Format("NEO98 GDI drag=%d x=%d y=%d", gdi_drag, x, y); Log(line);
    }
}

void Kernel::GdiCommand(char key)
{
    if (key == 'q' || key == 'Q') { running = false; return; }
    int index;
    if (key == ' ') { gdi_operation = (gdi_operation + 1) % 5; index = 0; }
    else if (key == 's' || key == 'S') { gdi_scroll ^= 1; index = 1; }
    else return;
    GdiWindowContent(index);
    const auto &w = gdi_windows[index];
    GdiDamage({w.x, w.y, w.x + w.width, w.y + w.height});
    CString line; line.Format("NEO98 GDI operation=%u scroll=%u", gdi_operation, gdi_scroll); Log(line);
}
