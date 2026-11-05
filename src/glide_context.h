// SPDX-License-Identifier: GPL-2.0-or-later
// The state machine of the Glide 2 driver: the combine units, the depth and
// fog of a 3dfx card, the texture memory of its two TMUs as bytes, the colour
// buffers and the linear frame buffer, drawn through Gpu::Backend::DrawGlide.
// It knows neither Unicorn nor the guest address space; guest memory, the
// screen and the clock go through Glide::Host, which src/x86_glide.cpp
// implements.
#pragma once
#include "gpu_backend.h"
#include <map>
#include <memory>
#include <vector>

namespace Glide
{
class Host
{
  public:
    virtual ~Host() {}
    virtual bool Read(unsigned address, void *out, unsigned bytes) = 0;
    virtual bool Write(unsigned address, const void *in, unsigned bytes) = 0;
    // Guest memory for the linear frame buffer, or 0.
    virtual unsigned Allocate(unsigned bytes) = 0;
    // The display mode of the card's resolution, and back.
    virtual bool OpenScreen(int width, int height) = 0;
    virtual void CloseScreen() = 0;
    // The front buffer as the monitor shows it: BGRA at the program's
    // resolution, which the host puts into the format of its screen, and
    // the same picture at the resolution the driver renders at.
    virtual void Present(const unsigned *bits, int width, int height, const unsigned *full, int full_width, int full_height) = 0;
    // Target pixels per screen pixel for a window of this size, 1 to 8.
    virtual int Scale(int width, int height) = 0;
    // Waits for the next vertical blank.
    virtual void Pace() = 0;
    virtual unsigned long long Microseconds() = 0;
    // Stops the program with a diagnosis; nothing is done in its place.
    virtual void Fail(const char *message) = 0;
    // A line of NEO98_TRACE_GLIDE.
    virtual void Trace(const char *line) = 0;
};

// What grSstQueryHardware reports.
struct Card
{
    // What GLIDE2X.DLL 2.61 of the tree's Voodoo 3 enters for a card with
    // 8 MB (10004052..10004144): GR_SSTTYPE_VOODOO, the frame buffer the
    // memory less two TMUs of 2 MB, revision 0x1002 for FBI and TMUs. Glide 2
    // on a Voodoo 3 shows a Voodoo Graphics. The MiniGL of the Quake II demo
    // takes only that type and only 2 or 4 MB of frame buffer (3dfxgl.dll
    // 10003060..1000323e); with 16 MB it refuses the card, as it did on the
    // real one.
    static constexpr unsigned Type = 0;             // GR_SSTTYPE_VOODOO
    static constexpr unsigned FrameBufferMegabytes = 4;
    static constexpr unsigned FbiRevision = 0x1002, TmuRevision = 0x1002;
    static constexpr unsigned Tmus = 2;
    static constexpr unsigned TmuMegabytes = 2;
    static constexpr unsigned TmuBytes = TmuMegabytes << 20;
    static constexpr const char *Version = "2.61.00.0658";
};

class Context
{
  public:
    Context(Gpu::Backend &gpu, unsigned owner);
    ~Context();
    // One export of GLIDE2X.DLL by its ordinal (src/win98_glide_exports.h)
    // with its stack words.
    unsigned Call(unsigned ordinal, const unsigned *args, Host &host);
    // Whether a window is open; grSstWinClose and the end of the process
    // close it.
    bool Open() const { return open; }
    void Close(Host &host);
    // Before a saved state: what the rasterizer holds of the two colour
    // buffers and of the depth buffer goes into their bytes.
    void StateSync();

  private:
    struct Buffer
    {
        std::vector<unsigned> pixels;   // BGRA, at the target resolution
        Gpu::Surface surface;
    };
    // A decoded texture of the texture memory, by where it lies and how it
    // is read.
    struct TextureKey
    {
        unsigned tmu, start, even_odd, small_lod, large_lod, aspect, format, table;
        bool operator<(const TextureKey &o) const;
    };
    struct Texture
    {
        std::vector<unsigned> texels;   // every level, largest first
        std::vector<unsigned> table;    // the palette or NCC table it was decoded with
        Gpu::Surface surface;
        unsigned long long generation = 0;   // the texture memory generation it was decoded at
        unsigned long long batch = 0;        // the batch that last sampled it
        unsigned long long used = 0;         // the frame that last sampled it
    };
    struct TmuSource
    {
        bool valid = false;
        unsigned start = 0, even_odd = 3, small_lod = 8, large_lod = 0, aspect = 3, format = 0;
    };
    struct Lfb
    {
        unsigned address = 0, bytes = 0;
        bool locked = false;
        unsigned buffer = 0, mode = 0, origin = 0;
        std::vector<unsigned char> snapshot;
    };
    struct UtilityInfo
    {
        unsigned board, valid, width, height, aspect, data, format, mipmap;
        unsigned mag_filter, min_filter, s_clamp, t_clamp, t_lod, texture_mode, bias;
        unsigned small_lod, large_lod, tmu, even_odd, base, trilinear;
        unsigned ncc[28];
    };
    unsigned UtilityTable(Host &host);
    static void UtilityRegisters(UtilityInfo &info, bool allocation);
    unsigned UtilityCall(unsigned ordinal, const unsigned *a, Host &host);

    // Window and buffers.
    bool OpenWindow(const unsigned *a, Host &host);
    Buffer &RenderTarget();
    Buffer &FrontBuffer() { return color[front]; }
    Buffer &BackBuffer() { return color[front ^ 1]; }
    Buffer *BufferOf(unsigned buffer);
    void EnsureBatch();
    void EndBatch();
    void Swap(unsigned interval, Host &host);
    void PresentFront(Host &host);
    void Clear(unsigned color, unsigned alpha, unsigned depth);
    // Colour values in the format of grSstWinOpen, as ARGB.
    unsigned Argb(unsigned value) const;

    // Drawing.
    Gpu::GlideVertex Vertex(const unsigned char *raw) const;
    bool ReadVertex(unsigned address, Gpu::GlideVertex &out, Host &host);
    void Triangle(const Gpu::GlideVertex &a, const Gpu::GlideVertex &b, const Gpu::GlideVertex &c);
    void Emit(unsigned primitive, const Gpu::GlideVertex *v, unsigned count);
    void Flush();
    void Prepare();
    bool TmuSamplesTexel(unsigned tmu) const;

    // Textures.
    void Download(unsigned tmu, unsigned address, const unsigned char *data, unsigned bytes);
    unsigned LevelOffset(unsigned lod, unsigned large_lod, unsigned aspect, unsigned format, unsigned even_odd) const;
    const Gpu::Surface *Resolve(unsigned tmu);
    void Decode(unsigned tmu, const TextureKey &key, Texture &texture);
    bool LevelPresent(unsigned lod, unsigned first, unsigned even_odd) const;
    void Evict();
    void SetGamma(float value, Host &host);

    // The linear frame buffer.
    unsigned Lock(const unsigned *a, Host &host);
    unsigned Unlock(unsigned type, unsigned buffer, Host &host);
    void ReadPixels(Buffer &b, int x, int y, int width, int height, unsigned origin, unsigned short *out, int out_stride);
    void WritePixel(Buffer &b, int x, int y, unsigned origin, unsigned argb, bool alpha);
    unsigned ReadRegion(const unsigned *a, Host &host);
    unsigned WriteRegion(const unsigned *a, Host &host);
    static unsigned LfbToArgb(unsigned mode, const unsigned char *p, bool &has_alpha);
    static unsigned LfbPixelBytes(unsigned mode);

    unsigned Unsupported(const char *name, Host &host);

    Gpu::Backend &gpu;
    unsigned owner;
    bool initialised = false, open = false;
    int width = 0, height = 0;
    int target_width = 0, target_height = 0;
    float scale = 1.0f;
    unsigned origin = 0;              // GR_ORIGIN_UPPER_LEFT 0, LOWER_LEFT 1
    unsigned color_format = 0;        // GR_COLORFORMAT_
    Buffer color[2];
    unsigned front = 0;
    unsigned render_buffer = 1;       // GR_BUFFER_BACKBUFFER
    std::vector<unsigned short> depth_bytes;
    Gpu::Surface depth;
    std::vector<unsigned> picture;   // the front buffer at the program's resolution
    // The gamma table of the video output (grGammaCorrectionValue). It works
    // between the frame buffer and the monitor, so the frame buffer, the LFB
    // and the dump keep the values drawn; `shown` is the full picture after it.
    unsigned char gamma[256];
    bool gamma_linear = true;
    std::vector<unsigned> shown;
    unsigned long long frame = 0, batch = 0, last_present = 0;
    bool batch_open = false, front_dirty = false;

    Gpu::GlideState state;
    int clip[4] = {0, 0, 0, 0};       // grClipWindow in screen pixels
    unsigned cull = 0;                // GR_CULL_
    unsigned hints = 0;               // GR_HINT_STWHINT
    unsigned alpha_bits = 0;          // grAlphaControlsITRGBLighting
    bool dirty = true;
    std::vector<Gpu::GlideVertex> pending;
    unsigned pending_primitive = 0;
    unsigned error_callback = 0;
    unsigned long long triangles_in = 0, triangles_out = 0;
    // NEO98_TRACE_GLIDE: what one frame did, written at its swap.
    bool trace = false;
    struct { unsigned triangles, lines, points, clears, lfb_writes, lfb_reads, downloads, decodes; } counted = {};

    std::vector<unsigned char> memory[2];
    // The generation of every 4 KiB page of the texture memory: the count of
    // the download that last wrote it.
    std::vector<unsigned long long> pages[2];
    unsigned long long memory_generation = 1;
    TmuSource source[2];
    unsigned palette[2][256] = {};
    unsigned ncc[2][2][12] = {};      // the packed NCC tables of each TMU
    unsigned ncc_select[2] = {};
    unsigned utility_table = 0, utility_count = 0;
    unsigned utility_used[2] = {}, utility_current[2] = {~0u, ~0u};
    unsigned utility_ncc[2][2] = {{~0u, ~0u}, {~0u, ~0u}}, utility_ncc_next[2] = {};
    // A hash of the palette and of the selected NCC table of each TMU. A
    // decoded texture keys on it, so a program that downloads the palette
    // of each texture before it draws with it finds the decoded one again.
    unsigned palette_hash[2] = {}, ncc_hash[2][2] = {};
    std::map<TextureKey, std::unique_ptr<Texture>> textures;
    unsigned next_handle = 1;
    const Gpu::Surface *bound[2] = {nullptr, nullptr};

    Lfb lfb[2];                       // GR_LFB_READ_ONLY 0, GR_LFB_WRITE_ONLY 1
};
} // namespace Glide
