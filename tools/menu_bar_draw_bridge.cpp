// SPDX-License-Identifier: GPL-2.0-or-later
// Host bridge for the menu item painting of win98_menu_bar_draw.h.
#include "../src/win98_menu_bar_draw.h"

namespace {
struct Probe {
    unsigned trace = 2166136261u;
    const unsigned *colors = nullptr;
    void Record(const unsigned *values, unsigned count)
    {
        for (unsigned i = 0; i < count; ++i)
            for (unsigned b = 0; b < 4; ++b)
                trace = (trace ^ ((values[i] >> (b * 8)) & 255)) * 16777619u;
    }
    unsigned owner_background = 0;
    bool owner_active = false;
    void Fill(int x, int y, int w, int h)
    {
        const unsigned values[] = {0x464c4c46u, (unsigned)x, (unsigned)y, (unsigned)w,
                                   (unsigned)h, colors[Win98MenuDraw::ColorHighlight]};
        Record(values, 6);
    }
    // 's border runs.
    void Begin(const Win98MenuDraw::Edge &edge)
    {
        owner_active = edge.owner_background;
        if (!owner_active)
            return;
        const unsigned values[] = {0x564f5250u, 0x6e1au};
        Record(values, 2);
    }
    void Middle(const Win98MenuDraw::Edge &) {}
    void Edge(int x, int y, int w, int h, unsigned index)
    {
        const unsigned values[] = {0x464c4c46u, (unsigned)x, (unsigned)y, (unsigned)w,
                                   (unsigned)h, owner_active ? owner_background : colors[index]};
        Record(values, 6);
    }
};
}

// item: owner_draw, state, state3, bitmap, x, y, cx, cy
// menu: flags4, flags5, font, owner_modern
// words: selection_a, selection_b, plain_gray, marker_x, marker_y
extern "C" unsigned menu_item_probe(const int *item_values, const int *menu_values,
                                    const int *words, const unsigned *colors, unsigned mode,
                                    unsigned *out_background, unsigned *out_text,
                                    unsigned *out_flags, unsigned *edge_state,
                                    unsigned owner_background, unsigned *trace)
{
    Win98MenuDraw::Settings settings;
    settings.colors = colors;
    settings.selection_a = (unsigned)words[0];
    settings.selection_b = (unsigned)words[1];
    settings.plain_gray = (unsigned)words[2];
    settings.marker_x = words[3];
    settings.marker_y = words[4];
    Win98MenuDraw::Item item;
    item.owner_draw = (unsigned)item_values[0];
    item.state = (unsigned)item_values[1];
    item.state3 = (unsigned)item_values[2];
    item.bitmap = (unsigned)item_values[3];
    item.x = item_values[4];
    item.y = item_values[5];
    item.cx = item_values[6];
    item.cy = item_values[7];
    Win98MenuDraw::Menu menu;
    menu.flags4 = (unsigned)menu_values[0];
    menu.flags5 = (unsigned)menu_values[1];
    menu.font = (unsigned)menu_values[2];
    menu.owner_modern = menu_values[3] != 0;
    const auto colorsOut = Win98MenuDraw::ItemColors(settings, item, menu, mode);
    Probe probe;
    probe.colors = colors;
    probe.owner_background = owner_background;
    const unsigned background[] = {0x4c434b42u, colors[colorsOut.background]};
    probe.Record(background, 2);
    const unsigned text[] = {0x4c435854u, colors[colorsOut.text]};
    probe.Record(text, 2);
    if (colorsOut.fill) {
        if (colorsOut.owner_background) {
            const unsigned provider[] = {0x564f5250u, 0x6e1au};
            probe.Record(provider, 2);
        }
        const unsigned fill[] = {0x464c4c46u, (unsigned)item.x, (unsigned)item.y,
                                 (unsigned)item.cx, (unsigned)item.cy,
                                 colorsOut.owner_background ? owner_background
                                                            : colors[colorsOut.background]};
        probe.Record(fill, 6);
    }
    const unsigned glyphs[] = {0x54584554u, (unsigned)item.x, (unsigned)item.y,
                               (unsigned)item.cx, (unsigned)item.cy, colorsOut.flags};
    probe.Record(glyphs, 6);
    const auto edge = Win98MenuDraw::ItemEdge(settings, item, menu, *edge_state);
    Win98MenuDraw::DrawItemEdge(probe, item, edge);
    probe.owner_active = false;
    Win98MenuDraw::Marker(probe, settings, item);
    *out_background = colors[colorsOut.background];
    *out_text = colors[colorsOut.text];
    *out_flags = colorsOut.flags;
    *trace = probe.trace;
    return Win98MenuDraw::EdgeFlags(settings);
}
