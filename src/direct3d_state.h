// SPDX-License-Identifier: GPL-2.0-or-later
// What the two Direct3D drivers of the port share besides the rasterizer.
// The Direct3D HAL of the display driver (src/x86_ddhal.cpp) serves the
// original D3DIM700.DLL of an x86 process, and the driver below the ARM64
// DDRAW.DLL (src/x86_dx_d3d.cpp) serves the Direct3D of the SDK. Both keep a
// Gpu::State per context and hand it to the same Gpu::Backend, so the state
// a device starts with, the way a D3DLIGHT7, a D3DMATERIAL7 and a transform
// land in it, and the surface formats the rasterizer takes are written once,
// here.
#pragma once
#include "gpu_backend.h"
#include "win98_ddhal.h"
#include <string.h>
namespace Direct3DState
{
namespace H = Win98DDHal;

// A DDPIXELFORMAT as far as a driver looks at it: flags, bit count, red,
// green, blue and alpha masks, and the FourCC of a compressed format.
struct Format
{
    unsigned flags, bits, r, g, b, a, fourcc = 0;
};
// The texture formats the drivers offer.
inline constexpr Format texture_formats[] = {
    {H::PixelFormat::RGB | H::PixelFormat::AlphaPixels, 32, 0xff0000, 0xff00, 0xff, 0xff000000u},
    {H::PixelFormat::RGB, 32, 0xff0000, 0xff00, 0xff, 0},
    {H::PixelFormat::RGB, 16, 0xf800, 0x07e0, 0x001f, 0},
    {H::PixelFormat::RGB | H::PixelFormat::AlphaPixels, 16, 0x7c00, 0x03e0, 0x001f, 0x8000},
    {H::PixelFormat::RGB | H::PixelFormat::AlphaPixels, 16, 0x0f00, 0x00f0, 0x000f, 0xf000},
    {H::PixelFormat::RGB, 16, 0x7c00, 0x03e0, 0x001f, 0},
    {H::PixelFormat::RGB | H::PixelFormat::Indexed8, 8, 0, 0, 0, 0},
    // Bump maps: V8U8, L6V5U5 and X8L8V8U8 (du, dv, luminance masks).
    {H::PixelFormat::BumpDuDv, 16, 0x00ff, 0xff00, 0, 0},
    {H::PixelFormat::BumpDuDv | H::PixelFormat::BumpLuminance, 16, 0x001f, 0x03e0, 0xfc00, 0},
    {H::PixelFormat::BumpDuDv | H::PixelFormat::BumpLuminance, 32, 0x0000ff, 0x00ff00, 0xff0000, 0},
    // The block compressed formats, offered when the rasterizer samples them
    // (Backend::CompressedTextures); they stay last in this list.
    {H::PixelFormat::FourCCFlag, 0, 0, 0, 0, 0, H::FourCC::Dxt1},
    {H::PixelFormat::FourCCFlag, 0, 0, 0, 0, 0, H::FourCC::Dxt2},
    {H::PixelFormat::FourCCFlag, 0, 0, 0, 0, 0, H::FourCC::Dxt3},
    {H::PixelFormat::FourCCFlag, 0, 0, 0, 0, 0, H::FourCC::Dxt4},
    {H::PixelFormat::FourCCFlag, 0, 0, 0, 0, 0, H::FourCC::Dxt5},
};
constexpr unsigned TextureFormatCount = sizeof texture_formats / sizeof texture_formats[0];
constexpr unsigned CompressedFormatCount = 5;
// The Z buffer formats: 16 and 32 bits of depth, and 24 bits of depth with 8
// of stencil in 32 (dwStencilBitDepth in the place of the red mask, then the
// Z mask and the stencil mask).
inline constexpr Format z_formats[] = {
    {H::PixelFormat::ZBuffer, 16, 0, 0xffff, 0, 0},
    {H::PixelFormat::ZBuffer, 32, 0, 0xffffffffu, 0, 0},
    {H::PixelFormat::ZBuffer | H::PixelFormat::StencilBuffer, 32, 8, 0x00ffffff, 0xff000000u, 0},
};
constexpr unsigned ZFormatCount = sizeof z_formats / sizeof z_formats[0];

// The bytes of one 4x4 block of a FourCC the drivers know, or 0.
inline unsigned BlockBytes(unsigned fourcc)
{
    if (fourcc == H::FourCC::Dxt1) return 8;
    if (fourcc == H::FourCC::Dxt2 || fourcc == H::FourCC::Dxt3 || fourcc == H::FourCC::Dxt4 || fourcc == H::FourCC::Dxt5) return 16;
    return 0;
}
// A compressed surface is rows of 4x4 blocks; its size is its linear size.
inline unsigned BlockPitch(unsigned width, unsigned fourcc) { return ((width + 3) / 4 ? (width + 3) / 4 : 1) * BlockBytes(fourcc); }
inline unsigned LinearSize(unsigned width, unsigned height, unsigned fourcc)
{
    return BlockPitch(width, fourcc) * ((height + 3) / 4 ? (height + 3) / 4 : 1);
}

// The state a DirectX 7 device starts with.
inline void Default(Gpu::State &s)
{
    namespace RS = H::RS;
    namespace TSS = H::TSS;
    s = Gpu::State{};
    s.render[RS::ZEnable] = 1;
    s.render[RS::FillMode] = 3;
    s.render[RS::ShadeMode] = 2;
    s.render[RS::ZWriteEnable] = 1;
    s.render[RS::SrcBlend] = 2;
    s.render[RS::DestBlend] = 1;
    s.render[RS::CullMode] = 3;
    s.render[RS::ZFunc] = 4;
    s.render[RS::AlphaFunc] = 8;
    s.render[RS::Lighting] = 1;
    s.render[RS::TextureFactor] = 0xffffffffu;
    // The transform-and-lighting defaults of DirectX 7: clipping on, vertex
    // colours feed the material (diffuse from colour 1, specular from colour
    // 2), a local viewer, identity transforms.
    s.render[RS::Clipping] = 1;
    s.render[RS::ColorVertex] = 1;
    s.render[RS::LocalViewer] = 1;
    s.render[RS::DiffuseMaterialSource] = 1;
    s.render[RS::SpecularMaterialSource] = 2;
    auto identity = [](float *m) {
        for (unsigned i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    };
    for (auto &w : s.world) identity(w);
    identity(s.view);
    identity(s.projection);
    for (auto &t : s.texture) identity(t);
    for (unsigned i = 0; i < Gpu::State::Stages; ++i)
    {
        s.stage[i][TSS::ColorOp] = i == 0 ? 4 : 1;
        s.stage[i][TSS::ColorArg1] = 2;
        s.stage[i][TSS::ColorArg2] = 1;
        s.stage[i][TSS::AlphaOp] = i == 0 ? 2 : 1;
        s.stage[i][TSS::AlphaArg1] = 2;
        s.stage[i][TSS::AlphaArg2] = 1;
        s.stage[i][TSS::TexCoordIndex] = i;
        s.stage[i][TSS::AddressU] = 1;
        s.stage[i][TSS::AddressV] = 1;
        s.stage[i][TSS::MagFilter] = 1;
        s.stage[i][TSS::MinFilter] = 1;
        s.stage[i][TSS::MipFilter] = 1;
    }
}

inline float FloatAt(const unsigned char *p)
{
    float f;
    memcpy(&f, p, 4);
    return f;
}

// The matrix a D3DTRANSFORMSTATETYPE names: WORLD 1, VIEW 2, PROJECTION 3,
// WORLD1 to WORLD3 4 to 6, TEXTURE0 to TEXTURE7 16 to 23; the DirectX 8
// numbers 256 to 259 name the world matrices too. Null for any other.
inline float *Transform(Gpu::State &s, unsigned type)
{
    if (type == 1) return s.world[0];
    if (type == 2) return s.view;
    if (type == 3) return s.projection;
    if (type >= 4 && type <= 6) return s.world[type - 3];
    if (type >= 16 && type < 16 + Gpu::State::Stages) return s.texture[type - 16];
    if (type >= 256 && type < 260) return s.world[type - 256];
    return nullptr;
}

// The 104 bytes of a D3DLIGHT7 into a light; `enabled` stays.
inline void Light(Gpu::Light &light, const unsigned char *data)
{
    unsigned type;
    memcpy(&type, data, 4);
    light.type = type;
    for (unsigned k = 0; k < 4; ++k)
    {
        light.diffuse[k] = FloatAt(data + 4 + k * 4);
        light.specular[k] = FloatAt(data + 0x14 + k * 4);
        light.ambient[k] = FloatAt(data + 0x24 + k * 4);
    }
    for (unsigned k = 0; k < 3; ++k)
    {
        light.position[k] = FloatAt(data + 0x34 + k * 4);
        light.direction[k] = FloatAt(data + 0x40 + k * 4);
        light.attenuation[k] = FloatAt(data + 0x54 + k * 4);
    }
    light.range = FloatAt(data + 0x4c);
    light.falloff = FloatAt(data + 0x50);
    light.theta = FloatAt(data + 0x60);
    light.phi = FloatAt(data + 0x64);
}

// The 68 bytes of a D3DMATERIAL7: diffuse, ambient, specular, emissive, power.
inline void Material(Gpu::Material &m, const unsigned char *data)
{
    for (unsigned k = 0; k < 4; ++k)
    {
        m.diffuse[k] = FloatAt(data + k * 4);
        m.ambient[k] = FloatAt(data + 16 + k * 4);
        m.specular[k] = FloatAt(data + 32 + k * 4);
        m.emissive[k] = FloatAt(data + 48 + k * 4);
    }
    m.power = FloatAt(data + 64);
}
} // namespace Direct3DState
