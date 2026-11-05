// SPDX-License-Identifier: GPL-2.0-or-later
// Circle input/display adapter. The game rules live in winmine.cpp.
#include "kernel.h"
#include <circle/string.h>
#include "../build/generated/winmine_bitmaps.h"

static constexpr unsigned Gray = 0xffc0c0c0, White = 0xffffffff, Black = 0xff000000;

void Kernel::MineLayout()
{
    const int width = game.columns * 16 + 24, height = game.rows * 16 + 67;
    mine_scale = height <= 240 ? 2 : 1;
    mine_width = width * mine_scale + 8;
    mine_height = height * mine_scale + 52;
    mine_left = (800 - mine_width) / 2;
    mine_top = (562 - mine_height) / 2;
}

void Kernel::Sprite(const unsigned *bitmap, int width, int source_y, int height,
                    int x, int y, int scale)
{
    for (int yy = 0; yy < height; ++yy) for (int xx = 0; xx < width; ++xx)
        FillRect(surface, {x + xx * scale, y + yy * scale,
                           x + (xx + 1) * scale, y + (yy + 1) * scale},
                 bitmap[(source_y + yy) * width + xx]);
}

void Kernel::PaintMines()
{
    const unsigned started = CTimer::GetClockTicks();
    const bool all = mine_full;
    MineLayout();
    const int s = mine_scale, cx = mine_left + 4, cy = mine_top + 48;
    const int cw = game.columns * 16 + 24, ch = game.rows * 16 + 67;
    unsigned pixels = 0;
    if (all) {
        FillRect(surface, {0, 0, 800, 600}, 0xff008080);
        Text(20, 18, "NEO98", White);
        Text(20, 38, "Minesweeper / native ARM64 port", White);
        FillRect(surface, {mine_left + 6, mine_top + 6, mine_left + mine_width + 6,
                           mine_top + mine_height + 6}, 0xff004848);
        FillRect(surface, {mine_left, mine_top, mine_left + mine_width, mine_top + mine_height}, Gray);
        DrawEdge(surface, {mine_left, mine_top, mine_left + mine_width, mine_top + mine_height}, false);
        FillRect(surface, {mine_left + 4, mine_top + 4, mine_left + mine_width - 4, mine_top + 27}, 0xff000080);
        Text(mine_left + 11, mine_top + 9, "Minesweeper", White);
        FillRect(surface, {mine_left + mine_width - 25, mine_top + 7,
                           mine_left + mine_width - 7, mine_top + 24}, Gray);
        DrawEdge(surface, {mine_left + mine_width - 25, mine_top + 7,
                           mine_left + mine_width - 7, mine_top + 24}, false);
        Text(mine_left + mine_width - 20, mine_top + 8, "x");
        Text(mine_left + 10, mine_top + 32, "Game");
        DrawEdge(surface, {cx, cy, cx + cw * s, cy + ch * s}, false);
        DrawEdge(surface, {cx + 9 * s, cy + 9 * s, cx + (cw - 9) * s, cy + 46 * s}, true);
        DrawEdge(surface, {cx + 9 * s, cy + 52 * s, cx + (cw - 9) * s, cy + (ch - 9) * s}, true);
        FillRect(surface, {0, 562, 800, 600}, Gray);
        FillRect(surface, {0, 562, 800, 564}, White);
        Button({6, 568, 104, 595}, "NEO98");
        Text(120, 575, running ? "Original game logic running on ARM64" : "Halted");
        Text(548, 575, "N: New    1 / 2 / 3: Level    D: Tests");
    }
    const int shown_face = face_pressed ? 4 : mine_capture ? 1 : game.face;
    if (all || painted_seconds != game.seconds || painted_remaining != game.remaining || painted_face != shown_face) {
        const int values[] = {game.remaining, game.seconds};
        for (int group = 0; group < 2; ++group) {
            const int value = values[group] < 0 ? -values[group] : values[group];
            const int digits[] = {values[group] < 0 ? 11 : value / 100, (value / 10) % 10, value % 10};
            const int left = group ? cw - 56 : 17;
            for (int i = 0; i < 3; ++i)
                Sprite(MineBitmap420, 13, (11 - digits[i]) * 23, 23, cx + (left + i * 13) * s, cy + 16 * s, s);
        }
        Sprite(MineBitmap430, 24, (4 - shown_face) * 24, 24,
               cx + ((cw - 24) / 2) * s, cy + 16 * s, s);
        painted_seconds = game.seconds; painted_remaining = game.remaining; painted_face = shown_face;
        if (!all) {
            Present({cx + 12 * s, cy + 14 * s, cx + (cw - 12) * s, cy + 41 * s});
            pixels += (cw - 24) * 27 * s * s;
        }
    }
    for (int y = 1; y <= game.rows; ++y) for (int x = 1; x <= game.columns; ++x) {
        const int index = y * 32 + x;
        if (!all && painted_cells[index] == game.cells[index]) continue;
        const int tile = game.Cell(y, x) & 0x1f;
        const int px = cx + (12 + (x - 1) * 16) * s, py = cy + (55 + (y - 1) * 16) * s;
        Sprite(MineBitmap410, 16, (15 - tile) * 16, 16, px, py, s);
        painted_cells[index] = game.cells[index];
        if (!all) { Present({px, py, px + 16 * s, py + 16 * s}); pixels += 256 * s * s; }
    }
    if (menu_open) {
        const int mx = mine_left + 5, my = mine_top + 48;
        FillRect(surface, {mx, my, mx + 212, my + 144}, Gray);
        DrawEdge(surface, {mx, my, mx + 212, my + 144}, false);
        const char *items[] = {"New game                     N", "Beginner                       1",
            "Intermediate                   2", "Expert                          3", "Marks (?)", "Back to tests                   D"};
        for (int i = 0; i < 6; ++i) Text(mx + 12, my + 7 + i * 23, items[i]);
    }
    if (all) { Present({0, 0, 800, 600}); pixels = 480000; }
    else if (menu_open) Present({mine_left + 5, mine_top + 48, mine_left + 217, mine_top + 192});
    mine_full = mine_dirty = false;
    CString line;
    line.Format("NEO98 MINEFRAME kind=%s pixels=%u elapsed_us=%u", all ? "full" : "partial", pixels,
                CTimer::GetClockTicks() - started);
    Log(line);
}

void Kernel::MineLog(const char *action)
{
    CString line;
    line.Format("NEO98 WINMINE action=%s phase=%d face=%d revealed=%d remaining=%d seconds=%d board=%dx%d hash=%08x",
                action, game.phase, game.face, game.revealed, game.remaining, game.seconds,
                game.columns, game.rows, game.Fingerprint());
    Log(line);
}

void Kernel::MineCommand(char key)
{
    switch (key) {
    case 'n': case 'N': game.Reset(); break;
    case '1': game.Configure(8, 8, 10, game.random); break;
    case '2': game.Configure(16, 16, 40, game.random); break;
    case '3': game.Configure(30, 16, 99, game.random); break;
    case 'q': case 'Q': running = false; break;
    default: return;
    }
    mine_capture = face_pressed = menu_open = false;
    game.chord = 0;
    mine_full = mine_dirty = true;
    MineLog("new-or-command");
}

void Kernel::DrawCursor()
{
    if (!mouse || (application && !application->CursorVisible()))
        return;
    // Cursor is composited only on the visible display; the drawing buffer
    // remains intact so moving the pointer can restore its old rectangle.
    const char *shape[] = {"X", "XX", "XOX", "XOOX", "XOOOX", "XOOOOX", "XOOOOOX",
        "XOOOOOOX", "XOOOOOOOX", "XOOOOXXXXX", "XOOXOX", "XOX XOX", "XX  XOX", "X    XOX", "     XOX", "      X"};
    for (int y = 0; y < 16; ++y) for (int x = 0; shape[y][x]; ++x)
        if (shape[y][x] != ' ' && mouse_x + x < 800 && mouse_y + y < 600)
            display.pixels[(mouse_y + y) * display.stride + mouse_x + x] = shape[y][x] == 'X' ? Black : White;
    asm volatile("dsb sy" ::: "memory");
}

void Kernel::MouseStatus(unsigned buttons, int dx, int dy, int wheel)
{
    const unsigned next = (instance->mouse_write + 1) % 32;
    if (next == instance->mouse_read) return;
    instance->mouse_reports[instance->mouse_write] = {buttons, dx, dy, wheel, ++instance->input_sequence};
    asm volatile("dmb sy" ::: "memory");
    instance->mouse_write = next;
}

void Kernel::MouseRemoved(CDevice *, void *)
{
    instance->mouse = nullptr;
    MouseStatus(0,0,0,0);
    instance->mouse_buttons = 0;
    instance->mine_capture = false;
    instance->game.Preview(-2, -2);
    instance->game.chord = 0;
    instance->mine_full = instance->mine_dirty = instance->full_dirty = true;
    instance->gdi_drag = -1;
    if (instance->gdi_view) instance->GdiDamage({0, 0, 800, 600});
}

void Kernel::MineInput(unsigned buttons, int dx, int dy, int wheel)
{
    const Rect old_cursor = {mouse_x, mouse_y, mouse_x + 11, mouse_y + 17};
    mouse_x += dx; mouse_y += dy;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x > 799) mouse_x = 799;
    if (mouse_y > 599) mouse_y = 599;
    Present(old_cursor);
    const unsigned down = buttons & ~mouse_buttons, up = mouse_buttons & ~buttons;
    mouse_buttons = buttons;
    if (gdi_view) { GdiInput(down, up, wheel); DrawCursor(); return; }
    if (!mines_view) { DrawCursor(); return; }
    MineLayout();
    const int cx = mine_left + 4, cy = mine_top + 48, cw = game.columns * 16 + 24;
    const int px = (mouse_x - cx), py = (mouse_y - cy), s = mine_scale;
    const bool on_face = px >= ((cw - 24) / 2) * s && px < ((cw + 24) / 2) * s && py >= 16 * s && py < 40 * s;
    int x = -2, y = -2;
    if (px >= 12 * s && py >= 55 * s && px < (12 + game.columns * 16) * s && py < (55 + game.rows * 16) * s) {
        x = (px - 12 * s) / (16 * s) + 1;
        y = (py - 55 * s) / (16 * s) + 1;
    }
    if (down & 1) {
        if (menu_open) {
            const int item = (mouse_y - (mine_top + 48)) / 23;
            if (mouse_x >= mine_left + 5 && mouse_x < mine_left + 217 &&
                mouse_y >= mine_top + 48 && item >= 0 && item < 6) {
                if (item < 4) MineCommand(item ? (char)('0' + item) : 'n');
                else if (item == 4) game.questions ^= 1;
                else Command('d');
            }
            menu_open = false; mine_full = mine_dirty = true;
        } else if (mouse_y >= mine_top + 28 && mouse_y < mine_top + 48 &&
                   mouse_x >= mine_left + 5 && mouse_x < mine_left + 51) {
            menu_open = true; mine_full = mine_dirty = true;
        } else if (mouse_y >= mine_top + 7 && mouse_y < mine_top + 24 &&
                   mouse_x >= mine_left + mine_width - 25 && mouse_x < mine_left + mine_width - 7) {
            Command('d');
        } else if (on_face) { face_pressed = true; mine_dirty = true; }
        else if (game.Active() && game.Inside(y, x)) {
            game.Preview(-2, -2); game.chord = (buttons & 6) != 0;
            mine_capture = true; game.Preview(y, x); mine_dirty = true;
        }
    }
    if ((down & 4) && game.Active() && game.Inside(y, x)) {
        game.Preview(-2, -2); game.chord = 1;
        mine_capture = true; mine_dirty = true;
    }
    if ((down & 2) && game.Active() && !menu_open) {
        if (mine_capture) { game.Preview(-2, -2); game.chord = 1; }
        else { game.Mark(y, x); MineLog("mark"); }
        mine_dirty = true;
    }
    if (mine_capture) {
        game.Preview(y, x); mine_dirty = true;
        if (up & 7) {
            if (game.Active()) game.Release();
            game.Preview(-2, -2); game.chord = 0;
            mine_capture = false;
            MineLog("open");
        }
    }
    if (face_pressed && (up & 1)) {
        face_pressed = false;
        if (on_face) MineCommand('n');
        mine_dirty = true;
    }
    if (mine_dirty) Paint();
    DrawCursor();
}
