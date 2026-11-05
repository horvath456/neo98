/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Direct3D 7 in the DDRAW.DLL of ARM64 processes: IDirect3D7, the interface
   IDirectDraw7 hands out through QueryInterface, and IDirect3DVertexBuffer7.
    */
#include "d3d_private.h"

const GUID d3d_iid_direct3d7 = {0xf5049e77, 0x4861, 0x11d2, {0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8}};
const GUID d3d_iid_device7 = {0xf5049e79, 0x4861, 0x11d2, {0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8}};
const GUID d3d_iid_vertexbuffer7 = {0xf5049e7d, 0x4861, 0x11d2, {0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8}};
const GUID d3d_iid_hal = {0x84e63de0, 0x46aa, 0x11cf, {0x81, 0x6f, 0x00, 0x00, 0xc0, 0x20, 0x15, 0x6e}};
const GUID d3d_iid_tnlhal = {0xf5049e78, 0x4861, 0x11d2, {0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8}};

/* What the rasterizer of the host does; FALSE without one. */
BOOL D3dCaps(NEO98D3D_CAPS *caps)
{
    memset(caps, 0, sizeof *caps);
    return D3dDrvGetCaps(caps, sizeof *caps) == DD_OK && caps->size == sizeof *caps;
}

/* The description of a device class. The HAL device and the
   transform-and-lighting device are the same rasterizer. In the list of
   EnumDevices the first does not name D3DDEVCAPS_HWTRANSFORMANDLIGHT, as a
   program expects of it; GetCaps of a device answers what the driver can,
   like the original (measured, tools/test_arm64_d3d.py). */
void D3dDescribe(const NEO98D3D_CAPS *caps, const GUID *device, D3DDEVICEDESC7 *desc, BOOL enumerating)
{
    D3DPRIMCAPS prim;
    memset(desc, 0, sizeof *desc);
    memset(&prim, 0, sizeof prim);
    prim.dwSize = sizeof prim;
    prim.dwMiscCaps = caps->miscCaps;
    prim.dwRasterCaps = caps->rasterCaps;
    prim.dwZCmpCaps = caps->zCmpCaps;
    prim.dwSrcBlendCaps = caps->srcBlendCaps;
    prim.dwDestBlendCaps = caps->destBlendCaps;
    prim.dwAlphaCmpCaps = caps->alphaCmpCaps;
    prim.dwShadeCaps = caps->shadeCaps;
    prim.dwTextureCaps = caps->textureCaps;
    prim.dwTextureFilterCaps = caps->textureFilterCaps;
    prim.dwTextureBlendCaps = caps->textureBlendCaps;
    prim.dwTextureAddressCaps = caps->textureAddressCaps;
    desc->dwDevCaps = caps->devCaps;
    if (enumerating && !IsEqualGUID(device, &d3d_iid_tnlhal)) desc->dwDevCaps &= ~(DWORD)D3DDEVCAPS_HWTRANSFORMANDLIGHT;
    desc->dpcLineCaps = prim;
    desc->dpcTriCaps = prim;
    desc->dwDeviceRenderBitDepth = caps->renderBitDepth;
    desc->dwDeviceZBufferBitDepth = caps->zBufferBitDepth;
    desc->dwMinTextureWidth = caps->minTextureWidth;
    desc->dwMinTextureHeight = caps->minTextureHeight;
    desc->dwMaxTextureWidth = caps->maxTextureWidth;
    desc->dwMaxTextureHeight = caps->maxTextureHeight;
    desc->dwMaxTextureRepeat = caps->maxTextureRepeat;
    desc->dwMaxTextureAspectRatio = caps->maxTextureAspectRatio;
    desc->dwMaxAnisotropy = caps->maxAnisotropy;
    desc->dvGuardBandLeft = desc->dvGuardBandTop = desc->dvGuardBandRight = desc->dvGuardBandBottom = 0.0f;
    desc->dvExtentsAdjust = 0.0f;
    desc->dwStencilCaps = caps->stencilCaps;
    desc->dwFVFCaps = caps->fvfCaps;
    desc->dwTextureOpCaps = caps->textureOpCaps;
    desc->wMaxTextureBlendStages = (WORD)caps->maxTextureBlendStages;
    desc->wMaxSimultaneousTextures = (WORD)caps->maxSimultaneousTextures;
    /* The HAL device of the original lights in software, any number of lights. */
    desc->dwMaxActiveLights = IsEqualGUID(device, &d3d_iid_tnlhal) ? caps->maxActiveLights : 0xffffffffu;
    memcpy(&desc->dvMaxVertexW, &caps->maxVertexW, 4);
    desc->deviceGUID = *device;
    desc->wMaxUserClipPlanes = (WORD)caps->maxUserClipPlanes;
    desc->wMaxVertexBlendMatrices = (WORD)caps->maxVertexBlendMatrices;
    desc->dwVertexProcessingCaps = caps->vertexProcessingCaps;
}

/* -------------------------------------------------------------- IDirect3D7 */
static D3dObject *This(void *face) { return (D3dObject *)((DdFace *)face)->object; }

static HRESULT WINAPI D3d_QueryInterface(void *face, REFIID iid, void **out)
{
    D3dObject *d3d = This(face);
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (!iid) return DDERR_INVALIDPARAMS;
    if (IsEqualGUID(iid, &d3d_iid_direct3d7))
    {
        DdAddRef(&d3d->references);
        *out = &d3d->face;
        return S_OK;
    }
    /* Every other interface is one of the DirectDraw object it belongs to. */
    return IUnknown_QueryInterface((IUnknown *)DdObjectFace(d3d->dd, DD_V7), iid, out);
}

static ULONG WINAPI D3d_AddRef(void *face) { return (ULONG)DdAddRef(&This(face)->references); }

static ULONG WINAPI D3d_Release(void *face)
{
    D3dObject *d3d = This(face);
    const LONG left = DdDropRef(&d3d->references);
    if (left > 0) return (ULONG)left;
    DdObjectRelease(d3d->dd);
    free(d3d);
    return 0;
}

static char hal_description[] = "Microsoft Direct3D Hardware acceleration through Direct3D HAL", hal_name[] = "Direct3D HAL";
static char tnl_description[] = "Microsoft Direct3D Hardware Transform and Lighting acceleration capable device",
            tnl_name[] = "Direct3D T&L HAL";

/* The devices: the rasterizer of the host as the HAL device, and as the
   transform-and-lighting device when it runs the vertex pipeline. There is
   no software rasterizer, so no RGB device. */
static HRESULT WINAPI D3d_EnumDevices(void *face, LPD3DENUMDEVICESCALLBACK7 callback, LPVOID context)
{
    NEO98D3D_CAPS caps;
    D3DDEVICEDESC7 desc;
    (void)face;
    if (!callback) return DDERR_INVALIDPARAMS;
    if (!D3dCaps(&caps)) return D3D_OK;
    D3dDescribe(&caps, &d3d_iid_hal, &desc, TRUE);
    if (callback(hal_description, hal_name, &desc, context) != D3DENUMRET_OK) return D3D_OK;
    if (caps.flags & NEO98D3D_CAP_TNL)
    {
        D3dDescribe(&caps, &d3d_iid_tnlhal, &desc, TRUE);
        callback(tnl_description, tnl_name, &desc, context);
    }
    return D3D_OK;
}

static HRESULT WINAPI D3d_CreateDevice(void *face, REFCLSID guid, LPDIRECTDRAWSURFACE7 surface, LPDIRECT3DDEVICE7 *out)
{
    D3dObject *d3d = This(face);
    D3dDevice *device = NULL;
    HRESULT result;
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (!guid || !surface) return DDERR_INVALIDPARAMS;
    result = D3dCreateDevice(d3d->dd, guid, DdSurfaceOf(surface), &device);
    if (FAILED(result)) return result;
    *out = (LPDIRECT3DDEVICE7)&device->face;
    return D3D_OK;
}

static HRESULT WINAPI D3d_EnumZBufferFormats(void *face, REFCLSID device, LPD3DENUMPIXELFORMATSCALLBACK callback, LPVOID context)
{
    NEO98D3D_CAPS caps;
    DWORD i;
    (void)face;
    if (!callback || !device) return DDERR_INVALIDPARAMS;
    if (!IsEqualGUID(device, &d3d_iid_hal) && !IsEqualGUID(device, &d3d_iid_tnlhal)) return DDERR_NOZBUFFERHW;
    if (!D3dCaps(&caps)) return DDERR_NOZBUFFERHW;
    for (i = 0; i < caps.zFormatCount; ++i)
    {
        DDPIXELFORMAT format;
        memset(&format, 0, sizeof format);
        format.dwSize = sizeof format;
        format.dwFlags = caps.zFormats[i].flags;
        format.dwZBufferBitDepth = caps.zFormats[i].bits;
        format.dwStencilBitDepth = caps.zFormats[i].r;
        format.dwZBitMask = caps.zFormats[i].g;
        format.dwStencilBitMask = caps.zFormats[i].b;
        if (callback(&format, context) != D3DENUMRET_OK) break;
    }
    return D3D_OK;
}

static HRESULT WINAPI D3d_EvictManagedTextures(void *face)
{
    /* A managed texture is a surface of the driver that is never lost; what
       the rasterizer keeps of it follows its bytes by itself. */
    (void)face;
    D3dFlushAll();
    return D3D_OK;
}

/* ------------------------------------------------- IDirect3DVertexBuffer7 */
static D3dVertexBuffer *Buffer(void *face) { return (D3dVertexBuffer *)((DdFace *)face)->object; }
D3dVertexBuffer *D3dVertexBufferOf(void *face) { return face ? Buffer(face) : NULL; }

static HRESULT WINAPI Vb_QueryInterface(void *face, REFIID iid, void **out)
{
    D3dVertexBuffer *vb = Buffer(face);
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (!iid) return DDERR_INVALIDPARAMS;
    if (!IsEqualGUID(iid, &dd_iid_unknown) && !IsEqualGUID(iid, &d3d_iid_vertexbuffer7)) return E_NOINTERFACE;
    DdAddRef(&vb->references);
    *out = &vb->face;
    return S_OK;
}

static ULONG WINAPI Vb_AddRef(void *face) { return (ULONG)DdAddRef(&Buffer(face)->references); }

static ULONG WINAPI Vb_Release(void *face)
{
    D3dVertexBuffer *vb = Buffer(face);
    const LONG left = DdDropRef(&vb->references);
    if (left > 0) return (ULONG)left;
    /* A batch may still name vertices copied from here; they are copies. */
    DdObjectRelease(vb->dd);
    free(vb->memory);
    free(vb);
    return 0;
}

static HRESULT WINAPI Vb_Lock(void *face, DWORD flags, LPVOID *data, LPDWORD size)
{
    D3dVertexBuffer *vb = Buffer(face);
    (void)flags;
    if (!data) return DDERR_INVALIDPARAMS;
    if (vb->caps & D3DVBCAPS_OPTIMIZED) return D3DERR_VERTEXBUFFEROPTIMIZED;
    DdAddRef(&vb->locks);
    *data = vb->memory;
    if (size) *size = vb->count * vb->stride;
    return D3D_OK;
}

static HRESULT WINAPI Vb_Unlock(void *face)
{
    D3dVertexBuffer *vb = Buffer(face);
    if (vb->locks <= 0) return D3DERR_VERTEXBUFFERUNLOCKFAILED;
    DdDropRef(&vb->locks);
    return D3D_OK;
}

static HRESULT WINAPI Vb_ProcessVerticesStrided(void *face, DWORD operation, DWORD dest_index, DWORD count,
                                                LPD3DDRAWPRIMITIVESTRIDEDDATA source, DWORD source_fvf, LPDIRECT3DDEVICE7 device,
                                                DWORD flags)
{
    D3dVertexBuffer *vb = Buffer(face);
    if (!source || !device) return DDERR_INVALIDPARAMS;
    if (vb->caps & D3DVBCAPS_OPTIMIZED) return D3DERR_VERTEXBUFFEROPTIMIZED;
    if (dest_index > vb->count || count > vb->count - dest_index) return DDERR_INVALIDPARAMS;
    return D3dProcessVertices(D3dDeviceOf(device), operation, vb->memory + (size_t)dest_index * vb->stride, vb->fvf, source, source_fvf,
                              count, flags);
}

static HRESULT WINAPI Vb_ProcessVertices(void *face, DWORD operation, DWORD dest_index, DWORD count, LPDIRECT3DVERTEXBUFFER7 source,
                                         DWORD source_index, LPDIRECT3DDEVICE7 device, DWORD flags)
{
    D3dVertexBuffer *from = D3dVertexBufferOf(source);
    D3DDRAWPRIMITIVESTRIDEDDATA strided;
    D3dLayout layout;
    if (!from || !device) return DDERR_INVALIDPARAMS;
    if (source_index > from->count || count > from->count - source_index) return DDERR_INVALIDPARAMS;
    if (!D3dLayoutOf(from->fvf, &layout)) return D3DERR_INVALIDVERTEXFORMAT;
    D3dStridedOf(from->memory + (size_t)source_index * from->stride, from->fvf, &layout, &strided);
    return Vb_ProcessVerticesStrided(face, operation, dest_index, count, &strided, from->fvf, device, flags);
}

static HRESULT WINAPI Vb_GetVertexBufferDesc(void *face, LPD3DVERTEXBUFFERDESC desc)
{
    D3dVertexBuffer *vb = Buffer(face);
    if (!desc) return DDERR_INVALIDPARAMS;
    desc->dwSize = sizeof *desc;
    desc->dwCaps = vb->caps;
    desc->dwFVF = vb->fvf;
    desc->dwNumVertices = vb->count;
    return D3D_OK;
}

/* Optimize lets a driver reorder the buffer for itself; after it the program
   may not lock it again. Nothing is reordered here. */
static HRESULT WINAPI Vb_Optimize(void *face, LPDIRECT3DDEVICE7 device, DWORD flags)
{
    D3dVertexBuffer *vb = Buffer(face);
    (void)flags;
    if (!device) return DDERR_INVALIDPARAMS;
    if (vb->caps & D3DVBCAPS_OPTIMIZED) return D3DERR_VERTEXBUFFEROPTIMIZED;
    if (vb->locks > 0) return D3DERR_VERTEXBUFFERLOCKED;
    vb->caps |= D3DVBCAPS_OPTIMIZED;
    return D3D_OK;
}

static const struct IDirect3DVertexBuffer7Vtbl buffer_table = {
    .QueryInterface = (void *)Vb_QueryInterface, .AddRef = (void *)Vb_AddRef, .Release = (void *)Vb_Release,
    .Lock = (void *)Vb_Lock, .Unlock = (void *)Vb_Unlock, .ProcessVertices = (void *)Vb_ProcessVertices,
    .GetVertexBufferDesc = (void *)Vb_GetVertexBufferDesc, .Optimize = (void *)Vb_Optimize,
    .ProcessVerticesStrided = (void *)Vb_ProcessVerticesStrided};

static HRESULT WINAPI D3d_CreateVertexBuffer(void *face, LPD3DVERTEXBUFFERDESC desc, LPDIRECT3DVERTEXBUFFER7 *out, DWORD flags)
{
    D3dObject *d3d = This(face);
    D3dVertexBuffer *vb;
    D3dLayout layout;
    (void)flags;
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (!desc || desc->dwSize != sizeof *desc) return DDERR_INVALIDPARAMS;
    if (!D3dLayoutOf(desc->dwFVF, &layout)) return D3DERR_INVALIDVERTEXFORMAT;
    if (!desc->dwNumVertices || desc->dwNumVertices > D3DMAXNUMVERTICES) return DDERR_INVALIDPARAMS;
    vb = (D3dVertexBuffer *)DdAllocate(sizeof *vb);
    if (!vb) return DDERR_OUTOFMEMORY;
    vb->memory = (BYTE *)calloc(desc->dwNumVertices, layout.stride);
    if (!vb->memory)
    {
        free(vb);
        return DDERR_OUTOFMEMORY;
    }
    vb->face.table = &buffer_table;
    vb->face.object = vb;
    vb->references = 1;
    vb->dd = d3d->dd;
    DdObjectAddRef(vb->dd);
    /* The vertices lie in memory of the program in any case. */
    vb->caps = (desc->dwCaps & (D3DVBCAPS_WRITEONLY | D3DVBCAPS_DONOTCLIP)) | D3DVBCAPS_SYSTEMMEMORY;
    vb->fvf = desc->dwFVF;
    vb->count = desc->dwNumVertices;
    vb->stride = layout.stride;
    *out = (LPDIRECT3DVERTEXBUFFER7)&vb->face;
    return D3D_OK;
}

static const struct IDirect3D7Vtbl d3d_table = {
    .QueryInterface = (void *)D3d_QueryInterface, .AddRef = (void *)D3d_AddRef, .Release = (void *)D3d_Release,
    .EnumDevices = (void *)D3d_EnumDevices, .CreateDevice = (void *)D3d_CreateDevice,
    .CreateVertexBuffer = (void *)D3d_CreateVertexBuffer, .EnumZBufferFormats = (void *)D3d_EnumZBufferFormats,
    .EvictManagedTextures = (void *)D3d_EvictManagedTextures};

/* IDirectDraw7::QueryInterface for IID_IDirect3D7. Without a rasterizer in
   the host there is no Direct3D, as on a display driver without a HAL. */
HRESULT D3dObjectFor(void *object, REFIID iid, void **out)
{
    DdObject *dd = (DdObject *)object;
    NEO98DX_INFO info;
    D3dObject *d3d;
    (void)iid;
    if (!DdDisplayInfo(&info) || !(info.caps & NEO98DX_CAP_3D)) return E_NOINTERFACE;
    d3d = (D3dObject *)DdAllocate(sizeof *d3d);
    if (!d3d) return E_OUTOFMEMORY;
    d3d->face.table = &d3d_table;
    d3d->face.object = d3d;
    d3d->references = 1;
    d3d->dd = dd;
    DdObjectAddRef(dd);
    *out = &d3d->face;
    return S_OK;
}

/* The old texture interfaces share the surface's COM identity and lifetime.
   They are available on offscreen surfaces as well as texture surfaces. */
static HRESULT WINAPI Texture_QueryInterface(void *face, REFIID iid, void **out)
{
    return IUnknown_QueryInterface((IUnknown *)DdSurfaceFace(DdSurfaceOf(face), DDS_V1), iid, out);
}
static ULONG WINAPI Texture_AddRef(void *face)
{
    return IUnknown_AddRef((IUnknown *)DdSurfaceFace(DdSurfaceOf(face), DDS_V1));
}
static ULONG WINAPI Texture_Release(void *face)
{
    return IUnknown_Release((IUnknown *)DdSurfaceFace(DdSurfaceOf(face), DDS_V1));
}
static HRESULT WINAPI Texture_Initialize(void *face, LPDIRECT3DDEVICE device, LPDIRECTDRAWSURFACE surface)
{
    (void)face; (void)device; (void)surface;
    return DDERR_ALREADYINITIALIZED;
}
HRESULT D3dLegacyTextureHandle(void *texture, void *device, LPD3DTEXTUREHANDLE out);
static HRESULT WINAPI Texture_GetHandle(void *face, void *device, LPD3DTEXTUREHANDLE handle)
{
    return D3dLegacyTextureHandle(face, device, handle);
}

static HRESULT WINAPI Texture_PaletteChanged(void *face, DWORD start, DWORD count)
{
    DD_METHOD_LOCK();
    DdSurfacePaletteChanged(DdSurfaceOf(face), start, count);
    return D3D_OK;
}
static HRESULT WINAPI Texture_Load(void *face, void *source_face)
{
    DD_METHOD_LOCK();
    DdSurface *dest = DdSurfaceOf(face), *source = DdSurfaceOf(source_face);
    if (!source || source->dead || dest->dead) return DDERR_INVALIDOBJECT;
    if (dest == source) return D3D_OK;
    if (dest->width != source->width || dest->height != source->height) return D3DERR_TEXTURE_LOAD_FAILED;
    return IDirectDrawSurface7_BltFast((LPDIRECTDRAWSURFACE7)DdSurfaceFace(dest, DDS_V7), 0, 0,
                                     (LPDIRECTDRAWSURFACE7)DdSurfaceFace(source, DDS_V7), NULL, DDBLTFAST_WAIT);
}
static HRESULT WINAPI Texture_Unload(void *face)
{
    DD_METHOD_LOCK();
    (void)face;
    /* Pixel storage belongs to the surface, including after Unload. */
    D3dFlushAll();
    return D3D_OK;
}
static const struct IDirect3DTextureVtbl texture_table1 = {
    .QueryInterface = (void *)Texture_QueryInterface, .AddRef = (void *)Texture_AddRef, .Release = (void *)Texture_Release,
    .Initialize = (void *)Texture_Initialize, .GetHandle = (void *)Texture_GetHandle,
    .PaletteChanged = (void *)Texture_PaletteChanged, .Load = (void *)Texture_Load, .Unload = (void *)Texture_Unload};
static const struct IDirect3DTexture2Vtbl texture_table2 = {
    .QueryInterface = (void *)Texture_QueryInterface, .AddRef = (void *)Texture_AddRef, .Release = (void *)Texture_Release,
    .GetHandle = (void *)Texture_GetHandle, .PaletteChanged = (void *)Texture_PaletteChanged, .Load = (void *)Texture_Load};
static HRESULT TextureFor(void *object, REFIID iid, void **out)
{
    DdSurface *s = (DdSurface *)object;
    const unsigned version = IsEqualGUID(iid, &IID_IDirect3DTexture2) ? 1 : 0;
    s->texture[version].table = version ? (const void *)&texture_table2 : (const void *)&texture_table1;
    s->texture[version].object = s;
    s->texture[version].version = version;
    DdSurfaceAddRef(s);
    *out = &s->texture[version];
    return S_OK;
}

HRESULT D3dLegacyFor(void *object, REFIID iid, void **out);
HRESULT D3dLegacySurfaceFor(void *object, REFIID iid, void **out);
void D3dRegister(void)
{
    dd_extensions[0].iid = &d3d_iid_direct3d7;
    dd_extensions[0].factory = D3dObjectFor;
    dd_extensions[1] = (DdExtension){&IID_IDirect3D, D3dLegacyFor};
    dd_extensions[2] = (DdExtension){&IID_IDirect3D2, D3dLegacyFor};
    dd_extensions[3] = (DdExtension){&IID_IDirect3D3, D3dLegacyFor};
    dd_surface_extensions[0] = (DdExtension){&IID_IDirect3DTexture, TextureFor};
    dd_surface_extensions[1] = (DdExtension){&IID_IDirect3DTexture2, TextureFor};
    dd_surface_extensions[2] = (DdExtension){&IID_IDirect3DHALDevice, D3dLegacySurfaceFor};
    dd_surface_extensions[3] = (DdExtension){&IID_IDirect3DTnLHalDevice, D3dLegacySurfaceFor};
    dd_surface_extensions[4] = (DdExtension){&IID_IDirect3DDevice, D3dLegacySurfaceFor};
}
