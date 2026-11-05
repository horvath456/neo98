// SPDX-License-Identifier: GPL-2.0-or-later
#include "linux_shared_memory.h"
#include "session_dib_memory.h"
// New x86 ABI adapter for native GDI objects and implemented drawing paths.
#include "x86_app.h"
#include "win98_dc_information.h"
#include "user.h"
#include "win98_broadcast.h"
#include "win98_gdi_owner.h"
#include "win98_gdi_clip_box.h"
#include "win98_gdi_wide_text.h"
#include "win98_font_width.h"
#include "gdi_truetype.h"
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "../build/generated/font_charset_tables.h"
#endif
#include <stdlib.h>
#include <string.h>
namespace X86
{
// Shared DLL data can retain system brushes after their caller exits.
unsigned Application::system_brushes[32] = {};
#include "x86_gdi_wide_text.inc"
// Private per-DC defaults represent the process's public stock objects.
// Returning the private allocation breaks GetCurrentObject/SelectObject
// identity and leaks an implementation handle to callers.
unsigned Application::PublicGdiObject(unsigned handle) const
{
    if (!Gdi::IsPrivateDefault(handle)) return handle;
    switch (Gdi::GetObjectType(handle)) {
    case 1: return stock[7] ? stock[7] : handle;
    case 2: return stock[0] ? stock[0] : handle;
    case 6: return stock[13] ? stock[13] : handle;
    default: return handle;
    }
}
unsigned Application::OwnGdi(unsigned handle, unsigned kind)
{
    if (!handle)
        return 0;
    for (const auto &object : gdi_objects)
        if (object.handle == handle)
            return handle;
    for (auto &object : gdi_objects)
        if (!object.handle)
        {
            object = {handle, kind};
            // Original GDI stamps the creating task into the owner WORD at
            // object+0xc. NEO98 uses the process id; SetObjectOwner can move it.
            Gdi::SetObjectOwner(handle, ProcessId());
            return handle;
        }
    const unsigned capacity = gdi_objects.capacity;
    const unsigned grown = capacity ? (capacity <= ~0u / 2 ? capacity * 2 : 0) : 64;
    auto *entries = grown ? new(std::nothrow) GdiObject[grown] : nullptr;
    if (entries)
    {
        for (unsigned i = 0; i < capacity; ++i)
            entries[i] = gdi_objects.entries[i];
        entries[capacity] = {handle, kind};
        delete[] gdi_objects.entries;
        gdi_objects.entries = entries;
        gdi_objects.capacity = grown;
        Gdi::SetObjectOwner(handle, ProcessId());
        return handle;
    }
    // Roll back a new allocation, never an object belonging to another task.
    if (OwnsGdiObject(handle))
    {
        if (kind == 2)
            Gdi::DeleteDC(handle);
        else
            Gdi::DeleteObject(handle);
    }
    last_error = 8;
    return 0;
}
// KERNEL32 ordinal 23, : it resolves a process handle through the
// process handle table and returns the 16-bit task word, zero when the handle
// is not a live process. NEO98 has no task words, so the low half of the
// process id stands in. The current-process pseudo-handle Windows 98 returns
//  and never enters the handle table.
// Read again for Windows 98 calls, which walks the current
// process's module references ([PDB+98h], then the list at [PDB+4Ch]) and
// compares each module base ([IMTE+34h]) with the handle, so the handle it
// accepts is an HMODULE of this process. GDI32 then passes the
// owner through MapHInstLS to the 16-bit side, so the object
// belongs to the module and no longer to the task. COMCTL32 does exactly that
// with its own instance (DllMain stores it ) for the image list
// DCs and brushes it keeps in its shared data section; left with the first
// process, they died with it and the next Dev-C++ drew black image lists.
// The port keeps the process-handle reading above for the pseudo-handle and
// adds the module: its token has bit 15 set and cannot be a process id.
unsigned Application::GdiOwnerToken(unsigned handle)
{
    if (handle == 0x7fffffffu)
        return ProcessId() & 65535;
    if (handle && !(handle & 0xffff) && ModuleIndex(handle) < ModuleCapacity)
        return 0x8000u | ((handle >> 16) & 0x7fffu);
    const auto *entry = FindKernelHandle(handle);
    if (!entry || entry->kind != 6)
        return 0;
    return ChildProcessIdentity(entry->object) & 65535;
}
// The owner field is a WORD in the original header, so the stored token is the
// low half of the process id. Zero means no owner was ever stamped, which the
// teardown treats as its own object.
bool Application::OwnsGdiObject(unsigned handle) const
{
    const unsigned owner = Gdi::GetObjectOwner(handle);
    return !owner || owner == (ProcessId() & 65535);
}
void Application::FreeGdi()
{
    // Original task cleanup only reaches objects whose owner WORD still names
    // the dying task. An object handed to another process through
    // SetObjectOwner stays alive and loses only this process's reference.
    // GDI SYSDELETEOBJECT ignores the private bit, so the cleanup does
    // delete private objects of its own task.
    // DCs can retain application fonts and brushes through saved states.
    for (auto &object : gdi_objects)
        if (object.kind == 2)
        {
            if (OwnsGdiObject(object.handle))
                Gdi::DeleteDC(object.handle);
            object = {};
        }
    for (auto &object : gdi_objects)
        if (object.handle)
        {
            if (OwnsGdiObject(object.handle))
                Gdi::SysDeleteObject(object.handle);
            object = {};
        }
    delete[] gdi_objects.entries;
    gdi_objects = {};
    // Fonts and private stock objects go before their stores. System brushes
    // belong to the session and survive child exit. The pattern brush USER
    // keeps for WM_CTLCOLORSCROLLBAR holds a bitmap of the store; leaving it
    // alive past the teardown left that bitmap behind for good.
    for (auto &font : fonts)
        if (font.handle)
            Gdi::DeleteObject(font.handle);
    for (unsigned i = 0; i < 9; ++i)
        if (stock[i])
        {
            if (i <= 5) User::ProtectStockBrush(stock[i], false);
            Gdi::DeleteObject(stock[i]);
        }
    // The region and bitmap stores are one system-wide resource, like GDI's
    // heaps: the task cleanup above (GDI for the objects whose owner
    // WORD names the task) never takes them down. Only the end of the last
    // process does. A root without a parent is not enough: a game whose
    // launcher ended first is an orphan without parent_process, and its end
    // freed every region of Explorer. User::DetachProcess ran
    // before, so the process table holds only the survivors.
    bool survivors = false;
    for (unsigned i = 0; i < User::ProcessContextCount(); ++i)
        survivors |= User::ProcessContextAt(i) != nullptr;
    // The brushes of GetSysColorBrush live in USER's heap and never die with
    // a task; COMCTL32 keeps them in its globals (g_hbrHighlight and the
    // rest) for every process. The same rule as the stores: an orphan such
    // as DDHELP.EXE, whose game ended first, deleted them at its own end,
    // and Explorer drew selected labels and disabled toolbar text with dead
    // brushes.
    if (!survivors) {
        for (auto &brush : system_brushes) {
            if (brush) { User::ProtectStockBrush(brush, false); Gdi::DeleteObject(brush); }
            brush = 0;
        }
        Gdi::ShutdownBitmaps();
    }
    FreeMetafiles();
}
unsigned Application::SelectMappedTextFont(unsigned dc, unsigned &temporary)
{
    temporary = 0;
    Gdi::SIZE window{}, viewport{};
    if (!Gdi::MappingExtents(dc,window,viewport)) return 0;
    if (window.cx == viewport.cx && window.cy == viewport.cy) return Gdi::GetCurrentObject(dc,6);
    if (!window.cx || !window.cy) return 0;
    const unsigned selected = Gdi::GetCurrentObject(dc,6);
    unsigned char logical[60]{};
    bool found = false;
    for (const auto &font : fonts)
        if (font.handle == selected) { memcpy(logical,font.logfont,60); found = true; break; }
    if (!found && !Gdi::FontInfo(selected,logical)) return 0;
    int height, width; memcpy(&height,logical,4); memcpy(&width,logical+4,4);
    Gdi::TEXTMETRICA metrics{};
    if (!Gdi::FontMetrics(Gdi::SelectedFont(dc),&metrics)) return 0;
    if (!height) height = metrics.tmHeight;
    auto scaled = [](int value,int numerator,int denominator) {
        long long n = static_cast<long long>(value) * numerator, d = denominator;
        if (d < 0) { d = -d; n = -n; }
        const long long answer = n < 0 ? -((-n + d / 2) / d) : (n + d / 2) / d;
        return int(answer < -32767 ? -32767 : answer > 32767 ? 32767 : answer);
    };
    // A negative height requests the character cell without internal
    // leading. Mirrored axes change positions, not that height sign.
    height = scaled(height,viewport.cy < 0 ? -viewport.cy : viewport.cy,
                           window.cy < 0 ? -window.cy : window.cy);
    if (!height) height = 1;
    // lfWidth zero keeps the face's natural aspect ratio. Win98
    // scales explicit widths, but does not synthesize one for an
    // anisotropic DC (metafile_contract physical-font comparison).
    width = scaled(width,viewport.cx < 0 ? -viewport.cx : viewport.cx,
                         window.cx < 0 ? -window.cx : window.cx);
    memcpy(logical,&height,4); memcpy(logical+4,&width,4);
    const unsigned scratch = Allocate(60);
    if (!scratch) return 0;
    const unsigned create[] = {scratch};
    if (Store(scratch,logical,60)) temporary = FontCreateApi("CreateFontIndirectA",create);
    Free(scratch);
    if (!temporary) return 0;
    const unsigned old_font = Gdi::SelectObject(dc,temporary);
    if (!old_font) { const unsigned drop[] = {temporary}; Api("DeleteObject",drop); return 0; }
    return old_font;
}
unsigned Application::GdiApi(ApiName name, const unsigned *a)
{
#define API(n) case API_ID(#n):
    switch (name.id)
    {
    API(CreateMetaFileA) API(CreateMetaFileW) API(CloseMetaFile) API(DeleteMetaFile)
    API(CopyMetaFileA) API(GetMetaFileA) API(GetMetaFileBitsEx) API(SetMetaFileBitsEx)
    API(PlayMetaFile) API(PlayMetaFileRecord) API(EnumMetaFile) return MetafileApi(name, a);
    API(CreateEnhMetaFileA) API(CloseEnhMetaFile) API(DeleteEnhMetaFile) API(CopyEnhMetaFileA)
    API(GetEnhMetaFileBits) API(SetEnhMetaFileBits) API(SetWinMetaFileBits) API(GetWinMetaFileBits) API(GetEnhMetaFileHeader)
    API(GetEnhMetaFilePaletteEntries) API(GetEnhMetaFileDescriptionA) API(PlayEnhMetaFile)
    API(PlayEnhMetaFileRecord) API(EnumEnhMetaFile) API(gdiPlaySpoolStream)
        return EnhancedApi(name, a);
    default:
        break;
    }
    // A DC over a printer driver draws through the driver
    // (src/x86_print_draw.cpp).
    if (print_host && a && PrinterDC(a[0]))
    {
        unsigned drawn = 0;
        if (PrinterDrawApi(name, a, drawn))
            return drawn;
    }
    // The bounds a drawing call adds, only for a DC that
    // accumulates them (src/x86_gdi32_gates.cpp).
    GdiAccumulate(name, a);
    // A metafile DC records instead of drawing. GDI: the DC
    // signature 4F51h makes the call return after the record.
    {
        unsigned recorded = 0;
        if (MetafileRecord(name, a, recorded))
            return recorded;
    }
    // A device context with an open path records instead of drawing. Every
    // GDI.EXE drawing entry tests the open bit of DC+7Ah first; see
    //
    {
        unsigned recorded = 0;
        if (PathRecord(name, a, recorded))
            return recorded;
    }
    // An information DC accepts the drawing calls and draws nothing.
    switch (name.id)
    {
    API(BitBlt) API(PatBlt) API(StretchBlt) API(StretchDIBits) API(SetDIBitsToDevice) API(TextOutA)
    API(ExtTextOutA) API(TextOutW) API(ExtTextOutW) API(LineTo) API(Polyline) API(Rectangle)
    API(Ellipse) API(Polygon) API(PolyPolygon) API(FillRect) API(FrameRect) API(InvertRect)
        if (Gdi::InformationDC(a[0])) return 0;
        break;
    default:
        break;
    }
    auto bitmap_call = [&]()
    {
        struct Memory final : Gdi::DibIO
        {
            Application &app;
            explicit Memory(Application &a) : app(a) {}
            bool AllocateDib(unsigned bytes,Gdi::DibLease &lease) override {return app.AllocateSessionDib(bytes,lease);}
            bool Read(unsigned p, void *data, unsigned size) override { return app.virtual_memory.Access(p, size, false) && app.Read(p, data, size); }
            bool Write(unsigned p, const void *data, unsigned size) override
            {
                return app.virtual_memory.Access(p, size, true) && app.Store(p, data, size);
            }
            bool Probe(unsigned p, bool write) override { return app.virtual_memory.Access(p, 1, write); }
            unsigned ExpectedVersion() const override
            {
                const auto v = app.image.SubsystemVersion();
                return ((v >> 8) & 0xff00) | (v & 255);
            }
            void Error(unsigned code) override { app.last_error = code; }
            void Fault(const char *why) override { app.Fail(why); }
        } memory(*this);
        if(API_IS(name,"GetObjectA")&&Gdi::HasPalette(a[0]))return Gdi::PaletteGetObject(a[0],a[1],a[2],memory);
        if(Gdi::IsPaletteApi(name))return Gdi::PaletteApi(name,a,memory);
        if(Gdi::IsPixelApi(name))return Gdi::PixelApi(name,a,memory);
        if(Gdi::IsStretchApi(name))return Gdi::StretchApi(name,a,memory);
        if(Gdi::IsArcApi(name))return Gdi::ArcApi(name,a,memory);
        if(Gdi::IsShapeApi(name))return Gdi::ShapeApi(name,a,memory);
        if(Gdi::IsPenApi(name))return Gdi::PenApi(name,a,memory);
        if(API_IS(name,"GetObjectA")&&Gdi::HasPen(a[0]))return Gdi::PenGetObject(a[0],a[1],a[2],memory);
        if(Gdi::IsDibApi(name))return Gdi::DibApi(name,a,memory);
        if ((API_IS(name,"GetBitmapBits") || API_IS(name,"SetBitmapBits")) && Gdi::HasDibBitmap(a[0]))
            return Gdi::DibBitmapBits(a[0], a[1], a[2], API_IS(name,"SetBitmapBits"), memory);
        if(API_IS(name,"GetObjectA")&&Gdi::HasDibBitmap(a[0])){
            // The answer names the bits (bmBits at +20). A process that did
            // not create the section gets them here, before it hands the
            // pointer to an API that reads it on the host's side.
            const unsigned result=Gdi::DibGetObject(a[0],a[1],a[2],memory);
            unsigned bits=0;
            if(result>=24&&a[2]&&virtual_memory.Access(a[2]+20,4,false)&&Read(a[2]+20,&bits,4)&&bits)MapSessionDib(bits);
            return result;
        }
        return Gdi::IsRegionApi(name) ? Gdi::RegionApi(name,a,memory) : Gdi::IsMappingApi(name) ? Gdi::MapApi(name,a,memory) : Gdi::BitmapApi(name, a, memory);
    };
    bool drawn = false;
    switch (name.id)
    {
    API(GetTextCharset) API(GetTextCharsetInfo) API(GetFontLanguageInfo) API(TranslateCharsetInfo) return FontCharsetApi(name,a);
    API(GetCharWidthW) API(GetCharABCWidthsA) return FontWidthApi(name,a);
    API(GetCharWidthA) API(GetTextFaceA) API(EnumFontsA) API(EnumFontFamiliesA) API(EnumFontFamiliesExA) return FontQueryApi(name,a);
    API(GetRasterizerCaps) return FontQueryApi(name,a);
    API(EnumObjects) API(AddFontResourceA) API(RemoveFontResourceA)
    API(CreateScalableFontResourceA) API(CreateColorSpaceA) API(DeleteColorSpace)
    API(GetLogColorSpaceA) API(SetColorSpace) API(GetColorSpace) API(SetICMProfileA)
    API(ordinal_100) API(ordinal_101) API(ordinal_102) API(ordinal_103) API(ordinal_104)
        return CensusImportApi(name,a);
    API(TextOutW) API(ExtTextOutW) API(GetTextExtentPointW) API(GetTextExtentPoint32W) return WideTextApi(name,a);
    API(CreateBitmap) API(CreateBitmapIndirect) API(GetBitmapBits) API(SetBitmapBits)
    {
        const unsigned result = bitmap_call();
        return !strncmp(name, "Create", 6) ? OwnGdi(result, 1) : result;
    }
    API(CreateSolidBrush) return OwnGdi(Gdi::CreateSolidBrush(a[0]), 1);
    API(CreatePatternBrush) return OwnGdi(Gdi::CreatePatternBrush(a[0]), 1);
    // GDI32 sends the three DIB styles of a LOGBRUSH to
    // CreateDIBPatternBrush and CreateDIBPatternBrushPt
    // (src/x86_gdi32_completion.cpp); every other style takes its old way.
    API(CreateBrushIndirect)
    {
        unsigned logical[3] = {};
        if (a[0] && Read(a[0], logical, sizeof logical) &&
            (logical[0] == 5 || logical[0] == 6 || logical[0] == 8))
            return Gdi32CompletionApi(name, a);
        return GdiImportsApi(name, a);
    }
    // The GDI32 exports that were still missing on 23 September 2026.
    API(FixBrushOrgEx) API(GetMetaRgn) API(ByeByeGDI) API(CreateDIBPatternBrush)
    API(CreateDIBPatternBrushPt) API(GetAspectRatioFilterEx)
    API(EnumICMProfilesA) API(UpdateICMRegKeyA)
        return Gdi32CompletionApi(name, a);
    API(CheckColorsInGamut) API(ColorCorrectPalette) API(DrawEscape) API(GdiComment)
    API(SetBoundsRect) API(GetBoundsRect) API(GetEnhMetaFileA) API(GetMiterLimit)
    API(SetMiterLimit) API(LineDDA) API(SetMagicColors) API(SetMetaRgn)
        return Gdi32GateApi(name, a);
    // and are exported twice, with and without the A.
    API(GetGlyphOutline) return GdiOutlineQueryApi("GetGlyphOutlineA", a);
    API(GetKerningPairs) return GdiOutlineQueryApi("GetKerningPairsA", a);
    API(CreateHatchBrush) API(CreateEllipticRgn)
    API(CreateEllipticRgnIndirect) API(CreateRoundRectRgn) API(FillRgn) API(FrameRgn)
    API(InvertRgn) API(PaintRgn) API(UnrealizeObject) API(GdiFlush) API(GetGraphicsMode)
    API(GetBitmapDimensionEx) API(SetBitmapDimensionEx) return GdiImportsApi(name, a);
    API(BeginPath) API(EndPath) API(AbortPath) API(CloseFigure) API(FillPath)
    API(StrokePath) API(StrokeAndFillPath) API(FlattenPath) API(WidenPath)
    API(PathToRegion) API(SelectClipPath) API(GetPath) API(SetArcDirection)
    API(GetArcDirection) API(PolyBezier) API(PolyBezierTo) API(PolylineTo)
    API(PolyPolyline) API(FloodFill) API(ExtFloodFill) return GdiPathsApi(name, a);
    API(Escape) API(ExtEscape) return GdiEscapeApi(name, a);
    API(StartDocA) API(EndDoc) API(StartPage) API(EndPage) API(AbortDoc)
    API(SetAbortProc) API(ResetDCA) return GdiDocumentApi(name, a);
    API(ChoosePixelFormat) API(DescribePixelFormat) API(SetPixelFormat)
    API(GetPixelFormat) API(SwapBuffers) return GdiPixelFormatApi(name, a);
    API(GetDeviceGammaRamp) API(SetDeviceGammaRamp) return GdiGammaApi(name, a);
    API(SetICMMode) return GdiIcmApi(name, a);
    API(GetTextExtentExPointA) return GdiTextExtentExApi(name, a);
    API(SetMapperFlags) return GdiMapperFlagsApi(name, a);
    API(GetCharacterPlacementA) return GdiCharacterPlacementApi(name, a);
    API(GetGlyphOutlineA) API(GetFontData) API(GetOutlineTextMetricsA)
    API(GetKerningPairsA) return GdiOutlineQueryApi(name, a);
    API(GetDeviceCaps)
        if(a[1]==2 && MetafileKindOf(a[0])==1)return 5; // DT_METAFILE
        return Gdi::DeviceCaps(a[0],a[1]);
    API(CreateDCA) API(CreateICA) {
        struct Request {
            Application &app;unsigned scratch;bool valid=true;
            unsigned Buffer(){return scratch+16;}
            bool Valid(){return valid && app.state!=Failed;}
            unsigned Read8(unsigned p){if(p==scratch+276){valid=false;app.Fail("Malformed device profile has no comma within 260 bytes");return 0;}unsigned char v=0;if(!app.Read(p,&v,1))valid=false;return v;}
            void Write8(unsigned p,unsigned v){unsigned char c=v;if(!app.Store(p,&c,1))valid=false;}
            unsigned Profile(unsigned device){
                const unsigned args[]={scratch,device,scratch+8,Buffer(),260};
                return app.Api("GetProfileStringA",args);
            }
            // The driver name "DISPLAY", or, with no driver at all, one of the
            // device names EnumDisplayDevices hands out. Win98 has that second
            // form since multiple monitors: GDI32 leaves a device it
            // finds no [devices] line for with a null driver, and the 16-bit
            // CreateDC below it recognises its own \\.\DISPLAYn. The original
            // DDRAW takes exactly this path in Windows 98.
            unsigned Upper(const char *s,unsigned i){unsigned ch=static_cast<unsigned char>(s[i]);return ch>='a'&&ch<='z'?ch-32:ch;}
            bool DisplayDriver(unsigned text){
                if(!text)return false;
                const char *name=app.ReadString(text);
                const char expected[]="DISPLAY";
                for(unsigned i=0;i<sizeof expected;++i)if(Upper(name,i)!=unsigned(expected[i]))return false;
                return true;
            }
            bool DisplayDevice(unsigned text){
                if(!text)return false;
                const char *name=app.ReadString(text);
                const char prefix[]="\\\\.\\DISPLAY";
                for(unsigned i=0;i+1<sizeof prefix;++i)if(Upper(name,i)!=unsigned(prefix[i]))return false;
                unsigned at=sizeof prefix-1,digits=0;
                while(Upper(name,at)>='0'&&Upper(name,at)<='9'){++at;++digits;}
                return digits && !Upper(name,at);
            }
            unsigned Create16(unsigned driver,unsigned device,unsigned output,unsigned mode,bool information){
                const bool display=driver?(DisplayDriver(driver)||DisplayDevice(driver)):DisplayDevice(device);
                if(!Valid())return 0;
                // Only the native display driver is installed. Printer and display
                // configuration drivers retain their unavailable-provider return.
                // GDI loads any other driver and makes a printer DC
                // over its DDI (src/x86_print_driver.cpp).
                if(!display)return app.PrinterCreateDC(driver,device,output,mode,information);
                if(mode){app.last_error=50;return 0;}
                (void)device;(void)output;
                const auto screen=User::GetDC(0);if(!screen){app.last_error=8;return 0;}
                const auto dc=Gdi::CreateDisplayDC(screen,information);User::ReleaseDC(0,screen);
                if(!dc)app.last_error=8;
                return dc;
            }
        } request{*this,Allocate(276)};
        if(!request.scratch){last_error=8;return 0;}
        const char section[]="devices";Store(request.scratch,section,sizeof section);Write32(request.scratch+8,0);
        unsigned result=Win98DcInformation::Create(request,a[0],a[1],a[2],a[3],API_IS(name,"CreateICA"));
        Free(request.scratch);return OwnGdi(result,2);
    }
    API(CreateCompatibleDC) return OwnGdi(Gdi::CreateCompatibleDC(a[0]), 2);
    API(DeleteDC)
    {
        if (PrinterDC(a[0]))
        {
            for (auto &object : gdi_objects)
                if (object.handle == a[0] && object.kind == 2)
                    object = {};
            return PrinterDeleteDC(a[0]);
        }
        for (auto &object : gdi_objects)
            if (object.handle == a[0] && object.kind == 2)
            {
                const bool result = Gdi::DeleteDC(a[0]);
                if (result)
                    object = {};
                return result;
            }
        return 0;
    }
    API(GetObjectA)
    {
        if(Gdi::HasPalette(a[0]))return bitmap_call();
        if (Gdi::HasOriginalBitmap(a[0]) || Gdi::HasDibBitmap(a[0]) || Gdi::HasPen(a[0]))
            return bitmap_call();
        unsigned info[3];
        if (Gdi::SolidBrushInfo(a[0], info))
        {
            if (!a[2])
                return sizeof info;
            const unsigned length = a[1] < sizeof info ? a[1] : sizeof info;
            if (length && !virtual_memory.Access(a[2], length, true))
                return 0;
            if (length)
                Store(a[2], info, length);
            return length;
        }
        for (const auto &font : fonts)
            if (font.handle && font.handle == a[0])
                return FontCreateApi(name, a);
        unsigned char logical[60];
        if(Gdi::FontInfo(a[0],logical))
        {
            if(!a[2])return sizeof logical;
            const unsigned bytes=a[1]<sizeof logical?a[1]:sizeof logical;
            if(bytes && !virtual_memory.Access(a[2],bytes,true))return 0;
            return bytes && Store(a[2],logical,bytes)?bytes:0;
        }
        if(!Gdi::ObjectExists(a[0]))last_error=87;
        return 0;
    }
    // GDI32 : the owner handle is validated first, and a rejected
    // owner returns zero without reaching the 16-bit side. The original uses
    // KERNEL32 ordinal 23, which resolves a process handle to its task word.
    API(SetObjectOwner)
    {
        const unsigned owner = GdiOwnerToken(a[1]);
        if (!Win98GdiOwner::OwnerAccepted(owner))
            return 0;
        return Win98GdiOwner::Result(Gdi::SetObjectOwner(a[0], owner));
    }
    API(RectVisible)
    {
        Gdi::RECT rect;
        if (!a[1] || !Read(a[1], &rect, sizeof rect))
            return 0;
        return Gdi::RectVisible(a[0], &rect) ? 1 : 0;
    }
    API(PtVisible) return Gdi::PtVisible(a[0], int(a[1]), int(a[2])) ? 1 : 0;
    // GDI32 : the DC is narrowed to a WORD and the rectangle is the
    // thunk's own eight WORDs, widened back to four LONGs on return. A null
    // LPRECT reaches the 16-bit probe with a zero selector; the adapter answers
    // ERROR instead of faulting, as RectVisible already does.
    API(GetClipBox)
    {
        Gdi::RECT rect = {};
        const unsigned result = Gdi::GetClipBox(a[0], a[1] ? &rect : nullptr);
        if (result && a[1] && !Store(a[1], &rect, sizeof rect))
            return Win98GdiClipBox::Error;
        return result;
    }
    // GDI32, ordinal 380. Writes lppt only when the caller passes one;
    // a DC it cannot resolve answers FALSE without touching it.
    API(GetBrushOrgEx)
    {
        Gdi::POINT point;
        if (!a[1] || !virtual_memory.Access(a[1], sizeof point, true) || !Gdi::GetBrushOrgEx(a[0], &point)) return 0;
        return Store(a[1], &point, sizeof point);
    }
    API(SetBrushOrgEx)
    {
        Gdi::POINT previous = {};
        if (!Gdi::SetBrushOrgEx(a[0], int(a[1]), int(a[2]), &previous))
            return 0;
        if (a[3])
        {
            // Win98 updates the origin before writing the old point. An
            // invalid output pointer faults through the caller's SEH; it
            // must not turn Store's host-side validation into a fatal error.
            if (!virtual_memory.Access(a[3], sizeof previous, true))
            {
                DispatchGuestException(0xc0000005u, 0xe, 2, 1, a[3], nullptr);
                return 0;
            }
            Store(a[3], &previous, sizeof previous);
        }
        return 1;
    }
    API(SaveDC) return Gdi::SaveDC(a[0]);
    API(RestoreDC) return Gdi::RestoreDC(a[0], int(a[1]));
    API(IntersectClipRect) return Gdi::IntersectClipRect(a[0], int(a[1]), int(a[2]), int(a[3]), int(a[4]));
    API(ExcludeClipRect) return Gdi::ExcludeClipRect(a[0], int(a[1]), int(a[2]), int(a[3]), int(a[4]));
    API(GetBkMode) return Gdi::GetBkMode(a[0]);
    API(GetBkColor) return Gdi::GetBkColor(a[0]);
    API(GetTextColor) return Gdi::GetTextColor(a[0]);
    API(SetBkMode) return Gdi::SetBkMode(a[0], int(a[1]));
    // Windows 98 keeps no GDI batch, so the limit
    // is one and setting it changes nothing; both answer the previous one.
    API(GdiGetBatchLimit) return 1;
    API(GdiSetBatchLimit) return 1;
    // GDI32: Windows 98 has only GM_COMPATIBLE. Any other mode is
    // ERROR_INVALID_PARAMETER and zero; the handle is never looked at.
    API(SetGraphicsMode) { if (a[1] == 1) return 1; last_error = 87; return 0; }
    // The colour management pair of GDI 40, reached through the thunk table of
    // GDI.EXE with CL 72h and 73h. Both
    // workers decide before anything else whether GDI has an ICM module at all:
    // the word at GDI's DGROUP +116h is the handle that Windows 98 stores when the
    // start-up load succeeds, and while it is zero GetICMProfile answers
    // SetLastError(87) and FALSE and ColorMatchToTarget
    // SetLastError(8) and FALSE. This port has no colour
    // management, so that is the state it is in. A handle that is no device
    // context never reaches the worker: the thunk stub validates it first
    // and leaves with the invalid-handle answer.
    // GDI, thunk table CL 38h, worker. The worker clears its
    // answer, reads the PDEVICE of the DC and asks whether the device
    // has a palette (RC_PALETTE, bit 100h of the word at +26h of the device
    // description). A device without one leaves at once, and the
    // answer is zero. The display of this port is palettized only
    // at eight bits and below (src/gdi.cpp), so the usual answer is that zero.
    // For a palettized device the worker builds a translation out of the DC's
    // palette and rewrites the pixels of the surface.
    API(UpdateColors)
    {
        const unsigned type = Gdi::GetObjectType(a[0]);
        if (type != 3 && type != 4 && type != 10 && type != 12)
        {
            last_error = 6;
            return 0;
        }
        // A palettized device: the pixels of the DC go from the slots its
        // palette had to the ones it has now (Gdi::UpdateColors).
        if (Gdi::DeviceCaps(a[0], 38) & 0x100)          // RASTERCAPS, RC_PALETTE
            return Gdi::UpdateColors(a[0]) ? 1 : 0;
        return 0;
    }
    API(GetICMProfileA) return GdiIcmApi(name, a);
    API(ColorMatchToTarget)
    {
        const unsigned type = Gdi::GetObjectType(a[0]);
        if (type != 3 && type != 4 && type != 10 && type != 12)
        {
            last_error = 6;
            return 0;
        }
        last_error = 8;
        return 0;
    }
    API(GetObjectType)
    {
        const unsigned kind=MetafileObjectType(a[0]);if(kind)return kind;
        unsigned result=Gdi::GetObjectType(a[0]);if(!result)last_error=6;return result;
    }
    API(GetCurrentObject) return PublicGdiObject(Gdi::GetCurrentObject(a[0],a[1]));
    API(GetTextAlign) return Gdi::GetTextAlign(a[0]);
    API(SetTextAlign) {unsigned result=Gdi::SetTextAlign(a[0],a[1]);if(result==65535)last_error=87;return result;}
    API(GetTextCharacterExtra) return Gdi::GetTextCharacterExtra(a[0]);
    API(SetTextCharacterExtra) return Gdi::SetTextCharacterExtra(a[0], int(a[1]));
    API(SetTextJustification) {bool result=Gdi::SetTextJustification(a[0],int(a[1]),int(a[2]));if(!result)last_error=87;return result;}
    API(GetTextMetricsA)
    {
        unsigned temporary = 0;
        const unsigned previous = SelectMappedTextFont(a[0],temporary);
        if (!previous) return 0;
        Gdi::TEXTMETRICA metrics{};
        const bool result = Gdi::GetTextMetricsA(a[0],&metrics) && Store(a[1],&metrics,sizeof metrics);
        if (temporary) { Gdi::SelectObject(a[0],previous); const unsigned drop[]={temporary}; Api("DeleteObject",drop); }
        return result;
    }
    API(GetTextExtentPoint32A)
    {
        if (a[2] > 4096 || !a[3] || (a[2] && !a[1]))
        {
            last_error = 87;
            return 0;
        }
        char text[4096];
        if (a[2] && !Read(a[1], text, a[2]))
            return 0;
        unsigned temporary = 0;
        const unsigned previous = SelectMappedTextFont(a[0],temporary);
        if (!previous) return 0;
        Gdi::SIZE size;
        const bool result = Gdi::GetTextExtentPoint32A(a[0], text, int(a[2]), &size) && Store(a[3], &size, sizeof size);
        if (temporary) { Gdi::SelectObject(a[0],previous); const unsigned drop[]={temporary}; Api("DeleteObject",drop); }
        return result;
    }
    API(GetCharWidth32A)
    {
        if (a[1] > a[2] || a[2] - a[1] >= 256)
        {
            last_error = 87;
            return 0;
        }
        int widths[256];
        const bool result = Gdi::GetCharWidth32A(a[0], a[1], a[2], widths);
        if (result)
            Store(a[3], widths, (a[2] - a[1] + 1) * sizeof(int));
        return result;
    }

    API(PatBlt) drawn = Gdi::PatBlt(a[0], int(a[1]), int(a[2]), int(a[3]), int(a[4]), a[5]);
        break;
    API(BitBlt) drawn =
        Gdi::BitBlt(a[0], int(a[1]), int(a[2]), int(a[3]), int(a[4]), a[5], int(a[6]), int(a[7]), a[8]);
        break;
    API(DrawFocusRect)
    {
        Gdi::RECT rect;
        if (!a[1] || !virtual_memory.Access(a[1], sizeof rect, false)) return 0;
        if (!Read(a[1], &rect, sizeof rect)) return 0;
        drawn = Gdi::DrawFocusRect(a[0], &rect);
        break;
    }
    API(FillRect) API(FrameRect)
    {
        Gdi::RECT rect;
        if (!Read(a[1], &rect, sizeof rect))
            return 0;
        unsigned brush = a[2];
        if (brush && brush <= 32)
        {
            const unsigned color[] = {brush - 1};
            brush = Api("GetSysColorBrush", color);
        }
        drawn = API_IS(name,"FillRect") ? Gdi::FillRect(a[0], &rect, brush) : Gdi::FrameRect(a[0], &rect, brush);
        break;
    }
    API(InvertRect)
    {
        Gdi::RECT rect;
        if (!a[1] || !Read(a[1], &rect, sizeof rect)) return 0;
        drawn = Gdi::InvertRect(a[0], &rect);
        break;
    }
    API(TextOutA) API(ExtTextOutA)
    {
        const bool extended = API_IS(name,"ExtTextOutA");
        const unsigned text = extended ? a[5] : a[3], length = extended ? a[6] : a[4];
        if (length > 4096 || (length && !text))
        {
            last_error = 87;
            return 0;
        }
        if(length&&Gdi::ValidDC(a[0])&&(Gdi::GetTextAlign(a[0])&0x100)){last_error=50;return 0;}
        char data[4096];
        int advances[4096];
        Gdi::RECT rect;
        if (length && !Read(text, data, length))
            return 0;
        if (extended && a[4] && !Read(a[4], &rect, sizeof rect))
            return 0;
        if (extended && a[7] && length && !Read(a[7], advances, length * 4))
            return 0;
        unsigned physical_font = 0;
        const unsigned old_font = SelectMappedTextFont(a[0],physical_font);
        if (!old_font) return 0;
        drawn = Gdi::ExtTextOutA(a[0], int(a[1]), int(a[2]), extended ? a[3] : 0,
                                 extended && a[4] ? &rect : nullptr, data, length,
                                 extended && a[7] ? advances : nullptr);
        if (physical_font)
        {
            Gdi::SelectObject(a[0],old_font);
            const unsigned drop[] = {physical_font}; Api("DeleteObject",drop);
        }
        break;
    }
    default:
    {
        if(API_IS(name,"SelectPalette")&&Gdi::DisplayDepth()<=8){
            // USER: a palette is the foreground palette only in a DC
            // of the active window (the top-level window of the DC's window);
            // everything else, a memory DC and the default palette too, selects
            // in the background.
            unsigned forced[3]={a[0],a[1],1};
            if(!a[2]&&a[1]!=Gdi::DefaultPalette()){
                unsigned window=User::WindowFromDC(a[0]);
                while(window&&(User::Style(window)&0x40000000)&&User::Parent(window))window=User::Parent(window);
                if(window&&window!=User::GetDesktopWindow()&&window==User::GetForegroundWindow())forced[2]=0;
            }
            a=forced;
            return bitmap_call();
        }
        if(API_IS(name,"RealizePalette")){
            // USER: when the system palette changed and the DC holds the
            // foreground palette, every top-level window
            // hears WM_PALETTECHANGED with the window of the DC.
            const unsigned result=bitmap_call();
            if(Gdi::LastRealizeChanged()&&Gdi::DisplayDepth()<=8&&Gdi::IsDCCurrentPalette(a[0])){
                const unsigned window=User::WindowFromDC(a[0]);
                if(window)BroadcastMessage(Win98Broadcast::Send,0x311,window,0);
            }
            return result;
        }
        if(Gdi::IsPaletteApi(name)){unsigned result=bitmap_call();return !strncmp(name,"Create",6)&&result!=Gdi::DefaultPalette()?OwnGdi(result,1):result;}
        if(Gdi::IsPixelApi(name)) {
            const unsigned result=bitmap_call();
            if ((API_IS(name,"SetPixel") && result != ~0u) ||
                (API_IS(name,"SetPixelV") && result)) User::DrawingChanged(a[0]);
            return result;
        }
        if(Gdi::IsStretchApi(name)){unsigned result=bitmap_call();
            if(result&&(API_IS(name,"StretchBlt")||API_IS(name,"MaskBlt")||API_IS(name,"PlgBlt")))User::DrawingChanged(a[0]);
            return !strncmp(name,"Create",6)?OwnGdi(result,1):result;}
        if(Gdi::IsArcApi(name)){unsigned result=bitmap_call();if(result)User::DrawingChanged(a[0]);return result;}
        if(Gdi::IsShapeApi(name)){
            unsigned result=bitmap_call();
            if(result&&!API_IS(name,"GetPolyFillMode")&&!API_IS(name,"SetPolyFillMode"))User::DrawingChanged(a[0]);
            return result;
        }
        if(Gdi::IsPenApi(name)){
            unsigned result=bitmap_call();
            if(result&&(API_IS(name,"LineTo")||API_IS(name,"Polyline")))User::DrawingChanged(a[0]);
            return !strncmp(name,"Create",6)||API_IS(name,"ExtCreatePen")?OwnGdi(result,1):result;
        }
        if(Gdi::IsDibApi(name)){
            unsigned result=bitmap_call();
            if(result&&result!=~0u&&(API_IS(name,"StretchDIBits")||API_IS(name,"SetDIBitsToDevice")))User::DrawingChanged(a[0]);
            return !strncmp(name,"Create",6)?OwnGdi(result,1):result;
        }
        if(Gdi::IsMappingApi(name))return bitmap_call();
        if(Gdi::IsRegionApi(name)){unsigned result=bitmap_call();return !strncmp(name,"Create",6)||API_IS(name,"ExtCreateRegion")?OwnGdi(result,1):result;}
        return Api(name, a);
    }
    }
    if (drawn)
        User::DrawingChanged(a[0]);
    return drawn;
#undef API
}
// The bits of GDI's DIB sections. Win98 allocates them in the shared arena,
// so a section is at one linear address in every process and lives as long as
// its object: COMCTL32 keeps the images of an image list in a DIB section,
// the system image list of the shell belongs to every process that loads
// SHELL32, and whichever of them adds an icon to a full list creates the next,
// larger section. ImageList_Draw then reads bmBits out of GetObject and blends
// a selected icon straight in those bytes. With the bits in the memory of the
// process that created the section, Explorer faulted there as soon as another
// process (the "Open With" dialog of rundll32) had grown the list.
// A block here is host memory with a guest address the session hands out in
// a window of its own, A0000000h to AFFFFFFFh, which no automatic reservation
// of a process enters (VirtualMemory::Space::Reserve). The process that
// creates a section maps the block at once. Every other process maps it when
// GetObject names the bits to it or when its code first touches them (the
// memory fault hook), the way the record of a critical section arrives
// (MapCriticalChunk). A deleted section stays in the table until every
// process that had it has taken it out of its own address space, which each
// does itself the next time it creates a section or when it ends; only then
// are the bytes freed and the range handed out again.
namespace
{
constexpr unsigned SessionDibBase = 0xa0000000u, SessionDibLimit = 0xb0000000u;
// FEXCore shares whole host pages of 16 KiB; a block is
// a multiple of that at a multiple of that, so no private guest page shares a
// host page with it.
constexpr unsigned SessionDibStep = SessionDibMemory::Alignment;
struct SessionDib
{
    unsigned base = 0, size = 0, bytes = 0;
    unsigned char *host = nullptr;
    void *storage = nullptr;
    bool dead = false;
    void *holders[Win98SessionArena::HolderCapacity] = {};
};
// Sorted by address.
SessionDib *session_dibs = nullptr;
unsigned session_dib_count = 0, session_dib_capacity = 0, session_dib_dead = 0;
SessionDib *FindSessionDib(unsigned address)
{
    if (address < SessionDibBase || address >= SessionDibLimit) return nullptr;
    unsigned low = 0, high = session_dib_count;
    while (low < high)
    {
        const unsigned middle = (low + high) / 2;
        const auto &b = session_dibs[middle];
        if (address < b.base) high = middle;
        else if (address - b.base >= b.size) low = middle + 1;
        else return &session_dibs[middle];
    }
    return nullptr;
}
bool HoldsSessionDib(const SessionDib &b, const void *process)
{
    for (auto *holder : b.holders) if (holder == process) return true;
    return false;
}
bool AnySessionDibHolder(const SessionDib &b)
{
    for (auto *holder : b.holders) if (holder) return true;
    return false;
}
void EraseSessionDib(SessionDib *b)
{
#if defined(__linux__) && defined(__x86_64__)
    if (!LinuxSharedMemory::Free(b->storage))
#endif
    free(b->storage);
    if (b->dead) --session_dib_dead;
    const unsigned index = unsigned(b - session_dibs);
    for (unsigned i = index + 1; i < session_dib_count; ++i) session_dibs[i - 1] = session_dibs[i];
    --session_dib_count;
}
// The object is gone (DeleteObject in whichever process). The bytes stay
// until the processes that mapped them have let go.
void ReleaseSessionDib(void *, const Gdi::DibLease &lease)
{
    auto *b = FindSessionDib(lease.guest);
    if (!b || b->base != lease.guest || b->dead) return;
    b->dead = true;
    ++session_dib_dead;
    if (!AnySessionDibHolder(*b)) EraseSessionDib(b);
}
} // namespace
struct Application::DibBackend final : VirtualMemory::Backend
{
    Application &app;
    unsigned char *host;
    unsigned origin;
    DibBackend(Application &a, unsigned char *p, unsigned base) : app(a), host(p), origin(base) {}
    bool Available(unsigned p, unsigned n) override { return app.AvailableImageAddress(p, n); }
    // Bitmap bits are data; the execute right comes only on demand, like for
    // every other page.
    bool Map(unsigned p, unsigned n, unsigned permissions) override
    {
        const unsigned rights = app.HostProtection(p, n, permissions & ~(unsigned)Prot::Exec);
        return app.cpu->MapHost(p, n, rights, host + (p - origin)) == CpuError::Ok;
    }
    bool Unmap(unsigned p, unsigned n) override
    {
        app.execute_pages.Remove(p, n);
        return app.cpu->Unmap(p, n) == CpuError::Ok;
    }
    bool Protect(unsigned p, unsigned n, unsigned permissions) override
    {
        return app.cpu->Protect(p, n, app.HostProtection(p, n, permissions & ~(unsigned)Prot::Exec)) == CpuError::Ok;
    }
};
// Maps the block at `index` into this process, at the block's address.
bool Application::HoldSessionDib(unsigned index)
{
    auto &b = session_dibs[index];
    unsigned slot = Win98SessionArena::HolderCapacity;
    for (unsigned i = 0; i < Win98SessionArena::HolderCapacity; ++i)
        if (!b.holders[i]) { slot = i; break; }
    if (slot == Win98SessionArena::HolderCapacity || !cpu) return false;
    DibBackend e(*this, b.host, b.base);
    if (virtual_memory.ReserveMapping(e, b.base >> 12, b.size >> 12, 0x60000) != b.base) return false;
    if (!virtual_memory.CommitMapping(e, b.base >> 12, b.size >> 12, 0x60000))
    {
        virtual_memory.ReleaseMapping(e, b.base);
        return false;
    }
    b.holders[slot] = this;
    return true;
}
bool Application::QuerySessionDib(unsigned address, VirtualMemory::Info &info)
{
    const auto *b = FindSessionDib(address);
    if (!b || b->dead || !HoldsSessionDib(*b, this)) return false;
    const unsigned page = address & ~4095u;
    const bool committed = page - b->base < b->bytes;
    // Host-page alignment is an allocation detail, not extra bitmap memory.
    info = {page, b->base, 4, b->base + (committed ? b->bytes : b->size) - page,
            committed ? 0x1000u : 0x2000u, committed ? 4u : 0u, 0x20000};
    return true;
}
// The bits of a section another process created, on first use here.
bool Application::MapSessionDib(unsigned address)
{
    auto *b = FindSessionDib(address);
    if (!b || b->dead || HoldsSessionDib(*b, this)) return false;
    return HoldSessionDib(unsigned(b - session_dibs));
}
// Takes the blocks of deleted sections out of this address space. With
// `unmap` false the address space itself is going (the end of the process).
void Application::DropSessionDibs(bool all, bool unmap)
{
    if (!all && !session_dib_dead) return;
    for (unsigned i = session_dib_count; i-- > 0;)
    {
        auto &b = session_dibs[i];
        if (!all && !b.dead) continue;
        bool held = false;
        for (auto *&holder : b.holders)
            if (holder == this) { holder = nullptr; held = true; }
        if (held && unmap && !ReleaseMappingPages(b.base)) { Fail("DIB section mapping release failed"); return; }
        if (b.dead && !AnySessionDibHolder(b)) EraseSessionDib(&b);
    }
}
bool Application::AllocateSessionDib(unsigned bytes, Gdi::DibLease &lease)
{
    if (!bytes || (bytes & 4095) || bytes > SessionDibLimit - SessionDibBase) return false;
    DropSessionDibs(false, true);
    if (state == Failed) return false;
    const unsigned size = (bytes + (SessionDibStep - 1)) & ~(SessionDibStep - 1);
    if (session_dib_count == session_dib_capacity)
    {
        const unsigned capacity = session_dib_capacity ? session_dib_capacity * 2 : 64;
        auto *grown = static_cast<SessionDib *>(realloc(session_dibs, capacity * sizeof(SessionDib)));
        if (!grown) return false;
        session_dibs = grown;
        session_dib_capacity = capacity;
    }
    // The host bytes lie in their host page as the guest bytes do in theirs.
    void *storage = nullptr;
    unsigned char *host = nullptr;
#if defined(__linux__) && defined(__x86_64__)
    if (ProcessCpuBackend() == CpuBackend::Native)
    {
        storage = LinuxSharedMemory::Allocate(size);
        host = static_cast<unsigned char *>(storage);
    }
    else
#endif
    {
        host = SessionDibMemory::Allocate(size, storage);
    }
    if (!host) return false;
    // First fit between the blocks of the session. A range this process
    // cannot take (an image that was loaded there) is passed over.
    unsigned at = SessionDibBase;
    for (unsigned i = 0; i <= session_dib_count; ++i)
    {
        const unsigned end = i < session_dib_count ? session_dibs[i].base : SessionDibLimit;
        if (end - at >= size && AvailableImageAddress(at, size))
        {
            for (unsigned n = session_dib_count; n > i; --n) session_dibs[n] = session_dibs[n - 1];
            ++session_dib_count;
            session_dibs[i] = SessionDib{};
            session_dibs[i].base = at;
            session_dibs[i].size = size;
            session_dibs[i].bytes = bytes;
            session_dibs[i].host = host;
            session_dibs[i].storage = storage;
            if (HoldSessionDib(i))
            {
                lease = {at, bytes, host, nullptr, ReleaseSessionDib};
                return true;
            }
            session_dibs[i].storage = nullptr;
            EraseSessionDib(&session_dibs[i]);
        }
        if (i < session_dib_count) at = session_dibs[i].base + session_dibs[i].size;
    }
#if defined(__linux__) && defined(__x86_64__)
    if (!LinuxSharedMemory::Free(storage))
#endif
    free(storage);
    return false;
}
bool Application::AllocateDibLease(unsigned bytes,Gdi::DibLease &lease)
{
    struct Backend final:VirtualMemory::Backend {
        Application &app;unsigned char *host;unsigned origin=0;
        Backend(Application &a,unsigned char *p):app(a),host(p){}
        bool Available(unsigned p,unsigned n)override{return app.AvailableImageAddress(p,n);}
        // Bitmap bits are data; the execute right comes only on demand, like
        // for every other page.
        bool Map(unsigned p,unsigned n,unsigned permissions)override{
            const unsigned rights=app.HostProtection(p,n,permissions&~(unsigned)Prot::Exec);
            // Without own host bytes the engine's guest memory is the host
            // memory of the bits (Cpu::HostPointer).
            return (host?app.cpu->MapHost(p,n,rights,host+(p-origin)):app.cpu->Map(p,n,rights))==CpuError::Ok;}
        bool Unmap(unsigned p,unsigned n)override{app.execute_pages.Remove(p,n);return app.cpu->Unmap(p,n)==CpuError::Ok;}
        bool Protect(unsigned p,unsigned n,unsigned permissions)override{return app.cpu->Protect(p,n,app.HostProtection(p,n,permissions&~(unsigned)Prot::Exec))==CpuError::Ok;}
    };
    if(!bytes||(bytes&4095))return false;
    Backend e(*this,nullptr);const unsigned base=virtual_memory.ReserveMapping(e,0x80060000,bytes>>12,0x60000);
    if(base==~0u)return false;
    e.origin=base;
    // An engine that hands out its guest memory (FEXCore)
    // keeps the bits in ordinary guest pages and GDI writes them in place;
    // otherwise the bits are host bytes mapped into the guest (Unicorn, and
    // the address space of a process without a processor, whose pages may
    // move to a processor later).
    unsigned char *storage=nullptr;
    if(cpu->Backend()!=CpuBackend::Fex&&cpu->Backend()!=CpuBackend::Arm64){
        storage=static_cast<unsigned char*>(calloc(1,bytes));
        if(!storage){virtual_memory.ReleaseMapping(e,base);return false;}
        e.host=storage;
    }
    if(!virtual_memory.CommitMapping(e,base>>12,bytes>>12,0x60000)){virtual_memory.ReleaseMapping(e,base);free(storage);return false;}
    auto *host=storage?storage:static_cast<unsigned char*>(cpu->HostPointer(base,bytes));
    if(!host){ReleaseMappingPages(base);return false;}
    lease={base,bytes,host,this,storage?ReleaseDibLease:ReleaseDibView};return true;
}
void Application::ReleaseDibLease(void *context,const Gdi::DibLease &lease)
{
    auto &app=*static_cast<Application*>(context);
    if(!app.ReleaseMappingPages(lease.guest)){app.Fail("DIB section mapping release failed");return;}
    free(lease.host);
}
void Application::ReleaseDibView(void *context,const Gdi::DibLease &lease)
{
    auto &app=*static_cast<Application*>(context);
    if(!app.ReleaseMappingPages(lease.guest))app.Fail("DIB section mapping release failed");
}
unsigned Application::CreateResourceBitmap(const unsigned char *resource,unsigned bytes)
{
    struct Memory final:Gdi::BitmapIO {
        Application &app;explicit Memory(Application &a):app(a){}
        bool Read(unsigned p,void *out,unsigned n)override{return app.Read(p,out,n);}
        bool Write(unsigned p,const void *data,unsigned n)override{return app.Store(p,data,n);}
        bool Probe(unsigned p,bool write)override{return app.virtual_memory.Access(p,1,write);}
        void Error(unsigned e)override{app.last_error=e;}void Fault(const char *s)override{app.Fail(s);}
    } memory(*this);
    return Gdi::CreateResourceBitmap(resource,bytes,memory);
}

} // namespace X86
