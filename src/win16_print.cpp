// SPDX-License-Identifier: GPL-2.0-or-later
// The gates of GDI.EXE a 16-bit printer driver calls, and the routines of the
// print segment of GDI.EXE (segment 8) behind them.
// A printer driver such as PSCRIPT.DRV runs as original 16-bit code in the
// process that made the printer DC (src/x86_win16_hosted.cpp). What it asks of
// GDI arrives here through the Win16 gates of GDI.EXE:
// GDI.281 DRVSETPRINTERDATA
// GDI.282 DRVGETPRINTERDATA
// Both keep the driver's private settings in the registry of the printer,
// HKLM\System\CurrentControlSet\Control\Print\Printers\<printer>, and treat
// two profile names specially: 1 or "Default DevMode" is the printer's
// default DEVMODE, 2 or "Printer Model" its driver model name.
#include "x86_print.h"
#include "win98_win16_user.h"
#include "gdi.h"
#include "gdi_font_catalog.h"
#include "gdi_truetype.h"
#include "win98_engine_font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace X86
{
namespace
{
constexpr unsigned HkeyLocalMachine = 0x80000002u;
constexpr unsigned ErrorInvalidParameter = 0x57, ErrorMoreData = 0xea, ErrorInvalidData = 0x0d,
                   ErrorInvalidPrinterName = 0x709;
constexpr unsigned RegDword = 4, RegBinary = 3, RegSz = 1;
// PRINTER_ATTRIBUTE_NETWORK and PRINTER_ATTRIBUTE_ENABLE_BIDI.
constexpr unsigned AttributeNetwork = 0x10, AttributeBidi = 0x800;
const char PrintersKey[] = "System\\CurrentControlSet\\Control\\Print\\Printers\\"; //
// KERNEL.87 compares without regard to case (KRNL386 folds both
// characters ).
bool SameProfile(const char *a, const char *b)
{
    for (;; ++a, ++b)
    {
        unsigned x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return false;
        if (!x) return true;
    }
}
} // namespace

// Guest scratch for the registry calls: the key path at +0, the handles at
// +400h, the counters after them.
struct Application::PrintRegistry
{
    Application &app;
    unsigned scratch = 0;
    explicit PrintRegistry(Application &a) : app(a) { scratch = app.Allocate(0x440); }
    ~PrintRegistry() { if (scratch) app.Free(scratch); }
    unsigned Path() const { return scratch; }
    unsigned Slot(unsigned n) const { return scratch + 0x400 + n * 4; }
    // "Printers\" + printer (+ suffix), as lstrcpy and lstrcat build it in a
    // stack buffer of 108h bytes.
    bool Key(unsigned printer, const char *suffix)
    {
        char path[0x108];
        const char *name = app.ReadString(printer);
        if (!name)
            return false;
        snprintf(path, sizeof path, "%s%s%s", PrintersKey, name, suffix ? suffix : "");
        return app.PutString(scratch, path) != 0;
    }
    unsigned Open(unsigned parent, unsigned path, unsigned slot)
    {
        const unsigned a[] = {parent, path, Slot(slot)};
        return app.Api("RegOpenKeyA", a);
    }
    void Close(unsigned key)
    {
        const unsigned a[] = {key};
        app.Api("RegCloseKey", a);
    }
    unsigned Query(unsigned key, unsigned name, unsigned type, unsigned data, unsigned size)
    {
        const unsigned a[] = {key, name, 0, type, data, size};
        return app.Api("RegQueryValueExA", a);
    }
    unsigned Set(unsigned key, unsigned name, unsigned type, unsigned data, unsigned size)
    {
        const unsigned a[] = {key, name, 0, type, data, size};
        return app.Api("RegSetValueExA", a);
    }
};

// GDI: the default DEVMODE (kind other than 1) or the model name
// (kind 1, at most 20h bytes) of a printer. Answers the size, or -1.
int Application::PrinterDefaults(unsigned printer, unsigned data, unsigned size_or_kind)
{
    PrintRegistry r(*this);
    if (!r.scratch || !r.Key(printer, nullptr) || r.Open(HkeyLocalMachine, r.Path(), 0))
        return -1;
    const unsigned key = Read32(r.Slot(0));
    const int limit = short(size_or_kind & 0xffff);
    int answer = -1;
    const unsigned name = r.scratch + 0x300;
    if (limit == 1)
    {
        // "Printer Driver" into the caller's buffer, 20h bytes; the
        // size comes back whatever the query answered.
        PutString(name, "Printer Driver");
        Write32(r.Slot(2), 0x20);
        r.Query(key, name, r.Slot(3), data, r.Slot(2));
        answer = short(Read32(r.Slot(2)) & 0xffff);
    }
    else
    {
        PutString(name, "Default DevMode");
        Write32(r.Slot(2), 0);
        if (!r.Query(key, name, r.Slot(3), 0, r.Slot(2)))
        {
            const unsigned stored = Read32(r.Slot(2));
            if (!data)
                answer = short(stored & 0xffff);
            else if (stored && short(stored & 0xffff) <= limit)
            {
                Write32(r.Slot(2), unsigned(limit));
                if (!r.Query(key, name, r.Slot(3), data, r.Slot(2)))
                    answer = short(Read32(r.Slot(2)) & 0xffff);
            }
        }
    }
    // The original leaves the key open on the model path; closing it
    // changes nothing a program can
    r.Close(key);
    return answer;
}

// GDI: the default DEVMODE written back as REG_BINARY. Answers the
// size written, or 0.
int Application::PrinterStoreDefaults(unsigned printer, unsigned data, unsigned size)
{
    PrintRegistry r(*this);
    if (!r.scratch || !r.Key(printer, nullptr) || r.Open(HkeyLocalMachine, r.Path(), 0))
        return 0;
    const unsigned key = Read32(r.Slot(0));
    const unsigned name = r.scratch + 0x300;
    PutString(name, "Default DevMode");
    const int bytes = short(size & 0xffff);
    const int answer = r.Set(key, name, RegBinary, data, unsigned(bytes)) ? 0 : bytes;
    r.Close(key);
    return answer;
}

// DRVGETPRINTERDATA, GDI. `profile_far` is the far pointer as the
// driver passed it, `profile` its flat address.
unsigned Application::PrinterGetData(unsigned printer, unsigned profile_far, unsigned profile, unsigned type,
                                     unsigned data, unsigned size, unsigned needed)
{
    if (!printer || !profile_far || !needed)
        return ErrorInvalidParameter;
    const bool named = profile_far >> 16 && profile;
    const bool model = profile_far == 2 || (named && SameProfile(ReadString(profile), "Printer Model"));
    const bool devmode = profile_far == 1 || (named && SameProfile(ReadString(profile), "Default DevMode"));
    if (devmode && profile_far != 2)
    {

        const int answer = PrinterDefaults(printer, data, size & 0xffff);
        if (answer == -1)
            return ErrorInvalidPrinterName;
        Write32(needed, unsigned(answer));
        if (data && unsigned(answer) > size)
            return ErrorMoreData;
        return 0;
    }
    if (model)
    {

        Write32(needed, 0x20);
        if (!data)
            return 0;
        if (size < 0x20)
            return ErrorMoreData;
        if (PrinterDefaults(printer, data, 1) == -1)
            return ErrorInvalidPrinterName;
        if (type)
            Write32(type, RegSz);
        return 0;
    }
    PrintRegistry r(*this);
    if (!r.scratch || !r.Key(printer, nullptr))
        return 8;
    unsigned result = r.Open(HkeyLocalMachine, r.Path(), 0);
    if (result)
        return result;
    const unsigned key = Read32(r.Slot(0));
    const unsigned name = r.scratch + 0x300;
    PutString(name, "Attributes");
    Write32(r.Slot(4), 4);
    result = r.Query(key, name, r.Slot(5), r.Slot(6), r.Slot(4));
    if (!result)
    {
        PutString(name, "\\PrinterDriverData");
        result = r.Open(key, name, 1);
    }
    if (!result)
    {
        const unsigned sub = Read32(r.Slot(1));
        const unsigned attributes = Read32(r.Slot(6));
        Write32(needed, size);
        result = r.Query(sub, profile, type, data, needed);
        if (result == 0x3f4 && (attributes & AttributeBidi) && !(attributes & AttributeNetwork))
        {
            // a bidirectional printer answers through its language
            // monitor. The value is marked -1 first, then asked of the
            // spooler (GDI, GetPrinterDataA in SPOOL32).
            Write32(r.Slot(7), 0xffffffffu);
            r.Set(sub, profile, RegDword, r.Slot(7), 4);
            result = PrinterSpoolGetData(printer, profile, type, data, size, needed);
        }
        if (!result && type && Read32(type) == RegDword && data && Read32(data) == 0xffffffffu)
            result = ErrorInvalidData;
        r.Close(sub);
    }
    r.Close(key);
    return result;
}

// DRVSETPRINTERDATA, GDI.
unsigned Application::PrinterSetData(unsigned printer, unsigned profile_far, unsigned profile, unsigned type,
                                     unsigned data, unsigned size)
{
    if (!printer || !profile_far)
        return ErrorInvalidParameter;
    const bool named = profile_far >> 16 && profile;
    const bool devmode = profile_far == 1 || (named && SameProfile(ReadString(profile), "Default DevMode"));
    if (profile_far == 2 || (!devmode && named && SameProfile(ReadString(profile), "Printer Model")))
        return ErrorInvalidParameter;
    if (devmode)
        return PrinterStoreDefaults(printer, data, size) > 0 ? 0 : ErrorInvalidPrinterName;
    PrintRegistry r(*this);
    if (!r.scratch || !r.Key(printer, "\\PrinterDriverData"))
        return 8;
    unsigned result = r.Open(HkeyLocalMachine, r.Path(), 0);
    if (result)
        return result;
    const unsigned key = Read32(r.Slot(0));
    if (data)
        result = r.Set(key, profile, type, data, size);
    else
    {
        const unsigned a[] = {key, profile};
        result = Api("RegDeleteValueA", a);
    }
    r.Close(key);
    return result;
}

// EngineEnumerateFont, GDI: the TrueType faces of GDI's engine for a
// driver, at the resolution of the DC whose fonts are being enumerated
// or at 300 dots per inch without one. Each face goes to the driver's callback as LOGFONT and
// NEWTEXTMETRIC with TRUETYPE_FONTTYPE; the answer is the callback's last.
unsigned Application::PrintEngineEnumerate(unsigned face, unsigned callback, unsigned data)
{
    if (!callback)
        return 0;
    char filter[32] = {};
    if (face)
        snprintf(filter, sizeof filter, "%s", ReadString(face));
    int dpi_x = 300, dpi_y = 300;
    if (print_enum && print_enum->dc)
    {
        unsigned char info[0x5c] = {};
        Read(print_enum->dc->gdiinfo.memory, info, sizeof info);
        dpi_x = info[0x58] | info[0x59] << 8;
        dpi_y = info[0x5a] | info[0x5b] << 8;
    }
    unsigned answer = 1;
    const unsigned installed = GdiFonts::Count();
    for (unsigned i = 0; i < installed && answer; ++i)
    {
        const auto &entry = *GdiFonts::At(i);
        if (entry.type != GdiFonts::TrueType)
            continue;
        bool repeated = false;
        for (unsigned j = 0; j < i; ++j)
            if (GdiFonts::At(j)->type == GdiFonts::TrueType && GdiFonts::SameFace(GdiFonts::At(j)->file, entry.file))
                repeated = true;
        if (repeated)
            continue;
        char physical[32] = {};
        Gdi::TEXTMETRICA tm = {};
        if (!entry.font.FaceName(physical) || !Gdi::FontMetrics(entry.font, &tm))
            continue;
        if (filter[0] && !SameProfile(filter, physical))
            continue;
        // The catalogue measures at 96 dots per inch; the engine answers at
        // the device's.
        auto scale = [](int value, int dpi) { return int((long long)value * dpi / 96); };
        tm.tmHeight = scale(tm.tmHeight, dpi_y);
        tm.tmAscent = scale(tm.tmAscent, dpi_y);
        tm.tmDescent = scale(tm.tmDescent, dpi_y);
        tm.tmInternalLeading = scale(tm.tmInternalLeading, dpi_y);
        tm.tmExternalLeading = scale(tm.tmExternalLeading, dpi_y);
        tm.tmAveCharWidth = scale(tm.tmAveCharWidth, dpi_x);
        tm.tmMaxCharWidth = scale(tm.tmMaxCharWidth, dpi_x);
        tm.tmDigitizedAspectX = dpi_x;
        tm.tmDigitizedAspectY = dpi_y;
        unsigned char record[264] = {};
        auto put = [&](unsigned at, int value) { memcpy(record + at, &value, 4); };
        put(0, tm.tmHeight);
        put(4, tm.tmAveCharWidth);
        put(16, tm.tmWeight);
        record[20] = tm.tmItalic;
        record[21] = tm.tmUnderlined;
        record[22] = tm.tmStruckOut;
        record[23] = tm.tmCharSet;
        record[27] = tm.tmPitchAndFamily;
        memcpy(record + 28, physical, strlen(physical) + 1);
        memcpy(record + 60, physical, strlen(physical) + 1);
        memcpy(record + 188, &tm, sizeof tm);
        answer = Win16FontCallback(callback, record, 4, data);
    }
    return answer;
}

// The GDI ordinals of the print segment. `words` is the frame size the
// ordinal table gives; Word(0) is the last argument.
bool Application::Win16PrintCall(unsigned ordinal, const Win16Frame &frame, unsigned &result, unsigned words)
{
    (void)words;
    auto far = [&](unsigned index) { return frame.Long(index); };
    switch (ordinal)
    {
    case 281: // DrvSetPrinterData(lpPrinter, lpProfile, dwType, lpData, dwSize), retf 14h
        result = PrinterSetData(frame.Pointer(8), far(6), frame.Pointer(6), frame.Long(4), frame.Pointer(2),
                                frame.Long(0));
        return true;
    case 282: // DrvGetPrinterData(lpPrinter, lpProfile, lpType, lpData, nSize, lpNeeded), retf 18h
        result = PrinterGetData(frame.Pointer(10), far(8), frame.Pointer(8), frame.Pointer(6), frame.Pointer(4),
                                frame.Long(2), frame.Pointer(0));
        if (log)
        {
            char text[200];
            snprintf(text, sizeof text, "PRINT DrvGetPrinterData printer=\"%s\" profile=%s%s size=%u result=%u",
                     frame.Pointer(10) ? ReadString(frame.Pointer(10)) : "", far(8) >> 16 ? "" : "#",
                     far(8) >> 16 ? ReadString(frame.Pointer(8)) : (far(8) == 1 ? "1" : "2"), frame.Long(2), result);
            log(text);
        }
        return true;
#include "win16_engine_font.inc"
    case 300: // EngineEnumerateFont(lpFaceName, lpCallback, lpData), GDI, retf 0Ch
        result = PrintEngineEnumerate(frame.Pointer(4), frame.Long(2), frame.Long(0));
        return true;
    // The job API, GDI (src/x86_print_spool.cpp).
    case 240: // OpenJob(lpOutput, lpTitle, hDC), retf 0Ah
        result = unsigned(PrintOpenJob(frame.Pointer(3), frame.Pointer(1), frame.Word(0))) & 0xffff;
        return true;
    case 241: // WriteSpool(hJob, lpData, cb), retf 8
        result = unsigned(PrintWriteSpool(frame.Word(3), frame.Pointer(1), frame.Word(0))) & 0xffff;
        return true;
    case 243: // CloseJob(hJob), retf 2
        result = unsigned(PrintCloseJob(frame.Word(0))) & 0xffff;
        return true;
    case 244: // DeleteJob(hJob, wDummy), retf 4
        result = unsigned(PrintDeleteJob(frame.Word(1))) & 0xffff;
        return true;
    case 246: // StartSpoolPage(hJob), retf 2
        result = unsigned(PrintStartSpoolPage(frame.Word(0))) & 0xffff;
        return true;
    case 247: // EndSpoolPage(hJob), retf 2
        result = unsigned(PrintEndSpoolPage(frame.Word(0))) & 0xffff;
        return true;
    case 253: // DeleteSpoolPage(hJob): GDI answers zero.
        result = 0;
        return true;
    // The document API of a program, GDI: each
    // entry checks the DC and goes to the body the 32-bit side
    // shares (src/x86_print_document.cpp).
    case 377: // StartDoc(hdc, lpdi)
    {
        // DOCINFO of the 16-bit side: cbSize, lpszDocName, lpszOutput, then
        // from 12h bytes on lpszDatatype and fwType; Windows 98 checks 0Ah bytes.
        const unsigned info = frame.Pointer(0);
        const unsigned size = info ? Win16Word(info) : 0;
        const unsigned wide = Allocate(20);
        if (!wide)
        {
            result = 0xffff;
            return true;
        }
        const unsigned values[5] = {20, info ? Win16FarPointer(info + 2) : 0, info ? Win16FarPointer(info + 6) : 0,
                                    info && size >= 14 ? Win16FarPointer(info + 10) : 0,
                                    info && size >= 18 ? Read32(info + 14) : 0};
        Store(wide, values, sizeof values);
        const unsigned a[] = {frame.Word(2), wide};
        result = Api("StartDocA", a) & 0xffff;
        Free(wide);
        return true;
    }
    case 378: // EndDoc(hdc)
    case 379: // StartPage(hdc)
    case 380: // EndPage(hdc)
    case 382: // AbortDoc(hdc)
    {
        const unsigned a[] = {frame.Word(0)};
        result = Api(ordinal == 378 ? "EndDoc" : ordinal == 379 ? "StartPage" : ordinal == 380 ? "EndPage" : "AbortDoc",
                     a) & 0xffff;
        return true;
    }
    case 381: // SetAbortProc(hdc, lpfnAbort)
    {
        const unsigned procedure = frame.Long(0);
        const unsigned a[] = {frame.Word(2), procedure ? Win16Mark(procedure) : 0};
        result = Api("SetAbortProc", a) & 0xffff;
        return true;
    }
    default:
        return false;
    }
}
} // namespace X86
