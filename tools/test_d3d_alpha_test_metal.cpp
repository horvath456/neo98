// SPDX-License-Identifier: GPL-2.0-or-later
// alpha-test discard and every packed-color boundary.
#include "host_metal.h"
#include "../src/gpu_backend.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

static bool Pass(unsigned function, unsigned alpha, unsigned reference)
{
    switch (function)
    {
    case 1: return false;
    case 2: return alpha < reference;
    case 3: return alpha == reference;
    case 4: return alpha <= reference;
    case 5: return alpha > reference;
    case 6: return alpha != reference;
    case 7: return alpha >= reference;
    default: return true;
    }
}

int main(int argc, char **argv)
{
    const bool packed_sweep = argc == 2 && !strcmp(argv[1], "--packed-sweep");
    auto *gpu = HostMetal::Install();
    assert(gpu);
    constexpr unsigned side = 128, clear = 0xff182030, rgb = 0x4080bf;
    std::vector<unsigned> pixels(side * side);
    Gpu::Surface target;
    target.bits = (unsigned char *)pixels.data();
    target.width = target.height = side;
    target.pitch = side * 4;
    target.bit_count = 32;
    target.r_mask = 0xff0000; target.g_mask = 0xff00;
    target.b_mask = 0xff; target.a_mask = 0xff000000;
    target.owner = 1; target.handle = 1;
    unsigned texel = 0xffffffff;
    auto white = target;
    white.bits = (unsigned char *)&texel;
    white.width = white.height = 1; white.pitch = 4; white.handle = 2;

    // VS 1.1: position = v0, texcoord = v1, diffuse = c0.
    const unsigned vs[] = {0xfffe0101, 1,0xc00f0000,0x90e40000,
                          1,0xe00f0000,0x90e40001, 1,0xd00f0000,0xa0e40000, 0xffff};
    // PS 1.1: r0 = c0. PS 2.0: oC0 = c31.
    const unsigned ps1[] = {0xffff0101, 1,0x800f0000,0xa0e40000, 0xffff};
    const unsigned ps2[] = {0xffff0200, 0x02000001,0x800f0800,0xa0e4001f, 0xffff};
    const float inputs[4][8] = {
        {-1.25f,1.25f,.5f,1, 0,0,0,1}, {1.25f,1.25f,.5f,1, 1,0,0,1},
        {-1.25f,-1.25f,.5f,1, 0,1,0,1}, {1.25f,-1.25f,.5f,1, 1,1,0,1}};
    struct Vertex { float x,y,z,w; unsigned color; float u,v; };
    Vertex vertices[] = {
        {-.5f,-.5f,.5f,1,0,0,0}, {127.5f,-.5f,.5f,1,0,1,0},
        {-.5f,127.5f,.5f,1,0,0,1}, {127.5f,127.5f,.5f,1,0,1,1}};
    unsigned failures = 0, cases = 0;
    for (unsigned reference = packed_sweep ? 0 : 127; reference < (packed_sweep ? 256u : 128u); ++reference)
    {
        const std::vector<unsigned> alphas = packed_sweep
            ? std::vector<unsigned>{reference ? reference - 1 : 0, reference, reference < 255 ? reference + 1 : 255}
            : std::vector<unsigned>{0, 64, 127, 128, 255};
        for (unsigned vertex = 0; vertex < (packed_sweep ? 1u : 2u); ++vertex)
        for (unsigned pixel = 0; pixel < (packed_sweep ? 1u : 3u); ++pixel)
        for (unsigned alpha : alphas)
        for (unsigned enabled = 0; enabled < 2; ++enabled)
        for (unsigned function = 1; function <= 8; ++function)
        {
            Gpu::State state;
            state.viewport_width = state.viewport_height = side;
            state.render[22] = 1; // CULL_NONE
            state.render[15] = enabled;
            state.render[24] = reference;
            state.render[25] = function;
            state.stage[0][1] = state.stage[0][4] = 2; // SELECTARG1
            state.stage[0][2] = state.stage[0][5] = 0; // DIFFUSE
            state.stage[1][1] = 1; // DISABLE
            if (vertex) state.vertex_shader = {vs, sizeof vs / 4, 1};
            if (pixel == 1) state.pixel_shader = {ps1, sizeof ps1 / 4, 2};
            if (pixel == 2) state.pixel_shader = {ps2, sizeof ps2 / 4, 3};
            const float color[] = {64/255.0f, 128/255.0f, 191/255.0f, alpha/255.0f};
            for (unsigned component = 0; component < 4; ++component)
            {
                state.vs_constants[0][component] = color[component];
                state.ps_constants[0][component] = color[component];
                state.ps_constants[31][component] = color[component];
            }
            for (auto &v : vertices) v.color = (alpha << 24) | rgb;
            Gpu::Draw draw;
            draw.primitive = Gpu::TriangleStrip; draw.fvf = 0x144;
            draw.stride = vertex ? sizeof inputs[0] : sizeof(Vertex);
            draw.vertices = vertex ? (const unsigned char *)inputs : (const unsigned char *)vertices;
            draw.vertex_count = 4; draw.primitive_count = 2; draw.shader_inputs = vertex;
            assert(gpu->Begin(target, nullptr));
            gpu->Clear(1, clear, 1, 0, nullptr, 0, state);
            for (unsigned s = 0; s < 8; ++s) gpu->SetTexture(s, &white);
            gpu->Draw(draw, state); gpu->End(); gpu->Sync(target, false);

            const unsigned expected = !enabled || Pass(function, alpha, reference) ? (alpha << 24) | rgb : clear;
            unsigned wrong = 0;
            // Stay inside both the fixed-function and programmable quads.
            for (unsigned y = 8; y < 120; ++y)
            for (unsigned x = 8; x < 120; ++x) wrong += pixels[y * side + x] != expected;
            if (wrong)
            {
                ++failures;
                printf("FAIL reference=%u vertex=%u pixel=%u alpha=%u enabled=%u function=%u wrong=%u expected=%08x center=%08x\n",
                       reference, vertex, pixel, alpha, enabled, function, wrong, expected, pixels[64 * side + 64]);
            }
            ++cases;
        }
    }
    gpu->Release(target.bits, pixels.size() * 4); gpu->Release(white.bits, 4);
    printf("ALPHA TEST cases=%u pixels_per_case=12544 failures=%u\n", cases, failures);
    return failures ? 1 : 0;
}
