// SPDX-License-Identifier: GPL-2.0-or-later
// Host bridge for the OEM bitmap strip comparison in tools/test_oem_bitmaps.py.
#include "win98_oem_bitmaps.h"

namespace {

Win98Oem::Metrics Read(const int *values)
{
    Win98Oem::Metrics m;
    m.scroll_width = values[0];
    m.scroll_height = values[1];
    m.border_y = values[2];
    m.scroll_cross_y = values[3];
    m.scroll_cross_x = values[4];
    m.size_x = values[5];
    m.size_y = values[6];
    m.edge_x = values[7];
    m.edge_y = values[8];
    m.small_x = values[9];
    m.small_y = values[10];
    m.caption_x = values[11];
    m.caption_y = values[12];
    m.menu_extra = values[13];
    m.menu_bias = values[14];
    m.menu_scroll = values[15];
    return m;
}

unsigned Crc32(const unsigned char *data, unsigned length)
{
    unsigned crc = 0xffffffffu;
    for (unsigned i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (unsigned)(-(int)(crc & 1)));
    }
    return ~crc;
}

}   // namespace

extern "C" {

void oem_size(unsigned index, const int *values, int *out)
{
    const Win98Oem::Metrics m = Read(values);
    int cx = 0, cy = 0;
    Win98Oem::Size(m, index, cx, cy);
    out[0] = cx;
    out[1] = cy;
}

int oem_pack(int width, const int *values, int *out)
{
    const Win98Oem::Metrics m = Read(values);
    Win98Oem::Cell cells[Win98UserResources::StripCount] = {};
    const int height = Win98Oem::Pack(m, width, cells);
    for (unsigned i = 0; i < Win98UserResources::StripCount; ++i) {
        out[i * 4 + 0] = cells[i].x;
        out[i * 4 + 1] = cells[i].y;
        out[i * 4 + 2] = cells[i].cx;
        out[i * 4 + 3] = cells[i].cy;
    }
    return height;
}

// The imported table, in the original's own byte order, so the comparison can
// prove the header carries the table and not a transcription of it.
unsigned oem_table(void)
{
    unsigned char raw[Win98UserResources::StripCount * 4];
    for (unsigned i = 0; i < Win98UserResources::StripCount; ++i) {
        raw[i * 4 + 0] = (unsigned char)(Win98UserResources::StripCalls[i][0] & 255);
        raw[i * 4 + 1] = (unsigned char)(Win98UserResources::StripCalls[i][0] >> 8);
        raw[i * 4 + 2] = (unsigned char)(Win98UserResources::StripCalls[i][1] & 255);
        raw[i * 4 + 3] = (unsigned char)(Win98UserResources::StripCalls[i][1] >> 8);
    }
    return Crc32(raw, sizeof raw);
}

}
