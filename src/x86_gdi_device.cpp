// SPDX-License-Identifier: GPL-2.0-or-later
// The device level GDI32 exports of the import list: Escape and ExtEscape for a
// screen device context, the print document calls, the pixel format group and
// the gamma ramp. None of them opens a new capability; each one reproduces the
// answer Windows 98 gives on a machine with the display driver and without a
// printer, an OpenGL provider or a gamma capable driver.
#include "user.h"
#include "x86_app.h"
#include <string.h>
namespace X86 {
namespace {
// GDI32 reads a class byte for nEscape 1..19 out of the table
// and picks one of nine targets from the table. Three of
// those nine are the version gate, one is the rejection, the other five all reach the thunk.
constexpr unsigned char EscapeClass[19] = {0, 0, 1, 8, 2, 8, 8, 3, 4, 4,
                                           4, 5, 5, 5, 8, 8, 6, 8, 7};
enum EscapeRoute { EscapeDriver, EscapeVersion, EscapeReject };
constexpr unsigned char EscapeTargets[9] = {EscapeVersion, EscapeDriver, EscapeDriver,
                                            EscapeDriver,  EscapeVersion, EscapeDriver,
                                            EscapeVersion, EscapeDriver,  EscapeReject};
// Windows 98 compares the numbers above 19 one by one.
bool EscapeNumberPasses(unsigned escape)
{
    switch (escape)
    {
    case 0x25: case 0x26: case 0x2a: case 0x202: case 0x0c01:
    case 0x1000: case 0x1001: case 0x1002:
    case 0x100e: case 0x100f: case 0x1010:
    case 0x1013: case 0x1014:
        return true;
    default:
        return false;
    }
}
unsigned EscapeRouteOf(unsigned escape)
{
    if (escape >= 1 && escape <= 19)
        return EscapeTargets[EscapeClass[escape - 1]];
    if (escape == 25)
        return EscapeVersion;
    return EscapeNumberPasses(escape) ? EscapeDriver : EscapeReject;
}
// DIBENG knows five numbers and the marker 5250h; Windows 98 answers zero
// for everything else. Only 8 and 0C01h of these ever get past GDI32.
bool DriverKnowsEscape(unsigned escape)
{
    switch (escape)
    {
    case 8: case 0x27: case 0x28: case 0x0c01: case 0x0c03: case 0x5250:
        return true;
    default:
        return false;
    }
}
} // namespace

// The expected Windows version of the running image, the value KERNEL32
// GetProcessDword(0,-2Ch) hands GDI32. GDI32 compares it against
// 400h, the original STARTDOC against 30Ah.
unsigned Application::ExpectedWindowsVersion()
{
    const auto v = image.SubsystemVersion();
    return ((v >> 8) & 0xff00) | (v & 255);
}

// Escape and ExtEscape. The 32-bit side is the
// gatekeeper: it answers most printer numbers itself and never reaches the
// driver. What does reach the driver ends in DIBENG's Control entry, and that
// answers zero for a number it does not know. Neither path touches LastError.
unsigned Application::GdiEscapeApi(ApiName name, const unsigned *a)
{
    const bool extended = API_IS(name, "ExtEscape");
    const unsigned escape = a[1];
    if (!Gdi::ValidDC(a[0]))
        return ~0u; // GDI answers 0FFFFh for a handle it rejects
    const unsigned route = EscapeRouteOf(escape);
    // from the expected version 4.00 on, the document escapes are
    // refused in the 32-bit layer.
    if (route == EscapeVersion && ExpectedWindowsVersion() >= 0x400)
        return ~0u;
    if (route == EscapeReject && !extended)
    {
        // Refused QUERYESCSUPPORT requests return zero; other unsupported
        // escape numbers return -1.
        return escape == 8 ? 0 : ~0u;
    }
    // GDI: an information context answers zero before the driver runs.
    if (Gdi::InformationDC(a[0]))
        return 0;
    // A printer driver answers for itself (src/x86_print_document.cpp).
    if (PrinterDC(a[0]))
        return PrinterEscape(a[0], escape, a[2], a[3], extended ? a[5] : a[4]);
    if (escape == 8)
    {
        // QUERYESCSUPPORT. DIBENG answers the marker 0x5250 for the
        // numbers its own Control entry knows, one for the marker itself and
        // zero for the rest.
        unsigned queried = 0;
        if (!a[3] || !Read(a[3], &queried, a[2] >= 4 ? 4 : 2))
            return 0;
        if (a[2] < 4)
            queried &= 65535;
        // The OpenGL driver answers for OPENGL_GETINFO (src/x86_opengl.cpp).
        if (OpenGLEscapeSupported(queried))
            return 1;
        // DIBENG does not answer one. See src/win98_ddraw_driver.h.
        return DriverEscapeSupport(queried, DriverKnowsEscape(queried));
    }
    if (escape == 0x0c03)
    {
        // DCICOMMAND. DIBENG forwards it to its own DCI half at
        // instead of falling through to a zero result.
        // Escape has one output pointer and no output size, ExtEscape both.
        return DciEscapeApi(a[0], a[2], a[3], extended ? a[4] : 0, extended ? a[5] : a[4]);
    }
    if (escape == 0x1101 && extended)
    {
        // OPENGL_GETINFO: the driver names its OpenGL driver.
        return OpenGLEscape(a[2], a[3], a[4], a[5]);
    }
    if (escape == 0x0c01)
    {
        // QUERYDIBSUPPORT, DIBENG. Eight bits per pixel add the
        // palette bit to the three the engine always offers.
        return Gdi::DeviceCaps(a[0], 12) == 8 ? 0x0f : 7;
    }
    return 0;
}

// The print document group. Windows 98 refuses every one of them on a display
// device context, and it does so without touching LastError.
// GDI reads GDIINFO.dpTechnology out of DC+38h and compares it with 1,
// DT_RASDISPLAY. For the display it asks KERNEL GetExpWinVer and answers 1 only
// up to 3.0A; from 3.10 on it answers 0FFFFh, SP_ERROR. StartPage,
// EndPage, EndDoc and AbortDoc do not look at the
// device type at all. They test the document bits at DC+100h and DC+101h, and
// those bits only ever get set by a StartDoc that worked. SP_APPABORT and
// SP_USERABORT come from the spooler return path, which a display context never
// enters.
unsigned Application::GdiDocumentApi(ApiName name, const unsigned *a)
{
    if (!Gdi::ValidDC(a[0]))
        return ~0u;
    // A DC over a printer driver (src/x86_print_document.cpp).
    if (PrinterDC(a[0]))
        return PrinterDocumentApi(name, a);
    // GDI stores the callback in DC+D0h without any device check and
    // answers TRUE. NEO98 has no printer to call it back from, so it only
    // records that the application set one.
    if (API_IS(name, "SetAbortProc"))
    {
        abort_procedures = a[1];
        return 1;
    }
    // GDI takes the same dpTechnology branch; the display path at
    // needs the dynamic resolution bit at GDIINFO+5Eh, which the DIB
    // engine does not carry, and answers NULL.
    if (API_IS(name, "ResetDCA"))
        return 0;
    if (API_IS(name, "StartDocA") && ExpectedWindowsVersion() <= 0x30a)
        return 1; // the compatibility answer for a Windows 3.0 image
    return ~0u;   // SP_ERROR
}

// Pixel-format APIs use matching wgl entries only when OPENGL32 is already
// loaded. Failure to find the module or entry returns zero without changing
// LastError.
unsigned Application::GdiPixelFormatApi(ApiName name, const unsigned *arguments)
{
    const char *entry = nullptr;
    unsigned count = 0;
    if (API_IS(name, "ChoosePixelFormat")) { entry = "wglChoosePixelFormat"; count = 2; }
    else if (API_IS(name, "DescribePixelFormat")) { entry = "wglDescribePixelFormat"; count = 4; }
    else if (API_IS(name, "SetPixelFormat")) { entry = "wglSetPixelFormat"; count = 3; }
    else if (API_IS(name, "GetPixelFormat")) { entry = "wglGetPixelFormat"; count = 1; }
    else if (API_IS(name, "SwapBuffers")) { entry = "wglSwapBuffers"; count = 1; }
    if (!entry)
        return 0;
    const unsigned saved_error = last_error;
    const unsigned scratch = Allocate(64);
    if (!scratch)
        return 0;
    static const char module_name[] = "OPENGL32";
    Store(scratch, module_name, sizeof module_name);
    const unsigned module = Api("GetModuleHandleA", &scratch);
    unsigned procedure = 0;
    if (module)
    {
        unsigned length = 0; while (entry[length]) ++length;
        Store(scratch, entry, length + 1);
        const unsigned query[] = {module, scratch};
        procedure = Api("GetProcAddress", query);
    }
    Free(scratch);
    last_error = saved_error;
    return procedure ? Call(procedure, arguments, count) : 0;
}

// The gamma ramp. GDI and Windows 98 check the buffer with IsBadWritePtr
// or IsBadReadPtr over 600h bytes, three tables of 256 WORDs, and then the
// capability bit 20h at GDIINFO+5Eh. A driver without that bit ends
// with SetLastError(50) and zero. This display driver has the bit: its ramp
// sits in front of the host image (User::SetGammaRamp), the way a card's
// RAMDAC sits in front of the monitor, and DDRAW's IDirectDrawGammaControl
// reaches it through here once the HAL reports DDCAPS2_PRIMARYGAMMA.
unsigned Application::GdiGammaApi(ApiName name, const unsigned *a)
{
    if (!Gdi::ValidDC(a[0]))
    {
        last_error = 87;
        return 0;
    }
    const bool write = API_IS(name, "GetDeviceGammaRamp");
    if (!a[1] || !virtual_memory.Access(a[1], 1, write) ||
        !virtual_memory.Access(a[1] + 0x5ff, 1, write))
    {
        last_error = 87;
        return 0;
    }
    unsigned short ramp[768];
    if (write)
    {
        User::GammaRamp(ramp);
        return Store(a[1], ramp, sizeof ramp) ? 1 : 0;
    }
    if (!Read(a[1], ramp, sizeof ramp)) return 0;
    User::SetGammaRamp(ramp);
    return 1;
}

// Logical colour spaces, profile names and ICM enablement belong to DC state.
// Native Win98 can enable ICM even when loading its colour transform fails.
unsigned Application::GdiIcmApi(ApiName name, const unsigned *a)
{
    if (API_IS(name, "CreateColorSpaceA"))
    {
        unsigned char logical[328];
        if (!a[0] || !Read(a[0], logical, sizeof logical)) { last_error = 87; return 0; }
        const unsigned h = Gdi::CreateColorSpace(logical);
        if (!h) last_error = 87;
        return h;
    }
    if (API_IS(name, "DeleteColorSpace")) return Gdi::DeleteColorSpace(a[0]);
    if (API_IS(name, "GetLogColorSpaceA"))
    {
        unsigned char logical[328];
        if (!a[1] || a[2] < sizeof logical || !Gdi::GetLogColorSpace(a[0], logical))
        { last_error = 87; return 0; }
        return Store(a[1], logical, sizeof logical);
    }
    if (!Gdi::ValidDC(a[0]))
    {
        last_error = API_IS(name, "SetICMMode") ? 87 : 6;
        return 0;
    }
    if (API_IS(name, "SetICMMode")) return Gdi::ColorMode(a[0], a[1]);
    char profile[260] = {};
    const char *selected = Gdi::ColorProfile(a[0]);
    if (selected && *selected) strcpy(profile, selected);
    else
    {
        const unsigned scratch = Allocate(260);
        if (!scratch) { last_error = 8; return 0; }
        const unsigned args[] = {scratch, 260};
        const unsigned length = Api("GetSystemDirectoryA", args);
        const char suffix[] = "\\COLOR\\sRGB Color Space Profile.icm";
        const bool ok = length && length + sizeof suffix <= sizeof profile && Read(scratch, profile, length);
        Free(scratch);
        if (!ok) { last_error = 87; return 0; }
        memcpy(profile + length, suffix, sizeof suffix);
    }
    if (API_IS(name, "GetColorSpace")) return Gdi::GetColorSpace(a[0], profile);
    if (API_IS(name, "SetColorSpace")) return Gdi::SetColorSpace(a[0], a[1], profile);
    if (API_IS(name, "GetICMProfileA"))
    {
        unsigned capacity = 0;
        if (!a[1] || !Read(a[1], &capacity, 4)) { last_error = 87; return 0; }
        const unsigned needed = strlen(profile) + 1;
        if (!Store(a[1], &needed, 4)) return 0;
        if (!a[2] || capacity < needed) { last_error = 122; return 0; }
        return Store(a[2], profile, needed);
    }
    return 0;
}
} // namespace X86
