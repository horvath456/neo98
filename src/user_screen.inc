// SPDX-License-Identifier: GPL-2.0-or-later
// Screen surface for the native USER provider. It has dimensions, depth,
// stride and a palette; every window draws directly into it. A memory lease
// can expose the same bytes at a guest address, allowing DirectDraw primary
// surfaces to write the display directly.
// The host image is 32-bit and normally follows the desktop mode. Application
// modes are scaled to it, with indexed pixels expanded through the palette.
// The desktop mode comes from HKCC\Display\Settings and defaults to
// 800x600x32.
// USER draws desktop content only in the desktop mode. When an application
// changes the mode, it owns the display as in exclusive DirectDraw; Render
// does not overlay desktop content. Application windows still draw in the
// active surface format. The driver draws the pointer above it.
// RestoreDisplayMode restores the desktop and invalidates all windows.
namespace {
namespace Modes = Win98DisplayModes;
struct ScreenSurface
{
    unsigned char *bits = nullptr;
    int width = 0, height = 0, depth = 0, stride = 0;
    unsigned bytes = 0, guest = 0;
    bool leased = false;
    unsigned colors[256] = {};
};
ScreenSurface screen_surface;
// The desktop display mode.
Modes::Mode desktop_mode{Modes::NativeWidth, Modes::NativeHeight, Modes::NativeDepth};
bool IsDesktopMode(int width, int height, int depth)
{
    return width == desktop_mode.width && height == desktop_mode.height && depth == desktop_mode.depth;
}
// Allocate host-image storage for the largest supported mode. If
// Modes::SetPanel expands the mode list, grow the storage on the next size
// change.
unsigned published_capacity = 0;
unsigned PublishedCapacity()
{
    unsigned count = 0, largest = 0;
    const Modes::Mode *modes = Modes::Table(count);
    for (unsigned i = 0; i < count; ++i)
        if (unsigned(modes[i].width) * modes[i].height > largest) largest = unsigned(modes[i].width) * modes[i].height;
    return largest * 4;
}
// Resize the host image to the desktop mode. The old pointer and contents
// become invalid; display black until the next publication.
void ResizePublished(int width, int height)
{
    if (!published_frame || (published_width == width && published_height == height)) return;
    const unsigned need = unsigned(width) * unsigned(height) * 4;
    if (need > PublishedCapacity()) return;
    if (need > published_capacity)
    {
        delete[] published_frame;
        published_capacity = PublishedCapacity();
        published_frame = new unsigned char[published_capacity]{};
    }
    published_width = width; published_height = height;
    memset(published_frame, 0, published_capacity);
    if (published_dc) Gdi::ResizeSurfaceDC(published_dc, {published_frame, width, height, width * 4, 32}, nullptr, 0);
    ResetPointerOverlay();
    ++frame_serial;
}
// Normally the host image follows the desktop size and scales application
// modes. Seamless hosts resize their window to the application mode, so the
// host image follows every mode and only depth conversion may be needed.
bool frame_follows_mode = false;
// The surface can be copied directly when it is 32-bit and matches the host
// image dimensions.
bool PublishedDirect(int width, int height, int depth)
{
    return depth == 32 && width == published_width && height == published_height;
}
// An alternate scanout buffer must match the surface width, height and depth,
// but may have its own stride.
const unsigned char *scanout_bits = nullptr;
int scanout_stride = 0;
void *host_scanout_context = nullptr;
HostScanout host_scanout = nullptr;
bool published_deferred = false;
void MaterializeScreen();
// Count each screen allocation; User::ScreenModeSerial.
unsigned screen_mode_serial = 0;
void *screen_lease_context = nullptr;
ScreenAllocate screen_lease_allocate = nullptr;
ScreenRelease screen_lease_release = nullptr;
void FreeScreenBits(ScreenSurface &s)
{
    if (!s.bits) return;
    if (s.leased) { if (screen_lease_release) screen_lease_release(screen_lease_context, s.guest, s.bits); }
    else delete[] s.bits;
    s.bits = nullptr; s.guest = 0; s.bytes = 0; s.leased = false;
}
bool AllocateScreenBits(ScreenSurface &s, unsigned bytes)
{
    const unsigned rounded = (bytes + 4095) & ~4095u;
    if (!rounded) return false;
    if (screen_lease_allocate)
    {
        unsigned guest = 0; unsigned char *host = nullptr;
        if (screen_lease_allocate(screen_lease_context, rounded, guest, host) && host && guest)
        {
            s.bits = host; s.guest = guest; s.bytes = rounded; s.leased = true;
            for (unsigned i = 0; i < rounded; ++i) host[i] = 0;
            return true;
        }
    }
    s.bits = new unsigned char[rounded]{};
    s.guest = 0; s.bytes = rounded; s.leased = false;
    return true;
}
// Create the mode surface and attach the screen DC. At eight bits per pixel
// or below, provide the color table GDI needs to resolve device colors.
bool BuildScreen(int width, int height, int depth)
{
    MaterializeScreen();
    const bool same_size = width == screen_surface.width && height == screen_surface.height;
    if (depth != 32 && host_window_surfaces) SetHostWindowSurfaces(false);
    if (screen) TouchCancelAll();
    ScreenSurface next;
    next.width = width; next.height = height; next.depth = depth;
    next.stride = Modes::Stride(width, depth);
    for (unsigned i = 0; i < 256; ++i) next.colors[i] = screen_surface.colors[i];
    if (depth <= 8 && screen_surface.depth > 8) Modes::DefaultPalette(next.colors);
    if (!AllocateScreenBits(next, unsigned(next.stride) * unsigned(height))) return false;
    const DibEng::Bitmap bitmap{next.bits, width, height, next.stride, depth};
    const unsigned colors = depth <= 8 ? (1u << depth) : 0u;
    // Replacing the allocator or colour depth at the same size retains the
    // existing image. Transparent controls rely on those background pixels.
    if (screen_surface.bits && same_size)
    {
        if (depth == screen_surface.depth)
            memcpy(next.bits, screen_surface.bits, size_t(next.stride) * height);
        else
        {
            const DibEng::Bitmap old_bitmap{screen_surface.bits, width, height,
                                           screen_surface.stride, screen_surface.depth};
            const unsigned old_colors = screen_surface.depth <= 8 ? 1u << screen_surface.depth : 0u;
            const auto source = Gdi::CreateSurfaceDC(old_bitmap, old_colors ? screen_surface.colors : nullptr, old_colors);
            const auto target = Gdi::CreateSurfaceDC(bitmap, colors ? next.colors : nullptr, colors);
            const bool copied = source && target && Gdi::BitBlt(target, 0, 0, width, height, source, 0, 0, 0xcc0020);
            if (target) Gdi::DeleteDC(target);
            if (source) Gdi::DeleteDC(source);
            if (!copied) { FreeScreenBits(next); return false; }
        }
    }
    const unsigned char *old_target = screen_surface.bits;
    if (screen)
    {
        if (!Gdi::ResizeSurfaceDC(screen, bitmap, colors ? next.colors : nullptr, colors))
        {
            FreeScreenBits(next);
            return false;
        }
        // All screen DCs share the same bytes. Replace their surfaces before
        // releasing the old allocation.
        if (old_target && old_target != bitmap.bits)
            Gdi::RetargetSurface(old_target, bitmap, colors ? next.colors : nullptr, colors);
    }
    else
    {
        screen = colors ? Gdi::CreateSurfaceDC(bitmap, next.colors, colors)
                        : Gdi::CreateSurfaceDC(bitmap);
        if (!screen) { FreeScreenBits(next); return false; }
    }
    if (same_size && depth != screen_surface.depth)
        ReformatSaved(screen_surface.depth, screen_surface.colors, depth, next.colors);
    FreeScreenBits(screen_surface);
    screen_surface = next;
    framebuffer = screen_surface.bits;
    ++screen_mode_serial;
    // A lock held across a mode change protects the new bytes.
    Gdi::SetScreenGuard(ScreenLockActive() ? screen_surface.bits : nullptr);
    // The old allocation and any scanout start within it are gone.
    scanout_bits = nullptr;
    scanout_stride = 0;
    Gdi::SetDisplayCaps(width, height, depth);
    // Reload system colors after a mode change. Indexed modes use the nearest
    // fixed palette colors.
    Win98Frame::RefreshSystemColors(depth <= 8, system_colour_listener);
    Gdi::SetDisplayColors(screen_surface.colors, depth <= 8 ? (1u << depth) : 256u);
    // Resize the host image with the desktop, or with an application mode
    // when SetHostFrameFollowsMode is enabled.
    if (IsDesktopMode(width, height, depth) || frame_follows_mode) ResizePublished(width, height);
    // A resized screen needs new visible regions; a converted image keeps
    // its regions and the saved backgrounds of open menus and dialogs.
    if (!same_size) ResetShown();
    return true;
}
// All window DCs draw into the screen surface in the current display format.
DibEng::Bitmap ScreenBitmapView()
{
    return {screen_surface.bits, screen_surface.width, screen_surface.height, screen_surface.stride, screen_surface.depth};
}
const unsigned *ScreenTable(unsigned &count)
{
    count = screen_surface.depth <= 8 ? (1u << screen_surface.depth) : 0u;
    return screen_surface.colors;
}
void ScreenSize(int &width, int &height) { width = screen_surface.width; height = screen_surface.height; }
void ReleaseScreen()
{
    FreeScreenBits(screen_surface);
    screen_surface = ScreenSurface{};
    framebuffer = nullptr;
    Gdi::SetScreenGuard(nullptr);
    desktop_mode = {Modes::NativeWidth, Modes::NativeHeight, Modes::NativeDepth};
}
// Update SM_CXSCREEN/SM_CYSCREEN, mirrored metrics 78/79 and the monitor
// rectangles after a mode change. Preserve the work-area insets relative to
// the old monitor bounds; leave the work area unchanged if monitor bounds did
// not change. This retains taskbar space across resolution switches without
// requiring Explorer to correct it.
void ApplyScreenMetrics(int width, int height)
{
    settings.metrics[0] = settings.metrics[78] = (unsigned short)width;
    settings.metrics[1] = settings.metrics[79] = (unsigned short)height;
    // The desktop window and its region follow the screen dimensions.
    if (desktop.width != width || desktop.height != height)
    {
        desktop.width = width; desktop.height = height;
        Layout(Desktop);
        if (desktop_region) { Gdi::DeleteObject(desktop_region); desktop_region = 0; }
    }
    const short old_monitor[4] = {settings.monitor[0], settings.monitor[1], settings.monitor[2], settings.monitor[3]};
    settings.monitor[0] = 0;
    settings.monitor[1] = 0;
    settings.monitor[2] = (short)width;
    settings.monitor[3] = (short)height;
    if (!memcmp(old_monitor, settings.monitor, sizeof old_monitor)) return;
    for (int i = 0; i < 4; ++i)
        settings.work[i] = (short)(settings.monitor[i] + (settings.work[i] - old_monitor[i]));
    // the largest tracking size follows the screen (twice the
    // sizing frame plus the virtual screen plus the word at 6E8h), and
    // recomputes what depends on the work area (SM_CXFULLSCREEN,
    // SM_CXMAXIMIZED and their heights). Without this SM_CXMAXTRACK and
    // SM_CYMAXTRACK stayed at 812 by 612 on every other screen, and a window
    // could not be made larger than that once DefWindowProc held a size to
    // them.
    settings.SetWord(0x354, (unsigned short)(settings.Word(0x31e) * 2 + settings.Word(0x37a) + settings.Word(0x6e8)));
    settings.SetWord(0x356, (unsigned short)(settings.Word(0x320) * 2 + settings.Word(0x37c) + settings.Word(0x6ea)));
    Win98Metrics::Recompute(settings);
}
// The screen memory is a session view and survives process exit. Transfer the
// allocator for future surfaces to another live process, preserving the
// current surface and guest address because another application may hold it
// as a primary surface.
// If the departing process owns the application mode, restore the desktop and
// repaint the shell. Do the same when no process retains the view; the
// replacement surface then has no guest address.
unsigned mode_process = 0;
void DropScreenLease(void *context, unsigned pid)
{
    const bool provider = screen_lease_allocate && screen_lease_context == context;
    void *heir = nullptr;
    if (provider)
    {
        for (const auto *p=processes;p;p=p->next)
            if (p->pid != pid && p->context && p->context != context) { heir = p->context; break; }
        if (heir) screen_lease_context = heir;
        else
        {
            // Without a provider, FreeScreenBits releases only its lease. The
            // view ends with the last process.
            screen_lease_context = nullptr;
            screen_lease_allocate = nullptr;
            screen_lease_release = nullptr;
        }
    }
    const bool owner = pid && mode_process == pid;
    if (owner) mode_process = 0;
    if (!screen_surface.bits) return;
    const bool changed = !IsDesktopMode(screen_surface.width, screen_surface.height, screen_surface.depth);
    if (!(provider && !heir) && !(owner && changed)) return;
    // BuildScreen retargets window DCs to the new storage and releases the
    // old bytes.
    const int width = desktop_mode.width, height = desktop_mode.height, depth = desktop_mode.depth;
    if (!BuildScreen(width, height, depth)) return;
    // Only a real mode change updates metrics; preserve work-area insets
    // through ApplyScreenMetrics. Post WM_DISPLAYCHANGE to top-level windows
    // because the process that owned the mode can no longer send it.
    if (changed)
    {
        ApplyScreenMetrics(width, height);
        unsigned windows[WindowCapacity], count = 0;
        if (WindowSnapshot(0, windows, WindowCapacity, count))
            for (unsigned i = 0; i < count; ++i)
                PostMessage(windows[i], 0x7e, unsigned(depth), unsigned(height) << 16 | unsigned(width));
    }
    Gdi::ResetSurfaceDC(screen);
    Gdi::SelectObject(screen, system_font);
    ResetPointerOverlay();
    dirty = true;
    InvalidateRect(Desktop, nullptr, true);
    ScreenChanged();
}
} // namespace

void SetHostFrameFollowsMode(bool on)
{
    if (frame_follows_mode == on) return;
    frame_follows_mode = on;
    if (!ScreenModeActive() || !screen_surface.bits) return;
    if (on) ResizePublished(screen_surface.width, screen_surface.height);
    else ResizePublished(desktop_mode.width, desktop_mode.height);
    dirty = true;
    if (!rendering) { PublishScreen(); ++frame_serial; ResetPointerOverlay(); MovePointer(); }
}
bool ScreenModeActive()
{
    return !IsDesktopMode(screen_surface.width, screen_surface.height, screen_surface.depth);
}
// DirectDraw exclusive mode arrives through the HAL SetExclusiveMode
// callback's dwEnterExcl field. It belongs to the requesting process and ends
// when that process exits.
namespace { unsigned exclusive_process = 0; bool exclusive_mode = false; }
void SetExclusiveMode(bool enter)
{
    exclusive_mode = enter;
    exclusive_process = enter ? CurrentProcess() : 0;
}
bool ExclusiveMode() { return exclusive_mode; }
unsigned ExclusiveProcess() { return exclusive_mode ? exclusive_process : 0; }
// A DirectDraw primary-surface lock gives the caller access to display pixels
// until Unlock. Windows 98 holds Win16Mutex for this interval. Here the
// session lock blocks other threads only when they access display pixels
// through ScreenWait; USER defers rendering, control paints, geometry copies
// and caret blinking. The holder never waits on itself.
// Track waiters in screen_waiters. The final Unlock grants each waiter
// access, and a new Lock waits until those grants are consumed. Scheduler
// block codes 17 and 18 query this state without periodic polling.
namespace
{
struct ScreenHolder { unsigned process = 0, thread = 0, depth = 0; bool parked = false; };
ScreenHolder screen_holders[8];
unsigned screen_holder_count = 0;
struct ScreenWaiter { unsigned process = 0, thread = 0; bool granted = false; };
ScreenWaiter screen_waiters[64];
void GrantScreenWaiters() { for (auto &w : screen_waiters) if (w.thread) w.granted = true; }
ScreenWaiter *FindScreenWaiter(unsigned thread)
{
    for (auto &w : screen_waiters) if (w.thread && w.thread == thread) return &w;
    return nullptr;
}
void DropScreenHolder(ScreenHolder &h)
{
    h = {};
    --screen_holder_count;
    if (!screen_holder_count)
    {
        GrantScreenWaiters();
        Gdi::SetScreenGuard(nullptr);
    }
}
}
bool DrawsOnScreen(Gdi::HDC dc) { return screen && dc && (dc == screen || Gdi::SameSurface(dc, screen)); }
bool ScreenLockActive() { return screen_holder_count != 0; }
void LockScreen(unsigned process, unsigned thread)
{
    ScreenHolder *free_slot = nullptr;
    for (auto &h : screen_holders)
    {
        if (h.thread && h.thread == thread) { ++h.depth; return; }
        if (!h.thread && !free_slot) free_slot = &h;
    }
    // Windows 98 serializes primary-surface locks through Win16Mutex, so
    // concurrent holders cannot exceed these slots.
    if (!free_slot) return;
    *free_slot = {process, thread, 1, false};
    ++screen_holder_count;
    // While locked, GDI reports each access to the screen guard
    // (Gdi::SetScreenGuard, Application::ScreenTouched).
    Gdi::SetScreenGuard(screen_surface.bits);
}
void UnlockScreen(unsigned process, unsigned thread)
{
    // An Unlock on another thread releases the process's lock.
    ScreenHolder *found = nullptr;
    for (auto &h : screen_holders) if (h.thread && h.thread == thread) { found = &h; break; }
    if (!found) for (auto &h : screen_holders) if (h.thread && h.process == process) { found = &h; break; }
    if (!found) return;
    if (!--found->depth) DropScreenHolder(*found);
}
bool ScreenHeldBy(unsigned thread)
{
    for (const auto &h : screen_holders) if (h.thread && h.thread == thread) return true;
    return false;
}
bool ScreenLockedElsewhere(unsigned process, unsigned thread)
{
    (void)process;
    if (!screen_holder_count) return false;
    // A holder never waits, even if another thread also holds the surface.
    if (ScreenHeldBy(thread)) return false;
    bool other = false;
    for (const auto &h : screen_holders)
        if (h.thread && h.thread != thread && !h.parked) other = true;
    if (!other) return false;
    const auto *w = FindScreenWaiter(thread);
    return !(w && w->granted);
}
bool ScreenMayDraw(unsigned process, unsigned thread) { return !ScreenLockedElsewhere(process, thread); }
void WaitScreen(unsigned process, unsigned thread)
{
    if (!thread || FindScreenWaiter(thread)) return;
    for (auto &w : screen_waiters)
        if (!w.thread) { w = {process, thread, false}; return; }
}
void DoneScreenWait(unsigned process, unsigned thread)
{
    (void)process;
    if (auto *w = FindScreenWaiter(thread)) *w = {};
}
bool ScreenGrantsPending(unsigned thread)
{
    for (const auto &w : screen_waiters)
        if (w.thread && w.thread != thread && w.granted) return true;
    return false;
}
// While a holder waits for messages, allow blocked painters to run. Windows
// 98 message waits release Win16Mutex through its scheduler; retaining the
// lock here could deadlock a synchronous send to a waiting thread.
bool ParkScreen(unsigned thread, bool parked)
{
    bool held = false;
    for (auto &h : screen_holders)
        if (h.thread && h.thread == thread) { h.parked = parked; held = true; }
    if (held && parked) GrantScreenWaiters();
    return held;
}
void DropScreenThread(unsigned process, unsigned thread)
{
    for (auto &h : screen_holders)
        if (h.thread && (h.thread == thread || (!thread && h.process == process))) DropScreenHolder(h);
    for (auto &w : screen_waiters)
        if (w.thread && (w.thread == thread || (!thread && w.process == process))) w = {};
}
void DropExclusiveMode(unsigned pid)
{
    if (exclusive_mode && exclusive_process == pid) { exclusive_mode = false; exclusive_process = 0; }
    DropScreenThread(pid, 0);
}
bool DesktopModeSupported(int width, int height, int depth)
{
    return Modes::DesktopCapable(width, height, depth);
}
bool SetDesktopMode(int width, int height, int depth)
{
    if (!Modes::DesktopCapable(width, height, depth)) return false;
    desktop_mode = {(unsigned short)width, (unsigned short)height, (unsigned char)depth};
    return true;
}
void DesktopMode(int &width, int &height, int &depth)
{
    width = desktop_mode.width; height = desktop_mode.height; depth = desktop_mode.depth;
}
void ScreenMode(int &width, int &height, int &depth)
{
    width = screen_surface.width; height = screen_surface.height; depth = screen_surface.depth;
}
unsigned ScreenModeSerial() { return screen_mode_serial; }
unsigned ScreenModeCount() { unsigned count = 0; Modes::Table(count); return count; }
bool ScreenModeAt(unsigned index, int &width, int &height, int &depth)
{
    unsigned count = 0;
    const auto *modes = Modes::Table(count);
    if (index >= count) return false;
    width = modes[index].width; height = modes[index].height; depth = modes[index].depth;
    return true;
}
bool ScreenModeSupported(int width, int height, int depth)
{
    return Modes::Supported(width, height, depth);
}
bool SetScreenPanel(int width, int height) { return Modes::SetPanel(width, height); }
void ScreenPanel(int &width, int &height)
{
    width = Modes::PanelMode().width; height = Modes::PanelMode().height;
}
unsigned ScreenGuestAddress() { return screen_surface.guest; }
const unsigned char *ScreenBits() { return screen_surface.bits; }
int ScreenStride() { return screen_surface.stride; }
unsigned ScreenBytes() { return screen_surface.bytes; }
const unsigned *ScreenColors() { return screen_surface.colors; }
// The realized display palette belongs to the surface and is shared by the
// screen DC, all display DCs using the same bytes and the published host
// image.
void InstallScreenPaletteSink()
{
    Gdi::SetDisplayPaletteSink(nullptr, [](void *, unsigned start, unsigned count,
                                           const unsigned *colors) {
        SetScreenColors(start, count, colors);
    });
}
bool SetScreenColors(unsigned start, unsigned count, const unsigned *colors)
{
    if (!colors || screen_surface.depth > 8) return false;
    const unsigned entries = 1u << screen_surface.depth;
    if (start >= entries) return false;
    if (count > entries - start) count = entries - start;
    for (unsigned i = 0; i < count; ++i) screen_surface.colors[start + i] = colors[i] & 0xffffff;
    Gdi::SetDisplayColors(screen_surface.colors, entries);
    const DibEng::Bitmap bitmap{screen_surface.bits, screen_surface.width, screen_surface.height,
                                screen_surface.stride, screen_surface.depth};
    if (!Gdi::SetSurfaceColors(screen, start, count, colors)) return false;
    Gdi::RetargetSurface(screen_surface.bits, bitmap, screen_surface.colors, entries);
    PublishScreen();
    return true;
}
// Gamma lookup tables for the host image's blue, green and red bytes, plus
// the application's original ramp values.
namespace
{
unsigned short gamma_ramp[768];
unsigned char gamma_table[3][256];
bool gamma_active = false, gamma_ready = false;
void GammaDefault()
{
    if (gamma_ready) return;
    for (unsigned i = 0; i < 256; ++i)
        for (unsigned c = 0; c < 3; ++c) { gamma_ramp[c * 256 + i] = (unsigned short)(i * 257); gamma_table[c][i] = (unsigned char)i; }
    gamma_ready = true;
}
void ApplyGamma()
{
    if (!gamma_active || !published_frame) return;
    unsigned char *p = published_frame;
    for (unsigned n = 0, pixels = unsigned(published_width * published_height); n < pixels; ++n, p += 4)
    {
        p[0] = gamma_table[2][p[0]];
        p[1] = gamma_table[1][p[1]];
        p[2] = gamma_table[0][p[2]];
    }
}
} // namespace
void SetGammaRamp(const unsigned short *ramp)
{
    GammaDefault();
    bool identity = true;
    for (unsigned c = 0; c < 3; ++c)
        for (unsigned i = 0; i < 256; ++i)
        {
            gamma_ramp[c * 256 + i] = ramp[c * 256 + i];
            gamma_table[c][i] = (unsigned char)(ramp[c * 256 + i] >> 8);
            if (gamma_table[c][i] != i) identity = false;
        }
    gamma_active = !identity;
    PublishScreen();
    ++frame_serial;
}
void GammaRamp(unsigned short *ramp)
{
    GammaDefault();
    for (unsigned i = 0; i < 768; ++i) ramp[i] = gamma_ramp[i];
}
// Refresh the host image from the screen surface. Copy matching modes
// directly; scale other modes and expand indexed pixels through the palette.
void PublishScreenBytes()
{
    if (!published_frame || !screen_surface.bits) return;
    const unsigned char *bits = scanout_bits ? scanout_bits : screen_surface.bits;
    const int stride = scanout_bits ? scanout_stride : screen_surface.stride;
    if (PublishedDirect(screen_surface.width, screen_surface.height, screen_surface.depth) &&
        stride == screen_surface.stride)
    {
        memcpy(published_frame, bits, size_t(published_width) * size_t(published_height) * 4u);
        ApplyGamma();
        // The copy took the pointer with it, as below. A caller that is not
        // Render (ScreenChanged after a DirectDraw blit to the primary
        // surface, ScanoutScreen) once left the frame without it until the
        // mouse moved: the pointer blinked over every windowed DirectDraw
        // game.
        ResetPointerOverlay();
        MovePointer();
        return;
    }
    const Modes::Surface source{bits, screen_surface.width, screen_surface.height,
                                stride, screen_surface.depth, screen_surface.colors};
    Modes::Publish(source, published_frame, published_width, published_height, published_width * 4);
    ApplyGamma();
    // The new image has no pointer overlay. Draw the pointer again from the
    // mode surface; MovePointer therefore remains idempotent in application
    // modes. Desktop modes other than 800x600x32 use the same overlay path.
    {
        ResetPointerOverlay();
        MovePointer();
    }
}
bool DeferScreenRead()
{
    int ow = 0, oh = 0; unsigned serial = 0;
    return host_scanout && !gamma_active && !rendering && !PointerVisible() &&
        (ScreenModeActive() || exclusive_mode || scanout_bits) && !Overlay(ow, oh, serial) &&
        host_scanout(host_scanout_context, ScanoutBits(), false);
}
void PublishScreen()
{
    // Coalesce CPU conversion just as the GPU readback is coalesced. Pixels
    // reads the latest bytes, including direct stores made after a flip or
    // after ScanoutScreen. The cursor is composed when those bytes are read.
    const bool cpu_deferred = !host_scanout && !rendering &&
        screen_surface.depth == 16 && (ScreenModeActive() || exclusive_mode || scanout_bits);
    published_deferred = cpu_deferred || DeferScreenRead();
    if (published_deferred) return;
    if (host_scanout) host_scanout(host_scanout_context, ScanoutBits(), true);
    PublishScreenBytes();
}
namespace
{
void MaterializeScreen()
{
    if (!published_deferred) return;
    published_deferred = false;
    if (host_scanout) host_scanout(host_scanout_context, ScanoutBits(), true);
    PublishScreenBytes();
}
}
void SetHostScanoutProvider(void *context, HostScanout provider)
{
    MaterializeScreen();
    host_scanout_context = context;
    host_scanout = provider;
}
namespace {
// Republish a rectangle in mode coordinates using Publish's mapping: host
// column x reads mode column x * mode_width / host_width. With patch, read
// 32-bit pixels from the patch instead of the surface. The pointer overlay is
// then scaled together with the mode image.
void PublishModeRect(int left, int top, int right, int bottom, const unsigned char *patch, int patch_stride)
{
    if (!published_frame || !screen_surface.bits || left >= right || top >= bottom) return;
    const int w = screen_surface.width, h = screen_surface.height;
    const Modes::Surface source{scanout_bits ? scanout_bits : screen_surface.bits, w, h,
                                scanout_bits ? scanout_stride : screen_surface.stride,
                                screen_surface.depth, screen_surface.colors};
    auto span = [](int first, int last, int mode, int host, int &from, int &to) {
        from = int(((long long)first * host + mode - 1) / mode);
        to = int(((long long)last * host + mode - 1) / mode);
        if (from < 0) from = 0;
        if (to > host) to = host;
    };
    int x0, x1, y0, y1;
    const int pw = published_width, ph = published_height;
    span(left, right, w, pw, x0, x1);
    span(top, bottom, h, ph, y0, y1);
    for (int y = y0; y < y1; ++y)
    {
        const int sy = int((long long)y * h / ph);
        unsigned char *row = published_frame + (long)y * pw * 4;
        for (int x = x0; x < x1; ++x)
        {
            const int sx = int((long long)x * w / pw);
            unsigned pixel;
            if (patch)
            {
                const unsigned char *p = patch + (long)(sy - top) * patch_stride + (sx - left) * 4;
                pixel = unsigned(p[0]) | (unsigned(p[1]) << 8) | (unsigned(p[2]) << 16);
            }
            else pixel = Modes::Pixel(source, sx, sy);
            unsigned char *out = row + x * 4;
            out[0] = pixel & 255; out[1] = (pixel >> 8) & 255; out[2] = (pixel >> 16) & 255; out[3] = 0;
            if (gamma_active)
            {
                out[0] = gamma_table[2][out[0]];
                out[1] = gamma_table[1][out[1]];
                out[2] = gamma_table[0][out[2]];
            }
        }
    }
}
// Read the rectangle beneath the pointer as 32-bit pixels for its AND/XOR
// overlay.
void ReadModeRect(int left, int top, int right, int bottom, unsigned char *patch, int patch_stride)
{
    MaterializeScreen();
    const Modes::Surface source{scanout_bits ? scanout_bits : screen_surface.bits, screen_surface.width,
                                screen_surface.height, scanout_bits ? scanout_stride : screen_surface.stride,
                                screen_surface.depth, screen_surface.colors};
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
        {
            const unsigned pixel = Modes::Pixel(source, x, y);
            unsigned char *p = patch + (long)(y - top) * patch_stride + (x - left) * 4;
            p[0] = pixel & 255; p[1] = (pixel >> 8) & 255; p[2] = (pixel >> 16) & 255; p[3] = 0;
        }
}
} // namespace
void ScreenChanged()
{
    PublishScreen();
    ++frame_serial;
}
// Scanout continues even without GDI drawing. Applications in a custom or
// exclusive DirectDraw mode often write the locked primary surface directly;
// publish those pixels even when USER has no paint work. Otherwise Render
// handles publication.
bool ScanoutScreen()
{
    if ((!ScreenModeActive() && !exclusive_mode && !scanout_bits) || !screen_surface.bits || rendering) return false;
    PublishScreen();
    ++frame_serial;
    return true;
}
void SetScanout(const unsigned char *bits, int stride)
{
    if (!bits || stride <= 0) { ResetScanout(); return; }
    scanout_bits = bits;
    scanout_stride = stride;
}
void ResetScanout()
{
    scanout_bits = nullptr;
    scanout_stride = 0;
}
const unsigned char *ScanoutBits() { return scanout_bits ? scanout_bits : screen_surface.bits; }
namespace
{
const unsigned *overlay_bits = nullptr;
int overlay_width = 0, overlay_height = 0;
unsigned overlay_serial = 0;
int host_output_width = 0, host_output_height = 0;
}
void SetOverlay(const unsigned *bits, int width, int height)
{
    overlay_bits = bits;
    overlay_width = width;
    overlay_height = height;
    ++overlay_serial;
}
void ClearOverlay()
{
    overlay_bits = nullptr;
    overlay_width = overlay_height = 0;
    ++overlay_serial;
}
const unsigned *Overlay(int &width, int &height, unsigned &serial)
{
    width = overlay_width;
    height = overlay_height;
    serial = overlay_serial;
    return overlay_bits;
}
void SetHostOutput(int width, int height)
{
    host_output_width = width;
    host_output_height = height;
}
void HostOutput(int &width, int &height)
{
    width = host_output_width;
    height = host_output_height;
}
void SettleScanout()
{
    MaterializeScreen();
    if (scanout_bits && screen_surface.bits && scanout_bits != screen_surface.bits)
    {
        Gdi::GuardScreenBits(scanout_bits, false);
        Gdi::GuardScreenBits(screen_surface.bits, true);
        const int row = screen_surface.stride < scanout_stride ? screen_surface.stride : scanout_stride;
        for (int y = 0; y < screen_surface.height; ++y)
            memcpy(screen_surface.bits + (long)y * screen_surface.stride, scanout_bits + (long)y * scanout_stride, size_t(row));
    }
    ResetScanout();
    PublishScreen();
}
void SetScreenLeaseProvider(void *context, ScreenAllocate allocate, ScreenRelease release)
{
    if (screen_lease_context == context && screen_lease_allocate == allocate &&
        screen_lease_release == release)
        return;
    screen_lease_context = context;
    screen_lease_allocate = allocate;
    screen_lease_release = release;
    // Recreate the existing surface with a guest address while preserving its
    // contents.
    if (screen_surface.bits && allocate && !screen_surface.leased)
    {
        if (BuildScreen(screen_surface.width, screen_surface.height, screen_surface.depth))
            dirty = true;
    }
}
static bool SetScreenSurface(int width, int height, int depth)
{
    // An unchanged mode needs no work. If an application mode becomes the
    // desktop mode, only update the host image.
    if (screen_surface.width == width && screen_surface.height == height &&
        screen_surface.depth == depth)
    {
        // The application mode becomes the desktop mode; resize the host
        // image to match.
        if (IsDesktopMode(width, height, depth) && (published_width != width || published_height != height))
        {
            ResizePublished(width, height);
            dirty = true;
            InvalidateRect(Desktop, nullptr, true);
            ScreenChanged();
        }
        return true;
    }
    // Switch the surface immediately, matching Windows 98 DISPLAY.
    // BuildScreen retargets every window DC through Gdi::RetargetSurface.
    // Equal dimensions preserve pixels and visible regions; otherwise the
    // next geometry pass invalidates visible windows.
    if (!BuildScreen(width, height, depth)) return false;
    // An application display mode belongs to the process that selected it
    // (DropScreenLease).
    mode_process = IsDesktopMode(width, height, depth) ? 0 : CurrentProcess();
    ApplyScreenMetrics(width, height);
    Gdi::ResetSurfaceDC(screen);
    Gdi::SelectObject(screen, system_font);
    ResetPointerOverlay();
    dirty = true;
    ScreenChanged();
    return true;
}

// BIOS/VGA modes are programmed through registers, not EnumDisplaySettings.
// The device has decoded the indexed planes into RGB for native scanout.
bool SetVgaScreenMode(int width, int height)
{
    return width > 0 && height > 0 && width <= 1024 && height <= 1024 &&
           SetScreenSurface(width, height, 32);
}
bool SetScreenMode(int width, int height, int depth)
{
    return Modes::Supported(width, height, depth) && SetScreenSurface(width, height, depth);
}
