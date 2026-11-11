// SPDX-License-Identifier: GPL-2.0-or-later
// The DirectDraw HAL and the Direct3D HAL of the NEO98 display driver.
// With a rasterizer installed (Application::SetGpuBackend, on macOS the
// Metal provider in tools/host_metal.mm) the display driver stops refusing
// the four DDHAL commands of the DCI escape and answers them the way a
// Windows 98 driver with an accelerator does: DDVERSIONINFO names the driver
// model, DDCREATEDRIVEROBJECT builds the driver object, and DDRAW16 index
// 0x30 hands DDRAW the DDHALINFO with the callback tables, the video memory
// heap, the mode list and the Direct3D data. The callbacks are gates of the
// pseudo module NEO98DD32.DLL, the 32-bit half of the driver, and end in
// DisplayDriverApi below.
// Video memory is a lease of host memory the guest maps, like the screen
// surface. The driver sizes every surface (CreateSurface) and DDRAW's heap
// manager cuts the block out of the lease, so every pointer a program gets
// from Lock is real memory. The primary surface is the screen surface itself.
// A flip moves the scanout of the screen to the memory of the back buffer,
// the way a card changes its display start address. Every blit into video
// memory is the driver's own (HalBlt), so every write into that memory comes
// through Lock, Unlock or a blit, and the driver knows about it.
// The Direct3D side takes the DrawPrimitives2 command stream of D3DIM700.DLL,
// keeps the render and texture stage states of each context, and hands the
// primitives to the backend with the state they run under. The backend keeps
// a render target on the GPU between batches and writes it back into the
// bytes when the CPU is about to look (Sync). The device is not a
// transform-and-lighting device: D3DIM transforms, lights and clips, and the
// vertices arrive in screen space.
// The numbers and their sources are in src/win98_ddhal.h.
#include "win98_ddhal.h"
#include "direct3d_state.h"
#include "win98_ddraw_driver.h"
#include "win98_display_modes.h"
#include "gpu_backend.h"
#include "gdi.h"
#include "user.h"
#include "x86_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace X86
{
namespace H = Win98DDHal;
namespace Modes = Win98DisplayModes;

// An object of the host: a loaded state keeps the one of its own process.
Gpu::Backend *Application::gpu_backend = nullptr;
void Application::SetGpuBackend(Gpu::Backend *backend)
{
    gpu_backend = backend;
    Gdi::SetSurfaceAccessSink(backend ? +[](const unsigned char *bits, bool write) {
        if (gpu_backend) gpu_backend->CpuAccess(bits, write);
    } : nullptr);
}
bool Application::DirectDrawHalAvailable() { return gpu_backend != nullptr; }

namespace
{
constexpr const char *DriverModule = "NEO98DD32.DLL";
// The texture and Z formats the driver offers and the sizes of compressed
// surfaces are shared with the driver of ARM64 processes (direct3d_state.h).
using Direct3DState::BlockBytes;
using Direct3DState::BlockPitch;
using Direct3DState::CompressedFormatCount;
using Direct3DState::Format;
using Direct3DState::LinearSize;
using Direct3DState::TextureFormatCount;
using Direct3DState::ZFormatCount;
using Direct3DState::texture_formats;
using Direct3DState::z_formats;
constexpr unsigned SurfaceDescSize = 0x6c, SurfaceDescFlags = 0x1 | 0x1000;   // DDSD_CAPS | DDSD_PIXELFORMAT

// The format list of the DirectX 8 and 9 runtime: a D3DFORMAT and what the
// driver does with it; every texture format of the DX7 list is here, and the
// three Z formats. The formats of the display (5-5-5 at sixteen bits) carry
// DISPLAYMODE and 3DACCELERATION; D3D9 0x42bd5e drops the whole list when a
// format has the second without the first.
struct D3DFormat
{
    unsigned format, operations;
};
namespace Op = H::FormatOp;
constexpr unsigned Surface3D = Op::Texture | Op::VolumeTexture | Op::CubeTexture | Op::OffscreenPlain;
const D3DFormat d3d_formats[] = {
    {22, Op::DisplayMode | Op::Acceleration | Surface3D | Op::OffscreenRenderTarget | Op::SameFormatRenderTarget},   // X8R8G8B8
    {24, Op::DisplayMode | Op::Acceleration | Surface3D | Op::OffscreenRenderTarget | Op::SameFormatRenderTarget},   // X1R5G5B5
    {21, Surface3D | Op::OffscreenRenderTarget | Op::SameFormatUpToAlpha},   // A8R8G8B8
    {23, Surface3D | Op::OffscreenRenderTarget},                            // R5G6B5
    {25, Surface3D | Op::SameFormatUpToAlpha},                              // A1R5G5B5
    {26, Surface3D},                                                        // A4R4G4B4
    {41, Op::Texture | Op::CubeTexture},                                    // P8
    {60, Op::Texture | Op::BumpMap},                                        // V8U8
    {61, Op::Texture | Op::BumpMap},                                        // L6V5U5
    {62, Op::Texture | Op::BumpMap},                                        // X8L8V8U8
    {H::FourCC::Dxt1, Op::Texture | Op::CubeTexture},
    {H::FourCC::Dxt2, Op::Texture | Op::CubeTexture},
    {H::FourCC::Dxt3, Op::Texture | Op::CubeTexture},
    {H::FourCC::Dxt4, Op::Texture | Op::CubeTexture},
    {H::FourCC::Dxt5, Op::Texture | Op::CubeTexture},
    {80, Op::ZStencil | Op::ZStencilAnyColor},   // D16
    {71, Op::ZStencil | Op::ZStencilAnyColor},   // D32
    {75, Op::ZStencil | Op::ZStencilAnyColor},   // D24S8
};
constexpr unsigned D3DFormatCount = sizeof d3d_formats / sizeof d3d_formats[0];

// The pixel format of the display in a mode: masks of the DIB engine, 5-5-5
// at sixteen bits (src/win98_display_modes.h).
void DisplayFormat(int depth, Format &f)
{
    f = {H::PixelFormat::RGB, unsigned(depth), 0, 0, 0, 0};
    if (depth == 8) f.flags |= H::PixelFormat::Indexed8;
    else if (depth == 16) { f.r = 0x7c00; f.g = 0x03e0; f.b = 0x001f; }
    else { f.r = 0xff0000; f.g = 0xff00; f.b = 0xff; }
}

// A DDPIXELFORMAT out of guest bytes.
Format FormatOf(const unsigned char *pf)
{
    auto get = [&](unsigned at) {
        return unsigned(pf[at]) | (unsigned(pf[at + 1]) << 8) | (unsigned(pf[at + 2]) << 16) | (unsigned(pf[at + 3]) << 24);
    };
    Format f = {get(H::PixelFormat::Flags), get(H::PixelFormat::BitCount), get(H::PixelFormat::RMask),
                get(H::PixelFormat::GMask), get(H::PixelFormat::BMask), get(H::PixelFormat::AMask)};
    if (f.flags & H::PixelFormat::FourCCFlag) f.fourcc = get(H::PixelFormat::FourCC);
    return f;
}
bool SameFormat(const Format &a, const Format &b)
{
    const unsigned kind = H::PixelFormat::RGB | H::PixelFormat::Indexed8 | H::PixelFormat::ZBuffer | H::PixelFormat::AlphaPixels |
                          H::PixelFormat::StencilBuffer | H::PixelFormat::BumpDuDv | H::PixelFormat::BumpLuminance;
    if ((a.flags & kind) != (b.flags & kind) || a.bits != b.bits) return false;
    if (a.flags & H::PixelFormat::Indexed8) return true;
    if (a.flags & H::PixelFormat::ZBuffer) return a.g == b.g && a.r == b.r;   // dwZBitMask, dwStencilBitDepth
    return a.r == b.r && a.g == b.g && a.b == b.b && ((a.flags & H::PixelFormat::AlphaPixels) ? a.a == b.a : true);
}
// Whether the driver keeps surfaces of this format in video memory: the
// display format, every texture format it lists and every Z format.
bool DriverFormat(const Format &f, int display_depth, bool compressed)
{
    if (f.flags & H::PixelFormat::FourCCFlag) return compressed && BlockBytes(f.fourcc) != 0;
    Format display;
    DisplayFormat(display_depth, display);
    if (SameFormat(f, display)) return true;
    for (const auto &t : texture_formats)
        if (!t.fourcc && SameFormat(f, t)) return true;
    for (const auto &z : z_formats)
        if (SameFormat(f, z)) return true;
    // D3DFMT_D24S8 of the DirectX 8 and 9 runtime: it describes the surface
    // with the depth in the upper 24 bits and the stencil below, the other
    // way round than the format of the DirectX 7 list (3D Driving-School 5
    // opens its device with this Z buffer). The rasterizer uses
    // the masks when synchronizing packed depth and stencil bytes.
    constexpr Format runtime_d24s8 = {H::PixelFormat::ZBuffer | H::PixelFormat::StencilBuffer, 32, 8, 0xffffff00u, 0x000000ffu, 0};
    return SameFormat(f, runtime_d24s8);
}
// The pitch this driver gives a surface: whole pixels, sixteen-byte rows.
unsigned DriverPitch(unsigned width, unsigned bits) { return (width * ((bits + 7) / 8) + 15) & ~15u; }
} // namespace

namespace
{
// The keys of a state block: 256 render states, 8x32 stage states, 14
// transforms (world 0..3, view, projection, texture 0..7), 32 lights' data,
// 32 lights' enable, the material, 6 clip planes, viewport, Z range and W
// range.
enum StateKey : unsigned
{
    KeyRender = 0, KeyStage = 256, KeyTransform = 512, KeyLight = 526, KeyLightEnable = 558,
    KeyMaterial = 590, KeyClip = 591, KeyViewport = 597, KeyZRange = 598, KeyWInfo = 599, StateKeys = 600
};
} // namespace

struct Application::HalSurface
{
    unsigned handle = 0, lcl = 0, local = 0, generation = 1, palette = 0;
    bool legacy_handle = false;
    unsigned legacy_references = 0;
    // A copy of the records in the driver's own memory, for a surface whose
    // records live only as long as CreateSurfaceEx; lcl names the copy.
    unsigned shadow = 0;
    // Lightweight DX9 mip levels have no runtime records. Their private
    // descriptors share the root's allocation, handle and generation.
    unsigned shadow_levels = 0, shadow_capacity = 0;
};
static constexpr unsigned HalShadowNodes = 8;
static constexpr unsigned HalShadowBytes = H::LocalBytes + H::GlobalBytes + H::MoreBytes + HalShadowNodes * 16;
static bool Grow(unsigned char *&buffer, unsigned &capacity, unsigned bytes);
// The bytes of an element of a D3DDECLTYPE.
static unsigned ElementBytes(unsigned type)
{
    static const unsigned sizes[17] = {4, 8, 12, 16, 4, 4, 4, 8, 4, 4, 8, 4, 8, 4, 4, 4, 8};
    return type < 17 ? sizes[type] : 0;
}

// A declared vertex shader: the D3DVSD declaration and the function (none
// for the fixed function pipeline fed through a declaration).
struct Application::HalVertexShader
{
    unsigned context = 0, handle = 0, declaration_count = 0, count = 0;
    unsigned *declaration = nullptr, *tokens = nullptr;
    unsigned long long key = 0;
};
// One element of a vertex layout: which stream, where in its vertex, into
// which input register, of which D3DDECLTYPE (the D3DVSDT_ of DirectX 8 are
// its first eight).
struct Application::HalElement
{
    unsigned stream = 0, offset = 0, reg = 0, type = 0;
};
// A vertex declaration of the DirectX 9 driver model: its D3DVERTEXELEMENT9,
// usage and usage index as usage | index << 8.
struct Application::HalDeclaration9
{
    unsigned context = 0, handle = 0, count = 0;
    HalElement elements[32];
    unsigned usage[32] = {};
};
// A list of words that grows on the runtime heap.
struct HalWords
{
    unsigned *items = nullptr;
    unsigned count = 0, capacity = 0;
    HalWords() = default;
    HalWords(const HalWords &) = delete;
    HalWords &operator=(const HalWords &) = delete;
    ~HalWords() { free(items); }
    void Clear() { count = 0; }
    bool Push(unsigned value)
    {
        if (count == capacity)
        {
            const unsigned grown = capacity ? capacity * 2 : 32;
            auto *more = static_cast<unsigned *>(realloc(items, grown * sizeof(unsigned)));
            if (!more) return false;
            items = more;
            capacity = grown;
        }
        items[count++] = value;
        return true;
    }
    void Erase(unsigned first, unsigned n)
    {
        if (first >= count) return;
        if (n > count - first) n = count - first;
        memmove(items + first, items + first + n, (count - first - n) * sizeof(unsigned));
        count -= n;
    }
};
struct Application::HalContext
{
    bool used = false;
    unsigned handle = 0, target = 0, z = 0, pid = 0, local = 0;
    Gpu::State state;
    // The DirectX 8 driver model: the vertex shader (an FVF code for the
    // fixed function pipeline), the vertex streams and the index buffer. A
    // stream without a buffer handle reads the vertices DrawPrimitives2
    // brings along (SETSTREAMSOURCEUM, or never set).
    unsigned vertex_shader = 0;
    // The DirectX 9 driver model: the declaration (or an FVF code) and the
    // vertex shader function set with SETVERTEXSHADERDECL and...FUNC.
    bool model9 = false;
    unsigned declaration9 = 0, function9 = 0;
    struct Stream
    {
        unsigned handle = 0, stride = 0, offset = 0;   // offset: bytes into the buffer (SETSTREAMSOURCE2)
    } streams[H::DP2::Streams];
    unsigned index_handle = 0, index_stride = 2;
    // The queries of the DirectX 9 model (id, D3DQUERYTYPE) and the ids of
    // the event queries whose end the next DrawPrimitives2 answers.
    // queries holds two words per query: id, type.
    HalWords queries, signalled;
};
// The driver object of one process.
struct Application::DirectDrawHal
{
    static constexpr unsigned HeapBytes = 64u << 20;
    // Surfaces grow without a limit a program would notice: Wonderland keeps
    // more than a thousand textures with their mip levels and system memory
    // copies. A handle finds its record through a small cache first.
    static constexpr unsigned ContextCapacity = 16, HandleCache = 4096;

    Gdi::DibLease heap{};
    // Guest blocks, flat addresses, and the 16:16 form of the ones DDRAW maps.
    unsigned info = 0, driver_callbacks = 0, surface_callbacks = 0, palette_callbacks = 0;
    unsigned misc2 = 0, callbacks3 = 0, d3d_callbacks = 0, global = 0, extended = 0;
    unsigned texture_list = 0, z_list = 0, modes = 0, heaps = 0;
    unsigned far_driver_callbacks = 0, far_surface_callbacks = 0, far_palette_callbacks = 0;
    unsigned far_modes = 0, far_heaps = 0, fourcc_list = 0, far_fourcc = 0, texture_formats = 0;
    unsigned mode_count = 0, objects = 0, driver_info_gate = 0;
    unsigned parse_unknown = 0;
    // The version of the DirectX 8 or 9 runtime that asked (DD_DXVERSION).
    unsigned dx_version = 0;
    // Scratch for command and vertex copies out of guest memory.
    unsigned char *commands = nullptr, *vertices = nullptr;
    unsigned command_capacity = 0, vertex_capacity = 0;
    unsigned short *indices = nullptr;
    unsigned index_capacity = 0;
    // Scratch for the vertices of one DirectX 8 draw out of a stream.
    unsigned char *stream_vertices = nullptr;
    unsigned stream_capacity = 0;
    // The pixel shaders of the DirectX 8 driver model, by context and
    // handle; the key counts every shader the driver was given.
    struct PixelShader
    {
        unsigned context = 0, handle = 0, count = 0;
        unsigned *tokens = nullptr;
        unsigned long long key = 0;
    };
    PixelShader *pixel_shaders = nullptr;
    unsigned pixel_shader_count = 0, pixel_shader_capacity = 0;
    // The declared vertex shaders; in the DirectX 9 model the functions,
    // without a declaration.
    HalVertexShader *vertex_shaders = nullptr;
    HalDeclaration9 *declarations9 = nullptr;
    unsigned declaration9_count = 0, declaration9_capacity = 0;
    unsigned vertex_shader_count = 0, vertex_shader_capacity = 0;
    unsigned long long shader_keys = 0;
    HalSurface *surfaces = nullptr;
    unsigned surface_count = 0, surface_capacity = 0;
    unsigned handle_cache[HandleCache] = {};   // handle modulo the size -> record index + 1
    unsigned lcl_cache[HandleCache] = {};      // LCL / 4 modulo the size -> record index + 1
    struct Palette
    {
        Palette *next = nullptr;
        unsigned handle = 0;
        unsigned colors[256];   // 0xAARRGGBB, the peFlags of the entry on top
        bool alpha = false;     // DDRAWIPAL_ALPHA: the top byte is the alpha of the entry
    };
    // The faces of the cube maps bound this batch, one set per stage.
    Gpu::Surface cube_faces[Gpu::State::Stages][6];
    Palette *palettes = nullptr;
    Palette *PaletteOf(unsigned handle, bool create = false)
    {
        for (auto *p = palettes; p; p = p->next) if (p->handle == handle) return p;
        if (!create) return nullptr;
        auto *p = static_cast<Palette *>(calloc(1, sizeof(Palette)));
        if (!p) return nullptr;
        p->handle = handle;
        p->next = palettes;
        palettes = p;
        return p;
    }
    // A state block: which states it holds (a mark per key, see StateKey)
    // and their values in a snapshot of the whole state. Recording marks a
    // key and copies its value in, executing copies the marked values into
    // the context, capturing copies them out of it.
    struct StateBlock
    {
        unsigned handle = 0;
        bool recording = false;
        Gpu::State *snapshot = nullptr;
        unsigned char marks[StateKeys] = {};
    };
    StateBlock blocks[32];
    unsigned recording = 0;   // handle of the block being recorded, or 0
    HalContext contexts[ContextCapacity];
    // Set by the Lock callback when the caller is about to write at least half
    // of the visible surface; the Unlock callback paces it.
    bool frame_lock = false;
    ~DirectDrawHal()
    {
        free(commands);
        free(vertices);
        free(indices);
        free(stream_vertices);
        for (unsigned i = 0; i < pixel_shader_count; ++i) free(pixel_shaders[i].tokens);
        free(pixel_shaders);
        for (unsigned i = 0; i < vertex_shader_count; ++i) { free(vertex_shaders[i].declaration); free(vertex_shaders[i].tokens); }
        free(vertex_shaders);
        free(declarations9);
        free(surfaces);
        while (palettes) { auto *next = palettes->next; free(palettes); palettes = next; }
        for (auto &b : blocks) delete b.snapshot;
    }
};

// A surface as a blit sees it: the pixel format, the geometry and the bytes.
// Video memory and the screen are host memory; a surface in system memory is
// the program's own memory, whose rows the blit copies in and out.
struct Application::HalPlane
{
    unsigned lcl = 0, fp = 0, caps = 0;
    int width = 0, height = 0, pitch = 0, bytes = 0;
    int slices = 1;                   // the images of a volume, pitch times height bytes each
    unsigned fourcc = 0, block = 0;   // a block compressed surface: bytes of one 4x4 block
    unsigned char *host = nullptr;
    Format format{};
};

bool Application::HalPlaneOf(unsigned lcl, HalPlane &p)
{
    p = {};
    if (!lcl) return false;
    const unsigned gbl = Read32(lcl + H::SurfaceLocal::Global);
    if (!gbl) return false;
    p.lcl = lcl;
    p.caps = Read32(lcl + H::SurfaceLocal::Caps);
    const unsigned size = Read32(gbl + H::SurfaceGlobal::Height);
    p.width = int(size >> 16);
    p.height = int(size & 0xffff);
    p.pitch = int(Read32(gbl + H::SurfaceGlobal::Pitch));
    p.fp = Read32(gbl + H::SurfaceGlobal::VidMem);
    if (Read32(lcl + H::SurfaceLocal::Flags) & H::SurfaceLocal::HasPixelFormat)
    {
        unsigned char pf[H::PixelFormat::Size] = {};
        Read(gbl + H::SurfaceGlobal::PixelFormat, pf, sizeof pf);
        p.format = FormatOf(pf);
    }
    else
    {
        int width = 0, height = 0, depth = 0;
        User::ScreenMode(width, height, depth);
        DisplayFormat(depth, p.format);
    }
    p.bytes = int((p.format.bits + 7) / 8);
    if (p.format.fourcc)
    {
        // Rows of 4x4 blocks; lPitch is the linear size, so the block row is
        // worked out here.
        p.fourcc = p.format.fourcc;
        p.block = BlockBytes(p.fourcc);
        if (!p.block || p.width <= 0 || p.height <= 0 || !p.fp) return false;
        p.pitch = int(BlockPitch(unsigned(p.width), p.fourcc));
        p.host = HalHostPointer(p.fp, LinearSize(unsigned(p.width), unsigned(p.height), p.fourcc));
        return true;
    }
    if (p.width <= 0 || p.height <= 0 || p.pitch <= 0 || p.bytes < 1 || p.bytes > 4 || !p.fp) return false;
    if (const unsigned slices = HalVolumeSlices(lcl)) p.slices = int(slices);
    p.host = HalHostPointer(p.fp, unsigned(p.pitch) * unsigned(p.height) * unsigned(p.slices));
    return true;
}

// The depth of a volume: DDSCAPS2_VOLUME in the ddsCapsEx of its MORE record
// and the depth in the low word of dwCaps4; zero for any other surface.
unsigned Application::HalVolumeSlices(unsigned lcl)
{
    const unsigned more = lcl ? Read32(lcl + H::SurfaceLocal::More) : 0;
    if (!more || !virtual_memory.Access(more + H::SurfaceMore::CapsEx, 12, false)) return 0;
    if (!(Read32(more + H::SurfaceMore::CapsEx) & H::DDSCAPS2_VOLUME)) return 0;
    const unsigned slices = Read32(more + H::SurfaceMore::Caps4) & 0xffff;
    return slices ? slices : 1;
}

// ---------------------------------------------------------------------------
// Guest memory helpers.

unsigned Application::HalGate(const char *name)
{
    return NewImport(DriverModule, name, 1);
}

// The host bytes behind a video memory address: the heap or the screen.
unsigned char *Application::HalHostPointer(unsigned flat, unsigned bytes)
{
    if (!ddhal) return nullptr;
    const auto &heap = ddhal->heap;
    if (heap.host && flat >= heap.guest && flat < heap.guest + heap.bytes && bytes <= heap.bytes - (flat - heap.guest))
        return heap.host + (flat - heap.guest);
    const unsigned screen = User::ScreenGuestAddress();
    if (screen && flat >= screen && flat < screen + User::ScreenBytes() && bytes <= User::ScreenBytes() - (flat - screen))
        return const_cast<unsigned char *>(User::ScreenBits()) + (flat - screen);
    return nullptr;
}

static void PutFormat(unsigned char *at, const Format &f)
{
    auto put = [&](unsigned offset, unsigned value) {
        at[offset] = value & 255; at[offset + 1] = (value >> 8) & 255;
        at[offset + 2] = (value >> 16) & 255; at[offset + 3] = value >> 24;
    };
    put(0, H::PixelFormat::Size);
    put(H::PixelFormat::Flags, f.flags);
    put(H::PixelFormat::FourCC, f.fourcc);
    put(H::PixelFormat::BitCount, f.bits);
    put(H::PixelFormat::RMask, f.r);
    put(H::PixelFormat::GMask, f.g);
    put(H::PixelFormat::BMask, f.b);
    put(H::PixelFormat::AMask, f.a);
}

// ---------------------------------------------------------------------------
// The driver object.

// DDCREATEDRIVEROBJECT. Builds the guest blocks once; DDRAW16 index 0x30
// refreshes the DDHALINFO out of them at every read.
bool Application::HalCreateDriverObject()
{
    if (!gpu_backend) return false;
    if (ddhal && ddhal->info) { ++ddhal->objects; return true; }
    if (!ddhal) ddhal = new DirectDrawHal;
    auto &h = *ddhal;
    // The video memory and the tables of the driver belong to the display
    // driver, not to a process. Under Win98 they lie in the shared arena (the
    // card's aperture and the driver's data), and DDRAW keeps pointers to
    // them in its driver object, which lives in its shared heap and outlasts
    // the process that made it. DDHELP cleans up after a process that ended
    // with its objects alive and calls D3DIM!Direct3D_HALCleanUp with the
    // D3D callback table of that object; a table in the private memory of
    // the dead game faulted there, the driver object stayed, and the next
    // DirectDraw program allocated from the dead game's video memory. So both are session views, the same bytes at the same
    // address in every process, made once and kept for the session.
    if (!h.heap.host)
    {
        const unsigned guest = ReserveSessionView("NEO98 DirectDraw video memory", DirectDrawHal::HeapBytes);
        unsigned char *host = guest && CommitSessionView(guest >> 12, DirectDrawHal::HeapBytes >> 12)
                                  ? SessionBytes(guest, DirectDrawHal::HeapBytes) : nullptr;
        if (!host)
        {
            DriverTrace("DDHAL video memory lease failed", DirectDrawHal::HeapBytes);
            return false;
        }
        h.heap = {guest, DirectDrawHal::HeapBytes, host, nullptr, nullptr};
    }
    unsigned count = 0;
    Modes::Table(count);
    h.mode_count = count;
    h.texture_formats = TextureFormatCount - (gpu_backend->CompressedTextures() ? 0 : CompressedFormatCount);
    const unsigned sizes[] = {H::Info::Size, H::DriverCallbacks::Size, H::SurfaceCallbacks::Size,
                              H::PaletteCallbacks::Size, H::Misc2Callbacks::Size, H::D3DCallbacks3::Size,
                              H::D3DCallbacks::Size, H::GlobalDriverData::Size, H::ExtendedCaps::Size,
                              SurfaceDescSize * TextureFormatCount, 4 * CompressedFormatCount,
                              H::PixelFormat::Size * ZFormatCount, H::ModeInfo::Size * count, H::Heap::Size};
    unsigned total = 0;
    for (unsigned size : sizes) total += (size + 15) & ~15u;
    const unsigned tables = ReserveSessionView("NEO98 DirectDraw driver tables", total);
    if (!tables || !CommitSessionView(tables >> 12, (total + 4095) >> 12))
    {
        DriverTrace("DDHAL guest blocks failed", total);
        return false;
    }
    unsigned next = tables, index = 0;
    auto take = [&]() { const unsigned at = next; next += (sizes[index++] + 15) & ~15u; return at; };
    h.info = take();
    h.driver_callbacks = take();
    h.surface_callbacks = take();
    h.palette_callbacks = take();
    h.misc2 = take();
    h.callbacks3 = take();
    h.d3d_callbacks = take();
    h.global = take();
    h.extended = take();
    h.texture_list = take();
    h.fourcc_list = take();
    h.z_list = take();
    h.modes = take();
    h.heaps = take();
    if (!h.info || !h.driver_callbacks || !h.surface_callbacks || !h.palette_callbacks || !h.misc2 ||
        !h.callbacks3 || !h.d3d_callbacks || !h.global || !h.extended || !h.texture_list || !h.z_list ||
        !h.modes || !h.heaps || !h.fourcc_list)
    {
        DriverTrace("DDHAL guest blocks failed", 0);
        h.info = 0;
        return false;
    }
    auto zero = [&](unsigned at, unsigned bytes) {
        static const unsigned char zeros[256] = {};
        while (bytes)
        {
            const unsigned n = bytes < sizeof zeros ? bytes : sizeof zeros;
            Store(at, zeros, n);
            at += n; bytes -= n;
        }
    };
    zero(h.info, H::Info::Size);
    zero(h.d3d_callbacks, H::D3DCallbacks::Size);
    zero(h.global, H::GlobalDriverData::Size);
    zero(h.extended, H::ExtendedCaps::Size);

    // DDHAL_DDCALLBACKS.
    zero(h.driver_callbacks, H::DriverCallbacks::Size);
    Write32(h.driver_callbacks, H::DriverCallbacks::Size);
    Write32(h.driver_callbacks + 4, H::DriverCallbacks::FlagDestroyDriver | H::DriverCallbacks::FlagSetMode |
                                        H::DriverCallbacks::FlagWaitForVerticalBlank |
                                        H::DriverCallbacks::FlagSetExclusiveMode | H::DriverCallbacks::FlagFlipToGDISurface |
                                        H::DriverCallbacks::FlagCanCreateSurface | H::DriverCallbacks::FlagCreateSurface);
    Write32(h.driver_callbacks + H::DriverCallbacks::DestroyDriver, HalGate("DestroyDriver"));
    Write32(h.driver_callbacks + H::DriverCallbacks::CanCreateSurface, HalGate("CanCreateSurface"));
    Write32(h.driver_callbacks + H::DriverCallbacks::CreateSurface, HalGate("CreateSurface"));
    Write32(h.driver_callbacks + H::DriverCallbacks::SetMode, HalGate("SetMode"));
    Write32(h.driver_callbacks + H::DriverCallbacks::WaitForVerticalBlank, HalGate("WaitForVerticalBlank"));
    Write32(h.driver_callbacks + H::DriverCallbacks::SetExclusiveMode, HalGate("SetExclusiveMode"));
    Write32(h.driver_callbacks + H::DriverCallbacks::FlipToGDISurface, HalGate("FlipToGDISurface"));
    // DDHAL_DDSURFACECALLBACKS.
    zero(h.surface_callbacks, H::SurfaceCallbacks::Size);
    Write32(h.surface_callbacks, H::SurfaceCallbacks::Size);
    Write32(h.surface_callbacks + 4, H::SurfaceCallbacks::FlagDestroySurface | H::SurfaceCallbacks::FlagFlip |
                                         H::SurfaceCallbacks::FlagLock | H::SurfaceCallbacks::FlagUnlock |
                                         H::SurfaceCallbacks::FlagGetBltStatus |
                                         H::SurfaceCallbacks::FlagGetFlipStatus | H::SurfaceCallbacks::FlagAddAttachedSurface |
                                         H::SurfaceCallbacks::FlagSetClipList | H::SurfaceCallbacks::FlagBlt);
    Write32(h.surface_callbacks + H::SurfaceCallbacks::DestroySurface, HalGate("DestroySurface"));
    Write32(h.surface_callbacks + H::SurfaceCallbacks::Blt, HalGate("Blt"));
    Write32(h.surface_callbacks + H::SurfaceCallbacks::Flip, HalGate("Flip"));
    Write32(h.surface_callbacks + H::SurfaceCallbacks::Lock, HalGate("Lock"));
    Write32(h.surface_callbacks + H::SurfaceCallbacks::Unlock, HalGate("Unlock"));
    Write32(h.surface_callbacks + H::SurfaceCallbacks::GetBltStatus, HalGate("GetBltStatus"));
    Write32(h.surface_callbacks + H::SurfaceCallbacks::GetFlipStatus, HalGate("GetFlipStatus"));
    Write32(h.surface_callbacks + H::SurfaceCallbacks::AddAttachedSurface, HalGate("AddAttachedSurface"));
    Write32(h.surface_callbacks + H::SurfaceCallbacks::SetClipList, HalGate("SetClipList"));
    // DDHAL_DDPALETTECALLBACKS: none; the palette of the primary keeps going
    // through GDI, like on the emulation path.
    zero(h.palette_callbacks, H::PaletteCallbacks::Size);
    Write32(h.palette_callbacks, H::PaletteCallbacks::Size);
    // DDHAL_DDMISCELLANEOUS2CALLBACKS.
    zero(h.misc2, H::Misc2Callbacks::Size);
    Write32(h.misc2, H::Misc2Callbacks::Size);
    Write32(h.misc2 + 4, H::Misc2Callbacks::FlagCreateSurfaceEx | H::Misc2Callbacks::FlagGetDriverState |
                             H::Misc2Callbacks::FlagDestroyDDLocal);
    Write32(h.misc2 + H::Misc2Callbacks::CreateSurfaceEx, HalGate("CreateSurfaceEx"));
    Write32(h.misc2 + H::Misc2Callbacks::GetDriverState, HalGate("GetDriverState"));
    Write32(h.misc2 + H::Misc2Callbacks::DestroyDDLocal, HalGate("DestroyDDLocal"));
    // D3DHAL_CALLBACKS3.
    zero(h.callbacks3, H::D3DCallbacks3::Size);
    Write32(h.callbacks3, H::D3DCallbacks3::Size);
    Write32(h.callbacks3 + H::D3DCallbacks3::Flags, H::D3DCallbacks3::FlagClear2 |
                                                        H::D3DCallbacks3::FlagValidateTextureStageState |
                                                        H::D3DCallbacks3::FlagDrawPrimitives2);
    Write32(h.callbacks3 + H::D3DCallbacks3::Clear2, HalGate("Clear2"));
    Write32(h.callbacks3 + H::D3DCallbacks3::ValidateTextureStageState, HalGate("ValidateTextureStageState"));
    Write32(h.callbacks3 + H::D3DCallbacks3::DrawPrimitives2, HalGate("DrawPrimitives2"));
    // D3DHAL_CALLBACKS: the context entries; everything of the execute buffer
    // model stays empty, this is a DrawPrimitives2 driver. Legacy runtimes
    // still need texture handles for their translated execute streams.
    Write32(h.d3d_callbacks, H::D3DCallbacks::Size);
    Write32(h.d3d_callbacks + H::D3DCallbacks::ContextCreate, HalGate("ContextCreate"));
    Write32(h.d3d_callbacks + H::D3DCallbacks::ContextDestroy, HalGate("ContextDestroy"));
    Write32(h.d3d_callbacks + H::D3DCallbacks::ContextDestroyAll, HalGate("ContextDestroyAll"));
    Write32(h.d3d_callbacks + H::D3DCallbacks::SceneCapture, HalGate("SceneCapture"));
    Write32(h.d3d_callbacks + H::D3DCallbacks::TextureCreate, HalGate("TextureCreate"));
    Write32(h.d3d_callbacks + H::D3DCallbacks::TextureDestroy, HalGate("TextureDestroy"));
    // The texture formats and the Z formats.
    for (unsigned i = 0; i < CompressedFormatCount; ++i)
        Write32(h.fourcc_list + i * 4, texture_formats[TextureFormatCount - CompressedFormatCount + i].fourcc);
    for (unsigned i = 0; i < h.texture_formats; ++i)
    {
        unsigned char desc[SurfaceDescSize] = {};
        desc[0] = SurfaceDescSize;
        desc[4] = SurfaceDescFlags & 255; desc[5] = (SurfaceDescFlags >> 8) & 255;
        PutFormat(desc + 0x48, texture_formats[i]);
        desc[0x68] = H::DDSCAPS_TEXTURE & 255; desc[0x69] = (H::DDSCAPS_TEXTURE >> 8) & 255;
        Store(h.texture_list + i * SurfaceDescSize, desc, SurfaceDescSize);
    }
    for (unsigned i = 0; i < ZFormatCount; ++i)
    {
        unsigned char pf[H::PixelFormat::Size] = {};
        PutFormat(pf, z_formats[i]);
        Store(h.z_list + i * H::PixelFormat::Size, pf, H::PixelFormat::Size);
    }
    // D3DHAL_GLOBALDRIVERDATA with the D3DDEVICEDESC_V1.
    {
        const unsigned g = h.global, d = g + H::GlobalDriverData::HwCaps;
        Write32(g, H::GlobalDriverData::Size);
        Write32(d, H::DeviceDesc::Size);
        Write32(d + H::DeviceDesc::Flags, H::DeviceDesc::AllFields);
        Write32(d + H::DeviceDesc::ColorModel, H::DeviceDesc::ColorRGB);
        Write32(d + H::DeviceDesc::DevCaps, H::DeviceDesc::DevCapsValue |
                                                (gpu_backend->TransformAndLight() ? H::DeviceDesc::HwTransformAndLight : 0));
        // D3DTRANSFORMCAPS: the GPU clips.
        Write32(d + H::DeviceDesc::TransformCaps, 8);
        Write32(d + H::DeviceDesc::TransformCaps + 4, H::DeviceDesc::TransformClip);
        Write32(d + H::DeviceDesc::Clipping, 0);
        // D3DLIGHTINGCAPS: point, spot and directional lights, the RGB model.
        Write32(d + H::DeviceDesc::LightingCaps, 16);
        Write32(d + H::DeviceDesc::LightingCaps + 4, H::DeviceDesc::LightCapsValue);
        Write32(d + H::DeviceDesc::LightingCaps + 8, 1);   // D3DLIGHTINGMODEL_RGB
        Write32(d + H::DeviceDesc::LightingCaps + 12, H::DeviceDesc::MaxLights);
        // The kernel has no <initializer_list>, so the two caps blocks are an array.
        const unsigned caps_blocks[2] = {H::DeviceDesc::LineCaps, H::DeviceDesc::TriCaps};
        for (unsigned caps : caps_blocks)
        {
            const unsigned p = d + caps;
            Write32(p, H::PrimCaps::Size);
            Write32(p + H::PrimCaps::MiscCaps, H::PrimCaps::MiscValue);
            Write32(p + H::PrimCaps::RasterCaps, gpu_backend->Limit(Gpu::Backend::Capability::Raster, H::PrimCaps::RasterValue));
            Write32(p + H::PrimCaps::ZCmpCaps, H::PrimCaps::CmpValue);
            Write32(p + H::PrimCaps::SrcBlendCaps, H::PrimCaps::BlendValue);
            Write32(p + H::PrimCaps::DestBlendCaps, H::PrimCaps::BlendValue);
            Write32(p + H::PrimCaps::AlphaCmpCaps, H::PrimCaps::CmpValue);
            Write32(p + H::PrimCaps::ShadeCaps, gpu_backend->Limit(Gpu::Backend::Capability::Shade, H::PrimCaps::ShadeValue));
            Write32(p + H::PrimCaps::TextureCaps, gpu_backend->Limit(Gpu::Backend::Capability::LegacyTexture, H::PrimCaps::TextureHalValue));
            Write32(p + H::PrimCaps::TextureFilterCaps, gpu_backend->Limit(Gpu::Backend::Capability::Filter, H::PrimCaps::FilterValue));
            Write32(p + H::PrimCaps::TextureBlendCaps, H::PrimCaps::TextureBlendValue);
            Write32(p + H::PrimCaps::TextureAddressCaps, H::PrimCaps::AddressValue);
            Write32(p + H::PrimCaps::StippleWidth, 0);
            Write32(p + H::PrimCaps::StippleHeight, 0);
        }
        Write32(d + H::DeviceDesc::RenderBitDepth, H::DDBD_16 | H::DDBD_32);
        Write32(d + H::DeviceDesc::ZBufferBitDepth, H::DDBD_16 | H::DDBD_32);
        Write32(d + H::DeviceDesc::MaxBufferSize, 0);
        // D3DIM!56675380 sizes its Execute scratch buffer from this cap,
        // then adds 32 TL vertices. Leave that headroom below the runtime's
        // 16-bit vertex-buffer limit (5666a463), also for state-only Execute.
        Write32(d + H::DeviceDesc::MaxVertexCount, 0xffff - 32);
        // Reserved members of D3DHAL_GLOBALDRIVERDATA must be zero.
        Write32(g + H::GlobalDriverData::NumVertices, 0);
        Write32(g + H::GlobalDriverData::NumClipVertices, 0);
        Write32(g + H::GlobalDriverData::NumTextureFormats, h.texture_formats);
        Write32(g + H::GlobalDriverData::TextureFormats, h.texture_list);
    }
    // D3DHAL_D3DEXTENDEDCAPS.
    {
        const unsigned e = h.extended;
        const float one = 1.0f, big = 1.0e10f;
        Write32(e, H::ExtendedCaps::Size);
        Write32(e + H::ExtendedCaps::MinTextureWidth, 1);
        Write32(e + H::ExtendedCaps::MaxTextureWidth, 4096);
        Write32(e + H::ExtendedCaps::MinTextureHeight, 1);
        Write32(e + H::ExtendedCaps::MaxTextureHeight, 4096);
        Write32(e + H::ExtendedCaps::MaxTextureRepeat, 8192);
        Write32(e + H::ExtendedCaps::MaxTextureAspectRatio, 0);
        Write32(e + H::ExtendedCaps::MaxAnisotropy, gpu_backend->Limit(Gpu::Backend::Capability::Anisotropy, H::ExtendedCaps::MaxAnisotropyValue));
        unsigned bits;
        memcpy(&bits, &one, 4);
        Write32(e + H::ExtendedCaps::StencilCaps, H::ExtendedCaps::StencilValue);
        Write32(e + H::ExtendedCaps::FVFCaps, H::ExtendedCaps::Stages);   // eight texture coordinate sets
        Write32(e + H::ExtendedCaps::TextureOpCaps, gpu_backend->Limit(Gpu::Backend::Capability::TextureOps, H::ExtendedCaps::TextureOpValue));
        Write32(e + H::ExtendedCaps::MaxTextureBlendStages, H::ExtendedCaps::Stages | (H::ExtendedCaps::Stages << 16));
        Write32(e + H::ExtendedCaps::MaxActiveLights, H::DeviceDesc::MaxLights);
        memcpy(&bits, &big, 4);
        Write32(e + H::ExtendedCaps::MaxVertexW, bits);
        // WORD wMaxUserClipPlanes, WORD wMaxVertexBlendMatrices.
        Write32(e + H::ExtendedCaps::MaxUserClipPlanes, H::ExtendedCaps::ClipPlanes | (H::ExtendedCaps::BlendMatrices << 16));
        Write32(e + H::ExtendedCaps::VertexProcessingCaps, H::ExtendedCaps::VertexProcessingValue);
    }
    // The mode list, the same linear modes the display offers.
    for (unsigned i = 0; i < count; ++i)
    {
        int width = 0, height = 0, depth = 0;
        User::ScreenModeAt(i, width, height, depth);
        const unsigned m = h.modes + i * H::ModeInfo::Size;
        Format f;
        DisplayFormat(depth, f);
        Write32(m + H::ModeInfo::Width, unsigned(width));
        Write32(m + H::ModeInfo::Height, unsigned(height));
        Write32(m + H::ModeInfo::Pitch, unsigned(Modes::Stride(width, depth)));
        Write32(m + H::ModeInfo::BPP, unsigned(depth));
        Write32(m + H::ModeInfo::Flags, depth == 8 ? H::ModeInfo::Palettized : 0);
        Write32(m + H::ModeInfo::RMask, f.r);
        Write32(m + H::ModeInfo::GMask, f.g);
        Write32(m + H::ModeInfo::BMask, f.b);
        Write32(m + H::ModeInfo::AMask, 0);
    }
    // The one linear heap: all of the lease.
    Write32(h.heaps + H::Heap::Flags, H::Heap::Linear);
    Write32(h.heaps + H::Heap::Start, h.heap.guest);
    Write32(h.heaps + H::Heap::End, h.heap.guest + h.heap.bytes - 1);
    Write32(h.heaps + H::Heap::Caps, 0);
    Write32(h.heaps + H::Heap::CapsAlt, 0);
    Write32(h.heaps + H::Heap::HeapPointer, 0);
    h.driver_info_gate = HalGate("GetDriverInfo");
    // The far pointers DDRAW maps back with MapSLFix (DDRAW 0x1cc54).
    h.far_driver_callbacks = SegmentMap(h.driver_callbacks);
    h.far_surface_callbacks = SegmentMap(h.surface_callbacks);
    h.far_palette_callbacks = SegmentMap(h.palette_callbacks);
    h.far_modes = SegmentMap(h.modes);
    h.far_fourcc = SegmentMap(h.fourcc_list);
    h.far_heaps = SegmentMap(h.heaps);
    ++h.objects;
    DriverTrace("DDHAL driver object", h.heap.guest);
    return true;
}

// The DDHALINFO as it stands now: the display mode may have changed since the
// last read, and DDRAW re-reads it after every SetMode.
bool Application::HalRefreshInfo()
{
    if (!ddhal || !ddhal->info) return false;
    auto &h = *ddhal;
    InstallScreenLease();
    if (!User::ScreenGuestAddress())
    {
        // The screen surface has no guest address until a lease is installed;
        // the primary surface needs one.
        int width = 0, height = 0, depth = 0;
        User::ScreenMode(width, height, depth);
        User::SetScreenMode(width, height, depth);
        if (!User::ScreenGuestAddress()) return false;
    }
    int width = 0, height = 0, depth = 0;
    User::ScreenMode(width, height, depth);
    const unsigned i = h.info;
    Write32(i, H::Info::Size);
    Write32(i + H::Info::Callbacks, h.far_driver_callbacks);
    Write32(i + H::Info::SurfaceCallbacks, h.far_surface_callbacks);
    Write32(i + H::Info::PaletteCallbacks, h.far_palette_callbacks);
    const unsigned v = i + H::Info::VidMem;
    Write32(v + H::VidMemInfo::Primary, User::ScreenGuestAddress());
    Write32(v + H::VidMemInfo::Flags, 0);
    Write32(v + H::VidMemInfo::DisplayWidth, unsigned(width));
    Write32(v + H::VidMemInfo::DisplayHeight, unsigned(height));
    Write32(v + H::VidMemInfo::DisplayPitch, unsigned(User::ScreenStride()));
    {
        Format f;
        DisplayFormat(depth, f);
        unsigned char pf[H::PixelFormat::Size];
        PutFormat(pf, f);
        Store(v + H::VidMemInfo::DisplayFormat, pf, sizeof pf);
    }
    Write32(v + H::VidMemInfo::OffscreenAlign, 16);
    Write32(v + H::VidMemInfo::OverlayAlign, 16);
    Write32(v + H::VidMemInfo::TextureAlign, 16);
    Write32(v + H::VidMemInfo::ZBufferAlign, 16);
    Write32(v + H::VidMemInfo::AlphaAlign, 16);
    Write32(v + H::VidMemInfo::HeapCount, 1);
    Write32(v + H::VidMemInfo::Heaps, h.far_heaps);
    // DDCORECAPS.
    const unsigned c = i + H::Info::Caps;
    Write32(c, H::Caps::Size);
    // Advertising DDCAPS_BLT makes the driver responsible for video-memory
    // blits and surface allocation. Supply CanCreateSurface and CreateSurface
    // with HalBlt, since DDRAW then rejects surfaces the driver cannot
    // allocate.
    const unsigned blt_caps = H::DDCAPS_BLT | H::DDCAPS_BLTCOLORFILL | H::DDCAPS_BLTDEPTHFILL | H::DDCAPS_BLTSTRETCH |
                              H::DDCAPS_COLORKEY;
    const unsigned fx_caps = H::DDFXCAPS_BLTMIRRORLEFTRIGHT | H::DDFXCAPS_BLTMIRRORUPDOWN | H::DDFXCAPS_BLTSHRINKX |
                             H::DDFXCAPS_BLTSHRINKXN | H::DDFXCAPS_BLTSHRINKY | H::DDFXCAPS_BLTSHRINKYN |
                             H::DDFXCAPS_BLTSTRETCHX | H::DDFXCAPS_BLTSTRETCHXN | H::DDFXCAPS_BLTSTRETCHY |
                             H::DDFXCAPS_BLTSTRETCHYN;
    const unsigned ckey_caps = H::DDCKEYCAPS_SRCBLT | H::DDCKEYCAPS_DESTBLT;
    Write32(c + H::Caps::DwCaps, H::DDCAPS_3D | H::DDCAPS_GDI | blt_caps | H::DDCAPS_CANBLTSYSMEM);
    Write32(c + H::Caps::CKeyCaps, ckey_caps);
    Write32(c + H::Caps::FXCaps, fx_caps);
    {
        // SRCCOPY, BLACKNESS and WHITENESS, one bit per raster operation
        // code (the high word of the ROP).
        unsigned rops[8] = {};
        const unsigned codes[3] = {0xcc, 0x00, 0xff};
        for (unsigned code : codes) rops[code / 32] |= 1u << (code % 32);
        for (unsigned k = 0; k < 8; ++k) Write32(c + H::Caps::Rops + k * 4, rops[k]);
        // System memory to video memory and back: the same blitter.
        const unsigned both[2] = {H::Caps::SVBCaps, H::Caps::VSBCaps};
        for (unsigned block : both)
        {
            Write32(c + block, blt_caps);
            Write32(c + block + H::Caps::BltCKeyCaps, ckey_caps);
            Write32(c + block + H::Caps::BltFXCaps, fx_caps);
            for (unsigned k = 0; k < 8; ++k) Write32(c + block + H::Caps::BltRops + k * 4, rops[k]);
        }
    }
    // Direct3D into a window (a back buffer blitted into the clipped primary)
    // and a flip that does not wait for the retrace are both real here.
    Write32(c + H::Caps::Caps2, H::DDCAPS2_WIDESURFACES | H::DDCAPS2_CANRENDERWINDOWED | H::DDCAPS2_FLIPNOVSYNC |
                                              H::DDCAPS2_PRIMARYGAMMA);
    Write32(c + H::Caps::ZBufferBitDepths, H::DDBD_16 | H::DDBD_32);
    Write32(c + H::Caps::VidMemTotal, h.heap.bytes);
    Write32(c + H::Caps::VidMemFree, h.heap.bytes);
    Write32(c + H::Caps::SurfaceCaps, H::DDSCAPS_3DDEVICE | H::DDSCAPS_BACKBUFFER | H::DDSCAPS_FLIP |
                                          H::DDSCAPS_FRONTBUFFER | H::DDSCAPS_OFFSCREENPLAIN |
                                          H::DDSCAPS_PRIMARYSURFACE | H::DDSCAPS_TEXTURE | H::DDSCAPS_ZBUFFER |
                                          H::DDSCAPS_MIPMAP | H::DDSCAPS_VIDEOMEMORY | H::DDSCAPS_LOCALVIDMEM);
    Write32(i + H::Info::MonitorFrequency, RefreshHertz);
    Write32(i + H::Info::GetDriverInfo, h.driver_info_gate);
    unsigned mode_index = 0;
    for (unsigned m = 0; m < h.mode_count; ++m)
    {
        int w = 0, hh = 0, d = 0;
        if (User::ScreenModeAt(m, w, hh, d) && w == width && hh == height && d == depth) mode_index = m;
    }
    Write32(i + H::Info::ModeIndex, mode_index);
    // The FourCC codes, a far pointer like the mode list (DDRAW 0x1cc54 maps
    // lpdwFourCC with MapSLFix); their count is in the DDCORECAPS.
    const bool compressed = gpu_backend->CompressedTextures();
    Write32(i + H::Info::FourCC, compressed ? h.far_fourcc : 0);
    Write32(c + H::Caps::NumFourCC, compressed ? CompressedFormatCount : 0);
    Write32(i + H::Info::ModeCount, h.mode_count);
    Write32(i + H::Info::ModeInfo, h.far_modes);
    Write32(i + H::Info::Flags, H::Info::FlagsValue | (gpu_backend->DriverModel8() ? H::Info::GetDriverInfo2 : 0));
    Write32(i + H::Info::PDevice, 0);
    Write32(i + H::Info::Instance, 1);
    Write32(i + H::Info::D3DGlobalDriverData, h.global);
    Write32(i + H::Info::D3DCallbacks, h.d3d_callbacks);
    Write32(i + H::Info::ExeBufCallbacks, 0);
    return true;
}

// The DDHAL commands inside DCICOMMAND, DDRAW 0x1d721..0x1d8ea. The answer is
// the escape result: positive for served, -1 for refused.
unsigned Application::DirectDrawHalCommand(unsigned command, unsigned parameter, unsigned out_bytes, unsigned out)
{
    namespace D = Win98DDrawDriver;
    (void)parameter;
    if (!gpu_backend) return ~0u;
    switch (command)
    {
    case D::DdVersionInfo:
        // DDVERSIONDATA: dwHALVersion, dwReserved1, dwReserved2.
        if (out && out_bytes >= 12 && virtual_memory.Access(out, 12, true))
        {
            Write32(out, H::HalVersion);
            Write32(out + 4, 0);
            Write32(out + 8, 0);
        }
        DriverTrace("DDVERSIONINFO", H::HalVersion);
        return 1;
    case D::DdGet32BitDriverName:
        // No separate 32-bit DLL: the 32-bit half lives in the gates.
        DriverTrace("DDGET32BITDRIVERNAME refused", 0);
        return ~0u;
    case D::DdNewCallbackFns:
        // The DDHALDDRAWFNS are for a driver that calls DDRAW16's SetInfo;
        // this driver answers index 0x30 itself.
        DriverTrace("DDNEWCALLBACKFNS", 1);
        return 1;
    case D::DdCreateDriverObject:
        if (!HalCreateDriverObject()) return ~0u;
        if (out && out_bytes >= 4 && virtual_memory.Access(out, 4, true)) Write32(out, 1);
        return 1;
    default:
        return ~0u;
    }
}

// DDRAW16 index 0x30, DDRAW 0x1cc45: the driver's DDHALINFO into DDRAW's
// buffer. 0x1cc97 checks the size field afterwards.
// The driver object belongs to the display driver, one for the session: under
// Win98 the driver hands its DDHALINFO to DDRAW16 once and every process reads
// the same. DDHELP never asks for a driver object of its own, but it is the
// process that sets the mode back after a game died in its mode, and DDRAW
// reads the DDHALINFO again there. Without the HAL in that process DDRAW
// rebuilt the shared driver object for a display without a HAL, and the
// primary surface of every other running program stayed lost for good
// (Restore answered DDERR_WRONGMODE). So a process without the record
// takes the session's, if the session has one.
unsigned Application::DirectDrawHalInfo(unsigned buffer)
{
    if (!buffer) return 0;
    if ((!ddhal || !ddhal->info) && FindSessionName(Win98SessionArena::KindView, "NEO98 DirectDraw driver tables") &&
        !HalCreateDriverObject())
        return 0;
    if (!HalRefreshInfo()) return 0;
    unsigned char block[H::Info::Size];
    if (!Read(ddhal->info, block, sizeof block) || !Store(buffer, block, sizeof block)) return 0;
    DriverTrace("DDHALINFO read", buffer);
    return 1;
}

// DDRAW16 index 0x31: the DDHALDDRAWFNS for a 16-bit driver. Nothing here
// calls them, so they stay empty.
unsigned Application::DirectDrawHalFns(unsigned buffer)
{
    if (buffer && virtual_memory.Access(buffer, 16, true))
    {
        Write32(buffer, 16);
        Write32(buffer + 4, 0);
        Write32(buffer + 8, 0);
        Write32(buffer + 12, 0);
    }
    return 0;
}

// Everything the rasterizer keeps for this driver's memory and for the screen
// goes: the process ends, the driver object goes, or the mode changes.
void Application::HalReleaseAll()
{
    if (!gpu_backend || !ddhal) return;
    if (ddhal->heap.host) gpu_backend->Release(ddhal->heap.host, ddhal->heap.bytes);
    if (User::ScreenBits()) gpu_backend->Release(User::ScreenBits(), User::ScreenBytes());
}

// The CPU is about to read (and with will_write change) the bytes of a
// surface: the rasterizer's picture goes into them first.
void Application::HalSync(const unsigned char *bits, bool will_write)
{
    if (!gpu_backend || !bits) return;
    Gpu::Surface view;
    view.bits = const_cast<unsigned char *>(bits);
    gpu_backend->Sync(view, will_write);
}

// A saved state carries the bytes of every surface, and the rasterizer may
// hold a newer picture for them than the bytes do (it writes back when the
// CPU reads, HalSync): the picture of each surface this driver knows goes
// into its bytes. The rasterizer keeps what it has. src/x86_state_machine.cpp.
void Application::HalSyncAll()
{
    if (!gpu_backend || !ddhal) return;
    gpu_backend->Flush();
    for (unsigned i = 0; i < ddhal->surface_count; ++i)
    {
        Gpu::Surface view;
        if (ddhal->surfaces[i].lcl && HalSurfaceView(ddhal->surfaces[i].lcl, view) && view.bits)
            gpu_backend->Sync(view, false);
    }
    HalSync(User::ScreenBits(), false);
}

void Application::FreeDirectDrawHal()
{
    if (!ddhal) return;
    HalReleaseAll();
    if (gpu_backend)
        for (unsigned i = 0; i < ddhal->surface_count; ++i)
            gpu_backend->Forget(process_id, ddhal->surfaces[i].handle);
    User::SettleScanout();
    // The video memory is the session's (HalCreateDriverObject); it stays.
    ddhal->heap = {};
    delete ddhal;
    ddhal = nullptr;
}

// ---------------------------------------------------------------------------
// Surfaces.

Application::HalSurface *Application::HalSurfaceByLcl(unsigned lcl)
{
    if (!ddhal || !lcl) return nullptr;
    auto matches = [&](const HalSurface &s) {
        if (s.lcl == lcl) return true;
        const unsigned offset = lcl - s.shadow;
        return s.shadow && offset % HalShadowBytes == 0 && offset / HalShadowBytes <= s.shadow_levels;
    };
    // Every draw looks up its textures and their mip levels here; a game that
    // keeps thousands of textures (Unreal Tournament over the DirectX 9
    // runtime) spent its frames in the scan. The cache is only a hint
    // and is checked against the record, which may have moved.
    unsigned &cached = ddhal->lcl_cache[(lcl >> 2) % DirectDrawHal::HandleCache];
    if (cached && cached <= ddhal->surface_count && matches(ddhal->surfaces[cached - 1])) return &ddhal->surfaces[cached - 1];
    for (unsigned i = 0; i < ddhal->surface_count; ++i)
        if (matches(ddhal->surfaces[i]))
        {
            cached = i + 1;
            return &ddhal->surfaces[i];
        }
    return nullptr;
}
// A flip exchanges the handles of the two surfaces of the chain (seen on a
// run: D3DIM names the render target by handle 1 and 2 in turn after every
// flip, while the LCLs keep their memory roles), so the handle of a record is
// read out of its DDRAWI_DDRAWSURFACE_MORE at every lookup.
// Vertex and index buffers (DDSCAPS_EXECUTEBUFFER, the ones D3DIM makes for
// the transform-and-lighting device and the buffers of the DirectX 8 runtime)
// carry handles as well. DrawPrimitives2 names them only as streams and
// index buffers, never as textures or render targets, so they answer only
// when `buffer` asks for one.
Application::HalSurface *Application::HalSurfaceByHandle(unsigned handle, bool buffer, unsigned local)
{
    if (!ddhal || !handle) return nullptr;
    auto current = [&](HalSurface &s) {
        // CreateSurfaceEx handles belong to one DDRAW_LOCAL. A former
        // context can leave the same integer in this process's table;
        // Render-target commands must not select it for the new context.
        if (local && s.local != local) return false;
        const bool executable = virtual_memory.Access(s.lcl + H::SurfaceLocal::Caps, 4, false) &&
                                (Read32(s.lcl + H::SurfaceLocal::Caps) & H::DDSCAPS_EXECUTEBUFFER);
        if (executable != buffer) return false;
        const unsigned more = virtual_memory.Access(s.lcl, 4, false) ? Read32(s.lcl + H::SurfaceLocal::More) : 0;
        if (!s.legacy_handle && more && virtual_memory.Access(more + H::SurfaceMore::Handle, 4, false))
            s.handle = Read32(more + H::SurfaceMore::Handle);
        return s.handle == handle;
    };
    unsigned &cached = ddhal->handle_cache[handle % DirectDrawHal::HandleCache];
    if (cached && cached <= ddhal->surface_count && current(ddhal->surfaces[cached - 1])) return &ddhal->surfaces[cached - 1];
    for (unsigned i = 0; i < ddhal->surface_count; ++i)
        if (current(ddhal->surfaces[i]))
        {
            cached = i + 1;
            return &ddhal->surfaces[i];
        }
    return nullptr;
}

// The pixel shaders a context was given (D3DDP2OP_CREATEPIXELSHADER). The
// driver keeps the tokens; the backend translates them when a draw uses one.
void Application::HalCreatePixelShader(HalContext &c, unsigned handle, const unsigned char *code, unsigned bytes)
{
    auto &h = *ddhal;
    HalDeletePixelShader(c, handle);
    if (!handle || bytes < 8 || (bytes & 3)) return;
    if (h.pixel_shader_count >= h.pixel_shader_capacity)
    {
        const unsigned next = h.pixel_shader_capacity ? h.pixel_shader_capacity * 2 : 64;
        auto *grown = static_cast<DirectDrawHal::PixelShader *>(realloc(h.pixel_shaders, next * sizeof(DirectDrawHal::PixelShader)));
        if (!grown) return;
        h.pixel_shaders = grown;
        h.pixel_shader_capacity = next;
    }
    auto *tokens = static_cast<unsigned *>(malloc(bytes));
    if (!tokens) return;
    memcpy(tokens, code, bytes);
    auto &shader = h.pixel_shaders[h.pixel_shader_count++];
    shader.context = c.handle;
    shader.handle = handle;
    shader.count = bytes / 4;
    shader.tokens = tokens;
    shader.key = ((unsigned long long)process_id << 40) | ++h.shader_keys;
    if (log && trace_calls)
    {
        char text[120];
        snprintf(text, sizeof text, "DDHAL pixel shader %u version=%08x tokens=%u", handle, tokens[0], shader.count);
        log(text);
    }
}

// The vertex shaders a context was given (D3DDP2OP_CREATEVERTEXSHADER): the
// D3DVSD declaration and the function, if any.
void Application::HalCreateVertexShader(HalContext &c, unsigned handle, const unsigned char *declaration, unsigned declaration_bytes,
                                        const unsigned char *code, unsigned code_bytes)
{
    auto &h = *ddhal;
    HalDeleteVertexShader(c, handle);
    if (!handle || (declaration_bytes & 3) || (code_bytes & 3)) return;
    if (h.vertex_shader_count >= h.vertex_shader_capacity)
    {
        const unsigned next = h.vertex_shader_capacity ? h.vertex_shader_capacity * 2 : 64;
        auto *grown = static_cast<HalVertexShader *>(realloc(h.vertex_shaders, next * sizeof(HalVertexShader)));
        if (!grown) return;
        h.vertex_shaders = grown;
        h.vertex_shader_capacity = next;
    }
    HalVertexShader shader;
    shader.context = c.handle;
    shader.handle = handle;
    shader.declaration_count = declaration_bytes / 4;
    shader.count = code_bytes / 4;
    shader.declaration = declaration_bytes ? static_cast<unsigned *>(malloc(declaration_bytes)) : nullptr;
    shader.tokens = code_bytes ? static_cast<unsigned *>(malloc(code_bytes)) : nullptr;
    if ((declaration_bytes && !shader.declaration) || (code_bytes && !shader.tokens))
    {
        free(shader.declaration);
        free(shader.tokens);
        return;
    }
    if (declaration_bytes) memcpy(shader.declaration, declaration, declaration_bytes);
    if (code_bytes) memcpy(shader.tokens, code, code_bytes);
    shader.key = ((unsigned long long)process_id << 40) | ++h.shader_keys;
    h.vertex_shaders[h.vertex_shader_count++] = shader;
    if (log && trace_calls)
    {
        char text[120];
        snprintf(text, sizeof text, "DDHAL vertex shader %u declaration=%u version=%08x tokens=%u", handle, shader.declaration_count,
                 shader.tokens ? shader.tokens[0] : 0, shader.count);
        log(text);
    }
}

void Application::HalDeleteVertexShader(HalContext &c, unsigned handle)
{
    auto &h = *ddhal;
    for (unsigned i = 0; i < h.vertex_shader_count; ++i)
        if (h.vertex_shaders[i].context == c.handle && h.vertex_shaders[i].handle == handle)
        {
            if (c.state.vertex_shader.tokens == h.vertex_shaders[i].tokens) c.state.vertex_shader = {};
            free(h.vertex_shaders[i].declaration);
            free(h.vertex_shaders[i].tokens);
            h.vertex_shaders[i] = h.vertex_shaders[--h.vertex_shader_count];
            return;
        }
}

const Application::HalVertexShader *Application::HalVertexShaderOf(HalContext &c)
{
    return HalVertexShaderOf(c, c.vertex_shader);
}

const Application::HalVertexShader *Application::HalVertexShaderOf(HalContext &c, unsigned handle)
{
    auto &h = *ddhal;
    for (unsigned i = 0; i < h.vertex_shader_count; ++i)
        if (h.vertex_shaders[i].context == c.handle && h.vertex_shaders[i].handle == handle) return &h.vertex_shaders[i];
    return nullptr;
}

// D3DDP2OP_CREATEVERTEXSHADERDECL: the D3DVERTEXELEMENT9 of a declaration,
// eight bytes each: stream, offset, type, method, usage, usage index.
void Application::HalCreateDeclaration9(HalContext &c, unsigned handle, const unsigned char *data, unsigned count)
{
    auto &h = *ddhal;
    HalDeleteDeclaration9(c, handle);
    if (!handle) return;
    if (h.declaration9_count >= h.declaration9_capacity)
    {
        const unsigned next = h.declaration9_capacity ? h.declaration9_capacity * 2 : 32;
        auto *grown = static_cast<HalDeclaration9 *>(realloc(h.declarations9, next * sizeof(HalDeclaration9)));
        if (!grown) return;
        h.declarations9 = grown;
        h.declaration9_capacity = next;
    }
    HalDeclaration9 &d = h.declarations9[h.declaration9_count++];
    d = HalDeclaration9{};
    d.context = c.handle;
    d.handle = handle;
    for (unsigned i = 0; i < count && d.count < 32; ++i)
    {
        const unsigned char *e = data + i * 8;
        const unsigned stream = e[0] | (e[1] << 8), offset = e[2] | (e[3] << 8), type = e[4], usage = e[6], index = e[7];
        if (stream == 0xff || type >= 17) continue;   // D3DDECL_END, D3DDECLTYPE_UNUSED
        d.elements[d.count] = {stream, offset, 0, type};
        d.usage[d.count] = usage | (index << 8);
        ++d.count;
    }
    if (log && trace_calls)
    {
        char text[120];
        snprintf(text, sizeof text, "DDHAL vertex declaration %u elements=%u", handle, d.count);
        log(text);
    }
}

void Application::HalDeleteDeclaration9(HalContext &c, unsigned handle)
{
    auto &h = *ddhal;
    for (unsigned i = 0; i < h.declaration9_count; ++i)
        if (h.declarations9[i].context == c.handle && h.declarations9[i].handle == handle)
        {
            h.declarations9[i] = h.declarations9[--h.declaration9_count];
            return;
        }
}

const Application::HalDeclaration9 *Application::HalDeclaration9Of(HalContext &c)
{
    auto &h = *ddhal;
    for (unsigned i = 0; i < h.declaration9_count; ++i)
        if (h.declarations9[i].context == c.handle && h.declarations9[i].handle == c.declaration9) return &h.declarations9[i];
    return nullptr;
}

// A DirectX 9 declaration read by a vertex shader: each element goes into
// the input register the function's dcl names for its usage and index.
unsigned Application::HalMapFunction(const HalDeclaration9 &declaration, const HalVertexShader &function, HalElement *layout)
{
    unsigned usage_of[16];
    for (unsigned &u : usage_of) u = ~0u;
    // The dcl instructions stand at the start: the instruction, the usage
    // token (usage in bits 0 to 4, index in 16 to 19), the input register.
    for (unsigned at = 1; at + 2 < function.count;)
    {
        const unsigned token = function.tokens[at];
        if ((token & 0xffff) == 0xfffe) { at += 1 + ((token >> 16) & 0x7fff); continue; }
        if ((token & 0xffff) != 31) break;
        const unsigned usage = function.tokens[at + 1], reg = function.tokens[at + 2];
        if ((((reg >> 28) & 7) | ((reg >> 8) & 0x18)) == 1 && (reg & 0x7ff) < 16)
            usage_of[reg & 0x7ff] = (usage & 31) | (((usage >> 16) & 15) << 8);
        at += 3;
    }
    unsigned count = 0;
    for (unsigned e = 0; e < declaration.count; ++e)
        for (unsigned reg = 0; reg < 16; ++reg)
            if (usage_of[reg] == declaration.usage[e] && count < 32)
            {
                layout[count] = declaration.elements[e];
                layout[count++].reg = reg;
            }
    return count;
}

// A DirectX 9 declaration for the fixed function pipeline: its usages onto
// the registers of the DirectX 8 declarations (D3DVSDE_), POSITIONT as the
// screen position.
unsigned Application::HalMapFixedFunction(const HalDeclaration9 &declaration, HalElement *layout, bool &pretransformed)
{
    unsigned count = 0;
    pretransformed = false;
    for (unsigned e = 0; e < declaration.count; ++e)
    {
        const unsigned usage = declaration.usage[e] & 255, index = declaration.usage[e] >> 8;
        int reg = -1;
        switch (usage)
        {
        case 0: reg = index == 0 ? 0 : -1; break;                       // POSITION
        case 9: reg = index == 0 ? 0 : -1; pretransformed = true; break; // POSITIONT
        case 1: reg = 1; break;                                         // BLENDWEIGHT
        case 2: reg = 2; break;                                         // BLENDINDICES
        case 3: reg = index == 0 ? 3 : -1; break;                       // NORMAL
        case 4: reg = 4; break;                                         // PSIZE
        case 10: reg = index < 2 ? 5 + int(index) : -1; break;          // COLOR0, COLOR1
        case 5: reg = index < 8 ? 7 + int(index) : -1; break;           // TEXCOORD0 to 7
        default: break;
        }
        if (reg < 0 || count >= 32) continue;
        layout[count] = declaration.elements[e];
        layout[count++].reg = unsigned(reg);
    }
    return count;
}

// SETVERTEXSHADER: an FVF code, or a shader of the context whose declaration
// may load constants (D3DVSD_CONST) as it is set.
void Application::HalSetVertexShader(HalContext &c, unsigned handle)
{
    c.vertex_shader = handle;
    c.state.vertex_shader = {};
    const auto *shader = HalVertexShaderOf(c);
    if (!shader) return;
    c.state.vertex_shader.tokens = shader->tokens;
    c.state.vertex_shader.count = shader->count;
    c.state.vertex_shader.key = shader->key;
    const unsigned *d = shader->declaration;
    for (unsigned at = 0; d && at < shader->declaration_count;)
    {
        const unsigned token = d[at++];
        if (token == 0xffffffffu) break;
        const unsigned kind = token >> 29;
        if (kind == 4)
        {
            // D3DVSD_CONST: the first register and a count of four floats.
            const unsigned first = token & 0x7f, registers = (token >> 25) & 15;
            for (unsigned r = 0; r < registers && at + 4 <= shader->declaration_count; ++r, at += 4)
                if (first + r < Gpu::State::VertexConstants) memcpy(c.state.vs_constants[first + r], &d[at], 16);
        }
        else if (kind == 5) at += (token >> 24) & 31;   // D3DVSD_EXT: its data follows
    }
}

// The input registers of a draw: for every vertex, each element of the
// declaration out of its stream (a buffer, or the vertices of this call) as
// four floats, registers without an element (0, 0, 0, 1). The declaration
// names the stream with D3DVSD_STREAM, then its elements in order, each
// D3DVSD_REG(register, type) or D3DVSD_SKIP(dwords).
// The elements of a DirectX 8 declaration: D3DVSD_STREAM names the stream,
// then its elements follow in order, each D3DVSD_REG(register, type) or
// D3DVSD_SKIP(dwords).
unsigned Application::HalElementsOf(const HalVertexShader &shader, HalElement *elements)
{
    unsigned count = 0, stream = 0, offset = 0;
    const unsigned *d = shader.declaration;
    for (unsigned at = 0; d && at < shader.declaration_count;)
    {
        const unsigned token = d[at++];
        if (token == 0xffffffffu) break;
        const unsigned kind = token >> 29;
        if (kind == 1) { stream = token & 15; offset = 0; }
        else if (kind == 2)
        {
            if (token & 0x10000000u) { offset += ((token >> 16) & 15) * 4; continue; }   // D3DVSD_SKIP
            const unsigned type = (token >> 16) & 15, reg = token & 31;
            if (type > 7 || reg >= 16 || count >= 32) return 0;
            elements[count++] = {stream, offset, reg, type};
            offset += ElementBytes(type);
        }
        else if (kind == 4) at += ((token >> 25) & 15) * 4;
        else if (kind == 5) at += (token >> 24) & 31;
    }
    return count;
}

// The input registers of a draw: for every vertex, each element out of its
// stream (a buffer, or the vertices of this call) as four floats, registers
// without an element (0, 0, 0, 1).
bool Application::HalGatherInputs(HalContext &c, const HalElement *elements, unsigned count, unsigned first, unsigned vertices,
                                  unsigned vertex_base, unsigned &slots)
{
    auto &h = *ddhal;
    slots = 1;
    for (unsigned e = 0; e < count; ++e)
        if (elements[e].reg + 1 > slots) slots = elements[e].reg + 1;
    const unsigned bytes = vertices * slots * 16;
    if (!Grow(h.stream_vertices, h.stream_capacity, bytes + 64)) return false;
    float *out = reinterpret_cast<float *>(h.stream_vertices);
    for (unsigned i = 0; i < vertices * slots; ++i)
    {
        out[i * 4] = out[i * 4 + 1] = out[i * 4 + 2] = 0.0f;
        out[i * 4 + 3] = 1.0f;
    }
    // Each stream's bytes once, then the elements out of them.
    unsigned char *scratch = nullptr;
    unsigned scratch_capacity = 0;
    bool ok = true;
    for (unsigned s = 0; s < H::DP2::Streams && ok; ++s)
    {
        unsigned extent = 0;
        for (unsigned e = 0; e < count; ++e)
        {
            if (elements[e].stream != s) continue;
            const unsigned width = ElementBytes(elements[e].type);
            if (!width || elements[e].offset > ~0u - width) { ok = false; break; }
            const unsigned end = elements[e].offset + width;
            if (end > extent) extent = end;
        }
        if (!ok) break;
        if (!extent) continue;
        const unsigned stride = c.streams[s].stride;
        unsigned address = vertex_base, size = ~0u;
        if (c.streams[s].handle)
        {
            auto *buffer = HalSurfaceByHandle(c.streams[s].handle, true);
            if (!buffer || !HalBufferBytes(buffer->lcl, address, size)) { DriverTrace("DDHAL draw8 no stream buffer", c.streams[s].handle); ok = false; break; }
            address += c.streams[s].offset;
            size = c.streams[s].offset < size ? size - c.streams[s].offset : 0;
        }
        // Include every declared attribute of the last vertex, too. A fixed
        // 32-byte tail omitted Halo's blend data in its 68-byte vertices.
        const unsigned long long from = (unsigned long long)first * stride;
        const unsigned long long length = (unsigned long long)(vertices - 1) * stride + extent;
        if (!address || from > ~0u - address || length > ~0u ||
            (size != ~0u && (from > size || length > size - from)) ||
            !Grow(scratch, scratch_capacity, unsigned(length)) || !Read(address + unsigned(from), scratch, unsigned(length)))
        {
            DriverTrace("DDHAL draw8 stream unreadable", s);
            ok = false;
            break;
        }
        for (unsigned e = 0; e < count; ++e)
        {
            if (elements[e].stream != s) continue;
            for (unsigned v = 0; v < vertices; ++v)
            {
                const unsigned at = v * stride + elements[e].offset;
                float *r = out + (v * slots + elements[e].reg) * 4;
                const unsigned char *p = scratch + at;
                auto f32 = [&](unsigned k) { float f; memcpy(&f, p + k * 4, 4); return f; };
                auto s16 = [&](unsigned k) { return float(short(p[k * 2] | (p[k * 2 + 1] << 8))); };
                auto u16 = [&](unsigned k) { return float(unsigned(p[k * 2] | (p[k * 2 + 1] << 8))); };
                auto half = [&](unsigned k) {
                    const unsigned v = p[k * 2] | (p[k * 2 + 1] << 8), e5 = (v >> 10) & 31, m = v & 1023;
                    float f = e5 == 0 ? m / 16777216.0f : e5 == 31 ? (m ? 0.0f : 65504.0f) : (1024 + m) * float(1u << e5) / 33554432.0f;
                    return (v & 0x8000) ? -f : f;
                };
                auto signed10 = [&](unsigned v) { const int x = int(v & 1023); return float(x >= 512 ? x - 1024 : x); };
                const unsigned packed = unsigned(p[0]) | unsigned(p[1]) << 8 | unsigned(p[2]) << 16 | unsigned(p[3]) << 24;
                switch (elements[e].type)
                {
                case 0: r[0] = f32(0); break;
                case 1: r[0] = f32(0); r[1] = f32(1); break;
                case 2: r[0] = f32(0); r[1] = f32(1); r[2] = f32(2); break;
                case 3: r[0] = f32(0); r[1] = f32(1); r[2] = f32(2); r[3] = f32(3); break;
                case 4:   // D3DCOLOR: B, G, R, A in memory, (R, G, B, A) in the register
                    r[0] = p[2] / 255.0f; r[1] = p[1] / 255.0f; r[2] = p[0] / 255.0f; r[3] = p[3] / 255.0f; break;
                case 5: r[0] = p[0]; r[1] = p[1]; r[2] = p[2]; r[3] = p[3]; break;   // UBYTE4
                case 6: r[0] = s16(0); r[1] = s16(1); break;                         // SHORT2
                case 7: r[0] = s16(0); r[1] = s16(1); r[2] = s16(2); r[3] = s16(3); break;   // SHORT4
                case 8: for (unsigned k = 0; k < 4; ++k) r[k] = p[k] / 255.0f; break;          // UBYTE4N
                case 9: r[0] = s16(0) / 32767.0f; r[1] = s16(1) / 32767.0f; break;             // SHORT2N
                case 10: for (unsigned k = 0; k < 4; ++k) r[k] = s16(k) / 32767.0f; break;     // SHORT4N
                case 11: r[0] = u16(0) / 65535.0f; r[1] = u16(1) / 65535.0f; break;            // USHORT2N
                case 12: for (unsigned k = 0; k < 4; ++k) r[k] = u16(k) / 65535.0f; break;     // USHORT4N
                case 13: r[0] = float(packed & 1023); r[1] = float((packed >> 10) & 1023); r[2] = float((packed >> 20) & 1023); break;   // UDEC3
                case 14: r[0] = signed10(packed) / 511.0f; r[1] = signed10(packed >> 10) / 511.0f; r[2] = signed10(packed >> 20) / 511.0f; break;   // DEC3N
                case 15: r[0] = half(0); r[1] = half(1); break;                                   // FLOAT16_2
                default: for (unsigned k = 0; k < 4; ++k) r[k] = half(k); break;                  // FLOAT16_4
                }
            }
        }
    }
    free(scratch);
    return ok;
}

// The FVF a declaration without a function describes, and the gathered
// input registers packed into it. The registers are those of D3DVSDE_:
// position 0, blend weights 1, normal 3, point size 4, diffuse 5, specular
// 6, texture coordinates 7 to 14; the element types give the sizes.
bool Application::HalPackDeclaration(const HalElement *elements, unsigned count, bool pretransformed, unsigned vertices, unsigned slots,
                                     unsigned &fvf, unsigned &stride)
{
    auto &h = *ddhal;
    int types[16];
    for (int &t : types) t = -1;
    for (unsigned e = 0; e < count; ++e)
        if (elements[e].reg < 16) types[elements[e].reg] = int(elements[e].type);
    if (types[0] < 0) return false;
    // D3DVSDT_ FLOAT1 to FLOAT4 give 1 to 4 floats.
    auto floats = [&](int type) {
        switch (type)
        {
        case 0: return 1u;
        case 1: case 6: case 9: case 11: case 15: return 2u;
        case 2: case 13: case 14: return 3u;
        default: return 4u;
        }
    };
    const unsigned weights = types[1] >= 0 && !pretransformed ? floats(types[1]) : 0;
    fvf = pretransformed ? 0x4 : weights ? (0x4 + weights * 2) : 0x2;   // XYZRHW, XYZ, or XYZB1 to XYZB4
    if (types[3] >= 0) fvf |= 0x10;
    if (types[4] >= 0) fvf |= 0x20;
    if (types[5] >= 0) fvf |= 0x40;
    if (types[6] >= 0) fvf |= 0x80;
    unsigned sets = 0, sizes[8] = {};
    for (unsigned t = 0; t < 8; ++t)
        if (types[7 + t] >= 0) { sets = t + 1; sizes[t] = floats(types[7 + t]); }
    for (unsigned t = 0; t < sets; ++t)
    {
        if (!sizes[t]) sizes[t] = 2;
        const unsigned code = sizes[t] == 1 ? 3 : sizes[t] - 2;   // D3DFVF_TEXCOORDSIZE
        fvf |= code << (16 + t * 2);
    }
    fvf |= sets << 8;
    stride = (pretransformed ? 16 : 12) + weights * 4 + (types[3] >= 0 ? 12 : 0) + (types[4] >= 0 ? 4 : 0) + (types[5] >= 0 ? 4 : 0) + (types[6] >= 0 ? 4 : 0);
    for (unsigned t = 0; t < sets; ++t) stride += sizes[t] * 4;
    // Packed behind the registers in the same scratch, then moved down.
    const unsigned register_bytes = vertices * slots * 16, bytes = vertices * stride;
    if (!Grow(h.stream_vertices, h.stream_capacity, register_bytes + bytes + 64)) return false;
    const float *in = reinterpret_cast<const float *>(h.stream_vertices);
    unsigned char *out = h.stream_vertices + register_bytes;
    auto colour = [](const float *r) {
        auto byte = [](float f) { return unsigned(f <= 0.0f ? 0 : f >= 1.0f ? 255 : unsigned(f * 255.0f + 0.5f)); };
        return byte(r[2]) | byte(r[1]) << 8 | byte(r[0]) << 16 | byte(r[3]) << 24;
    };
    for (unsigned v = 0; v < vertices; ++v)
    {
        const float *r = in + v * slots * 4;
        unsigned char *p = out + v * stride;
        auto put = [&](unsigned reg, unsigned count) {
            static const float none[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            memcpy(p, reg < slots ? r + reg * 4 : none, count * 4);
            p += count * 4;
        };
        put(0, pretransformed ? 4 : 3);
        if (weights) put(1, weights);
        if (types[3] >= 0) put(3, 3);
        if (types[4] >= 0) put(4, 1);
        for (unsigned reg = 5; reg <= 6; ++reg)
            if (types[reg] >= 0)
            {
                const unsigned value = reg < slots ? colour(r + reg * 4) : 0xffffffffu;
                memcpy(p, &value, 4);
                p += 4;
            }
        for (unsigned t = 0; t < sets; ++t) put(7 + t, sizes[t]);
    }
    memmove(h.stream_vertices, out, bytes);
    return true;
}

void Application::HalDeletePixelShader(HalContext &c, unsigned handle)
{
    auto &h = *ddhal;
    for (unsigned i = 0; i < h.pixel_shader_count; ++i)
        if (h.pixel_shaders[i].context == c.handle && h.pixel_shaders[i].handle == handle)
        {
            if (c.state.pixel_shader.tokens == h.pixel_shaders[i].tokens) c.state.pixel_shader = {};
            free(h.pixel_shaders[i].tokens);
            h.pixel_shaders[i] = h.pixel_shaders[--h.pixel_shader_count];
            return;
        }
}

void Application::HalSetPixelShader(HalContext &c, unsigned handle)
{
    c.state.pixel_shader = {};
    auto &h = *ddhal;
    for (unsigned i = 0; handle && i < h.pixel_shader_count; ++i)
        if (h.pixel_shaders[i].context == c.handle && h.pixel_shaders[i].handle == handle)
        {
            c.state.pixel_shader.tokens = h.pixel_shaders[i].tokens;
            c.state.pixel_shader.count = h.pixel_shaders[i].count;
            c.state.pixel_shader.key = h.pixel_shaders[i].key;
        }
}

// The mip level of a surface: the DDSCAPS_MIPMAP surface attached to the one
// above, level times.
unsigned Application::HalMipLevel(unsigned lcl, unsigned level)
{
    for (unsigned n = 0; lcl && n < level; ++n)
    {
        unsigned next = 0;
        for (unsigned node = Read32(lcl + H::SurfaceLocal::AttachList), guard = 0; node && guard < 16 && !next; ++guard)
        {
            if (!virtual_memory.Access(node, 16, false)) break;
            const unsigned attached = Read32(node + H::Attach::Surface);
            if (attached && attached != lcl && (Read32(attached + H::SurfaceLocal::Caps) & H::DDSCAPS_MIPMAP)) next = attached;
            node = Read32(node + H::Attach::Link);
        }
        lcl = next;
    }
    return lcl;
}

// The blits of the DirectX 9 model, handed to the driver's Blt in a
// DDHAL_BLTDATA of their own: a copy, stretched when the rects differ, or a
// fill with a D3DCOLOR put into the format of the surface.
bool Application::HalBlt9(unsigned source, const int *source_rect, unsigned source_level, unsigned dest, const int *dest_rect,
                          unsigned dest_level, bool fill, unsigned color)
{
    namespace D = H::Data;
    auto surface = [&](unsigned handle, unsigned level) {
        auto *record = handle ? HalSurfaceByHandle(handle) : nullptr;
        return record ? HalMipLevel(record->lcl, level) : 0u;
    };
    const unsigned dest_lcl = surface(dest, dest_level), source_lcl = fill ? 0 : surface(source, source_level);
    if (!dest_lcl || (!fill && !source_lcl)) { DriverTrace("DDHAL blit9 without its surfaces", dest); return false; }
    const unsigned data = Allocate(D::BltRects + 16);
    if (!data) return false;
    unsigned char zero[D::BltRects + 16] = {};
    Store(data, zero, sizeof zero);
    Write32(data + D::BltDest, dest_lcl);
    for (unsigned k = 0; k < 4; ++k) Write32(data + D::BltDestRect + k * 4, unsigned(dest_rect[k]));
    if (fill)
    {
        // D3DCOLOR into the masks of the destination.
        HalPlane plane;
        unsigned pixel = color;
        if (HalPlaneOf(dest_lcl, plane) && plane.format.bits < 32)
        {
            auto channel = [&](unsigned mask, unsigned value) {
                if (!mask) return 0u;
                unsigned shift = 0, bits = 0;
                while (!((mask >> shift) & 1)) ++shift;
                while ((mask >> (shift + bits)) & 1) ++bits;
                return ((value * ((1u << bits) - 1) + 127) / 255) << shift;
            };
            pixel = channel(plane.format.r, (color >> 16) & 255) | channel(plane.format.g, (color >> 8) & 255) |
                    channel(plane.format.b, color & 255) | channel(plane.format.a, color >> 24);
        }
        Write32(data + D::BltFlags, D::BltColorFill);
        Write32(data + D::BltFX + D::FxFillColor, pixel);
    }
    else
    {
        Write32(data + D::BltSource, source_lcl);
        for (unsigned k = 0; k < 4; ++k) Write32(data + D::BltSourceRect + k * 4, unsigned(source_rect[k]));
    }
    HalBlt(data);
    const unsigned result = Read32(data + D::BltResult);
    Free(data);
    if (result) DriverTrace("DDHAL blit9 refused", result);
    return result == H::DD_OK;
}

// The bytes of a vertex or index buffer: fpVidMem, and dwLinearSize (the
// union of lPitch) as its size.
bool Application::HalBufferBytes(unsigned lcl, unsigned &address, unsigned &size)
{
    const unsigned gbl = lcl ? Read32(lcl + H::SurfaceLocal::Global) : 0;
    if (!gbl) return false;
    address = Read32(gbl + H::SurfaceGlobal::VidMem);
    size = Read32(gbl + H::SurfaceGlobal::Pitch);
    return address != 0 && size != 0;
}

// The record of a surface, made when CreateSurfaceEx names it.
// The handles of a surface and the surfaces attached below it that are new
// to their records, in the order HalRegisterSurface meets them. The count
// grows past the capacity when the list does not hold them all.
unsigned Application::HalChainHandles(unsigned lcl, unsigned depth, HalKeep *list, unsigned count, unsigned capacity)
{
    if (!lcl || depth > 32) return count;
    if (!virtual_memory.Access(lcl, H::LocalBytes, false)) return capacity + 1;   // the old way, one pass each
    const unsigned more = Read32(lcl + H::SurfaceLocal::More);
    const unsigned handle = more && virtual_memory.Access(more + H::SurfaceMore::Handle, 4, false) ? Read32(more + H::SurfaceMore::Handle) : 0;
    const auto *record = HalSurfaceByLcl(lcl);
    if (handle && (!record || record->handle != handle))
    {
        if (count < capacity) list[count] = {handle, lcl};
        ++count;
    }
    unsigned node = Read32(lcl + H::SurfaceLocal::AttachList);
    for (unsigned guard = 0; node && guard < 64; ++guard)
    {
        if (!virtual_memory.Access(node, 16, false)) break;
        const unsigned attached = Read32(node + H::Attach::Surface);
        if (attached && attached != lcl) count = HalChainHandles(attached, depth + 1, list, count, capacity);
        node = Read32(node + H::Attach::Link);
    }
    return count;
}

void Application::HalRegisterSurface(unsigned lcl, unsigned local, unsigned depth, bool evict)
{
    if (!ddhal || !lcl || depth > 32) return;
    // A texture comes with its mip levels: one pass over the records for the
    // whole chain instead of one per level. Unreal Tournament names more
    // than a thousand chains through CreateSurfaceEx while it loads, and
    // the passes were most of its start over DirectX 9.
    if (evict && depth == 0)
    {
        HalKeep chain[64];
        const unsigned count = HalChainHandles(lcl, 0, chain, 0, 64);
        if (count <= 64)
        {
            HalEvictHandles(chain, count);
            evict = false;
        }
    }
    const unsigned more = Read32(lcl + H::SurfaceLocal::More);
    const unsigned handle = more ? Read32(more + H::SurfaceMore::Handle) : 0;
    auto *record = HalSurfaceByLcl(lcl);
    // A handle that is new to this record may still sit on a record of a
    // surface that went without telling the driver; that one is stale.
    if (evict && handle && (!record || record->handle != handle))
    {
        HalEvictHandle(handle, lcl);
        record = HalSurfaceByLcl(lcl);
    }
    if (!record)
    {
        if (ddhal->surface_count >= ddhal->surface_capacity)
        {
            const unsigned next = ddhal->surface_capacity ? ddhal->surface_capacity * 2 : 256;
            auto *grown = static_cast<HalSurface *>(realloc(ddhal->surfaces, next * sizeof(HalSurface)));
            if (!grown)
            {
                DriverTrace("DDHAL surface table full", lcl);
                return;
            }
            ddhal->surfaces = grown;
            ddhal->surface_capacity = next;
        }
        record = &ddhal->surfaces[ddhal->surface_count++];
        *record = {};
        record->lcl = lcl;
    }
    if (record->handle != handle && gpu_backend && record->handle) gpu_backend->Forget(process_id, record->handle);
    record->handle = handle;
    record->local = local;
    ++record->generation;
    if (log && trace_calls)
    {
        char text[400];
        unsigned at = snprintf(text, sizeof text, "DDHAL more %08x:", more);
        for (unsigned i = 0; i < 34 && more && virtual_memory.Access(more + i * 4, 4, false); ++i)
            at += snprintf(text + at, sizeof text - at, " %08x", Read32(more + i * 4));
        log(text);
        const unsigned gbl = Read32(lcl + H::SurfaceLocal::Global);
        snprintf(text, sizeof text, "DDHAL surface lcl=%08x handle=%u caps=%08x vidmem=%08x pitch=%u size=%ux%u",
                 lcl, handle, Read32(lcl + H::SurfaceLocal::Caps), gbl ? Read32(gbl + H::SurfaceGlobal::VidMem) : 0,
                 gbl ? Read32(gbl + H::SurfaceGlobal::Pitch) : 0, gbl ? Read32(gbl + H::SurfaceGlobal::Height) >> 16 : 0,
                 gbl ? Read32(gbl + H::SurfaceGlobal::Height) & 0xffff : 0);
        log(text);
    }
    // The attached surfaces, mip levels and Z buffers, carry handles of
    // their own.
    unsigned node = Read32(lcl + H::SurfaceLocal::AttachList);
    for (unsigned guard = 0; node && guard < 64; ++guard)
    {
        if (!virtual_memory.Access(node, 16, false)) break;
        if (log && trace_calls)
        {
            char text[120];
            snprintf(text, sizeof text, "DDHAL attach %08x: %08x %08x %08x %08x", node, Read32(node), Read32(node + 4), Read32(node + 8), Read32(node + 12));
            log(text);
        }
        const unsigned attached = Read32(node + H::Attach::Surface);
        if (attached && attached != lcl) HalRegisterSurface(attached, local, depth + 1, evict);
        node = Read32(node + H::Attach::Link);
    }
}

void Application::HalForgetSurface(unsigned lcl)
{
    HalPlane plane;
    if (gpu_backend && HalPlaneOf(lcl, plane) && plane.host)
        gpu_backend->Release(plane.host, (unsigned long)plane.pitch * (unsigned long)plane.height);
    auto *record = HalSurfaceByLcl(lcl);
    if (!record) return;
    if (gpu_backend && record->handle) gpu_backend->Forget(process_id, record->handle);
    if (record->shadow) Free(record->shadow);
    *record = ddhal->surfaces[--ddhal->surface_count];
}

// CreateSurfaceEx for a surface of the DirectX 8 and 9 runtime. DDRAW
// builds the LCL, MORE and GBL of such a surface in one heap block for the
// call and frees it on return (seen on a run: HeapFree of the LCL right after
// CreateSurfaceEx, for the runtime's buffers and for a texture the driver had
// made in video memory), so the driver keeps a copy, as the DirectX 8 DDK
// asks of a driver on Win9x, and finds the surface by its handle from then
// on. The attached surfaces, mip levels and cube faces, get copies of their
// own, linked by attachments in the driver's memory. fpVidMem zero says the
// surface is going.
unsigned Application::HalRegisterShadow(unsigned lcl, unsigned local, unsigned depth)
{
    namespace L = H::SurfaceLocal;
    const unsigned more = Read32(lcl + L::More), gbl = Read32(lcl + L::Global);
    const unsigned handle = more ? Read32(more + H::SurfaceMore::Handle) : 0;
    if (!ddhal || !handle || !gbl || depth > 16) return 0;
    HalSurface *record = HalShadowByHandle(handle);
    if (!Read32(gbl + H::SurfaceGlobal::VidMem))
    {
        if (record) HalForgetSurface(record->lcl);
        return 0;
    }
    constexpr unsigned Nodes = HalShadowNodes, Bytes = HalShadowBytes;
    unsigned mip_levels = 0;
    if (Read32(more + H::SurfaceMore::Caps3) & H::SurfaceMore::LightweightMipMap)
    {
        const unsigned dimensions = Read32(gbl + H::SurfaceGlobal::Height);
        unsigned w = dimensions >> 16, h = dimensions & 0xffff, d = HalVolumeSlices(lcl);
        for (; w && h && (w > 1 || h > 1 || d > 1) && mip_levels < Gpu::Surface::MaxLevels; ++mip_levels)
        {
            if (w > 1) w /= 2;
            if (h > 1) h /= 2;
            if (d > 1) d /= 2;
        }
    }
    if (record && record->shadow_capacity < mip_levels + 1)
    {
        HalForgetSurface(record->lcl);
        record = nullptr;
    }
    const unsigned copy_lcl = record ? record->shadow : Allocate(Bytes * (mip_levels + 1));
    if (!copy_lcl) return 0;
    const unsigned copy_gbl = copy_lcl + H::LocalBytes, copy_more = copy_gbl + H::GlobalBytes;
    const unsigned copy_nodes = copy_more + H::MoreBytes;
    unsigned char bytes[H::MoreBytes];
    Read(lcl, bytes, H::LocalBytes);
    Store(copy_lcl, bytes, H::LocalBytes);
    Read(gbl, bytes, H::GlobalBytes);
    Store(copy_gbl, bytes, H::GlobalBytes);
    Read(more, bytes, H::MoreBytes);
    Store(copy_more, bytes, H::MoreBytes);
    Write32(copy_lcl + L::More, copy_more);
    Write32(copy_lcl + L::Global, copy_gbl);
    Write32(copy_lcl + L::AttachList, 0);
    Write32(copy_lcl + L::AttachListFrom, 0);
    if (!record)
    {
        HalRegisterSurface(copy_lcl, local, 0);
        record = HalSurfaceByLcl(copy_lcl);
        if (!record) { Free(copy_lcl); return 0; }
        record->shadow = copy_lcl;
        record->shadow_capacity = mip_levels + 1;
    }
    record->shadow_levels = mip_levels;
    record->handle = handle;
    record->local = local;
    ++record->generation;
    // The attachments: every attached surface below this one, copied, in a
    // list of ATTACHLIST nodes of the copy.
    unsigned used = 0, previous = 0;
    for (unsigned node = Read32(lcl + L::AttachList), guard = 0; node && guard < 32 && used < Nodes; ++guard)
    {
        if (!virtual_memory.Access(node, 16, false)) break;
        const unsigned attached = Read32(node + H::Attach::Surface);
        node = Read32(node + H::Attach::Link);
        if (!attached || attached == lcl) continue;
        const unsigned child = HalRegisterShadow(attached, local, depth + 1);
        if (!child) continue;
        const unsigned mine = copy_nodes + used * 16;
        Write32(mine, 0);
        Write32(mine + H::Attach::Link, 0);
        Write32(mine + H::Attach::Surface, child);
        Write32(mine + 12, 0);
        if (previous) Write32(previous + H::Attach::Link, mine);
        else Write32(copy_lcl + L::AttachList, mine);
        previous = mine;
        ++used;
    }
    // DX9 supplies a single descriptor for a lightweight full mip chain.
    // CreateSurface reserves its contiguous storage. Expand only our private
    // descriptors so TexBlt, Blt9 and texture views can use the ordinary chain.
    if (mip_levels && used < Nodes)
    {
        HalPlane root;
        if (HalPlaneOf(copy_lcl, root))
        {
            unsigned w = unsigned(root.width), h = unsigned(root.height), d = unsigned(root.slices);
            unsigned fp = root.fp, parent = copy_lcl;
            unsigned node = copy_nodes + used * 16;
            Write32(copy_lcl + L::Caps, root.caps | H::DDSCAPS_MIPMAP);
            for (unsigned n = 1; n <= mip_levels; ++n)
            {
                fp += root.fourcc ? LinearSize(w, h, root.fourcc) * d : DriverPitch(w, root.format.bits) * h * d;
                if (w > 1) w /= 2;
                if (h > 1) h /= 2;
                if (d > 1) d /= 2;
                const unsigned child = copy_lcl + n * Bytes;
                const unsigned global = child + H::LocalBytes, extra = global + H::GlobalBytes;
                Read(copy_lcl, bytes, H::LocalBytes); Store(child, bytes, H::LocalBytes);
                Read(copy_gbl, bytes, H::GlobalBytes); Store(global, bytes, H::GlobalBytes);
                Read(copy_more, bytes, H::MoreBytes); Store(extra, bytes, H::MoreBytes);
                Write32(child + L::Global, global);
                Write32(child + L::More, extra);
                Write32(child + L::AttachList, 0);
                Write32(extra + H::SurfaceMore::Handle, 0);
                Write32(extra + H::SurfaceMore::Caps3, 0);
                Write32(extra + H::SurfaceMore::Caps4, (Read32(extra + H::SurfaceMore::Caps4) & 0xffff0000u) | d);
                Write32(global + H::SurfaceGlobal::Height, (w << 16) | h);
                Write32(global + H::SurfaceGlobal::Pitch, root.fourcc ? LinearSize(w, h, root.fourcc) : DriverPitch(w, root.format.bits));
                Write32(global + H::SurfaceGlobal::VidMem, fp);
                Write32(node, 0);
                Write32(node + H::Attach::Link, Read32(parent + L::AttachList));
                Write32(node + H::Attach::Surface, child);
                Write32(node + 12, 0);
                Write32(parent + L::AttachList, node);
                parent = child;
                node = extra + H::MoreBytes;
            }
        }
    }
    if (log && (trace_calls || getenv("NEO98_TRACE_DDHAL_TARGET")))
    {
        char text[160];
        snprintf(text, sizeof text, "DDHAL shadow handle=%u lcl=%08x copy=%08x caps=%08x vidmem=%08x size=%u attached=%u", handle, lcl, copy_lcl,
                 Read32(lcl + L::Caps), Read32(gbl + H::SurfaceGlobal::VidMem), Read32(gbl + H::SurfaceGlobal::Pitch), used);
        log(text);
    }
    return copy_lcl;
}

// Forgets every record but the one of `keep` that carries `handle`: the
// handle of a copy, or the one the MORE of a live surface names now.
void Application::HalEvictHandle(unsigned handle, unsigned keep)
{
    const HalKeep one{handle, keep};
    HalEvictHandles(&one, 1);
}

// HalEvictHandle for several pairs in one pass over the records.
void Application::HalEvictHandles(const HalKeep *list, unsigned count)
{
    if (!ddhal) return;
    bool any = false;
    for (unsigned k = 0; k < count; ++k) any |= list[k].handle != 0;
    if (!any) return;
    for (unsigned i = 0; i < ddhal->surface_count;)
    {
        HalSurface &r = ddhal->surfaces[i];
        unsigned current = r.handle;
        if (!r.shadow)
        {
            const unsigned more = virtual_memory.Access(r.lcl, 4, false) ? Read32(r.lcl + H::SurfaceLocal::More) : 0;
            current = more && virtual_memory.Access(more + H::SurfaceMore::Handle, 4, false) ? Read32(more + H::SurfaceMore::Handle) : 0;
        }
        bool stale = false;
        unsigned handle = 0;
        for (unsigned k = 0; k < count && !stale; ++k)
        {
            const unsigned keep = list[k].keep;
            const bool shadow_of_keep = r.shadow && keep && r.lcl == keep;
            stale = list[k].handle && current == list[k].handle && r.lcl != keep && !shadow_of_keep;
            handle = list[k].handle;
        }
        if (stale)
        {
            if (log && (trace_calls || getenv("NEO98_TRACE_DDHAL_TARGET")))
            {
                char text[96];
                snprintf(text, sizeof text, "DDHAL handle %u given anew, record of %08x forgotten", handle, r.lcl);
                log(text);
            }
            if (gpu_backend) gpu_backend->Forget(process_id, handle);
            if (r.shadow) Free(r.shadow);
            r = ddhal->surfaces[--ddhal->surface_count];
            continue;
        }
        ++i;
    }
}

// The record of a copied surface by its handle.
Application::HalSurface *Application::HalShadowByHandle(unsigned handle)
{
    for (unsigned i = 0; ddhal && i < ddhal->surface_count; ++i)
        if (ddhal->surfaces[i].shadow && ddhal->surfaces[i].handle == handle) return &ddhal->surfaces[i];
    return nullptr;
}

// Whether a surface of the DirectX 8 and 9 runtime lives only for the call
// that names it: every one but the flipping chain, whose records DDRAW keeps
// and whose video memory moves with each flip.
bool Application::HalTransientSurface(unsigned lcl)
{
    if (!ddhal || !ddhal->dx_version || !lcl) return false;
    const unsigned caps = Read32(lcl + H::SurfaceLocal::Caps);
    constexpr unsigned chain = H::DDSCAPS_PRIMARYSURFACE | H::DDSCAPS_FLIP | H::DDSCAPS_FRONTBUFFER | H::DDSCAPS_BACKBUFFER;
    return !(caps & chain);
}

// The view of a surface for the backend, read out of DDRAW's records at the
// moment of use, because the video memory pointer of a flipping surface
// changes with every flip.
bool Application::HalSurfaceView(unsigned lcl, Gpu::Surface &view)
{
    view = {};
    if (!lcl) return false;
    const unsigned gbl = Read32(lcl + H::SurfaceLocal::Global);
    if (!gbl) return false;
    const unsigned size = Read32(gbl + H::SurfaceGlobal::Height);
    view.height = int(size & 0xffff);
    view.width = int(size >> 16);
    view.pitch = int(Read32(gbl + H::SurfaceGlobal::Pitch));
    const unsigned flags = Read32(lcl + H::SurfaceLocal::Flags), caps = Read32(lcl + H::SurfaceLocal::Caps);
    unsigned char pf[H::PixelFormat::Size] = {};
    if (flags & H::SurfaceLocal::HasPixelFormat)
        Read(gbl + H::SurfaceGlobal::PixelFormat, pf, sizeof pf);
    else
    {
        int width = 0, height = 0, depth = 0;
        User::ScreenMode(width, height, depth);
        Format f;
        DisplayFormat(depth, f);
        PutFormat(pf, f);
    }
    auto get = [&](unsigned at) {
        return unsigned(pf[at]) | (unsigned(pf[at + 1]) << 8) | (unsigned(pf[at + 2]) << 16) | (unsigned(pf[at + 3]) << 24);
    };
    const unsigned pflags = get(H::PixelFormat::Flags);
    if (pflags & H::PixelFormat::FourCCFlag) view.fourcc = get(H::PixelFormat::FourCC);
    view.bump = (pflags & H::PixelFormat::BumpDuDv) != 0;
    view.bump_luminance = (pflags & H::PixelFormat::BumpLuminance) != 0;
    view.bit_count = get(H::PixelFormat::BitCount);
    view.r_mask = get(H::PixelFormat::RMask);
    view.g_mask = get(H::PixelFormat::GMask);
    view.b_mask = get(H::PixelFormat::BMask);
    view.a_mask = get(H::PixelFormat::AMask);
    view.indexed = (pflags & H::PixelFormat::Indexed8) != 0;
    if ((caps & H::DDSCAPS_ZBUFFER) || (pflags & H::PixelFormat::ZBuffer))
    {
        view.depth = true;
        view.stencil_bits = (pflags & H::PixelFormat::StencilBuffer) ? get(H::PixelFormat::RMask) : 0;
        // dwZBufferBitDepth counts the packed pixel. The Z mask identifies
        // its depth bits; D24S8 occupies 32 bits but has 24 bits of Z.
        const unsigned z_mask = get(H::PixelFormat::GMask);
        view.z_bits = z_mask ? unsigned(__builtin_popcount(z_mask)) : view.bit_count - view.stencil_bits;
    }
    if (flags & H::SurfaceLocal::HasSourceColorKey)
    {
        view.color_key = true;
        view.key_low = Read32(lcl + H::SurfaceLocal::SourceColorKey);
        view.key_high = Read32(lcl + H::SurfaceLocal::SourceColorKey + 4);
    }
    if (view.width <= 0 || view.height <= 0 || view.pitch <= 0) return false;
    const unsigned fp = Read32(gbl + H::SurfaceGlobal::VidMem);
    view.slices = int(HalVolumeSlices(lcl));
    unsigned bytes = unsigned(view.pitch) * unsigned(view.height) * unsigned(view.slices ? view.slices : 1);
    if (view.fourcc)
    {
        // A block compressed surface: its lPitch is its linear size.
        if (!BlockBytes(view.fourcc)) return false;
        view.pitch = int(BlockPitch(unsigned(view.width), view.fourcc));
        bytes = LinearSize(unsigned(view.width), unsigned(view.height), view.fourcc);
    }
    view.bits = HalHostPointer(fp, bytes);
    if (!view.bits) return false;
    view.owner = process_id;
    if (auto *record = HalSurfaceByLcl(lcl))
    {
        // Legacy Direct3D does not send DP2 palette updates. Its DirectDraw
        // surface retains the attached palette interface and color table.
        if (view.indexed && record->legacy_handle)
        {
            auto pointer = [&](unsigned base, unsigned offset) {
                return base && virtual_memory.Access(base + offset, 4, false) ? Read32(base + offset) : 0u;
            };
            const unsigned palette_object = pointer(lcl, H::SurfaceLocal::Palette);
            const unsigned palette_local = pointer(palette_object, 4);
            const unsigned palette_global = pointer(palette_local, 4);
            const unsigned entries = pointer(palette_global, 16);
            if (entries && virtual_memory.Access(entries, 256 * 4, false))
                if (auto *palette = ddhal->PaletteOf(palette_global, true))
                {
                    const bool alpha = (pointer(palette_global, 4) & 0x2000) != 0;
                    bool changed = record->palette != palette_global || palette->alpha != alpha;
                    palette->alpha = alpha;
                    for (unsigned i = 0; i < 256; ++i)
                    {
                        const unsigned entry = Read32(entries + i * 4);
                        const unsigned color = ((entry & 255) << 16) | (entry & 0xff00) |
                                               ((entry >> 16) & 255) | (entry & 0xff000000u);
                        changed = changed || palette->colors[i] != color;
                        palette->colors[i] = color;
                    }
                    record->palette = palette_global;
                    if (changed) ++record->generation;
                }
        }
        view.handle = record->handle;
        view.generation = record->generation;
        if (view.indexed && record->palette)
            if (auto *p = ddhal->PaletteOf(record->palette))
            {
                view.palette = p->colors;
                view.palette_alpha = p->alpha;
            }
    }
    if (view.indexed && !view.palette) view.palette = User::ScreenColors();
    // The mip chain: each level is the DDSCAPS_MIPMAP surface attached to the
    // one above, half the size, the same format.
    if (caps & H::DDSCAPS_MIPMAP)
    {
        unsigned level = lcl;
        int width = view.width, height = view.height, depth = view.slices;
        for (unsigned n = 0; n < Gpu::Surface::MaxLevels; ++n)
        {
            if (width == 1 && height == 1 && depth <= 1) break;
            const int want_w = width > 1 ? width / 2 : 1;
            const int want_h = height > 1 ? height / 2 : 1;
            const int want_d = depth > 1 ? depth / 2 : depth;
            unsigned node = Read32(level + H::SurfaceLocal::AttachList), next = 0;
            for (unsigned guard = 0; node && guard < 16 && !next; ++guard)
            {
                if (!virtual_memory.Access(node, 16, false)) break;
                const unsigned attached = Read32(node + H::Attach::Surface);
                if (attached && attached != level && (Read32(attached + H::SurfaceLocal::Caps) & H::DDSCAPS_MIPMAP))
                {
                    // A cube also attaches its other full-size faces here.
                    // DDSCAPS_MIPMAP alone does not identify a lower level.
                    const unsigned global = Read32(attached + H::SurfaceLocal::Global);
                    const unsigned dimensions = global ? Read32(global + H::SurfaceGlobal::Height) : 0;
                    if (int(dimensions >> 16) == want_w && int(dimensions & 0xffff) == want_h &&
                        (!depth || int(HalVolumeSlices(attached)) == want_d)) next = attached;
                }
                node = Read32(node + H::Attach::Link);
            }
            if (!next) break;
            const unsigned g = Read32(next + H::SurfaceLocal::Global);
            if (!g) break;
            const unsigned size = Read32(g + H::SurfaceGlobal::Height);
            const int w = int(size >> 16), h = int(size & 0xffff);
            int pitch = int(Read32(g + H::SurfaceGlobal::Pitch));
            if (pitch <= 0) break;
            const int slices = view.slices ? int(HalVolumeSlices(next)) : 0;
            unsigned level_bytes = unsigned(pitch) * unsigned(h) * unsigned(slices ? slices : 1);
            if (view.fourcc)
            {
                pitch = int(BlockPitch(unsigned(w), view.fourcc));
                level_bytes = LinearSize(unsigned(w), unsigned(h), view.fourcc);
            }
            unsigned char *bits = HalHostPointer(Read32(g + H::SurfaceGlobal::VidMem), level_bytes);
            if (!bits) break;
            view.levels[n] = {bits, w, h, pitch, slices};
            view.level_count = n + 1;
            // A write into a lower level is a change of the texture.
            if (auto *record = HalSurfaceByLcl(next)) view.generation += record->generation;
            level = next;
            width = w; height = h; depth = slices;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// The DirectDraw callbacks.

// CanCreateSurface. DDRAW asks before it creates a surface in video memory;
// bIsDifferentPixelFormat says the description names a format other than the
// display's. Without this callback DDRAW's own creation path refuses every
// video memory surface of another format, a 32-bit ARGB texture in a 16-bit
// mode as much as in a 32-bit one (DDERR_INVALIDPIXELFORMAT out of DDRAW
// 0x2d5c4), because the caps the texture formats are listed under belong to
// the Direct3D side. A driver answers from the formats it actually keeps.
unsigned Application::HalCanCreateSurface(unsigned data)
{
    namespace D = H::Data;
    const unsigned desc = Read32(data + D::CanCreateDesc);
    unsigned result = H::DD_OK;
    if (desc && Read32(data + D::CanCreateDifferentFormat))
    {
        unsigned char pf[H::PixelFormat::Size] = {};
        Read(desc + D::DescPixelFormat, pf, sizeof pf);
        int width = 0, height = 0, depth = 0;
        User::ScreenMode(width, height, depth);
        if (!DriverFormat(FormatOf(pf), depth, gpu_backend->CompressedTextures())) result = H::DDERR_INVALIDPIXELFORMAT;
    }
    if (log && (trace_calls || getenv("NEO98_TRACE_D3D")) && desc)
    {
        char text[160];
        snprintf(text, sizeof text, "DDHAL CanCreateSurface caps=%08x different=%u result=%08x", Read32(desc + D::DescCaps),
                 Read32(data + D::CanCreateDifferentFormat), result);
        log(text);
    }
    Write32(data + D::CanCreateResult, result);
    return H::DriverHandled;
}

// CreateSurface. The driver decides the layout of each surface of the list:
// the pitch out of the pixel format, whole pixels in sixteen-byte rows, and
// the size of the block. DDRAW then cuts the block out of the heap the
// DDHALINFO lists (fpVidMem = DDHAL_PLEASEALLOC_BLOCKSIZE and
// DDHAL_DRIVER_NOTHANDLED), so its free memory count stays right. The
// primary surface is the screen and keeps the address DDRAW gives it.
unsigned Application::HalCreateSurface(unsigned data)
{
    namespace D = H::Data;
    const unsigned list = Read32(data + D::CreateSurfaceList), count = Read32(data + D::CreateSurfaceCount);
    int width = 0, height = 0, depth = 0;
    User::ScreenMode(width, height, depth);
    for (unsigned i = 0; i < count && i < 256; ++i)
    {
        const unsigned lcl = Read32(list + i * 4);
        const unsigned gbl = lcl ? Read32(lcl + H::SurfaceLocal::Global) : 0;
        if (!gbl) continue;
        const unsigned caps = Read32(lcl + H::SurfaceLocal::Caps);
        if (caps & H::DDSCAPS_PRIMARYSURFACE) continue;
        unsigned bits = unsigned(depth), fourcc = 0;
        if (Read32(lcl + H::SurfaceLocal::Flags) & H::SurfaceLocal::HasPixelFormat)
        {
            bits = Read32(gbl + H::SurfaceGlobal::PixelFormat + H::PixelFormat::BitCount);
            if (Read32(gbl + H::SurfaceGlobal::PixelFormat + H::PixelFormat::Flags) & H::PixelFormat::FourCCFlag)
                fourcc = Read32(gbl + H::SurfaceGlobal::PixelFormat + H::PixelFormat::FourCC);
        }
        const unsigned size = Read32(gbl + H::SurfaceGlobal::Height);
        const unsigned w = size >> 16, h = size & 0xffff;
        const unsigned more = Read32(lcl + H::SurfaceLocal::More);
        const bool lightweight = more && (Read32(more + H::SurfaceMore::Caps3) & H::SurfaceMore::LightweightMipMap);
        auto allocation = [&](unsigned base) {
            if (!lightweight) return base;
            unsigned mw = w, mh = h, md = HalVolumeSlices(lcl);
            if (!md) md = 1;
            for (unsigned level = 0; (mw > 1 || mh > 1 || md > 1) && level < Gpu::Surface::MaxLevels; ++level)
            {
                if (mw > 1) mw /= 2;
                if (mh > 1) mh /= 2;
                if (md > 1) md /= 2;
                base += fourcc ? LinearSize(mw, mh, fourcc) * md : DriverPitch(mw, bits) * mh * md;
            }
            return base;
        };
        if (fourcc && BlockBytes(fourcc) && w && h)
        {
            // A compressed surface: lPitch is its linear size (the union with
            // dwLinearSize), the block that many bytes.
            const unsigned linear = LinearSize(w, h, fourcc);
            Write32(gbl + H::SurfaceGlobal::Pitch, linear);
            Write32(gbl + H::SurfaceGlobal::BlockSizeX, allocation(linear));
            Write32(gbl + H::SurfaceGlobal::BlockSizeY, 1);
            Write32(gbl + H::SurfaceGlobal::VidMem, D::PleaseAllocBlockSize);
            continue;
        }
        if (!w || !h || !bits || bits > 32) continue;
        // A volume holds its slices one after the other.
        const unsigned pitch = DriverPitch(w, bits), slices = HalVolumeSlices(lcl);
        Write32(gbl + H::SurfaceGlobal::Pitch, pitch);
        Write32(gbl + H::SurfaceGlobal::BlockSizeX, allocation(pitch * h * (slices ? slices : 1)));
        Write32(gbl + H::SurfaceGlobal::BlockSizeY, 1);
        Write32(gbl + H::SurfaceGlobal::VidMem, D::PleaseAllocBlockSize);
        if (log && (trace_calls || getenv("NEO98_TRACE_D3D")))
        {
            char text[160];
            snprintf(text, sizeof text, "DDHAL CreateSurface lcl=%08x caps=%08x %ux%u bits=%u pitch=%u", lcl, caps, w, h, bits, pitch);
            log(text);
        }
    }
    Write32(data + D::CreateSurfaceResult, H::DD_OK);
    return H::DriverNotHandled;
}

unsigned Application::HalFlip(unsigned data)
{
    const unsigned target = Read32(data + H::Data::FlipTarget);
    Gpu::Surface view;
    if (!HalSurfaceView(target, view))
    {
        Write32(data + H::Data::FlipResult, H::DDERR_GENERIC);
        return H::DriverHandled;
    }
    // A GPU presenter reads the resident image. Other hosts first need the
    // picture in guest memory for their CPU compositor.
    if (view.bits == User::ScreenBits()) User::ResetScanout();
    else User::SetScanout(view.bits, view.pitch);
    if (!User::DeferScreenRead()) HalSync(view.bits, false);
    User::ScreenChanged();
    ++hal_presents;
    if (log && trace_calls)
    {
        char text[160];
        const unsigned char *p = view.bits + (view.height / 2) * view.pitch + (view.width / 2) * (view.bit_count / 8);
        snprintf(text, sizeof text, "DDHAL flip target=%08x bits=%p pitch=%d centre=%02x%02x%02x%02x corner=%02x%02x%02x%02x screen=%d",
                 target, (void *)view.bits, view.pitch, p[0], p[1], p[2], p[3], view.bits[0], view.bits[1], view.bits[2], view.bits[3],
                 view.bits == User::ScreenBits());
        log(text);
    }
    // One flip per refresh period: a program that flips as fast as it can
    // waits for the retrace, the way a card holds the flip until the vertical
    // blank. The clock is the one of the display (PaceFrame).
    // DDFLIP_NOVSYNC (8) asks for the flip at once, tearing and all.
    if (!(Read32(data + H::Data::FlipFlags) & 0x8)) PaceFrame();
    Write32(data + H::Data::FlipResult, H::DD_OK);
    return H::DriverHandled;
}

// The Lock callback. DDRAW's lock path (0x1835c) keeps the surface only when
// the driver has a Lock entry and reads that slot either way, so a
// driver with video memory surfaces has to have one; the bytes themselves are
// at fpVidMem and DDRAW hands that pointer out, hence DDHAL_DRIVER_NOTHANDLED.
// What the driver does learn here is how much of which surface the caller is
// about to write, and that is what paces the frames below.
unsigned Application::HalLock(unsigned data)
{
    namespace D = H::Data;
    const unsigned lcl = Read32(data + D::Surface);
    Gpu::Surface view;
    const bool ok = HalSurfaceView(lcl, view);
    // A Lock of the surface the screen is, GetDC on it included, holds the
    // screen's pixels until Unlock: other threads' drawing on the screen
    // waits for it (src/x86_screen_lock.cpp). A new Lock first lets the
    // threads draw that the last Unlock handed the screen to, the way the
    // Win16Mutex goes to its waiters before DDRAW gets it back; it does so
    // before this callback changes anything, so Dispatch can enter it again.
    const bool screen = ok && view.bits && (view.bits == User::ScreenBits() || view.bits == User::ScanoutBits());
    if (screen)
    {
        const unsigned thread = ThreadId();
        if (!User::ScreenHeldBy(thread) && User::ScreenGrantsPending(thread))
        {
            threads[current_thread].block = 18;
            waiting = true;
            return 0;
        }
        User::LockScreen(process_id, thread);
    }
    // Whatever the caller writes through the pointer is new to the
    // rasterizer; GetDC locks through here as well.
    if (auto *s = HalSurfaceByLcl(lcl)) ++s->generation;
    // DDLOCK_READONLY (10h) promises no write; anything else may write.
    if (ok) HalSync(view.bits, !(Read32(data + D::LockFlags) & 0x10));
    // A frame is a lock that covers at least half of the surface the monitor
    // is reading; a sprite or a dirty rectangle is not. This is the rule of
    // DciBeginAccess on the emulation path, with the lock rectangle of
    // DDHAL_LOCKDATA in place of the DCI rectangle.
    ddhal->frame_lock = false;
    if (ok && view.bits == User::ScanoutBits() && view.width > 0 && view.height > 0)
    {
        long long covered = (long long)view.width * view.height;
        if (Read32(data + D::LockHasRect))
        {
            const int left = int(Read32(data + D::LockArea)), top = int(Read32(data + D::LockArea + 4));
            const int right = int(Read32(data + D::LockArea + 8)), bottom = int(Read32(data + D::LockArea + 12));
            covered = (long long)(right - left) * (bottom - top);
        }
        ddhal->frame_lock = covered * 2 >= (long long)view.width * view.height;
    }
    if (log && trace_calls)
    {
        char text[200];
        snprintf(text, sizeof text, "DDHAL lock lcl=%08x bits=%p pitch=%d screen=%d ok=%d rect=%u frame=%d", lcl,
                 (void *)view.bits, view.pitch, view.bits == User::ScreenBits(), ok ? 1 : 0, Read32(data + D::LockHasRect),
                 ddhal->frame_lock ? 1 : 0);
        log(text);
    }
    Write32(data + D::LockResult, H::DD_OK);
    return H::DriverNotHandled;
}

// The Unlock callback. The HEL wrote through the pointer of the lock; if those
// bytes are the ones the monitor shows, the host image follows them now, the
// way DCIEndAccess publishes on the emulation path, and a whole frame waits
// for the rest of the refresh period like a flip does.
unsigned Application::HalUnlock(unsigned data)
{
    namespace D = H::Data;
    const unsigned lcl = Read32(data + D::Surface);
    if (auto *s = HalSurfaceByLcl(lcl)) ++s->generation;
    Gpu::Surface view;
    const bool viewed = HalSurfaceView(lcl, view);
    const bool visible = viewed && view.bits == User::ScanoutBits();
    // The end of the Lock of HalLock: the threads that wait for the screen
    // draw now (src/user_screen.inc, UnlockScreen).
    if (viewed && view.bits && (view.bits == User::ScreenBits() || view.bits == User::ScanoutBits()))
        User::UnlockScreen(process_id, ThreadId());
    if (visible) User::ScreenChanged();
    Write32(data + D::SurfaceResult, H::DD_OK);
    const bool frame = ddhal->frame_lock;
    ddhal->frame_lock = false;
    if (visible && frame) PaceFrame();
    // DDHAL_DRIVER_NOTHANDLED: DDRAW finishes the unlock itself.
    return H::DriverNotHandled;
}

// The WaitForVerticalBlank callback. A driver of Windows 98 blocks here until
// the beam reaches the blank; the emulation path of DDRAW does it for itself
// by polling the input status register of the VGA at port 3DAh, which this
// port answers out of the wall clock (Application::GuestPortInput). The raster
// is the same one: RefreshHertz pictures a second with a retrace of
// RetraceMicroseconds. DDWAITVB_I_TESTVB only asks where the beam is.
unsigned Application::HalWaitForVerticalBlank(unsigned data)
{
    namespace D = H::Data;
    const unsigned flags = Read32(data + D::VBlankFlags);
    const unsigned long long period = 1000000ull / RefreshHertz;
    const unsigned long long now = Now(), phase = now % period;
    const bool in_blank = phase < RetraceMicroseconds;
    Write32(data + D::VBlankInVB, in_blank ? 1 : 0);
    // DDWAITVB_BLOCKBEGINEVENT sets the event in hEvent at the next blank
    // instead of blocking. This driver has no timer that could do that, so it
    // says so rather than answer for a thing it did not do.
    if (flags & H::Data::WaitVbBlockBeginEvent)
    {
        Write32(data + D::VBlankResult, H::DDERR_UNSUPPORTED);
        return H::DriverHandled;
    }
    Write32(data + D::VBlankResult, H::DD_OK);
    if (!(flags & H::Data::WaitVbTest))
    {
        unsigned long long deadline = 0;
        bool blank_after = in_blank;
        if (flags & H::Data::WaitVbBlockEnd)
        {
            deadline = now - phase + (in_blank ? RetraceMicroseconds : period + RetraceMicroseconds);
            blank_after = false;
        }
        else if (flags & H::Data::WaitVbBlockBegin)
        {
            if (!in_blank) deadline = now - phase + period;
            blank_after = true;
        }
        if (deadline > now)
        {
            auto &thread = threads[current_thread];
            thread.block = 3;
            thread.deadline = deadline;
            Suspend(Idle);
        }
        Write32(data + D::VBlankInVB, blank_after ? 1 : 0);
    }
    return H::DriverHandled;
}

unsigned Application::HalSetMode(unsigned data)
{
    const unsigned index = Read32(data + H::Data::ModeIndex);
    int width = 0, height = 0, depth = 0;
    unsigned result = H::DDERR_UNSUPPORTEDMODE;
    if (User::ScreenModeAt(index, width, height, depth))
    {
        User::ResetScanout();
        if (ApplyDisplayMode(width, height, depth, 0) == 0) result = H::DD_OK;
    }
    DriverTrace("DDHAL SetMode", index);
    Write32(data + H::Data::ModeResult, result);
    return H::DriverHandled;
}

// The questions of the DirectX 8 and 9 runtime (GUID_GetDriverInfo2). The
// answer goes into lpvData and its size is the return value; zero leaves the
// question unanswered, and the runtime does without.
unsigned Application::HalDriverInfo2(unsigned out, unsigned expected)
{
    namespace I = H::DriverInfo2;
    namespace C = H::Caps8;
    const unsigned type = Read32(out + I::Type);
    if (log && trace_calls)
    {
        char text[96];
        snprintf(text, sizeof text, "DDHAL GetDriverInfo2 type=%u expected=%u", type, expected);
        log(text);
    }
    if (type == I::DxVersion)
    {
        // DD_DXVERSION: the version of the runtime after the header.
        ddhal->dx_version = Read32(out + I::Payload);
        return expected;
    }
    if (type == I::GetDdiVersion)
    {
        if (!gpu_backend->DriverModel9() || Read32(out + I::Payload) < 9) return 0;
        Write32(out + I::Payload + 4, I::Ddi9);
        return expected;
    }
    if (type == I::GetQueryCount)
    {
        Write32(out + I::Payload, 1);
        return expected;
    }
    if (type == I::GetQuery)
    {
        if (Read32(out + I::Payload) != 0) return 0;
        Write32(out + I::Payload, 8);   // D3DQUERYTYPE_EVENT
        return expected;
    }
    if (type == I::GetFormatCount)
    {
        Write32(out + I::Payload, D3DFormatCount);
        return expected;
    }
    if (type == I::GetFormat)
    {
        // DD_GETFORMATDATA: the index, then the DDPIXELFORMAT to fill.
        const unsigned index = Read32(out + I::Payload);
        if (index >= D3DFormatCount) return 0;
        unsigned pf[8] = {H::PixelFormat::Size, H::D3DFormatFlag, d3d_formats[index].format, 0, d3d_formats[index].operations, 0, 0, 0};
        Store(out + I::Payload + 4, pf, sizeof pf);
        return expected;
    }
    const bool nine = type == I::GetCaps9;
    if (nine ? (expected < I::Caps9Size || !gpu_backend->DriverModel9()) : (type != I::GetCaps8 || expected < I::Caps8Size)) return 0;
    // D3DCAPS8, from the same blocks the DirectX 7 runtime reads: the
    // D3DDEVICEDESC_V1 of the global driver data and the extended caps.
    const unsigned d = ddhal->global + H::GlobalDriverData::HwCaps, tri = d + H::DeviceDesc::TriCaps, e = ddhal->extended;
    unsigned c[H::Caps9::Count] = {};
    c[C::DeviceType] = 1;   // D3DDEVTYPE_HAL
    c[C::Caps2] = C::Caps2Value;
    c[C::PresentationIntervals] = C::PresentationValue;
    c[C::DevCaps] = Read32(d + H::DeviceDesc::DevCaps);
    c[C::PrimitiveMiscCaps] = Read32(tri + H::PrimCaps::MiscCaps);
    c[C::RasterCaps] = Read32(tri + H::PrimCaps::RasterCaps);
    c[C::ZCmpCaps] = Read32(tri + H::PrimCaps::ZCmpCaps);
    c[C::SrcBlendCaps] = Read32(tri + H::PrimCaps::SrcBlendCaps);
    c[C::DestBlendCaps] = Read32(tri + H::PrimCaps::DestBlendCaps);
    c[C::AlphaCmpCaps] = Read32(tri + H::PrimCaps::AlphaCmpCaps);
    c[C::ShadeCaps] = Read32(tri + H::PrimCaps::ShadeCaps);
    c[C::TextureCaps] = C::TextureValue;
    c[C::TextureFilterCaps] = c[C::CubeTextureFilterCaps] = c[C::VolumeTextureFilterCaps] = gpu_backend->Limit(Gpu::Backend::Capability::Filter, C::FilterValue);
    c[C::TextureAddressCaps] = c[C::VolumeTextureAddressCaps] = Read32(tri + H::PrimCaps::TextureAddressCaps);
    c[C::MaxVolumeExtent] = C::VolumeExtent;
    c[C::LineCaps] = C::LineValue;
    c[C::MaxTextureWidth] = Read32(e + H::ExtendedCaps::MaxTextureWidth);
    c[C::MaxTextureHeight] = Read32(e + H::ExtendedCaps::MaxTextureHeight);
    c[C::MaxTextureRepeat] = Read32(e + H::ExtendedCaps::MaxTextureRepeat);
    c[C::MaxTextureAspectRatio] = c[C::MaxTextureWidth];
    c[C::MaxAnisotropy] = Read32(e + H::ExtendedCaps::MaxAnisotropy);
    c[C::MaxVertexW] = Read32(e + H::ExtendedCaps::MaxVertexW);
    for (unsigned i = 0; i < 5; ++i) c[C::GuardBandLeft + i] = Read32(e + H::ExtendedCaps::GuardBandLeft + i * 4);
    c[C::StencilCaps] = Read32(e + H::ExtendedCaps::StencilCaps);
    c[C::FVFCaps] = Read32(e + H::ExtendedCaps::FVFCaps);
    c[C::TextureOpCaps] = Read32(e + H::ExtendedCaps::TextureOpCaps);
    const unsigned stages = Read32(e + H::ExtendedCaps::MaxTextureBlendStages);
    c[C::MaxTextureBlendStages] = stages & 0xffff;
    c[C::MaxSimultaneousTextures] = stages >> 16;
    c[C::VertexProcessingCaps] = C::VertexProcessingValue;
    c[C::MaxActiveLights] = Read32(e + H::ExtendedCaps::MaxActiveLights);
    const unsigned planes = Read32(e + H::ExtendedCaps::MaxUserClipPlanes);
    c[C::MaxUserClipPlanes] = planes & 0xffff;
    c[C::MaxVertexBlendMatrices] = planes >> 16;
    const float one = 1.0f;
    memcpy(&c[C::MaxPointSize], &one, 4);
    c[C::MaxPrimitiveCount] = 0xffff;
    c[C::MaxVertexIndex] = 0xffff;
    c[C::MaxStreams] = 1;
    c[C::MaxStreamStride] = 256;
    // Vertex shaders 1.1 with sixteen streams, when the backend runs them.
    if (gpu_backend->VertexShaders())
    {
        c[C::VertexShaderVersion] = H::Caps8::VertexShaderValue;
        c[C::MaxVertexShaderConst] = H::Caps8::VertexConstants;
        c[C::MaxStreams] = H::DP2::Streams;
    }
    // Pixel shaders 1.1 to 1.4, when the backend translates them.
    if (gpu_backend->PixelShaders())
    {
        c[C::PixelShaderVersion] = H::Caps8::PixelShaderValue;
        const float range = H::Caps8::PixelShaderRange;
        memcpy(&c[C::MaxPixelShaderValue], &range, 4);
    }
    if (!nine)
    {
        Store(out, c, I::Caps8Size);
        return I::Caps8Size;
    }
    // D3DCAPS9: one adapter, one render target, the four declaration types
    // the gather knows beyond those of DirectX 8.
    namespace N = H::Caps9;
    // Shader model 2, with the limits D3D9 0x426c9f to 0x426d3e holds a DDI 9
    // driver to, and D3DPMISCCAPS_FOGINFVF it asks for with it.
    if (gpu_backend->VertexShaders() && gpu_backend->PixelShaders())
    {
        c[C::VertexShaderVersion] = N::VertexShaderValue;
        c[C::MaxVertexShaderConst] = Gpu::State::VertexConstants;
        c[C::PixelShaderVersion] = N::PixelShaderValue;
        c[N::VS20Caps + 2] = N::Temporaries;
        c[N::VS20Caps + 3] = 1;
        c[N::PS20Caps + 2] = N::Temporaries;
        c[N::PS20Caps + 4] = N::PixelInstructions;
        c[C::PrimitiveMiscCaps] |= N::FogInFvf;
    }
    c[N::DevCaps2] = N::DevCaps2Value;
    c[C::Caps2] |= N::Caps2Value;
    c[C::RasterCaps] |= N::RasterValue;
    c[N::NumberOfAdaptersInGroup] = 1;
    c[N::DeclTypes] = N::DeclTypesValue;
    c[N::NumSimultaneousRTs] = 1;
    c[N::StretchRectFilterCaps] = N::StretchRectValue;
    Store(out, c, I::Caps9Size);
    return I::Caps9Size;
}

unsigned Application::HalGetDriverInfo(unsigned data)
{
    unsigned char guid[16] = {};
    Read(data + H::DriverInfoData::Guid, guid, 16);
    if (trace_calls) DriverTrace("DDHAL GetDriverInfo", unsigned(guid[0]) | guid[1] << 8 | guid[2] << 16 | unsigned(guid[3]) << 24);
    const unsigned expected = Read32(data + H::DriverInfoData::ExpectedSize);
    const unsigned out = Read32(data + H::DriverInfoData::Data);
    unsigned source = 0, size = 0;
    if (H::SameGuid(guid, H::D3DCallbacks3Guid)) { source = ddhal->callbacks3; size = H::D3DCallbacks3::Size; }
    else if (H::SameGuid(guid, H::D3DExtendedCapsGuid)) { source = ddhal->extended; size = H::ExtendedCaps::Size; }
    else if (H::SameGuid(guid, H::Misc2CallbacksGuid)) { source = ddhal->misc2; size = H::Misc2Callbacks::Size; }
    else if (H::SameGuid(guid, H::MoreSurfaceCapsGuid))
    {
        // DDMORESURFACECAPS: dwSize, ddsCapsMore (dwCaps2, dwCaps3, dwCaps4),
        // then for the one heap a pair of DDSCAPSEX restrictions, all open.
        unsigned block[10] = {40, H::DDSCAPS2_CUBEMAP | H::DDSCAPS2_CUBEMAP_ALLFACES | H::DDSCAPS2_VOLUME, 0, 0, 0, 0, 0, 0, 0, 0};
        const unsigned n = expected < sizeof block ? expected : sizeof block;
        if (out) Store(out, block, n);
        Write32(data + H::DriverInfoData::ActualSize, sizeof block);
        Write32(data + H::DriverInfoData::Result, H::DD_OK);
        return H::DriverHandled;
    }
    else if (H::SameGuid(guid, H::ZPixelFormatsGuid))
    {
        // A DWORD count followed by the DDPIXELFORMATs.
        unsigned char block[4 + H::PixelFormat::Size * ZFormatCount];
        block[0] = ZFormatCount; block[1] = block[2] = block[3] = 0;
        Read(ddhal->z_list, block + 4, sizeof block - 4);
        const unsigned n = expected < sizeof block ? expected : sizeof block;
        if (out) Store(out, block, n);
        Write32(data + H::DriverInfoData::ActualSize, sizeof block);
        Write32(data + H::DriverInfoData::Result, H::DD_OK);
        return H::DriverHandled;
    }
    else if (H::SameGuid(guid, H::GetDriverInfo2Guid) && out && Read32(out + H::DriverInfo2::Magic) == H::DriverInfo2::MagicValue)
    {
        const unsigned actual = HalDriverInfo2(out, expected);
        if (!actual)
        {
            Write32(data + H::DriverInfoData::Result, H::DDERR_CURRENTLYNOTAVAIL);
            return H::DriverHandled;
        }
        Write32(data + H::DriverInfoData::ActualSize, actual);
        Write32(data + H::DriverInfoData::Result, H::DD_OK);
        return H::DriverHandled;
    }
    else if (H::SameGuid(guid, H::ParseUnknownCommandGuid))
    {
        // DDRAW 0x1ba7f: lpvData is the address of D3DParseUnknownCommand
        // itself, and dwExpectedSize is zero.
        ddhal->parse_unknown = out;
        Write32(data + H::DriverInfoData::ActualSize, 4);
        Write32(data + H::DriverInfoData::Result, H::DD_OK);
        return H::DriverHandled;
    }
    if (!source)
    {
        if (log && trace_calls)
        {
            char text[200];
            snprintf(text, sizeof text, "DDHAL GetDriverInfo unknown %08x-%04x-%04x expected=%u data=%08x %08x %08x %08x",
                     unsigned(guid[0]) | guid[1] << 8 | guid[2] << 16 | unsigned(guid[3]) << 24,
                     guid[4] | guid[5] << 8, guid[6] | guid[7] << 8, expected,
                     out ? Read32(out) : 0, out ? Read32(out + 4) : 0, out ? Read32(out + 8) : 0, out ? Read32(out + 12) : 0);
            log(text);
        }
        Write32(data + H::DriverInfoData::Result, H::DDERR_CURRENTLYNOTAVAIL);
        return H::DriverHandled;
    }
    unsigned char block[256];
    const unsigned n = expected < size ? expected : size;
    Read(source, block, n);
    if (out) Store(out, block, n);
    Write32(data + H::DriverInfoData::ActualSize, size);
    Write32(data + H::DriverInfoData::Result, H::DD_OK);
    return H::DriverHandled;
}

// ---------------------------------------------------------------------------
// Blits.
// With DDCAPS_BLT this driver carries out every blit whose destination is in
// its video memory, and with DDCAPS_CANBLTSYSMEM also those between system
// memory and video memory; only system memory to system memory stays with the
// emulation. That is what a card of 1999 did, and it is what lets the driver
// know about every write into its memory: a texture changes through Lock,
// Unlock or a blit, and each of those comes through here and raises the
// generation the rasterizer uploads by.

// Copies a rect of 4x4 blocks between two surfaces of one FourCC: the rect
// of the source (left, top, right, bottom) to the point of the destination.
// Both have to sit on block boundaries, except where a rect ends at the edge
// of its surface.
bool Application::HalCopyBlocks(const HalPlane &d, const HalPlane &s, const int *rect, int x, int y)
{
    if (!d.fourcc || d.fourcc != s.fourcc) return false;
    const int left = rect[0], top = rect[1], right = rect[2] < s.width ? rect[2] : s.width;
    const int bottom = rect[3] < s.height ? rect[3] : s.height;
    if ((left | top | x | y) & 3) return false;
    if (((right & 3) && right != s.width) || ((bottom & 3) && bottom != s.height)) return false;
    if (right <= left || bottom <= top) return true;
    const int columns = (right - left + 3) / 4, rows = (bottom - top + 3) / 4;
    if (x / 4 + columns > (d.width + 3) / 4 || y / 4 + rows > (d.height + 3) / 4) return false;
    const unsigned bytes = unsigned(columns) * s.block;
    unsigned char row[16384];
    if (bytes > sizeof row) return false;
    for (int r = 0; r < rows; ++r)
    {
        const unsigned from = unsigned((top / 4 + r) * s.pitch) + unsigned(left / 4) * s.block;
        const unsigned to = unsigned((y / 4 + r) * d.pitch) + unsigned(x / 4) * d.block;
        if (s.host) memcpy(row, s.host + from, bytes);
        else if (!Read(s.fp + from, row, bytes)) return false;
        if (d.host) memcpy(d.host + to, row, bytes);
        else if (!Store(d.fp + to, row, bytes)) return false;
    }
    return true;
}

namespace
{
struct Rect
{
    int left = 0, top = 0, right = 0, bottom = 0;
    int Width() const { return right - left; }
    int Height() const { return bottom - top; }
    bool Empty() const { return right <= left || bottom <= top; }
};
Rect Intersect(const Rect &a, const Rect &b)
{
    return {a.left > b.left ? a.left : b.left, a.top > b.top ? a.top : b.top,
            a.right < b.right ? a.right : b.right, a.bottom < b.bottom ? a.bottom : b.bottom};
}
unsigned LoadPixel(const unsigned char *p, int bytes)
{
    switch (bytes)
    {
    case 1: return p[0];
    case 2: return unsigned(p[0]) | unsigned(p[1]) << 8;
    case 3: return unsigned(p[0]) | unsigned(p[1]) << 8 | unsigned(p[2]) << 16;
    default: return unsigned(p[0]) | unsigned(p[1]) << 8 | unsigned(p[2]) << 16 | unsigned(p[3]) << 24;
    }
}
void StorePixel(unsigned char *p, int bytes, unsigned v)
{
    p[0] = (unsigned char)v;
    if (bytes > 1) p[1] = (unsigned char)(v >> 8);
    if (bytes > 2) p[2] = (unsigned char)(v >> 16);
    if (bytes > 3) p[3] = (unsigned char)(v >> 24);
}
// The rows of `r` as a block of bytes: the host bytes themselves, or a copy.
struct Rows
{
    unsigned char *at = nullptr;   // pixel (r.left, r.top)
    int pitch = 0;
    unsigned char *copy = nullptr;
    Rows() = default;
    Rows(const Rows &) = delete;
    Rows &operator=(const Rows &) = delete;
    ~Rows() { free(copy); }
    bool Allocate(size_t bytes)
    {
        copy = static_cast<unsigned char *>(malloc(bytes ? bytes : 1));
        at = copy;
        return copy != nullptr;
    }
};
} // namespace

// DDHAL_BLTDATA. Colour fill, depth fill, copies with and without stretching,
// mirroring, source and destination colour keys, and the clip list DDRAW hands
// over for a clipped destination. The raster operations are SRCCOPY,
// BLACKNESS and WHITENESS, the ones the caps name.
unsigned Application::HalBlt(unsigned data)
{
    namespace D = H::Data;
    const unsigned flags = Read32(data + D::BltFlags);
    auto answer = [&](unsigned result) {
        Write32(data + D::BltResult, result);
        return H::DriverHandled;
    };
    HalPlane dst, src;
    if (!HalPlaneOf(Read32(data + D::BltDest), dst)) return answer(H::DDERR_INVALIDPARAMS);
    if (dst.caps & H::DDSCAPS_PRIMARYSURFACE) ++hal_presents;
    const unsigned src_lcl = Read32(data + D::BltSource);
    const bool has_src = src_lcl && HalPlaneOf(src_lcl, src);
    auto rect_at = [&](unsigned at) {
        return Rect{int(Read32(at)), int(Read32(at + 4)), int(Read32(at + 8)), int(Read32(at + 12))};
    };
    const bool clipped = Read32(data + D::BltIsClipped) != 0;
    Rect drect = rect_at(data + (clipped ? D::BltOrigDest : D::BltDestRect));
    Rect srect = rect_at(data + (clipped ? D::BltOrigSource : D::BltSourceRect));
    if (dst.fourcc || (has_src && src.fourcc))
    {
        // Compressed surfaces copy block for block: the same FourCC, the
        // same size, no key, no effect, no stretching.
        const unsigned plain = flags & ~(D::BltRopFlag2 | 0x1000000u | 0x200u);   // DDBLT_ROP, _WAIT, _ASYNC
        if (!has_src || plain || drect.Width() != srect.Width() || drect.Height() != srect.Height())
            return answer(H::DDERR_UNSUPPORTED);
        HalSync(src.host, false);
        HalSync(dst.host, true);
        const int r[4] = {srect.left, srect.top, srect.right, srect.bottom};
        if (!HalCopyBlocks(dst, src, r, drect.left, drect.top)) return answer(H::DDERR_UNSUPPORTED);
        if (auto *record = HalSurfaceByLcl(dst.lcl)) ++record->generation;
        return answer(H::DD_OK);
    }
    const Rect dbounds{0, 0, dst.width, dst.height};
    // The clip rects, in destination coordinates; one rect, the whole
    // destination, when DDRAW did not clip.
    Rect clips[64];
    unsigned clip_count = 0;
    if (clipped)
    {
        const unsigned count = Read32(data + D::BltRectCount), list = Read32(data + D::BltRects);
        for (unsigned i = 0; i < count && clip_count < 64; ++i)
        {
            const Rect c = Intersect(Intersect(rect_at(list + i * 16), drect), dbounds);
            if (!c.Empty()) clips[clip_count++] = c;
        }
    }
    else
    {
        const Rect c = Intersect(drect, dbounds);
        if (!c.Empty()) clips[clip_count++] = c;
    }
    const unsigned fx = data + D::BltFX;
    unsigned rop = D::RopSourceCopy;
    if (flags & D::BltRopFlag2) rop = Read32(fx + D::FxRop);
    const bool fill = (flags & D::BltColorFill) || (flags & D::BltDepthFill) ||
                      ((flags & D::BltRopFlag2) && (rop == D::RopBlackness || rop == D::RopWhiteness));
    if (!fill && (!has_src || rop != D::RopSourceCopy)) return answer(H::DDERR_UNSUPPORTED);
    if (flags & (D::BltAlphaAny | D::BltZBufferAny)) return answer(H::DDERR_UNSUPPORTED);
    if (!fill && src.bytes != dst.bytes) return answer(H::DDERR_UNSUPPORTED);
    if (clip_count == 0) return answer(H::DD_OK);
    // The rasterizer's pictures into the bytes: the source is read, the
    // destination partly overwritten.
    if (has_src) HalSync(src.host, false);
    HalSync(dst.host, true);


    // The destination bytes of the union of the clip rects.
    Rect touched = clips[0];
    for (unsigned i = 1; i < clip_count; ++i)
    {
        touched.left = touched.left < clips[i].left ? touched.left : clips[i].left;
        touched.top = touched.top < clips[i].top ? touched.top : clips[i].top;
        touched.right = touched.right > clips[i].right ? touched.right : clips[i].right;
        touched.bottom = touched.bottom > clips[i].bottom ? touched.bottom : clips[i].bottom;
    }
    auto open = [&](const HalPlane &p, const Rect &r, bool load, Rows &rows) {
        const int bytes = p.bytes;
        if (p.host)
        {
            rows.at = p.host + r.top * p.pitch + r.left * bytes;
            rows.pitch = p.pitch;
            return true;
        }
        rows.pitch = r.Width() * bytes;
        if (!rows.Allocate(size_t(rows.pitch) * size_t(r.Height()))) return false;
        if (load)
            for (int y = 0; y < r.Height(); ++y)
                if (!Read(p.fp + unsigned((r.top + y) * p.pitch + r.left * bytes), rows.at + y * rows.pitch, unsigned(rows.pitch)))
                    return false;
        return true;
    };
    auto close = [&](const HalPlane &p, const Rect &r, Rows &rows) {
        if (p.host) return true;
        for (int y = 0; y < r.Height(); ++y)
            if (!Store(p.fp + unsigned((r.top + y) * p.pitch + r.left * p.bytes), rows.at + y * rows.pitch, unsigned(rows.pitch)))
                return false;
        return true;
    };
    const bool dest_key = (flags & (D::BltKeyDest | D::BltKeyDestOverride)) != 0;
    Rows out;
    if (!open(dst, touched, fill ? dest_key : true, out)) return answer(H::DDERR_INVALIDPARAMS);
    auto pixel_at = [&](int x, int y) { return out.at + (y - touched.top) * out.pitch + (x - touched.left) * dst.bytes; };
    unsigned dkey_low = 0, dkey_high = 0;
    if (flags & D::BltKeyDestOverride) { dkey_low = Read32(fx + D::FxDestKey); dkey_high = Read32(fx + D::FxDestKey + 4); }
    else if (flags & D::BltKeyDest) { dkey_low = Read32(dst.lcl + H::SurfaceLocal::DestColorKey); dkey_high = Read32(dst.lcl + H::SurfaceLocal::DestColorKey + 4); }
    const unsigned mask = dst.bytes >= 4 ? 0xffffffffu : (1u << (dst.bytes * 8)) - 1;

    if (fill)
    {
        unsigned value = Read32(fx + D::FxFillColor);
        if ((flags & D::BltRopFlag2) && rop == D::RopBlackness) value = 0;
        if ((flags & D::BltRopFlag2) && rop == D::RopWhiteness) value = 0xffffffffu;
        value &= mask;
        for (unsigned c = 0; c < clip_count; ++c)
            for (int y = clips[c].top; y < clips[c].bottom; ++y)
            {
                unsigned char *p = pixel_at(clips[c].left, y);
                for (int x = clips[c].left; x < clips[c].right; ++x, p += dst.bytes)
                {
                    if (dest_key)
                    {
                        const unsigned d = LoadPixel(p, dst.bytes) & mask;
                        if (d < dkey_low || d > dkey_high) continue;
                    }
                    StorePixel(p, dst.bytes, value);
                }
            }
        if ((flags & D::BltDepthFill) && gpu_backend)
        {
            // The rasterizer keeps its own depth for a Z buffer.
            Gpu::Surface view;
            if (HalSurfaceView(dst.lcl, view) && view.depth)
            {
                const unsigned zmask = dst.format.g ? dst.format.g : mask;
                const float depth = float(double(value & zmask) / double(zmask));
                for (unsigned c = 0; c < clip_count; ++c)
                {
                    const int r[4] = {clips[c].left, clips[c].top, clips[c].right, clips[c].bottom};
                    gpu_backend->FillDepth(view, r, depth);
                }
            }
        }
    }
    else
    {
        const bool src_key = (flags & (D::BltKeySource | D::BltKeySourceOverride)) != 0;
        unsigned skey_low = 0, skey_high = 0;
        if (flags & D::BltKeySourceOverride) { skey_low = Read32(fx + D::FxSourceKey); skey_high = Read32(fx + D::FxSourceKey + 4); }
        else if (flags & D::BltKeySource) { skey_low = Read32(src.lcl + H::SurfaceLocal::SourceColorKey); skey_high = Read32(src.lcl + H::SurfaceLocal::SourceColorKey + 4); }
        if (skey_high < skey_low) skey_high = skey_low;
        if (dkey_high < dkey_low) dkey_high = dkey_low;
        const unsigned ddfx = (flags & D::BltDdFx) ? Read32(fx + D::FxDdFx) : 0;
        const bool mirror_x = (ddfx & D::FxMirrorLeftRight) != 0, mirror_y = (ddfx & D::FxMirrorUpDown) != 0;
        const Rect sbounds{0, 0, src.width, src.height};
        const Rect sread = Intersect(srect, sbounds);
        if (sread.Empty() || drect.Empty()) return answer(H::DD_OK);
        // A copy inside one surface reads a snapshot of the source rect, so
        // an overlapping move is right in every direction.
        Rows in;
        const bool same = src.host && dst.host && src.fp == dst.fp;
        if (same)
        {
            in.pitch = sread.Width() * src.bytes;
            if (!in.Allocate(size_t(in.pitch) * size_t(sread.Height()))) return answer(H::DDERR_OUTOFMEMORY);
            for (int y = 0; y < sread.Height(); ++y)
                memcpy(in.at + y * in.pitch, src.host + (sread.top + y) * src.pitch + sread.left * src.bytes, size_t(in.pitch));
        }
        else if (!open(src, sread, true, in)) return answer(H::DDERR_INVALIDPARAMS);
        const int dw = drect.Width(), dh = drect.Height(), sw = srect.Width(), sh = srect.Height();
        const bool straight = dw == sw && dh == sh && !mirror_x && !mirror_y && !src_key && !dest_key;
        for (unsigned c = 0; c < clip_count; ++c)
        {
            const Rect &cr = clips[c];
            for (int y = cr.top; y < cr.bottom; ++y)
            {
                int ry = y - drect.top;
                if (mirror_y) ry = dh - 1 - ry;
                const int sy = srect.top + int((long long)ry * sh / dh);
                if (sy < sread.top || sy >= sread.bottom) continue;
                const unsigned char *srow = in.at + (sy - sread.top) * in.pitch;
                unsigned char *d = pixel_at(cr.left, y);
                if (straight)
                {
                    const int sx0 = srect.left + (cr.left - drect.left);
                    int from = sx0, to = sx0 + cr.Width();
                    if (from < sread.left || to > sread.right) { /* partly outside the source: per pixel below */ }
                    else
                    {
                        memcpy(d, srow + (from - sread.left) * src.bytes, size_t(to - from) * size_t(src.bytes));
                        continue;
                    }
                }
                for (int x = cr.left; x < cr.right; ++x, d += dst.bytes)
                {
                    int rx = x - drect.left;
                    if (mirror_x) rx = dw - 1 - rx;
                    const int sx = srect.left + int((long long)rx * sw / dw);
                    if (sx < sread.left || sx >= sread.right) continue;
                    const unsigned v = LoadPixel(srow + (sx - sread.left) * src.bytes, src.bytes) & mask;
                    if (src_key && v >= skey_low && v <= skey_high) continue;
                    if (dest_key)
                    {
                        const unsigned dv = LoadPixel(d, dst.bytes) & mask;
                        if (dv < dkey_low || dv > dkey_high) continue;
                    }
                    StorePixel(d, dst.bytes, v);
                }
            }
        }
    }
    if (!close(dst, touched, out)) return answer(H::DDERR_INVALIDPARAMS);
    if (auto *record = HalSurfaceByLcl(dst.lcl)) ++record->generation;
    if (dst.host && dst.host == User::ScanoutBits())
    {
        User::ScreenChanged();
        // A whole picture onto the visible surface waits for the rest of the
        // refresh period, like a flip.
        if ((long long)touched.Width() * touched.Height() * 2 >= (long long)dst.width * dst.height) PaceFrame();
    }
    if (log && trace_calls)
    {
        char text[200];
        snprintf(text, sizeof text, "DDHAL blt flags=%08x dst=%08x %d,%d-%d,%d src=%08x %d,%d-%d,%d clips=%u rop=%08x", flags, dst.lcl,
                 drect.left, drect.top, drect.right, drect.bottom, has_src ? src.lcl : 0, srect.left, srect.top, srect.right, srect.bottom,
                 clip_count, rop);
        log(text);
    }
    return answer(H::DD_OK);
}

// D3DDP2OP_TEXBLT: the rect of the source texture to the point of the
// destination texture, level by level down both mip chains, each level with
// the rect halved. A destination handle of zero asks the driver to preload
// the source, which here needs nothing: every texture is uploaded when it is
// drawn with.
void Application::HalTextureBlt(unsigned dest_handle, unsigned source_handle, const int *point, const int *rect)
{
    if (!dest_handle) return;
    auto *dest = HalSurfaceByHandle(dest_handle);
    auto *source = HalSurfaceByHandle(source_handle);
    if (!dest || !source) return;
    unsigned dl = dest->lcl, sl = source->lcl;
    int x = point[0], y = point[1];
    int left = rect[0], top = rect[1], right = rect[2], bottom = rect[3];
    for (unsigned level = 0; level < 16 && dl && sl; ++level)
    {
        HalPlane d, s;
        if (!HalPlaneOf(dl, d) || !HalPlaneOf(sl, s) || d.bytes != s.bytes || d.fourcc != s.fourcc) break;
        HalSync(s.host, false);
        HalSync(d.host, true);
        if (left < 0) left = 0;
        if (top < 0) top = 0;
        if (right > s.width) right = s.width;
        if (bottom > s.height) bottom = s.height;
        const int w = right - left < d.width - x ? right - left : d.width - x;
        const int h = bottom - top < d.height - y ? bottom - top : d.height - y;
        if (d.fourcc)
        {
            // Blocks, level by level; a small level is one block.
            const int r[4] = {left, top, right, bottom};
            HalCopyBlocks(d, s, r, x, y);
        }
        else if (w > 0 && h > 0)
        {
            unsigned char row[16384];
            const unsigned bytes = unsigned(w * s.bytes);
            for (int r = 0; r < h && bytes <= sizeof row; ++r)
            {
                const unsigned from = s.fp + unsigned((top + r) * s.pitch + left * s.bytes);
                const unsigned to = d.fp + unsigned((y + r) * d.pitch + x * d.bytes);
                if (s.host) memcpy(row, s.host + (top + r) * s.pitch + left * s.bytes, bytes);
                else if (!Read(from, row, bytes)) break;
                if (d.host) memcpy(d.host + (y + r) * d.pitch + x * d.bytes, row, bytes);
                else if (!Store(to, row, bytes)) break;
            }
        }
        if (auto *record = HalSurfaceByLcl(dl)) ++record->generation;
        // The next level of each chain: the attached DDSCAPS_MIPMAP surface.
        auto next = [&](unsigned lcl) -> unsigned {
            unsigned node = Read32(lcl + H::SurfaceLocal::AttachList);
            for (unsigned guard = 0; node && guard < 16; ++guard)
            {
                if (!virtual_memory.Access(node, 16, false)) return 0;
                const unsigned attached = Read32(node + H::Attach::Surface);
                if (attached && attached != lcl && (Read32(attached + H::SurfaceLocal::Caps) & H::DDSCAPS_MIPMAP)) return attached;
                node = Read32(node + H::Attach::Link);
            }
            return 0;
        };
        if (!(d.caps & H::DDSCAPS_MIPMAP) || !(s.caps & H::DDSCAPS_MIPMAP)) break;
        dl = next(dl);
        sl = next(sl);
        x /= 2; y /= 2; left /= 2; top /= 2; right = (right + 1) / 2; bottom = (bottom + 1) / 2;
    }
}

// A box of one volume into another, level by level down both mip chains:
// rows of whole texels, slice by slice.
void Application::HalVolumeBlt(unsigned dest_handle, unsigned source_handle, const int *point, const int *box)
{
    auto *dest = dest_handle ? HalSurfaceByHandle(dest_handle) : nullptr;
    auto *source = source_handle ? HalSurfaceByHandle(source_handle) : nullptr;
    if (!dest || !source) return;
    int x = point[0], y = point[1], z = point[2];
    int left = box[0], top = box[1], right = box[2], bottom = box[3], front = box[4], back = box[5];
    for (unsigned level = 0; level < 16; ++level)
    {
        const unsigned dl = HalMipLevel(dest->lcl, level), sl = HalMipLevel(source->lcl, level);
        HalPlane d, s;
        if (!dl || !sl || !HalPlaneOf(dl, d) || !HalPlaneOf(sl, s) || d.bytes != s.bytes || d.fourcc || s.fourcc) break;
        if (!d.host && !s.host) break;
        if (s.host) HalSync(s.host, false);
        if (d.host) HalSync(d.host, true);
        left = left < 0 ? 0 : left; top = top < 0 ? 0 : top; front = front < 0 ? 0 : front;
        right = right > s.width ? s.width : right; bottom = bottom > s.height ? s.height : bottom; back = back > s.slices ? s.slices : back;
        const int w = right - left < d.width - x ? right - left : d.width - x;
        const int h = bottom - top < d.height - y ? bottom - top : d.height - y;
        const int n = back - front < d.slices - z ? back - front : d.slices - z;
        const unsigned bytes = w > 0 ? unsigned(w * s.bytes) : 0;
        unsigned char row[16384];
        for (int k = 0; k < n && bytes && bytes <= sizeof row; ++k)
            for (int r = 0; r < h; ++r)
            {
                const unsigned from = unsigned((front + k) * s.pitch * s.height + (top + r) * s.pitch + left * s.bytes);
                const unsigned to = unsigned((z + k) * d.pitch * d.height + (y + r) * d.pitch + x * d.bytes);
                if (s.host) memcpy(row, s.host + from, bytes);
                else if (!Read(s.fp + from, row, bytes)) break;
                if (d.host) memcpy(d.host + to, row, bytes);
                else if (!Store(d.fp + to, row, bytes)) break;
            }
        if (auto *record = HalSurfaceByLcl(dl)) ++record->generation;
        x /= 2; y /= 2; z /= 2; left /= 2; top /= 2; front /= 2;
        right = (right + 1) / 2; bottom = (bottom + 1) / 2; back = (back + 1) / 2;
    }
}

// ---------------------------------------------------------------------------
// Direct3D contexts.

Application::HalContext *Application::HalContextOf(unsigned handle)
{
    if (!ddhal || !handle || handle > DirectDrawHal::ContextCapacity) return nullptr;
    auto &c = ddhal->contexts[handle - 1];
    return c.used ? &c : nullptr;
}

unsigned Application::HalContextCreate(unsigned data)
{
    unsigned slot = 0;
    for (; slot < DirectDrawHal::ContextCapacity; ++slot)
        if (!ddhal->contexts[slot].used) break;
    if (slot == DirectDrawHal::ContextCapacity)
    {
        Write32(data + H::Data::ContextResult, H::DDERR_OUTOFMEMORY);
        return H::DriverHandled;
    }
    auto &c = ddhal->contexts[slot];
    c.used = true;
    c.handle = slot + 1;
    c.local = Read32(data + H::Data::ContextLocal);
    c.target = Read32(data + H::Data::ContextTarget);
    c.z = Read32(data + H::Data::ContextZ);
    // Before DirectX 7, ContextCreate supplies surface interfaces rather
    // than local records. DDRAWI_DDRAWSURFACE_INT keeps lpLcl after lpVtbl.
    auto surface_local = [&](unsigned surface) {
        if (!surface || !virtual_memory.Access(surface, 8, false)) return surface;
        const unsigned table = Read32(surface);
        // An interface starts with a vtable of code pointers; a local
        // surface starts with its MORE record and that record's size.
        if (virtual_memory.Access(table, 4, false) && virtual_memory.Executable(Read32(table)))
            return Read32(surface + 4);
        return surface;
    };
    const unsigned target = surface_local(c.target);
    const bool legacy = target != c.target;
    c.target = target;
    c.z = surface_local(c.z);
    c.pid = Read32(data + H::Data::ContextPid);
    c.queries.Clear();
    c.signalled.Clear();
    Direct3DState::Default(c.state);
    // The render target and its Z buffer were created before the device and
    // may not have handles yet.
    HalRegisterSurface(c.target, c.local, 0, !legacy);
    if (c.z) HalRegisterSurface(c.z, c.local, 0, !legacy);
    if (legacy)
    {
        const unsigned surfaces[] = {c.target, c.z};
        for (const unsigned lcl : surfaces)
            if (auto *surface = HalSurfaceByLcl(lcl))
            {
                surface->legacy_handle = true;
                surface->handle = lcl;
            }
    }
    Write32(data + H::Data::ContextHandle, c.handle);
    Write32(data + H::Data::ContextResult, H::DD_OK);
    DriverTrace("DDHAL ContextCreate", c.handle);
    if (log && (trace_calls || getenv("NEO98_TRACE_DDHAL_TARGET")))
    {
        char text[400];
        snprintf(text, sizeof text, "DDHAL ContextCreate handle=%u target=%08x z=%08x pid=%08x local=%08x",
                 c.handle, c.target, c.z, c.pid, c.local);
        log(text);
        // The first sixteen dwords of the target's LCL, its GBL and its MORE,
        // so the record layout can be read off a run.
        for (unsigned which = 0; which < 3; ++which)
        {
            const unsigned block = which == 0 ? c.target : which == 1 ? Read32(c.target + H::SurfaceLocal::Global) : Read32(c.target + H::SurfaceLocal::More);
            unsigned at = snprintf(text, sizeof text, "DDHAL %s %08x:", which == 0 ? "lcl" : which == 1 ? "gbl" : "more", block);
            for (unsigned i = 0; i < 20 && block && virtual_memory.Access(block + i * 4, 4, false); ++i)
                at += snprintf(text + at, sizeof text - at, " %08x", Read32(block + i * 4));
            log(text);
        }
    }
    return H::DriverHandled;
}

// A render target the monitor is showing, a program drawing straight into the
// front buffer, gets its picture into the bytes at the end of every batch.
void Application::HalShowIfVisible(HalContext &c)
{
    Gpu::Surface view;
    if (!HalSurfaceView(c.target, view) || view.bits != User::ScanoutBits()) return;
    HalSync(view.bits, false);
    User::ScreenChanged();
}

// Begins a batch on the context's render target. False when the target has no
// bytes the backend can render into.
bool Application::HalBeginBatch(HalContext &c)
{
    Gpu::Surface target, z;
    const bool view = HalSurfaceView(c.target, target);
    if (!view || target.depth)
    {
        if (log && getenv("NEO98_TRACE_DDHAL_TARGET"))
        {
            char text[256];
            snprintf(text, sizeof text, "DDHAL target rejected context=%u lcl=%08x view=%d depth=%d size=%dx%d pitch=%d bpp=%u bits=%p",
                     c.handle, c.target, view, target.depth, target.width, target.height, target.pitch, target.bit_count, (void*)target.bits);
            log(text);
        }
        return false;
    }
    const bool has_z = c.z && HalSurfaceView(c.z, z) && z.depth;
    const bool begun = gpu_backend->Begin(target, has_z ? &z : nullptr);
    if (!begun && log && getenv("NEO98_TRACE_DDHAL_TARGET"))
    {
        char text[256];
        snprintf(text, sizeof text, "DDHAL backend rejected context=%u lcl=%08x size=%dx%d pitch=%d bpp=%u bits=%p z=%d %dx%d",
                 c.handle, c.target, target.width, target.height, target.pitch, target.bit_count, (void*)target.bits, has_z,
                 has_z ? z.width : 0, has_z ? z.height : 0);
        log(text);
    }
    return begun;
}

// A cube map names its +X face; the other five hang in its attach list, each
// with its DDSCAPS2_CUBEMAP_ face bit in the ddsCapsEx of its MORE record. The
// view gets all six in the order +X, -X, +Y, -Y, +Z, -Z, and a write into any
// of them changes the texture.
void Application::HalCubeFaces(unsigned lcl, unsigned stage, Gpu::Surface &view)
{
    auto caps2 = [&](unsigned surface) {
        const unsigned more = Read32(surface + H::SurfaceLocal::More);
        return more && virtual_memory.Access(more + H::SurfaceMore::CapsEx, 4, false) ? Read32(more + H::SurfaceMore::CapsEx) : 0u;
    };
    if (!(caps2(lcl) & H::DDSCAPS2_CUBEMAP) || stage >= Gpu::State::Stages) return;
    auto *faces = ddhal->cube_faces[stage];
    unsigned found = 0, generation = 0;
    for (unsigned f = 0; f < 6; ++f)
    {
        const unsigned bit = H::DDSCAPS2_CUBEMAP_POSITIVEX << f;
        unsigned face = (caps2(lcl) & bit) ? lcl : 0;
        for (unsigned node = Read32(lcl + H::SurfaceLocal::AttachList), guard = 0; !face && node && guard < 32; ++guard)
        {
            if (!virtual_memory.Access(node, 16, false)) break;
            const unsigned attached = Read32(node + H::Attach::Surface);
            // DDSCAPS2_MIPMAPSUBLEVEL (10000h) marks a lower level of a face.
            if (attached && !(caps2(attached) & 0x10000) && (caps2(attached) & bit))
                face = attached;
            node = Read32(node + H::Attach::Link);
        }
        if (!face || !HalSurfaceView(face, faces[f])) break;
        generation += faces[f].generation;
        ++found;
    }
    if (found != 6) return;
    view.faces = faces;
    view.generation = generation;
}

// The texture of every stage, so the backend sees a change of the texture
// map, a new generation or new bytes.
void Application::HalBindTextures(HalContext &c)
{
    // The stages end at the first one whose colour operation is
    // D3DTOP_DISABLE; the textures behind it are not sampled, so they are
    // not looked up either.
    bool ended = false;
    for (unsigned stage = 0; stage < Gpu::State::Stages; ++stage)
    {
        // A pixel shader samples any stage, whatever its colour operation.
        ended = ended || (!c.state.pixel_shader.tokens && c.state.stage[stage][H::TSS::ColorOp] == 1);
        const unsigned handle = ended ? 0 : c.state.stage[stage][H::TSS::TextureMap];
        Gpu::Surface view;
        auto *record = HalSurfaceByHandle(handle);
        const bool ok = record && HalSurfaceView(record->lcl, view) && !view.depth;
        if (ok) HalCubeFaces(record->lcl, stage, view);
        if (ok) gpu_backend->SetTexture(stage, &view);
        else gpu_backend->SetTexture(stage, nullptr);
        if (log && trace_calls && handle)
        {
            char text[200];
            snprintf(text, sizeof text, "DDHAL texture stage=%u handle=%u %dx%d pitch=%d bits=%u fourcc=%08x levels=%u gen=%u masks=%08x %08x %08x %08x key=%d",
                     stage, handle, view.width, view.height, view.pitch, view.bit_count, view.fourcc, view.level_count, view.generation,
                     view.r_mask, view.g_mask, view.b_mask, view.a_mask, view.color_key ? 1 : 0);
            log(text);
        }
    }
}

// Ensures the scratch buffers hold `bytes`.
static bool Grow(unsigned char *&buffer, unsigned &capacity, unsigned bytes)
{
    if (bytes <= capacity) return true;
    unsigned next = capacity ? capacity : 65536;
    while (next < bytes)
    {
        if (next > ~0u / 2) return false;
        next *= 2;
    }
    auto *p = static_cast<unsigned char *>(realloc(buffer, next));
    if (!p) return false;
    buffer = p;
    capacity = next;
    return true;
}
static bool GrowIndices(unsigned short *&buffer, unsigned &capacity, unsigned count)
{
    if (count <= capacity) return true;
    unsigned next = capacity ? capacity : 4096;
    while (next < count)
    {
        if (next > ~0u / 2) return false;
        next *= 2;
    }
    auto *p = static_cast<unsigned short *>(realloc(buffer, next * sizeof(unsigned short)));
    if (!p) return false;
    buffer = p;
    capacity = next;
    return true;
}

// One state of a block key, from one state into another.
static void CopyStateKey(Gpu::State &to, const Gpu::State &from, unsigned key)
{
    if (key < KeyStage) to.render[key] = from.render[key];
    else if (key < KeyTransform)
    {
        const unsigned k = key - KeyStage;
        to.stage[k / 32][k % 32] = from.stage[k / 32][k % 32];
    }
    else if (key < KeyLight)
    {
        const unsigned k = key - KeyTransform;
        float *t = k < 4 ? to.world[k] : k == 4 ? to.view : k == 5 ? to.projection : to.texture[k - 6];
        const float *f = k < 4 ? from.world[k] : k == 4 ? from.view : k == 5 ? from.projection : from.texture[k - 6];
        memcpy(t, f, 64);
    }
    else if (key < KeyLightEnable)
    {
        const bool enabled = to.lights[key - KeyLight].enabled;
        to.lights[key - KeyLight] = from.lights[key - KeyLight];
        to.lights[key - KeyLight].enabled = enabled;
    }
    else if (key < KeyMaterial) to.lights[key - KeyLightEnable].enabled = from.lights[key - KeyLightEnable].enabled;
    else if (key == KeyMaterial) to.material = from.material;
    else if (key < KeyViewport) memcpy(to.clip_plane[key - KeyClip], from.clip_plane[key - KeyClip], 16);
    else if (key == KeyViewport)
    {
        to.viewport_x = from.viewport_x; to.viewport_y = from.viewport_y;
        to.viewport_width = from.viewport_width; to.viewport_height = from.viewport_height;
    }
    else if (key == KeyZRange) { to.z_min = from.z_min; to.z_max = from.z_max; }
    else if (key == KeyWInfo) { to.w_near = from.w_near; to.w_far = from.w_far; }
}

// A state of the context changed: the block being recorded takes it.
void Application::HalRecord(HalContext &c, unsigned key)
{
    if (!ddhal->recording || key >= StateKeys) return;
    for (auto &b : ddhal->blocks)
        if (b.handle == ddhal->recording && b.recording && b.snapshot)
        {
            b.marks[key] = 1;
            CopyStateKey(*b.snapshot, c.state, key);
        }
}

// One state of a context, into the context and into the recording block.
void Application::HalSetRenderState(HalContext &c, unsigned state, unsigned value, unsigned rstates)
{
    if (state >= Gpu::State::RenderStates) return;
    c.state.render[state] = value;
    if (rstates) Write32(rstates + state * 4, value);
    HalRecord(c, KeyRender + state);
    // D3DIM forwards legacy Execute states without converting them to TSS.
    // Keep the backend's stage zero in step with those states; later explicit
    // texture stage commands can still replace it normally.
    if (state == H::RS::TextureHandle)
        HalSetStageState(c, 0, H::TSS::TextureMap, value);
    if (state == H::RS::TextureHandle || state == H::RS::TextureMapBlend)
    {
        // The blend operation survives an empty binding. Device3 can send
        // TEXTUREMAPBLEND before its first TextureMap stage command (Re-Volt).
        // Suppress an unbound legacy stage only while drawing below.
        unsigned color = 1, alpha = 1;
        {
            switch (c.state.render[H::RS::TextureMapBlend])
            {
            case 1: case 7: color = alpha = 2; break; // DECAL/COPY, SELECTARG1
            case 2:                               // MODULATE
            {
                color = 4;
                Gpu::Surface texture;
                auto *record = HalSurfaceByHandle(c.state.stage[0][H::TSS::TextureMap]);
                const bool has_alpha = record && HalSurfaceView(record->lcl, texture) &&
                    (texture.a_mask || texture.palette_alpha || texture.fourcc);
                alpha = has_alpha ? 2 : 3; // Texture alpha, otherwise diffuse alpha.
                break;
            }
            case 3: color = 13; alpha = 3; break;    // DECALALPHA
            case 4: color = alpha = 4; break;       // MODULATEALPHA
            case 8: color = 7; alpha = 3; break;    // ADD
            default: return;
            }
        }
        HalSetStageState(c, 0, H::TSS::ColorArg1, 2); // TEXTURE
        HalSetStageState(c, 0, H::TSS::ColorArg2, 0); // DIFFUSE
        HalSetStageState(c, 0, H::TSS::AlphaArg1, 2);
        HalSetStageState(c, 0, H::TSS::AlphaArg2, 0);
        HalSetStageState(c, 0, H::TSS::ColorOp, color);
        HalSetStageState(c, 0, H::TSS::AlphaOp, alpha);
    }
}
void Application::HalSetStageState(HalContext &c, unsigned stage, unsigned state, unsigned value)
{
    if (stage >= Gpu::State::Stages || state >= Gpu::State::StageStates) return;
    // The backend uses the D3D7 enums. D3D8/9 pass D3DTEXTUREFILTERTYPE
    // through DP2: mip NONE/POINT/LINEAR are 0/1/2, mag ANISOTROPIC is 3.
    if (ddhal->dx_version >= 0x800)
    {
        if (state == H::TSS::MipFilter && value <= 2) ++value;
        if (state == H::TSS::MagFilter && value == 3) value = 5;
    }
    c.state.stage[stage][state] = value;
    HalRecord(c, KeyStage + stage * 32 + state);
    // D3DTSS_ADDRESS is both directions at once; D3DIM700 hands it through
    // as it is (a clamped texture wrapped at its edges).
    if (state == H::TSS::Address)
    {
        HalSetStageState(c, stage, H::TSS::AddressU, value);
        HalSetStageState(c, stage, H::TSS::AddressV, value);
    }
}

// Original Device3/RGB draws diffuse colour while a legacy texture is
// unbound, but binding it again restores the blend operation. Keep that
// draw-time suppression out of the persistent state and state-block marks.
static void DrawLegacyTexture(Gpu::Backend &backend, const Gpu::Draw &draw, Gpu::State &state)
{
    auto &stage = state.stage[0];
    const unsigned color = stage[H::TSS::ColorOp];
    const bool unbound = !state.pixel_shader.tokens &&
        state.render[H::RS::TextureMapBlend] && !stage[H::TSS::TextureMap] &&
        (stage[H::TSS::ColorArg1] & 15) == 2 && color != 3;
    if (unbound) stage[H::TSS::ColorOp] = 1;
    backend.Draw(draw, state);
    stage[H::TSS::ColorOp] = color;
}

// D3DHAL_DP2STATESET: dwOperation, dwParam (the block handle), sbType. A
// block holds whatever was set while it was recorded, on the
// transform-and-lighting device the transforms, lights, material and clip
// planes as well.
void Application::HalStateSet(HalContext &c, unsigned operation, unsigned handle, unsigned rstates)
{
    auto find = [&](bool make) -> DirectDrawHal::StateBlock * {
        for (auto &b : ddhal->blocks) if (b.handle == handle) return &b;
        if (!make) return nullptr;
        for (auto &b : ddhal->blocks)
            if (!b.handle)
            {
                b.handle = handle;
                b.recording = false;
                memset(b.marks, 0, sizeof b.marks);
                if (!b.snapshot) b.snapshot = new Gpu::State;
                return &b;
            }
        return nullptr;
    };
    switch (operation)
    {
    case 0:   // D3DHAL_STATESETBEGIN
        if (auto *b = find(true)) { memset(b->marks, 0, sizeof b->marks); b->recording = true; ddhal->recording = handle; }
        break;
    case 1:   // D3DHAL_STATESETEND
        if (auto *b = find(false)) b->recording = false;
        ddhal->recording = 0;
        break;
    case 2:   // D3DHAL_STATESETDELETE
        if (auto *b = find(false)) { b->handle = 0; b->recording = false; memset(b->marks, 0, sizeof b->marks); }
        break;
    case 3:   // D3DHAL_STATESETEXECUTE
        if (auto *b = find(false))
            for (unsigned key = 0; key < StateKeys; ++key)
                if (b->marks[key])
                {
                    CopyStateKey(c.state, *b->snapshot, key);
                    if (key < KeyStage && rstates) Write32(rstates + key * 4, c.state.render[key]);
                }
        break;
    case 4:   // D3DHAL_STATESETCAPTURE
        if (auto *b = find(false))
            for (unsigned key = 0; key < StateKeys; ++key)
                if (b->marks[key]) CopyStateKey(*b->snapshot, c.state, key);
        break;
    default:
        break;
    }
}

// DrawPrimitives2. The command buffer and the vertex buffer are DirectDraw
// surfaces in system memory or, with D3DHALDP2_USERMEMVERTICES, the program's
// own memory; both are copied out of the guest once per call.
unsigned Application::HalDrawPrimitives2(unsigned data)
{
    namespace D = H::Data;
    auto *context = HalContextOf(Read32(data + D::DP2Context));
    if (!context)
    {
        Write32(data + D::DP2Result, H::DDERR_INVALIDPARAMS);
        return H::DriverHandled;
    }
    auto &c = *context;
    auto &h = *ddhal;
    const unsigned flags = Read32(data + D::DP2Flags), fvf = Read32(data + D::DP2VertexType);
    const unsigned commands_lcl = Read32(data + D::DP2Commands);
    const unsigned command_offset = Read32(data + D::DP2CommandOffset), command_length = Read32(data + D::DP2CommandLength);
    const unsigned vertex_ref = Read32(data + D::DP2Vertices);
    const unsigned vertex_offset = Read32(data + D::DP2VertexOffset), vertex_length = Read32(data + D::DP2VertexLength);
    const unsigned rstates = Read32(data + D::DP2RenderStates);
    unsigned stride = Read32(data + D::DP2Result);   // dwVertexSize on the way in
    const auto layout = Gpu::VertexLayout::Of(fvf);
    // Older runtimes leave an HRESULT in the size/result union when
    // submitting translated execute buffers. It is not a vertex stride.
    if (!stride || (stride & 0x80000000u) || stride < layout.stride) stride = layout.stride;
    auto fail = [&](unsigned offset, const char *why, bool diagnostic = true) {
        if (diagnostic) DriverTrace(why, offset);
        Write32(data + D::DP2Result, H::D3DERR_COMMAND_UNPARSED);
        // D3DIM adds this to the surface base, not to dwCommandOffset.
        Write32(data + D::DP2ErrorOffset, command_offset + offset);
        return H::DriverHandled;
    };
    if (log && trace_calls)
    {
        char text[240];
        snprintf(text, sizeof text, "DDHAL DP2 data flags=%08x fvf=%08x commands=%08x+%u len=%u vertices=%08x+%u len=%u size=%u rstates=%08x",
                 flags, fvf, commands_lcl, command_offset, command_length, vertex_ref, vertex_offset, vertex_length, stride, rstates);
        log(text);
        if (vertex_ref && !(flags & D::DP2UserMemVertices))
        {
            const unsigned gbl = Read32(vertex_ref + H::SurfaceLocal::Global);
            snprintf(text, sizeof text, "DDHAL DP2 vertex surface gbl=%08x vidmem=%08x pitch=%08x caps=%08x", gbl,
                     gbl ? Read32(gbl + H::SurfaceGlobal::VidMem) : 0, gbl ? Read32(gbl + H::SurfaceGlobal::Pitch) : 0, Read32(vertex_ref + H::SurfaceLocal::Caps));
            log(text);
        }
    }
    if (!commands_lcl) return fail(0, "DDHAL DrawPrimitives2 without commands");
    const unsigned commands_gbl = Read32(commands_lcl + H::SurfaceLocal::Global);
    const unsigned command_base = (commands_gbl ? Read32(commands_gbl + H::SurfaceGlobal::VidMem) : 0) + command_offset;
    unsigned vertex_base = 0;
    if (flags & D::DP2UserMemVertices) vertex_base = vertex_ref + vertex_offset;
    else if (vertex_ref)
    {
        const unsigned gbl = Read32(vertex_ref + H::SurfaceLocal::Global);
        vertex_base = (gbl ? Read32(gbl + H::SurfaceGlobal::VidMem) : 0) + vertex_offset;
    }
    if (command_length > ~0u - 4 || !virtual_memory.Access(command_base, command_length, false) ||
        !Grow(h.commands, h.command_capacity, command_length + 4) || !Read(command_base, h.commands, command_length))
        return fail(0, "DDHAL DrawPrimitives2 command buffer unreadable");
    // State-only flushes do not define vertex data (D3DIM leaves ddrval in
    // the size union). The driver contract requires a vertex buffer only
    // when a primitive actually refers to it.
    unsigned vertex_count = 0;
    bool vertices_loaded = false;
    auto read_vertices = [&](unsigned needed) {
        if (vertices_loaded && needed <= vertex_count) return true;
        if (!vertex_base || !needed || needed > vertex_length) return false;
        const unsigned long long wide_bytes = (unsigned long long)needed * stride;
        if (wide_bytes > ~0u - 64) return false;
        const unsigned bytes = (unsigned)wide_bytes;
        if (!virtual_memory.Access(vertex_base, bytes, false) ||
            !Grow(h.vertices, h.vertex_capacity, bytes + 64) || !Read(vertex_base, h.vertices, bytes))
            return false;
        vertex_count = needed;
        vertices_loaded = true;
        return true;
    };
    const unsigned char *cmd = h.commands;
    auto word = [&](unsigned at) { return unsigned(cmd[at]) | (unsigned(cmd[at + 1]) << 8); };
    auto dword = [&](unsigned at) { return word(at) | (word(at + 2) << 16); };
    auto as_float = [&](unsigned at) { float f; const unsigned u = dword(at); memcpy(&f, &u, 4); return f; };
    bool begun = false;
    auto begin = [&]() {
        if (begun) return true;
        begun = HalBeginBatch(c);
        return begun;
    };
    auto draw = [&](Gpu::Draw &d) {
        // Execute runtimes can publish a maximum index range rather than
        // the allocation's vertex count. Read only the vertices this draw
        // references, retaining the runtime's range as an upper bound.
        unsigned long long needed = 0;
        if (d.indices)
        {
            for (unsigned i = 0; i < d.index_count; ++i)
                if (unsigned(d.indices[i]) + 1 > needed) needed = unsigned(d.indices[i]) + 1;
            needed += d.base_vertex;
        }
        else
        {
            const unsigned long long count = d.primitive_count;
            const auto elements = d.primitive == Gpu::PointList ? count :
                                  d.primitive == Gpu::LineList ? count * 2 :
                                  d.primitive == Gpu::LineStrip ? count + 1 :
                                  d.primitive == Gpu::TriangleList ? count * 3 : count + 2;
            needed = d.first_vertex + elements;
        }
        if (needed > ~0u || !read_vertices(unsigned(needed)) || !begin()) return false;
        d.fvf = fvf;
        d.stride = stride;
        d.vertices = h.vertices;
        d.vertex_count = vertex_count;
        HalBindTextures(c);
        if (log && trace_calls)
        {
            char text[200];
            const unsigned handle = c.state.stage[0][H::TSS::TextureMap];
            snprintf(text, sizeof text, "DDHAL draw prim=%u n=%u fvf=%08x s0 op=%u a1=%u a2=%u tex=%u found=%d s1 op=%u tex=%u tci=%u ttf=%u light=%u",
                     d.primitive, d.primitive_count, fvf, c.state.stage[0][H::TSS::ColorOp], c.state.stage[0][H::TSS::ColorArg1],
                     c.state.stage[0][H::TSS::ColorArg2], handle, HalSurfaceByHandle(handle) ? 1 : 0, c.state.stage[1][H::TSS::ColorOp],
                     c.state.stage[1][H::TSS::TextureMap], c.state.stage[0][H::TSS::TexCoordIndex], c.state.stage[0][H::TSS::TextureTransformFlags],
                     c.state.render[H::RS::Lighting]);
            log(text);
        }
        DrawLegacyTexture(*gpu_backend, d, c.state);
        return true;
    };
    // The DirectX 8 draws: vertices out of stream 0 (a buffer, or the
    // vertices of this call), indices out of the index buffer. The vertices a
    // draw uses are copied into their own scratch, from vertex 0, and indices
    // become sixteen bits relative to the first vertex copied.
    auto stream_stride = [&]() {
        const unsigned set = c.streams[0].stride;
        return set ? set : Gpu::VertexLayout::Of(c.vertex_shader).stride;
    };
    auto stream_draw = [&](unsigned type, unsigned primitives, unsigned offset, unsigned vertex_total, unsigned min_index,
                           bool indexed, unsigned index_offset) {
        if (!primitives) return true;
        // The vertex layout: a DirectX 8 shader's declaration, or a DirectX 9
        // declaration read through the dcl of the function or, without one,
        // through the registers of the fixed function pipeline; none for an
        // FVF.
        HalElement layout[32];
        unsigned layout_count = 0;
        bool declared = false, function = false, pretransformed = false;
        if (c.model9)
        {
            if (const auto *declaration = HalDeclaration9Of(c))
            {
                declared = true;
                const auto *program = c.function9 ? HalVertexShaderOf(c, c.function9) : nullptr;
                function = program && program->tokens;
                layout_count = function ? HalMapFunction(*declaration, *program, layout) : HalMapFixedFunction(*declaration, layout, pretransformed);
            }
        }
        else if (const auto *program = HalVertexShaderOf(c))
        {
            declared = true;
            function = program->tokens != nullptr;
            layout_count = HalElementsOf(*program, layout);
        }
        else if (c.vertex_shader & 1) { DriverTrace("DDHAL draw8 unknown vertex shader", c.vertex_shader); return true; }
        if (function && !c.state.vertex_shader.tokens) { DriverTrace("DDHAL draw8 vertex shader not set", c.function9); return true; }
        const bool program = declared;
        Gpu::Draw d;
        unsigned elements = 0;
        switch (type)   // D3DPRIMITIVETYPE
        {
        case 1: d.primitive = Gpu::PointList; elements = primitives; break;
        case 2: d.primitive = Gpu::LineList; elements = primitives * 2; break;
        case 3: d.primitive = Gpu::LineStrip; elements = primitives + 1; break;
        case 4: d.primitive = Gpu::TriangleList; elements = primitives * 3; break;
        case 5: d.primitive = Gpu::TriangleStrip; elements = primitives + 2; break;
        case 6: d.primitive = Gpu::TriangleFan; elements = primitives + 2; break;
        default: DriverTrace("DDHAL draw8 primitive type", type); return false;
        }
        const unsigned stride = stream_stride();
        const unsigned vertices = indexed ? vertex_total : elements;
        if ((!stride && !program) || !vertices || vertices > 0x10000) { DriverTrace("DDHAL draw8 vertex count", vertices); return false; }
        if (program)
        {
            // The streams of the declaration into input registers, from the
            // vertex the offset names in stream 0 on.
            unsigned slots = 0;
            if (!HalGatherInputs(c, layout, layout_count, stride ? offset / stride : 0, vertices, vertex_base, slots)) return false;
            d.shader_inputs = function;
            d.stride = slots * 16;
            // A declaration without a function feeds the fixed function
            // pipeline: its registers packed into the FVF they describe.
            if (!function && !HalPackDeclaration(layout, layout_count, pretransformed, vertices, slots, d.fvf, d.stride)) return false;
        }
        else
        {
            unsigned address = vertex_base, size = ~0u;
            if (c.streams[0].handle)
            {
                auto *buffer = HalSurfaceByHandle(c.streams[0].handle, true);
                if (!buffer || !HalBufferBytes(buffer->lcl, address, size)) { DriverTrace("DDHAL draw8 no stream buffer", c.streams[0].handle); return false; }
            }
            if (!address) { DriverTrace("DDHAL draw8 no vertices", 0); return false; }
            if (c.streams[0].handle)
            {
                address += c.streams[0].offset;
                size = c.streams[0].offset < size ? size - c.streams[0].offset : 0;
            }
            const unsigned bytes = vertices * stride;
            if (size != ~0u && (offset > size || bytes > size - offset)) { DriverTrace("DDHAL draw8 past the stream", offset); return false; }
            if (!Grow(h.stream_vertices, h.stream_capacity, bytes + 64) || !Read(address + offset, h.stream_vertices, bytes)) return false;
            d.stride = stride;
        }
        if (!program) d.fvf = c.vertex_shader;
        d.vertices = h.stream_vertices;
        d.vertex_count = vertices;
        d.primitive_count = primitives;
        if (indexed)
        {
            auto *buffer = HalSurfaceByHandle(c.index_handle, true);
            unsigned index_at = 0, index_size = 0;
            if (!buffer || !HalBufferBytes(buffer->lcl, index_at, index_size)) { DriverTrace("DDHAL draw8 no index buffer", c.index_handle); return false; }
            const unsigned index_bytes = elements * c.index_stride;
            if (index_offset > index_size || index_bytes > index_size - index_offset) { DriverTrace("DDHAL draw8 past the indices", index_offset); return false; }
            if (!GrowIndices(h.indices, h.index_capacity, elements * 2)) return false;
            unsigned char *raw = reinterpret_cast<unsigned char *>(h.indices + elements);
            if (c.index_stride == 4)
            {
                if (!GrowIndices(h.indices, h.index_capacity, elements * 3)) return false;
                raw = reinterpret_cast<unsigned char *>(h.indices + elements);
            }
            if (!Read(index_at + index_offset, raw, index_bytes)) return false;
            for (unsigned i = 0; i < elements; ++i)
            {
                const unsigned value = c.index_stride == 4 ? unsigned(raw[i * 4]) | unsigned(raw[i * 4 + 1]) << 8 |
                                                                 unsigned(raw[i * 4 + 2]) << 16 | unsigned(raw[i * 4 + 3]) << 24
                                                           : unsigned(raw[i * 2]) | unsigned(raw[i * 2 + 1]) << 8;
                const unsigned relative = value - min_index;
                h.indices[i] = (unsigned short)(relative < vertices ? relative : 0);
            }
            d.indices = h.indices;
            d.index_count = elements;
        }
        if (!begin()) return false;
        HalBindTextures(c);
        if (log && trace_calls)
        {
            char text[160];
            snprintf(text, sizeof text, "DDHAL draw8 type=%u n=%u fvf=%08x stride=%u vertices=%u indexed=%d stream=%u indices=%u",
                     type, primitives, c.vertex_shader, stride, vertices, indexed ? 1 : 0, c.streams[0].handle, c.index_handle);
            log(text);
        }
        DrawLegacyTexture(*gpu_backend, d, c.state);
        return true;
    };
    unsigned at = 0;
    while (at + 4 <= command_length)
    {
        const unsigned op = cmd[at], count = word(at + 2), start = at;
        at += 4;
        if (log && trace_calls)
        {
            char text[200];
            snprintf(text, sizeof text, "DDHAL DP2 op=%u count=%u at=%u fvf=%08x stride=%u vertices=%u", op, count, start, fvf, stride, vertex_count);
            log(text);
            if (vertex_count && (op == H::DP2::TriangleList || op == H::DP2::IndexedTriangleList2 || op == H::DP2::TriangleFan) && at + 2 <= command_length)
            {
                float v[4]; memcpy(v, h.vertices + word(at) * stride, 16);
                unsigned color = 0, specular = 0; memcpy(&color, h.vertices + word(at) * stride + 16, 4);
                if (stride >= 24) memcpy(&specular, h.vertices + word(at) * stride + 20, 4);
                snprintf(text, sizeof text, "DDHAL DP2 first vertex %u: x=%.2f y=%.2f z=%.4f rhw=%.4f color=%08x specular=%08x", word(at), v[0], v[1], v[2], v[3], color, specular);
                log(text);
            }
        }
        switch (op)
        {
        case H::DP2::TriangleList:
        case H::DP2::TriangleStrip:
        case H::DP2::TriangleFan:
        case H::DP2::LineList:
        case H::DP2::LineStrip:
        {
            if (at + 2 > command_length) return fail(start, "DDHAL DP2 truncated primitive");
            Gpu::Draw d;
            d.primitive = op == H::DP2::TriangleList ? Gpu::TriangleList : op == H::DP2::TriangleStrip ? Gpu::TriangleStrip
                        : op == H::DP2::TriangleFan ? Gpu::TriangleFan : op == H::DP2::LineList ? Gpu::LineList : Gpu::LineStrip;
            d.first_vertex = word(at);
            d.primitive_count = count;
            at += 2;
            if (count && !draw(d)) return fail(start, "DDHAL DP2 render target unusable");
            break;
        }
        case H::DP2::Points:
        {
            for (unsigned i = 0; i < count; ++i)
            {
                if (at + 4 > command_length) return fail(start, "DDHAL DP2 truncated points");
                Gpu::Draw d;
                d.primitive = Gpu::PointList;
                d.primitive_count = word(at);
                d.first_vertex = word(at + 2);
                at += 4;
                if (d.primitive_count && !draw(d)) return fail(start, "DDHAL DP2 render target unusable");
            }
            break;
        }
        case H::DP2::IndexedTriangleList:
        {
            if (at + count * 8 > command_length) return fail(start, "DDHAL DP2 truncated indexed list");
            if (!GrowIndices(h.indices, h.index_capacity, count * 3)) return fail(start, "DDHAL DP2 out of memory");
            for (unsigned i = 0; i < count; ++i)
            {
                h.indices[i * 3] = (unsigned short)word(at + i * 8);
                h.indices[i * 3 + 1] = (unsigned short)word(at + i * 8 + 2);
                h.indices[i * 3 + 2] = (unsigned short)word(at + i * 8 + 4);
            }
            at += count * 8;
            Gpu::Draw d;
            d.primitive = Gpu::TriangleList;
            d.indices = h.indices;
            d.index_count = count * 3;
            d.primitive_count = count;
            if (count && !draw(d)) return fail(start, "DDHAL DP2 render target unusable");
            break;
        }
        case H::DP2::IndexedTriangleList2:
        case H::DP2::IndexedLineList2:
        {
            const unsigned per = op == H::DP2::IndexedTriangleList2 ? 3 : 2;
            if (at + 2 + count * per * 2 > command_length) return fail(start, "DDHAL DP2 truncated indexed list 2");
            if (!GrowIndices(h.indices, h.index_capacity, count * per)) return fail(start, "DDHAL DP2 out of memory");
            const unsigned base = word(at);
            at += 2;
            for (unsigned i = 0; i < count * per; ++i) h.indices[i] = (unsigned short)word(at + i * 2);
            at += count * per * 2;
            Gpu::Draw d;
            d.primitive = per == 3 ? Gpu::TriangleList : Gpu::LineList;
            d.indices = h.indices;
            d.index_count = count * per;
            d.base_vertex = base;
            d.primitive_count = count;
            if (count && !draw(d)) return fail(start, "DDHAL DP2 render target unusable");
            break;
        }
        case H::DP2::IndexedLineList:
        {
            if (at + count * 4 > command_length) return fail(start, "DDHAL DP2 truncated indexed lines");
            if (!GrowIndices(h.indices, h.index_capacity, count * 2)) return fail(start, "DDHAL DP2 out of memory");
            for (unsigned i = 0; i < count * 2; ++i) h.indices[i] = (unsigned short)word(at + i * 2);
            at += count * 4;
            Gpu::Draw d;
            d.primitive = Gpu::LineList;
            d.indices = h.indices;
            d.index_count = count * 2;
            d.primitive_count = count;
            if (count && !draw(d)) return fail(start, "DDHAL DP2 render target unusable");
            break;
        }
        case H::DP2::IndexedTriangleStrip:
        case H::DP2::IndexedTriangleFan:
        case H::DP2::IndexedLineStrip:
        {
            const unsigned vertices = op == H::DP2::IndexedLineStrip ? count + 1 : count + 2;
            if (at + 2 + vertices * 2 > command_length) return fail(start, "DDHAL DP2 truncated indexed strip");
            if (!GrowIndices(h.indices, h.index_capacity, vertices)) return fail(start, "DDHAL DP2 out of memory");
            const unsigned base = word(at);
            at += 2;
            for (unsigned i = 0; i < vertices; ++i) h.indices[i] = (unsigned short)word(at + i * 2);
            at += vertices * 2;
            Gpu::Draw d;
            d.primitive = op == H::DP2::IndexedTriangleStrip ? Gpu::TriangleStrip
                        : op == H::DP2::IndexedTriangleFan ? Gpu::TriangleFan : Gpu::LineStrip;
            d.indices = h.indices;
            d.index_count = vertices;
            d.base_vertex = base;
            d.primitive_count = count;
            if (count && !draw(d)) return fail(start, "DDHAL DP2 render target unusable");
            break;
        }
        case H::DP2::TriangleFanImm:
        case H::DP2::LineListImm:
        {
            // The vertices follow the command inline, after the edge flags of
            // a fan, aligned to four bytes.
            const unsigned vertices = op == H::DP2::TriangleFanImm ? count + 2 : count * 2;
            if (op == H::DP2::TriangleFanImm) at += 4;
            at = (at + 3) & ~3u;
            if (at + vertices * stride > command_length) return fail(start, "DDHAL DP2 truncated immediate vertices");
            Gpu::Draw d;
            d.primitive = op == H::DP2::TriangleFanImm ? Gpu::TriangleFan : Gpu::LineList;
            d.primitive_count = count;
            if (count)
            {
                if (!begin()) return fail(start, "DDHAL DP2 render target unusable");
                d.fvf = fvf;
                d.stride = stride;
                d.vertices = cmd + at;
                d.vertex_count = vertices;
                HalBindTextures(c);
                DrawLegacyTexture(*gpu_backend, d, c.state);
            }
            at += vertices * stride;
            at = (at + 3) & ~3u;
            break;
        }
        case H::DP2::RenderState:
            if (at + count * 8 > command_length) return fail(start, "DDHAL DP2 truncated render states");
            for (unsigned i = 0; i < count; ++i)
            {
                HalSetRenderState(c, dword(at + i * 8), dword(at + i * 8 + 4), rstates);
                if (log && trace_calls)
                {
                    char text[80];
                    snprintf(text, sizeof text, "DDHAL DP2 renderstate %u = %08x", dword(at + i * 8), dword(at + i * 8 + 4));
                    log(text);
                }
            }
            at += count * 8;
            break;
        case H::DP2::TextureStageState:
            if (at + count * 8 > command_length) return fail(start, "DDHAL DP2 truncated stage states");
            for (unsigned i = 0; i < count; ++i)
            {
                HalSetStageState(c, word(at + i * 8), word(at + i * 8 + 2), dword(at + i * 8 + 4));
                if (log && trace_calls)
                {
                    char text[80];
                    snprintf(text, sizeof text, "DDHAL DP2 stagestate %u.%u = %08x", word(at + i * 8), word(at + i * 8 + 2), dword(at + i * 8 + 4));
                    log(text);
                }
            }
            at += count * 8;
            break;
        case H::DP2::ViewportInfo:
            if (at + count * 16 > command_length) return fail(start, "DDHAL DP2 truncated viewport");
            for (unsigned i = 0; i < count; ++i)
            {
                c.state.viewport_x = int(dword(at + i * 16));
                c.state.viewport_y = int(dword(at + i * 16 + 4));
                c.state.viewport_width = int(dword(at + i * 16 + 8));
                c.state.viewport_height = int(dword(at + i * 16 + 12));
            }
            HalRecord(c, KeyViewport);
            at += count * 16;
            break;
        case H::DP2::WInfo:
            if (at + count * 8 > command_length) return fail(start, "DDHAL DP2 truncated W range");
            for (unsigned i = 0; i < count; ++i) { c.state.w_near = as_float(at + i * 8); c.state.w_far = as_float(at + i * 8 + 4); }
            HalRecord(c, KeyWInfo);
            at += count * 8;
            break;
        case H::DP2::ZRange:
            if (at + count * 8 > command_length) return fail(start, "DDHAL DP2 truncated Z range");
            for (unsigned i = 0; i < count; ++i) { c.state.z_min = as_float(at + i * 8); c.state.z_max = as_float(at + i * 8 + 4); }
            HalRecord(c, KeyZRange);
            at += count * 8;
            break;
        case H::DP2::SetRenderTarget:
            if (at + count * 8 > command_length) return fail(start, "DDHAL DP2 truncated render target");
            for (unsigned i = 0; i < count; ++i)
            {
                if (begun) { gpu_backend->End(); begun = false; }
                auto *target = HalSurfaceByHandle(dword(at + i * 8), false, c.local);
                auto *z = HalSurfaceByHandle(dword(at + i * 8 + 4), false, c.local);
                if (log && (trace_calls || getenv("NEO98_TRACE_DDHAL_TARGET")))
                {
                    char text[160];
                    snprintf(text, sizeof text, "DDHAL DP2 SetRenderTarget handles=%u,%u lcl=%08x,%08x records=%u", dword(at + i * 8), dword(at + i * 8 + 4),
                             target ? target->lcl : 0, z ? z->lcl : 0, ddhal->surface_count);
                    log(text);
                }
                if ((target && c.target != target->lcl) || c.z != (z ? z->lcl : 0)) gpu_backend->Flush();
                if (target) c.target = target->lcl;
                c.z = z ? z->lcl : 0;
            }
            at += count * 8;
            break;
        case H::DP2::Clear:
        {
            if (at + 16 + count * 16 > command_length) return fail(start, "DDHAL DP2 truncated clear");
            const unsigned cflags = dword(at), color = dword(at + 4), stencil = dword(at + 12);
            const float depth = as_float(at + 8);
            int rects[64][4];
            const unsigned n = count < 64 ? count : 64;
            for (unsigned i = 0; i < n; ++i)
                for (unsigned k = 0; k < 4; ++k) rects[i][k] = int(dword(at + 16 + i * 16 + k * 4));
            // Like Clear2, DP2 receives the already clipped rectangle list.
            // Zero rectangles must not reach the backend's whole-view clear.
            if (n && begin()) gpu_backend->Clear(cflags, color, depth, stencil, &rects[0][0], n, c.state);
            at += 16 + count * 16;
            break;
        }
        case H::DP2::SetPalette:
            // D3DHAL_DP2SETPALETTE: dwPaletteHandle, dwPaletteFlags, dwSurfaceHandle.
            if (at + count * 12 > command_length) return fail(start, "DDHAL DP2 truncated palette");
            for (unsigned i = 0; i < count; ++i)
            {
                const unsigned handle = dword(at + i * 12), palette_flags = dword(at + i * 12 + 4);
                if (auto *s = HalSurfaceByHandle(dword(at + i * 12 + 8)))
                {
                    s->palette = handle;
                    ++s->generation;
                }
                // DDRAWIPAL_ALPHA (2000h): the peFlags of each entry are its alpha.
                auto *p = h.PaletteOf(handle, true);
                if (!p) return fail(start, "DDHAL DP2 palette allocation failed");
                p->alpha = (palette_flags & 0x2000) != 0;
                if (log && trace_calls)
                {
                    char text[80];
                    snprintf(text, sizeof text, "DDHAL DP2 setpalette handle=%u flags=%08x", handle, palette_flags);
                    log(text);
                }
            }
            at += count * 12;
            break;
        case H::DP2::UpdatePalette:
        {
            // D3DHAL_DP2UPDATEPALETTE: dwPaletteHandle, wStartIndex, wNumEntries, then the PALETTEENTRYs.
            if (at + 8 > command_length) return fail(start, "DDHAL DP2 truncated palette update");
            const unsigned handle = dword(at), first = word(at + 4), entries = word(at + 6);
            if (at + 8 + entries * 4 > command_length) return fail(start, "DDHAL DP2 truncated palette entries");
            auto *p = h.PaletteOf(handle, true);
            if (!p) return fail(start, "DDHAL DP2 palette allocation failed");
            for (unsigned i = 0; i < entries && first + i < 256; ++i)
            {
                const unsigned e = dword(at + 8 + i * 4);   // PALETTEENTRY: red, green, blue, flags
                p->colors[first + i] = ((e & 255) << 16) | (e & 0xff00) | ((e >> 16) & 255) | (e & 0xff000000u);
            }
            for (unsigned k = 0; k < h.surface_count; ++k) if (h.surfaces[k].palette == handle) ++h.surfaces[k].generation;
            at += 8 + entries * 4;
            break;
        }
        case H::DP2::StateSet:
            // D3DHAL_DP2STATESET: dwOperation, dwParam, sbType.
            if (at + count * 12 > command_length) return fail(start, "DDHAL DP2 truncated state set");
            for (unsigned i = 0; i < count; ++i) HalStateSet(c, dword(at + i * 12), dword(at + i * 12 + 4), rstates);
            at += count * 12;
            break;
        case H::DP2::SetTexLod:
        case H::DP2::SetPriority:
            at += count * 8;
            break;
        case H::DP2::SetMaterial:
            // D3DMATERIAL7: diffuse, ambient, specular, emissive, power.
            if (at + count * 68 > command_length) return fail(start, "DDHAL DP2 truncated material");
            for (unsigned i = 0; i < count; ++i)
            {
                Direct3DState::Material(c.state.material, cmd + at + i * 68);
            }
            HalRecord(c, KeyMaterial);
            at += count * 68;
            break;
        case H::DP2::SetLight:
            // D3DHAL_DP2SETLIGHT: dwIndex, dwDataType, then a D3DLIGHT7 when the type says so.
            for (unsigned i = 0; i < count; ++i)
            {
                if (at + 8 > command_length) return fail(start, "DDHAL DP2 truncated light");
                const unsigned type = dword(at + 4);
                if (log && trace_calls)
                {
                    char text[80];
                    snprintf(text, sizeof text, "DDHAL DP2 setlight index=%u type=%u", dword(at), type);
                    log(text);
                }
                const unsigned index = dword(at);
                at += 8;
                auto *light = index < Gpu::State::Lights ? &c.state.lights[index] : nullptr;
                // D3DHAL_SETLIGHT_ENABLE 0, _DISABLE 1, _DATA 2 with a D3DLIGHT7.
                if (type == 0 && light) light->enabled = true;
                else if (type == 1 && light) light->enabled = false;
                if ((type == 0 || type == 1) && light) HalRecord(c, KeyLightEnable + index);
                else if (type == 2)
                {
                    if (at + 104 > command_length) return fail(start, "DDHAL DP2 truncated light data");
                    if (light)
                    {
                        Direct3DState::Light(*light, cmd + at);
                        HalRecord(c, KeyLight + index);
                    }
                    at += 104;
                }
            }
            break;
        case H::DP2::CreateLight:
            at += count * 4;   // D3DHAL_DP2CREATELIGHT: dwIndex
            break;
        case H::DP2::SetTransform:
        case H::DP2::MultiplyTransform:
            // D3DHAL_DP2SETTRANSFORM: xfrmType, then the D3DMATRIX. WORLD 1,
            // VIEW 2, PROJECTION 3, WORLD1 to WORLD3 4 to 6, TEXTURE0 to
            // TEXTURE7 16 to 23; the DirectX 8 numbers 256 to 259 name the
            // world matrices too. D3DHAL_DP2MULTIPLYTRANSFORM has the same
            // form and puts the matrix in front of the one the state holds.
            if (at + count * 68 > command_length) return fail(start, "DDHAL DP2 truncated transform");
            for (unsigned i = 0; i < count; ++i)
            {
                const unsigned b = at + i * 68, type = dword(b);
                float *m = Direct3DState::Transform(c.state, type);
                if (m)
                {
                    float given[16];
                    for (unsigned k = 0; k < 16; ++k) given[k] = as_float(b + 4 + k * 4);
                    if (op == H::DP2::MultiplyTransform)
                    {
                        float product[16];
                        for (unsigned r = 0; r < 4; ++r)
                            for (unsigned col = 0; col < 4; ++col)
                            {
                                float sum = 0.0f;
                                for (unsigned k = 0; k < 4; ++k) sum += given[r * 4 + k] * m[k * 4 + col];
                                product[r * 4 + col] = sum;
                            }
                        memcpy(given, product, sizeof given);
                    }
                    memcpy(m, given, sizeof given);
                    const unsigned slot = type == 1 ? 0 : type == 2 ? 4 : type == 3 ? 5 : type >= 4 && type <= 6 ? type - 3
                                        : type >= 16 && type < 24 ? 6 + type - 16 : type - 256;
                    HalRecord(c, KeyTransform + slot);
                }
                if (log && trace_calls)
                {
                    char text[200];
                    snprintf(text, sizeof text, "DDHAL DP2 transform %u: %.3f %.3f %.3f %.3f / %.3f %.3f %.3f %.3f / %.3f %.3f %.3f %.3f / %.3f %.3f %.3f %.3f",
                             type, as_float(b + 4), as_float(b + 8), as_float(b + 12), as_float(b + 16), as_float(b + 20), as_float(b + 24),
                             as_float(b + 28), as_float(b + 32), as_float(b + 36), as_float(b + 40), as_float(b + 44), as_float(b + 48),
                             as_float(b + 52), as_float(b + 56), as_float(b + 60), as_float(b + 64));
                    log(text);
                }
            }
            at += count * 68;
            break;
        case H::DP2::SetClipPlane:
            // D3DHAL_DP2SETCLIPPLANE: dwIndex, plane[4].
            if (at + count * 20 > command_length) return fail(start, "DDHAL DP2 truncated clip plane");
            for (unsigned i = 0; i < count; ++i)
            {
                const unsigned index = dword(at + i * 20);
                if (index < Gpu::State::ClipPlanes)
                {
                    for (unsigned k = 0; k < 4; ++k) c.state.clip_plane[index][k] = as_float(at + i * 20 + 4 + k * 4);
                    HalRecord(c, KeyClip + index);
                }
            }
            at += count * 20;
            break;
        case H::DP2::Ext:
            // D3DHAL_DP2EXT: dwExtension, dwSize of the data that follows.
            if (at + 8 > command_length) return fail(start, "DDHAL DP2 truncated extension");
            at += 8 + dword(at + 4);
            break;
        case H::DP2::TexBlt:
            // D3DHAL_DP2TEXBLT: dwDDDestSurface, dwDDSrcSurface, pDest,
            // rSrc, dwFlags; the texture load of IDirect3DDevice7::Load.
            if (at + count * 36 > command_length) return fail(start, "DDHAL DP2 truncated texture blit");
            if (begun) { gpu_backend->End(); begun = false; }
            for (unsigned i = 0; i < count; ++i)
            {
                const unsigned b = at + i * 36;
                const int point[2] = {int(dword(b + 8)), int(dword(b + 12))};
                const int rect[4] = {int(dword(b + 16)), int(dword(b + 20)), int(dword(b + 24)), int(dword(b + 28))};
                HalTextureBlt(dword(b), dword(b + 4), point, rect);
            }
            at += count * 36;
            break;
        case H::DP2::VolumeBlt:
            // D3DHAL_DP2VOLUMEBLT: dwDDDestSurface, dwDDSrcSurface, the
            // destination x, y and z, the source D3DBOX (left, top, right,
            // bottom, front, back) and dwFlags; UpdateTexture of a volume.
            if (at + count * 48 > command_length) return fail(start, "DDHAL DP2 truncated volume blit");
            if (begun) { gpu_backend->End(); begun = false; }
            for (unsigned i = 0; i < count; ++i)
            {
                const unsigned b = at + i * 48;
                const int point[3] = {int(dword(b + 8)), int(dword(b + 12)), int(dword(b + 16))};
                int box[6];
                for (unsigned k = 0; k < 6; ++k) box[k] = int(dword(b + 20 + k * 4));
                HalVolumeBlt(dword(b), dword(b + 4), point, box);
            }
            at += count * 48;
            break;
        case H::DP2::SetVertexShader:
            // D3DHAL_DP2VERTEXSHADER: the handle, an FVF code for the fixed
            // function pipeline or a shader the context was given.
            if (at + count * 4 > command_length) return fail(start, "DDHAL DP2 truncated vertex shader");
            if (count) HalSetVertexShader(c, dword(at + (count - 1) * 4));
            at += count * 4;
            break;
        case H::DP2::DeleteVertexShader:
            if (at + count * 4 > command_length) return fail(start, "DDHAL DP2 truncated shader handle");
            for (unsigned i = 0; i < count; ++i) HalDeleteVertexShader(c, dword(at + i * 4));
            at += count * 4;
            break;
        case H::DP2::DeletePixelShader:
        case H::DP2::SetPixelShader:
            // D3DHAL_DP2PIXELSHADER: the handle; zero is the texture stages.
            if (at + count * 4 > command_length) return fail(start, "DDHAL DP2 truncated shader handle");
            for (unsigned i = 0; i < count; ++i)
            {
                const unsigned handle = dword(at + i * 4);
                if (op == H::DP2::SetPixelShader) HalSetPixelShader(c, handle);
                else HalDeletePixelShader(c, handle);
            }
            at += count * 4;
            break;
        case H::DP2::CreateVertexShader:
            // D3DHAL_DP2CREATEVERTEXSHADER: handle, declaration and code
            // sizes in bytes, then the declaration and the code.
            for (unsigned i = 0; i < count; ++i)
            {
                if (at + 12 > command_length) return fail(start, "DDHAL DP2 truncated vertex shader creation");
                const unsigned declaration = dword(at + 4), code = dword(at + 8);
                if (at + 12 + declaration + code > command_length) return fail(start, "DDHAL DP2 truncated vertex shader code");
                HalCreateVertexShader(c, dword(at), cmd + at + 12, declaration, cmd + at + 12 + declaration, code);
                at += 12 + declaration + code;
            }
            break;
        case H::DP2::CreatePixelShader:
            // D3DHAL_DP2CREATEPIXELSHADER: handle, code size, the code.
            for (unsigned i = 0; i < count; ++i)
            {
                if (at + 8 > command_length) return fail(start, "DDHAL DP2 truncated pixel shader creation");
                const unsigned bytes = dword(at + 4);
                if (at + 8 + bytes > command_length) return fail(start, "DDHAL DP2 truncated pixel shader code");
                HalCreatePixelShader(c, dword(at), cmd + at + 8, bytes);
                at += 8 + bytes;
            }
            break;
        case H::DP2::SetVertexShaderConst:
        case H::DP2::SetPixelShaderConst:
            // D3DHAL_DP2SETVERTEXSHADERCONST: first register and count, then
            // four floats per register.
            for (unsigned i = 0; i < count; ++i)
            {
                if (at + 8 > command_length) return fail(start, "DDHAL DP2 truncated shader constants");
                const unsigned first = dword(at), registers = dword(at + 4), bytes = registers * 16;
                if (at + 8 + bytes > command_length) return fail(start, "DDHAL DP2 truncated shader constant data");
                if (op == H::DP2::SetPixelShaderConst)
                    for (unsigned r = 0; r < registers && first + r < Gpu::State::PixelConstants; ++r)
                        for (unsigned k = 0; k < 4; ++k) c.state.ps_constants[first + r][k] = as_float(at + 8 + r * 16 + k * 4);
                else
                    for (unsigned r = 0; r < registers && first + r < Gpu::State::VertexConstants; ++r)
                        for (unsigned k = 0; k < 4; ++k) c.state.vs_constants[first + r][k] = as_float(at + 8 + r * 16 + k * 4);
                at += 8 + bytes;
            }
            break;
        case H::DP2::SetStreamSource:
            // D3DHAL_DP2SETSTREAMSOURCE: stream, buffer handle, stride.
            if (at + count * 12 > command_length) return fail(start, "DDHAL DP2 truncated stream source");
            for (unsigned i = 0; i < count; ++i)
            {
                const unsigned stream = dword(at + i * 12);
                if (stream >= H::DP2::Streams) continue;
                c.streams[stream].handle = dword(at + i * 12 + 4);
                c.streams[stream].stride = dword(at + i * 12 + 8);
                c.streams[stream].offset = 0;
                if (log && trace_calls)
                {
                    char text[96];
                    snprintf(text, sizeof text, "DDHAL DP2 stream %u handle=%u stride=%u", stream, c.streams[stream].handle, c.streams[stream].stride);
                    log(text);
                }
            }
            at += count * 12;
            break;
        case H::DP2::CreateVertexShaderDecl:
            // D3DHAL_DP2CREATEVERTEXSHADERDECL: handle and element count, then the elements.
            for (unsigned i = 0; i < count; ++i)
            {
                if (at + 8 > command_length) return fail(start, "DDHAL DP2 truncated declaration");
                const unsigned elements = dword(at + 4);
                if (at + 8 + elements * 8 > command_length) return fail(start, "DDHAL DP2 truncated declaration elements");
                HalCreateDeclaration9(c, dword(at), cmd + at + 8, elements);
                at += 8 + elements * 8;
            }
            break;
        case H::DP2::CreateVertexShaderFunc:
            // D3DHAL_DP2CREATEVERTEXSHADERFUNC: handle and size, then the tokens.
            for (unsigned i = 0; i < count; ++i)
            {
                if (at + 8 > command_length) return fail(start, "DDHAL DP2 truncated shader function");
                const unsigned bytes = dword(at + 4);
                if (at + 8 + bytes > command_length) return fail(start, "DDHAL DP2 truncated shader function code");
                HalCreateVertexShader(c, dword(at), nullptr, 0, cmd + at + 8, bytes);
                at += 8 + bytes;
            }
            break;
        case H::DP2::DeleteVertexShaderDecl:
        case H::DP2::SetVertexShaderDecl:
        case H::DP2::DeleteVertexShaderFunc:
        case H::DP2::SetVertexShaderFunc:
            if (at + count * 4 > command_length) return fail(start, "DDHAL DP2 truncated shader handle");
            for (unsigned i = 0; i < count; ++i)
            {
                const unsigned handle = dword(at + i * 4);
                c.model9 = true;
                if (op == H::DP2::DeleteVertexShaderDecl) HalDeleteDeclaration9(c, handle);
                else if (op == H::DP2::DeleteVertexShaderFunc) HalDeleteVertexShader(c, handle);
                else if (op == H::DP2::SetVertexShaderDecl)
                {
                    // A declaration, or an FVF code for the fixed function pipeline.
                    c.declaration9 = handle;
                    c.vertex_shader = HalDeclaration9Of(c) ? 0 : handle;
                }
                else
                {
                    c.function9 = handle;
                    c.state.vertex_shader = {};
                    if (const auto *program = handle ? HalVertexShaderOf(c, handle) : nullptr)
                    {
                        c.state.vertex_shader.tokens = program->tokens;
                        c.state.vertex_shader.count = program->count;
                        c.state.vertex_shader.key = program->key;
                    }
                }
            }
            at += count * 4;
            break;
        case H::DP2::SetVertexShaderConstI:
        case H::DP2::SetPixelShaderConstI:
        case H::DP2::SetVertexShaderConstB:
        case H::DP2::SetPixelShaderConstB:
        {
            // The integer (four per register) and boolean (one) constants.
            const bool integer = op == H::DP2::SetVertexShaderConstI || op == H::DP2::SetPixelShaderConstI;
            const bool vertex = op == H::DP2::SetVertexShaderConstI || op == H::DP2::SetVertexShaderConstB;
            const unsigned per = integer ? 16 : 4;
            for (unsigned i = 0; i < count; ++i)
            {
                if (at + 8 > command_length) return fail(start, "DDHAL DP2 truncated shader constants");
                const unsigned first = dword(at), registers = dword(at + 4), bytes = registers * per;
                if (at + 8 + bytes > command_length) return fail(start, "DDHAL DP2 truncated shader constant data");
                for (unsigned r = 0; r < registers && first + r < Gpu::State::IntegerConstants; ++r)
                {
                    if (integer)
                        for (unsigned k = 0; k < 4; ++k)
                            (vertex ? c.state.vs_integers : c.state.ps_integers)[first + r][k] = int(dword(at + 8 + r * 16 + k * 4));
                    else (vertex ? c.state.vs_booleans : c.state.ps_booleans)[first + r] = dword(at + 8 + r * 4) ? 1 : 0;
                }
                at += 8 + bytes;
            }
            break;
        }
        case H::DP2::SetRenderTarget2:
            // D3DHAL_DP2SETRENDERTARGET2: index and surface handle.
            if (at + count * 8 > command_length) return fail(start, "DDHAL DP2 truncated render target 2");
            for (unsigned i = 0; i < count; ++i)
            {
                if (dword(at + i * 8) != 0) continue;
                if (begun) { gpu_backend->End(); begun = false; }
                if (auto *target = HalSurfaceByHandle(dword(at + i * 8 + 4), false, c.local))
                {
                    if (c.target != target->lcl) gpu_backend->Flush();
                    c.target = target->lcl;
                }
            }
            at += count * 8;
            break;
        case H::DP2::SetDepthStencil:
            // D3DHAL_DP2SETDEPTHSTENCIL: the handle of the Z buffer, zero for none.
            if (at + count * 4 > command_length) return fail(start, "DDHAL DP2 truncated depth stencil");
            if (count)
            {
                if (begun) { gpu_backend->End(); begun = false; }
                auto *z = HalSurfaceByHandle(dword(at + (count - 1) * 4), false, c.local);
                if (c.z != (z ? z->lcl : 0)) gpu_backend->Flush();
                c.z = z ? z->lcl : 0;
            }
            at += count * 4;
            break;
        case H::DP2::SetStreamSourceFreq:
            // Instancing frequency, which this driver does not offer.
            if (at + count * 8 > command_length) return fail(start, "DDHAL DP2 truncated stream frequency");
            at += count * 8;
            break;
        case H::DP2::CreateQuery:
            // D3DHAL_DP2CREATEQUERY: id and D3DQUERYTYPE.
            if (at + count * 8 > command_length) return fail(start, "DDHAL DP2 truncated query");
            for (unsigned i = 0; i < count; ++i)
                if (c.queries.Push(dword(at + i * 8)) && !c.queries.Push(dword(at + i * 8 + 4)))
                    c.queries.Erase(c.queries.count - 1, 1);
            at += count * 8;
            break;
        case H::DP2::DeleteQuery:
            // D3DHAL_DP2DELETEQUERY: the id.
            if (at + count * 4 > command_length) return fail(start, "DDHAL DP2 truncated query");
            for (unsigned i = 0; i < count; ++i)
            {
                const unsigned id = dword(at + i * 4);
                for (unsigned k = 0; k + 1 < c.queries.count; k += 2)
                    if (c.queries.items[k] == id) { c.queries.Erase(k, 2); break; }
                for (unsigned k = 0; k < c.signalled.count; ++k)
                    if (c.signalled.items[k] == id) { c.signalled.Erase(k, 1); break; }
            }
            at += count * 4;
            break;
        case H::DP2::IssueQuery:
            // D3DHAL_DP2ISSUEQUERY: id and D3DISSUE_ flags. An event is over
            // once the commands before it are given to the GPU; the backend
            // keeps their order, so the answer goes back with this call.
            if (at + count * 8 > command_length) return fail(start, "DDHAL DP2 truncated query");
            for (unsigned i = 0; i < count; ++i)
            {
                const unsigned id = dword(at + i * 8);
                if (!(dword(at + i * 8 + 4) & H::DP2::IssueEnd)) continue;
                gpu_backend->Flush();
                for (unsigned k = 0; k + 1 < c.queries.count; k += 2)
                    if (c.queries.items[k] == id && c.queries.items[k + 1] == H::DP2::QueryEvent) { c.signalled.Push(id); break; }
            }
            at += count * 8;
            break;
        case H::DP2::GenerateMipSubLevels:
        {
            // D3DHAL_DP2GENERATEMIPSUBLEVELS: the texture handle and a filter;
            // every level is the one above, halved by a stretching blit.
            if (at + count * 8 > command_length) return fail(start, "DDHAL DP2 truncated mip generation");
            if (begun) { gpu_backend->End(); begun = false; }
            for (unsigned i = 0; i < count; ++i)
            {
                const unsigned handle = dword(at + i * 8);
                auto *record = HalSurfaceByHandle(handle);
                for (unsigned level = 0; record; ++level)
                {
                    const unsigned upper = HalMipLevel(record->lcl, level), lower = HalMipLevel(record->lcl, level + 1);
                    if (!upper || !lower) break;
                    auto size = [&](unsigned lcl, int *rect) {
                        const unsigned gbl = Read32(lcl + H::SurfaceLocal::Global), packed = Read32(gbl + H::SurfaceGlobal::Height);
                        rect[0] = rect[1] = 0;
                        rect[2] = int(packed >> 16);
                        rect[3] = int(packed & 0xffff);
                    };
                    int source[4], dest[4];
                    size(upper, source);
                    size(lower, dest);
                    if (!HalBlt9(handle, source, level, handle, dest, level + 1, false, 0)) break;
                }
            }
            at += count * 8;
            break;
        }
        case H::DP2::Blt9:
        case H::DP2::SurfaceBlt:
            // D3DHAL_DP2BLT and SURFACEBLT: source handle, rect and mip level,
            // destination handle, rect and mip level, flags (StretchRect,
            // UpdateSurface).
            if (at + count * 52 > command_length) return fail(start, "DDHAL DP2 truncated blit");
            if (begun) { gpu_backend->End(); begun = false; }
            for (unsigned i = 0; i < count; ++i)
            {
                const unsigned b = at + i * 52;
                const int source[4] = {int(dword(b + 4)), int(dword(b + 8)), int(dword(b + 12)), int(dword(b + 16))};
                const int dest[4] = {int(dword(b + 28)), int(dword(b + 32)), int(dword(b + 36)), int(dword(b + 40))};
                HalBlt9(dword(b), source, dword(b + 20), dword(b + 24), dest, dword(b + 44), false, 0);
            }
            at += count * 52;
            break;
        case H::DP2::ColorFill:
            // D3DHAL_DP2COLORFILL: surface handle, rect, D3DCOLOR.
            if (at + count * 24 > command_length) return fail(start, "DDHAL DP2 truncated colour fill");
            if (begun) { gpu_backend->End(); begun = false; }
            for (unsigned i = 0; i < count; ++i)
            {
                const unsigned b = at + i * 24;
                const int rect[4] = {int(dword(b + 4)), int(dword(b + 8)), int(dword(b + 12)), int(dword(b + 16))};
                HalBlt9(0, nullptr, 0, dword(b), rect, 0, true, dword(b + 20));
            }
            at += count * 24;
            break;
        case H::DP2::SetScissorRect:
            // D3DHAL_DP2SETSCISSORRECT: a RECT.
            if (at + count * 16 > command_length) return fail(start, "DDHAL DP2 truncated scissor rect");
            if (count)
                for (unsigned k = 0; k < 4; ++k) c.state.scissor[k] = int(dword(at + (count - 1) * 16 + k * 4));
            at += count * 16;
            break;
        case H::DP2::SetStreamSource2:
            // D3DHAL_DP2SETSTREAMSOURCE2: stream, buffer handle, byte offset, stride.
            if (at + count * 16 > command_length) return fail(start, "DDHAL DP2 truncated stream source 2");
            for (unsigned i = 0; i < count; ++i)
            {
                const unsigned stream = dword(at + i * 16);
                if (stream >= H::DP2::Streams) continue;
                c.streams[stream].handle = dword(at + i * 16 + 4);
                c.streams[stream].offset = dword(at + i * 16 + 8);
                c.streams[stream].stride = dword(at + i * 16 + 12);
            }
            at += count * 16;
            break;
        case H::DP2::SetStreamSourceUM:
            // D3DHAL_DP2SETSTREAMSOURCEUM: stream and stride; the vertices
            // are the ones this DrawPrimitives2 brings.
            if (at + count * 8 > command_length) return fail(start, "DDHAL DP2 truncated user stream");
            for (unsigned i = 0; i < count; ++i)
            {
                const unsigned stream = dword(at + i * 8);
                if (stream >= H::DP2::Streams) continue;
                c.streams[stream].handle = 0;
                c.streams[stream].stride = dword(at + i * 8 + 4);
                c.streams[stream].offset = 0;
            }
            at += count * 8;
            break;
        case H::DP2::SetIndices:
            // D3DHAL_DP2SETINDICES: buffer handle and index size.
            if (at + count * 8 > command_length) return fail(start, "DDHAL DP2 truncated indices");
            if (count)
            {
                c.index_handle = dword(at + (count - 1) * 8);
                c.index_stride = dword(at + (count - 1) * 8 + 4) == 4 ? 4 : 2;
            }
            at += count * 8;
            break;
        case H::DP2::DrawPrimitive:
        case H::DP2::DrawPrimitive2:
        {
            // D3DHAL_DP2DRAWPRIMITIVE: type, first vertex, primitive count;
            // D3DHAL_DP2DRAWPRIMITIVE2: type, byte offset of the first
            // vertex, primitive count.
            if (at + count * 12 > command_length) return fail(start, "DDHAL DP2 truncated draw");
            for (unsigned i = 0; i < count; ++i)
            {
                const unsigned b = at + i * 12, type = dword(b), first = dword(b + 4), primitives = dword(b + 8);
                const unsigned offset = op == H::DP2::DrawPrimitive ? first * stream_stride() : first;
                if (!stream_draw(type, primitives, offset, 0, 0, false, 0)) return fail(start, "DDHAL DP2 stream draw failed");
            }
            at += count * 12;
            break;
        }
        case H::DP2::DrawIndexedPrimitive:
        case H::DP2::DrawIndexedPrimitive2:
        {
            // D3DHAL_DP2DRAWINDEXEDPRIMITIVE: type, base vertex index, min
            // index, vertex count, start index, primitive count. The second
            // form has the base vertex and the start index as byte offsets.
            if (at + count * 24 > command_length) return fail(start, "DDHAL DP2 truncated indexed draw");
            for (unsigned i = 0; i < count; ++i)
            {
                const unsigned b = at + i * 24, type = dword(b), base = dword(b + 4), min_index = dword(b + 8);
                const unsigned vertices = dword(b + 12), start_index = dword(b + 16), primitives = dword(b + 20);
                const bool second = op == H::DP2::DrawIndexedPrimitive2;
                const unsigned offset = second ? base + min_index * stream_stride() : (base + min_index) * stream_stride();
                const unsigned index_offset = second ? start_index : start_index * c.index_stride;
                if (!stream_draw(type, primitives, offset, vertices, min_index, true, index_offset))
                    return fail(start, "DDHAL DP2 indexed stream draw failed");
            }
            at += count * 24;
            break;
        }
        case H::DP2::BufferBlt:
        {
            // D3DHAL_DP2BUFFERBLT: destination and source buffer, the offset
            // in the destination, the D3DRANGE of the source, flags.
            if (at + count * 24 > command_length) return fail(start, "DDHAL DP2 truncated buffer blit");
            for (unsigned i = 0; i < count; ++i)
            {
                const unsigned b = at + i * 24;
                auto *dest = HalSurfaceByHandle(dword(b), true);
                auto *source = HalSurfaceByHandle(dword(b + 4), true);
                if (!dest || !source) continue;
                unsigned dest_at = 0, dest_size = 0, source_at = 0, source_size = 0;
                if (!HalBufferBytes(dest->lcl, dest_at, dest_size) || !HalBufferBytes(source->lcl, source_at, source_size)) continue;
                const unsigned offset = dword(b + 8), from = dword(b + 12), bytes = dword(b + 16);
                if (from > source_size || bytes > source_size - from || offset > dest_size || bytes > dest_size - offset) continue;
                if (!Grow(h.stream_vertices, h.stream_capacity, bytes) || !Read(source_at + from, h.stream_vertices, bytes)) continue;
                Store(dest_at + offset, h.stream_vertices, bytes);
            }
            at += count * 24;
            break;
        }
        case H::DP2::AddDirtyRect:
            if (at + count * 20 > command_length) return fail(start, "DDHAL DP2 truncated dirty rect");
            at += count * 20;
            break;
        case H::DP2::AddDirtyBox:
            if (at + count * 28 > command_length) return fail(start, "DDHAL DP2 truncated dirty box");
            at += count * 28;
            break;
        default:
        {
            // DDRAW supplies this callback through GetDriverInfo. It can
            // skip opaque tokens; Execute operations still return UNPARSED
            // so that D3DIM handles them and resumes the driver afterwards.
            if (begun)
            {
                gpu_backend->End();
                HalShowIfVisible(c);
                begun = false;
            }
            if (!h.parse_unknown) return fail(start, "DDHAL DP2 unknown command");
            const unsigned next = Allocate(4);
            if (!next) return fail(start, "DDHAL DP2 parser allocation failed");
            Write32(next, 0);
            const unsigned args[] = {command_base + start, next};
            const unsigned result = Call(h.parse_unknown, args, 2);
            const unsigned resume = Read32(next);
            Free(next);
            if (result || resume < command_base || resume - command_base <= start ||
                resume - command_base > command_length)
                return fail(start, "DDHAL DP2 runtime parser refused command", result != H::D3DERR_COMMAND_UNPARSED);
            at = resume - command_base;
            break;
        }
        }
    }
    if (begun)
    {
        gpu_backend->End();
        HalShowIfVisible(c);
    }
    if (log && trace_calls)
    {
        Gpu::Surface view;
        if (HalSurfaceView(c.target, view))
        {
            char text[160];
            const unsigned char *p = view.bits + (view.height / 2) * view.pitch + (view.width / 2) * (view.bit_count / 8);
            snprintf(text, sizeof text, "DDHAL DP2 end target=%08x bits=%p begun=%d centre=%02x%02x%02x%02x", c.target, (void *)view.bits, begun ? 1 : 0, p[0], p[1], p[2], p[3]);
            log(text);
        }
    }
    // The answers to the queries: D3DHAL_DP2RESPONSE with RESPONSEQUERY and
    // one {id, size, BOOL} per event, written over the start of the command
    // buffer, their bytes in dwErrorOffset (read by d3d9.dll).
    unsigned answered = 0;
    if (c.signalled.count && commands_gbl)
    {
        const unsigned room = Read32(commands_gbl + H::SurfaceGlobal::Pitch);
        const unsigned fit = room > 8 ? (room - 8) / 12 : 0;
        const unsigned n = c.signalled.count < fit ? c.signalled.count : fit;
        if (n)
        {
            const unsigned base = Read32(commands_gbl + H::SurfaceGlobal::VidMem);
            answered = 8 + n * 12;
            Write32(base, H::DP2::ResponseQuery | (n << 16));
            Write32(base + 4, answered);
            for (unsigned i = 0; i < n; ++i)
            {
                Write32(base + 8 + i * 12, c.signalled.items[i]);
                Write32(base + 12 + i * 12, 4);
                Write32(base + 16 + i * 12, 1);
            }
            c.signalled.Erase(0, n);
        }
    }
    Write32(data + D::DP2ErrorOffset, answered);
    Write32(data + D::DP2Result, H::DD_OK);
    return H::DriverHandled;
}

unsigned Application::HalClear2(unsigned data)
{
    namespace D = H::Data;
    auto *context = HalContextOf(Read32(data));
    if (!context)
    {
        Write32(data + D::ClearResult, H::DDERR_INVALIDPARAMS);
        return H::DriverHandled;
    }
    auto &c = *context;
    const unsigned flags = Read32(data + D::ClearFlags), color = Read32(data + D::ClearColor);
    const unsigned stencil = Read32(data + D::ClearStencil), rects = Read32(data + D::ClearRects);
    unsigned count = Read32(data + D::ClearRectCount);
    // D3DIM700 1e161 clips the public rectangles against the viewport.
    // A public null list becomes one viewport rectangle; an empty clipped
    // list reaches Clear2 with zero rectangles (1e2d0, callback 1e315).
    if (!count)
    {
        Write32(data + D::ClearResult, H::DD_OK);
        return H::DriverHandled;
    }
    float depth = 1.0f;
    {
        const unsigned bits = Read32(data + D::ClearDepth);
        memcpy(&depth, &bits, 4);
    }
    int list[64][4];
    if (count > 64) count = 64;
    for (unsigned i = 0; i < count; ++i)
    {
        unsigned r[4] = {};
        Read(rects + i * 16, r, 16);
        for (unsigned k = 0; k < 4; ++k) list[i][k] = int(r[k]);
    }
    if (!HalBeginBatch(c))
    {
        Write32(data + D::ClearResult, H::DDERR_GENERIC);
        return H::DriverHandled;
    }
    gpu_backend->Clear(flags, color, depth, stencil, count ? &list[0][0] : nullptr, count, c.state);
    gpu_backend->End();
    HalShowIfVisible(c);
    Write32(data + D::ClearResult, H::DD_OK);
    return H::DriverHandled;
}

// ---------------------------------------------------------------------------
// The gates of NEO98DD32.DLL. Every callback takes one pointer and answers
// DDHAL_DRIVER_HANDLED or DDHAL_DRIVER_NOTHANDLED, with the HRESULT inside
// the block.
unsigned Application::DisplayDriverApi(ApiName name, const unsigned *a)
{
    namespace D = H::Data;
    const unsigned data = a[0];
    if (!gpu_backend || !data)
        return H::DriverNotHandled;
    // DDRAW's session-wide driver can outlive the process that created it.
    // A later process calls its cached HAL callbacks without requesting a
    // new driver object or DDHALINFO first. Attach this caller to the existing
    // session tables and video memory before serving those callbacks.
    if (!ddhal && (!FindSessionName(Win98SessionArena::KindView, "NEO98 DirectDraw driver tables") ||
                   !HalCreateDriverObject()))
        return H::DriverNotHandled;
    switch (name.id)
    {
    API_CASE("GetDriverInfo") return HalGetDriverInfo(data);
    API_CASE("DestroyDriver")
        Write32(data + D::DestroyDriverResult, H::DD_OK);
        if (ddhal->objects) --ddhal->objects;
        if (!ddhal->objects)
        {
            HalReleaseAll();
            for (auto &c : ddhal->contexts) c.used = false;
            for (unsigned i = 0; i < ddhal->surface_count; ++i) gpu_backend->Forget(process_id, ddhal->surfaces[i].handle);
            ddhal->surface_count = 0;
            User::SettleScanout();
        }
        DriverTrace("DDHAL DestroyDriver", ddhal->objects);
        return H::DriverHandled;
    API_CASE("CanCreateSurface") return HalCanCreateSurface(data);
    API_CASE("Blt") return HalBlt(data);
    API_CASE("CreateSurface") return HalCreateSurface(data);
    API_CASE("SetMode") return HalSetMode(data);
    API_CASE("WaitForVerticalBlank") return HalWaitForVerticalBlank(data);
    API_CASE("SetExclusiveMode")
        // DDHAL_SETEXCLUSIVEMODEDATA::dwEnterExcl reports entry to or exit
        // from exclusive mode. Forward it to USER screen state.
        User::SetExclusiveMode(Read32(data + D::ExclusiveEnter) != 0);
        DriverTrace("DDHAL SetExclusiveMode", Read32(data + D::ExclusiveEnter));
        Write32(data + D::ExclusiveResult, H::DD_OK);
        return H::DriverHandled;
    API_CASE("FlipToGDISurface")
        // DDRAW 2e949 requests leaving GDI before the first surface Flip.
        // HANDLED exits Flip without rotating the surface memory.
        // Let DDRAW continue to HalFlip and its normal pointer rotation.
        if (!Read32(data + D::ToGdi)) return H::DriverNotHandled;
        User::ResetScanout();
        User::ScreenChanged();
        Write32(data + D::ToGdiResult, H::DD_OK);
        return H::DriverHandled;
    API_CASE("DestroySurface")
    {
        // A surface of the DirectX 8 runtime comes with a fresh LCL; its
        // record is the copy that carries its handle.
        const unsigned lcl = Read32(data + D::Surface);
        HalSurface *copy = nullptr;
        if (!HalSurfaceByLcl(lcl) && HalTransientSurface(lcl))
        {
            const unsigned more = Read32(lcl + H::SurfaceLocal::More);
            copy = more ? HalShadowByHandle(Read32(more + H::SurfaceMore::Handle)) : nullptr;
        }
        HalForgetSurface(copy ? copy->lcl : lcl);
        Write32(data + D::SurfaceResult, H::DD_OK);
        return H::DriverHandled;
    }
    API_CASE("Flip") return HalFlip(data);
    API_CASE("Lock") return HalLock(data);
    API_CASE("Unlock") return HalUnlock(data);
    API_CASE("GetBltStatus")
    API_CASE("GetFlipStatus")
        Write32(data + D::StatusResult, H::DD_OK);
        return H::DriverHandled;
    API_CASE("AddAttachedSurface")
        Write32(data + D::AttachedResult, H::DD_OK);
        return H::DriverHandled;
    API_CASE("SetClipList")
        Write32(data + D::ClipListResult, H::DD_OK);
        return H::DriverHandled;
    API_CASE("CreateSurfaceEx")
    {
        // A system memory surface has no DestroySurface; DDRAW says it is
        // going with a CreateSurfaceEx whose fpVidMem is zero (DDK). The
        // record goes then, before its LCL memory serves another surface: a
        // Blitz3D game destroyed a vertex buffer this way, and its LCL came
        // back sixteen bytes lower as a texture that drew white.
        const unsigned lcl = Read32(data + D::CreateExSurface);
        const unsigned gbl = lcl ? Read32(lcl + H::SurfaceLocal::Global) : 0;
        // A handle names one surface: a record that still carries the handle
        // CreateSurfaceEx now gives another surface is left over from a
        // surface that went without telling the driver (Kroko-Mania drops its
        // first Direct3D 8 device, and the runtime hands its handles to the
        // flip chain of the next one).
        const unsigned more = lcl ? Read32(lcl + H::SurfaceLocal::More) : 0;
        if (gbl && Read32(gbl + H::SurfaceGlobal::VidMem) && more)
            HalEvictHandle(Read32(more + H::SurfaceMore::Handle), lcl);
        if (HalTransientSurface(lcl)) HalRegisterShadow(lcl, Read32(data + D::CreateExLocal), 0);
        else if (gbl && !Read32(gbl + H::SurfaceGlobal::VidMem)) HalForgetSurface(lcl);
        else HalRegisterSurface(lcl, Read32(data + D::CreateExLocal), 0);
        Write32(data + D::CreateExResult, H::DD_OK);
        return H::DriverHandled;
    }
    API_CASE("GetDriverState")
        Write32(data + D::DriverStateResult, H::DDERR_UNSUPPORTED);
        return H::DriverNotHandled;
    API_CASE("DestroyDDLocal")
    {
        const unsigned local = Read32(data + D::DestroyLocal);
        for (unsigned i = 0; i < ddhal->surface_count;)
            if (ddhal->surfaces[i].local == local) { gpu_backend->Forget(process_id, ddhal->surfaces[i].handle); ddhal->surfaces[i] = ddhal->surfaces[--ddhal->surface_count]; }
            else ++i;
        for (auto &c : ddhal->contexts) if (c.local == local) c.used = false;
        Write32(data + D::DestroyLocalResult, H::DD_OK);
        return H::DriverHandled;
    }
    API_CASE("ContextCreate") return HalContextCreate(data);
    API_CASE("ContextDestroy")
        if (auto *c = HalContextOf(Read32(data + D::DestroyContext))) c->used = false;
        Write32(data + D::DestroyContextResult, H::DD_OK);
        return H::DriverHandled;
    API_CASE("ContextDestroyAll")
    {
        const unsigned pid = Read32(data + D::DestroyContext);
        for (auto &c : ddhal->contexts) if (c.pid == pid) c.used = false;
        Write32(data + D::DestroyContextResult, H::DD_OK);
        return H::DriverHandled;
    }
    API_CASE("TextureCreate")
    {
        // D3DHAL_TEXTURECREATEDATA: context, surface interface, handle, result.
        auto *c = HalContextOf(Read32(data));
        const unsigned object = Read32(data + 4);
        const unsigned lcl = object ? Read32(object + 4) : 0;
        if (c && lcl && !HalSurfaceByLcl(lcl)) HalRegisterSurface(lcl, c->local, 0, false);
        auto *surface = c && lcl ? HalSurfaceByLcl(lcl) : nullptr;
        if (surface)
        {
            surface->legacy_handle = true;
            surface->handle = lcl;
            ++surface->legacy_references;
        }
        Write32(data + 8, surface ? surface->handle : 0);
        Write32(data + 12, surface ? H::DD_OK : H::DDERR_INVALIDPARAMS);
        return H::DriverHandled;
    }
    API_CASE("TextureDestroy")
    {
        // D3DHAL_TEXTUREDESTROYDATA: context, texture handle, result.
        if (auto *surface = HalSurfaceByHandle(Read32(data + 4)))
            if (surface->legacy_references && !--surface->legacy_references) HalForgetSurface(surface->lcl);
        Write32(data + 8, H::DD_OK);
        return H::DriverHandled;
    }
    API_CASE("SceneCapture")
        Write32(data + D::SceneResult, H::DD_OK);
        return H::DriverHandled;
    API_CASE("Clear2") return HalClear2(data);
    API_CASE("ValidateTextureStageState")
        Write32(data + D::ValidatePasses, 1);
        Write32(data + D::ValidateResult, H::DD_OK);
        return H::DriverHandled;
    API_CASE("DrawPrimitives2") return HalDrawPrimitives2(data);
    default:
        break;
    }
    DriverTrace("DDHAL unknown callback", name.id);
    return H::DriverNotHandled;
}
} // namespace X86
