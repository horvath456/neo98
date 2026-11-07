// SPDX-License-Identifier: GPL-2.0-or-later
#include "gdi.h"
#include <new>
#include "gdi_state.h"
#include "gdi_truetype.h"
#include <stdint.h>
#include "win98_gdi_object_type.h"
#include "win98_gdi_owner.h"
#include "win98_gdi_visible.h"
#include "win98_gdi_clip_box.h"
#include "win98_dc_attributes.h"
#include "win98_gdi_text_layout.h"
#include "win98_text_simulation.h"
#include "win98_dc_information.h"
#include "win98_bitmap.h"
#include "win98_bitmap_thunk.h"
#include "win98_bitmap_entry.h"
#include "win98_bitmap_query.h"
#include "win98_pattern_brush.h"
#include "win98_focus_rect.h"
#include "win98_dc_map.h"
#include "win98_region.h"
#include "win98_region_api.h"
#include "win98_region_clip.h"
#include "win98_dib_layout.h"
#include "win98_dib_rle.h"
#include "win98_dib_public.h"
#include "win98_dib_color.h"
#include "win98_dib_raster.h"
#include "win98_pen_line.h"
#include "win98_pen_width.h"
#include "win98_gdi_pixel.h"
#include "win98_gdi_palette.h"
#include "win98_gdi_realize.h"
#include "win98_dib_dither.h"
#include "win98_dib_palette.h"
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "win98_gdi_palette_tables.h"
#endif
#include "win98_gdi_shapes.h"
#include "win98_gdi_arcs.h"
#include "win98_gdi_region_shape.h"
#include "win98_gdi_hatch.h"
#include "win98_gdi_path.h"
#include "win98_gdi_widen.h"
#include "win98_dib_stretch.h"
#include "win98_dib_stretch_objects.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <new>
// Needs calloc and free from <stdlib.h>.
#include "win98_gdi_flood.h"

namespace Gdi {
namespace {
// Native slots grow without relocating objects. A WORD retains fifteen index
// bits and one generation bit; GDI's original local/32-bit heap handles are
// even WORDs, so this covers their entire nonzero handle count.
constexpr unsigned MaxIndexMask = (1u << 15) - 1, ClipCapacity = 64;
unsigned IndexBits = 15, IndexMask = MaxIndexMask;
unsigned Capacity=0, free_object_hint=0;
enum Kind { Empty, BitmapObject, BrushObject, FontObject, DCObject, RegionObject, PenObject, PaletteObject, ColorSpaceObject };
struct ClipSnapshot {unsigned references,raw,count;RECT* rectangles;};
struct State {
    HBITMAP bitmap = 0;
    HBRUSH brush = 0;
    HFONT font = 0;
    unsigned pen = 0, palette = 0;
    unsigned color_space = 0, icm_mode = 1;
    char color_profile[260] = {};
    Win98PenLine::Point position{};
    Win98DcMap::State mapping;
    POINT surface_origin = {}; // A local surface placed at a device origin (menus, probes), independent of GDI state.
    // GDI32 reads and writes the brush origin as part of the DC state,
    // so SaveDC/RestoreDC carry it with everything else here.
    POINT brush_origin = {};
    RECT clip[ClipCapacity] = {};
    ClipSnapshot* clip_snapshot = nullptr;
    ClipSnapshot* application_snapshot = nullptr;
    ClipSnapshot* system_snapshot = nullptr;
    // GDI DC+14h, the meta region SetMetaRgn builds out of the
    // application clip; the effective clip is intersected with it.
    ClipSnapshot* meta_snapshot = nullptr;
    // GDI DC+74h, the miter limit as an IEEE float; 10.0 from the DC
    // template.
    unsigned miter_limit = 0x41200000u;
    unsigned arc_flags = 0;
    // GDI DC+E6h..F4h and the flag byte DC+FEh of SetBoundsRect : the
    // application and the window-manager bounds in surface coordinates, bit 1
    // and 4 "holds data", bit 2 and 8 "accumulates". A new DC is zeroed.
    short bounds[2][4] = {};
    unsigned bounds_flags = 0;
    unsigned clips = 1;
    unsigned stretch_mode = 1;
    unsigned poly_fill_mode = 1;
    unsigned text_align = 0;
    // GDI DC+A4h, the font mapper flag word SetMapperFlags exchanges.
    unsigned mapper_flags = 0;
    // GDI DC+ACh and +AEh, the aspect ratio filter Windows 98 computes when
    // SetMapperFlags turns ASPECT_FILTERING on; GetAspectRatioFilterEx reads
    // them. A new DC starts with zero (Windows 98 allocates it zeroed).
    int aspect_filter_x = 0, aspect_filter_y = 0;
    DibText::DrawMode text_mode = {13, OPAQUE};
    // The surface of a DC over a device driver (a printer) is not a bitmap
    // but the page of its GDIINFO, dpHorzRes by dpVertRes (GDI builds the
    // visible region of the DC from it). A clip without an application region
    // and the clip box are bounded by it, as by a bitmap's size elsewhere.
    int device_width = 0, device_height = 0;
};
// The path store of a device context. Windows 98 keeps the path object in the
// WORD at DC+78h and its three bits in the byte at DC+7Ah; see
// src/win98_gdi_path.h.
struct GdiPath {
    Win98GdiPath::Point *points;
    unsigned char *types;
    unsigned count, capacity;
};
struct SavedState { State state; SavedState *previous; };
struct Context {
    State current;
    SavedState *saved = nullptr;
    ~Context() { while(saved){auto *previous=saved->previous;delete saved;saved=previous;} }
    unsigned saves = 0;
    unsigned signature = 0x4f4d;
    bool device_dc = false;
    bool memory_dc = true;
    unsigned char original_initial[264] = {};
    HBITMAP default_bitmap = 0;
    HBRUSH default_brush = 0;
    HFONT default_font = 0;
    unsigned default_pen = 0;
    GdiPath *path = nullptr;      // DC+78h
    unsigned path_flags = 0;      // DC+7Ah
    // A DC of a device driver GDI loaded, a printer: the GDIINFO its Enable
    // filled (DC+38h) and the escape the driver answers GetDeviceCaps 110 to
    // 115 through.
    unsigned char *device_info = nullptr;
    unsigned device_info_bytes = 0;
    void *device_context = nullptr;
    int (*device_escape)(void *, unsigned, unsigned (&)[2]) = nullptr;
    const DeviceDraw *device_draw = nullptr;
};
void ReleasePath(Context &); // src/gdi_paths.inc
struct DibRecord;
void DestroyDib(DibRecord *);
struct Object {
    DibRecord *dib = nullptr;
    Kind kind = Empty;
    unsigned generation = 1, references = 0;
    HDC owner = 0;
    DibEng::Bitmap bitmap = {};
    DibEng::Brush brush = {};
    DibText::Font font;
    Win98PenLine::Pen pen{};
    Win98GdiPalette::Palette palette{};
    unsigned logical_brush_color=0;
    unsigned char *allocation = nullptr;
    unsigned pattern[64] = {};
    Context *context = nullptr;
    bool private_default = false;
    bool protected_stock_brush = false;
    // Original GDI object header: the owner WORD at object+0xc and the private
    // bit 0x2000 of the signature WORD at object+2. See win98_gdi_owner.h.
    unsigned task_owner = 0;
    bool object_private = false;
    bool hollow_brush = false;
    unsigned original_bitmap = 0;
    bool display_compatible = false;
    // The host copy of a segmented DDB matches guest memory; see
    // BitmapStore::Sync.
    bool host_copy_current = false;
    unsigned original_brush = 0;
    unsigned original_region = 0;
    bool monochrome_pattern = false, pattern_ready = false;
    // The hatch index of a BS_HATCHED brush, Win98GdiHatch::Styles for any
    // other brush, and the WORD dimension pair of a bitmap object.
    unsigned hatch_style = Win98GdiHatch::Styles;
    // A DIB pattern brush (src/gdi_dib_brush.inc): the copy of the DIB lives
    // in `allocation`, the top-left 8 by 8 pixels in `pattern` (RGB usage) or
    // as palette indices in `dib_index`.
    bool dib_brush = false, dib_realizable = false, dib_two_colors = false;
    unsigned dib_usage = 0, dib_bytes = 0;
    unsigned char dib_index[64] = {};
    // A DIB over 8 by 8 pixels: its size, and the whole DIB as realized for
    // the last surface.
    int dib_width = 0, dib_height = 0;
    mutable unsigned *dib_tile = nullptr;
    int dimension_x = 0, dimension_y = 0;
};
struct ObjectPage {Object entries[64];};
// The pages live as long as the process. Freed by a destructor at exit,
// they took the last pointers to the contexts and palettes in them, which
// a leak checker then reports.
struct ObjectStore {
    ObjectPage *pages[(MaxIndexMask+63)/64]{};
    Object &operator[](unsigned i){return pages[i/64]->entries[i%64];}
    bool Grow(){
        if(Capacity==IndexMask)return false;
        auto *page=new(std::nothrow) ObjectPage;
        if(!page)return false;
        pages[Capacity/64]=page;
        Capacity=Capacity+64>IndexMask?IndexMask:Capacity+64;return true;
    }
} objects;
// A 16-bit gate returns handles in AX. While such a gate runs, Allocate
// must assign handles that fit in one word.
unsigned narrow_handles = 0;
unsigned NarrowGenerations = 1;
// A Win16 caller retains the low WORD. Its generation bit and index must
// name the same object even when a Win32 caller holds the full generation.
bool Names(const Object &o, unsigned handle)
{
    const unsigned generation = handle >> IndexBits;
    if (o.generation == generation) return true;
    return handle <= 0xffff && generation && (o.generation & NarrowGenerations) == generation;
}
Object *Find(unsigned handle, Kind kind)
{
    const unsigned index = handle & IndexMask;
    if (!index || index > Capacity) return nullptr;
    Object &o = objects[index - 1];
    return o.kind == kind && Names(o, handle) ? &o : nullptr;
}
Object *FindAny(unsigned handle)
{
    const unsigned index = handle & IndexMask;
    if (!index || index > Capacity) return nullptr;
    Object &o = objects[index - 1];
    return o.kind != Empty && Names(o, handle) ? &o : nullptr;
}
// The original signature WORD of a native object, including the stock bit for
// the DC default objects and the private bit set through MakeObjectPrivate.
unsigned FullSignature(const Object &o)
{
    unsigned signature = 0;
    switch (o.kind) {
        case PenObject: signature = 0x4f47; break;
        case BrushObject: signature = 0x4f48; break;
        case FontObject: signature = 0x4f49; break;
        case PaletteObject: signature = 0x4f4a; break;
        case BitmapObject: signature = 0x4f4b; break;
        case RegionObject: signature = 0x4f4c; break;
        case DCObject: signature = o.context ? o.context->signature : 0; break;
        default: return 0;
    }
    if (o.object_private) signature |= Win98GdiOwner::PrivateFlag;
    if (o.private_default) signature |= Win98GdiOwner::StockFlag;
    return signature;
}
unsigned Allocate(Kind kind)
{
    for(;;){
        for(unsigned i=free_object_hint;i<Capacity;++i)if(objects[i].kind==Empty){
            unsigned generation=objects[i].generation;
            if(narrow_handles&&generation>NarrowGenerations)generation=1;
            objects[i]=Object{};objects[i].kind=kind;objects[i].generation=generation;
            free_object_hint=i+1;
            return (generation<<IndexBits)|(i+1);
        }
        if(!objects.Grow())return 0;
    }
}

void Free(unsigned handle);
#include "gdi_bitmap.inc"
#include "gdi_region_store.inc"
void Free(unsigned handle)
{
    Object &o = objects[(handle & IndexMask) - 1];
    if (o.original_bitmap) { bitmap_store.Destroy(o.original_bitmap); return; }
    if (o.original_brush) { bitmap_store.DestroyBrush(o.original_brush); return; }
    if(o.original_region){RegionSerial serial;region_store.Destroy(o.original_region);}
    DestroyDib(o.dib);
    delete[] o.palette.entries;
    delete[] o.allocation; delete[] o.dib_tile; delete o.context;
    unsigned next = o.generation == ((1u << (32-IndexBits))-1) ? 1 : o.generation + 1;
    // Keep the WORD generation bit nonzero (see Names).
    if (!(next & NarrowGenerations)) ++next;
    // Metafile handles use E... and may now have a nonzero 15-bit index.
    // Keep that tag out of native generations (src/x86_gdi_metafile.cpp).
    if(((next<<IndexBits)&0xf0000000u)==0xe0000000u)next=(0xf0000000u>>IndexBits)|1;
    o = Object{}; o.generation = next;
    const unsigned slot=(handle&IndexMask)-1;if(slot<free_object_hint)free_object_hint=slot;
}
Context *DC(HDC dc) { Object *o = Find(dc, DCObject); return o ? o->context : nullptr; }
void Retain(unsigned handle, Kind kind, HDC owner)
{
    Object *o = Find(handle, kind);
    if(kind==PenObject||kind==PaletteObject)o->references=Win98PenLine::AddReference(o->references*4)/4;else ++o->references;
    if (o->original_bitmap) bitmap_store.References(o->original_bitmap, o->references);
    if (kind == BitmapObject) o->owner = owner;
}
void Release(unsigned handle, Kind kind)
{
    Object *o = Find(handle, kind);
    if(kind==PenObject||kind==PaletteObject)o->references=Win98PenLine::RemoveReference(o->references*4)/4;else --o->references;
    if (!o->references) o->owner = 0;
    if (o->original_bitmap) bitmap_store.References(o->original_bitmap, o->references);
}
void RetainState(const State &s, HDC dc) { RetainClip(s); Retain(s.bitmap, BitmapObject, dc); Retain(s.brush, BrushObject, dc); Retain(s.font, FontObject, dc); Retain(s.pen, PenObject, dc); Retain(s.palette,PaletteObject,dc); if(s.color_space)Retain(s.color_space,ColorSpaceObject,dc); }
void ReleaseState(const State &s) { ReleaseClip(s); Release(s.bitmap, BitmapObject); Release(s.brush, BrushObject); Release(s.font, FontObject); Release(s.pen, PenObject); Release(s.palette,PaletteObject); if(s.color_space)Release(s.color_space,ColorSpaceObject); }
int display_width=800, display_height=600, display_depth=32;
unsigned system_palette_use=1;   // SYSPAL_STATIC
// The display system palette uses DIB order, 0x00rrggbb. It belongs to the
// device: all windows realize their palettes into this shared table, as on
// Windows 98.
unsigned display_colors[256]={};
// GDI tracks the colors, slot flags, and foreground palette
// (src/win98_gdi_realize.h).
Win98GdiRealize::System system_palette;
void *display_palette_context=nullptr;
void (*display_palette_sink)(void *,unsigned,unsigned,const unsigned *)=nullptr;
// Displays at eight bits per pixel or below have a color table. All
// non-printer DCs, including memory DCs, use this device.
bool PaletteDisplay() { return display_depth <= 8; }
unsigned Pixel(COLORREF color) { return ((color & 255) << 16) | (color & 0xff00) | ((color >> 16) & 255); }
bool RealizeDeviceColor(const Object &, COLORREF, unsigned &, const State* = nullptr);
// A surface of the display device (the screen or a bitmap in its format),
// not a DIB section; defined with the DIB records.
bool DeviceSurface(const Object &);
#include "gdi_palette_store.inc"
#include "gdi_dib_brush.inc"
bool RealizeBrush(const Object &brush, const State &state, unsigned (&pixels)[64], DibEng::Brush &out)
{
    out = brush.brush;
    if (brush.hollow_brush) return false;
    if (brush.dib_brush)
    {
        const auto *surface = Find(state.bitmap, BitmapObject);
        if (!surface || !RealizeDibBrush(brush, state, *surface, pixels)) return false;
        out.pattern = pixels;
    }
    if (brush.original_brush && !brush.pattern_ready) return false;
    if (brush.monochrome_pattern)
    {
        for (unsigned i = 0; i < 64; ++i)
            pixels[i] = Win98PatternBrush::MonoColor(brush.pattern[i], state.text_mode.foreground, state.text_mode.background);
        out.pattern = pixels;
    }
    // DIBENG realizes a hatch brush like a monochrome pattern brush: the
    // eight mask bytes of the index, rotated to the brush origin and expanded
    // to one byte per pixel. A set bit is a hatch pixel and takes the brush's
    // own colour, a clear bit the DC background colour.
    if (brush.hatch_style < Win98GdiHatch::Styles)
    {
        for (unsigned y = 0; y < 8; ++y)
            for (unsigned x = 0; x < 8; ++x)
                pixels[y * 8 + x] = Pixel(Win98GdiHatch::Pixel(brush.hatch_style, x, y)
                    ? brush.logical_brush_color : state.text_mode.background);
        out.pattern = pixels;
    }
    const auto *bitmap=Find(state.bitmap,BitmapObject);
    if(!bitmap)return false;
    // Windows 98 sets bit 20h of DC+0Fh for a DIB brush over 8 by 8 pixels; the
    // blit then fills, which tiles the whole DIB.
    if(brush.dib_brush && brush.dib_realizable && (brush.dib_width > 8 || brush.dib_height > 8)) {
        if(!RealizeDibTile(brush, state))return false;
        const unsigned count=unsigned(brush.dib_width)*unsigned(brush.dib_height);
        for(unsigned i=0;i<count;++i)
            if(!RealizeDeviceColor(*bitmap,brush.dib_tile[i],brush.dib_tile[i]))return false;
        out.tile=brush.dib_tile;out.tile_width=brush.dib_width;out.tile_height=brush.dib_height;
        out.tile_x=int(state.brush_origin.x)+state.mapping.device_origin.x-state.surface_origin.x;
        out.tile_y=int(state.brush_origin.y)+state.mapping.device_origin.y-state.surface_origin.y;
    }
    if(out.pattern) {
        for(unsigned i=0;i<64;++i)
            if(!RealizeDeviceColor(*bitmap,Pixel(out.pattern[i]),pixels[i]))return false;
        // The driver samples its tile in surface coordinates. Align the tile
        // with the DC's device origin and the caller's brush origin first.
        unsigned shifted[64];
        const unsigned ox = unsigned(state.brush_origin.x) + unsigned(state.mapping.device_origin.x) - unsigned(state.surface_origin.x);
        const unsigned oy = unsigned(state.brush_origin.y) + unsigned(state.mapping.device_origin.y) - unsigned(state.surface_origin.y);
        for (unsigned y = 0; y < 8; ++y)
            for (unsigned x = 0; x < 8; ++x)
                shifted[y * 8 + x] = pixels[((y - oy) & 7) * 8 + ((x - ox) & 7)];
        memcpy(pixels, shifted, sizeof shifted);
        out.pattern=pixels;
    } else if(PaletteDisplay()&&bitmap->bitmap.depth==8&&DeviceSurface(*bitmap)){
        // DIBENG on the display with eight bits: a solid colour
        // that is none of the twenty static ones is rastered into an 8x8
        // pattern of static colours (Win98DibDither), turned to the brush
        // origin like any pattern.
        unsigned color=0,slot=0;
        if(!ResolveLogicalColor(state,brush.logical_brush_color,color))return false;
        if(color&0xff000000)return RealizeDeviceColor(*bitmap,color,out.color,nullptr);
        if(Win98DibDither::StaticSlot(color,slot)){out.color=slot;return true;}
        unsigned char tile[64];
        Win98DibDither::Pattern(color,tile);
        const unsigned ox = unsigned(state.brush_origin.x) + unsigned(state.mapping.device_origin.x) - unsigned(state.surface_origin.x);
        const unsigned oy = unsigned(state.brush_origin.y) + unsigned(state.mapping.device_origin.y) - unsigned(state.surface_origin.y);
        for (unsigned y = 0; y < 8; ++y)
            for (unsigned x = 0; x < 8; ++x)
                pixels[y * 8 + x] = tile[((y - oy) & 7) * 8 + ((x - ox) & 7)];
        out.pattern=pixels;
    } else if(!RealizeDeviceColor(*bitmap,brush.logical_brush_color,out.color,&state))return false;
    return true;
}
bool Intersect(RECT &out, const RECT &a, const RECT &b) { return Win98::IntersectRect(&out, &a, &b) != 0; }
POINT DevicePoint(const State &s,int x,int y)
{
    const auto p=Win98DcMap::Transform(s.mapping,{x,y},false,false);
    return {p.x-s.surface_origin.x,p.y-s.surface_origin.y};
}
// The inverse of DevicePoint: GDI converts device units back to logical
// ones, world transform first.
POINT LogicalPoint(const State &s,int x,int y)
{
    const auto p=Win98DcMap::Transform(s.mapping,{x+s.surface_origin.x,y+s.surface_origin.y},true,false);
    return {p.x,p.y};
}
bool Rectangle(const State &s, int x, int y, int w, int h, RECT &r)
{
    if (w < 0 || h < 0 || w > 32767 || h > 32767) return false;
    const auto a=DevicePoint(s,x,y);
    const auto b=DevicePoint(s,int(unsigned(x)+unsigned(w)),int(unsigned(y)+unsigned(h)));
    r={a.x<b.x?a.x:b.x,a.y<b.y?a.y:b.y,a.x>b.x?a.x:b.x,a.y>b.y?a.y:b.y};
    return true;
}
bool Paint(Context &dc, const RECT &r, unsigned rop, const DibEng::Brush &brush)
{
    BitmapAccess access(Find(dc.current.bitmap, BitmapObject), true);
    auto &bitmap = Find(dc.current.bitmap, BitmapObject)->bitmap;
    const RECT bounds = {0, 0, bitmap.width, bitmap.height};
    RECT bounded;
    if (!Intersect(bounded, r, bounds)) return true;
    for (unsigned i = 0; i < dc.current.clips; ++i) {
        RECT clip;
        if (!Intersect(clip, bounded, ClipAt(dc.current,i))) continue;
        if (!DibEng::BitBlt(bitmap, clip.left, clip.top, clip.right - clip.left,
                            clip.bottom - clip.top, nullptr, 0, 0, rop, brush)) return false;
    }
    return true;
}
}
#include "gdi_map.inc"
#include "gdi_region_api.inc"
void AttachDisplayColors(HBITMAP);
#include "gdi_dib_api.inc"
#include "gdi_device_color.inc"
#include "gdi_pen_line.inc"
#include "gdi_shapes.inc"
#include "gdi_arcs.inc"
// The display side of CreateCompatibleBitmap (below).
bool DisplayLikeDC(HDC);
void AttachDisplayColors(HBITMAP);
#include "win98_dib_stretch_native.h"

unsigned BitmapApi(ApiName name, const unsigned *a, BitmapIO &io)
{
    struct Thunk
    {
        BitmapStore &store;
        BitmapIO &io;
        unsigned Read16(unsigned p) { unsigned short v = 0; if (!io.Read(p, &v, 2)) store.Fault("Invalid BITMAP32 read"); return v; }
        unsigned Read32(unsigned p) { unsigned v = 0; if (!io.Read(p, &v, 4)) store.Fault("Invalid BITMAP32 read"); return v; }
        void Error(unsigned e) { io.Error(e); }
        bool MapBits(unsigned p, unsigned count, unsigned &mapped, unsigned &cookie)
        {
            mapped = cookie = 0;
            if (!p || !count) return true;
            if (count > 0xffff0000 || (unsigned long long)p + count > 0x100000000ull) return false;
            cookie = store.Map(nullptr, count, &io, p);
            if (cookie) mapped = store.ranges[cookie - 1].base;
            return cookie != 0;
        }
        void UnmapBits(unsigned cookie) { store.Unmap(cookie); }
        unsigned CreateBitmap16(const Win98BitmapThunk::Bitmap16 &b)
        {
            if (store.failed) return 0;
            return Win98Bitmap::Create(store, store.state,
                {b.type, b.width, b.height, b.stride, b.planes, b.bpp, b.bits});
        }
        unsigned BitmapType(unsigned handle)
        { return store.Record(handle) ? store.Read16(store.Object(handle) + 2) & 0x5fff : 0; }
        unsigned GetBitmap16(unsigned handle, unsigned char (&out)[84])
        { return BitmapType(handle) == 0x4f4b ? Win98BitmapQuery::ReadDdb(store, handle, out) : 0; }
        void WriteGuest8(unsigned p, unsigned v)
        { const unsigned char b = v; if (!io.Write(p, &b, 1)) store.Fault("GDI bitmap query output access failed"); }
        unsigned MapPointer(unsigned)
        { store.Fault("Unexpected selector pointer in software bitmap query"); return 0; }
        bool Probe(unsigned bits, bool write)
        {
            if (!bits) return false;
            auto *r = store.Address(bits, 1, store.read_cache);
            return r && (!r->io || r->io->Probe(r->guest + bits - r->base, write));
        }
        bool ProbeReadByte(unsigned bits) { return Probe(bits, false); }
        bool ProbeWriteByte(unsigned bits) { return Probe(bits, true); }
        void Reject(unsigned, unsigned) {} // Original recovery returns zero; outer thunk supplies error 87.
        unsigned Transfer(unsigned handle, unsigned count, unsigned bits, bool write)
        {
            const unsigned result = write ? Win98Bitmap::SetBits(store, handle, count, bits)
                                          : Win98Bitmap::GetBits(store, handle, count, bits);
            // The guest wrote the pixels. Discard the host copy and read them
            // again.
            if (write) if (auto *o = Find(store.Native(handle), BitmapObject)) store.Refresh(*o);
            return result;
        }
        unsigned GetBitmapBits(unsigned h, unsigned n, unsigned p) { return Transfer(h, n, p, false); }
        unsigned SetBitmapBits(unsigned h, unsigned n, unsigned p) { return Transfer(h, n, p, true); }
        unsigned GetBitmapBits16(unsigned h, unsigned n, unsigned p) { return Win98BitmapEntry::GetBits(*this,h,n,p); }
        unsigned SetBitmapBits16(unsigned h, unsigned n, unsigned p) { return Win98BitmapEntry::SetBits(*this,h,n,p); }
    } thunk{bitmap_store, io};
    bitmap_store.io = &io;
    bitmap_store.failed = false;
    bool globals_before[BitmapStore::GlobalCount], locals_before[BitmapStore::LocalCapacity];
    for (unsigned i = 0; i < BitmapStore::GlobalCount; ++i) globals_before[i] = bitmap_store.globals[i].range;
    for (unsigned i = 0; i < BitmapStore::LocalCapacity; ++i) locals_before[i] = bitmap_store.locals[i].native;
    const bool create = API_IS(name,"CreateBitmap") || API_IS(name,"CreateBitmapIndirect");
    unsigned result = 0;
    if (create)
    {
        if (bitmap_store.Initialize())
        {
            const unsigned local = API_IS(name,"CreateBitmap")
                ? Win98BitmapThunk::Create(thunk, a[0], a[1], a[2], a[3], a[4])
                : Win98BitmapThunk::Indirect(thunk, a[0]);
            if (local && bitmap_store.Attach(local)) result = bitmap_store.Native(local);
            else if (local) { bitmap_store.Destroy(local); io.Error(8); }
        }
        else io.Error(8);
    }
    else
    {
        auto *o = Find(a[0], BitmapObject);
        const unsigned local = o ? o->original_bitmap : 0;
        if (API_IS(name,"GetObjectA")) result = Win98BitmapQuery::Get(thunk, local, a[1], a[2]);
        else result = API_IS(name,"SetBitmapBits") ? Win98BitmapThunk::SetBits(thunk, local, a[1], a[2])
                                                    : Win98BitmapThunk::GetBits(thunk, local, a[1], a[2]);
    }
    if (bitmap_store.failed)
    {
        // Native fault boundary: discard allocations made by the interrupted call.
        for (unsigned i = 0; i < BitmapStore::LocalCapacity; ++i)
            if (!locals_before[i] && bitmap_store.locals[i].native)
            {
                const unsigned handle = 0x4000 + i * 4;
                if (bitmap_store.state.empty_bitmap == handle) bitmap_store.state.empty_bitmap = 0;
                bitmap_store.Destroy(handle);
            }
        for (unsigned i = 0; i < BitmapStore::GlobalCount; ++i)
            if (!globals_before[i]) bitmap_store.GlobalFree(i + 1);
        result = 0;
    }
    bitmap_store.io = nullptr;
    return result;
}

bool BitmapDimensions(HBITMAP handle, int &width, int &height)
{
    const auto *object = Find(handle, BitmapObject);
    if (!object || !DibEng::Valid(object->bitmap)) return false;
    width = object->bitmap.width;
    height = object->bitmap.height;
    return true;
}

bool BitmapInfo(HBITMAP handle, unsigned (&info)[6])
{
    auto *o = Find(handle, BitmapObject);
    if (!o || !o->original_bitmap) return false;
    struct Query
    {
        unsigned char *output;
        unsigned GetBitmap16(unsigned h, unsigned char (&out)[84]) { return Win98BitmapQuery::ReadDdb(bitmap_store,h,out); }
        void WriteGuest8(unsigned p, unsigned v) { output[p-1] = v; }
        unsigned MapPointer(unsigned) { return 0; } // ReadDdb always returns null bits.
        void Error(unsigned) {}
    } query{reinterpret_cast<unsigned char *>(info)};
    return Win98BitmapQuery::Get(query, o->original_bitmap, 24, 1) == 24;
}
bool HasOriginalBitmap(HGDIOBJ handle)
{ auto *o = Find(handle, BitmapObject); return o && o->original_bitmap; }
// GDI.462 ISGDIOBJECT over the native store. Zero for a stale or unknown
// handle and for every signature outside 0x4f47..0x4f53.
unsigned IsGdiObject(HGDIOBJ handle)
{
    const Object *o = FindAny(handle);
    return o ? Win98GdiOwner::Classify(handle, FullSignature(*o)) : 0;
}
// GDI32 ordinal 400 through GDI.461: the gate at GDI, then the WORD
// exchange. The previous owner is the result.
unsigned SetObjectOwner(HGDIOBJ handle, unsigned owner)
{
    Object *o = FindAny(handle);
    if (!o || !Win98GdiOwner::OwnerGate(FullSignature(*o))) return 0;
    return Win98GdiOwner::ExchangeOwner(o->task_owner, owner);
}
// The original has no getter; this reads the same field for the native
// process teardown and for the tests.
unsigned GetObjectOwner(HGDIOBJ handle)
{
    const Object *o = FindAny(handle);
    return o ? o->task_owner : 0;
}
// GDI.463 MAKEOBJECTPRIVATE. Returns the previous flag as its bit
// value, not as a boolean. There is no GDI32 export for it.
unsigned MakeObjectPrivate(HGDIOBJ handle, bool make_private)
{
    Object *o = FindAny(handle);
    if (!o) return 0;
    unsigned signature = FullSignature(*o);
    const unsigned previous = Win98GdiOwner::ExchangePrivate(signature, make_private);
    o->object_private = make_private;
    return previous;
}
// GDI, the body of GDI32!ByeByeGDI: first the DCs whose owner word is
// `owner`, then every other object with that owner in the local heap. The signature test masks the private and stock
// bits, so both kinds go. Answers the number of objects deleted.
bool DeleteDC(HDC handle);
bool SysDeleteObject(HGDIOBJ handle);
unsigned DeleteObjectsOfOwner(unsigned owner)
{
    owner &= 0xffff;
    if (!owner) return 0;
    unsigned deleted = 0;
    for (unsigned pass = 0; pass < 2; ++pass)
        for (unsigned i = 0; i < Capacity; ++i) {
            Object &o = objects[i];
            if (o.kind == Empty || (o.task_owner & 0xffff) != owner) continue;
            if ((o.kind == DCObject) != (pass == 0)) continue;
            const unsigned handle = (o.generation << IndexBits) | (i + 1);
            if (pass == 0 ? DeleteDC(handle) : SysDeleteObject(handle)) ++deleted;
        }
    return deleted;
}
// SYSDELETEOBJECT, mask 0x80: the private bit does not stop
// this path, and Windows 98 clears it before the object is destroyed.
bool SysDeleteObject(HGDIOBJ handle)
{
    Object *o = FindAny(handle);
    if (!o) return false;
    o->object_private = false;
    return DeleteObject(handle);
}
namespace {
// The effective clip rectangles are the intersection of the application clip
// region and the window-visible region the USER side installs, so they are the
// native stand-in for the original DC region field at DC+0x24.
struct VisibleBands {
    const State &s;
    unsigned Bands() const { return s.clips; }
    Win98GdiVisible::Rect Band(unsigned i) const
    {
        const RECT &r = ClipAt(s, i);
        return {r.left, r.top, r.right, r.bottom};
    }
};
} // namespace
// GDI32 ordinal 357 through GDI.104. The expected Windows version is 0x400 for
// every caller NEO98 loads, so the exclusive edge rule applies.
bool RectVisible(HDC handle, const RECT *rect)
{
    const auto *c = DC(handle);
    if (!c || !rect) return false;
    if (!Win98GdiVisible::DcGate(c->signature)) return false;
    Win98GdiVisible::Rect logical{rect->left, rect->top, rect->right, rect->bottom};
    Win98GdiVisible::Normalize(logical);
    const auto a = DevicePoint(c->current, logical.left, logical.top);
    const auto b = DevicePoint(c->current, logical.right, logical.bottom);
    VisibleBands bands{c->current};
    return Win98GdiVisible::Visible(bands,
                                    Win98GdiVisible::Prepare({a.x, a.y, b.x, b.y}, 0x400));
}
// GDI32 ordinal 354 through GDI.103, the same gate and the same region.
bool PtVisible(HDC handle, int x, int y)
{
    const auto *c = DC(handle);
    if (!c) return false;
    if (!Win98GdiVisible::DcGate(c->signature)) return false;
    const auto p = DevicePoint(c->current, x, y);
    VisibleBands bands{c->current};
    return Win98GdiVisible::Point(bands, p.x, p.y);
}
// GDI32 ordinal 241 through GDI.77. Native USER keeps the effective
// clip current, so the visible-region realization never has to run,
// and NEO98 has no metafile recording object at DC+106h.
unsigned GetClipBox(HDC handle, RECT *rect)
{
    const auto *c = DC(handle);
    if (!c) return Win98GdiClipBox::Error;
    struct Environment {
        const Context &dc;
        unsigned type = 0;
        bool VisibleInvalid() { return false; }
        void RealizeVisible() {}
        unsigned MetafileObject() { return 0; }
        unsigned MetaRegion() { return 0; }
        unsigned ApplicationClip() { return 0; }
        int DeviceWidth() { return 0; }
        int DeviceHeight() { return 0; }
        unsigned CreateEmptyRegion() { return 0; }
        void CombineRegions(unsigned, unsigned, unsigned) {}
        void DeleteRegion(unsigned) {}
        unsigned RealizedClip() { return 1; }
        // The effective clip list is the native stand-in for DC+24h. Its
        // bounding box is the region record's rectangle at +16h, and its
        // classification follows : no covered rectangle is NULLREGION,
        // one filled rectangle is SIMPLEREGION, anything else COMPLEXREGION.
        void RegionBounds(unsigned, Win98GdiClipBox::Rect &out)
        {
            RECT box{};
            type = EffectiveClipBox(dc.current, box);
            out = {box.left, box.top, box.right, box.bottom};
        }
        unsigned RegionBytes(unsigned) { return type == 3 ? 43u : 42u; }
        unsigned RegionBands(unsigned) { return type ? type - 1 : 0; }
        // the inverse of the mapping DevicePoint applies.
        void DeviceToLogical(Win98GdiClipBox::Rect &r)
        {
            const auto a = LogicalPoint(dc.current, r.left, r.top);
            const auto b = LogicalPoint(dc.current, r.right, r.bottom);
            r = {a.x, a.y, b.x, b.y};
        }
        void ParameterError(unsigned) {}
    } environment{*c};
    Win98GdiClipBox::Rect box{};
    const unsigned result = Win98GdiClipBox::Body(environment, c->signature, rect != nullptr, box);
    // widens the four WORDs whenever the caller supplied a rectangle,
    // even after a failed 16-bit call. The native adapter writes the rectangle
    // only for a result the 16-bit side produced, because NEO98 has no thunk
    // frame to leak.
    if (rect && result) *rect = {box.left, box.top, box.right, box.bottom};
    return result;
}
unsigned GetObjectType(HGDIOBJ handle)
{
    const unsigned index=handle&IndexMask;
    if(!index||index>Capacity)return 0;
    const auto&o=objects[index-1];
    if(o.kind==Empty||!Names(o,handle))return 0;
    unsigned signature=0;
    switch(o.kind){
        case PenObject:signature=0x4f47;break;
        case BrushObject:signature=0x4f48;break;
        case FontObject:signature=0x4f49;break;
        case PaletteObject:signature=0x4f4a;break;
        case BitmapObject:signature=0x4f4b;break;
        case RegionObject:signature=0x4f4c;break;
        case DCObject:signature=o.context->signature;break;
        default:return 0;
    }
    bool invalid=false;
    return Win98GdiObjectType::Type(signature,0,o.context&&o.context->memory_dc?1:0,invalid);
}
unsigned ColorMode(HDC handle, unsigned mode)
{
    auto *c = DC(handle);
    if (!c || mode < 1 || mode > 4) return 0;
    if (mode != 3) c->current.icm_mode = mode == 2 ? 2 : 1;
    return c->current.icm_mode;
}
unsigned CreateColorSpace(const unsigned char *logical)
{
    unsigned header[3]; memcpy(header, logical, sizeof header);
    if (header[0] != 0x50534f43 || header[1] != 0x400 || header[2] != 328) return 0;
    const unsigned h = Allocate(ColorSpaceObject);
    if (!h) return 0;
    auto *o = Find(h, ColorSpaceObject);
    o->allocation = new unsigned char[328];
    if (!o->allocation) { Free(h); return 0; }
    memcpy(o->allocation, logical, 328);
    o->allocation[327] = 0;
    return h;
}
bool DeleteColorSpace(unsigned h)
{
    auto *o = Find(h, ColorSpaceObject);
    if (!o || o->references || o->private_default) return false;
    Free(h); return true;
}
bool GetLogColorSpace(unsigned h, unsigned char *logical)
{
    const auto *o = Find(h, ColorSpaceObject);
    if (!o) return false;
    memcpy(logical, o->allocation, 328); return true;
}
unsigned GetColorSpace(HDC handle, const char *profile)
{
    auto *c = DC(handle); if (!c) return 0;
    if (c->current.color_space) return c->current.color_space;
    static unsigned standard = 0;
    if (!Find(standard, ColorSpaceObject))
    {
        // The installed Win98 sRGB profile's default LOGCOLORSPACEA.
        const unsigned fields[17] = {0x50534f43,0x400,328,0,4,
            0x6b91,0x3db8,0x70e,0x5793,0xa9ba,0x1bf4,0x38aa,0x188e,0x12888,
            0x1cccd,0x1cccd,0x1cccd};
        unsigned char logical[328] = {}; memcpy(logical, fields, sizeof fields);
        if (profile) for (unsigned i=0;i<259 && profile[i];++i) logical[68+i]=profile[i];
        standard = CreateColorSpace(logical);
        if (!standard) return 0;
        Find(standard, ColorSpaceObject)->private_default = true;
    }
    c->current.color_space = standard; Retain(standard, ColorSpaceObject, handle);
    return standard;
}
unsigned SetColorSpace(HDC handle, unsigned space, const char *profile)
{
    auto *c = DC(handle); if (!c || !Find(space, ColorSpaceObject)) return 0;
    const unsigned old = GetColorSpace(handle, profile);
    if (!old) return 0;
    Retain(space, ColorSpaceObject, handle); Release(old, ColorSpaceObject);
    c->current.color_space = space; return old;
}
const char *ColorProfile(HDC handle)
{
    const auto *c = DC(handle); return c ? c->current.color_profile : nullptr;
}
bool SetColorProfile(HDC handle, const char *profile)
{
    auto *c = DC(handle); if (!c || !profile || strlen(profile) >= 260) return false;
    strcpy(c->current.color_profile, profile); return true;
}
namespace {
HGDIOBJ PublicBitmap(HGDIOBJ handle)
{
    const auto *object = Find(handle, BitmapObject);
    if (!object || !object->private_default) return handle;
    return bitmap_store.Initialize() ? bitmap_store.Native(bitmap_store.state.empty_bitmap) : 0;
}
}
HGDIOBJ GetCurrentObject(HDC handle,unsigned type)
{
    const auto*c=DC(handle);if(!c)return 0;
    struct View {
        const State&s;
        unsigned Selected(unsigned offset){switch(offset){
            case 0x18:return s.pen;case 0x1a:return s.brush;case 0x1c:return s.font;
            case 0x1e:return PublicBitmap(s.bitmap);case 0x20:return s.palette;default:return 0;
        }}
    }view{c->current};
    return Win98GdiObjectType::Current(view,c->signature,type);
}
// The full handle of the object a handle or its low word names, zero when it
// names none. A handle kept as a word (the clipboard entry of USER, a 16-bit
// program) goes back to a 32-bit caller as the handle GDI gave out.
HGDIOBJ FullHandle(HGDIOBJ handle)
{
    const Object *o = FindAny(handle);
    return o ? (o->generation << IndexBits) | (handle & IndexMask) : 0;
}
bool IsPrivateDefault(HGDIOBJ handle)
{
    const auto *object = FindAny(handle);
    return object && object->private_default;
}

bool ObjectExists(HGDIOBJ handle)
{ return Find(handle, BitmapObject) || Find(handle, BrushObject) || Find(handle, FontObject) || Find(handle,RegionObject) || Find(handle,PenObject) || Find(handle,PaletteObject); }
namespace {
void ReleaseDetachedDib(void *, const DibLease &lease) { free(lease.host); }
}
// The bits of a DIB section are memory of the process that created it: host
// bytes mapped into it, or its own guest pages where the engine hands them
// out (Application::AllocateDibLease). Win98 keeps them in the shared arena,
// where they survive the process as long as the object does. Here the object
// gets a copy while the process can still give its memory back; every
// bitmap that views the old bytes follows.
void DetachDibLeases(void *context)
{
    for (unsigned i = 0; i < Capacity; ++i)
    {
        Object &o = objects[i];
        if (o.kind == Empty || !o.dib || !o.dib->lease.release || o.dib->lease.context != context)
            continue;
        const DibLease old = o.dib->lease;
        auto *kept = static_cast<unsigned char *>(malloc(old.bytes ? old.bytes : 1));
        if (!kept)
            continue;
        memcpy(kept, old.host, old.bytes);
        for (unsigned j = 0; j < Capacity; ++j)
        {
            auto &bits = objects[j].bitmap.bits;
            if (objects[j].kind != Empty && bits >= old.host && bits < old.host + old.bytes)
                bits = kept + (bits - old.host);
        }
        o.dib->lease.host = kept;
        o.dib->lease.context = nullptr;
        o.dib->lease.release = ReleaseDetachedDib;
        old.release(old.context, old);
    }
}
void ShutdownBitmaps() { region_store.Shutdown(); bitmap_store.Shutdown(); if(default_palette){auto*p=Find(default_palette,PaletteObject);if(p&&!p->references){Free(default_palette);default_palette=0;}} }

namespace {
// A surface format without a lease: the bits belong to the caller. GDI
// keeps the colour table for indexed surfaces or the masks for RGB565.
bool AttachSurfaceColors(Object &b,const DibEng::Bitmap &bitmap,const unsigned *colors,unsigned count,bool rgb565=false)
{
    DestroyDib(b.dib); b.dib=nullptr;
    if((!colors||!count)&&!rgb565)return true;
    if(!colors)count=0;
    if(count>256)count=256;
    auto *storage=calloc(1,sizeof(DibRecord));
    if(!storage)return false;
    auto *record=new (storage) DibRecord{};
    auto &f=record->format;
    f.width=unsigned(bitmap.width);f.height=unsigned(bitmap.height);
    f.stride=unsigned(bitmap.stride);f.depth=unsigned(bitmap.depth);
    f.bytes=f.stride*f.height;f.colors=count;f.palette=40;
    f.bottom_up=false;f.rgb565=rgb565;
    f.Write32(0x100,40);
    f.Write32(0x104,f.width);
    f.Write32(0x108,unsigned(-int(f.height)));
    f.Write16(0x10c,1);
    f.Write16(0x10e,f.depth);
    f.Write32(0x114,f.bytes);
    f.Write32(0x120,count);
    if(rgb565)
    {
        f.Write32(0x110,3); // BI_BITFIELDS, as on a DirectDraw 5-6-5 DIB.
        f.Write32(0x128,0xf800);f.Write32(0x12c,0x07e0);f.Write32(0x130,0x001f);
        f.palette=52;
    }
    for(unsigned i=0;i<count;++i)f.Write32(0x100+f.palette+i*4,colors[i]&0xffffff);
    // This is a device color table, not a DIB section. Display pixels and
    // device-format bitmap pixels are indices into the system palette.
    record->colours_only=true;
    b.dib=record;
    return true;
}
}
bool ResizeSurfaceDC(HDC handle,const DibEng::Bitmap &bitmap)
{
    auto *c=DC(handle);if(!c||c->memory_dc||!DibEng::Valid(bitmap))return false;
    auto *b=Find(c->default_bitmap,BitmapObject);if(!b||b->allocation)return false;
    // A cached DirectDraw DC keeps its 565 layout when another surface is
    // attached; its DIB descriptor must follow that surface's dimensions.
    if(b->dib&&b->dib->colours_only&&b->dib->format.rgb565&&
       !AttachSurfaceColors(*b,bitmap,nullptr,0,true))return false;
    b->bitmap=bitmap;return true;
}
bool ResizeSurfaceDC(HDC handle,const DibEng::Bitmap &bitmap,const unsigned *colors,unsigned count)
{
    auto *c=DC(handle);if(!c||c->memory_dc||!DibEng::Valid(bitmap))return false;
    auto *b=Find(c->default_bitmap,BitmapObject);if(!b||b->allocation)return false;
    if(!AttachSurfaceColors(*b,bitmap,colors,count))return false;
    b->bitmap=bitmap;return true;
}
bool SetSurfaceColors(HDC handle,unsigned start,unsigned count,const unsigned *colors)
{
    auto *c=DC(handle);if(!c||!colors)return false;
    auto *b=Find(c->default_bitmap,BitmapObject);
    if(!b||!b->dib||b->dib->format.depth>8)return false;
    auto &f=b->dib->format;
    if(start>=f.colors)return false;
    if(count>f.colors-start)count=f.colors-start;
    for(unsigned i=0;i<count;++i)f.Write32(0x100+f.palette+(start+i)*4,colors[i]&0xffffff);
    return true;
}
HDC CreateSurfaceDC(const DibEng::Bitmap &bitmap)
{
    if (!DibEng::Valid(bitmap)) return 0;
    HDC dc = CreateCompatibleDC();
    if (!dc) return 0;
    auto *c = DC(dc);c->memory_dc=false;
    auto *b = Find(c->default_bitmap, BitmapObject);
    delete[] b->allocation; b->allocation = nullptr;
    b->bitmap = bitmap;
    return dc;
}
void SetScreenGuard(const unsigned char *bits) { screen_guard = bits; }
void SetScreenGuardSink(void (*sink)(bool write)) { screen_guard_sink = sink; }
void SetSurfaceAccessSink(void (*sink)(const unsigned char *, bool)) { surface_access_sink = sink; }
void GuardScreenBits(const unsigned char *bits, bool write)
{
    if (surface_access_sink) surface_access_sink(bits, write);
    if (screen_guard && bits == screen_guard && screen_guard_sink) screen_guard_sink(write);
}
bool SameSurface(HDC a,HDC b)
{
    auto *ca=DC(a),*cb=DC(b);
    if(!ca||!cb)return false;
    auto *oa=Find(ca->current.bitmap,BitmapObject),*ob=Find(cb->current.bitmap,BitmapObject);
    return oa&&ob&&oa->bitmap.bits&&oa->bitmap.bits==ob->bitmap.bits;
}
unsigned RetargetSurface(const unsigned char *old_bits,const DibEng::Bitmap &bitmap,
                         const unsigned *colors,unsigned count)
{
    if(!old_bits||!DibEng::Valid(bitmap))return 0;
    unsigned moved=0;
    for(unsigned i=0;i<Capacity;++i)
    {
        Object &o=objects[i];
        if(o.kind!=BitmapObject||o.allocation||o.bitmap.bits!=old_bits)continue;
        if(!AttachSurfaceColors(o,bitmap,colors,count))continue;
        o.bitmap=bitmap;
        ++moved;
    }
    return moved;
}
HDC CreateSurfaceDC(const DibEng::Bitmap &bitmap,const unsigned *colors,unsigned count,bool rgb565)
{
    const HDC dc = CreateSurfaceDC(bitmap);
    if (!dc) return 0;
    auto *c = DC(dc);
    if (!AttachSurfaceColors(*Find(c->default_bitmap, BitmapObject), bitmap, colors, count, rgb565))
    { DeleteDC(dc); return 0; }
    return dc;
}

namespace {
// The font a new device context starts with (SetDefaultFont).
DibText::Font initial_font;
}
void SetDefaultFont(const unsigned char *data, unsigned length)
{
    DibText::Font font;
    initial_font = font.Load(data, length) ? font : DibText::Font{};
}
HDC CreateCompatibleDC(HDC compatible)
{
    if (compatible && !DC(compatible)) return 0;
    HDC handle = Allocate(DCObject);
    if (!handle) return 0;
    auto *c = new Context;
    Find(handle, DCObject)->context = c;
    struct Initial {
        unsigned char *p;
        void Write8(unsigned at,unsigned v){p[at]=v;}
        void Write16(unsigned at,unsigned v){Write8(at,v);Write8(at+1,v>>8);}
    } initial{c->original_initial};
    Win98DcInformation::InitialAttributes(initial);
    c->current.text_mode.rop2 = c->original_initial[0x4e];
    c->current.text_mode.background_mode = c->original_initial[0x50];
    c->current.mapping.mode = c->original_initial[0x94];
    c->current.mapping.window_extent = {c->original_initial[0x84],c->original_initial[0x86]};
    c->current.mapping.viewport_extent = {c->original_initial[0x8c],c->original_initial[0x8e]};
    c->current.stretch_mode = c->original_initial[0x72];
    c->current.palette=DefaultPalette();
    c->default_bitmap = Allocate(BitmapObject);
    c->default_brush = Allocate(BrushObject);
    c->default_font = initial_font.data ? CreateFontView(initial_font.data, initial_font.length)
                                        : CreateFontResource(0);
    c->default_pen = CreatePen(0,0,0);
    if (!c->current.palette || !c->default_bitmap || !c->default_brush || !c->default_font || !c->default_pen) {
        if (c->default_bitmap) Free(c->default_bitmap);
        if (c->default_brush) Free(c->default_brush);
        if (c->default_font) Free(c->default_font);
        if (c->default_pen) Free(c->default_pen);
        Free(handle); return 0;
    }
    auto *b = Find(c->default_bitmap, BitmapObject);
    b->allocation = new unsigned char[4]{};
    b->bitmap = {b->allocation, 1, 1, 4, 1}; b->private_default = true;
    auto *brush = Find(c->default_brush, BrushObject);
    brush->brush.color = 0xffffff; brush->logical_brush_color=0xffffff; brush->private_default = true;
    c->current.bitmap = c->default_bitmap; c->current.brush = c->default_brush;
    c->current.pen = c->default_pen; Find(c->default_pen,PenObject)->private_default = true;
    c->current.font = c->default_font; Find(c->default_font, FontObject)->private_default = true;
    c->current.clip[0] = {-32767, -32767, 32767, 32767};
    RetainState(c->current, handle);
    return handle;
}

bool ValidDC(HDC handle) { return DC(handle) != nullptr; }
bool InformationDC(HDC handle) { const auto *c=DC(handle);return c && !Win98DcInformation::DrawingAllowed(c->signature); }
HDC CreateDisplayDC(HDC screen,bool information)
{
    auto *source=DC(screen);if(!source)return 0;
    const auto *surface=Find(source->current.bitmap,BitmapObject);
    if(!surface || !DibEng::Valid(surface->bitmap))return 0;
    const HDC handle=CreateCompatibleDC(screen);if(!handle)return 0;
    auto *c=DC(handle);c->device_dc=true;c->memory_dc=false;c->signature=Win98DcInformation::Signature(information);
    auto *bitmap=Find(c->default_bitmap,BitmapObject);
    delete[] bitmap->allocation;bitmap->allocation=nullptr;
    // Information contexts have attributes and device capabilities, no drawing surface.
    bitmap->bitmap=information?DibEng::Bitmap{}:surface->bitmap;
    // The color table belongs to the surface. A display DC needs it to
    // resolve colors and blit DIBs on an eight-bit display, even though it
    // shares the same pixel storage.
    if(!information&&surface->dib&&surface->dib->format.depth<=8){
        const auto &f=surface->dib->format;
        unsigned colors[256]={};
        const unsigned count=f.colors>256?256u:f.colors;
        for(unsigned i=0;i<count;++i)colors[i]=f.Read32(0x100+f.palette+i*4);
        AttachSurfaceColors(*bitmap,bitmap->bitmap,colors,count);
    }
    return handle;
}
// Display dimensions and depth change with the mode. Applications query them
// through GetDeviceCaps; DirectDraw uses planes times bits per pixel to
// allocate its surfaces.
namespace {
// Display-format bitmaps use system-palette slots and the current
// system-palette table.
void RefreshDeviceTables()
{
    if(!PaletteDisplay())return;
    for(unsigned i=0;i<Capacity;++i){
        Object &o=objects[i];
        if(o.kind!=BitmapObject||!o.dib||!o.dib->colours_only||o.bitmap.depth>8)continue;
        auto &f=o.dib->format;
        for(unsigned c=0;c<f.colors&&c<256;++c)f.Write32(0x100+f.palette+c*4,display_colors[c]);
    }
}
// GDI calls the driver SETPALETTE entry after changing the system palette.
void ProgramDisplay(unsigned start,unsigned count)
{
    if(!PaletteDisplay())return;
    const unsigned size=1u<<unsigned(display_depth);
    if(start>=size)return;
    if(count>size-start)count=size-start;
    unsigned colors[256]={};
    for(unsigned i=0;i<count;++i){colors[i]=Pixel(system_palette.color[start+i]);display_colors[start+i]=colors[i];}
    if(display_palette_sink)display_palette_sink(display_palette_context,start,count,colors);
    RefreshDeviceTables();
}
// The default palette occupies the fixed-color slots.
void MapDefaultPalette()
{
    auto *p=default_palette?Find(default_palette,PaletteObject):nullptr;
    if(!p)return;
    for(unsigned i=0;i<p->palette.count;++i){
        const unsigned short slot=(unsigned short)Win98GdiRealize::StaticSlot(system_palette,i);
        p->palette.entries[i].mapping[0]=p->palette.entries[i].mapping[1]=p->palette.entries[i].mapping[2]=slot;
    }
    p->palette.realized=true;p->palette.state=2;
}
void ResetSystemPalette()
{
    Win98GdiRealize::Reset(system_palette,1u<<unsigned(display_depth));
    system_palette_use=1;
    for(unsigned i=0;i<Capacity;++i){
        Object &o=objects[i];
        if(o.kind!=PaletteObject)continue;
        o.palette.state=0;o.palette.time=0;o.palette.realized=false;
    }
    MapDefaultPalette();
}
}
void SetDisplayPaletteSink(void *context,void (*sink)(void *,unsigned,unsigned,const unsigned *))
{ display_palette_context=context; display_palette_sink=sink; }
// Program the system palette as a display driver programs its DAC registers.
// Colors use COLORREF order, 0x00bbggrr, as in PALETTEENTRY.
unsigned SetDisplayPalette(unsigned start,unsigned count,const unsigned *entries)
{
    if(!entries||display_depth>8)return 0;
    const unsigned size=1u<<unsigned(display_depth);
    if(start>=size)return 0;
    if(count>size-start)count=size-start;
    if(!count)return 0;
    unsigned colors[256]={};
    for(unsigned i=0;i<count;++i){
        const unsigned entry=entries[i]&0xffffff;
        colors[i]=((entry&255)<<16)|(entry&0xff00)|((entry>>16)&255);
        display_colors[start+i]=colors[i];
        system_palette.color[start+i]=entry;
    }
    if(display_palette_sink)display_palette_sink(display_palette_context,start,count,colors);
    RefreshDeviceTables();
    return count;
}
void SetDisplayColors(const unsigned *colors,unsigned count)
{
    if(!colors)return;
    if(count>256)count=256;
    for(unsigned i=0;i<count;++i){display_colors[i]=colors[i]&0xffffff;system_palette.color[i]=Pixel(colors[i]&0xffffff);}
    RefreshDeviceTables();
}
int DisplayDepth() { return display_depth; }
int SurfaceDepth(HDC handle)
{
    const auto *c=DC(handle);if(!c)return 0;
    const auto *b=Find(c->current.bitmap,BitmapObject);
    return b?b->bitmap.depth:0;
}
// A DC on the display: a window, screen or display DC. Memory DCs have the
// device of their bitmap, a printer DC that of its driver.
bool DisplayLikeDC(HDC handle)
{
    const auto *c=DC(handle);
    return c && !c->memory_dc && !c->device_info;
}
void AttachDisplayColors(HBITMAP handle)
{
    auto *b=Find(handle,BitmapObject);
    if(!b||b->dib||b->bitmap.depth>8||display_depth>8)return;
    const unsigned count=1u<<unsigned(b->bitmap.depth);
    if(AttachSurfaceColors(*b,b->bitmap,display_colors,count)&&b->dib)b->dib->colours_only=true;
}
void SetDisplayCaps(int width,int height,int depth)
{
    if(width<=0||height<=0||depth<=0)return;
    const int previous=display_depth;
    // A mode switch preserves the display association of existing DDBs.
    // Their stored pixel format stays unchanged, and even a new memory DC
    // can select them. A bitmap created with an incompatible depth still
    // fails selection until it belongs to a matching display mode.
    if(depth!=previous)
        for(unsigned i=0;i<Capacity;++i)
            if(auto &o=objects[i];o.kind==BitmapObject&&o.original_bitmap&&o.bitmap.depth==previous)
                o.display_compatible=true;
    display_width=width;display_height=height;display_depth=depth;
    // Initialize the device color table with the fixed display colors. All
    // remaining slots are free, and no palette has been realized yet.
    if(depth<=8&&depth!=previous)ResetSystemPalette();
}
unsigned DeviceCaps(HDC handle,unsigned index)
{
    auto *c=DC(handle);if(!c)return 0;
    if(c->device_info){
        // GDI answers from the GDIINFO of the driver; GDI32
        // asks the driver's escapes 12 to 14 for the indices 110 to 115.
        struct Device {
            Context &c;
            unsigned Word(unsigned index){return index+1<c.device_info_bytes?c.device_info[index]|unsigned(c.device_info[index+1])<<8:0;}
            unsigned BitmapCaps(unsigned){return 0;}
            int Escape(unsigned number,unsigned (&pair)[2]){return c.device_escape?c.device_escape(c.device_context,number,pair):0;}
            unsigned Direct(unsigned index){return Win98DcInformation::Caps32(*this,c.signature,0,index);}
        } device{*c};
        return Win98DcInformation::Query(device,index);
    }
    struct Display {
        unsigned signature;
        unsigned char data[112]={};
        void Set(unsigned index,unsigned v){data[index]=v;data[index+1]=v>>8;}
        unsigned Word(unsigned index){return data[index]|unsigned(data[index+1])<<8;}
        unsigned BitmapCaps(unsigned){return 0;}
        int Escape(unsigned,unsigned (&)[2]){return 0;}
        unsigned Direct(unsigned index){return Win98DcInformation::Caps32(*this,signature,0,index);}
    } display{c->signature};
    // Native framebuffer driver capabilities. Hardware values are not original code.
    display.Set(0,0x400);display.Set(2,1);display.Set(4,211);display.Set(6,158);
    display.Set(8,unsigned(display_width));display.Set(10,unsigned(display_height));
    display.Set(12,unsigned(display_depth));display.Set(14,1);
    display.Set(20,0);display.Set(22,0);display.Set(26,0xffff);
    // At eight bits per pixel or below, advertise RC_PALETTE (0x100), twenty
    // fixed colors in dpNumColors, the table size in dpPalColors and entry
    // precision in dpPalResolution. Above eight bits, Caps32 returns -1 for
    // dpNumColors.
    const bool palettized=display_depth<=8;
    if(palettized){display.Set(24,20);display.Set(104,1u<<unsigned(display_depth));display.Set(108,24);}
    display.Set(30,2);display.Set(34,1);display.Set(36,1);
    // dpCaps1. The whole drawing lower edge of NEO98 is the DIB engine port,
    // so C1_DIBENGINE (0x10) is set, the bit DDRAW16 asks for. The
    // gamma bit 0x20 and the dynamic-resolution bit 0x80 stay clear: GDI
    // and Windows 98 read the same word, and this driver has neither a
    // gamma table nor a ResetDC path. dpCaps2 at 96 is
    // printer and colour management and stays zero.
    display.Set(94,0x10);
    display.Set(38,palettized?0x2a81u|0x100u:0x2a81u);display.Set(88,96);display.Set(90,96);
    return Win98DcInformation::Query(display,index);
}

bool DeleteDC(HDC handle)
{
    auto *c = DC(handle);
    if (!c) return false;
    ReleaseState(c->current);
    for (auto *saved=c->saved;saved;saved=saved->previous) ReleaseState(saved->state);
    ReleasePath(*c);
    delete[] c->device_info; c->device_info = nullptr;
    Free(c->default_bitmap); Free(c->default_brush); Free(c->default_font); Free(c->default_pen); Free(handle);
    return true;
}
// The DC GDI.EXE makes for a driver it loaded : no drawing surface of
// its own, the capabilities of the driver's GDIINFO.
HDC CreateDeviceDC(const unsigned char *gdiinfo,unsigned bytes,bool information)
{
    if(!gdiinfo||bytes<0x6e)return 0;
    const HDC handle=CreateCompatibleDC(0);if(!handle)return 0;
    auto *c=DC(handle);c->device_dc=true;c->memory_dc=false;c->signature=Win98DcInformation::Signature(information);
    auto *bitmap=Find(c->default_bitmap,BitmapObject);
    delete[] bitmap->allocation;bitmap->allocation=nullptr;bitmap->bitmap=DibEng::Bitmap{};
    c->device_info=new unsigned char[bytes];memcpy(c->device_info,gdiinfo,bytes);c->device_info_bytes=bytes;
    // The clip of a new printer DC is its page, GDIINFO dpHorzRes and dpVertRes.
    const int width=gdiinfo[8]|gdiinfo[9]<<8,height=gdiinfo[10]|gdiinfo[11]<<8;
    c->current.clip[0]={0,0,width,height};
    c->current.device_width=width;c->current.device_height=height;
    // The visible region of a device DC is the whole device (GDI builds it
    // from GDIINFO when it makes the DC); drawing clips against it.
    {
        RegionSerial serial;
        if(!region_store.Initialize()){DeleteDC(handle);return 0;}
        const unsigned page=region_store.Create({0,0,unsigned(width),unsigned(height)});
        if(!page||!ReplaceClip(c->current,page,false)){DeleteDC(handle);return 0;}
    }
    return handle;
}
bool DeviceInfo(HDC handle,const unsigned char *&gdiinfo,unsigned &bytes)
{
    auto *c=DC(handle);if(!c||!c->device_info)return false;
    gdiinfo=c->device_info;bytes=c->device_info_bytes;return true;
}
void SetDeviceEscape(HDC handle,void *context,int (*escape)(void *,unsigned,unsigned (&)[2]))
{
    if(auto *c=DC(handle)){c->device_context=context;c->device_escape=escape;}
}
void SetDeviceDraw(HDC handle,const DeviceDraw *draw)
{
    if(auto *c=DC(handle))c->device_draw=draw;
}

HBITMAP CreateDIBSection(HDC dc, const BITMAPINFO *info, unsigned usage, void **bits,
                         unsigned section, unsigned offset)
{
    if (bits) *bits = nullptr;
    if (!bits || !info || (dc && !DC(dc)) || usage || section || offset) return 0;
    const auto &h = info->bmiHeader;
    if (h.biSize != 40 || h.biPlanes != 1 || h.biCompression || h.biClrUsed ||
        h.biWidth < 1 || h.biWidth > 4096 || !h.biHeight || h.biHeight < -4096 || h.biHeight > 4096) return 0;
    if (h.biBitCount != 1 && h.biBitCount != 4 && h.biBitCount != 8 && h.biBitCount != 16 &&
        h.biBitCount != 24 && h.biBitCount != 32) return 0;
    const int height = h.biHeight < 0 ? -h.biHeight : h.biHeight;
    const int stride = ((h.biWidth * h.biBitCount + 31) / 32) * 4;
    HBITMAP handle = Allocate(BitmapObject);
    if (!handle) return 0;
    auto *o = Find(handle, BitmapObject);
    o->allocation = new unsigned char[(DibEng::Size)height * stride]{};
    o->bitmap = {o->allocation + (h.biHeight > 0 ? (height - 1) * stride : 0),
                 h.biWidth, height, h.biHeight > 0 ? -stride : stride, h.biBitCount};
    *bits = o->allocation;
    return handle;
}

HBRUSH CreateSolidBrush(COLORREF color)
{
    HBRUSH handle = Allocate(BrushObject);
    if (handle) {Find(handle, BrushObject)->brush.color = Pixel(color);Find(handle,BrushObject)->logical_brush_color=color;}
    return handle;
}

// GDI.604. SetSysColors recolours the system brushes in place, so every handle
// GetSysColorBrush ever handed out keeps working.
bool SetSolidBrushColour(HBRUSH handle, COLORREF color)
{
    auto *object = Find(handle, BrushObject);
    if (!object) return false;
    object->brush.color = Pixel(color);
    object->logical_brush_color = color;
    return true;
}

HBRUSH CreateNullBrush()
{
    auto handle=Allocate(BrushObject);
    if(handle)Find(handle,BrushObject)->hollow_brush=true;
    return handle;
}

void ProtectStockBrush(HBRUSH handle,bool on)
{
    if(auto *brush=Find(handle,BrushObject))brush->protected_stock_brush=on;
}
bool ProtectedStockBrush(HBRUSH handle)
{
    const auto *brush=Find(handle,BrushObject);
    return brush&&brush->protected_stock_brush;
}

bool SolidBrushInfo(HBRUSH handle, unsigned (&info)[3])
{
    const auto *brush = Find(handle, BrushObject);
    // GDI over a style 5 brush: lbStyle 5, lbColor the usage word,
    // lbHatch the handle a 16-bit caller gave, zero for one made by GDI32.
    if (brush && brush->dib_brush) { info[0] = 5; info[1] = brush->dib_usage; info[2] = 0; return true; }
    if(brush && brush->original_brush)
    {
        const unsigned original=bitmap_store.Object(brush->original_brush);
        info[0]=bitmap_store.Read16(original+14);info[1]=bitmap_store.Read32(original+16);
        info[2]=bitmap_store.Native(bitmap_store.Read16(original+22));return true;
    }
    if (!brush || brush->brush.pattern) return false;
    if (brush->hatch_style < Win98GdiHatch::Styles)
    { info[0] = 2; info[1] = brush->logical_brush_color; info[2] = brush->hatch_style; return true; }
    info[0] = brush->hollow_brush ? 1 : 0; info[1] = brush->logical_brush_color; info[2] = 0;
    return true;
}
unsigned PatternBrushDib(HBRUSH handle, unsigned char *data, unsigned capacity)
{
    const auto *o=Find(handle,BrushObject);
    if(!o || !o->original_brush)return 0;
    const unsigned storage=bitmap_store.Read16(bitmap_store.Object(o->original_brush)+20);
    const unsigned descriptor=bitmap_store.GlobalLock(storage);
    if(!descriptor)return 0;
    const unsigned width=bitmap_store.Read16(descriptor+2),height=bitmap_store.Read16(descriptor+4);
    const unsigned source_stride=bitmap_store.Read16(descriptor+6),depth=bitmap_store.Read8(descriptor+9);
    const unsigned row=(width*depth+7)/8,stride=((width*depth+31)/32)*4,palette=depth==1?8:0;
    if(!width || !height || bitmap_store.Read8(descriptor+8)!=1 ||
       (depth!=1 && depth!=24 && depth!=32) || source_stride<row ||
       height>(4u*1024*1024-40-palette)/stride)
    {bitmap_store.GlobalUnlock(storage);return 0;}
    const unsigned bytes=40+palette+height*stride;
    if(data && capacity>=bytes)
    {
        memset(data,0,bytes);
        const unsigned header[10]={40,width,height,1|(depth<<16),0,height*stride,0,0,0,0};
        memcpy(data,header,40);
        if(palette){data[44]=data[45]=data[46]=255;}
        const unsigned bits=bitmap_store.ReadPointer(descriptor+10);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<row;++x)
            data[40+palette+y*stride+x]=bitmap_store.Read8(bits+(height-1-y)*source_stride+x);
    }
    bitmap_store.GlobalUnlock(storage);return bytes;
}
#include "gdi_palette.inc"
#include "gdi_pixel.inc"
// Update visible pixels in the DC by mapping each old palette slot to its new
// slot after realization, matching Windows 98 UpdateColors.
bool UpdateColors(HDC handle)
{
    auto *c=DC(handle);if(!c||c->device_info||!PaletteDisplay())return false;
    auto *object=Find(c->current.bitmap,BitmapObject);
    auto *p=Find(c->current.palette,PaletteObject);
    if(!object||!p||!DibEng::Valid(object->bitmap))return false;
    GuardScreen(object,true);
    auto &bitmap=object->bitmap;
    if(bitmap.depth!=8)return true;
    unsigned char table[256];
    Win98GdiRealize::UpdateTable(p->palette,256,table);
    BitmapAccess access(object,true);
    const RECT bounds={0,0,bitmap.width,bitmap.height};
    for(unsigned i=0;i<c->current.clips;++i){
        RECT clip;if(!Intersect(clip,bounds,ClipAt(c->current,i)))continue;
        for(int y=clip.top;y<clip.bottom;++y)for(int x=clip.left;x<clip.right;++x)
            DibEng::WritePixel(bitmap,x,y,table[DibEng::ReadPixel(bitmap,x,y)&255]);
    }
    return true;
}
COLORREF GetPixel(HDC h,int x,int y){return PixelAccess(0,h,x,y,0,nullptr);}

HBRUSH CreateDibPatternBrush(const unsigned char *dib, unsigned size, unsigned usage)
{
    return CreateDibPatternBrushObject(dib, size, usage);
}
HBRUSH CreatePatternBrush(HBITMAP handle)
{
    auto *b = Find(handle, BitmapObject);
    BitmapAccess access(b, false);
    if (b && b->original_bitmap)
    {
        bitmap_store.failed = false;
        const auto original = Win98PatternBrush::PublicCreate(bitmap_store, bitmap_store.state.serial, b->original_bitmap);
        if (!original) return 0;
        bitmap_store.AttachBrush(original);
        return bitmap_store.Native(original);
    }
    if (!b || !DibEng::Valid(b->bitmap) ||
        (b->bitmap.depth != 1 && b->bitmap.depth != 32)) return 0;
    HBRUSH result = Allocate(BrushObject);
    if (!result) return 0;
    auto *o = Find(result, BrushObject);
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x)
        o->pattern[y * 8 + x] = DibEng::ReadPixel(b->bitmap, x % b->bitmap.width, y % b->bitmap.height);
    o->monochrome_pattern = b->bitmap.depth == 1;
    o->brush.pattern = o->pattern;
    o->pattern_ready = true;
    return result;
}

HFONT CreateFontResource(unsigned index)
{
    auto font = DibText::OriginalFont(index);
    if (!font.data) return 0;
    const auto handle = Allocate(FontObject);
    if (handle) Find(handle, FontObject)->font = font;
    return handle;
}
bool FontInfo(HFONT handle, unsigned char (&logical)[60])
{
    const auto *o=Find(handle,FontObject);
    if(!o || !o->font.data || o->font.length<118)return false;
    const auto *p=o->font.data;
    memset(logical,0,sizeof logical);
    const unsigned height=o->font.Height(), weight=p[83]|(unsigned(p[84])<<8);
    memcpy(logical,&height,4);memcpy(logical+16,&weight,4);
    logical[20]=p[80];logical[21]=p[81];logical[22]=p[82];logical[23]=p[85];
    logical[27]=p[90];
    char face[32]={};o->font.FaceName(face);memcpy(logical+28,face,32);
    return true;
}
HFONT CreateFontView(const unsigned char *data, unsigned length)
{
    DibText::Font font;
    if (!font.Load(data, length)) return 0;
    const auto handle = Allocate(FontObject);
    if (handle) Find(handle, FontObject)->font = font;
    return handle;
}
HFONT CreateFontOutline(const DibText::Font &view)
{
    if (!view.data || !view.outline) return 0;
    const auto handle = Allocate(FontObject);
    if (handle) Find(handle, FontObject)->font = view;
    return handle;
}
HFONT CreateFontMemory(const unsigned char *data, unsigned length)
{
    DibText::Font font;
    if (!font.Load(data, length)) return 0;
    const auto handle = Allocate(FontObject); if (!handle) return 0;
    auto *o = Find(handle, FontObject); o->allocation = new unsigned char[length];
    __builtin_memcpy(o->allocation, data, length); o->font.Load(o->allocation, length); return handle;
}
HFONT CreateFontVariant(HFONT base, bool italic, bool underline, bool strikeout, bool bold)
{
    auto *o = Find(base, FontObject);
    if (!o || !o->font.data || o->font.length < 118) return 0;
    if (bold && !o->font.outline)
    {
        // GDI : a raster face lighter than the
        // request gets the simulation flag 200h, its weight plus 300 and one
        // pixel of overhang. Like the TrueType realization (
        // src/gdi_truetype.cpp) every character is one pixel wider and each
        // row is ORed with itself one pixel to the right, so the extents,
        // GetCharWidth and the dialog base units carry the pixel.
        // The copy is a FNT 3.0 marked "SB" in dfReserved1 with the overhang
        // at 8Eh (Win98TextSimulation::BoldOverhang).
        const DibText::Font &plain = o->font;
        const unsigned char *p = plain.data;
        const unsigned first = p[0x5f], last = p[0x60], count = last - first + 1, height = plain.Height();
        const unsigned table = 148, entry = 6, entries = count + 1;
        char face[32] = {};
        plain.FaceName(face);
        const unsigned face_length = unsigned(__builtin_strlen(face)) + 1;
        unsigned bits = 0;
        for (unsigned i = 0; i < count; ++i) bits += ((plain.Width(i) + 1 + 7) / 8) * height;
        const unsigned face_at = table + entries * entry, bits_at = face_at + face_length;
        const unsigned length = bits_at + bits;
        auto *copy = new unsigned char[length];
        __builtin_memset(copy, 0, length);
        __builtin_memcpy(copy, p, 118);
        auto word = [&](unsigned at, unsigned v) { copy[at] = v & 255; copy[at + 1] = (v >> 8) & 255; };
        auto dword = [&](unsigned at, unsigned v) { word(at, v & 0xffff); word(at + 2, v >> 16); };
        auto read = [&](unsigned at) { return unsigned(p[at] | unsigned(p[at + 1]) << 8); };
        word(0, 0x300); dword(2, length);
        word(0x53, read(0x53) + 300);
        word(0x5b, read(0x5b) + 1); word(0x5d, read(0x5d) + 1);
        if (read(0x56)) word(0x56, read(0x56) + 1);
        dword(0x65, 0); dword(0x69, face_at); dword(0x6d, 0); dword(0x71, bits_at);
        dword(0x76, 0); copy[0x84] = 'S'; copy[0x85] = 'B'; copy[0x8e] = 1;
        __builtin_memcpy(copy + face_at, face, face_length);
        unsigned at = bits_at;
        for (unsigned i = 0; i < count; ++i)
        {
            const unsigned width = plain.Width(i), source = plain.Bits(i);
            const unsigned columns = (width + 7) / 8, wide = width + 1, wide_columns = (wide + 7) / 8;
            word(table + i * entry, wide); dword(table + i * entry + 2, at);
            for (unsigned y = 0; y < height; ++y)
                for (unsigned x = 0; x < width; ++x)
                {
                    if (source + (x / 8) * height + y >= plain.length || x / 8 >= columns) continue;
                    if (!(p[source + (x / 8) * height + y] & (0x80 >> (x & 7)))) continue;
                    for (unsigned k = 0; k < 2; ++k)
                        copy[at + ((x + k) / 8) * height + y] |= (unsigned char)(0x80 >> ((x + k) & 7));
                }
            at += wide_columns * height;
        }
        word(table + count * entry, 0); dword(table + count * entry + 2, at < length ? at : bits_at);
        copy[80] = italic ? 1 : 0; copy[81] = underline ? 1 : 0; copy[82] = strikeout ? 1 : 0;
        const HFONT handle = CreateFontMemory(copy, length);
        delete[] copy;
        return handle;
    }
    const unsigned length = o->font.length;
    auto *copy = new unsigned char[length];
    __builtin_memcpy(copy, o->font.data, length);
    copy[80] = italic ? 1 : 0; copy[81] = underline ? 1 : 0; copy[82] = strikeout ? 1 : 0;
    const HFONT handle = CreateFontMemory(copy, length);
    delete[] copy;
    return handle;
}
DibText::Font SelectedFont(HDC handle)
{
    auto *c=DC(handle);if(!c)return {};
    auto *o=Find(c->current.font,FontObject);return o?o->font:DibText::Font{};
}
bool GetTextMetricsA(HDC handle, TEXTMETRICA *metrics)
{
    auto *c=DC(handle);if(!c)return false;
    if(c->device_draw)return metrics&&c->device_draw->metrics(c->device_context,handle,metrics);
    if (!FontMetrics(SelectedFont(handle),metrics)) return false;
    if (!(c->current.mapping.transform_flags&1))
    {
        const auto &m=c->current.mapping;
        auto convert=[](int value,int window,int viewport) {
            return Win98DcMap::MultiplyDivide(unsigned(value),unsigned(window < 0 ? -window : window),
                unsigned(viewport < 0 ? -viewport : viewport)).value;
        };
        int *vertical[]={&metrics->tmHeight,&metrics->tmAscent,&metrics->tmDescent,
            &metrics->tmInternalLeading,&metrics->tmExternalLeading};
        for (auto value:vertical) *value=convert(*value,m.window_extent.y,m.viewport_extent.y);
        int *horizontal[]={&metrics->tmAveCharWidth,&metrics->tmMaxCharWidth,&metrics->tmOverhang};
        for (auto value:horizontal) *value=convert(*value,m.window_extent.x,m.viewport_extent.x);
    }
    return true;
}
bool FontMetrics(const DibText::Font &font,TEXTMETRICA *metrics)
{
    if(!font.data || !metrics)return false;
    const auto *p=font.data;
    auto word = [&](unsigned at) { return (int)(p[at] | (unsigned)p[at + 1] << 8); };
    *metrics = {};
    metrics->tmHeight = font.Height(); metrics->tmAscent = font.Ascent(); metrics->tmDescent = font.Height() - font.Ascent();
    metrics->tmInternalLeading = word(76); metrics->tmExternalLeading = word(78);
    metrics->tmAveCharWidth = word(91); metrics->tmMaxCharWidth = word(93); metrics->tmWeight = word(83);
    metrics->tmOverhang = Win98TextSimulation::ItalicOverhang(font) + Win98TextSimulation::BoldOverhang(font);
    metrics->tmDigitizedAspectX = word(72); metrics->tmDigitizedAspectY = word(70);
    metrics->tmFirstChar = p[95]; metrics->tmLastChar = p[96]; metrics->tmDefaultChar = p[95] + p[97]; metrics->tmBreakChar = p[95] + p[98];
    metrics->tmItalic = p[80]; metrics->tmUnderlined = p[81]; metrics->tmStruckOut = p[82];
    metrics->tmPitchAndFamily = p[90]; metrics->tmCharSet = p[85];
    // A realized TrueType font keeps GDI's character range
    // with charset dword 0100FF1Fh, and the overhang of a
    // simulated bold in its header.
    if (Win98TextSimulation::TrueTypeStrike(font) && font.outline) {
        metrics->tmFirstChar = p[0x90]; metrics->tmLastChar = p[0x91];
        metrics->tmDefaultChar = p[0x92]; metrics->tmBreakChar = p[0x93]; metrics->tmOverhang = p[0x8e];
    }
    return true;
}

HGDIOBJ SelectObject(HDC handle, HGDIOBJ object)
{
    auto *c = DC(handle);
    if (!c) return 0;
    Kind kind = BitmapObject;
    if (bitmap_store.state.empty_bitmap && object == bitmap_store.Native(bitmap_store.state.empty_bitmap))
        object = c->default_bitmap;
    Object *o = Find(object, kind);
    if (!o) { kind = BrushObject; o = Find(object, kind); }
    if (!o) { kind = FontObject; o = Find(object, kind); }
    if (!o) { kind = PenObject; o = Find(object, kind); }
    if (!c->memory_dc && kind == BitmapObject) return 0;
    // An unrelated DDB must match the display depth. Bitmaps retained across
    // a display change, and DIB sections, keep their own pixel format.
    if (o && kind == BitmapObject && o->original_bitmap &&
        !o->display_compatible && o->bitmap.depth != 1 && o->bitmap.depth != display_depth) return 0;
    // Win98 gives every memory DC the same stock bitmap, 1 by 1 and
    // monochrome, and SelectObject skips the rule that a bitmap
    // lives in one DC only for it. So the bitmap one
    // DC answered on its first SelectObject can go back into any other DC and
    // takes the selected bitmap out there. DESK.CPL relies on it after a
    // colour change: it frees the monitor picture from its drawing DC with
    // the default bitmap of another DC, and otherwise the picture stays held
    // and the Settings page paints it black. Here each DC keeps a
    // default bitmap of its own, so another DC's one stands for this DC's.
    if (o && kind == BitmapObject && o->private_default && object != c->default_bitmap && o->allocation &&
        o->bitmap.width == 1 && o->bitmap.height == 1 && o->bitmap.depth == 1)
    {
        object = c->default_bitmap;
        o = Find(object, kind);
    }
    if (o && kind == FontObject && o->private_default && object != c->default_font)
    {
        object = c->default_font;
        o = Find(object, kind);
    }
    if (!o || (kind == BitmapObject && ((o->owner && o->owner != handle) || !DibEng::Valid(o->bitmap)))) return 0;
    // Private defaults belong to their creating DC, even while deselected.
    if (o->private_default && object != c->default_bitmap && object != c->default_brush && object != c->default_font && object != c->default_pen) return 0;
    if(kind==PenObject){
        struct E {HDC owner;bool Realize(unsigned){return true;}void AddReference(unsigned h){Retain(h,PenObject,owner);}void RemoveReference(unsigned h){Release(h,PenObject);}}e{handle};
        return Win98PenLine::Select(e,c->current.pen,object);
    }
    unsigned &selected = kind == BitmapObject ? c->current.bitmap : kind == BrushObject ? c->current.brush : kind == FontObject ? c->current.font : c->current.pen;
    const unsigned previous = selected;
    Retain(object, kind, handle); Release(previous, kind); selected = object;
    return kind == BitmapObject ? PublicBitmap(previous) : previous;
}

bool DeleteObject(HGDIOBJ handle)
{
    if(const auto *o=FindAny(handle))if(o->private_default)return true;
    // GDI with mask 0xa0: a private object refuses deletion and still
    // reports success.
    if (const Object *p = FindAny(handle))
        if (Win98GdiOwner::DeleteRefused(p->object_private ? Win98GdiOwner::PrivateFlag : 0,
                                         Win98GdiOwner::DeleteMask))
            return true;
    if(auto*p=Find(handle,PaletteObject)){if(!p->references&&!p->palette.stock)Free(handle);return true;}
    auto *o = Find(handle, BitmapObject);
    if (o && o->original_bitmap) return bitmap_store.Destroy(o->original_bitmap) != 0;
    if (!o) o = Find(handle, BrushObject);
    if (!o) o = Find(handle, FontObject);
    if (!o) o = Find(handle, RegionObject);
    if (!o && Find(handle,PenObject)) return DeletePenObject(handle);
    if (!o || o->references || o->private_default) return false;
    Free(handle); return true;
}

int SaveDC(HDC handle)
{
    auto *c = DC(handle);
    if (!c || c->saves == 0x7fffffffu) return 0;
    // GDI allocates another DC state in the GDI heap;
    // leaves the chain and level unchanged if allocation fails.
    auto *saved=new(std::nothrow) SavedState{c->current,c->saved};
    if(!saved)return 0;
    RetainState(c->current, handle);c->saved=saved;
    return ++c->saves;
}

bool RestoreDC(HDC handle, int saved)
{
    auto *c = DC(handle);
    if (!c || !saved || saved < -(int)c->saves || saved > (int)c->saves) return false;
    const unsigned index = saved < 0 ? c->saves + saved : saved - 1;
    // GDI (SETDCSTATE) calls the path release, so restoring
    // a state drops whatever path the device context was building.
    ReleasePath(*c);
    ReleaseState(c->current);
    while(c->saves>index+1){
        auto *saved=c->saved;c->saved=saved->previous;
        ReleaseState(saved->state);delete saved;--c->saves;
    }
    auto *restored=c->saved;c->saved=restored->previous;
    c->current=restored->state; // transfer this saved state's references
    delete restored;c->saves=index;
    return true;
}

// GDI32, ordinal 380. The public entry resolves the DC, rejects an
// object whose tag at +2 is outside the DC family, and for a DC that is not a
// metafile ('Q') shifts the requested point by the DC's origin words at +CCh
// and +CEh before the worker stores it; the previous point it
// writes back through lppt is shifted back by the same amounts. NEO98 keeps a
// DC's origin in Win98DcMap and stores a relative origin after WORD narrowing.
// The public getter subtracts the device origin after sign-extending those
// WORDs.
bool SetBrushOrgEx(HDC handle, int x, int y, POINT *previous)
{
    auto *c = DC(handle);
    if (!c) return false;
    if (previous) *previous = c->current.brush_origin;
    const auto origin = c->current.mapping.device_origin;
    c->current.brush_origin = {short(unsigned(x) + unsigned(origin.x)) - origin.x,
                               short(unsigned(y) + unsigned(origin.y)) - origin.y};
    return true;
}

bool GetBrushOrgEx(HDC handle, POINT *point)
{
    auto *c = DC(handle);
    if (!c || !point) return false;
    *point = c->current.brush_origin;
    return true;
}

bool SetViewportOrgEx(HDC handle, int x, int y, POINT *previous)
{
    auto *c = DC(handle);
    if (!c) return false;
    const auto old=Win98DcMap::SetOrigin(c->current.mapping,true,{x,y});
    if (previous) *previous={old.x,old.y};
    return true;
}

namespace {
int CombineClipRectangle(HDC handle,int left,int top,int right,int bottom,unsigned mode){
    auto*c=DC(handle);RECT r;
    if(right<left){int swap=left;left=right;right=swap;}
    if(bottom<top){int swap=top;top=bottom;bottom=swap;}
    if(!c||(long long)right-left>32767||(long long)bottom-top>32767||!Rectangle(c->current,left,top,right-left,bottom-top,r))return 0;
    RegionSerial serial;if(!region_store.Initialize())return 0;
    unsigned previous=CopyClip(c->current,true);if(!previous)return 0;
    unsigned rectangle=region_store.Create({unsigned(r.left),unsigned(r.top),unsigned(r.right),unsigned(r.bottom)});
    if(!rectangle){region_store.Destroy(previous);return 0;}
    unsigned result=Win98Region::Combine(region_store,region_store.state,previous,previous,rectangle,mode);region_store.Destroy(rectangle);
    if(!result){region_store.Destroy(previous);return 0;}if(!ReplaceClip(c->current,previous,true))return 0;return EffectiveClipType(c->current);
}
}
int IntersectClipRect(HDC h,int l,int t,int r,int b){return CombineClipRectangle(h,l,t,r,b,1);}
int ExcludeClipRect(HDC h,int l,int t,int r,int b){return CombineClipRectangle(h,l,t,r,b,4);}

COLORREF SetTextColor(HDC handle, COLORREF color)
{
    auto *c = DC(handle); if (!c) return ~0u;
    if(!ResolveLogicalColor(c->current,color,color))return ~0u;
    auto previous = c->current.text_mode.foreground; c->current.text_mode.foreground = Pixel(color); return Pixel(previous);
}

int IntersectClipRegion(HDC handle,const RECT*rectangles,unsigned count){
    auto*c=DC(handle);if(!c||(count&&!rectangles)||count>65535)return 0;
    RegionSerial serial;if(!region_store.Initialize())return 0;
    unsigned input=region_store.Create({0,0,0,0});if(!input)return 0;
    for(unsigned i=0;i<count;++i){const auto&r=rectangles[i];RECT d;
        if(r.right<r.left||r.bottom<r.top||(long long)r.right-r.left>32767||(long long)r.bottom-r.top>32767||!Rectangle(c->current,r.left,r.top,r.right-r.left,r.bottom-r.top,d)||
           !Win98Region::Append(region_store,region_store.state,input,{unsigned(d.left),unsigned(d.top),unsigned(d.right),unsigned(d.bottom)})){region_store.Destroy(input);return 0;}
    }
    unsigned previous=CopyClip(c->current,false);if(!previous){region_store.Destroy(input);return 0;}
    unsigned result=Win98Region::Combine(region_store,region_store.state,previous,previous,input,1);region_store.Destroy(input);
    if(!result){region_store.Destroy(previous);return 0;}return ReplaceClip(c->current,previous,false)?EffectiveClipType(c->current):0;
}

// Native window-region providers for USER's visible regions. They read and drive
// the original band representation and boolean core; no new region algorithm is
// added here. Region objects stay owned by their creator.
HGDIOBJ CreateRegionRect(int left,int top,int right,int bottom){
    RegionSerial serial;if(!region_store.Initialize())return 0;
    return PublishRegion(Win98RegionApi::Create(region_store,region_store.state,
        unsigned(left),unsigned(top),unsigned(right),unsigned(bottom)));
}
int CombineRegions(HGDIOBJ destination,HGDIOBJ first,HGDIOBJ second,unsigned mode){
    RegionSerial serial;if(!region_store.Initialize())return 0;
    return int(Win98RegionApi::Combine(region_store,region_store.state,RawRegion(destination),
                                       RawRegion(first),RawRegion(second),mode));
}
unsigned RegionRectangles(HGDIOBJ handle,RECT*out,unsigned capacity,int dx,int dy){
    RegionSerial serial;const unsigned raw=RawRegion(handle);if(!raw)return 0;
    auto*snapshot=Snapshot(raw);if(!snapshot)return 0;
    const unsigned count=snapshot->count;
    if(out)for(unsigned i=0;i<count&&i<capacity;++i){const auto&r=snapshot->rectangles[i];
        out[i]={r.left+dx,r.top+dy,r.right+dx,r.bottom+dy};}
    free(snapshot); // The band storage belongs to the caller's object.
    return count;
}
RegionRectanglesCopy::~RegionRectanglesCopy(){delete[] rectangles;}
bool RegionRectanglesCopy::Read(HGDIOBJ handle,int dx,int dy)
{
    RegionSerial serial;const unsigned raw=RawRegion(handle);if(!raw)return false;
    auto *snapshot=Snapshot(raw);if(!snapshot)return false;
    const unsigned n=snapshot->count;
    auto *copy=n?new(std::nothrow) RECT[n]:nullptr;
    if(n&&!copy){free(snapshot);return false;}
    for(unsigned i=0;i<n;++i){const auto&r=snapshot->rectangles[i];
        copy[i]={r.left+dx,r.top+dy,r.right+dx,r.bottom+dy};}
    free(snapshot);delete[] rectangles;rectangles=copy;count=n;return true;
}
bool RegionContainsPoint(HGDIOBJ handle,int x,int y){
    RegionSerial serial;const unsigned raw=RawRegion(handle);
    return raw&&Win98Region::Point(region_store,raw,unsigned(x),unsigned(y))!=0;
}
bool OffsetRegionObject(HGDIOBJ handle,int dx,int dy){
    RegionSerial serial;const unsigned raw=RawRegion(handle);
    return raw&&Win98Region::Offset(region_store,raw,unsigned(dx),unsigned(dy))!=0;
}
bool RegionBounds(HGDIOBJ handle,RECT*out){
    RegionSerial serial;const unsigned raw=RawRegion(handle);if(!raw||!out)return false;
    const auto r=Win98Region::Bounds(region_store,raw);
    *out={Win98Region::S(r.left),Win98Region::S(r.top),Win98Region::S(r.right),Win98Region::S(r.bottom)};
    return true;
}

bool CopySurface(HDC target, int x, int y, const DibEng::Bitmap &source)
{
    auto *c = DC(target); RECT area;
    if (!c || !(c->current.mapping.transform_flags&1) || !DibEng::Valid(source) ||
        !Rectangle(c->current,x,y,source.width,source.height,area)) return false;
    // Menu snapshots are 32-bit even on a paletted desktop. The driver
    // raster operation below only copies matching formats; let GDI realize
    // colours in the destination palette when the formats differ.
    const auto *object = Find(c->current.bitmap, BitmapObject);
    if (!object) return false;
    if (object->bitmap.depth != source.depth)
    {
        const auto dc = CreateSurfaceDC(source);
        if (!dc) return false;
        const bool copied = BitBlt(target, x, y, source.width, source.height, dc, 0, 0, SRCCOPY);
        DeleteDC(dc);
        return copied;
    }
    BitmapAccess access(Find(c->current.bitmap, BitmapObject), true);
    auto &destination = Find(c->current.bitmap,BitmapObject)->bitmap;
    if (destination.bits == source.bits) return false;
    const RECT bounds={0,0,destination.width,destination.height}; RECT bounded;
    if (!Intersect(bounded,area,bounds)) return true;
    // SRCCOPY does not inspect the selected brush (which may be hollow).
    DibEng::Brush brush{};
    for (unsigned i=0;i<c->current.clips;++i)
    {
        RECT clip;
        if (Intersect(clip,bounded,ClipAt(c->current,i)) &&
            !DibEng::BitBlt(destination,clip.left,clip.top,clip.right-clip.left,clip.bottom-clip.top,
                           &source,clip.left-area.left,clip.top-area.top,SRCCOPY,brush)) return false;
    }
    return true;
}
bool ResetSurfaceDC(HDC handle)
{
    auto *c=DC(handle);if(!c)return false;
    State fresh;
    fresh.palette=DefaultPalette();fresh.bitmap=c->current.bitmap;fresh.brush=c->default_brush;fresh.font=c->default_font;fresh.pen=c->default_pen;
    fresh.clip[0]={-32767,-32767,32767,32767};
    RetainState(fresh,handle);ReleaseState(c->current);c->current=fresh;
    return true;
}
// USER's own and class DCs (CS_OWNDC, CS_CLASSDC) keep their attributes and
// the application clip from lease to lease; only the visible region is set
// again, as USER does through SetDCOrg and the visrgn of the DCE.
bool ResetSystemClip(HDC handle)
{
    auto *c=DC(handle);if(!c)return false;
    return ReplaceClip(c->current,0,false);
}
namespace {
struct DcAttributeFields {
    const Context *context;
    unsigned Read16(unsigned) {return context->current.text_mode.background_mode;}
    unsigned Read32(unsigned offset) {
        // Existing native DRAWMODE stores physical RGB pixels. This provider
        // converts back to logical COLORREF for the original DC-field query.
        return Pixel(offset==0x66?context->current.text_mode.background:
                                  context->current.text_mode.foreground);
    }
};
}
COLORREF GetBkColor(HDC handle) {
    auto*c=DC(handle);DcAttributeFields e{c};return Win98DcAttributes::BkColor(e,c?c->signature:0);
}
COLORREF GetTextColor(HDC handle) {
    auto*c=DC(handle);DcAttributeFields e{c};return Win98DcAttributes::TextColor(e,c?c->signature:0);
}
int GetBkMode(HDC handle) {
    auto*c=DC(handle);DcAttributeFields e{c};return Win98DcAttributes::BkMode(e,c?c->signature:0);
}
COLORREF SetBkColor(HDC handle, COLORREF color)
{
    auto *c = DC(handle); if (!c) return ~0u;
    if(!ResolveLogicalColor(c->current,color,color))return ~0u;
    auto previous = c->current.text_mode.background; c->current.text_mode.background = Pixel(color); return Pixel(previous);
}
int SetBkMode(HDC handle, int mode)
{
    auto *c = DC(handle); if (!c || (mode != TRANSPARENT && mode != OPAQUE)) return 0;
    int previous = c->current.text_mode.background_mode; c->current.text_mode.background_mode = mode; return previous;
}

// GDI exchanges the DWORD at DC+A4h. A null `set` only reads it.
bool MapperFlags(HDC handle, const unsigned *set, unsigned &previous)
{
    auto *c = DC(handle);
    if (!c) return false;
    previous = c->current.mapper_flags;
    if (set) c->current.mapper_flags = *set;
    return true;
}
// SetMetaRgn, GDI (src/gdi_region_store.inc).
unsigned SetMetaRgn(HDC handle)
{
    auto *c = DC(handle);
    return c ? SetMetaRegion(c->current) : 0;
}
// GDI DC+74h. A null `set` only reads it.
bool MiterLimit(HDC handle, const unsigned *set, unsigned &previous)
{
    auto *c = DC(handle);
    if (!c) return false;
    previous = c->current.miter_limit;
    if (set) c->current.miter_limit = *set;
    return true;
}
// SetBoundsRect, GDI. `rect` is logical for the application set and
// already in surface coordinates for the window manager's (flag 8000h).
unsigned SetBoundsRect(HDC handle, const RECT *rect, unsigned flags)
{
    auto *c = DC(handle);
    if (!c) return 0;
    auto &st = c->current;
    const bool manager = (flags & 0x8000) != 0;
    const unsigned data = manager ? 1 : 4, enabled = manager ? 2 : 8;
    short *r = st.bounds[manager ? 1 : 0];
    const unsigned previous = (manager ? 0x8000u : 0u) | ((st.bounds_flags & data) ? 3u : 1u) |
                              ((st.bounds_flags & enabled) ? 4u : 8u);
    if (flags & 1)
    {
        r[0] = r[1] = 0x7fff;
        r[2] = r[3] = short(0x8000);
        st.bounds_flags &= ~data;
    }
    if ((flags & 2) && rect)
    {
        RECT d = *rect;
        if (!manager)
        {
            const auto a = DevicePoint(st, d.left, d.top), b = DevicePoint(st, d.right, d.bottom);
            d = {a.x, a.y, b.x, b.y};
        }
        // No normalisation: an inverted rectangle is dropped.
        if (d.right > d.left && d.bottom > d.top)
        {
            if (d.left < r[0]) r[0] = short(d.left);
            if (d.top < r[1]) r[1] = short(d.top);
            if (d.right > r[2]) r[2] = short(d.right);
            if (d.bottom > r[3]) r[3] = short(d.bottom);
            st.bounds_flags |= data;
        }
    }
    if (flags & 4) st.bounds_flags |= enabled;
    if (flags & 8) st.bounds_flags &= ~enabled;
    return previous;
}
// GetBoundsRect, GDI: DCB_RESET (1) when there is nothing,
// otherwise DCB_SET (3). The application set is clipped to the visible
// bounds and goes back to logical units.
unsigned GetBoundsRect(HDC handle, RECT *rect, unsigned flags)
{
    auto *c = DC(handle);
    if (!c) return 0;
    auto &st = c->current;
    if (flags & 0x8000)
    {
        if (!(st.bounds_flags & 1)) return 1;
        if (rect) *rect = {st.bounds[1][0], st.bounds[1][1], st.bounds[1][2], st.bounds[1][3]};
    }
    else
    {
        if (!(st.bounds_flags & 4)) return 1;
        const auto *bitmap = Find(st.bitmap, BitmapObject);
        const int width = bitmap ? bitmap->bitmap.width : 0, height = bitmap ? bitmap->bitmap.height : 0;
        const short *r = st.bounds[0];
        const int left = r[0] > 0 ? r[0] : 0, top = r[1] > 0 ? r[1] : 0;
        const int right = r[2] < width ? r[2] : width, bottom = r[3] < height ? r[3] : height;
        const auto a = LogicalPoint(st, left, top), b = LogicalPoint(st, right, bottom);
        if (rect) *rect = {a.x, a.y, b.x, b.y};
    }
    if (flags & 1) SetBoundsRect(handle, nullptr, flags & 0x8001);
    return 3;
}
// a drawing call adds its surface rectangle to every set that
// accumulates. A zero right or bottom stands for 7FFFh.
void AccumulateBounds(HDC handle, int left, int top, int right, int bottom)
{
    auto *c = DC(handle);
    if (!c || !(c->current.bounds_flags & 0x0a)) return;
    auto &st = c->current;
    if (!right) right = 0x7fff;
    if (!bottom) bottom = 0x7fff;
    if (!(right > left && bottom > top)) return;
    st.bounds_flags |= (st.bounds_flags >> 1) & 5;
    for (unsigned set = 0; set < 2; ++set)
    {
        if (!(st.bounds_flags & (set ? 2u : 8u))) continue;
        short *r = st.bounds[set];
        if (left < r[0]) r[0] = short(left);
        if (top < r[1]) r[1] = short(top);
        if (right > r[2]) r[2] = short(right);
        if (bottom > r[3]) r[3] = short(bottom);
    }
}
bool CurrentPosition(HDC handle, POINT &out)
{
    const auto *c = DC(handle);
    if (!c) return false;
    out = {c->current.position.x, c->current.position.y};
    return true;
}
bool AccumulatesBounds(HDC handle)
{
    const auto *c = DC(handle);
    return c && (c->current.bounds_flags & 0x0a);
}
// A logical rectangle of a drawing call in surface coordinates.
void AccumulateLogical(HDC handle, int left, int top, int right, int bottom)
{
    auto *c = DC(handle);
    if (!c || !(c->current.bounds_flags & 0x0a)) return;
    const auto a = DevicePoint(c->current, left, top), b = DevicePoint(c->current, right, bottom);
    AccumulateBounds(handle, a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.x > b.x ? a.x : b.x,
                     a.y > b.y ? a.y : b.y);
}
// GDI DC+ACh and +AEh. A null `set` only reads them.
bool AspectFilter(HDC handle, const int *set, int &x, int &y)
{
    auto *c = DC(handle);
    if (!c) return false;
    if (set) { c->current.aspect_filter_x = set[0]; c->current.aspect_filter_y = set[1]; }
    x = c->current.aspect_filter_x;
    y = c->current.aspect_filter_y;
    return true;
}
unsigned GetTextAlign(HDC handle){auto*c=DC(handle);return c?c->current.text_align:~0u;}
unsigned SetTextAlign(HDC handle,unsigned align){
    auto*c=DC(handle);if(!c||!Win98GdiTextLayout::ValidAlign(align))return 65535;
    return Win98GdiTextLayout::SetAlign(c->current.text_align,align);
}
namespace {
int TextDistance(const State&s,int amount,bool inverse){
    const auto a=Win98DcMap::Transform(s.mapping,{0,0},inverse,true);
    const auto b=Win98DcMap::Transform(s.mapping,{Win98GdiTextLayout::Word(amount),0},inverse,true);
    return Win98GdiTextLayout::Word(unsigned(b.x)-unsigned(a.x));
}
}
int GetTextCharacterExtra(HDC handle){
    auto*c=DC(handle);return c?Win98GdiTextLayout::ExtraQuery(TextDistance(c->current,c->current.text_mode.char_extra,true)):0;
}
int SetTextCharacterExtra(HDC handle,int extra){
    auto*c=DC(handle);if(!c)return 0;
    const int previous=GetTextCharacterExtra(handle);
    c->current.text_mode.char_extra=TextDistance(c->current,extra,false);return previous;
}
bool SetTextJustification(HDC handle,int extra,int breaks){
    auto*c=DC(handle);if(!c)return false;
    if(!Win98GdiTextLayout::Word(breaks)&&Win98GdiTextLayout::Word(extra))return false;
    return Win98GdiTextLayout::Justify(c->current.text_mode,TextDistance(c->current,extra,false),breaks);
}
static bool TextExtent(HDC handle, const char *text, int length, SIZE *size,unsigned options)
{
    auto *c = DC(handle);
    if (!c || !size || (!text && length) || length < 0 || length > 4096) return false;
    if (!length) { *size = {0, 0}; return true; }
    if (c->device_draw) return !options&&c->device_draw->extent(c->device_context, handle, text, unsigned(length), size);
    const auto &font = Find(c->current.font, FontObject)->font;
    const unsigned extent = DibText::ExtTextOut(nullptr, 0, 0, {}, (const unsigned char *)text,
                                               -length, font, c->current.text_mode,nullptr,nullptr,options);
    if (extent == 0x80000000) return false;
    *size = {(int)(extent & 65535), (int)(extent >> 16)};
    if (!(c->current.mapping.transform_flags&1))
    {
        const auto &m=c->current.mapping;
        size->cx=Win98DcMap::MultiplyDivide(unsigned(size->cx),unsigned(m.window_extent.x<0?-m.window_extent.x:m.window_extent.x),
            unsigned(m.viewport_extent.x<0?-m.viewport_extent.x:m.viewport_extent.x)).value;
        size->cy=Win98DcMap::MultiplyDivide(unsigned(size->cy),unsigned(m.window_extent.y<0?-m.window_extent.y:m.window_extent.y),
            unsigned(m.viewport_extent.y<0?-m.viewport_extent.y:m.viewport_extent.y)).value;
    }
    return true;
}
bool GetTextExtentPoint32A(HDC handle,const char *text,int length,SIZE *size)
{return TextExtent(handle,text,length,size,0);}
bool GetTextExtentGlyphs(HDC handle,const unsigned short *text,int length,SIZE *size)
{return TextExtent(handle,reinterpret_cast<const char*>(text),length,size,0x10);}
bool GetCharWidth32A(HDC handle, unsigned first, unsigned last, int *widths)
{
    auto *c = DC(handle);
    if (!c || !(c->current.mapping.transform_flags&1) || !widths || first > last || last > 255) return false;
    if (c->device_draw) return c->device_draw->widths(c->device_context, handle, first, last, widths);
    const auto &font = Find(c->current.font, FontObject)->font;
    for (unsigned ch = first; ch <= last; ++ch) widths[ch - first] = font.Width(font.Index(ch));
    return true;
}

namespace {
// The three conversions above, pixel by pixel. Only 1, 16, 24 and 32 bits take
// part: a 4- or 8-bit DDB has no colour table in this store, and the original's
// palette path for it is not implemented here.
// mono_key: the source DC's background pixel (0x00RRGGBB) when the destination
// is monochrome. GDI's colour-to-mono transfer sets exactly the pixels of that
// colour to 1 and everything else to 0; this is how COMCTL32's image lists
// derive their masks (SetBkColor on the image DC, BitBlt into the mask).
bool ConvertDepth(const DibEng::Bitmap &source, const State &state, bool source565,
                  bool target565, int depth, unsigned char *&storage, DibEng::Bitmap &out,
                  const unsigned *mono_key = nullptr)
{
    const auto usable = [](int bits) { return bits == 1 || bits == 16 || bits == 24 || bits == 32; };
    if (!usable(source.depth) || !usable(depth) || source.width <= 0 || source.height <= 0)
        return false;
    const int stride = ((source.width * depth + 31) / 32) * 4;
    storage = new unsigned char[(DibEng::Size)stride * source.height]{};
    out = {storage, source.width, source.height, stride, depth};
    for (int y = 0; y < source.height; ++y)
        for (int x = 0; x < source.width; ++x)
        {
            unsigned value = DibEng::ReadPixel(source, x, y);
            if (source.depth == 1)
            {
                value = Win98PatternBrush::MonoColor(value, state.text_mode.foreground,
                                                     state.text_mode.background);
                if (depth != 32)
                    value = Win98DibRaster::Convert(value, 32, false, unsigned(depth), target565);
            }
            else if (depth == 1 && mono_key)
            {
                const unsigned key = Win98DibRaster::Convert(*mono_key & 0xffffff, 32, false, unsigned(source.depth), source565);
                const unsigned mask = source.depth == 16 ? (source565 ? 0xffffu : 0x7fffu) : 0xffffffu;
                value = (value & mask) == (key & mask);
            }
            else if (depth == 1)
                value = Win98DibRaster::Monochrome(value, unsigned(source.depth), source565, unsigned(x));
            else
                value = Win98DibRaster::Convert(value, unsigned(source.depth), source565,
                                                unsigned(depth), target565);
            DibEng::WritePixel(out, x, y, value);
        }
    return true;
}
} // namespace
bool BitBlt(HDC target, int x, int y, int w, int h, HDC source, int sx, int sy, unsigned rop)
{
    auto *d = DC(target); auto *s = DC(source); RECT area;
    if (!d || !Rectangle(d->current, x, y, w, h, area) || rop & 0xff000000) return false;
    // the blit family adds its target rectangle to the bounds, before
    // any clipping.
    if (d->current.bounds_flags & 0x0a) AccumulateBounds(target, area.left, area.top, area.right, area.bottom);
    BitmapAccess destination_access(Find(d->current.bitmap, BitmapObject), true);
    BitmapAccess source_access(s ? Find(s->current.bitmap, BitmapObject) : nullptr, false);
    auto &dest = Find(d->current.bitmap, BitmapObject)->bitmap;
    const bool needs_source = DibEng::UsesSource(rop);
    if (!needs_source) sx = sy = 0;
    if (needs_source && !s) return false;
    if(needs_source && (!(d->current.mapping.transform_flags&1) || !(s->current.mapping.transform_flags&1)))
        return false; // Scaled/mirrored bitmap realization is a separate raster port.
    const DibEng::Bitmap *src = needs_source ? &Find(s->current.bitmap, BitmapObject)->bitmap : nullptr;
    auto *source_object=src?Find(s->current.bitmap,BitmapObject):nullptr;
    if (src) {
        RECT source_area;
        if(!Rectangle(s->current,sx,sy,w,h,source_area) ||
           source_area.right-source_area.left!=area.right-area.left ||
           source_area.bottom-source_area.top!=area.bottom-area.top)return false;
        // The existing raster core has no StretchBlt/color-conversion port.
        // Equal transformed source/destination extents use its actual BitBlt.
        sx=source_area.left;sy=source_area.top;
    }
    // GDI: a DIB section drawn onto a DC that has none (the
    // screen, a bitmap in the format of the display) is not a blit between two
    // bitmaps of the device. GDI hands the section's bits to the driver the
    // way StretchDIBits does, so on the palettized
    // display its colours go through the DC's palette to the slots that palette
    // is realized on (BlitDib, DeviceTranslation). Raw indices would name
    // slots of the system palette that hold other colours.
    const auto *destination_object=Find(d->current.bitmap,BitmapObject);
    const bool source_section=source_object&&source_object->dib&&!source_object->dib->colours_only;
    if(src&&source_section&&dest.depth<=8&&PaletteDisplay()&&
       destination_object->dib&&destination_object->dib->colours_only)
        return BlitDib(*d,source_object->dib->format,*src,area,sx,sy,rop,nullptr,nullptr,true);
    if(src&&src->depth!=dest.depth&&(source_object->dib||
       (Find(d->current.bitmap,BitmapObject)->dib&&
        (src->depth>8||(src->depth==1&&dest.depth<=8))))){
        // A DIB source of another depth, or a direct-colour or monochrome DDB
        // going into a palettised DIB section (COMCTL32 copies LoadBitmap strips
        // and icon masks into its 4- and 8-bit image lists): BlitDib converts per
        // pixel. A monochrome DDB belongs here too, because only BlitDib knows the
        // target's colour table: DIBENG expands the mask into two
        // already realized colours, and on a palettised surface those are two
        // colour-table indices, which ConvertDepth cannot produce.
        // Direct-colour targets instead use ConvertDepth below: a mono DDB
        // has no DIB palette, so its bits must expand through the DC colours.
        const unsigned key=Pixel(s->current.text_mode.background); // back to COLORREF
        DibFormat plain;plain.depth=unsigned(src->depth);plain.width=unsigned(src->width);plain.height=unsigned(src->height);
        return BlitDib(*d,source_object->dib?source_object->dib->format:plain,*src,area,sx,sy,rop,dest.depth==1?&key:nullptr,
                       nullptr,source_section);
    }
    // A source of another colour depth without a DIB behind it. Win98's DIBENG
    // converts while it blits: expands a monochrome source with
    // the destination's text and background colours through the shifted mask's
    // carry, and the generated direct-colour paths in win98_dib_raster.h carry
    // everything else across. NEO98's raster core blits inside one depth, so
    // the source is converted into a copy of the destination's depth first and
    // the raster operation then runs unchanged over it. This is the blit the
    // original COMCTL32 image list needs: it draws every icon into a 16-bit
    // image bitmap and a monochrome mask bitmap, and puts the mask back on a
    // 32-bit window surface.
    unsigned char *converted = nullptr;
    DibEng::Bitmap converted_bitmap = {};
    if (src && src->depth != dest.depth)
    {
        const auto *destination_object = Find(d->current.bitmap, BitmapObject);
        if (!ConvertDepth(*src, d->current, false,
                          destination_object->dib && destination_object->dib->format.rgb565,
                          dest.depth, converted, converted_bitmap,
                          dest.depth == 1 ? &s->current.text_mode.background : nullptr))
        { delete[] converted; return false; }
        src = &converted_bitmap;
    }
    unsigned char *snapshot = nullptr;
    DibEng::Bitmap copy = {};
    // Multiple clip rectangles can otherwise overwrite source pixels needed by
    // a later rectangle. Also avoid the unresolved monochrome reverse emitter.
    if (src && src->bits == dest.bits && (d->current.clips > 1 || dest.depth == 1)) {
        const int stride = ((src->width * src->depth + 31) / 32) * 4;
        snapshot = new unsigned char[(DibEng::Size)stride * src->height]{};
        copy = {snapshot, src->width, src->height, stride, src->depth};
        for (int row = 0; row < src->height; ++row) for (int col = 0; col < src->width; ++col)
            DibEng::WritePixel(copy, col, row, DibEng::ReadPixel(*src, col, row));
        src = &copy;
    }
    unsigned pattern[64]; DibEng::Brush brush{};
    if (DibEng::UsesPattern(rop) &&
        !RealizeBrush(*Find(d->current.brush,BrushObject), d->current, pattern, brush))
    { delete[] snapshot; delete[] converted; return false; }
    const RECT bounds = {0, 0, dest.width, dest.height}; RECT bounded;
    bool result = true;
    if (Intersect(bounded, area, bounds)) for (unsigned i = 0; i < d->current.clips; ++i) {
        RECT clip;
        if (!Intersect(clip, bounded, ClipAt(d->current,i))) continue;
        if (!DibEng::BitBlt(dest, clip.left, clip.top, clip.right - clip.left, clip.bottom - clip.top,
                            src, sx + clip.left - area.left, sy + clip.top - area.top, rop, brush)) { result = false; break; }
    }
    delete[] snapshot;
    delete[] converted;
    return result;
}

#include "gdi_caret.inc"
#include "gdi_focus_rect.inc"
#include "gdi_region_brush.inc"
#include "gdi_paths.inc"
#include "gdi_scroll_dc.inc"
#include "gdi_census_imports.inc"

bool PatBlt(HDC dc, int x, int y, int w, int h, unsigned rop)
{
    // The operation index is the only meaningful byte for a pattern blit.
    // Negative extents cover the rectangle on the other side of the origin.
    rop &= 0x00ff0000;
    if (w < -32767 || h < -32767) return false;
    if (w < 0) { x = int(unsigned(x) + unsigned(w)); w = -w; }
    if (h < 0) { y = int(unsigned(y) + unsigned(h)); h = -h; }
    if (auto *c = DC(dc); c && c->device_draw) return c->device_draw->pat_blt(c->device_context, dc, x, y, w, h, rop);
    return !DibEng::UsesSource(rop) && BitBlt(dc, x, y, w, h, 0, 0, 0, rop);
}
bool FillRect(HDC handle, const RECT *rect, HBRUSH brush)
{
    auto *c = DC(handle); auto *b = Find(brush, BrushObject); RECT area;
    if (!c || !b || !rect || rect->right < rect->left || rect->bottom < rect->top ||
        (long long)rect->right - rect->left > 32767 || (long long)rect->bottom - rect->top > 32767 ||
        !Rectangle(c->current, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, area)) return false;
    if(b->hollow_brush)return true;
    if(c->current.bounds_flags&0x0a)AccumulateBounds(handle,area.left,area.top,area.right,area.bottom);
    if(c->device_draw){
        // USER: FillRect is PatBlt(PATCOPY) with the brush selected.
        const unsigned previous=SelectObject(handle,brush);if(!previous)return false;
        const bool done=c->device_draw->pat_blt(c->device_context,handle,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,PATCOPY);
        SelectObject(handle,previous);return done;
    }
    unsigned pattern[64]; DibEng::Brush realized;
    return RealizeBrush(*b, c->current, pattern, realized) && Paint(*c, area, PATCOPY, realized);
}
// USER and Windows 98 use the same PATBLT path as other
// drawing, including mapping, the effective clip and pattern brush origin.
bool InvertRect(HDC handle, const RECT *rect)
{
    return rect && PatBlt(handle, short(rect->left), short(rect->top),
        short(unsigned(rect->right) - unsigned(rect->left)),
        short(unsigned(rect->bottom) - unsigned(rect->top)), 0x00550009);
}
bool FrameRect(HDC handle, const RECT *rect, HBRUSH brush)
{
    auto *c = DC(handle);
    const auto *object = Find(brush, BrushObject);
    if (!c || !rect || !object) return false;
    if (object->hollow_brush) return true;
    const auto &m = c->current.mapping;
    const int dx = (m.window_extent.x ^ m.viewport_extent.x) < 0 ? -1 : 1;
    const int dy = (m.window_extent.y ^ m.viewport_extent.y) < 0 ? -1 : 1;
    const int x = short(rect->left), y = short(rect->top);
    const int w = short(unsigned(rect->right) - unsigned(rect->left));
    const int h = short(unsigned(rect->bottom) - unsigned(rect->top));
    if (!w || !h || (w ^ dx) < 0 || (h ^ dy) < 0) return true;
    const unsigned previous = SelectObject(handle, brush);
    if (!previous) return false;
    bool result = PatBlt(handle, x, y, w, dy, PATCOPY);
    result = PatBlt(handle, x, y + h - dy, w, dy, PATCOPY) && result;
    if (h * dy > 2) {
        result = PatBlt(handle, x, y + dy, dx, h - 2 * dy, PATCOPY) && result;
        result = PatBlt(handle, x + w - dx, y + dy, dx, h - 2 * dy, PATCOPY) && result;
    }
    SelectObject(handle, previous);
    return result;
}
bool TextOutA(HDC handle, int x, int y, const char *text, int length)
{
    return length >= 0 && ExtTextOutA(handle, x, y, 0, nullptr, text, length);
}
bool MappingExtents(HDC handle, SIZE &window, SIZE &viewport)
{
    const auto *c = DC(handle); if (!c) return false;
    const auto &m = c->current.mapping;
    window = {m.window_extent.x, m.window_extent.y};
    viewport = {m.viewport_extent.x, m.viewport_extent.y};
    return true;
}
static bool TextOutput(HDC handle, int x, int y, unsigned options, const RECT *rect,
                 const char *text, unsigned length, const int *advances)
{
    auto *c = DC(handle); RECT origin, opaque;
    if(c&&c->device_draw&&(options&0x10))return false;
    // The text and its opaque rectangle go into the bounds.
    if (c && (c->current.bounds_flags & 0x0a))
    {
        if (rect) AccumulateLogical(handle, rect->left, rect->top, rect->right, rect->bottom);
        SIZE size{};
        if (text && length && length <= 4096 && TextExtent(handle, text, int(length), &size,options&0x10))
            AccumulateLogical(handle, x, y, x + size.cx, y + size.cy);
    }
    if (c && c->device_draw && (text || !length) && length <= 4096 && !(options & ~6u))
        return c->device_draw->text_out(c->device_context, handle, x, y, options, rect, text, length, advances);
    if (!c || (!text && length) || length > 4096 || options & ~0x16u ||
        !Rectangle(c->current, x, y, 0, 0, origin)) return false;
    if (rect && (rect->right < rect->left || rect->bottom < rect->top ||
        (long long)rect->right - rect->left > 32767 || (long long)rect->bottom - rect->top > 32767 ||
        !Rectangle(c->current, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, opaque))) return false;
    BitmapAccess access(Find(c->current.bitmap, BitmapObject), true);
    auto &bitmap = Find(c->current.bitmap, BitmapObject)->bitmap;
    if (bitmap.depth != 32 && bitmap.depth != 24 && bitmap.depth != 16 && bitmap.depth != 8 && bitmap.depth != 1)
        return false;
    // The text colours are kept as 32-bit pixels; any other surface takes them
    // realized to its own format, the way DIBENG's ExtTextOut gets physical
    // colours from GDI (a DirectDraw surface of 16 bits).
    auto physical_text = c->current.text_mode;
    if (bitmap.depth != 32) {
        const auto &object = *Find(c->current.bitmap, BitmapObject);
        if (!RealizeDeviceColor(object, Pixel(physical_text.foreground), physical_text.foreground) ||
            !RealizeDeviceColor(object, Pixel(physical_text.background), physical_text.background)) return false;
    }
    const auto &font = Find(c->current.font, FontObject)->font;
    short *dx = nullptr;
    if (advances) {
        dx = new short[length];
        for (unsigned i = 0; i < length; ++i) dx[i] = TextDistance(c->current, advances[i], false);
    }
    // The selected physical font is realized for the mapping by the API layer.
    if(length&&(c->current.text_align&0x100)){delete[]dx;return false;}
    auto measuring=c->current.text_mode;
    unsigned extent=length?DibText::ExtTextOut(nullptr,0,0,{},(const unsigned char*)text,-int(length),font,measuring,dx,nullptr,options&0x10):0;
    if(extent==0x80000000u){delete[]dx;return false;}
    const auto current = Win98DcMap::Transform(c->current.mapping,
        {c->current.position.x,c->current.position.y},false,false);
    const auto placed=Win98GdiTextLayout::Place(c->current.text_align,{origin.left,origin.top},
        {current.x,current.y},int(extent&65535),font.Height(),font.Ascent());
    const auto *outline=GdiTrueType::FontOf(font);
    const int angle=outline?outline->escapement:0;
    const int anchor_x=(c->current.text_align&1)?current.x:origin.left;
    const int anchor_y=(c->current.text_align&1)?current.y:origin.top;
    int offset_x=placed.x-anchor_x,offset_y=placed.y-anchor_y;
    GdiTrueType::Rotate(angle,offset_x,offset_y);
    origin.left=anchor_x+offset_x;origin.top=anchor_y+offset_y;
    bool result = true;
    // A simulated italic raster font goes to the driver once per band of two
    // rows, each band moved right by (height - row - 1) / 2 and clipped to its
    // rows; the first band is open to the top of the clip, the last one to its
    // bottom. Any other font is one call.
    const bool slanted = length && Win98TextSimulation::ItalicBands(font);
    const int cell = int(font.Height());
    for (unsigned i = 0; result && i < c->current.clips; ++i) {
        const RECT clip = ClipAt(c->current, i);
        for (int row = 0; row < (slanted ? cell : 1); row += 2) {
            RECT band = clip;
            int shift = 0;
            if (slanted) {
                if (row) band.top = origin.top + row;
                if (row + 2 < cell) band.bottom = origin.top + row + 2;
                shift = Win98TextSimulation::ItalicShift(cell, row);
                if (band.top < clip.top) band.top = clip.top;
                if (band.bottom > clip.bottom) band.bottom = clip.bottom;
                if (band.top >= band.bottom) continue;
            }
            auto dm = physical_text;
            if (DibText::ExtTextOut(&bitmap, origin.left + shift, origin.top, band,
                 (const unsigned char *)text, length, font, dm, dx, rect ? &opaque : nullptr, options)) { result = false; break; }
        }
    }
    delete[] dx;
    // Underline and strikeout: GDI draws them itself after the text with a
    // PS_SOLID pen of width zero in the text colour and R2_COPYPEN, one
    // horizontal line per row, from the start of the run to its end without
    // the last pixel (GDI, Polyline ). Row and thickness
    // come from Win98TextSimulation::LineMetrics. With ETO_CLIPPED the lines
    // stay inside the rectangle.
    for(int which=0;result&&length&&font.data&&which<2;++which){
        if(!font.data[which?82:81])continue;
        int top=0,thickness=0;
        if(!Win98TextSimulation::LineMetrics(font,which!=0,top,thickness))continue;
        DibEng::Brush line{};line.color=physical_text.foreground;
        int left=origin.left+Win98TextSimulation::LineShift(font,top),right=left+int(extent&65535);
        if(!angle&&rect&&(options&4)){if(left<opaque.left)left=opaque.left;if(right>opaque.right)right=opaque.right;}
        if(left>=right)continue;
        const RECT bounds={0,0,bitmap.width,bitmap.height};
        for(int row=origin.top+top;result&&row<origin.top+top+thickness;++row){
            if(!angle&&rect&&(options&4)&&(row<opaque.top||row>=opaque.bottom))continue;
            auto bar=GdiTrueType::RotateRect(angle,{left-origin.left,row-origin.top,right-origin.left,row+1-origin.top});
            bar.left+=origin.left;bar.right+=origin.left;bar.top+=origin.top;bar.bottom+=origin.top;
            if(rect&&(options&4)){RECT clipped;if(!Intersect(clipped,bar,opaque))continue;bar=clipped;}
            RECT bounded;if(!Intersect(bounded,bar,bounds))continue;
            for(unsigned i=0;i<c->current.clips;++i){
                RECT clip;
                if(Intersect(clip,bounded,ClipAt(c->current,i))&&
                   !DibEng::BitBlt(bitmap,clip.left,clip.top,clip.right-clip.left,clip.bottom-clip.top,
                                   nullptr,0,0,0xF00021,line)){result=false;break;}
            }
        }
    }
    if(result&&length&&(c->current.text_align&1)){
        auto next=Win98GdiTextLayout::Advance(c->current.text_align,
            {current.x,current.y},int(extent&65535));
        int advance_x=next.x-current.x,advance_y=next.y-current.y;
        GdiTrueType::Rotate(angle,advance_x,advance_y);
        next.x=current.x+advance_x;next.y=current.y+advance_y;
        const auto logical = Win98DcMap::Transform(c->current.mapping,{next.x,next.y},true,false);
        c->current.position={logical.x,logical.y};
    }
    return result;
}
bool ExtTextOutA(HDC handle,int x,int y,unsigned options,const RECT *rect,const char *text,unsigned length,const int *advances)
{return !(options&~6u)&&TextOutput(handle,x,y,options,rect,text,length,advances);}
bool ExtTextOutGlyphs(HDC handle,int x,int y,unsigned options,const RECT *rect,const unsigned short *glyphs,unsigned length,const int *advances)
{return !(options&~6u)&&TextOutput(handle,x,y,options|0x10,rect,reinterpret_cast<const char*>(glyphs),length,advances);}
unsigned LiveObjects() { unsigned n = 0; for(unsigned i=0;i<Capacity;++i)n+=objects[i].kind!=Empty; return n; }
unsigned ObjectCapacity() { return IndexMask; }
void NarrowHandles(bool on) { if (on) ++narrow_handles; else if (narrow_handles) --narrow_handles; }
bool PrepareLegacyObjects(unsigned count)
{
    if (count != 2048) return false;
    for (auto *&page : objects.pages) { delete page; page = nullptr; }
    Capacity = free_object_hint = 0;
    IndexBits = 12;
    IndexMask = (1u << IndexBits) - 1;
    NarrowGenerations = (0x10000u >> IndexBits) - 1;
    while (Capacity < count) if (!objects.Grow()) return false;
    return true;
}
void *LegacyObject(unsigned index) { return index < Capacity ? &objects[index] : nullptr; }
void *LegacySavedState(void *link)
{
    auto *&head = *static_cast<SavedState **>(link);
    auto *saved = new(std::nothrow) SavedState{{}, head};
    if (!saved) return nullptr;
    head = saved;
    return &saved->state;
}
bool FinishLegacyContext(void *context, unsigned count)
{
    auto &c = *static_cast<Context *>(context);
    if (c.saves > count) return false;
    // Unused array entries held no references. Discard their copied values
    // without releasing objects that belong to the live saved levels.
    while (count > c.saves) {
        --count;
        auto *unused = c.saved;
        if (!unused) return false;
        c.saved = unused->previous;
        delete unused;
    }
    return true;
}
void *PrepareLegacyRegions(unsigned count)
{
    if (count != 384) return nullptr;
    auto *blocks = new(std::nothrow) RegionStore::Block[count];
    if (!blocks) return nullptr;
    delete[] region_store.blocks;
    region_store.blocks = blocks;
    region_store.capacity = count;
    return blocks;
}
void *PrepareLegacyBitmapRanges(unsigned count)
{
    if (count != 8192) return nullptr;
    auto *ranges = new(std::nothrow) BitmapStore::Range[count];
    if (!ranges) return nullptr;
    delete[] bitmap_store.ranges;
    bitmap_store.ranges = ranges;
    bitmap_store.range_capacity = count;
    bitmap_store.read_cache = bitmap_store.write_cache = nullptr;
    for (auto *&entry : bitmap_store.recent) entry = nullptr;
    bitmap_store.next_recent = 0;
    return ranges;
}
}
