// SPDX-License-Identifier: GPL-2.0-or-later
// DirectDraw display-driver interface: DCICOMMAND escapes, DDHAL requests,
// and DDRAW.VXD. Validate display depth, read compatibility settings, connect
// DDHELP, open the device, and negotiate DCI support. Drivers without a
// compatible HAL use software emulation. Bind guest operations in
// src/x86_ddraw_driver.cpp.
#pragma once
namespace Win98DDrawDriver
{
using U = unsigned;

// GDI escape numbers. DCICOMMAND is 3075; the DIB engine also knows the marker
// 0x5250, which is the ASCII "RP" its own Control entry answers with.
enum : U
{
    QueryEscSupport = 8,
    DciCommand = 0x0c03,
    QueryDibSupport = 0x0c01,
    DibEngineMarker = 0x5250,
};

// DIBENG QUERYESCSUPPORT returns 0x5250 for supported control numbers (8,
// 0x27, 0x28, 0x0c01, 0x0c03), one for 0x5250 itself, and zero otherwise.
inline U EscapeSupport(U escape, U driver_answer)
{
    if (escape == DibEngineMarker)
        return 1;
    return driver_answer ? U(DibEngineMarker) : 0;
}

// The commands that travel in the DCICMD header of DCICOMMAND. 1 to 9 are DCI
// proper, 10 to 13 are the DirectDraw HAL negotiation DDRAW.DLL adds.
enum : U
{
    DciCreatePrimarySurface = 1,
    DciCreateOffscreenSurface = 2,
    DciCreateOverlaySurface = 3,
    DciEnum = 4,
    DdCreateDriverObject = 10,
    DdGet32BitDriverName = 11,
    DdNewCallbackFns = 12,
    DdVersionInfo = 13,
};

// The DCICMD header DDRAW.DLL pushes, 0x14 bytes.
struct Command
{
    U command = 0, parameter1 = 0, parameter2 = 0, version = 0, reserved = 0;
};

// The DIB engine implements only DCICREATEPRIMARYSURFACE. Other DCI and DDHAL
// commands return -1, causing DirectDrawCreate to use emulation when no HAL
// driver is available.
inline bool DibEngineServes(U command) { return command == DciCreatePrimarySurface; }
inline U Refused() { return 0xffffu; }

// Create a shared 0x48-byte DCISURFACEINFO from the display device. Reject
// flag 0x20 or a missing surface selector. Clear the caller's output pointer
// before validation, then return the shared structure and increment its
// reference count. Fill dimensions, stride, depth, masks, memory location,
// and access callbacks from the device. BeginAccess rejects disabled or
// changed surfaces; EndAccess notifies the driver. DestroySurface decrements
// the count.
struct SurfaceInfo
{
    U size = 0x48;
    U caps = 0x10;
    U compression = 0;
    U mask[3] = {0, 0, 0};
    U width = 0;
    U height = 0;
    int stride = 0;
    U bit_count = 0;
    U offset = 0;        // dwOffSurface
    U selector = 0;      // wSelSurface
    U device = 0;        // dwReserved1, the device this surface belongs to
    U begin_access = 0, end_access = 0, destroy_surface = 0;
};
// DCI error codes, dciddi.h. The DIB engine only ever produces the first two.
enum : U
{
    DciOk = 0,
    DciFailGeneric = 0xffffu,        // -1 as the 16-bit word the escape returns
    DciFailUnsupportedVersion = 0xfffeu,
    DciFailInvalidSurface = 0xfffdu,
    DciFailUnsupported = 0xfffcu,
};
// Write the 0x48-byte structure read by the caller.
inline void Serialize(const SurfaceInfo &s, unsigned char *out)
{
    auto put32 = [&](U at, U v) { for (U i = 0; i < 4; ++i) out[at + i] = (v >> (8 * i)) & 255; };
    for (U i = 0; i < 0x48; ++i) out[i] = 0;
    put32(0x00, s.size);
    put32(0x04, s.caps);
    put32(0x08, s.compression);
    put32(0x0c, s.mask[0]);
    put32(0x10, s.mask[1]);
    put32(0x14, s.mask[2]);
    put32(0x18, s.width);
    put32(0x1c, s.height);
    put32(0x20, U(s.stride));
    put32(0x24, s.bit_count);
    put32(0x28, s.offset);
    out[0x2c] = s.selector & 255;
    out[0x2d] = (s.selector >> 8) & 255;
    put32(0x30, s.device);
    put32(0x3c, s.begin_access);
    put32(0x40, s.end_access);
    put32(0x44, s.destroy_surface);
}

// The device \\.\DDRAW.VXD. DDRAW.DLL opens it and keeps the handle
// in the DirectDrawGlobal at +0x44 (0x1d50c). The open is the whole of what
// DirectDrawCreate needs from it; the control codes belong to the paths that
// allocate video memory on a real card.
inline bool DeviceName(const char *name)
{
    if (!name) return false;
    const char *want = "DDRAW.VXD";
    for (U i = 0;; ++i)
    {
        const char a = name[i] >= 'a' && name[i] <= 'z' ? char(name[i] - 32) : name[i];
        if (a != want[i]) return false;
        if (!want[i]) return true;
    }
}
} // namespace Win98DDrawDriver
