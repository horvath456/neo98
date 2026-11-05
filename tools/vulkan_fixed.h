// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../src/gpu_backend.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace HostVulkan::Fixed
{
struct Vec
{
    float v[4] = {};
    float &operator[](unsigned i) { return v[i]; }
    float operator[](unsigned i) const { return v[i]; }
};
inline Vec Load(const float *p)
{
    Vec r;
    memcpy(r.v, p, sizeof r.v);
    return r;
}
inline Vec Color(unsigned c)
{
    return {{float((c >> 16) & 255) / 255, float((c >> 8) & 255) / 255, float(c & 255) / 255,
             float(c >> 24) / 255}};
}
inline Vec Add(Vec a, Vec b)
{
    for (unsigned i = 0; i < 4; ++i)
        a[i] += b[i];
    return a;
}
inline Vec Mul(Vec a, float b)
{
    for (float &v : a.v)
        v *= b;
    return a;
}
inline Vec Product(Vec a, Vec b)
{
    for (unsigned i = 0; i < 4; ++i)
        a[i] *= b[i];
    return a;
}
inline float Dot(Vec a, Vec b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline Vec Normal(Vec a)
{
    float n = std::sqrt(Dot(a, a));
    return n > 0 ? Mul(a, 1 / n) : a;
}
inline Vec Transform(Vec p, const float *m)
{
    Vec r;
    for (unsigned j = 0; j < 4; ++j)
        for (unsigned i = 0; i < 4; ++i)
            r[j] += p[i] * m[i * 4 + j];
    return r;
}
inline void Multiply(const float *a, const float *b, float *r)
{
    for (unsigned i = 0; i < 4; ++i)
        for (unsigned j = 0; j < 4; ++j)
        {
            r[i * 4 + j] = 0;
            for (unsigned k = 0; k < 4; ++k)
                r[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j];
        }
}
inline Vec TransformNormal(Vec n, const float *m)
{
    // Inverse transpose of the world-view 3x3, including nonuniform scales.
    const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::abs(det) < 1e-20f)
        return n;
    return {{(n[0] * (e * i - f * h) + n[1] * (c * h - b * i) + n[2] * (b * f - c * e)) / det,
             (n[0] * (f * g - d * i) + n[1] * (a * i - c * g) + n[2] * (c * d - a * f)) / det,
             (n[0] * (d * h - e * g) + n[1] * (b * g - a * h) + n[2] * (a * e - b * d)) / det, 0}};
}
inline float Float(unsigned bits)
{
    float r;
    memcpy(&r, &bits, 4);
    return r;
}
inline float Fog(unsigned mode, float depth, const Gpu::State &s)
{
    const float start = Float(s.render[36]), end = Float(s.render[37]), density = Float(s.render[38]);
    float f = mode == 3   ? (end - depth) / std::max(end - start, 1e-6f)
              : mode == 1 ? std::exp(-density * depth)
                          : std::exp(-density * density * depth * depth);
    return std::clamp(f, 0.0f, 1.0f);
}
struct Vertex
{
    Vec position, color, specular, tex[8];
    float depth = 0, point_size = 1, clip[6] = {1, 1, 1, 1, 1, 1};
};
struct Prepared
{
    float worldview[4][16] = {}, normals[4][16] = {};
    Vec light_position[Gpu::State::Lights], light_direction[Gpu::State::Lights];
    unsigned light_indices[Gpu::State::Lights] = {}, light_count = 0;
    explicit Prepared(const Gpu::State &s)
    {
        for (unsigned i = 0; i < 4; ++i)
        {
            Multiply(s.world[i], s.view, worldview[i]);
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                Vec basis;
                basis[axis] = 1;
                Vec n = TransformNormal(basis, worldview[i]);
                for (unsigned j = 0; j < 3; ++j)
                    normals[i][axis * 4 + j] = n[j];
            }
        }
        for (unsigned i = 0; i < Gpu::State::Lights; ++i)
        {
            const auto &l = s.lights[i];
            if (!l.enabled || l.type < 1 || l.type > 3)
                continue;
            light_indices[light_count++] = i;
            Vec p = {{l.position[0], l.position[1], l.position[2], 1}}, d;
            memcpy(d.v, l.direction, 12);
            light_position[i] = Transform(p, s.view);
            light_direction[i] = Normal(Transform(d, s.view));
        }
    }
};
inline Vec ReadColor(const unsigned char *p, int offset, Vec fallback)
{
    if (offset < 0)
        return fallback;
    unsigned c;
    memcpy(&c, p + offset, 4);
    return Color(c);
}
inline Vec Source(unsigned source, Vec material, const Gpu::VertexLayout &f, Vec diffuse, Vec specular)
{
    return source == 1 && f.diffuse >= 0 ? diffuse : source == 2 && f.specular >= 0 ? specular : material;
}
inline Vertex Convert(const unsigned char *p, const Gpu::VertexLayout &f, const Gpu::State &s, int width,
                      int height, const Prepared *prepared = nullptr, unsigned texture_stages = 8,
                      unsigned texture_transforms = 0xff)
{
    Vertex o;
    Vec position = {{0, 0, 0, 1}}, world, eye, normal;
    memcpy(position.v, p, 12);
    if (f.transformed)
        memcpy(&position[3], p + 12, 4);
    o.color = ReadColor(p, f.diffuse, {{1, 1, 1, 1}});
    o.specular = ReadColor(p, f.specular, {{0, 0, 0, 1}});
    const float vw = float(s.viewport_width ? s.viewport_width : width),
                vh = float(s.viewport_height ? s.viewport_height : height);
    if (f.clip_space)
    {
        o.position = position;
        o.position[1] = -o.position[1];
        o.depth = position[3] ? position[2] / position[3] : 0;
    }
    else if (f.transformed)
    {
        const float w = position[3] ? 1 / position[3] : 1;
        o.position = {{((position[0] - s.viewport_x + 0.5f) * 2 / vw - 1) * w,
                       ((position[1] - s.viewport_y + 0.5f) * 2 / vh - 1) * w, position[2] * w, w}};
        o.depth = ((s.projection[15] == 0 && s.projection[11] != 0) || s.w_far > 1) ? w : position[2];
    }
    else
    {
        Vec n;
        if (f.normal >= 0)
            memcpy(n.v, p + f.normal, 12);
        const unsigned blend = s.render[151] <= 3 ? std::min(s.render[151], std::min(f.weights, 3u)) : 0;
        float left = 1;
        for (unsigned i = 0; i <= blend; ++i)
        {
            float weight = left;
            if (i < blend)
            {
                memcpy(&weight, p + 12 + i * 4, 4);
                left -= weight;
            }
            float wv_storage[16];
            const float *wv = prepared ? prepared->worldview[i] : wv_storage;
            if (!prepared)
                Multiply(s.world[i], s.view, wv_storage);
            world = Add(world, Mul(Transform(position, s.world[i]), weight));
            eye = Add(eye, Mul(Transform(position, wv), weight));
            normal = Add(normal,
                         Mul(prepared ? Transform(n, prepared->normals[i]) : TransformNormal(n, wv), weight));
        }
        if (s.render[143])
            normal = Normal(normal);
        if (s.render[137])
        {
            const bool cv = s.render[141];
            Vec md = cv ? Source(s.render[145], Load(s.material.diffuse), f, o.color, o.specular)
                        : Load(s.material.diffuse);
            Vec ms = cv ? Source(s.render[146], Load(s.material.specular), f, o.color, o.specular)
                        : Load(s.material.specular);
            Vec ma = cv ? Source(s.render[147], Load(s.material.ambient), f, o.color, o.specular)
                        : Load(s.material.ambient);
            Vec me = cv ? Source(s.render[148], Load(s.material.emissive), f, o.color, o.specular)
                        : Load(s.material.emissive);
            Vec ambient = Color(s.render[139]), diffuse, specular;
            Vec viewer = s.render[142] ? Normal(Mul(eye, -1)) : Vec{{0, 0, -1, 0}};
            for (unsigned light = 0; light < (prepared ? prepared->light_count : Gpu::State::Lights); ++light)
            {
                const unsigned i = prepared ? prepared->light_indices[light] : light;
                const auto &l = s.lights[i];
                if (!l.enabled || l.type < 1 || l.type > 3)
                    continue;
                Vec dir;
                memcpy(dir.v, l.direction, 12);
                dir = prepared ? prepared->light_direction[i] : Normal(Transform(dir, s.view));
                Vec to = Mul(dir, -1);
                float strength = 1;
                if (l.type != 3)
                {
                    Vec lp = {{l.position[0], l.position[1], l.position[2], 1}};
                    Vec delta =
                        Add(prepared ? prepared->light_position[i] : Transform(lp, s.view), Mul(eye, -1));
                    float distance = std::sqrt(Dot(delta, delta));
                    if (l.range > 0 && distance > l.range)
                        continue;
                    to = distance > 0 ? Mul(delta, 1 / distance) : Vec{{0, 0, -1, 0}};
                    float attenuation = l.attenuation[0] + l.attenuation[1] * distance +
                                        l.attenuation[2] * distance * distance;
                    strength = attenuation > 0 ? 1 / attenuation : 1;
                    if (l.type == 2)
                    {
                        float rho = Dot(Mul(to, -1), dir), outer = std::cos(l.phi * 0.5f),
                              inner = std::cos(l.theta * 0.5f);
                        if (rho <= outer)
                            continue;
                        if (rho < inner)
                            strength *= std::pow((rho - outer) / std::max(inner - outer, 1e-6f), l.falloff);
                    }
                }
                float ndl = std::max(Dot(normal, to), 0.0f);
                ambient = Add(ambient, Mul(Load(l.ambient), strength));
                diffuse = Add(diffuse, Mul(Load(l.diffuse), ndl * strength));
                if (s.render[29] && ndl > 0)
                    specular = Add(
                        specular,
                        Mul(Load(l.specular),
                            std::pow(std::max(Dot(normal, Normal(Add(to, viewer))), 0.0f), s.material.power) *
                                strength));
            }
            o.color = Add(me, Add(Product(ambient, ma), Product(diffuse, md)));
            o.color[3] = md[3];
            o.specular = Product(specular, ms);
            o.specular[3] = 1;
            for (float &c : o.color.v)
                c = std::clamp(c, 0.0f, 1.0f);
            for (float &c : o.specular.v)
                c = std::clamp(c, 0.0f, 1.0f);
        }
        if (s.render[28] && !s.render[35] && s.render[140])
            o.specular[3] = Fog(s.render[140], s.render[48] ? std::sqrt(Dot(eye, eye)) : std::abs(eye[2]), s);
        o.position = Transform(eye, s.projection);
        o.depth = s.projection[15] == 0 && s.projection[11] != 0 ? eye[2]
                  : o.position[3]                                ? o.position[2] / o.position[3]
                                                                 : 0;
        o.position[0] += o.position[3] / vw;
        o.position[1] = -o.position[1] + o.position[3] / vh;
        if (s.render[136])
            for (unsigned i = 0; i < 6; ++i)
                if (s.render[152] & (1u << i))
                {
                    o.clip[i] = 0;
                    for (unsigned j = 0; j < 4; ++j)
                        o.clip[i] += world[j] * s.clip_plane[i][j];
                }
    }
    if (f.point_size >= 0)
        memcpy(&o.point_size, p + f.point_size, 4);
    for (unsigned stage = 0; stage < texture_stages; ++stage)
    {
        unsigned index = s.stage[stage][11];
        Vec t = {{0, 0, 0, 1}};
        if (!f.transformed && (index >> 16) == 1)
            t = normal;
        else if (!f.transformed && (index >> 16) == 2)
            t = eye;
        else if (!f.transformed && (index >> 16) == 3)
        {
            Vec e = Normal(eye);
            t = Add(e, Mul(normal, -2 * Dot(e, normal)));
        }
        else if (f.texture[index & 7] >= 0)
            memcpy(t.v, p + f.texture[index & 7], std::min(f.texture_size[index & 7], 4u) * 4);
        if ((s.stage[stage][24] & 255) && (texture_transforms & (1u << stage)))
            t = Transform(t, s.texture[stage]);
        o.tex[stage] = t;
    }
    return o;
}
} // namespace HostVulkan::Fixed
