// SPDX-License-Identifier: GPL-2.0-or-later
// The exports that the original DLLs of the Win98 tree
// bind and that need a body of their own rather than a place in an existing
// family.
#include "x86_app.h"
#include <string.h>

namespace X86 {

unsigned Application::CensusImportApi(ApiName name, const unsigned *a)
{
    // GDI32 is `mov cl,1Eh` in front of the thunk table, so the body
    // is GDI.EXE ordinal 71 and its worker.
    if (API_IS(name, "EnumObjects"))
    {
        // the handle has to carry one of the device context signatures
        // 4F4Dh to 4F50h, the object type has to be 1 or 2, and the
        // callback has to point into a code segment. All three
        // failures end in the parameter reporter of KRNL386 and leave the
        // export with zero; the Win32 error codes here are the ones the
        // neighbouring exports of this port give in the same place.
        const unsigned type = Gdi::GetObjectType(a[0]);
        if (type != 3 && type != 4 && type != 10 && type != 12) { last_error = 6; return 0; }
        if (a[1] != 1 && a[1] != 2) { last_error = 87; return 0; }
        if (!a[2]) { last_error = 87; return 0; }
        // asks the device driver, entry +18h of its table, which is
        // EnumObj of DIBENG in this port's configuration. DIBENG enumerates one
        // object per colour of the surface's colour table and per style: five
        // pen styles, or one solid brush and the six hatches. A surface deeper
        // than eight bits has no colour table, so nothing is enumerated and the
        // answer is the zero that DIBENG cleared before it looked.
        unsigned colours[256];
        const unsigned count = Gdi::SurfaceColours(a[0], colours, 256);
        if (count == ~0u)
        {
            Fail("EnumObjects on a palettized surface without a colour table: the colour table "
                 "DIBENG walks at PDEVICE +20h has no counterpart here");
            return 0;
        }
        if (!count) return 0;
        // The 16-bit driver fills a ten-byte LOGPEN or LOGBRUSH; the thunk of
        // GDI32 widens it to the Win32 shape the guest callback expects, so the
        // record this port hands over is the Win32 one: LOGPEN is style, width
        // and colour in three dwords plus the unused second width word,
        // LOGBRUSH is style, colour and hatch.
        const unsigned record = Allocate(16);
        if (!record) { last_error = 8; return 0; }
        unsigned result = 0;
        // for pens, for brushes. Both walk the colour table
        // once per style, and the brush pass starts with the solid style before
        // it counts the hatches down from five.
        const unsigned passes = a[1] == 1 ? 5u : 7u;
        for (unsigned pass = 0; pass < passes; ++pass)
        {
            unsigned fields[4] = {};
            if (a[1] == 1)
                fields[0] = pass;                     // lopnStyle, PS_SOLID to PS_DASHDOTDOT
            else if (pass)
            {
                fields[0] = 2;                        // lbStyle BS_HATCHED
                fields[2] = 6 - pass;                 // lbHatch, five down to zero
            }
            for (unsigned i = 0; i < count; ++i)
            {
                // A colour table entry whose reserved byte has its top bit set
                // is skipped.
                if (colours[i] & 0x80000000u) continue;
                const unsigned colour = ((colours[i] & 0xffu) << 16) | (colours[i] & 0xff00u) |
                                        ((colours[i] >> 16) & 0xffu);
                unsigned image[4] = {fields[0], 0, 0, 0};
                if (a[1] == 1) { image[1] = 0; image[2] = 0; image[3] = colour; }
                else { image[1] = colour; image[2] = fields[2]; image[3] = 0; }
                if (!Store(record, image, 16)) { Free(record); return 0; }
                const unsigned arguments[2] = {record, a[3]};
                result = Call(a[2], arguments, 2);
                if (!result) { Free(record); return 0; }
            }
        }
        Free(record);
        return result;
    }

    // GDI32 (CL 29h) and (CL 1Dh) are bare thunks: the bodies
    // are GDI.EXE ADDFONTRESOURCE and REMOVEFONTRESOURCE,
    // which check the name (at most 100h bytes) and jump to the
    // workers. The worker opens the file, asks the engine
    // for its faces and links them into the font list at GDI DGROUP +1F2h.
    // This port has the raster fonts of the image and no font installation at
    // all, so no file adds a face, and the answer of the
    // original for a file whose faces the engine will not take is zero. The
    // thunk adds no error code of its own, so the last error stays as it was.
    if (API_IS(name, "AddFontResourceA") || API_IS(name, "RemoveFontResourceA"))
    {
        if (!a[0]) return 0;
        char path[257];
        unsigned length = 0;
        for (; length < sizeof path; ++length) {
            if (a[0] > ~length || !virtual_memory.Access(a[0] + length, 1, false) ||
                !Read(a[0] + length, path + length, 1)) return 0;
            if (!path[length]) break;
        }
        if (length == sizeof path) return 0;
        // Since the font catalogue reads the installation's files, a file the
        // guest adds goes into the same list. Removing is not ported.
        return API_IS(name, "AddFontResourceA") ? AddFontResourceFile(path) : 0;
    }
    // GDI32, CL 17h. It writes a.FOT stub file for a TrueType file,
    // which needs the rasterizer this port does not have, so the answer is the
    // zero the original gives for a file the engine declines.
    if (API_IS(name, "CreateScalableFontResourceA"))
    {
        for (unsigned i = 1; i < 4; ++i)
            if (a[i]) { ReadString(a[i]); if (state == Failed) return 0; }
        return 0;
    }
    if (API_IS(name, "CreateColorSpaceA") || API_IS(name, "DeleteColorSpace") ||
        API_IS(name, "GetLogColorSpaceA") || API_IS(name, "SetColorSpace") ||
        API_IS(name, "GetColorSpace")) return GdiIcmApi(name, a);
    // SetICMProfileA, CL 71h, stub, worker. This one does not
    // ask whether colour management is loaded at all. It wants a readable name
    // (else SetLastError(87) and FALSE ), makes the full
    // path out of the Windows directory, "COLOR" and the name when the name
    // carries no path of its own, and asks
    // GetFileAttributes for it: a file that is not there is SetLastError(2) and
    // FALSE. Only then does it write the name into the device context
    // at +BEh and answer TRUE.
    if (API_IS(name, "SetICMProfileA"))
    {
        const unsigned kind = Gdi::GetObjectType(a[0]);
        if (kind != 3 && kind != 4 && kind != 10 && kind != 12) { last_error = 6; return 0; }
        if (!a[1]) { last_error = 87; return 0; }
        const char *given = ReadString(a[1]);
        if (state == Failed || !given) { last_error = 87; return 0; }
        char path[520] = {};
        unsigned at = 0;
        bool own_path = false;
        for (unsigned i = 0; given[i]; ++i)
            if (given[i] == '\\' || given[i] == '/' || given[i] == ':') own_path = true;
        if (!own_path)
        {
            const unsigned directory = Allocate(260);
            if (!directory) { last_error = 8; return 0; }
            const unsigned arguments[2] = {directory, 260};
            const unsigned written = Api("GetWindowsDirectoryA", arguments);
            char windows[264] = {};
            const bool read = written && written < 260 && Read(directory, windows, written + 1);
            Free(directory);
            if (!read) { last_error = 87; return 0; }
            for (unsigned i = 0; windows[i] && at < 400; ++i) path[at++] = windows[i];
            if (at && path[at - 1] != '\\') path[at++] = '\\';
            const char folder[] = "COLOR\\";
            for (unsigned i = 0; folder[i] && at < 480; ++i) path[at++] = folder[i];
        }
        for (unsigned i = 0; given[i] && at < 500; ++i) path[at++] = given[i];
        path[at] = 0;
        const unsigned buffer = Allocate(at + 1);
        if (!buffer) { last_error = 8; return 0; }
        unsigned attributes = ~0u;
        if (Store(buffer, path, at + 1))
        {
            const unsigned arguments[1] = {buffer};
            attributes = Api("GetFileAttributesA", arguments);
        }
        Free(buffer);
        if (attributes == ~0u) { last_error = 2; return 0; }
        return Gdi::SetColorProfile(a[0], path);
    }

    // The five nameless printer entries of GDI32, ordinals 100 to 104, word
    // thunks with CL 0Dh to 9 into the print segment of GDI.EXE. They ask the 16-bit printer driver about itself;
    // src/x86_print_setup.cpp. Without an installed printer Windows 98 finds no
    // driver and each answers minus one without an error code.
    if (API_IS(name, "ordinal_100") || API_IS(name, "ordinal_101") || API_IS(name, "ordinal_102") ||
        API_IS(name, "ordinal_103") || API_IS(name, "ordinal_104"))
        return PrinterSetupApi(API_IS(name, "ordinal_100")   ? 100
                               : API_IS(name, "ordinal_101") ? 101
                               : API_IS(name, "ordinal_102") ? 102
                               : API_IS(name, "ordinal_103") ? 103
                                                             : 104,
                               a);

    // WINMM!DriverCallback, the way back from a driver to the
    // program. It switches on the whole flag dword, not on its low bits:
    // DCB_WINDOW (1) posts to a window, DCB_TASK (2) to a thread, DCB_FUNCTION
    // (3) calls the procedure directly and answers TRUE, DCB_EVENT (5) sets an
    // event, and every other value answers FALSE without doing anything. The
    // three posting forms answer whatever they called answered.
    if (API_IS(name, "DriverCallback"))
    {
        switch (a[1])
        {
        case 1: { const unsigned message[4] = {a[0], a[3], a[2], a[5]};
                  return Api("PostMessageA", message); }
        case 2: { const unsigned message[4] = {a[0], a[3], a[2], a[5]};
                  return Api("PostThreadMessageA", message); }
        case 3: { const unsigned arguments[5] = {a[2], a[3], a[4], a[5], a[6]};
                  Call(a[0], arguments, 5);
                  return 1; }
        case 5: { const unsigned event[1] = {a[0]};
                  return Api("SetEvent", event); }
        default:
            return 0;
        }
    }
    Fail(name);
    return 0;
}
} // namespace X86
