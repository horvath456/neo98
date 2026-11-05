// SPDX-License-Identifier: GPL-2.0-or-later
// The pixel pipeline of a 3dfx Voodoo in Metal, for Backend::DrawGlide
// (src/gpu_backend.h). Included by tools/host_metal.mm only.
// The card iterates every value linearly across the screen, colour, 1/z, 1/w
// and s/w, t/w of each TMU alike, and a texture coordinate is divided by its
// 1/w per pixel. Every varying is therefore center_no_perspective. The pixel
// then runs the units of the card in their order: TMU 1, TMU 0 fed by TMU 1,
// the chroma key on the colour "other" input, the colour and alpha combine
// units of the FBI, the alpha test, fog, and the depth of the pixel from Z or
// from the 16-bit floating point W.
#pragma once

namespace HostMetalGlide
{
const char *const ShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct GlideVertex
{
    float x, y, ooz, oow, r, g, b, a, s0, t0, q0, s1, t1, q1;
};

struct GlideVertexUniforms
{
    float2 size;     // the target in pixels
    float scale;     // target pixels per screen pixel
    float pad;
};

struct GlideOut
{
    float4 position [[position]];
    float4 color [[center_no_perspective]];
    float ooz [[center_no_perspective]];
    float oow [[center_no_perspective]];
    float3 tex0 [[center_no_perspective]];
    float3 tex1 [[center_no_perspective]];
    float point_size [[point_size]];
};

// The card samples a screen pixel at its middle, x + 0.5, as Metal does, so
// a Glide coordinate is a target coordinate times the scale. Measured against
// Direct3D with the menu of Unreal Tournament: with the sample on integer
// coordinates every tile of text lay one pixel right of and below the
// Direct3D picture (tools/test_glide_ut.py).
vertex GlideOut glide_vertex(uint vid [[vertex_id]], device const GlideVertex *v [[buffer(0)]],
                             constant GlideVertexUniforms &u [[buffer(1)]])
{
    const GlideVertex g = v[vid];
    GlideOut o;
    const float px = g.x * u.scale, py = g.y * u.scale;
    o.position = float4(px / u.size.x * 2.0 - 1.0, 1.0 - py / u.size.y * 2.0, 0.5, 1.0);
    o.color = float4(g.r, g.g, g.b, g.a);
    o.ooz = g.ooz;
    o.oow = g.oow;
    o.tex0 = float3(g.s0, g.t0, g.q0);
    o.tex1 = float3(g.s1, g.t1, g.q1);
    o.point_size = u.scale;
    return o;
}

struct GlideFragmentUniforms
{
    uint4 color;          // function, factor, local, other
    uint4 alpha;          // function, factor, local, other
    uint4 tmu_rgb[2];     // function, factor, invert, textured
    uint4 tmu_alpha[2];   // function, factor, invert, mipmapped
    float4 tex_scale[2];  // s and t to normalised coordinates, LOD bias, log2 of the target scale
    float4 detail[2];     // bias, scale, maximum, unused
    float4 constant_color;   // color0, the local input
    float4 constant_other;   // color1, the other input
    float4 fog_color;
    uint4 flags;          // colour invert, alpha invert, chroma key (bit 0) and alpha mask (bit 1), chroma value
    uint4 tests;          // alpha function, alpha reference, fog mode, depth mode
    int4 depth;           // bias
    float4 fog_table[16];
};

struct GlideDepthOut
{
    float4 color [[color(0)]];
    float depth [[depth(any)]];
};

// GR_COMBINE_FUNCTION_: what the unit makes of its factor, its local and
// other value and the local alpha.
static float3 glide_function(uint f, float3 factor, float3 local, float3 other, float local_alpha)
{
    switch (f)
    {
    case 0x1: return local;
    case 0x2: return float3(local_alpha);
    case 0x3: return factor * other;
    case 0x4: return factor * other + local;
    case 0x5: return factor * other + local_alpha;
    case 0x6: return factor * (other - local);
    case 0x7: return factor * (other - local) + local;
    case 0x8: return factor * (other - local) + local_alpha;
    case 0x9: return -factor * local + local;
    case 0x10: return -factor * local + local_alpha;
    default: return float3(0.0);
    }
}

// GR_COMBINE_FACTOR_: the low three bits pick the value, bit 3 takes one
// minus it. Value 5 is the texture colour in the FBI and the fraction of the
// level of detail in a TMU; value 4 is the texture alpha in the FBI and the
// detail factor in a TMU.
static float3 glide_factor(uint f, float3 local, float other_alpha, float local_alpha, float3 value4, float3 value5)
{
    float3 v;
    switch (f & 7)
    {
    case 1: v = local; break;
    case 2: v = float3(other_alpha); break;
    case 3: v = float3(local_alpha); break;
    case 4: v = value4; break;
    case 5: v = value5; break;
    default: v = float3(0.0); break;
    }
    return (f & 8) ? 1.0 - v : v;
}

static bool glide_compare(uint function, float a, float b)
{
    switch (function)
    {
    case 1: return a < b;
    case 2: return a == b;
    case 3: return a <= b;
    case 4: return a > b;
    case 5: return a != b;
    case 6: return a >= b;
    case 7: return true;
    default: return false;
    }
}

// One TMU: its texel as local, the TMU above it as other.
static float4 glide_tmu(constant GlideFragmentUniforms &u, uint i, float3 tex, float4 upstream,
                        texture2d<float> t, sampler s)
{
    float4 texel = float4(0.0);
    float lod = 0.0;
    if (u.tmu_rgb[i].w != 0 && tex.z != 0.0)
    {
        const float2 uv = tex.xy / tex.z * u.tex_scale[i].xy;
        if (u.tmu_alpha[i].w != 0)
        {
            // The card chose the level at the program's resolution; the
            // target has 2^w times as many pixels in each direction.
            const float lod_bias = u.tex_scale[i].z + u.tex_scale[i].w;
            lod = t.calculate_unclamped_lod(s, uv) + lod_bias;
            texel = t.sample(s, uv, bias(lod_bias));
        }
        else texel = t.sample(s, uv, level(0.0));
    }
    const float detail = clamp((u.detail[i].x - lod * 4.0) * exp2(u.detail[i].y) / 255.0, 0.0, u.detail[i].z);
    const float fraction = fract(lod);
    const float3 rgb_factor = glide_factor(u.tmu_rgb[i].y, texel.rgb, upstream.a, texel.a, float3(detail), float3(fraction));
    const float alpha_factor = glide_factor(u.tmu_alpha[i].y, float3(texel.a), upstream.a, texel.a, float3(detail), float3(fraction)).x;
    float3 rgb = glide_function(u.tmu_rgb[i].x, rgb_factor, texel.rgb, upstream.rgb, texel.a);
    float alpha = glide_function(u.tmu_alpha[i].x, float3(alpha_factor), float3(texel.a), float3(upstream.a), texel.a).x;
    rgb = saturate(rgb);
    alpha = saturate(alpha);
    if (u.tmu_rgb[i].z & 1) rgb = 1.0 - rgb;
    if (u.tmu_rgb[i].z & 2) alpha = 1.0 - alpha;
    return float4(rgb, alpha);
}

// The W buffer value of a pixel: a 4.12 floating point number of 1/w, so
// that 1/w = 2^-e * (1 - m/8192) with m the twelve bit mantissa. It grows
// with w; 0xFFFF is the farthest.
static float glide_wdepth(float oow)
{
    if (oow >= 1.0) return 0.0;
    if (oow <= 0.0) return 65535.0;
    const float e = floor(-log2(oow));
    if (e >= 16.0) return 65535.0;
    const float mantissa = 1.0 - oow * exp2(e);   // 0 to 0.5
    return min(65535.0, e * 4096.0 + floor(mantissa * 8192.0));
}

fragment GlideDepthOut glide_fragment(GlideOut in [[stage_in]], constant GlideFragmentUniforms &u [[buffer(0)]],
                                      texture2d<float> t0 [[texture(0)]], texture2d<float> t1 [[texture(1)]],
                                      sampler s0 [[sampler(0)]], sampler s1 [[sampler(1)]])
{
    const float4 iterated = saturate(in.color / 255.0);
    const float z = clamp(in.ooz, 0.0, 65535.0);
    // TMU 1 has nothing above it; TMU 0 takes its output as other.
    const float4 upstream = glide_tmu(u, 1, in.tex1, float4(0.0), t1, s1);
    const float4 texture = glide_tmu(u, 0, in.tex0, upstream, t0, s0);
    // GR_COMBINE_LOCAL_ and _OTHER_ of both FBI units.
    // Local 2 is the override of fbzColorPath: the top bit of the texture
    // alpha picks the constant.
    const float3 color_local = u.color.z == 1 ? u.constant_color.rgb
                             : u.color.z == 2 ? (texture.a >= 0.5 ? u.constant_color.rgb : iterated.rgb) : iterated.rgb;
    const float3 color_other = u.color.w == 1 ? texture.rgb : u.color.w == 2 ? u.constant_other.rgb : iterated.rgb;
    const float alpha_local = u.alpha.z == 1 ? u.constant_color.a : u.alpha.z == 2 ? floor(z / 256.0) / 255.0 : iterated.a;
    const float alpha_other = u.alpha.w == 1 ? texture.a : u.alpha.w == 2 ? u.constant_other.a : iterated.a;
    // The alpha mask drops a pixel whose alpha other input has a clear low bit.
    if ((u.flags.z & 2) != 0 && (uint(round(alpha_other * 255.0)) & 1) == 0) discard_fragment();
    // The chroma key compares the colour other input, in eight bits.
    if ((u.flags.z & 1) != 0)
    {
        const uint3 c = uint3(round(color_other * 255.0));
        const uint key = u.flags.w;
        if (c.r == ((key >> 16) & 255) && c.g == ((key >> 8) & 255) && c.b == (key & 255)) discard_fragment();
    }
    const float3 color_factor = glide_factor(u.color.y, color_local, alpha_other, alpha_local, float3(texture.a), texture.rgb);
    float3 rgb = saturate(glide_function(u.color.x, color_factor, color_local, color_other, alpha_local));
    const float alpha_factor = glide_factor(u.alpha.y, float3(alpha_local), alpha_other, alpha_local, float3(texture.a), float3(texture.a)).x;
    float alpha = saturate(glide_function(u.alpha.x, float3(alpha_factor), float3(alpha_local), float3(alpha_other), alpha_local).x);
    if (u.flags.x != 0) rgb = 1.0 - rgb;
    if (u.flags.y != 0) alpha = 1.0 - alpha;
    // The alpha test, in eight bits against the reference.
    if (!glide_compare(u.tests.x, round(alpha * 255.0), float(u.tests.y))) discard_fragment();
    const float wdepth = glide_wdepth(in.oow);
    // Fog: GR_FOG_WITH_ITERATED_ALPHA 1, _WITH_TABLE 2, _WITH_ITERATED_Z 3;
    // GR_FOG_MULT2 leaves the colour out, GR_FOG_ADD2 the fog colour.
    const uint fog = u.tests.z;
    if ((fog & 0xff) == 4)
    {
        // The constant fog of fogMode: the fog colour itself.
        rgb = (fog & 0x100) ? u.fog_color.rgb : rgb + u.fog_color.rgb;
    }
    else if ((fog & 0xff) != 0)
    {
        float f;
        if ((fog & 0xff) == 1) f = iterated.a;
        else if ((fog & 0xff) == 3) f = floor(z / 256.0) / 255.0;
        else
        {
            const uint index = min(63u, uint(wdepth) >> 10);
            const uint next = min(63u, index + 1);
            const float a = u.fog_table[index >> 2][index & 3], b = u.fog_table[next >> 2][next & 3];
            f = mix(a, b, float(uint(wdepth) & 1023) / 1024.0) / 255.0;
        }
        const float3 add = (fog & 0x200) ? float3(0.0) : u.fog_color.rgb;
        rgb = (fog & 0x100) ? f * add : rgb + f * (add - rgb);
    }
    GlideDepthOut out;
    out.color = float4(saturate(rgb), alpha);
    // GR_DEPTHBUFFER_ZBUFFER 1, _WBUFFER 2, and the two compare-to-bias modes.
    const uint mode = u.tests.w;
    const float value = (mode == 2 || mode == 4) ? wdepth : z;
    out.depth = clamp(value + float(u.depth.x), 0.0, 65535.0) / 65535.0;
    return out;
}
)MSL";

// The uniforms above, laid out like their MSL twins.
struct VertexUniforms
{
    float size[2];
    float scale;
    float pad;
};
struct FragmentUniforms
{
    unsigned color[4];
    unsigned alpha[4];
    unsigned tmu_rgb[2][4];
    unsigned tmu_alpha[2][4];
    float tex_scale[2][4];
    float detail[2][4];
    float constant_color[4];
    float constant_other[4];
    float fog_color[4];
    unsigned flags[4];
    unsigned tests[4];
    int depth[4];
    float fog_table[64];
};
static_assert(sizeof(Gpu::GlideVertex) == 14 * sizeof(float), "the MSL GlideVertex has fourteen floats");
} // namespace HostMetalGlide
