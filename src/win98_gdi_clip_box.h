// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Original Win98 GetClipBox.
//
// GDI32!GetClipBox (ordinal 241) sets CX to 08ADh, eight argument
// bytes and FT thunk index ADh, narrows the DC to a WORD and hands the 16-bit
// side a private eight-byte WORD rectangle at EBP-48h. A null LPRECT is pushed
// unchanged, so the 16-bit worker receives the far pointer. On the
// way back the 16-bit int is sign extended, and the four WORDs are
// widened to four LONGs whenever the caller passed a rectangle - including
// after a failed call, which is why an ERROR still writes the caller's RECT.
//
// GDI.EXE GETCLIPBOX, ordinal 77, runs the DC handle gate
// and the write probe, then tail-jumps into the worker.
// The worker reads the realized clip at DC+24h for an ordinary DC, or combines
// the meta region at DC+14h with the application clip at DC+12h for a metafile
// DC, and asks the bounds routine for the rectangle and the region
// type. A NULLREGION returns 1 without converting the rectangle; every other
// result converts the two corners from device to logical units.
//
// The bounds routine copies the region record's signed WORD bounding rectangle
// at +16h and classifies : a used byte length above 42 is
// COMPLEXREGION, otherwise the band count at +12h plus one, so zero bands are
// NULLREGION and one band is SIMPLEREGION.
namespace Win98GdiClipBox {
using U = unsigned;
// The Win32 names the widened return carries.
enum Type { Error = 0, Null = 1, Simple = 2, Complex = 3 };
struct Rect { int left = 0, top = 0, right = 0, bottom = 0; };

// shared with RectVisible and PtVisible: the masked signature must lie
// in 4F4Dh..4F50h. A refusal enters HandleParamError with 6062h and the public
// entry answers zero.
inline bool DcGate(U signature) { return signature - 0x4f4d <= 3u; }
constexpr U GateError = 0x6062;

// `used` is the region record's byte length at +0Eh and `bands` its
// band count at +12h.
inline U Classify(U used, U bands) { return used > 42 ? U(Complex) : bands + 1; }

// the rectangle always comes out of the region record, whatever the
// classification says.
template <class E> U Bounds(E &e, U region, Rect &out)
{
    e.RegionBounds(region, out);
    return Classify(e.RegionBytes(region), e.RegionBands(region));
}

// The worker. `out` is the eight-byte WORD rectangle the thunk owns.
template <class E> U Worker(E &e, Rect &out)
{
    // DC+0Eh bit 2 means the visible region is stale, and the USER DC
    // hook realizes it before anything is read.
    if (e.VisibleInvalid()) e.RealizeVisible();
    if (!e.MetafileObject()) {
        // An ordinary DC stores the intersection of the visible, meta
        // and application clipping regions.
        const U type = Bounds(e, e.RealizedClip(), out);
        if (type == Null) return Null;  // Windows 98 leaves the rectangle in device units.
        e.DeviceToLogical(out);
        return type;
    }
    // For a metafile DC, choose the clipping region according to
    // whether the meta and application regions are present.
    const U meta = e.MetaRegion(), clip = e.ApplicationClip();
    if (!meta && !clip) {
        // the whole device surface, always SIMPLEREGION, and the
        // conversion still runs.
        out = Rect{0, 0, e.DeviceWidth(), e.DeviceHeight()};
        e.DeviceToLogical(out);
        return Simple;
    }
    U type = 0;
    if (meta && clip) {
        const U scratch = e.CreateEmptyRegion();
        if (!scratch) return Error;  // Windows 98 leaves the caller's rectangle alone.
        e.CombineRegions(scratch, clip, meta);
        type = Bounds(e, scratch, out);
        e.DeleteRegion(scratch);
    } else type = Bounds(e, meta ? meta : clip, out);
    if (type == Null) return Null;
    e.DeviceToLogical(out);
    return type;
}

// The gate and the probe run before the worker; a rejected DC returns
// ERROR without touching the rectangle.
// A null LPRECT is not filtered anywhere: GDI32 pushes it unchanged and the
// probe touches ES:0000. The native adapter answers ERROR instead of
// faulting, the same decision RectVisible already takes.
template <class E> U Body(E &e, U signature, bool rectangle, Rect &out)
{
    if (!DcGate(signature)) { e.ParameterError(GateError); return Error; }
    if (!rectangle) return Error;
    return Worker(e, out);
}
}
