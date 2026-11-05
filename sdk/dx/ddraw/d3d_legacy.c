/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Legacy Direct3D COM objects. Geometry and lighting use d3d_vertex.c;
   drawing uses the existing device pipeline. Handles belong to a device. */
#include "d3d_private.h"

typedef struct Legacy Legacy;
typedef struct LegacyDevice LegacyDevice;
typedef struct LegacyView LegacyView;
typedef struct LegacyMaterial LegacyMaterial;
typedef struct LegacyLight LegacyLight;
typedef struct LegacyExecute LegacyExecute;
typedef struct LegacyBase
{
    DdFace face[3];
    LONG refs;
    unsigned kind;
} LegacyBase;
enum
{
    L_ROOT,
    L_DEVICE,
    L_VIEW,
    L_MATERIAL,
    L_LIGHT,
    L_EXECUTE,
    L_VERTEXBUFFER
};
struct Legacy
{
    LegacyBase base;
    DdObject *dd;
};
struct LegacyMaterial
{
    LegacyBase base;
    D3DMATERIAL data;
};
struct LegacyLight
{
    LegacyBase base;
    D3DLIGHT2 data;
    LegacyView *view;
};
struct LegacyView
{
    LegacyBase base;
    D3DVIEWPORT one;
    D3DVIEWPORT2 two;
    unsigned set;
    LegacyDevice *device;
    LegacyLight **lights;
    DWORD count;
    DWORD background;
    DdSurface *depth;
};
typedef struct LegacyVertexBuffer
{
    LegacyBase base;
    IDirect3DVertexBuffer7 *core;
} LegacyVertexBuffer;
struct LegacyExecute
{
    LegacyBase base;
    BYTE *bytes;
    DWORD size;
    BOOL locked, owns_bytes;
    LegacyDevice *owner;
    D3DEXECUTEDATA data;
};
struct LegacyDevice
{
    LegacyBase base;
    Legacy *root;
    IDirect3DDevice7 *core;
    DdSurface *aggregate; /* controlling outer of a directly created Device1 */
    LegacyView **views;
    DWORD view_count;
    LegacyView *current;
    LegacyMaterial **materials;
    DWORD material_count, light_state[9], legacy_render[256];
    DdSurface **textures;
    DWORD texture_count;
    D3DMATRIX **matrices;
    DWORD matrix_count;
    unsigned created_version;
    D3DMATRIX projection;
    D3DPRIMITIVETYPE primitive;
    DWORD fvf, vertex_count, index_count;
    BYTE *vertices;
    WORD *indices;
    BOOL immediate, immediate_indexed;
    LegacyDevice *next;
};
static LegacyDevice *legacy_devices;
static const void *root_tables[3], *device_tables[3], *view_tables[3], *material_tables[3];
static const void *light_table, *execute_table, *vertexbuffer_table;
static LegacyBase *Base(void *face)
{
    return face ? ((DdFace *)face)->object : NULL;
}
static void *Face(LegacyBase *b, unsigned version)
{
    return &b->face[version];
}
static void Init(LegacyBase *b, unsigned kind, const void *const *tables, unsigned count)
{
    b->refs = 1;
    b->kind = kind;
    for (unsigned n = 0; n < count; ++n)
    {
        b->face[n].table = tables[n];
        b->face[n].object = b;
        b->face[n].version = n;
    }
}
static ULONG WINAPI LAdd(void *face)
{
    LegacyBase *b = Base(face);
    if (b->kind == L_DEVICE && ((LegacyDevice *)b)->aggregate)
        return IUnknown_AddRef((IUnknown *)DdSurfaceFace(((LegacyDevice *)b)->aggregate, DDS_V1));
    return DdAddRef(&b->refs);
}
static ULONG WINAPI LRelease(void *face);
static HRESULT WINAPI LQuery(void *face, REFIID iid, void **out)
{
    DD_METHOD_LOCK();
    static const GUID *const families[][3] = {{&IID_IDirect3D, &IID_IDirect3D2, &IID_IDirect3D3},
                                              {&IID_IDirect3DDevice, &IID_IDirect3DDevice2, &IID_IDirect3DDevice3},
                                              {&IID_IDirect3DViewport, &IID_IDirect3DViewport2, &IID_IDirect3DViewport3},
                                              {&IID_IDirect3DMaterial, &IID_IDirect3DMaterial2, &IID_IDirect3DMaterial3},
                                              {&IID_IDirect3DLight, NULL, NULL},
                                              {&IID_IDirect3DExecuteBuffer, NULL, NULL},
                                              {&IID_IDirect3DVertexBuffer, NULL, NULL}};
    LegacyBase *b = Base(face);
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (!iid)
        return DDERR_INVALIDPARAMS;
    if (b->kind == L_ROOT && IsEqualGUID(iid, &dd_iid_unknown))
        return IUnknown_QueryInterface((IUnknown *)DdObjectFace(((Legacy *)b)->dd, DD_V1), iid, out);
    if (b->kind == L_DEVICE && ((LegacyDevice *)b)->aggregate)
    {
        if (IsEqualGUID(iid, &IID_IDirect3DDevice2) || IsEqualGUID(iid, &IID_IDirect3DDevice3))
            return DDERR_INVALIDPARAMS;
        return IUnknown_QueryInterface((IUnknown *)DdSurfaceFace(((LegacyDevice *)b)->aggregate, DDS_V1), iid, out);
    }
    for (unsigned n = 0; n < 3; ++n)
        if ((n == 0 && IsEqualGUID(iid, &dd_iid_unknown)) || (families[b->kind][n] && IsEqualGUID(iid, families[b->kind][n])))
        {
            if (b->kind == L_DEVICE && n + 1 > ((LegacyDevice *)b)->created_version)
                return DDERR_INVALIDPARAMS;
            LAdd(face);
            *out = Face(b, n);
            return S_OK;
        }
    if (b->kind == L_ROOT)
        return IUnknown_QueryInterface((IUnknown *)DdObjectFace(((Legacy *)b)->dd, DD_V1), iid, out);
    return E_NOINTERFACE;
}
static ULONG WINAPI LRelease(void *face)
{
    DD_METHOD_LOCK();
    LegacyBase *b = Base(face);
    if (b->kind == L_DEVICE && ((LegacyDevice *)b)->aggregate)
        return IUnknown_Release((IUnknown *)DdSurfaceFace(((LegacyDevice *)b)->aggregate, DDS_V1));
    LONG left = DdDropRef(&b->refs);
    if (left > 0)
        return left;
    if (b->kind == L_ROOT)
    {
        Legacy *r = (Legacy *)b;
        r->dd->legacy = NULL;
        DdObjectRelease(r->dd);
    }
    else if (b->kind == L_DEVICE)
    {
        LegacyDevice *d = (LegacyDevice *)b;
        LegacyDevice **link = &legacy_devices;
        while (*link && *link != d)
            link = &(*link)->next;
        if (*link)
            *link = d->next;
        for (DWORD n = 0; n < d->view_count; ++n)
        {
            d->views[n]->device = NULL;
            LRelease(Face(&d->views[n]->base, 0));
        }
        for (DWORD n = 0; n < d->material_count; ++n)
            if (d->materials[n])
                LRelease(Face(&d->materials[n]->base, 0));
        for (DWORD n = 0; n < d->texture_count; ++n)
            if (d->textures[n])
                DdSurfaceRelease(d->textures[n]);
        for (DWORD n = 0; n < d->matrix_count; ++n)
            free(d->matrices[n]);
        free(d->views);
        free(d->materials);
        free(d->textures);
        free(d->matrices);
        free(d->vertices);
        free(d->indices);
        IDirect3DDevice7_Release(d->core);
        LRelease(Face(&d->root->base, 0));
    }
    else if (b->kind == L_VIEW)
    {
        LegacyView *v = (LegacyView *)b;
        for (DWORD n = 0; n < v->count; ++n)
        {
            v->lights[n]->view = NULL;
            LRelease(Face(&v->lights[n]->base, 0));
        }
        if (v->depth)
            DdSurfaceRelease(v->depth);
        free(v->lights);
    }
    else if (b->kind == L_EXECUTE)
    {
        LegacyExecute *e = (LegacyExecute *)b;
        if (e->owns_bytes)
            free(e->bytes);
        LRelease(Face(&e->owner->base, 0));
    }
    else if (b->kind == L_VERTEXBUFFER)
        IDirect3DVertexBuffer7_Release(((LegacyVertexBuffer *)b)->core);
    free(b);
    return 0;
}
static HRESULT WINAPI Initialized(void *face, void *unused)
{
    (void)face;
    (void)unused;
    return DDERR_ALREADYINITIALIZED;
}
static DWORD VertexFormat(void *face, DWORD format)
{
    if (((DdFace *)face)->version != 1)
        return format;
    return format == D3DVT_VERTEX     ? D3DFVF_VERTEX
           : format == D3DVT_LVERTEX  ? D3DFVF_LVERTEX
           : format == D3DVT_TLVERTEX ? D3DFVF_TLVERTEX
                                      : 0;
}
static void MaterialApply(LegacyDevice *d)
{
    DWORD h = d->light_state[D3DLIGHTSTATE_MATERIAL];
    if (!h || h > d->material_count || !d->materials[h - 1])
        return;
    const D3DMATERIAL *m = &d->materials[h - 1]->data;
    D3DMATERIAL7 value;
    value.diffuse = m->diffuse;
    value.ambient = m->ambient;
    value.specular = m->specular;
    value.emissive = m->emissive;
    value.power = m->power;
    IDirect3DDevice7_SetMaterial(d->core, &value);
}
static HRESULT WINAPI MaterialSet(void *face, LPD3DMATERIAL data)
{
    DD_METHOD_LOCK();
    if (!data || data->dwSize != sizeof *data)
        return DDERR_INVALIDPARAMS;
    LegacyMaterial *m = (LegacyMaterial *)Base(face);
    m->data = *data;
    for (LegacyDevice *d = legacy_devices; d; d = d->next)
        if (d->light_state[1] && d->light_state[1] <= d->material_count && d->materials[d->light_state[1] - 1] == m)
            MaterialApply(d);
    return D3D_OK;
}
static HRESULT WINAPI MaterialGet(void *face, LPD3DMATERIAL data)
{
    DD_METHOD_LOCK();
    if (!data || data->dwSize != sizeof *data)
        return DDERR_INVALIDPARAMS;
    *data = ((LegacyMaterial *)Base(face))->data;
    return D3D_OK;
}
static HRESULT WINAPI MaterialHandle(void *face, void *device, LPD3DMATERIALHANDLE out)
{
    DD_METHOD_LOCK();
    if (!device || Base(device)->kind != L_DEVICE || !out)
        return DDERR_INVALIDPARAMS;
    LegacyDevice *d = (LegacyDevice *)Base(device);
    LegacyMaterial *m = (LegacyMaterial *)Base(face);
    for (DWORD n = 0; n < d->material_count; ++n)
        if (d->materials[n] == m)
        {
            *out = n + 1;
            return D3D_OK;
        }
    LegacyMaterial **more = realloc(d->materials, (d->material_count + 1) * sizeof *more);
    if (!more)
        return DDERR_OUTOFMEMORY;
    d->materials = more;
    more[d->material_count++] = m;
    LAdd(face);
    *out = d->material_count;
    return D3D_OK;
}
static HRESULT WINAPI MaterialReserve(void *face)
{
    (void)face;
    return D3D_OK;
}
static void LightApply(LegacyView *v)
{
    if (!v->device || v->device->current != v)
        return;
    IDirect3DDevice7 *core = v->device->core;
    for (DWORD n = 0; n < v->count; ++n)
    {
        D3DLIGHT2 *l = &v->lights[n]->data;
        D3DLIGHT7 p = {0};
        p.dltType = l->dltType;
        p.dcvDiffuse = l->dcvColor;
        p.dcvSpecular = l->dcvColor;
        p.dvPosition = l->dvPosition;
        p.dvDirection = l->dvDirection;
        p.dvRange = l->dvRange;
        p.dvFalloff = l->dvFalloff;
        p.dvAttenuation0 = l->dvAttenuation0;
        p.dvAttenuation1 = l->dvAttenuation1;
        p.dvAttenuation2 = l->dvAttenuation2;
        p.dvTheta = l->dvTheta;
        p.dvPhi = l->dvPhi;
        IDirect3DDevice7_SetLight(core, n, &p);
        IDirect3DDevice7_LightEnable(core, n, (l->dwFlags & D3DLIGHT_ACTIVE) != 0);
    }
}
static HRESULT WINAPI LightSet(void *face, LPD3DLIGHT p)
{
    DD_METHOD_LOCK();
    if (!p || (p->dwSize != sizeof(D3DLIGHT) && p->dwSize != sizeof(D3DLIGHT2)))
        return DDERR_INVALIDPARAMS;
    LegacyLight *l = (LegacyLight *)Base(face);
    memset(&l->data, 0, sizeof l->data);
    memcpy(&l->data, p, p->dwSize);
    if (p->dwSize == sizeof(D3DLIGHT))
        l->data.dwFlags = D3DLIGHT_ACTIVE;
    if (l->view)
        LightApply(l->view);
    return D3D_OK;
}
static HRESULT WINAPI LightGet(void *face, LPD3DLIGHT p)
{
    DD_METHOD_LOCK();
    if (!p || (p->dwSize != sizeof(D3DLIGHT) && p->dwSize != sizeof(D3DLIGHT2)))
        return DDERR_INVALIDPARAMS;
    DWORD size = p->dwSize;
    memcpy(p, &((LegacyLight *)Base(face))->data, size);
    p->dwSize = size;
    return D3D_OK;
}
/* Viewport1/2 express their own clip rectangle. Device7 uses [-1,+1],
   so append the rectangle's affine conversion to the logical projection. */
static D3DMATRIX ProjectionFor(LegacyDevice *d, LegacyView *v)
{
    D3DMATRIX result = d->projection;
    if (v && v->set)
    {
        D3DMATRIX rectangle = {0};
        rectangle._11 = 2 / v->two.dvClipWidth;
        rectangle._22 = 2 / v->two.dvClipHeight;
        rectangle._33 = rectangle._44 = 1;
        rectangle._41 = -1 - 2 * v->two.dvClipX / v->two.dvClipWidth;
        rectangle._42 = 1 - 2 * v->two.dvClipY / v->two.dvClipHeight;
        D3dMultiply(&result, &d->projection, &rectangle);
    }
    return result;
}
static HRESULT WINAPI DeviceSetTransform(void *face, D3DTRANSFORMSTATETYPE type, D3DMATRIX *matrix)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    if (!matrix)
        return DDERR_INVALIDPARAMS;
    if (type != D3DTRANSFORMSTATE_PROJECTION)
        return IDirect3DDevice7_SetTransform(d->core, type, matrix);
    d->projection = *matrix;
    D3DMATRIX adjusted = ProjectionFor(d, d->current);
    return IDirect3DDevice7_SetTransform(d->core, type, &adjusted);
}
static HRESULT WINAPI DeviceGetTransform(void *face, D3DTRANSFORMSTATETYPE type, D3DMATRIX *matrix)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    if (!matrix)
        return DDERR_INVALIDPARAMS;
    if (type != D3DTRANSFORMSTATE_PROJECTION)
        return IDirect3DDevice7_GetTransform(d->core, type, matrix);
    *matrix = d->projection;
    return D3D_OK;
}
static HRESULT WINAPI DeviceMultiplyTransform(void *face, D3DTRANSFORMSTATETYPE type, D3DMATRIX *matrix)
{
    DD_METHOD_LOCK();
    if (!matrix)
        return DDERR_INVALIDPARAMS;
    D3DMATRIX prior, product;
    HRESULT hr = DeviceGetTransform(face, type, &prior);
    if (FAILED(hr))
        return hr;
    D3dMultiply(&product, matrix, &prior);
    return DeviceSetTransform(face, type, &product);
}
static void ViewApply(LegacyView *v)
{
    if (!v->device || v->device->current != v || !v->set)
        return;
    D3DVIEWPORT7 p = {v->two.dwX, v->two.dwY, v->two.dwWidth, v->two.dwHeight, v->two.dvMinZ, v->two.dvMaxZ};
    IDirect3DDevice7_SetViewport(v->device->core, &p);
    D3DMATRIX adjusted = ProjectionFor(v->device, v);
    IDirect3DDevice7_SetTransform(v->device->core, D3DTRANSFORMSTATE_PROJECTION, &adjusted);
    LightApply(v);
}
static HRESULT WINAPI ViewSet2(void *face, LPD3DVIEWPORT2 p)
{
    DD_METHOD_LOCK();
    LegacyView *v = (LegacyView *)Base(face);
    if (!p || p->dwSize != sizeof *p)
        return DDERR_INVALIDPARAMS;
    if (!v->device)
        return D3DERR_VIEWPORTHASNODEVICE;
    if (p->dvClipWidth <= 0 || p->dvClipHeight <= 0)
        return DDERR_INVALIDPARAMS;
    v->two = *p;
    v->one = (D3DVIEWPORT){sizeof(D3DVIEWPORT),
                           p->dwX,
                           p->dwY,
                           p->dwWidth,
                           p->dwHeight,
                           p->dwWidth / p->dvClipWidth,
                           p->dwHeight / p->dvClipHeight,
                           fmaxf(fabsf(p->dvClipX), fabsf(p->dvClipX + p->dvClipWidth)),
                           fmaxf(fabsf(p->dvClipY), fabsf(p->dvClipY - p->dvClipHeight)),
                           p->dvMinZ,
                           p->dvMaxZ};
    v->set = 2;
    ViewApply(v);
    return D3D_OK;
}
static HRESULT WINAPI ViewGet2(void *face, LPD3DVIEWPORT2 p)
{
    DD_METHOD_LOCK();
    LegacyView *v = (LegacyView *)Base(face);
    if (!p || p->dwSize != sizeof *p)
        return DDERR_INVALIDPARAMS;
    if (!v->set)
        return D3DERR_VIEWPORTDATANOTSET;
    *p = v->two;
    return D3D_OK;
}
static HRESULT WINAPI ViewSet(void *face, LPD3DVIEWPORT p)
{
    DD_METHOD_LOCK();
    LegacyView *v = (LegacyView *)Base(face);
    if (!p || p->dwSize != sizeof *p)
        return DDERR_INVALIDPARAMS;
    if (!v->device)
        return D3DERR_VIEWPORTHASNODEVICE;
    if (p->dvScaleX <= 0 || p->dvScaleY <= 0)
        return DDERR_INVALIDPARAMS;
    v->one = *p;
    float width = p->dwWidth / p->dvScaleX, height = p->dwHeight / p->dvScaleY;
    v->two = (D3DVIEWPORT2){sizeof(D3DVIEWPORT2), p->dwX, p->dwY, p->dwWidth, p->dwHeight, -width * .5f,
                            height * .5f,         width,  height, p->dvMinZ,  p->dvMaxZ};
    v->set = 1;
    ViewApply(v);
    return D3D_OK;
}
static HRESULT WINAPI ViewGet(void *face, LPD3DVIEWPORT p)
{
    DD_METHOD_LOCK();
    LegacyView *v = (LegacyView *)Base(face);
    if (!p || p->dwSize != sizeof *p)
        return DDERR_INVALIDPARAMS;
    if (!v->set)
        return D3DERR_VIEWPORTDATANOTSET;
    *p = v->one;
    return D3D_OK;
}
/* D3DIM copies a 32-byte legacy vertex, replacing XYZ/reserved with XYZRHW.
   Transform math stays in the existing ProcessVertices implementation. */
static HRESULT WINAPI ViewTransform(void *face, DWORD count, LPD3DTRANSFORMDATA data, DWORD flags, DWORD *offscreen)
{
    DD_METHOD_LOCK();
    LegacyView *v = (LegacyView *)Base(face);
    if (!data || data->dwSize != sizeof *data || !offscreen || !data->lpIn || !data->lpOut || data->dwInSize < sizeof(D3DLVERTEX) ||
        data->dwOutSize < sizeof(D3DTLVERTEX))
        return DDERR_INVALIDPARAMS;
    if (!v->device)
        return D3DERR_VIEWPORTHASNODEVICE;
    if (!v->set)
        return D3DERR_VIEWPORTDATANOTSET;
    if (flags != D3DTRANSFORM_UNCLIPPED)
        return DDERR_UNSUPPORTED;
    D3dDevice *core = D3dDeviceOf(v->device->core);
    D3DVIEWPORT7 saved = core->state.viewport;
    D3DMATRIX projection = core->state.matrix[D3D_MATRIX_PROJECTION];
    core->state.matrix[D3D_MATRIX_PROJECTION] = ProjectionFor(v->device, v);
    core->state.viewport = (D3DVIEWPORT7){v->two.dwX, v->two.dwY, v->two.dwWidth, v->two.dwHeight, v->two.dvMinZ, v->two.dvMaxZ};
    HRESULT hr = D3D_OK;
    for (DWORD n = 0; n < count; ++n)
    {
        BYTE *in = (BYTE *)data->lpIn + (size_t)n * data->dwInSize;
        BYTE *out = (BYTE *)data->lpOut + (size_t)n * data->dwOutSize;
        D3DDRAWPRIMITIVESTRIDEDDATA source = {0};
        source.position.lpvData = in;
        source.position.dwStride = data->dwInSize;
        float position[4];
        hr = D3dProcessVertices(core, D3DVOP_TRANSFORM, (BYTE *)position, D3DFVF_XYZRHW, &source, D3DFVF_XYZ, 1, 0);
        if (FAILED(hr))
            break;
        memmove(out, in, sizeof(D3DLVERTEX));
        memcpy(out, position, sizeof position);
    }
    core->state.viewport = saved;
    core->state.matrix[D3D_MATRIX_PROJECTION] = projection;
    *offscreen = 0;
    return hr;
}
static HRESULT WINAPI ViewLightElements(void *face, DWORD count, LPD3DLIGHTDATA data)
{
    (void)face;
    (void)count;
    (void)data;
    /* D3DIM's version-3 viewport does not expose this obsolete operation. */
    return E_NOTIMPL;
}
static HRESULT WINAPI ViewAddLight(void *face, LPDIRECT3DLIGHT light)
{
    DD_METHOD_LOCK();
    LegacyView *v = (LegacyView *)Base(face);
    if (!light || Base(light)->kind != L_LIGHT)
        return DDERR_INVALIDPARAMS;
    LegacyLight *l = (LegacyLight *)Base(light);
    if (l->view)
        return D3DERR_LIGHTHASVIEWPORT;
    LegacyLight **more = realloc(v->lights, (v->count + 1) * sizeof *more);
    if (!more)
        return DDERR_OUTOFMEMORY;
    v->lights = more;
    more[v->count++] = l;
    l->view = v;
    LAdd(light);
    LightApply(v);
    return D3D_OK;
}
static HRESULT WINAPI ViewDeleteLight(void *face, LPDIRECT3DLIGHT light)
{
    DD_METHOD_LOCK();
    LegacyView *v = (LegacyView *)Base(face);
    LegacyLight *l = (LegacyLight *)Base(light);
    for (DWORD n = 0; n < v->count; ++n)
        if (v->lights[n] == l)
        {
            if (v->device && v->device->current == v)
                IDirect3DDevice7_LightEnable(v->device->core, v->count - 1, FALSE);
            memmove(v->lights + n, v->lights + n + 1, (--v->count - n) * sizeof *v->lights);
            l->view = NULL;
            LRelease(light);
            LightApply(v);
            return D3D_OK;
        }
    return D3DERR_LIGHTNOTINTHISVIEWPORT;
}
static HRESULT WINAPI ViewBackground(void *face, D3DMATERIALHANDLE handle)
{
    DD_METHOD_LOCK();
    ((LegacyView *)Base(face))->background = handle;
    return D3D_OK;
}
static HRESULT WINAPI ViewGetBackground(void *face, LPD3DMATERIALHANDLE out, LPBOOL valid)
{
    DD_METHOD_LOCK();
    if (!out || !valid)
        return DDERR_INVALIDPARAMS;
    *out = ((LegacyView *)Base(face))->background;
    *valid = *out != 0;
    return D3D_OK;
}
static HRESULT WINAPI ViewClear2(void *face, DWORD count, LPD3DRECT rects, DWORD flags, D3DCOLOR color, D3DVALUE z, DWORD stencil)
{
    DD_METHOD_LOCK();
    LegacyView *v = (LegacyView *)Base(face);
    if (!v->device)
        return D3DERR_VIEWPORTHASNODEVICE;
    if (!v->set)
        return D3DERR_VIEWPORTDATANOTSET;
    if (!count)
        return D3D_OK;
    D3DVIEWPORT7 saved, area = {v->two.dwX, v->two.dwY, v->two.dwWidth, v->two.dwHeight, v->two.dvMinZ, v->two.dvMaxZ};
    IDirect3DDevice7_GetViewport(v->device->core, &saved);
    HRESULT hr = IDirect3DDevice7_SetViewport(v->device->core, &area);
    if (SUCCEEDED(hr))
        hr = IDirect3DDevice7_Clear(v->device->core, count, rects, flags, color, z, stencil);
    IDirect3DDevice7_SetViewport(v->device->core, &saved);
    return hr;
}
static HRESULT WINAPI ViewClear(void *face, DWORD count, LPD3DRECT rects, DWORD flags)
{
    DD_METHOD_LOCK();
    LegacyView *v = (LegacyView *)Base(face);
    DWORD color = 0;
    if (v->device && v->background && v->background <= v->device->material_count)
    {
        D3DCOLORVALUE c = v->device->materials[v->background - 1]->data.diffuse;
        color = ((DWORD)(fminf(fmaxf(c.a, 0), 1) * 255) << 24) | ((DWORD)(fminf(fmaxf(c.r, 0), 1) * 255) << 16) |
                ((DWORD)(fminf(fmaxf(c.g, 0), 1) * 255) << 8) | (DWORD)(fminf(fmaxf(c.b, 0), 1) * 255);
    }
    return ViewClear2(face, count, rects, flags, color, 1, 0);
}
static HRESULT WINAPI DeviceAddView(void *face, void *view)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    if (!view || Base(view)->kind != L_VIEW)
        return DDERR_INVALIDPARAMS;
    LegacyView *v = (LegacyView *)Base(view);
    if (v->device)
        return DDERR_INVALIDPARAMS;
    LegacyView **more = realloc(d->views, (d->view_count + 1) * sizeof *more);
    if (!more)
        return DDERR_OUTOFMEMORY;
    d->views = more;
    more[d->view_count++] = v;
    v->device = d;
    LAdd(view);
    return D3D_OK;
}
static HRESULT WINAPI DeviceDeleteView(void *face, void *view)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    LegacyView *v = (LegacyView *)Base(view);
    for (DWORD n = 0; n < d->view_count; ++n)
        if (d->views[n] == v)
        {
            if (d->current == v)
            {
                for (DWORD light = 0; light < v->count; ++light)
                    IDirect3DDevice7_LightEnable(d->core, light, FALSE);
                d->current = NULL;
            }
            v->device = NULL;
            memmove(d->views + n, d->views + n + 1, (--d->view_count - n) * sizeof *d->views);
            LRelease(view);
            return D3D_OK;
        }
    return DDERR_INVALIDPARAMS;
}
static HRESULT WINAPI DeviceCurrent(void *face, void *view)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    LegacyView *v = (LegacyView *)Base(view);
    if (!v || v->device != d)
        return DDERR_INVALIDPARAMS;
    if (d->current)
        for (DWORD n = 0; n < d->current->count; ++n)
            IDirect3DDevice7_LightEnable(d->core, n, FALSE);
    d->current = v;
    ViewApply(v);
    return D3D_OK;
}
static HRESULT WINAPI DeviceGetCurrent(void *face, void **out)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (!d->current)
        return D3DERR_NOCURRENTVIEWPORT;
    *out = Face(&d->current->base, ((DdFace *)face)->version);
    LAdd(*out);
    return D3D_OK;
}
static HRESULT WINAPI DeviceBeginScene(void *face)
{
    return IDirect3DDevice7_BeginScene(((LegacyDevice *)Base(face))->core);
}
static HRESULT WINAPI DeviceEndScene(void *face)
{
    return IDirect3DDevice7_EndScene(((LegacyDevice *)Base(face))->core);
}
static HRESULT Ready(LegacyDevice *d)
{
    return !d->current ? D3DERR_NOCURRENTVIEWPORT : !d->current->set ? D3DERR_VIEWPORTDATANOTSET : D3D_OK;
}
static HRESULT WINAPI DeviceDraw(void *face, D3DPRIMITIVETYPE type, DWORD format, void *vertices, DWORD count, DWORD flags)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    HRESULT hr = Ready(d);
    if (FAILED(hr))
        return hr;
    return IDirect3DDevice7_DrawPrimitive(d->core, type, VertexFormat(face, format), vertices, count, flags);
}
static HRESULT WINAPI DeviceDrawIndexed(void *face, D3DPRIMITIVETYPE type, DWORD format, void *vertices, DWORD count, WORD *indices,
                                        DWORD index_count, DWORD flags)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    HRESULT hr = Ready(d);
    if (FAILED(hr))
        return hr;
    return IDirect3DDevice7_DrawIndexedPrimitive(d->core, type, VertexFormat(face, format), vertices, count, indices, index_count, flags);
}
static HRESULT WINAPI DeviceSetLightState(void *face, D3DLIGHTSTATETYPE state, DWORD value)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    if (state < 1 || state > 8)
        return DDERR_INVALIDPARAMS;
    static const D3DRENDERSTATETYPE map[] = {0,
                                             0,
                                             D3DRENDERSTATE_AMBIENT,
                                             0,
                                             D3DRENDERSTATE_FOGVERTEXMODE,
                                             D3DRENDERSTATE_FOGSTART,
                                             D3DRENDERSTATE_FOGEND,
                                             D3DRENDERSTATE_FOGDENSITY,
                                             D3DRENDERSTATE_COLORVERTEX};
    if (state == 1)
    {
        if (value > d->material_count)
            return DDERR_INVALIDPARAMS;
        d->light_state[state] = value;
        MaterialApply(d);
        return D3D_OK;
    }
    d->light_state[state] = value;
    return map[state] ? IDirect3DDevice7_SetRenderState(d->core, map[state], value) : D3D_OK;
}
static HRESULT WINAPI DeviceGetLightState(void *face, D3DLIGHTSTATETYPE state, DWORD *out)
{
    DD_METHOD_LOCK();
    if (!out || state < 1 || state > 8)
        return DDERR_INVALIDPARAMS;
    *out = ((LegacyDevice *)Base(face))->light_state[state];
    return D3D_OK;
}
static HRESULT WINAPI DeviceCreateMatrix(void *face, LPD3DMATRIXHANDLE out)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    if (!out)
        return DDERR_INVALIDPARAMS;
    D3DMATRIX *m = calloc(1, sizeof *m);
    if (!m)
        return DDERR_OUTOFMEMORY;
    D3DMATRIX **more = realloc(d->matrices, (d->matrix_count + 1) * sizeof *more);
    if (!more)
    {
        free(m);
        return DDERR_OUTOFMEMORY;
    }
    d->matrices = more;
    more[d->matrix_count++] = m;
    *out = d->matrix_count;
    return D3D_OK;
}
static HRESULT WINAPI DeviceSetMatrix(void *face, D3DMATRIXHANDLE h, LPD3DMATRIX p)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    if (!p || !h || h > d->matrix_count || !d->matrices[h - 1])
        return D3DERR_INVALIDMATRIX;
    *d->matrices[h - 1] = *p;
    return D3D_OK;
}
static HRESULT WINAPI DeviceGetMatrix(void *face, D3DMATRIXHANDLE h, LPD3DMATRIX p)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    if (!p || !h || h > d->matrix_count || !d->matrices[h - 1])
        return D3DERR_INVALIDMATRIX;
    *p = *d->matrices[h - 1];
    return D3D_OK;
}
static HRESULT WINAPI DeviceDeleteMatrix(void *face, D3DMATRIXHANDLE h)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    if (!h || h > d->matrix_count || !d->matrices[h - 1])
        return D3DERR_INVALIDMATRIX;
    free(d->matrices[h - 1]);
    d->matrices[h - 1] = NULL;
    return D3D_OK;
}
static HRESULT WINAPI RootCreateLight(void *face, void **out, IUnknown *outer)
{
    DD_METHOD_LOCK();
    (void)face;
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    LegacyLight *l = calloc(1, sizeof *l);
    if (!l)
        return DDERR_OUTOFMEMORY;
    Init(&l->base, L_LIGHT, &light_table, 1);
    l->data.dwSize = sizeof l->data;
    *out = Face(&l->base, 0);
    return D3D_OK;
}
static HRESULT WINAPI RootCreateMaterial(void *face, void **out, IUnknown *outer)
{
    DD_METHOD_LOCK();
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    LegacyMaterial *m = calloc(1, sizeof *m);
    if (!m)
        return DDERR_OUTOFMEMORY;
    Init(&m->base, L_MATERIAL, material_tables, 3);
    m->data.dwSize = sizeof m->data;
    *out = Face(&m->base, ((DdFace *)face)->version);
    return D3D_OK;
}
static HRESULT WINAPI RootCreateView(void *face, void **out, IUnknown *outer)
{
    DD_METHOD_LOCK();
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    LegacyView *v = calloc(1, sizeof *v);
    if (!v)
        return DDERR_OUTOFMEMORY;
    Init(&v->base, L_VIEW, view_tables, 3);
    *out = Face(&v->base, ((DdFace *)face)->version);
    return D3D_OK;
}
static HRESULT WINAPI RootCreateDevice3(void *face, REFCLSID guid, void *surface, void **out, IUnknown *outer)
{
    DD_METHOD_LOCK();
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    if (!guid)
        return DDERR_INVALIDPARAMS;
    if (!surface)
        return DDERR_INVALIDOBJECT;
    Legacy *r = (Legacy *)Base(face);
    D3dDevice *core = NULL;
    HRESULT hr = D3dCreateDevice(r->dd, guid, DdSurfaceOf(surface), &core);
    if (FAILED(hr))
        return hr;
    LegacyDevice *d = calloc(1, sizeof *d);
    if (!d)
    {
        IDirect3DDevice7_Release((IDirect3DDevice7 *)&core->face);
        return DDERR_OUTOFMEMORY;
    }
    Init(&d->base, L_DEVICE, device_tables, 3);
    d->core = (IDirect3DDevice7 *)&core->face;
    d->root = r;
    IDirect3DDevice7_GetTransform(d->core, D3DTRANSFORMSTATE_PROJECTION, &d->projection);
    d->created_version = ((DdFace *)face)->version + 1;
    LAdd(face);
    d->light_state[3] = D3DCOLOR_RGB;
    d->legacy_render[D3DRENDERSTATE_TEXTUREADDRESS] = d->legacy_render[D3DRENDERSTATE_TEXTUREADDRESSU] =
        d->legacy_render[D3DRENDERSTATE_TEXTUREADDRESSV] = D3DTADDRESS_WRAP;
    d->legacy_render[D3DRENDERSTATE_TEXTUREMIN] = d->legacy_render[D3DRENDERSTATE_TEXTUREMAG] = D3DFILTER_NEAREST;
    d->legacy_render[D3DRENDERSTATE_TEXTUREMAPBLEND] = D3DTBLEND_MODULATE;
    d->legacy_render[D3DRENDERSTATE_ANISOTROPY] = 1;
    d->light_state[D3DLIGHTSTATE_FOGEND] = d->light_state[D3DLIGHTSTATE_FOGDENSITY] = 0x3f800000;
    d->light_state[D3DLIGHTSTATE_COLORVERTEX] = TRUE;
    d->next = legacy_devices;
    legacy_devices = d;
    *out = Face(&d->base, ((DdFace *)face)->version);
    return D3D_OK;
}
static HRESULT WINAPI RootCreateDevice2(void *face, REFCLSID guid, void *surface, void **out)
{
    DD_METHOD_LOCK();
    return RootCreateDevice3(face, guid, surface, out, NULL);
}

static HRESULT WINAPI ExecuteLock(void *face, LPD3DEXECUTEBUFFERDESC p)
{
    DD_METHOD_LOCK();
    LegacyExecute *e = (LegacyExecute *)Base(face);
    if (!p || p->dwSize != sizeof *p)
        return DDERR_INVALIDPARAMS;
    if (e->locked)
        return D3DERR_EXECUTE_LOCKED;
    e->locked = TRUE;
    p->dwFlags = D3DDEB_BUFSIZE | D3DDEB_LPDATA;
    p->dwBufferSize = e->size;
    p->lpData = e->bytes;
    return D3D_OK;
}
static HRESULT WINAPI ExecuteUnlock(void *face)
{
    DD_METHOD_LOCK();
    LegacyExecute *e = (LegacyExecute *)Base(face);
    if (!e->locked)
        return D3DERR_EXECUTE_NOT_LOCKED;
    e->locked = FALSE;
    return D3D_OK;
}
static HRESULT WINAPI ExecuteGet(void *face, LPD3DEXECUTEDATA p)
{
    DD_METHOD_LOCK();
    if (!p || p->dwSize != sizeof *p)
        return DDERR_INVALIDPARAMS;
    *p = ((LegacyExecute *)Base(face))->data;
    return D3D_OK;
}
static HRESULT WINAPI ExecuteSet(void *face, LPD3DEXECUTEDATA p)
{
    DD_METHOD_LOCK();
    LegacyExecute *e = (LegacyExecute *)Base(face);
    if (!p || p->dwSize != sizeof *p)
        return DDERR_INVALIDPARAMS;
    if (p->dwVertexOffset > e->size || p->dwVertexCount > (e->size - p->dwVertexOffset) / sizeof(D3DVERTEX) ||
        p->dwInstructionOffset > e->size || p->dwInstructionLength > e->size - p->dwInstructionOffset)
        return DDERR_INVALIDPARAMS;
    e->data = *p;
    return D3D_OK;
}
static HRESULT WINAPI DeviceCreateExecute(void *face, LPD3DEXECUTEBUFFERDESC p, LPDIRECT3DEXECUTEBUFFER *out, IUnknown *outer)
{
    DD_METHOD_LOCK();
    (void)face;
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    if (!p || p->dwSize != sizeof *p || !(p->dwFlags & D3DDEB_BUFSIZE) || !p->dwBufferSize)
        return DDERR_INVALIDPARAMS;
    LegacyExecute *e = calloc(1, sizeof *e);
    if (!e)
        return DDERR_OUTOFMEMORY;
    e->owns_bytes = !(p->dwFlags & D3DDEB_LPDATA);
    if (!e->owns_bytes && !p->lpData)
    {
        free(e);
        return DDERR_INVALIDPARAMS;
    }
    e->bytes = e->owns_bytes ? calloc(1, p->dwBufferSize) : p->lpData;
    if (!e->bytes)
    {
        free(e);
        return DDERR_OUTOFMEMORY;
    }
    Init(&e->base, L_EXECUTE, &execute_table, 1);
    e->size = p->dwBufferSize;
    e->owner = (LegacyDevice *)Base(face);
    LAdd(face);
    *out = Face(&e->base, 0);
    return D3D_OK;
}
static HRESULT WINAPI DeviceSetRender(void *face, D3DRENDERSTATETYPE state, DWORD value);
static HRESULT WINAPI DeviceExecute(void *face, LPDIRECT3DEXECUTEBUFFER buffer, LPDIRECT3DVIEWPORT view, DWORD flags)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    LegacyExecute *e = (LegacyExecute *)Base(buffer);
    if (!e || e->base.kind != L_EXECUTE || e->owner != d || !view || Base(view)->kind != L_VIEW || flags > 2)
        return DDERR_INVALIDPARAMS;
    if (e->locked)
        return D3DERR_EXECUTE_LOCKED;
    HRESULT hr = DeviceCurrent(face, view);
    if (FAILED(hr))
        return hr;
    if (FAILED(hr = Ready(d)))
        return hr;
    DWORD count = e->data.dwVertexCount;
    D3DTLVERTEX *vertices = calloc(count ? count : 1, sizeof *vertices);
    if (!vertices)
        return DDERR_OUTOFMEMORY;
    BYTE *begin = e->bytes + e->data.dwInstructionOffset, *end = begin + e->data.dwInstructionLength, *at = begin;
    hr = D3DERR_EXECUTE_FAILED;
    while ((size_t)(end - at) >= sizeof(D3DINSTRUCTION))
    {
        BYTE *instruction = at;
        D3DINSTRUCTION ins;
        memcpy(&ins, at, sizeof ins);
        at += sizeof ins;
        if (ins.bOpcode == D3DOP_EXIT)
        {
            hr = D3D_OK;
            break;
        }
        if (!ins.bSize || (size_t)(end - at) < (size_t)ins.bSize * ins.wCount)
            break;
        for (unsigned n = 0; n < ins.wCount; ++n)
        {
            BYTE *p = at + n * ins.bSize;
            hr = D3D_OK;
            if (ins.bOpcode == D3DOP_PROCESSVERTICES && ins.bSize == sizeof(D3DPROCESSVERTICES))
            {
                D3DPROCESSVERTICES *op = (void *)p;
                if (op->wStart > count || op->dwCount > count - op->wStart || op->wDest > count || op->dwCount > count - op->wDest)
                {
                    hr = DDERR_INVALIDPARAMS;
                    break;
                }
                DWORD operation = op->dwFlags & D3DPROCESSVERTICES_OPMASK;
                BYTE *source = e->bytes + e->data.dwVertexOffset + op->wStart * sizeof(D3DVERTEX);
                if (operation == D3DPROCESSVERTICES_COPY)
                    memcpy(vertices + op->wDest, source, op->dwCount * sizeof *vertices);
                else
                {
                    DWORD fvf = operation == D3DPROCESSVERTICES_TRANSFORM ? D3DFVF_LVERTEX : D3DFVF_VERTEX;
                    D3dLayout layout;
                    D3DDRAWPRIMITIVESTRIDEDDATA data;
                    D3dLayoutOf(fvf, &layout);
                    D3dStridedOf(source, fvf, &layout, &data);
                    DWORD bits = D3DVOP_TRANSFORM | (operation == D3DPROCESSVERTICES_TRANSFORMLIGHT ? D3DVOP_LIGHT : 0) |
                                 ((op->dwFlags & D3DPROCESSVERTICES_UPDATEEXTENTS) ? D3DVOP_EXTENTS : 0);
                    hr = D3dProcessVertices(D3dDeviceOf(d->core), bits, (BYTE *)(vertices + op->wDest), D3DFVF_TLVERTEX, &data, fvf,
                                            op->dwCount, 0);
                }
            }
            else if (ins.bOpcode == D3DOP_POINT && ins.bSize == sizeof(D3DPOINT))
            {
                D3DPOINT *op = (void *)p;
                if (op->wFirst > count || op->wCount > count - op->wFirst)
                {
                    hr = DDERR_INVALIDPARAMS;
                    break;
                }
                hr = IDirect3DDevice7_DrawPrimitive(d->core, D3DPT_POINTLIST, D3DFVF_TLVERTEX, vertices + op->wFirst, op->wCount, 0);
            }
            else if (ins.bOpcode == D3DOP_SETSTATUS && ins.bSize == sizeof(D3DSTATUS))
            {
                D3DSTATUS *op = (void *)p;
                if (op->dwFlags & D3DSETSTATUS_STATUS)
                    e->data.dsStatus.dwStatus = op->dwStatus;
                if (op->dwFlags & D3DSETSTATUS_EXTENTS)
                    e->data.dsStatus.drExtent = op->drExtent;
                e->data.dsStatus.dwFlags = op->dwFlags;
            }
            else if (ins.bOpcode == D3DOP_BRANCHFORWARD && ins.bSize == sizeof(D3DBRANCH))
            {
                D3DBRANCH *op = (void *)p;
                BOOL match = (e->data.dsStatus.dwStatus & op->dwMask) == op->dwValue;
                if (op->bNegate)
                    match = !match;
                if (match && op->dwOffset)
                {
                    if (op->dwOffset < sizeof ins + ins.bSize * ins.wCount || op->dwOffset > (size_t)(end - instruction))
                    {
                        hr = DDERR_INVALIDPARAMS;
                        break;
                    }
                    at = instruction + op->dwOffset;
                    goto next_instruction;
                }
            }
            else if (ins.bOpcode == D3DOP_TEXTURELOAD && ins.bSize == sizeof(D3DTEXTURELOAD))
            {
                D3DTEXTURELOAD *op = (void *)p;
                void *src = NULL, *dst = NULL;
                if (!op->hSrcTexture || !op->hDestTexture || op->hSrcTexture > d->texture_count || op->hDestTexture > d->texture_count)
                {
                    hr = DDERR_INVALIDPARAMS;
                    break;
                }
                hr = IUnknown_QueryInterface((IUnknown *)DdSurfaceFace(d->textures[op->hSrcTexture - 1], DDS_V1), &IID_IDirect3DTexture2,
                                             &src);
                if (SUCCEEDED(hr))
                    hr = IUnknown_QueryInterface((IUnknown *)DdSurfaceFace(d->textures[op->hDestTexture - 1], DDS_V1),
                                                 &IID_IDirect3DTexture2, &dst);
                if (SUCCEEDED(hr))
                    hr = IDirect3DTexture2_Load((IDirect3DTexture2 *)dst, src);
                if (src)
                    IDirect3DTexture2_Release((IDirect3DTexture2 *)src);
                if (dst)
                    IDirect3DTexture2_Release((IDirect3DTexture2 *)dst);
            }
            else if (ins.bOpcode == D3DOP_TRIANGLE && ins.bSize == sizeof(D3DTRIANGLE))
            {
                D3DTRIANGLE *t = (void *)p;
                WORD indices[] = {t->v1, t->v2, t->v3};
                if (t->v1 >= count || t->v2 >= count || t->v3 >= count)
                {
                    hr = DDERR_INVALIDPARAMS;
                    break;
                }
                hr = IDirect3DDevice7_DrawIndexedPrimitive(d->core, D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, vertices, count, indices, 3, 0);
            }
            else if (ins.bOpcode == D3DOP_LINE && ins.bSize == sizeof(D3DLINE))
            {
                D3DLINE *t = (void *)p;
                WORD indices[] = {t->v1, t->v2};
                if (t->v1 >= count || t->v2 >= count)
                {
                    hr = DDERR_INVALIDPARAMS;
                    break;
                }
                hr = IDirect3DDevice7_DrawIndexedPrimitive(d->core, D3DPT_LINELIST, D3DFVF_TLVERTEX, vertices, count, indices, 2, 0);
            }
            else if (ins.bOpcode == D3DOP_STATERENDER && ins.bSize == sizeof(D3DSTATE))
            {
                D3DSTATE *s = (void *)p;
                hr = DeviceSetRender(face, s->drstRenderStateType, s->dwArg[0]);
            }
            else if (ins.bOpcode == D3DOP_STATELIGHT && ins.bSize == sizeof(D3DSTATE))
            {
                D3DSTATE *s = (void *)p;
                hr = DeviceSetLightState(face, s->dlstLightStateType, s->dwArg[0]);
            }
            else if (ins.bOpcode == D3DOP_STATETRANSFORM && ins.bSize == sizeof(D3DSTATE))
            {
                D3DSTATE *s = (void *)p;
                D3DMATRIX m;
                hr = DeviceGetMatrix(face, s->dwArg[0], &m);
                if (SUCCEEDED(hr))
                    hr = DeviceSetTransform(face, s->dtstTransformStateType, &m);
            }
            else if (ins.bOpcode == D3DOP_MATRIXLOAD && ins.bSize == sizeof(D3DMATRIXLOAD))
            {
                D3DMATRIXLOAD *s = (void *)p;
                D3DMATRIX m;
                hr = DeviceGetMatrix(face, s->hSrcMatrix, &m);
                if (SUCCEEDED(hr))
                    hr = DeviceSetMatrix(face, s->hDestMatrix, &m);
            }
            else if (ins.bOpcode == D3DOP_MATRIXMULTIPLY && ins.bSize == sizeof(D3DMATRIXMULTIPLY))
            {
                D3DMATRIXMULTIPLY *s = (void *)p;
                D3DMATRIX a, b, c;
                hr = DeviceGetMatrix(face, s->hSrcMatrix1, &a);
                if (SUCCEEDED(hr))
                    hr = DeviceGetMatrix(face, s->hSrcMatrix2, &b);
                if (SUCCEEDED(hr))
                {
                    D3dMultiply(&c, &a, &b);
                    hr = DeviceSetMatrix(face, s->hDestMatrix, &c);
                }
            }
            else
            {
                hr = D3DERR_EXECUTE_FAILED;
                break;
            }
            if (FAILED(hr))
                break;
        }
        if (FAILED(hr))
            break;
        at += ins.bSize * ins.wCount;
    next_instruction:
        hr = D3DERR_EXECUTE_FAILED;
    }
    free(vertices);
    return hr;
}
static HRESULT WINAPI DeviceGetRoot(void *face, void **out)
{
    DD_METHOD_LOCK();
    if (!out)
        return DDERR_INVALIDPARAMS;
    LegacyDevice *d = (LegacyDevice *)Base(face);
    *out = Face(&d->root->base, ((DdFace *)face)->version);
    LAdd(*out);
    return D3D_OK;
}
static HRESULT WINAPI DeviceSetTarget(void *face, void *surface, DWORD flags)
{
    DD_METHOD_LOCK();
    if (!surface)
        return DDERR_INVALIDPARAMS;
    return IDirect3DDevice7_SetRenderTarget(((LegacyDevice *)Base(face))->core, DdSurfaceFace(DdSurfaceOf(surface), DDS_V7), flags);
}
static HRESULT WINAPI DeviceGetTarget(void *face, void **out)
{
    DD_METHOD_LOCK();
    if (!out)
        return DDERR_INVALIDPARAMS;
    LegacyDevice *d = (LegacyDevice *)Base(face);
    DdSurface *s = D3dDeviceOf(d->core)->target;
    *out = DdSurfaceFace(s, ((DdFace *)face)->version == 2 ? DDS_V4 : DDS_V1);
    DdSurfaceAddRef(s);
    return D3D_OK;
}

static LegacyDevice *LegacyDeviceOf(void *face)
{
    if (!face)
        return NULL;
    for (unsigned n = 0; n < 3; ++n)
        if (((DdFace *)face)->table == device_tables[n])
            return (LegacyDevice *)Base(face);
    return NULL;
}
HRESULT D3dLegacyTextureHandle(void *texture, void *device, LPD3DTEXTUREHANDLE out)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = LegacyDeviceOf(device);
    if (!d || !out)
        return DDERR_INVALIDPARAMS;
    DdSurface *s = DdSurfaceOf(texture);
    if (!s)
        return DDERR_INVALIDPARAMS;
    for (DWORD n = 0; n < d->texture_count; ++n)
        if (d->textures[n] == s)
        {
            *out = n + 1;
            return D3D_OK;
        }
    DdSurface **more = realloc(d->textures, (d->texture_count + 1) * sizeof *more);
    if (!more)
        return DDERR_OUTOFMEMORY;
    d->textures = more;
    more[d->texture_count++] = s;
    DdSurfaceAddRef(s);
    *out = d->texture_count;
    return D3D_OK;
}
static HRESULT WINAPI DeviceTexture(void *face, DWORD stage, LPDIRECT3DTEXTURE2 texture)
{
    DD_METHOD_LOCK();
    return IDirect3DDevice7_SetTexture(((LegacyDevice *)Base(face))->core, stage,
                                       texture ? DdSurfaceFace(DdSurfaceOf(texture), DDS_V7) : NULL);
}
static HRESULT WINAPI DeviceGetTexture(void *face, DWORD stage, LPDIRECT3DTEXTURE2 *out)
{
    DD_METHOD_LOCK();
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = NULL;
    IDirectDrawSurface7 *s = NULL;
    HRESULT hr = IDirect3DDevice7_GetTexture(((LegacyDevice *)Base(face))->core, stage, &s);
    if (SUCCEEDED(hr) && s)
    {
        hr = IDirectDrawSurface7_QueryInterface(s, &IID_IDirect3DTexture2, (void **)out);
        IDirectDrawSurface7_Release(s);
    }
    return hr;
}
static HRESULT WINAPI ViewNextLight(void *face, LPDIRECT3DLIGHT light, LPDIRECT3DLIGHT *out, DWORD flags)
{
    DD_METHOD_LOCK();
    LegacyView *v = (LegacyView *)Base(face);
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = NULL;
    DWORD n = 0;
    if (flags == D3DNEXT_TAIL)
        n = v->count ? v->count - 1 : 0;
    else if (flags == D3DNEXT_NEXT)
    {
        for (; n < v->count && v->lights[n] != (LegacyLight *)Base(light); ++n)
        {
        }
        ++n;
    }
    else if (flags != D3DNEXT_HEAD)
        return DDERR_INVALIDPARAMS;
    if (n >= v->count)
        return D3D_OK;
    *out = Face(&v->lights[n]->base, 0);
    LAdd(*out);
    return D3D_OK;
}
static HRESULT WINAPI DeviceNextView(void *face, void *view, void **out, DWORD flags)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = NULL;
    DWORD n = 0;
    if (flags == D3DNEXT_TAIL)
        n = d->view_count ? d->view_count - 1 : 0;
    else if (flags == D3DNEXT_NEXT)
    {
        for (; n < d->view_count && d->views[n] != (LegacyView *)Base(view); ++n)
        {
        }
        ++n;
    }
    else if (flags != D3DNEXT_HEAD)
        return DDERR_INVALIDPARAMS;
    if (n >= d->view_count)
        return D3DERR_NOVIEWPORTS;
    *out = Face(&d->views[n]->base, ((DdFace *)face)->version);
    LAdd(*out);
    return D3D_OK;
}
static HRESULT WINAPI ViewSetDepth(void *face, void *surface)
{
    DD_METHOD_LOCK();
    LegacyView *v = (LegacyView *)Base(face);
    DdSurface *s = surface ? DdSurfaceOf(surface) : NULL;
    if (s)
        DdSurfaceAddRef(s);
    if (v->depth)
        DdSurfaceRelease(v->depth);
    v->depth = s;
    return D3D_OK;
}
static HRESULT WINAPI ViewGetDepth(void *face, void **out, LPBOOL valid)
{
    DD_METHOD_LOCK();
    if (!out || !valid)
        return DDERR_INVALIDPARAMS;
    LegacyView *v = (LegacyView *)Base(face);
    *valid = v->depth != NULL;
    *out = v->depth ? DdSurfaceFace(v->depth, DDS_V1) : NULL;
    if (v->depth)
        DdSurfaceAddRef(v->depth);
    return D3D_OK;
}
static HRESULT WINAPI ViewGetDepth2(void *face, void **out, LPBOOL valid)
{
    DD_METHOD_LOCK();
    HRESULT hr = ViewGetDepth(face, out, valid);
    if (SUCCEEDED(hr) && *out)
        *out = DdSurfaceFace(((LegacyView *)Base(face))->depth, DDS_V4);
    return hr;
}
static HRESULT WINAPI DeviceBegin(void *face, D3DPRIMITIVETYPE type, DWORD format, DWORD flags)
{
    DD_METHOD_LOCK();
    (void)flags;
    LegacyDevice *d = (LegacyDevice *)Base(face);
    if (d->immediate)
        return D3DERR_INBEGIN;
    D3dLayout layout;
    format = VertexFormat(face, format);
    if (!D3dLayoutOf(format, &layout))
        return D3DERR_INVALIDVERTEXTYPE;
    free(d->vertices);
    free(d->indices);
    d->vertices = NULL;
    d->indices = NULL;
    d->vertex_count = d->index_count = 0;
    d->fvf = format;
    d->primitive = type;
    d->immediate = TRUE;
    d->immediate_indexed = FALSE;
    return D3D_OK;
}
static HRESULT WINAPI DeviceBeginIndexed(void *face, D3DPRIMITIVETYPE type, DWORD format, LPVOID vertices, DWORD count, DWORD flags)
{
    DD_METHOD_LOCK();
    if (!vertices || count > D3DMAXNUMVERTICES)
        return DDERR_INVALIDPARAMS;
    HRESULT hr = DeviceBegin(face, type, format, flags);
    if (FAILED(hr))
        return hr;
    LegacyDevice *d = (LegacyDevice *)Base(face);
    D3dLayout layout;
    D3dLayoutOf(d->fvf, &layout);
    d->vertices = malloc(count * layout.stride);
    if (!d->vertices)
    {
        d->immediate = FALSE;
        return DDERR_OUTOFMEMORY;
    }
    memcpy(d->vertices, vertices, count * layout.stride);
    d->vertex_count = count;
    d->immediate_indexed = TRUE;
    return D3D_OK;
}
static HRESULT WINAPI DeviceVertex(void *face, LPVOID vertex)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    if (!d->immediate)
        return D3DERR_NOTINBEGIN;
    if (!vertex || d->vertex_count == D3DMAXNUMVERTICES)
        return DDERR_INVALIDPARAMS;
    D3dLayout layout;
    D3dLayoutOf(d->fvf, &layout);
    BYTE *more = realloc(d->vertices, (d->vertex_count + 1) * layout.stride);
    if (!more)
        return DDERR_OUTOFMEMORY;
    d->vertices = more;
    memcpy(more + d->vertex_count++ * layout.stride, vertex, layout.stride);
    return D3D_OK;
}
static HRESULT WINAPI DeviceIndex(void *face, WORD index)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    if (!d->immediate)
        return D3DERR_NOTINBEGIN;
    if (index >= d->vertex_count || d->index_count == D3DMAXNUMVERTICES)
        return DDERR_INVALIDPARAMS;
    WORD *more = realloc(d->indices, (d->index_count + 1) * sizeof *more);
    if (!more)
        return DDERR_OUTOFMEMORY;
    d->indices = more;
    more[d->index_count++] = index;
    return D3D_OK;
}
static HRESULT WINAPI DeviceEnd(void *face, DWORD flags)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    if (!d->immediate)
        return D3DERR_NOTINBEGIN;
    d->immediate = FALSE;
    HRESULT hr = Ready(d);
    if (FAILED(hr))
        return hr;
    return d->immediate_indexed ? IDirect3DDevice7_DrawIndexedPrimitive(d->core, d->primitive, d->fvf, d->vertices, d->vertex_count,
                                                                        d->indices, d->index_count, flags)
                                : IDirect3DDevice7_DrawPrimitive(d->core, d->primitive, d->fvf, d->vertices, d->vertex_count, flags);
}
static HRESULT WINAPI DeviceSetRender(void *face, D3DRENDERSTATETYPE state, DWORD value)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    if ((unsigned)state >= 256)
        return DDERR_INVALIDPARAMS;
    if (state == D3DRENDERSTATE_TEXTUREHANDLE)
    {
        if (value > d->texture_count)
            return DDERR_INVALIDPARAMS;
        d->legacy_render[state] = value;
        HRESULT hr = IDirect3DDevice7_SetTexture(d->core, 0, value ? DdSurfaceFace(d->textures[value - 1], DDS_V7) : NULL);
        if (FAILED(hr))
            return hr;
        if (value)
            return DeviceSetRender(face, D3DRENDERSTATE_TEXTUREMAPBLEND, d->legacy_render[D3DRENDERSTATE_TEXTUREMAPBLEND]);
        IDirect3DDevice7_SetTextureStageState(d->core, 0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        return IDirect3DDevice7_SetTextureStageState(d->core, 0, D3DTSS_COLOROP, D3DTOP_DISABLE);
    }
    if (state == D3DRENDERSTATE_TEXTUREMAPBLEND)
    {
        DWORD color, alpha;
        switch (value)
        {
        case D3DTBLEND_DECAL:
        case D3DTBLEND_COPY:
            color = D3DTOP_SELECTARG1;
            alpha = D3DTOP_SELECTARG1;
            break;
        case D3DTBLEND_MODULATE:
            color = D3DTOP_MODULATE;
            alpha = D3DTOP_SELECTARG1;
            break;
        case D3DTBLEND_DECALALPHA:
            color = D3DTOP_BLENDTEXTUREALPHA;
            alpha = D3DTOP_SELECTARG2;
            break;
        case D3DTBLEND_MODULATEALPHA:
            color = alpha = D3DTOP_MODULATE;
            break;
        case D3DTBLEND_ADD:
            color = D3DTOP_ADD;
            alpha = D3DTOP_SELECTARG2;
            break;
        default:
            return DDERR_UNSUPPORTED;
        }
        d->legacy_render[state] = value;
        IDirect3DDevice7_SetTextureStageState(d->core, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        IDirect3DDevice7_SetTextureStageState(d->core, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        IDirect3DDevice7_SetTextureStageState(d->core, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        IDirect3DDevice7_SetTextureStageState(d->core, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        IDirect3DDevice7_SetTextureStageState(d->core, 0, D3DTSS_COLOROP, color);
        return IDirect3DDevice7_SetTextureStageState(d->core, 0, D3DTSS_ALPHAOP, alpha);
    }
    DWORD stage = state == D3DRENDERSTATE_TEXTUREADDRESS    ? D3DTSS_ADDRESS
                  : state == D3DRENDERSTATE_TEXTUREADDRESSU ? D3DTSS_ADDRESSU
                  : state == D3DRENDERSTATE_TEXTUREADDRESSV ? D3DTSS_ADDRESSV
                  : state == D3DRENDERSTATE_BORDERCOLOR     ? D3DTSS_BORDERCOLOR
                  : state == D3DRENDERSTATE_MIPMAPLODBIAS   ? D3DTSS_MIPMAPLODBIAS
                  : state == D3DRENDERSTATE_ANISOTROPY      ? D3DTSS_MAXANISOTROPY
                                                            : 0;
    if (stage)
    {
        d->legacy_render[state] = value;
        return IDirect3DDevice7_SetTextureStageState(d->core, 0, stage, value);
    }
    if (state == D3DRENDERSTATE_TEXTUREMAG)
    {
        d->legacy_render[state] = value;
        return IDirect3DDevice7_SetTextureStageState(d->core, 0, D3DTSS_MAGFILTER,
                                                     value == D3DFILTER_NEAREST ? D3DTFG_POINT : D3DTFG_LINEAR);
    }
    if (state == D3DRENDERSTATE_TEXTUREMIN)
    {
        if (value < 1 || value > 6)
            return DDERR_INVALIDPARAMS;
        d->legacy_render[state] = value;
        IDirect3DDevice7_SetTextureStageState(d->core, 0, D3DTSS_MINFILTER,
                                              value == 1 || value == 3 || value == 4 ? D3DTFN_POINT : D3DTFN_LINEAR);
        return IDirect3DDevice7_SetTextureStageState(d->core, 0, D3DTSS_MIPFILTER,
                                                     value < 3                  ? D3DTFP_NONE
                                                     : value == 3 || value == 5 ? D3DTFP_POINT
                                                                                : D3DTFP_LINEAR);
    }
    if (state == D3DRENDERSTATE_WRAPU || state == D3DRENDERSTATE_WRAPV)
    {
        d->legacy_render[state] = value;
        return IDirect3DDevice7_SetRenderState(d->core, D3DRENDERSTATE_WRAP0,
                                               (d->legacy_render[D3DRENDERSTATE_WRAPU] ? D3DWRAP_U : 0) |
                                                   (d->legacy_render[D3DRENDERSTATE_WRAPV] ? D3DWRAP_V : 0));
    }
    return IDirect3DDevice7_SetRenderState(d->core, state, value);
}
static HRESULT WINAPI DeviceGetRender(void *face, D3DRENDERSTATETYPE state, DWORD *out)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    if (!out || (unsigned)state >= 256)
        return DDERR_INVALIDPARAMS;
    switch (state)
    {
    case D3DRENDERSTATE_TEXTUREMAPBLEND:
    case D3DRENDERSTATE_TEXTUREHANDLE:
    case D3DRENDERSTATE_TEXTUREADDRESS:
    case D3DRENDERSTATE_TEXTUREADDRESSU:
    case D3DRENDERSTATE_TEXTUREADDRESSV:
    case D3DRENDERSTATE_BORDERCOLOR:
    case D3DRENDERSTATE_MIPMAPLODBIAS:
    case D3DRENDERSTATE_ANISOTROPY:
    case D3DRENDERSTATE_TEXTUREMAG:
    case D3DRENDERSTATE_TEXTUREMIN:
    case D3DRENDERSTATE_WRAPU:
    case D3DRENDERSTATE_WRAPV:
        *out = d->legacy_render[state];
        return D3D_OK;
    default:
        return IDirect3DDevice7_GetRenderState(d->core, state, out);
    }
}

#include "d3d_legacy_caps.inc"
#include "d3d_legacy_vb.inc"

/* Device3 statistics match D3DIM 4.09; older device statistics remain open. */
static HRESULT WINAPI DeviceStats(void *face, LPD3DSTATS out)
{
    (void)out;
    return ((LegacyDevice *)Base(face))->created_version >= 3 ? E_NOTIMPL : DDERR_UNSUPPORTED;
}
static HRESULT WINAPI ExecuteInitialize(void *face, LPDIRECT3DDEVICE device, LPD3DEXECUTEBUFFERDESC desc)
{
    (void)face;
    (void)device;
    (void)desc;
    return DDERR_ALREADYINITIALIZED;
}
static HRESULT WINAPI ExecuteValidate(void *face, DWORD *offset, LPD3DVALIDATECALLBACK callback, void *context, DWORD reserved)
{
    (void)face;
    (void)offset;
    (void)callback;
    (void)context;
    (void)reserved;
    return E_NOTIMPL;
}
static HRESULT WINAPI ExecuteOptimize(void *face, DWORD reserved)
{
    (void)face;
    return reserved ? DDERR_INVALIDPARAMS : E_NOTIMPL;
}

/* Method tables and exact-signature forwarding functions are below. */
#include "d3d_legacy_tables.inc"
HRESULT D3dLegacyFor(void *object, REFIID iid, void **out)
{
    DD_METHOD_LOCK();
    DdObject *dd = object;
    if (dd->created_version == DD_V7)
        return E_NOINTERFACE;
    Legacy *r = dd->legacy;
    if (!r)
    {
        r = calloc(1, sizeof *r);
        if (!r)
            return DDERR_OUTOFMEMORY;
        Init(&r->base, L_ROOT, root_tables, 3);
        r->dd = dd;
        dd->legacy = r;
        DdObjectAddRef(dd);
    }
    else
        LAdd(Face(&r->base, 0));
    unsigned version = IsEqualGUID(iid, &IID_IDirect3D3) ? 2 : IsEqualGUID(iid, &IID_IDirect3D2) ? 1 : 0;
    *out = Face(&r->base, version);
    return D3D_OK;
}

/* Direct3D 1 creates its device by querying a render target surface. */
HRESULT D3dLegacySurfaceFor(void *object, REFIID iid, void **out)
{
    DD_METHOD_LOCK();
    DdSurface *s = object;
    if (!s || !s->dd || s->dead)
        return DDERR_INVALIDOBJECT;
    /* Original Surface1 accepts the device-class GUID, not the bare
       IDirect3DDevice IID, even after the aggregate has been created. */
    if (IsEqualGUID(iid, &IID_IDirect3DDevice))
        return DDERR_INVALIDPARAMS;
    for (LegacyDevice *d = legacy_devices; d; d = d->next)
        if (D3dDeviceOf(d->core)->target == s)
        {
            if (d->created_version != 1 || !IsEqualGUID(iid, &D3dDeviceOf(d->core)->guid))
                return DDERR_INVALIDPARAMS;
            *out = Face(&d->base, 0);
            LAdd(*out);
            return D3D_OK;
        }
    void *root = NULL;
    HRESULT hr = D3dLegacyFor(s->dd, &IID_IDirect3D, &root);
    if (FAILED(hr))
        return hr;
    hr = RootCreateDevice3(root, iid, DdSurfaceFace(s, DDS_V1), out, NULL);
    if (SUCCEEDED(hr))
    {
        LegacyDevice *d = (LegacyDevice *)Base(*out);
        d->aggregate = s;
        s->legacy_device = d;
        /* Transfer the core's target reference to the returned Device1
           interface. The surface now owns the device and destroys it. */
        D3dDeviceOf(d->core)->borrowed_target = TRUE;
    }
    LRelease(root);
    return hr;
}

/* Called before the last surface reference frees its pixels. No reference
   cycle exists: the aggregated core borrows its controlling surface. */
void D3dLegacySurfaceDestroy(DdSurface *surface)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = surface->legacy_device;
    if (!d)
        return;
    surface->legacy_device = NULL;
    d->aggregate = NULL;
    LRelease(Face(&d->base, 0));
}
