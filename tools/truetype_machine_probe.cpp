// SPDX-License-Identifier: GPL-2.0-or-later
// Drives src/truetype_machine.cpp over the files of build/win98-c and prints
// what the original driver answers. tools/test_truetype_machine.py reads it.
#include "../src/truetype_machine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
const char *tree = "build/win98-c";

bool Read(const char *path, unsigned char *&data, unsigned &size)
{
    // "C:\\WINDOWS\\FONTS\\ARIAL.TTF" under the tree, case-insensitively.
    char full[1024];
    snprintf(full, sizeof full, "%s%s", tree, path[1] == ':' ? path + 2 : path);
    for (char *p = full; *p; ++p)
        if (*p == '\\')
            *p = '/';
    FILE *file = fopen(full, "rb");
    if (!file)
    {
        // The tree stores its names in upper case; try that.
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
    // The probe answers the one value the driver needs to find a font file.
    if (strstr(key, "Fonts Folder") && !strcmp(name, "Fonts"))
    {
        snprintf(reinterpret_cast<char *>(data), size, "%s", "C:\\WINDOWS\\FONTS");
        size = unsigned(strlen(reinterpret_cast<char *>(data))) + 1;
        return true;
    }
    (void)key;
    (void)name;
    return false;
}
} // namespace

int main(int argc, char **argv)
{
    const char *font = argc > 1 ? argv[1] : "C:\\WINDOWS\\FONTS\\ARIAL.TTF";
    const int ppem = argc > 2 ? atoi(argv[2]) : 13;
    const char *text = argc > 3 ? argv[3] : "Ww";
    if (argc > 4)
        tree = argv[4];
    // A symbol font maps its characters at F000h + code; GDI passes that
    // start to entry 30 (with the start of FONTINFO+50h).
    const unsigned base = argc > 5 ? unsigned(strtoul(argv[5], nullptr, 16)) : 0;
    TrueTypeMachine::SetReader(Read);
    TrueTypeMachine::SetQuery(Query);
    if (!TrueTypeMachine::Start())
    {
        printf("start failed: %s\n", TrueTypeMachine::Failure());
        return 2;
    }
    bool ok = false;
    unsigned arguments[8] = {TrueTypeMachine::String(font), 0};
    const unsigned file = TrueTypeMachine::Call(TrueTypeMachine::OpenFile, arguments, 2, ok);
    printf("open %08x ok=%d\n", file, ok);
    if (!ok || !file)
        return 3;
    arguments[0] = file;
    const unsigned context = TrueTypeMachine::Call(TrueTypeMachine::NewContext, arguments, 1, ok);
    printf("context %08x ok=%d\n", context, ok);
    if (!ok || !context)
        return 4;
    // The transformation record of GDI: size, the two spot sizes of the
    // device and the 2x2 matrix in 16.16.
    const unsigned record = TrueTypeMachine::Allocate(0x1c);
    const unsigned fields[7] = {0x1c, 0xb505, 0xb505, unsigned(ppem) << 16, 0, 0, unsigned(ppem) << 16};
    TrueTypeMachine::Write(record, fields, sizeof fields);
    arguments[0] = context;
    arguments[1] = record;
    printf("transform %08x ok=%d\n", TrueTypeMachine::Call(TrueTypeMachine::SetTransform, arguments, 2, ok), ok);
    if (!ok)
        return 5;
    const unsigned metrics = TrueTypeMachine::Allocate(0x20), bitmap = TrueTypeMachine::Allocate(65536);
    // GDI maps characters to glyph indices through entry 30 (GDI) and then asks for the glyph by index; a symbol font needs it,
    // since its cmap maps F000h + code.
    const unsigned count = unsigned(strlen(text));
    const unsigned input = TrueTypeMachine::Allocate(count * 2 + 2);
    const unsigned output = TrueTypeMachine::Allocate(count * 2 + 2);
    for (unsigned i = 0; i < count; ++i)
    {
        const unsigned short code = (unsigned short)(base + (unsigned char)text[i]);
        TrueTypeMachine::Write(input + i * 2, &code, 2);
    }
    const unsigned mapping[5] = {file, output, count * 2, input, 0};
    printf("indices %u ok=%d", TrueTypeMachine::Call(TrueTypeMachine::GlyphIndices, mapping, 5, ok), ok);
    for (unsigned i = 0; i < count; ++i)
    {
        unsigned short index = 0;
        TrueTypeMachine::Read(output + i * 2, &index, 2);
        printf(" %c=%u", text[i], index);
    }
    printf("\n");
    if (!ok)
        return 7;
    // A seventh argument repeats the glyph requests without printing, for
    // timing the engine.
    for (unsigned pass = argc > 6 ? unsigned(atoi(argv[6])) : 0; pass; --pass)
        for (unsigned i = 0; i < count; ++i)
        {
            unsigned short index = 0;
            TrueTypeMachine::Read(output + i * 2, &index, 2);
            const unsigned request[7] = {context, 0x80000031u, metrics, bitmap, 65536, index, 0};
            TrueTypeMachine::Call(TrueTypeMachine::Glyph, request, 6, ok);
            if (!ok)
                return 6;
        }
    for (unsigned i = 0; i < count; ++i)
    {
        const char *c = text + i;
        unsigned short index = 0;
        TrueTypeMachine::Read(output + i * 2, &index, 2);
        const unsigned request[7] = {context, 0x80000031u, metrics, bitmap, 65536, index, 0};
        const unsigned bytes = TrueTypeMachine::Call(TrueTypeMachine::Glyph, request, 6, ok);
        int values[6] = {};
        TrueTypeMachine::Read(metrics, values, sizeof values);
        printf("glyph %c index=%u bytes=%u box=%dx%d origin=%d,%d advance=%d ok=%d\n", *c, index, bytes,
               values[0], values[1], values[2] >> 16, values[3] >> 16, values[4] >> 16, ok);
        if (!ok)
            return 6;
        const unsigned stride = ((values[0] + 31) / 32) * 4;
        unsigned char rows[8192] = {};
        TrueTypeMachine::Read(bitmap, rows, bytes < sizeof rows ? bytes : sizeof rows);
        for (int y = 0; y < values[1]; ++y)
        {
            char line[128] = {};
            for (int x = 0; x < values[0] && x < 120; ++x)
                line[x] = rows[y * stride + x / 8] & (0x80 >> (x & 7)) ? '#' : '.';
            printf("  %s\n", line);
        }
    }
    return 0;
}
