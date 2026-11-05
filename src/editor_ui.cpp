// SPDX-License-Identifier: GPL-2.0-or-later
// A native application exercising the recovered driver text path.
#include "kernel.h"
#include "user_draw.h"
#include <circle/string.h>
#include <circle/util.h>

namespace {
const Gdi::RECT Document = {20, 91, 657, 410};
void Fill(Gdi::HDC dc, Gdi::RECT r, unsigned pixel)
{
    const auto brush = Gdi::CreateSolidBrush(Gdi::RGB(pixel >> 16, pixel >> 8, pixel));
    Gdi::FillRect(dc, &r, brush); Gdi::DeleteObject(brush);
}
void Label(Gdi::HDC dc, int x, int y, const char *text, unsigned pixel = 0)
{
    Gdi::SetTextColor(dc, Gdi::RGB(pixel >> 16, pixel >> 8, pixel));
    Gdi::SetBkMode(dc, Gdi::TRANSPARENT); Gdi::TextOutA(dc, x, y, text, strlen(text));
}
void Edge(Gdi::HDC dc, Gdi::RECT r, bool pressed = false)
{
    Win98Draw::DrawEdge(dc,&r,pressed?10:5,15);
}
void DrawButton(Gdi::HDC dc, Gdi::RECT r, unsigned state = 0)
{
    Win98Draw::ButtonFrame(dc, &r, state, 0x1000);
}
void ScrollThumb(const TextEdit &editor, int &top, int &height)
{
    const int track = 298;
    height = editor.LineCount() > (unsigned)editor.VisibleLines() ? track * editor.VisibleLines() / editor.LineCount() : track;
    if (height < 20) height = 20;
    const int maximum = (int)editor.LineCount() - editor.VisibleLines();
    top = 102 + (maximum > 0 ? (track - height) * editor.FirstLine() / maximum : 0);
}
}

void Kernel::EditorMetrics()
{
    const auto dc = gdi_windows[2].dc;
    Gdi::SaveDC(dc); Gdi::SelectObject(dc, editor_fonts[editor_font]);
    int widths[256]; Gdi::SIZE size;
    Gdi::GetCharWidth32A(dc, 0, 255, widths); Gdi::GetTextExtentPoint32A(dc, "M", 1, &size);
    editor.Metrics(widths, size.cy + 3, Document.right - Document.left - 2, Document.bottom - Document.top);
    Gdi::RestoreDC(dc, -1);
}

void Kernel::EditorContent()
{
    const auto &w = gdi_windows[2]; const auto dc = w.dc;
    Fill(dc, {0, 0, w.width, w.height}, 0xc0c0c0); Edge(dc, {0, 0, w.width, w.height});
    Fill(dc, {4, 4, w.width - 4, 28}, 0x000080);
    Label(dc, 11, 9, "Untitled - NEO98 Edit", 0xffffff);
    DrawButton(dc, {w.width - 25, 7, w.width - 7, 25}); Label(dc, w.width - 21, 9, "X");
    Label(dc, 18, 35, "Undo"); Label(dc, 80, 35, "Cut"); Label(dc, 122, 35, "Copy");
    Label(dc, 174, 35, "Paste"); Label(dc, 230, 35, "Select all");
    Label(dc, 18, 60, "MS Sans Serif");
    static const char *points[] = {"8", "10", "12", "14", "18", "24"};
    for (unsigned i = 0; i < 6; ++i) {
        const int left = 158 + i * 51;
        DrawButton(dc, {left, 55, left + 45, 78}, editor_font == i ? 0x400 : 0); Label(dc, left + 15, 60, points[i]);
    }
    Label(dc, 497, 60, "Word wrap");
    Edge(dc, {12, 85, 676, 418}, true); Fill(dc, {14, 87, 660, 416}, 0xffffff);
    Fill(dc, {660, 87, 674, 416}, 0xe0e0e0);
    DrawButton(dc, {660, 87, 674, 102}); Label(dc, 663, 88, "^");
    DrawButton(dc, {660, 400, 674, 416}); Label(dc, 663, 400, "v");
    int thumb, thumb_top; ScrollThumb(editor, thumb_top, thumb);
    DrawButton(dc, {660, thumb_top, 674, thumb_top + thumb});

    Gdi::SaveDC(dc); Gdi::SelectObject(dc, editor_fonts[editor_font]);
    Gdi::IntersectClipRect(dc, Document.left, Document.top, Document.right, Document.bottom);
    const unsigned a = editor.SelectionStart(), b = editor.SelectionEnd();
    for (unsigned row = editor.FirstLine(); row < editor.LineCount() && row <= (unsigned)(editor.FirstLine() + editor.VisibleLines()); ++row) {
        const auto &line = editor.GetLine(row);
        const int y = Document.top + ((int)row - editor.FirstLine()) * editor.LineHeight();
        Gdi::SetTextColor(dc, 0); Gdi::SetBkMode(dc, Gdi::TRANSPARENT);
        Gdi::TextOutA(dc, Document.left, y, editor.Data() + line.start, line.length);
        const unsigned start = a > line.start ? a : line.start;
        const unsigned end = b < line.start + line.length ? b : line.start + line.length;
        const bool newline = b > line.start + line.length && a <= line.start + line.length;
        if (start < end || newline) {
            const int left = Document.left + editor.PrefixWidth(row, start - line.start);
            const int right = newline ? Document.right : Document.left + editor.PrefixWidth(row, end - line.start);
            const Gdi::RECT selected = {left, y, right, y + editor.LineHeight()};
            Gdi::SetTextColor(dc, Gdi::RGB(255, 255, 255)); Gdi::SetBkColor(dc, Gdi::RGB(0, 0, 128));
            Gdi::ExtTextOutA(dc, Document.left, y, Gdi::ETO_OPAQUE | Gdi::ETO_CLIPPED, &selected,
                             editor.Data() + line.start, line.length);
        }
    }
    if (a == b) {
        const int x = Document.left + editor.CursorX();
        const int y = Document.top + ((int)editor.CursorLine() - editor.FirstLine()) * editor.LineHeight();
        Fill(dc, {x, y, x + 1, y + editor.LineHeight() - 2}, 0);
    }
    Gdi::RestoreDC(dc, -1);
    Edge(dc, {12, 425, 676, 446}, true);
    CString status;
    status.Format("Ln %u / %u    %u chars    %u selected    %s pt    In memory",
                  editor.CursorLine() + 1, editor.LineCount(), editor.Length(), b - a, points[editor_font]);
    Label(dc, 19, 429, status);
}

void Kernel::EditorChanged(const char *action)
{
    EditorContent(); const auto &w = gdi_windows[2];
    GdiDamage({w.x, w.y, w.x + w.width, w.y + w.height});
    unsigned hash = 2166136261u;
    for (unsigned i = 0; i < editor.Length(); ++i) hash = (hash ^ (unsigned char)editor.Data()[i]) * 16777619u;
    CString line;
    line.Format("NEO98 EDIT action=%s length=%u cursor=%u selected=%u font=%u lines=%u scroll=%d hash=%08x",
                action, editor.Length(), editor.Cursor(), editor.SelectionEnd() - editor.SelectionStart(),
                editor_font, editor.LineCount(), editor.FirstLine(), hash);
    Log(line);
}

void Kernel::EditorCommand(const char *text, unsigned modifiers)
{
    const unsigned char key = text[0];
    if (key == 27 && !text[1]) {
        editor_view = false; editor_selecting = false; gdi_drag = -1;
        GdiDamage({0, 0, 800, 600}); Log("NEO98 VIEW gdi"); return;
    }
    const bool shift = modifiers & 0x22;
    const char *action = "move";
    if (key == 27) {
        if (!strcmp(text, "\x1b[D")) editor.Move(TextEdit::Left, shift);
        else if (!strcmp(text, "\x1b[C")) editor.Move(TextEdit::Right, shift);
        else if (!strcmp(text, "\x1b[A")) editor.Move(TextEdit::Up, shift);
        else if (!strcmp(text, "\x1b[B")) editor.Move(TextEdit::Down, shift);
        else if (!strcmp(text, "\x1b[1~")) editor.Move(TextEdit::Home, shift);
        else if (!strcmp(text, "\x1b[4~")) editor.Move(TextEdit::End, shift);
        else if (!strcmp(text, "\x1b[1;5H")) editor.Move(TextEdit::DocumentHome, shift);
        else if (!strcmp(text, "\x1b[1;5F")) editor.Move(TextEdit::DocumentEnd, shift);
        else if (!strcmp(text, "\x1b[5~")) editor.Move(TextEdit::PageUp, shift);
        else if (!strcmp(text, "\x1b[6~")) editor.Move(TextEdit::PageDown, shift);
        else if (!strcmp(text, "\x1b[3~")) { if (!editor.Erase(true)) return; action = "delete"; }
        else return;
    } else if (key == 1) { editor.SelectAll(); action = "select-all"; }
    else if (key == 3 || key == 24) {
        if (editor.SelectionStart() == editor.SelectionEnd()) return;
        editor_clipboard_length = editor.Copy(editor_clipboard);
        if (key == 24) editor.Erase(false);
        action = key == 3 ? "copy" : "cut";
    } else if (key == 22) {
        if (!editor_clipboard_length || !editor.Insert(editor_clipboard, editor_clipboard_length)) return;
        action = "paste";
    } else if (key == 26) { if (!editor.Undo()) return; action = "undo"; }
    else if (key == 127 || key == 8) { if (!editor.Erase(false)) return; action = "backspace"; }
    else if (key == '\t') { if (!editor.Insert("    ", 4)) return; action = "type"; }
    else if (key == '\n' || key == '\r' || key >= 32) {
        const char value = key == '\r' ? '\n' : key;
        if (!editor.Insert(&value, 1)) return;
        action = "type";
    } else return;
    EditorChanged(action);
}

void Kernel::EditorInput(unsigned down, unsigned up, int wheel)
{
    auto &w = gdi_windows[2];
    const int x = mouse_x - w.x, y = mouse_y - w.y;
    if (wheel && x >= 0 && x < w.width && y >= 0 && y < w.height) {
        editor.Scroll(-wheel * 3); EditorChanged("scroll");
    }
    if (up & MOUSE_BUTTON_LEFT) {
        if (gdi_drag == 2) Log("NEO98 GDI drag=end");
        gdi_drag = -1; editor_selecting = false;
    }
    if (down & MOUSE_BUTTON_LEFT) {
        if (x >= w.width - 25 && x < w.width - 7 && y >= 7 && y < 25) { EditorCommand("\x1b", 0); return; }
        if (x >= 4 && x < w.width - 4 && y >= 4 && y < 28) { gdi_drag = 2; gdi_drag_x = x; gdi_drag_y = y; }
        else if (y >= 55 && y < 78 && x >= 158 && x < 158 + 6 * 51) {
            const unsigned font = (x - 158) / 51;
            if (editor_font != font) { editor_font = font; EditorMetrics(); EditorChanged("font"); }
        } else if (y >= 32 && y < 50) {
            if (x >= 18 && x < 65) EditorCommand("\x1a", 0);
            else if (x >= 80 && x < 112) EditorCommand("\x18", 0);
            else if (x >= 122 && x < 166) EditorCommand("\x03", 0);
            else if (x >= 174 && x < 218) EditorCommand("\x16", 0);
            else if (x >= 230 && x < 303) EditorCommand("\x01", 0);
        } else if (x >= 660 && x < 674 && y >= 87 && y < 416) {
            int top, height; ScrollThumb(editor, top, height);
            if (y >= top && y < top + height) return;
            editor.Scroll(y < 102 ? -1 : y >= 400 ? 1 : y < top ? -editor.VisibleLines() : editor.VisibleLines());
            EditorChanged("scroll");
        } else if (x >= Document.left && x < Document.right && y >= Document.top && y < Document.bottom) {
            editor.SelectAt(x - Document.left, y - Document.top, key_modifiers & 0x22);
            editor_selecting = true; EditorChanged("select");
        }
    } else if (editor_selecting && (mouse_buttons & MOUSE_BUTTON_LEFT)) {
        const auto previous = editor.Cursor();
        editor.SelectAt(x - Document.left, y - Document.top, true);
        if (previous != editor.Cursor()) EditorChanged("select");
    }
    if (gdi_drag == 2 && (mouse_buttons & MOUSE_BUTTON_LEFT)) {
        int nx = mouse_x - gdi_drag_x, ny = mouse_y - gdi_drag_y;
        if (nx < 0) nx = 0; if (nx > 800 - w.width) nx = 800 - w.width;
        if (ny < 60) ny = 60; if (ny > 554 - w.height) ny = 554 - w.height;
        if (nx == w.x && ny == w.y) return;
        GdiDamage({w.x, w.y, w.x + w.width, w.y + w.height});
        w.x = nx; w.y = ny; GdiDamage({w.x, w.y, w.x + w.width, w.y + w.height});
        CString line; line.Format("NEO98 GDI drag=2 x=%d y=%d", nx, ny); Log(line);
    }
}
