// SPDX-License-Identifier: GPL-2.0-or-later
// The document of a printer DC: StartDoc, StartPage, EndPage, EndDoc,
// AbortDoc, SetAbortProc and Escape on a DC whose device is a 16-bit printer
// driver. GDI.EXE turns each of them into a Control call of the driver; the driver
// writes its output through the job API of src/x86_print_spool.cpp.
// A driver whose GDIINFO asks for banding (dpRaster & RC_BANDING) or that
// accepts EMF spooling (dpCaps1 & C1_EMF_COMPLIANT) makes GDI record each
// page into a metafile and play it back band by band or hand it to
// the spooler as EMF. PSCRIPT.DRV asks for neither, so GDI calls it
// directly; the recording paths stop with a diagnosis here.
#include "x86_print.h"
#include "gdi.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
using namespace Print;
namespace
{
// The escapes of the document (GDI escape numbers).
enum : unsigned
{
    EscNewFrame = 1, EscAbortDoc = 2, EscNextBand = 3, EscQueryEscSupport = 8, EscSetAbortProc = 9,
    EscStartDoc = 10, EscEndDoc = 11, EscGetPhysPageSize = 12, EscGetPrintingOffset = 13,
    EscGetScalingFactor = 14, EscPassThrough = 19
};
// The document bits of GDI's DC+100h : RC_BANDING and EMF spooling,
// a page open, the document started.
enum : unsigned
{
    DocBanding = 0x0001, DocEmf = 0x0002, DocRecording = 0x0010, DocPage = 0x0020, DocStarted = 0x0100
};
constexpr unsigned RcBanding = 0x0002, C1EmfCompliant = 0x0008;
} // namespace

// MapLS for the flat thunks of GDI32 (SMapLS_IP_EBP_n): a selector whose base
// is the flat address, so the 16-bit driver reads and writes the caller's
// memory itself. NULL stays NULL.
unsigned Application::PrintMapFlat(unsigned flat)
{
    if (!flat || !Win16HostedReady())
        return 0;
    const unsigned selector = Win16Segment(flat, 0xffff, false, false);
    return selector ? selector << 16 : 0;
}
void Application::PrintUnmapFar(unsigned far)
{
    if (far)
        selectors.Unmap(far & 0xffff0000u);
}

// Control(lpPDevice, wFunction, lpInData, lpOutData), DDI ordinal 3.
int Application::PrintControl(PrintDc &dc, unsigned escape, unsigned in_far, unsigned out_far)
{
    bool ok = false;
    const unsigned push[] = {dc.pdevice.selector, 0, escape, in_far >> 16, in_far & 0xffff, out_far >> 16,
                             out_far & 0xffff};
    const int result = short(PrintCall(*dc.driver, DdiControl, push, 7, ok) & 0xffff);
    return ok ? result : -1;
}

// GDI: the document bits of StartDoc. EMF spooling and banding are the
// recording paths; the direct path needs neither.
unsigned Application::PrintDocumentMode(PrintDc &dc, unsigned fwtype)
{
    unsigned char info[0x70] = {};
    Read(dc.gdiinfo.memory, info, sizeof info);
    const unsigned raster = info[0x26] | info[0x27] << 8, caps1 = info[0x5e] | info[0x5f] << 8;
    if (fwtype & 1)
        return 8; // DI_APPBANDING
    if (raster & RcBanding)
        return DocBanding;
    // EMF needs C1_EMF_COMPLIANT among other conditions; without
    // that bit the answer is the direct path.
    if (caps1 & C1EmfCompliant)
        return DocEmf;
    return 0;
}

// StartDoc, GDI with the core, and the conversion of GDI32

int Application::PrinterStartDoc(PrintDc &dc, unsigned docinfo)
{
    if (dc.document & DocStarted)
        return -1;
    // a DOCINFOA of 14h bytes carries name, output, datatype and
    // fwType; any other size only name and output. then keeps name and
    // output and clears the rest.
    unsigned name = 0, output = 0, fwtype = 0;
    if (docinfo)
    {
        const unsigned size = Read32(docinfo);
        name = Read32(docinfo + 4);
        output = Read32(docinfo + 8);
        if (size == 0x14)
            fwtype = Read32(docinfo + 0x10);
    }
    const unsigned mode = PrintDocumentMode(dc, fwtype);
    if (mode)
    {
        Fail("StartDoc on a printer DC whose driver bands or spools EMF is not built yet");
        return -1;
    }
    dc.document = mode;
    const unsigned name_far = PrintMapFlat(name), output_far = PrintMapFlat(output);
    // DOCINFO of 16 bits in the scratch block: cbSize 12h, the two far
    // pointers, no datatype, no fwType.
    const unsigned info = dc.scratch.memory + 0x100;
    Win16Word(info, 0x12);
    Write32(info + 2, name_far);
    Write32(info + 6, output_far);
    Write32(info + 10, 0);
    Write32(info + 14, 0);
    unsigned char gdiinfo[2] = {};
    Read(dc.gdiinfo.memory, gdiinfo, 2);
    const unsigned version = gdiinfo[0] | gdiinfo[1] << 8;
    dc.in_call = true;
    const int result = PrintControl(dc, EscStartDoc, name_far, version > 0x300 ? dc.scratch.Far(0x100) : 0);
    dc.in_call = false;
    PrintUnmapFar(name_far);
    PrintUnmapFar(output_far);
    if (result > 0)
    {
        dc.document |= DocStarted;
        return 1;
    }
    return result;
}

// StartPage, GDI: no driver call on the direct path.
int Application::PrinterStartPage(PrintDc &dc)
{
    if (!(dc.document & DocStarted))
        return -1;
    dc.document |= DocPage;
    return 1;
}

// EndPage, GDI: Control(NEWFRAME).
int Application::PrinterEndPage(PrintDc &dc)
{
    if (!(dc.document & DocPage))
        return -1;
    dc.in_call = true;
    const int result = PrintControl(dc, EscNewFrame, 0, 0);
    dc.in_call = false;
    dc.document &= ~DocPage;
    return result;
}

// EndDoc, GDI: Control(ENDDOC). A page still open is an error for a
// program of Windows 3.1 and later.
int Application::PrinterEndDoc(PrintDc &dc)
{
    if (!(dc.document & DocStarted))
        return -1;
    if (dc.document & DocPage)
        return -1;
    dc.in_call = true;
    const int result = PrintControl(dc, EscEndDoc, 0, 0);
    dc.in_call = false;
    dc.document = 0;
    return result;
}

// AbortDoc, GDI: the abort procedure goes, an open page is ended
// first, then Control(ABORTDOC).
int Application::PrinterAbortDoc(PrintDc &dc)
{
    if (!(dc.document & DocStarted))
        return -1;
    dc.abort_procedure = 0;
    if (dc.document & DocPage)
        PrinterEndPage(dc);
    dc.in_call = true;
    const int result = PrintControl(dc, EscAbortDoc, 0, 0);
    dc.in_call = false;
    dc.document = 0;
    return result;
}

// The document group of GDI32 for a printer DC. GDI32 turns a
// negative StartDoc answer into the Win32 error and answers SP_ERROR; the
// page and document calls hand the sign-extended word back.
unsigned Application::PrinterDocumentApi(ApiName name, const unsigned *a)
{
    PrintDc *dc = PrintFind(a[0]);
    if (!dc)
        return ~0u;
    if (API_IS(name, "SetAbortProc"))
    {
        // wraps the procedure with AllocSLCallback; Windows 98 stores it
        // at DC+D0h and answers TRUE. QUERYABORT calls it back.
        dc->abort_procedure = a[1];
        return 1;
    }
    if (dc->information)
        return ~0u;
    if (API_IS(name, "StartDocA"))
    {
        const int result = PrinterStartDoc(*dc, a[1]);
        if (result >= 0)
            return unsigned(result);
        switch (result)
        {
        case -1:
            if (!last_error)
                last_error = 87;
            break;
        case -4:
            last_error = 0x70;
            break;
        case -5:
            last_error = 8;
            break;
        case -3:
            last_error = 0x3f;
            break;
        default:
            break;
        }
        return ~0u;
    }
    if (API_IS(name, "StartPage"))
        return unsigned(PrinterStartPage(*dc));
    if (API_IS(name, "EndPage"))
        return unsigned(PrinterEndPage(*dc));
    if (API_IS(name, "EndDoc"))
        return unsigned(PrinterEndDoc(*dc));
    if (API_IS(name, "AbortDoc"))
        return unsigned(PrinterAbortDoc(*dc));
    if (API_IS(name, "ResetDCA"))
    {
        Fail("ResetDC on a printer DC (GDI, Control(RESETDEVICE)) is not built yet");
        return 0;
    }
    return ~0u;
}

// Escape and ExtEscape on a printer DC once GDI32 has let the number through
// (src/x86_gdi_device.cpp). converts the data of each escape between
// the 32-bit and the 16-bit form; GDI then calls Control with DC+FEh
// bit 10h set around it.
unsigned Application::PrinterEscape(unsigned hdc, unsigned escape, unsigned in_bytes, unsigned in,
                                    unsigned out)
{
    PrintDc *dc = PrintFind(hdc);
    if (!dc)
        return ~0u;
    const unsigned scratch = dc->scratch.memory + 0x200;
    int result = 0;
    dc->in_call = true;
    switch (escape)
    {
    case EscQueryEscSupport:
    {
        // An INT of 32 bits becomes the WORD the driver reads.
        unsigned queried = 0;
        if (!in || !Read(in, &queried, in_bytes >= 4 ? 4 : 2))
        {
            dc->in_call = false;
            return 0;
        }
        Win16Word(scratch, queried & 0xffff);
        result = PrintControl(*dc, escape, dc->scratch.Far(0x200), 0);
        break;
    }
    case EscGetPhysPageSize:
    case EscGetPrintingOffset:
    case EscGetScalingFactor:
    {
        // A POINT of two WORDs from the driver, two LONGs for the caller.
        Win16Word(scratch, 0);
        Win16Word(scratch + 2, 0);
        result = PrintControl(*dc, escape, 0, dc->scratch.Far(0x200));
        if (out)
        {
            Write32(out, unsigned(int(short(Win16Word(scratch)))));
            Write32(out + 4, unsigned(int(short(Win16Word(scratch + 2)))));
        }
        break;
    }
    case EscPassThrough:
    {
        // A WORD count and the bytes, the same in both forms.
        const unsigned in_far = PrintMapFlat(in);
        result = PrintControl(*dc, escape, in_far, 0);
        PrintUnmapFar(in_far);
        break;
    }
    case EscNewFrame:
    case EscStartDoc:
    case EscEndDoc:
    case EscAbortDoc:
    case EscSetAbortProc:
    case EscNextBand:
        // GDI32 refuses these for an image of version 4.00 and later before
        // they get here; an older image takes the document path.
    default:
    {
        dc->in_call = false;
        char text[160];
        snprintf(text, sizeof text,
                 "Escape %u on a printer DC needs the conversion of GDI32, which is not built yet", escape);
        Fail(text);
        return 0;
    }
    }
    dc->in_call = false;
    return unsigned(result);
}
} // namespace X86
