/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DDRAW.DLL for ARM64 processes: IDirectDrawPalette, the 256 colours of an
   eight bit surface. On the primary surface they are the colour table of the
   display. */
#include "ddraw_private.h"

static DdPalette *This(void *face) { return (DdPalette *)((DdFace *)face)->object; }

DdPalette *DdPaletteOf(void *face) { return face ? This(face) : NULL; }
void DdPaletteAddRef(DdPalette *palette) { DdAddRef(&palette->references); }

void DdPaletteRelease(DdPalette *palette)
{
    if (DdDropRef(&palette->references) > 0) return;
    free(palette);
}

void DdPaletteAttach(DdPalette *palette, DdSurface *surface, BOOL attach)
{
    unsigned i;
    for (i = 0; i < 8; ++i)
        if (palette->surfaces[i] == (attach ? NULL : surface))
        {
            palette->surfaces[i] = attach ? surface : NULL;
            return;
        }
}

static HRESULT WINAPI Palette_QueryInterface(void *face, REFIID iid, void **out)
{
    DD_METHOD_LOCK();
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (!iid || (!IsEqualGUID(iid, &dd_iid_unknown) && !IsEqualGUID(iid, &IID_IDirectDrawPalette))) return E_NOINTERFACE;
    DdPaletteAddRef(This(face));
    *out = face;
    return S_OK;
}

static ULONG WINAPI Palette_AddRef(void *face) {
    DD_METHOD_LOCK(); return (ULONG)DdAddRef(&This(face)->references); }

static ULONG WINAPI Palette_Release(void *face)
{
    DD_METHOD_LOCK();
    DdPalette *palette = This(face);
    const LONG left = palette->references - 1;
    DdPaletteRelease(palette);
    return left > 0 ? (ULONG)left : 0;
}

static HRESULT WINAPI Palette_GetCaps(void *face, LPDWORD caps)
{
    DD_METHOD_LOCK();
    DdPalette *palette = This(face);
    unsigned i;
    if (!caps) return DDERR_INVALIDPARAMS;
    *caps = palette->caps;
    for (i = 0; i < 8; ++i)
        if (palette->surfaces[i] && (palette->surfaces[i]->caps & DDSCAPS_PRIMARYSURFACE)) *caps |= DDPCAPS_PRIMARYSURFACE;
    return DD_OK;
}

static HRESULT WINAPI Palette_GetEntries(void *face, DWORD flags, DWORD start, DWORD count, LPPALETTEENTRY out)
{
    DD_METHOD_LOCK();
    DdPalette *palette = This(face);
    if (flags || !out || start >= palette->count || count > palette->count - start) return DDERR_INVALIDPARAMS;
    memcpy(out, palette->entries + start, count * sizeof(PALETTEENTRY));
    return DD_OK;
}

static HRESULT WINAPI Palette_Initialize(void *face, LPDIRECTDRAW dd, DWORD flags, LPPALETTEENTRY colors)
{
    DD_METHOD_LOCK();
    (void)face;
    (void)dd;
    (void)flags;
    (void)colors;
    return DDERR_ALREADYINITIALIZED;
}

static HRESULT WINAPI Palette_SetEntries(void *face, DWORD flags, DWORD start, DWORD count, LPPALETTEENTRY in)
{
    DD_METHOD_LOCK();
    DdPalette *palette = This(face);
    unsigned i;
    if (flags || !in || start >= palette->count || count > palette->count - start) return DDERR_INVALIDPARAMS;
    /* Without DDPCAPS_ALLOW256 the first and the last entry stay black and
       white, the two colours Windows keeps for itself. */
    for (i = 0; i < count; ++i)
    {
        const DWORD at = start + i;
        if (palette->count == 256 && !(palette->caps & DDPCAPS_ALLOW256) && (at == 0 || at == 255)) continue;
        palette->entries[at] = in[i];
    }
    for (i = 0; i < 8; ++i)
        if (palette->surfaces[i]) DdSurfacePaletteChanged(palette->surfaces[i], start, count);
    return DD_OK;
}

static const struct IDirectDrawPaletteVtbl palette_table = {
    .QueryInterface = (void *)Palette_QueryInterface, .AddRef = (void *)Palette_AddRef, .Release = (void *)Palette_Release,
    .GetCaps = (void *)Palette_GetCaps, .GetEntries = (void *)Palette_GetEntries, .Initialize = (void *)Palette_Initialize,
    .SetEntries = (void *)Palette_SetEntries};

/* A palette of 2, 4, 16 or 256 entries. Surfaces here have eight bit indices
   or none, so only a palette of 256 finds a surface to go on; the smaller
   ones exist as objects, as with the original. Entries that are themselves
   indices (DDPCAPS_8BITENTRIES) belong to surface formats nobody makes here. */
HRESULT DdCreatePalette(DWORD flags, const PALETTEENTRY *colors, DdPalette **out)
{
    const DWORD depth = flags & (DDPCAPS_1BIT | DDPCAPS_2BIT | DDPCAPS_4BIT | DDPCAPS_8BIT);
    DdPalette *palette;
    DWORD count;
    if (flags & DDPCAPS_8BITENTRIES) return DDERR_UNSUPPORTED;
    if (!colors) return DDERR_INVALIDPARAMS;
    if (depth == DDPCAPS_8BIT) count = 256;
    else if (depth == DDPCAPS_4BIT) count = 16;
    else if (depth == DDPCAPS_2BIT) count = 4;
    else if (depth == DDPCAPS_1BIT) count = 2;
    else return DDERR_INVALIDPARAMS;
    if (flags & ~(depth | DDPCAPS_ALLOW256 | DDPCAPS_INITIALIZE | DDPCAPS_ALPHA | DDPCAPS_VSYNC)) return DDERR_INVALIDPARAMS;
    palette = (DdPalette *)DdAllocate(sizeof *palette);
    if (!palette) return DDERR_OUTOFMEMORY;
    palette->face.table = &palette_table;
    palette->face.object = palette;
    palette->references = 1;
    palette->caps = flags & (depth | DDPCAPS_ALLOW256 | DDPCAPS_ALPHA | DDPCAPS_VSYNC);
    palette->count = count;
    memcpy(palette->entries, colors, count * sizeof(PALETTEENTRY));
    if (count == 256 && !(flags & DDPCAPS_ALLOW256))
    {
        memset(&palette->entries[0], 0, sizeof(PALETTEENTRY));
        memset(&palette->entries[255], 0xff, 3);
        palette->entries[255].peFlags = 0;
    }
    *out = palette;
    return DD_OK;
}
