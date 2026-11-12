/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DDRAW.DLL for ARM64 processes: the DirectDraw object behind IDirectDraw,
   IDirectDraw2, IDirectDraw4 and IDirectDraw7. */
#include "ddraw_private.h"

static DdObject *dd_objects;

static DdObject *This(void *face) { return (DdObject *)((DdFace *)face)->object; }
static unsigned Version(void *face) { return ((DdFace *)face)->version; }

void DdObjectAddRef(DdObject *dd) { DdAddRef(&dd->references); }

/* ----------------------------------------------------------- exclusive mode */
/* The window of a program in exclusive mode. DirectDraw watches it: when the
   program loses the foreground it gives the screen back (the window goes to
   the task bar, the mode of the desktop returns, the exclusive mode is free
   for the next program), and when it comes back it takes the screen again.
   The surfaces of the driver are lost in between, and the program restores
   them. The original DDRAW does the same from the window procedure it hooks. */
static LRESULT CALLBACK ExclusiveWindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

static DdObject *ObjectOfWindow(HWND window)
{
    DdObject *dd;
    for (dd = dd_objects; dd; dd = dd->next)
        if (dd->window == window && dd->previous_procedure) return dd;
    return NULL;
}

static void HookWindow(DdObject *dd)
{
    if (!dd->window || dd->previous_procedure || (dd->level & DDSCL_NOWINDOWCHANGES)) return;
    dd->previous_procedure = (WNDPROC)SetWindowLongPtrA(dd->window, GWLP_WNDPROC, (LONG_PTR)ExclusiveWindowProcedure);
}

static void UnhookWindow(DdObject *dd)
{
    if (!dd->previous_procedure) return;
    if (IsWindow(dd->window) && (WNDPROC)GetWindowLongPtrA(dd->window, GWLP_WNDPROC) == ExclusiveWindowProcedure)
        SetWindowLongPtrA(dd->window, GWLP_WNDPROC, (LONG_PTR)dd->previous_procedure);
    dd->previous_procedure = NULL;
}

/* The window covers the screen of the mode. */
static void CoverScreen(DdObject *dd)
{
    NEO98DX_INFO info;
    if (!dd->window || (dd->level & DDSCL_NOWINDOWCHANGES) || !DdDisplayInfo(&info)) return;
    SetWindowPos(dd->window, HWND_TOP, 0, 0, (int)info.width, (int)info.height, SWP_NOACTIVATE);
}

static void LeaveExclusive(DdObject *dd, BOOL restore_mode)
{
    if (restore_mode && dd->mode_set)
    {
        DdFlush3D();
        DdDrvRestoreMode();
    }
    if (dd->exclusive) DdDrvSetExclusive(dd->window, FALSE);
    dd->exclusive = FALSE;
}

static LRESULT CALLBACK ExclusiveWindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    DdObject *dd = ObjectOfWindow(window);
    WNDPROC previous = dd ? dd->previous_procedure : NULL;
    if (!previous) return DefWindowProcA(window, message, wparam, lparam);
    if (message == WM_ACTIVATEAPP && (dd->level & DDSCL_EXCLUSIVE))
    {
        if (!wparam && dd->exclusive)
        {
            dd->inactive = TRUE;
            LeaveExclusive(dd, TRUE);
            ShowWindow(window, SW_MINIMIZE);
        }
        else if (wparam && dd->inactive)
        {
            if (DdDrvSetExclusive(window, TRUE) == DD_OK)
            {
                dd->exclusive = TRUE;
                dd->inactive = FALSE;
                DdFlush3D();
                if (dd->mode_set) DdDrvSetMode(dd->mode_width, dd->mode_height, dd->mode_bits);
                ShowWindow(window, SW_RESTORE);
                CoverScreen(dd);
            }
        }
    }
    else if (message == WM_NCDESTROY)
    {
        /* The window goes; the object keeps working without it. */
        LRESULT result;
        dd->previous_procedure = NULL;
        SetWindowLongPtrA(window, GWLP_WNDPROC, (LONG_PTR)previous);
        result = CallWindowProcA(previous, window, message, wparam, lparam);
        LeaveExclusive(dd, TRUE);
        return result;
    }
    return CallWindowProcA(previous, window, message, wparam, lparam);
}

/* ------------------------------------------------------------------ IUnknown */
static HRESULT WINAPI Dd_QueryInterface(void *face, REFIID iid, void **out)
{
    DD_METHOD_LOCK();
    DdObject *dd = This(face);
    int version = -1;
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (!iid) return DDERR_INVALIDPARAMS;
    if (IsEqualGUID(iid, &dd_iid_unknown) || IsEqualGUID(iid, &IID_IDirectDraw)) version = DD_V1;
    else if (IsEqualGUID(iid, &IID_IDirectDraw2)) version = DD_V2;
    else if (IsEqualGUID(iid, &IID_IDirectDraw4)) version = DD_V4;
    else if (IsEqualGUID(iid, &IID_IDirectDraw7)) version = DD_V7;
    if (version < 0) return DdQueryExtension(dd_extensions, dd, iid, out);
    DdAddRef(&dd->face_references[version]);
    DdObjectAddRef(dd);
    *out = &dd->face[version];
    return S_OK;
}

static ULONG WINAPI Dd_AddRef(void *face)
{
    DD_METHOD_LOCK();
    DdObject *dd = This(face);
    DdObjectAddRef(dd);
    return (ULONG)DdAddRef(&dd->face_references[Version(face)]);
}

void DdObjectRelease(DdObject *dd)
{
    DdObject **link;
    if (DdDropRef(&dd->references) > 0) return;
    /* Surfaces a program still holds outlive the object as dead surfaces. */
    while (dd->surfaces) DdSurfaceOrphan(dd->surfaces);
    UnhookWindow(dd);
    LeaveExclusive(dd, TRUE);
    DdLock();
    for (link = &dd_objects; *link; link = &(*link)->next)
        if (*link == dd) { *link = dd->next; break; }
    DdUnlock();
    free(dd);
}

static ULONG WINAPI Dd_Release(void *face)
{
    DD_METHOD_LOCK();
    DdObject *dd = This(face);
    LONG left;
    /* A Release beyond the references of this interface does nothing. */
    if (dd->face_references[Version(face)] <= 0) return 0;
    left = DdDropRef(&dd->face_references[Version(face)]);
    DdObjectRelease(dd);
    return (ULONG)left;
}

void DdObjectForgetSurface(DdObject *dd, DdSurface *surface)
{
    DdSurface **link;
    for (link = &dd->surfaces; *link; link = &(*link)->next)
        if (*link == surface) { *link = surface->next; break; }
    surface->next = NULL;
    if (dd->primary == surface) dd->primary = NULL;
}

/* ------------------------------------------------------------------ creation */
/* Compact packs the video memory, which DirectDraw only lets the owner of the
   screen do; there is nothing to pack here. */
static HRESULT WINAPI Dd_Compact(void *face)
{
    DD_METHOD_LOCK();
    DdObject *dd = This(face);
    return (dd->level & DDSCL_EXCLUSIVE) && dd->exclusive ? DD_OK : DDERR_NOEXCLUSIVEMODE;
}

static HRESULT WINAPI Dd_CreateClipper(void *face, DWORD flags, LPDIRECTDRAWCLIPPER *out, IUnknown *outer)
{
    DD_METHOD_LOCK();
    (void)face;
    return DirectDrawCreateClipper(flags, out, outer);
}

static HRESULT WINAPI Dd_CreatePalette(void *face, DWORD flags, LPPALETTEENTRY colors, LPDIRECTDRAWPALETTE *out, IUnknown *outer)
{
    DD_METHOD_LOCK();
    DdPalette *palette = NULL;
    HRESULT result;
    (void)face;
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    result = DdCreatePalette(flags, colors, &palette);
    if (FAILED(result)) return result;
    *out = (LPDIRECTDRAWPALETTE)&palette->face;
    return DD_OK;
}

/* The description an interface takes: IDirectDraw and IDirectDraw2 the short
   one, IDirectDraw4 and 7 the long one, and neither the other (measured
   against the original). */
static DWORD DescriptionSize(unsigned version) { return version >= DD_V4 ? sizeof(DDSURFACEDESC2) : sizeof(DDSURFACEDESC); }

/* A description as a DDSURFACEDESC2. The two agree up to the pixel format;
   the short one ends with a DDSCAPS. */
static BOOL WidenDescription(const void *in, unsigned version, DDSURFACEDESC2 *out)
{
    const DDSURFACEDESC *old = (const DDSURFACEDESC *)in;
    memset(out, 0, sizeof *out);
    if (!in || old->dwSize != DescriptionSize(version)) return FALSE;
    if (old->dwSize == sizeof(DDSURFACEDESC2))
    {
        memcpy(out, in, sizeof *out);
        return TRUE;
    }
    memcpy(out, old, offsetof(DDSURFACEDESC, ddsCaps));
    out->dwSize = sizeof *out;
    out->ddsCaps.dwCaps = old->ddsCaps.dwCaps;
    /* dwZBufferBitDepth of the old description is a field of the union with
       the mip count; the pixel format says it in the new one. */
    if ((old->dwFlags & DDSD_ZBUFFERBITDEPTH) && !(old->dwFlags & DDSD_PIXELFORMAT))
    {
        memset(&out->ddpfPixelFormat, 0, sizeof out->ddpfPixelFormat);
        out->ddpfPixelFormat.dwSize = sizeof out->ddpfPixelFormat;
        out->ddpfPixelFormat.dwFlags = DDPF_ZBUFFER;
        out->ddpfPixelFormat.dwZBufferBitDepth = old->dwZBufferBitDepth;
        out->ddpfPixelFormat.dwZBitMask = old->dwZBufferBitDepth >= 32 ? 0xffffffffu : (1u << old->dwZBufferBitDepth) - 1;
        out->dwFlags = (out->dwFlags & ~DDSD_ZBUFFERBITDEPTH) | DDSD_PIXELFORMAT;
        out->dwMipMapCount = 0;
    }
    return TRUE;
}

static unsigned SurfaceVersion(unsigned version)
{
    return version == DD_V7 ? DDS_V7 : version == DD_V4 ? DDS_V4 : DDS_V1;
}

static HRESULT WINAPI Dd_CreateSurface(void *face, void *description, void **out, IUnknown *outer)
{
    DD_METHOD_LOCK();
    DdObject *dd = This(face);
    DDSURFACEDESC2 desc;
    DdSurface *surface = NULL;
    HRESULT result;
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    if (!WidenDescription(description, Version(face), &desc)) return DDERR_INVALIDPARAMS;
    if (!dd->level) return DDERR_NOCOOPERATIVELEVELSET;
    result = DdCreateSurfaceTree(dd, &desc, SurfaceVersion(Version(face)), Version(face), &surface);
    if (FAILED(result)) return result;
    *out = DdSurfaceFace(surface, SurfaceVersion(Version(face)));
    return DD_OK;
}

static HRESULT WINAPI Dd_DuplicateSurface(void *face, void *original, void **out)
{
    DD_METHOD_LOCK();
    DdSurface *copy = NULL;
    HRESULT result;
    if (!out || !original) return DDERR_INVALIDPARAMS;
    *out = NULL;
    result = DdSurfaceDuplicate(DdSurfaceOf(original), SurfaceVersion(Version(face)), Version(face), &copy);
    if (FAILED(result)) return result;
    *out = DdSurfaceFace(copy, SurfaceVersion(Version(face)));
    return DD_OK;
}

/* ------------------------------------------------------------- enumerations */
typedef HRESULT (WINAPI *ModeCallback)(void *description, LPVOID context);

static HRESULT WINAPI Dd_EnumDisplayModes(void *face, DWORD flags, void *filter, LPVOID context, ModeCallback callback)
{
    DD_METHOD_LOCK();
    const BOOL wide = Version(face) >= DD_V4;
    DDSURFACEDESC2 want;
    NEO98DX_INFO info;
    DWORD index;
    if (!callback || (flags & ~(DDEDM_REFRESHRATES | DDEDM_STANDARDVGAMODES))) return DDERR_INVALIDPARAMS;
    if (filter && !WidenDescription(filter, Version(face), &want)) return DDERR_INVALIDPARAMS;
    if (!DdDisplayInfo(&info)) return DDERR_GENERIC;
    for (index = 0; index < info.modeCount; ++index)
    {
        NEO98DX_MODE mode;
        DDSURFACEDESC2 desc;
        DDSURFACEDESC narrow;
        if (DdDrvGetMode(index, &mode, sizeof mode) != DD_OK) continue;
        if (filter)
        {
            if ((want.dwFlags & DDSD_WIDTH) && want.dwWidth != mode.width) continue;
            if ((want.dwFlags & DDSD_HEIGHT) && want.dwHeight != mode.height) continue;
            if ((want.dwFlags & DDSD_PIXELFORMAT) && want.ddpfPixelFormat.dwRGBBitCount != mode.bits) continue;
            if ((want.dwFlags & DDSD_REFRESHRATE) && want.dwRefreshRate && want.dwRefreshRate != mode.refresh) continue;
        }
        memset(&desc, 0, sizeof desc);
        desc.dwSize = sizeof desc;
        desc.dwFlags = DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_REFRESHRATE;
        desc.dwWidth = mode.width;
        desc.dwHeight = mode.height;
        desc.lPitch = (LONG)mode.pitch;
        desc.dwRefreshRate = (flags & DDEDM_REFRESHRATES) ? mode.refresh : 0;
        DdFormatToPixelFormat(&mode.format, &desc.ddpfPixelFormat);
        if (wide)
        {
            if (callback(&desc, context) == DDENUMRET_CANCEL) break;
            continue;
        }
        memset(&narrow, 0, sizeof narrow);
        memcpy(&narrow, &desc, offsetof(DDSURFACEDESC, ddsCaps));
        narrow.dwSize = sizeof narrow;
        if (callback(&narrow, context) == DDENUMRET_CANCEL) break;
    }
    return DD_OK;
}

typedef HRESULT (WINAPI *SurfaceCallback)(void *surface, void *description, LPVOID context);

/* CANBECREATED describes combinations of mode sizes and formats. It does
   not allocate surfaces: the original passes NULL as the first argument.
   DDSD_PIXELFORMAT requests the mode formats, including duplicates; without
   it the current format is used. Width/height fields fix those dimensions.
   The original ignores CANCEL in this enumeration (unlike DOESEXIST). */
static HRESULT EnumCreatable(void *face, DWORD flags, void *filter, LPVOID context, SurfaceCallback callback)
{
    DDSURFACEDESC2 want, desc, input;
    NEO98DX_INFO info;
    NEO98DX_MODE last_mode;
    DWORD i, j, k, width, height;
    if ((flags & ~(DDENUMSURFACES_CANBECREATED | DDENUMSURFACES_DOESEXIST | DDENUMSURFACES_MATCH)) ||
        !(flags & DDENUMSURFACES_MATCH) || !WidenDescription(filter, Version(face), &want)) return DDERR_INVALIDPARAMS;
    if (!DdDisplayInfo(&info)) return DDERR_GENERIC;
    if (!info.modeCount || DdDrvGetMode(info.modeCount - 1, &last_mode, sizeof last_mode) != DD_OK) return DDERR_GENERIC;
    for (i = 0; i < info.modeCount; ++i)
    {
        NEO98DX_MODE mode;
        if (DdDrvGetMode(i, &mode, sizeof mode) != DD_OK) continue;
        width = want.dwFlags & DDSD_WIDTH ? want.dwWidth : mode.width;
        height = want.dwFlags & DDSD_HEIGHT ? want.dwHeight : mode.height;
        /* A size occurs once, even when several display depths share it. */
        for (j = 0; j < i; ++j)
        {
            NEO98DX_MODE earlier;
            if (DdDrvGetMode(j, &earlier, sizeof earlier) != DD_OK) continue;
            if ((want.dwFlags & DDSD_WIDTH ? want.dwWidth : earlier.width) == width &&
                (want.dwFlags & DDSD_HEIGHT ? want.dwHeight : earlier.height) == height) break;
        }
        if (j < i) continue;
        for (k = 0; k < (want.dwFlags & DDSD_PIXELFORMAT ? info.modeCount : 1); ++k)
        {
            NEO98DX_FORMAT format = info.format;
            DDSURFACEDESC narrow;
            DWORD caps = want.ddsCaps.dwCaps;
            BOOL system = (caps & DDSCAPS_SYSTEMMEMORY) != 0;
            if (want.dwFlags & DDSD_PIXELFORMAT)
            {
                if (DdDrvGetMode(k, &mode, sizeof mode) != DD_OK) continue;
                format = mode.format;
            }
            memset(&desc, 0, sizeof desc);
            desc.dwSize = sizeof desc;
            desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT;
            desc.dwWidth = width; desc.dwHeight = height;
            desc.ddsCaps = want.ddsCaps;
            DdFormatToPixelFormat(&format, &desc.ddpfPixelFormat);
            /* DDRAW also changes the caller's description before invoking
               it. Without format enumeration, its format scan has already
               reached the final mode, even though the output uses the
               current display format. */
            input = want;
            input.dwFlags |= DDSD_WIDTH | DDSD_HEIGHT;
            input.dwWidth = width; input.dwHeight = height;
            DdFormatToPixelFormat(want.dwFlags & DDSD_PIXELFORMAT ? &format : &last_mode.format, &input.ddpfPixelFormat);
            input.dwSize = DescriptionSize(Version(face));
            memcpy(filter, &input, input.dwSize);
            /* The HAL accepts these display formats in video memory except
               RGB24. DDRAW can fall back to its HEL for a plain offscreen
               surface. Generic system-memory surfaces require the current
               display format; offscreen surfaces and textures do not. */
            if (caps & (DDSCAPS_PRIMARYSURFACE | DDSCAPS_OVERLAY | DDSCAPS_BACKBUFFER | DDSCAPS_FRONTBUFFER | DDSCAPS_ZBUFFER)) continue;
            if (system && (caps & DDSCAPS_VIDEOMEMORY)) continue;
            if (!system && format.bits == 24)
            {
                if ((caps & DDSCAPS_VIDEOMEMORY) || !(caps & DDSCAPS_OFFSCREENPLAIN)) continue;
                system = TRUE;
            }
            if (system && !(caps & (DDSCAPS_OFFSCREENPLAIN | DDSCAPS_TEXTURE)) &&
                memcmp(&format, &info.format, sizeof format)) continue;
            desc.ddsCaps.dwCaps = (caps & ~(DDSCAPS_SYSTEMMEMORY | DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM | DDSCAPS_NONLOCALVIDMEM)) |
                                 (system ? DDSCAPS_SYSTEMMEMORY : DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM);
            {
                DWORD alignment = system ? 8 : 16;
                desc.lPitch = (LONG)((width * ((format.bits + 7) / 8) + alignment - 1) & ~(alignment - 1));
            }
            if (Version(face) >= DD_V4) callback(NULL, &desc, context);
            else
            {
                memcpy(&narrow, &desc, sizeof narrow);
                narrow.dwSize = sizeof narrow;
                callback(NULL, &narrow, context);
            }
        }
    }
    return DD_OK;
}

/* The surfaces that exist, or descriptions of surfaces that can be made. */
static HRESULT WINAPI Dd_EnumSurfaces(void *face, DWORD flags, void *filter, LPVOID context, SurfaceCallback callback)
{
    DD_METHOD_LOCK();
    DdObject *dd = This(face);
    const unsigned version = SurfaceVersion(Version(face));
    DDSURFACEDESC2 want;
    DdSurface *list[256], *surface;
    unsigned count = 0, i;
    if (!callback) return DDERR_INVALIDPARAMS;
    if (flags & DDENUMSURFACES_CANBECREATED) return EnumCreatable(face, flags, filter, context, callback);
    if (!(flags & DDENUMSURFACES_DOESEXIST)) flags |= DDENUMSURFACES_DOESEXIST;
    if (!(flags & (DDENUMSURFACES_ALL | DDENUMSURFACES_MATCH | DDENUMSURFACES_NOMATCH))) return DDERR_INVALIDPARAMS;
    if (!(flags & DDENUMSURFACES_ALL) && !WidenDescription(filter, Version(face), &want)) return DDERR_INVALIDPARAMS;
    for (surface = dd->surfaces; surface && count < 256; surface = surface->next)
    {
        if (surface->owner) continue;   /* an implicit surface is reached through its owner */
        if (!(flags & DDENUMSURFACES_ALL))
        {
            BOOL match = TRUE;
            if ((want.dwFlags & DDSD_CAPS) && (surface->caps & want.ddsCaps.dwCaps) != want.ddsCaps.dwCaps) match = FALSE;
            if ((want.dwFlags & DDSD_WIDTH) && want.dwWidth != surface->width) match = FALSE;
            if ((want.dwFlags & DDSD_HEIGHT) && want.dwHeight != surface->height) match = FALSE;
            if (match != ((flags & DDENUMSURFACES_MATCH) != 0)) continue;
        }
        list[count++] = surface;
    }
    for (i = 0; i < count; ++i) DdSurfaceAddRef(list[i]);
    for (i = 0; i < count; ++i)
    {
        DDSURFACEDESC2 desc;
        memset(&desc, 0, sizeof desc);
        DdSurfaceDescribe(list[i], &desc, version >= DDS_V4 ? sizeof(DDSURFACEDESC2) : sizeof(DDSURFACEDESC));
        if (callback(DdSurfaceFace(list[i], version), &desc, context) == DDENUMRET_CANCEL)
        {
            /* The callback owns the references it was given; the rest go back. */
            for (++i; i < count; ++i) DdSurfaceRelease(list[i]);
            break;
        }
    }
    return DD_OK;
}

/* ------------------------------------------------------------------ the device */
static HRESULT WINAPI Dd_FlipToGDISurface(void *face)
{
    DD_METHOD_LOCK();
    DdObject *dd = This(face);
    DdSurface *primary = dd->primary, *walk;
    DdAttachment *a;
    DWORD chain[16];
    unsigned count = 0;
    if (!primary) return DDERR_NOTFOUND;
    /* The chain of the primary surface, in the order of its flips. */
    for (walk = primary; walk && count < 16; )
    {
        DdSurface *next = NULL;
        if (!walk->store->driver) break;
        chain[count++] = walk->store->driver;
        for (a = walk->attached; a && !next; a = a->next)
            if ((a->surface->caps & DDSCAPS_FLIP) && a->surface != primary && a->implicit) next = a->surface;
        walk = next;
    }
    if (count < 2) return DD_OK;
    return DdDrvFlip(chain, count, NEO98DX_FLIP_TOGDI);
}

/* What this driver and this emulation layer do. The driver blits, fills,
   stretches, mirrors and keys between surfaces of equal pixel size, flips in
   exclusive mode and keeps a gamma ramp; the emulation layer does the same
   between surfaces in system memory. Overlays, alpha blits, rotation and
   raster operations beyond SRCCOPY, BLACKNESS and WHITENESS are not offered. */
static void FillCaps(DDCAPS_DX7 *caps, const NEO98DX_INFO *info, BOOL driver)
{
    const DWORD blt = DDCAPS_BLT | DDCAPS_BLTCOLORFILL | DDCAPS_BLTSTRETCH | DDCAPS_COLORKEY | DDCAPS_CANCLIP | DDCAPS_CANCLIPSTRETCHED;
    const DWORD fx = DDFXCAPS_BLTMIRRORLEFTRIGHT | DDFXCAPS_BLTMIRRORUPDOWN | DDFXCAPS_BLTSHRINKX | DDFXCAPS_BLTSHRINKXN |
                     DDFXCAPS_BLTSHRINKY | DDFXCAPS_BLTSHRINKYN | DDFXCAPS_BLTSTRETCHX | DDFXCAPS_BLTSTRETCHXN |
                     DDFXCAPS_BLTSTRETCHY | DDFXCAPS_BLTSTRETCHYN;
    const DWORD keys = DDCKEYCAPS_SRCBLT | DDCKEYCAPS_DESTBLT;
    const unsigned codes[3] = {0xcc, 0x00, 0xff};   /* SRCCOPY, BLACKNESS, WHITENESS */
    DWORD rops[DD_ROP_SPACE];
    unsigned i;
    memset(caps, 0, sizeof *caps);
    memset(rops, 0, sizeof rops);
    for (i = 0; i < 3; ++i) rops[codes[i] / 32] |= 1u << (codes[i] % 32);
    caps->dwSize = sizeof *caps;
    caps->dwCaps = blt | DDCAPS_BLTDEPTHFILL | DDCAPS_GDI | DDCAPS_PALETTE;
    caps->dwCKeyCaps = keys;
    caps->dwFXCaps = fx;
    caps->dwPalCaps = DDPCAPS_8BIT | DDPCAPS_ALLOW256 | DDPCAPS_PRIMARYSURFACE;
    memcpy(caps->dwRops, rops, sizeof rops);
    caps->dwSSBCaps = blt;
    caps->dwSSBCKeyCaps = keys;
    caps->dwSSBFXCaps = fx;
    memcpy(caps->dwSSBRops, rops, sizeof rops);
    caps->ddsCaps.dwCaps = DDSCAPS_BACKBUFFER | DDSCAPS_COMPLEX | DDSCAPS_FLIP | DDSCAPS_FRONTBUFFER | DDSCAPS_OFFSCREENPLAIN |
                           DDSCAPS_PALETTE | DDSCAPS_PRIMARYSURFACE | DDSCAPS_SYSTEMMEMORY | DDSCAPS_VISIBLE;
    if (driver)
    {
        caps->dwCaps |= DDCAPS_CANBLTSYSMEM | DDCAPS_READSCANLINE | ((info->caps & NEO98DX_CAP_3D) ? DDCAPS_3D : 0);
        caps->dwCaps2 = DDCAPS2_WIDESURFACES | DDCAPS2_PRIMARYGAMMA | DDCAPS2_NOPAGELOCKREQUIRED |
                        ((info->caps & NEO98DX_CAP_3D) ? DDCAPS2_CANRENDERWINDOWED : 0);
        caps->dwVidMemTotal = info->videoMemoryTotal;
        caps->dwVidMemFree = info->videoMemoryFree;
        caps->dwSVBCaps = caps->dwVSBCaps = blt;
        caps->dwSVBCKeyCaps = caps->dwVSBCKeyCaps = keys;
        caps->dwSVBFXCaps = caps->dwVSBFXCaps = fx;
        memcpy(caps->dwSVBRops, rops, sizeof rops);
        memcpy(caps->dwVSBRops, rops, sizeof rops);
        caps->ddsCaps.dwCaps |= DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM;
        if (info->caps & NEO98DX_CAP_3D)
        {
            caps->dwZBufferBitDepths = DDBD_16 | DDBD_32;
            caps->ddsCaps.dwCaps |= DDSCAPS_3DDEVICE | DDSCAPS_TEXTURE | DDSCAPS_ZBUFFER | DDSCAPS_MIPMAP;
        }
    }
    else
    {
        caps->dwCaps2 = DDCAPS2_WIDESURFACES | DDCAPS2_NOPAGELOCKREQUIRED;
        caps->dwZBufferBitDepths = DDBD_16 | DDBD_32;
        caps->ddsCaps.dwCaps |= DDSCAPS_TEXTURE | DDSCAPS_ZBUFFER | DDSCAPS_MIPMAP;
    }
    caps->ddsOldCaps.dwCaps = caps->ddsCaps.dwCaps;
}

/* A caller says with dwSize which version of the structure it has room for. */
static HRESULT CopyCaps(const DDCAPS_DX7 *full, void *out)
{
    const DWORD size = ((const DDCAPS_DX7 *)out)->dwSize;
    /* The structures of DirectX 3, 5 and 7, and the one of the first
       DirectDraw, which ends before the caps of the system memory blits. */
    if (size != offsetof(DDCAPS_DX3, dwSVBCaps) && size != sizeof(DDCAPS_DX3) && size != sizeof(DDCAPS_DX5) &&
        size != sizeof(DDCAPS_DX7))
        return DDERR_INVALIDPARAMS;
    memcpy(out, full, size);
    ((DDCAPS_DX7 *)out)->dwSize = size;
    return DD_OK;
}

static HRESULT WINAPI Dd_GetCaps(void *face, void *driver, void *emulation)
{
    DD_METHOD_LOCK();
    NEO98DX_INFO info;
    DDCAPS_DX7 caps;
    HRESULT result;
    (void)face;
    if (!driver && !emulation) return DDERR_INVALIDPARAMS;
    if (!DdDisplayInfo(&info)) return DDERR_GENERIC;
    if (driver)
    {
        FillCaps(&caps, &info, TRUE);
        result = CopyCaps(&caps, driver);
        if (FAILED(result)) return result;
    }
    if (emulation)
    {
        FillCaps(&caps, &info, FALSE);
        result = CopyCaps(&caps, emulation);
        if (FAILED(result)) return result;
    }
    return DD_OK;
}

static HRESULT WINAPI Dd_GetDisplayMode(void *face, void *out)
{
    DD_METHOD_LOCK();
    DDSURFACEDESC2 desc;
    NEO98DX_INFO info;
    const DWORD size = out ? ((DDSURFACEDESC *)out)->dwSize : 0;
    if (size != DescriptionSize(Version(face))) return DDERR_INVALIDPARAMS;
    if (!DdDisplayInfo(&info)) return DDERR_GENERIC;
    memset(&desc, 0, sizeof desc);
    desc.dwFlags = DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_REFRESHRATE;
    desc.dwWidth = info.width;
    desc.dwHeight = info.height;
    desc.lPitch = (LONG)info.pitch;
    desc.dwRefreshRate = info.refresh;
    DdFormatToPixelFormat(&info.format, &desc.ddpfPixelFormat);
    memset(out, 0, size);
    memcpy(out, &desc, size < sizeof desc ? offsetof(DDSURFACEDESC, ddsCaps) : sizeof desc);
    ((DDSURFACEDESC *)out)->dwSize = size;
    return DD_OK;
}

static HRESULT WINAPI Dd_GetFourCCCodes(void *face, LPDWORD count, LPDWORD codes)
{
    DD_METHOD_LOCK();
    /* The compressed texture formats, when the rasterizer samples them. */
    static const DWORD dxt[] = {MAKEFOURCC('D', 'X', 'T', '1'), MAKEFOURCC('D', 'X', 'T', '2'), MAKEFOURCC('D', 'X', 'T', '3'),
                                MAKEFOURCC('D', 'X', 'T', '4'), MAKEFOURCC('D', 'X', 'T', '5')};
    NEO98DX_INFO info;
    DWORD have = 0, room, i;
    (void)face;
    if (!count) return DDERR_INVALIDPARAMS;
    if (DdDisplayInfo(&info) && (info.caps & NEO98DX_CAP_DXT)) have = sizeof dxt / sizeof dxt[0];
    room = *count;
    *count = have;
    if (codes)
        for (i = 0; i < have && i < room; ++i) codes[i] = dxt[i];
    return DD_OK;
}

static HRESULT WINAPI Dd_GetGDISurface(void *face, void **out)
{
    DD_METHOD_LOCK();
    DdObject *dd = This(face);
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (!dd->primary) return DDERR_NOTFOUND;
    DdSurfaceAddRef(dd->primary);
    *out = DdSurfaceFace(dd->primary, SurfaceVersion(Version(face)));
    return DD_OK;
}

static HRESULT WINAPI Dd_GetMonitorFrequency(void *face, LPDWORD frequency)
{
    DD_METHOD_LOCK();
    NEO98DX_INFO info;
    (void)face;
    if (!frequency) return DDERR_INVALIDPARAMS;
    if (!DdDisplayInfo(&info)) return DDERR_GENERIC;
    *frequency = info.refresh;
    return DD_OK;
}

static HRESULT WINAPI Dd_GetScanLine(void *face, LPDWORD line)
{
    DD_METHOD_LOCK();
    (void)face;
    if (!line) return DDERR_INVALIDPARAMS;
    return DdDrvGetScanLine(line);
}

static HRESULT WINAPI Dd_GetVerticalBlankStatus(void *face, BOOL *in_blank)
{
    DD_METHOD_LOCK();
    DWORD blank = 0;
    HRESULT result;
    (void)face;
    if (!in_blank) return DDERR_INVALIDPARAMS;
    result = DdDrvWaitForVerticalBlank(0, &blank);
    *in_blank = blank != 0;
    return result;
}

static HRESULT WINAPI Dd_Initialize(void *face, GUID *guid)
{
    DD_METHOD_LOCK();
    (void)face;
    (void)guid;
    return DDERR_ALREADYINITIALIZED;
}

static HRESULT WINAPI Dd_RestoreDisplayMode(void *face)
{
    DD_METHOD_LOCK();
    DdObject *dd = This(face);
    HRESULT result = DD_OK;
    if (dd->mode_set)
    {
        DdFlush3D();
        result = DdDrvRestoreMode();
        if (result == DD_OK)
        {
            NEO98DX_INFO info;
            dd->mode_set = FALSE;
            if (DdDisplayInfo(&info))
            {
                dd->mode_width = info.width;
                dd->mode_height = info.height;
                dd->mode_bits = info.format.bits;
            }
        }
    }
    return result;
}

static HRESULT WINAPI Dd_SetCooperativeLevel(void *face, HWND window, DWORD flags)
{
    DD_METHOD_LOCK();
    DdObject *dd = This(face);
    const DWORD known = DDSCL_FULLSCREEN | DDSCL_ALLOWREBOOT | DDSCL_NOWINDOWCHANGES | DDSCL_NORMAL | DDSCL_EXCLUSIVE |
                        DDSCL_ALLOWMODEX | DDSCL_SETFOCUSWINDOW | DDSCL_SETDEVICEWINDOW | DDSCL_CREATEDEVICEWINDOW |
                        DDSCL_MULTITHREADED | DDSCL_FPUSETUP | DDSCL_FPUPRESERVE;
    HRESULT result;
    if (flags & ~known) return DDERR_INVALIDPARAMS;
    /* The window belongs to another device window scheme this driver has no use for. */
    if (flags & (DDSCL_SETDEVICEWINDOW | DDSCL_CREATEDEVICEWINDOW)) return DDERR_UNSUPPORTED;
    if (flags & DDSCL_SETFOCUSWINDOW)
    {
        if (flags & ~(DDSCL_SETFOCUSWINDOW | DDSCL_MULTITHREADED | DDSCL_FPUSETUP | DDSCL_FPUPRESERVE)) return DDERR_INVALIDPARAMS;
        dd->window = window;
        return DD_OK;
    }
    /* Exactly one of the two levels; exclusive and full screen go together
       and need a window. */
    if (!(flags & DDSCL_NORMAL) == !(flags & DDSCL_EXCLUSIVE)) return DDERR_INVALIDPARAMS;
    if (!(flags & DDSCL_EXCLUSIVE) != !(flags & DDSCL_FULLSCREEN)) return DDERR_INVALIDPARAMS;
    if ((flags & DDSCL_EXCLUSIVE) && (!window || !IsWindow(window))) return DDERR_INVALIDPARAMS;
    if (window && !IsWindow(window)) return DDERR_INVALIDPARAMS;
    if (flags & DDSCL_EXCLUSIVE)
    {
        if (dd->window && dd->window != window) UnhookWindow(dd);
        result = DdDrvSetExclusive(window, TRUE);
        if (result != DD_OK) return result;
        dd->window = window;
        dd->level = flags;
        dd->exclusive = TRUE;
        dd->inactive = FALSE;
        HookWindow(dd);
        return DD_OK;
    }
    UnhookWindow(dd);
    LeaveExclusive(dd, TRUE);
    dd->mode_set = FALSE;
    dd->inactive = FALSE;
    dd->window = window;
    dd->level = flags;
    return DD_OK;
}

static HRESULT SetMode(DdObject *dd, DWORD width, DWORD height, DWORD bits, DWORD refresh, DWORD flags)
{
    HRESULT result;
    if (flags & ~DDSDM_STANDARDVGAMODE) return DDERR_INVALIDPARAMS;
    /* Only the owner of the screen changes its mode. */
    if (!(dd->level & DDSCL_EXCLUSIVE) || !dd->exclusive) return DDERR_NOEXCLUSIVEMODE;
    if (refresh)
    {
        NEO98DX_INFO info;
        if (!DdDisplayInfo(&info) || refresh != info.refresh) return DDERR_INVALIDMODE;
    }
    DdFlush3D();
    result = DdDrvSetMode(width, height, bits);
    if (result != DD_OK) return result;
    dd->mode_set = TRUE;
    dd->mode_width = width;
    dd->mode_height = height;
    dd->mode_bits = bits;
    CoverScreen(dd);
    return DD_OK;
}

static HRESULT WINAPI Dd_SetDisplayMode1(void *face, DWORD width, DWORD height, DWORD bits)
{
    DD_METHOD_LOCK();
    return SetMode(This(face), width, height, bits, 0, 0);
}

static HRESULT WINAPI Dd_SetDisplayMode(void *face, DWORD width, DWORD height, DWORD bits, DWORD refresh, DWORD flags)
{
    DD_METHOD_LOCK();
    return SetMode(This(face), width, height, bits, refresh, flags);
}

static HRESULT WINAPI Dd_WaitForVerticalBlank(void *face, DWORD flags, HANDLE event)
{
    DD_METHOD_LOCK();
    (void)face;
    (void)event;
    if (flags != DDWAITVB_BLOCKBEGIN && flags != DDWAITVB_BLOCKEND && flags != DDWAITVB_BLOCKBEGINEVENT) return DDERR_INVALIDPARAMS;
    return DdDrvWaitForVerticalBlank(flags, NULL);
}

static HRESULT WINAPI Dd_GetAvailableVidMem(void *face, void *caps, LPDWORD total, LPDWORD available)
{
    DD_METHOD_LOCK();
    NEO98DX_INFO info;
    (void)face;
    (void)caps;
    if (!DdDisplayInfo(&info)) return DDERR_GENERIC;
    if (total) *total = info.videoMemoryTotal;
    if (available) *available = info.videoMemoryFree;
    return DD_OK;
}

static HRESULT WINAPI Dd_GetSurfaceFromDC(void *face, HDC dc, void **out)
{
    DD_METHOD_LOCK();
    DdObject *dd = This(face);
    DdSurface *surface;
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    for (surface = dd->surfaces; surface; surface = surface->next)
        if (surface->dc && surface->dc == dc)
        {
            DdSurfaceAddRef(surface);
            *out = DdSurfaceFace(surface, SurfaceVersion(Version(face)));
            return DD_OK;
        }
    return DDERR_NOTFOUND;
}

static HRESULT WINAPI Dd_RestoreAllSurfaces(void *face)
{
    DD_METHOD_LOCK();
    DdObject *dd = This(face);
    DdSurface *surface;
    HRESULT result = DD_OK;
    for (surface = dd->surfaces; surface; surface = surface->next)
        if (DdSurfaceIsLost(surface) != DD_OK)
        {
            const HRESULT one = DdSurfaceRestore(surface);
            if (FAILED(one)) result = one;
        }
    return result;
}

static HRESULT WINAPI Dd_TestCooperativeLevel(void *face)
{
    DD_METHOD_LOCK();
    DdObject *dd = This(face);
    NEO98DX_INFO info;
    if (!DdDisplayInfo(&info)) return DDERR_GENERIC;
    if (dd->level & DDSCL_EXCLUSIVE) return dd->exclusive ? DD_OK : DDERR_NOEXCLUSIVEMODE;
    /* A windowed program: another program owns the screen, or the mode is
       not the one this object works in. */
    if (info.exclusiveProcess && info.exclusiveProcess != info.process) return DDERR_EXCLUSIVEMODEALREADYSET;
    if (info.width != dd->mode_width || info.height != dd->mode_height || info.format.bits != dd->mode_bits) return DDERR_WRONGMODE;
    return DD_OK;
}

/* The display driver of NEO98; it has no vendor of the PCI world. */
static HRESULT WINAPI Dd_GetDeviceIdentifier(void *face, void *out, DWORD flags)
{
    DD_METHOD_LOCK();
    DDDEVICEIDENTIFIER2 id;
    if (!out || (flags & ~DDGDI_GETHOSTIDENTIFIER)) return DDERR_INVALIDPARAMS;
    memset(&id, 0, sizeof id);
    strcpy(id.szDriver, "display");
    strcpy(id.szDescription, "NEO98 display driver");
    id.liDriverVersion.QuadPart = ((LONGLONG)4 << 48) | ((LONGLONG)10 << 32) | ((LONGLONG)NEO98DX_VERSION);
    id.guidDeviceIdentifier.Data1 = 0x41524d39;   /* a constant of this driver */
    id.guidDeviceIdentifier.Data2 = 0x3844;
    id.guidDeviceIdentifier.Data3 = 0x5844;
    id.dwWHQLLevel = 0;
    memcpy(out, &id, Version(face) >= DD_V7 ? sizeof(DDDEVICEIDENTIFIER2) : sizeof(DDDEVICEIDENTIFIER));
    return DD_OK;
}

/* The original cannot start a refresh-rate test without monitor information.
   This virtual display has no such data; output arguments remain untouched. */
static HRESULT WINAPI Dd_StartModeTest(void *face, LPSIZE modes, DWORD count, DWORD flags)
{
    DD_METHOD_LOCK();
    (void)face;
    (void)modes;
    (void)count;
    (void)flags;
    return DDERR_NOMONITORINFORMATION;
}

static HRESULT WINAPI Dd_EvaluateMode(void *face, DWORD flags, DWORD *timeout)
{
    DD_METHOD_LOCK();
    (void)face;
    (void)flags;
    (void)timeout;
    return DDERR_NOMONITORINFORMATION;
}

/* ------------------------------------------------------------- method tables */
#define DD_COMMON \
    .QueryInterface = (void *)Dd_QueryInterface, .AddRef = (void *)Dd_AddRef, .Release = (void *)Dd_Release, \
    .Compact = (void *)Dd_Compact, .CreateClipper = (void *)Dd_CreateClipper, .CreatePalette = (void *)Dd_CreatePalette, \
    .CreateSurface = (void *)Dd_CreateSurface, .DuplicateSurface = (void *)Dd_DuplicateSurface, \
    .EnumDisplayModes = (void *)Dd_EnumDisplayModes, .EnumSurfaces = (void *)Dd_EnumSurfaces, \
    .FlipToGDISurface = (void *)Dd_FlipToGDISurface, .GetCaps = (void *)Dd_GetCaps, .GetDisplayMode = (void *)Dd_GetDisplayMode, \
    .GetFourCCCodes = (void *)Dd_GetFourCCCodes, .GetGDISurface = (void *)Dd_GetGDISurface, \
    .GetMonitorFrequency = (void *)Dd_GetMonitorFrequency, .GetScanLine = (void *)Dd_GetScanLine, \
    .GetVerticalBlankStatus = (void *)Dd_GetVerticalBlankStatus, .Initialize = (void *)Dd_Initialize, \
    .RestoreDisplayMode = (void *)Dd_RestoreDisplayMode, .SetCooperativeLevel = (void *)Dd_SetCooperativeLevel, \
    .WaitForVerticalBlank = (void *)Dd_WaitForVerticalBlank

static const struct IDirectDrawVtbl table1 = {DD_COMMON, .SetDisplayMode = (void *)Dd_SetDisplayMode1};
static const struct IDirectDraw2Vtbl table2 = {DD_COMMON, .SetDisplayMode = (void *)Dd_SetDisplayMode,
                                               .GetAvailableVidMem = (void *)Dd_GetAvailableVidMem};
static const struct IDirectDraw4Vtbl table4 = {DD_COMMON, .SetDisplayMode = (void *)Dd_SetDisplayMode,
                                               .GetAvailableVidMem = (void *)Dd_GetAvailableVidMem,
                                               .GetSurfaceFromDC = (void *)Dd_GetSurfaceFromDC,
                                               .RestoreAllSurfaces = (void *)Dd_RestoreAllSurfaces,
                                               .TestCooperativeLevel = (void *)Dd_TestCooperativeLevel,
                                               .GetDeviceIdentifier = (void *)Dd_GetDeviceIdentifier};
static const struct IDirectDraw7Vtbl table7 = {DD_COMMON, .SetDisplayMode = (void *)Dd_SetDisplayMode,
                                               .GetAvailableVidMem = (void *)Dd_GetAvailableVidMem,
                                               .GetSurfaceFromDC = (void *)Dd_GetSurfaceFromDC,
                                               .RestoreAllSurfaces = (void *)Dd_RestoreAllSurfaces,
                                               .TestCooperativeLevel = (void *)Dd_TestCooperativeLevel,
                                               .GetDeviceIdentifier = (void *)Dd_GetDeviceIdentifier,
                                               .StartModeTest = (void *)Dd_StartModeTest, .EvaluateMode = (void *)Dd_EvaluateMode};

void *DdObjectFace(DdObject *dd, unsigned version) { return &dd->face[version]; }

/* The interface of a version with a reference of its own. */
void *DdObjectTake(DdObject *dd, unsigned version)
{
    DdAddRef(&dd->face_references[version]);
    DdObjectAddRef(dd);
    return &dd->face[version];
}

HRESULT DdCreateObject(DdObject **out)
{
    static const void *const tables[DD_VERSIONS] = {&table1, &table2, &table4, &table7};
    DdObject *dd = (DdObject *)DdAllocate(sizeof *dd);
    NEO98DX_INFO info;
    unsigned i;
    if (!dd) return DDERR_OUTOFMEMORY;
    for (i = 0; i < DD_VERSIONS; ++i)
    {
        dd->face[i].table = tables[i];
        dd->face[i].object = dd;
        dd->face[i].version = i;
    }
    dd->references = 1;
    /* DdObjectFace hands out the one reference of a new object. */
    if (DdDisplayInfo(&info))
    {
        dd->mode_width = info.width;
        dd->mode_height = info.height;
        dd->mode_bits = info.format.bits;
    }
    DdLock();
    dd->next = dd_objects;
    dd_objects = dd;
    DdUnlock();
    *out = dd;
    return DD_OK;
}
