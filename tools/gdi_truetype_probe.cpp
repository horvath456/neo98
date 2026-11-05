// SPDX-License-Identifier: GPL-2.0-or-later
// Drives src/gdi_truetype.cpp over the files of build/win98-c: realized
// metrics, text output into a 1 bpp bitmap and GetGlyphOutline. The tests
// tools/test_truetype_machine.py reads it; it also serves to look at one font by
// hand.
//
// gdi-truetype-probe TREE metrics FILE HEIGHT [WIDTH WEIGHT ITALIC]
// gdi-truetype-probe TREE text FILE HEIGHT TEXT [WEIGHT ITALIC]
// gdi-truetype-probe TREE outline FILE HEIGHT CODE FORMAT [BUFFER]
#include "../src/gdi_truetype.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
const char *tree = "build/win98-c";

bool Read(const char *path, unsigned char *&data, unsigned &size)
{
    char full[1024];
    snprintf(full, sizeof full, "%s%s", tree, path[1] == ':' ? path + 2 : path);
    for (char *p = full; *p; ++p)
        if (*p == '\\')
            *p = '/';
    FILE *file = fopen(full, "rb");
    if (!file)
    {
        char *leaf = strrchr(full, '/');
        for (char *p = leaf ? leaf : full; *p; ++p)
            if (*p >= 'a' && *p <= 'z')
                *p = char(*p - 32);
        file = fopen(full, "rb");
    }
    if (!file)
        return false;
    fseek(file, 0, SEEK_END);
    const long bytes = ftell(file);
    fseek(file, 0, SEEK_SET);
    data = static_cast<unsigned char *>(malloc(bytes > 0 ? bytes : 1));
    size = unsigned(bytes);
    const bool ok = data && fread(data, 1, bytes, file) == size_t(bytes);
    fclose(file);
    if (!ok)
        free(data), data = nullptr;
    return ok;
}

bool Query(const char *key, const char *name, unsigned char *data, unsigned &size)
{
    if (strstr(key, "Fonts Folder") && !strcmp(name, "Fonts"))
    {
        snprintf(reinterpret_cast<char *>(data), size, "%s", "C:\\WINDOWS\\FONTS");
        size = unsigned(strlen(reinterpret_cast<char *>(data))) + 1;
        return true;
    }
    return false;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 5)
        return 1;
    tree = argv[1];
    const char *mode = argv[2];
    if (!GdiTrueType::Start(Read, Query))
    {
        printf("start failed: %s\n", GdiTrueType::Failure());
        return 2;
    }
    const auto *face = GdiTrueType::Install(argv[3]);
    if (!face)
    {
        printf("install failed\n");
        return 3;
    }
    printf("face family=%s style=%s full=%s upem=%d ascent=%d cell=%d external=%d average=%d maximum=%d "
           "weight=%u charset=%u pitch=%02x italic=%d first=%02x last=%02x symbol=%x glyphs=%u\n",
           face->family, face->style, face->full, face->upem, face->ascent, face->cell, face->external,
           face->average, face->maximum, face->weight, face->charset, face->pitch, face->italic, face->first,
           face->last, face->symbol_base, face->glyphs);
    const int height = atoi(argv[4]);
    if (!strcmp(mode, "metrics"))
    {
        const int width = argc > 5 ? atoi(argv[5]) : 0;
        const unsigned weight = argc > 6 ? unsigned(atoi(argv[6])) : 400;
        const bool italic = argc > 7 && atoi(argv[7]);
        const auto *font = GdiTrueType::Realize(face, height, width, weight, italic, false, false);
        if (!font)
            return 4;
        printf("font ppem=%d xscale=%u ascent=%d descent=%d internal=%d external=%d average=%d maximum=%d "
               "weight=%u bold=%d italic=%d underline=%d/%d strike=%d/%d\n",
               font->ppem, font->xscale, font->ascent, font->descent, font->internal, font->external,
               font->average, font->maximum, font->weight, font->bold, font->italic, font->underline_top,
               font->underline_thickness, font->strike_top, font->strike_thickness);
        printf("widths");
        for (unsigned c = 32; c < 256; ++c)
            printf(" %u", font->view.Width(c));
        printf("\nindices");
        for (unsigned c = 32; c < 256; ++c)
            printf(" %u", font->index[c]);
        printf("\n");
        return 0;
    }
    if (!strcmp(mode, "text"))
    {
        const char *text = argc > 5 ? argv[5] : "Welcome";
        const unsigned weight = argc > 6 ? unsigned(atoi(argv[6])) : 400;
        const bool italic = argc > 7 && atoi(argv[7]);
        const auto *font = GdiTrueType::Realize(face, height, 0, weight, italic, false, false);
        if (!font)
            return 4;
        const int n = int(strlen(text));
        DibText::DrawMode dm;
        dm.background_mode = 1;
        dm.foreground = 0;
        dm.background = 1;
        const unsigned extent =
            DibText::ExtTextOut(nullptr, 0, 0, {}, reinterpret_cast<const unsigned char *>(text), -n, font->view, dm);
        const int w = int(extent & 0xffff) + 40, h = int(font->view.Height()) + 8;
        const int stride = ((w + 31) / 32) * 4;
        unsigned char *bits = static_cast<unsigned char *>(malloc(size_t(stride * h)));
        memset(bits, 0xff, size_t(stride * h));
        DibEng::Bitmap bitmap = {bits, w, h, stride, 1};
        const Win98::Rect clip = {0, 0, w, h};
        if (!GdiTrueType::Draw(bitmap, 20, 4, clip, reinterpret_cast<const unsigned char *>(text), n, *font, dm,
                               nullptr, nullptr, 0))
            return 5;
        printf("extent %u %u origin 20 4 ascent %d\n", extent & 0xffff, extent >> 16, font->ascent);
        for (int y = 0; y < h; ++y)
        {
            printf("row ");
            for (int x = 0; x < w; ++x)
                putchar(DibEng::ReadPixel(bitmap, x, y) ? '.' : '#');
            putchar('\n');
        }
        return 0;
    }
    if (!strcmp(mode, "outline") && argc > 6)
    {
        const auto *font = GdiTrueType::Realize(face, height, 0, 400, false, false, false);
        if (!font)
            return 4;
        const unsigned code = unsigned(strtoul(argv[5], nullptr, 0)), format = unsigned(strtoul(argv[6], nullptr, 0));
        GdiTrueType::GlyphMetrics gm;
        const unsigned need = GdiTrueType::GlyphOutline(*font, code, format, nullptr, nullptr, 0, gm);
        printf("need %d box %u %u origin %d %d cell %d %d\n", int(need), gm.black_x, gm.black_y, gm.origin_x,
               gm.origin_y, gm.cell_x, gm.cell_y);
        if (need == ~0u || !need)
            return 0;
        const unsigned room = argc > 7 ? unsigned(atoi(argv[7])) : need;
        unsigned char *buffer = static_cast<unsigned char *>(calloc(room, 1));
        const unsigned got = GdiTrueType::GlyphOutline(*font, code, format, nullptr, buffer, room, gm);
        printf("data %u", got);
        for (unsigned i = 0; i < got; ++i)
            printf(" %02x", buffer[i]);
        printf("\n");
        return 0;
    }
    return 1;
}
