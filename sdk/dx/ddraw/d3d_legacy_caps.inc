/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Old capability records describe the same HAL using the pre-DX7 layout. */
static void LegacyDescribe(const D3DDEVICEDESC7 *seven, D3DDEVICEDESC *old)
{
    memset(old, 0, sizeof *old);
    old->dwSize = sizeof *old;
    old->dwFlags = 0x7ff;
    old->dcmColorModel = D3DCOLOR_RGB;
    old->dwDevCaps = seven->dwDevCaps;
    old->dtcTransformCaps.dwSize = sizeof old->dtcTransformCaps;
    old->dtcTransformCaps.dwCaps = D3DTRANSFORMCAPS_CLIP;
    old->bClipping = FALSE;
    old->dlcLightingCaps.dwSize = sizeof old->dlcLightingCaps;
    old->dlcLightingCaps.dwCaps = D3DLIGHTCAPS_POINT | D3DLIGHTCAPS_SPOT | D3DLIGHTCAPS_DIRECTIONAL;
    old->dlcLightingCaps.dwLightingModel = D3DLIGHTINGMODEL_RGB;
    NEO98D3D_CAPS hardware;
    if (D3dCaps(&hardware))
    {
        old->dlcLightingCaps.dwNumLights = hardware.maxActiveLights;
        old->dwDevCaps = hardware.devCaps;
    }
    old->dpcLineCaps = seven->dpcLineCaps;
    old->dpcTriCaps = seven->dpcTriCaps;
    old->dpcLineCaps.dwTextureCaps |= D3DPTEXTURECAPS_POW2;
    old->dpcTriCaps.dwTextureCaps |= D3DPTEXTURECAPS_POW2;
    old->dwDeviceRenderBitDepth = seven->dwDeviceRenderBitDepth;
    old->dwDeviceZBufferBitDepth = seven->dwDeviceZBufferBitDepth;
    old->dwMaxVertexCount = D3DMAXNUMVERTICES;
    old->dwMinTextureWidth = seven->dwMinTextureWidth;
    old->dwMinTextureHeight = seven->dwMinTextureHeight;
    old->dwMaxTextureWidth = seven->dwMaxTextureWidth;
    old->dwMaxTextureHeight = seven->dwMaxTextureHeight;
    old->dwMaxTextureRepeat = seven->dwMaxTextureRepeat;
    old->dwMaxTextureAspectRatio = seven->dwMaxTextureAspectRatio;
    old->dwMaxAnisotropy = seven->dwMaxAnisotropy;
    old->dvGuardBandLeft = seven->dvGuardBandLeft;
    old->dvGuardBandTop = seven->dvGuardBandTop;
    old->dvGuardBandRight = seven->dvGuardBandRight;
    old->dvGuardBandBottom = seven->dvGuardBandBottom;
    old->dvExtentsAdjust = seven->dvExtentsAdjust;
    old->dwStencilCaps = seven->dwStencilCaps;
    old->dwFVFCaps = seven->dwFVFCaps;
    old->dwTextureOpCaps = seven->dwTextureOpCaps;
    old->wMaxTextureBlendStages = seven->wMaxTextureBlendStages;
    old->wMaxSimultaneousTextures = seven->wMaxSimultaneousTextures;
}
/* The HEL part of a HAL descriptor advertises its transform/light stage,
   not a software rasterizer: all primitive/render-depth caps remain zero. */
static void LegacyHel(D3DDEVICEDESC *old)
{
    memset(old, 0, sizeof *old);
    old->dwSize = sizeof *old;
    old->dwFlags = 0x1f;
    old->dwDevCaps = D3DDEVCAPS_FLOATTLVERTEX;
    old->bClipping = TRUE;
    old->dtcTransformCaps.dwSize = sizeof old->dtcTransformCaps;
    old->dtcTransformCaps.dwCaps = D3DTRANSFORMCAPS_CLIP;
    old->dlcLightingCaps.dwSize = sizeof old->dlcLightingCaps;
    old->dlcLightingCaps.dwCaps = D3DLIGHTCAPS_POINT | D3DLIGHTCAPS_SPOT | D3DLIGHTCAPS_DIRECTIONAL | D3DLIGHTCAPS_PARALLELPOINT;
    old->dlcLightingCaps.dwLightingModel = D3DLIGHTINGMODEL_RGB;
    old->dpcLineCaps.dwSize = old->dpcTriCaps.dwSize = sizeof(D3DPRIMCAPS);
    old->dwMaxVertexCount = D3DMAXNUMVERTICES;
}
static HRESULT CopyCaps(D3DDEVICEDESC *out, const D3DDEVICEDESC *value)
{
    if (!out)
        return D3D_OK;
    DWORD size = out->dwSize;
    if (size != 172 && size != 204 && size != sizeof *out)
        return DDERR_INVALIDPARAMS;
    memcpy(out, value, size);
    out->dwSize = size;
    return D3D_OK;
}
static HRESULT WINAPI DeviceCaps(void *face, LPD3DDEVICEDESC hw, LPD3DDEVICEDESC sw)
{
    DD_METHOD_LOCK();
    D3DDEVICEDESC7 seven;
    D3DDEVICEDESC old, empty = {0};
    HRESULT hr = IDirect3DDevice7_GetCaps(((LegacyDevice *)Base(face))->core, &seven);
    if (FAILED(hr))
        return hr;
    LegacyDescribe(&seven, &old);
    LegacyHel(&empty);
    hr = CopyCaps(hw, &old);
    return FAILED(hr) ? hr : CopyCaps(sw, &empty);
}
typedef struct LegacyEnum
{
    LPD3DENUMDEVICESCALLBACK callback;
    void *context;
} LegacyEnum;
static HRESULT CALLBACK EnumLegacy(char *description, char *name, D3DDEVICEDESC7 *seven, void *context)
{
    LegacyEnum *e = context;
    D3DDEVICEDESC old, empty = {0};
    LegacyDescribe(seven, &old);
    LegacyHel(&empty);
    return e->callback(&seven->deviceGUID, description, name, &old, &empty, e->context);
}
static HRESULT WINAPI RootEnum(void *face, LPD3DENUMDEVICESCALLBACK callback, void *context)
{
    DD_METHOD_LOCK();
    if (!callback)
        return DDERR_INVALIDPARAMS;
    IDirect3D7 *root = NULL;
    HRESULT hr = D3dObjectFor(((Legacy *)Base(face))->dd, &IID_IDirect3D7, (void **)&root);
    if (FAILED(hr))
        return hr;
    LegacyEnum e = {callback, context};
    hr = IDirect3D7_EnumDevices(root, EnumLegacy, &e);
    IDirect3D7_Release(root);
    return hr;
}
static HRESULT WINAPI RootZFormats(void *face, REFCLSID guid, LPD3DENUMPIXELFORMATSCALLBACK callback, void *context)
{
    DD_METHOD_LOCK();
    IDirect3D7 *root = NULL;
    HRESULT hr = D3dObjectFor(((Legacy *)Base(face))->dd, &IID_IDirect3D7, (void **)&root);
    if (FAILED(hr))
        return hr;
    hr = IDirect3D7_EnumZBufferFormats(root, guid, callback, context);
    IDirect3D7_Release(root);
    return hr;
}
static HRESULT WINAPI RootEvict(void *face)
{
    (void)face;
    D3dFlushAll();
    return D3D_OK;
}
static HRESULT WINAPI DeviceFormats3(void *face, LPD3DENUMPIXELFORMATSCALLBACK callback, void *context)
{
    DD_METHOD_LOCK();
    return IDirect3DDevice7_EnumTextureFormats(((LegacyDevice *)Base(face))->core, callback, context);
}
typedef struct LegacyFormats
{
    LPD3DENUMTEXTUREFORMATSCALLBACK callback;
    void *context;
} LegacyFormats;
static HRESULT CALLBACK EnumLegacyFormat(DDPIXELFORMAT *format, void *context)
{
    LegacyFormats *e = context;
    DDSURFACEDESC desc = {0};
    desc.dwSize = sizeof desc;
    desc.dwFlags = DDSD_PIXELFORMAT;
    desc.ddpfPixelFormat = *format;
    return e->callback(&desc, e->context);
}
static HRESULT WINAPI DeviceFormats(void *face, LPD3DENUMTEXTUREFORMATSCALLBACK callback, void *context)
{
    DD_METHOD_LOCK();
    if (!callback)
        return DDERR_INVALIDPARAMS;
    LegacyFormats e = {callback, context};
    return DeviceFormats3(face, EnumLegacyFormat, &e);
}
static HRESULT WINAPI DeviceSwapTextures(void *face, void *a, void *b)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    DdSurface *one = DdSurfaceOf(a), *two = DdSurfaceOf(b);
    DWORD first = 0, second = 0;
    for (DWORD n = 0; n < d->texture_count; ++n)
    {
        if (d->textures[n] == one)
            first = n + 1;
        if (d->textures[n] == two)
            second = n + 1;
    }
    if (!first || !second)
        return DDERR_INVALIDPARAMS;
    d->textures[first - 1] = two;
    d->textures[second - 1] = one;
    return D3D_OK;
}
typedef struct LegacyFind
{
    const D3DFINDDEVICESEARCH *search;
    D3DFINDDEVICERESULT *result;
    BOOL found;
} LegacyFind;
static HRESULT CALLBACK FindLegacy(GUID *guid, char *description, char *name, D3DDEVICEDESC *hw, D3DDEVICEDESC *sw, void *context)
{
    (void)description;
    (void)name;
    LegacyFind *find = context;
    const D3DFINDDEVICESEARCH *s = find->search;
    if ((s->dwFlags & D3DFDS_GUID) && !IsEqualGUID(guid, &s->guid))
        return D3DENUMRET_OK;
    if ((s->dwFlags & D3DFDS_HARDWARE) && !s->bHardware)
        return D3DENUMRET_OK;
    if ((s->dwFlags & D3DFDS_COLORMODEL) && s->dcmColorModel != hw->dcmColorModel)
        return D3DENUMRET_OK;
    const D3DPRIMCAPS *p = (s->dwFlags & D3DFDS_LINES) ? &hw->dpcLineCaps : &hw->dpcTriCaps;
    const DWORD flags[] = {D3DFDS_MISCCAPS,          D3DFDS_RASTERCAPS,       D3DFDS_ZCMPCAPS,          D3DFDS_SRCBLENDCAPS,
                           D3DFDS_DSTBLENDCAPS,      D3DFDS_ALPHACMPCAPS,     D3DFDS_SHADECAPS,         D3DFDS_TEXTURECAPS,
                           D3DFDS_TEXTUREFILTERCAPS, D3DFDS_TEXTUREBLENDCAPS, D3DFDS_TEXTUREADDRESSCAPS};
    const DWORD *have = &p->dwMiscCaps, *want = &s->dpcPrimCaps.dwMiscCaps;
    for (unsigned n = 0; n < sizeof flags / sizeof flags[0]; ++n)
        if ((s->dwFlags & flags[n]) && (have[n] & want[n]) != want[n])
            return D3DENUMRET_OK;
    find->result->guid = *guid;
    find->result->ddHwDesc = *hw;
    find->result->ddSwDesc = *sw;
    find->found = TRUE;
    return D3DENUMRET_CANCEL;
}
static HRESULT WINAPI RootFind(void *face, LPD3DFINDDEVICESEARCH search, LPD3DFINDDEVICERESULT result)
{
    DD_METHOD_LOCK();
    if (!search || !result || search->dwSize != sizeof *search || result->dwSize != sizeof *result)
        return DDERR_INVALIDPARAMS;
    LegacyFind find = {search, result, FALSE};
    HRESULT hr = RootEnum(face, FindLegacy, &find);
    return FAILED(hr) ? hr : find.found ? D3D_OK : DDERR_NOTFOUND;
}
