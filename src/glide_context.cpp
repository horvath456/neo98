// SPDX-License-Identifier: GPL-2.0-or-later
// The Glide 2 driver's state machine (src/glide_context.h).
// Every export of GLIDE2X.DLL ends in Context::Call with its stack words.
// State calls change Gpu::GlideState; a draw turns GrVertex structures of the
// guest into Gpu::GlideVertex and queues them, and the queue goes to the
// backend before the state changes, at a buffer swap, a clear and every
// access of the linear frame buffer. The texture memory of each TMU is a
// byte array the program manages itself; a texture is decoded from it into
// BGRA the first time a draw samples it, and again after a download wrote
// into its bytes.
#include "glide_context.h"
#include "glide_clip.h"
#include "win98_glide_exports.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Glide
{
namespace
{
using namespace Win98Glide;

// GrVertex of Glide 2 with two TMUs: x, y, z, r, g, b, ooz, a, oow, then
// sow, tow, oow for each TMU.
constexpr unsigned VertexBytes = 15 * 4;
constexpr unsigned PageBytes = 4096;
// Screen resolutions of grSstWinOpen, GR_RESOLUTION_320x200 (0) to
// GR_RESOLUTION_400x300 (15).
constexpr int Resolutions[16][2] = {
    {320, 200}, {320, 240}, {400, 256}, {512, 384}, {640, 200}, {640, 350}, {640, 400}, {640, 480},
    {800, 600}, {960, 720}, {856, 480}, {512, 256}, {1024, 768}, {1280, 1024}, {1600, 1200}, {400, 300},
};
// The linear frame buffer is 1024 pixels wide on the card: 2048 bytes a
// line in the 16-bit formats, 4096 in the 32-bit ones.
constexpr unsigned LfbPixels = 1024;

float Float(unsigned word)
{
    float f;
    memcpy(&f, &word, 4);
    return f;
}

// The size of a level: the long side is 256 >> lod, the short one follows
// the aspect ratio, GR_ASPECT_8x1 (0) to GR_ASPECT_1x8 (6).
void LevelSize(unsigned lod, unsigned aspect, int &w, int &h)
{
    const int side = 256 >> (lod > 8 ? 8 : lod);
    w = h = side;
    if (aspect < 3) h = side >> (3 - aspect);
    else if (aspect > 3) w = side >> (aspect - 3);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
}

unsigned TexelBytes(unsigned format) { return format >= 8 ? 2 : 1; }

// The bytes a level takes in texture memory: at least two texels in each
// direction, which grTexCalcMemRequired reports as well.
unsigned LevelBytes(unsigned lod, unsigned aspect, unsigned format)
{
    int w, h;
    LevelSize(lod, aspect, w, h);
    return unsigned((w < 2 ? 2 : w) * (h < 2 ? 2 : h)) * TexelBytes(format);
}

unsigned Expand(unsigned value, unsigned bits)
{
    // Bit replication, as the card widens a channel to eight bits.
    value &= (1u << bits) - 1;
    unsigned out = 0;
    for (int shift = 8 - int(bits); shift > -int(bits); shift -= int(bits))
        out |= shift >= 0 ? value << shift : value >> -shift;
    return out & 255;
}

unsigned Rgb565(unsigned v) { return Expand(v >> 11, 5) << 16 | Expand(v >> 5, 6) << 8 | Expand(v, 5); }
unsigned Rgb555(unsigned v) { return Expand(v >> 10, 5) << 16 | Expand(v >> 5, 5) << 8 | Expand(v, 5); }

unsigned short To565(unsigned argb)
{
    return (unsigned short)(((argb >> 8) & 0xf800) | ((argb >> 5) & 0x07e0) | ((argb >> 3) & 0x001f));
}

// A signed nine-bit field of an NCC register.
int Signed9(unsigned word, unsigned shift)
{
    int v = int((word >> shift) & 0x1ff);
    return v & 0x100 ? v - 0x200 : v;
}

// One texel of an NCC table (YIQ): four bits Y, two bits I, two bits Q.
unsigned Ncc(const unsigned *table, unsigned texel)
{
    const unsigned y = (table[(texel >> 4) >> 2] >> (8 * ((texel >> 4) & 3))) & 255;
    const unsigned i = table[4 + ((texel >> 2) & 3)], q = table[8 + (texel & 3)];
    auto channel = [&](unsigned shift) {
        int v = int(y) + Signed9(i, shift) + Signed9(q, shift);
        return unsigned(v < 0 ? 0 : v > 255 ? 255 : v);
    };
    return 0xff000000u | channel(18) << 16 | channel(9) << 8 | channel(0);
}

// A screen coordinate as the card takes it: a signed 12.4 fixed point number
// in sixteen bits. Programs add 3 << 18 to x and y so that the FPU rounds
// to sixteenths of a pixel; that sum disappears in the conversion, as it did
// on the card.
float Snap(float v)
{
    const long long fixed = (long long)(v * 16.0f);
    return float((short)(unsigned short)(fixed & 0xffff)) / 16.0f;
}

// Buffer names of GR_BUFFER_.
constexpr unsigned FrontBufferId = 0, BackBufferId = 1, AuxBufferId = 2;
} // namespace

bool Context::TextureKey::operator<(const TextureKey &o) const
{
    const unsigned a[8] = {tmu, start, even_odd, small_lod, large_lod, aspect, format, table};
    const unsigned b[8] = {o.tmu, o.start, o.even_odd, o.small_lod, o.large_lod, o.aspect, o.format, o.table};
    return std::lexicographical_compare(a, a + 8, b, b + 8);
}

Context::Context(Gpu::Backend &gpu, unsigned owner) : gpu(gpu), owner(owner)
{
    trace = getenv("NEO98_TRACE_GLIDE") != nullptr;
    for (unsigned t = 0; t < 2; ++t)
    {
        memory[t].assign(Card::TmuBytes, 0);
        pages[t].assign(Card::TmuBytes / PageBytes, 0);
    }
}

Context::~Context()
{
    if (batch_open) gpu.End();
    for (auto &entry : textures) gpu.Forget(owner, entry.second->surface.handle);
    for (auto &b : color)
        if (!b.pixels.empty()) gpu.Release(b.surface.bits, b.pixels.size() * 4);
    if (!depth_bytes.empty()) gpu.Forget(owner, depth.handle);
}

unsigned Context::Argb(unsigned v) const
{
    switch (color_format)
    {
    case 1: return (v & 0xff00ff00u) | (v & 0xff) << 16 | (v >> 16 & 0xff);   // ABGR
    case 2: return v >> 8 | v << 24;                                          // RGBA
    case 3: return (v & 0xff) << 24 | (v >> 8 & 0xff) << 16 | (v >> 16 & 0xff) << 8 | v >> 24;   // BGRA
    default: return v;                                                       // ARGB
    }
}

Context::Buffer &Context::RenderTarget() { return render_buffer == FrontBufferId ? FrontBuffer() : BackBuffer(); }

Context::Buffer *Context::BufferOf(unsigned buffer)
{
    if (buffer == FrontBufferId) return &FrontBuffer();
    if (buffer == BackBufferId) return &BackBuffer();
    return nullptr;
}

void Context::EnsureBatch()
{
    if (batch_open) return;
    gpu.Begin(RenderTarget().surface, &depth);
    batch_open = true;
    ++batch;
    // Begin forgets what the stages sampled.
    dirty = true;
}

void Context::EndBatch()
{
    Flush();
    if (!batch_open) return;
    gpu.End();
    batch_open = false;
}

bool Context::OpenWindow(const unsigned *a, Host &host)
{
    // (hWnd, screen_resolution, refresh_rate, color_format, origin_location,
    // nColBuffers, nAuxBuffers)
    const unsigned resolution = a[1];
    if (resolution >= 16 || open) return false;
    width = Resolutions[resolution][0];
    height = Resolutions[resolution][1];
    color_format = a[3] & 3;
    origin = a[4] == 1 ? 1 : 0;
    if (!host.OpenScreen(width, height)) return false;
    // grSstWinOpen of the Voodoo 3 driver loads gamma 1.3 unless the
    // environment names other values (1000f515..1000f55c).
    SetGamma(1.3f, host);
    scale = float(std::max(1, std::min(8, host.Scale(width, height))));
    target_width = int(float(width) * scale);
    target_height = int(float(height) * scale);
    for (unsigned i = 0; i < 2; ++i)
    {
        Buffer &b = color[i];
        b.pixels.assign(size_t(target_width) * size_t(target_height), 0xff000000u);
        b.surface = Gpu::Surface{};
        b.surface.bits = reinterpret_cast<unsigned char *>(b.pixels.data());
        b.surface.width = target_width;
        b.surface.height = target_height;
        b.surface.pitch = target_width * 4;
        b.surface.bit_count = 32;
        b.surface.r_mask = 0xff0000;
        b.surface.g_mask = 0xff00;
        b.surface.b_mask = 0xff;
        b.surface.a_mask = 0xff000000u;
        b.surface.owner = owner;
        b.surface.handle = next_handle++;
        // The bytes are new: the GPU takes them at the first Begin.
        gpu.Sync(b.surface, true);
    }
    depth_bytes.assign(size_t(target_width) * size_t(target_height), 0);
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
    front = 0;
    render_buffer = BackBufferId;
    state.scale = scale;
    clip[0] = clip[1] = 0;
    clip[2] = width;
    clip[3] = height;
    open = true;
    dirty = true;
    return true;
}

void Context::Close(Host &host)
{
    if (!open) return;
    EndBatch();
    for (auto &l : lfb) l.locked = false;
    for (auto &b : color)
    {
        gpu.Release(b.surface.bits, b.pixels.size() * 4);
        b.pixels.clear();
        b.pixels.shrink_to_fit();
    }
    gpu.Forget(owner, depth.handle);
    depth_bytes.clear();
    open = false;
    host.CloseScreen();
}

void Context::StateSync()
{
    if (!open) return;
    gpu.Sync(color[0].surface, false);
    gpu.Sync(color[1].surface, false);
    if (!depth_bytes.empty()) gpu.SyncDepth(depth);
}

void Context::PresentFront(Host &host)
{
    Buffer &b = FrontBuffer();
    gpu.Sync(b.surface, false);
    // One screen pixel is the middle of its block of target pixels.
    const int k = target_width / (width ? width : 1), half = k / 2;
    for (int y = 0; y < height; ++y)
    {
        const unsigned *row = b.pixels.data() + size_t(y * k + half) * size_t(target_width);
        unsigned *out = picture.data() + size_t(y) * size_t(width);
        for (int x = 0; x < width; ++x) out[x] = row[x * k + half];
    }
    const unsigned *full = b.pixels.data();
    if (!gamma_linear)
    {
        auto correct = [&](unsigned c) {
            return (c & 0xff000000u) | unsigned(gamma[(c >> 16) & 255]) << 16 | unsigned(gamma[(c >> 8) & 255]) << 8 |
                   gamma[c & 255];
        };
        for (unsigned &c : picture) c = correct(c);
        if (target_width != width || target_height != height)
        {
            shown.resize(b.pixels.size());
            for (size_t i = 0; i < shown.size(); ++i) shown[i] = correct(b.pixels[i]);
            full = shown.data();
        }
    }
    host.Present(picture.data(), width, height, full, target_width, target_height);
    last_present = host.Microseconds();
    front_dirty = false;
}

// The gamma table of the Voodoo 3 driver (glide3x h3 minihwc hwcGammaRGB,
// called by grGammaCorrectionValue): pow(x / 255, 1 / gamma) *
// 255 + 0.5, truncated.
void Context::SetGamma(float value, Host &host)
{
    gamma_linear = true;
    for (unsigned x = 0; x < 256; ++x)
    {
        gamma[x] = (unsigned char)std::min(255.0f, std::pow(x / 255.0f, 1.0f / value) * 255.0f + 0.5f);
        gamma_linear = gamma_linear && gamma[x] == x;
    }
    if (trace)
    {
        char text[96];
        snprintf(text, sizeof text, "GLIDE gamma=%.3f", double(value));
        host.Trace(text);
    }
}

void Context::Swap(unsigned interval, Host &host)
{
    EndBatch();
    front ^= 1;
    PresentFront(host);
    if (trace)
    {
        // The middle pixel and the number of pixels that are not black, for
        // a run that shows nothing.
        unsigned lit = 0;
        for (unsigned c : picture) lit += (c & 0xffffff) != 0;
        char text[256];
        snprintf(text, sizeof text,
                 "GLIDE swap frame=%llu triangles=%u lines=%u points=%u clears=%u lfb_writes=%u lfb_reads=%u downloads=%u decodes=%u lit=%u centre=%06x",
                 frame, counted.triangles, counted.lines, counted.points, counted.clears, counted.lfb_writes, counted.lfb_reads,
                 counted.downloads, counted.decodes, lit, picture.empty() ? 0 : picture[picture.size() / 2 + unsigned(width) / 2] & 0xffffff);
        host.Trace(text);
        counted = {};
        // NEO98_GLIDE_DUMP=FILE: every hundredth front buffer as a PPM, at
        // the resolution the driver renders at; NEO98_GLIDE_DUMP_FRAME=N
        // the one of frame N only.
        static const char *dump = getenv("NEO98_GLIDE_DUMP");
        static const char *only = getenv("NEO98_GLIDE_DUMP_FRAME");
        if (dump && (only ? frame == strtoull(only, nullptr, 10) : frame % 100 == 0))
            if (FILE *f = fopen(dump, "wb"))
            {
                fprintf(f, "P6\n%d %d\n255\n", target_width, target_height);
                for (unsigned c : FrontBuffer().pixels)
                {
                    const unsigned char rgb[3] = {(unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c};
                    fwrite(rgb, 1, 3, f);
                }
                fclose(f);
            }
    }
    ++frame;
    Evict();
    // The wait for the vertical blank comes last: the thread may sleep in
    // it when the process ends, and the context is gone when it wakes.
    for (unsigned i = 0; i < interval && i < 4; ++i) host.Pace();
}

void Context::Clear(unsigned color_value, unsigned alpha, unsigned depth_value)
{
    const bool clear_color = state.rgb_mask || state.alpha_mask;
    const bool clear_depth = state.depth_mask;
    if (!clear_color && !clear_depth) return;
    ++counted.clears;
    EnsureBatch();
    Flush();
    const unsigned argb = (Argb(color_value) & 0xffffff) | (alpha & 255) << 24;
    int rect[4];
    for (unsigned i = 0; i < 4; ++i) rect[i] = int(float(clip[i]) * scale);
    if (origin == 1)
    {
        // The clip window counts from the bottom.
        const int top = target_height - rect[3], bottom = target_height - rect[1];
        rect[1] = top;
        rect[3] = bottom;
    }
    static const Gpu::State none;
    gpu.Clear((clear_color ? 1u : 0u) | (clear_depth ? 2u : 0u), argb, float(depth_value & 0xffff) / 65535.0f, 0, rect, 1, none);
    if (render_buffer == FrontBufferId) front_dirty = true;
}

// A GrVertex of the guest as the backend takes it: y turned when the origin
// is at the bottom, s, t and 1/w per TMU after the hints.
Gpu::GlideVertex Context::Vertex(const unsigned char *raw) const
{
    float f[15];
    memcpy(f, raw, sizeof f);
    Gpu::GlideVertex v;
    v.x = Snap(f[0]);
    v.y = origin == 1 ? float(height - 1) - Snap(f[1]) : Snap(f[1]);
    v.r = f[3]; v.g = f[4]; v.b = f[5];
    v.ooz = f[6];
    v.a = f[7];
    v.oow = f[8];
    // The card sends the 1/w of the vertex to every TMU and s, t of TMU 0 to
    // TMU 1 as well; a TMU has values of its own only where the program said
    // so with GR_HINT_STWHINT (grHints), or when TMU 0 does not sample.
    v.s0 = f[9];
    v.t0 = f[10];
    v.q0 = (hints & 0x2) ? f[11] : f[8];   // GR_STWHINT_W_DIFF_TMU0
    const bool own_st = !(hints & 0x10) && TmuSamplesTexel(0) ? false : true;   // GR_STWHINT_ST_DIFF_TMU1
    v.s1 = own_st ? f[12] : f[9];
    v.t1 = own_st ? f[13] : f[10];
    v.q1 = (hints & 0x8) ? f[14] : f[8];   // GR_STWHINT_W_DIFF_TMU1
    return v;
}

bool Context::ReadVertex(unsigned address, Gpu::GlideVertex &out, Host &host)
{
    unsigned char raw[VertexBytes];
    if (!address || !host.Read(address, raw, sizeof raw)) return false;
    out = Vertex(raw);
    return true;
}

// grTexCombine: whether the TMU's own texel enters its result, the mask
// Glide keeps as tmuMask.
bool Context::TmuSamplesTexel(unsigned tmu) const
{
    const auto &t = state.tmu[tmu];
    auto uses = [](unsigned function, unsigned factor) {
        const unsigned value = factor & 7;
        return function != 0 && function != 3 ? true : value == 1 || value == 3;
    };
    return uses(t.rgb_function, t.rgb_factor) || uses(t.alpha_function, t.alpha_factor);
}

void Context::Triangle(const Gpu::GlideVertex &a, const Gpu::GlideVertex &b, const Gpu::GlideVertex &c)
{
    ++triangles_in;
    // The area in the program's coordinates; y was turned for a bottom
    // origin, which turns the sign as well.
    float area = (a.x - b.x) * (b.y - c.y) - (b.x - c.x) * (a.y - b.y);
    if (origin == 1) area = -area;
    if (area == 0.0f) return;
    // GR_CULL_NEGATIVE drops a negative area, GR_CULL_POSITIVE a positive one.
    if ((cull == 1 && area < 0.0f) || (cull == 2 && area > 0.0f)) return;
    ++triangles_out;
    ++counted.triangles;
    const Gpu::GlideVertex v[3] = {a, b, c};
    Emit(Gpu::TriangleList, v, 3);
}

void Context::Emit(unsigned primitive, const Gpu::GlideVertex *v, unsigned count)
{
    EnsureBatch();
    if (dirty)
    {
        Flush();
        Prepare();
    }
    if (primitive != pending_primitive || pending.size() > 60000)
    {
        Flush();
        pending_primitive = primitive;
    }
    pending.insert(pending.end(), v, v + count);
    if (render_buffer == FrontBufferId) front_dirty = true;
}

void Context::Flush()
{
    if (pending.empty() || !batch_open) { pending.clear(); return; }
    gpu.DrawGlide(pending_primitive, pending.data(), unsigned(pending.size()), state);
    pending.clear();
}

// The textures of both TMUs and the clip window in target pixels, for the
// draws that follow.
void Context::Prepare()
{
    for (unsigned t = 0; t < 2; ++t)
    {
        bound[t] = Resolve(t);
        gpu.SetTexture(t, bound[t]);
        state.tmu[t].textured = bound[t] != nullptr;
        if (bound[t])
        {
            int w, h;
            LevelSize(source[t].large_lod, source[t].aspect, w, h);
            state.tmu[t].width = w;
            state.tmu[t].height = h;
        }
    }
    for (unsigned i = 0; i < 4; ++i) state.clip[i] = int(float(clip[i]) * scale);
    if (origin == 1)
    {
        const int top = target_height - state.clip[3], bottom = target_height - state.clip[1];
        state.clip[1] = top;
        state.clip[3] = bottom;
    }
    state.scale = scale;
    dirty = false;
}

bool Context::LevelPresent(unsigned lod, unsigned, unsigned even_odd) const
{
    // GR_MIPMAPLEVELMASK_EVEN 1 holds lods 0, 2, 4...; _ODD 2 holds 1, 3...
    if (even_odd == 1) return (lod & 1) == 0;
    if (even_odd == 2) return (lod & 1) == 1;
    return true;
}

// Where a level lies from the start address of its texture: after every
// level of the same set that is larger.
unsigned Context::LevelOffset(unsigned lod, unsigned large_lod, unsigned aspect, unsigned format, unsigned even_odd) const
{
    unsigned offset = 0;
    for (unsigned l = large_lod; l < lod && l <= 8; ++l)
        if (LevelPresent(l, large_lod, even_odd)) offset += LevelBytes(l, aspect, format);
    return offset;
}

void Context::Download(unsigned tmu, unsigned address, const unsigned char *data, unsigned bytes)
{
    if (tmu >= 2 || !bytes || address >= Card::TmuBytes) return;
    if (bytes > Card::TmuBytes - address) bytes = Card::TmuBytes - address;
    // What is queued samples the memory as it was.
    Flush();
    memcpy(memory[tmu].data() + address, data, bytes);
    ++counted.downloads;
    ++memory_generation;
    for (unsigned p = address / PageBytes; p <= (address + bytes - 1) / PageBytes; ++p) pages[tmu][p] = memory_generation;
    dirty = true;
}

const Gpu::Surface *Context::Resolve(unsigned tmu)
{
    const TmuSource &s = source[tmu];
    if (!s.valid) return nullptr;
    TextureKey key{tmu, s.start, s.even_odd, s.small_lod, s.large_lod, s.aspect, s.format, 0};
    const unsigned f = s.format;
    if (f == 5 || f == 0xe) key.table = palette_hash[tmu];
    if (f == 1 || f == 9) key.table = ncc_hash[tmu][ncc_select[tmu] & 1];
    auto &slot = textures[key];
    if (!slot) slot.reset(new Texture);
    Texture &t = *slot;
    // The newest download into its bytes.
    unsigned bytes = 0;
    for (unsigned l = s.large_lod; l <= s.small_lod && l <= 8; ++l)
        if (LevelPresent(l, s.large_lod, s.even_odd)) bytes += LevelBytes(l, s.aspect, s.format);
    unsigned long long newest = 0;
    if (bytes && s.start < Card::TmuBytes)
    {
        const unsigned end = std::min(Card::TmuBytes, s.start + bytes);
        for (unsigned p = s.start / PageBytes; p <= (end - 1) / PageBytes; ++p) newest = std::max(newest, pages[tmu][p]);
    }
    // The table the texture was decoded with, compared in full: the key
    // holds only its hash.
    const unsigned *table = nullptr;
    unsigned table_words = 0;
    if (f == 5 || f == 0xe) { table = palette[tmu]; table_words = 256; }
    if (f == 1 || f == 9) { table = ncc[tmu][ncc_select[tmu] & 1]; table_words = 12; }
    const bool same_table = !table || (t.table.size() == table_words && !memcmp(t.table.data(), table, table_words * 4));
    if (t.texels.empty() || newest > t.generation || !same_table)
    {
        if (table) t.table.assign(table, table + table_words);
        // A texture the open batch sampled already gets a new handle, so
        // the draws before keep what they saw.
        if (!t.texels.empty() && t.batch == batch && batch_open)
        {
            gpu.Forget(owner, t.surface.handle);
            t.surface.handle = 0;
        }
        Decode(tmu, key, t);
        ++counted.decodes;
        t.generation = memory_generation;
    }
    t.batch = batch;
    t.used = frame;
    return &t.surface;
}

void Context::Decode(unsigned tmu, const TextureKey &key, Texture &t)
{
    const unsigned format = key.format, aspect = key.aspect;
    const unsigned first = key.large_lod, last = key.small_lod < first ? first : key.small_lod;
    // The levels, largest first, each as BGRA.
    size_t total = 0;
    for (unsigned l = first; l <= last && l <= 8; ++l)
    {
        int w, h;
        LevelSize(l, aspect, w, h);
        total += size_t(w) * size_t(h);
    }
    t.texels.assign(total, 0);
    const unsigned *ncc_table = ncc[tmu][ncc_select[tmu] & 1];
    size_t at = 0;
    unsigned index = 0;
    const unsigned *previous = nullptr;
    int previous_w = 0, previous_h = 0;
    for (unsigned l = first; l <= last && l <= 8; ++l, ++index)
    {
        int w, h;
        LevelSize(l, aspect, w, h);
        unsigned *out = t.texels.data() + at;
        if (LevelPresent(l, first, key.even_odd))
        {
            const unsigned offset = key.start + LevelOffset(l, first, aspect, format, key.even_odd);
            const unsigned char *m = memory[tmu].data();
            const unsigned bpp = TexelBytes(format);
            for (int i = 0; i < w * h; ++i)
            {
                const unsigned address = offset + unsigned(i) * bpp;
                unsigned v = address + bpp <= Card::TmuBytes ? m[address] : 0;
                if (bpp == 2 && address + 1 < Card::TmuBytes) v |= unsigned(m[address + 1]) << 8;
                unsigned argb = 0;
                switch (format)
                {
                case 0x0: argb = 0xff000000u | Expand(v >> 5, 3) << 16 | Expand(v >> 2, 3) << 8 | Expand(v, 2); break;   // RGB_332
                case 0x1: argb = Ncc(ncc_table, v); break;                                                          // YIQ_422
                case 0x2: argb = v << 24 | v << 16 | v << 8 | v; break;                                             // ALPHA_8
                case 0x3: argb = 0xff000000u | v << 16 | v << 8 | v; break;                                         // INTENSITY_8
                case 0x4:                                                                                         // ALPHA_INTENSITY_44
                {
                    const unsigned i4 = Expand(v, 4), a4 = Expand(v >> 4, 4);
                    argb = a4 << 24 | i4 << 16 | i4 << 8 | i4;
                    break;
                }
                case 0x5: argb = 0xff000000u | (palette[tmu][v & 255] & 0xffffff); break;                         // P_8
                case 0x8: argb = (v >> 8) << 24 | Expand(v >> 5, 3) << 16 | Expand(v >> 2, 3) << 8 | Expand(v, 2); break;   // ARGB_8332
                case 0x9: argb = (v >> 8) << 24 | (Ncc(ncc_table, v & 255) & 0xffffff); break;                   // AYIQ_8422
                case 0xa: argb = 0xff000000u | Rgb565(v); break;                                                  // RGB_565
                case 0xb: argb = (v & 0x8000 ? 0xff000000u : 0) | Rgb555(v); break;                               // ARGB_1555
                case 0xc: argb = Expand(v >> 12, 4) << 24 | Expand(v >> 8, 4) << 16 | Expand(v >> 4, 4) << 8 | Expand(v, 4); break;   // ARGB_4444
                case 0xd: argb = (v >> 8) << 24 | (v & 255) << 16 | (v & 255) << 8 | (v & 255); break;           // ALPHA_INTENSITY_88
                case 0xe: argb = (v >> 8) << 24 | (palette[tmu][v & 255] & 0xffffff); break;                     // AP_88
                default: argb = 0; break;
                }
                out[i] = argb;
            }
        }
        else if (previous)
        {
            // A level of the other set lies on the other TMU; this one has
            // the next larger level in its place, halved.
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    out[y * w + x] = previous[(y * previous_h / h) * previous_w + x * previous_w / w];
        }
        previous = out;
        previous_w = w;
        previous_h = h;
        at += size_t(w) * size_t(h);
    }
    int w0, h0;
    LevelSize(first, aspect, w0, h0);
    Gpu::Surface &s = t.surface;
    const unsigned handle = s.handle ? s.handle : next_handle++;
    s = Gpu::Surface{};
    s.bits = reinterpret_cast<unsigned char *>(t.texels.data());
    s.width = w0;
    s.height = h0;
    s.pitch = w0 * 4;
    s.bit_count = 32;
    s.r_mask = 0xff0000;
    s.g_mask = 0xff00;
    s.b_mask = 0xff;
    s.a_mask = 0xff000000u;
    s.owner = owner;
    s.handle = handle;
    s.generation = unsigned(memory_generation);
    size_t level_at = size_t(w0) * size_t(h0);
    for (unsigned l = first + 1; l <= last && l <= 8 && s.level_count < Gpu::Surface::MaxLevels; ++l)
    {
        int w, h;
        LevelSize(l, aspect, w, h);
        auto &level = s.levels[s.level_count++];
        level.bits = reinterpret_cast<unsigned char *>(t.texels.data() + level_at);
        level.width = w;
        level.height = h;
        level.pitch = w * 4;
        level_at += size_t(w) * size_t(h);
    }
}

// Textures no draw sampled for a while go, at a swap, when the batch is
// closed.
void Context::Evict()
{
    if (textures.size() < 2048) return;
    for (auto it = textures.begin(); it != textures.end();)
    {
        if (it->second->used + 8 < frame)
        {
            if (it->second->surface.handle) gpu.Forget(owner, it->second->surface.handle);
            it = textures.erase(it);
        }
        else ++it;
    }
    dirty = true;
}

unsigned Context::LfbPixelBytes(unsigned mode)
{
    switch (mode)
    {
    case 0x0: case 0x1: case 0x2: return 2;   // 565, 555, 1555
    case 0x4: case 0x5: return 4;             // 888, 8888
    default: return 0;
    }
}

unsigned Context::LfbToArgb(unsigned mode, const unsigned char *p, bool &has_alpha)
{
    const unsigned v16 = unsigned(p[0]) | unsigned(p[1]) << 8;
    has_alpha = false;
    switch (mode)
    {
    case 0x0: return 0xff000000u | Rgb565(v16);
    case 0x1: return 0xff000000u | Rgb555(v16);
    case 0x2: has_alpha = true; return (v16 & 0x8000 ? 0xff000000u : 0) | Rgb555(v16);
    case 0x4: return 0xff000000u | (unsigned(p[0]) | unsigned(p[1]) << 8 | unsigned(p[2]) << 16);
    case 0x5: has_alpha = true; return unsigned(p[0]) | unsigned(p[1]) << 8 | unsigned(p[2]) << 16 | unsigned(p[3]) << 24;
    default: return 0;
    }
}

// Screen pixels of a buffer as 565, from the middle of each block of target
// pixels, with y counted from the given origin.
void Context::ReadPixels(Buffer &b, int x, int y, int w, int h, unsigned from, unsigned short *out, int out_stride)
{
    const int k = target_width / (width ? width : 1), half = k / 2;
    for (int row = 0; row < h; ++row)
    {
        int sy = y + row;
        if (from == 1) sy = height - 1 - sy;
        if (sy < 0 || sy >= height) continue;
        const unsigned *line = b.pixels.data() + size_t(sy * k + half) * size_t(target_width);
        for (int column = 0; column < w; ++column)
        {
            const int sx = x + column;
            if (sx < 0 || sx >= width) continue;
            out[row * out_stride + column] = To565(line[sx * k + half]);
        }
    }
}

void Context::WritePixel(Buffer &b, int x, int y, unsigned from, unsigned argb, bool alpha)
{
    if (from == 1) y = height - 1 - y;
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    const int k = target_width / (width ? width : 1);
    for (int dy = 0; dy < k; ++dy)
    {
        unsigned *p = b.pixels.data() + size_t(y * k + dy) * size_t(target_width) + size_t(x * k);
        for (int dx = 0; dx < k; ++dx) p[dx] = alpha ? argb : (p[dx] & 0xff000000u) | (argb & 0xffffff);
    }
}

unsigned Context::Lock(const unsigned *a, Host &host)
{
    // (type, buffer, writeMode, origin, pixelPipeline, info)
    const unsigned type = a[0] & ~0x10u, buffer = a[1];
    unsigned mode = a[2];
    const unsigned from = a[3] == 0xff ? origin : (a[3] ? 1 : 0);
    if (!open || type > 1 || lfb[type].locked) return 0;
    if (buffer == AuxBufferId) return Unsupported("grLfbLock of the depth buffer", host);
    Buffer *b = BufferOf(buffer);
    if (!b) return 0;
    if (type == 1)
    {
        if (a[4]) return Unsupported("grLfbLock through the pixel pipeline", host);
        if (mode == 0xff) mode = 0;   // GR_LFBWRITEMODE_ANY is 565 for a colour buffer
        if (mode >= 0xc && mode <= 0xf) return Unsupported("grLfbLock with a depth write mode", host);
        if (!LfbPixelBytes(mode)) return 0;
    }
    else mode = 0;   // a read is 565
    const unsigned pixel = LfbPixelBytes(mode), stride = LfbPixels * pixel;
    Lfb &l = lfb[type];
    const unsigned needed = LfbPixels * 4 * unsigned(height);
    if (!l.address || l.bytes < needed)
    {
        l.address = host.Allocate(needed);
        l.bytes = l.address ? needed : 0;
        if (!l.address) return 0;
    }
    EndBatch();
    gpu.Sync(b->surface, false);
    // The buffer's pixels in the lock's format, so that a write lock sees
    // what is there and its unlock knows what the program changed.
    std::vector<unsigned char> bytes(size_t(stride) * size_t(height), 0);
    const int k = target_width / (width ? width : 1), half = k / 2;
    for (int y = 0; y < height; ++y)
    {
        const int sy = from == 1 ? height - 1 - y : y;
        const unsigned *line = b->pixels.data() + size_t(sy * k + half) * size_t(target_width);
        unsigned char *out = bytes.data() + size_t(y) * stride;
        for (int x = 0; x < width; ++x)
        {
            const unsigned c = line[x * k + half];
            unsigned v = 0;
            switch (mode)
            {
            case 0x0: v = To565(c); break;
            case 0x1: v = ((c >> 9) & 0x7c00) | ((c >> 6) & 0x03e0) | ((c >> 3) & 0x001f); break;
            case 0x2: v = ((c >> 9) & 0x7c00) | ((c >> 6) & 0x03e0) | ((c >> 3) & 0x001f) | (c >> 31 ? 0x8000 : 0); break;
            case 0x4: v = c & 0xffffff; break;
            default: v = c; break;
            }
            memcpy(out + x * pixel, &v, pixel);
        }
    }
    if (!host.Write(l.address, bytes.data(), unsigned(bytes.size()))) return 0;
    l.snapshot.swap(bytes);
    l.locked = true;
    l.buffer = buffer;
    l.mode = mode;
    l.origin = from;
    // GrLfbInfo_t: size, lfbPtr, strideInBytes, writeMode, origin.
    const unsigned info = a[5];
    if (info)
    {
        const unsigned words[4] = {l.address, stride, mode, from};
        host.Write(info + 4, words, sizeof words);
    }
    return 1;
}

unsigned Context::Unlock(unsigned type, unsigned buffer, Host &host)
{
    type &= ~0x10u;
    if (type > 1 || !lfb[type].locked) return 0;
    Lfb &l = lfb[type];
    l.locked = false;
    (void)buffer;
    if (type == 0) { ++counted.lfb_reads; return 1; }
    Buffer *b = BufferOf(l.buffer);
    if (!b) return 1;
    const unsigned pixel = LfbPixelBytes(l.mode), stride = LfbPixels * pixel;
    std::vector<unsigned char> now(l.snapshot.size());
    if (!host.Read(l.address, now.data(), unsigned(now.size()))) return 1;
    bool synced = false;
    for (int y = 0; y < height; ++y)
    {
        const unsigned char *was = l.snapshot.data() + size_t(y) * stride, *is = now.data() + size_t(y) * stride;
        if (!memcmp(was, is, size_t(width) * pixel)) continue;
        for (int x = 0; x < width; ++x)
        {
            if (!memcmp(was + x * pixel, is + x * pixel, pixel)) continue;
            if (!synced)
            {
                EndBatch();
                gpu.Sync(b->surface, true);
                synced = true;
            }
            bool alpha = false;
            unsigned argb = LfbToArgb(l.mode, is + x * pixel, alpha);
            if (l.mode == 0x4 || l.mode == 0x5)
            {
                // The 32-bit formats carry the colour format of the window.
                unsigned raw;
                memcpy(&raw, is + x * pixel, 4);
                argb = Argb(raw) | (l.mode == 0x4 ? 0xff000000u : 0);
            }
            WritePixel(*b, x, y, l.origin, argb, alpha);
        }
    }
    if (synced && l.buffer == FrontBufferId) front_dirty = true;
    ++counted.lfb_writes;
    return 1;
}

unsigned Context::ReadRegion(const unsigned *a, Host &host)
{
    // (src_buffer, src_x, src_y, src_width, src_height, dst_stride, dst_data)
    if (!open) return 0;
    if (a[0] == AuxBufferId) return Unsupported("grLfbReadRegion of the depth buffer", host);
    Buffer *b = BufferOf(a[0]);
    const int x = int(a[1]), y = int(a[2]), w = int(a[3]), h = int(a[4]);
    if (!b || x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > width || y + h > height) return 0;
    EndBatch();
    gpu.Sync(b->surface, false);
    std::vector<unsigned short> row(static_cast<size_t>(w));
    for (int r = 0; r < h; ++r)
    {
        ReadPixels(*b, x, y + r, w, 1, 0, row.data(), w);
        if (!host.Write(a[6] + unsigned(r) * a[5], row.data(), unsigned(w * 2))) return 0;
    }
    return 1;
}

unsigned Context::WriteRegion(const unsigned *a, Host &host)
{
    // (dst_buffer, dst_x, dst_y, src_format, src_width, src_height, src_stride, src_data)
    if (!open) return 0;
    const unsigned format = a[3];
    if (a[0] == AuxBufferId || (format >= 0xc && format <= 0xf)) return Unsupported("grLfbWriteRegion of depth", host);
    if (format == 0x80) return Unsupported("grLfbWriteRegion with GR_LFB_SRC_FMT_RLE16", host);
    Buffer *b = BufferOf(a[0]);
    const unsigned pixel = LfbPixelBytes(format);
    const int x = int(a[1]), y = int(a[2]), w = int(a[4]), h = int(a[5]);
    if (!b || !pixel || w <= 0 || h <= 0) return 0;
    EndBatch();
    gpu.Sync(b->surface, true);
    std::vector<unsigned char> row(size_t(w) * pixel);
    for (int r = 0; r < h; ++r)
    {
        if (!host.Read(a[7] + unsigned(r) * a[6], row.data(), unsigned(row.size()))) return 0;
        for (int c = 0; c < w; ++c)
        {
            bool alpha = false;
            unsigned argb = LfbToArgb(format, row.data() + c * pixel, alpha);
            if (pixel == 4)
            {
                unsigned raw;
                memcpy(&raw, row.data() + c * pixel, 4);
                argb = Argb(raw) | (format == 0x4 ? 0xff000000u : 0);
            }
            WritePixel(*b, x + c, y + r, 0, argb, alpha);
        }
    }
    if (a[0] == FrontBufferId) front_dirty = true;
    ++counted.lfb_writes;
    return 1;
}

unsigned Context::Unsupported(const char *name, Host &host)
{
    char text[200];
    snprintf(text, sizeof text, "GLIDE2X.DLL: %s is not built ", name);
    host.Fail(text);
    return 0;
}

#include "glide_utilities.inc"

unsigned Context::Call(unsigned ordinal, const unsigned *a, Host &host)
{
    // A picture drawn straight into the front buffer reaches the screen at
    // the refresh rate, as the monitor would read it.
    if (front_dirty && open && host.Microseconds() - last_present > 16000)
    {
        EndBatch();
        PresentFront(host);
    }
    // Every call that changes the pixel state sends what is queued under the
    // old state first.
    auto change = [this]() {
        Flush();
        dirty = true;
    };
    switch (ordinal)
    {
    // Start, card and window.
    case grGlideInit:
        initialised = true;
        UtilityCall(guTexMemReset, nullptr, host);
        return 0;
    case grGlideShutdown:
        Close(host);
        initialised = false;
        return 0;
    case grGlideGetVersion:
    {
        char text[80] = {};
        snprintf(text, sizeof text, "%s", Card::Version);
        host.Write(a[0], text, unsigned(strlen(text) + 1));
        return 0;
    }
    case grSstQueryHardware:
    case grSstQueryBoards:
    {
        // GrHwConfiguration: num_sst, then four boards of 36 bytes: type,
        // fbRam, fbiRev, nTexelfx, sliDetect, and tmuRev, tmuRam per TMU.
        unsigned words[37] = {};
        words[0] = 1;
        words[1] = Card::Type;
        words[2] = Card::FrameBufferMegabytes;
        words[3] = Card::FbiRevision;
        words[4] = Card::Tmus;
        words[5] = 0;
        for (unsigned t = 0; t < 2; ++t)
        {
            words[6 + 2 * t] = Card::TmuRevision;
            words[7 + 2 * t] = Card::TmuMegabytes;
        }
        if (ordinal == grSstQueryBoards) return host.Write(a[0], words, 4) ? 1 : 0;
        return host.Write(a[0], words, sizeof words) ? 1 : 0;
    }
    case grSstSelect:
        if (a[0] != 0) return Unsupported("grSstSelect of a second board", host);
        return 0;
    case grSstWinOpen:
        return OpenWindow(a, host) ? 1 : 0;
    case grSstWinClose:
        Close(host);
        return 0;
    case grSstControl:
        return 1;
    case grSstOrigin:
        EndBatch();
        origin = a[0] == 1 ? 1 : 0;
        dirty = true;
        return 0;
    case grSstScreenWidth:
        return unsigned(width);
    case grSstScreenHeight:
        return unsigned(height);
    case grSstIdle:
        EndBatch();
        return 0;
    case grSstIsBusy:
        return 0;
    case grSstStatus:
    {
        // The status register: FIFO space in bits 0 to 5 and 12 to 27, the
        // vertical retrace in bit 6, the busy bits 7 to 9 clear.
        const unsigned long long period = 1000000ull / 60, phase = host.Microseconds() % period;
        return 0x3fu | 0x0ffff000u | (phase < 1000 ? 0x40u : 0u);
    }
    case grSstVRetraceOn:
    {
        const unsigned long long period = 1000000ull / 60, phase = host.Microseconds() % period;
        return phase < 1000 ? 1 : 0;
    }
    case grSstVideoLine:
    {
        const unsigned long long period = 1000000ull / 60, phase = host.Microseconds() % period;
        return unsigned(phase * unsigned(height + 45) / period);
    }
    case grSstPerfStats:
    {
        // GrSstPerfStats_t: the card counted pixels; this driver counts
        // triangles only and reports no pixel counts.
        const unsigned words[5] = {0, 0, 0, 0, 0};
        host.Write(a[0], words, sizeof words);
        return 0;
    }
    case grSstResetPerfStats:
        return 0;
    case grTriStats:
    {
        const unsigned in = unsigned(triangles_in), out = unsigned(triangles_out);
        if (a[0]) host.Write(a[0], &in, 4);
        if (a[1]) host.Write(a[1], &out, 4);
        return 0;
    }
    case grResetTriStats:
        triangles_in = triangles_out = 0;
        return 0;
    case grErrorSetCallback:
        error_callback = a[0];
        return 0;
    case grHints:
        if (a[0] == 0)   // GR_HINT_STWHINT
        {
            change();
            hints = a[1];
        }
        return 0;
    case grGlideGetState:
    case grGlideSetState:
        return Unsupported(ordinal == grGlideGetState ? "grGlideGetState" : "grGlideSetState", host);

    // Buffers.
    case grBufferClear:
        if (!open) return 0;
        Clear(a[0], a[1], a[2]);
        return 0;
    case grBufferSwap:
        if (!open) return 0;
        Swap(a[0], host);
        return 0;
    case grBufferNumPending:
        return 0;
    case grRenderBuffer:
        if (a[0] > 1) return Unsupported("grRenderBuffer of a third buffer", host);
        EndBatch();
        render_buffer = a[0];
        return 0;
    case grClipWindow:
        change();
        clip[0] = int(a[0]);
        clip[1] = int(a[1]);
        clip[2] = int(a[2]);
        clip[3] = int(a[3]);
        return 0;
    case grColorMask:
        change();
        state.rgb_mask = a[0] != 0;
        state.alpha_mask = a[1] != 0;
        return 0;

    // Drawing.
    case guDrawTriangleWithClip:
    {
        if (!open) return 0;
        ClipVertex input[3];
        for (unsigned i = 0; i < 3; ++i)
            if (!a[i] || !host.Read(a[i], input[i].data(), VertexBytes)) return 0;
        const auto polygon = ClipTriangle(input, clip);
        if (polygon.size() < 3) return 0;
        const auto first = Vertex(reinterpret_cast<const unsigned char *>(polygon[0].data()));
        for (unsigned i = 1; i + 1 < polygon.size(); ++i)
            Triangle(first, Vertex(reinterpret_cast<const unsigned char *>(polygon[i].data())),
                     Vertex(reinterpret_cast<const unsigned char *>(polygon[i + 1].data())));
        return 0;
    }
    case grDrawTriangle:
    {
        if (!open) return 0;
        Gpu::GlideVertex v[3];
        for (unsigned i = 0; i < 3; ++i)
            if (!ReadVertex(a[i], v[i], host)) return 0;
        Triangle(v[0], v[1], v[2]);
        return 0;
    }
    case grDrawLine:
    {
        if (!open) return 0;
        Gpu::GlideVertex v[2];
        for (unsigned i = 0; i < 2; ++i)
            if (!ReadVertex(a[i], v[i], host)) return 0;
        ++counted.lines;
        Emit(Gpu::LineList, v, 2);
        return 0;
    }
    case grDrawPoint:
    {
        if (!open) return 0;
        Gpu::GlideVertex v;
        if (!ReadVertex(a[0], v, host)) return 0;
        ++counted.points;
        Emit(Gpu::PointList, &v, 1);
        return 0;
    }
    case grDrawPolygonVertexList:
    case grDrawPlanarPolygonVertexList:
    {
        // (nverts, vlist): a fan around the first vertex.
        if (!open || a[0] < 3 || a[0] > 4096) return 0;
        std::vector<unsigned char> raw(size_t(a[0]) * VertexBytes);
        if (!host.Read(a[1], raw.data(), unsigned(raw.size()))) return 0;
        const Gpu::GlideVertex first = Vertex(raw.data());
        for (unsigned i = 1; i + 1 < a[0]; ++i)
            Triangle(first, Vertex(raw.data() + i * VertexBytes), Vertex(raw.data() + (i + 1) * VertexBytes));
        return 0;
    }
    case grDrawPolygon:
    case grDrawPlanarPolygon:
    {
        // (nverts, ilist, vlist): the fan through an index list.
        if (!open || a[0] < 3 || a[0] > 4096) return 0;
        std::vector<int> index(a[0]);
        if (!host.Read(a[1], index.data(), a[0] * 4)) return 0;
        std::vector<Gpu::GlideVertex> v(a[0]);
        for (unsigned i = 0; i < a[0]; ++i)
            if (index[i] < 0 || !ReadVertex(a[2] + unsigned(index[i]) * VertexBytes, v[i], host)) return 0;
        for (unsigned i = 1; i + 1 < a[0]; ++i) Triangle(v[0], v[i], v[i + 1]);
        return 0;
    }

    // Combine units.
    case grColorCombine:
        change();
        state.color_function = a[0];
        state.color_factor = a[1];
        state.color_local = a[2];
        state.color_other = a[3];
        state.color_invert = a[4] != 0;
        return 0;
    case grAlphaCombine:
        change();
        state.alpha_function = a[0];
        state.alpha_factor = a[1];
        state.alpha_local = a[2];
        state.alpha_other = a[3];
        state.alpha_invert = a[4] != 0;
        return 0;
    case grTexCombine:
    {
        if (a[0] > 1) return 0;
        change();
        auto &t = state.tmu[a[0]];
        t.rgb_function = a[1];
        t.rgb_factor = a[2];
        t.alpha_function = a[3];
        t.alpha_factor = a[4];
        t.rgb_invert = a[5] != 0;
        t.alpha_invert = a[6] != 0;
        return 0;
    }
    case grTexCombineFunction:
    case guTexCombineFunction:
    {
        // GR_TEXTURECOMBINE_ as the grTexCombine it stands for.
        if (a[0] > 1) return 0;
        static const unsigned presets[11][4] = {
            {0x0, 0x0, 0, 0},   // ZERO
            {0x1, 0x0, 0, 0},   // DECAL
            {0x3, 0x8, 0, 0},   // OTHER
            {0x4, 0x8, 0, 0},   // ADD
            {0x3, 0x1, 0, 0},   // MULTIPLY
            {0x6, 0x8, 0, 0},   // SUBTRACT
            {0x7, 0xc, 0, 0},   // DETAIL
            {0x7, 0x4, 0, 0},   // DETAIL_OTHER
            {0x7, 0xd, 0, 0},   // TRILINEAR_ODD
            {0x7, 0x5, 0, 0},   // TRILINEAR_EVEN
            {0x0, 0x0, 1, 1},   // ONE
        };
        if (a[1] > 10) return Unsupported("grTexCombineFunction with an unknown function", host);
        change();
        auto &t = state.tmu[a[0]];
        const unsigned *p = presets[a[1]];
        t.rgb_function = t.alpha_function = p[0];
        t.rgb_factor = t.alpha_factor = p[1];
        t.rgb_invert = p[2] != 0;
        t.alpha_invert = p[3] != 0;
        return 0;
    }
    case guColorCombineFunction:
    {
        // GR_COLORCOMBINE_ as the grColorCombine it stands for: function,
        // factor, local, other, invert. The two DELTA0 forms iterate the
        // colour of the first vertex; this driver iterates all three.
        static const unsigned presets[17][5] = {
            {0x0, 0x0, 1, 2, 0},   // ZERO
            {0x1, 0x0, 1, 2, 0},   // CCRGB
            {0x1, 0x0, 0, 2, 0},   // ITRGB
            {0x1, 0x0, 0, 2, 0},   // ITRGB_DELTA0
            {0x3, 0x8, 1, 1, 0},   // DECAL_TEXTURE
            {0x3, 0x1, 1, 1, 0},   // TEXTURE_TIMES_CCRGB
            {0x3, 0x1, 0, 1, 0},   // TEXTURE_TIMES_ITRGB
            {0x3, 0x1, 0, 1, 0},   // TEXTURE_TIMES_ITRGB_DELTA0
            {0x5, 0x1, 0, 1, 0},   // TEXTURE_TIMES_ITRGB_ADD_ALPHA
            {0x3, 0x3, 1, 1, 0},   // TEXTURE_TIMES_ALPHA
            {0x4, 0x3, 0, 1, 0},   // TEXTURE_TIMES_ALPHA_ADD_ITRGB
            {0x4, 0x8, 0, 1, 0},   // TEXTURE_ADD_ITRGB
            {0x6, 0x8, 0, 1, 0},   // TEXTURE_SUB_ITRGB
            {0x7, 0x4, 1, 0, 0},   // CCRGB_BLEND_ITRGB_ON_TEXALPHA
            {0x4, 0x3, 0, 1, 0},   // DIFF_SPEC_A
            {0x5, 0x1, 0, 1, 0},   // DIFF_SPEC_B
            {0x0, 0x0, 1, 2, 1},   // ONE
        };
        if (a[0] > 16) return Unsupported("guColorCombineFunction with an unknown function", host);
        change();
        const unsigned *p = presets[a[0]];
        state.color_function = p[0];
        state.color_factor = p[1];
        state.color_local = p[2];
        state.color_other = p[3];
        state.color_invert = p[4] != 0;
        return 0;
    }
    case guAlphaSource:
    {
        // GR_ALPHASOURCE_ as the grAlphaCombine it stands for.
        static const unsigned presets[4][4] = {
            {0x1, 0x0, 1, 2},   // CC_ALPHA
            {0x1, 0x0, 0, 2},   // ITERATED_ALPHA
            {0x3, 0x8, 1, 1},   // TEXTURE_ALPHA
            {0x3, 0x1, 0, 1},   // TEXTURE_ALPHA_TIMES_ITERATED_ALPHA
        };
        if (a[0] > 3) return Unsupported("guAlphaSource with an unknown source", host);
        change();
        state.alpha_function = presets[a[0]][0];
        state.alpha_factor = presets[a[0]][1];
        state.alpha_local = presets[a[0]][2];
        state.alpha_other = presets[a[0]][3];
        state.alpha_invert = false;
        return 0;
    }
    case grConstantColorValue:
        change();
        state.constant_color = Argb(a[0]);
        return 0;
    case grConstantColorValue4:
    {
        change();
        auto channel = [](unsigned word) {
            const float f = Float(word);
            return unsigned(f < 0.0f ? 0.0f : f > 255.0f ? 255.0f : f);
        };
        state.constant_color = channel(a[0]) << 24 | channel(a[1]) << 16 | channel(a[2]) << 8 | channel(a[3]);
        return 0;
    }
    case grAlphaControlsITRGBLighting:
        if (a[0]) return Unsupported("grAlphaControlsITRGBLighting", host);
        return 0;

    // Pixel tests and blending.
    case grAlphaBlendFunction:
        change();
        state.rgb_source = a[0];
        state.rgb_destination = a[1];
        state.alpha_source = a[2];
        state.alpha_destination = a[3];
        return 0;
    case grAlphaTestFunction:
        change();
        state.alpha_test = a[0] & 7;
        return 0;
    case grAlphaTestReferenceValue:
        change();
        state.alpha_reference = a[0] & 255;
        return 0;
    case grChromakeyMode:
        change();
        state.chroma_key = a[0] != 0;
        return 0;
    case grChromakeyValue:
        change();
        state.chroma_value = Argb(a[0]) & 0xffffff;
        return 0;
    case grCullMode:
        cull = a[0];
        return 0;
    case grDitherMode:
        // The colour buffers have eight bits a channel; there is nothing to
        // dither.
        return 0;
    case grGammaCorrectionValue:
    {
        float value;
        memcpy(&value, &a[0], 4);
        if (!(value > 0.0f)) return Unsupported("grGammaCorrectionValue with a gamma that is not positive", host);
        SetGamma(value, host);
        return 0;
    }
    case grDisableAllEffects:
        change();
        state.rgb_source = state.alpha_source = 4;
        state.rgb_destination = state.alpha_destination = 0;
        state.alpha_test = 7;
        state.chroma_key = false;
        state.depth_mode = 0;
        state.fog_mode = 0;
        return 0;

    // Depth.
    case grDepthBufferMode:
        change();
        if (a[0] == 3 || a[0] == 4) return Unsupported("grDepthBufferMode comparing to the bias", host);
        state.depth_mode = a[0];
        return 0;
    case grDepthBufferFunction:
        change();
        state.depth_function = a[0] & 7;
        return 0;
    case grDepthMask:
        change();
        state.depth_mask = a[0] != 0;
        return 0;
    case grDepthBiasLevel:
        change();
        state.depth_bias = int(short(a[0] & 0xffff));
        return 0;

    // Fog.
    case grFogMode:
        change();
        state.fog_mode = a[0];
        return 0;
    case grFogColorValue:
        change();
        state.fog_color = Argb(a[0]) & 0xffffff;
        return 0;
    case grFogTable:
        change();
        host.Read(a[0], state.fog_table, 64);
        return 0;
    case guFogGenerateExp:
    case guFogGenerateExp2:
    case guFogGenerateLinear:
    {
        unsigned char table[64];
        for (int i = 0; i < 64; ++i)
        {
            const double w = std::pow(2.0, 3.0 + double(i >> 2)) / double(8 - (i & 3));
            double f;
            if (ordinal == guFogGenerateLinear)
            {
                const double near_w = Float(a[1]), far_w = Float(a[2]);
                f = far_w > near_w ? (w - near_w) / (far_w - near_w) : 0.0;
            }
            else
            {
                const double density = Float(a[1]);
                const double last = std::pow(2.0, 3.0 + 15.0) / 5.0;
                const double d = ordinal == guFogGenerateExp ? density * w : density * w * density * w;
                const double d_last = ordinal == guFogGenerateExp ? density * last : density * last * density * last;
                const double scale = 1.0 - std::exp(-d_last);
                f = scale > 0.0 ? (1.0 - std::exp(-d)) / scale : 0.0;
            }
            if (f < 0.0) f = 0.0;
            if (f > 1.0) f = 1.0;
            table[i] = (unsigned char)(f * 255.0);
        }
        host.Write(a[0], table, 64);
        return 0;
    }

    // Textures.
    case guTexAllocateMemory:
    case guTexChangeAttributes:
    case guTexDownloadMipMap:
    case guTexDownloadMipMapLevel:
    case guTexGetCurrentMipMap:
    case guTexGetMipMapInfo:
    case guTexMemQueryAvail:
    case guTexMemReset:
    case guTexSource:
        return UtilityCall(ordinal, a, host);
    case grTexMinAddress:
        return 0;
    case grTexMaxAddress:
        // The end of the TMU's memory less 16 bytes (100067d6).
        return Card::TmuBytes - 16;
    case grTexCalcMemRequired:
    {
        // (lodmin, lodmax, aspect, format): lodmin is the small level.
        unsigned bytes = 0;
        for (unsigned l = a[1]; l <= a[0] && l <= 8; ++l) bytes += LevelBytes(l, a[2], a[3]);
        return (bytes + 7) & ~7u;
    }
    case grTexTextureMemRequired:
    {
        // (evenOdd, info): GrTexInfo is smallLod, largeLod, aspectRatio,
        // format, data.
        unsigned info[4];
        if (!host.Read(a[1], info, sizeof info)) return 0;
        unsigned bytes = 0;
        for (unsigned l = info[1]; l <= info[0] && l <= 8; ++l)
            if (LevelPresent(l, info[1], a[0])) bytes += LevelBytes(l, info[2], info[3]);
        return (bytes + 7) & ~7u;
    }
    case grTexSource:
    {
        // (tmu, startAddress, evenOdd, info)
        if (a[0] > 1) return 0;
        unsigned info[4];
        if (!host.Read(a[3], info, sizeof info)) return 0;
        change();
        TmuSource &s = source[a[0]];
        s.valid = true;
        s.start = a[1];
        s.even_odd = a[2] & 3;
        s.small_lod = info[0];
        s.large_lod = info[1];
        s.aspect = info[2];
        s.format = info[3];
        if (s.large_lod > 8 || s.small_lod > 8 || s.aspect > 6 || s.format > 15) s.valid = false;
        return 0;
    }
    case grTexDownloadMipMap:
    {
        // (tmu, startAddress, evenOdd, info): every level from the large
        // to the small one follows the other in info->data.
        if (a[0] > 1) return 0;
        unsigned info[5];
        if (!host.Read(a[3], info, sizeof info)) return 0;
        const unsigned small = info[0], large = info[1], aspect = info[2], format = info[3];
        if (large > 8 || small > 8 || aspect > 6) return 0;
        unsigned data = info[4];
        for (unsigned l = large; l <= small; ++l)
        {
            int w, h;
            LevelSize(l, aspect, w, h);
            const unsigned bytes = unsigned(w * h) * TexelBytes(format);
            if (LevelPresent(l, large, a[2] & 3))
            {
                std::vector<unsigned char> level(bytes);
                if (!host.Read(data, level.data(), bytes)) return 0;
                Download(a[0], a[1] + LevelOffset(l, large, aspect, format, a[2] & 3), level.data(), bytes);
            }
            data += bytes;
        }
        return 0;
    }
    case grTexDownloadMipMapLevel:
    case grTexDownloadMipMapLevelPartial:
    {
        // (tmu, startAddress, thisLod, largeLod, aspectRatio, format,
        // evenOdd, data[, t, max_t]); data is the first row to load.
        if (a[0] > 1) return 0;
        const unsigned lod = a[2], large = a[3], aspect = a[4], format = a[5], even_odd = a[6] & 3;
        if (lod > 8 || large > 8 || aspect > 6) return 0;
        int w, h;
        LevelSize(lod, aspect, w, h);
        unsigned first_row = 0, last_row = unsigned(h - 1);
        if (ordinal == grTexDownloadMipMapLevelPartial)
        {
            first_row = a[8];
            last_row = a[9];
            if (last_row >= unsigned(h) || first_row > last_row) return 0;
        }
        const unsigned row_bytes = unsigned(w) * TexelBytes(format);
        const unsigned bytes = (last_row - first_row + 1) * row_bytes;
        std::vector<unsigned char> rows(bytes);
        if (!host.Read(a[7], rows.data(), bytes)) return 0;
        Download(a[0], a[1] + LevelOffset(lod, large, aspect, format, even_odd) + first_row * row_bytes, rows.data(), bytes);
        return 0;
    }
    case grTexDownloadTable:
    case grTexDownloadTablePartial:
    {
        // (tmu, type, data[, start, end]): GR_TEXTABLE_NCC0 0, NCC1 1,
        // PALETTE 2. A palette is 256 FxU32; a GuNccTable has its packed
        // registers at byte 64.
        if (a[0] > 1) return 0;
        const unsigned tmu = a[0];
        change();
        if (a[1] == 2)
        {
            unsigned first = 0, last = 255;
            if (ordinal == grTexDownloadTablePartial)
            {
                first = a[3];
                last = a[4];
                if (last > 255 || first > last) return 0;
            }
            // The partial form points at the whole palette as well.
            if (!host.Read(a[2] + first * 4, palette[tmu] + first, (last - first + 1) * 4)) return 0;
        }
        else if (a[1] <= 1)
        {
            if (ordinal == grTexDownloadTablePartial) return Unsupported("grTexDownloadTablePartial of an NCC table", host);
            if (!host.Read(a[2] + 64, ncc[tmu][a[1]], sizeof ncc[tmu][a[1]])) return 0;
        }
        else return 0;
        // FNV-1a over the table the download changed.
        auto hash = [](const unsigned *words, unsigned count) {
            unsigned h = 2166136261u;
            for (unsigned i = 0; i < count; ++i) h = (h ^ words[i]) * 16777619u;
            return h;
        };
        if (a[1] == 2) palette_hash[tmu] = hash(palette[tmu], 256);
        else ncc_hash[tmu][a[1]] = hash(ncc[tmu][a[1]], 12);
        return 0;
    }
    case grTexNCCTable:
        if (a[0] > 1) return 0;
        change();
        ncc_select[a[0]] = a[1] & 1;
        return 0;
    case grTexFilterMode:
        if (a[0] > 1) return 0;
        change();
        state.tmu[a[0]].min_bilinear = a[1] != 0;
        state.tmu[a[0]].mag_bilinear = a[2] != 0;
        return 0;
    case grTexClampMode:
        if (a[0] > 1) return 0;
        change();
        state.tmu[a[0]].clamp_s = a[1] != 0;
        state.tmu[a[0]].clamp_t = a[2] != 0;
        return 0;
    case grTexMipMapMode:
        // (tmu, mode, lodBlend): trilinear blending needs a second pass or
        // the second TMU on the card; the mode picks the nearest level.
        if (a[0] > 1) return 0;
        change();
        state.tmu[a[0]].mipmap = a[1];
        return 0;
    case grTexLodBiasValue:
        if (a[0] > 1) return 0;
        change();
        state.tmu[a[0]].lod_bias = Float(a[1]);
        return 0;
    case grTexDetailControl:
        if (a[0] > 1) return 0;
        change();
        state.tmu[a[0]].detail_bias = int(a[1]);
        state.tmu[a[0]].detail_scale = a[2] & 7;
        state.tmu[a[0]].detail_max = Float(a[3]);
        return 0;

    // The linear frame buffer.
    case grLfbLock:
        return Lock(a, host);
    case grLfbUnlock:
        return Unlock(a[0], a[1], host);
    case grLfbReadRegion:
        return ReadRegion(a, host);
    case grLfbWriteRegion:
        return WriteRegion(a, host);

    default:
        break;
    }
    const char *name = "an export";
    for (const auto &e : Exports)
        if (e.ordinal == ordinal) name = e.name;
    return Unsupported(name, host);
}
} // namespace Glide
