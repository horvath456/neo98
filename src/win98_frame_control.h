// SPDX-License-Identifier: GPL-2.0-or-later
// DrawFrameControl layout and state decisions for caption, scroll, menu, popup-
// menu, and button controls. Raster operations are environment providers.
#pragma once
#include "user_draw.h"

namespace Win98Frame {

// COLOR_ indices this body reads from USER's initialized table at data 0E4Ch.
enum {
    ColorMenu = 4, ColorWindow = 5, ColorWindowFrame = 6, ColorBtnFace = 15,
    ColorBtnShadow = 16, ColorBtnText = 18, ColorBtnHighlight = 20, ColorHotLight = 26,
    ColorCount = 29};

// Frame control types and the state bits this body examines.
enum {
    Caption = 1, Menu = 2, Scroll = 3, Button = 4, PopupMenu = 5,
    MenuCheck = 0x0001, MenuBullet = 0x0002, ButtonPush = 0x0010,
    Inactive = 0x0100, Pushed = 0x0200, Checked = 0x0400, Transparent = 0x0800,
    Hot = 0x1000, AdjustRect = 0x2000, Flat = 0x4000, Mono = 0x8000};

struct Environment {
    // The frame boundary; its fill callback is the same one DrawEdge uses.
    Win98Draw::Painter painter;
    void *context = nullptr;
    // USER's COLOR_ value table; ColorCount entries of 00BBGGRR.
    const unsigned *colors = nullptr;
    // USER words 182h and 184h: a color index plus one. Values above the
    // table are a raw brush handle this implementation rejects. Windows 98 reads
    // 184h for DFCS_BUTTONRADIOIMAGE and 182h for DFCS_BUTTONRADIOMASK.
    unsigned menu_background = 0;
    unsigned check_background = 0;
    // The original hands a LOGFONT naming MARLETT, weight 400, symbol charset
    // and lfHeight = cell size to CreateFontIndirect.
    unsigned (*create_font)(void *, int height) = nullptr;
    void (*delete_font)(void *, unsigned handle) = nullptr;
    unsigned (*select_font)(void *, unsigned handle) = nullptr;   // returns previous
    int (*set_bk_mode)(void *, int mode) = nullptr;               // returns previous
    // One Marlett cell, drawn opaquely by the caller's text renderer.
    void (*cell)(void *, int x, int y, char code, unsigned rgb, int height) = nullptr;
    // USER keeps exactly one Marlett font, for the size it was last asked for.
    unsigned cached_font = 0;
    int cached_size = 0;
};

namespace Detail {

inline int W(int value) { return (short)(unsigned short)value; }
// The original divides a WORD difference by two, rounding toward zero.
inline int Half(int value) { value = W(value); return W((value - (value < 0 ? -1 : 0)) >> 1); }

inline void Fill(Environment &e, int x, int y, int width, int height, unsigned rgb)
{
    e.painter.fill(e.painter.context, W(x), W(y), W(width), W(height), rgb);
}

// USER maps a stored color index plus one through its brush table.
inline bool FillIndexed(Environment &e, const Gdi::RECT &r, unsigned selector)
{
    const unsigned index = (selector - 1) & 0xffff;
    if (index > 0x1d || index >= ColorCount)
        return false;   // A raw brush handle is not a reconstructable value here.
    Fill(e, r.left, r.top, W(r.right - r.left), W(r.bottom - r.top), e.colors[index]);
    return true;
}

// The caption cell for close, minimize, maximize, restore and help.
inline char CaptionCell(unsigned state)
{
    switch (state & 3) {
    case 1: return '0';
    case 2: return '1';
    case 3: return '2';
    default: return state & 4 ? 's' : 'r';
    }
}

// Center one cell, choose its color and repeat it for inactive frames.
inline bool DrawCell(Environment &e, char code, unsigned state, const Gdi::RECT &r)
{
    const unsigned inactive = state & Inactive;
    const int width = W(r.right - r.left), height = W(r.bottom - r.top);
    const int size = height > width ? width : height;
    int x = W(r.left + Half(W(width - size)));
    int y = W(r.top + Half(W(height - size)));
    const unsigned rgb = inactive ? e.colors[ColorBtnHighlight]
                       : state & Hot ? e.colors[ColorHotLight] : e.colors[ColorBtnText];
    if (state & (Inactive | Pushed)) { x = W(x + 1); y = W(y + 1); }
    e.cell(e.context, x, y, code, rgb, size);
    if (inactive)
        e.cell(e.context, W(x - 1), W(y - 1), code, e.colors[ColorBtnShadow], size);
    return true;
}

// Scroll arrows: up, down, left and right.
inline bool ScrollCell(Environment &e, unsigned state, const Gdi::RECT &r)
{
    char code = state & 2 ? '3' : '5';
    if (state & 1)
        ++code;
    return DrawCell(e, code, state, r);
}

// Menu and popup-menu cells sit on a filled background.
inline bool MenuCell(Environment &e, unsigned type, unsigned state, const Gdi::RECT &r)
{
    const int width = W(r.right - r.left), height = W(r.bottom - r.top);
    const int size = height > width ? width : height;
    const int x = W(r.left + Half(W(width - size)) - (width > 0xb ? 1 : 0));
    const int y = W(r.top + Half(W(height - size)));
    if (!FillIndexed(e, r, e.menu_background))
        return false;
    const char code = type == Menu
        ? (state & MenuCheck ? 'a' : state & MenuBullet ? 'h' : '8')
        : CaptionCell(state);
    e.cell(e.context, x, y, code, 0, size);
    return true;
}

// The two six-cell Marlett strings USER keeps in its code
// segment: "gfedcb" for a check box, "nmlkji" for a radio button.
inline const char *ButtonCells(unsigned state)
{
    return (state & 7) ? "nmlkji" : "gfedcb";
}

// Check boxes, radio buttons and the three-state cell. The original
// stacks five Marlett cells at one position, each in its own color, and adds a
// sixth for the check itself. An indeterminate three-state box replaces the
// first cell with the shared dither pattern.
inline bool ButtonCell(Environment &e, unsigned state, const Gdi::RECT &rect)
{
    const bool indeterminate = (state & 0x708) == 0x408;
    const char *codes = ButtonCells(state);
    unsigned colors[5] = {0, 0, 0, 0, 0};
    if (state & 2) {                      // DFCS_BUTTONRADIOMASK: five black cells
        if (!FillIndexed(e, rect, e.menu_background))
            return false;
    } else {
        if (state & Flat) {
            colors[1] = colors[2] = e.colors[ColorWindow];
            colors[3] = colors[4] = state & Mono ? e.colors[ColorWindowFrame]
                                                 : e.colors[ColorBtnShadow];
        } else {
            colors[1] = e.colors[22];     // COLOR_3DLIGHT
            colors[2] = e.colors[21];     // COLOR_3DDKSHADOW
            colors[3] = e.colors[ColorBtnHighlight];
            colors[4] = e.colors[ColorBtnShadow];
        }
        colors[0] = state & (Inactive | Pushed) ? e.colors[ColorBtnFace]
                  : indeterminate ? e.colors[ColorBtnHighlight] : e.colors[ColorWindow];
        if (state & 1) {                  // DFCS_BUTTONRADIOIMAGE
            if (!FillIndexed(e, rect, e.check_background))
                return false;
        }
    }
    const int width = W(rect.right - rect.left), height = W(rect.bottom - rect.top);
    const int size = height > width ? width : height;
    const int x = W(rect.left + Half(W(width - size)));
    const int y = W(rect.top + Half(W(height - size)));
    // Low color or a white button highlight keeps the original dither branch.
    if (indeterminate && !(e.painter.depth >= 8 && e.colors[ColorBtnHighlight] != 0xffffff)) {
        if (!e.painter.pattern)
            return false;
        e.painter.pattern(e.painter.context, x, y, width, height,
                          e.colors[ColorBtnHighlight], e.colors[ColorBtnFace]);
    } else {
        e.cell(e.context, x, y, codes[0], colors[0], size);
    }
    for (unsigned index = 1; index < 5; ++index)
        e.cell(e.context, x, y, codes[index], colors[index], size);
    if (state & Checked) {
        const unsigned rgb = state & 0x108 ? e.colors[ColorBtnShadow]
                           : state & Hot ? e.colors[ColorHotLight] : e.colors[8];
        e.cell(e.context, x, y, codes[5], rgb, size);
    }
    return true;
}

// The size grip fills its square and stacks two cells on it.
inline bool SizeGrip(Environment &e, unsigned state, const Gdi::RECT &r)
{
    const int height = W(r.bottom - r.top), width = W(r.right - r.left);
    const int size = height > width ? width : height;
    const int x = W(r.right - size), y = W(r.bottom - size);
    const bool plain = (state & (Flat | Mono)) != 0;
    const unsigned face = plain ? e.colors[ColorWindow] : e.colors[ColorBtnFace];
    const unsigned first = plain ? e.colors[ColorWindowFrame] : e.colors[ColorBtnHighlight];
    const unsigned second = plain ? e.colors[ColorWindowFrame] : e.colors[ColorBtnShadow];
    Fill(e, r.left, r.top, width, height, face);
    e.cell(e.context, x, y, 'o', first, size);
    e.cell(e.context, x, y, 'p', second, size);
    return true;
}

// One cached Marlett font, rebuilt whenever the cell size changes.
inline unsigned CellFont(Environment &e, const Gdi::RECT &r)
{
    int size = W(r.bottom - r.top);
    const int width = W(r.right - r.left);
    if (size > width)
        size = width;
    if (size <= 0)
        return 0;
    if (e.cached_font) {
        if (e.cached_size == size)
            return e.cached_font;
        e.delete_font(e.context, e.cached_font);
    }
    e.cached_size = size;
    e.cached_font = e.create_font(e.context, size);
    return e.cached_font;
}

}   // namespace Detail

// the body the public export reaches after its handle checks.
inline bool DrawFrameControl(Environment &e, Gdi::RECT *rectangle, unsigned type, unsigned state)
{
    using namespace Detail;
    if (!rectangle || !e.colors || !e.painter.fill || !e.cell || !e.create_font ||
        !e.delete_font || !e.select_font || !e.set_bk_mode)
        return false;
    bool result = true;
    bool framed = false;
    Gdi::RECT r = *rectangle;                       // Windows 98 copies into USER's frame
    state &= 0xffff;
    type &= 0xffff;
    if (e.painter.depth == 1)
        state |= Mono;
    if (state & Mono)
        state |= Flat;
    const bool skip = type == Menu || type == PopupMenu ||
                      (type == Button && !(state & ButtonPush)) ||
                      (type == Scroll && (state & 8));
    if (!skip) {
        // Everything but a scroll control also asks for the soft outer edge.
        const unsigned edge = (state & (Flat | Mono)) | (type == Scroll ? 0x2000u : 0x3000u);
        if (!Win98Draw::ButtonFrame(e.painter, &r, state, edge))
            return false;
        if (state & AdjustRect)
            *rectangle = r;
        framed = true;
    }
    const unsigned font = CellFont(e, r);
    if (!font)
        return false;
    const int previous_mode = e.set_bk_mode(e.context, 1);
    const unsigned previous_font = e.select_font(e.context, font);
    if (!framed) {
        if (type == Menu || type == PopupMenu) {
            if (state & 0x18) {
                // The undocumented pair of scroll cells a menu uses; the
                // background comes from the caller's rectangle, not the frame.
                if (!(state & Transparent))
                    FillIndexed(e, *rectangle, ColorMenu + 1);
                ScrollCell(e, (state & 0x1100) | (state & 8 ? 0u : 1u), r);
            } else if (!MenuCell(e, type, state, r)) {
                result = false;
            }
        } else if (type == Button) {
            result = ButtonCell(e, state, r);   //
        } else {
            SizeGrip(e, state, *rectangle);
        }
    } else if (type == Caption) {
        DrawCell(e, CaptionCell(state), state, r);
    } else if (type == Scroll) {
        ScrollCell(e, state, r);
    } else if (type != Button) {
        result = false;
    }
    e.set_bk_mode(e.context, previous_mode);
    e.select_font(e.context, previous_font);
    return result;
}

// The recorded display target both the host comparison and the retained
// replay use. Its fills, cells and font calls are hashed in call order.
struct Probe {
    unsigned char *pixels = nullptr;
    int width = 96, height = 80;
    unsigned trace = 2166136261u;
    unsigned created = 0, deleted = 0, cells_drawn = 0;
    unsigned colors[ColorCount] = {};
    unsigned next_font = 1;
    int sizes[64] = {};
    int current = 0;
};

// keep retains USER's cached Marlett font across calls, the way repeated
// calls reuse the cached font.
bool Run(Probe &, Gdi::RECT *, unsigned type, unsigned state, int depth,
         int border_width, int border_height, unsigned menu_background,
         unsigned check_background, bool keep);

// Native adapter: the same body drawing through the project's GDI, with the
// Marlett cells rendered from the original outlines.
bool DrawFrameControl(Gdi::HDC, Gdi::RECT *, unsigned type, unsigned state,
                      unsigned menu_background=ColorWindow+1, unsigned check_background=ColorWindowFrame+1);
// The classic Win98 COLOR_ table this adapter and GetSysColor share.
const unsigned *SystemColors();
// The application font service supplies a new Marlett HFONT at this height.
// The drawing adapter selects, restores and deletes it. Without a provider,
// standalone/freestanding drawing keeps using the generated glyph cells.
void SetFontProvider(Gdi::HFONT (*)(int height));
// USER SetOneSysColor writes one entry of that table. The colour is
// stored the way the table holds it, 00RRGGBB.
bool SetSystemColor(unsigned index, unsigned colour);
// USER: on a display with a palette every colour except
// COLOR_BACKGROUND, the three border and workspace colours and COLOR_INFOBK
// is the nearest colour the default palette has (GetNearestColor), so the 3-D
// edges stay solid. After a mode change the table follows the new display;
// `changed` hears every entry whose shown colour moved (COLORREF), for the
// system brushes.
void RefreshSystemColors(bool palette, void (*changed)(unsigned index, unsigned colorref));

// Replay of the retained original results; zero when every vector matches.
unsigned OriginalSelfTest();
unsigned OriginalTestCount();

}   // namespace Win98Frame
