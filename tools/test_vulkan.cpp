// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/gpu_backend.h"
#include "host_vulkan.h"
#include "vulkan_texture.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
static void Test16(unsigned green_bits)
{
    // Every packed value, including RGB555's unused high bit and padded rows.
    std::vector<unsigned short> values(256 * 257, 0xa5a5);
    for (unsigned y = 0; y < 256; ++y)
        for (unsigned x = 0; x < 256; ++x)
            values[y * 257 + x] = y * 256 + x;
    Gpu::Surface surface;
    surface.bits = reinterpret_cast<unsigned char *>(values.data());
    surface.width = surface.height = 256;
    surface.pitch = 257 * 2;
    surface.bit_count = 16;
    surface.r_mask = green_bits == 6 ? 0xf800 : 0x7c00;
    surface.g_mask = green_bits == 6 ? 0x7e0 : 0x3e0;
    surface.b_mask = 0x1f;
    std::vector<unsigned char> decoded;
    assert(HostVulkan::Texture::Decode(surface, decoded));
    for (unsigned v = 0; v < 65536; ++v)
    {
        assert(decoded[v * 4] == HostVulkan::Texture::Channel(v, surface.r_mask));
        assert(decoded[v * 4 + 1] == HostVulkan::Texture::Channel(v, surface.g_mask));
        assert(decoded[v * 4 + 2] == HostVulkan::Texture::Channel(v, surface.b_mask));
        assert(decoded[v * 4 + 3] == 255);
    }
    // All 256 channel intensities, with different red/green/blue values.
    for (unsigned v = 0; v < 65536; ++v)
    {
        decoded[v * 4] = v & 255;
        decoded[v * 4 + 1] = v >> 8;
        decoded[v * 4 + 2] = (v * 17) & 255;
    }
    if (green_bits == 6)
        HostVulkan::Texture::Encode16<6>(decoded.data(), surface);
    else
        HostVulkan::Texture::Encode16<5>(decoded.data(), surface);
    for (unsigned y = 0; y < 256; ++y)
    {
        for (unsigned x = 0; x < 256; ++x)
        {
            const unsigned v = y * 256 + x;
            const auto *p = decoded.data() + v * 4;
            const unsigned expected = (((unsigned(p[0]) * 31 + 127) / 255) << (5 + green_bits)) |
                                      (((unsigned(p[1]) * ((1u << green_bits) - 1) + 127) / 255) << 5) |
                                      ((unsigned(p[2]) * 31 + 127) / 255);
            assert(values[y * 257 + x] == expected);
        }
        assert(values[y * 257 + 256] == 0xa5a5);
    }
}
static void TestBatches(Gpu::Backend &gpu)
{
    std::vector<unsigned> pixels(64, 0), other(64, 0);
    std::vector<unsigned short> depths(64, 0xffff);
    Gpu::Surface color;
    color.bits = reinterpret_cast<unsigned char *>(pixels.data());
    color.width = color.height = 8;
    color.pitch = 32;
    color.bit_count = 32;
    color.r_mask = 0xff0000;
    color.g_mask = 0xff00;
    color.b_mask = 0xff;
    Gpu::Surface depth = color;
    depth.bits = reinterpret_cast<unsigned char *>(depths.data());
    depth.pitch = 16;
    depth.bit_count = depth.z_bits = 16;
    depth.depth = true;
    Gpu::State state;
    state.viewport_width = state.viewport_height = 8;
    assert(gpu.Begin(color, &depth));
    gpu.Clear(3, 0xff0000ff, .5f, 0, nullptr, 0, state);
    gpu.End();
    // Consecutive logical batches retain both attachments and pending work.
    for (int x = 0; x < 7; ++x)
    {
        assert(gpu.Begin(color, &depth));
        const int rect[] = {x, 0, x + 1, 1};
        gpu.Clear(1, 0xffff0000, 1, 0, rect, 1, state);
        gpu.End();
    }
    Gpu::Surface next = color;
    next.bits = reinterpret_cast<unsigned char *>(other.data());
    assert(gpu.Begin(next, &depth));
    gpu.Clear(1, 0xff00ff00, 1, 0, nullptr, 0, state);
    gpu.End();
    // Return to the resident front buffer without replacing its pixels.
    assert(gpu.Begin(color, &depth));
    const int bottom[] = {7, 7, 8, 8};
    gpu.Clear(1, 0xffffff00, 1, 0, bottom, 1, state);
    gpu.End();
    gpu.Sync(color, false);
    gpu.Sync(next, false);
    gpu.Sync(depth, false);
    for (unsigned x = 0; x < 7; ++x)
        assert(pixels[x] == 0xff0000);
    assert(pixels[7] == 0xff && pixels[63] == 0xffff00);
    assert(other[0] == 0xff00 && other[63] == 0xff00);
    assert(depths[0] == 0x8000 && depths[63] == 0x8000);
    // A CPU lock in between batches must upload the new bytes.
    gpu.Sync(next, true);
    other[0] = 0xffffff;
    ++next.generation;
    assert(gpu.Begin(next, &depth));
    const int rect[] = {7, 7, 8, 8};
    gpu.Clear(1, 0xffff0000, 1, 0, rect, 1, state);
    gpu.End();
    gpu.Sync(next, false);
    assert(other[0] == 0xffffff && other[63] == 0xff0000);
    gpu.Sync(depth, true);
    depths[0] = 0x1234;
    assert(gpu.Begin(next, &depth));
    gpu.Clear(2, 0, .25f, 0, rect, 1, state);
    gpu.End();
    gpu.Sync(depth, false);
    assert(depths[0] == 0x1234 && depths[63] == 0x4000);
    gpu.Sync(next, false);
    assert(other[0] == 0xffffff && other[63] == 0xff0000);
    gpu.Release(color.bits, pixels.size() * sizeof(unsigned));
    gpu.Release(next.bits, other.size() * sizeof(unsigned));
    gpu.Release(depth.bits, depths.size() * sizeof(unsigned short));
}
static void TestResidentReads(Gpu::Backend &gpu)
{
    std::vector<unsigned> memory[3];
    Gpu::Surface surfaces[3];
    Gpu::State state; state.viewport_width = state.viewport_height = 64;
    for (unsigned i = 0; i < 3; ++i)
    {
        memory[i].resize(64 * 64, 0xa5a5a5a5);
        auto &s = surfaces[i]; s.bits = reinterpret_cast<unsigned char *>(memory[i].data());
        s.width = s.height = 64; s.pitch = 256; s.bit_count = 32;
        s.r_mask = 0xff0000; s.g_mask = 0xff00; s.b_mask = 0xff; s.handle = 100 + i;
        assert(gpu.Begin(s, nullptr));
        gpu.Clear(1, 0xff000000 | (0x11u << (i * 8)), 1, 0, nullptr, 0, state);
        gpu.End();
    }
    // The third target evicts the first; the two resident targets stay lazy.
    assert(memory[0][0] == 0x11 && memory[1][0] == 0xa5a5a5a5);
    gpu.CpuAccess(surfaces[1].bits, false);
    assert(memory[1][0] == 0x1100);
    gpu.CpuAccess(surfaces[2].bits, true);
    assert(memory[2][0] == 0x110000);
    struct Vertex { float x,y,z,w; unsigned color; float u,v; };
    const Vertex triangle[] = {{0,0,0,1,0xffffffff,0,0}, {64,0,0,1,0xffffffff,1,0}, {0,64,0,1,0xffffffff,0,1}};
    Gpu::Draw draw; draw.primitive = Gpu::TriangleList; draw.fvf = 0x144;
    draw.stride = sizeof(Vertex); draw.vertices = reinterpret_cast<const unsigned char *>(triangle);
    draw.vertex_count = 3; draw.primitive_count = 1;
    state.stage[0][1] = 2; state.stage[0][2] = 2;
    for (auto color : {0xff00ff00u, 0xff0000ffu})
    {
        assert(gpu.Begin(surfaces[1], nullptr));
        gpu.Clear(1, color, 1, 0, nullptr, 0, state); gpu.End();
        assert(gpu.Begin(surfaces[2], nullptr));
        gpu.SetTexture(0, &surfaces[1]); gpu.Draw(draw, state); gpu.End();
        gpu.Sync(surfaces[2], false);
        assert(memory[2][8 * 64 + 8] == (color & 0xffffff));
    }
    for (auto &s : surfaces) gpu.Release(s.bits, 64 * 64 * 4);
}
static void TestPrimitiveRanges(Gpu::Backend &gpu)
{
    std::vector<unsigned> pixels(64 * 64);
    Gpu::Surface surface;
    surface.bits = reinterpret_cast<unsigned char *>(pixels.data());
    surface.width = surface.height = 64;
    surface.pitch = 256;
    surface.bit_count = 32;
    surface.r_mask = 0xff0000; surface.g_mask = 0xff00; surface.b_mask = 0xff;
    Gpu::State state;
    state.viewport_width = state.viewport_height = 64;
    state.render[22] = 1; // No culling.
    state.stage[0][1] = 2; state.stage[0][2] = 3; // Select texture factor.
    state.stage[1][1] = 1;
    struct Vertex { float x, y, z, rhw; unsigned color; };
    const Vertex left[] = {{4,4,0,1,0xffffffff}, {28,4,0,1,0xffffffff}, {4,28,0,1,0xffffffff}};
    const Vertex right[] = {{36,36,0,1,0xffffffff}, {60,36,0,1,0xffffffff}, {36,60,0,1,0xffffffff}};
    for (auto primitive : {Gpu::PointList, Gpu::LineList, Gpu::LineStrip,
                           Gpu::TriangleList, Gpu::TriangleStrip, Gpu::TriangleFan})
    {
        const unsigned n = primitive == Gpu::PointList ? 1 : primitive <= Gpu::LineStrip ? 2 : 3;
        std::vector<Vertex> vertices;
        vertices.insert(vertices.end(), right, right + n);
        vertices.insert(vertices.end(), left, left + n);
        vertices.insert(vertices.end(), right, right + n);
        Gpu::Draw draw;
        draw.primitive = primitive; draw.primitive_count = 1;
        draw.fvf = 0x44; draw.stride = sizeof(Vertex);
        draw.vertices = reinterpret_cast<const unsigned char *>(vertices.data());
        draw.vertex_count = vertices.size();
        assert(gpu.Begin(surface, nullptr));
        gpu.Clear(1, 0xff0000ff, 1, 0, nullptr, 0, state);
        draw.first_vertex = n * 2;
        state.render[60] = 0xff00ff00;
        gpu.Draw(draw, state);
        draw.first_vertex = n;
        state.render[60] = 0xffff0000;
        gpu.Draw(draw, state);
        gpu.End(); gpu.Sync(surface, false);
        unsigned red = 0, green = 0;
        for (unsigned y = 0; y < 64; ++y) for (unsigned x = 0; x < 64; ++x)
        {
            const auto color = pixels[y * 64 + x];
            if (color == 0xff0000) { assert(x < 32 && y < 32); ++red; }
            else if (color == 0xff00) { assert(x >= 32 && y >= 32); ++green; }
            else assert(color == 0xff);
        }
        assert(red && green);
        const auto expected = pixels;
        assert(gpu.Begin(surface, nullptr));
        draw.primitive_count = 0;
        gpu.Draw(draw, state);
        draw.primitive_count = 1;
        draw.first_vertex = draw.vertex_count;
        gpu.Draw(draw, state);
        draw.first_vertex = ~0u;
        gpu.Draw(draw, state);
        gpu.End(); gpu.Sync(surface, false);
        assert(pixels == expected);
        // Oversized primitive counts clamp to the buffer without wrapping.
        assert(gpu.Begin(surface, nullptr));
        draw.first_vertex = n * 2; draw.primitive_count = ~0u;
        state.render[60] = 0xff00ff00;
        gpu.Draw(draw, state);
        gpu.End(); gpu.Sync(surface, false);
        assert(pixels == expected);
    }
    gpu.Release(surface.bits, pixels.size() * sizeof(unsigned));
    puts("VULKAN PRIMITIVE RANGES PASS: six topologies, shared buffer, offsets, empty draws, bounds");
}
static void TestPackedClip(Gpu::Backend &gpu)
{
    struct Vertex { float position[4]; unsigned color, secondary; float uv[2][4]; };
    static_assert(sizeof(Vertex) == 56);
    const Vertex vertices[] = {
        {{-.8f,-.8f,.2f,1},0xff804020,0x80000000,{{0,0,0,1},{1,0,0,1}}},
        {{ .8f,-.8f,.2f,1},0xff208040,0x80000000,{{1,0,0,1},{0,0,0,1}}},
        {{  0, .4f,.1f,.5f},0xff402080,0x80000000,{{0,1,0,1},{1,1,0,1}}}
    };
    unsigned texels[] = {0xffff8040,0xff40ff80,0xff8040ff,0xffffffff};
    std::vector<unsigned> pixels(64*64);
    Gpu::Surface surface, texture;
    surface.bits = reinterpret_cast<unsigned char *>(pixels.data());
    surface.width = surface.height = 64; surface.pitch = 256; surface.bit_count = 32;
    surface.r_mask = 0xff0000; surface.g_mask = 0xff00; surface.b_mask = 0xff;
    texture = surface; texture.bits = reinterpret_cast<unsigned char *>(texels);
    texture.width = texture.height = 2; texture.pitch = 8; texture.a_mask = 0xff000000;
    Gpu::State state; state.viewport_width = state.viewport_height = 64;
    state.render[22] = 1; state.render[9] = 2;
    for (unsigned unit=0;unit<2;++unit) {
        auto *stage = state.stage[unit];
        stage[1]=stage[4]=4; stage[2]=stage[5]=2; stage[3]=stage[6]=unit?1:0;
        stage[11]=unit; stage[13]=stage[14]=1; stage[16]=stage[17]=1;
        stage[24]=0x104;
        for (unsigned i=0;i<16;++i) state.texture[unit][i]=i%5==0?1:0;
    }
    state.stage[2][1]=1;
    // Padded input forces the general conversion path for the same vertices.
    unsigned char padded[3][60] = {};
    for (unsigned i=0;i<3;++i) memcpy(padded[i],vertices+i,sizeof(Vertex));
    unsigned short indices[] = {0,1,2};
    for (bool indexed : {false,true}) for (unsigned units : {1u,2u}) {
        state.stage[1][1]=units==1?1:4;
        std::vector<unsigned> reference;
        for (bool direct : {false,true}) {
            assert(gpu.Begin(surface,nullptr)); gpu.Clear(1,0xff123456,1,0,nullptr,0,state);
            gpu.SetTexture(0,&texture); gpu.SetTexture(1,&texture);
            Gpu::Draw d; d.primitive=Gpu::TriangleList; d.primitive_count=1;
            d.fvf=0x4002|0x40|0x80|0x200|(2u<<16)|(2u<<18);
            d.stride=direct?56:60; d.vertices=direct?reinterpret_cast<const unsigned char *>(vertices):padded[0];
            d.vertex_count=3; d.indices=indexed?indices:nullptr; d.index_count=indexed?3:0;
            gpu.Draw(d,state); gpu.End(); gpu.Sync(surface,false);
            if (!direct) reference=pixels;
            else for (unsigned i=0;i<pixels.size();++i) for (unsigned shift : {0u,8u,16u}) {
                int difference=int((pixels[i]>>shift)&255)-int((reference[i]>>shift)&255);
                assert(difference>=-1&&difference<=1);
            }
        }
    }
    gpu.Release(surface.bits,pixels.size()*4); gpu.Release(texture.bits,sizeof(texels));
    puts("VULKAN PACKED CLIP PASS: general-path comparison, colours, perspective UVs, two textures, indices");
}
int main()
{
    Test16(5);
    Test16(6);
    auto *gpu = HostVulkan::Install();
    if (!gpu)
    {
        fprintf(stderr, "No Vulkan device is available for this test.\n");
        return 77;
    }
    TestBatches(*gpu);
    TestPackedClip(*gpu);
    TestResidentReads(*gpu);
    TestPrimitiveRanges(*gpu);
    std::vector<unsigned> pixels(64 * 64, 0x00ffffff);
    Gpu::Surface surface;
    surface.bits = reinterpret_cast<unsigned char *>(pixels.data());
    surface.width = surface.height = 64;
    surface.pitch = 64 * 4;
    surface.bit_count = 32;
    surface.r_mask = 0xff0000;
    surface.g_mask = 0xff00;
    surface.b_mask = 0xff;
    Gpu::State state;
    state.viewport_width = state.viewport_height = 64;
    assert(gpu->Begin(surface, nullptr));
    gpu->Clear(1, 0xff0000ff, 1, 0, nullptr, 0, state);
    struct Vertex
    {
        float x, y, z, rhw;
        unsigned color;
    };
    const Vertex triangle[] = {
        {4, 4, 0, 1, 0xffff0000}, {60, 4, 0, 1, 0xffff0000}, {4, 60, 0, 1, 0xffff0000}};
    Gpu::Draw draw;
    draw.primitive = Gpu::TriangleList;
    draw.fvf = 0x44;
    draw.stride = sizeof(Vertex);
    draw.vertices = reinterpret_cast<const unsigned char *>(triangle);
    draw.vertex_count = 3;
    draw.primitive_count = 1;
    gpu->Draw(draw, state);
    gpu->End();
    gpu->Sync(surface, false);
    assert(pixels[16 * 64 + 16] == 0x00ff0000);
    assert(pixels[62 * 64 + 62] == 0x000000ff);
    // A second batch starts with CPU writes and preserves pixels outside a clear.
    gpu->Sync(surface, true);
    pixels[0] = 0x0000ff00;
    assert(gpu->Begin(surface, nullptr));
    const int rect[] = {20, 20, 24, 24};
    gpu->Clear(1, 0xffffffff, 1, 0, rect, 1, state);
    gpu->End();
    gpu->Sync(surface, false);
    assert(pixels[0] == 0x0000ff00 && pixels[21 * 64 + 21] == 0x00ffffff);
    assert(pixels[16 * 64 + 16] == 0x00ff0000);
    std::vector<unsigned char> packed(8 * 20, 0xa5);
    surface.bits = packed.data();
    surface.width = surface.height = 8;
    surface.pitch = 20;
    surface.bit_count = 16;
    surface.r_mask = 0xf800;
    surface.g_mask = 0x7e0;
    surface.b_mask = 0x1f;
    assert(gpu->Begin(surface, nullptr));
    gpu->Clear(1, 0xff00ff00, 1, 0, nullptr, 0, state);
    gpu->End();
    gpu->Sync(surface, false);
    for (unsigned y = 0; y < 8; ++y)
    {
        for (unsigned x = 0; x < 8; ++x)
        {
            unsigned short value;
            memcpy(&value, packed.data() + y * 20 + x * 2, 2);
            assert(value == 0x7e0);
        }
        for (unsigned x = 16; x < 20; ++x)
            assert(packed[y * 20 + x] == 0xa5);
    }
    assert(gpu->DriverModel8() && gpu->DriverModel9() && gpu->Glide());
    Gpu::Surface depth;
    assert(!gpu->Begin(surface, &depth));
    surface.bits = reinterpret_cast<unsigned char *>(pixels.data());
    surface.width = surface.height = 64;
    surface.pitch = 256;
    surface.bit_count = 32;
    surface.r_mask = 0xff0000;
    surface.g_mask = 0xff00;
    surface.b_mask = 0xff;
    state = {};
    state.viewport_width = state.viewport_height = 64;
    state.render[22] = 1;
    auto render = [&](const Vertex *v) {
        draw.vertices = reinterpret_cast<const unsigned char *>(v);
        gpu->Draw(draw, state);
    };
    auto finish = [&] {
        gpu->End();
        gpu->Sync(surface, false);
    };
    auto near = [](unsigned a, unsigned b) { return a > b ? a - b <= 1 : b - a <= 1; };
    Vertex translucent[] = {{4, 4, 0, 1, 0x80ff0000}, {60, 4, 0, 1, 0x80ff0000}, {4, 60, 0, 1, 0x80ff0000}};
    assert(gpu->Begin(surface, nullptr));
    gpu->Clear(1, 0xff0000ff, 1, 0, nullptr, 0, state);
    state.render[27] = 1;
    state.render[19] = 5;
    state.render[20] = 6;
    render(translucent);
    finish();
    unsigned color = pixels[16 * 64 + 16];
    assert(near((color >> 16) & 255, 128) && near(color & 255, 127));
    state.render[27] = 0;
    state.render[15] = 1;
    state.render[24] = 192;
    state.render[25] = 5;
    assert(gpu->Begin(surface, nullptr));
    gpu->Clear(1, 0xff0000ff, 1, 0, nullptr, 0, state);
    render(translucent);
    finish();
    assert(pixels[16 * 64 + 16] == 0xff);
    state.render[15] = 0;
    std::vector<unsigned> depths(64 * 64, 0xffffffff);
    depth.bits = reinterpret_cast<unsigned char *>(depths.data());
    depth.width = depth.height = 64;
    depth.pitch = 256;
    depth.bit_count = 32;
    depth.z_bits = 24;
    depth.stencil_bits = 8;
    depth.depth = true;
    Vertex closer[] = {
        {4, 4, .25f, 1, 0xffff0000}, {60, 4, .25f, 1, 0xffff0000}, {4, 60, .25f, 1, 0xffff0000}};
    Vertex farther[] = {
        {4, 4, .75f, 1, 0xff00ff00}, {60, 4, .75f, 1, 0xff00ff00}, {4, 60, .75f, 1, 0xff00ff00}};
    state.render[7] = state.render[14] = 1;
    state.render[23] = 2;
    assert(gpu->Begin(surface, &depth));
    gpu->Clear(7, 0xff0000ff, 1, 0, nullptr, 0, state);
    render(closer);
    render(farther);
    finish();
    gpu->SyncDepth(depth);
    assert(pixels[16 * 64 + 16] == 0xff0000);
    assert(near(depths[16 * 64 + 16] & 0xffffff, 0x400000));
    state.render[7] = 0;
    state.render[52] = 1;
    state.render[56] = 8;
    state.render[55] = 3;
    state.render[53] = state.render[54] = 1;
    state.render[57] = 7;
    state.render[58] = state.render[59] = 255;
    assert(gpu->Begin(surface, &depth));
    gpu->Clear(7, 0xff0000ff, 1, 0, nullptr, 0, state);
    render(closer);
    state.render[56] = 3;
    state.render[57] = 8;
    render(farther);
    finish();
    gpu->SyncDepth(depth);
    assert(pixels[16 * 64 + 16] == 0xff0000 && depths[16 * 64 + 16] >> 24 == 7);
    state.render[52] = 0;
    struct Textured
    {
        float x, y, z, rhw;
        unsigned color;
        float u, v;
    };
    const Textured textured[] = {{4, 4, 0, 1, 0xffffffff, .5f, .5f},
                                 {60, 4, 0, 1, 0xffffffff, .5f, .5f},
                                 {4, 60, 0, 1, 0xffffffff, .5f, .5f}};
    std::vector<unsigned> texels(4, 0xffff0000);
    Gpu::Surface texture = surface;
    texture.bits = reinterpret_cast<unsigned char *>(texels.data());
    texture.width = texture.height = 2;
    texture.pitch = 8;
    texture.a_mask = 0xff000000;
    texture.owner = 1;
    texture.handle = 3;
    texture.generation = 1;
    state.stage[0][1] = 4;
    state.stage[0][2] = 2;
    state.stage[0][3] = 0;
    state.stage[0][4] = 2;
    state.stage[0][5] = 2;
    state.stage[1][1] = 1;
    draw.fvf = 0x144;
    draw.stride = sizeof(Textured);
    draw.vertices = reinterpret_cast<const unsigned char *>(textured);
    assert(gpu->Begin(surface, nullptr));
    gpu->SetTexture(0, &texture);
    gpu->Draw(draw, state);
    finish();
    assert(pixels[16 * 64 + 16] == 0xff0000);
    for (auto &p : texels)
        p = 0xff00ff00;
    ++texture.generation;
    assert(gpu->Begin(surface, nullptr));
    gpu->SetTexture(0, &texture);
    gpu->Draw(draw, state);
    finish();
    assert(pixels[16 * 64 + 16] == 0xff00);
    state.stage[1][1] = 7;
    state.stage[1][2] = 1;
    state.stage[1][3] = 3;
    state.stage[1][4] = 1;
    state.render[60] = 0xff000040;
    assert(gpu->Begin(surface, nullptr));
    gpu->SetTexture(0, &texture);
    gpu->Draw(draw, state);
    finish();
    assert(pixels[16 * 64 + 16] == 0xff40);
    unsigned char dxt[] = {0, 0xf8, 0x1f, 0, 0, 0, 0, 0};
    texture.bits = dxt;
    texture.width = texture.height = 4;
    texture.pitch = 8;
    texture.fourcc = 0x31545844;
    texture.bit_count = 0;
    texture.handle = 4;
    ++texture.generation;
    state.stage[1][1] = 1;
    assert(gpu->Begin(surface, nullptr));
    gpu->SetTexture(0, &texture);
    gpu->Draw(draw, state);
    finish();
    assert(pixels[16 * 64 + 16] == 0xff0000);
    state.stage[0][1] = 1;
    state.render[28] = 1;
    state.render[35] = 3;
    state.render[34] = 0xff0000ff;
    float start = 0, end = 1;
    memcpy(&state.render[36], &start, 4);
    memcpy(&state.render[37], &end, 4);
    draw.fvf = 0x44;
    draw.stride = sizeof(Vertex);
    assert(gpu->Begin(surface, nullptr));
    render(closer);
    finish();
    color = pixels[16 * 64 + 16];
    assert(near((color >> 16) & 255, 191) && near(color & 255, 64));
    state.render[28] = 0;
    struct Model
    {
        float x, y, z, nx, ny, nz;
        unsigned color;
    };
    const Model model[] = {{-.8f, .8f, .5f, 0, 0, -1, 0xffffffff},
                           {.8f, .8f, .5f, 0, 0, -1, 0xffffffff},
                           {-.8f, -.8f, .5f, 0, 0, -1, 0xffffffff}};
    for (unsigned i = 0; i < 4; ++i)
        state.world[0][i * 5] = state.view[i * 5] = state.projection[i * 5] = 1;
    state.render[137] = state.render[143] = 1;
    state.material.diffuse[0] = state.material.diffuse[3] = 1;
    state.lights[0].enabled = true;
    state.lights[0].type = 3;
    state.lights[0].direction[2] = 1;
    for (unsigned i = 0; i < 4; ++i)
        state.lights[0].diffuse[i] = 1;
    draw.fvf = 0x52;
    draw.stride = sizeof(Model);
    draw.vertices = reinterpret_cast<const unsigned char *>(model);
    assert(gpu->Begin(surface, nullptr));
    gpu->Clear(1, 0xff0000ff, 1, 0, nullptr, 0, state);
    gpu->Draw(draw, state);
    finish();
    assert(pixels[16 * 64 + 16] == 0xff0000);
    // The indexed path must use the base vertex and reject an out-of-bounds index.
    state = {};
    state.viewport_width = state.viewport_height = 64;
    state.render[22] = 1;
    const Vertex indexed_vertices[] = {{0, 0, 0, 1, 0xff00ff00}, triangle[0], triangle[1], triangle[2]};
    unsigned short indices[] = {0, 1, 2};
    draw = {};
    draw.primitive = Gpu::TriangleList;
    draw.fvf = 0x44;
    draw.stride = sizeof(Vertex);
    draw.vertices = reinterpret_cast<const unsigned char *>(indexed_vertices);
    draw.vertex_count = 4;
    draw.primitive_count = 1;
    draw.indices = indices;
    draw.index_count = 3;
    draw.base_vertex = 1;
    assert(gpu->Begin(surface, nullptr));
    gpu->Clear(1, 0xff0000ff, 1, 0, nullptr, 0, state);
    gpu->Draw(draw, state);
    finish();
    assert(pixels[16 * 64 + 16] == 0xff0000);
    draw.base_vertex = 0xffffffff;
    assert(gpu->Begin(surface, nullptr));
    gpu->Clear(1, 0xff0000ff, 1, 0, nullptr, 0, state);
    gpu->Draw(draw, state);
    finish();
    assert(pixels[16 * 64 + 16] == 0xff);
    draw.base_vertex = 1;
    state.render[9] = 1;
    Vertex varied[] = {indexed_vertices[0], triangle[0], triangle[1], triangle[2]};
    varied[2].color = 0xff00ff00;
    varied[3].color = 0xff0000ff;
    draw.vertices = reinterpret_cast<const unsigned char *>(varied);
    assert(gpu->Begin(surface, nullptr));
    gpu->Draw(draw, state);
    finish();
    assert(pixels[16 * 64 + 16] == 0xff0000);
    state.render[9] = 2;
    state.render[7] = 2;
    state.render[14] = 1;
    state.render[23] = 2;
    state.w_near = 1;
    state.w_far = 5;
    Vertex wn[] = {
        {4, 4, .8f, .5f, 0xffff0000}, {60, 4, .8f, .5f, 0xffff0000}, {4, 60, .8f, .5f, 0xffff0000}};
    Vertex wf[] = {
        {4, 4, .1f, .25f, 0xff00ff00}, {60, 4, .1f, .25f, 0xff00ff00}, {4, 60, .1f, .25f, 0xff00ff00}};
    draw.indices = nullptr;
    draw.vertex_count = 3;
    draw.first_vertex = 0;
    assert(gpu->Begin(surface, &depth));
    gpu->Clear(7, 0xff0000ff, 1, 0, nullptr, 0, state);
    render(wn);
    render(wf);
    finish();
    gpu->SyncDepth(depth);
    assert(pixels[16 * 64 + 16] == 0xff0000);
    assert(near(depths[16 * 64 + 16] & 0xffffff, 0x400000));
    state = {};
    state.viewport_width = state.viewport_height = 64;
    state.render[22] = 1;
    if (gpu->PixelShaders() && gpu->VertexShaders())
    {
        const unsigned ps[] = {0xffff0101, 1, 0x800f0000, 0xa0e40000, 0xffff}; // mov r0,c0
        state.pixel_shader = {ps, 5, 1};
        state.ps_constants[0][2] = state.ps_constants[0][3] = 1;
        assert(gpu->Begin(surface, nullptr));
        render(triangle);
        finish();
        assert(pixels[16 * 64 + 16] == 0xff);
        state.ps_constants[0][2] = 0;
        state.ps_constants[0][1] = 1;
        assert(gpu->Begin(surface, nullptr));
        render(triangle);
        finish();
        assert(pixels[16 * 64 + 16] == 0xff00);
        const unsigned vs[] = {0xfffe0101, 1, 0xc00f0000, 0x90e40000, 1, 0xd00f0000, 0x90e40001, 0xffff};
        struct Inputs
        {
            float position[4], color[4];
        };
        const Inputs inputs[] = {{{-.8f, .8f, .5f, 1}, {1, 0, 0, 1}},
                                 {{.8f, .8f, .5f, 1}, {1, 0, 0, 1}},
                                 {{-.8f, -.8f, .5f, 1}, {1, 0, 0, 1}}};
        state.pixel_shader = {};
        state.vertex_shader = {vs, 8, 2};
        draw.shader_inputs = true;
        draw.stride = sizeof(Inputs);
        draw.vertices = reinterpret_cast<const unsigned char *>(inputs);
        assert(gpu->Begin(surface, nullptr));
        gpu->Clear(1, 0xff0000ff, 1, 0, nullptr, 0, state);
        gpu->Draw(draw, state);
        finish();
        assert(pixels[16 * 64 + 16] == 0xff0000);
        const unsigned ps2[] = {0xffff0200, 0x02000001, 0x800f0800, 0xa0e40000, 0xffff}; // mov oC0,c0
        state.pixel_shader = {ps2, 5, 3};
        assert(gpu->Begin(surface, nullptr));
        gpu->Draw(draw, state);
        finish();
        assert(pixels[16 * 64 + 16] == 0xff00);
        state.pixel_shader = {};
        state.vertex_shader = {};
        draw.shader_inputs = false;
        draw.stride = sizeof(Vertex);
        draw.fvf = 0x44;
    }
    // More distinct descriptor sets than one pool holds must preserve the target.
    state.stage[0][1] = 2;
    state.stage[0][2] = 3;
    state.stage[0][4] = 1;
    assert(gpu->Begin(surface, nullptr));
    for (unsigned i = 0; i < 1030; ++i)
    {
        state.render[60] = 0xff010000 | i;
        render(triangle);
    }
    finish();
    assert(pixels[16 * 64 + 16] == 0x010405);
    state.stage[0][1] = 1;
    Gpu::GlideState glide;
    glide.clip[2] = glide.clip[3] = 64;
    Gpu::GlideVertex gv[3];
    for (unsigned i = 0; i < 3; ++i)
    {
        gv[i].x = triangle[i].x;
        gv[i].y = triangle[i].y;
        gv[i].r = gv[i].a = 255;
        gv[i].ooz = 16384;
        gv[i].oow = .5f;
        gv[i].s0 = gv[i].t0 = 128;
        gv[i].q0 = 1;
    }
    assert(gpu->Begin(surface, &depth));
    gpu->Clear(7, 0xff0000ff, 1, 0, nullptr, 0, state);
    glide.depth_mode = 1;
    glide.depth_mask = true;
    gpu->DrawGlide(Gpu::TriangleList, gv, 3, glide);
    finish();
    gpu->SyncDepth(depth);
    assert(pixels[16 * 64 + 16] == 0xff0000);
    assert(near(depths[16 * 64 + 16] & 0xffffff, 0x400040));
    glide.depth_mode = 2;
    assert(gpu->Begin(surface, &depth));
    gpu->Clear(7, 0xff0000ff, 1, 0, nullptr, 0, state);
    gpu->DrawGlide(Gpu::TriangleList, gv, 3, glide);
    finish();
    gpu->SyncDepth(depth);
    assert(near(depths[16 * 64 + 16] & 0xffffff, 0x100010));
    // Glide texels, chroma rejection, alpha test, fog table, blend and color masks.
    texture.fourcc = 0;
    texture.bit_count = 32;
    texture.bits = reinterpret_cast<unsigned char *>(texels.data());
    texture.width = texture.height = 2;
    texture.pitch = 8;
    ++texture.generation;
    glide.depth_mode = 0;
    glide.tmu[0].textured = true;
    glide.tmu[0].width = glide.tmu[0].height = 2;
    glide.color_function = 3;
    glide.color_factor = 8;
    glide.color_other = 1;
    assert(gpu->Begin(surface, nullptr));
    gpu->SetTexture(0, &texture);
    gpu->DrawGlide(Gpu::TriangleList, gv, 3, glide);
    finish();
    assert(pixels[16 * 64 + 16] == 0xff00);
    glide.chroma_key = true;
    glide.chroma_value = 0xff00;
    assert(gpu->Begin(surface, nullptr));
    gpu->Clear(1, 0xff0000ff, 1, 0, nullptr, 0, state);
    gpu->SetTexture(0, &texture);
    gpu->DrawGlide(Gpu::TriangleList, gv, 3, glide);
    finish();
    assert(pixels[16 * 64 + 16] == 0xff);
    glide.chroma_key = false;
    glide.color_function = 1;
    glide.alpha_test = 4;
    glide.alpha_reference = 255;
    assert(gpu->Begin(surface, nullptr));
    gpu->DrawGlide(Gpu::TriangleList, gv, 3, glide);
    finish();
    assert(pixels[16 * 64 + 16] == 0xff);
    glide.alpha_test = 7;
    glide.fog_mode = 2;
    glide.fog_color = 0xff;
    for (auto &f : glide.fog_table)
        f = 128;
    assert(gpu->Begin(surface, nullptr));
    gpu->DrawGlide(Gpu::TriangleList, gv, 3, glide);
    finish();
    color = pixels[16 * 64 + 16];
    assert(near((color >> 16) & 255, 127) && near(color & 255, 128));
    glide.fog_mode = 0;
    glide.rgb_source = 1;
    glide.rgb_destination = 5;
    for (auto &v : gv)
        v.a = 128;
    assert(gpu->Begin(surface, nullptr));
    gpu->Clear(1, 0xff0000ff, 1, 0, nullptr, 0, state);
    gpu->DrawGlide(Gpu::TriangleList, gv, 3, glide);
    finish();
    color = pixels[16 * 64 + 16];
    assert(near((color >> 16) & 255, 128) && near(color & 255, 127));
    glide.rgb_mask = false;
    assert(gpu->Begin(surface, nullptr));
    gpu->Clear(1, 0xff0000ff, 1, 0, nullptr, 0, state);
    gpu->DrawGlide(Gpu::TriangleList, gv, 3, glide);
    finish();
    assert(pixels[16 * 64 + 16] == 0xff);
    printf("VULKAN PASS: %s, triangle, clears, CPU writes, RGB565, blend, alpha, depth, stencil, textures, "
           "generations, multitexture, DXT1, fog, lighting, indices, Glide "
           "combine/texture/chroma/alpha/Z/W/fog/blend/masks\n",
           gpu->Name());
}
