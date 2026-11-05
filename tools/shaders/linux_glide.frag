// SPDX-License-Identifier: GPL-2.0-or-later
// Glide combine, fog and W-depth formulas shared with host_metal_glide.h.
#version 450
layout(location=0) noperspective in vec4 diffuse;
layout(location=1) noperspective in vec4 specular;
layout(location=2) noperspective in vec4 texcoord[8];
layout(location=10) in float depth;
layout(binding=0) uniform sampler2DArray images[8];
layout(std140,binding=1) uniform GlideUniforms {
    uvec4 color;          // function, factor, local, other
    uvec4 alpha;          // function, factor, local, other
    uvec4 tmu_rgb[2];     // function, factor, invert, textured
    uvec4 tmu_alpha[2];   // function, factor, invert, mipmapped
    vec4 tex_scale[2];  // s and t to normalised coordinates, LOD bias, log2 of the target scale
    vec4 detail[2];     // bias, scale, maximum, unused
    vec4 constant_color;   // color0, the local input
    vec4 constant_other;   // color1, the other input
    vec4 fog_color;
    uvec4 flags;          // colour invert, alpha invert, chroma key (bit 0) and alpha mask (bit 1), chroma value
    uvec4 tests;          // alpha function, alpha reference, fog mode, depth mode
    ivec4 depth;           // bias
    vec4 fog_table[16];
} u;
layout(location=0) out vec4 color;
float saturate(float v) { return clamp(v, 0.0, 1.0); }
vec3 saturate(vec3 v) { return clamp(v, 0.0, 1.0); }
vec4 saturate(vec4 v) { return clamp(v, 0.0, 1.0); }
vec3 glide_function(uint f, vec3 factor, vec3 local, vec3 other, float local_alpha)
{
    switch (f)
    {
    case 0x1: return local;
    case 0x2: return vec3(local_alpha);
    case 0x3: return factor * other;
    case 0x4: return factor * other + local;
    case 0x5: return factor * other + local_alpha;
    case 0x6: return factor * (other - local);
    case 0x7: return factor * (other - local) + local;
    case 0x8: return factor * (other - local) + local_alpha;
    case 0x9: return -factor * local + local;
    case 0x10: return -factor * local + local_alpha;
    default: return vec3(0.0);
    }
}

// GR_COMBINE_FACTOR_: the low three bits pick the value, bit 3 takes one
// minus it. Value 5 is the texture colour in the FBI and the fraction of the
// level of detail in a TMU; value 4 is the texture alpha in the FBI and the
// detail factor in a TMU.
vec3 glide_factor(uint f, vec3 local, float other_alpha, float local_alpha, vec3 value4, vec3 value5)
{
    vec3 v;
    switch (f & 7)
    {
    case 1: v = local; break;
    case 2: v = vec3(other_alpha); break;
    case 3: v = vec3(local_alpha); break;
    case 4: v = value4; break;
    case 5: v = value5; break;
    default: v = vec3(0.0); break;
    }
    return ((f & 8u) != 0u) ? 1.0 - v : v;
}

bool glide_compare(uint function, float a, float b)
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
vec4 glide_tmu(uint i, vec3 tex, vec4 upstream,
                        sampler2DArray t)
{
    vec4 texel = vec4(0.0);
    float lod = 0.0;
    if (u.tmu_rgb[i].w != 0 && tex.z != 0.0)
    {
        const vec2 uv = tex.xy / tex.z * u.tex_scale[i].xy;
        if (u.tmu_alpha[i].w != 0)
        {
            // The card chose the level at the program's resolution; the
            // target has 2^w times as many pixels in each direction.
            const float lod_bias = u.tex_scale[i].z + u.tex_scale[i].w;
            lod = textureQueryLod(t, uv).y + lod_bias;
            texel = texture(t, vec3(uv, 0.0), lod_bias);
        }
        else texel = textureLod(t, vec3(uv, 0.0), 0.0);
    }
    const float detail = clamp((u.detail[i].x - lod * 4.0) * exp2(u.detail[i].y) / 255.0, 0.0, u.detail[i].z);
    const float fraction = fract(lod);
    const vec3 rgb_factor = glide_factor(u.tmu_rgb[i].y, texel.rgb, upstream.a, texel.a, vec3(detail), vec3(fraction));
    const float alpha_factor = glide_factor(u.tmu_alpha[i].y, vec3(texel.a), upstream.a, texel.a, vec3(detail), vec3(fraction)).x;
    vec3 rgb = glide_function(u.tmu_rgb[i].x, rgb_factor, texel.rgb, upstream.rgb, texel.a);
    float alpha = glide_function(u.tmu_alpha[i].x, vec3(alpha_factor), vec3(texel.a), vec3(upstream.a), texel.a).x;
    rgb = saturate(rgb);
    alpha = saturate(alpha);
    if ((u.tmu_rgb[i].z & 1u) != 0u) rgb = 1.0 - rgb;
    if ((u.tmu_rgb[i].z & 2u) != 0u) alpha = 1.0 - alpha;
    return vec4(rgb, alpha);
}

// The W buffer value of a pixel: a 4.12 floating point number of 1/w, so
// that 1/w = 2^-e * (1 - m/8192) with m the twelve bit mantissa. It grows
// with w; 0xFFFF is the farthest.
float glide_wdepth(float oow)
{
    if (oow >= 1.0) return 0.0;
    if (oow <= 0.0) return 65535.0;
    const float e = floor(-log2(oow));
    if (e >= 16.0) return 65535.0;
    const float mantissa = 1.0 - oow * exp2(e);   // 0 to 0.5
    return min(65535.0, e * 4096.0 + floor(mantissa * 8192.0));
}

void main()
{
    const vec4 iterated = saturate(diffuse / 255.0);
    const float z = clamp(depth, 0.0, 65535.0);
    // TMU 1 has nothing above it; TMU 0 takes its output as other.
    const vec4 upstream = glide_tmu(1u, texcoord[1].xyz, vec4(0.0), images[1]);
    const vec4 texture = glide_tmu(0u, texcoord[0].xyz, upstream, images[0]);
    // GR_COMBINE_LOCAL_ and _OTHER_ of both FBI units.
    // Local 2 is the override of fbzColorPath: the top bit of the texture
    // alpha picks the constant.
    const vec3 color_local = u.color.z == 1 ? u.constant_color.rgb
                             : u.color.z == 2 ? (texture.a >= 0.5 ? u.constant_color.rgb : iterated.rgb) : iterated.rgb;
    const vec3 color_other = u.color.w == 1 ? texture.rgb : u.color.w == 2 ? u.constant_other.rgb : iterated.rgb;
    const float alpha_local = u.alpha.z == 1 ? u.constant_color.a : u.alpha.z == 2 ? floor(z / 256.0) / 255.0 : iterated.a;
    const float alpha_other = u.alpha.w == 1 ? texture.a : u.alpha.w == 2 ? u.constant_other.a : iterated.a;
    // The alpha mask drops a pixel whose alpha other input has a clear low bit.
    if ((u.flags.z & 2) != 0 && (uint(round(alpha_other * 255.0)) & 1) == 0) discard;
    // The chroma key compares the colour other input, in eight bits.
    if ((u.flags.z & 1) != 0)
    {
        const uvec3 c = uvec3(round(color_other * 255.0));
        const uint key = u.flags.w;
        if (c.r == ((key >> 16) & 255) && c.g == ((key >> 8) & 255) && c.b == (key & 255)) discard;
    }
    const vec3 color_factor = glide_factor(u.color.y, color_local, alpha_other, alpha_local, vec3(texture.a), texture.rgb);
    vec3 rgb = saturate(glide_function(u.color.x, color_factor, color_local, color_other, alpha_local));
    const float alpha_factor = glide_factor(u.alpha.y, vec3(alpha_local), alpha_other, alpha_local, vec3(texture.a), vec3(texture.a)).x;
    float alpha = saturate(glide_function(u.alpha.x, vec3(alpha_factor), vec3(alpha_local), vec3(alpha_other), alpha_local).x);
    if (u.flags.x != 0) rgb = 1.0 - rgb;
    if (u.flags.y != 0) alpha = 1.0 - alpha;
    // The alpha test, in eight bits against the reference.
    if (!glide_compare(u.tests.x, round(alpha * 255.0), float(u.tests.y))) discard;
    const float wdepth = glide_wdepth(specular.x);
    // Fog: GR_FOG_WITH_ITERATED_ALPHA 1, _WITH_TABLE 2, _WITH_ITERATED_Z 3;
    // GR_FOG_MULT2 leaves the colour out, GR_FOG_ADD2 the fog colour.
    const uint fog = u.tests.z;
    if ((fog & 0xff) == 4)
    {
        // The constant fog of fogMode: the fog colour itself.
        rgb = ((fog & 0x100u) != 0u) ? u.fog_color.rgb : rgb + u.fog_color.rgb;
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
        const vec3 add = ((fog & 0x200u) != 0u) ? vec3(0.0) : u.fog_color.rgb;
        rgb = ((fog & 0x100u) != 0u) ? f * add : rgb + f * (add - rgb);
    }
    color = vec4(saturate(rgb), alpha);
    // GR_DEPTHBUFFER_ZBUFFER 1, _WBUFFER 2, and the two compare-to-bias modes.
    const uint mode = u.tests.w;
    const float value = (mode == 2 || mode == 4) ? wdepth : z;
    gl_FragDepth = clamp(value + float(u.depth.x), 0.0, 65535.0) / 65535.0;
}
