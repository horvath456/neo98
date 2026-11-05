// SPDX-License-Identifier: GPL-2.0-or-later
// The 3dfx Voodoo of a DOS machine, drawn by Gpu::Backend. src/dos_voodoo.h.
// The registers of the card and the calls of Glide describe the same pixel
// pipeline: Glide is the library that wrote these registers. So a command of
// the card becomes the Gpu::GlideState the Glide driver of the Windows side
// builds from grColorCombine and its neighbours, and the same shader draws
// both. The bit fields below are those of the SST-1 register set
// (fbzColorPath, fbzMode, alphaMode, fogMode, textureMode, tLOD, tDetail).
//
#include "dos_voodoo.h"
#include "dos_vm.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace DosVoodoo
{
namespace
{
constexpr unsigned Bits(unsigned value, unsigned shift, unsigned mask) { return (value >> shift) & mask; }

// One combine unit of the card computes
// (zero_other ? 0 : other) - (sub_local ? local : 0)) * factor + add
// with add nothing, the local colour or the local alpha. Each of the twelve
// cases is one GR_COMBINE_FUNCTION_ of Glide; the two that subtract the local
// colour from nothing and add nothing back come out below zero, which the
// card clamps to GR_COMBINE_FUNCTION_ZERO.
unsigned Function(bool zero_other, bool sub_local, unsigned add)
{
    if (zero_other)
    {
        if (sub_local) return add == 1 ? 0x9 : add == 2 ? 0x10 : 0x0;
        return add == 1 ? 0x1 : add == 2 ? 0x2 : 0x0;
    }
    if (sub_local) return add == 1 ? 0x7 : add == 2 ? 0x8 : 0x6;
    return add == 1 ? 0x4 : add == 2 ? 0x5 : 0x3;
}

// The factor: the mselect field picks the value as GR_COMBINE_FACTOR_ counts
// them, and the card takes one minus it unless reverse_blend is set, which is
// bit 3 of the Glide value.
unsigned Factor(unsigned select, bool reverse) { return (select & 7) | (reverse ? 0u : 8u); }

int Signed6(unsigned value) { return int(value & 0x3f) - ((value & 0x20) ? 64 : 0); }

void Describe(Gpu::Surface &s, unsigned *bits, int width, int height, unsigned owner, unsigned handle)
{
    s = Gpu::Surface{};
    s.bits = reinterpret_cast<unsigned char *>(bits);
    s.width = width;
    s.height = height;
    s.pitch = width * 4;
    s.bit_count = 32;
    s.r_mask = 0xff0000;
    s.g_mask = 0xff00;
    s.b_mask = 0xff;
    s.a_mask = 0xff000000u;
    s.owner = owner;
    s.handle = handle;
}
} // namespace

static_assert(sizeof(DosVoodooVertex) == sizeof(Gpu::GlideVertex), "a vertex of the card is a vertex of the backend");

Renderer::Renderer(Gpu::Backend &gpu, unsigned owner) : gpu(gpu), owner(owner)
{
    for (auto &channel : gamma)
        for (unsigned i = 0; i < 256; ++i) channel[i] = (unsigned char)i;
}

Renderer::~Renderer() { Drop(); }

// The buffers and textures of the card leave the backend.
void Renderer::Drop()
{
    for (auto &b : color)
    {
        if (!b.pixels.empty()) gpu.Release(b.surface.bits, b.pixels.size() * 4);
        b.pixels.clear();
        b.pixels.shrink_to_fit();
    }
    if (!depth_bytes.empty()) gpu.Forget(owner, depth.handle);
    depth_bytes.clear();
    depth_bytes.shrink_to_fit();
    for (auto &t : textures)
        if (t.surface.handle) gpu.Forget(owner, t.surface.handle);
    textures.clear();
    width = height = 0;
}

// New buffers for a new resolution of the card or of the target. When only
// the multiple changes (the window takes or leaves the whole display), the
// colour buffers keep their pictures, resampled: a program that drew a menu
// once does not draw it again. The depth starts anew.
void Renderer::Resize(unsigned new_width, unsigned new_height, unsigned new_scale)
{
    const bool keep = new_width == width && new_height == height && !color[0].pixels.empty();
    const unsigned old_scale = scale;
    std::vector<unsigned> kept[3];
    for (unsigned i = 0; i < 3; ++i)
    {
        Buffer &b = color[i];
        if (b.pixels.empty()) continue;
        if (keep) gpu.Sync(b.surface, false);
        gpu.Release(b.surface.bits, b.pixels.size() * 4);
        if (keep) kept[i].swap(b.pixels);
    }
    if (!depth_bytes.empty()) gpu.Forget(owner, depth.handle);
    width = new_width;
    height = new_height;
    scale = new_scale;
    const int target_width = int(width * scale), target_height = int(height * scale);
    for (unsigned i = 0; i < 3; ++i)
    {
        Buffer &b = color[i];
        b.pixels.assign(size_t(target_width) * size_t(target_height), 0xff000000u);
        if (keep)
            for (int y = 0; y < target_height; ++y)
            {
                const unsigned *from = kept[i].data() + size_t(unsigned(y) * old_scale / scale) * (width * old_scale);
                unsigned *to = b.pixels.data() + size_t(y) * size_t(target_width);
                for (int x = 0; x < target_width; ++x) to[x] = from[unsigned(x) * old_scale / scale];
            }
        Describe(b.surface, b.pixels.data(), target_width, target_height, owner, next_handle++);
        // The bytes are new: the GPU takes them at the first Begin.
        gpu.Sync(b.surface, true);
    }
    depth_bytes.assign(size_t(target_width) * size_t(target_height), 0xffff);
    depth = Gpu::Surface{};
    depth.bits = reinterpret_cast<unsigned char *>(depth_bytes.data());
    depth.width = target_width;
    depth.height = target_height;
    depth.pitch = target_width * 2;
    depth.bit_count = 16;
    depth.depth = true;
    depth.z_bits = 16;
    depth.owner = owner;
    depth.handle = next_handle++;
    picture.assign(size_t(width) * size_t(height), 0);
    full.clear();
    full_made = false;
    // The clip rectangle of the card stays what the program set.
    if (keep)
        for (int &edge : clip) edge = int(unsigned(edge) / old_scale * scale);
    else { clip[0] = clip[1] = 0; clip[2] = target_width; clip[3] = target_height; }
}

// A texture the machine decoded. Its index may have held other texels; the
// generation tells the backend to take the new ones.
void Renderer::Upload(const DosVoodooTexture &in)
{
    if (!in.texels || !in.width || !in.height || in.width > 1024 || in.height > 1024 || !in.levels || in.index > 65535)
    { healthy = false; return; }
    if (in.index >= textures.size()) textures.resize(in.index + 1);
    Texture &t = textures[in.index];
    size_t count = 0;
    unsigned levels = 0;
    for (unsigned w = in.width, h = in.height; levels < in.levels && levels <= Gpu::Surface::MaxLevels;)
    {
        count += size_t(w) * size_t(h);
        ++levels;
        if (w == 1 && h == 1) break;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    const unsigned handle = t.surface.handle ? t.surface.handle : next_handle++;
    const unsigned generation = t.surface.generation + 1;
    t.texels.assign(in.texels, in.texels + count);
    t.levels = levels;
    Describe(t.surface, t.texels.data(), int(in.width), int(in.height), owner, handle);
    t.surface.generation = generation;
    size_t at = size_t(in.width) * size_t(in.height);
    for (unsigned l = 1, w = in.width, h = in.height; l < levels; ++l)
    {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        auto &level = t.surface.levels[t.surface.level_count++];
        level.bits = reinterpret_cast<unsigned char *>(t.texels.data() + at);
        level.width = int(w);
        level.height = int(h);
        level.pitch = int(w) * 4;
        at += size_t(w) * size_t(h);
    }
}

// The registers of a command as the state of the pixel pipeline.
void Renderer::State(const DosVoodooCommand &c, Gpu::GlideState &s) const
{
    s = Gpu::GlideState{};
    const unsigned path = c.color_path, fbz = c.fbz_mode, alpha = c.alpha_mode, fog = c.fog_mode;

    // fbzColorPath: the colour and alpha combine units of the FBI.
    const unsigned color_add = Bits(path, 14, 3), alpha_add = Bits(path, 23, 3);
    s.color_function = Function(Bits(path, 8, 1), Bits(path, 9, 1), color_add == 1 ? 1 : color_add == 2 ? 2 : 0);
    s.color_factor = Factor(Bits(path, 10, 7), Bits(path, 13, 1));
    s.color_local = Bits(path, 7, 1) ? 2 : Bits(path, 4, 1);
    s.color_other = Bits(path, 0, 3);
    s.color_invert = Bits(path, 16, 1);
    s.alpha_function = Function(Bits(path, 17, 1), Bits(path, 18, 1), alpha_add ? 1 : 0);
    s.alpha_factor = Factor(Bits(path, 19, 7), Bits(path, 22, 1));
    s.alpha_local = Bits(path, 5, 3);
    s.alpha_other = Bits(path, 2, 3);
    s.alpha_invert = Bits(path, 25, 1);
    s.constant_color = c.constant0;
    s.constant_other = c.constant1;
    s.constant_split = true;

    // textureMode, tLOD and tDetail of each TMU. A TMU that takes no part
    // passes on what the one above it made (TMU 0) or makes nothing (TMU 1).
    for (unsigned i = 0; i < 2; ++i)
    {
        auto &t = s.tmu[i];
        const bool present = c.textured[i] && c.texture[i] < textures.size() && textures[c.texture[i]].surface.handle;
        if (!present)
        {
            t.rgb_function = t.alpha_function = i == 0 && c.textured[1] ? 0x3 : 0x0;
            t.rgb_factor = t.alpha_factor = 8;
            continue;
        }
        const unsigned mode = c.texture_mode[i], lod = c.lod[i], detail = c.detail[i];
        const Texture &texture = textures[c.texture[i]];
        const unsigned rgb_add = Bits(mode, 18, 3), a_add = Bits(mode, 27, 3);
        t.rgb_function = Function(Bits(mode, 12, 1), Bits(mode, 13, 1), rgb_add == 1 ? 1 : rgb_add == 2 ? 2 : 0);
        t.rgb_factor = Factor(Bits(mode, 14, 7), Bits(mode, 17, 1));
        t.rgb_invert = Bits(mode, 20, 1);
        t.alpha_function = Function(Bits(mode, 21, 1), Bits(mode, 22, 1), a_add ? 1 : 0);
        t.alpha_factor = Factor(Bits(mode, 23, 7), Bits(mode, 26, 1));
        t.alpha_invert = Bits(mode, 29, 1);
        t.textured = true;
        t.min_bilinear = Bits(mode, 1, 1);
        t.mag_bilinear = Bits(mode, 2, 1);
        t.clamp_s = Bits(mode, 6, 1);
        t.clamp_t = Bits(mode, 7, 1);
        // Mip mapping is on when tLOD allows more than one level and the
        // texture memory held more than one.
        t.mipmap = Bits(lod, 0, 0x3f) != Bits(lod, 6, 0x3f) && texture.levels > 1 ? 1 : 0;
        t.lod_bias = float(Signed6(Bits(lod, 12, 0x3f))) / 4.0f;
        t.width = texture.surface.width;
        t.height = texture.surface.height;
        t.detail_max = float(Bits(detail, 0, 0xff)) / 255.0f;
        t.detail_bias = Signed6(Bits(detail, 8, 0x3f));
        t.detail_scale = Bits(detail, 14, 7);
    }

    // alphaMode: the alpha test and the blend factors, which Glide passes on
    // as they are.
    s.alpha_test = Bits(alpha, 0, 1) ? Bits(alpha, 1, 7) : 7;
    s.alpha_reference = Bits(alpha, 24, 0xff);
    if (Bits(alpha, 4, 1))
    {
        s.rgb_source = Bits(alpha, 8, 15);
        s.rgb_destination = Bits(alpha, 12, 15);
        s.alpha_source = Bits(alpha, 16, 15);
        s.alpha_destination = Bits(alpha, 20, 15);
    }

    // fbzMode: chroma key, alpha mask, the buffers written and the depth.
    s.chroma_key = Bits(fbz, 1, 1);
    s.chroma_value = c.chroma_key & 0xffffff;
    s.alpha_bit_test = Bits(fbz, 13, 1);
    s.rgb_mask = Bits(fbz, 9, 1);
    const bool auxiliary = Bits(fbz, 10, 1), alpha_planes = Bits(fbz, 18, 1), depth_buffer = Bits(fbz, 4, 1);
    const unsigned depth_source = Bits(fbz, 3, 1) && !Bits(fbz, 20, 1) ? 2 : 1;   // W, or Z and the constant of a compare against zaColor
    if (alpha_planes)
    {
        // The auxiliary buffer holds alpha: there is no depth.
        s.alpha_mask = auxiliary;
    }
    else if (depth_buffer)
    {
        s.depth_mode = depth_source;
        s.depth_function = Bits(fbz, 5, 7);
        s.depth_mask = auxiliary;
    }
    else if (auxiliary)
    {
        // No depth test, but the pixel's depth is written.
        s.depth_mode = depth_source;
        s.depth_function = 7;
        s.depth_mask = true;
    }
    if (Bits(fbz, 16, 1) && !Bits(fbz, 20, 1)) s.depth_bias = int(short(c.za_color & 0xffff));

    // fogMode.
    if (Bits(fog, 0, 1))
    {
        const unsigned source = Bits(fog, 3, 3);
        s.fog_mode = Bits(fog, 5, 1) ? 4 : source == 1 ? 1 : source == 2 ? 3 : 2;
        if (Bits(fog, 2, 1)) s.fog_mode |= 0x100;
        if (Bits(fog, 1, 1)) s.fog_mode |= 0x200;
        s.fog_color = c.fog_color & 0xffffff;
        memcpy(s.fog_table, fog_table, sizeof s.fog_table);
    }

    for (unsigned i = 0; i < 4; ++i) s.clip[i] = clip[i];
    s.scale = float(scale);
}

void Renderer::Gamma(const DosVoodooFrame &frame)
{
    if (!memcmp(frame.gamma_exponent, gamma_exponent, sizeof gamma_exponent) &&
        !memcmp(frame.gamma_factor, gamma_factor, sizeof gamma_factor))
        return;
    memcpy(gamma_exponent, frame.gamma_exponent, sizeof gamma_exponent);
    memcpy(gamma_factor, frame.gamma_factor, sizeof gamma_factor);
    gamma_linear = true;
    for (unsigned c = 0; c < 3; ++c)
        for (unsigned i = 0; i < 256; ++i)
        {
            const float value = std::pow(float(i) / 255.0f, gamma_exponent[c]) * gamma_factor[c] * 255.0f + 0.5f;
            gamma[c][i] = (unsigned char)std::min(255.0f, std::max(0.0f, value));
            gamma_linear = gamma_linear && gamma[c][i] == i;
        }
}

// The buffer the monitor shows comes back from the GPU as the picture of the
// machine, through the gamma of the DAC.
void Renderer::Present(unsigned shown)
{
    Buffer &b = color[shown % 3];
    gpu.Sync(b.surface, false);
    const unsigned target_width = width * scale, half = scale / 2;
    // One screen pixel is the middle of its block of target pixels.
    for (unsigned y = 0; y < height; ++y)
    {
        const unsigned *row = b.pixels.data() + size_t(y * scale + half) * target_width;
        unsigned *out = picture.data() + size_t(y) * width;
        for (unsigned x = 0; x < width; ++x) out[x] = row[x * scale + half];
    }
    auto correct = [&](unsigned p) {
        return 0xff000000u | unsigned(gamma[0][(p >> 16) & 255]) << 16 | unsigned(gamma[1][(p >> 8) & 255]) << 8 | gamma[2][p & 255];
    };
    for (unsigned &p : picture) p = gamma_linear ? (p | 0xff000000u) : correct(p);
    this->shown = shown % 3;
    full_made = false;
}

// The program reads through the linear frame buffer: the buffers it reads
// come back from the GPU as the card holds them, 565 and the 16 bit depth at
// the resolution of the card. As in the picture, one pixel of the card is the
// middle of its block of target pixels.
void Renderer::Read(unsigned mask, DosVoodooReadback &readback)
{
    const unsigned target_width = width * scale, half = scale / 2;
    const unsigned columns = std::min(width, DosVoodooReadback::Pitch), rows = std::min(height, DosVoodooReadback::Rows);
    for (unsigned i = 0; i < 3; ++i)
    {
        if (!(mask & (DosVoodooReadback::Color << i))) continue;
        if (!readback.color[i]) { healthy = false; continue; }
        Buffer &b = color[i];
        gpu.Sync(b.surface, false);
        for (unsigned y = 0; y < rows; ++y)
        {
            const unsigned *row = b.pixels.data() + size_t(y * scale + half) * target_width;
            unsigned short *out = readback.color[i] + size_t(y) * DosVoodooReadback::Pitch;
            for (unsigned x = 0; x < columns; ++x)
            {
                const unsigned p = row[x * scale + half];
                out[x] = (unsigned short)(((p >> 8) & 0xf800) | ((p >> 5) & 0x07e0) | ((p >> 3) & 0x001f));
            }
        }
    }
    if ((mask & DosVoodooReadback::Depth) && !readback.depth) healthy = false;
    if ((mask & DosVoodooReadback::Depth) && readback.depth)
    {
        gpu.SyncDepth(depth);
        for (unsigned y = 0; y < rows; ++y)
        {
            const unsigned short *row = depth_bytes.data() + size_t(y * scale + half) * target_width;
            unsigned short *out = readback.depth + size_t(y) * DosVoodooReadback::Pitch;
            for (unsigned x = 0; x < columns; ++x) out[x] = row[x * scale + half];
        }
    }
}

// The buffers of the card as a saved state held them :
// the colour buffers, 565, become the bytes of the targets, which the GPU
// takes at their next Begin; the depth is drawn, one point a pixel of the
// card, written without a test and without colour. Read gives the same
// values back.
void Renderer::Restore(const DosVoodooFrame &frame)
{
    const unsigned target_width = width * scale, pitch = frame.restore_pitch;
    if (pitch < width) { healthy = false; return; }
    for (unsigned i = 0; i < 3; ++i)
    {
        if (!frame.restore_color[i]) continue;
        Buffer &b = color[i];
        gpu.Sync(b.surface, true);
        for (unsigned y = 0; y < height * scale; ++y)
        {
            const unsigned short *in = frame.restore_color[i] + size_t(y / scale) * pitch;
            unsigned *out = b.pixels.data() + size_t(y) * target_width;
            for (unsigned x = 0; x < target_width; ++x)
            {
                const unsigned p = in[x / scale], red = p >> 11, green = (p >> 5) & 63, blue = p & 31;
                out[x] = 0xff000000u | (red << 3 | red >> 2) << 16 | (green << 2 | green >> 4) << 8 | (blue << 3 | blue >> 2);
            }
        }
    }
    if (!frame.restore_depth) return;
    if (!gpu.Begin(color[0].surface, &depth)) { healthy = false; return; }
    DosVoodooCommand command;
    command.type = DosVoodooCommand::RawPixels;
    command.fbz_mode = 1u << 10;   // the auxiliary buffer is written, nothing is tested, no colour
    Gpu::GlideState state;
    State(command, state);
    state.clip[0] = state.clip[1] = 0;
    state.clip[2] = int(target_width);
    state.clip[3] = int(height * scale);
    gpu.SetTexture(0, nullptr);
    gpu.SetTexture(1, nullptr);
    constexpr unsigned Rows = 32;
    scratch.resize(size_t(width) * Rows);
    for (unsigned top = 0; top < height; top += Rows)
    {
        const unsigned rows = std::min(Rows, height - top);
        Gpu::GlideVertex *v = scratch.data();
        for (unsigned y = top; y < top + rows; ++y)
            for (unsigned x = 0; x < width; ++x, ++v)
            {
                *v = Gpu::GlideVertex{};
                v->x = float(x) + 0.5f;
                v->y = float(y) + 0.5f;
                v->ooz = float(frame.restore_depth[size_t(y) * pitch + x]);
                v->oow = 1.0f;
            }
        gpu.DrawGlide(Gpu::PointList, scratch.data(), width * rows, state);
    }
    gpu.End();
}

// The shown buffer as it was drawn. Without a gamma curve these are the
// pixels of the buffer themselves.
const unsigned *Renderer::Full()
{
    if (scale == 1) return picture.data();
    const Buffer &b = color[shown];
    if (gamma_linear) return b.pixels.data();
    if (!full_made)
    {
        full.resize(b.pixels.size());
        for (size_t i = 0; i < full.size(); ++i)
        {
            const unsigned p = b.pixels[i];
            full[i] = 0xff000000u | unsigned(gamma[0][(p >> 16) & 255]) << 16 | unsigned(gamma[1][(p >> 8) & 255]) << 8 | gamma[2][p & 255];
        }
        full_made = true;
    }
    return full.data();
}

bool Renderer::Draw(const DosVoodooFrame &frame, unsigned wanted_scale, DosVoodooReadback *readback)
{
    if (!frame.active)
    {
        if (!active) return false;
        active = false;
        Drop();
        return true;
    }
    if (!gpu.Glide() || !frame.width || !frame.height || frame.width > 1024 || frame.height > 1024 ||
        (frame.command_count && !frame.commands) || (frame.vertex_count && !frame.vertices) ||
        (frame.texture_count && !frame.textures)) { healthy = false; return false; }
    const unsigned target_scale = std::max(1u, std::min(8u, wanted_scale));
    const bool fresh = !active || frame.width != width || frame.height != height || target_scale != scale;
    if (fresh) Resize(frame.width, frame.height, target_scale);
    active = true;
    Gamma(frame);
    memcpy(fog_table, frame.fog_table, sizeof fog_table);
    for (unsigned i = 0; i < frame.texture_count; ++i) Upload(frame.textures[i]);
    if (frame.restore_pitch) Restore(frame);

    const int target_width = int(width * scale), target_height = int(height * scale);
    int open = -1;
    bool drew_shown = false;
    Gpu::GlideState state;
    static const Gpu::State none;
    for (unsigned n = 0; n < frame.command_count; ++n)
    {
        const DosVoodooCommand &c = frame.commands[n];
        if (c.type > DosVoodooCommand::Clip) { healthy = false; continue; }
        if (c.type == DosVoodooCommand::Clip)
        {
            if (c.clip_active)
                for (unsigned i = 0; i < 4; ++i) clip[i] = c.clip[i] * int(scale);
            else { clip[0] = clip[1] = 0; clip[2] = target_width; clip[3] = target_height; }
            continue;
        }
        if (int(c.buffer % 3) != open)
        {
            if (open >= 0) gpu.End();
            open = int(c.buffer % 3);
            if (!gpu.Begin(color[open].surface, &depth)) { healthy = false; open = -1; continue; }
            if (unsigned(open) == frame.shown % 3) drew_shown = true;
        }
        if (c.type == DosVoodooCommand::Fill)
        {
            const unsigned flags = (Bits(c.fbz_mode, 9, 1) ? 1u : 0u) | (Bits(c.fbz_mode, 10, 1) && c.auxiliary && !Bits(c.fbz_mode, 18, 1) ? 2u : 0u);
            if (!flags) continue;
            int rect[4] = {0, 0, target_width, target_height};
            if (c.clip_active)
                for (unsigned i = 0; i < 4; ++i) rect[i] = c.clip[i] * int(scale);
            rect[0] = std::max(0, rect[0]);
            rect[1] = std::max(0, rect[1]);
            rect[2] = std::min(target_width, rect[2]);
            rect[3] = std::min(target_height, rect[3]);
            if (rect[2] <= rect[0] || rect[3] <= rect[1]) continue;
            gpu.Clear(flags, c.constant1, float(c.za_color & 0xffff) / 65535.0f, 0, rect, 1, none);
            continue;
        }
        if (!c.vertex_count || c.first_vertex > frame.vertex_count || c.vertex_count > frame.vertex_count - c.first_vertex)
        { healthy = false; continue; }
        if ((c.textured[0] && c.texture[0] >= textures.size()) ||
            (c.textured[1] && c.texture[1] >= textures.size())) { healthy = false; continue; }
        State(c, state);
        const auto *vertices = reinterpret_cast<const Gpu::GlideVertex *>(frame.vertices + c.first_vertex);
        const bool triangles = c.type == DosVoodooCommand::Triangles;
        // A pixel of the linear frame buffer is written whatever the clip
        // rectangle of the triangles says.
        if (!triangles) { state.clip[0] = state.clip[1] = 0; state.clip[2] = target_width; state.clip[3] = target_height; }
        const bool turned = triangles && Bits(c.fbz_mode, 17, 1), constant_depth = triangles && Bits(c.fbz_mode, 20, 1);
        if (turned || constant_depth)
        {
            // The Y origin at the bottom, and the depth compare against
            // zaColor, where the depth of the pixel is that constant.
            scratch.assign(vertices, vertices + c.vertex_count);
            for (auto &v : scratch)
            {
                if (turned) v.y = float(frame.y_origin + 1) - v.y;
                if (constant_depth) v.ooz = float(c.za_color & 0xffff);
            }
            vertices = scratch.data();
        }
        for (unsigned t = 0; t < 2; ++t)
            gpu.SetTexture(t, state.tmu[t].textured ? &textures[c.texture[t]].surface : nullptr);
        gpu.DrawGlide(triangles ? Gpu::TriangleList : Gpu::PointList, vertices, c.vertex_count, state);
        if (triangles) this->triangles += c.vertex_count / 3;
    }
    if (open >= 0) gpu.End();
    if (!frame.read_mask)
    {
        ++frames;
        Present(frame.shown);
        return true;
    }
    // Cut by a read, between two vertical blanks: the program gets its
    // buffers, and the monitor a new picture only when the buffer it shows
    // was drawn into or is another one now.
    ++reads;
    if (readback) Read(frame.read_mask, *readback);
    else healthy = false;
    if (!fresh && !drew_shown && frame.shown % 3 == shown) return false;
    Present(frame.shown);
    return true;
}
bool Drain(const DosVm::Provider &provider, DosVm::Session *session, Gpu::Backend &gpu,
               unsigned owner, Renderer *&renderer, unsigned scale)
{
    if (!session || !provider.VoodooFrame) return true;
    unsigned taken = 0;
    DosVoodooFrame frame;
    while (taken < 4 && provider.VoodooFrame(provider.owner, session, frame))
    {
        ++taken;
        struct Drawn
        {
            const DosVm::Provider &provider;
            DosVm::Session *session;
            bool wanted;
            ~Drawn() { if (wanted && provider.VoodooDrawn) provider.VoodooDrawn(provider.owner, session); }
        } drawn{provider, session, frame.read_mask != 0};
        if (!renderer && frame.active) renderer = new Renderer(gpu, owner);
        if (renderer)
        {
            auto *readback = provider.VoodooReadback ? provider.VoodooReadback(provider.owner, session) : nullptr;
            renderer->Draw(frame, scale, readback);
        }
    }
    return !renderer || renderer->Healthy();
}
} // namespace DosVoodoo
