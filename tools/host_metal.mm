// SPDX-License-Identifier: GPL-2.0-or-later
// The Metal GPU backend (src/gpu_backend.h): the rasterizer behind the Direct3D
// HAL and the OpenGL driver of the NEO98 display driver.
// The HAL (src/x86_ddhal.cpp) hands this file the DrawPrimitives2 batches of
// the original D3DIM700.DLL: screen-space vertices with diffuse, specular and
// texture coordinates, the render states and texture stage states they run
// under, and the DirectDraw surfaces they draw into and sample from. Every
// surface is guest-visible memory. A render target is a BGRA texture that
// stays on the GPU between batches; Begin uploads its bytes only when the CPU
// wrote them. Consecutive batches share one render encoder; End closes the
// logical batch, Flush submits it, and Sync converts the texture back into
// the bytes when the driver says the CPU is about to look: a Lock, a blit, a flip. A texture is
// converted and uploaded when its generation changed since the last upload.
// The pipeline is the DirectX 7 fixed function pipeline without transform and
// lighting: the vertex shader maps D3D screen space (pixel centres on integer
// coordinates, y down) onto Metal's clip space and keeps 1/w for perspective
// correct interpolation; the fragment shader runs two texture stages with the
// D3DTOP_ operations, alpha test, specular add and vertex fog. Blending, depth
// test and culling are pipeline and depth-stencil states, cached by their
// render state values.
//
#import <Metal/Metal.h>
#include "host_surface_pixels.h"
#include "host_metal.h"
#include "../src/gpu_backend.h"
#include "../src/d3d_shader_msl.h"
#include "host_metal_glide.h"
#include <simd/simd.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace
{
using Gpu::Surface;

const char *const ShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

// Must match the C++ structures of the same names below.
struct Light
{
    float4 diffuse, specular, ambient;
    float4 position;      // eye space
    float4 direction;     // eye space: towards the light for a directional
                          // light, the spot axis for a spot light
    float4 p1;            // range, falloff, attenuation 0, attenuation 1
    float4 p2;            // attenuation 2, cos(theta/2), cos(phi/2), type
};

struct VertexUniforms
{
    float4x4 world[4];
    float4x4 worldview[4];
    float4x4 normal_matrix[4];
    float4x4 projection;
    float4x4 texture[8];
    float4 viewport;      // x, y, width, height of the D3D viewport
    float4 ambient;       // D3DRENDERSTATE_AMBIENT
    float4 mat_diffuse, mat_ambient, mat_specular, mat_emissive;
    float4 fog;           // start, end, density
    float4 clip_plane[6];
    Light lights[8];
    float mat_power;
    uint light_count;
    uint flags;
    uint sources;         // D3DMCS_ of diffuse, specular, ambient, emissive, four bits each
    uint fog_mode;        // D3DFOGMODE of vertex fog, 0 for none
    uint blend;           // D3DRENDERSTATE_VERTEXBLEND: weights in use
    uint clip_mask;
    uint stride, weights, normal, diffuse, specular;
    uint tex_offset[8];
    uint tex_size[8];
    uint tex_index[8];    // per stage: coordinate set | texgen << 16
    uint tex_flags[8];    // per stage: D3DTSS_TEXTURETRANSFORMFLAGS
};
constant uint FlagLighting = 1u, FlagSpecular = 2u, FlagLocalViewer = 4u, FlagNormalize = 8u;
constant uint FlagColorVertex = 16u, FlagRangeFog = 32u, FlagFogW = 64u, FlagShaderFog = 128u;

// The scalar helpers of translated vertex shaders (src/d3d_shader_msl.cpp),
// with the answers Direct3D gives for zero.
static float vs_rcp(float x) { return x == 0.0 ? INFINITY : 1.0 / x; }
static float vs_rsq(float x) { const float a = abs(x); return a == 0.0 ? INFINITY : rsqrt(a); }
static float vs_log(float x) { const float a = abs(x); return a == 0.0 ? -INFINITY : log2(a); }
static float4 vs_expp(float x) { const float f = floor(x); return float4(exp2(f), x - f, exp2(x), 1.0); }
static float4 vs_logp(float x)
{
    const float a = abs(x);
    if (a == 0.0) return float4(-INFINITY, 1.0, -INFINITY, 1.0);
    const float e = floor(log2(a));
    return float4(e, a / exp2(e), log2(a), 1.0);
}
static float4 vs_lit(float4 s)
{
    float4 d = float4(1.0, 0.0, 0.0, 1.0);
    if (s.x > 0.0)
    {
        d.y = s.x;
        d.z = s.y > 0.0 ? pow(s.y, clamp(s.w, -128.0, 128.0)) : 0.0;
    }
    return d;
}

struct FragmentUniforms
{
    uint color_op[8], color_arg1[8], color_arg2[8];
    uint alpha_op[8], alpha_arg1[8], alpha_arg2[8];
    uint textured[8];
    uint projected[8];    // 0, or 1 + the component that divides
    uint keyed[8];
    float4 tfactor;
    float4 fog_color;
    uint alpha_test;
    uint alpha_func;
    float alpha_ref;
    uint fog;
    uint specular;
    uint fog_mode;        // D3DFOGMODE of table fog, 0 for vertex fog
    float fog_start;
    float fog_end;
    float fog_density;
    float4 bump_matrix[8];     // D3DTSS_BUMPENVMAT00, 01, 10, 11 per stage
    float4 bump_luminance[8];  // D3DTSS_BUMPENVLSCALE, LOFFSET per stage
    float4 w_range;            // near and far W of a W buffer
    uint signed_texels[8];     // the stage samples a bump map: du and dv are signed
};

static float4 unpack(uint c)
{
    // Match the correctly rounded CPU normalization of ALPHAREF. Metal's
    // fast division rounds 127/255 one ULP up, reversing LE/GREATER at
    // the packed diffuse boundary.
    return precise::divide(float4(float((c >> 16) & 255u), float((c >> 8) & 255u), float(c & 255u), float(c >> 24)), float4(255.0));
}
static uint load_uint(device const uchar *p)
{
    return uint(p[0]) | (uint(p[1]) << 8) | (uint(p[2]) << 16) | (uint(p[3]) << 24);
}
static float load_float(device const uchar *p)
{
    return as_type<float>(load_uint(p));
}
// A texture coordinate set as four components: the missing ones are 1 right
// after the last given one and 0 beyond, which puts the translation of a 2D
// texture matrix into its third row, as in Direct3D.
static float4 load_coords(device const uchar *v, constant VertexUniforms &u, uint set)
{
    if (set >= 8u || u.tex_offset[set] == 0xffffffffu) return float4(0.0, 0.0, 1.0, 0.0);
    device const uchar *p = v + u.tex_offset[set];
    const uint n = u.tex_size[set];
    float4 t = float4(load_float(p), 1.0, 0.0, 0.0);
    if (n > 1u) t = float4(t.x, load_float(p + 4), 1.0, 0.0);
    if (n > 2u) t = float4(t.x, t.y, load_float(p + 8), 1.0);
    if (n > 3u) t.w = load_float(p + 12);
    return t;
}

static float4 argument(uint sel, float4 diffuse, float4 current, float4 tex, float4 tfactor, float4 specular)
{
    float4 v;
    switch (sel & 7u)
    {
    case 0u: v = diffuse; break;
    case 1u: v = current; break;
    case 2u: v = tex; break;
    case 3u: v = tfactor; break;
    case 4u: v = specular; break;
    default: v = current; break;
    }
    if (sel & 0x20u) v = float4(v.a);
    if (sel & 0x10u) v = 1.0 - v;
    return v;
}

static float4 operate(uint op, float4 a1, float4 a2, float4 diffuse, float4 current, float4 tex, float4 tfactor)
{
    switch (op)
    {
    case 2u: return a1;                                   // SELECTARG1
    case 3u: return a2;                                   // SELECTARG2
    case 4u: return a1 * a2;                              // MODULATE
    case 5u: return a1 * a2 * 2.0;                        // MODULATE2X
    case 6u: return a1 * a2 * 4.0;                        // MODULATE4X
    case 7u: return a1 + a2;                              // ADD
    case 8u: return a1 + a2 - 0.5;                        // ADDSIGNED
    case 9u: return (a1 + a2 - 0.5) * 2.0;                // ADDSIGNED2X
    case 10u: return a1 - a2;                             // SUBTRACT
    case 11u: return a1 + a2 - a1 * a2;                   // ADDSMOOTH
    case 12u: return mix(a2, a1, diffuse.a);              // BLENDDIFFUSEALPHA
    case 13u: return mix(a2, a1, tex.a);                  // BLENDTEXTUREALPHA
    case 14u: return mix(a2, a1, tfactor.a);              // BLENDFACTORALPHA
    case 15u: return a1 + a2 * (1.0 - tex.a);             // BLENDTEXTUREALPHAPM
    case 16u: return mix(a2, a1, current.a);              // BLENDCURRENTALPHA
    case 18u: return float4(a1.rgb + a1.a * a2.rgb, a1.a);            // MODULATEALPHA_ADDCOLOR
    case 19u: return float4(a1.rgb * a2.rgb + a1.a, a1.a);            // MODULATECOLOR_ADDALPHA
    case 20u: return float4((1.0 - a1.a) * a2.rgb + a1.rgb, a1.a);    // MODULATEINVALPHA_ADDCOLOR
    case 21u: return float4((1.0 - a1.rgb) * a2.rgb + a1.a, a1.a);    // MODULATEINVCOLOR_ADDALPHA
    case 24u: return float4(saturate(dot((a1.rgb - 0.5) * 2.0, (a2.rgb - 0.5) * 2.0)));   // DOTPRODUCT3
    default: return a1;
    }
}

// The pipeline once for Gouraud and once for flat shading: D3DSHADE_FLAT
// takes the colours of the first vertex of a primitive, which is what
// [[flat]] does in Metal. The vertex stage writes clip distances, which the
// fragment stage does not read, so each shading has an output and an input
// structure with the same members otherwise.
#define VARYINGS(NAME, QUALIFIER)                                                                             \
struct In##NAME                                                                                               \
{                                                                                                             \
    float4 position [[position]];                                                                             \
    float4 diffuse QUALIFIER;                                                                                 \
    float4 specular QUALIFIER;                                                                                \
    float4 tex0; float4 tex1; float4 tex2; float4 tex3;                                                       \
    float4 tex4; float4 tex5; float4 tex6; float4 tex7;                                                       \
    float depth;                                                                                              \
    float w;                                                                                                  \
};                                                                                                            \
struct Out##NAME                                                                                              \
{                                                                                                             \
    float4 position [[position]];                                                                             \
    float point_size [[point_size]];                                                                          \
    float4 diffuse QUALIFIER;                                                                                 \
    float4 specular QUALIFIER;                                                                                \
    float4 tex0; float4 tex1; float4 tex2; float4 tex3;                                                       \
    float4 tex4; float4 tex5; float4 tex6; float4 tex7;                                                       \
    float depth;                                                                                              \
    float w;                                                                                                  \
    float clip [[clip_distance]] [6];                                                                         \
};

VARYINGS(main, )
VARYINGS(flat, [[flat]])

struct Shaded
{
    float4 position, diffuse, specular;
    float4 tex[8];
    float depth;
    float w;              // the eye depth, for a W buffer
    float clip[6];
};

// A vertex in screen space: D3DFVF_XYZRHW, lit already.
static Shaded pretransformed(uint vid, device const uchar *data, constant VertexUniforms &u)
{
    device const uchar *v = data + vid * u.stride;
    const float x = load_float(v), y = load_float(v + 4), z = load_float(v + 8), rhw = load_float(v + 12);
    const float w = rhw != 0.0 ? 1.0 / rhw : 1.0;
    const float nx = ((x + 0.5 - u.viewport.x) / u.viewport.z) * 2.0 - 1.0;
    const float ny = 1.0 - ((y + 0.5 - u.viewport.y) / u.viewport.w) * 2.0;
    Shaded o;
    o.position = float4(nx * w, ny * w, clamp(z, 0.0, 1.0) * w, w);
    o.diffuse = u.diffuse != 0xffffffffu ? unpack(load_uint(v + u.diffuse)) : float4(1.0);
    o.specular = u.specular != 0xffffffffu ? unpack(load_uint(v + u.specular)) : float4(0.0, 0.0, 0.0, 1.0);
    for (uint s = 0u; s < 8u; ++s) o.tex[s] = load_coords(v, u, u.tex_index[s] & 7u);
    o.depth = (u.flags & FlagFogW) != 0u ? w : z;
    o.w = w;
    for (uint k = 0u; k < 6u; ++k) o.clip[k] = 1.0;
    return o;
}

// A vertex in clip space, lit: D3DFVF_XYZW, the OpenGL front end. Metal
// clips it and maps it onto the viewport with the Z range of the state; the
// pixel centres of OpenGL and Metal are the same, so nothing moves.
static Shaded clip_space(uint vid, device const uchar *data, constant VertexUniforms &u)
{
    device const uchar *v = data + vid * u.stride;
    Shaded o;
    o.position = float4(load_float(v), load_float(v + 4), load_float(v + 8), load_float(v + 12));
    o.diffuse = u.diffuse != 0xffffffffu ? unpack(load_uint(v + u.diffuse)) : float4(1.0);
    o.specular = u.specular != 0xffffffffu ? unpack(load_uint(v + u.specular)) : float4(0.0, 0.0, 0.0, 1.0);
    for (uint s = 0u; s < 8u; ++s) o.tex[s] = load_coords(v, u, u.tex_index[s] & 7u);
    o.depth = o.position.w;
    for (uint k = 0u; k < 6u; ++k) o.clip[k] = 1.0;
    return o;
}

// The colour a material source names: the material itself, or the vertex's
// diffuse (1) or specular (2) colour when the vertex has one.
static float4 source(uint which, float4 material, bool has_diffuse, float4 diffuse, bool has_specular, float4 specular)
{
    if (which == 1u && has_diffuse) return diffuse;
    if (which == 2u && has_specular) return specular;
    return material;
}

// A vertex in model space: the fixed function pipeline of DirectX 7.
static Shaded transform_and_light(uint vid, device const uchar *data, constant VertexUniforms &u)
{
    device const uchar *v = data + vid * u.stride;
    const float4 p = float4(load_float(v), load_float(v + 4), load_float(v + 8), 1.0);
    float3 n = float3(0.0);
    if (u.normal != 0xffffffffu) n = float3(load_float(v + u.normal), load_float(v + u.normal + 4), load_float(v + u.normal + 8));
    // Vertex blending: the weights after the position, the last matrix
    // taking what is left of 1.
    float4 eye = float4(0.0), world = float4(0.0);
    float3 en = float3(0.0);
    const uint matrices = min(u.blend, min(u.weights, 3u)) + 1u;
    float left = 1.0;
    for (uint i = 0u; i < matrices; ++i)
    {
        float weight = left;
        if (i + 1u < matrices)
        {
            weight = load_float(v + 12u + i * 4u);
            left -= weight;
        }
        eye += weight * (u.worldview[i] * p);
        world += weight * (u.world[i] * p);
        en += weight * (u.normal_matrix[i] * float4(n, 0.0)).xyz;
    }
    if ((u.flags & FlagNormalize) != 0u && dot(en, en) > 0.0) en = normalize(en);
    const bool has_diffuse = u.diffuse != 0xffffffffu, has_specular = u.specular != 0xffffffffu;
    const float4 vd = has_diffuse ? unpack(load_uint(v + u.diffuse)) : float4(1.0);
    const float4 vs = has_specular ? unpack(load_uint(v + u.specular)) : float4(0.0, 0.0, 0.0, 1.0);
    Shaded o;
    if ((u.flags & FlagLighting) != 0u)
    {
        const bool colour = (u.flags & FlagColorVertex) != 0u;
        const float4 m_diffuse = colour ? source(u.sources & 15u, u.mat_diffuse, has_diffuse, vd, has_specular, vs) : u.mat_diffuse;
        const float4 m_specular = colour ? source((u.sources >> 4) & 15u, u.mat_specular, has_diffuse, vd, has_specular, vs) : u.mat_specular;
        const float4 m_ambient = colour ? source((u.sources >> 8) & 15u, u.mat_ambient, has_diffuse, vd, has_specular, vs) : u.mat_ambient;
        const float4 m_emissive = colour ? source((u.sources >> 12) & 15u, u.mat_emissive, has_diffuse, vd, has_specular, vs) : u.mat_emissive;
        float3 ambient = float3(0.0), diffuse = float3(0.0), specular = float3(0.0);
        const float3 to_eye = (u.flags & FlagLocalViewer) != 0u ? normalize(-eye.xyz) : float3(0.0, 0.0, -1.0);
        for (uint i = 0u; i < u.light_count; ++i)
        {
            constant Light &l = u.lights[i];
            const uint type = uint(l.p2.w);
            float3 to_light;
            float strength = 1.0;
            if (type == 3u) to_light = l.direction.xyz;
            else
            {
                const float3 d = l.position.xyz - eye.xyz;
                const float distance = length(d);
                if (distance > l.p1.x) continue;
                to_light = distance > 0.0 ? d / distance : float3(0.0, 0.0, -1.0);
                const float a = l.p1.z + l.p1.w * distance + l.p2.x * distance * distance;
                strength = a > 0.0 ? 1.0 / a : 1.0;
                if (type == 2u)
                {
                    const float rho = dot(-to_light, l.direction.xyz);
                    if (rho <= l.p2.z) continue;
                    if (rho < l.p2.y)
                    {
                        const float t = (rho - l.p2.z) / max(l.p2.y - l.p2.z, 1.0e-6);
                        strength *= l.p1.y == 1.0 ? t : pow(t, l.p1.y);
                    }
                }
            }
            const float n_dot_l = max(dot(en, to_light), 0.0);
            ambient += l.ambient.rgb * strength;
            diffuse += l.diffuse.rgb * n_dot_l * strength;
            if ((u.flags & FlagSpecular) != 0u && n_dot_l > 0.0)
            {
                const float3 h = normalize(to_light + to_eye);
                specular += l.specular.rgb * pow(max(dot(en, h), 0.0), u.mat_power) * strength;
            }
        }
        o.diffuse = float4(saturate(m_emissive.rgb + (u.ambient.rgb + ambient) * m_ambient.rgb + diffuse * m_diffuse.rgb),
                           saturate(m_diffuse.a));
        o.specular = float4(saturate(specular * m_specular.rgb), 1.0);
    }
    else
    {
        o.diffuse = vd;
        o.specular = vs;
    }
    // Vertex fog: the factor into the specular alpha, out of the eye depth or
    // the distance from the eye (D3DRENDERSTATE_RANGEFOGENABLE).
    if (u.fog_mode != 0u)
    {
        const float d = (u.flags & FlagRangeFog) != 0u ? length(eye.xyz) : abs(eye.z);
        float f;
        if (u.fog_mode == 3u) f = (u.fog.y - d) / max(u.fog.y - u.fog.x, 1.0e-6);
        else if (u.fog_mode == 1u) f = exp(-u.fog.z * d);
        else f = exp(-(u.fog.z * d) * (u.fog.z * d));
        // D3DIM's software vertex path stores fog in D3DCOLOR's alpha byte.
        // Preserve that precision before interpolation.
        o.specular.a = floor(saturate(f) * 255.0 + 0.5) / 255.0;
    }
    float4 clip = u.projection * eye;
    // Direct3D puts pixel centres on whole numbers, Metal half a pixel later.
    clip.x += clip.w / u.viewport.z;
    clip.y -= clip.w / u.viewport.w;
    o.position = clip;
    o.depth = (u.flags & FlagFogW) != 0u ? eye.z : (clip.w != 0.0 ? clip.z / clip.w : 0.0);
    o.w = clip.w;
    for (uint k = 0u; k < 6u; ++k)
        o.clip[k] = (u.clip_mask & (1u << k)) != 0u ? dot(u.clip_plane[k], world) : 1.0;
    // Texture coordinates per stage: the set the stage names or a generated
    // one, through the texture matrix when the stage asks for it.
    const float3 reflection = reflect(normalize(eye.xyz), en);
    for (uint s = 0u; s < 8u; ++s)
    {
        const uint index = u.tex_index[s];
        float4 t;
        switch (index >> 16)
        {
        case 1u: t = float4(en, 1.0); break;           // D3DTSS_TCI_CAMERASPACENORMAL
        case 2u: t = float4(eye.xyz, 1.0); break;      // CAMERASPACEPOSITION
        case 3u: t = float4(reflection, 1.0); break;   // CAMERASPACEREFLECTIONVECTOR
        default: t = load_coords(v, u, index & 7u); break;
        }
        if ((u.tex_flags[s] & 0xffu) != 0u) t = u.texture[s] * t;
        o.tex[s] = t;
    }
    return o;
}

#define VERTEX(KIND, NAME)                                                                                   \
vertex Out##NAME vertex_##KIND##_##NAME(uint vid [[vertex_id]], device const uchar *data [[buffer(0)]],     \
                                        constant VertexUniforms &u [[buffer(1)]])                           \
{                                                                                                            \
    const Shaded s = KIND(vid, data, u);                                                                     \
    Out##NAME o;                                                                                             \
    o.position = s.position;                                                                                 \
    o.point_size = 1.0;                                                                                      \
    o.diffuse = s.diffuse;                                                                                   \
    o.specular = s.specular;                                                                                 \
    o.tex0 = s.tex[0]; o.tex1 = s.tex[1]; o.tex2 = s.tex[2]; o.tex3 = s.tex[3];                              \
    o.tex4 = s.tex[4]; o.tex5 = s.tex[5]; o.tex6 = s.tex[6]; o.tex7 = s.tex[7];                              \
    o.depth = s.depth;                                                                                       \
    o.w = s.w;                                                                                               \
    for (uint k = 0u; k < 6u; ++k) o.clip[k] = s.clip[k];                                                    \
    return o;                                                                                                \
}
VERTEX(pretransformed, main)
VERTEX(pretransformed, flat)
VERTEX(transform_and_light, main)
VERTEX(transform_and_light, flat)
VERTEX(clip_space, main)
VERTEX(clip_space, flat)

#define TEXTURES                                                                                             \
    texture2d<float> t0, texture2d<float> t1, texture2d<float> t2, texture2d<float> t3,                      \
    texture2d<float> t4, texture2d<float> t5, texture2d<float> t6, texture2d<float> t7,                      \
    texturecube<float> q0, texturecube<float> q1, texturecube<float> q2, texturecube<float> q3,              \
    texturecube<float> q4, texturecube<float> q5, texturecube<float> q6, texturecube<float> q7,              \
    texture3d<float> vol0, texture3d<float> vol1, texture3d<float> vol2, texture3d<float> vol3,              \
    texture3d<float> vol4, texture3d<float> vol5, texture3d<float> vol6, texture3d<float> vol7,              \
    sampler s0, sampler s1, sampler s2, sampler s3, sampler s4, sampler s5, sampler s6, sampler s7
#define TEXTURE_NAMES t0, t1, t2, t3, t4, t5, t6, t7, q0, q1, q2, q3, q4, q5, q6, q7,                          \
    vol0, vol1, vol2, vol3, vol4, vol5, vol6, vol7, s0, s1, s2, s3, s4, s5, s6, s7
#define TEXTURE_ARGUMENTS                                                                                    \
    texture2d<float> t0 [[texture(0)]], texture2d<float> t1 [[texture(1)]],                                  \
    texture2d<float> t2 [[texture(2)]], texture2d<float> t3 [[texture(3)]],                                  \
    texture2d<float> t4 [[texture(4)]], texture2d<float> t5 [[texture(5)]],                                  \
    texture2d<float> t6 [[texture(6)]], texture2d<float> t7 [[texture(7)]],                                  \
    texturecube<float> q0 [[texture(8)]], texturecube<float> q1 [[texture(9)]],                              \
    texturecube<float> q2 [[texture(10)]], texturecube<float> q3 [[texture(11)]],                            \
    texturecube<float> q4 [[texture(12)]], texturecube<float> q5 [[texture(13)]],                            \
    texturecube<float> q6 [[texture(14)]], texturecube<float> q7 [[texture(15)]],                            \
    texture3d<float> vol0 [[texture(16)]], texture3d<float> vol1 [[texture(17)]],                            \
    texture3d<float> vol2 [[texture(18)]], texture3d<float> vol3 [[texture(19)]],                            \
    texture3d<float> vol4 [[texture(20)]], texture3d<float> vol5 [[texture(21)]],                            \
    texture3d<float> vol6 [[texture(22)]], texture3d<float> vol7 [[texture(23)]],                            \
    sampler s0 [[sampler(0)]], sampler s1 [[sampler(1)]], sampler s2 [[sampler(2)]], sampler s3 [[sampler(3)]], \
    sampler s4 [[sampler(4)]], sampler s5 [[sampler(5)]], sampler s6 [[sampler(6)]], sampler s7 [[sampler(7)]]

static float4 sample_stage(uint stage, float2 uv, TEXTURES)
{
    switch (stage)
    {
    case 0u: return t0.sample(s0, uv);
    case 1u: return t1.sample(s1, uv);
    case 2u: return t2.sample(s2, uv);
    case 3u: return t3.sample(s3, uv);
    case 4u: return t4.sample(s4, uv);
    case 5u: return t5.sample(s5, uv);
    case 6u: return t6.sample(s6, uv);
    default: return t7.sample(s7, uv);
    }
}
static float4 sample_cube(uint stage, float3 direction, TEXTURES)
{
    switch (stage)
    {
    case 0u: return q0.sample(s0, direction);
    case 1u: return q1.sample(s1, direction);
    case 2u: return q2.sample(s2, direction);
    case 3u: return q3.sample(s3, direction);
    case 4u: return q4.sample(s4, direction);
    case 5u: return q5.sample(s5, direction);
    case 6u: return q6.sample(s6, direction);
    default: return q7.sample(s7, direction);
    }
}
static float4 sample_volume(uint stage, float3 c, TEXTURES)
{
    switch (stage)
    {
    case 0u: return vol0.sample(s0, c);
    case 1u: return vol1.sample(s1, c);
    case 2u: return vol2.sample(s2, c);
    case 3u: return vol3.sample(s3, c);
    case 4u: return vol4.sample(s4, c);
    case 5u: return vol5.sample(s5, c);
    case 6u: return vol6.sample(s6, c);
    default: return vol7.sample(s7, c);
    }
}

// The helpers of translated pixel shaders (src/d3d_shader_msl.cpp). A stage
// samples its texture at the coordinates, a cube map or a volume with three
// of them; a
// stage without a texture answers black. `projected` applies the stage's
// D3DTTFF_PROJECTED, which texture instructions before version 1.4 honour.
static float4 shader_sample(constant FragmentUniforms &u, uint stage, float4 c, bool projected, TEXTURES)
{
    if (stage >= 8u || u.textured[stage] == 0u) return float4(0.0, 0.0, 0.0, 1.0);
    float4 v;
    if (u.textured[stage] == 2u) v = sample_cube(stage, c.xyz, TEXTURE_NAMES);
    else if (u.textured[stage] == 3u) v = sample_volume(stage, c.xyz, TEXTURE_NAMES);
    else
    {
        float2 uv = c.xy;
        const uint p = u.projected[stage];
        if (projected && p != 0u) { const float q = c[p - 1u]; if (q != 0.0) uv /= q; }
        v = sample_stage(stage, uv, TEXTURE_NAMES);
    }
    // A bump map keeps du and dv with 128 for zero.
    if (u.signed_texels[stage] != 0u) v.xy = (v.xy * 255.0 - 128.0) / 127.0;
    return v;
}
// texbem and texbeml: the coordinates of the stage moved by its bump matrix
// times du and dv of the source, and with luminance its scale and offset.
static float4 texbem(constant FragmentUniforms &u, uint stage, float4 coords, float4 bump, bool luminance, TEXTURES)
{
    const float4 m = u.bump_matrix[stage];   // 00, 01, 10, 11
    const float2 uv = coords.xy + float2(bump.x * m.x + bump.y * m.z, bump.x * m.y + bump.y * m.w);
    float4 v = shader_sample(u, stage, float4(uv, 0.0, 1.0), false, TEXTURE_NAMES);
    if (luminance) v.rgb *= saturate(bump.z * u.bump_luminance[stage].x + u.bump_luminance[stage].y);
    return v;
}
// bem of version 1.4: the same offset as arithmetic.
static float4 bem(constant FragmentUniforms &u, uint stage, float4 a, float4 b)
{
    const float4 m = u.bump_matrix[stage];
    return float4(a.x + b.x * m.x + b.y * m.z, a.y + b.x * m.y + b.y * m.w, a.z, a.w);
}
// texld of shader model 2: mode 0 plain, 1 biased by c.w (texldb), 2 at the
// level c.w (texldl).
static float4 shader_sample2(constant FragmentUniforms &u, uint stage, float4 c, uint mode, TEXTURES)
{
    if (stage >= 8u || u.textured[stage] == 0u) return float4(0.0, 0.0, 0.0, 1.0);
    float4 v;
    if (u.textured[stage] == 2u) v = sample_cube(stage, c.xyz, TEXTURE_NAMES);
    else if (u.textured[stage] == 3u) v = sample_volume(stage, c.xyz, TEXTURE_NAMES);
    else if (mode == 0u) v = sample_stage(stage, c.xy, TEXTURE_NAMES);
    else
    {
        switch (stage)
        {
        case 0u: v = mode == 1u ? t0.sample(s0, c.xy, bias(c.w)) : t0.sample(s0, c.xy, level(c.w)); break;
        case 1u: v = mode == 1u ? t1.sample(s1, c.xy, bias(c.w)) : t1.sample(s1, c.xy, level(c.w)); break;
        case 2u: v = mode == 1u ? t2.sample(s2, c.xy, bias(c.w)) : t2.sample(s2, c.xy, level(c.w)); break;
        case 3u: v = mode == 1u ? t3.sample(s3, c.xy, bias(c.w)) : t3.sample(s3, c.xy, level(c.w)); break;
        case 4u: v = mode == 1u ? t4.sample(s4, c.xy, bias(c.w)) : t4.sample(s4, c.xy, level(c.w)); break;
        case 5u: v = mode == 1u ? t5.sample(s5, c.xy, bias(c.w)) : t5.sample(s5, c.xy, level(c.w)); break;
        case 6u: v = mode == 1u ? t6.sample(s6, c.xy, bias(c.w)) : t6.sample(s6, c.xy, level(c.w)); break;
        default: v = mode == 1u ? t7.sample(s7, c.xy, bias(c.w)) : t7.sample(s7, c.xy, level(c.w)); break;
        }
    }
    if (u.signed_texels[stage] != 0u) v.xy = (v.xy * 255.0 - 128.0) / 127.0;
    return v;
}
// nrm: the vector over the length of its xyz.
static float4 vs_nrm(float4 v)
{
    const float l = length(v.xyz);
    return l > 0.0 ? v / l : v;
}
// texm3x3spec and texm3x3vspec: the eye vector reflected about the normal.
static float3 reflect_eye(float3 n, float3 e)
{
    const float nn = dot(n, n);
    return nn != 0.0 ? 2.0 * dot(n, e) / nn * n - e : -e;
}

// The texture stages. A bump stage (BUMPENVMAP, BUMPENVMAPLUMINANCE) reads
// du and dv out of its texture, turns them through its 2x2 matrix into an
// offset for the coordinates of the next stage, and with luminance scales the
// colour that stage samples; its own colour result is the current colour.
static float4 stages(constant FragmentUniforms &u, float4 diffuse, float4 specular, thread const float4 *coords, TEXTURES)
{
    float4 current = diffuse;
    float2 bump = float2(0.0);
    float luminance = 1.0;
    for (uint stage = 0u; stage < 8u; ++stage)
    {
        const uint cop = u.color_op[stage];
        if (cop == 1u) break;   // DISABLE ends the chain
        float4 tex = float4(1.0);
        if (u.textured[stage] != 0u)
        {
            const float4 c = coords[stage];
            if (u.textured[stage] == 2u)
                tex = sample_cube(stage, c.xyz, TEXTURE_NAMES);
            else if (u.textured[stage] == 3u)
                tex = sample_volume(stage, c.xyz, TEXTURE_NAMES);
            else
            {
                float2 uv = c.xy;
                const uint p = u.projected[stage];
                if (p != 0u) { const float q = c[p - 1u]; if (q != 0.0) uv /= q; }
                tex = sample_stage(stage, uv + bump, TEXTURE_NAMES);
            }
            tex.rgb *= luminance;
            if (u.keyed[stage] != 0u && tex.a < 0.5) discard_fragment();
        }
        bump = float2(0.0);
        luminance = 1.0;
        if (cop == 22u || cop == 23u)
        {
            const float du = (tex.r * 255.0 - 128.0) / 127.0, dv = (tex.g * 255.0 - 128.0) / 127.0;
            const float4 m = u.bump_matrix[stage];   // 00, 01, 10, 11
            bump = float2(du * m.x + dv * m.z, du * m.y + dv * m.w);
            if (cop == 23u) luminance = saturate(tex.b * u.bump_luminance[stage].x + u.bump_luminance[stage].y);
            continue;
        }
        const float4 c1 = argument(u.color_arg1[stage], diffuse, current, tex, u.tfactor, specular);
        const float4 c2 = argument(u.color_arg2[stage], diffuse, current, tex, u.tfactor, specular);
        const float4 a1 = argument(u.alpha_arg1[stage], diffuse, current, tex, u.tfactor, specular);
        const float4 a2 = argument(u.alpha_arg2[stage], diffuse, current, tex, u.tfactor, specular);
        const float4 color = operate(cop, c1, c2, diffuse, current, tex, u.tfactor);
        float4 alpha = current;
        if (u.alpha_op[stage] != 1u) alpha = operate(u.alpha_op[stage], a1, a2, diffuse, current, tex, u.tfactor);
        current = clamp(float4(color.rgb, alpha.a), 0.0, 1.0);
    }
    return current;
}

static float4 finish(constant FragmentUniforms &u, float depth, float4 specular, float4 current)
{
    if (u.alpha_test != 0u)
    {
        const float a = current.a, r = u.alpha_ref;
        // Keep the predicate materialized across the switch. On Apple Metal,
        // optimizing it into discard control flow loses opaque PS 1/2 pixels
        // (test_d3d_alpha_test_metal), even with a constant alpha of 1.
        volatile bool pass = true;
        switch (u.alpha_func)
        {
        case 1u: pass = false; break;
        case 2u: pass = a < r; break;
        case 3u: pass = abs(a - r) < 0.5 / 255.0; break;
        case 4u: pass = a <= r; break;
        case 5u: pass = a > r; break;
        case 6u: pass = abs(a - r) >= 0.5 / 255.0; break;
        case 7u: pass = a >= r; break;
        default: pass = true; break;
        }
        if (!pass) discard_fragment();
    }
    if (u.specular != 0u) current.rgb += specular.rgb;
    if (u.fog != 0u)
    {
        // Vertex fog carries its factor in the specular alpha; table fog
        // computes it per pixel from the eye depth (W) or the device depth.
        float f = specular.a;
        if (u.fog_mode != 0u)
        {
            const float d = depth;
            if (u.fog_mode == 3u) f = (u.fog_end - d) / max(u.fog_end - u.fog_start, 1.0e-6);   // LINEAR
            else if (u.fog_mode == 1u) f = exp(-u.fog_density * d);                             // EXP
            else f = exp(-(u.fog_density * d) * (u.fog_density * d));                           // EXP2
            f = clamp(f, 0.0, 1.0);
        }
        current.rgb = mix(u.fog_color.rgb, current.rgb, f);
    }
    return clamp(current, 0.0, 1.0);
}


// A W buffer (D3DZB_USEW) keeps the eye depth between the W range of the
// scene, linear, instead of Z; the fragment writes it.
struct DepthOut
{
    float4 color [[color(0)]];
    float depth [[depth(any)]];
};

#define FRAGMENT(NAME)                                                                                       \
fragment float4 fragment_##NAME(In##NAME in [[stage_in]], constant FragmentUniforms &u [[buffer(0)]],       \
                                TEXTURE_ARGUMENTS)                                                           \
{                                                                                                            \
    const float4 coords[8] = {in.tex0, in.tex1, in.tex2, in.tex3, in.tex4, in.tex5, in.tex6, in.tex7};      \
    return finish(u, in.depth, in.specular, stages(u, in.diffuse, in.specular, coords, TEXTURE_NAMES));      \
}                                                                                                            \
fragment DepthOut fragment_##NAME##_w(In##NAME in [[stage_in]], constant FragmentUniforms &u [[buffer(0)]], \
                                      TEXTURE_ARGUMENTS)                                                     \
{                                                                                                            \
    const float4 coords[8] = {in.tex0, in.tex1, in.tex2, in.tex3, in.tex4, in.tex5, in.tex6, in.tex7};      \
    DepthOut o;                                                                                              \
    o.color = finish(u, in.depth, in.specular, stages(u, in.diffuse, in.specular, coords, TEXTURE_NAMES));   \
    o.depth = saturate((in.w - u.w_range.x) / max(u.w_range.y - u.w_range.x, 1.0e-6));                     \
    return o;                                                                                                \
}

FRAGMENT(main)
FRAGMENT(flat)

struct ClearUniforms
{
    float4 color;
    float depth;
};

vertex float4 clear_vertex(uint vid [[vertex_id]], constant ClearUniforms &u [[buffer(1)]])
{
    const float2 corners[3] = {float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0)};
    return float4(corners[vid], u.depth, 1.0);
}

fragment float4 clear_fragment(constant ClearUniforms &u [[buffer(0)]])
{
    return u.color;
}
)MSL";

// The source of a translated pixel shader: the backend's shader source, the
// translated function and its fragment entry points, Gouraud and flat, each
// with and without a W buffer.
std::string PixelShaderSource(const std::string &body)
{
    std::string source = ShaderSource;
    source += body;
    source += R"MSL(
#define SHADER_ENTRY(NAME)                                                                                   \
fragment float4 shader_##NAME(In##NAME in [[stage_in]], constant FragmentUniforms &u [[buffer(0)]],        \
                              constant float4 *c [[buffer(1)]], constant int4 *ci [[buffer(2)]], constant uint *cb [[buffer(3)]], TEXTURE_ARGUMENTS)                           \
{                                                                                                            \
    const float4 coords[8] = {in.tex0, in.tex1, in.tex2, in.tex3, in.tex4, in.tex5, in.tex6, in.tex7};      \
    const float4 color = saturate(shader_body(in.diffuse, in.specular, coords, u, c, ci, cb, TEXTURE_NAMES));        \
    return finish(u, in.depth, in.specular, color);                                                          \
}                                                                                                            \
fragment DepthOut shader_##NAME##_w(In##NAME in [[stage_in]], constant FragmentUniforms &u [[buffer(0)]],  \
                                    constant float4 *c [[buffer(1)]], constant int4 *ci [[buffer(2)]], constant uint *cb [[buffer(3)]], TEXTURE_ARGUMENTS)                     \
{                                                                                                            \
    const float4 coords[8] = {in.tex0, in.tex1, in.tex2, in.tex3, in.tex4, in.tex5, in.tex6, in.tex7};      \
    DepthOut o;                                                                                              \
    const float4 color = saturate(shader_body(in.diffuse, in.specular, coords, u, c, ci, cb, TEXTURE_NAMES));        \
    o.color = finish(u, in.depth, in.specular, color);                                                       \
    o.depth = saturate((in.w - u.w_range.x) / max(u.w_range.y - u.w_range.x, 1.0e-6));                     \
    return o;                                                                                                \
}
SHADER_ENTRY(main)
SHADER_ENTRY(flat)
)MSL";
    return source;
}

// The source of a translated vertex shader: its function and its vertex
// entry points, Gouraud and flat. The inputs are the draw's input registers,
// u.stride bytes per vertex; the outputs go the way the fixed function
// pipeline puts them: clip space moved half a pixel, the texture coordinate
// set of each stage, oFog as the fog factor in the specular alpha.
std::string VertexShaderSource(const std::string &body)
{
    std::string source = ShaderSource;
    source += body;
    source += R"MSL(
#define VERTEX_SHADER_ENTRY(NAME)                                                                            \
vertex Out##NAME vshader_##NAME(uint vid [[vertex_id]], device const float4 *inputs [[buffer(0)]],           \
                                constant VertexUniforms &u [[buffer(1)]], constant float4 *c [[buffer(2)]], constant int4 *ci [[buffer(3)]], constant uint *cb [[buffer(4)]]) \
{                                                                                                            \
    const uint slots = u.stride / 16u;                                                                       \
    float4 v[16];                                                                                            \
    for (uint i = 0u; i < 16u; ++i) v[i] = i < slots ? inputs[vid * slots + i] : float4(0.0, 0.0, 0.0, 1.0); \
    float4 oPos = float4(0.0, 0.0, 0.0, 1.0), oFog = float4(1.0), oPts = float4(1.0);                        \
    float4 oD[2] = {float4(1.0), float4(0.0)};                                                               \
    float4 oT[8];                                                                                            \
    for (uint i = 0u; i < 8u; ++i) oT[i] = float4(0.0, 0.0, 0.0, 1.0);                                       \
    vertex_body(v, c, ci, cb, oPos, oD, oT, oFog, oPts);                                                             \
    Out##NAME o;                                                                                             \
    float4 clip = oPos;                                                                                      \
    clip.x += clip.w / u.viewport.z;                                                                         \
    clip.y -= clip.w / u.viewport.w;                                                                         \
    o.position = clip;                                                                                       \
    o.point_size = max(oPts.x, 1.0);                                                                         \
    o.diffuse = saturate(oD[0]);                                                                             \
    o.specular = saturate(oD[1]);                                                                            \
    if ((u.flags & FlagShaderFog) != 0u) o.specular.a = saturate(oFog.x);                                    \
    float4 t[8];                                                                                             \
    for (uint s = 0u; s < 8u; ++s) t[s] = oT[u.tex_index[s] & 7u];                                           \
    o.tex0 = t[0]; o.tex1 = t[1]; o.tex2 = t[2]; o.tex3 = t[3];                                              \
    o.tex4 = t[4]; o.tex5 = t[5]; o.tex6 = t[6]; o.tex7 = t[7];                                              \
    o.depth = clip.w != 0.0 ? clip.z / clip.w : 0.0;                                                         \
    o.w = clip.w;                                                                                            \
    for (uint k = 0u; k < 6u; ++k) o.clip[k] = 1.0;                                                          \
    return o;                                                                                                \
}
VERTEX_SHADER_ENTRY(main)
VERTEX_SHADER_ENTRY(flat)
)MSL";
    return source;
}

// The uniforms of the shaders, laid out like their MSL twins above.
struct LightUniforms
{
    simd_float4 diffuse, specular, ambient, position, direction, p1, p2;
};
struct VertexUniforms
{
    simd_float4x4 world[4], worldview[4], normal_matrix[4], projection, texture[8];
    simd_float4 viewport, ambient, mat_diffuse, mat_ambient, mat_specular, mat_emissive, fog;
    simd_float4 clip_plane[6];
    LightUniforms lights[8];
    float mat_power;
    unsigned light_count, flags, sources, fog_mode, blend, clip_mask;
    unsigned stride, weights, normal, diffuse, specular;
    unsigned tex_offset[8], tex_size[8], tex_index[8], tex_flags[8];
};
constexpr unsigned FlagLighting = 1, FlagSpecular = 2, FlagLocalViewer = 4, FlagNormalize = 8;
constexpr unsigned FlagColorVertex = 16, FlagRangeFog = 32, FlagFogW = 64, FlagShaderFog = 128;
struct FragmentUniforms
{
    unsigned color_op[8], color_arg1[8], color_arg2[8];
    unsigned alpha_op[8], alpha_arg1[8], alpha_arg2[8];
    unsigned textured[8], projected[8], keyed[8];
    simd_float4 tfactor, fog_color;
    unsigned alpha_test, alpha_func;
    float alpha_ref;
    unsigned fog, specular, fog_mode;
    float fog_start, fog_end, fog_density;
    simd_float4 bump_matrix[8], bump_luminance[8], w_range;
    unsigned signed_texels[8];
};
struct ClearUniforms
{
    float color[4];
    float depth;
    float pad[3];
};

// D3DMATRIX arithmetic, row major with row vectors. Uploaded as they are, the
// rows of a D3DMATRIX are the columns of a Metal float4x4, so v * M in
// Direct3D is M * v in the shader.
void Multiply(const float *a, const float *b, float *out)
{
    float r[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] + a[i * 4 + 3] * b[12 + j];
    memcpy(out, r, sizeof r);
}
simd_float4x4 Raw(const float *m)
{
    simd_float4x4 r;
    memcpy(&r, m, 64);
    return r;
}
// The matrix normals go through: the inverse transpose of the upper 3x3.
void NormalMatrix(const float *m, float *out)
{
    const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], k = m[10];
    const float A = e * k - f * h, B = -(d * k - f * g), C = d * h - e * g;
    const float det = a * A + b * B + c * C;
    const float inv = det != 0.0f ? 1.0f / det : 0.0f;
    // The inverse is the adjugate over the determinant; its transpose puts
    // the cofactors in place.
    const float cof[9] = {A, B, C, -(b * k - c * h), a * k - c * g, -(a * h - b * g), b * f - c * e, -(a * f - c * d), a * e - b * d};
    for (int i = 0; i < 16; ++i) out[i] = 0.0f;
    for (int r = 0; r < 3; ++r)
        for (int col = 0; col < 3; ++col) out[r * 4 + col] = cof[r * 3 + col] * inv;
    out[15] = 1.0f;
}
simd_float4 Color4(const float *c) { return simd_make_float4(c[0], c[1], c[2], c[3]); }
simd_float4 Unpack(unsigned c)
{
    return simd_make_float4(((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f, (c >> 24) / 255.0f);
}

// A channel of a masked pixel format: shift and width of the mask.
struct Channel
{
    unsigned shift = 0, bits = 0;
    void Of(unsigned mask)
    {
        shift = bits = 0;
        if (!mask) return;
        while (!(mask & 1)) { mask >>= 1; ++shift; }
        while (mask & 1) { mask >>= 1; ++bits; }
    }
    unsigned To8(unsigned pixel) const
    {
        if (!bits) return 0;
        const unsigned v = (pixel >> shift) & ((1u << bits) - 1);
        if (bits >= 8) return v >> (bits - 8);
        return (v * 255 + ((1u << bits) - 1) / 2) / ((1u << bits) - 1);
    }
};

// Converts the bytes of a surface into BGRA8 rows. Alpha is 255 where the
// format has no alpha channel.
void ToBgra(const Surface &s, std::vector<unsigned> &out)
{
    out.resize(size_t(s.width) * size_t(s.height));
    Channel r, g, b, a;
    r.Of(s.r_mask); g.Of(s.g_mask); b.Of(s.b_mask); a.Of(s.a_mask);
    const unsigned bytes = s.bit_count / 8;
    // A keyed texel keeps its colour and loses its alpha; the fragment shader
    // drops it when the colour key render state is on.
    const bool keyed = s.color_key;
    const unsigned key_low = s.key_low, key_high = s.key_high;
    for (int y = 0; y < s.height; ++y)
    {
        const unsigned char *row = s.bits + size_t(y) * size_t(s.pitch);
        unsigned *dst = out.data() + size_t(y) * size_t(s.width);
        for (int x = 0; x < s.width; ++x)
        {
            unsigned pixel = 0;
            if (s.indexed)
            {
                const unsigned index = row[x];
                const bool key = keyed && index >= key_low && index <= key_high;
                // A palette with DDRAWIPAL_ALPHA carries the alpha of each
                // entry in its top byte.
                const unsigned alpha = s.palette && s.palette_alpha ? s.palette[index] >> 24 : 255u;
                dst[x] = (s.palette ? s.palette[index] & 0xffffff : index * 0x010101u) | (key ? 0u : alpha << 24);
                continue;
            }
            switch (bytes)
            {
            case 1: pixel = row[x]; break;
            case 2: pixel = row[x * 2] | (row[x * 2 + 1] << 8); break;
            case 3: pixel = row[x * 3] | (row[x * 3 + 1] << 8) | (row[x * 3 + 2] << 16); break;
            default: pixel = row[x * 4] | (row[x * 4 + 1] << 8) | (row[x * 4 + 2] << 16) | (unsigned(row[x * 4 + 3]) << 24); break;
            }
            if (s.bump)
            {
                // du and dv are signed fields; they go into red and green
                // with 128 for zero, the luminance into blue.
                auto signed8 = [](const Channel &c, unsigned p) -> unsigned {
                    if (!c.bits) return 128;
                    const int v = int((p >> c.shift) & ((1u << c.bits) - 1));
                    const int half = 1 << (c.bits - 1);
                    const int value = v >= half ? v - 2 * half : v;
                    const int scaled = value * 127 / half;
                    return unsigned(scaled + 128 < 0 ? 0 : scaled + 128 > 255 ? 255 : scaled + 128);
                };
                const unsigned lum = s.bump_luminance ? b.To8(pixel) : 255;
                dst[x] = lum | (signed8(g, pixel) << 8) | (signed8(r, pixel) << 16) | 0xff000000u;
                continue;
            }
            unsigned alpha = s.a_mask ? a.To8(pixel) : 255;
            if (keyed && pixel >= key_low && pixel <= key_high) alpha = 0;
            dst[x] = b.To8(pixel) | (g.To8(pixel) << 8) | (r.To8(pixel) << 16) | (alpha << 24);
        }
    }
}

// Writes BGRA8 rows back into the bytes of a surface.
void FromBgra(const Surface &s, const unsigned *in)
{
    HostSurfacePixels::FromBgra(s, in);
}

MTLBlendFactor BlendFactor(unsigned d3d, bool alpha)
{
    switch (d3d)
    {
    case 1: return MTLBlendFactorZero;
    case 2: return MTLBlendFactorOne;
    case 3: return alpha ? MTLBlendFactorSourceAlpha : MTLBlendFactorSourceColor;
    case 4: return alpha ? MTLBlendFactorOneMinusSourceAlpha : MTLBlendFactorOneMinusSourceColor;
    case 5: return MTLBlendFactorSourceAlpha;
    case 6: return MTLBlendFactorOneMinusSourceAlpha;
    case 7: return MTLBlendFactorDestinationAlpha;
    case 8: return MTLBlendFactorOneMinusDestinationAlpha;
    case 9: return alpha ? MTLBlendFactorDestinationAlpha : MTLBlendFactorDestinationColor;
    case 10: return alpha ? MTLBlendFactorOneMinusDestinationAlpha : MTLBlendFactorOneMinusDestinationColor;
    case 11: return MTLBlendFactorSourceAlphaSaturated;
    case 12: return MTLBlendFactorSourceAlpha;          // BOTHSRCALPHA: dest becomes 1 - src alpha
    case 13: return MTLBlendFactorOneMinusSourceAlpha;  // BOTHINVSRCALPHA
    default: return MTLBlendFactorOne;
    }
}

MTLCompareFunction Compare(unsigned d3d)
{
    switch (d3d)
    {
    case 1: return MTLCompareFunctionNever;
    case 2: return MTLCompareFunctionLess;
    case 3: return MTLCompareFunctionEqual;
    case 4: return MTLCompareFunctionLessEqual;
    case 5: return MTLCompareFunctionGreater;
    case 6: return MTLCompareFunctionNotEqual;
    case 7: return MTLCompareFunctionGreaterEqual;
    default: return MTLCompareFunctionAlways;
    }
}

MTLSamplerAddressMode Address(unsigned d3d)
{
    switch (d3d)
    {
    case 2: return MTLSamplerAddressModeMirrorRepeat;
    case 3: return MTLSamplerAddressModeClampToEdge;
    case 4: return MTLSamplerAddressModeClampToBorderColor;
    default: return MTLSamplerAddressModeRepeat;
    }
}

struct PassStatistics
{
    unsigned long long passes = 0, batches = 0, load_clears = 0, quad_clears = 0;
    void Log() const
    {
        if (getenv("NEO98_METAL_STATS"))
            fprintf(stderr, "METAL passes=%llu batches=%llu load_clears=%llu quad_clears=%llu\n",
                    passes, batches, load_clears, quad_clears);
    }
    ~PassStatistics() { Log(); }
} pass_statistics;

class MetalBackend final : public Gpu::Backend
{
  public:
    bool Open()
    {
        device = MTLCreateSystemDefaultDevice();
        if (!device) return false;
        queue = [device newCommandQueue];
        NSError *error = nil;
        MTLCompileOptions *options = [MTLCompileOptions new];
        library = [device newLibraryWithSource:[NSString stringWithUTF8String:ShaderSource] options:options error:&error];
        if (!library)
        {
            fprintf(stderr, "Metal: %s\n", error ? error.localizedDescription.UTF8String : "no library");
            return false;
        }
        vertex_function = [library newFunctionWithName:@"vertex_pretransformed_main"];
        fragment_function = [library newFunctionWithName:@"fragment_main"];
        vertex_flat = [library newFunctionWithName:@"vertex_pretransformed_flat"];
        fragment_flat = [library newFunctionWithName:@"fragment_flat"];
        fragment_w = [library newFunctionWithName:@"fragment_main_w"];
        fragment_flat_w = [library newFunctionWithName:@"fragment_flat_w"];
        vertex_tnl = [library newFunctionWithName:@"vertex_transform_and_light_main"];
        vertex_tnl_flat = [library newFunctionWithName:@"vertex_transform_and_light_flat"];
        vertex_clip = [library newFunctionWithName:@"vertex_clip_space_main"];
        vertex_clip_flat = [library newFunctionWithName:@"vertex_clip_space_flat"];
        clear_vertex = [library newFunctionWithName:@"clear_vertex"];
        clear_fragment = [library newFunctionWithName:@"clear_fragment"];
        name_text = std::string("Metal ") + device.name.UTF8String;
        // DXT1 to DXT5 are BC1 to BC3; Apple GPUs sample them since macOS 11.
        if (@available(macOS 11.0, *)) compressed = device.supportsBCTextureCompression;
        return vertex_function && fragment_function && vertex_flat && fragment_flat && vertex_tnl && vertex_tnl_flat &&
               vertex_clip && vertex_clip_flat &&
               fragment_w && fragment_flat_w &&
               clear_vertex && clear_fragment;
    }
    const char *Name() const override { return name_text.c_str(); }
    bool CompressedTextures() const override { return compressed; }
    // NEO98_D3D_NO_TNL=1 hides the transform-and-lighting device, so a
    // program can be compared on both devices.
    bool TransformAndLight() const override { return !getenv("NEO98_D3D_NO_TNL"); }
    bool DriverModel8() const override { return !getenv("NEO98_D3D_DX7_DRIVER"); }
    bool DriverModel9() const override { return DriverModel8() && !getenv("NEO98_D3D_DX8_DRIVER"); }
    bool PixelShaders() const override { return !getenv("NEO98_D3D_NO_PIXEL_SHADERS"); }
    bool VertexShaders() const override { return !getenv("NEO98_D3D_NO_VERTEX_SHADERS"); }

    bool Begin(const Surface &target, const Surface *depth) override
    {
        const bool with_depth = depth && depth->bits;
        if (!target.bits || target.width <= 0 || target.height <= 0 || target.bit_count < 16)
        {
            Flush();
            batch_active = false;
            return false;
        }
        if (!SameSurface(current_target, target) || has_depth != with_depth ||
            (with_depth && !SameSurface(current_depth, *depth))) Flush();
        current_target = target;
        has_depth = with_depth;
        current_depth = with_depth ? *depth : Surface{};
        batch_active = true;
        for (unsigned i = 0; i < Stages; ++i) { bound[i] = 0; bound_texture[i] = nil; }
        ++pass_statistics.batches;
        return true;
    }

    // Defer attachment loads until the first command is known. A full clear
    // needs neither the guest's old bytes nor a load of the old GPU contents.
    bool StartPass(unsigned clear = 0, unsigned color = 0, float depth = 1, unsigned stencil = 0)
    {
        if (!batch_active) return false;
        if (encoder) return true;
        const auto &target = current_target;
        // The colour texture of this memory: made once, uploaded only when
        // the CPU wrote the bytes since the GPU last drew into them.
        Resident &r = residents[target.bits];
        if (!r.texture || r.width != target.width || r.height != target.height || !SameLayout(r.surface, target))
        {
            MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                         width:target.width height:target.height mipmapped:NO];
            d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            d.storageMode = MTLStorageModeShared;
            r.texture = [device newTextureWithDescriptor:d];
            r.width = target.width;
            r.height = target.height;
            r.cpu_newer = true;
            r.gpu_newer = false;
        }
        r.surface = target;
        if (r.cpu_newer && !(clear & 1))
        {
            WaitIdle();
            ToBgra(target, conversion);
            [r.texture replaceRegion:MTLRegionMake2D(0, 0, target.width, target.height) mipmapLevel:0
                           withBytes:conversion.data() bytesPerRow:size_t(target.width) * 4];
            r.cpu_newer = false;
        }
        if (clear & 1) r.cpu_newer = false;
        color_texture = r.texture;
        if (has_depth) depth_texture = DepthTexture(current_depth, clear);
        else depth_texture = nil;
        depth_stencil = depth_texture && depth_texture.pixelFormat == MTLPixelFormatDepth32Float_Stencil8;
        command_buffer = [queue commandBuffer];
        discard_color = discard_depth = false;
        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = color_texture;
        pass.colorAttachments[0].loadAction = (clear & 1) ? MTLLoadActionClear : MTLLoadActionLoad;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(((color >> 16) & 255) / 255.0,
            ((color >> 8) & 255) / 255.0, (color & 255) / 255.0, (color >> 24) / 255.0);
        pass.colorAttachments[0].storeAction = MTLStoreActionUnknown;
        if (depth_texture)
        {
            pass.depthAttachment.texture = depth_texture;
            pass.depthAttachment.loadAction = (clear & 2) ? MTLLoadActionClear : MTLLoadActionLoad;
            pass.depthAttachment.clearDepth = std::clamp(depth, 0.0f, 1.0f);
            pass.depthAttachment.storeAction = MTLStoreActionUnknown;
            if (depth_stencil)
            {
                pass.stencilAttachment.texture = depth_texture;
                pass.stencilAttachment.loadAction = (clear & 4) ? MTLLoadActionClear : MTLLoadActionLoad;
                pass.stencilAttachment.clearStencil = stencil & 255;
                pass.stencilAttachment.storeAction = MTLStoreActionUnknown;
            }
        }
        encoder = [command_buffer renderCommandEncoderWithDescriptor:pass];
        if (encoder) ++pass_statistics.passes;
        return encoder != nil;
    }

    void Clear(unsigned flags, unsigned color, float depth, unsigned stencil, const int *rects, unsigned rect_count,
               const Gpu::State &state) override
    {
        if (!batch_active || !(flags & (has_depth ? (current_depth.stencil_bits ? 7 : 3) : 1))) return;
        bool full_clear = false;
        if (!rect_count)
            full_clear = state.viewport_width <= 0 || state.viewport_height <= 0 ||
                (state.viewport_x == 0 && state.viewport_y == 0 &&
                 state.viewport_width == current_target.width && state.viewport_height == current_target.height);
        else for (unsigned i = 0; i < rect_count; ++i)
            if (rects[i*4] <= 0 && rects[i*4+1] <= 0 &&
                rects[i*4+2] >= current_target.width && rects[i*4+3] >= current_target.height) full_clear = true;
        if (has_depth && (flags & 6) && (current_depth.width != current_target.width ||
            current_depth.height != current_target.height)) full_clear = false;
        if (!encoder && full_clear)
        {
            if (StartPass(flags, color, depth, stencil)) ++pass_statistics.load_clears;
            return;
        }
        if (!StartPass()) return;
        ++pass_statistics.quad_clears;
        ClearUniforms u = {};
        u.color[0] = ((color >> 16) & 255) / 255.0f;
        u.color[1] = ((color >> 8) & 255) / 255.0f;
        u.color[2] = (color & 255) / 255.0f;
        u.color[3] = (color >> 24) / 255.0f;
        u.depth = depth < 0.0f ? 0.0f : depth > 1.0f ? 1.0f : depth;
        const bool clear_color = (flags & 1) != 0, clear_depth = (flags & 2) != 0 && depth_texture;
        const bool clear_stencil = (flags & 4) != 0 && depth_stencil;
        if (!clear_color && !clear_depth && !clear_stencil) return;
        id<MTLRenderPipelineState> pipeline = ClearPipeline(clear_color);
        // D3DCLEAR_STENCIL writes the value everywhere the rects reach.
        DepthStencilKey k;
        k.z_enable = clear_depth; k.z_write = clear_depth; k.z_func = 8;
        if (clear_stencil) { k.stencil = true; k.fail = k.z_fail = k.pass = 3; k.func = 8; k.read_mask = 0xff; k.write_mask = 0xff; }
        [encoder setRenderPipelineState:pipeline];
        [encoder setDepthStencilState:DepthStencil(k)];
        if (clear_stencil) [encoder setStencilReferenceValue:stencil & 0xff];
        [encoder setCullMode:MTLCullModeNone];
        [encoder setTriangleFillMode:MTLTriangleFillModeFill];
        [encoder setDepthBias:0 slopeScale:0 clamp:0];
        [encoder setVertexBytes:&u length:sizeof u atIndex:1];
        [encoder setFragmentBytes:&u length:sizeof u atIndex:0];
        MTLViewport whole = {0, 0, double(current_target.width), double(current_target.height), 0, 1};
        [encoder setViewport:whole];
        int viewport[4] = {state.viewport_x, state.viewport_y, state.viewport_width, state.viewport_height};
        if (viewport[2] <= 0 || viewport[3] <= 0) { viewport[0] = viewport[1] = 0; viewport[2] = current_target.width; viewport[3] = current_target.height; }
        const int none[4] = {viewport[0], viewport[1], viewport[0] + viewport[2], viewport[1] + viewport[3]};
        for (unsigned i = 0; i < (rect_count ? rect_count : 1); ++i)
        {
            const int *r = rect_count ? rects + i * 4 : none;
            int left = r[0] < 0 ? 0 : r[0], top = r[1] < 0 ? 0 : r[1];
            int right = r[2] > current_target.width ? current_target.width : r[2];
            int bottom = r[3] > current_target.height ? current_target.height : r[3];
            if (right <= left || bottom <= top) continue;
            MTLScissorRect scissor = {NSUInteger(left), NSUInteger(top), NSUInteger(right - left), NSUInteger(bottom - top)};
            [encoder setScissorRect:scissor];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        }
        MTLScissorRect all = {0, 0, NSUInteger(current_target.width), NSUInteger(current_target.height)};
        [encoder setScissorRect:all];
    }

    void SetTexture(unsigned stage, const Surface *texture) override
    {
        if (stage >= Stages) return;
        if (!texture || !texture->bits || texture->width <= 0 || texture->height <= 0)
        {
            bound[stage] = 0;
            bound_texture[stage] = nil;
            return;
        }
        bound[stage] = Key(texture->owner, texture->handle);
        keyed[stage] = texture->color_key;
        cube[stage] = texture->faces != nullptr;
        volume[stage] = texture->slices > 0;
        bumped[stage] = texture->bump;
        // A texture that is a render target the GPU drew into is sampled
        // where it is, unless the CPU wrote it since.
        auto found = residents.find(texture->bits);
        if (found != residents.end() && !found->second.cpu_newer && !texture->color_key && !texture->level_count && !texture->fourcc &&
            !texture->slices && !texture->faces && SameLayout(found->second.surface, *texture) &&
            found->second.width == texture->width && found->second.height == texture->height)
        {
            if (current_target.bits == texture->bits)
            {
                // A surface that is both source and destination samples the
                // completed contents, never the attachment being written.
                Flush();
                MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                    width:texture->width height:texture->height mipmapped:NO];
                d.usage = MTLTextureUsageShaderRead;
                id<MTLTexture> copy = [device newTextureWithDescriptor:d];
                id<MTLCommandBuffer> buffer = [queue commandBuffer];
                id<MTLBlitCommandEncoder> blit = [buffer blitCommandEncoder];
                [blit copyFromTexture:found->second.texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                    sourceSize:MTLSizeMake(texture->width,texture->height,1) toTexture:copy
                    destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
                [blit endEncoding]; [buffer commit]; in_flight = buffer;
                bound_texture[stage] = copy;
            }
            else bound_texture[stage] = found->second.texture;
            return;
        }
        bool changed = false;
        auto sync_level = [&](const Surface &level) {
            auto resident = residents.find(level.bits);
            if (resident != residents.end())
            {
                changed |= resident->second.gpu_newer || (encoder && current_target.bits == level.bits);
                Sync(level, false);
            }
        };
        auto sync_face = [&](const Surface &face) {
            sync_level(face);
            for (unsigned i = 0; i < face.level_count; ++i)
            {
                Surface level; level.bits = face.levels[i].bits;
                sync_level(level);
            }
        };
        if (texture->faces) for (unsigned i = 0; i < 6; ++i) sync_face(texture->faces[i]);
        else sync_face(*texture);
        if (changed) textures.erase(Key(texture->owner, texture->handle));
        bound_texture[stage] = Texture(*texture);
    }

    void Draw(const Gpu::Draw &draw, const Gpu::State &state) override
    {
        if (!draw.vertices || !draw.stride || !StartPass()) return;
        using RS = RenderState;
        const auto layout = Gpu::VertexLayout::Of(draw.fvf);
        // A vertex shader reads input registers and replaces the vertex stage.
        const VertexProgram *program = draw.shader_inputs && state.vertex_shader.tokens ? ProgramOf(state.vertex_shader) : nullptr;
        if (draw.shader_inputs && !program) return;
        const bool tnl = !program && !layout.transformed, clip = !program && layout.clip_space;
        // Pipeline: blending, fill mode, and which vertex stage.
        const bool blend = state.render[RS::AlphaBlendEnable] != 0;
        const unsigned src = state.render[RS::SrcBlend], dst = state.render[RS::DestBlend];
        const bool flat = state.render[RS::ShadeMode] == 1;   // D3DSHADE_FLAT
        // D3DZB_USEW (2): the fragment writes the W depth itself.
        const bool wbuffer = depth_texture && state.render[RS::ZEnable] == 2;
        // A pixel shader replaces the texture stages and the specular sum.
        const PixelShader *shader = state.pixel_shader.tokens ? ShaderOf(state.pixel_shader) : nullptr;
        if (state.pixel_shader.tokens && !shader) return;
        id<MTLRenderPipelineState> pipeline = Pipeline(blend, src, dst, flat, tnl ? VertexTnl : clip ? VertexClip : VertexScreen, wbuffer, shader, program, state.render[168] & 15);
        if (!pipeline) return;
        [encoder setRenderPipelineState:pipeline];
        {
            DepthStencilKey k;
            k.z_enable = depth_texture && state.render[RS::ZEnable];
            k.z_write = depth_texture && state.render[RS::ZWriteEnable];
            k.z_func = state.render[RS::ZFunc];
            if (depth_stencil && state.render[RS::StencilEnable])
            {
                k.stencil = true;
                k.fail = state.render[RS::StencilFail];
                k.z_fail = state.render[RS::StencilZFail];
                k.pass = state.render[RS::StencilPass];
                k.func = state.render[RS::StencilFunc];
                k.read_mask = state.render[RS::StencilMask] & 0xff;
                k.write_mask = state.render[RS::StencilWriteMask] & 0xff;
                [encoder setStencilReferenceValue:state.render[RS::StencilRef] & 0xff];
            }
            [encoder setDepthStencilState:DepthStencil(k)];
        }
        // D3D counts winding on the screen with y down, Metal after the flip of
        // the viewport, so a clockwise D3D triangle is clockwise here as well.
        [encoder setFrontFacingWinding:MTLWindingClockwise];
        switch (state.render[RS::CullMode])
        {
        case 2: [encoder setCullMode:MTLCullModeFront]; break;
        case 3: [encoder setCullMode:MTLCullModeBack]; break;
        default: [encoder setCullMode:MTLCullModeNone]; break;
        }
        [encoder setTriangleFillMode:state.render[RS::FillMode] == 2 ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];
        // Viewport.
        int vx = state.viewport_x, vy = state.viewport_y, vw = state.viewport_width, vh = state.viewport_height;
        if (vw <= 0 || vh <= 0) { vx = vy = 0; vw = current_target.width; vh = current_target.height; }
        if (vx < 0) vx = 0; if (vy < 0) vy = 0;
        if (vx + vw > current_target.width) vw = current_target.width - vx;
        if (vy + vh > current_target.height) vh = current_target.height - vy;
        if (vw <= 0 || vh <= 0) return;
        // The transform-and-lighting device maps its clip space onto the D3D
        // viewport with its Z range; screen-space vertices carry their own Z.
        double z_near = 0.0, z_far = 1.0;
        if ((tnl || program || clip) && state.z_max >= state.z_min && !((tnl || program) && state.z_max == state.z_min)) { z_near = state.z_min; z_far = state.z_max; }
        MTLViewport viewport = {double(vx), double(vy), double(vw), double(vh), z_near, z_far};
        [encoder setViewport:viewport];
        MTLScissorRect scissor = {NSUInteger(vx), NSUInteger(vy), NSUInteger(vw), NSUInteger(vh)};
        if (state.render[174])   // D3DRS_SCISSORTESTENABLE: the rect within the viewport
        {
            const int left = std::max(vx, state.scissor[0]), top = std::max(vy, state.scissor[1]);
            const int right = std::min(vx + vw, state.scissor[2]), bottom = std::min(vy + vh, state.scissor[3]);
            if (right <= left || bottom <= top) return;
            scissor = {NSUInteger(left), NSUInteger(top), NSUInteger(right - left), NSUInteger(bottom - top)};
        }
        [encoder setScissorRect:scissor];
        // Vertex uniforms.
        VertexUniforms vu = {};
        vu.viewport = simd_make_float4(float(vx), float(vy), float(vw), float(vh));
        vu.stride = draw.stride;
        vu.weights = layout.weights;
        vu.normal = layout.normal >= 0 ? unsigned(layout.normal) : 0xffffffffu;
        vu.diffuse = layout.diffuse >= 0 ? unsigned(layout.diffuse) : 0xffffffffu;
        vu.specular = layout.specular >= 0 ? unsigned(layout.specular) : 0xffffffffu;
        for (unsigned i = 0; i < 8; ++i)
        {
            vu.tex_offset[i] = layout.texture[i] >= 0 ? unsigned(layout.texture[i]) : 0xffffffffu;
            vu.tex_size[i] = layout.texture_size[i];
            vu.tex_index[i] = state.stage[i][TSS::TexCoordIndex];
            // Clip space vertices carry their coordinates as they are; only
            // the projection bit of the flags applies to them.
            vu.tex_flags[i] = tnl ? state.stage[i][TSS::TextureTransformFlags]
                            : clip ? state.stage[i][TSS::TextureTransformFlags] & 0x1ffu : 0;
        }
        const bool fog_on = state.render[RS::FogEnable] != 0;
        if (tnl)
        {
            // The fixed function pipeline: world times view per blend
            // matrix, the normal matrices, the projection, lights in eye
            // space.
            for (unsigned i = 0; i < 4; ++i)
            {
                float wv[16], nm[16];
                Multiply(state.world[i], state.view, wv);
                NormalMatrix(wv, nm);
                vu.world[i] = Raw(state.world[i]);
                vu.worldview[i] = Raw(wv);
                vu.normal_matrix[i] = Raw(nm);
            }
            vu.projection = Raw(state.projection);
            for (unsigned i = 0; i < 8; ++i) vu.texture[i] = Raw(state.texture[i]);
            vu.ambient = Unpack(state.render[RS::Ambient]);
            const auto &m = state.material;
            vu.mat_diffuse = Color4(m.diffuse);
            vu.mat_ambient = Color4(m.ambient);
            vu.mat_specular = Color4(m.specular);
            vu.mat_emissive = Color4(m.emissive);
            vu.mat_power = m.power;
            const float *v = state.view;
            unsigned count = 0;
            for (unsigned i = 0; i < Gpu::State::Lights && count < 8; ++i)
            {
                const auto &l = state.lights[i];
                if (!l.enabled || l.type < 1 || l.type > 3) continue;
                auto &out = vu.lights[count++];
                out.diffuse = Color4(l.diffuse);
                out.specular = Color4(l.specular);
                out.ambient = Color4(l.ambient);
                const float *p = l.position, *d = l.direction;
                const float ex = p[0] * v[0] + p[1] * v[4] + p[2] * v[8] + v[12];
                const float ey = p[0] * v[1] + p[1] * v[5] + p[2] * v[9] + v[13];
                const float ez = p[0] * v[2] + p[1] * v[6] + p[2] * v[10] + v[14];
                float dx = d[0] * v[0] + d[1] * v[4] + d[2] * v[8];
                float dy = d[0] * v[1] + d[1] * v[5] + d[2] * v[9];
                float dz = d[0] * v[2] + d[1] * v[6] + d[2] * v[10];
                const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (length > 0.0f) { dx /= length; dy /= length; dz /= length; }
                out.position = simd_make_float4(ex, ey, ez, 1.0f);
                // A directional light shines along its direction, so the
                // vector towards it is the opposite; a spot keeps its axis.
                out.direction = l.type == 3 ? simd_make_float4(-dx, -dy, -dz, 0.0f) : simd_make_float4(dx, dy, dz, 0.0f);
                const float range = l.type == 3 || l.range <= 0.0f ? 3.0e38f : l.range;
                out.p1 = simd_make_float4(range, l.falloff, l.attenuation[0], l.attenuation[1]);
                out.p2 = simd_make_float4(l.attenuation[2], std::cos(l.theta * 0.5f), std::cos(l.phi * 0.5f), float(l.type));
            }
            vu.light_count = count;
            if (state.render[RS::Lighting]) vu.flags |= FlagLighting;
            if (state.render[RS::SpecularEnable]) vu.flags |= FlagSpecular;
            if (state.render[RS::LocalViewer]) vu.flags |= FlagLocalViewer;
            if (state.render[RS::NormalizeNormals]) vu.flags |= FlagNormalize;
            if (state.render[RS::ColorVertex]) vu.flags |= FlagColorVertex;
            if (state.render[RS::RangeFogEnable]) vu.flags |= FlagRangeFog;
            // Table fog reads the eye depth when the projection keeps it in
            // W (_34 is 1, _44 is 0), the device depth otherwise.
            if (state.projection[15] == 0.0f && state.projection[11] != 0.0f) vu.flags |= FlagFogW;
            vu.sources = (state.render[RS::DiffuseMaterialSource] & 15) | (state.render[RS::SpecularMaterialSource] & 15) << 4 |
                         (state.render[RS::AmbientMaterialSource] & 15) << 8 | (state.render[RS::EmissiveMaterialSource] & 15) << 12;
            vu.fog_mode = fog_on && state.render[RS::FogTableMode] == 0 ? state.render[RS::FogVertexMode] : 0;
            float start = 0.0f, end = 1.0f, density = 1.0f;
            memcpy(&start, &state.render[RS::FogStart], 4);
            memcpy(&end, &state.render[RS::FogEnd], 4);
            memcpy(&density, &state.render[RS::FogDensity], 4);
            vu.fog = simd_make_float4(start, end, density, 0.0f);
            const unsigned blend_state = state.render[RS::VertexBlend];
            vu.blend = blend_state <= 3 ? blend_state : 0;
            vu.clip_mask = state.render[RS::Clipping] ? state.render[RS::ClipPlaneEnable] & 63 : 0;
            for (unsigned k = 0; k < 6; ++k) vu.clip_plane[k] = Color4(state.clip_plane[k]);
        }
        else if (program)
        {
            // Vertex fog with a shader: its oFog is the factor.
            if (fog_on && state.render[RS::FogTableMode] == 0) vu.flags |= FlagShaderFog;
            [encoder setVertexBytes:state.vs_constants length:sizeof state.vs_constants atIndex:2];
            [encoder setVertexBytes:state.vs_integers length:sizeof state.vs_integers atIndex:3];
            [encoder setVertexBytes:state.vs_booleans length:sizeof state.vs_booleans atIndex:4];
        }
        else if (!(state.w_near == 0.0f && state.w_far == 1.0f) && state.w_far > state.w_near)
            vu.flags |= FlagFogW;   // D3DIM named a W range: the projection keeps the eye depth in W
        // Fragment uniforms.
        FragmentUniforms fu = {};
        for (unsigned s = 0; s < Stages; ++s)
        {
            const unsigned *ts = state.stage[s];
            fu.color_op[s] = ts[TSS::ColorOp];
            fu.alpha_op[s] = ts[TSS::AlphaOp];
            fu.color_arg1[s] = ts[TSS::ColorArg1];
            fu.color_arg2[s] = ts[TSS::ColorArg2];
            fu.alpha_arg1[s] = ts[TSS::AlphaArg1];
            fu.alpha_arg2[s] = ts[TSS::AlphaArg2];
            fu.textured[s] = bound_texture[s] ? (cube[s] ? 2 : volume[s] ? 3 : 1) : 0;
            fu.signed_texels[s] = bound_texture[s] && bumped[s] ? 1 : 0;
            float m[4];
            for (unsigned k = 0; k < 4; ++k) memcpy(&m[k], &ts[TSS::BumpEnvMat00 + k], 4);
            fu.bump_matrix[s] = simd_make_float4(m[0], m[1], m[2], m[3]);
            float scale = 0.0f, offset = 0.0f;
            memcpy(&scale, &ts[TSS::BumpEnvLScale], 4);
            memcpy(&offset, &ts[TSS::BumpEnvLOffset], 4);
            fu.bump_luminance[s] = simd_make_float4(scale, offset, 0.0f, 0.0f);
            fu.keyed[s] = state.render[RS::ColorKeyEnable] && keyed[s] ? 1 : 0;
            const unsigned flags = vu.tex_flags[s];
            const unsigned components = flags & 0xff;
            fu.projected[s] = (flags & 0x100) && components >= 2 && components <= 4 ? components : 0;
            if (bound_texture[s])
                [encoder setFragmentTexture:bound_texture[s] atIndex:cube[s] ? 8 + s : volume[s] ? 16 + s : s];
            // fragment_main declares every sampler, even when guest state
            // disables a stage. Metal validates those arguments at each draw.
            [encoder setFragmentSamplerState:Sampler(ts[TSS::MagFilter], ts[TSS::MinFilter], ts[TSS::MipFilter],
                                                     ts[TSS::AddressU] ? ts[TSS::AddressU] : ts[TSS::Address],
                                                     ts[TSS::AddressV] ? ts[TSS::AddressV] : ts[TSS::Address],
                                                     ts[TSS::MaxAnisotropy], ts[TSS::AddressW])
                                    atIndex:s];
        }
        fu.w_range = simd_make_float4(state.w_near, state.w_far > state.w_near ? state.w_far : state.w_near + 1.0f, 0.0f, 0.0f);
        fu.tfactor = Unpack(state.render[RS::TextureFactor]);
        fu.fog_color = Unpack(state.render[RS::FogColor] | 0xff000000u);
        fu.alpha_test = state.render[RS::AlphaTestEnable];
        fu.alpha_func = state.render[RS::AlphaFunc];
        fu.alpha_ref = (state.render[RS::AlphaRef] & 255) / 255.0f;
        fu.fog = fog_on;
        fu.specular = shader ? 0 : state.render[RS::SpecularEnable];
        if (shader)
        {
            [encoder setFragmentBytes:state.ps_constants length:sizeof state.ps_constants atIndex:1];
            [encoder setFragmentBytes:state.ps_integers length:sizeof state.ps_integers atIndex:2];
            [encoder setFragmentBytes:state.ps_booleans length:sizeof state.ps_booleans atIndex:3];
        }
        fu.fog_mode = state.render[RS::FogTableMode];   // D3DFOG_NONE 0, EXP 1, EXP2 2, LINEAR 3
        memcpy(&fu.fog_start, &state.render[RS::FogStart], 4);
        memcpy(&fu.fog_end, &state.render[RS::FogEnd], 4);
        memcpy(&fu.fog_density, &state.render[RS::FogDensity], 4);
        // D3DRENDERSTATE_ZBIAS moves a polygon towards the viewer by 0 to 16
        // steps; Metal's bias adds to the depth, so it is negative here.
        const unsigned zbias = state.render[RS::ZBias] & 15;
        [encoder setDepthBias:zbias ? -8.0f * float(zbias) : 0.0f slopeScale:0.0f clamp:0.0f];
        [encoder setVertexBytes:&vu length:sizeof vu atIndex:1];
        [encoder setFragmentBytes:&fu length:sizeof fu atIndex:0];
        // The vertices, copied into a buffer of this batch.
        const size_t bytes = size_t(draw.vertex_count) * draw.stride;
        if (!bytes) return;
        NSUInteger vertex_offset = 0;
        id<MTLBuffer> vertex_buffer = Stage(draw.vertices, bytes, vertex_offset);
        [encoder setVertexBuffer:vertex_buffer offset:vertex_offset atIndex:0];
        // The primitive. Fans become lists; strips and lists go as they are.
        MTLPrimitiveType type = MTLPrimitiveTypeTriangle;
        switch (draw.primitive)
        {
        case Gpu::PointList: type = MTLPrimitiveTypePoint; break;
        case Gpu::LineList: type = MTLPrimitiveTypeLine; break;
        case Gpu::LineStrip: type = MTLPrimitiveTypeLineStrip; break;
        case Gpu::TriangleStrip: type = MTLPrimitiveTypeTriangleStrip; break;
        default: type = MTLPrimitiveTypeTriangle; break;
        }
        if (draw.indices)
        {
            scratch.clear();
            if (draw.primitive == Gpu::TriangleFan)
            {
                for (unsigned i = 1; i + 1 < draw.index_count; ++i)
                {
                    scratch.push_back(draw.indices[0]); scratch.push_back(draw.indices[i]); scratch.push_back(draw.indices[i + 1]);
                }
            }
            else scratch.assign(draw.indices, draw.indices + draw.index_count);
            if (scratch.empty()) return;
            for (auto &i : scratch) i = (unsigned short)(i + draw.base_vertex);
            NSUInteger index_offset = 0;
            id<MTLBuffer> index_buffer = Stage(scratch.data(), scratch.size() * 2, index_offset);
            [encoder drawIndexedPrimitives:type indexCount:scratch.size() indexType:MTLIndexTypeUInt16 indexBuffer:index_buffer indexBufferOffset:index_offset];
            return;
        }
        unsigned count = 0;
        switch (draw.primitive)
        {
        case Gpu::PointList: count = draw.primitive_count; break;
        case Gpu::LineList: count = draw.primitive_count * 2; break;
        case Gpu::LineStrip: count = draw.primitive_count + 1; break;
        case Gpu::TriangleList: count = draw.primitive_count * 3; break;
        case Gpu::TriangleStrip: count = draw.primitive_count + 2; break;
        case Gpu::TriangleFan: count = draw.primitive_count + 2; break;
        default: return;
        }
        if (draw.first_vertex + count > draw.vertex_count)
        {
            if (draw.first_vertex >= draw.vertex_count) return;
            count = draw.vertex_count - draw.first_vertex;
        }
        if (draw.primitive == Gpu::TriangleFan)
        {
            scratch.clear();
            for (unsigned i = 1; i + 1 < count; ++i)
            {
                scratch.push_back((unsigned short)draw.first_vertex);
                scratch.push_back((unsigned short)(draw.first_vertex + i));
                scratch.push_back((unsigned short)(draw.first_vertex + i + 1));
            }
            if (scratch.empty()) return;
            NSUInteger index_offset = 0;
            id<MTLBuffer> index_buffer = Stage(scratch.data(), scratch.size() * 2, index_offset);
            [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:scratch.size() indexType:MTLIndexTypeUInt16 indexBuffer:index_buffer indexBufferOffset:index_offset];
            return;
        }
        [encoder drawPrimitives:type vertexStart:draw.first_vertex vertexCount:count];
    }

    id<MTLBuffer> Stage(const void *data, size_t bytes, NSUInteger &offset)
    {
        if (bytes > ArenaBytes)
        {
            id<MTLBuffer> own = [device newBufferWithBytes:data length:bytes options:MTLResourceStorageModeShared];
            batch_arenas.push_back(own);
            offset = 0;
            return own;
        }
        arena_used = (arena_used + 15) & ~size_t(15);
        if (!arena || arena_used + bytes > ArenaBytes)
        {
            if (arena) batch_arenas.push_back(arena);
            arena = nil;
            {
                std::lock_guard<std::mutex> hold(*arena_lock);
                if (!free_arenas->empty()) { arena = free_arenas->back(); free_arenas->pop_back(); }
            }
            if (!arena) arena = [device newBufferWithLength:ArenaBytes options:MTLResourceStorageModeShared];
            arena_used = 0;
        }
        memcpy((char *)[arena contents] + arena_used, data, bytes);
        offset = arena_used;
        arena_used += bytes;
        return arena;
    }

    // The buffers of the batch go back to the pool once the GPU is done with
    // it; the one being filled goes with them, so the next batch starts on a
    // fresh one.
    void RetireArenas(id<MTLCommandBuffer> buffer)
    {
        if (arena) { batch_arenas.push_back(arena); arena = nil; arena_used = 0; }
        if (batch_arenas.empty()) return;
        auto retired = std::make_shared<std::vector<id<MTLBuffer>>>(std::move(batch_arenas));
        batch_arenas.clear();
        auto pool = free_arenas;
        auto lock = arena_lock;
        [buffer addCompletedHandler:^(id<MTLCommandBuffer>) {
            std::lock_guard<std::mutex> hold(*lock);
            for (id<MTLBuffer> b : *retired)
                if ([b length] == ArenaBytes && pool->size() < 8) pool->push_back(b);
        }];
    }

    void End() override
    {
        batch_active = false;
        // Bound retained vertex memory when a guest never presents.
        if (batch_arenas.size() >= 8) Flush();
    }

    void Flush() override
    {
        if (!encoder) return;
        [encoder setColorStoreAction:discard_color ? MTLStoreActionDontCare : MTLStoreActionStore atIndex:0];
        if (depth_texture)
        {
            [encoder setDepthStoreAction:discard_depth ? MTLStoreActionDontCare : MTLStoreActionStore];
            if (depth_stencil) [encoder setStencilStoreAction:discard_depth ? MTLStoreActionDontCare : MTLStoreActionStore];
        }
        [encoder endEncoding];
        encoder = nil;
        RetireArenas(command_buffer);
        // The picture stays on the GPU; Sync brings it into the bytes when
        // the CPU looks. The queue runs the buffers in order, so waiting for
        // the last one waits for all.
        [command_buffer commit];
        in_flight = command_buffer;
        command_buffer = nil;
        auto found = residents.find(current_target.bits);
        if (found != residents.end()) found->second.gpu_newer = true;
        if (has_depth)
        {
            auto z = depths.find(DepthKey(current_depth));
            if (z != depths.end()) z->second.gpu_newer = true;
        }
    }

    void Sync(const Surface &s, bool will_write) override
    {
        if (!s.bits) return;
        bool changed = will_write;
        if (encoder && (current_target.bits == s.bits || (has_depth && current_depth.bits == s.bits))) Flush();
        auto found = residents.find(s.bits);
        if (found != residents.end())
        {
            Resident &r = found->second;
            if (r.gpu_newer)
            {
                changed = true;
                WaitIdle();
                conversion.resize(size_t(r.width) * size_t(r.height));
                [r.texture getBytes:conversion.data() bytesPerRow:size_t(r.width) * 4
                         fromRegion:MTLRegionMake2D(0, 0, r.width, r.height) mipmapLevel:0];
                FromBgra(r.surface, conversion.data());
                r.gpu_newer = false;
            }
            if (will_write) r.cpu_newer = true;
        }
        for (auto &entry : depths)
            if (entry.second.surface.bits == s.bits)
            {
                DownloadDepth(entry.second);
                if (will_write) entry.second.cpu_newer = true;
            }
        // GDI writes and GPU readbacks need not raise the DirectDraw texture
        // generation. A converted mip or cube face must be uploaded again.
        if (changed)
            for (auto &entry : textures)
                if (std::find(entry.second.sources.begin(), entry.second.sources.end(), s.bits) != entry.second.sources.end())
                    entry.second.texture = nil;
    }

    bool CopyColor(const Surface &source, const Surface &target) override
    {
        if (!source.bits || !target.bits || source.bits == target.bits ||
            source.width <= 0 || source.height <= 0 || source.bit_count != 32 ||
            target.width != source.width || target.height != source.height ||
            !SameLayout(source, target)) return false;
        Flush();
        auto found = residents.find(source.bits);
        if (found == residents.end() || !found->second.texture || found->second.cpu_newer ||
            !SameSurface(found->second.surface, source)) return false;
        Resident &r = residents[target.bits];
        if (!r.texture || r.width != target.width || r.height != target.height || !SameLayout(r.surface, target))
        {
            MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                width:target.width height:target.height mipmapped:NO];
            d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            d.storageMode = MTLStorageModeShared;
            r.texture = [device newTextureWithDescriptor:d];
            if (!r.texture) return false;
        }
        id<MTLCommandBuffer> buffer = [queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [buffer blitCommandEncoder];
        [blit copyFromTexture:found->second.texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
            sourceSize:MTLSizeMake(source.width,source.height,1) toTexture:r.texture
            destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
        [blit endEncoding]; [buffer commit]; in_flight = buffer;
        r.surface = target;
        r.width = target.width; r.height = target.height;
        r.cpu_newer = false; r.gpu_newer = true;
        for (auto &entry : textures)
            if (std::find(entry.second.sources.begin(), entry.second.sources.end(), target.bits) != entry.second.sources.end())
                entry.second.texture = nil;
        return true;
    }

    void CpuAccess(const unsigned char *bits, bool write) override
    {
        if (!bits) return;
        // A DC can point at a row inside the surface, rather than its base.
        const auto address = reinterpret_cast<uintptr_t>(bits);
        for (auto &entry : residents)
        {
            const auto base = reinterpret_cast<uintptr_t>(entry.first);
            if (address >= base && address - base < size_t(entry.second.surface.pitch) * entry.second.height)
            {
                Sync(entry.second.surface, write);
                return;
            }
        }
    }

    void Release(const unsigned char *bits, unsigned long bytes) override
    {
        const auto base = reinterpret_cast<uintptr_t>(bits);
        auto overlaps = [&](const Surface &s) {
            const auto start = reinterpret_cast<uintptr_t>(s.bits);
            const size_t size = s.gpu_only_depth ? 1 : size_t(s.pitch) * s.height;
            return s.bits && start < base + bytes && base < start + size;
        };
        if (overlaps(current_target) || (has_depth && overlaps(current_depth)))
        {
            // These attachments are being freed, so their last tile contents
            // need no store. Other attachments of the pass still survive.
            discard_color = overlaps(current_target);
            discard_depth = has_depth && overlaps(current_depth);
            Flush(); batch_active = false;
            current_target = {}; current_depth = {}; has_depth = false;
        }
        for (auto it = residents.begin(); it != residents.end();)
            if (overlaps(it->second.surface)) it = residents.erase(it); else ++it;
        for (auto it = depths.begin(); it != depths.end();)
            if (overlaps(it->second.surface)) it = depths.erase(it); else ++it;
        for (auto it = textures.begin(); it != textures.end();)
        {
            const auto address = reinterpret_cast<uintptr_t>(it->second.bits);
            if (address >= base && address - base < bytes) it = textures.erase(it); else ++it;
        }
    }

    void Forget(unsigned owner, unsigned handle) override
    {
        const unsigned long long key = Key(owner, handle);
        if (Key(current_target.owner, current_target.handle) == key ||
            (has_depth && Key(current_depth.owner, current_depth.handle) == key))
        {
            Flush(); batch_active = false;
            current_target = {}; current_depth = {}; has_depth = false;
        }
        textures.erase(key);
        auto depth = depths.find(key);
        if (depth != depths.end()) { DownloadDepth(depth->second); depths.erase(depth); }
        for (auto it = residents.begin(); it != residents.end();)
            if (Key(it->second.surface.owner, it->second.surface.handle) == key)
            {
                // Forget also replaces handles without freeing their memory.
                Sync(it->second.surface, false);
                it = residents.erase(it);
            }
            else ++it;
        for (unsigned s = 0; s < Stages; ++s)
            if (bound[s] == key) { bound[s] = 0; bound_texture[s] = nil; }
    }

    void FillDepth(const Surface &z, const int *rect, float value) override
    {
        if (encoder) Flush();
        id<MTLTexture> texture = DepthTexture(z);
        if (!texture) return;
        const int width = int(texture.width), height = int(texture.height);
        const int left = rect[0] < 0 ? 0 : rect[0], top = rect[1] < 0 ? 0 : rect[1];
        const int right = rect[2] > width ? width : rect[2], bottom = rect[3] > height ? height : rect[3];
        if (right <= left || bottom <= top) return;
        const float depth = value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
        const bool whole = left == 0 && top == 0 && right == width && bottom == height;
        id<MTLCommandBuffer> buffer = [queue commandBuffer];
        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.depthAttachment.texture = texture;
        pass.depthAttachment.loadAction = whole ? MTLLoadActionClear : MTLLoadActionLoad;
        pass.depthAttachment.clearDepth = depth;
        pass.depthAttachment.storeAction = MTLStoreActionStore;
        const bool with_stencil = texture.pixelFormat == MTLPixelFormatDepth32Float_Stencil8;
        if (with_stencil)
        {
            // A depth fill leaves the stencil as it is.
            pass.stencilAttachment.texture = texture;
            pass.stencilAttachment.loadAction = MTLLoadActionLoad;
            pass.stencilAttachment.storeAction = MTLStoreActionStore;
        }
        id<MTLRenderCommandEncoder> e = [buffer renderCommandEncoderWithDescriptor:pass];
        if (!whole)
        {
            // Part of the buffer: a triangle over the rect at that depth,
            // written unconditionally.
            __strong id<MTLRenderPipelineState> &fill = depth_fill_pipeline[with_stencil ? 1 : 0];
            if (!fill)
            {
                MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
                d.vertexFunction = clear_vertex;
                d.depthAttachmentPixelFormat = texture.pixelFormat;
                if (with_stencil) d.stencilAttachmentPixelFormat = texture.pixelFormat;
                NSError *error = nil;
                fill = [device newRenderPipelineStateWithDescriptor:d error:&error];
                if (!fill) fprintf(stderr, "Metal depth fill pipeline: %s\n", error ? error.localizedDescription.UTF8String : "failed");
            }
            if (fill)
            {
                ClearUniforms u = {};
                u.depth = depth;
                DepthStencilKey k;
                k.z_enable = k.z_write = true;
                k.z_func = 8;
                [e setRenderPipelineState:fill];
                [e setDepthStencilState:DepthStencil(k)];
                MTLViewport all = {0, 0, double(width), double(height), 0, 1};
                [e setViewport:all];
                MTLScissorRect scissor = {NSUInteger(left), NSUInteger(top), NSUInteger(right - left), NSUInteger(bottom - top)};
                [e setScissorRect:scissor];
                [e setVertexBytes:&u length:sizeof u atIndex:1];
                [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            }
        }
        [e endEncoding];
        [buffer commit];
        in_flight = buffer;
        depths[DepthKey(z)].gpu_newer = true;
        ++pass_statistics.passes;
    }

    // The depth texture is private to the GPU: a blit brings it into a
    // buffer the CPU reads.
    void SyncDepth(const Surface &z) override { Sync(z, false); }

    bool Glide() const override { return !getenv("NEO98_NO_GLIDE"); }

    // Glide's pixel pipeline (tools/host_metal_glide.h): its own vertex and
    // fragment functions, compiled at the first draw, with the blend, colour
    // mask and depth state of the call.
    void DrawGlide(unsigned primitive, const Gpu::GlideVertex *vertices, unsigned count, const Gpu::GlideState &g) override
    {
        if (!StartPass() || !vertices || !count) return;
        if (!GlideLibrary()) return;
        const bool blend = !(g.rgb_source == 4 && g.rgb_destination == 0 && g.alpha_source == 4 && g.alpha_destination == 0);
        const unsigned key = (blend ? 1u << 31 : 0) | (g.rgb_source & 15) | (g.rgb_destination & 15) << 4 |
                             (g.alpha_source & 15) << 8 | (g.alpha_destination & 15) << 12 | (g.rgb_mask ? 1u << 16 : 0) |
                             (g.alpha_mask ? 1u << 17 : 0) | (depth_texture ? 1u << 18 : 0) | (depth_stencil ? 1u << 19 : 0);
        id<MTLRenderPipelineState> pipeline = GlidePipeline(key, blend, g);
        if (!pipeline) return;
        [encoder setRenderPipelineState:pipeline];
        {
            DepthStencilKey k;
            // GR_CMP_ counts from NEVER 0, D3DCMP_ from NEVER 1.
            k.z_enable = depth_texture && g.depth_mode != 0;
            k.z_write = depth_texture && g.depth_mode != 0 && g.depth_mask;
            k.z_func = (g.depth_function & 7) + 1;
            [encoder setDepthStencilState:DepthStencil(k)];
        }
        [encoder setCullMode:MTLCullModeNone];
        [encoder setTriangleFillMode:MTLTriangleFillModeFill];
        [encoder setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];
        MTLViewport whole = {0, 0, double(current_target.width), double(current_target.height), 0, 1};
        [encoder setViewport:whole];
        const int left = std::max(0, g.clip[0]), top = std::max(0, g.clip[1]);
        const int right = std::min(current_target.width, g.clip[2]), bottom = std::min(current_target.height, g.clip[3]);
        if (right <= left || bottom <= top) return;
        MTLScissorRect scissor = {NSUInteger(left), NSUInteger(top), NSUInteger(right - left), NSUInteger(bottom - top)};
        [encoder setScissorRect:scissor];
        HostMetalGlide::VertexUniforms vu = {};
        vu.size[0] = float(current_target.width);
        vu.size[1] = float(current_target.height);
        vu.scale = g.scale;
        HostMetalGlide::FragmentUniforms fu = {};
        fu.color[0] = g.color_function; fu.color[1] = g.color_factor; fu.color[2] = g.color_local; fu.color[3] = g.color_other;
        fu.alpha[0] = g.alpha_function; fu.alpha[1] = g.alpha_factor; fu.alpha[2] = g.alpha_local; fu.alpha[3] = g.alpha_other;
        for (unsigned i = 0; i < 2; ++i)
        {
            const auto &t = g.tmu[i];
            const bool textured = t.textured && bound_texture[i];
            fu.tmu_rgb[i][0] = t.rgb_function; fu.tmu_rgb[i][1] = t.rgb_factor;
            fu.tmu_rgb[i][2] = (t.rgb_invert ? 1u : 0u) | (t.alpha_invert ? 2u : 0u);
            fu.tmu_rgb[i][3] = textured ? 1 : 0;
            fu.tmu_alpha[i][0] = t.alpha_function; fu.tmu_alpha[i][1] = t.alpha_factor;
            fu.tmu_alpha[i][3] = t.mipmap != 0 ? 1 : 0;
            // The long side of a texture spans 256 in s or t.
            const float w = float(t.width > 0 ? t.width : 1), h = float(t.height > 0 ? t.height : 1);
            fu.tex_scale[i][0] = (w >= h ? 1.0f : h / w) / 256.0f;
            fu.tex_scale[i][1] = (h >= w ? 1.0f : w / h) / 256.0f;
            fu.tex_scale[i][2] = t.lod_bias;
            fu.tex_scale[i][3] = g.scale > 1.0f ? std::log2(g.scale) : 0.0f;
            fu.detail[i][0] = float(t.detail_bias);
            fu.detail[i][1] = float(t.detail_scale);
            fu.detail[i][2] = t.detail_max;
            // A TMU without a texture still has a binding, which it never samples.
            [encoder setFragmentTexture:textured ? bound_texture[i] : GlideBlank() atIndex:i];
            [encoder setFragmentSamplerState:GlideSampler(t) atIndex:i];
        }
        auto channel = [](unsigned c, unsigned shift) { return float((c >> shift) & 255) / 255.0f; };
        fu.constant_color[0] = channel(g.constant_color, 16); fu.constant_color[1] = channel(g.constant_color, 8);
        fu.constant_color[2] = channel(g.constant_color, 0); fu.constant_color[3] = channel(g.constant_color, 24);
        const unsigned other = g.constant_split ? g.constant_other : g.constant_color;
        fu.constant_other[0] = channel(other, 16); fu.constant_other[1] = channel(other, 8);
        fu.constant_other[2] = channel(other, 0); fu.constant_other[3] = channel(other, 24);
        fu.fog_color[0] = channel(g.fog_color, 16); fu.fog_color[1] = channel(g.fog_color, 8);
        fu.fog_color[2] = channel(g.fog_color, 0); fu.fog_color[3] = 1.0f;
        fu.flags[0] = g.color_invert ? 1 : 0;
        fu.flags[1] = g.alpha_invert ? 1 : 0;
        fu.flags[2] = (g.chroma_key ? 1u : 0u) | (g.alpha_bit_test ? 2u : 0u);
        fu.flags[3] = g.chroma_value & 0xffffff;
        fu.tests[0] = g.alpha_test & 7;
        fu.tests[1] = g.alpha_reference & 255;
        fu.tests[2] = g.fog_mode;
        fu.tests[3] = g.depth_mode;
        fu.depth[0] = g.depth_bias;
        for (unsigned i = 0; i < 64; ++i) fu.fog_table[i] = float(g.fog_table[i]);
        [encoder setVertexBytes:&vu length:sizeof vu atIndex:1];
        [encoder setFragmentBytes:&fu length:sizeof fu atIndex:0];
        NSUInteger offset = 0;
        id<MTLBuffer> buffer = Stage(vertices, size_t(count) * sizeof *vertices, offset);
        [encoder setVertexBuffer:buffer offset:offset atIndex:0];
        const MTLPrimitiveType type = primitive == Gpu::PointList ? MTLPrimitiveTypePoint
                                    : primitive == Gpu::LineList ? MTLPrimitiveTypeLine : MTLPrimitiveTypeTriangle;
        [encoder drawPrimitives:type vertexStart:0 vertexCount:count];
    }

  private:
    // The D3DRENDERSTATE_ and D3DTSS_ numbers, the same as src/win98_ddhal.h.
    struct RenderState
    {
        static constexpr unsigned ZEnable = 7, FillMode = 8, ShadeMode = 9, ZWriteEnable = 14, AlphaTestEnable = 15, SrcBlend = 19;
        static constexpr unsigned DestBlend = 20, CullMode = 22, ZFunc = 23, AlphaRef = 24, AlphaFunc = 25;
        static constexpr unsigned AlphaBlendEnable = 27, FogEnable = 28, SpecularEnable = 29, FogColor = 34;
        static constexpr unsigned FogTableMode = 35, FogStart = 36, FogEnd = 37, FogDensity = 38;
        static constexpr unsigned ColorKeyEnable = 41, ZBias = 47, RangeFogEnable = 48, TextureFactor = 60;
        static constexpr unsigned StencilEnable = 52, StencilFail = 53, StencilZFail = 54, StencilPass = 55;
        static constexpr unsigned StencilFunc = 56, StencilRef = 57, StencilMask = 58, StencilWriteMask = 59;
        static constexpr unsigned Clipping = 136, Lighting = 137, Ambient = 139, FogVertexMode = 140, ColorVertex = 141;
        static constexpr unsigned LocalViewer = 142, NormalizeNormals = 143, DiffuseMaterialSource = 145;
        static constexpr unsigned SpecularMaterialSource = 146, AmbientMaterialSource = 147, EmissiveMaterialSource = 148;
        static constexpr unsigned VertexBlend = 151, ClipPlaneEnable = 152;
    };
    struct TSS
    {
        static constexpr unsigned ColorOp = 1, ColorArg1 = 2, ColorArg2 = 3, AlphaOp = 4, AlphaArg1 = 5, AlphaArg2 = 6;
        static constexpr unsigned TexCoordIndex = 11, Address = 12, AddressU = 13, AddressV = 14, MagFilter = 16;
        static constexpr unsigned MinFilter = 17, MipFilter = 18, MaxAnisotropy = 21, TextureTransformFlags = 24, AddressW = 25;
        static constexpr unsigned BumpEnvMat00 = 7, BumpEnvLScale = 22, BumpEnvLOffset = 23;
    };
    // A surface handle is unique within its process only; the cache keys on
    // the process and the handle together.
    static unsigned long long Key(unsigned owner, unsigned handle) { return ((unsigned long long)owner << 32) | handle; }
    struct CachedTexture
    {
        id<MTLTexture> texture;
        const unsigned char *bits = nullptr;
        std::vector<const unsigned char *> sources;
        unsigned generation = 0;
        unsigned long long hash = 0;
        int width = 0, height = 0;
        unsigned levels = 0, fourcc = 0;
    };

    // Both APIs stop at 1x1x1. Surface::level_count excludes level zero;
    // do not let an excess guest attachment create an invalid descriptor.
    static unsigned MipLevels(const Surface &s)
    {
        int extent = s.width > s.height ? s.width : s.height;
        if (s.slices > extent) extent = s.slices;
        unsigned extra = 0;
        while (extent > 1) { extent /= 2; ++extra; }
        return 1 + (s.level_count < extra ? s.level_count : extra);
    }

    // A cube map: six square faces in the order Direct3D and Metal share,
    // +X, -X, +Y, -Y, +Z, -Z, each with its mip levels.
    id<MTLTexture> CubeTexture(const Surface &s, CachedTexture &entry)
    {
        if (!entry.texture || entry.width != s.width || entry.height != s.height || entry.levels != s.level_count ||
            entry.texture.textureType != MTLTextureTypeCube)
        {
            MTLTextureDescriptor *d = [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                           size:s.width mipmapped:s.level_count > 0];
            d.mipmapLevelCount = MipLevels(s);
            d.usage = MTLTextureUsageShaderRead;
            d.storageMode = MTLStorageModeShared;
            entry.texture = [device newTextureWithDescriptor:d];
            entry.width = s.width;
            entry.height = s.height;
            entry.levels = s.level_count;
            entry.fourcc = 0;
        }
        for (unsigned f = 0; f < 6; ++f)
        {
            const Surface &face = s.faces[f];
            ToBgra(face, texture_conversion);
            [entry.texture replaceRegion:MTLRegionMake2D(0, 0, face.width, face.height) mipmapLevel:0 slice:f
                               withBytes:texture_conversion.data() bytesPerRow:size_t(face.width) * 4 bytesPerImage:0];
            for (unsigned n = 0; n < face.level_count && n + 1 < (unsigned)entry.texture.mipmapLevelCount; ++n)
            {
                Surface level = face;
                level.bits = face.levels[n].bits;
                level.width = face.levels[n].width;
                level.height = face.levels[n].height;
                level.pitch = face.levels[n].pitch;
                level.level_count = 0;
                ToBgra(level, texture_conversion);
                [entry.texture replaceRegion:MTLRegionMake2D(0, 0, level.width, level.height) mipmapLevel:n + 1 slice:f
                                   withBytes:texture_conversion.data() bytesPerRow:size_t(level.width) * 4 bytesPerImage:0];
            }
        }
        entry.generation = s.generation;
        entry.hash = 1;   // a cube map is never the plain texture of the same handle
        return entry.texture;
    }

    // A volume: the slices of each level one after the other, converted
    // slice by slice.
    id<MTLTexture> VolumeTexture(const Surface &s, CachedTexture &entry)
    {
        if (!entry.texture || entry.width != s.width || entry.height != s.height || entry.levels != s.level_count ||
            entry.texture.textureType != MTLTextureType3D || (int)entry.texture.depth != s.slices)
        {
            MTLTextureDescriptor *d = [MTLTextureDescriptor new];
            d.textureType = MTLTextureType3D;
            d.pixelFormat = MTLPixelFormatBGRA8Unorm;
            d.width = s.width;
            d.height = s.height;
            d.depth = s.slices;
            d.mipmapLevelCount = MipLevels(s);
            d.usage = MTLTextureUsageShaderRead;
            d.storageMode = MTLStorageModeShared;
            entry.texture = [device newTextureWithDescriptor:d];
            entry.width = s.width;
            entry.height = s.height;
            entry.levels = s.level_count;
            entry.fourcc = 0;
        }
        for (unsigned n = 0; n < (unsigned)entry.texture.mipmapLevelCount; ++n)
        {
            Surface level = s;
            level.level_count = 0;
            if (n)
            {
                level.bits = s.levels[n - 1].bits;
                level.width = s.levels[n - 1].width;
                level.height = s.levels[n - 1].height;
                level.pitch = s.levels[n - 1].pitch;
                level.slices = s.levels[n - 1].slices;
            }
            const int slices = level.slices < 1 ? 1 : level.slices;
            for (int z = 0; z < slices && level.bits; ++z)
            {
                Surface slice = level;
                slice.bits = level.bits + size_t(z) * size_t(level.pitch) * size_t(level.height);
                ToBgra(slice, texture_conversion);
                [entry.texture replaceRegion:MTLRegionMake3D(0, 0, z, level.width, level.height, 1) mipmapLevel:n slice:0
                                   withBytes:texture_conversion.data() bytesPerRow:size_t(level.width) * 4
                               bytesPerImage:size_t(level.width) * size_t(level.height) * 4];
            }
        }
        entry.generation = s.generation;
        entry.hash = 2;   // a volume is never the plain texture of the same handle
        return entry.texture;
    }

    // A DXT texture goes to the GPU as it is: the blocks of DXT1 are BC1, of
    // DXT2 and DXT3 BC2, of DXT4 and DXT5 BC3. DXT2 and DXT4 carry colours
    // premultiplied by alpha, which the sampler returns as stored, the way the
    // cards of the time did.
    id<MTLTexture> CompressedTexture(const Surface &s, CachedTexture &entry)
    {
        const unsigned c = s.fourcc & 0xff000000u;
        const MTLPixelFormat format = c == 0x31000000u ? MTLPixelFormatBC1_RGBA
                                    : c == 0x32000000u || c == 0x33000000u ? MTLPixelFormatBC2_RGBA : MTLPixelFormatBC3_RGBA;
        const bool cube = s.faces != nullptr;
        const MTLTextureType type = cube ? MTLTextureTypeCube : MTLTextureType2D;
        if (!entry.texture || entry.width != s.width || entry.height != s.height || entry.levels != s.level_count ||
            entry.fourcc != s.fourcc || entry.texture.textureType != type)
        {
            MTLTextureDescriptor *d = cube
                ? [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:format size:s.width mipmapped:s.level_count > 0]
                : [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:s.width height:s.height mipmapped:s.level_count > 0];
            d.mipmapLevelCount = MipLevels(s);
            d.usage = MTLTextureUsageShaderRead;
            d.storageMode = MTLStorageModeShared;
            entry.texture = [device newTextureWithDescriptor:d];
            entry.width = s.width;
            entry.height = s.height;
            entry.levels = s.level_count;
            entry.fourcc = s.fourcc;
        }
        // A compressed cube still has six faces. Binding a 2D allocation
        // to a cube argument gives undefined samples, including alpha.
        for (unsigned f = 0; f < (cube ? 6u : 1u); ++f)
        {
            const Surface &face = cube ? s.faces[f] : s;
            [entry.texture replaceRegion:MTLRegionMake2D(0, 0, face.width, face.height) mipmapLevel:0 slice:f
                               withBytes:face.bits bytesPerRow:size_t(face.pitch) bytesPerImage:0];
            for (unsigned n = 0; n < face.level_count && n + 1 < (unsigned)entry.texture.mipmapLevelCount; ++n)
                [entry.texture replaceRegion:MTLRegionMake2D(0, 0, face.levels[n].width, face.levels[n].height) mipmapLevel:n + 1 slice:f
                                   withBytes:face.levels[n].bits bytesPerRow:size_t(face.levels[n].pitch) bytesPerImage:0];
        }
        entry.generation = s.generation;
        entry.hash = cube ? 1 : 0;
        return entry.texture;
    }
    struct CachedDepth
    {
        id<MTLTexture> texture;
        int width = 0, height = 0;
        Surface surface;
        bool gpu_newer = false, cpu_newer = true;
    };
    // A render target as the GPU keeps it, by the memory it belongs to.
    struct Resident
    {
        id<MTLTexture> texture;
        int width = 0, height = 0;
        Surface surface;          // the layout of the bytes, for the write back
        bool gpu_newer = false;   // drawn into since the bytes were written back
        bool cpu_newer = false;   // the CPU wrote the bytes since the upload
    };
    static bool SameSurface(const Surface &a, const Surface &b)
    {
        return a.bits == b.bits && a.owner == b.owner && a.handle == b.handle &&
            a.width == b.width && a.height == b.height && SameLayout(a, b) &&
            a.z_bits == b.z_bits && a.stencil_bits == b.stencil_bits && a.gpu_only_depth == b.gpu_only_depth;
    }
    static bool SameLayout(const Surface &a, const Surface &b)
    {
        return a.pitch == b.pitch && a.bit_count == b.bit_count && a.r_mask == b.r_mask && a.g_mask == b.g_mask &&
               a.b_mask == b.b_mask && a.a_mask == b.a_mask;
    }
    void WaitIdle()
    {
        if (!in_flight) return;
        [in_flight waitUntilCompleted];
        in_flight = nil;
    }

    id<MTLTexture> Texture(const Surface &s)
    {
        // The generation is the driver's count of writes: Lock, Unlock, a
        // blit, a texture blit or a palette change raise it, and every write
        // into video memory comes through one of those, so an unchanged
        // generation means unchanged bytes. The colour key belongs to the
        // upload, because keyed texels lose their alpha there.
        auto &entry = textures[Key(s.owner, s.handle)];
        const unsigned long long key_state = s.faces ? 1 : s.slices ? 2 : s.color_key ? (0x9e3779b97f4a7c15ull * (s.key_low + 1)) + s.key_high : 0;
        if (entry.texture && entry.width == s.width && entry.height == s.height && entry.generation == s.generation && entry.bits == s.bits &&
            entry.hash == key_state && entry.levels == s.level_count && entry.fourcc == s.fourcc)
            return entry.texture;
        // Commands retain the previous object. Never replace bytes sampled
        // by a draw that is still in the open encoder (or in flight).
        entry.texture = nil;
        entry.bits = s.bits;
        entry.sources.clear();
        auto remember = [&](const Surface &face) {
            entry.sources.push_back(face.bits);
            for (unsigned i = 0; i < face.level_count; ++i) entry.sources.push_back(face.levels[i].bits);
        };
        if (s.faces) for (unsigned i = 0; i < 6; ++i) remember(s.faces[i]);
        else remember(s);
        if (s.fourcc) return CompressedTexture(s, entry);
        if (s.faces) return CubeTexture(s, entry);
        if (s.slices) return VolumeTexture(s, entry);
        if (!entry.texture || entry.width != s.width || entry.height != s.height || entry.levels != s.level_count || entry.fourcc ||
            entry.texture.textureType != MTLTextureType2D)
        {
            entry.fourcc = 0;
            MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                         width:s.width height:s.height mipmapped:s.level_count > 0];
            d.mipmapLevelCount = MipLevels(s);
            d.usage = MTLTextureUsageShaderRead;
            d.storageMode = MTLStorageModeShared;
            entry.texture = [device newTextureWithDescriptor:d];
            entry.width = s.width;
            entry.height = s.height;
            entry.levels = s.level_count;
        }
        ToBgra(s, texture_conversion);
        [entry.texture replaceRegion:MTLRegionMake2D(0, 0, s.width, s.height) mipmapLevel:0
                           withBytes:texture_conversion.data() bytesPerRow:size_t(s.width) * 4];
        // The attached levels, converted with the format of level 0.
        for (unsigned n = 0; n < s.level_count && n + 1 < (unsigned)entry.texture.mipmapLevelCount; ++n)
        {
            Surface level = s;
            level.bits = s.levels[n].bits;
            level.width = s.levels[n].width;
            level.height = s.levels[n].height;
            level.pitch = s.levels[n].pitch;
            level.level_count = 0;
            ToBgra(level, texture_conversion);
            [entry.texture replaceRegion:MTLRegionMake2D(0, 0, level.width, level.height) mipmapLevel:n + 1
                               withBytes:texture_conversion.data() bytesPerRow:size_t(level.width) * 4];
        }
        entry.generation = s.generation;
        entry.hash = key_state;
        return entry.texture;
    }

    static unsigned long long DepthKey(const Surface &z)
    {
        return Key(z.owner, z.handle ? z.handle : 0xffffffffu);
    }
    static unsigned DepthBits(const Surface &z)
    {
        return z.g_mask ? unsigned(__builtin_popcount(z.g_mask)) : z.z_bits ? z.z_bits : z.bit_count - z.stencil_bits;
    }
    static unsigned DepthShift(const Surface &z) { return z.g_mask ? unsigned(__builtin_ctz(z.g_mask)) : 0; }
    static uint64_t DepthMask(const Surface &z) { return (uint64_t(1) << DepthBits(z)) - 1; }
    static unsigned StencilShift(const Surface &z)
    {
        return z.b_mask ? unsigned(__builtin_ctz(z.b_mask)) : DepthBits(z);
    }

    // Use separate depth and stencil planes in the transfer buffer. Metal
    // exposes both parts of D32S8 through the corresponding blit option.
    void DownloadDepth(CachedDepth &entry)
    {
        if (!entry.gpu_newer) return;
        const auto &z = entry.surface;
        if (z.gpu_only_depth || !z.bits || !z.bit_count || z.bit_count > 32 || !DepthBits(z) || DepthBits(z) > 32) return;
        const bool stencil = z.stencil_bits != 0;
        const size_t row = (size_t(z.width) * 4 + 255) & ~size_t(255);
        const size_t stencil_row = (size_t(z.width) + 255) & ~size_t(255);
        const size_t depth_bytes = row * z.height, bytes = depth_bytes + (stencil ? stencil_row * z.height : 0);
        if (!depth_read || depth_read.length < bytes) depth_read = [device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> buffer = [queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [buffer blitCommandEncoder];
        [blit copyFromTexture:entry.texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
            sourceSize:MTLSizeMake(z.width,z.height,1) toBuffer:depth_read destinationOffset:0
            destinationBytesPerRow:row destinationBytesPerImage:depth_bytes
            options:stencil ? MTLBlitOptionDepthFromDepthStencil : MTLBlitOptionNone];
        if (stencil)
            [blit copyFromTexture:entry.texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(z.width,z.height,1) toBuffer:depth_read destinationOffset:depth_bytes
                destinationBytesPerRow:stencil_row destinationBytesPerImage:stencil_row*z.height
                options:MTLBlitOptionStencilFromDepthStencil];
        [blit endEncoding]; [buffer commit]; [buffer waitUntilCompleted]; in_flight = nil;
        const auto *data = static_cast<const unsigned char *>(depth_read.contents);
        const unsigned step = (z.bit_count + 7) / 8, shift = DepthShift(z), stencil_shift = StencilShift(z);
        const uint64_t mask = DepthMask(z);
        for (int y = 0; y < z.height; ++y)
            for (int x = 0; x < z.width; ++x)
            {
                const float depth = reinterpret_cast<const float *>(data + y * row)[x];
                unsigned value = static_cast<unsigned>(std::clamp(double(depth), 0.0, 1.0) * mask + .5) << shift;
                if (stencil && stencil_shift < 32) value |= unsigned(data[depth_bytes + y * stencil_row + x]) << stencil_shift;
                memcpy(z.bits + size_t(y) * z.pitch + x * step, &value, step);
            }
        entry.gpu_newer = false;
    }

    id<MTLTexture> DepthTexture(const Surface &z, unsigned clear = 0)
    {
        auto &entry = depths[DepthKey(z)];
        const MTLPixelFormat format = z.stencil_bits ? MTLPixelFormatDepth32Float_Stencil8 : MTLPixelFormatDepth32Float;
        if (!entry.texture || !SameSurface(entry.surface, z) || entry.texture.pixelFormat != format)
        {
            DownloadDepth(entry);
            MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                width:z.width height:z.height mipmapped:NO];
            d.usage = MTLTextureUsageRenderTarget;
            d.storageMode = MTLStorageModePrivate;
            entry.texture = [device newTextureWithDescriptor:d];
            entry.width = z.width; entry.height = z.height;
            entry.cpu_newer = true; entry.gpu_newer = false;
        }
        entry.surface = z;
        if (!entry.cpu_newer) return entry.texture;
        const bool stencil = z.stencil_bits != 0;
        const bool upload_depth = !(clear & 2), upload_stencil = stencil && !(clear & 4);
        if (!z.gpu_only_depth && (upload_depth || upload_stencil) && z.bit_count && z.bit_count <= 32 && DepthBits(z) && DepthBits(z) <= 32)
        {
            const size_t row = (size_t(z.width) * 4 + 255) & ~size_t(255);
            const size_t stencil_row = (size_t(z.width) + 255) & ~size_t(255);
            const size_t depth_bytes = row * z.height, bytes = depth_bytes + (stencil ? stencil_row * z.height : 0);
            id<MTLBuffer> upload = [device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
            auto *data = static_cast<unsigned char *>(upload.contents);
            const unsigned step = (z.bit_count + 7) / 8, shift = DepthShift(z), stencil_shift = StencilShift(z);
            const uint64_t mask = DepthMask(z);
            for (int y = 0; y < z.height; ++y)
                for (int x = 0; x < z.width; ++x)
                {
                    unsigned value = 0; memcpy(&value, z.bits + size_t(y)*z.pitch + x*step, step);
                    reinterpret_cast<float *>(data + y*row)[x] = float(double((value >> shift) & mask) / double(mask));
                    if (stencil) data[depth_bytes + y*stencil_row + x] = stencil_shift < 32 ? value >> stencil_shift : 0;
                }
            id<MTLCommandBuffer> buffer = [queue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [buffer blitCommandEncoder];
            if (upload_depth)
                [blit copyFromBuffer:upload sourceOffset:0 sourceBytesPerRow:row sourceBytesPerImage:depth_bytes
                    sourceSize:MTLSizeMake(z.width,z.height,1) toTexture:entry.texture destinationSlice:0
                    destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)
                    options:stencil ? MTLBlitOptionDepthFromDepthStencil : MTLBlitOptionNone];
            if (upload_stencil)
                [blit copyFromBuffer:upload sourceOffset:depth_bytes sourceBytesPerRow:stencil_row sourceBytesPerImage:stencil_row*z.height
                    sourceSize:MTLSizeMake(z.width,z.height,1) toTexture:entry.texture destinationSlice:0
                    destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0) options:MTLBlitOptionStencilFromDepthStencil];
            [blit endEncoding]; [buffer commit]; in_flight = buffer;
        }
        entry.cpu_newer = false;
        return entry.texture;
    }

    // A translated pixel shader: its four fragment entry points (Gouraud and
    // flat, each with and without a W buffer), or none when the translation
    // or the compiler refused it.
    struct PixelShader
    {
        id<MTLFunction> main, flat, main_w, flat_w;
    };
    const PixelShader *ShaderOf(const Gpu::Shader &given)
    {
        auto found = pixel_shaders.find(given.key);
        if (found != pixel_shaders.end()) return found->second.main ? &found->second : nullptr;
        PixelShader &shader = pixel_shaders[given.key];
        std::string body, error;
        if (!Direct3D::PixelShaderToMsl(given.tokens, given.count, "shader_body", body, error))
        {
            fprintf(stderr, "Metal pixel shader %llx: %s\n", given.key, error.c_str());
            return nullptr;
        }
        const std::string source = PixelShaderSource(body);
        MTLCompileOptions *options = [MTLCompileOptions new];
        NSError *failure = nil;
        id<MTLLibrary> compiled = [device newLibraryWithSource:[NSString stringWithUTF8String:source.c_str()] options:options error:&failure];
        if (!compiled)
        {
            fprintf(stderr, "Metal pixel shader %llx: %s\n%s", given.key, failure ? failure.localizedDescription.UTF8String : "failed", body.c_str());
            return nullptr;
        }
        shader.main = [compiled newFunctionWithName:@"shader_main"];
        shader.flat = [compiled newFunctionWithName:@"shader_flat"];
        shader.main_w = [compiled newFunctionWithName:@"shader_main_w"];
        shader.flat_w = [compiled newFunctionWithName:@"shader_flat_w"];
        return shader.main ? &shader : nullptr;
    }

    // A translated vertex shader: its two vertex entry points, or none when
    // the translation or the compiler refused it.
    struct VertexProgram
    {
        id<MTLFunction> main, flat;
    };
    const VertexProgram *ProgramOf(const Gpu::Shader &given)
    {
        auto found = vertex_programs.find(given.key);
        if (found != vertex_programs.end()) return found->second.main ? &found->second : nullptr;
        VertexProgram &program = vertex_programs[given.key];
        std::string body, error;
        if (!Direct3D::VertexShaderToMsl(given.tokens, given.count, "vertex_body", body, error))
        {
            fprintf(stderr, "Metal vertex shader %llx: %s\n", given.key, error.c_str());
            return nullptr;
        }
        NSError *failure = nil;
        id<MTLLibrary> compiled = [device newLibraryWithSource:[NSString stringWithUTF8String:VertexShaderSource(body).c_str()]
                                                       options:[MTLCompileOptions new] error:&failure];
        if (!compiled)
        {
            fprintf(stderr, "Metal vertex shader %llx: %s\n%s", given.key, failure ? failure.localizedDescription.UTF8String : "failed", body.c_str());
            return nullptr;
        }
        program.main = [compiled newFunctionWithName:@"vshader_main"];
        program.flat = [compiled newFunctionWithName:@"vshader_flat"];
        return program.main ? &program : nullptr;
    }

    bool GlideLibrary()
    {
        if (glide_vertex && glide_fragment) return true;
        if (glide_failed) return false;
        NSError *error = nil;
        id<MTLLibrary> compiled = [device newLibraryWithSource:[NSString stringWithUTF8String:HostMetalGlide::ShaderSource]
                                                       options:[MTLCompileOptions new] error:&error];
        if (compiled)
        {
            glide_vertex = [compiled newFunctionWithName:@"glide_vertex"];
            glide_fragment = [compiled newFunctionWithName:@"glide_fragment"];
        }
        if (!glide_vertex || !glide_fragment)
        {
            fprintf(stderr, "Metal Glide library: %s\n", error ? error.localizedDescription.UTF8String : "no entry point");
            glide_failed = true;
            return false;
        }
        return true;
    }

    id<MTLTexture> GlideBlank()
    {
        if (glide_blank) return glide_blank;
        MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                     width:1 height:1 mipmapped:NO];
        d.usage = MTLTextureUsageShaderRead;
        d.storageMode = MTLStorageModeShared;
        glide_blank = [device newTextureWithDescriptor:d];
        const unsigned zero = 0;
        [glide_blank replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:&zero bytesPerRow:4];
        return glide_blank;
    }

    // GR_BLEND_ as Metal blend factors. Value 2 and 6 name the other
    // colour: the destination as a source factor, the source as a
    // destination factor. 15 is GR_BLEND_ALPHA_SATURATE as a source factor
    // and GR_BLEND_PREFOG_COLOR as a destination factor, which has no Metal
    // counterpart and takes the source colour.
    static MTLBlendFactor GlideBlend(unsigned f, bool destination, bool alpha)
    {
        switch (f)
        {
        case 0: return MTLBlendFactorZero;
        case 1: return MTLBlendFactorSourceAlpha;
        case 2: return destination ? (alpha ? MTLBlendFactorSourceAlpha : MTLBlendFactorSourceColor)
                                   : (alpha ? MTLBlendFactorDestinationAlpha : MTLBlendFactorDestinationColor);
        case 3: return MTLBlendFactorDestinationAlpha;
        case 4: return MTLBlendFactorOne;
        case 5: return MTLBlendFactorOneMinusSourceAlpha;
        case 6: return destination ? (alpha ? MTLBlendFactorOneMinusSourceAlpha : MTLBlendFactorOneMinusSourceColor)
                                   : (alpha ? MTLBlendFactorOneMinusDestinationAlpha : MTLBlendFactorOneMinusDestinationColor);
        case 7: return MTLBlendFactorOneMinusDestinationAlpha;
        case 15: return destination ? MTLBlendFactorSourceColor : MTLBlendFactorSourceAlphaSaturated;
        default: return MTLBlendFactorZero;
        }
    }

    id<MTLRenderPipelineState> GlidePipeline(unsigned key, bool blend, const Gpu::GlideState &g)
    {
        auto found = glide_pipelines.find(key);
        if (found != glide_pipelines.end()) return found->second;
        MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
        d.vertexFunction = glide_vertex;
        d.fragmentFunction = glide_fragment;
        d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        d.colorAttachments[0].writeMask = (g.rgb_mask ? MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue : 0) |
                                          (g.alpha_mask ? MTLColorWriteMaskAlpha : 0);
        d.colorAttachments[0].blendingEnabled = blend;
        if (blend)
        {
            d.colorAttachments[0].sourceRGBBlendFactor = GlideBlend(g.rgb_source, false, false);
            d.colorAttachments[0].destinationRGBBlendFactor = GlideBlend(g.rgb_destination, true, false);
            d.colorAttachments[0].sourceAlphaBlendFactor = GlideBlend(g.alpha_source, false, true);
            d.colorAttachments[0].destinationAlphaBlendFactor = GlideBlend(g.alpha_destination, true, true);
        }
        if (depth_texture) d.depthAttachmentPixelFormat = depth_texture.pixelFormat;
        if (depth_stencil) d.stencilAttachmentPixelFormat = depth_texture.pixelFormat;
        NSError *error = nil;
        id<MTLRenderPipelineState> state = [device newRenderPipelineStateWithDescriptor:d error:&error];
        if (!state) fprintf(stderr, "Metal Glide pipeline: %s\n", error ? error.localizedDescription.UTF8String : "failed");
        glide_pipelines[key] = state;
        return state;
    }

    // A TMU's filters and clamping. GR_MIPMAP_NEAREST_DITHER samples the
    // nearest level as well; the dither only hid the step on the card.
    id<MTLSamplerState> GlideSampler(const Gpu::GlideState::Tmu &t)
    {
        const unsigned key = (t.min_bilinear ? 1u : 0u) | (t.mag_bilinear ? 2u : 0u) | (t.mipmap ? 4u : 0u) |
                             (t.clamp_s ? 8u : 0u) | (t.clamp_t ? 16u : 0u);
        auto found = glide_samplers.find(key);
        if (found != glide_samplers.end()) return found->second;
        MTLSamplerDescriptor *d = [MTLSamplerDescriptor new];
        d.minFilter = t.min_bilinear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        d.magFilter = t.mag_bilinear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        d.mipFilter = t.mipmap ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterNotMipmapped;
        d.sAddressMode = t.clamp_s ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
        d.tAddressMode = t.clamp_t ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
        id<MTLSamplerState> state = [device newSamplerStateWithDescriptor:d];
        glide_samplers[key] = state;
        return state;
    }

    enum VertexStage : unsigned { VertexScreen, VertexTnl, VertexClip };
    id<MTLRenderPipelineState> Pipeline(bool blend, unsigned src, unsigned dst, bool flat, VertexStage stage, bool wbuffer,
                                        const PixelShader *shader = nullptr, const VertexProgram *program = nullptr, unsigned color_mask = 15)
    {
        const bool tnl = stage == VertexTnl;
        const unsigned key = (blend ? 1u << 31 : 0) | (src << 8) | dst | (depth_texture ? 1u << 30 : 0) | (flat ? 1u << 29 : 0) |
                             (tnl ? 1u << 28 : 0) | (depth_stencil ? 1u << 27 : 0) | (wbuffer ? 1u << 26 : 0) |
                             (stage == VertexClip ? 1u << 25 : 0) | (color_mask << 16);
        auto &cache = shader || program ? shader_pipelines[{shader, program}] : pipelines;
        auto found = cache.find(key);
        if (found != cache.end()) return found->second;
        MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
        d.vertexFunction = tnl ? (flat ? vertex_tnl_flat : vertex_tnl)
                         : stage == VertexClip ? (flat ? vertex_clip_flat : vertex_clip)
                         : (flat ? vertex_flat : vertex_function);
        if (program) d.vertexFunction = flat ? program->flat : program->main;
        d.fragmentFunction = wbuffer ? (flat ? fragment_flat_w : fragment_w) : (flat ? fragment_flat : fragment_function);
        if (shader) d.fragmentFunction = wbuffer ? (flat ? shader->flat_w : shader->main_w) : (flat ? shader->flat : shader->main);
        d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        // D3DCOLORWRITEENABLE uses RGBA bits 0..3; Metal's flags have
        // the reverse bit order. Alpha-only passes must preserve RGB.
        d.colorAttachments[0].writeMask = ((color_mask & 1) ? MTLColorWriteMaskRed : 0) |
                                          ((color_mask & 2) ? MTLColorWriteMaskGreen : 0) |
                                          ((color_mask & 4) ? MTLColorWriteMaskBlue : 0) |
                                          ((color_mask & 8) ? MTLColorWriteMaskAlpha : 0);
        d.colorAttachments[0].blendingEnabled = blend;
        if (blend)
        {
            d.colorAttachments[0].sourceRGBBlendFactor = BlendFactor(src, false);
            d.colorAttachments[0].sourceAlphaBlendFactor = BlendFactor(src, true);
            d.colorAttachments[0].destinationRGBBlendFactor = src == 12 ? MTLBlendFactorOneMinusSourceAlpha : src == 13 ? MTLBlendFactorSourceAlpha : BlendFactor(dst, false);
            d.colorAttachments[0].destinationAlphaBlendFactor = src == 12 ? MTLBlendFactorOneMinusSourceAlpha : src == 13 ? MTLBlendFactorSourceAlpha : BlendFactor(dst, true);
        }
        if (depth_texture) d.depthAttachmentPixelFormat = depth_texture.pixelFormat;
        if (depth_stencil) d.stencilAttachmentPixelFormat = depth_texture.pixelFormat;
        NSError *error = nil;
        id<MTLRenderPipelineState> state = [device newRenderPipelineStateWithDescriptor:d error:&error];
        if (!state) fprintf(stderr, "Metal pipeline: %s\n", error ? error.localizedDescription.UTF8String : "failed");
        cache[key] = state;
        return state;
    }

    id<MTLRenderPipelineState> ClearPipeline(bool color)
    {
        const unsigned key = (color ? 1 : 0) | (depth_texture ? 2 : 0) | (depth_stencil ? 4 : 0);
        auto found = clear_pipelines.find(key);
        if (found != clear_pipelines.end()) return found->second;
        MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
        d.vertexFunction = clear_vertex;
        d.fragmentFunction = clear_fragment;
        d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        d.colorAttachments[0].writeMask = color ? MTLColorWriteMaskAll : MTLColorWriteMaskNone;
        if (depth_texture) d.depthAttachmentPixelFormat = depth_texture.pixelFormat;
        if (depth_stencil) d.stencilAttachmentPixelFormat = depth_texture.pixelFormat;
        NSError *error = nil;
        id<MTLRenderPipelineState> state = [device newRenderPipelineStateWithDescriptor:d error:&error];
        if (!state) fprintf(stderr, "Metal clear pipeline: %s\n", error ? error.localizedDescription.UTF8String : "failed");
        clear_pipelines[key] = state;
        return state;
    }

    // The depth and stencil test of a draw, by its render state values.
    struct DepthStencilKey
    {
        bool z_enable = false, z_write = false, stencil = false;
        unsigned z_func = 8, fail = 1, z_fail = 1, pass = 1, func = 8, read_mask = 0xff, write_mask = 0xff;
        unsigned long long Value() const
        {
            return (unsigned long long)(z_enable | z_write << 1 | stencil << 2) | (unsigned long long)(z_func & 15) << 3 |
                   (unsigned long long)(fail & 15) << 7 | (unsigned long long)(z_fail & 15) << 11 |
                   (unsigned long long)(pass & 15) << 15 | (unsigned long long)(func & 15) << 19 |
                   (unsigned long long)(read_mask & 255) << 23 | (unsigned long long)(write_mask & 255) << 31;
        }
    };
    static MTLStencilOperation StencilOperation(unsigned d3d)
    {
        switch (d3d)   // D3DSTENCILOP_
        {
        case 2: return MTLStencilOperationZero;
        case 3: return MTLStencilOperationReplace;
        case 4: return MTLStencilOperationIncrementClamp;
        case 5: return MTLStencilOperationDecrementClamp;
        case 6: return MTLStencilOperationInvert;
        case 7: return MTLStencilOperationIncrementWrap;
        case 8: return MTLStencilOperationDecrementWrap;
        default: return MTLStencilOperationKeep;
        }
    }
    id<MTLDepthStencilState> DepthStencil(const DepthStencilKey &k)
    {
        const unsigned long long key = k.Value();
        auto found = depth_states.find(key);
        if (found != depth_states.end()) return found->second;
        MTLDepthStencilDescriptor *d = [MTLDepthStencilDescriptor new];
        d.depthCompareFunction = k.z_enable ? Compare(k.z_func) : MTLCompareFunctionAlways;
        d.depthWriteEnabled = k.z_enable && k.z_write;
        if (k.stencil)
        {
            MTLStencilDescriptor *st = [MTLStencilDescriptor new];
            st.stencilCompareFunction = Compare(k.func);
            st.stencilFailureOperation = StencilOperation(k.fail);
            st.depthFailureOperation = StencilOperation(k.z_fail);
            st.depthStencilPassOperation = StencilOperation(k.pass);
            st.readMask = k.read_mask;
            st.writeMask = k.write_mask;
            d.frontFaceStencil = st;
            d.backFaceStencil = st;
        }
        id<MTLDepthStencilState> state = [device newDepthStencilStateWithDescriptor:d];
        depth_states[key] = state;
        return state;
    }

    id<MTLSamplerState> Sampler(unsigned mag, unsigned min, unsigned mip, unsigned u, unsigned v, unsigned anisotropy, unsigned w)
    {
        // D3DTSS_ADDRESSW of a volume; unset it wraps.
        if (!w) w = 1;
        // D3DTFN_ANISOTROPIC (3) and D3DTFG_ANISOTROPIC (5) with
        // D3DTSS_MAXANISOTROPY, at most 16 as Metal has it.
        unsigned aniso = (mag == 5 || min == 3) ? (anisotropy < 1 ? 1 : anisotropy > 16 ? 16 : anisotropy) : 1;
        const unsigned key = (mag & 15) | ((min & 15) << 4) | ((mip & 15) << 8) | ((u & 15) << 12) | ((v & 15) << 16) | (aniso << 20) |
                             ((w & 15) << 26);
        auto found = samplers.find(key);
        if (found != samplers.end()) return found->second;
        MTLSamplerDescriptor *d = [MTLSamplerDescriptor new];
        // D3DTFG_POINT 1, LINEAR 2, FLATCUBIC 3, GAUSSIANCUBIC 4, ANISOTROPIC 5.
        d.magFilter = mag >= 2 ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        d.minFilter = min >= 2 ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        // D3DTFP_NONE 1, POINT 2, LINEAR 3; a texture without levels samples level 0 either way.
        d.mipFilter = mip == 2 ? MTLSamplerMipFilterNearest : mip == 3 ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNotMipmapped;
        d.maxAnisotropy = aniso;
        d.sAddressMode = Address(u);
        d.tAddressMode = Address(v);
        d.rAddressMode = Address(w);
        id<MTLSamplerState> state = [device newSamplerStateWithDescriptor:d];
        samplers[key] = state;
        return state;
    }

    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLLibrary> library;
    id<MTLFunction> vertex_function, fragment_function, vertex_flat, fragment_flat, vertex_tnl, vertex_tnl_flat;
    id<MTLFunction> vertex_clip, vertex_clip_flat;
    id<MTLFunction> fragment_w, fragment_flat_w;
    id<MTLFunction> clear_vertex, clear_fragment;
    id<MTLCommandBuffer> command_buffer, in_flight;
    id<MTLRenderCommandEncoder> encoder;
    // Vertices and indices of a batch go into large shared buffers, one
    // after the other; a buffer returns to the pool when the GPU has run the
    // batch that used it. A buffer per draw cost more than the draw itself
    // for a program that draws a quad per character.
    static constexpr size_t ArenaBytes = 4u << 20;
    id<MTLBuffer> arena;
    size_t arena_used = 0;
    std::vector<id<MTLBuffer>> batch_arenas;
    std::shared_ptr<std::vector<id<MTLBuffer>>> free_arenas = std::make_shared<std::vector<id<MTLBuffer>>>();
    std::shared_ptr<std::mutex> arena_lock = std::make_shared<std::mutex>();
    id<MTLTexture> color_texture, depth_texture;
    id<MTLBuffer> depth_read;
    id<MTLRenderPipelineState> depth_fill_pipeline[2];
    bool depth_stencil = false;   // the Z buffer of this batch has a stencil
    static constexpr unsigned Stages = Gpu::State::Stages;
    id<MTLTexture> bound_texture[Stages];
    unsigned long long bound[Stages] = {};
    bool keyed[Stages] = {};
    bool cube[Stages] = {};   // the texture of the stage is a cube map
    bool volume[Stages] = {}; // the texture of the stage is a volume
    bool bumped[Stages] = {}; // the texture of the stage is a bump map
    bool batch_active = false;
    bool discard_color = false, discard_depth = false;
    bool has_depth = false;
    Surface current_target, current_depth;
    std::vector<unsigned> conversion, texture_conversion;
    std::vector<unsigned short> scratch;
    std::map<unsigned long long, CachedTexture> textures;
    std::map<unsigned long long, CachedDepth> depths;
    std::map<const unsigned char *, Resident> residents;
    std::map<unsigned, id<MTLRenderPipelineState>> pipelines, clear_pipelines;
    std::map<unsigned long long, PixelShader> pixel_shaders;
    std::map<unsigned long long, VertexProgram> vertex_programs;
    std::map<std::pair<const PixelShader *, const VertexProgram *>, std::map<unsigned, id<MTLRenderPipelineState>>> shader_pipelines;
    std::map<unsigned long long, id<MTLDepthStencilState>> depth_states;
    std::map<unsigned, id<MTLSamplerState>> samplers;
    std::string name_text;
    bool compressed = false;
    id<MTLFunction> glide_vertex, glide_fragment;
    id<MTLTexture> glide_blank;
    bool glide_failed = false;
    std::map<unsigned, id<MTLRenderPipelineState>> glide_pipelines;
    std::map<unsigned, id<MTLSamplerState>> glide_samplers;
};
} // namespace

namespace HostMetal
{
unsigned long long RenderPasses() { return pass_statistics.passes; }
void LogStatistics() { pass_statistics.Log(); }

bool CompilePixelShader(const unsigned *tokens, unsigned count, char *error, unsigned error_size)
{
    std::string body, reason;
    if (!Direct3D::PixelShaderToMsl(tokens, count, "shader_body", body, reason))
    {
        snprintf(error, error_size, "translation: %s", reason.c_str());
        return false;
    }
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) { snprintf(error, error_size, "no Metal device"); return false; }
    NSError *failure = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:[NSString stringWithUTF8String:PixelShaderSource(body).c_str()]
                                                  options:[MTLCompileOptions new] error:&failure];
    if (!library || ![library newFunctionWithName:@"shader_main_w"])
    {
        snprintf(error, error_size, "%s\n%s", failure ? failure.localizedDescription.UTF8String : "no entry point", body.c_str());
        return false;
    }
    return true;
}

bool CompileVertexShader(const unsigned *tokens, unsigned count, char *error, unsigned error_size)
{
    std::string body, reason;
    if (!Direct3D::VertexShaderToMsl(tokens, count, "vertex_body", body, reason))
    {
        snprintf(error, error_size, "translation: %s", reason.c_str());
        return false;
    }
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) { snprintf(error, error_size, "no Metal device"); return false; }
    NSError *failure = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:[NSString stringWithUTF8String:VertexShaderSource(body).c_str()]
                                                  options:[MTLCompileOptions new] error:&failure];
    if (!library || ![library newFunctionWithName:@"vshader_flat"])
    {
        snprintf(error, error_size, "%s\n%s", failure ? failure.localizedDescription.UTF8String : "no entry point", body.c_str());
        return false;
    }
    return true;
}

Gpu::Backend *Install()
{
    static MetalBackend *backend = nullptr;
    static bool tried = false;
    if (tried) return backend;
    tried = true;
    if (getenv("NEO98_NO_DIRECT3D")) return nullptr;
    auto *b = new MetalBackend;
    if (!b->Open())
    {
        delete b;
        return nullptr;
    }
    backend = b;
    return backend;
}
} // namespace HostMetal
