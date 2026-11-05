// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string.h>
#include "pc_vga_palette.h"

// System-VM VGA. DOS processes keep their own DOSBox VGA. Register and memory
// semantics follow the IBM VGA register interface, also implemented in the
// pinned DOSBox Pure src/hardware/vga_{seq,gfx,crtc,memory}.cpp.
namespace PcVga
{
struct Device
{
    unsigned char planes[4][65536] = {}, latch[4] = {};
    unsigned char seq[8] = {}, gfx[16] = {}, crtc[32] = {}, attr[32] = {};
    unsigned char dac[256][3] = {};
    unsigned char si = 0, gi = 0, ci = 0, ai = 0, misc = 0x63, mask = 255;
    unsigned dac_read = 0, dac_write = 0, dac_state = 0;
    bool attr_data = false, enabled = true, active = false;
    unsigned mode = 3;

    void Mode(unsigned value)
    {
        memcpy(dac, DefaultPalette, sizeof dac);
        mode = value & 0x7f;
        active = mode == 0x13 || mode == 0x12;
        if (!(value & 0x80)) memset(planes, 0, sizeof planes);
        memset(seq, 0, sizeof seq); memset(gfx, 0, sizeof gfx);
        memset(crtc, 0, sizeof crtc); memset(attr, 0, sizeof attr);
        seq[0] = 3; seq[1] = 1; seq[2] = 15; seq[4] = mode == 0x13 ? 14 : 6;
        gfx[5] = mode == 0x13 ? 0x40 : 0; gfx[6] = 5; gfx[7] = 15; gfx[8] = 255;
        const unsigned char mode13[] = {0x5f,0x4f,0x50,0x82,0x54,0x80,0xbf,0x1f,
            0,0x41,0,0,0,0,0,0,0x9c,0x8e,0x8f,0x28,0x40,0x96,0xb9,0xa3,0xff};
        memcpy(crtc, mode13, sizeof mode13);
        if (mode == 0x12)
        { crtc[7] = 0x3e; crtc[9] = 0x40; crtc[0x12] = 0xdf; crtc[0x14] = 0; crtc[0x17] = 0xe3; }
        for (unsigned i = 0; i < 16; ++i) attr[i] = i;
        attr[0x10] = mode == 0x13 ? 0x41 : 1; attr[0x12] = 15;
        mask = 255; enabled = true; attr_data = false;
    }
    bool Offset(unsigned &address) const
    {
        if (address < 0xa0000 || address >= 0xc0000) return false;
        address -= 0xa0000;
        switch ((gfx[6] >> 2) & 3)
        {
        case 1: return address < 0x10000;
        case 2: if (address < 0x10000 || address >= 0x18000) return false; address -= 0x10000; break;
        case 3: if (address < 0x18000) return false; address -= 0x18000; break;
        }
        address &= 65535;
        return true;
    }
    unsigned char Read(unsigned address)
    {
        if (!Offset(address)) return 255;
        unsigned plane = gfx[4] & 3;
        if (seq[4] & 8) { plane = address & 3; address >>= 2; }
        else if (gfx[5] & 0x10) { plane = (plane & 2) | (address & 1); address &= ~1u; }
        for (unsigned p = 0; p < 4; ++p) latch[p] = planes[p][address];
        if (!(gfx[5] & 8)) return latch[plane];
        unsigned result = 255;
        for (unsigned p = 0; p < 4; ++p)
            if (gfx[7] & (1u << p)) result &= latch[p] ^ ((gfx[2] & (1u << p)) ? 0 : 255);
        return result;
    }
    void Write(unsigned address, unsigned char value)
    {
        if (!Offset(address)) return;
        unsigned maps = seq[2];
        if (seq[4] & 8) { maps &= 1u << (address & 3); address >>= 2; }
        else if (!(seq[4] & 4)) { maps &= 5u << (address & 1); address &= ~1u; }
        const unsigned rotate = gfx[3] & 7, wm = gfx[5] & 3;
        const unsigned char rotated = (value >> rotate) | (value << ((8 - rotate) & 7));
        for (unsigned p = 0; p < 4; ++p)
        {
            if (!(maps & (1u << p))) continue;
            if (wm == 1) { planes[p][address] = latch[p]; continue; }
            unsigned bits = gfx[8], data = rotated;
            if (wm == 0 && (gfx[1] & (1u << p))) data = (gfx[0] & (1u << p)) ? 255 : 0;
            if (wm == 2) data = (value & (1u << p)) ? 255 : 0;
            if (wm == 3) { data = (gfx[0] & (1u << p)) ? 255 : 0; bits &= rotated; }
            switch ((gfx[3] >> 3) & 3)
            {
            case 1: data &= latch[p]; break;
            case 2: data |= latch[p]; break;
            case 3: data ^= latch[p]; break;
            }
            planes[p][address] = (data & bits) | (latch[p] & ~bits);
        }
    }
    unsigned Input(unsigned port, unsigned retrace)
    {
        switch (port)
        {
        case 0x3c0: return ai | (enabled ? 32 : 0);
        case 0x3c1: return attr[ai];
        case 0x3c4: return si;
        case 0x3c5: return seq[si];
        case 0x3c6: return mask;
        case 0x3c7: return dac_state;
        case 0x3c8: return dac_write / 3;
        case 0x3c9: { unsigned at = dac_read; dac_read = (at + 1) % 768; return dac[at / 3][at % 3]; }
        case 0x3cc: return misc;
        case 0x3ce: return gi;
        case 0x3cf: return gfx[gi];
        case 0x3b4: case 0x3d4: return ci;
        case 0x3b5: case 0x3d5: return crtc[ci];
        case 0x3ba: case 0x3da: attr_data = false; return retrace;
        default: return 255;
        }
    }
    void Output(unsigned port, unsigned char value)
    {
        switch (port)
        {
        case 0x3c0:
            if (attr_data) attr[ai] = value;
            else { ai = value & 31; enabled = value & 32; }
            attr_data = !attr_data; break;
        case 0x3c2: misc = value; break;
        case 0x3c4: si = value & 7; break;
        case 0x3c5: seq[si] = value; break;
        case 0x3c6: mask = value; break;
        case 0x3c7: dac_read = value * 3; dac_state = 3; break;
        case 0x3c8: dac_write = value * 3; dac_state = 0; break;
        case 0x3c9: dac[dac_write / 3][dac_write % 3] = value & 63; dac_write = (dac_write + 1) % 768; break;
        case 0x3ce: gi = value & 15; break;
        case 0x3cf: gfx[gi] = value; break;
        case 0x3b4: case 0x3d4: ci = value & 31; break;
        case 0x3b5: case 0x3d5:
            if (ci < 8 && (crtc[0x11] & 128))
            { if (ci == 7) crtc[7] = (crtc[7] & ~16) | (value & 16); }
            else crtc[ci] = value;
            break;
        }
    }
    unsigned Width() const { return (crtc[1] + 1) * ((gfx[5] & 0x40) ? 4 : 8); }
    unsigned Height() const
    {
        unsigned lines = crtc[0x12] | ((crtc[7] & 2) << 7) | ((crtc[7] & 64) << 3);
        return (lines + 1) / ((crtc[9] & 31) + 1) / ((crtc[9] & 128) ? 2 : 1);
    }
    unsigned Pixel(unsigned x, unsigned y) const
    {
        const unsigned start = crtc[0xc] * 256 + crtc[0xd];
        const unsigned stride = crtc[0x13] * 2;
        const unsigned address = (start + y * stride + x / ((gfx[5] & 0x40) ? 4 : 8)) & 65535;
        unsigned index = 0;
        if (gfx[5] & 0x40) index = planes[x & 3][address];
        else
        {
            for (unsigned p = 0; p < 4; ++p) index |= ((planes[p][address] >> (7 - (x & 7))) & 1) << p;
            index = attr[index & attr[0x12]];
        }
        if (!enabled || (seq[1] & 32)) index = 0;
        index &= mask;
        unsigned rgb = 0;
        for (unsigned c = 0; c < 3; ++c) rgb = (rgb << 8) | (dac[index][c] * 255u / 63u);
        return rgb;
    }
};
}
