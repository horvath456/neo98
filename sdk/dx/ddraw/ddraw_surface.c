/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DDRAW.DLL for ARM64 processes: the surface behind IDirectDrawSurface 1, 2,
   3, 4 and 7 and IDirectDrawGammaControl.

   A surface in system memory is memory of the process, and everything
   between two such surfaces happens here. The primary surface, the back
   buffers of a flipping chain and video memory are surfaces of the driver
   (NEO98DX.DLL): their Lock, Unlock, Blt, Flip and GetDC are calls into it.
   With a rasterizer in the host, Z buffers, textures and 3D targets are
   driver surfaces too, which is what Direct3D builds on. */
#include "ddraw_private.h"

static DdSurface *This(void *face) { return (DdSurface *)((DdFace *)face)->object; }
static unsigned Version(void *face) { return ((DdFace *)face)->version; }

DdSurface *DdSurfaceOf(void *face) { return face ? This(face) : NULL; }
void *DdSurfaceFace(DdSurface *surface, unsigned version) { return &surface->face[version]; }
void DdSurfaceAddRef(DdSurface *surface) { DdAddRef(&surface->references); }

static unsigned PixelBytes(const DdSurface *s) { return (s->format.bits + 7) / 8; }

/* The surface as the driver or the blitter is told about it. */
static void PlaneOf(const DdSurface *s, NEO98DX_PLANE *plane)
{
    memset(plane, 0, sizeof *plane);
    plane->width = s->width;
    plane->height = s->height;
    plane->pitch = s->store->pitch;
    plane->format = s->format;
    if (s->store->driver) plane->handle = s->store->driver;
    else plane->bits = (DXU32)(ULONG_PTR)s->store->memory;
}

/* ------------------------------------------------------------------- stores */
static void StoreRelease(DdStore *store)
{
    if (!store || DdDropRef(&store->references) > 0) return;
    /* A batch of Direct3D may still name the surface. */
    if (store->driver) DdFlush3D();
    if (store->driver) DdDrvDestroySurface(store->driver);
    if (store->memory && store->own_memory) free(store->memory);
    free(store);
}

static void RememberMode(DdStore *store)
{
    NEO98DX_INFO info;
    if (!DdDisplayInfo(&info)) return;
    store->mode_width = info.width;
    store->mode_height = info.height;
    store->mode_bits = info.format.bits;
}

/* ----------------------------------------------------------------- lifetime */
static void FreePrivateData(DdSurface *s)
{
    while (s->private_data)
    {
        DdPrivate *next = s->private_data->next;
        if ((s->private_data->flags & 1) && s->private_data->data)   /* DDSPD_IUNKNOWNPOINTER */
            IUnknown_Release(*(IUnknown **)s->private_data->data);
        free(s->private_data->data);
        free(s->private_data);
        s->private_data = next;
    }
}

static void Destroy(DdSurface *s)
{
    D3dLegacySurfaceDestroy(s);
    if (s->dc)
    {
        DdDrvReleaseDC(s->dc);
        s->dc = NULL;
    }
    while (s->attached)
    {
        DdAttachment *a = s->attached;
        s->attached = a->next;
        if (a->surface->owner == s) a->surface->owner = NULL;
        if (s->store && s->store->driver && a->surface->store && a->surface->store->driver)
            DdDrvAttach(s->store->driver, a->surface->store->driver, FALSE);
        DdSurfaceRelease(a->surface);
        free(a);
    }
    if (s->clipper) DdClipperRelease(s->clipper);
    if (s->palette)
    {
        DdPaletteAttach(s->palette, s, FALSE);
        DdPaletteRelease(s->palette);
    }
    FreePrivateData(s);
    StoreRelease(s->store);
    if (s->dd)
    {
        DdObject *dd = s->dd;
        DdObjectForgetSurface(dd, s);
        s->dd = NULL;
        if (s->holds_dd) DdObjectRelease(dd);
    }
    free(s);
}

void DdSurfaceRelease(DdSurface *surface)
{
    if (DdDropRef(&surface->references) <= 0) Destroy(surface);
}

/* The DirectDraw object goes while the program still holds the surface: its
   pixels go back to the driver, and every call answers that the object is
   gone. */
void DdSurfaceOrphan(DdSurface *s)
{
    DdObject *dd = s->dd;
    if (!dd) return;
    DdObjectForgetSurface(dd, s);
    s->dd = NULL;
    s->dead = TRUE;
    if (s->dc)
    {
        DdDrvReleaseDC(s->dc);
        s->dc = NULL;
    }
    if (s->store && s->store->driver && s->store->references == 1)
    {
        DdFlush3D();
        DdDrvDestroySurface(s->store->driver);
        s->store->driver = 0;
    }
}

/* ------------------------------------------------------------- descriptions */
static DWORD KeyFlags(const DdSurface *s) { return s->key_flags; }

void DdSurfaceDescribe(DdSurface *s, void *out, DWORD size)
{
    DDSURFACEDESC2 desc;
    memset(&desc, 0, sizeof desc);
    desc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT | KeyFlags(s);
    desc.dwHeight = s->height;
    desc.dwWidth = s->width;
    desc.lPitch = s->store ? s->store->pitch : 0;
    if (s->format.flags & DDPF_FOURCC)
    {
        /* A compressed surface is rows of 4x4 blocks and says its size. */
        desc.dwFlags = (desc.dwFlags & ~(DWORD)DDSD_PITCH) | DDSD_LINEARSIZE;
        desc.dwLinearSize = (DWORD)desc.lPitch * ((s->height + 3) / 4 ? (s->height + 3) / 4 : 1);
    }
    if (s->back_buffers)
    {
        desc.dwFlags |= DDSD_BACKBUFFERCOUNT;
        desc.dwBackBufferCount = s->back_buffers;
    }
    if (s->mip_count)
    {
        desc.dwFlags |= DDSD_MIPMAPCOUNT;
        desc.dwMipMapCount = s->mip_count;
    }
    desc.ddckCKDestOverlay = s->key_dest_overlay;
    desc.ddckCKDestBlt = s->key_dest_blt;
    desc.ddckCKSrcOverlay = s->key_src_overlay;
    desc.ddckCKSrcBlt = s->key_src_blt;
    DdFormatToPixelFormat(&s->format, &desc.ddpfPixelFormat);
    desc.ddsCaps.dwCaps = s->caps;
    desc.ddsCaps.dwCaps2 = s->caps2;
    desc.ddsCaps.dwCaps3 = s->caps3;
    desc.ddsCaps.dwCaps4 = s->caps4;
    if (size == sizeof(DDSURFACEDESC2))
    {
        desc.dwSize = size;
        memcpy(out, &desc, sizeof desc);
        return;
    }
    {
        DDSURFACEDESC *narrow = (DDSURFACEDESC *)out;
        memset(narrow, 0, sizeof *narrow);
        memcpy(narrow, &desc, offsetof(DDSURFACEDESC, ddsCaps));
        narrow->dwSize = sizeof *narrow;
        narrow->ddsCaps.dwCaps = s->caps;
    }
}

/* The description an interface takes: the first three the short one, the
   fourth and the seventh the long one, and neither the other (measured
   against the original DDRAW.DLL). */
static BOOL DescriptionSize(const void *desc, unsigned version)
{
    const DWORD size = desc ? ((const DDSURFACEDESC *)desc)->dwSize : 0;
    return size == (version >= DDS_V4 ? sizeof(DDSURFACEDESC2) : sizeof(DDSURFACEDESC));
}

/* ----------------------------------------------------------------- creation */
static const struct IDirectDrawSurfaceVtbl surface_table1;
static const struct IDirectDrawSurface2Vtbl surface_table2;
static const struct IDirectDrawSurface3Vtbl surface_table3;
static const struct IDirectDrawSurface4Vtbl surface_table4;
static const struct IDirectDrawSurface7Vtbl surface_table7;
static const struct IDirectDrawGammaControlVtbl gamma_table;

static DdSurface *NewSurface(DdObject *dd, unsigned version, unsigned made_through)
{
    static const void *const tables[DDS_VERSIONS] = {&surface_table1, &surface_table2, &surface_table3, &surface_table4,
                                                     &surface_table7};
    DdSurface *s = (DdSurface *)DdAllocate(sizeof *s);
    unsigned i;
    if (!s) return NULL;
    for (i = 0; i < DDS_VERSIONS; ++i)
    {
        s->face[i].table = tables[i];
        s->face[i].object = s;
        s->face[i].version = i;
    }
    s->gamma.table = &gamma_table;
    s->gamma.object = s;
    s->references = 1;
    s->dd = dd;
    s->made_through = made_through;
    /* A surface made through IDirectDraw4 or 7 keeps its object alive. */
    s->holds_dd = version >= DDS_V4;
    if (s->holds_dd) DdObjectAddRef(dd);
    s->next = dd->surfaces;
    dd->surfaces = s;
    return s;
}

/* One surface with its pixels. `driver` asks for a surface of the driver;
   `fallback` allows system memory when the driver has none of that kind. */
static HRESULT CreateOne(DdObject *dd, unsigned version, unsigned made_through, DWORD caps, DWORD caps2, DWORD width, DWORD height,
                         const NEO98DX_FORMAT *format, BOOL own_format, BOOL driver, BOOL fallback, void *client_memory,
                         LONG client_pitch, DdSurface **out)
{
    DdSurface *s = NewSurface(dd, version, made_through);
    DdStore *store = (DdStore *)DdAllocate(sizeof *store);
    HRESULT result = DD_OK;
    *out = NULL;
    if (!s || !store)
    {
        free(store);
        if (s) DdSurfaceRelease(s);
        return DDERR_OUTOFMEMORY;
    }
    store->references = 1;
    s->store = store;
    s->caps = caps & ~(DDSCAPS_SYSTEMMEMORY | DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM | DDSCAPS_NONLOCALVIDMEM);
    s->caps2 = caps2;
    s->width = width;
    s->height = height;
    s->format = *format;
    s->own_format = own_format;
    if (driver)
    {
        NEO98DX_CREATE create;
        memset(&create, 0, sizeof create);
        create.size = sizeof create;
        create.caps = caps;
        create.caps2 = caps2;
        create.width = width;
        create.height = height;
        create.format = *format;
        result = DdDrvCreateSurface(&create, sizeof create);
        if (result == DD_OK)
        {
            store->driver = create.handle;
            store->pitch = create.pitch;
            s->width = create.width;
            s->height = create.height;
            s->format = create.format;
            s->caps |= DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM;
            RememberMode(store);
        }
        else if (!fallback)
        {
            DdSurfaceRelease(s);
            return result;
        }
    }
    if (!store->driver)
    {
        const unsigned bytes = (format->bits + 7) / 8;
        s->caps |= DDSCAPS_SYSTEMMEMORY;
        if (client_memory)
        {
            store->memory = (BYTE *)client_memory;
            store->pitch = client_pitch ? client_pitch : (LONG)((width * bytes + 3) & ~3u);
        }
        else
        {
            /* Rows of whole pixels, a multiple of eight bytes long. */
            store->pitch = (LONG)((width * bytes + 7) & ~7u);
            store->memory = (BYTE *)calloc(1, (size_t)store->pitch * height);
            store->own_memory = TRUE;
            if (!store->memory)
            {
                DdSurfaceRelease(s);
                return DDERR_OUTOFMEMORY;
            }
        }
    }
    *out = s;
    return DD_OK;
}

static HRESULT Attach(DdSurface *parent, DdSurface *child, BOOL implicit)
{
    DdAttachment *a = (DdAttachment *)DdAllocate(sizeof *a), **tail;
    if (!a) return DDERR_OUTOFMEMORY;
    a->surface = child;
    a->implicit = implicit;
    for (tail = &parent->attached; *tail; tail = &(*tail)->next) {}
    *tail = a;
    if (implicit) child->owner = parent;
    if (parent->store->driver && child->store->driver) DdDrvAttach(parent->store->driver, child->store->driver, TRUE);
    return DD_OK;
}

HRESULT DdCreateSurfaceTree(DdObject *dd, const DDSURFACEDESC2 *desc, unsigned version, unsigned made_through, DdSurface **out)
{
    const DWORD flags = desc->dwFlags;
    DWORD caps = desc->ddsCaps.dwCaps, caps2 = desc->ddsCaps.dwCaps2;
    DWORD width = desc->dwWidth, height = desc->dwHeight, back_buffers = 0, levels = 0;
    const BOOL primary = (caps & DDSCAPS_PRIMARYSURFACE) != 0;
    NEO98DX_INFO info;
    NEO98DX_FORMAT format;
    BOOL own_format = FALSE, driver, fallback;
    DdSurface *root = NULL, *previous;
    HRESULT result;
    DWORD i, cube_faces = 0, first_face = 0;

    *out = NULL;
    if (!(flags & DDSD_CAPS)) return DDERR_INVALIDPARAMS;
    if (!DdDisplayInfo(&info)) return DDERR_GENERIC;
    if ((caps & DDSCAPS_SYSTEMMEMORY) && (caps & DDSCAPS_VIDEOMEMORY)) return DDERR_INVALIDCAPS;
    if (caps & DDSCAPS_OVERLAY) return DDERR_NOOVERLAYHW;
    if (caps & (DDSCAPS_BACKBUFFER | DDSCAPS_FRONTBUFFER)) return DDERR_INVALIDCAPS;   /* the chain names those itself */
    /* Original DDRAW7 rejects VOLUME with INVALIDCAPS. Cube maps are a
       root face, its mip chain, and the other requested face chains. */
    if (caps2 & DDSCAPS2_VOLUME) return DDERR_INVALIDCAPS;
    if (caps2 & DDSCAPS2_CUBEMAP)
    {
        cube_faces = caps2 & DDSCAPS2_CUBEMAP_ALLFACES;
        if (!(caps & DDSCAPS_TEXTURE) || !cube_faces || width != height || (caps & DDSCAPS_FLIP))
            return DDERR_INVALIDPARAMS;
        if (!(caps & DDSCAPS_COMPLEX)) return DDERR_DDSCAPSCOMPLEXREQUIRED;
        first_face = cube_faces & (0u - cube_faces);
        caps2 = (caps2 & ~DDSCAPS2_CUBEMAP_ALLFACES) | first_face;
    }
    if (flags & DDSD_BACKBUFFERCOUNT) back_buffers = desc->dwBackBufferCount;
    if (caps & DDSCAPS_FLIP)
    {
        if (!(caps & DDSCAPS_COMPLEX) || !back_buffers) return DDERR_INVALIDCAPS;
    }
    else if (back_buffers) return DDERR_INVALIDCAPS;
    if (back_buffers > 15) return DDERR_INVALIDPARAMS;

    if (primary)
    {
        if (flags & (DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_LPSURFACE)) return DDERR_INVALIDPARAMS;
        if (caps & (DDSCAPS_SYSTEMMEMORY | DDSCAPS_TEXTURE | DDSCAPS_ZBUFFER | DDSCAPS_OFFSCREENPLAIN)) return DDERR_INVALIDCAPS;
        if (dd->primary) return DDERR_PRIMARYSURFACEALREADYEXISTS;
        /* A flipping primary surface changes what the monitor shows: only
           the owner of the screen has one. */
        if ((caps & DDSCAPS_FLIP) && !((dd->level & DDSCL_EXCLUSIVE) && dd->exclusive)) return DDERR_NOEXCLUSIVEMODE;
        format = info.format;
        width = info.width;
        height = info.height;
    }
    else
    {
        if (!(flags & DDSD_WIDTH) || !(flags & DDSD_HEIGHT) || !width || !height) return DDERR_INVALIDPARAMS;
        if (width > 8192 || height > 8192) return DDERR_TOOBIGSIZE;
        if (flags & DDSD_PIXELFORMAT)
        {
            /* The original reports an unknown format as INVALIDPIXELFORMAT. */
            if (!DdPixelFormatToFormat(&desc->ddpfPixelFormat, &format))
            {
                DWORD code = desc->ddpfPixelFormat.dwFourCC;
                /* A known DXT format unavailable in this driver keeps its
                   existing INVALIDPARAMS result (also in the software runner). */
                if ((desc->ddpfPixelFormat.dwFlags & DDPF_FOURCC) &&
                    (code & 0xffffff) == MAKEFOURCC('D', 'X', 'T', 0) && (code >> 24) >= '1' && (code >> 24) <= '5')
                    return DDERR_INVALIDPARAMS;
                return DDERR_INVALIDPIXELFORMAT;
            }
            own_format = TRUE;
        }
        else if (caps & DDSCAPS_ZBUFFER) return DDERR_INVALIDPIXELFORMAT;   /* a Z buffer says its depth */
        else format = info.format;
        if ((caps & DDSCAPS_ZBUFFER) && !(format.flags & DDPF_ZBUFFER)) return DDERR_INVALIDPIXELFORMAT;
    }
    if (caps & DDSCAPS_MIPMAP)
    {
        DWORD most = 1, w = width, h = height;
        if (!(caps & DDSCAPS_TEXTURE)) return DDERR_INVALIDCAPS;
        while (w > 1 || h > 1) { w = w > 1 ? w / 2 : 1; h = h > 1 ? h / 2 : 1; ++most; }
        if (caps & DDSCAPS_COMPLEX) levels = (flags & DDSD_MIPMAPCOUNT) ? desc->dwMipMapCount : most;
        else levels = 1;
        if (!levels || levels > most) return DDERR_INVALIDPARAMS;
    }

    /* Where the pixels live. The primary surface, its chain and what the
       program asks for in video memory are the driver's, and so are Z
       buffers, textures and 3D targets when the host has a rasterizer.
       Everything else is system memory: a plain off-screen surface blits
       fastest there, without leaving the program. */
    driver = primary || (caps & DDSCAPS_VIDEOMEMORY) != 0;
    fallback = FALSE;
    /* A compressed surface is the rasterizer's; nothing here reads its blocks. */
    if (!primary && (format.flags & DDPF_FOURCC))
    {
        if (caps & DDSCAPS_SYSTEMMEMORY) return DDERR_INVALIDPARAMS;   /* as the original answers */
        driver = TRUE;
    }
    if (!driver && !(caps & DDSCAPS_SYSTEMMEMORY) && (info.caps & NEO98DX_CAP_3D) &&
        ((caps & (DDSCAPS_ZBUFFER | DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE)) || (caps2 & DDSCAPS2_TEXTUREMANAGE)))
    {
        driver = TRUE;
        fallback = TRUE;
    }
    if ((flags & DDSD_LPSURFACE) && (driver || !desc->lpSurface)) return DDERR_INVALIDPARAMS;

    result = CreateOne(dd, version, made_through, caps | (back_buffers ? DDSCAPS_FRONTBUFFER : 0) | (primary ? DDSCAPS_VISIBLE : 0), caps2, width,
                       height, &format, own_format, driver, fallback, (flags & DDSD_LPSURFACE) ? desc->lpSurface : NULL,
                       (flags & DDSD_PITCH) ? desc->lPitch : 0, &root);
    if (FAILED(result)) return result;
    root->back_buffers = back_buffers;
    root->mip_count = levels;
    if (flags & DDSD_CKSRCBLT) { root->key_src_blt = desc->ddckCKSrcBlt; root->key_flags |= DDSD_CKSRCBLT; }
    if (flags & DDSD_CKDESTBLT) { root->key_dest_blt = desc->ddckCKDestBlt; root->key_flags |= DDSD_CKDESTBLT; }
    if (flags & DDSD_CKSRCOVERLAY) { root->key_src_overlay = desc->ddckCKSrcOverlay; root->key_flags |= DDSD_CKSRCOVERLAY; }
    if (flags & DDSD_CKDESTOVERLAY) { root->key_dest_overlay = desc->ddckCKDestOverlay; root->key_flags |= DDSD_CKDESTOVERLAY; }
    if (root->store->driver && (root->key_flags & DDSD_CKSRCBLT))
        DdDrvSetColorKey(root->store->driver, 1, root->key_src_blt.dwColorSpaceLowValue, root->key_src_blt.dwColorSpaceHighValue);

    /* The back buffers of a flipping chain, each attached to the one before. */
    previous = root;
    for (i = 0; i < back_buffers; ++i)
    {
        DdSurface *back = NULL;
        const DWORD back_caps = (caps & ~(DDSCAPS_PRIMARYSURFACE | DDSCAPS_VISIBLE)) | (i == 0 ? DDSCAPS_BACKBUFFER : 0);
        result = CreateOne(dd, version, made_through, back_caps, caps2, root->width, root->height, &root->format, own_format,
                           root->store->driver != 0, FALSE, NULL, 0, &back);
        if (SUCCEEDED(result)) result = Attach(previous, back, TRUE);
        if (FAILED(result))
        {
            if (back) DdSurfaceRelease(back);
            DdSurfaceRelease(root);
            return result;
        }
        previous = back;
    }
    /* The levels of a mip chain below the first, each half the one above. */
    previous = root;
    for (i = 1; i < levels; ++i)
    {
        DdSurface *level = NULL;
        const DWORD w = previous->width > 1 ? previous->width / 2 : 1, h = previous->height > 1 ? previous->height / 2 : 1;
        result = CreateOne(dd, version, made_through, caps, caps2 | DDSCAPS2_MIPMAPSUBLEVEL, w, h, &root->format, own_format,
                           root->store->driver != 0, FALSE, NULL, 0, &level);
        if (SUCCEEDED(result))
        {
            level->mip_count = levels - i;
            result = Attach(previous, level, TRUE);
        }
        if (FAILED(result))
        {
            if (level) DdSurfaceRelease(level);
            DdSurfaceRelease(root);
            return result;
        }
        previous = level;
    }
    for (i = 0; i < 6; ++i)
    {
        const DWORD bit = DDSCAPS2_CUBEMAP_POSITIVEX << i;
        if ((cube_faces & bit) && bit != first_face)
        {
            DDSURFACEDESC2 face_desc = *desc;
            DdSurface *face = NULL;
            face_desc.ddsCaps.dwCaps2 = (caps2 & ~DDSCAPS2_CUBEMAP_ALLFACES) | bit;
            result = DdCreateSurfaceTree(dd, &face_desc, version, made_through, &face);
            if (SUCCEEDED(result)) result = Attach(root, face, TRUE);
            if (FAILED(result))
            {
                if (face) DdSurfaceRelease(face);
                DdSurfaceRelease(root);
                return result;
            }
        }
    }
    if (primary) dd->primary = root;
    *out = root;
    return DD_OK;
}

/* A second surface on the pixels of the first. The primary surface, a
   surface made with another one and a 3D surface have no duplicate. */
HRESULT DdSurfaceDuplicate(DdSurface *original, unsigned version, unsigned made_through, DdSurface **out)
{
    DdSurface *s;
    *out = NULL;
    if (!original || original->dead) return DDERR_INVALIDOBJECT;
    if ((original->caps & (DDSCAPS_PRIMARYSURFACE | DDSCAPS_3DDEVICE | DDSCAPS_ZBUFFER | DDSCAPS_TEXTURE | DDSCAPS_FLIP)) ||
        original->owner || original->attached)
        return DDERR_CANTDUPLICATE;
    s = NewSurface(original->dd, version, made_through);
    if (!s) return DDERR_OUTOFMEMORY;
    s->caps = original->caps;
    s->caps2 = original->caps2;
    s->width = original->width;
    s->height = original->height;
    s->format = original->format;
    s->own_format = original->own_format;
    s->store = original->store;
    DdAddRef(&s->store->references);
    *out = s;
    return DD_OK;
}

/* ----------------------------------------------------------------- IUnknown */
static HRESULT WINAPI Dds_QueryInterface(void *face, REFIID iid, void **out)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    int version = -1;
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (!iid) return DDERR_INVALIDPARAMS;
    if (IsEqualGUID(iid, &dd_iid_unknown) || IsEqualGUID(iid, &IID_IDirectDrawSurface)) version = DDS_V1;
    else if (IsEqualGUID(iid, &IID_IDirectDrawSurface2)) version = DDS_V2;
    else if (IsEqualGUID(iid, &IID_IDirectDrawSurface3)) version = DDS_V3;
    else if (IsEqualGUID(iid, &IID_IDirectDrawSurface4)) version = DDS_V4;
    else if (IsEqualGUID(iid, &IID_IDirectDrawSurface7)) version = DDS_V7;
    if (version >= 0)
    {
        DdSurfaceAddRef(s);
        *out = &s->face[version];
        return S_OK;
    }
    /* Every surface answers for the gamma control, as with the original; the
       ramp itself belongs to the display, and only the primary surface
       reaches it. */
    if (IsEqualGUID(iid, &IID_IDirectDrawGammaControl))
    {
        DdSurfaceAddRef(s);
        *out = &s->gamma;
        return S_OK;
    }
    return DdQueryExtension(dd_surface_extensions, s, iid, out);
}

static ULONG WINAPI Dds_AddRef(void *face) {
    DD_METHOD_LOCK(); return (ULONG)DdAddRef(&This(face)->references); }

static ULONG WINAPI Dds_Release(void *face)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    const LONG left = s->references - 1;
    DdSurfaceRelease(s);
    return left > 0 ? (ULONG)left : 0;
}

/* -------------------------------------------------------------- lost, found */
HRESULT DdSurfaceIsLost(DdSurface *s)
{
    if (s->dead) return DDERR_INVALIDOBJECT;
    if (!s->store->driver) return DD_OK;
    return DdDrvIsLost(s->store->driver);
}

/* A surface of the driver comes back in the mode it was made in. In another
   mode its description would no longer be true (DDERR_WRONGMODE), and the
   program makes its surfaces anew. */
HRESULT DdSurfaceRestore(DdSurface *s)
{
    NEO98DX_INFO info;
    NEO98DX_CREATE create;
    HRESULT result;
    if (s->dead) return DDERR_INVALIDOBJECT;
    if (!s->store->driver) return DD_OK;
    if (!DdDisplayInfo(&info)) return DDERR_GENERIC;
    if (info.width != s->store->mode_width || info.height != s->store->mode_height || info.format.bits != s->store->mode_bits)
        return DDERR_WRONGMODE;
    if (info.exclusiveProcess && info.exclusiveProcess != info.process) return DDERR_WRONGMODE;
    memset(&create, 0, sizeof create);
    DdFlush3D();
    result = DdDrvRestore(s->store->driver, &create, sizeof create);
    if (result == DD_OK) s->store->pitch = create.pitch;
    return result;
}

static HRESULT WINAPI Dds_IsLost(void *face) {
    DD_METHOD_LOCK(); return DdSurfaceIsLost(This(face)); }

static HRESULT WINAPI Dds_Restore(void *face)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    DdAttachment *a;
    HRESULT result = DdSurfaceRestore(s);
    /* The surfaces made with this one come back with it. */
    for (a = s->attached; a && SUCCEEDED(result); a = a->next)
        if (a->implicit) result = Dds_Restore(&a->surface->face[DDS_V1]);
    return result;
}

/* ------------------------------------------------------------------ pixels */
static HRESULT WINAPI Dds_Lock(void *face, LPRECT rect, void *out, DWORD flags, HANDLE event)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    BYTE *bits;
    LONG pitch;
    DWORD size;
    (void)event;
    if (!DescriptionSize(out, Version(face))) return DDERR_INVALIDPARAMS;
    size = ((DDSURFACEDESC *)out)->dwSize;
    if (s->dead) return DDERR_INVALIDOBJECT;
    if (s->dc) return DDERR_SURFACEBUSY;
    if (rect && (rect->left < 0 || rect->top < 0 || rect->right > (LONG)s->width || rect->bottom > (LONG)s->height ||
                 rect->left >= rect->right || rect->top >= rect->bottom))
        return DDERR_INVALIDRECT;
    /* One lock of the whole surface at a time. */
    if (s->locks && !rect) return DDERR_SURFACEBUSY;
    if (s->store->driver)
    {
        NEO98DX_LOCK lock;
        HRESULT result;
        /* What Direct3D drew, or drew with this texture, comes first. */
        DdFlush3D();
        memset(&lock, 0, sizeof lock);
        lock.size = sizeof lock;
        lock.handle = s->store->driver;
        lock.flags = flags;
        if (rect)
        {
            lock.hasRect = 1;
            lock.rect[0] = rect->left; lock.rect[1] = rect->top; lock.rect[2] = rect->right; lock.rect[3] = rect->bottom;
        }
        result = DdDrvLock(&lock, sizeof lock);
        if (result != DD_OK) return result;
        bits = (BYTE *)(ULONG_PTR)lock.bits;
        pitch = lock.pitch;
        s->store->pitch = pitch;
    }
    else
    {
        bits = s->store->memory;
        pitch = s->store->pitch;
    }
    ++s->locks;
    DdSurfaceDescribe(s, out, size);
    ((DDSURFACEDESC *)out)->dwFlags |= DDSD_LPSURFACE;
    if (s->format.flags & DDPF_FOURCC)
    {
        /* Rows of blocks: the rect names the block it starts in. */
        const unsigned block = (unsigned)pitch / ((s->width + 3) / 4 ? (s->width + 3) / 4 : 1);
        ((DDSURFACEDESC *)out)->lpSurface = rect ? bits + (size_t)(rect->top / 4) * pitch + (size_t)(rect->left / 4) * block : bits;
        return DD_OK;
    }
    ((DDSURFACEDESC *)out)->lPitch = pitch;
    ((DDSURFACEDESC *)out)->lpSurface = rect ? bits + (size_t)rect->top * pitch + (size_t)rect->left * PixelBytes(s) : bits;
    return DD_OK;
}

static HRESULT WINAPI Dds_Unlock(void *face, void *where)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    (void)where;
    if (s->dead) return DDERR_INVALIDOBJECT;
    if (!s->locks) return DDERR_NOTLOCKED;
    --s->locks;
    ++s->uniqueness;
    if (s->store->driver) return DdDrvUnlock(s->store->driver);
    return DD_OK;
}

/* The clip list of a destination with a clipper, for the blitter of this
   DLL: the list the program set, or the visible region of the window. The
   caller frees `data`. */
static HRESULT ClipRects(DdClipper *clipper, RGNDATA **data, const DDBLIT_RECT **rects, unsigned *count)
{
    *data = NULL;
    *rects = NULL;
    *count = 0;
    if (clipper->window)
    {
        DWORD bytes = 0;
        HRESULT result = DdDrvGetClipList(clipper->window, NULL, &bytes);
        if (result != DD_OK) return result;
        *data = (RGNDATA *)malloc(bytes ? bytes : sizeof(RGNDATA));
        if (!*data) return DDERR_OUTOFMEMORY;
        result = DdDrvGetClipList(clipper->window, *data, &bytes);
        if (result != DD_OK) return result;
        *rects = (const DDBLIT_RECT *)(*data)->Buffer;
        *count = (*data)->rdh.nCount;
        return DD_OK;
    }
    if (!clipper->list) return DDERR_NOCLIPLIST;
    *rects = (const DDBLIT_RECT *)clipper->list->Buffer;
    *count = clipper->list->rdh.nCount;
    return DD_OK;
}

/* One blit, by the driver when a surface of the driver takes part, here
   otherwise. `job_flags` are NEO98DX_BLT_ values. */
static HRESULT RunBlit(DdSurface *dst, const RECT *drect, DdSurface *src, const RECT *srect, unsigned job_flags, DWORD fill,
                       const DDCOLORKEY *src_key, const DDCOLORKEY *dst_key)
{
    const BOOL by_driver = dst->store->driver || (src && src->store->driver);
    if (dst->locks || dst->dc || (src && (src->locks || src->dc))) return DDERR_SURFACEBUSY;
    /* The original DDRAW rejects a format conversion, including different
       masks at the same depth. A raw copy would silently change colours. */
    if (src && memcmp(&src->format, &dst->format, sizeof src->format)) return DDERR_UNSUPPORTED;
    ++dst->uniqueness;
    if (by_driver)
    {
        NEO98DX_BLT blt;
        DdFlush3D();
        memset(&blt, 0, sizeof blt);
        blt.size = sizeof blt;
        PlaneOf(dst, &blt.dst);
        if (src) PlaneOf(src, &blt.src);
        blt.dstRect[0] = drect->left; blt.dstRect[1] = drect->top; blt.dstRect[2] = drect->right; blt.dstRect[3] = drect->bottom;
        if (srect)
        {
            blt.srcRect[0] = srect->left; blt.srcRect[1] = srect->top; blt.srcRect[2] = srect->right; blt.srcRect[3] = srect->bottom;
        }
        blt.flags = job_flags;
        blt.fill = fill;
        if (src_key) { blt.srcKeyLow = src_key->dwColorSpaceLowValue; blt.srcKeyHigh = src_key->dwColorSpaceHighValue; }
        if (dst_key) { blt.dstKeyLow = dst_key->dwColorSpaceLowValue; blt.dstKeyHigh = dst_key->dwColorSpaceHighValue; }
        if (dst->clipper)
        {
            blt.clipped = 1;
            if (dst->clipper->window) blt.clipWindow = (DXU32)(ULONG_PTR)dst->clipper->window;
            else if (dst->clipper->list)
            {
                blt.clipCount = dst->clipper->list->rdh.nCount;
                blt.clipRects = (DXU32)(ULONG_PTR)dst->clipper->list->Buffer;
            }
            else return DDERR_NOCLIPLIST;
        }
        return DdDrvBlt(&blt, sizeof blt);
    }
    {
        DDBLIT_JOB job;
        RGNDATA *data = NULL;
        HRESULT result = DD_OK;
        int status;
        memset(&job, 0, sizeof job);
        job.dst.bits = dst->store->memory;
        job.dst.pitch = dst->store->pitch;
        job.dst.width = (int)dst->width;
        job.dst.height = (int)dst->height;
        job.dst.bytes = (int)PixelBytes(dst);
        if (src)
        {
            job.src.bits = src->store->memory;
            job.src.pitch = src->store->pitch;
            job.src.width = (int)src->width;
            job.src.height = (int)src->height;
            job.src.bytes = (int)PixelBytes(src);
            job.srect.left = srect->left; job.srect.top = srect->top; job.srect.right = srect->right; job.srect.bottom = srect->bottom;
        }
        job.drect.left = drect->left; job.drect.top = drect->top; job.drect.right = drect->right; job.drect.bottom = drect->bottom;
        job.flags = job_flags;
        job.fill = fill;
        if (src_key) { job.src_key_low = src_key->dwColorSpaceLowValue; job.src_key_high = src_key->dwColorSpaceHighValue; }
        if (dst_key) { job.dst_key_low = dst_key->dwColorSpaceLowValue; job.dst_key_high = dst_key->dwColorSpaceHighValue; }
        if (dst->clipper)
        {
            result = ClipRects(dst->clipper, &data, &job.clips, &job.clip_count);
            if (result != DD_OK || !job.clip_count)
            {
                free(data);
                return result;
            }
        }
        status = DdBlit(&job, NULL);
        free(data);
        return status == DDBLIT_OK ? DD_OK : status == DDBLIT_MEMORY ? DDERR_OUTOFMEMORY : DDERR_INVALIDPARAMS;
    }
}

static BOOL Inside(const RECT *r, const DdSurface *s)
{
    return r->left >= 0 && r->top >= 0 && r->right <= (LONG)s->width && r->bottom <= (LONG)s->height && r->left <= r->right &&
           r->top <= r->bottom;
}

static HRESULT WINAPI Dds_Blt(void *face, LPRECT dest_rect, void *source, LPRECT source_rect, DWORD flags, LPDDBLTFX fx)
{
    DD_METHOD_LOCK();
    DdSurface *dst = This(face), *src = DdSurfaceOf(source);
    /* The alpha and Z flags: the original takes them and blits as if they
       were not there (measured: DDBLT_ALPHASRC and DDBLT_ZBUFFER answer
       DD_OK with a plain copy). */
    const DWORD passed_over = DDBLT_ALPHADEST | DDBLT_ALPHADESTCONSTOVERRIDE | DDBLT_ALPHADESTNEG | DDBLT_ALPHADESTSURFACEOVERRIDE |
                              DDBLT_ALPHAEDGEBLEND | DDBLT_ALPHASRC | DDBLT_ALPHASRCCONSTOVERRIDE | DDBLT_ALPHASRCNEG |
                              DDBLT_ALPHASRCSURFACEOVERRIDE | DDBLT_ZBUFFER | DDBLT_ZBUFFERDESTCONSTOVERRIDE |
                              DDBLT_ZBUFFERDESTOVERRIDE | DDBLT_ZBUFFERSRCCONSTOVERRIDE | DDBLT_ZBUFFERSRCOVERRIDE;
    const DWORD served = DDBLT_ASYNC | DDBLT_COLORFILL | DDBLT_DDFX | DDBLT_KEYDEST | DDBLT_KEYDESTOVERRIDE | DDBLT_KEYSRC |
                         DDBLT_KEYSRCOVERRIDE | DDBLT_ROP | DDBLT_WAIT | DDBLT_DEPTHFILL | DDBLT_DONOTWAIT | passed_over;
    RECT drect, srect;
    unsigned job = 0;
    DWORD fill = 0;
    const DDCOLORKEY *src_key = NULL, *dst_key = NULL;
    BOOL filling = FALSE;

    if (dst->dead || (src && src->dead)) return DDERR_INVALIDOBJECT;
    /* Rotation and the raster operations of DirectDraw are beyond this
       blitter, here and in the driver. */
    if (flags & DDBLT_ROTATIONANGLE) return DDERR_NOROTATIONHW;
    if (flags & DDBLT_DDROPS) return DDERR_NODDROPSHW;
    if (flags & ~served) return DDERR_INVALIDPARAMS;
    if ((flags & (DDBLT_COLORFILL | DDBLT_DDFX | DDBLT_ROP | DDBLT_DEPTHFILL | DDBLT_KEYDESTOVERRIDE | DDBLT_KEYSRCOVERRIDE)) &&
        (!fx || fx->dwSize != sizeof *fx))
        return DDERR_INVALIDPARAMS;
    if (flags & DDBLT_COLORFILL) { filling = TRUE; fill = fx->dwFillColor; }
    else if (flags & DDBLT_DEPTHFILL)
    {
        if (!(dst->format.flags & DDPF_ZBUFFER)) return DDERR_INVALIDPARAMS;
        filling = TRUE;
        fill = fx->dwFillDepth;
        job |= NEO98DX_BLT_DEPTHFILL;
    }
    else if (flags & DDBLT_ROP)
    {
        if (fx->dwROP == 0x00000042) { filling = TRUE; fill = 0; }                  /* BLACKNESS */
        else if (fx->dwROP == 0x00FF0062) { filling = TRUE; fill = 0xffffffffu; }   /* WHITENESS */
        else if (fx->dwROP != 0x00CC0020) return DDERR_NORASTEROPHW;                /* SRCCOPY */
    }
    if (filling) job |= NEO98DX_BLT_FILL;
    else if (!src) return DDERR_INVALIDPARAMS;
    if (flags & DDBLT_DDFX)
    {
        if (fx->dwDDFX & ~(DDBLTFX_MIRRORLEFTRIGHT | DDBLTFX_MIRRORUPDOWN | DDBLTFX_NOTEARING)) return DDERR_NOROTATIONHW;
        if (fx->dwDDFX & DDBLTFX_MIRRORLEFTRIGHT) job |= NEO98DX_BLT_MIRRORX;
        if (fx->dwDDFX & DDBLTFX_MIRRORUPDOWN) job |= NEO98DX_BLT_MIRRORY;
    }
    if (flags & DDBLT_KEYSRCOVERRIDE) { src_key = &fx->ddckSrcColorkey; job |= NEO98DX_BLT_KEYSRC; }
    else if (flags & DDBLT_KEYSRC)
    {
        /* A keyed blit of a surface without that key is a wrong parameter
           (measured; DDERR_NOCOLORKEY is the answer of GetColorKey). */
        if (!src || !(src->key_flags & DDSD_CKSRCBLT)) return DDERR_INVALIDPARAMS;
        src_key = &src->key_src_blt;
        job |= NEO98DX_BLT_KEYSRC;
    }
    if (flags & DDBLT_KEYDESTOVERRIDE) { dst_key = &fx->ddckDestColorkey; job |= NEO98DX_BLT_KEYDEST; }
    else if (flags & DDBLT_KEYDEST)
    {
        if (!(dst->key_flags & DDSD_CKDESTBLT)) return DDERR_INVALIDPARAMS;
        dst_key = &dst->key_dest_blt;
        job |= NEO98DX_BLT_KEYDEST;
    }

    if (dest_rect) drect = *dest_rect;
    else { drect.left = 0; drect.top = 0; drect.right = (LONG)dst->width; drect.bottom = (LONG)dst->height; }
    if (drect.right < drect.left || drect.bottom < drect.top) return DDERR_INVALIDRECT;
    /* Without a clipper the rectangle has to lie on the surface; a clipper
       cuts whatever it is given. */
    if (!dst->clipper && !Inside(&drect, dst)) return DDERR_INVALIDRECT;
    if (!filling)
    {
        if (source_rect) srect = *source_rect;
        else { srect.left = 0; srect.top = 0; srect.right = (LONG)src->width; srect.bottom = (LONG)src->height; }
        if (!Inside(&srect, src)) return DDERR_INVALIDRECT;
    }
    if (drect.right == drect.left || drect.bottom == drect.top) return DD_OK;
    return RunBlit(dst, &drect, filling ? NULL : src, filling ? NULL : &srect, job, fill, src_key, dst_key);
}

static HRESULT WINAPI Dds_BltBatch(void *face, LPDDBLTBATCH batch, DWORD count, DWORD flags)
{
    DD_METHOD_LOCK();
    (void)face;
    (void)batch;
    (void)count;
    (void)flags;
    return DDERR_UNSUPPORTED;   /* DirectDraw never implemented it either */
}

static HRESULT WINAPI Dds_BltFast(void *face, DWORD x, DWORD y, void *source, LPRECT source_rect, DWORD trans)
{
    DD_METHOD_LOCK();
    DdSurface *dst = This(face), *src = DdSurfaceOf(source);
    RECT drect, srect;
    unsigned job = 0;
    const DDCOLORKEY *src_key = NULL, *dst_key = NULL;
    if (!src) return DDERR_INVALIDPARAMS;
    if (dst->dead || src->dead) return DDERR_INVALIDOBJECT;
    if (dst->clipper) return DDERR_BLTFASTCANTCLIP;
    if (source_rect) srect = *source_rect;
    else { srect.left = 0; srect.top = 0; srect.right = (LONG)src->width; srect.bottom = (LONG)src->height; }
    if (!Inside(&srect, src)) return DDERR_INVALIDRECT;
    drect.left = (LONG)x;
    drect.top = (LONG)y;
    drect.right = drect.left + (srect.right - srect.left);
    drect.bottom = drect.top + (srect.bottom - srect.top);
    if (!Inside(&drect, dst)) return DDERR_INVALIDRECT;
    if (trans & DDBLTFAST_SRCCOLORKEY)
    {
        if (!(src->key_flags & DDSD_CKSRCBLT)) return DDERR_INVALIDPARAMS;
        src_key = &src->key_src_blt;
        job |= NEO98DX_BLT_KEYSRC;
    }
    if (trans & DDBLTFAST_DESTCOLORKEY)
    {
        if (!(dst->key_flags & DDSD_CKDESTBLT)) return DDERR_INVALIDPARAMS;
        dst_key = &dst->key_dest_blt;
        job |= NEO98DX_BLT_KEYDEST;
    }
    if (drect.right == drect.left || drect.bottom == drect.top) return DD_OK;
    return RunBlit(dst, &drect, src, &srect, job, 0, src_key, dst_key);
}

static HRESULT WINAPI Dds_GetBltStatus(void *face, DWORD flags)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    if (flags != DDGBS_CANBLT && flags != DDGBS_ISBLTDONE) return DDERR_INVALIDPARAMS;
    if (s->dead) return DDERR_INVALIDOBJECT;
    if (flags == DDGBS_CANBLT && (s->locks || s->dc)) return DDERR_SURFACEBUSY;
    return DD_OK;   /* a blit is done when its call returns */
}

/* ------------------------------------------------------------------- chains */
static HRESULT WINAPI Dds_Flip(void *face, void *target, DWORD flags)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face), *override = DdSurfaceOf(target), *walk;
    DWORD chain[16];
    unsigned count = 0;
    BOOL found = FALSE;
    if (s->dead) return DDERR_INVALIDOBJECT;
    if (!(s->caps & DDSCAPS_FLIP) || !(s->caps & DDSCAPS_FRONTBUFFER) || !s->store->driver) return DDERR_NOTFLIPPABLE;
    if ((s->caps & DDSCAPS_PRIMARYSURFACE) && !(s->dd && s->dd->exclusive)) return DDERR_NOEXCLUSIVEMODE;
    for (walk = s; walk && count < 16; )
    {
        DdSurface *next = NULL;
        DdAttachment *a;
        if (walk->locks || walk->dc) return DDERR_SURFACEBUSY;
        chain[count++] = walk->store->driver;
        if (walk == override) found = TRUE;
        for (a = walk->attached; a && !next; a = a->next)
            if (a->implicit && (a->surface->caps & DDSCAPS_FLIP)) next = a->surface;
        walk = next;
    }
    if (count < 2) return DDERR_NOTFLIPPABLE;
    DdFlush3D();
    if (override)
    {
        /* The named surface becomes the front, the front takes its place. */
        if (!found || override == s) return DDERR_NOTFLIPPABLE;
        chain[1] = override->store->driver;
        count = 2;
    }
    return DdDrvFlip(chain, count, flags & ~NEO98DX_FLIP_TOGDI);
}

static HRESULT WINAPI Dds_GetFlipStatus(void *face, DWORD flags)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    if (flags != DDGFS_CANFLIP && flags != DDGFS_ISFLIPDONE) return DDERR_INVALIDPARAMS;
    if (s->dead) return DDERR_INVALIDOBJECT;
    return DD_OK;
}

static HRESULT WINAPI Dds_AddAttachedSurface(void *face, void *other)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face), *child = DdSurfaceOf(other);
    DdAttachment *a;
    HRESULT result;
    if (!child || child == s) return DDERR_INVALIDPARAMS;
    if (s->dead || child->dead) return DDERR_INVALIDOBJECT;
    for (a = s->attached; a; a = a->next)
        if (a->surface == child) return DDERR_SURFACEALREADYATTACHED;
    /* A Z buffer goes to the surface it serves; both are of one size. Other
       pairs are made together by CreateSurface. */
    if (!(child->caps & DDSCAPS_ZBUFFER)) return DDERR_CANNOTATTACHSURFACE;
    if (child->width != s->width || child->height != s->height) return DDERR_CANNOTATTACHSURFACE;
    for (a = s->attached; a; a = a->next)
        if (a->surface->caps & DDSCAPS_ZBUFFER) return DDERR_CANNOTATTACHSURFACE;
    result = Attach(s, child, FALSE);
    if (SUCCEEDED(result)) DdSurfaceAddRef(child);
    return result;
}

static HRESULT WINAPI Dds_DeleteAttachedSurface(void *face, DWORD flags, void *other)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face), *child = DdSurfaceOf(other);
    DdAttachment **link = &s->attached;
    BOOL any = FALSE;
    (void)flags;
    if (s->dead) return DDERR_INVALIDOBJECT;
    while (*link)
    {
        DdAttachment *a = *link;
        if (child && a->surface != child) { link = &a->next; continue; }
        if (a->implicit)
        {
            if (child) return DDERR_CANNOTDETACHSURFACE;
            link = &a->next;
            continue;
        }
        *link = a->next;
        if (s->store->driver && a->surface->store->driver) DdDrvAttach(s->store->driver, a->surface->store->driver, FALSE);
        DdSurfaceRelease(a->surface);
        free(a);
        any = TRUE;
    }
    return any || !child ? DD_OK : DDERR_SURFACENOTATTACHED;
}

static HRESULT WINAPI Dds_GetAttachedSurface(void *face, void *wanted, void **out)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face), *found = NULL;
    const unsigned version = Version(face);
    DdAttachment *a;
    DWORD caps, caps2 = 0;
    if (!wanted || !out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (s->dead) return DDERR_INVALIDOBJECT;
    caps = ((DDSCAPS *)wanted)->dwCaps;
    if (version >= DDS_V4) caps2 = ((DDSCAPS2 *)wanted)->dwCaps2;
    for (a = s->attached; a; a = a->next)
        if ((a->surface->caps & caps) == caps && (a->surface->caps2 & caps2) == caps2)
        {
            if (found) return DDERR_INVALIDPARAMS;   /* more than one fits */
            found = a->surface;
        }
    /* The last surface of a flipping chain leads back to the first. */
    if (!found && (s->caps & DDSCAPS_FLIP) && s->owner)
    {
        BOOL last = TRUE;
        DdSurface *root = s;
        for (a = s->attached; a; a = a->next)
            if (a->implicit && (a->surface->caps & DDSCAPS_FLIP)) last = FALSE;
        while (root->owner) root = root->owner;
        if (last && (root->caps & caps) == caps && (root->caps2 & caps2) == caps2) found = root;
    }
    if (!found) return DDERR_NOTFOUND;
    DdSurfaceAddRef(found);
    *out = &found->face[version];
    return DD_OK;
}

typedef HRESULT (WINAPI *AttachedCallback)(void *surface, void *description, LPVOID context);

static HRESULT WINAPI Dds_EnumAttachedSurfaces(void *face, LPVOID context, AttachedCallback callback)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    const unsigned version = Version(face);
    DdSurface *list[32];
    unsigned count = 0, i;
    DdAttachment *a;
    if (!callback) return DDERR_INVALIDPARAMS;
    if (s->dead) return DDERR_INVALIDOBJECT;
    for (a = s->attached; a && count < 32; a = a->next) list[count++] = a->surface;
    for (i = 0; i < count; ++i) DdSurfaceAddRef(list[i]);
    for (i = 0; i < count; ++i)
    {
        DDSURFACEDESC2 desc;
        DdSurfaceDescribe(list[i], &desc, version >= DDS_V4 ? sizeof(DDSURFACEDESC2) : sizeof(DDSURFACEDESC));
        if (callback(&list[i]->face[version], &desc, context) == DDENUMRET_CANCEL)
        {
            for (++i; i < count; ++i) DdSurfaceRelease(list[i]);
            break;
        }
    }
    return DD_OK;
}

/* ----------------------------------------------------------------- overlays */
/* This display has no overlays; no surface is one. */
static HRESULT WINAPI Dds_AddOverlayDirtyRect(void *face, LPRECT rect)
{
    DD_METHOD_LOCK();
    (void)face;
    (void)rect;
    return DDERR_NOTAOVERLAYSURFACE;
}

static HRESULT WINAPI Dds_EnumOverlayZOrders(void *face, DWORD flags, LPVOID context, void *callback)
{
    DD_METHOD_LOCK();
    (void)face;
    (void)flags;
    (void)context;
    (void)callback;
    return DDERR_NOTAOVERLAYSURFACE;
}

static HRESULT WINAPI Dds_GetOverlayPosition(void *face, LPLONG x, LPLONG y)
{
    DD_METHOD_LOCK();
    (void)face;
    (void)x;
    (void)y;
    return DDERR_NOTAOVERLAYSURFACE;
}

static HRESULT WINAPI Dds_SetOverlayPosition(void *face, LONG x, LONG y)
{
    DD_METHOD_LOCK();
    (void)face;
    (void)x;
    (void)y;
    return DDERR_NOTAOVERLAYSURFACE;
}

static HRESULT WINAPI Dds_UpdateOverlay(void *face, LPRECT source, void *destination, LPRECT dest, DWORD flags, void *fx)
{
    DD_METHOD_LOCK();
    (void)face;
    (void)source;
    (void)destination;
    (void)dest;
    (void)flags;
    (void)fx;
    return DDERR_NOTAOVERLAYSURFACE;
}

static HRESULT WINAPI Dds_UpdateOverlayDisplay(void *face, DWORD flags)
{
    DD_METHOD_LOCK();
    (void)face;
    (void)flags;
    return DDERR_NOTAOVERLAYSURFACE;
}

static HRESULT WINAPI Dds_UpdateOverlayZOrder(void *face, DWORD flags, void *reference)
{
    DD_METHOD_LOCK();
    (void)face;
    (void)flags;
    (void)reference;
    return DDERR_NOTAOVERLAYSURFACE;
}

/* --------------------------------------------------------------- properties */
static HRESULT WINAPI Dds_GetCaps(void *face, void *out)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    if (!out) return DDERR_INVALIDPARAMS;
    ((DDSCAPS *)out)->dwCaps = s->caps;
    if (Version(face) >= DDS_V4)
    {
        DDSCAPS2 *wide = (DDSCAPS2 *)out;
        wide->dwCaps2 = s->caps2;
        wide->dwCaps3 = s->caps3;
        wide->dwCaps4 = s->caps4;
    }
    return DD_OK;
}

static HRESULT WINAPI Dds_GetPixelFormat(void *face, LPDDPIXELFORMAT out)
{
    DD_METHOD_LOCK();
    if (!out || out->dwSize != sizeof *out) return DDERR_INVALIDPARAMS;
    DdFormatToPixelFormat(&This(face)->format, out);
    return DD_OK;
}

static HRESULT WINAPI Dds_GetSurfaceDesc(void *face, void *out)
{
    DD_METHOD_LOCK();
    if (!DescriptionSize(out, Version(face))) return DDERR_INVALIDPARAMS;
    DdSurfaceDescribe(This(face), out, ((DDSURFACEDESC *)out)->dwSize);
    return DD_OK;
}

static HRESULT WINAPI Dds_Initialize(void *face, void *dd, void *desc)
{
    DD_METHOD_LOCK();
    (void)face;
    (void)dd;
    (void)desc;
    return DDERR_ALREADYINITIALIZED;
}

static HRESULT WINAPI Dds_GetClipper(void *face, LPDIRECTDRAWCLIPPER *out)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (!s->clipper) return DDERR_NOCLIPPERATTACHED;
    DdClipperAddRef(s->clipper);
    *out = (LPDIRECTDRAWCLIPPER)&s->clipper->face;
    return DD_OK;
}

static HRESULT WINAPI Dds_SetClipper(void *face, LPDIRECTDRAWCLIPPER clipper)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    DdClipper *next = DdClipperOf(clipper);
    if (s->dead) return DDERR_INVALIDOBJECT;
    if (next == s->clipper) return DD_OK;
    if (next) DdClipperAddRef(next);
    if (s->clipper) DdClipperRelease(s->clipper);
    s->clipper = next;
    return DD_OK;
}

static DDCOLORKEY *KeyOf(DdSurface *s, DWORD which, DWORD *flag)
{
    switch (which & (DDCKEY_DESTBLT | DDCKEY_DESTOVERLAY | DDCKEY_SRCBLT | DDCKEY_SRCOVERLAY))
    {
    case DDCKEY_SRCBLT: *flag = DDSD_CKSRCBLT; return &s->key_src_blt;
    case DDCKEY_DESTBLT: *flag = DDSD_CKDESTBLT; return &s->key_dest_blt;
    case DDCKEY_SRCOVERLAY: *flag = DDSD_CKSRCOVERLAY; return &s->key_src_overlay;
    case DDCKEY_DESTOVERLAY: *flag = DDSD_CKDESTOVERLAY; return &s->key_dest_overlay;
    default: return NULL;
    }
}

static HRESULT WINAPI Dds_GetColorKey(void *face, DWORD which, LPDDCOLORKEY out)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    DWORD flag = 0;
    DDCOLORKEY *key = KeyOf(s, which, &flag);
    if (!out || !key) return DDERR_INVALIDPARAMS;
    if (!(s->key_flags & flag)) return DDERR_NOCOLORKEY;
    *out = *key;
    return DD_OK;
}

static HRESULT WINAPI Dds_SetColorKey(void *face, DWORD which, LPDDCOLORKEY in)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    DWORD flag = 0;
    DDCOLORKEY *key = KeyOf(s, which, &flag);
    if (!key) return DDERR_INVALIDPARAMS;
    if (s->dead) return DDERR_INVALIDOBJECT;
    if (in)
    {
        *key = *in;
        /* Without DDCKEY_COLORSPACE the key is the one colour of the low value. */
        if (!(which & DDCKEY_COLORSPACE)) key->dwColorSpaceHighValue = key->dwColorSpaceLowValue;
        s->key_flags |= flag;
    }
    else s->key_flags &= ~flag;
    ++s->uniqueness;
    if (flag == DDSD_CKSRCBLT && s->store->driver)
    {
        DdFlush3D();
        DdDrvSetColorKey(s->store->driver, in ? 1 : 0, key->dwColorSpaceLowValue, key->dwColorSpaceHighValue);
    }
    return DD_OK;
}

/* The colours of an eight bit surface for the driver: 0x00RRGGBB with the
   flags of the entry on top. */
static void DriverColors(const DdPalette *palette, DWORD *colors)
{
    unsigned i;
    for (i = 0; i < 256; ++i)
        colors[i] = (DWORD)palette->entries[i].peFlags << 24 | (DWORD)palette->entries[i].peRed << 16 |
                    (DWORD)palette->entries[i].peGreen << 8 | palette->entries[i].peBlue;
}

void DdSurfacePaletteChanged(DdSurface *s, DWORD start, DWORD count)
{
    DWORD colors[256];
    if (!s->palette || !s->store->driver) return;
    DriverColors(s->palette, colors);
    DdFlush3D();
    DdDrvSetPalette(s->store->driver, start, count, colors + start, (s->palette->caps & DDPCAPS_ALPHA) ? 1 : 0);
    ++s->uniqueness;
}

static HRESULT WINAPI Dds_GetPalette(void *face, LPDIRECTDRAWPALETTE *out)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (!s->palette) return DDERR_NOPALETTEATTACHED;
    DdPaletteAddRef(s->palette);
    *out = (LPDIRECTDRAWPALETTE)&s->palette->face;
    return DD_OK;
}

static HRESULT WINAPI Dds_SetPalette(void *face, LPDIRECTDRAWPALETTE palette)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    DdPalette *next = DdPaletteOf(palette);
    if (s->dead) return DDERR_INVALIDOBJECT;
    if (next && (!(s->format.flags & DDPF_PALETTEINDEXED8) || next->count != 256)) return DDERR_INVALIDPIXELFORMAT;
    if (next == s->palette) return DD_OK;
    if (next)
    {
        DdPaletteAddRef(next);
        DdPaletteAttach(next, s, TRUE);
    }
    if (s->palette)
    {
        DdPaletteAttach(s->palette, s, FALSE);
        DdPaletteRelease(s->palette);
    }
    s->palette = next;
    if (next) DdSurfacePaletteChanged(s, 0, 256);
    else if (s->store->driver)
    {
        DdFlush3D();
        DdDrvSetPalette(s->store->driver, 0, 0, NULL, 0);
    }
    return DD_OK;
}

/* ---------------------------------------------------------------------- GDI */
static HRESULT WINAPI Dds_GetDC(void *face, HDC *out)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    NEO98DX_DC request;
    DWORD colors[256];
    HRESULT result;
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (s->dead) return DDERR_INVALIDOBJECT;
    if (s->dc) return DDERR_DCALREADYCREATED;
    if (s->locks) return DDERR_SURFACEBUSY;
    memset(&request, 0, sizeof request);
    request.size = sizeof request;
    PlaneOf(s, &request.plane);
    if (s->palette)
    {
        unsigned i;
        DriverColors(s->palette, colors);
        for (i = 0; i < 256; ++i) colors[i] &= 0xffffff;
        request.colors = (DXU32)(ULONG_PTR)colors;
    }
    DdFlush3D();
    result = DdDrvGetDC(&request, sizeof request);
    if (result != DD_OK) return result;
    s->dc = (HDC)(LONG_PTR)(LONG)request.dc;
    *out = s->dc;
    return DD_OK;
}

static HRESULT WINAPI Dds_ReleaseDC(void *face, HDC dc)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    HRESULT result;
    if (!s->dc || s->dc != dc) return DDERR_NODC;
    result = DdDrvReleaseDC(dc);
    s->dc = NULL;
    ++s->uniqueness;
    return result;
}

/* --------------------------------------------------------- later interfaces */
static HRESULT WINAPI Dds_GetDDInterface(void *face, LPVOID *out)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (!s->dd) return DDERR_INVALIDOBJECT;
    /* The interface of DirectDraw the surface was made through (measured). */
    *out = DdObjectTake(s->dd, s->made_through);
    return DD_OK;
}

static HRESULT WINAPI Dds_PageLock(void *face, DWORD flags)
{
    DD_METHOD_LOCK();
    (void)face;
    (void)flags;
    return DD_OK;   /* no surface here is paged out */
}

static HRESULT WINAPI Dds_PageUnlock(void *face, DWORD flags)
{
    DD_METHOD_LOCK();
    (void)face;
    (void)flags;
    return DD_OK;
}

/* SetSurfaceDesc gives a surface in system memory the memory of the program. */
static HRESULT WINAPI Dds_SetSurfaceDesc(void *face, void *in, DWORD flags)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    DDSURFACEDESC2 desc;
    const DDSURFACEDESC *narrow = (const DDSURFACEDESC *)in;
    DdStore *store;
    if (flags || !DescriptionSize(in, Version(face))) return DDERR_INVALIDPARAMS;
    memset(&desc, 0, sizeof desc);
    memcpy(&desc, in, narrow->dwSize == sizeof desc ? sizeof desc : offsetof(DDSURFACEDESC, ddsCaps));
    if (s->dead) return DDERR_INVALIDOBJECT;
    if (s->store->driver || s->locks || s->dc || s->owner || s->attached) return DDERR_INVALIDSURFACETYPE;
    if (!(desc.dwFlags & DDSD_LPSURFACE) || !desc.lpSurface) return DDERR_INVALIDPARAMS;
    if (desc.dwFlags & ~(DDSD_LPSURFACE | DDSD_PITCH | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT)) return DDERR_INVALIDPARAMS;
    if (desc.dwFlags & DDSD_PIXELFORMAT)
    {
        NEO98DX_FORMAT format;
        if (!DdPixelFormatToFormat(&desc.ddpfPixelFormat, &format)) return DDERR_INVALIDPIXELFORMAT;
        s->format = format;
        s->own_format = TRUE;
    }
    if (desc.dwFlags & DDSD_WIDTH) s->width = desc.dwWidth;
    if (desc.dwFlags & DDSD_HEIGHT) s->height = desc.dwHeight;
    /* A store other surfaces share stays theirs. */
    store = s->store;
    if (store->references > 1)
    {
        DdStore *fresh = (DdStore *)DdAllocate(sizeof *fresh);
        if (!fresh) return DDERR_OUTOFMEMORY;
        fresh->references = 1;
        StoreRelease(store);
        s->store = store = fresh;
    }
    else if (store->memory && store->own_memory) free(store->memory);
    store->memory = (BYTE *)desc.lpSurface;
    store->own_memory = FALSE;
    store->pitch = (desc.dwFlags & DDSD_PITCH) ? desc.lPitch : (LONG)((s->width * PixelBytes(s) + 3) & ~3u);
    ++s->uniqueness;
    return DD_OK;
}

static DdPrivate **FindPrivate(DdSurface *s, const GUID *tag)
{
    DdPrivate **link;
    for (link = &s->private_data; *link; link = &(*link)->next)
        if (IsEqualGUID(&(*link)->tag, tag)) return link;
    return NULL;
}

static HRESULT WINAPI Dds_FreePrivateData(void *face, REFGUID tag)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    DdPrivate **link = tag ? FindPrivate(s, tag) : NULL, *entry;
    if (!link) return DDERR_NOTFOUND;
    entry = *link;
    *link = entry->next;
    if ((entry->flags & 1) && entry->data) IUnknown_Release(*(IUnknown **)entry->data);
    free(entry->data);
    free(entry);
    return DD_OK;
}

static HRESULT WINAPI Dds_SetPrivateData(void *face, REFGUID tag, LPVOID data, DWORD size, DWORD flags)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    DdPrivate *entry;
    if (!tag || !data || !size) return DDERR_INVALIDPARAMS;
    if ((flags & 1) && size != sizeof(IUnknown *)) return DDERR_INVALIDPARAMS;   /* DDSPD_IUNKNOWNPOINTER */
    Dds_FreePrivateData(face, tag);
    entry = (DdPrivate *)DdAllocate(sizeof *entry);
    if (!entry) return DDERR_OUTOFMEMORY;
    entry->data = malloc(size);
    if (!entry->data)
    {
        free(entry);
        return DDERR_OUTOFMEMORY;
    }
    memcpy(entry->data, data, size);
    entry->tag = *tag;
    entry->size = size;
    entry->flags = flags;
    if (flags & 1) IUnknown_AddRef(*(IUnknown **)entry->data);
    entry->next = s->private_data;
    s->private_data = entry;
    return DD_OK;
}

static HRESULT WINAPI Dds_GetPrivateData(void *face, REFGUID tag, LPVOID buffer, LPDWORD size)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    DdPrivate **link = tag ? FindPrivate(s, tag) : NULL;
    if (!size) return DDERR_INVALIDPARAMS;
    if (!link) return DDERR_NOTFOUND;
    if (!buffer || *size < (*link)->size)
    {
        *size = (*link)->size;
        return DDERR_MOREDATA;
    }
    memcpy(buffer, (*link)->data, (*link)->size);
    *size = (*link)->size;
    if ((*link)->flags & 1) IUnknown_AddRef(*(IUnknown **)buffer);
    return DD_OK;
}

static HRESULT WINAPI Dds_GetUniquenessValue(void *face, LPDWORD value)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    if (!value) return DDERR_INVALIDPARAMS;
    /* Zero says the surface may change at any moment; a locked one does. */
    *value = s->locks || s->dc ? 0 : s->uniqueness + 1;
    return DD_OK;
}

static HRESULT WINAPI Dds_ChangeUniquenessValue(void *face)
{
    DD_METHOD_LOCK();
    ++This(face)->uniqueness;
    return DD_OK;
}

/* Priority and level of detail belong to textures the texture manager keeps. */
static HRESULT WINAPI Dds_SetPriority(void *face, DWORD priority)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    if (!(s->caps2 & (DDSCAPS2_TEXTUREMANAGE | DDSCAPS2_D3DTEXTUREMANAGE))) return DDERR_INVALIDOBJECT;
    s->priority = priority;
    return DD_OK;
}

static HRESULT WINAPI Dds_GetPriority(void *face, LPDWORD priority)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    if (!priority) return DDERR_INVALIDPARAMS;
    if (!(s->caps2 & (DDSCAPS2_TEXTUREMANAGE | DDSCAPS2_D3DTEXTUREMANAGE))) return DDERR_INVALIDOBJECT;
    *priority = s->priority;
    return DD_OK;
}

static HRESULT WINAPI Dds_SetLOD(void *face, DWORD lod)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    if (!(s->caps2 & (DDSCAPS2_TEXTUREMANAGE | DDSCAPS2_D3DTEXTUREMANAGE))) return DDERR_INVALIDOBJECT;
    if (lod && lod >= s->mip_count) return DDERR_INVALIDPARAMS;
    s->lod = lod;
    return DD_OK;
}

static HRESULT WINAPI Dds_GetLOD(void *face, LPDWORD lod)
{
    DD_METHOD_LOCK();
    DdSurface *s = This(face);
    if (!lod) return DDERR_INVALIDPARAMS;
    if (!(s->caps2 & (DDSCAPS2_TEXTUREMANAGE | DDSCAPS2_D3DTEXTUREMANAGE))) return DDERR_INVALIDOBJECT;
    *lod = s->lod;
    return DD_OK;
}

/* ------------------------------------------------- IDirectDrawGammaControl */
static HRESULT WINAPI Gamma_QueryInterface(void *face, REFIID iid, void **out)
{
    DD_METHOD_LOCK();
    return Dds_QueryInterface(&This(face)->face[DDS_V1], iid, out);
}

static HRESULT WINAPI Gamma_GetGammaRamp(void *face, DWORD flags, LPDDGAMMARAMP ramp)
{
    DD_METHOD_LOCK();
    if (flags || !ramp || !(This(face)->caps & DDSCAPS_PRIMARYSURFACE)) return DDERR_INVALIDPARAMS;
    return DdDrvGetGammaRamp(ramp);
}

static HRESULT WINAPI Gamma_SetGammaRamp(void *face, DWORD flags, LPDDGAMMARAMP ramp)
{
    DD_METHOD_LOCK();
    (void)flags;   /* DDSGR_CALIBRATE: this display has no calibrator */
    if (!ramp || !(This(face)->caps & DDSCAPS_PRIMARYSURFACE)) return DDERR_INVALIDPARAMS;
    return DdDrvSetGammaRamp(ramp);
}

static const struct IDirectDrawGammaControlVtbl gamma_table = {
    .QueryInterface = (void *)Gamma_QueryInterface, .AddRef = (void *)Dds_AddRef, .Release = (void *)Dds_Release,
    .GetGammaRamp = (void *)Gamma_GetGammaRamp, .SetGammaRamp = (void *)Gamma_SetGammaRamp};

/* ------------------------------------------------------------ method tables */
#define DDS_COMMON \
    .QueryInterface = (void *)Dds_QueryInterface, .AddRef = (void *)Dds_AddRef, .Release = (void *)Dds_Release, \
    .AddAttachedSurface = (void *)Dds_AddAttachedSurface, .AddOverlayDirtyRect = (void *)Dds_AddOverlayDirtyRect, \
    .Blt = (void *)Dds_Blt, .BltBatch = (void *)Dds_BltBatch, .BltFast = (void *)Dds_BltFast, \
    .DeleteAttachedSurface = (void *)Dds_DeleteAttachedSurface, .EnumAttachedSurfaces = (void *)Dds_EnumAttachedSurfaces, \
    .EnumOverlayZOrders = (void *)Dds_EnumOverlayZOrders, .Flip = (void *)Dds_Flip, \
    .GetAttachedSurface = (void *)Dds_GetAttachedSurface, .GetBltStatus = (void *)Dds_GetBltStatus, \
    .GetCaps = (void *)Dds_GetCaps, .GetClipper = (void *)Dds_GetClipper, .GetColorKey = (void *)Dds_GetColorKey, \
    .GetDC = (void *)Dds_GetDC, .GetFlipStatus = (void *)Dds_GetFlipStatus, \
    .GetOverlayPosition = (void *)Dds_GetOverlayPosition, .GetPalette = (void *)Dds_GetPalette, \
    .GetPixelFormat = (void *)Dds_GetPixelFormat, .GetSurfaceDesc = (void *)Dds_GetSurfaceDesc, \
    .Initialize = (void *)Dds_Initialize, .IsLost = (void *)Dds_IsLost, .Lock = (void *)Dds_Lock, \
    .ReleaseDC = (void *)Dds_ReleaseDC, .Restore = (void *)Dds_Restore, .SetClipper = (void *)Dds_SetClipper, \
    .SetColorKey = (void *)Dds_SetColorKey, .SetOverlayPosition = (void *)Dds_SetOverlayPosition, \
    .SetPalette = (void *)Dds_SetPalette, .Unlock = (void *)Dds_Unlock, .UpdateOverlay = (void *)Dds_UpdateOverlay, \
    .UpdateOverlayDisplay = (void *)Dds_UpdateOverlayDisplay, .UpdateOverlayZOrder = (void *)Dds_UpdateOverlayZOrder
#define DDS_V2_MORE \
    .GetDDInterface = (void *)Dds_GetDDInterface, .PageLock = (void *)Dds_PageLock, .PageUnlock = (void *)Dds_PageUnlock
#define DDS_V4_MORE \
    .SetSurfaceDesc = (void *)Dds_SetSurfaceDesc, .SetPrivateData = (void *)Dds_SetPrivateData, \
    .GetPrivateData = (void *)Dds_GetPrivateData, .FreePrivateData = (void *)Dds_FreePrivateData, \
    .GetUniquenessValue = (void *)Dds_GetUniquenessValue, .ChangeUniquenessValue = (void *)Dds_ChangeUniquenessValue

static const struct IDirectDrawSurfaceVtbl surface_table1 = {DDS_COMMON};
static const struct IDirectDrawSurface2Vtbl surface_table2 = {DDS_COMMON, DDS_V2_MORE};
static const struct IDirectDrawSurface3Vtbl surface_table3 = {DDS_COMMON, DDS_V2_MORE, .SetSurfaceDesc = (void *)Dds_SetSurfaceDesc};
static const struct IDirectDrawSurface4Vtbl surface_table4 = {DDS_COMMON, DDS_V2_MORE, DDS_V4_MORE};
static const struct IDirectDrawSurface7Vtbl surface_table7 = {DDS_COMMON, DDS_V2_MORE, DDS_V4_MORE,
                                                              .SetPriority = (void *)Dds_SetPriority,
                                                              .GetPriority = (void *)Dds_GetPriority,
                                                              .SetLOD = (void *)Dds_SetLOD, .GetLOD = (void *)Dds_GetLOD};
