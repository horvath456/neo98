// SPDX-License-Identifier: GPL-2.0-or-later
// NEO98DX.DLL, the display half: the DirectDraw driver below the ARM64
// DDRAW.DLL (sdk/dx/ddraw).
// An ARM64 process cannot load the original DDRAW.DLL, so it gets one built
// for ARM64 with the SDK, and that one calls here the way the original calls
// the DirectDraw HAL of the display driver (src/x86_ddhal.cpp). The functions
// are flat, described in sdk/api/70-neo98dx.api; the structures are the ones
// of sdk/dx/neo98dx_abi.h, 32-bit fields only.
// What the two drivers share is the display itself: the screen surface is the
// primary surface (User::ScreenBits), a blit with a clipper writes into the
// visible region of the window (WindowVisibleRegion, the routine behind
// DDRAW16 index 39), a flip moves the scanout (User::SetScanout), a mode
// change goes through ApplyDisplayMode, a Lock of the primary surface takes
// the lock of the screen, and whole frames are paced by PaceFrame. What they
// do not share is the surface memory: the original DDRAW cuts its surfaces
// out of the video memory heap of the session, this driver gives every
// surface a block of its own in the address space of the process
// (AllocateDibLease), whose bytes the host sees at a fixed pointer. Lock
// hands that address out, GDI draws on the same bytes (Gdi::CreateSurfaceDC),
// and the rasterizer takes them as a Gpu::Surface (DxSurfaceView), which is
// what Direct3D for ARM64 builds on.
// The blitter is sdk/dx/ddblit.h, the same source the DLL runs for surfaces
// in system memory.
#include "../sdk/dx/neo98dx_abi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../sdk/dx/ddblit.h"
#include "direct3d_state.h"
#include "gdi.h"
#include "gpu_backend.h"
#include "user.h"
#include "win98_ddraw16_surface.h"
#include "win98_ddhal.h"
#include "win98_display_modes.h"
#include "x86_app.h"
namespace X86
{
namespace
{
// HRESULTs of DirectDraw (0x8876 is its facility).
enum : unsigned
{
    DdOk = 0,
    DdGeneric = 0x80004005u,
    DdInvalidParams = 0x80070057u,
    DdOutOfMemory = 0x8007000eu,
    DdUnsupported = 0x80004001u,
    DdInvalidMode = 0x88760078u,
    DdInvalidObject = 0x88760082u,
    DdInvalidPixelFormat = 0x88760091u,
    DdInvalidRect = 0x88760096u,
    DdUnsupportedMode = 0x887600deu,
    DdOutOfVideoMemory = 0x8876017cu,
    DdSurfaceBusy = 0x887601aeu,
    DdSurfaceLost = 0x887601c2u,
    DdExclusiveModeAlreadySet = 0x88760245u,
    DdNotLocked = 0x88760248u,
    DdCantCreateDc = 0x88760249u,
    DdNoDc = 0x8876024au,
};
// DDSCAPS_ and DDPF_ values the driver looks at.
enum : unsigned
{
    CapsBackBuffer = 0x4, CapsFlip = 0x10, CapsFrontBuffer = 0x20, CapsOffscreenPlain = 0x40, CapsPrimary = 0x200,
    CapsTexture = 0x1000, Caps3DDevice = 0x2000, CapsVideoMemory = 0x4000, CapsZBuffer = 0x20000, CapsMipMap = 0x400000,
    Caps2TextureManage = 0x10, Caps2D3DTextureManage = 0x20,
    PfAlphaPixels = 0x1, PfFourCC = 0x4, PfIndexed8 = 0x20, PfRgb = 0x40, PfZBuffer = 0x400, PfStencil = 0x4000,
    PfBumpLuminance = 0x40000, PfBumpDuDv = 0x80000,
};
// What a process may hold in surfaces of the driver. The number is the one
// the x86 HAL names for its heap; behind it is memory of the Mac.
constexpr unsigned VideoMemoryBytes = 64u << 20;

void DisplayFormat(int depth, NEO98DX_FORMAT &f)
{
    f = {PfRgb, 0, unsigned(depth), 0, 0, 0, 0};
    if (depth == 8) f.flags |= PfIndexed8;
    else if (depth == 16) { f.r = 0x7c00; f.g = 0x03e0; f.b = 0x001f; }
    else { f.r = 0xff0000; f.g = 0xff00; f.b = 0xff; }
}
// The formats the driver keeps surfaces in: the display's in any depth, and
// the texture and Z formats the rasterizer takes, the lists the x86 HAL
// offers (direct3d_state.h). A FourCC format only with a rasterizer that
// samples it.
bool DriverFormat(const NEO98DX_FORMAT &f, bool compressed)
{
    namespace D = Direct3DState;
    if (f.flags & PfFourCC) return compressed && D::BlockBytes(f.fourcc) != 0;
    const unsigned kind = PfRgb | PfIndexed8 | PfZBuffer | PfAlphaPixels | PfStencil | PfBumpDuDv | PfBumpLuminance;
    auto same = [&](const D::Format &t) {
        if ((f.flags & kind) != (t.flags & kind) || f.bits != t.bits) return false;
        if (t.flags & PfIndexed8) return true;
        if (t.flags & PfZBuffer) return f.g == t.g && f.r == t.r;   // dwZBitMask, dwStencilBitDepth
        return f.r == t.r && f.g == t.g && f.b == t.b && ((t.flags & PfAlphaPixels) ? f.a == t.a : true);
    };
    for (const auto &t : D::texture_formats)
        if (!t.fourcc && same(t)) return true;
    for (const auto &z : D::z_formats)
        if (same(z)) return true;
    // The display format of the other depths: 24 bits, and 8 bits as it is.
    return (f.flags & kind) == PfRgb && f.bits == 24 && f.r == 0xff0000 && f.g == 0xff00 && f.b == 0xff;
}
unsigned DriverPitch(unsigned width, unsigned bits) { return (width * ((bits + 7) / 8) + 15) & ~15u; }
} // namespace

// One surface of the driver.
struct Application::DxSurface
{
    bool used = false, primary = false;
    unsigned handle = 0, caps = 0, caps2 = 0;
    int width = 0, height = 0, pitch = 0;
    NEO98DX_FORMAT format{};
    // The bytes now. A flip rotates them along its chain, so the primary
    // surface may hold a block of the driver and a back buffer the screen.
    unsigned char *host = nullptr;
    unsigned guest = 0, bytes = 0;
    bool screen = false;          // the bytes are the screen surface
    Gdi::DibLease lease{};        // the block this surface brought along, wherever a flip took it
    unsigned mode_serial = 0;
    // A managed texture (DDSCAPS2_TEXTUREMANAGE) outlives a change of the
    // display mode: Direct3D keeps its picture, it is never lost.
    bool managed = false;
    bool Lost() const { return !host || (!managed && mode_serial != User::ScreenModeSerial()); }
    // The bytes the pixels take: rows of pixels, or rows of 4x4 blocks.
    unsigned PixelBytes() const
    {
        return format.fourcc ? Direct3DState::LinearSize(unsigned(width), unsigned(height), format.fourcc) : unsigned(pitch) * unsigned(height);
    }
    unsigned generation = 1;
    unsigned locks = 0;
    bool screen_locked = false, frame_lock = false;
    bool color_key = false;
    unsigned key_low = 0, key_high = 0;
    unsigned *palette = nullptr;  // 256 entries 0xAARRGGBB, or null
    bool palette_alpha = false;
    unsigned z = 0;               // the attached Z buffer
    unsigned mip = 0;             // the next level of the mip chain
    unsigned cube[6] = {}, cube_root = 0;
};

struct Application::DxDriver
{
    DxSurface *surfaces = nullptr;
    unsigned count = 0, capacity = 0, next_handle = 0x1000;
    unsigned video_bytes = 0;
    bool exclusive = false;
    unsigned exclusive_window = 0;
    bool mode_set = false;        // this process changed the display mode
    struct OpenDc
    {
        unsigned dc = 0, surface = 0;
        const unsigned char *bits = nullptr;
        bool screen_locked = false;
    } dcs[32];
    ~DxDriver()
    {
        for (unsigned i = 0; i < count; ++i) free(surfaces[i].palette);
        free(surfaces);
    }
};

const char *Application::DxDriverModule() { return "NEO98DX.DLL"; }

Application::DxSurface *Application::DxSurfaceOf(unsigned handle)
{
    if (!dx_driver || !handle) return nullptr;
    for (unsigned i = 0; i < dx_driver->count; ++i)
        if (dx_driver->surfaces[i].used && dx_driver->surfaces[i].handle == handle) return &dx_driver->surfaces[i];
    return nullptr;
}

// The CPU is about to read (and with will_write change) the bytes of a
// surface: the rasterizer's picture goes into them first.
void Application::DxSync(const unsigned char *bits, bool will_write)
{
    if (!gpu_backend || !bits) return;
    Gpu::Surface view;
    view.bits = const_cast<unsigned char *>(bits);
    gpu_backend->Sync(view, will_write);
}

// A surface of the driver as the rasterizer takes it, the counterpart of
// HalSurfaceView. Direct3D for ARM64 names its render target, its Z buffer and
// its textures by these handles.
bool Application::DxSurfaceView(unsigned handle, Gpu::Surface &view)
{
    view = {};
    auto *s = DxSurfaceOf(handle);
    if (!s || s->Lost()) return false;
    view.bits = s->host;
    view.width = s->width;
    view.height = s->height;
    view.pitch = s->pitch;
    view.bit_count = s->format.bits;
    view.r_mask = s->format.r;
    view.g_mask = s->format.g;
    view.b_mask = s->format.b;
    view.a_mask = (s->format.flags & PfAlphaPixels) ? s->format.a : 0;
    view.indexed = (s->format.flags & PfIndexed8) != 0;
    view.fourcc = (s->format.flags & PfFourCC) ? s->format.fourcc : 0;
    view.bump = (s->format.flags & PfBumpDuDv) != 0;
    view.bump_luminance = (s->format.flags & PfBumpLuminance) != 0;
    if ((s->caps & CapsZBuffer) || (s->format.flags & PfZBuffer))
    {
        view.depth = true;
        view.z_bits = s->format.bits;
        view.stencil_bits = (s->format.flags & PfStencil) ? s->format.r : 0;
    }
    view.color_key = s->color_key;
    view.key_low = s->key_low;
    view.key_high = s->key_high;
    view.handle = s->handle;
    view.owner = process_id;
    view.generation = s->generation;
    if (view.indexed)
    {
        view.palette = s->palette ? s->palette : User::ScreenColors();
        view.palette_alpha = s->palette && s->palette_alpha;
    }
    unsigned next = s->mip;
    for (unsigned n = 0; n < Gpu::Surface::MaxLevels && next; ++n)
    {
        auto *level = DxSurfaceOf(next);
        if (!level || !level->host) break;
        view.levels[n] = {level->host, level->width, level->height, level->pitch, 0};
        view.level_count = n + 1;
        view.generation += level->generation;
        next = level->mip;
    }
    return true;
}

// The same six views as HalCubeFaces, kept by the D3D context while the
// backend uses them. A complete cube is required by the shared rasterizer.
void Application::DxCubeFaces(unsigned handle, Gpu::Surface *faces, Gpu::Surface &view)
{
    auto *surface = DxSurfaceOf(handle);
    if (!surface || !(surface->caps2 & Win98DDHal::DDSCAPS2_CUBEMAP)) return;
    auto *root = surface->cube_root ? DxSurfaceOf(surface->cube_root) : surface;
    if (!root) return;
    unsigned generation = 0;
    for (unsigned i = 0; i < 6; ++i)
    {
        const unsigned bit = Win98DDHal::DDSCAPS2_CUBEMAP_POSITIVEX << i;
        const unsigned face = root->caps2 & bit ? root->handle : root->cube[i];
        if (!face || !DxSurfaceView(face, faces[i])) return;
        generation += faces[i].generation;
    }
    view.faces = faces;
    view.generation = generation;
}

// A plane of a call as the blitter takes it: a surface of the driver, or
// bytes of the program, which the host reaches in place. `surface` is the
// record of a driver surface.
bool Application::DxPlane(const void *abi_plane, bool write, void *blit_plane, DxSurface **surface, unsigned &result)
{
    const auto &p = *static_cast<const NEO98DX_PLANE *>(abi_plane);
    auto &out = *static_cast<DDBLIT_PLANE *>(blit_plane);
    *surface = nullptr;
    result = DdOk;
    if (p.handle)
    {
        auto *s = DxSurfaceOf(p.handle);
        if (!s) { result = DdInvalidObject; return false; }
        if (s->Lost()) { result = DdSurfaceLost; return false; }
        out = {s->host, s->pitch, s->width, s->height, int((s->format.bits + 7) / 8)};
        *surface = s;
        return true;
    }
    const int bytes = int((p.format.bits + 7) / 8);
    if (!p.bits || !p.width || !p.height || p.pitch <= 0 || bytes < 1 || bytes > 4 || p.width > 16384 || p.height > 16384 ||
        unsigned(p.pitch) < p.width * unsigned(bytes))
    {
        result = DdInvalidParams;
        return false;
    }
    const unsigned span = unsigned(p.pitch) * (p.height - 1) + p.width * unsigned(bytes);
    if (!virtual_memory.Access(p.bits, span, write)) { result = DdInvalidParams; return false; }
    auto *host = static_cast<unsigned char *>(cpu->HostPointer(p.bits, span));
    if (!host) { result = DdUnsupported; return false; }
    out = {host, p.pitch, int(p.width), int(p.height), bytes};
    return true;
}

void Application::DxReleaseSurfaceMemory(DxSurface &s)
{
    if (!dx_driver) return;
    if (s.lease.host)
    {
        if (gpu_backend) gpu_backend->Release(s.lease.host, s.lease.bytes);
        if (User::ScanoutBits() == s.lease.host) User::SettleScanout();
        if (dx_driver->video_bytes >= s.lease.bytes) dx_driver->video_bytes -= s.lease.bytes;
        if (s.lease.release) s.lease.release(s.lease.context, s.lease);
        s.lease = {};
    }
}

// The bytes of a surface the driver keeps: a block in the address space of
// the process the host sees in place.
bool Application::DxAllocateSurfaceMemory(DxSurface &s)
{
    const unsigned bytes = (s.PixelBytes() + 4095) & ~4095u;
    if (!bytes || dx_driver->video_bytes + bytes > VideoMemoryBytes) return false;
    Gdi::DibLease lease{};
    if (!AllocateDibLease(bytes, lease) || !lease.host) return false;
    memset(lease.host, 0, bytes);
    s.lease = lease;
    s.host = lease.host;
    s.guest = lease.guest;
    s.bytes = bytes;
    s.screen = false;
    dx_driver->video_bytes += bytes;
    return true;
}

// The primary surface is the screen: its bytes, its geometry and its format.
bool Application::DxBindPrimary(DxSurface &s)
{
    InstallScreenLease();
    if (!User::ScreenGuestAddress())
    {
        // The screen has a guest address only once a lease is installed.
        int width = 0, height = 0, depth = 0;
        User::ScreenMode(width, height, depth);
        User::SetScreenMode(width, height, depth);
        if (!User::ScreenGuestAddress()) return false;
    }
    int width = 0, height = 0, depth = 0;
    User::ScreenMode(width, height, depth);
    s.width = width;
    s.height = height;
    s.pitch = User::ScreenStride();
    DisplayFormat(depth, s.format);
    s.host = const_cast<unsigned char *>(User::ScreenBits());
    s.guest = User::ScreenGuestAddress();
    s.bytes = User::ScreenBytes();
    s.screen = true;
    s.mode_serial = User::ScreenModeSerial();
    return s.host != nullptr;
}

unsigned Application::DxCreateSurface(unsigned address, unsigned bytes)
{
    NEO98DX_CREATE c{};
    if (bytes != sizeof c || !virtual_memory.Access(address, sizeof c, true) || !Read(address, &c, sizeof c) || c.size != sizeof c)
        return DdInvalidParams;
    if (!dx_driver) dx_driver = new DxDriver;
    auto &d = *dx_driver;
    DxSurface *slot = nullptr;
    for (unsigned i = 0; i < d.count && !slot; ++i)
        if (!d.surfaces[i].used) slot = &d.surfaces[i];
    if (!slot)
    {
        if (d.count == d.capacity)
        {
            const unsigned grown = d.capacity ? d.capacity * 2 : 64;
            auto *more = static_cast<DxSurface *>(realloc(static_cast<void *>(d.surfaces), grown * sizeof(DxSurface)));
            if (!more) return DdOutOfMemory;
            d.surfaces = more;
            d.capacity = grown;
        }
        slot = &d.surfaces[d.count++];
    }
    *slot = DxSurface{};
    auto &s = *slot;
    s.caps = c.caps;
    s.caps2 = c.caps2;
    if (c.caps & CapsPrimary)
    {
        s.primary = true;
        if (!DxBindPrimary(s)) return DdGeneric;
    }
    else
    {
        int width = 0, height = 0, depth = 0;
        User::ScreenMode(width, height, depth);
        if (!c.format.bits && !(c.format.flags & PfFourCC)) DisplayFormat(depth, c.format);
        if (!DriverFormat(c.format, gpu_backend && gpu_backend->CompressedTextures())) return DdInvalidPixelFormat;
        if (!c.width || !c.height || c.width > 8192 || c.height > 8192) return DdInvalidParams;
        s.width = int(c.width);
        s.height = int(c.height);
        s.format = c.format;
        if (c.format.flags & PfFourCC)
        {
            s.format.bits = 0;
            s.pitch = int(Direct3DState::BlockPitch(c.width, c.format.fourcc));
        }
        else s.pitch = int(DriverPitch(c.width, c.format.bits));
        s.managed = (c.caps & CapsTexture) && (c.caps2 & (Caps2TextureManage | Caps2D3DTextureManage));
        s.mode_serial = User::ScreenModeSerial();
        if (!DxAllocateSurfaceMemory(s)) return DdOutOfVideoMemory;
    }
    s.handle = d.next_handle++;
    s.used = true;
    c.handle = s.handle;
    c.width = unsigned(s.width);
    c.height = unsigned(s.height);
    c.format = s.format;
    c.bits = s.guest;
    c.pitch = s.pitch;
    c.modeSerial = s.mode_serial;
    if (!Store(address, &c, sizeof c))
    {
        DxDestroySurface(s.handle);
        return DdInvalidParams;
    }
    if (log && trace_calls)
    {
        char text[200];
        snprintf(text, sizeof text, "DXDRV CreateSurface handle=%x caps=%08x %dx%d bits=%u pitch=%d at=%08x", s.handle, s.caps, s.width,
                 s.height, s.format.bits, s.pitch, s.guest);
        log(text);
    }
    return DdOk;
}

unsigned Application::DxDestroySurface(unsigned handle)
{
    auto *s = DxSurfaceOf(handle);
    if (!s) return DdInvalidObject;
    auto &d = *dx_driver;
    for (auto &open : d.dcs)
        if (open.dc && open.surface == handle)
        {
            if (open.screen_locked) User::UnlockScreen(process_id, ThreadId());
            Gdi::DeleteDC(open.dc);
            open = {};
        }
    if (s->screen_locked) User::UnlockScreen(process_id, ThreadId());
    // A surface that leaves while its bytes are on the scanout takes the
    // picture back into the screen first.
    if (s->host && User::ScanoutBits() == s->host && s->host != User::ScreenBits()) User::SettleScanout();
    if (gpu_backend) gpu_backend->Forget(process_id, handle);
    // A flip may have carried the block of this surface to another one of its
    // chain. That one keeps its picture: it moves into the bytes this surface
    // holds now, and the block goes back with its owner.
    if (s->lease.host && s->host != s->lease.host)
        for (unsigned i = 0; i < d.count; ++i)
        {
            auto &t = d.surfaces[i];
            if (!t.used || &t == s || t.host != s->lease.host) continue;
            const bool shown = User::ScanoutBits() == t.host;
            if (s->host && t.mode_serial == User::ScreenModeSerial())
            {
                const int row = t.pitch < s->pitch ? t.pitch : s->pitch;
                for (int y = 0; y < t.height; ++y) memcpy(s->host + (long)y * s->pitch, t.host + (long)y * t.pitch, size_t(row));
            }
            t.host = s->host; t.guest = s->guest; t.bytes = s->bytes; t.pitch = s->pitch; t.screen = s->screen;
            ++t.generation;
            if (shown)
            {
                if (t.host == User::ScreenBits()) User::ResetScanout();
                else User::SetScanout(t.host, t.pitch);
                User::ScreenChanged();
            }
        }
    DxReleaseSurfaceMemory(*s);
    for (unsigned i = 0; i < d.count; ++i)
    {
        if (d.surfaces[i].z == handle) d.surfaces[i].z = 0;
        if (d.surfaces[i].mip == handle) d.surfaces[i].mip = 0;
        if (d.surfaces[i].cube_root == handle) d.surfaces[i].cube_root = 0;
        for (auto &face : d.surfaces[i].cube) if (face == handle) face = 0;
    }
    free(s->palette);
    *s = DxSurface{};
    return DdOk;
}

unsigned Application::DxAttach(unsigned handle, unsigned attached, bool attach)
{
    auto *s = DxSurfaceOf(handle), *other = DxSurfaceOf(attached);
    if (!s || !other) return DdInvalidObject;
    if (other->caps & CapsZBuffer) s->z = attach ? attached : 0;
    else if ((s->caps2 & Win98DDHal::DDSCAPS2_CUBEMAP) &&
             (other->caps2 & Win98DDHal::DDSCAPS2_CUBEMAP) &&
             ((s->caps2 ^ other->caps2) & Win98DDHal::DDSCAPS2_CUBEMAP_ALLFACES))
    {
        for (unsigned i = 0; i < 6; ++i)
            if (other->caps2 & (Win98DDHal::DDSCAPS2_CUBEMAP_POSITIVEX << i)) s->cube[i] = attach ? attached : 0;
        other->cube_root = attach ? (s->cube_root ? s->cube_root : s->handle) : 0;
    }
    else if (other->caps & CapsMipMap) s->mip = attach ? attached : 0;
    ++s->generation;
    return DdOk;
}

unsigned Application::DxLock(unsigned address, unsigned bytes)
{
    NEO98DX_LOCK l{};
    if (bytes != sizeof l || !virtual_memory.Access(address, sizeof l, true) || !Read(address, &l, sizeof l) || l.size != sizeof l)
        return DdInvalidParams;
    auto *s = DxSurfaceOf(l.handle);
    if (!s) return DdInvalidObject;
    if (s->Lost()) return DdSurfaceLost;
    // A Lock of the bytes the screen is holds the screen's pixels until
    // Unlock, like the Lock callback of the HAL (HalLock): other
    // threads' drawing waits, and a new Lock first lets the threads draw that
    // the last Unlock handed the screen to.
    const bool screen = s->host == User::ScreenBits() || s->host == User::ScanoutBits();
    if (screen && !s->screen_locked)
    {
        const unsigned thread = ThreadId();
        if (!User::ScreenHeldBy(thread) && User::ScreenGrantsPending(thread))
        {
            threads[current_thread].block = 18;
            waiting = true;
            return 0;
        }
        User::LockScreen(process_id, thread);
        s->screen_locked = true;
    }
    ++s->generation;
    ++s->locks;
    // DDLOCK_READONLY (10h) promises no write.
    DxSync(s->host, !(l.flags & 0x10));
    // A frame is a lock that covers at least half of the surface the monitor
    // reads.
    s->frame_lock = false;
    if (s->host == User::ScanoutBits())
    {
        long long covered = (long long)s->width * s->height;
        if (l.hasRect) covered = (long long)(l.rect[2] - l.rect[0]) * (l.rect[3] - l.rect[1]);
        s->frame_lock = covered * 2 >= (long long)s->width * s->height;
    }
    l.bits = s->guest;
    l.pitch = s->pitch;
    if (!Store(address, &l, sizeof l)) return DdInvalidParams;
    return DdOk;
}

unsigned Application::DxUnlock(unsigned handle)
{
    auto *s = DxSurfaceOf(handle);
    if (!s) return DdInvalidObject;
    if (!s->locks) return DdNotLocked;
    --s->locks;
    ++s->generation;
    if (s->locks) return DdOk;
    const bool visible = s->host && s->host == User::ScanoutBits();
    if (s->screen_locked)
    {
        User::UnlockScreen(process_id, ThreadId());
        s->screen_locked = false;
    }
    if (visible) User::ScreenChanged();
    const bool frame = s->frame_lock;
    s->frame_lock = false;
    if (visible && frame)
    {
        ++hal_presents;
        PaceFrame();
    }
    return DdOk;
}

// The visible region of the client area of a window as rects on the screen;
// the region is the one DDRAW16 index 39 answers the original DDRAW with.
// Answers the count; `rects` is the caller's to free.
unsigned Application::DxWindowClip(unsigned window, void **rects)
{
    *rects = nullptr;
    const unsigned region = WindowVisibleRegion(window);
    if (!region) return 0;
    unsigned count = 0;
    const unsigned size_of[] = {region, 0, 0};
    const unsigned need = GdiApi("GetRegionData", size_of);
    const unsigned scratch = need >= 32 ? Allocate(need) : 0;
    if (scratch)
    {
        const unsigned read[] = {region, need, scratch};
        if (GdiApi("GetRegionData", read))
        {
            count = Read32(scratch + 8);   // RGNDATAHEADER.nCount
            if (count > (need - 32) / 16) count = (need - 32) / 16;
            auto *list = static_cast<DDBLIT_RECT *>(malloc((count ? count : 1) * sizeof(DDBLIT_RECT)));
            if (list)
                for (unsigned i = 0; i < count; ++i)
                {
                    int r[4] = {};
                    Read(scratch + 32 + i * 16, r, sizeof r);
                    list[i] = {r[0], r[1], r[2], r[3]};
                }
            else count = 0;
            *rects = list;
        }
        Free(scratch);
    }
    const unsigned drop[] = {region};
    Api("DeleteObject", drop);
    return count;
}

unsigned Application::DxBlt(unsigned address, unsigned bytes)
{
    NEO98DX_BLT b{};
    if (bytes != sizeof b || !Read(address, &b, sizeof b) || b.size != sizeof b) return DdInvalidParams;
    DDBLIT_JOB job{};
    DxSurface *dst = nullptr, *src = nullptr;
    unsigned result = DdOk;
    if (!DxPlane(&b.dst, true, &job.dst, &dst, result)) return result;
    const bool fill = (b.flags & NEO98DX_BLT_FILL) != 0;
    if (!fill && !DxPlane(&b.src, false, &job.src, &src, result)) return result;
    if (!fill && job.src.bytes != job.dst.bytes) return DdUnsupported;
    if ((dst && dst->format.fourcc) || (src && src->format.fourcc))
    {
        // Compressed surfaces: whole 4x4 blocks between two surfaces of the
        // same FourCC, unscaled, as the x86 HAL copies them (HalCopyBlocks).
        if (fill || !dst || !src || dst->format.fourcc != src->format.fourcc || b.clipped ||
            (b.flags & (NEO98DX_BLT_KEYSRC | NEO98DX_BLT_KEYDEST | NEO98DX_BLT_MIRRORX | NEO98DX_BLT_MIRRORY)))
            return DdUnsupported;
        const int w = b.srcRect[2] - b.srcRect[0], h = b.srcRect[3] - b.srcRect[1];
        if (w <= 0 || h <= 0 || b.srcRect[0] < 0 || b.srcRect[1] < 0 || b.srcRect[2] > src->width || b.srcRect[3] > src->height ||
            b.dstRect[0] < 0 || b.dstRect[1] < 0 || b.dstRect[2] - b.dstRect[0] != w || b.dstRect[3] - b.dstRect[1] != h ||
            b.dstRect[2] > dst->width || b.dstRect[3] > dst->height)
            return DdInvalidRect;
        if ((b.srcRect[0] | b.srcRect[1] | b.dstRect[0] | b.dstRect[1]) & 3) return DdInvalidRect;
        const unsigned block = Direct3DState::BlockBytes(dst->format.fourcc);
        DxSync(src->host, false);
        DxSync(dst->host, true);
        const unsigned columns = unsigned(w + 3) / 4, rows = unsigned(h + 3) / 4;
        for (unsigned row = 0; row < rows; ++row)
            memmove(dst->host + (unsigned(b.dstRect[1]) / 4 + row) * unsigned(dst->pitch) + unsigned(b.dstRect[0]) / 4 * block,
                    src->host + (unsigned(b.srcRect[1]) / 4 + row) * unsigned(src->pitch) + unsigned(b.srcRect[0]) / 4 * block,
                    columns * block);
        ++dst->generation;
        return DdOk;
    }
    job.drect = {b.dstRect[0], b.dstRect[1], b.dstRect[2], b.dstRect[3]};
    job.srect = {b.srcRect[0], b.srcRect[1], b.srcRect[2], b.srcRect[3]};
    job.flags = b.flags;
    job.fill = b.fill;
    job.src_key_low = b.srcKeyLow;
    job.src_key_high = b.srcKeyHigh;
    job.dst_key_low = b.dstKeyLow;
    job.dst_key_high = b.dstKeyHigh;
    // The clip list: the visible region of a window, worked out here so that
    // nothing runs between the region and the pixels, or the rects of the
    // caller. A clipped blit without any rect draws nothing.
    DDBLIT_RECT *clips = nullptr;
    struct Drop { DDBLIT_RECT *&p; ~Drop() { free(p); } } drop{clips};
    if (b.clipped)
    {
        unsigned count = 0;
        if (b.clipWindow)
        {
            void *list = nullptr;
            count = DxWindowClip(b.clipWindow, &list);
            clips = static_cast<DDBLIT_RECT *>(list);
        }
        else if (b.clipCount)
        {
            if (b.clipCount > 4096 || !virtual_memory.Access(b.clipRects, b.clipCount * 16, false)) return DdInvalidParams;
            clips = static_cast<DDBLIT_RECT *>(malloc(b.clipCount * sizeof(DDBLIT_RECT)));
            if (!clips) return DdOutOfMemory;
            Read(b.clipRects, clips, b.clipCount * 16);
            count = b.clipCount;
        }
        if (!count) return DdOk;
        job.clips = clips;
        job.clip_count = count;
    }
    const bool to_screen = dst && dst->host && (dst->host == User::ScreenBits() || dst->host == User::ScanoutBits());
    const bool from_screen = src && src->host && (src->host == User::ScreenBits() || src->host == User::ScanoutBits());
    // Another thread holds the pixels of the screen with a Lock: this blit
    // waits for its Unlock, as a drawing call of GDI does.
    if ((to_screen || from_screen) && User::ScreenLockedElsewhere(process_id, ThreadId())) WaitScreenFree();
    if (src) DxSync(src->host, false);
    if (dst) DxSync(dst->host, true);
    DDBLIT_RECT touched{};
    const int status = DdBlit(&job, &touched);
    if (status == DDBLIT_MEMORY) return DdOutOfMemory;
    if (status != DDBLIT_OK) return DdInvalidParams;
    if (dst)
    {
        ++dst->generation;
        // The rasterizer keeps its own depth for a Z buffer.
        if ((b.flags & NEO98DX_BLT_DEPTHFILL) && gpu_backend)
        {
            Gpu::Surface view;
            if (DxSurfaceView(dst->handle, view) && view.depth)
            {
                const unsigned zmask = dst->format.g ? dst->format.g : 0xffffffffu;
                const int r[4] = {touched.left, touched.top, touched.right, touched.bottom};
                gpu_backend->FillDepth(view, r, float(double(b.fill & zmask) / double(zmask)));
            }
        }
    }
    if (dst && dst->host && dst->host == User::ScanoutBits())
    {
        ++hal_presents;
        User::ScreenChanged();
        // A whole picture onto the visible surface waits for the rest of the
        // refresh period, like a flip.
        const long long area = (long long)(touched.right - touched.left) * (touched.bottom - touched.top);
        if (!(b.flags & NEO98DX_BLT_NOWAIT) && area * 2 >= (long long)dst->width * dst->height) PaceFrame();
    }
    if (log && trace_calls)
    {
        char text[240];
        snprintf(text, sizeof text, "DXDRV Blt flags=%x dst=%x %d,%d-%d,%d src=%x %d,%d-%d,%d clips=%u touched=%d,%d-%d,%d", b.flags,
                 b.dst.handle, job.drect.left, job.drect.top, job.drect.right, job.drect.bottom, b.src.handle, job.srect.left,
                 job.srect.top, job.srect.right, job.srect.bottom, job.clip_count, touched.left, touched.top, touched.right,
                 touched.bottom);
        log(text);
    }
    return DdOk;
}

// Flip. `list` names the chain, the visible surface first; the bytes move one
// place towards the front and the old front goes to the end, the way DDRAW
// rotates fpVidMem, and the scanout goes to the bytes of the new front.
unsigned Application::DxFlip(unsigned list, unsigned count, unsigned flags)
{
    if (count < 2 || count > 16 || !virtual_memory.Access(list, count * 4, false)) return DdInvalidParams;
    DxSurface *chain[16];
    for (unsigned i = 0; i < count; ++i)
    {
        chain[i] = DxSurfaceOf(Read32(list + i * 4));
        if (!chain[i]) return DdInvalidObject;
        if (chain[i]->Lost()) return DdSurfaceLost;
        if (chain[i]->locks) return DdSurfaceBusy;
        if (chain[i]->width != chain[0]->width || chain[i]->height != chain[0]->height ||
            chain[i]->format.bits != chain[0]->format.bits)
            return DdInvalidParams;
    }
    struct Memory { unsigned char *host; unsigned guest, bytes; int pitch; bool screen; };
    auto take = [](DxSurface &s) { return Memory{s.host, s.guest, s.bytes, s.pitch, s.screen}; };
    auto give = [](DxSurface &s, const Memory &m) {
        s.host = m.host; s.guest = m.guest; s.bytes = m.bytes; s.pitch = m.pitch; s.screen = m.screen;
        ++s.generation;
    };
    auto turn = [&]() {
        const Memory front = take(*chain[0]);
        for (unsigned i = 0; i + 1 < count; ++i) give(*chain[i], take(*chain[i + 1]));
        give(*chain[count - 1], front);
    };
    if (flags & NEO98DX_FLIP_TOGDI)
    {
        // FlipToGDISurface: the surface whose bytes are the screen comes to
        // the front, so that what GDI draws is what the monitor shows.
        unsigned turns = 0;
        while (turns < count && !chain[0]->screen) { turn(); ++turns; }
        if (!turns) return DdOk;
    }
    else turn();
    auto &shown = *chain[0];
    // Only the chain of the primary surface is on the monitor.
    if (!shown.primary) return DdOk;
    // The picture the GPU drew goes into the bytes the monitor now reads.
    DxSync(shown.host, false);
    if (shown.host == User::ScreenBits()) User::ResetScanout();
    else User::SetScanout(shown.host, shown.pitch);
    User::ScreenChanged();
    ++hal_presents;
    // DDFLIP_NOVSYNC (8) asks for the flip at once.
    if (!(flags & (0x8 | NEO98DX_FLIP_TOGDI))) PaceFrame();
    return DdOk;
}

unsigned Application::DxIsLost(unsigned handle)
{
    auto *s = DxSurfaceOf(handle);
    if (!s) return DdInvalidObject;
    return s->Lost() ? DdSurfaceLost : DdOk;
}

// Restore: the primary surface is the screen of the mode that stands now; any
// other surface gets new, empty bytes. Whether the mode is the right one is
// the DLL's question (DDERR_WRONGMODE).
unsigned Application::DxRestore(unsigned handle, unsigned address, unsigned bytes)
{
    auto *s = DxSurfaceOf(handle);
    if (!s) return DdInvalidObject;
    NEO98DX_CREATE c{};
    if (bytes != sizeof c || !virtual_memory.Access(address, sizeof c, true)) return DdInvalidParams;
    if (s->Lost())
    {
        if (s->locks) return DdSurfaceBusy;
        if (gpu_backend) gpu_backend->Forget(process_id, handle);
        DxReleaseSurfaceMemory(*s);
        if (s->primary)
        {
            if (!DxBindPrimary(*s)) return DdGeneric;
        }
        else
        {
            s->mode_serial = User::ScreenModeSerial();
            if (!DxAllocateSurfaceMemory(*s)) { s->host = nullptr; return DdOutOfVideoMemory; }
        }
        ++s->generation;
    }
    c.size = sizeof c;
    c.caps = s->caps;
    c.caps2 = s->caps2;
    c.handle = s->handle;
    c.width = unsigned(s->width);
    c.height = unsigned(s->height);
    c.format = s->format;
    c.bits = s->guest;
    c.pitch = s->pitch;
    c.modeSerial = s->mode_serial;
    return Store(address, &c, sizeof c) ? DdOk : DdInvalidParams;
}

// The colours of an eight bit surface: `colors` are 0x00RRGGBB with the
// peFlags of the entry on top. The primary surface programs the colour table
// of the display, as the palette of the primary does on the x86 path through
// DDRAW16 index 37; any surface keeps them for the rasterizer.
unsigned Application::DxSetPalette(unsigned handle, unsigned start, unsigned count, unsigned colors, unsigned flags)
{
    auto *s = DxSurfaceOf(handle);
    if (!s) return DdInvalidObject;
    if (!colors)
    {
        free(s->palette);
        s->palette = nullptr;
        ++s->generation;
        return DdOk;
    }
    if (start >= 256 || count > 256 - start || !virtual_memory.Access(colors, count * 4, false)) return DdInvalidParams;
    if (!s->palette)
    {
        s->palette = static_cast<unsigned *>(calloc(256, sizeof(unsigned)));
        if (!s->palette) return DdOutOfMemory;
    }
    Read(colors, s->palette + start, count * 4);
    s->palette_alpha = (flags & 1) != 0;
    ++s->generation;
    if (s->primary && s->mode_serial == User::ScreenModeSerial() && (s->format.flags & PfIndexed8))
    {
        unsigned plain[256];
        for (unsigned i = 0; i < count; ++i) plain[i] = s->palette[start + i] & 0xffffff;
        User::SetScreenColors(start, count, plain);
        User::ScreenChanged();
    }
    return DdOk;
}

unsigned Application::DxSetColorKey(unsigned handle, unsigned flags, unsigned low, unsigned high)
{
    auto *s = DxSurfaceOf(handle);
    if (!s) return DdInvalidObject;
    s->color_key = (flags & 1) != 0;
    s->key_low = low;
    s->key_high = high;
    ++s->generation;
    return DdOk;
}

// GDI on a surface. The context lies on the bytes themselves, those of a
// driver surface or those of the program, so what GDI draws is in the surface
// at once and what a Lock wrote is under the next drawing call. The DIB
// engine draws eight bits through a colour table, sixteen as 5-5-5 or
// 5-6-5 with a bitfields descriptor, and twenty-four and thirty-two.
unsigned Application::DxGetDC(unsigned address, unsigned bytes)
{
    NEO98DX_DC request{};
    if (bytes != sizeof request || !virtual_memory.Access(address, sizeof request, true) || !Read(address, &request, sizeof request) ||
        request.size != sizeof request)
        return DdInvalidParams;
    if (!dx_driver) dx_driver = new DxDriver;
    DDBLIT_PLANE plane{};
    DxSurface *surface = nullptr;
    unsigned result = DdOk;
    if (!DxPlane(&request.plane, true, &plane, &surface, result)) return result;
    const NEO98DX_FORMAT format = surface ? surface->format : request.plane.format;
    // DDRAW16 selects 565 from the red mask alone. Its other DCs
    // use BI_RGB: alpha and alternate RGB masks do not change GDI's layout.
    const bool rgb565 = Win98DDraw16Surface::Is565(format.bits, format.r);
    const bool drawable = Win98DDraw16Surface::Describes(format.flags) && !(format.flags & PfZBuffer) &&
                          ((format.bits == 8 && (format.flags & PfIndexed8)) ||
                           format.bits == 16 || format.bits == 24 || format.bits == 32);
    if (!drawable) return DdCantCreateDc;
    DxDriver::OpenDc *slot = nullptr;
    for (auto &open : dx_driver->dcs)
        if (!open.dc) { slot = &open; break; }
    if (!slot) return DdCantCreateDc;
    const bool screen = plane.bits == User::ScreenBits() || plane.bits == User::ScanoutBits();
    // GetDC locks the surface; on the screen that is the lock of its pixels.
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
    DxSync(plane.bits, true);
    const DibEng::Bitmap bitmap{plane.bits, plane.width, plane.height, plane.pitch, int(format.bits)};
    unsigned dc = 0;
    if (format.bits == 8)
    {
        unsigned table[256] = {};
        if (request.colors && virtual_memory.Access(request.colors, sizeof table, false))
        {
            Read(request.colors, table, sizeof table);
            for (auto &entry : table) entry &= 0xffffff;
        }
        else if (const unsigned *display = User::ScreenColors())
            for (unsigned i = 0; i < 256; ++i) table[i] = display[i];
        dc = Gdi::CreateSurfaceDC(bitmap, table, 256);
    }
    else dc = Gdi::CreateSurfaceDC(bitmap, nullptr, 0, rgb565);
    if (!dc)
    {
        if (screen) User::UnlockScreen(process_id, ThreadId());
        return DdCantCreateDc;
    }
    *slot = {dc, surface ? surface->handle : 0, plane.bits, screen};
    if (surface) { ++surface->generation; ++surface->locks; }
    request.dc = dc;
    Store(address, &request, sizeof request);
    return DdOk;
}

unsigned Application::DxReleaseDC(unsigned dc)
{
    if (!dx_driver || !dc) return DdNoDc;
    for (auto &open : dx_driver->dcs)
    {
        if (open.dc != dc) continue;
        Gdi::DeleteDC(open.dc);
        if (auto *s = DxSurfaceOf(open.surface))
        {
            ++s->generation;
            if (s->locks) --s->locks;
        }
        if (open.screen_locked) User::UnlockScreen(process_id, ThreadId());
        // The bytes may be the ones the monitor shows.
        if (open.bits == User::ScanoutBits() || open.bits == User::ScreenBits()) User::ScreenChanged();
        open = {};
        return DdOk;
    }
    return DdNoDc;
}

unsigned Application::DxGetInfo(unsigned address, unsigned bytes)
{
    NEO98DX_INFO info{};
    if (bytes != sizeof info || !virtual_memory.Access(address, sizeof info, true)) return DdInvalidParams;
    int width = 0, height = 0, depth = 0;
    User::ScreenMode(width, height, depth);
    info.size = sizeof info;
    info.version = NEO98DX_VERSION;
    info.width = unsigned(width);
    info.height = unsigned(height);
    info.pitch = unsigned(Win98DisplayModes::Stride(width, depth));
    info.refresh = RefreshHertz;
    DisplayFormat(depth, info.format);
    info.modeSerial = User::ScreenModeSerial();
    info.modeCount = User::ScreenModeCount();
    info.caps = (gpu_backend ? NEO98DX_CAP_3D : 0) | NEO98DX_CAP_GAMMA | (gpu_backend && gpu_backend->CompressedTextures() ? NEO98DX_CAP_DXT : 0);
    info.videoMemoryTotal = VideoMemoryBytes;
    info.videoMemoryFree = VideoMemoryBytes - (dx_driver ? dx_driver->video_bytes : 0);
    int dw = 0, dh = 0, dd = 0;
    User::DesktopMode(dw, dh, dd);
    info.desktopWidth = unsigned(dw);
    info.desktopHeight = unsigned(dh);
    info.desktopBits = unsigned(dd);
    info.exclusiveProcess = User::ExclusiveMode() ? User::ExclusiveProcess() : 0;
    info.process = process_id;
    return Store(address, &info, sizeof info) ? DdOk : DdInvalidParams;
}

unsigned Application::DxGetMode(unsigned index, unsigned address, unsigned bytes)
{
    NEO98DX_MODE mode{};
    if (bytes != sizeof mode || !virtual_memory.Access(address, sizeof mode, true)) return DdInvalidParams;
    int width = 0, height = 0, depth = 0;
    if (!User::ScreenModeAt(index, width, height, depth)) return DdInvalidMode;
    mode.width = unsigned(width);
    mode.height = unsigned(height);
    mode.bits = unsigned(depth);
    mode.refresh = RefreshHertz;
    DisplayFormat(depth, mode.format);
    mode.pitch = unsigned(Win98DisplayModes::Stride(width, depth));
    return Store(address, &mode, sizeof mode) ? DdOk : DdInvalidParams;
}

unsigned Application::DxSetMode(unsigned width, unsigned height, unsigned bits)
{
    if (!User::ScreenModeSupported(int(width), int(height), int(bits))) return DdInvalidMode;
    if (!dx_driver) dx_driver = new DxDriver;
    User::ResetScanout();
    if (ApplyDisplayMode(int(width), int(height), int(bits), 0) != 0) return DdUnsupportedMode;
    dx_driver->mode_set = true;
    if (log && trace_calls)
    {
        char text[96];
        snprintf(text, sizeof text, "DXDRV SetMode %ux%ux%u", width, height, bits);
        log(text);
    }
    return DdOk;
}

// Back to the mode of the desktop, the mode of the registry.
unsigned Application::DxRestoreMode()
{
    int width = 0, height = 0, depth = 0;
    User::DesktopMode(width, height, depth);
    User::SettleScanout();
    if (ApplyDisplayMode(width, height, depth, 0) != 0) return DdGeneric;
    if (dx_driver) dx_driver->mode_set = false;
    return DdOk;
}

// The exclusive mode is a state of the display, one for the session; it
// belongs to the process that entered it (User::SetExclusiveMode).
unsigned Application::DxSetExclusive(unsigned window, bool enter)
{
    if (!dx_driver) dx_driver = new DxDriver;
    auto &d = *dx_driver;
    if (enter)
    {
        if (User::ExclusiveMode() && User::ExclusiveProcess() != process_id) return DdExclusiveModeAlreadySet;
        User::SetExclusiveMode(true);
        d.exclusive = true;
        d.exclusive_window = window;
        return DdOk;
    }
    if (d.exclusive)
    {
        User::SettleScanout();
        User::SetExclusiveMode(false);
        d.exclusive = false;
        d.exclusive_window = 0;
    }
    return DdOk;
}

// The raster of the display, the one HalWaitForVerticalBlank keeps:
// RefreshHertz pictures a second with a retrace of RetraceMicroseconds.
// DDWAITVB_BLOCKBEGIN 1, _BLOCKBEGINEVENT 2, _BLOCKEND 4; zero only asks.
unsigned Application::DxWaitForVerticalBlank(unsigned flags, unsigned in_blank_address)
{
    const unsigned long long period = 1000000ull / RefreshHertz;
    const unsigned long long now = Now(), phase = now % period;
    const bool in_blank = phase < RetraceMicroseconds;
    if (flags & 2) return DdUnsupported;
    bool blank_after = in_blank;
    unsigned long long deadline = 0;
    if (flags & 4)
    {
        deadline = now - phase + (in_blank ? RetraceMicroseconds : period + RetraceMicroseconds);
        blank_after = false;
    }
    else if (flags & 1)
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
    if (in_blank_address && virtual_memory.Access(in_blank_address, 4, true)) Write32(in_blank_address, blank_after ? 1 : 0);
    return DdOk;
}

unsigned Application::DxGetScanLine(unsigned address)
{
    if (!address || !virtual_memory.Access(address, 4, true)) return DdInvalidParams;
    int width = 0, height = 0, depth = 0;
    User::ScreenMode(width, height, depth);
    const unsigned long long period = 1000000ull / RefreshHertz, phase = Now() % period;
    // The retrace comes first in a period, then the lines of the picture.
    unsigned line = 0;
    if (phase >= RetraceMicroseconds)
        line = unsigned((phase - RetraceMicroseconds) * unsigned(height) / (period - RetraceMicroseconds));
    Write32(address, line);
    return phase < RetraceMicroseconds ? 0x88760219u : DdOk;   // DDERR_VERTICALBLANKINPROGRESS
}

unsigned Application::DxGammaRamp(unsigned address, bool set)
{
    unsigned short ramp[768];
    if (!address || !virtual_memory.Access(address, sizeof ramp, !set)) return DdInvalidParams;
    if (set)
    {
        Read(address, ramp, sizeof ramp);
        User::SetGammaRamp(ramp);
    }
    else
    {
        User::GammaRamp(ramp);
        Store(address, ramp, sizeof ramp);
    }
    return DdOk;
}

// The visible region of a window's client area as an RGNDATA, what
// IDirectDrawClipper::GetClipList hands out. `bytes` holds the room and gets
// the size.
unsigned Application::DxGetClipList(unsigned window, unsigned data, unsigned bytes_address)
{
    if (!bytes_address || !virtual_memory.Access(bytes_address, 4, true)) return DdInvalidParams;
    if (!User::IsWindow(window)) return DdInvalidObject;
    const unsigned region = WindowVisibleRegion(window);
    if (!region) return DdGeneric;
    const unsigned size_of[] = {region, 0, 0};
    const unsigned need = GdiApi("GetRegionData", size_of);
    unsigned result = DdOk;
    if (data)
    {
        const unsigned room = Read32(bytes_address);
        if (room < need || !virtual_memory.Access(data, need, true)) result = 0x88760236u;   // DDERR_REGIONTOOSMALL
        else
        {
            const unsigned read[] = {region, need, data};
            if (!GdiApi("GetRegionData", read)) result = DdGeneric;
        }
    }
    Write32(bytes_address, need);
    const unsigned drop[] = {region};
    Api("DeleteObject", drop);
    return result;
}

// A process ends, or leaves DirectDraw: the exclusive mode, the scanout, the
// contexts and the surfaces go. The mode it set falls back to the desktop
// with the screen lease (User::DropScreenLease).
void Application::FreeDxDriver()
{
    FreeDxD3d();
    if (!dx_driver) return;
    auto &d = *dx_driver;
    for (auto &open : d.dcs)
        if (open.dc)
        {
            if (open.screen_locked) User::UnlockScreen(process_id, 0);
            Gdi::DeleteDC(open.dc);
        }
    if (gpu_backend)
        for (unsigned i = 0; i < d.count; ++i)
            if (d.surfaces[i].used)
            {
                gpu_backend->Forget(process_id, d.surfaces[i].handle);
                if (d.surfaces[i].lease.host) gpu_backend->Release(d.surfaces[i].lease.host, d.surfaces[i].lease.bytes);
            }
    // The scanout may stand in a block of this process.
    for (unsigned i = 0; i < d.count; ++i)
        if (d.surfaces[i].used && d.surfaces[i].lease.host && User::ScanoutBits() == d.surfaces[i].lease.host) User::SettleScanout();
    if (d.exclusive) User::SetExclusiveMode(false);
    // The blocks are memory of the process and go with its address space.
    delete dx_driver;
    dx_driver = nullptr;
}

unsigned Application::DxDriverApi(ApiName name, const unsigned *a)
{
    switch (name.id)
    {
    API_CASE("DxDrvVersion") return NEO98DX_VERSION;
    API_CASE("DdDrvGetInfo") return DxGetInfo(a[0], a[1]);
    API_CASE("DdDrvGetMode") return DxGetMode(a[0], a[1], a[2]);
    API_CASE("DdDrvSetMode") return DxSetMode(a[0], a[1], a[2]);
    API_CASE("DdDrvRestoreMode") return DxRestoreMode();
    API_CASE("DdDrvSetExclusive") return DxSetExclusive(a[0], a[1] != 0);
    API_CASE("DdDrvWaitForVerticalBlank") return DxWaitForVerticalBlank(a[0], a[1]);
    API_CASE("DdDrvGetScanLine") return DxGetScanLine(a[0]);
    API_CASE("DdDrvSetGammaRamp") return DxGammaRamp(a[0], true);
    API_CASE("DdDrvGetGammaRamp") return DxGammaRamp(a[0], false);
    API_CASE("DdDrvCreateSurface") return DxCreateSurface(a[0], a[1]);
    API_CASE("DdDrvDestroySurface") return DxDestroySurface(a[0]);
    API_CASE("DdDrvAttach") return DxAttach(a[0], a[1], a[2] != 0);
    API_CASE("DdDrvLock") return DxLock(a[0], a[1]);
    API_CASE("DdDrvUnlock") return DxUnlock(a[0]);
    API_CASE("DdDrvBlt") return DxBlt(a[0], a[1]);
    API_CASE("DdDrvFlip") return DxFlip(a[0], a[1], a[2]);
    API_CASE("DdDrvIsLost") return DxIsLost(a[0]);
    API_CASE("DdDrvRestore") return DxRestore(a[0], a[1], a[2]);
    API_CASE("DdDrvSetPalette") return DxSetPalette(a[0], a[1], a[2], a[3], a[4]);
    API_CASE("DdDrvSetColorKey") return DxSetColorKey(a[0], a[1], a[2], a[3]);
    API_CASE("DdDrvGetDC") return DxGetDC(a[0], a[1]);
    API_CASE("DdDrvReleaseDC") return DxReleaseDC(a[0]);
    API_CASE("DdDrvGetClipList") return DxGetClipList(a[0], a[1], a[2]);
    API_CASE("D3dDrvGetCaps") return DxD3dGetCaps(a[0], a[1]);
    API_CASE("D3dDrvCreateContext") return DxD3dCreateContext(a[0], a[1], a[2]);
    API_CASE("D3dDrvDestroyContext") return DxD3dDestroyContext(a[0]);
    API_CASE("D3dDrvRun") return DxD3dRun(a[0], a[1], a[2]);
    default:
        return DxSoundApi(name, a);
    }
}
} // namespace X86
