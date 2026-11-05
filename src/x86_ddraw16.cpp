// SPDX-License-Identifier: GPL-2.0-or-later
// Marshal DDRAW flat-thunk arguments into WORDs and far pointers, preserving
// the caller's frame and pointer-translation mask.
#include "win98_ddraw16.h"
#include "win98_ddraw16_blit.h"
#include "win98_ddraw16_surface.h"
#include "win98_ddraw_driver.h"
#include "gdi.h"
#include "user.h"
#include "dibeng.h"
#include "win98_dib_stretch.h"
#include "x86_app.h"
#include <stdio.h>
namespace X86
{
namespace
{
namespace W = Win98DDraw16;
// Capacity and token range for the handle translation below.
constexpr unsigned WordHandleLimit = 0xf800, TokenTop = 0xffff;
enum Answer
{
    DibEngineProbe,   // is the display DIB engine based
    DciProbe,         // CreateDC("display") and QUERYESCSUPPORT
    DeleteProbeDC,    // the partner of the probe
    DciPrimary,       // the DCI primary surface
    DciSurfaceProbe,  // does that surface carry DCI_1632_ACCESS
    DciDestroy,       // the DestroySurface method of the surface
    DciEnd,           // the EndAccess method of the surface
    DciAccess,        // the BeginAccess bracket with its rectangle
    ObjectPrivate,    // GDI MakeObjectPrivate
    ModeX,            // the Mode X size check
    SelectRealize,   // GDISelectPalette and GDIRealizePalette
    DriverPalette,   // the SETPALETTE entry of the display driver
    DisplayMode,      // ChangeDisplaySettings for the HEL
    Zero,             // the original's own answer without a driver or a record
    Echo,             // a setter on 16-bit module state; the original leaves
                      // its argument in AX and the caller discards it
    NotHandled,       // a DDHAL callback the 16-bit driver did not take
    HelBlit,          // the HEL blit through the DIB engine
    SurfaceDc,        // the device context on a DirectDraw surface
    ReleaseSurfaceDc, // giving that context back
    FailGeneric,      // 0x80004005 as a dword result
    FailUnsupported,  // 0x8876024e as a dword result
    HalInfo,          // index 0x30, the driver's DDHALINFO for DDRAW 0x1cc45
    HalFns,           // index 0x31, the DDHALDDRAWFNS for a 16-bit driver
    Win98Version,     // the Windows 95 patch of a driver with a HAL
    DeviceConfig,     // the devnode and the resources of the display
    GammaProbe,       // does the driver of this DC carry a gamma ramp
    VisibleRegion,    // GDI InquireVisRgn: the visible region of a DC
};
struct Row
{
    unsigned char index, words;
    Answer answer;
    const char *what;
};
// The thunk catalogue covers the supported DDRAW calls. Unsupported targets
// still fail at the trap gate.
const Row rows[] = {
    {0, 1, Zero, "PDEVICE capability probe 6:1f04"},
    {1, 1, GammaProbe, "gamma ramp probe 6:1eaa"},
    {3, 6, DciAccess, "primary surface access bracket 4:02ec"},
    {4, 2, DciEnd, "surface access release 4:017e"},
    {5, 2, DciDestroy, "surface method 4:0142"},
    {6, 3, DciPrimary, "surface helper 4:038c"},
    {7, 1, DeleteProbeDC, "DCI probe context delete 4:0070"},
    {8, 0, DciProbe, "DCI display probe 4:0000"},
    {9, 1, DciSurfaceProbe, "surface record probe 4:03c2"},
    {10, 2, NotHandled, "DDHAL GetFlipStatus 3:02b4"},
    {23, 2, NotHandled, "DDHAL palette SetEntries 3:02f0"},
    {36, 2, ObjectPrivate, "MakeObjectPrivate 6:1c78"},
    {37, 8, DriverPalette, "display driver SetPalette 3:0330"},
    {38, 1, Zero, "palette helper 3:044e"},
    {39, 1, VisibleRegion, "InquireVisRgn 3:046e"},
    {40, 3, SelectRealize, "GDISelectPalette and GDIRealizePalette 3:030e"},
    {41, 16, HelBlit, "HEL blit through the DIB engine 6:1f3a"},
    {43, 4, Zero, "device context access bracket 6:1e2e"},
    {44, 1, ReleaseSurfaceDc, "surface device context release 6:1db4"},
    {45, 5, SurfaceDc, "surface device context 6:1c96"},
    {46, 2, Zero, "unused stub 6:1e04"},
    {47, 4, DisplayMode, "display mode change 5:001c"},
    {48, 2, HalInfo, "DDHALINFO of the driver with a HAL, src/x86_ddhal.cpp"},
    {49, 2, HalFns, "DDHALDDRAWFNS for a 16-bit driver, src/x86_ddhal.cpp"},
    {50, 2, Zero, "driver object list remove 2:07dc"},
    {51, 4, Zero, "driver object list register 2:086a"},
    {52, 2, FailGeneric, "Mode X flip 9:0038"},
    {53, 0, FailGeneric, "Mode X restore 9:0278"},
    {54, 3, ModeX, "Mode X set 9:018e"},
    {55, 4, Zero, "Mode X palette 9:0314"},
    {56, 0, Win98Version, "Windows version check 11:01b4"},
    {57, 0, DibEngineProbe, "DIB engine display probe 2:08bc"},
    {58, 1, Echo, "full-screen flag 9:017a"},
    {59, 4, Zero, "registry refresh rate 10:05d6"},
    {61, 6, DeviceConfig, "display devnode configuration 10:04b2"},
    {62, 4, Zero, "registry resolution 10:050a"},
    {63, 8, Zero, "CONFIGMG device query 10:02c4"},
    {64, 2, Zero, "registry MaxResolution 10:022a"},
};
const Row *Find(unsigned ordinal)
{
    for (const auto &row : rows)
        if (unsigned(row.index) + 1 == ordinal)
            return &row;
    return nullptr;
}
} // namespace

unsigned Application::DirectDraw16Words(unsigned ordinal)
{
    const Row *row = Find(ordinal);
    return row ? unsigned(row->words) : ~0u;
}

// The catalogue ordinals of DDRAW16.DLL, which are index byte plus one.
// `stack` addresses the last word the 32-bit stub pushed.
unsigned Application::DirectDraw16(unsigned ordinal, unsigned stack)
{
    const Row *row = Find(ordinal);
    if (!row)
        return 0;
    auto word = [&](unsigned at) {
        unsigned short value = 0;
        Read(stack + at, &value, 2);
        return unsigned(value);
    };
    unsigned result = 0;
    const char *reason = "";
    switch (row->answer)
    {
    case DibEngineProbe:
    {
        struct Screen final : W::Display
        {
            Application &app;
            explicit Screen(Application &a) : app(a) {}
            unsigned Caps1Of() override
            {
                const unsigned screen[] = {0};
                const unsigned hdc = Api("GetDC", screen);
                if (!hdc)
                    return 0;
                const unsigned query[] = {hdc, W::Caps1};
                const unsigned caps = app.GdiApi("GetDeviceCaps", query);
                const unsigned release[] = {0, hdc};
                Api("ReleaseDC", release);
                return caps;
            }
            unsigned Api(const char *name, const unsigned *a) { return app.Api(name, a); }
            // The answer comes out of dpCaps1 itself, where
            // C1_DIBENGINE now stands, so the substitute this entry carried is
            // gone. The second half of the original test reads the 16-bit
            // PDEVICE of the same DC and compares its signature with 0x5250;
            // there is no 16-bit PDEVICE here, and that half stays unanswered.
            bool DibEngineDriver() override { return false; }
        } screen(*this);
        result = W::DibEngineDisplay(screen);
        reason = "the display is the DIB engine port";
        break;
    }
    case DciProbe:
    {
        struct Probe final : W::DciProbe
        {
            Application &app;
            unsigned scratch = 0;
            explicit Probe(Application &a) : app(a) { scratch = a.Allocate(4096); }
            ~Probe() override { if (scratch) app.Free(scratch); }
            unsigned CreateDisplayDC() override
            {
                if (!scratch)
                    return 0;
                const char name[] = "display";
                app.Store(scratch, name, sizeof name);
                // and wrap the call in SetErrorMode, the way
                // DCIMAN does.
                const unsigned quiet[] = {0x8000};
                const unsigned previous = app.Api("SetErrorMode", quiet);
                const unsigned create[] = {scratch, 0, 0, 0};
                const unsigned hdc = app.GdiApi("CreateDCA", create);
                const unsigned restore[] = {previous};
                app.Api("SetErrorMode", restore);
                return hdc;
            }
            unsigned QueryDciCommand(unsigned hdc) override
            {
                if (!scratch)
                    return 0;
                const unsigned short command = (unsigned short)W::DciCommand;
                app.Store(scratch + 512, &command, 2);
                const unsigned arguments[] = {hdc, W::QueryEscSupport, 2, scratch + 512, 0};
                const unsigned answer = app.GdiApi("Escape", arguments);
                return answer == ~0u ? 0u : answer;
            }
            void DeleteDC(unsigned hdc) override
            {
                const unsigned arguments[] = {hdc};
                app.GdiApi("DeleteDC", arguments);
            }
        } probe(*this);
        result = DirectDraw16WordHandle(W::OpenDciDisplay(probe));
        reason = result ? "the display driver knows DCICOMMAND" : "no DCI on this display";
        break;
    }
    case Echo:
        result = word(0);
        reason = "16-bit module state this port does not keep";
        break;
    case GammaProbe:
    {
        // Windows 98 takes the device of the DC and answers one when the word at
        // +7Ch of it is not zero; DDRAW 0x451d6 asks it before it reads the
        // ramp with GetDeviceGammaRamp and, on success, sets
        // DDCAPS2_PRIMARYGAMMA itself (0x451ed). The display driver of this
        // port keeps a ramp in front of the host image (User::SetGammaRamp),
        // so every display context has one.
        const unsigned hdc = DirectDraw16NativeHandle(word(0));
        result = hdc && Gdi::ValidDC(hdc) ? 1 : 0;
        reason = result ? "the display driver has a gamma ramp" : "not a context of this port";
        break;
    }
    case VisibleRegion:
    {
        // hands GDI's InquireVisRgn through: the visible region of the
        // DC in screen coordinates. DDRAW 0x29cd5 asks it for the DC of the
        // window a clipper names (GetDCEx before it), reads it with
        // GetRegionData and deletes it (0x29d19), and a zero is
        // DDERR_GENERIC for every blit through that clipper. The region is
        // built here as Win98 keeps it: the client area of the window on the
        // screen, cut to the screen, less every visible window above it.
        const unsigned hdc = DirectDraw16NativeHandle(word(0));
        const unsigned window_of[] = {hdc};
        const unsigned hwnd = hdc && Gdi::ValidDC(hdc) ? Api("WindowFromDC", window_of) : 0;
        // The region leaves through AX, so GDI gives it a handle that fits a
        // word, as it does for every object made inside a 16-bit gate.
        struct Narrow { Narrow() { Gdi::NarrowHandles(true); } ~Narrow() { Gdi::NarrowHandles(false); } } narrow;
        unsigned visible = WindowVisibleRegion(hwnd);
        if (!visible && hdc && Gdi::ValidDC(hdc))
        {
            // A DC of the screen itself.
            int width = 0, height = 0, depth = 0;
            User::ScreenMode(width, height, depth);
            const unsigned r[] = {0, 0, unsigned(width), unsigned(height)};
            visible = Api("CreateRectRgn", r);
        }
        // The thunk hands back AX; the low word names the region as a
        // Win9x GDI handle does (gdi.cpp, Names).
        result = visible & 0xffff;
        reason = result ? "the visible region of the window, a copy the caller deletes" : "no window behind this context";
        break;
    }
    case DeleteProbeDC:
    {
        const unsigned hdc = DirectDraw16NativeHandle(word(0));
        const unsigned arguments[] = {hdc};
        if (hdc)
        {
            GdiApi("DeleteDC", arguments);
            DirectDraw16ReleaseHandle(word(0));
        }
        reason = hdc ? "probe context deleted" : "not a context of this port";
        break;
    }
    case DciPrimary:
    {
        // Three words: the caller's output-buffer far pointer and the
        // provider HDC. Create the primary surface and copy its description
        // to the buffer.
        const unsigned copy = selectors.Linear((word(2) << 16) | word(0));
        result = DirectDraw16CreatePrimary(DirectDraw16NativeHandle(word(4)), copy);
        reason = result ? "the display driver has no primary surface"
                        : "the primary surface of the display driver";
        break;
    }
    case DciSurfaceProbe:
    {
        // One word: HDC. Create the surface, validate 0x48 readable bytes,
        // check DCI_1632_ACCESS and release it. Return one if 16-bit callers
        // can access its pixels directly.
        result = DirectDraw16SurfaceHasSelector(DirectDraw16NativeHandle(word(0)));
        reason = result ? "the primary surface carries DCI_1632_ACCESS"
                        : "the primary surface has no 16-bit selector";
        break;
    }
    case DciDestroy:
    {
        // Two words: the surface far pointer. A copy has DestroySurface=-1
        // and references its backing surface through dwReserved2; invoke that
        // surface's method.
        const unsigned surface = DirectDraw16SurfaceOriginal(
            selectors.Linear((word(2) << 16) | word(0)));
        result = surface ? DciDestroySurface(surface) : Win98DDrawDriver::DciFailGeneric;
        reason = result ? "not a surface of this display driver"
                        : "the primary surface is given back";
        break;
    }
    case DciEnd:
    {
        // Two words: the surface far pointer. Resolve copies through
        // dwReserved2 and invoke the backing surface's EndAccess method.
        const unsigned surface = DirectDraw16SurfaceOriginal(
            selectors.Linear((word(2) << 16) | word(0)));
        result = surface ? DciEndAccess(surface) : Win98DDrawDriver::DciFailGeneric;
        reason = result ? "not a surface of this display driver"
                        : "the primary surface is closed again";
        break;
    }
    case DciAccess:
    {
        // Six words: height, width, top, left and surface far pointer. Build
        // a left/top/right/bottom rectangle for BeginAccess.
        auto signed_word = [&](unsigned at) {
            const unsigned v = word(at);
            return int(v & 0x8000 ? int(v) - 65536 : int(v));
        };
        const unsigned copy = selectors.Linear((word(10) << 16) | word(8));
        const unsigned surface = DirectDraw16SurfaceOriginal(copy);
        if (!surface) { result = Win98DDrawDriver::DciFailGeneric; reason = "not a surface of this display driver"; break; }
        result = DciBeginAccess(surface, signed_word(6), signed_word(4), signed_word(2),
                                signed_word(0));
        // Refresh the copy after successful access because the driver may
        // have changed the description.
        if (!result && copy != surface) DirectDraw16CopySurfaceInfo(surface, copy);
        reason = result ? "the display driver refused the access"
                        : "the primary surface is open for access";
        break;
    }
    case ObjectPrivate:
    {
        // hands the pair straight to GDI.463 and answers one only for a
        // one. NEO98 owns its GDI objects itself, so the ownership change is
        // nothing to do; a handle that is not an object still answers zero.
        const unsigned handle = word(2);
        const unsigned arguments[] = {handle};
        result = GdiApi("GetObjectType", arguments) ? 1u : 0u;
        reason = result ? "object marked private" : "not a GDI object";
        break;
    }
    case ModeX:
        // Windows 98 answers before any register is touched; a size the table does
        // The error flag requires VGA register programming, which is not implemented.
        result = W::SetModeX(word(4), word(2));
        reason = result == W::InvalidMode ? "no Mode X size" : "no VGA register access";
        break;
    case DisplayMode:
    {
        // is ChangeDisplaySettings(lpDevMode, dwFlags) and everything
        // the original wraps around it: the full-screen window
        // DynaResFullscreenWindow, the SYSTEM.INI lines and GDI.121 Death and
        // GDI.122 Resurrection around the change. None of those three exist
        // here; the compositor's screen surface is the display, and USER's own
        // entry already stops every painter while it swaps the surface.
        // Two callers, both in the HEL of the 32-bit half. EnumDisplayModes
        // confirms every mode of the list with CDS_TEST
        // and drops the ones that fail (wFlags |= 4, unsupported), and HEL
        // SetMode hands over the DEVMODE it built, with
        // the flags 0x80000000, or 0x80000004 with a null DEVMODE to go back.
        // 0x80000000 is DynaRes's own bit and means nothing to USER.
        const unsigned flags = word(0) | (word(2) << 16);
        const unsigned devmode = selectors.Linear((word(6) << 16) | word(4));
        // with a null DEVMODE goes back to the registry setting.
        result = ChangeDisplaySettings(devmode, flags & 0x7fffffffu);
        // Windows 98 answers DDERR_UNSUPPORTEDMODE when ChangeDisplaySettings
        // does not report success; HEL SetMode turns anything non-zero into
        // DDERR_GENERIC.
        if (result) result = W::UnsupportedMode;
        reason = result ? "the display has no such mode" : "display mode set";
        break;
    }
    case HalInfo:
        // The one argument is the far pointer of DDRAW's 0x1cc-byte frame
        // buffer, mapped by SMapLS_IP_EBP_8 in the stub.
        result = DirectDrawHalInfo(selectors.Linear((word(2) << 16) | word(0)));
        reason = result ? "DDHALINFO copied" : "no DirectDraw HAL in this driver";
        break;
    case HalFns:
        result = DirectDrawHalFns(selectors.Linear((word(2) << 16) | word(0)));
        reason = "no 16-bit driver calls these";
        break;
    case DeviceConfig:
    {
        // with three arguments: the size of the buffer ([bp+6], a
        // dword), the buffer ([bp+0ah]) and the device name ([bp+0eh]).
        // Windows 98 finds the devnode of the display in the Configuration
        // Manager, and Windows 98 reads its allocated logical configuration into
        // the buffer with CONFIGMG service 3Bh, CM_Get_Alloc_Log_Conf; the
        // answer is the devnode in DX:AX, and a buffer under 0D8h bytes
        // answers zero. DDRAW 0x2b021 asks this for a driver with a
        // HAL only, drops the driver object on a zero answer (0x2b02a,
        // DDERR_GENERIC) and reads wNumMemWindows out of the buffer to alias
        // the memory windows of the card through DDRAW.VXD.
        // This display has no memory windows, no ports and no interrupts:
        // its video memory is host memory the guest maps, so the
        // configuration is empty. The devnode is the one the original itself
        // takes when it has no name to look up, one. Only with a
        // rasterizer behind the driver, because only then is there a HAL to
        // ask; otherwise the answer stays the zero of a device without a
        // registry node.
        const unsigned size = word(0) | (word(2) << 16);
        const unsigned buffer = selectors.Linear((word(6) << 16) | word(4));
        if (!DirectDrawHalAvailable() || size < 0xd8 || !buffer || !virtual_memory.Access(buffer, 0xd8, true))
        {
            result = 0;
            reason = DirectDrawHalAvailable() ? "buffer too small for a CMCONFIG" : "no registry node without a HAL";
            break;
        }
        static const unsigned char empty[0xd8] = {};
        Store(buffer, empty, sizeof empty);
        result = 1;
        reason = "devnode one, a configuration without memory windows";
        break;
    }
    case Win98Version:
        // fills an OSVERSIONINFO and answers one when the major version
        // is above four or the minor version of 4.x is ten or more ( to); only on Windows 95 does it go on to patch a KERNEL routine
        // This is Windows 4.10, so the answer is
        // one and nothing is touched. DDRAW 0x1ca2f asks on the HAL path only.
        result = 1;
        reason = "Windows 4.10, nothing to patch";
        break;
    case SelectRealize:
    {
        // GDI.361 GDISelectPalette and GDI.362 GDIRealizePalette realize an
        // application palette into the device table. The three argument words
        // are flag, palette and DC. DirectDraw selects it with flag one and
        // then zero, corresponding to background-forced and normal selection.
        const unsigned hdc = word(4), palette = word(2), background = word(0);
        if (!hdc || !palette) { reason = "no palette to realize"; break; }
        const unsigned select[] = {hdc, palette, background};
        if (!GdiApi("SelectPalette", select)) { reason = "the palette is not one"; break; }
        const unsigned realize[] = {hdc};
        result = GdiApi("RealizePalette", realize);
        reason = "palette realized on the display";
        break;
    }
    case DriverPalette:
    {
        // Forward the palette to display-driver SETPALETTE (ordinal 22),
        // returning zero if unsupported. Neo98 uses the screen surface's
        // palette as its DAC table.
        // The thunk pushes HDC (WORD), first (DWORD), count (DWORD), mapped
        // entries (DWORD), then flag (WORD). Thus first is at +10 in the
        // final argument block; +8 is the high word of count. The entries
        // pointer is advanced by first*4. Reading +8 would corrupt palette
        // entry zero on partial updates.
        const unsigned start = word(10), count = word(6);
        const unsigned flat = selectors.Linear((word(4) << 16) | word(2));
        if (!flat || !count || count > 256) { reason = "no palette entries"; break; }
        unsigned entries[256] = {};
        if (!Read(flat, entries, count * 4)) { reason = "the entries are unreadable"; break; }
        result = Gdi::SetDisplayPalette(start, count, entries);
        reason = result ? "the display palette is programmed" : "the display has no palette";
        break;
    }
    case NotHandled:
        // The 16-bit HAL callback tables at DDRAW.DLL and 0x57598
        // are only installed with a driver behind them, so these two are not
        // reached; if one is, the honest answer is that the driver did not do
        // the work.
        result = W::DriverNotHandled;
        reason = "no 16-bit DirectDraw HAL";
        break;
    case SurfaceDc:
    {
        // Five words: palette far pointer, DDSURFACEDESC far pointer and
        // reference-device HDC.
        const unsigned palette = selectors.Linear((word(2) << 16) | word(0));
        const unsigned descriptor = selectors.Linear((word(6) << 16) | word(4));
        result = DirectDrawSurfaceDc(descriptor, palette, word(8));
        reason = result ? "a device context on the surface" : "no device context for this surface";
        break;
    }
    case ReleaseSurfaceDc:
        result = DirectDrawReleaseSurfaceDc(word(0));
        reason = result ? "the surface context is given back" : "not a surface context of this port";
        break;
    case HelBlit:
    {
        // Two equal blocks of eight words, destination first. See
        // win98_ddraw16_blit.h for field definitions.
        namespace B = Win98DDraw16Blit;
        auto plane = [&](unsigned at) {
            B::Plane p;
            p.width = int(short(word(at)));
            p.height = int(short(word(at + 2)));
            p.x = int(short(word(at + 4)));
            p.y = int(short(word(at + 6)));
            p.depth = word(at + 8) & 0xff;
            p.stride = word(at + 10);
            p.bits = word(at + 12) | (word(at + 14) << 16);
            return p;
        };
        const B::Plane source = plane(0), destination = plane(16);
        if (!B::Addressable(source) || !B::Addressable(destination))
        {
            result = B::Refused;
            reason = "the blit has no readable pixels";
            break;
        }
        // Both addresses are already linear, so no flat selector is needed.
        // Read the required guest rows, blit, and write them back. Include
        // the horizontal offset in the row width.
        auto rows = [&](const B::Plane &p, int count) {
            return unsigned(B::DeltaScan(p.stride) < 0 ? -B::DeltaScan(p.stride)
                                                       : B::DeltaScan(p.stride)) * unsigned(count);
        };
        const int source_rows = source.y + source.height, dest_rows = destination.y + destination.height;
        if (source.x < 0 || source.y < 0 || destination.x < 0 || destination.y < 0 ||
            source_rows <= 0 || dest_rows <= 0)
        {
            result = B::Refused;
            reason = "the blit rectangle is outside its surface";
            break;
        }
        const unsigned source_bytes = rows(source, source_rows), dest_bytes = rows(destination, dest_rows);
        if (!source_bytes || !dest_bytes || source_bytes > (64u << 20) || dest_bytes > (64u << 20))
        {
            result = B::Refused;
            reason = "the blit is larger than a surface can be";
            break;
        }
        unsigned char *source_bits = new unsigned char[source_bytes]{};
        unsigned char *dest_bits = new unsigned char[dest_bytes]{};
        bool ok = Read(source.bits, source_bits, source_bytes) &&
                  Read(destination.bits, dest_bits, dest_bytes);
        if (ok)
        {
            // Use the rectangle's required width and the number of loaded
            // rows. Clip through the rectangle rather than artificial surface
            // dimensions.
            DibEng::Bitmap from{source_bits, source.x + source.width, source_rows,
                                B::DeltaScan(source.stride), int(source.depth)};
            DibEng::Bitmap to{dest_bits, destination.x + destination.width, dest_rows,
                              B::DeltaScan(destination.stride), int(destination.depth)};
            const DibEng::Brush brush{};
            if (B::Stretches(source, destination))
            {
                // Apply the sampling rules in win98_dib_stretch.h. This path
                // needs only equal-depth blits because its caller accepts
                // matching 8-, 16- or 32-bit surfaces.
                struct Sink
                {
                    DibEng::Bitmap &to;
                    const DibEng::Bitmap &from;
                    int *AllocateSamples(unsigned count) { return new int[count]{}; }
                    void FreeSamples(int *samples) { delete[] samples; }
                    void Pixel(int x, int y, int sx, int sy)
                    {
                        DibEng::WritePixel(to, x, y, DibEng::ReadPixel(from, sx, sy));
                    }
                } sink{to, from};
                const Win98DibStretch::Request request{
                    from.width, from.height,
                    {source.x, source.y, source.x + source.width, source.y + source.height},
                    {destination.x, destination.y, destination.x + destination.width,
                     destination.y + destination.height},
                    {0, 0, to.width, to.height}};
                // Both surfaces have the same depth, restricted by the caller
                // to 8, 16 or 32 bits per pixel.
                if (source.depth != destination.depth) ok = false;
                else ok = Win98DibStretch::Raster(sink, request);
            }
            else if (!DibEng::BitBlt(to, destination.x, destination.y, destination.width,
                                     destination.height, &from, source.x, source.y, B::SrcCopy,
                                     brush))
                ok = false;
        }
        if (ok) ok = Store(destination.bits, dest_bits, dest_bytes);
        delete[] source_bits;
        delete[] dest_bits;
        result = ok ? B::Done : B::Refused;
        reason = ok ? "the DIB engine moved the pixels" : "the blit could not reach its pixels";
        break;
    }
    case FailGeneric:
        result = W::Generic;
        reason = "no Mode X in effect";
        break;
    case FailUnsupported:
        result = W::UnsupportedMode;
        reason = "no display mode change in this port";
        break;
    default:
        // The original's own answer when the thing it works on is not there:
        // an unreadable 16-bit object, a null callback field in the
        // surface structure, a display driver
        // without the SETPALETTE ordinal, a primary surface whose
        // PDEVICE signature does not match, an empty driver object
        // list and a device without a registry node

        reason = "nothing of this kind exists in this port";
        break;
    }
    if (log)
    {
        char line[256];
        snprintf(line, sizeof line, "DDRAW16 index=%u %s: %s result=%08x", row->index, row->what,
                 reason, result);
        log(line);
    }
    return result;
}

// Native GDI handles include a generation above bit 15, but 16-bit gates
// return only AX. A word-sized handle is its own token; larger handles need a
// mapping so truncation does not lose the object.
// Tokens occupy the top of the word range. Handles use generation*0x1000+slot
// with slots up to 0x800, so 0xf800 is the largest possible word-sized
// handle. Values above it are available for tokens.
unsigned Application::DirectDraw16WordHandle(unsigned handle)
{
    if (!handle || handle <= WordHandleLimit) return handle;
    const unsigned count = sizeof ddraw16_word_handles / sizeof ddraw16_word_handles[0];
    for (unsigned i = 0; i < count; ++i)
        if (ddraw16_word_handles[i] == handle) return TokenTop - i;
    for (unsigned i = 0; i < count; ++i)
        if (!ddraw16_word_handles[i]) { ddraw16_word_handles[i] = handle; return TokenTop - i; }
    return 0;
}

// The token of a handle that is gone is free again; a handle that fits a
// word has no token.
void Application::DirectDraw16ReleaseHandle(unsigned token)
{
    const unsigned count = sizeof ddraw16_word_handles / sizeof ddraw16_word_handles[0];
    if (token > WordHandleLimit && TokenTop - token < count)
        ddraw16_word_handles[TokenTop - token] = 0;
}

unsigned Application::DirectDraw16NativeHandle(unsigned token)
{
    const unsigned count = sizeof ddraw16_word_handles / sizeof ddraw16_word_handles[0];
    if (token <= WordHandleLimit) return token;
    const unsigned index = TokenTop - token;
    return index < count ? ddraw16_word_handles[index] : 0;
}

// Copy DCISURFACEINFO into the caller's buffer, retain the backing surface in
// dwReserved2 and mark the copy with DestroySurface=-1. A selector, when
// present, contributes its base to dwOffSurface and is cleared. Our driver
// already returns a linear address, so clear DCI_1632_ACCESS in the copy.
// Accept structure sizes from 0x48 through 0x54 bytes.
unsigned Application::DirectDraw16CopySurfaceInfo(unsigned source, unsigned copy)
{
    namespace D = Win98DDrawDriver;
    if (!source || !copy) return D::DciFailGeneric;   // Both buffers are required.
    unsigned size = Read32(source);
    if (size < 0x48) { size = 0x48; Write32(source, size); }   //
    if (size > 0x54) return D::DciFailGeneric;                 //
    unsigned char bytes[0x54] = {};
    if (!Read(source, bytes, size) || !Store(copy, bytes, size)) return D::DciFailGeneric;
    Write32(copy + 0x34, source);                              //
    if (Read32(source + 0x3c))                                 //
    {
        Write32(copy + 0x3c, ~0u);
        Write32(copy + 0x40, ~0u);
    }
    Write32(copy + 0x44, ~0u);                                 //
    unsigned short selector = 0;
    Read(source + 0x2c, &selector, 2);
    if (!selector) Write32(copy + 4, Read32(copy + 4) & ~0x40u);   //
    return 0;
}

// DestroySurface=-1 identifies a copy; dwReserved2 points to its backing
// surface.
unsigned Application::DirectDraw16SurfaceOriginal(unsigned surface)
{
    if (!surface) return 0;
    return Read32(surface + 0x44) == ~0u ? Read32(surface + 0x34) : surface;
}

// Build DCICREATEINPUT with command 1, version 0x100, requested capability
// 0x10 and other fields zero. Send DCICOMMAND through the display driver's
// Escape interface. Copy the returned DCISURFACEINFO into the caller's
// buffer.
unsigned Application::DirectDraw16CreatePrimary(unsigned hdc, unsigned copy)
{
    namespace D = Win98DDrawDriver;
    if (!copy) return D::DciFailGeneric;
    const unsigned scratch = Allocate(4096);
    if (!scratch) return D::DciFailGeneric;
    unsigned char input[0x38] = {};
    auto put32 = [&](unsigned at, unsigned v) {
        for (unsigned i = 0; i < 4; ++i) input[at + i] = (v >> (8 * i)) & 255;
    };
    put32(0x00, D::DciCreatePrimarySurface);   //
    put32(0x0c, 0x100);                        // dwVersion
    put32(0x2c, 0x10);                         // DCI_VISIBLE
    unsigned answer = D::DciFailGeneric;
    if (Store(scratch, input, sizeof input))
    {
        Write32(scratch + 0x100, 0);
        // The 16-bit Escape forwards arbitrary command numbers to the display
        // driver. The 32-bit GDI escape filter rejects DCICOMMAND, so bypass
        // that filter here.
        if (Gdi::ValidDC(hdc))
            answer = DciEscapeApi(hdc, sizeof input, scratch, 0, scratch + 0x100) & 0xffffu;
        if (!answer) answer = DirectDraw16CopySurfaceInfo(Read32(scratch + 0x100), copy);
    }
    Free(scratch);
    return answer;
}

// Create the surface, validate 0x48 readable bytes, inspect DCI_1632_ACCESS
// and release it. Return a boolean, not an error code.
unsigned Application::DirectDraw16SurfaceHasSelector(unsigned hdc)
{
    const unsigned copy = Allocate(4096);
    if (!copy) return 0;
    unsigned answer = 0;
    if (!DirectDraw16CreatePrimary(hdc, copy))
    {
        const unsigned original = DirectDraw16SurfaceOriginal(copy);
        if (original && Read32(original) >= 0x48) answer = (Read32(copy + 4) & 0x40) ? 1u : 0u;
        if (original) DciDestroySurface(original);   //
    }
    Free(copy);
    return answer;
}

// A surface DC must share its pixels with DirectDraw Lock so writes remain
// visible between GetDC and ReleaseDC. The screen lease exposes the same
// bytes to host and guest, allowing the primary DC to use them directly.
// Gdi::SameSurface recognizes it as a display DC and publishes drawing.
// HEL offscreen surfaces reside in guest allocations without a host pointer.
// Copy their pixels in on GetDC and back on ReleaseDC.
unsigned Application::DirectDrawSurfaceDc(unsigned descriptor, unsigned palette,
                                          unsigned reference)
{
    namespace S = Win98DDraw16Surface;
    (void)reference;
    if (!descriptor) return 0;
    auto field = [&](unsigned at) { unsigned v = 0; Read(descriptor + at, &v, 4); return v; };
    const unsigned pixel_flags = field(S::PixelFlags), bit_count = field(S::BitCount);
    if (!S::Describes(pixel_flags)) return 0;              //
    int screen_width = 0, screen_height = 0, screen_depth = 0;
    User::ScreenMode(screen_width, screen_height, screen_depth);
    if (!S::ReferenceFits(bit_count, palette != 0, unsigned(screen_depth)))
        return 0;                                          // Invalid reference device.
    const unsigned width = field(S::Width), height = field(S::Height);
    const unsigned pitch = field(S::Pitch), bits = field(S::Surface);
    if (!width || !height || !pitch || !bits) return 0;
    if (bit_count != 8 && bit_count != 16 && bit_count != 24 && bit_count != 32) return 0;
    if (width > 4096 || height > 4096) return 0;
    const unsigned bytes = S::Bytes(pitch, height);
    if (!bytes || bytes > (64u << 20)) return 0;
    const bool is_565 = S::Is565(bit_count, field(S::RedMask));
    const bool own = S::WantsOwnDc(field(S::Caps), ddraw_surface_busy);   //
    // A screen surface already has its pixels in place.
    unsigned char *shared = nullptr;
    if (bits && bits == User::ScreenGuestAddress() && int(width) == screen_width &&
        int(height) == screen_height && bit_count == unsigned(screen_depth) &&
        int(pitch) == User::ScreenStride() && bytes <= User::ScreenBytes())
        shared = const_cast<unsigned char *>(User::ScreenBits());
    // Reuse the cached DC while depth and the RGB565 flag match.
    if (!own)
        for (auto &slot : ddraw_surface_dcs)
        {
            if (!slot.used || !slot.cached) continue;
            if (!S::CacheFits(slot.depth, slot.wide, bit_count, is_565))
            {
                // A different depth discards the cached context.
                Gdi::DeleteDC(slot.dc);
                if (!slot.shared) delete[] slot.host;
                slot = Application::DDrawSurfaceDc{};
                break;
            }
            // Attach the new surface to the existing DC: load its bytes and
            // retarget the DC surface.
            if (shared)
            {
                if (!slot.shared) delete[] slot.host;
                slot.host = shared;
                slot.bytes = bytes;
                slot.shared = true;
            }
            else
            {
                if (slot.shared) { slot.host = nullptr; slot.bytes = 0; slot.shared = false; }
                if (slot.bytes != bytes)
                {
                    unsigned char *grown = new unsigned char[bytes]{};
                    delete[] slot.host;
                    slot.host = grown;
                    slot.bytes = bytes;
                }
                if (!Read(bits, slot.host, bytes)) return 0;
            }
            const DibEng::Bitmap again{slot.host, int(width), int(height), int(pitch), int(bit_count)};
            bool retargeted = false;
            if (bit_count == 8)
            {
                unsigned table[256] = {};
                if (palette) { unsigned e[256] = {}; if (Read(palette, e, sizeof e)) for (unsigned i = 0; i < 256; ++i) table[i] = e[i] & 0xffffffu; }
                else if (const unsigned *screen = User::ScreenColors()) for (unsigned i = 0; i < 256; ++i) table[i] = screen[i];
                retargeted = Gdi::ResizeSurfaceDC(slot.dc, again, table, 256);
            }
            else
                retargeted = Gdi::ResizeSurfaceDC(slot.dc, again);
            if (!retargeted) return 0;                      //
            slot.guest = bits; slot.width = int(width); slot.height = int(height);
            slot.stride = int(pitch);
            ++ddraw_surface_busy;                           //
            return slot.dc;
        }
    Application::DDrawSurfaceDc *free_slot = nullptr;
    for (auto &slot : ddraw_surface_dcs)
        if (!slot.used) { free_slot = &slot; break; }
    if (!free_slot) return 0;                               //
    unsigned char *host = shared;
    if (!host)
    {
        host = new unsigned char[bytes]{};
        if (!Read(bits, host, bytes)) { delete[] host; return 0; }   //
    }
    unsigned colors[256] = {};
    unsigned color_count = 0;
    if (bit_count == 8)
    {
        color_count = 256;
        if (palette)
        {
            // Call SetDIBColorTable with all 256 entries.
            unsigned entries[256] = {};
            if (Read(palette, entries, sizeof entries))
                for (unsigned i = 0; i < 256; ++i)
                    colors[i] = entries[i] & 0xffffffu;
        }
        else
        {
            // Without an explicit palette, use the reference device's table,
            // which here is the display.
            const unsigned *screen = User::ScreenColors();
            if (screen) for (unsigned i = 0; i < 256; ++i) colors[i] = screen[i];
        }
    }
    const DibEng::Bitmap bitmap{host, int(width), int(height), int(pitch), int(bit_count)};
    const unsigned dc = Gdi::CreateSurfaceDC(bitmap, color_count ? colors : nullptr, color_count, is_565);
    if (!dc) { if (!shared) delete[] host; return 0; }       //
    free_slot->dc = dc; free_slot->guest = bits; free_slot->host = host;
    free_slot->bytes = bytes; free_slot->depth = bit_count;
    free_slot->width = int(width); free_slot->height = int(height);
    free_slot->stride = int(pitch); free_slot->used = true; free_slot->wide = is_565;
    free_slot->cached = !own; free_slot->shared = shared != nullptr;
    if (!own) ++ddraw_surface_busy;                          //
    return dc;
}

// Detach the pixels, write them back to guest memory and destroy an unshared
// DC.
unsigned Application::DirectDrawReleaseSurfaceDc(unsigned dc)
{
    if (!dc) return 0;
    // Resolve the word-sized HDC through Gdi::FullHandle. After enough
    // generations the full handle exceeds a word; failing to resolve it would
    // leak DCs until DD16_GetDC exhausts its slots.
    if (const unsigned full = Gdi::FullHandle(dc)) dc = full;
    for (auto &slot : ddraw_surface_dcs)
    {
        if (!slot.used || slot.dc != dc) continue;
        // Detach the surface. Only guest-memory surfaces need writeback;
        // storage owned directly by the DC does not.
        if (!slot.shared) Store(slot.guest, slot.host, slot.bytes);
        // The bytes may be the ones the monitor shows, on the HEL path the
        // shared screen surface, on the HAL path a video memory surface the
        // last flip put on the scanout. Either way the host image follows.
        User::ScreenChanged();
        if (!slot.cached)
        {
            // Destroy an unshared DC.
            Gdi::DeleteDC(slot.dc);                                    //
            if (!slot.shared) delete[] slot.host;
            slot = Application::DDrawSurfaceDc{};
            return 1;
        }
        // Keep the cached DC and decrement its reference count. Discard it
        // only when the pixel depth changes.
        if (ddraw_surface_busy) --ddraw_surface_busy;
        slot.guest = 0;
        return 1;
    }
    return 0;
}
unsigned Application::WindowVisibleRegion(unsigned hwnd)
{
    if (!hwnd || !User::IsWindow(hwnd))
        return 0;
    const unsigned scratch = Allocate(32);
    if (!scratch)
        return 0;
    auto region = [&](int left, int top, int right, int bottom) {
        const unsigned r[] = {unsigned(left), unsigned(top), unsigned(right), unsigned(bottom)};
        return Api("CreateRectRgn", r);
    };
    int width = 0, height = 0, depth = 0;
    User::ScreenMode(width, height, depth);
    const unsigned client[] = {hwnd, scratch};
    Api("GetClientRect", client);
    Write32(scratch + 16, 0);
    Write32(scratch + 20, 0);
    const unsigned origin[] = {hwnd, scratch + 16};
    Api("ClientToScreen", origin);
    const int x = int(Read32(scratch + 16)), y = int(Read32(scratch + 20));
    unsigned visible = region(x + int(Read32(scratch)), y + int(Read32(scratch + 4)), x + int(Read32(scratch + 8)),
                              y + int(Read32(scratch + 12)));
    const unsigned screen = region(0, 0, width, height);
    const unsigned both[] = {visible, visible, screen, 1};   // RGN_AND
    Api("CombineRgn", both);
    const unsigned drop_screen[] = {screen};
    Api("DeleteObject", drop_screen);
    // Every visible top-level window above the window's own.
    const unsigned root_of[] = {hwnd, 2};   // GA_ROOT
    unsigned above = Api("GetAncestor", root_of);
    for (unsigned guard = 0; above && guard < 256; ++guard)
    {
        const unsigned previous[] = {above, 3};   // GW_HWNDPREV
        above = Api("GetWindow", previous);
        if (!above) break;
        const unsigned shown[] = {above};
        if (!Api("IsWindowVisible", shown)) continue;
        const unsigned frame[] = {above, scratch};
        if (!Api("GetWindowRect", frame)) continue;
        const unsigned cover = region(int(Read32(scratch)), int(Read32(scratch + 4)), int(Read32(scratch + 8)),
                                      int(Read32(scratch + 12)));
        const unsigned cut[] = {visible, visible, cover, 4};   // RGN_DIFF
        Api("CombineRgn", cut);
        const unsigned drop[] = {cover};
        Api("DeleteObject", drop);
    }
    Free(scratch);
    return visible;
}
} // namespace X86
