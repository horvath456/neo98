// SPDX-License-Identifier: GPL-2.0-or-later
// The DirectDraw bottom edge of the NEO98 display driver: the DCI escape
// DCICOMMAND with the four DDHAL commands DirectDraw sends inside it, and the
// VxD device \\.\DDRAW.VXD that DirectDrawCreate opens before anything else.
// Nothing here is a success stub. Every answer is the answer a Windows 98
// display driver that sits on the DIB engine and has no DirectDraw HAL gives,
// read out of DIBENG.DLL, and the addresses are in src/win98_ddraw_driver.h.
// That driver puts DirectDraw on its emulation path, the HEL, which is exactly
// the path NEO98 wants: the pixels are ours, the acceleration is nobody's.
//
#include "win98_ddraw_driver.h"
#include "win98_ddhal.h"
#include "x86_app.h"
#include <stdio.h>
#include <stdlib.h>
namespace X86
{
namespace D = Win98DDrawDriver;

// The QUERYESCSUPPORT answer of the display driver. GDI32 does not produce it;
// DIBENG does, and it does not answer one. It answers the marker
// 0x5250 for every escape number its Control entry knows and one for the
// marker itself. DirectDraw reads this and wants 0xff or 0x100, the
// DCI version of a driver with a DirectDraw HAL; 0x5250 is neither, so the
// create goes on without a HAL. DCIMAN only wants something non-zero.
unsigned Application::DriverEscapeSupport(unsigned escape, bool known)
{
    // With a rasterizer behind the driver there is a DirectDraw HAL, and the
    // answer for DCICOMMAND is the DCI version a driver with one gives.
    // src/x86_ddhal.cpp.
    if (escape == D::DciCommand && DirectDrawHalAvailable()) return Win98DDHal::DciVersion;
    return D::EscapeSupport(escape, known ? 1 : 0);
}

// Escape(hdc, DCICOMMAND,...) and ExtEscape with the same number. The DIB
// engine refuses every command except DCICREATEPRIMARYSURFACE,
// returning 0xffff for unsupported commands. The four DDHAL
// commands DirectDraw sends, DDCREATEDRIVEROBJECT, DDGET32BITDRIVERNAME,
// DDNEWCALLBACKFNS and DDVERSIONINFO, are among them, so a DIB engine driver
// refuses all four and DirectDraw builds an emulation-only object.
// The important part is that this is a refusal with a value, not the zero
// GDI32 hands back for an escape the driver does not know at all. That zero
// was.
unsigned Application::DciEscapeApi(unsigned hdc, unsigned in_bytes, unsigned in,
                                   unsigned out_bytes, unsigned out)
{
    D::Command command;
    if (in_bytes < 4 || !in || !Read(in, &command, in_bytes < sizeof command ? in_bytes : sizeof command))
        return D::Refused();
    // The four DDHAL commands, served when the driver has a HAL.
    if (command.command >= D::DdCreateDriverObject && command.command <= D::DdVersionInfo && DirectDrawHalAvailable())
        return DirectDrawHalCommand(command.command, command.parameter1, out_bytes, out);
    // A driver with a HAL hands out no DCI primary surface: DirectDraw takes
    // the primary from vmiData.fpPrimary, and a Lock on it goes to the video
    // memory the flip left it in, not to a fixed frame buffer. With both, the
    // HEL wrote into the DCI surface while the flip showed the other buffer.
    if (command.command == D::DciCreatePrimarySurface && DirectDrawHalAvailable())
    {
        DriverTrace("DCICREATEPRIMARYSURFACE refused, the HAL has the primary", 0);
        return D::Refused();
    }
    if (!D::DibEngineServes(command.command))
    {
        DriverTrace("DCICOMMAND refused", command.command);
        return D::Refused();
    }
    // DCICREATEPRIMARYSURFACE. The output buffer is four bytes, an
    // LPDCISURFACEINFO; zeroes it and Windows 98 writes the far pointer
    // of the driver's one structure into it.
    (void)hdc;
    // ExtEscape provides an output capacity; Escape provides only a pointer.
    // Reject an explicit capacity below four bytes, but accept an unspecified
    // capacity for the 16-bit DirectDraw/DCIMAN path.
    if (out_bytes && out_bytes < 4) return D::Refused();
    return DciCreatePrimarySurface(out);
}

// DCICREATEPRIMARYSURFACE. The DIB engine builds the answer out of
// the PDEVICE: width, height, stride, bits and the 16:16 pointer at
// PDEVICE+0x16 and +0x1a. NEO98 has no 16-bit PDEVICE and no LDT selector for
// the screen, so the pointer is flat: dwOffSurface carries the linear guest
// address of the screen surface and wSelSurface stays zero. That is the one
// place where this driver cannot follow the original literally, because the
// original's pointer form does not exist here; everything else is the
// structure the DIB engine writes.
// Without a guest address there is no surface at all, and that is the refusal
// The lease is installed first, so the screen gets one.
unsigned Application::DciCreatePrimarySurface(unsigned out)
{
    if (!out || !virtual_memory.Access(out, 4, true)) return D::Refused();
    Write32(out, 0);   //
    InstallScreenLease();
    if (!User::ScreenGuestAddress())
    {
        DriverTrace("DCICREATEPRIMARYSURFACE without a screen address", 0);
        return D::Refused();
    }
    if (!dci_surface)
    {
        dci_surface = Allocate(4096);
        if (!dci_surface) return D::Refused();
    }
    D::SurfaceInfo info;
    int width = 0, height = 0, depth = 0;
    User::ScreenMode(width, height, depth);
    info.width = unsigned(width);
    info.height = unsigned(height);
    info.stride = User::ScreenStride();
    info.bit_count = unsigned(depth);
    info.offset = User::ScreenGuestAddress();
    info.selector = 0;
    // Windows 98 keeps the device the surface belongs to in dwReserved1, and
    // Windows 98 reads it back there. There is one device here.
    info.device = dci_surface;
    // Windows 98 write three 16-bit far pointers into the driver's own
    // code. The DCI manager of NEO98 is native code (src/x86_dciman16.cpp) and
    // calls the three routines below directly, so there is nothing for a guest
    // to call and the three fields stay zero.
    unsigned char bytes[0x48];
    D::Serialize(info, bytes);
    if (!Store(dci_surface, bytes, sizeof bytes)) return D::Refused();
    Write32(out, dci_surface);
    ++dci_surfaces;   //
    DriverTrace("DCICREATEPRIMARYSURFACE", info.offset);
    return D::DciOk;
}

// A re-check, not a lock: the surface is still the one the create
// described only while stride and bit count are unchanged, and a mode change
// between the two is exactly what makes them differ. Windows 98 answers
// DCI_FAIL_INVALIDSURFACE then, and the caller has to create the surface
// again.
unsigned Application::DciBeginAccess(unsigned surface, int x, int y, int dx, int dy)
{
    if (!surface || surface != dci_surface || !dci_surfaces) return D::Refused();
    // DirectDraw marks the primary surface busy while it is locked, so
    // BeginAccess refuses access. The native screen lock records that state.
    // DCI holds the Win16Mutex only during the flat thunk call, not until
    // DCIEndAccess.
    if (User::ScreenLockActive()) return D::DciFailGeneric;
    int width = 0, height = 0, depth = 0;
    User::ScreenMode(width, height, depth);
    if (Read32(surface + 0x20) != unsigned(User::ScreenStride()) ||
        Read32(surface + 0x24) != unsigned(depth))
    {
        DriverTrace("DCIBEGINACCESS after a mode change", unsigned(depth));
        return D::DciFailInvalidSurface;
    }
    // The requested access rectangle must lie within the surface.
    if (x < 0 || y < 0 || dx < 0 || dy < 0 || x + dx > width || y + dy > height)
        return D::DciFailInvalidSurface;
    DriverTrace("DCIBEGINACCESS", (unsigned(x) << 16) | (unsigned(y) & 65535));
    // A frame is an access that covers at least half of the screen; a sprite
    // or a dirty rectangle is not. DciEndAccess paces frames.
    dci_frame_access = (long long)dx * dy * 2 >= (long long)width * height;
    return D::DciOk;
}

// The display shows RefreshHertz pictures a second, and a card of the time
// moved a full frame over its bus in about that time. A program that draws
// whole frames into the primary surface as fast as it can, and counts frames
// instead of time for an animation, ran several hundred frames a second here:
// the menu and the windmill of Moorhuhn raced while its birds, which follow
// the clock, flew at the right speed. A frame that ends earlier than
// one refresh period after the previous one waits for the rest of the period,
// like a Sleep. There is one clock, because there is one display: the DCI
// path, the Flip of the DirectDraw HAL and a whole-frame Unlock of the visible
// surface all pass through here.
void Application::PaceFrame()
{
    // Benchmark opt-in. Normal runs retain the display clock because some
    // games advance animations once per frame rather than by elapsed time.
    if (getenv("NEO98_UNCAPPED")) return;
    const unsigned long long period = 1000000ull / RefreshHertz, now = Now();
    if (getenv("NEO98_TRACE_PACING"))
        fprintf(stderr, "PACE now=%llu previous=%llu presents=%u thread=%u\n", now, dci_frame_time,
                hal_presents, current_thread);
    if (dci_frame_time && now < dci_frame_time + period)
    {
        // The thread sleeps once the call that paces has returned to the
        // guest, before its next instruction (Application::Step): there it
        // stands between two instructions, with nothing of this call left on
        // the host stack, which is where a machine can be saved. A program spends most of its time in
        // this very wait.
        threads[current_thread].sleep_after_call = dci_frame_time + period;
        dci_frame_time += period;
    }
    else dci_frame_time = now;
}

// The driver learns that the caller is done; NEO98 publishes the
// surface, because the host image is a copy of it.
unsigned Application::DciEndAccess(unsigned surface)
{
    if (!surface || surface != dci_surface || !dci_surfaces) return D::Refused();
    User::ScreenChanged();
    if (dci_frame_access)
    {
        dci_frame_access = false;
        PaceFrame();
    }
    return D::DciOk;
}

// The structure is static, so this only counts the handout down.
unsigned Application::DciDestroySurface(unsigned surface)
{
    if (!surface || surface != dci_surface || !dci_surfaces) return D::Refused();
    --dci_surfaces;   //
    return D::DciOk;
}

// DeviceIoControl on the \\.\DDRAW.VXD handle. DirectDrawCreate only needs the
// open to work; the control codes belong to the video memory services of a
// real card. VWIN32 answers an unknown control code with ERROR_NOT_SUPPORTED,
// and so does this device.
unsigned Application::DirectDrawVxdControl(const unsigned *a)
{
    if (a[1] == ~0u)   // DIOC_CLOSEHANDLE
    {
        if (a[6] && virtual_memory.Access(a[6], 4, true)) Write32(a[6], 0);
        return 0;
    }
    // 1Ch and 1Dh lock and unlock the pages of a system memory surface for a
    // driver that blits from and to system memory (DDCAPS_CANBLTSYSMEM, the
    // HAL of src/x86_ddhal.cpp). DDRAW 0x2ff2a sends 14h bytes: the address,
    // the size, flags and two places for the answer; the second receives a
    // table with one entry per page, whose page bits DDRAW keeps and whose
    // first entry it gives the offset into the page (0x2ffbe). DDRAW 0x30053
    // sends 10h bytes with that table in the last dword to unlock. A card
    // wants physical pages for its DMA; this driver reads the surface through
    // its linear address, so the table names the linear pages.
    if (a[1] == 0x1c && gpu_backend && a[2] && a[3] >= 0x14 && virtual_memory.Access(a[2], 0x14, false))
    {
        const unsigned address = Read32(a[2]), size = Read32(a[2] + 4);
        const unsigned handle_at = Read32(a[2] + 12), table_at = Read32(a[2] + 16);
        const unsigned pages = ((address & 0xfff) + size + 0xfff) >> 12;
        const unsigned table = pages ? Allocate(pages * 4) : 0;
        if (!table || !virtual_memory.Access(handle_at, 4, true) || !virtual_memory.Access(table_at, 4, true))
        {
            if (table) Free(table);
            return 8;   // ERROR_NOT_ENOUGH_MEMORY
        }
        for (unsigned i = 0; i < pages; ++i) Write32(table + i * 4, (address & ~0xfffu) + i * 0x1000);
        Write32(handle_at, table);
        Write32(table_at, table);
        if (a[4] && a[5] >= 4 && virtual_memory.Access(a[4], 4, true)) Write32(a[4], 0);
        if (a[6] && virtual_memory.Access(a[6], 4, true)) Write32(a[6], 4);
        DriverTrace("DDRAW.VXD page lock", pages);
        return 0;
    }
    if (a[1] == 0x1d && gpu_backend && a[2] && a[3] >= 0x10 && virtual_memory.Access(a[2], 0x10, false))
    {
        const unsigned table = Read32(a[2] + 12);
        if (table) Free(table);
        if (a[4] && a[5] >= 4 && virtual_memory.Access(a[4], 4, true)) Write32(a[4], 0);
        if (a[6] && virtual_memory.Access(a[6], 4, true)) Write32(a[6], 4);
        return 0;
    }
    DriverTrace("DDRAW.VXD control", a[1]);
    return 50;
}

void Application::DriverTrace(const char *what, unsigned value)
{
    if (!log) return;
    char text[96] = "DDRAWDRIVER ";
    unsigned at = 12;
    for (const char *p = what; *p && at < sizeof text - 12; ++p) text[at++] = *p;
    text[at++] = ' '; text[at++] = '0'; text[at++] = 'x';
    for (unsigned i = 0; i < 8; ++i) text[at++] = "0123456789abcdef"[(value >> (28 - 4 * i)) & 15];
    text[at] = 0;
    log(text);
}
} // namespace X86
