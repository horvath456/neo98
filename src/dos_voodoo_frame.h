// SPDX-License-Identifier: GPL-2.0-or-later
// What the 3dfx Voodoo of the DOS machine draws in one frame, as the machine
// hands it to the host: the card is emulated in the DOS core down to its
// registers, and its pixels are drawn by Gpu::Backend (src/dos_voodoo.cpp).
// The core follows every write to the card. Where its own software rasterizer
// would fill spans, it records a command with the registers of the pixel
// pipeline as the program left them and the three vertices the triangle
// set-up of the card computed. This header describes that record. It is plain
// C++ without a type of the core or of the port, because both include it:
// tools/build_dosbox_pure.py copies it into the patched clone of the core.
//
#pragma once
#define NEO98_DOS_VOODOO_FRAME_H

// A vertex in the layout of Gpu::GlideVertex. x and y are screen pixels with
// the pixel centres at one half, y counted as the card's registers count it:
// from the top, or from the bottom with the Y origin bit of fbzMode.
struct DosVoodooVertex
{
    float x = 0, y = 0;
    float z = 0;            // iterated Z as the 16 bit depth, 0 to 65535
    float oow = 0;          // iterated 1/w of the FBI
    float r = 0, g = 0, b = 0, a = 0;   // 0 to 255
    // Per TMU: s/w and t/w in texels of a texture 256 wide, and the 1/w the
    // TMU divides them by (1 without perspective correction).
    float s0 = 0, t0 = 0, q0 = 0, s1 = 0, t1 = 0, q1 = 0;
};

struct DosVoodooCommand
{
    // Triangles: three vertices each. Pixels: one vertex per pixel the
    // program wrote through the linear frame buffer, Raw without and Blended
    // with the pixel pipeline (alpha test, fog, blending). Fill is
    // fastfillCMD, Clip the clip rectangle for the triangles that follow.
    enum Type : unsigned { Triangles, RawPixels, BlendedPixels, Fill, Clip };
    unsigned type = Triangles;
    unsigned buffer = 0;        // the colour buffer drawn to, 0 to 2
    unsigned first_vertex = 0, vertex_count = 0;
    // The registers of the card. A pixel command carries only the bits that
    // apply to it.
    unsigned fbz_mode = 0, color_path = 0, alpha_mode = 0, fog_mode = 0;
    // color0 and color1 (the names are macros in the core), chromaKey,
    // fogColor and zaColor.
    unsigned constant0 = 0, constant1 = 0, chroma_key = 0, fog_color = 0, za_color = 0;
    // Per TMU: whether it takes part, textureMode, tLOD and tDetail, and the
    // texture it samples as an index of DosVoodooTexture.
    bool textured[2] = {false, false};
    unsigned texture_mode[2] = {0, 0}, lod[2] = {0, 0}, detail[2] = {0, 0};
    unsigned texture[2] = {0, 0};
    // Fill and Clip: left, top, right, bottom in screen pixels from the top;
    // not active means the whole buffer. Fill clears the depth only on a
    // card that has an auxiliary buffer.
    bool clip_active = false;
    int clip[4] = {0, 0, 0, 0};
    bool auxiliary = false;
};

// A texture the core decoded from the memory of a TMU: every mip level it
// found, the largest first, as 0xAARRGGBB. An index is used again for other
// texels once the core has not drawn with it for a while.
struct DosVoodooTexture
{
    unsigned index = 0;
    unsigned width = 0, height = 0;
    unsigned levels = 1;
    const unsigned *texels = nullptr;
};

struct DosVoodooFrame
{
    // The card is initialised and its video output replaces the VGA's. While
    // it is not, the picture of the machine is the VGA's again.
    bool active = false;
    unsigned width = 0, height = 0;
    unsigned y_origin = 0;      // rows count as y_origin + 1 - y with the Y origin bit
    unsigned shown = 0;         // the colour buffer the monitor shows after this frame
    const DosVoodooCommand *commands = nullptr;
    unsigned command_count = 0;
    const DosVoodooVertex *vertices = nullptr;
    unsigned vertex_count = 0;
    const DosVoodooTexture *textures = nullptr;
    unsigned texture_count = 0;
    unsigned char fog_table[64] = {};
    // The gamma of the DAC between the frame buffer and the monitor, per
    // channel: out = in ^ exponent * factor, both from 0 to 1.
    float gamma_exponent[3] = {1, 1, 1}, gamma_factor[3] = {1, 1, 1};
    // Not zero: the program reads through the linear frame buffer, and the
    // record was cut there instead of at the vertical blank. The bits of
    // DosVoodooReadback name the buffers the host fills once it has drawn
    // the frame; the sink returns only then, because the program waits for
    // the pixels.
    unsigned read_mask = 0;
    // With a pitch: the buffers of the card as a saved state held them, to be put into the host's buffers before the
    // commands of this frame are drawn. Rows of restore_pitch pixels from
    // the top, the colour buffers as 565 and the depth as 16 bits, like the
    // readback; a pointer that is null leaves its buffer alone.
    const unsigned short *restore_color[3] = {nullptr, nullptr, nullptr};
    const unsigned short *restore_depth = nullptr;
    unsigned restore_pitch = 0;
};

// The sides of the linear frame buffer the program reads back: the three
// colour buffers as 565 and the depth buffer, rows of Pitch pixels from the
// top, at the resolution of the card whatever the host draws at. The host
// fills the ones a frame asks for (DosVoodooFrame::read_mask) after it has
// drawn that frame; the core reads them.
struct DosVoodooReadback
{
    static constexpr unsigned Pitch = 1024, Rows = 1024;
    // The bits of read_mask: Color << n for colour buffer n, and the depth.
    enum : unsigned { Color = 1, Depth = 8 };
    unsigned short *color[3] = {nullptr, nullptr, nullptr};
    unsigned short *depth = nullptr;
};
