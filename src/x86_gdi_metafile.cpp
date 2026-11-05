// SPDX-License-Identifier: GPL-2.0-or-later
// GDI32 metafiles: recording DCs, record serialization and playback through
// the native GDI calls.
#include "x86_app.h"
#include "win98_metafile.h"
#include "win98_metafile_emf.h"
#include <string.h>
#include <stdlib.h>
namespace X86
{
namespace
{
// GDI's growing object table reserves the E... tag from its generations.
// Metafile identities keep their separate slot and generation encoding.
constexpr unsigned MetafileTag = 0xe0000000u, MetafileTagMask = 0xf0000000u;
constexpr unsigned MetafileSlots = 32, MetafileObjects = 64, MetafileLimit = 4u << 20;
unsigned NameId(const char *text) { return ApiId(text); }
void Put16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
void Put32(unsigned char *p, unsigned v) { Put16(p, v & 65535); Put16(p + 2, v >> 16); }
unsigned Get16(const unsigned char *p) { return p[0] | (unsigned(p[1]) << 8); }
unsigned Get32(const unsigned char *p) { return Get16(p) | (Get16(p + 2) << 16); }
int Signed16(unsigned v) { return int(v & 32767) - int(v & 32768); }
} // namespace

// A metafile owns a DC while recording. After CloseMetaFile, bits contains
// the complete header and record image.
struct Application::MetafileSlot
{
    bool used = false, recording = false, enhanced = false;
    unsigned generation = 1;
    unsigned dc = 0;
    unsigned char *bits = nullptr;
    unsigned bytes = 0, capacity = 0;
    unsigned objects = 0, max_record = 0;
    unsigned object_handles[MetafileObjects] = {};
    char file[260] = {};
    // For enhanced metafiles, create the header when closing.
    unsigned records = 0, device[2] = {}, millimeters[2] = {};
    unsigned saved_depth = 0;
    int bounds[4] = {0, 0, -1, -1}, frame[4] = {0, 0, -1, -1};
    bool framed = false, bounded = false;
    unsigned char description[512] = {};
    unsigned description_bytes = 0;
};
// Each active enumeration has its own logical mapping. The destination DC
// exposes the composed mapping only once an output record realizes it.
struct EnhancedPlayback
{
    EnhancedPlayback *previous = nullptr;
    unsigned dc = 0, handles = 0, logical = 0, scratch = 0;
    double scale[2] = {1,1}, offset[2] = {};
};
struct Application::MetafileState
{
    EnhancedPlayback *playback = nullptr;
    MetafileSlot entries[MetafileSlots];
    unsigned recording_count = 0;
};

Application::MetafileState &Application::Metafiles()
{
    if (!metafiles)
        metafiles = new MetafileState();
    return *metafiles;
}

void Application::FreeMetafiles()
{
    if (!metafiles)
        return;
    for (auto &e : metafiles->entries)
        free(e.bits);
    delete metafiles;
    metafiles = nullptr;
}

Application::MetafileSlot *Application::MetafileEntry(unsigned handle)
{
    if (!metafiles || (handle & MetafileTagMask) != MetafileTag || (handle & 0xfff))
        return nullptr;
    const unsigned index = (handle >> 12) & 255;
    if (!index || index > MetafileSlots)
        return nullptr;
    auto &e = metafiles->entries[index - 1];
    return e.used && e.generation == ((handle >> 20) & 255) ? &e : nullptr;
}

Application::MetafileSlot *Application::RecordingMetafile(unsigned dc)
{
    if (!metafiles || !metafiles->recording_count || !dc)
        return nullptr;
    for (auto &e : metafiles->entries)
        if (e.used && e.recording && e.dc == dc)
            return &e;
    return nullptr;
}

unsigned Application::MetafileHandleOf(const MetafileSlot *entry) const
{
    const unsigned index = unsigned(entry - metafiles->entries);
    return MetafileTag | (entry->generation << 20) | ((index + 1) << 12);
}

// Append rdSize (DWORD), rdFunction (WORD) and parameter words. rdSize counts
// words including the record header.
bool Application::MetafileWrite(MetafileSlot &e, unsigned function,
                                const unsigned short *parameters, unsigned words)
{
    const unsigned size = 3 + words;
    const unsigned extra = size * 2;
    if (e.bytes + extra > MetafileLimit)
    {
        last_error = 8;
        return false;
    }
    if (e.bytes + extra > e.capacity)
    {
        unsigned capacity = e.capacity ? e.capacity * 2 : 1024;
        while (capacity < e.bytes + extra)
            capacity *= 2;
        auto *grown = static_cast<unsigned char *>(realloc(e.bits, capacity));
        if (!grown)
        {
            last_error = 8;
            return false;
        }
        e.bits = grown;
        e.capacity = capacity;
    }
    unsigned char *at = e.bits + e.bytes;
    Put32(at, size);
    Put16(at + 4, function);
    for (unsigned i = 0; i < words; ++i)
        Put16(at + 6 + i * 2, parameters[i]);
    e.bytes += size * 2;
    if (size > e.max_record)
        e.max_record = size;
    return true;
}

namespace
{
// shape describes Win32 arguments after HDC. Store the last argument first in
// rdParm to match the 16-bit Pascal playback layout.
unsigned PackShape(const char *shape, const unsigned *a, unsigned short *parm)
{
    unsigned words = 0;
    const unsigned length = unsigned(strlen(shape));
    for (unsigned i = length; i-- > 0;)
    {
        const unsigned value = a[1 + i];
        if (shape[i] == '0')
            continue;
        if (shape[i] == 'd')
        {
            parm[words++] = (unsigned short)value;
            parm[words++] = (unsigned short)(value >> 16);
        }
        else
            parm[words++] = (unsigned short)value;
    }
    return words;
}
bool UnpackShape(const char *shape, const unsigned char *parm, unsigned words, unsigned *a)
{
    unsigned used = 0;
    const unsigned length = unsigned(strlen(shape));
    for (unsigned i = length; i-- > 0;)
    {
        if (shape[i] == '0')
        {
            a[1 + i] = 0;
            continue;
        }
        const unsigned need = shape[i] == 'd' ? 2u : 1u;
        if (used + need > words)
            return false;
        if (shape[i] == 'd')
            a[1 + i] = Get32(parm + used * 2);
        else if (shape[i] == 'w')
            a[1 + i] = unsigned(Signed16(Get16(parm + used * 2)));
        else
            a[1 + i] = Get16(parm + used * 2);
        used += need;
    }
    return used == words;
}
// These four table records have no native provider. Playback fails instead of
// reporting success.
bool Playable(unsigned function)
{
    return function != 0x0105 && function != 0x0419 && function != 0x0548 && function != 0x0231;
}
// Windows 98 records surface changes and device-state calls. If an
// unsupported call reaches a recording DC, stop with a diagnostic rather than
// silently dropping it.
constexpr const char *MustRecordNames[] = {
    "BitBlt",         "StretchBlt",      "StretchDIBits",   "SetDIBitsToDevice",
    "MaskBlt",        "PlgBlt",          "FillRect",        "FrameRect",
    "InvertRect",     "DrawFocusRect",   "TextOutW",        "ExtTextOutW",
    "FillRgn",        "FrameRgn",        "InvertRgn",       "PaintRgn",
    "SelectClipRgn",  "ExtSelectClipRgn","SetPixel",        "SetPixelV",
    "SelectPalette",  "RealizePalette",  "AnimatePalette",  "SetPaletteEntries",
    "ResizePalette",  "PolyBezier",      "PolyBezierTo",    "PolylineTo",
    "PolyPolyline",   "PolyPolygon",     "Polygon",         "Polyline",
    "TextOutA",       "ExtTextOutA",     "SetWorldTransform","ModifyWorldTransform",
    "SetGraphicsMode","SetTextJustification","SetTextCharacterExtra","LineTo",
    "Rectangle",      "Ellipse",         "RoundRect",       "Arc",
    "Chord",          "Pie",             "PatBlt",          "SelectObject"};
constexpr ApiIdTable MustRecordIds(MustRecordNames);
} // namespace

// Return true when the call belongs to a metafile DC and has been recorded.
// Recording does not draw to the surface.
bool Application::MetafileRecord(ApiName name, const unsigned *a, unsigned &result)
{
    if (!metafiles || !metafiles->recording_count)
        return false;
    // DeleteObject identifies an object rather than a DC. Emit a deletion
    // record for every open metafile containing it, then delete the object
    // normally.
    if (API_IS(name, "DeleteObject"))
    {
        for (auto &open : metafiles->entries)
            if (open.used && open.recording)
                for (unsigned i = 0; i < MetafileObjects; ++i)
                    if (open.object_handles[i] == a[0])
                    {
                        const unsigned short index = (unsigned short)i;
                        if (open.enhanced)
                        {
                            unsigned char payload[4]; Put32(payload, i);
                            EnhancedWrite(open, Win98MetafileEmf::DeleteObject, payload, 4);
                        }
                        else MetafileWrite(open, Win98Metafile::DeleteObject, &index, 1);
                        open.object_handles[i] = 0;
                    }
        return false;
    }
    auto *entry = RecordingMetafile(a[0]);
    if (!entry)
        return false;
    if (API_IS(name,"TextOutW") || API_IS(name,"ExtTextOutW"))
    {
        result = WideTextApi(name, a);
        return true;
    }
    MetafileSlot &e = *entry;
    if (API_IS(name,"FillRect"))
    {
        int rect[4];
        if(!a[1] || !Read(a[1],rect,sizeof rect)){result=0;return true;}
        unsigned brush=a[2];
        if(brush && brush<=32) { const unsigned color[]={brush-1};brush=Api("GetSysColorBrush",color); }
        const unsigned select[]={a[0],brush};
        const unsigned old=GdiApi("SelectObject",select);
        if(!old){result=0;return true;}
        const unsigned fill[]={a[0],unsigned(rect[0]),unsigned(rect[1]),unsigned(rect[2]-rect[0]),unsigned(rect[3]-rect[1]),Gdi::PATCOPY};
        result=GdiApi("PatBlt",fill);
        const unsigned restore[]={a[0],old};GdiApi("SelectObject",restore);
        return true;
    }
    if (e.enhanced)
        return EnhancedRecord(e, name, a, result);
    if(API_IS(name,"BeginPath") || API_IS(name,"EndPath") || API_IS(name,"AbortPath") ||
       API_IS(name,"CloseFigure") || API_IS(name,"FillPath") || API_IS(name,"StrokePath"))
    { result=0;return true; }
    // Records with fixed parameter lists.
    for (unsigned i = 0; i < Win98Metafile::TableCount; ++i)
    {
        const auto &table = Win98Metafile::Table[i];
        if (NameId(table.api) != name.id)
            continue;
        unsigned short parm[16];
        const unsigned words = PackShape(table.shape, a, parm);
        result = MetafileWrite(e, table.function, parm, words);
        return true;
    }
    switch (name.id)
    {
    case API_ID("StretchBlt"):
    {
        // GDI -> : META_DIBSTRETCHBLT embeds the entire
        // source bitmap, omitting its HDC from the reversed WORD arguments.
        // rounds the DDB depth to 1/4/8/24; Windows 98 builds BI_RGB.
        result = 0;
        const unsigned short parameters[] = {
            (unsigned short)a[10], (unsigned short)(a[10] >> 16),
            (unsigned short)a[9], (unsigned short)a[8],
            (unsigned short)a[7], (unsigned short)a[6],
            (unsigned short)a[5], (unsigned short)a[4],
            (unsigned short)a[3], (unsigned short)a[2], (unsigned short)a[1]};
        if (!a[5] || a[5] == a[0]) {
            result = MetafileWrite(e, 0x0b41, parameters, 11);
            return true;
        }
        const unsigned bitmap = Gdi::GetCurrentObject(a[5], 7);
        if (Gdi::GetObjectType(a[5]) != 10) return true;
        unsigned object[6] = {};
        const unsigned query_buffer = Allocate(sizeof object);
        if (!query_buffer) return true;
        const unsigned query[] = {bitmap, sizeof object, query_buffer};
        const bool queried = GdiApi("GetObjectA", query) == sizeof object &&
                             Read(query_buffer, object, sizeof object);
        Free(query_buffer);
        if (!queried) return true;
        const unsigned width = object[1], height = object[2];
        const unsigned source_depth = (object[4] & 65535) * (object[4] >> 16);
        const unsigned depth = source_depth <= 1 ? 1 : source_depth <= 4 ? 4 : source_depth <= 8 ? 8 : 24;
        const unsigned colors = depth <= 8 ? 1u << depth : 0;
        const unsigned info = 40 + colors * 4;
        const unsigned long long pixel_bytes = ((static_cast<unsigned long long>(width) * depth + 31) / 32) * 4 * height;
        if (!width || !height || pixel_bytes + info + 26 > MetafileLimit) {
            last_error = 8;
            return true;
        }
        const unsigned bytes = info + unsigned(pixel_bytes);
        const unsigned scratch = Allocate(bytes);
        auto *payload = static_cast<unsigned char *>(calloc(1, 20 + bytes));
        if (!scratch || !payload) {
            if (scratch) Free(scratch);
            free(payload);
            last_error = 8;
            return true;
        }
        memcpy(payload, parameters, 12);
        memcpy(payload + 12, parameters + 7, 8);
        Put32(payload + 20, 40); Put32(payload + 24, width);
        Put32(payload + 28, height); Put16(payload + 32, 1); Put16(payload + 34, depth);
        const unsigned extract[] = {a[5], bitmap, 0, height, scratch + info, scratch, 0};
        if (Store(scratch, payload + 20, bytes) &&
            GdiApi("GetDIBits", extract) == height && Read(scratch, payload + 20, bytes))
            result = MetafileWrite(e, 0x0b41, reinterpret_cast<unsigned short *>(payload), (20 + bytes) / 2);
        free(payload);
        Free(scratch);
        return true;
    }
    case API_ID("SelectObject"):
        result = MetafileSelect(e, a[0], a[1]);
        return true;
    case API_ID("Polygon"):
    case API_ID("Polyline"):
        result = MetafilePoly(e, name, a);
        return true;
    case API_ID("PolyPolygon"):
        result = MetafilePolyPolygon(e, a);
        return true;
    case API_ID("TextOutA"):
    case API_ID("ExtTextOutA"):
        result = MetafileText(e, name, a);
        return true;
    default:
        break;
    }
    if (MustRecordIds.Has(name.id))
    {
        MetafileUnsupported(name, false);
        result = 0;
        return true;
    }
    return false;
}

// Win98 DrawIcon records each plane as a DIB stretch blit, bracketed by
// background/text colour and stretch-mode changes. Keeping these records
// also preserves EnumMetaFile's callback and cancellation opportunities.
bool Application::MetafileIcon(unsigned dc, int x, int y,
                                const WinImage::Image &image, unsigned flags,
                                unsigned &result)
{
    auto *entry = RecordingMetafile(dc);
    if (!entry) return false;
    result = 0;
    if (entry->enhanced) {
        MetafileUnsupported("DrawIcon", true);
        return true;
    }
    if (!image.color || !image.mask || !(flags & 3)) return true;
    auto state_record = [&](unsigned function, unsigned value, unsigned words) {
        const unsigned short parameters[] = {(unsigned short)value, (unsigned short)(value >> 16)};
        return MetafileWrite(*entry, function, parameters, words);
    };
    // Setters on a WMF DC return the recording success value (1), which
    // USER passes back as the old state. The original icon WMF confirms it.
    for (unsigned plane = 1; plane <= 2; ++plane) {
        if (!(flags & plane)) continue;
        const unsigned depth = plane == 1 ? 1 : 24;
        const unsigned stride = ((image.width * depth + 31) / 32) * 4;
        const unsigned info = 40 + (plane == 1 ? 8 : 0);
        const unsigned bytes = 20 + info + stride * image.height;
        auto *payload = static_cast<unsigned char *>(calloc(1, bytes));
        if (!payload) { last_error = 8; return true; }
        const unsigned rop = (flags & 3) == 3 ? (plane == 1 ? Gdi::SRCAND : Gdi::SRCINVERT) : Gdi::SRCCOPY;
        Put32(payload, rop);
        Put16(payload + 4, image.height); Put16(payload + 6, image.width);
        Put16(payload + 12, image.height); Put16(payload + 14, image.width);
        Put16(payload + 16, unsigned(y)); Put16(payload + 18, unsigned(x));
        Put32(payload + 20, 40); Put32(payload + 24, image.width);
        Put32(payload + 28, image.height); Put16(payload + 32, 1); Put16(payload + 34, depth);
        if (plane == 1) Put32(payload + 64, 0x00ffffff);
        for (unsigned row = 0; row < image.height; ++row) {
            auto *out = payload + 20 + info + (image.height - 1 - row) * stride;
            for (unsigned column = 0; column < image.width; ++column) {
                const unsigned index = row * image.width + column;
                if (plane == 1) {
                    if (image.mask[index]) out[column / 8] |= 128 >> (column & 7);
                } else {
                    const unsigned color = image.color[index];
                    out[column * 3] = (unsigned char)color;
                    out[column * 3 + 1] = (unsigned char)(color >> 8);
                    out[column * 3 + 2] = (unsigned char)(color >> 16);
                }
            }
        }
        const bool written = state_record(0x0201, 0xffffff, 2) &&
            state_record(0x0209, 0, 2) && state_record(0x0107, plane == 1 ? 1 : 3, 1) &&
            MetafileWrite(*entry, 0x0b41, reinterpret_cast<unsigned short *>(payload), bytes / 2) &&
            state_record(0x0107, 1, 1) && state_record(0x0209, 1, 2) && state_record(0x0201, 1, 2);
        free(payload);
        if (!written) return true;
    }
    result = 1;
    return true;
}

// Stop execution and identify the call with no supported record.
void Application::MetafileUnsupported(ApiName name, bool enhanced)
{
    static char why[160] = {};
    const char *prefix = enhanced ? "This GDI call has no enhanced metafile record in NEO98: "
                                  : "This GDI call has no metafile record in NEO98: ";
    unsigned i = 0;
    for (; prefix[i] && i < sizeof why - 1; ++i)
        why[i] = prefix[i];
    for (unsigned j = 0; name.text && name.text[j] && i < sizeof why - 1; ++j, ++i)
        why[i] = name.text[j];
    why[i] = 0;
    Fail(why);
}

// SelectObject assigns a metafile object-table slot, emits its creation
// record if needed, then META_SELECTOBJECT. Keep the backing DC state
// synchronized so the return value identifies the actual previously selected
// object.
unsigned Application::MetafileSelect(MetafileSlot &e, unsigned dc, unsigned object)
{
    const unsigned type = Gdi::GetObjectType(object);
    if (type != 1 && type != 2 && type != 6)
    {
        Fail("SelectObject on a metafile DC supports pens, brushes and fonts only");
        return 0;
    }
    unsigned index = MetafileObjects;
    for (unsigned i = 0; i < MetafileObjects; ++i)
        if (e.object_handles[i] == object)
        {
            index = i;
            break;
        }
    if (index == MetafileObjects)
    {
        for (unsigned i = 0; i < MetafileObjects; ++i)
            if (!e.object_handles[i])
            {
                index = i;
                break;
            }
        if (index == MetafileObjects)
        {
            last_error = 8;
            return 0;
        }
        if (!MetafileCreateRecord(e, object, type))
            return 0;
        e.object_handles[index] = object;
        if (index + 1 > e.objects)
            e.objects = index + 1;
    }
    const unsigned short word = (unsigned short)index;
    if (!MetafileWrite(e, Win98Metafile::SelectObject, &word, 1))
        return 0;
    const unsigned previous = Gdi::SelectObject(dc, object);
    // A caller may keep the initial stock object after CloseMetaFile destroys
    // this private recording DC. Return the process stock brush, whose lifetime
    // does not depend on that DC.
    return type == 2 && Gdi::IsPrivateDefault(previous) ? stock[0] : previous;
}

// Creation records use LOGPEN16 (10 bytes), LOGBRUSH16 (8 bytes) and
// LOGFONT16 (50 bytes). GetObjectA supplies the native 32-bit form.
bool Application::MetafileCreateRecord(MetafileSlot &e, unsigned object, unsigned type)
{
    if(type==2)
    {
        const unsigned bytes=Gdi::PatternBrushDib(object,nullptr,0);
        if(bytes)
        {
            auto *payload=new unsigned short[(bytes+4)/2];
            payload[0]=3;payload[1]=0; // BS_PATTERN, DIB_RGB_COLORS
            const bool ok=Gdi::PatternBrushDib(object,reinterpret_cast<unsigned char *>(payload+2),bytes)==bytes &&
                          MetafileWrite(e,0x142,payload,(bytes+4)/2);
            delete[] payload;return ok;
        }
    }
    const unsigned scratch = Allocate(64);
    if (!scratch)
    {
        last_error = 8;
        return false;
    }
    unsigned char raw[64] = {};
    const unsigned request[] = {object, 64, scratch};
    const unsigned copied = GdiApi("GetObjectA", request);
    bool ok = copied && Read(scratch, raw, copied <= 64 ? copied : 64);
    Free(scratch);
    if (!ok)
    {
        last_error = 6;
        return false;
    }
    unsigned short parm[32] = {};
    unsigned words = 0, function = 0;
    if (type == 1) // LOGPEN32: style, POINT width, color
    {
        function = Win98Metafile::CreatePenIndirect;
        parm[0] = (unsigned short)Get32(raw);
        parm[1] = (unsigned short)Get32(raw + 4);
        parm[2] = (unsigned short)Get32(raw + 8);
        parm[3] = (unsigned short)Get32(raw + 12);
        parm[4] = (unsigned short)(Get32(raw + 12) >> 16);
        words = 5;
    }
    else if (type == 2) // LOGBRUSH32: style, color, hatch
    {
        function = Win98Metafile::CreateBrushIndirect;
        parm[0] = (unsigned short)Get32(raw);
        parm[1] = (unsigned short)Get32(raw + 4);
        parm[2] = (unsigned short)(Get32(raw + 4) >> 16);
        parm[3] = (unsigned short)Get32(raw + 8);
        words = 4;
    }
    else // LOGFONTA: fuenf LONGs, acht BYTEs, 32 Byte Name
    {
        function = Win98Metafile::CreateFontIndirect;
        auto *out = reinterpret_cast<unsigned char *>(parm);
        for (unsigned i = 0; i < 5; ++i)
            Put16(out + i * 2, Get32(raw + i * 4) & 65535);
        for (unsigned i = 0; i < 8; ++i)
            out[10 + i] = raw[20 + i];
        memcpy(out + 18, raw + 28, 32);
        words = 25;
    }
    return MetafileWrite(e, function, parm, words);
}

// META_POLYGON and META_POLYLINE contain a point count followed by POINT16
// entries.
unsigned Application::MetafilePoly(MetafileSlot &e, ApiName name, const unsigned *a)
{
    const unsigned count = a[2];
    if (!count || count > 4096)
    {
        last_error = 87;
        return 0;
    }
    auto *parm = new unsigned short[count * 2 + 1];
    parm[0] = (unsigned short)count;
    bool ok = true;
    for (unsigned i = 0; ok && i < count; ++i)
    {
        int point[2];
        ok = Read(a[1] + i * 8, point, sizeof point);
        parm[1 + i * 2] = (unsigned short)point[0];
        parm[2 + i * 2] = (unsigned short)point[1];
    }
    const unsigned function = API_IS(name, "Polygon") ? Win98Metafile::Polygon : Win98Metafile::Polyline;
    const unsigned result = ok && MetafileWrite(e, function, parm, count * 2 + 1);
    delete[] parm;
    return result;
}

// META_POLYPOLYGON: ring count, point counts, then all points.
unsigned Application::MetafilePolyPolygon(MetafileSlot &e, const unsigned *a)
{
    const unsigned polygons = a[3];
    if (!polygons || polygons > 256)
    {
        last_error = 87;
        return 0;
    }
    unsigned counts[256], total = 0;
    for (unsigned i = 0; i < polygons; ++i)
    {
        if (!Read(a[2] + i * 4, &counts[i], 4) || counts[i] > 4096)
        {
            last_error = 87;
            return 0;
        }
        total += counts[i];
    }
    const unsigned words = 1 + polygons + total * 2;
    auto *parm = new unsigned short[words];
    parm[0] = (unsigned short)polygons;
    for (unsigned i = 0; i < polygons; ++i)
        parm[1 + i] = (unsigned short)counts[i];
    bool ok = true;
    for (unsigned i = 0; ok && i < total; ++i)
    {
        int point[2];
        ok = Read(a[1] + i * 8, point, sizeof point);
        parm[1 + polygons + i * 2] = (unsigned short)point[0];
        parm[2 + polygons + i * 2] = (unsigned short)point[1];
    }
    const unsigned result = ok && MetafileWrite(e, Win98Metafile::PolyPolygon, parm, words);
    delete[] parm;
    return result;
}

// META_TEXTOUT stores character count, text, then y and x. META_EXTTEXTOUT
// starts with y and x.
unsigned Application::MetafileText(MetafileSlot &e, ApiName name, const unsigned *a)
{
    const bool extended = API_IS(name, "ExtTextOutA");
    const unsigned text = extended ? a[5] : a[3], length = extended ? a[6] : a[4];
    if (length > 4096 || (length && !text))
    {
        last_error = 87;
        return 0;
    }
    char data[4096] = {};
    if (length && !Read(text, data, length))
        return 0;
    const unsigned padded = (length + 1) & ~1u;
    unsigned short parm[8 + 2048 + 4096] = {}; // rect, 4096 bytes, optional WORD advances
    unsigned words = 0;
    if (!extended)
    {
        parm[0] = (unsigned short)length;
        memcpy(parm + 1, data, length);
        words = 1 + padded / 2;
        parm[words++] = (unsigned short)a[2];
        parm[words++] = (unsigned short)a[1];
        return MetafileWrite(e, Win98Metafile::TextOut, parm, words);
    }
    const bool rectangle = (a[3] & (Gdi::ETO_OPAQUE | Gdi::ETO_CLIPPED)) && a[4];
    parm[words++] = (unsigned short)a[2];
    parm[words++] = (unsigned short)a[1];
    parm[words++] = (unsigned short)length;
    parm[words++] = (unsigned short)a[3];
    if (rectangle)
    {
        int rect[4];
        if (!Read(a[4], rect, sizeof rect))
            return 0;
        for (unsigned i = 0; i < 4; ++i)
            parm[words++] = (unsigned short)rect[i];
    }
    memcpy(parm + words, data, length);
    words += padded / 2;
    if (a[7] && length)
    {
        for (unsigned i = 0; i < length; ++i)
        {
            int advance = 0;
            if (!Read(a[7] + i * 4, &advance, 4))
                return 0;
            parm[words++] = (unsigned short)advance;
        }
    }
    return MetafileWrite(e, Win98Metafile::ExtTextOut, parm, words);
}

// CLOSEMETAFILE writes a header of nine WORDs.
void Application::MetafileHeader(unsigned char *out, const MetafileSlot &e,
                                 unsigned type, unsigned record_bytes)
{
    Put16(out, type);
    Put16(out + 2, Win98Metafile::HeaderWords);
    Put16(out + 4, Win98Metafile::Version);
    Put32(out + 6, Win98Metafile::HeaderWords + record_bytes / 2);
    Put16(out + 10, e.objects);
    Put32(out + 12, e.max_record);
    Put16(out + 16, 0);
}

// CloseMetaFile appends the end record, prepends the header and writes the
// image if a filename was supplied.
unsigned Application::MetafileClose(MetafileSlot &e)
{
    if (!MetafileWrite(e, Win98Metafile::Eof, nullptr, 0))
        return 0;
    const unsigned records = e.bytes;
    auto *image = static_cast<unsigned char *>(malloc(Win98Metafile::HeaderBytes + records));
    if (!image)
    {
        last_error = 8;
        return 0;
    }
    MetafileHeader(image, e, e.file[0] ? Win98Metafile::FileType : Win98Metafile::MemoryType, records);
    memcpy(image + Win98Metafile::HeaderBytes, e.bits, records);
    free(e.bits);
    e.bits = image;
    e.bytes = Win98Metafile::HeaderBytes + records;
    e.capacity = e.bytes;
    e.recording = false;
    metafiles->recording_count--;
    const unsigned dc = e.dc;
    e.dc = 0;
    if (dc)
    {
        const unsigned request[] = {dc};
        GdiApi("DeleteDC", request);
    }
    if (e.file[0] && !MetafileStore(e.file, e.bits, e.bytes))
    {
        MetafileRelease(e);
        return 0;
    }
    return MetafileHandleOf(&e);
}

// Release a slot and advance its generation.
void Application::MetafileRelease(MetafileSlot &e)
{
    free(e.bits);
    const unsigned generation = e.generation == 255 ? 1 : e.generation + 1;
    e = MetafileSlot{};
    e.generation = generation;
}

// Use the KERNEL32 file layer so metafiles share ordinary guest path mapping.
bool Application::MetafileStore(const char *path, const unsigned char *data, unsigned bytes)
{
    const unsigned scratch = Allocate(bytes + 272);
    if (!scratch)
    {
        last_error = 8;
        return false;
    }
    bool ok = Store(scratch, path, unsigned(strlen(path)) + 1) && Store(scratch + 264, data, bytes);
    const unsigned create[] = {scratch, 0x40000000u, 0, 0, 2, 0x80, 0};
    const unsigned file = ok ? Api("CreateFileA", create) : ~0u;
    if (file == ~0u)
        ok = false;
    if (ok)
    {
        const unsigned write[] = {file, scratch + 264, bytes, scratch + 260, 0};
        unsigned written = 0;
        ok = Api("WriteFile", write) != 0 && Read(scratch + 260, &written, 4) && written == bytes;
        const unsigned close[] = {file};
        Api("CloseHandle", close);
    }
    Free(scratch);
    return ok;
}
unsigned char *Application::MetafileLoad(unsigned path, unsigned &bytes)
{
    const unsigned create[] = {path, 0x80000000u, 1, 0, 3, 0x80, 0};
    const unsigned file = Api("CreateFileA", create);
    if (file == ~0u)
        return nullptr;
    const unsigned query[] = {file, 0};
    const unsigned size = Api("GetFileSize", query);
    unsigned char *data = nullptr;
    if (size != ~0u && size >= Win98Metafile::HeaderBytes && size <= MetafileLimit)
    {
        const unsigned scratch = Allocate(size + 8);
        if (scratch)
        {
            const unsigned read[] = {file, scratch, size, scratch + size, 0};
            unsigned got = 0;
            if (Api("ReadFile", read) && Read(scratch + size, &got, 4) && got == size)
            {
                data = static_cast<unsigned char *>(malloc(size));
                if (data && !Read(scratch, data, size))
                {
                    free(data);
                    data = nullptr;
                }
            }
            Free(scratch);
        }
    }
    const unsigned close[] = {file};
    Api("CloseHandle", close);
    bytes = data ? size : 0;
    return data;
}

// Accept a complete metafile image after validating its header. Remove an
// optional placeable header identified by D7CDC69Ah.
unsigned Application::MetafileAdopt(const unsigned char *data, unsigned bytes)
{
    unsigned skip = 0;
    if (bytes >= 22 && Get32(data) == Win98Metafile::Placeable)
        skip = 22;
    if (bytes - skip < Win98Metafile::HeaderBytes || Get16(data + skip + 2) != Win98Metafile::HeaderWords ||
        (Get16(data + skip + 4) != Win98Metafile::Version && Get16(data + skip + 4) != 0x0100))
    {
        last_error = 87;
        return 0;
    }
    auto &store = Metafiles();
    for (auto &e : store.entries)
        if (!e.used)
        {
            e.bits = static_cast<unsigned char *>(malloc(bytes - skip));
            if (!e.bits)
            {
                last_error = 8;
                return 0;
            }
            memcpy(e.bits, data + skip, bytes - skip);
            e.used = true;
            e.enhanced = false;
            e.recording = false;
            e.bytes = e.capacity = bytes - skip;
            e.objects = Get16(e.bits + 10);
            e.max_record = Get32(e.bits + 12);
            e.file[0] = 0;
            return MetafileHandleOf(&e);
        }
    last_error = 8;
    return 0;
}

// Play one record into the target DC. Table records use native API calls;
// object records use native constructors. Return FALSE for records without a
// player.
unsigned Application::MetafilePlayRecord(unsigned dc, unsigned handles, unsigned count,
                                         const unsigned char *record, unsigned bytes)
{
    if (bytes < 6)
        return 0;
    const unsigned size = Get32(record), function = Get16(record + 4);
    if (size < 3 || size * 2 > bytes)
        return 0;
    const unsigned words = size - 3;
    const unsigned char *parm = record + 6;
    if (!function)
        return 1;
    auto slot = [&](unsigned index) { return handles + index * 4; };
    auto object = [&](unsigned index) {
        unsigned handle = 0;
        return index < count && Read(slot(index), &handle, 4) ? handle : 0;
    };
    if (const auto *table = Win98Metafile::Find(function))
    {
        if (!Playable(function))
            return 0;
        unsigned args[10] = {dc};
        if (!UnpackShape(table->shape, parm, words, args))
            return 0;
        // Do not interpret these GDI return values as success flags. Many
        // report the previous state, which may legitimately be zero.
        GdiApi(ApiName(table->api), args);
        return state == Failed ? 0 : 1;
    }
    switch (function)
    {
    case Win98Metafile::SelectObject:
    {
        if (words < 1)
            return 0;
        const unsigned handle = object(Get16(parm));
        if (!handle)
            return 0;
        const unsigned args[] = {dc, handle};
        return GdiApi("SelectObject", args) ? 1 : 0;
    }
    case Win98Metafile::DeleteObject:
    {
        if (words < 1)
            return 0;
        const unsigned index = Get16(parm), handle = object(index);
        if (!handle)
            return 0;
        const unsigned args[] = {handle};
        const unsigned result = GdiApi("DeleteObject", args);
        const unsigned empty = 0;
        Store(slot(index), &empty, 4);
        return result ? 1 : 0;
    }
    case Win98Metafile::CreatePenIndirect:
    case Win98Metafile::CreateBrushIndirect:
    case Win98Metafile::CreateFontIndirect:
        return MetafileCreateObject(function, parm, words, handles, count);
    case 0x0b41: // META_DIBSTRETCHBLT
    {
        // Without an embedded DIB the original record retains its source-DC
        // WORD; the operation uses only the destination and pattern.
        if (words == 11) {
            const unsigned args[] = {dc, unsigned(Signed16(Get16(parm + 20))),
                unsigned(Signed16(Get16(parm + 18))), unsigned(Signed16(Get16(parm + 16))),
                unsigned(Signed16(Get16(parm + 14))), Get32(parm)};
            return GdiApi("PatBlt", args);
        }
        if (words < 30) return 0;
        const unsigned char *dib = parm + 20;
        const unsigned header = Get32(dib), depth = Get16(dib + 14);
        if (header != 40 || Get16(dib + 12) != 1 || Get32(dib + 16) != 0 ||
            (depth != 1 && depth != 4 && depth != 8 && depth != 24 && depth != 32)) return 0;
        const unsigned colors = depth <= 8 ? (Get32(dib + 32) ? Get32(dib + 32) : 1u << depth) : 0;
        if (colors > 256) return 0;
        const unsigned info = header + colors * 4, bytes = words * 2 - 20;
        if (info > bytes) return 0;
        const unsigned scratch = Allocate(bytes);
        if (!scratch) return 0;
        unsigned result = 0;
        if (Store(scratch, dib, bytes)) {
            const unsigned args[] = {dc, unsigned(Signed16(Get16(parm + 18))), unsigned(Signed16(Get16(parm + 16))),
                unsigned(Signed16(Get16(parm + 14))), unsigned(Signed16(Get16(parm + 12))),
                unsigned(Signed16(Get16(parm + 10))),
                // WMF blits retain bitmap coordinates ('s DDB path).
                // StretchDIBits measures y from the bottom of that bitmap.
                unsigned(int(Get32(dib + 8)) - Signed16(Get16(parm + 8)) - Signed16(Get16(parm + 4))),
                unsigned(Signed16(Get16(parm + 6))), unsigned(Signed16(Get16(parm + 4))),
                scratch + info, scratch, 0, Get32(parm)};
            result = GdiApi("StretchDIBits", args);
        }
        Free(scratch);
        return result && result != ~0u;
    }
    case 0x142: // META_DIBCREATEPATTERNBRUSH
    {
        if(words<22)return 0;
        const unsigned scratch=Allocate(words*2-4);
        if(!scratch)return 0;
        const unsigned args[]={scratch,Get16(parm+2)};
        const unsigned handle=Store(scratch,parm+4,words*2-4)?GdiApi("CreateDIBPatternBrushPt",args):0;
        Free(scratch);if(!handle)return 0;
        for(unsigned i=0;i<count;++i)
        {
            unsigned existing=0;
            if(Read(slot(i),&existing,4) && !existing)return Store(slot(i),&handle,4);
        }
        const unsigned dead[]={handle};GdiApi("DeleteObject",dead);return 0;
    }
    case Win98Metafile::Polygon:
    case Win98Metafile::Polyline:
    case Win98Metafile::PolyPolygon:
        return MetafilePlayPoly(dc, function, parm, words);
    case Win98Metafile::TextOut:
    case Win98Metafile::ExtTextOut:
        return MetafilePlayText(dc, function, parm, words);
    case Win98Metafile::Escape:
    {
        // Ignore MFCOMMENT; pass other escape records and their data to
        // Escape(hdc, n, count, data, 0).
        if (words < 2)
            return 0;
        const unsigned escape = Get16(parm), length = Get16(parm + 2);
        if (escape == Win98Metafile::MfComment)
            return 1;
        if (words < 2 + (length + 1) / 2)
            return 0;
        const unsigned scratch = Allocate(length + 4);
        if (!scratch)
            return 0;
        unsigned result = 0;
        if (!length || Store(scratch, parm + 4, length))
        {
            const unsigned args[] = {dc, escape, length, scratch, 0};
            result = GdiApi("Escape", args);
        }
        Free(scratch);
        return int(result) > 0 ? 1 : 0;
    }
    default:
        break;
    }
    return 0;
}

// Widen creation-record structures to their 32-bit form and call the native
// constructor. Store the result in the first free object-table slot.
unsigned Application::MetafileCreateObject(unsigned function, const unsigned char *parm,
                                           unsigned words, unsigned handles, unsigned count)
{
    unsigned char wide[64] = {};
    unsigned size = 0;
    const char *api = nullptr;
    if (function == Win98Metafile::CreatePenIndirect)
    {
        if (words < 5)
            return 0;
        Put32(wide, Get16(parm));
        Put32(wide + 4, unsigned(Signed16(Get16(parm + 2))));
        Put32(wide + 8, unsigned(Signed16(Get16(parm + 4))));
        Put32(wide + 12, Get32(parm + 6));
        size = 16;
        api = "CreatePenIndirect";
    }
    else if (function == Win98Metafile::CreateBrushIndirect)
    {
        if (words < 4)
            return 0;
        Put32(wide, Get16(parm));
        Put32(wide + 4, Get32(parm + 2));
        Put32(wide + 8, unsigned(Signed16(Get16(parm + 6))));
        size = 12;
        api = "CreateBrushIndirect";
    }
    else
    {
        if (words < 25)
            return 0;
        for (unsigned i = 0; i < 5; ++i)
            Put32(wide + i * 4, unsigned(Signed16(Get16(parm + i * 2))));
        for (unsigned i = 0; i < 8; ++i)
            wide[20 + i] = parm[10 + i];
        memcpy(wide + 28, parm + 18, 32);
        wide[59] = 0;
        size = 60;
        api = "CreateFontIndirectA";
    }
    const unsigned scratch = Allocate(size);
    if (!scratch)
        return 0;
    const unsigned args[] = {scratch};
    const unsigned handle = Store(scratch, wide, size) ? GdiApi(ApiName(api), args) : 0;
    Free(scratch);
    if (!handle)
        return 0;
    for (unsigned i = 0; i < count; ++i)
    {
        unsigned present = 0;
        if (Read(handles + i * 4, &present, 4) && !present)
            return Store(handles + i * 4, &handle, 4) ? 1 : 0;
    }
    const unsigned dead[] = {handle};
    GdiApi("DeleteObject", dead);
    return 0;
}

unsigned Application::MetafilePlayPoly(unsigned dc, unsigned function, const unsigned char *parm,
                                       unsigned words)
{
    if (!words)
        return 0;
    const unsigned first = Get16(parm);
    unsigned polygons = 1, total = first, offset = 1;
    unsigned counts[256];
    if (function == Win98Metafile::PolyPolygon)
    {
        polygons = first;
        if (!polygons || polygons > 256 || words < 1 + polygons)
            return 0;
        total = 0;
        for (unsigned i = 0; i < polygons; ++i)
        {
            counts[i] = Get16(parm + 2 + i * 2);
            total += counts[i];
        }
        offset = 1 + polygons;
    }
    else
        counts[0] = first;
    if (!total || total > 8192 || words < offset + total * 2)
        return 0;
    const unsigned points_bytes = total * 8, counts_bytes = polygons * 4;
    const unsigned scratch = Allocate(points_bytes + counts_bytes);
    if (!scratch)
        return 0;
    bool ok = true;
    for (unsigned i = 0; ok && i < total; ++i)
    {
        const int point[2] = {Signed16(Get16(parm + (offset + i * 2) * 2)),
                              Signed16(Get16(parm + (offset + i * 2 + 1) * 2))};
        ok = Store(scratch + i * 8, point, sizeof point);
    }
    for (unsigned i = 0; ok && i < polygons; ++i)
        ok = Store(scratch + points_bytes + i * 4, &counts[i], 4);
    unsigned result = 0;
    if (ok)
    {
        if (function == Win98Metafile::PolyPolygon)
        {
            const unsigned args[] = {dc, scratch, scratch + points_bytes, polygons};
            result = GdiApi("PolyPolygon", args);
        }
        else
        {
            const unsigned args[] = {dc, scratch, total};
            result = GdiApi(function == Win98Metafile::Polygon ? ApiName("Polygon") : ApiName("Polyline"), args);
        }
    }
    Free(scratch);
    return result ? 1 : 0;
}

unsigned Application::MetafilePlayText(unsigned dc, unsigned function, const unsigned char *parm,
                                       unsigned words)
{
    unsigned length = 0, x = 0, y = 0, options = 0, offset = 0;
    bool rectangle = false;
    if (function == Win98Metafile::TextOut)
    {
        if (words < 3)
            return 0;
        length = Get16(parm);
        const unsigned padded = (length + 1) / 2;
        if (words < 1 + padded + 2)
            return 0;
        offset = 1;
        y = unsigned(Signed16(Get16(parm + (1 + padded) * 2)));
        x = unsigned(Signed16(Get16(parm + (2 + padded) * 2)));
    }
    else
    {
        if (words < 4)
            return 0;
        y = unsigned(Signed16(Get16(parm)));
        x = unsigned(Signed16(Get16(parm + 2)));
        length = Get16(parm + 4);
        options = Get16(parm + 6);
        rectangle = (options & (Gdi::ETO_OPAQUE | Gdi::ETO_CLIPPED)) != 0;
        offset = rectangle ? 8u : 4u;
        if (words < offset + (length + 1) / 2)
            return 0;
    }
    if (length > 4096)
        return 0;
    const unsigned scratch = Allocate(length + 24);
    if (!scratch)
        return 0;
    bool ok = !length || Store(scratch, parm + offset * 2, length);
    unsigned result = 0;
    if (ok && function == Win98Metafile::TextOut)
    {
        const unsigned args[] = {dc, x, y, scratch, length};
        result = GdiApi("TextOutA", args);
    }
    else if (ok)
    {
        unsigned rect = 0;
        if (rectangle)
        {
            const int box[4] = {Signed16(Get16(parm + 8)), Signed16(Get16(parm + 10)),
                                Signed16(Get16(parm + 12)), Signed16(Get16(parm + 14))};
            rect = scratch + length + 8;
            ok = Store(rect, box, sizeof box);
        }
        const unsigned args[] = {dc, x, y, options, rect, scratch, length, 0};
        result = ok && GdiApi("ExtTextOutA", args);
    }
    Free(scratch);
    return result ? 1 : 0;
}

// PlayMetaFile allocates a guest object table and plays each record without
// interpreting its result. Stop at the end record or when QUERYABORT rejects
// playback on an ordinary DC. Preserve the target's brush, pen, font, palette
// and clipping region; restore them before deleting table objects, even after
// early termination.
unsigned Application::MetafilePlay(unsigned dc, MetafileSlot &e)
{
    const unsigned count = e.objects ? e.objects : 1;
    const unsigned handles = Allocate(count * 4);
    if (!handles)
    {
        last_error = 8;
        return 0;
    }
    for (unsigned i = 0; i < count; ++i)
    {
        const unsigned empty = 0;
        Store(handles + i * 4, &empty, 4);
    }
    // Object order: OBJ_PEN, OBJ_BRUSH, OBJ_PAL, OBJ_FONT.
    static constexpr unsigned kinds[] = {2, 1, 5, 6};
    unsigned saved[4] = {};
    for (unsigned i = 0; i < 4; ++i)
    {
        const unsigned query[] = {dc, kinds[i]};
        saved[i] = GdiApi("GetCurrentObject", query);
    }
    const unsigned empty_rect[] = {0, 0, 0, 0};
    unsigned clip = GdiApi("CreateRectRgn", empty_rect);
    if (clip)
    {
        const unsigned query[] = {dc, clip};
        if (int(GdiApi("GetClipRgn", query)) != 1)
        {
            const unsigned dead[] = {clip};
            GdiApi("DeleteObject", dead);
            clip = 0;
        }
    }
    unsigned at = Win98Metafile::HeaderBytes, result = 1;
    while (at + 6 <= e.bytes)
    {
        const unsigned size = Get32(e.bits + at);
        if (size < 3 || at + size * 2 > e.bytes)
        {
            result = 0;
            break;
        }
        if (!Get16(e.bits + at + 4))
            break;
        if (PrintFind(dc) && !PrintQueryAbort(dc))
        {
            result = 0;
            break;
        }
        MetafilePlayRecord(dc, handles, count, e.bits + at, size * 2);
        at += size * 2;
        if (state == Failed)
        {
            result = 0;
            break;
        }
    }
    for (unsigned i = 0; i < 4; ++i)
    {
        const unsigned query[] = {dc, kinds[i]};
        if (saved[i] && GdiApi("GetCurrentObject", query) != saved[i])
        {
            if (kinds[i] == 5)
            {
                const unsigned args[] = {dc, saved[i], 0};
                GdiApi("SelectPalette", args);
            }
            else
            {
                const unsigned args[] = {dc, saved[i]};
                GdiApi("SelectObject", args);
            }
        }
    }
    {
        const unsigned args[] = {dc, clip};
        GdiApi("SelectClipRgn", args);
    }
    if (clip)
    {
        const unsigned dead[] = {clip};
        GdiApi("DeleteObject", dead);
    }
    for (unsigned i = 0; i < count; ++i)
    {
        unsigned handle = 0;
        if (Read(handles + i * 4, &handle, 4) && handle)
        {
            const unsigned dead[] = {handle};
            GdiApi("DeleteObject", dead);
        }
    }
    Free(handles);
    return result;
}

unsigned Application::MetafileApi(ApiName name, const unsigned *a)
{
    auto &store = Metafiles();
    switch (name.id)
    {
    case API_ID("CreateMetaFileA"):
    case API_ID("CreateMetaFileW"):
    {
        const bool wide = API_IS(name, "CreateMetaFileW");
        for (auto &e : store.entries)
            if (!e.used)
            {
                // Advance the slot generation so stale handles cannot
                // identify a new metafile.
                const unsigned generation = e.generation;
                e = MetafileSlot{};
                e.generation = generation;
                if (a[0])
                {
                    if (wide)
                    {
                        unsigned short unit = 0;
                        unsigned i = 0;
                        while (i < 259 && Read(a[0] + i * 2, &unit, 2) && unit)
                            e.file[i++] = char(unit);
                        e.file[i] = 0;
                    }
                    else
                    {
                        const char *path = ReadString(a[0]);
                        if (!path)
                            return 0;
                        unsigned i = 0;
                        while (i < 259 && path[i])
                        {
                            e.file[i] = path[i];
                            ++i;
                        }
                        e.file[i] = 0;
                    }
                }
                const unsigned dc = OwnGdi(Gdi::CreateCompatibleDC(0), 2);
                if (!dc)
                {
                    last_error = 8;
                    return 0;
                }
                e.used = e.recording = true;
                e.dc = dc;
                store.recording_count++;
                return dc;
            }
        last_error = 8;
        return 0;
    }
    case API_ID("CloseMetaFile"):
    {
        auto *e = RecordingMetafile(a[0]);
        if (!e || e->enhanced)
        {
            last_error = 6;
            return 0;
        }
        return MetafileClose(*e);
    }
    case API_ID("DeleteMetaFile"):
    {
        auto *e = MetafileEntry(a[0]);
        if (!e || e->enhanced)
        {
            last_error = 6;
            return 0;
        }
        MetafileRelease(*e);
        return 1;
    }
    case API_ID("GetMetaFileBitsEx"):
    {
        auto *e = MetafileEntry(a[0]);
        if (!e || e->enhanced || e->recording)
        {
            last_error = 6;
            return 0;
        }
        if (!a[2])
            return e->bytes;
        if (a[1] < e->bytes)
            return 0;
        return Store(a[2], e->bits, e->bytes) ? e->bytes : 0;
    }
    case API_ID("SetMetaFileBitsEx"):
    {
        if (!a[0] || !a[1] || a[0] > MetafileLimit)
        {
            last_error = 87;
            return 0;
        }
        auto *data = static_cast<unsigned char *>(malloc(a[0]));
        if (!data)
        {
            last_error = 8;
            return 0;
        }
        const unsigned handle = Read(a[1], data, a[0]) ? MetafileAdopt(data, a[0]) : 0;
        free(data);
        return handle;
    }
    case API_ID("GetMetaFileA"):
    {
        unsigned bytes = 0;
        auto *data = MetafileLoad(a[0], bytes);
        if (!data)
        {
            last_error = 2;
            return 0;
        }
        const unsigned handle = MetafileAdopt(data, bytes);
        free(data);
        return handle;
    }
    case API_ID("CopyMetaFileA"):
    {
        auto *e = MetafileEntry(a[0]);
        if (!e || e->enhanced || e->recording)
        {
            last_error = 6;
            return 0;
        }
        const unsigned handle = MetafileAdopt(e->bits, e->bytes);
        auto *copy = MetafileEntry(handle);
        if (handle && a[1])
        {
            const char *path = ReadString(a[1]);
            if (!path)
                return 0;
            unsigned i = 0;
            while (i < 259 && path[i])
            {
                copy->file[i] = path[i];
                ++i;
            }
            copy->file[i] = 0;
            Put16(copy->bits, Win98Metafile::FileType);
            if (!MetafileStore(copy->file, copy->bits, copy->bytes))
                return 0;
        }
        return handle;
    }
    case API_ID("PlayMetaFile"):
    {
        auto *e = MetafileEntry(a[1]);
        if (!e || e->enhanced || e->recording || !Gdi::ValidDC(a[0]))
        {
            last_error = 6;
            return 0;
        }
        return MetafilePlay(a[0], *e);
    }
    case API_ID("PlayMetaFileRecord"):
    {
        // The caller supplies the handle table and record memory. The record
        // contains its own size.
        unsigned char head[6];
        if (!a[2] || !Read(a[2], head, sizeof head))
            return 0;
        const unsigned size = Get32(head);
        if (size < 3 || size > 65536)
            return 0;
        auto *record = static_cast<unsigned char *>(malloc(size * 2));
        if (!record)
        {
            last_error = 8;
            return 0;
        }
        const unsigned result =
            Read(a[2], record, size * 2) ? MetafilePlayRecord(a[0], a[1], a[3], record, size * 2) : 0;
        free(record);
        return result;
    }
    case API_ID("EnumMetaFile"):
    {
        auto *e = MetafileEntry(a[1]);
        if (!e || e->enhanced || e->recording || !a[2])
        {
            last_error = 6;
            return 0;
        }
        const unsigned count = e->objects ? e->objects : 1;
        // Enumerate a snapshot: guest callbacks may delete this metafile and
        // free bits. The loop must not read the released storage.
        const unsigned bytes = e->bytes;
        auto *image = static_cast<unsigned char *>(malloc(bytes ? bytes : 1));
        if (!image)
        {
            last_error = 8;
            return 0;
        }
        memcpy(image, e->bits, bytes);
        const unsigned handles = Allocate(count * 4 + 65536);
        if (!handles)
        {
            free(image);
            last_error = 8;
            return 0;
        }
        const unsigned record = handles + count * 4;

        for (unsigned i = 0; i < count; ++i)
        {
            const unsigned empty = 0;
            Store(handles + i * 4, &empty, 4);
        }
        unsigned at = Win98Metafile::HeaderBytes, result = 1;
        while (at + 6 <= bytes)
        {
            const unsigned size = Get32(image + at);
            if (size < 3 || at + size * 2 > bytes || size * 2 > 65536)
            {
                result = 0;
                break;
            }
            if (!Get16(image + at + 4)) break;
            if (!Store(record, image + at, size * 2))
            {
                result = 0;
                break;
            }
            const unsigned arguments[] = {a[0], handles, record, count, a[3]};
            if (!Call(a[2], arguments, 5) || state == Failed) {
                result = 0;
                break;
            }
            at += size * 2;
        }
        Free(handles);
        free(image);
        return result;

    }
    default:
        break;
    }
    last_error = 120;
    return 0;
}

// Enhanced metafiles. Record layouts, headers and playback are defined in
// src/win98_metafile_emf.h.
namespace
{
unsigned EmfGeometry(unsigned type)
{
    // Number of leading LONG coordinates that extend rclBounds.
    switch (type)
    {
    case 27: case 54: return 2;
    case 42: case 43: case 44: case 45: case 46: case 47: return 4;
    default: return 0;
    }
}
} // namespace

// A record contains iType, nSize and payload. nSize includes the header and
// is a multiple of four.
bool Application::EnhancedWrite(MetafileSlot &e, unsigned type, const unsigned char *payload,
                                unsigned bytes)
{
    const unsigned size = 8 + ((bytes + 3) & ~3u);
    if (e.bytes + size > MetafileLimit)
    {
        last_error = 8;
        return false;
    }
    if (e.bytes + size > e.capacity)
    {
        unsigned capacity = e.capacity ? e.capacity * 2 : 1024;
        while (capacity < e.bytes + size)
            capacity *= 2;
        auto *grown = static_cast<unsigned char *>(realloc(e.bits, capacity));
        if (!grown)
        {
            last_error = 8;
            return false;
        }
        e.bits = grown;
        e.capacity = capacity;
    }
    unsigned char *at = e.bits + e.bytes;
    Put32(at, type);
    Put32(at + 4, size);
    memset(at + 8, 0, size - 8);
    if (bytes)
        memcpy(at + 8, payload, bytes);
    e.bytes += size;
    ++e.records;
    return true;
}

void Application::EnhancedBounds(MetafileSlot &e, int left, int top, int right, int bottom)
{
    Gdi::RECT device = {left,top,right,bottom};
    if(Gdi::DeviceRect(e.dc,device))
    { left=device.left;top=device.top;right=device.right;bottom=device.bottom; }
    if (left > right)
    {
        const int swap = left;
        left = right;
        right = swap;
    }
    if (top > bottom)
    {
        const int swap = top;
        top = bottom;
        bottom = swap;
    }
    if (!e.bounded)
    {
        e.bounds[0] = left;
        e.bounds[1] = top;
        e.bounds[2] = right;
        e.bounds[3] = bottom;
        e.bounded = true;
        return;
    }
    if (left < e.bounds[0]) e.bounds[0] = left;
    if (top < e.bounds[1]) e.bounds[1] = top;
    if (right > e.bounds[2]) e.bounds[2] = right;
    if (bottom > e.bounds[3]) e.bounds[3] = bottom;
}

bool Application::EnhancedRecord(MetafileSlot &e, ApiName name, const unsigned *a, unsigned &result)
{
    if (API_IS(name, "SetPixel")) name = ApiName("SetPixelV");
    for (unsigned i = 0; i < Win98MetafileEmf::TableCount; ++i)
    {
        const auto &table = Win98MetafileEmf::Table[i];
        if (NameId(table.api) != name.id)
            continue;
        unsigned char payload[64];
        unsigned bytes = 0;
        for (const char *p = table.shape; *p; ++p)
        {
            if (*p == '0')
                continue;
            Put32(payload + bytes, a[1 + unsigned(p - table.shape)]);
            bytes += 4;
        }
        int restore_relative = 0;
        if(table.type==34)
        {
            restore_relative=int(a[1])<0?int(a[1]):int(a[1])-int(e.saved_depth)-1;
            if(!a[1] || restore_relative>=0 || unsigned(-restore_relative)>e.saved_depth)
            { result=0;return true; }
            Put32(payload,unsigned(restore_relative));
        }
        const unsigned geometry = EmfGeometry(table.type);
        if (table.type == 54)
        {
            Gdi::POINT previous;
            if(Gdi::CurrentPosition(a[0],previous))EnhancedBounds(e,previous.x,previous.y,previous.x,previous.y);
        }
        if ((geometry == 2 && table.type != 27) || table.type == 15)
            EnhancedBounds(e, int(a[1]), int(a[2]), int(a[1]), int(a[2]));
        else if (geometry == 4)
            EnhancedBounds(e, int(a[1]), int(a[2]), int(a[3]), int(a[4]));
        result = EnhancedWrite(e, table.type, payload, bytes);
        if(result && geometry && Gdi::PathRecording(a[0]))
        {
            // Keep the recording DC's path in step with the EMF stream.
            // Merely moving its cursor for LineTo leaves no figure for a
            // following CloseFigure (native emf_path probe).
            e.recording=false;
            result=GdiApi(name,a);
            e.recording=true;
        }
        else if(result && (table.type==27 || table.type==54))
        {
            e.recording=false;
            const unsigned move[]={a[0],a[1],a[2],0};GdiApi("MoveToEx",move);
            e.recording=true;
        }
        if (result && !geometry && table.type != 15)
        {
            // State setters affect queries on the recording DC as well.
            e.recording = false;
            result = GdiApi(name, a);
            e.recording = true;
            if(result && table.type==33)++e.saved_depth;
            if(result && table.type==34)e.saved_depth=unsigned(int(e.saved_depth)+restore_relative);
        }
        return true;
    }
    switch (name.id)
    {
    case API_ID("PatBlt"):
    {
        unsigned char payload[92] = {};
        Gdi::RECT bounds={(int)a[1],(int)a[2],int(a[1]+a[3]),int(a[2]+a[4])};
        Gdi::DeviceRect(a[0],bounds); memcpy(payload,&bounds,16);
        for(unsigned i=0;i<5;++i)Put32(payload+16+i*4,a[i+1]);
        Put32(payload+44,0x3f800000);Put32(payload+56,0x3f800000);
        EnhancedBounds(e,int(a[1]),int(a[2]),int(a[1]+a[3]),int(a[2]+a[4]));
        result=EnhancedWrite(e,76,payload,sizeof payload);return true;
    }
    case API_ID("SelectObject"):
        result = EnhancedSelect(e, a[0], a[1]);
        return true;
    case API_ID("SelectClipRgn"):
    case API_ID("ExtSelectClipRgn"):
    {
        const unsigned mode = API_IS(name, "SelectClipRgn") ? 5 : a[2];
        unsigned request[] = {a[1], 0, 0};
        const unsigned bytes = a[1] ? GdiApi("GetRegionData", request) : 0;
        if ((a[1] && !bytes) || (!a[1] && mode != 5) || bytes > MetafileLimit - 16)
        { result = 0; return true; }
        const unsigned scratch = Allocate(bytes + 16);
        if (!scratch) { result = 0; last_error = 8; return true; }
        auto *payload = new unsigned char[bytes + 8];
        Put32(payload, bytes); Put32(payload + 4, mode);
        request[1] = bytes; request[2] = scratch;
        bool ok = !bytes || (GdiApi("GetRegionData", request) && Read(scratch, payload + 8, bytes));
        if (ok) ok = EnhancedWrite(e, 75, payload, bytes + 8);
        delete[] payload;
        e.recording = false;
        result = ok ? GdiApi(name, a) : 0;
        e.recording = true;
        if (result && a[1])
        {
            const unsigned box[] = {a[1], scratch}; result = GdiApi("GetRgnBox", box);
        }
        Free(scratch); return true;
    }
    case API_ID("Polygon"):
    case API_ID("Polyline"):
        result = EnhancedPoly(e, name, a);
        return true;
    case API_ID("TextOutA"):
    case API_ID("ExtTextOutA"):
        result = EnhancedText(e, name, a);
        return true;
    default:
        break;
    }
    if (MustRecordIds.Has(name.id))
    {
        MetafileUnsupported(name, true);
        result = 0;
        return true;
    }
    return false;
}

// EMR_SELECTOBJECT names a handle-table slot. Slot zero belongs to the
// metafile; objects start at one.
unsigned Application::EnhancedSelect(MetafileSlot &e, unsigned dc, unsigned object)
{
    const unsigned type = Gdi::GetObjectType(object);
    if (type != 1 && type != 2 && type != 6)
    {
        last_error = 6;
        return 0;
    }
    unsigned index = 0;
    for (unsigned i = 1; i < MetafileObjects; ++i)
        if (e.object_handles[i] == object)
        {
            index = i;
            break;
        }
    if (!index)
    {
        for (unsigned i = 1; i < MetafileObjects; ++i)
            if (!e.object_handles[i])
            {
                index = i;
                break;
            }
        if (!index)
        {
            last_error = 8;
            return 0;
        }
        const unsigned scratch = Allocate(64);
        if (!scratch)
        {
            last_error = 8;
            return 0;
        }
        unsigned char raw[64] = {};
        const unsigned request[] = {object, 64, scratch};
        const unsigned copied = GdiApi("GetObjectA", request);
        const bool ok = copied && Read(scratch, raw, copied <= 64 ? copied : 64);
        Free(scratch);
        if (!ok)
        {
            last_error = 6;
            return 0;
        }
        unsigned char payload[324] = {};
        Put32(payload, index);
        unsigned record = Win98MetafileEmf::CreatePen, bytes = 20;
        if (type == 6)
        {
            // EXTLOGFONTW contains LOGFONTW followed by optional font metadata.
            // Convert the ANSI face using the process code page, not byte casts.
            record = Win98MetafileEmf::ExtCreateFontIndirectW;
            bytes = sizeof payload;
            memcpy(payload + 4, raw, 28);
            for (unsigned i = 0; i < 31 && raw[28 + i]; ++i)
                Put16(payload + 32 + i * 2, AnsiWide(raw[28 + i]));
        }
        else
        {
            if (type == 2) { record = Win98MetafileEmf::CreateBrushIndirect; bytes = 16; }
            memcpy(payload + 4, raw, bytes - 4);
        }
        if (!EnhancedWrite(e, record, payload, bytes))
            return 0;
        e.object_handles[index] = object;
        if (index + 1 > e.objects)
            e.objects = index + 1;
    }
    unsigned char payload[4];
    Put32(payload, index);
    if (!EnhancedWrite(e, Win98MetafileEmf::SelectObject, payload, 4))
        return 0;
    return Gdi::SelectObject(dc, object);
}

// EMR_POLYGON16 and EMR_POLYLINE16 contain bounds, point count and pairs of
// 16-bit coordinates.
unsigned Application::EnhancedPoly(MetafileSlot &e, ApiName name, const unsigned *a)
{
    const unsigned count = a[2];
    if (!count || count > 4096)
    {
        last_error = 87;
        return 0;
    }
    const unsigned bytes = 20 + count * 4;
    auto *payload = new unsigned char[bytes];
    bool ok = true;
    int left = 0, top = 0, right = 0, bottom = 0;
    for (unsigned i = 0; ok && i < count; ++i)
    {
        int point[2];
        ok = Read(a[1] + i * 8, point, sizeof point);
        Put16(payload + 20 + i * 4, unsigned(point[0]) & 65535);
        Put16(payload + 22 + i * 4, unsigned(point[1]) & 65535);
        if (!i)
        {
            left = right = point[0];
            top = bottom = point[1];
        }
        if (point[0] < left) left = point[0];
        if (point[0] > right) right = point[0];
        if (point[1] < top) top = point[1];
        if (point[1] > bottom) bottom = point[1];
    }
    Put32(payload, unsigned(left));
    Put32(payload + 4, unsigned(top));
    Put32(payload + 8, unsigned(right));
    Put32(payload + 12, unsigned(bottom));
    Put32(payload + 16, count);
    unsigned result = 0;
    if (ok)
    {
        EnhancedBounds(e, left, top, right, bottom);
        result = EnhancedWrite(e,
                               API_IS(name, "Polygon") ? Win98MetafileEmf::Polygon16
                                                       : Win98MetafileEmf::Polyline16,
                               payload, bytes);
    }
    delete[] payload;
    return result;
}

// EMR_EXTTEXTOUTA contains bounds, mapping mode, two scales and EMRTEXT, with
// its text at the end of the record.
unsigned Application::EnhancedText(MetafileSlot &e, ApiName name, const unsigned *a)
{
    const bool extended = API_IS(name, "ExtTextOutA");
    const unsigned text = extended ? a[5] : a[3], length = extended ? a[6] : a[4];
    if (length > 4096 || (length && !text))
    {
        last_error = 87;
        return 0;
    }
    const unsigned x = a[1], y = a[2], options = extended ? a[3] : 0;
    char data[4096] = {};
    if (length && !Read(text, data, length))
        return 0;
    int box[4] = {0, 0, 0, 0};
    if (extended && a[4] && (options & (Gdi::ETO_OPAQUE | Gdi::ETO_CLIPPED)) &&
        !Read(a[4], box, sizeof box))
        return 0;
    // The payload contains rclBounds, iGraphicsMode, both scales and EMRTEXT,
    // followed by text. offString is relative to the record start, including
    // the eight-byte header.
    const unsigned prefix = 68, dx_offset = prefix + ((length * 2 + 3) & ~3u);
    const unsigned bytes = dx_offset + length * 4;
    auto *payload = new unsigned char[bytes];
    memset(payload, 0, bytes);
    const bool boxed = box[0] != box[2] || box[1] != box[3];
    Put32(payload, unsigned(boxed ? box[0] : int(x)));
    Put32(payload + 4, unsigned(boxed ? box[1] : int(y)));
    Put32(payload + 8, unsigned(boxed ? box[2] : int(x)));
    Put32(payload + 12, unsigned(boxed ? box[3] : int(y)));
    Put32(payload + 16, 1);          // iGraphicsMode GM_COMPATIBLE
    Put32(payload + 20, 0x3f800000); // exScale 1.0
    Put32(payload + 24, 0x3f800000); // eyScale 1.0
    Put32(payload + 28, x);
    Put32(payload + 32, y);
    Put32(payload + 36, length);
    Put32(payload + 40, prefix + 8); // offString
    Put32(payload + 44, options);
    for (unsigned i = 0; i < 4; ++i)
        Put32(payload + 48 + i * 4, unsigned(box[i]));
    Put32(payload + 64, dx_offset + 8);
    for(unsigned i=0;i<length;++i)Put16(payload+prefix+i*2,AnsiWide((unsigned char)data[i]));
    if (length && extended && a[7])
    {
        if (!Read(a[7], payload + dx_offset, length * 4)) { delete[] payload; return 0; }
    }
    else if (length)
    {
        const unsigned scratch = Allocate(4);
        if (!scratch) { delete[] payload; last_error = 8; return 0; }
        bool ok = true;
        for (unsigned i = 0; i < length && ok; ++i)
        {
            const unsigned args[] = {a[0], (unsigned char)data[i], (unsigned char)data[i], scratch};
            ok = GdiApi("GetCharWidthA", args) && Read(scratch, payload + dx_offset + i * 4, 4);
        }
        Free(scratch);
        if (!ok) { delete[] payload; return 0; }
    }
    if(!boxed && length)
    {
        Gdi::SIZE size={};
        if(Gdi::GetTextExtentPoint32A(a[0],data,int(length),&size))
        {
            long long width=0;
            for(unsigned i=0;i<length;++i)width+=int(Get32(payload+dx_offset+i*4));
            if(width>=-2147483647 && width<=2147483647)
            {
                Put32(payload+8,x+unsigned(width));Put32(payload+12,y+unsigned(size.cy));
            }
        }
    }
    EnhancedBounds(e, int(Get32(payload)), int(Get32(payload + 4)), int(Get32(payload + 8)),
                   int(Get32(payload + 12)));
    const unsigned result = EnhancedWrite(e, Win98MetafileEmf::ExtTextOutW, payload, bytes);
    delete[] payload;
    return result;
}

// CloseEnhMetaFile appends the end record and prepends the header. The header
// is a type-1 record with the description after its 88 fixed bytes.
unsigned Application::EnhancedClose(MetafileSlot &e)
{
    if(e.saved_depth)
    {
        unsigned char restore[4];Put32(restore,unsigned(-int(e.saved_depth)));
        if(!EnhancedWrite(e,34,restore,4))return 0;
        e.saved_depth=0;
    }
    unsigned char eof[12];
    Put32(eof, 0);       // nPalEntries
    Put32(eof + 4, 16);  // offPalEntries, relative to the record start
    Put32(eof + 8, 20);  // nSizeLast
    if (!EnhancedWrite(e, Win98MetafileEmf::Eof, eof, sizeof eof))
        return 0;
    const unsigned description = (e.description_bytes + 3) & ~3u;
    const unsigned head = Win98MetafileEmf::HeaderBytes + description;
    const unsigned total = head + e.bytes;
    auto *image = static_cast<unsigned char *>(malloc(total));
    if (!image)
    {
        last_error = 8;
        return 0;
    }
    memset(image, 0, head);
    Put32(image, Win98MetafileEmf::Header);
    Put32(image + 4, head);
    for (unsigned i = 0; i < 4; ++i)
        Put32(image + 8 + i * 4, unsigned(e.bounds[i]));
    if (!e.framed)
    {
        // Without an explicit frame, convert bounds to hundredths of a
        // millimeter as required by the format.
        for (unsigned i = 0; i < 4; ++i)
        {
            const unsigned axis = i & 1;
            const int value = e.bounds[i];
            const int pixels = int(e.device[axis] ? e.device[axis] : 1);
            const int size = int(e.millimeters[axis] ? e.millimeters[axis] : 1);
            e.frame[i] = int((long long)value * size * 100 / pixels);
        }
    }
    for (unsigned i = 0; i < 4; ++i)
        Put32(image + 24 + i * 4, unsigned(e.frame[i]));
    Put32(image + 40, Win98MetafileEmf::Signature);
    Put32(image + 44, Win98MetafileEmf::VersionValue);
    Put32(image + 48, total);
    Put32(image + 52, e.records + 1);
    Put16(image + 56, e.objects ? e.objects : 1);
    Put16(image + 58, 0);
    Put32(image + 60, e.description_bytes / 2);
    Put32(image + 64, e.description_bytes ? Win98MetafileEmf::HeaderBytes : 0);
    Put32(image + 68, 0);
    Put32(image + 72, e.device[0]);
    Put32(image + 76, e.device[1]);
    Put32(image + 80, e.millimeters[0]);
    Put32(image + 84, e.millimeters[1]);
    if (e.description_bytes)
        memcpy(image + Win98MetafileEmf::HeaderBytes, e.description, e.description_bytes);
    memcpy(image + head, e.bits, e.bytes);
    free(e.bits);
    e.bits = image;
    e.bytes = e.capacity = total;
    e.recording = false;
    metafiles->recording_count--;
    const unsigned dc = e.dc;
    e.dc = 0;
    if (dc)
    {
        const unsigned request[] = {dc};
        GdiApi("DeleteDC", request);
    }
    if (e.file[0] && !MetafileStore(e.file, e.bits, e.bytes))
    {
        MetafileRelease(e);
        return 0;
    }
    return MetafileHandleOf(&e);
}

// Accept a complete EMF image.
unsigned Application::EnhancedAdopt(const unsigned char *data, unsigned bytes)
{
    if (bytes < Win98MetafileEmf::HeaderBytes || Get32(data) != Win98MetafileEmf::Header ||
        Get32(data + 40) != Win98MetafileEmf::Signature || Get32(data + 4) > bytes ||
        Get32(data + 48) != bytes)
    {
        last_error = 87;
        return 0;
    }
    auto &store = Metafiles();
    for (auto &e : store.entries)
        if (!e.used)
        {
            e.bits = static_cast<unsigned char *>(malloc(bytes));
            if (!e.bits)
            {
                last_error = 8;
                return 0;
            }
            memcpy(e.bits, data, bytes);
            e.used = true;
            e.enhanced = true;
            e.recording = false;
            e.bytes = e.capacity = bytes;
            e.objects = Get16(e.bits + 56);
            e.records = Get32(e.bits + 52);
            return MetafileHandleOf(&e);
        }
    last_error = 8;
    return 0;
}

// Play an EMF record into the target DC. Return FALSE for types without a
// player.
unsigned Application::EnhancedPlayRecord(unsigned dc, unsigned handles, unsigned count,
                                         const unsigned char *record, unsigned bytes)
{
    if (bytes < 8)
        return 0;
    const unsigned type = Get32(record), size = Get32(record + 4);
    if (size < 8 || size > bytes)
        return 0;
    const unsigned char *payload = record + 8;
    const unsigned payload_bytes = size - 8;
    if (type == Win98MetafileEmf::Header || type == Win98MetafileEmf::Eof)
        return 1;
    EnhancedPlayback *playback = metafiles ? metafiles->playback : nullptr;
    while (playback && (playback->dc != dc || playback->handles != handles)) playback = playback->previous;
    // Realize at output, as GDI does. State records retain the logical
    // extents in the playback context, rather than multiplying target scaling
    // repeatedly into already transformed extents.
    if (playback && playback->logical &&
        (type == 15 || type == 29 || type == 30 || (type >= 42 && type <= 47) ||
         type == 54 || type == 76 || type == 83 || type == 84 || type == 86 || type == 87))
    {
        const char *queries[] = {"GetWindowOrgEx", "GetViewportOrgEx", "GetWindowExtEx", "GetViewportExtEx"};
        int values[8];
        for (unsigned i = 0; i < 4; ++i)
        {
            const unsigned args[] = {playback->logical, playback->scratch + i * 8};
            GdiApi(ApiName(queries[i]), args);
        }
        if (!Read(playback->scratch, values, sizeof values)) return 0;
        int window[2], viewport[2], origin[2];
        for (unsigned axis = 0; axis < 2; ++axis)
        {
            if (!values[4 + axis]) return 0;
            const double scale = playback->scale[axis] * values[6 + axis] / values[4 + axis];
            const double absolute = scale < 0 ? -scale : scale;
            if (!(absolute >= 1.0 / 32768 && absolute < 32768)) return 0;
            // GDI normalizes an EFLOAT into signed 16-bit viewport
            // and window extents. Subunit scales use the maximum denominator.
            unsigned denominator = 32767;
            if (absolute >= 1)
            {
                denominator = 16384;
                while (absolute * denominator >= 32768 && denominator > 1) denominator >>= 1;
            }
            const double numerator = scale * (absolute < 1 ? 32768 : denominator);
            viewport[axis] = int(numerator);
            if (numerator < viewport[axis]) --viewport[axis];
            window[axis] = int(denominator);
            const double translated = playback->offset[axis] + playback->scale[axis] * values[2 + axis];
            if (!(translated >= -32768 && translated <= 32767)) return 0;
            origin[axis] = int(translated < 0 ? translated - .5 : translated + .5);
        }
        const unsigned mode[] = {dc, 8}; GdiApi("SetMapMode", mode);
        const unsigned wo[] = {dc, unsigned(values[0]), unsigned(values[1]), 0};
        const unsigned vo[] = {dc, unsigned(origin[0]), unsigned(origin[1]), 0};
        const unsigned we[] = {dc, unsigned(window[0]), unsigned(window[1]), 0};
        const unsigned ve[] = {dc, unsigned(viewport[0]), unsigned(viewport[1]), 0};
        GdiApi("SetWindowOrgEx", wo); GdiApi("SetViewportOrgEx", vo);
        GdiApi("SetWindowExtEx", we); GdiApi("SetViewportExtEx", ve);
    }
    if (const auto *table = Win98MetafileEmf::Find(type))
    {
        unsigned args[10] = {dc};
        unsigned used = 0;
        for (const char *p = table->shape; *p; ++p)
        {
            const unsigned index = 1 + unsigned(p - table->shape);
            if (*p == '0')
            {
                args[index] = 0;
                continue;
            }
            if (used + 4 > payload_bytes)
                return 0;
            args[index] = Get32(payload + used);
            used += 4;
        }
        if (playback && playback->logical)
        {
            if ((type >= 9 && type <= 12) || type == 17 || type == 31 || type == 32)
            {
                args[0] = playback->logical;
                GdiApi(ApiName(table->api), args);
                return state == Failed ? 0 : 1;
            }
            if (type == 33 || type == 34)
            {
                args[0] = playback->logical; GdiApi(ApiName(table->api), args); args[0] = dc;
            }
        }
        // As with WMF, these calls usually return previous state, not a
        // success flag.
        GdiApi(ApiName(table->api), args);
        return state == Failed ? 0 : 1;
    }
    switch (type)
    {
    case 76: // EMR_BITBLT without a source bitmap (PatBlt)
    {
        if(payload_bytes<92 || Get32(payload+80) || Get32(payload+88))return 0;
        const unsigned args[]={dc,Get32(payload+16),Get32(payload+20),Get32(payload+24),Get32(payload+28),Get32(payload+32)};
        return GdiApi("PatBlt",args);
    }
    case 75: // EMR_EXTSELECTCLIPRGN
    {
        if (payload_bytes < 8) return 0;
        const unsigned length = Get32(payload), mode = Get32(payload + 4);
        if (length > payload_bytes - 8) return 0;
        unsigned region = 0;
        if (length)
        {
            const unsigned scratch = Allocate(length);
            if (!scratch) return 0;
            const unsigned create[] = {0, length, scratch};
            if (Store(scratch, payload + 8, length)) region = GdiApi("ExtCreateRegion", create);
            Free(scratch); if (!region) return 0;
        }
        const unsigned select[] = {dc, region, mode};
        const unsigned result = GdiApi("ExtSelectClipRgn", select);
        if (region) { const unsigned dead[] = {region}; GdiApi("DeleteObject", dead); }
        return result != 0;
    }
    case Win98MetafileEmf::SelectObject:
    {
        if (payload_bytes < 4)
            return 0;
        const unsigned index = Get32(payload);
        unsigned handle = 0;
        if (index & Win98MetafileEmf::StockObject)
        {
            const unsigned stock[] = {index & 0x7fffffffu};
            handle = Api("GetStockObject", stock);
        }
        else if (index < count)
            Read(handles + index * 4, &handle, 4);
        if (!handle)
            return 0;
        const unsigned args[] = {dc, handle};
        return GdiApi("SelectObject", args) ? 1 : 0;
    }
    case Win98MetafileEmf::DeleteObject:
    {
        if (payload_bytes < 4)
            return 0;
        const unsigned index = Get32(payload);
        unsigned handle = 0;
        if (index >= count || !Read(handles + index * 4, &handle, 4) || !handle)
            return 0;
        const unsigned args[] = {handle};
        const unsigned result = GdiApi("DeleteObject", args);
        const unsigned empty = 0;
        Store(handles + index * 4, &empty, 4);
        return result ? 1 : 0;
    }
    case Win98MetafileEmf::CreatePen:
    case Win98MetafileEmf::CreateBrushIndirect:
    {
        const unsigned structure = type == Win98MetafileEmf::CreatePen ? 16u : 12u;
        if (payload_bytes < 4 + structure)
            return 0;
        const unsigned index = Get32(payload);
        if (!index || index >= count)
            return 0;
        const unsigned scratch = Allocate(structure);
        if (!scratch)
            return 0;
        const unsigned args[] = {scratch};
        const unsigned handle =
            Store(scratch, payload + 4, structure)
                ? GdiApi(type == Win98MetafileEmf::CreatePen ? ApiName("CreatePenIndirect")
                                                             : ApiName("CreateBrushIndirect"),
                         args)
                : 0;
        Free(scratch);
        if (!handle)
            return 0;
        return Store(handles + index * 4, &handle, 4) ? 1 : 0;
    }
    case Win98MetafileEmf::ExtCreateFontIndirectW:
    {
        if (payload_bytes < 324) return 0;
        const unsigned index = Get32(payload);
        if (!index || index >= count) return 0;
        unsigned char font[60] = {};
        memcpy(font, payload + 4, 28);
        for (unsigned i = 0; i < 31; ++i)
        {
            const unsigned character = Get16(payload + 32 + i * 2);
            if (!character) break;
            font[28 + i] = (unsigned char)AnsiNarrow(character);
        }
        const unsigned scratch = Allocate(sizeof font);
        if (!scratch) return 0;
        const unsigned args[] = {scratch};
        const unsigned handle = Store(scratch, font, sizeof font)
                                  ? GdiApi("CreateFontIndirectA", args) : 0;
        Free(scratch);
        if (!handle) return 0;
        return Store(handles + index * 4, &handle, 4) ? 1 : 0;
    }
    case Win98MetafileEmf::Polygon16:
    case Win98MetafileEmf::Polyline16:
    {
        if (payload_bytes < 20)
            return 0;
        const unsigned count16 = Get32(payload + 16);
        if (!count16 || count16 > 8192 || payload_bytes < 20 + count16 * 4)
            return 0;
        const unsigned scratch = Allocate(count16 * 8);
        if (!scratch)
            return 0;
        bool ok = true;
        for (unsigned i = 0; ok && i < count16; ++i)
        {
            const int point[2] = {Signed16(Get16(payload + 20 + i * 4)),
                                  Signed16(Get16(payload + 22 + i * 4))};
            ok = Store(scratch + i * 8, point, sizeof point);
        }
        const unsigned args[] = {dc, scratch, count16};
        const unsigned result =
            ok ? GdiApi(type == Win98MetafileEmf::Polygon16 ? ApiName("Polygon") : ApiName("Polyline"),
                        args)
               : 0;
        Free(scratch);
        return result ? 1 : 0;
    }
    case Win98MetafileEmf::ExtTextOutA:
    case Win98MetafileEmf::ExtTextOutW:
    {
        if (payload_bytes < 68)
            return 0;
        const unsigned x = Get32(payload + 28), y = Get32(payload + 32);
        const unsigned length = Get32(payload + 36), offset = Get32(payload + 40);
        const unsigned options = Get32(payload + 44);
        const unsigned text_bytes=length*(type==Win98MetafileEmf::ExtTextOutW?2:1);
        if (length > 4096 || offset < 8 || offset - 8 > payload_bytes || text_bytes > payload_bytes-(offset-8))
            return 0;
        const bool boxed = (options & (Gdi::ETO_OPAQUE | Gdi::ETO_CLIPPED)) != 0;
        const unsigned dx_offset = Get32(payload + 64);
        if (dx_offset && (dx_offset < 8 || dx_offset - 8 > payload_bytes ||
                          length > (payload_bytes - (dx_offset - 8)) / 4)) return 0;
        const unsigned scratch = Allocate(length + 24 + length * 4);
        if (!scratch)
            return 0;
        char text[4096];
        for(unsigned i=0;i<length;++i)text[i]=type==Win98MetafileEmf::ExtTextOutW
            ?char(AnsiNarrow(Get16(payload+offset-8+i*2))):char(payload[offset-8+i]);
        bool ok = !length || Store(scratch, text, length);
        unsigned rect = 0;
        if (ok && boxed)
        {
            rect = scratch + length + 8;
            ok = Store(rect, payload + 48, 16);
        }
        const unsigned dx = dx_offset ? scratch + length + 24 : 0;
        if (ok && dx) ok = Store(dx, payload + dx_offset - 8, length * 4);
        const unsigned args[] = {dc, x, y, options, rect, scratch, length, dx};
        const unsigned result = ok ? GdiApi("ExtTextOutA", args) : 0;
        Free(scratch);
        return result ? 1 : 0;
    }
    default:
        break;
    }
    return 0;
}

// PlayEnhMetaFile maps the metafile frame to the target rectangle.
// SaveDC/RestoreDC preserves the previous mapping mode.
unsigned Application::EnhancedPlay(unsigned dc, MetafileSlot &e, unsigned rectangle,
                                   unsigned procedure, unsigned data)
{
    const unsigned header = Get32(e.bits + 4);
    if (header < Win98MetafileEmf::HeaderBytes || header > e.bytes)
        return 0;
    const unsigned count = Get16(e.bits + 56) ? Get16(e.bits + 56) : 1u;
    const unsigned handles = Allocate(count * 4 + (procedure ? 65536u : 0u));
    if (!handles)
    {
        last_error = 8;
        return 0;
    }
    for (unsigned i = 0; i < count; ++i)
    {
        const unsigned empty = 0;
        Store(handles + i * 4, &empty, 4);
    }
    const unsigned save_args[] = {dc};
    const unsigned saved = dc ? GdiApi("SaveDC", save_args) : 0;
    if (dc && !saved) { Free(handles); return 0; }
    if (saved)
    {
        const struct { const char *api; unsigned value; } defaults[] = {
            {"SetTextAlign",0},{"SetBkColor",0xffffff},{"SetTextColor",0},
            {"SetROP2",13},{"SetArcDirection",1},{"SetPolyFillMode",1},{"SetStretchBltMode",1}
        };
        for (const auto &setting : defaults)
        {
            const unsigned args[] = {dc, setting.value}; GdiApi(ApiName(setting.api), args);
        }
    }
    const unsigned copy = handles + count * 4;
    // Callbacks may delete the enumerated metafile and free bits. Use a copy
    // for callback enumeration; direct playback can read the original
    // storage.
    const unsigned bytes = e.bytes;
    unsigned char *owned = nullptr;
    if (procedure)
    {
        owned = static_cast<unsigned char *>(malloc(bytes ? bytes : 1));
        if (!owned)
        {
            last_error = 8;
            if (saved) { const unsigned restore[] = {dc, saved}; GdiApi("RestoreDC", restore); }
            Free(handles);
            return 0;
        }
        memcpy(owned, e.bits, bytes);
    }
    const unsigned char *image = owned ? owned : e.bits;
    EnhancedPlayback playback;
    playback.dc = dc; playback.handles = handles;
    int target[4];
    if (saved && rectangle && Read(rectangle, target, sizeof target))
    {
        playback.logical = Gdi::CreateCompatibleDC(dc);
        playback.scratch = Allocate(32);
        if (!playback.logical || !playback.scratch)
        {
            if (playback.logical) Gdi::DeleteDC(playback.logical);
            if (playback.scratch) Free(playback.scratch);
            const unsigned restore[] = {dc, saved}; GdiApi("RestoreDC", restore);
            free(owned); Free(handles); last_error = 8; return 0;
        }
        // GDI: physical frame -> requested logical rectangle ->
        // destination device coordinates. rclBounds is not the playback frame.
        int destination[4]; memcpy(destination, target, sizeof target);
        Store(playback.scratch, destination, sizeof destination);
        const unsigned convert[] = {dc, playback.scratch, 2};
        GdiApi("LPtoDP", convert); Read(playback.scratch, destination, sizeof destination);
        for (unsigned axis = 0; axis < 2; ++axis)
        {
            const int start = int(Get32(image + 24 + axis * 4));
            const int finish = int(Get32(image + 32 + axis * 4));
            const unsigned pixels = Get32(image + 72 + axis * 4);
            const unsigned mm = Get32(image + 80 + axis * 4);
            const double span = double(finish) - start;
            const double target_span = double(destination[axis + 2]) - destination[axis];
            const double physical = span ? target_span / span : target_span + 1;
            playback.scale[axis] = pixels ? physical * (double(mm) * 100 / pixels) : 1;
            playback.offset[axis] = destination[axis] - physical * start;
        }
        const unsigned mode[] = {dc, 8}, origin[] = {dc, 0, 0, 0}, extent[] = {dc, 1, 1, 0};
        GdiApi("SetMapMode", mode);
        GdiApi("SetWindowOrgEx", origin); GdiApi("SetViewportOrgEx", origin);
        GdiApi("SetWindowExtEx", extent); GdiApi("SetViewportExtEx", extent);
    }
    playback.previous = Metafiles().playback;
    Metafiles().playback = &playback;

    // Enumeration includes the header; playback starts after it.
    unsigned at = procedure ? 0 : header, result = 1;
    while (at + 8 <= bytes)
    {
        const unsigned size = Get32(image + at + 4);
        if (size < 8 || (size & 3) || at + size > bytes)
        {
            result = 0;
            break;
        }
        if (procedure)
        {
            if (size > 65536 || !Store(copy, image + at, size))
            {
                result = 0;
                break;
            }
            const unsigned arguments[] = {dc, handles, copy, count, data};
            if (!Call(procedure, arguments, 5) || state == Failed)
                break;
        }
        else if (!EnhancedPlayRecord(dc, handles, count, image + at, size) || state == Failed)
        {
            if(log)
            {
                char message[]="EMF playback: record 0x00000000 failed";
                const unsigned type=Get32(image+at);
                for(unsigned i=0;i<8;++i)message[23+i]="0123456789abcdef"[(type>>(28-i*4))&15];
                log(message);
            }
            result = 0;
            break;
        }
        if (Get32(image + at) == Win98MetafileEmf::Eof)
            break;
        at += size;
    }
    Metafiles().playback = playback.previous;
    if (playback.logical) Gdi::DeleteDC(playback.logical);
    if (playback.scratch) Free(playback.scratch);
    free(owned);
    if (saved)
    {
        const unsigned restore[] = {dc, saved};
        GdiApi("RestoreDC", restore);
    }

    for (unsigned i = 0; i < count; ++i)
    {
        unsigned handle = 0;
        if (Read(handles + i * 4, &handle, 4) && handle)
        {
            const unsigned dead[] = {handle};
            GdiApi("DeleteObject", dead);
        }
    }
    Free(handles);
    return result;
}

unsigned Application::EnhancedApi(ApiName name, const unsigned *a)
{
    auto &store = Metafiles();
    switch (name.id)
    {
    case API_ID("CreateEnhMetaFileA"):
    {
        for (auto &e : store.entries)
            if (!e.used)
            {
                // Advance the slot generation so stale handles cannot
                // identify a new metafile.
                const unsigned generation = e.generation;
                e = MetafileSlot{};
                e.generation = generation;
                if (a[1])
                {
                    const char *path = ReadString(a[1]);
                    if (!path)
                        return 0;
                    unsigned i = 0;
                    while (i < 259 && path[i])
                    {
                        e.file[i] = path[i];
                        ++i;
                    }
                    e.file[i] = 0;
                }
                if (a[2])
                {
                    int frame[4];
                    if (!Read(a[2], frame, sizeof frame))
                        return 0;
                    for (unsigned i = 0; i < 4; ++i)
                        e.frame[i] = frame[i];
                    e.framed = true;
                }
                if (a[3])
                {
                    // Descriptions are double-null-terminated UTF-16. Widen
                    // ANSI bytes directly from guest memory; ReadString would
                    // stop at the first null and truncate the description.
                    unsigned char text[252] = {};
                    if (!Read(a[3], text, sizeof text - 2))
                    {
                        unsigned i = 0;
                        while (i < sizeof text - 2 && Read(a[3] + i, text + i, 1) && text[i])
                            ++i;
                    }
                    unsigned i = 0;
                    while (i < 250 && (text[i] || text[i + 1]))
                    {
                        Put16(e.description + i * 2, text[i]);
                        ++i;
                    }
                    Put16(e.description + i * 2, 0);
                    Put16(e.description + i * 2 + 2, 0);
                    e.description_bytes = i * 2 + 4;
                }
                const unsigned dc = OwnGdi(Gdi::CreateCompatibleDC(0), 2);
                if (!dc)
                {
                    last_error = 8;
                    return 0;
                }
                const unsigned reference = a[0] && Gdi::ValidDC(a[0]) ? a[0] : dc;
                e.device[0] = Gdi::DeviceCaps(reference, 8);   // HORZRES
                e.device[1] = Gdi::DeviceCaps(reference, 10);  // VERTRES
                e.millimeters[0] = Gdi::DeviceCaps(reference, 4);  // HORZSIZE
                e.millimeters[1] = Gdi::DeviceCaps(reference, 6);  // VERTSIZE
                e.used = e.recording = e.enhanced = true;
                e.dc = dc;
                store.recording_count++;
                return dc;
            }
        last_error = 8;
        return 0;
    }
    case API_ID("CloseEnhMetaFile"):
    {
        auto *e = RecordingMetafile(a[0]);
        if (!e || !e->enhanced)
        {
            last_error = 6;
            return 0;
        }
        return EnhancedClose(*e);
    }
    case API_ID("DeleteEnhMetaFile"):
    {
        auto *e = MetafileEntry(a[0]);
        if (!e || !e->enhanced)
        {
            last_error = 6;
            return 0;
        }
        MetafileRelease(*e);
        return 1;
    }
    case API_ID("GetEnhMetaFileBits"):
    {
        auto *e = MetafileEntry(a[0]);
        if (!e || !e->enhanced || e->recording)
        {
            last_error = 6;
            return 0;
        }
        if (!a[2])
            return e->bytes;
        if (a[1] < e->bytes)
            return 0;
        return Store(a[2], e->bits, e->bytes) ? e->bytes : 0;
    }
    case API_ID("GetWinMetaFileBits"):
    {
        // GetWinMetaFileBits(hemf, cbBuffer, lpbBuffer, fnMapMode, hdcRef):
        // the records of an enhanced metafile as a Windows metafile. GDI32
        // plays the enhanced records into an old-format recording and hands
        // out its bits; with a null buffer it answers the size. The scaling
        // the original derives from fnMapMode and the reference device is not
        // implemented: the records arrive in the units they were recorded in.
        auto *source = MetafileEntry(a[0]);
        if (!source || !source->enhanced || source->recording) { last_error = 6; return 0; }
        if (a[2] && (!a[1] || !virtual_memory.Access(a[2], a[1], true))) { last_error = 87; return 0; }
        const unsigned none[] = {0};
        const unsigned recording = MetafileApi("CreateMetaFileA", none);
        if (!recording) return 0;
        const unsigned play[] = {recording, a[0], 0};
        EnhancedApi("PlayEnhMetaFile", play);
        const unsigned close[] = {recording};
        const unsigned old_format = MetafileApi("CloseMetaFile", close);
        if (!old_format) return 0;
        const unsigned bits[] = {old_format, a[2] ? a[1] : 0, a[2]};
        const unsigned result = MetafileApi("GetMetaFileBitsEx", bits);
        const unsigned drop[] = {old_format};
        MetafileApi("DeleteMetaFile", drop);
        return result;
    }
    case API_ID("SetWinMetaFileBits"):
    {
        // Positive anisotropic/isotropic extents specify a frame in 0.01 mm.
        // Otherwise the reference device supplies the initial viewport. WMF
        // window extents then determine how much of that surface is drawn.
        if (!a[0] || !a[1] || !virtual_memory.Access(a[1], a[0], false)) { last_error = 87; return 0; }
        if (a[3] && !virtual_memory.Access(a[3], 16, false)) { last_error = 87; return 0; }
        const unsigned bits[] = {a[0], a[1]};
        const unsigned old_format = MetafileApi("SetMetaFileBitsEx", bits);
        if (!old_format) return 0;
        unsigned frame = 0;
        if (a[3])
        {
            const unsigned mode = Read32(a[3]);
            const int width = (int)Read32(a[3] + 4), height = (int)Read32(a[3] + 8);
            if ((mode == 7 || mode == 8) && width > 0 && height > 0)
            {
                frame = Allocate(16);
                if (frame) { const unsigned rectangle[4] = {0, 0, (unsigned)width, (unsigned)height}; Store(frame, rectangle, sizeof rectangle); }
            }
        }
        const unsigned create[] = {a[2], 0, frame, 0};
        const unsigned recording = EnhancedApi("CreateEnhMetaFileA", create);
        if (frame) Free(frame);
        unsigned result = 0;
        if (recording)
        {
            const unsigned mode = a[3] ? Read32(a[3]) : 8;
            if (mode == 7 || mode == 8)
            {
                const auto *entry = RecordingMetafile(recording);
                int width = entry->device[0], height = entry->device[1];
                const int x = a[3] ? int(Read32(a[3] + 4)) : 0;
                const int y = a[3] ? int(Read32(a[3] + 8)) : 0;
                if (x > 0 && y > 0)
                {
                    width = int((static_cast<long long>(x) * width) / (entry->millimeters[0] * 100));
                    height = int((static_cast<long long>(y) * height) / (entry->millimeters[1] * 100));
                }
                if (!width) width = 1;
                if (!height) height = 1;
                const unsigned mapping[] = {recording, mode};
                const unsigned extent[] = {recording, unsigned(width), unsigned(height), 0};
                GdiApi("SetMapMode", mapping);
                GdiApi("SetWindowExtEx", extent);
                GdiApi("SetViewportExtEx", extent);
            }
            const unsigned play[] = {recording, old_format};
            MetafileApi("PlayMetaFile", play);
            const unsigned close[] = {recording};
            result = EnhancedApi("CloseEnhMetaFile", close);
        }
        const unsigned drop[] = {old_format};
        MetafileApi("DeleteMetaFile", drop);
        return result;
    }
    case API_ID("SetEnhMetaFileBits"):
    {
        if (!a[0] || !a[1] || a[0] > MetafileLimit)
        {
            last_error = 87;
            return 0;
        }
        auto *data = static_cast<unsigned char *>(malloc(a[0]));
        if (!data)
        {
            last_error = 8;
            return 0;
        }
        const unsigned handle = Read(a[1], data, a[0]) ? EnhancedAdopt(data, a[0]) : 0;
        free(data);
        return handle;
    }
    case API_ID("GetEnhMetaFileHeader"):
    {
        auto *e = MetafileEntry(a[0]);
        if (!e || !e->enhanced || e->recording)
        {
            last_error = 6;
            return 0;
        }
        const unsigned header = Get32(e->bits + 4);
        if (!a[2])
            return header;
        const unsigned length = a[1] < header ? a[1] : header;
        return length && Store(a[2], e->bits, length) ? length : 0;
    }
    case API_ID("GetEnhMetaFilePaletteEntries"):
    {
        auto *e = MetafileEntry(a[0]);
        if (!e || !e->enhanced || e->recording)
        {
            last_error = 6;
            return ~0u;
        }
        // This recorder emits no palette records, so nPalEntries is zero.
        return Get32(e->bits + 68);
    }
    case API_ID("GetEnhMetaFileDescriptionA"):
    {
        auto *e = MetafileEntry(a[0]);
        if (!e || !e->enhanced || e->recording)
        {
            last_error = 6;
            return ~0u;
        }
        const unsigned characters = Get32(e->bits + 60), offset = Get32(e->bits + 64);
        if (!characters || !offset || offset + characters * 2 > e->bytes)
            return 0;
        if (!a[2])
            return characters;
        const unsigned length = a[1] < characters ? a[1] : characters;
        char text[256];
        const unsigned copied = length < sizeof text ? length : unsigned(sizeof text);
        for (unsigned i = 0; i < copied; ++i)
            text[i] = char(Get16(e->bits + offset + i * 2));
        return Store(a[2], text, copied) ? copied : 0;
    }
    case API_ID("CopyEnhMetaFileA"):
    {
        auto *e = MetafileEntry(a[0]);
        if (!e || !e->enhanced || e->recording)
        {
            last_error = 6;
            return 0;
        }
        const unsigned handle = EnhancedAdopt(e->bits, e->bytes);
        auto *copy = MetafileEntry(handle);
        if (handle && a[1])
        {
            const char *path = ReadString(a[1]);
            if (!path)
                return 0;
            unsigned i = 0;
            while (i < 259 && path[i])
            {
                copy->file[i] = path[i];
                ++i;
            }
            copy->file[i] = 0;
            if (!MetafileStore(copy->file, copy->bits, copy->bytes))
                return 0;
        }
        return handle;
    }
    case API_ID("PlayEnhMetaFile"):
    {
        auto *e = MetafileEntry(a[1]);
        if (!e || !e->enhanced || e->recording || !Gdi::ValidDC(a[0]))
        {
            last_error = 6;
            return 0;
        }
        return EnhancedPlay(a[0], *e, a[2], 0, 0);
    }
    case API_ID("PlayEnhMetaFileRecord"):
    {
        unsigned char head[8];
        if (!a[2] || !Read(a[2], head, sizeof head))
            return 0;
        const unsigned size = Get32(head + 4);
        if (size < 8 || (size & 3) || size > 65536)
            return 0;
        auto *record = static_cast<unsigned char *>(malloc(size));
        if (!record)
        {
            last_error = 8;
            return 0;
        }
        const unsigned result =
            Read(a[2], record, size) ? EnhancedPlayRecord(a[0], a[1], a[3], record, size) : 0;
        free(record);
        return result;
    }
    case API_ID("EnumEnhMetaFile"):
    {
        auto *e = MetafileEntry(a[1]);
        if (!e || !e->enhanced || e->recording || !a[2] || (a[0] && !a[4]))
        {
            last_error = 6;
            return 0;
        }
        return EnhancedPlay(a[0], *e, a[4], a[2], a[3]);
    }
    case API_ID("gdiPlaySpoolStream"):
        // Printing is not implemented. Without a spooler there is no stream
        // for this call to play.
        last_error = 50;
        return 0;
    default:
        break;
    }
    last_error = 120;
    return 0;
}

// For the gates of src/x86_gdi32_gates.cpp, which see only the DC: 0 when
// it records nothing, 1 for a WMF DC, 2 for an EMF DC.
unsigned Application::MetafileKindOf(unsigned dc)
{
    const auto *e = RecordingMetafile(dc);
    return !e ? 0 : e->enhanced ? 2 : 1;
}
unsigned Application::MetafileObjectType(unsigned handle)
{
    if(const auto *e=MetafileEntry(handle))return e->recording?0:e->enhanced?13:9;
    if(const auto *e=RecordingMetafile(handle))return e->enhanced?12:4;
    return 0;
}
bool Application::EnhancedRecordRaw(unsigned dc, unsigned type, const unsigned char *payload, unsigned bytes)
{
    auto *e = RecordingMetafile(dc);
    return e && e->enhanced && EnhancedWrite(*e, type, payload, bytes);
}
void Application::EnhancedBoundsRaw(unsigned dc, int left, int top, int right, int bottom)
{
    if (auto *e = RecordingMetafile(dc); e && e->enhanced)
        EnhancedBounds(*e, left, top, right, bottom);
}
// GetEnhMetaFileA, GDI over the helper : the file must hold a
// header of 58h bytes with iType 1, the signature " EMF", a nonzero handle
// count and a byte count that is a multiple of four. The original keeps the
// file open and reads records on demand; the port takes the nBytes bytes at
// once, so a file shorter than its own byte count is refused here.
unsigned Application::EnhancedOpenFile(unsigned path)
{
    if (!path || !virtual_memory.Access(path, 1, false))
        return 0;
    unsigned bytes = 0;
    unsigned char *data = MetafileLoad(path, bytes);
    if (!data)
        return 0;
    unsigned handle = 0;
    if (bytes >= 0x58 && Get32(data) == 1 && Get32(data + 40) == Win98MetafileEmf::Signature &&
        Get16(data + 56) && !(Get32(data + 48) & 3) && Get32(data + 48) <= bytes)
        handle = EnhancedAdopt(data, Get32(data + 48));
    free(data);
    return handle;
}
} // namespace X86

