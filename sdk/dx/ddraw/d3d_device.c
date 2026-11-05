/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Direct3D 7 in the DDRAW.DLL of ARM64 processes: IDirect3DDevice7.


   The device keeps its state here (d3d_private.h) and collects its work in a
   batch of commands for the driver. A setter changes the state of the device
   and appends the change to the batch; while a state block is recorded it
   only writes into the block. A draw copies its vertices into the pool of
   the device and appends one command. Flush sends the batch: one call into
   the host for everything since the last one. */
#include "d3d_private.h"

LONG d3d_pending;
static D3dDevice *devices;

static D3dDevice *This(void *face) { return (D3dDevice *)((DdFace *)face)->object; }
D3dDevice *D3dDeviceOf(void *face) { return face ? This(face) : NULL; }

/* ------------------------------------------------------------------ batches */
static HRESULT Flush(D3dDevice *d)
{
    HRESULT result = D3D_OK;
    if (!d->command_bytes)
    {
        d->pool_bytes = 0;
        return D3D_OK;
    }
    result = D3dDrvRun(d->context, d->commands, d->command_bytes);
    d->command_bytes = 0;
    d->last_command = ~0u;
    d->pool_bytes = 0;
    DdDropRef(&d3d_pending);
    if (FAILED(result) && SUCCEEDED(d->flush_result)) d->flush_result = result;
    return result;
}

void D3dFlushAll(void)
{
    D3dDevice *d;
    for (d = devices; d; d = d->next) Flush(d);
}

/* Room for a command of `words` words behind its header; with `merge` a
   command of the same kind that ends the batch grows instead (a run of
   render states is one command). Null when memory is out. */
static DWORD *Append(D3dDevice *d, DWORD op, DWORD words, BOOL merge)
{
    const DWORD bytes = words * 4;
    NEO98D3D_COMMAND *command;
    DWORD *out;
    /* A batch nobody asks for still goes after a megabyte of commands. */
    if (d->command_bytes + bytes + sizeof *command > (1u << 20)) Flush(d);
    if (d->command_bytes + bytes + sizeof *command > d->command_capacity)
    {
        DWORD capacity = d->command_capacity ? d->command_capacity * 2 : 16384;
        BYTE *more;
        while (capacity < d->command_bytes + bytes + sizeof *command) capacity *= 2;
        more = (BYTE *)realloc(d->commands, capacity);
        if (!more) return NULL;
        d->commands = more;
        d->command_capacity = capacity;
    }
    if (!d->command_bytes) DdAddRef(&d3d_pending);
    if (merge && d->last_command != ~0u)
    {
        command = (NEO98D3D_COMMAND *)(d->commands + d->last_command);
        if (command->op == op && d->last_command + command->bytes == d->command_bytes)
        {
            out = (DWORD *)(d->commands + d->command_bytes);
            command->bytes += bytes;
            d->command_bytes += bytes;
            return out;
        }
    }
    d->last_command = d->command_bytes;
    command = (NEO98D3D_COMMAND *)(d->commands + d->command_bytes);
    command->op = op;
    command->bytes = (DWORD)sizeof *command + bytes;
    d->command_bytes += command->bytes;
    return (DWORD *)(command + 1);
}

/* Room in the pool for the vertices and indices of one draw. The batch names
   the pool by address, so the pool only moves while it is empty. */
static BYTE *Pool(D3dDevice *d, DWORD bytes)
{
    BYTE *out;
    bytes = (bytes + 7) & ~7u;
    if (d->pool_bytes + bytes > d->pool_capacity)
    {
        Flush(d);
        if (bytes > d->pool_capacity)
        {
            const DWORD capacity = (bytes + (1u << 20) - 1) & ~((1u << 20) - 1);
            BYTE *more = (BYTE *)malloc(capacity);
            if (!more) return NULL;
            free(d->pool);
            d->pool = more;
            d->pool_capacity = capacity;
        }
    }
    out = d->pool + d->pool_bytes;
    d->pool_bytes += bytes;
    return out;
}

static void EmitRender(D3dDevice *d, DWORD state, DWORD value)
{
    DWORD *w = Append(d, NEO98D3D_OP_RENDERSTATE, 2, TRUE);
    if (w) { w[0] = state; w[1] = value; }
}

static void EmitStage(D3dDevice *d, DWORD stage, DWORD state, DWORD value)
{
    DWORD *w = Append(d, NEO98D3D_OP_STAGESTATE, 3, TRUE);
    if (w) { w[0] = stage; w[1] = state; w[2] = value; }
}

static DWORD DriverSurface(const DdSurface *s) { return s && s->store && !s->dead ? s->store->driver : 0; }

static void EmitTexture(D3dDevice *d, DWORD stage, const DdSurface *texture)
{
    DWORD *w = Append(d, NEO98D3D_OP_TEXTURE, 2, TRUE);
    if (w) { w[0] = stage; w[1] = DriverSurface(texture); }
}

static void EmitViewport(D3dDevice *d)
{
    DWORD *w = Append(d, NEO98D3D_OP_VIEWPORT, 6, FALSE);
    if (w) memcpy(w, &d->state.viewport, 24);
}

static const DWORD matrix_types[D3D_MATRICES] = {
    D3DTRANSFORMSTATE_WORLD, D3DTRANSFORMSTATE_WORLD1, D3DTRANSFORMSTATE_WORLD2, D3DTRANSFORMSTATE_WORLD3,
    D3DTRANSFORMSTATE_VIEW, D3DTRANSFORMSTATE_PROJECTION,
    D3DTRANSFORMSTATE_TEXTURE0, D3DTRANSFORMSTATE_TEXTURE1, D3DTRANSFORMSTATE_TEXTURE2, D3DTRANSFORMSTATE_TEXTURE3,
    D3DTRANSFORMSTATE_TEXTURE4, D3DTRANSFORMSTATE_TEXTURE5, D3DTRANSFORMSTATE_TEXTURE6, D3DTRANSFORMSTATE_TEXTURE7};

static int MatrixIndex(DWORD type)
{
    int i;
    for (i = 0; i < D3D_MATRICES; ++i)
        if (matrix_types[i] == type) return i;
    return -1;
}

static void EmitMatrix(D3dDevice *d, int index)
{
    DWORD *w = Append(d, NEO98D3D_OP_TRANSFORM, 17, FALSE);
    if (!w) return;
    w[0] = matrix_types[index];
    memcpy(w + 1, &d->state.matrix[index], 64);
}

static void EmitMaterial(D3dDevice *d)
{
    DWORD *w = Append(d, NEO98D3D_OP_MATERIAL, 17, FALSE);
    if (w) memcpy(w, &d->state.material, 68);
}

static void EmitLight(D3dDevice *d, const D3dLight *light)
{
    DWORD *w;
    if (light->slot < 0) return;
    w = Append(d, NEO98D3D_OP_LIGHT, 27, FALSE);
    if (!w) return;
    w[0] = (DWORD)light->slot;
    memcpy(w + 1, &light->data, 104);
}

static void EmitLightEnable(D3dDevice *d, int slot, BOOL enable)
{
    DWORD *w = Append(d, NEO98D3D_OP_LIGHTENABLE, 2, TRUE);
    if (w) { w[0] = (DWORD)slot; w[1] = enable ? 1 : 0; }
}

static void EmitClipPlane(D3dDevice *d, DWORD index)
{
    DWORD *w = Append(d, NEO98D3D_OP_CLIPPLANE, 5, FALSE);
    if (!w) return;
    w[0] = index;
    memcpy(w + 1, d->state.clip[index], 16);
}

/* -------------------------------------------------------------------- state */
static const D3DMATRIX identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static DWORD FloatBits(float value)
{
    DWORD bits;
    memcpy(&bits, &value, 4);
    return bits;
}

/* The state a device of DirectX 7 starts with. */
static void DefaultState(D3dDevice *d)
{
    D3dState *s = &d->state;
    DWORD i;
    memset(s->render, 0, sizeof s->render);
    memset(s->stage, 0, sizeof s->stage);
    s->render[D3DRENDERSTATE_TEXTUREPERSPECTIVE] = TRUE;
    s->render[D3DRENDERSTATE_ZENABLE] = d->depth ? D3DZB_TRUE : D3DZB_FALSE;
    s->render[D3DRENDERSTATE_FILLMODE] = D3DFILL_SOLID;
    s->render[D3DRENDERSTATE_SHADEMODE] = D3DSHADE_GOURAUD;
    s->render[D3DRENDERSTATE_ZWRITEENABLE] = TRUE;
    s->render[D3DRENDERSTATE_LASTPIXEL] = TRUE;
    s->render[D3DRENDERSTATE_SRCBLEND] = D3DBLEND_ONE;
    s->render[D3DRENDERSTATE_DESTBLEND] = D3DBLEND_ZERO;
    s->render[D3DRENDERSTATE_CULLMODE] = D3DCULL_CCW;
    s->render[D3DRENDERSTATE_ZFUNC] = D3DCMP_LESSEQUAL;
    s->render[D3DRENDERSTATE_ALPHAFUNC] = D3DCMP_ALWAYS;
    s->render[D3DRENDERSTATE_FOGEND] = FloatBits(1.0f);
    s->render[D3DRENDERSTATE_FOGDENSITY] = FloatBits(1.0f);
    s->render[D3DRENDERSTATE_STENCILFAIL] = D3DSTENCILOP_KEEP;
    s->render[D3DRENDERSTATE_STENCILZFAIL] = D3DSTENCILOP_KEEP;
    s->render[D3DRENDERSTATE_STENCILPASS] = D3DSTENCILOP_KEEP;
    s->render[D3DRENDERSTATE_STENCILFUNC] = D3DCMP_ALWAYS;
    s->render[D3DRENDERSTATE_STENCILMASK] = 0xffffffffu;
    s->render[D3DRENDERSTATE_STENCILWRITEMASK] = 0xffffffffu;
    s->render[D3DRENDERSTATE_TEXTUREFACTOR] = 0xffffffffu;
    s->render[D3DRENDERSTATE_CLIPPING] = TRUE;
    s->render[D3DRENDERSTATE_LIGHTING] = TRUE;
    s->render[D3DRENDERSTATE_COLORVERTEX] = TRUE;
    s->render[D3DRENDERSTATE_LOCALVIEWER] = TRUE;
    s->render[D3DRENDERSTATE_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1;
    s->render[D3DRENDERSTATE_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
    s->render[D3DRENDERSTATE_AMBIENTMATERIALSOURCE] = D3DMCS_MATERIAL;
    s->render[D3DRENDERSTATE_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL;
    for (i = 0; i < D3D_STAGES; ++i)
    {
        DWORD *t = s->stage[i];
        t[D3DTSS_COLOROP] = i == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
        t[D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        t[D3DTSS_COLORARG2] = D3DTA_CURRENT;
        t[D3DTSS_ALPHAOP] = i == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
        t[D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        t[D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        /* Every stage starts with the first coordinate set (measured against
           D3DIM700.DLL; the documentation says the number of the stage). */
        t[D3DTSS_TEXCOORDINDEX] = 0;
        t[D3DTSS_ADDRESS] = D3DTADDRESS_WRAP;
        t[D3DTSS_ADDRESSU] = D3DTADDRESS_WRAP;
        t[D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
        t[D3DTSS_MAGFILTER] = D3DTFG_POINT;
        t[D3DTSS_MINFILTER] = D3DTFN_POINT;
        t[D3DTSS_MIPFILTER] = D3DTFP_NONE;
        t[D3DTSS_MAXANISOTROPY] = 1;
    }
    for (i = 0; i < D3D_MATRICES; ++i) s->matrix[i] = identity;
    memset(&s->material, 0, sizeof s->material);
    memset(s->clip, 0, sizeof s->clip);
    s->viewport.dwX = s->viewport.dwY = 0;
    s->viewport.dwWidth = d->target->width;
    s->viewport.dwHeight = d->target->height;
    s->viewport.dvMinZ = 0.0f;
    s->viewport.dvMaxZ = 1.0f;
}

/* Everything the driver's context holds, said once: after this the context
   and the device agree, whatever the context started with. */
static void EmitAll(D3dDevice *d)
{
    DWORD i, k;
    for (i = 1; i < D3D_RENDERSTATES; ++i) EmitRender(d, i, d->state.render[i]);
    for (i = 0; i < D3D_STAGES; ++i)
    {
        for (k = D3DTSS_COLOROP; k <= D3DTSS_TEXTURETRANSFORMFLAGS; ++k) EmitStage(d, i, k, d->state.stage[i][k]);
        EmitTexture(d, i, d->state.texture[i]);
    }
    for (i = 0; i < D3D_MATRICES; ++i) EmitMatrix(d, (int)i);
    EmitMaterial(d);
    EmitViewport(d);
    for (i = 0; i < D3D_CLIPPLANES; ++i) EmitClipPlane(d, i);
}

static void SetRenderNow(D3dDevice *d, DWORD state, DWORD value)
{
    if (d->state.render[state] == value) return;
    d->state.render[state] = value;
    EmitRender(d, state, value);
}

/* The render states of the interfaces before this one that a device of
   DirectX 7 no longer has: the texture handle and what became texture stage
   states, the stipple pattern and others. GetRenderState and SetRenderState
   refuse each of them (measured against D3DIM700.DLL for every number,
   tools/test_arm64_d3d.py). */
static BOOL Retired(DWORD state)
{
    switch (state)
    {
    case D3DRENDERSTATE_TEXTUREHANDLE: case D3DRENDERSTATE_TEXTUREADDRESS: case D3DRENDERSTATE_WRAPU: case D3DRENDERSTATE_WRAPV:
    case D3DRENDERSTATE_MONOENABLE: case D3DRENDERSTATE_ROP2: case D3DRENDERSTATE_PLANEMASK: case D3DRENDERSTATE_TEXTUREMAG:
    case D3DRENDERSTATE_TEXTUREMIN: case D3DRENDERSTATE_TEXTUREMAPBLEND: case D3DRENDERSTATE_SUBPIXEL: case D3DRENDERSTATE_SUBPIXELX:
    case D3DRENDERSTATE_STIPPLEENABLE: case D3DRENDERSTATE_BORDERCOLOR: case D3DRENDERSTATE_TEXTUREADDRESSU:
    case D3DRENDERSTATE_TEXTUREADDRESSV: case D3DRENDERSTATE_MIPMAPLODBIAS: case D3DRENDERSTATE_ANISOTROPY:
    case D3DRENDERSTATE_TRANSLUCENTSORTINDEPENDENT:
        return TRUE;
    default:
        return state >= D3DRENDERSTATE_STIPPLEPATTERN00 && state <= D3DRENDERSTATE_STIPPLEPATTERN31;
    }
}

static void SetStageNow(D3dDevice *d, DWORD stage, DWORD state, DWORD value)
{
    if (state == D3DTSS_ADDRESS)
    {
        /* D3DTSS_ADDRESS sets both directions. */
        SetStageNow(d, stage, D3DTSS_ADDRESSU, value);
        SetStageNow(d, stage, D3DTSS_ADDRESSV, value);
    }
    if (d->state.stage[stage][state] == value) return;
    d->state.stage[stage][state] = value;
    EmitStage(d, stage, state, value);
}

static void SetTextureNow(D3dDevice *d, DWORD stage, DdSurface *texture)
{
    DdSurface *old = d->state.texture[stage];
    if (old == texture) return;
    if (texture) DdSurfaceAddRef(texture);
    d->state.texture[stage] = texture;
    EmitTexture(d, stage, texture);
    /* The old texture may go with this release; the batch that names it runs
       before its pixels do (DdFlush3D in the surface). */
    if (old) DdSurfaceRelease(old);
}

static void SetMatrixNow(D3dDevice *d, int index, const D3DMATRIX *matrix)
{
    d->state.matrix[index] = *matrix;
    EmitMatrix(d, index);
}

static D3dLight *FindLight(D3dDevice *d, DWORD index, BOOL create)
{
    static const D3DLIGHT7 standard = {D3DLIGHT_DIRECTIONAL, {{1.0f}, {1.0f}, {1.0f}, {0.0f}}, {{0.0f}, {0.0f}, {0.0f}, {0.0f}},
                                       {{0.0f}, {0.0f}, {0.0f}, {0.0f}}, {{0.0f}, {0.0f}, {0.0f}}, {{0.0f}, {0.0f}, {1.0f}},
                                       0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    D3dLight *light;
    DWORD i;
    for (i = 0; i < d->light_count; ++i)
        if (d->lights[i].index == index) return &d->lights[i];
    if (!create) return NULL;
    if (d->light_count == d->light_capacity)
    {
        const DWORD capacity = d->light_capacity ? d->light_capacity * 2 : 16;
        D3dLight *more = (D3dLight *)realloc(d->lights, capacity * sizeof *more);
        if (!more) return NULL;
        d->lights = more;
        d->light_capacity = capacity;
    }
    light = &d->lights[d->light_count++];
    light->index = index;
    light->data = standard;
    light->enabled = FALSE;
    light->slot = -1;
    return light;
}

static HRESULT SetLightNow(D3dDevice *d, DWORD index, const D3DLIGHT7 *data)
{
    D3dLight *light = FindLight(d, index, TRUE);
    if (!light) return DDERR_OUTOFMEMORY;
    light->data = *data;
    EmitLight(d, light);
    return D3D_OK;
}

/* An enabled light takes one of the slots of the driver's context; the
   program's light numbers are any DWORD. More lights than slots stay
   enabled here and wait for a free one. */
static HRESULT EnableLightNow(D3dDevice *d, DWORD index, BOOL enable)
{
    D3dLight *light = FindLight(d, index, TRUE);
    if (!light) return DDERR_OUTOFMEMORY;
    if (enable && light->slot < 0)
    {
        int slot;
        for (slot = 0; slot < (int)NEO98D3D_LIGHT_SLOTS; ++slot)
            if (!(d->slots & (1u << slot))) break;
        if (slot < (int)NEO98D3D_LIGHT_SLOTS)
        {
            d->slots |= 1u << slot;
            light->slot = slot;
            EmitLight(d, light);
            EmitLightEnable(d, slot, TRUE);
        }
    }
    else if (!enable && light->slot >= 0)
    {
        EmitLightEnable(d, light->slot, FALSE);
        d->slots &= ~(1u << light->slot);
        light->slot = -1;
    }
    light->enabled = enable;
    return D3D_OK;
}

/* The Z buffer of a render target: the attached surface with DDSCAPS_ZBUFFER. */
static DdSurface *DepthOf(DdSurface *target)
{
    DdAttachment *a;
    for (a = target->attached; a; a = a->next)
        if ((a->surface->caps & DDSCAPS_ZBUFFER) && DriverSurface(a->surface)) return a->surface;
    return NULL;
}

static HRESULT CheckTarget(const DdSurface *target)
{
    if (!target || target->dead) return DDERR_INVALIDPARAMS;
    if (!(target->caps & DDSCAPS_3DDEVICE)) return DDERR_INVALIDCAPS;
    if (target->caps & DDSCAPS_ZBUFFER) return DDERR_INVALIDCAPS;
    if (!DriverSurface(target)) return D3DERR_SURFACENOTINVIDMEM;
    if (target->format.flags & DDPF_PALETTEINDEXED8) return DDERR_INVALIDPIXELFORMAT;
    return D3D_OK;
}

/* ----------------------------------------------------------------- lifetime */
static const struct IDirect3DDevice7Vtbl device_table;

HRESULT D3dCreateDevice(DdObject *dd, const GUID *guid, DdSurface *target, D3dDevice **out)
{
    NEO98D3D_CAPS caps;
    D3dDevice *d;
    HRESULT result;
    *out = NULL;
    if (!D3dCaps(&caps)) return DDERR_NO3D;
    if (!IsEqualGUID(guid, &d3d_iid_hal) && !(IsEqualGUID(guid, &d3d_iid_tnlhal) && (caps.flags & NEO98D3D_CAP_TNL)))
        return DDERR_INVALIDPARAMS;
    result = CheckTarget(target);
    if (FAILED(result)) return result;
    d = (D3dDevice *)DdAllocate(sizeof *d);
    if (!d) return DDERR_OUTOFMEMORY;
    d->face.table = &device_table;
    d->face.object = d;
    d->references = 1;
    d->guid = *guid;
    d->target = target;
    d->depth = DepthOf(target);
    d->last_command = ~0u;
    result = D3dDrvCreateContext(DriverSurface(target), DriverSurface(d->depth), &d->context);
    if (FAILED(result))
    {
        free(d);
        return result;
    }
    d->dd = dd;
    DdObjectAddRef(dd);
    DdSurfaceAddRef(target);
    if (d->depth) DdSurfaceAddRef(d->depth);
    DefaultState(d);
    EmitAll(d);
    d->clip_status.dwFlags = D3DCLIPSTATUS_EXTENTS2;
    d->clip_status.dwStatus = 0;
    DdLock();
    d->next = devices;
    devices = d;
    DdUnlock();
    *out = d;
    return D3D_OK;
}

static void FreeBlock(D3dBlock *block)
{
    DWORD i;
    if (!block) return;
    for (i = 0; i < D3D_STAGES; ++i)
        if (block->state.texture[i]) DdSurfaceRelease(block->state.texture[i]);
    free(block->lights);
    free(block);
}

static void Destroy(D3dDevice *d)
{
    D3dDevice **link;
    DWORD i;
    Flush(d);
    DdLock();
    for (link = &devices; *link; link = &(*link)->next)
        if (*link == d) { *link = d->next; break; }
    DdUnlock();
    D3dDrvDestroyContext(d->context);
    for (i = 0; i < D3D_STAGES; ++i)
        if (d->state.texture[i]) DdSurfaceRelease(d->state.texture[i]);
    for (i = 0; i < d->block_count; ++i) FreeBlock(d->blocks[i]);
    if (d->recording) FreeBlock(d->recording);
    free(d->blocks);
    free(d->lights);
    if (d->depth) DdSurfaceRelease(d->depth);
    if (!d->borrowed_target) DdSurfaceRelease(d->target);
    DdObjectRelease(d->dd);
    free(d->commands);
    free(d->pool);
    free(d);
}

static HRESULT WINAPI Dev_QueryInterface(void *face, REFIID iid, void **out)
{
    D3dDevice *d = This(face);
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (!iid) return DDERR_INVALIDPARAMS;
    if (!IsEqualGUID(iid, &dd_iid_unknown) && !IsEqualGUID(iid, &d3d_iid_device7)) return E_NOINTERFACE;
    DdAddRef(&d->references);
    *out = &d->face;
    return S_OK;
}

static ULONG WINAPI Dev_AddRef(void *face) { return (ULONG)DdAddRef(&This(face)->references); }

static ULONG WINAPI Dev_Release(void *face)
{
    D3dDevice *d = This(face);
    const LONG left = DdDropRef(&d->references);
    if (left > 0) return (ULONG)left;
    Destroy(d);
    return 0;
}

/* ------------------------------------------------------------------- device */
static HRESULT WINAPI Dev_GetCaps(void *face, LPD3DDEVICEDESC7 desc)
{
    D3dDevice *d = This(face);
    NEO98D3D_CAPS caps;
    if (!desc) return DDERR_INVALIDPARAMS;
    if (!D3dCaps(&caps)) return DDERR_NO3D;
    D3dDescribe(&caps, &d->guid, desc, FALSE);
    return D3D_OK;
}

static HRESULT WINAPI Dev_EnumTextureFormats(void *face, LPD3DENUMPIXELFORMATSCALLBACK callback, LPVOID context)
{
    NEO98D3D_CAPS caps;
    DWORD i;
    (void)face;
    if (!callback) return DDERR_INVALIDPARAMS;
    if (!D3dCaps(&caps)) return DDERR_NO3D;
    for (i = 0; i < caps.textureFormatCount; ++i)
    {
        DDPIXELFORMAT format;
        DdFormatToPixelFormat(&caps.textureFormats[i], &format);
        if (callback(&format, context) != D3DENUMRET_OK) break;
    }
    return D3D_OK;
}

static HRESULT WINAPI Dev_BeginScene(void *face)
{
    D3dDevice *d = This(face);
    if (d->in_scene) return D3DERR_SCENE_IN_SCENE;
    d->in_scene = TRUE;
    d->flush_result = D3D_OK;
    return D3D_OK;
}

static HRESULT WINAPI Dev_EndScene(void *face)
{
    D3dDevice *d = This(face);
    HRESULT result;
    if (!d->in_scene) return D3DERR_SCENE_NOT_IN_SCENE;
    d->in_scene = FALSE;
    Flush(d);
    /* A batch of this scene the driver could not run, because the target is
       lost, is the answer of the scene. */
    result = d->flush_result;
    d->flush_result = D3D_OK;
    return result;
}

static HRESULT WINAPI Dev_GetDirect3D(void *face, LPDIRECT3D7 *out)
{
    if (!out) return DDERR_INVALIDPARAMS;
    return D3dObjectFor(This(face)->dd, &d3d_iid_direct3d7, (void **)out);
}

static HRESULT WINAPI Dev_SetRenderTarget(void *face, LPDIRECTDRAWSURFACE7 surface, DWORD flags)
{
    D3dDevice *d = This(face);
    DdSurface *target = DdSurfaceOf(surface), *depth, *old_target = d->target, *old_depth = d->depth;
    DWORD *w;
    HRESULT result;
    (void)flags;
    result = CheckTarget(target);
    if (FAILED(result)) return result;
    depth = DepthOf(target);
    w = Append(d, NEO98D3D_OP_TARGET, 2, FALSE);
    if (!w) return DDERR_OUTOFMEMORY;
    w[0] = DriverSurface(target);
    w[1] = DriverSurface(depth);
    DdSurfaceAddRef(target);
    if (depth) DdSurfaceAddRef(depth);
    d->target = target;
    d->depth = depth;
    if (old_depth) DdSurfaceRelease(old_depth);
    DdSurfaceRelease(old_target);
    return D3D_OK;
}

static HRESULT WINAPI Dev_GetRenderTarget(void *face, LPDIRECTDRAWSURFACE7 *out)
{
    D3dDevice *d = This(face);
    if (!out) return DDERR_INVALIDPARAMS;
    DdSurfaceAddRef(d->target);
    *out = (LPDIRECTDRAWSURFACE7)DdSurfaceFace(d->target, DDS_V7);
    return D3D_OK;
}

static HRESULT WINAPI Dev_Clear(void *face, DWORD count, LPD3DRECT rects, DWORD flags, D3DCOLOR color, D3DVALUE z, DWORD stencil)
{
    D3dDevice *d = This(face);
    DWORD *w;
    if (!(flags & (D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)) ||
        (flags & ~(DWORD)(D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)))
        return DDERR_INVALIDPARAMS;
    if ((count && !rects) || count > 4096) return DDERR_INVALIDPARAMS;
    if ((flags & D3DCLEAR_ZBUFFER) && !d->depth) return D3DERR_ZBUFFER_NOTPRESENT;
    if ((flags & D3DCLEAR_STENCIL) && !(d->depth && (d->depth->format.flags & DDPF_STENCILBUFFER))) return D3DERR_STENCILBUFFER_NOTPRESENT;
    w = Append(d, NEO98D3D_OP_CLEAR, 5 + count * 4, FALSE);
    if (!w) return DDERR_OUTOFMEMORY;
    w[0] = flags;
    w[1] = color;
    w[2] = FloatBits(z);
    w[3] = stencil;
    /* Clear stays inside the viewport: each rect is cut to it, and a call
       whose rects all lie outside clears nothing. */
    {
        const D3DVIEWPORT7 *v = &d->state.viewport;
        const LONG left = (LONG)v->dwX, top = (LONG)v->dwY, right = left + (LONG)v->dwWidth, bottom = top + (LONG)v->dwHeight;
        DWORD i, kept = 0;
        for (i = 0; i < count; ++i)
        {
            LONG r[4] = {rects[i].x1 > left ? rects[i].x1 : left, rects[i].y1 > top ? rects[i].y1 : top,
                         rects[i].x2 < right ? rects[i].x2 : right, rects[i].y2 < bottom ? rects[i].y2 : bottom};
            if (r[0] >= r[2] || r[1] >= r[3]) continue;
            memcpy(w + 5 + kept * 4, r, 16);
            ++kept;
        }
        w[4] = kept;
        if (count && !kept)
        {
            /* Nothing to clear: the command goes again. */
            d->command_bytes = d->last_command;
            d->last_command = ~0u;
            if (!d->command_bytes) DdDropRef(&d3d_pending);
        }
        else if (kept < count)
        {
            const DWORD spare = (count - kept) * 16;
            ((NEO98D3D_COMMAND *)(d->commands + d->last_command))->bytes -= spare;
            d->command_bytes -= spare;
        }
    }
    return D3D_OK;
}

static D3dBlockLight *BlockLight(D3dBlock *block, DWORD index)
{
    DWORD i;
    for (i = 0; i < block->light_count; ++i)
        if (block->lights[i].index == index) return &block->lights[i];
    if (block->light_count == block->light_capacity)
    {
        const DWORD capacity = block->light_capacity ? block->light_capacity * 2 : 8;
        D3dBlockLight *more = (D3dBlockLight *)realloc(block->lights, capacity * sizeof *more);
        if (!more) return NULL;
        block->lights = more;
        block->light_capacity = capacity;
    }
    memset(&block->lights[block->light_count], 0, sizeof block->lights[0]);
    block->lights[block->light_count].index = index;
    return &block->lights[block->light_count++];
}

static HRESULT WINAPI Dev_SetTransform(void *face, D3DTRANSFORMSTATETYPE type, LPD3DMATRIX matrix)
{
    D3dDevice *d = This(face);
    const int index = MatrixIndex(type);
    if (!matrix) return DDERR_INVALIDPARAMS;
    if (index < 0) return D3D_OK;   /* no such matrix: taken and dropped, as the original does */
    if (d->recording)
    {
        d->recording->state.matrix[index] = *matrix;
        d->recording->matrix_mark[index] = 1;
        return D3D_OK;
    }
    SetMatrixNow(d, index, matrix);
    return D3D_OK;
}

static HRESULT WINAPI Dev_GetTransform(void *face, D3DTRANSFORMSTATETYPE type, LPD3DMATRIX matrix)
{
    D3dDevice *d = This(face);
    const int index = MatrixIndex(type);
    if (!matrix || index < 0) return DDERR_INVALIDPARAMS;
    *matrix = d->state.matrix[index];
    return D3D_OK;
}

/* The matrix goes in front of the one the device holds. */
static HRESULT WINAPI Dev_MultiplyTransform(void *face, D3DTRANSFORMSTATETYPE type, LPD3DMATRIX matrix)
{
    D3dDevice *d = This(face);
    const int index = MatrixIndex(type);
    D3DMATRIX product;
    if (!matrix || index < 0) return DDERR_INVALIDPARAMS;
    if (d->recording)
    {
        D3dMultiply(&product, matrix, d->recording->matrix_mark[index] ? &d->recording->state.matrix[index] : &d->state.matrix[index]);
        d->recording->state.matrix[index] = product;
        d->recording->matrix_mark[index] = 1;
        return D3D_OK;
    }
    D3dMultiply(&product, matrix, &d->state.matrix[index]);
    SetMatrixNow(d, index, &product);
    return D3D_OK;
}

static HRESULT WINAPI Dev_SetViewport(void *face, LPD3DVIEWPORT7 viewport)
{
    D3dDevice *d = This(face);
    if (!viewport) return DDERR_INVALIDPARAMS;
    if (viewport->dwX > d->target->width || viewport->dwWidth > d->target->width - viewport->dwX || viewport->dwY > d->target->height ||
        viewport->dwHeight > d->target->height - viewport->dwY || !(viewport->dvMinZ <= viewport->dvMaxZ) || viewport->dvMinZ < 0.0f ||
        viewport->dvMaxZ > 1.0f)
        return DDERR_INVALIDPARAMS;
    if (d->recording)
    {
        d->recording->state.viewport = *viewport;
        d->recording->viewport_mark = TRUE;
        return D3D_OK;
    }
    d->state.viewport = *viewport;
    EmitViewport(d);
    return D3D_OK;
}

static HRESULT WINAPI Dev_GetViewport(void *face, LPD3DVIEWPORT7 viewport)
{
    if (!viewport) return DDERR_INVALIDPARAMS;
    *viewport = This(face)->state.viewport;
    return D3D_OK;
}

static HRESULT WINAPI Dev_SetMaterial(void *face, LPD3DMATERIAL7 material)
{
    D3dDevice *d = This(face);
    if (!material) return DDERR_INVALIDPARAMS;
    if (d->recording)
    {
        d->recording->state.material = *material;
        d->recording->material_mark = TRUE;
        return D3D_OK;
    }
    d->state.material = *material;
    EmitMaterial(d);
    return D3D_OK;
}

static HRESULT WINAPI Dev_GetMaterial(void *face, LPD3DMATERIAL7 material)
{
    if (!material) return DDERR_INVALIDPARAMS;
    *material = This(face)->state.material;
    return D3D_OK;
}

static HRESULT WINAPI Dev_SetLight(void *face, DWORD index, LPD3DLIGHT7 light)
{
    D3dDevice *d = This(face);
    if (!light) return DDERR_INVALIDPARAMS;
    if (light->dltType < D3DLIGHT_POINT || light->dltType > D3DLIGHT_DIRECTIONAL) return DDERR_INVALIDPARAMS;
    if (light->dltType != D3DLIGHT_DIRECTIONAL && (light->dvRange < 0.0f || light->dvAttenuation0 < 0.0f || light->dvAttenuation1 < 0.0f ||
                                                   light->dvAttenuation2 < 0.0f))
        return DDERR_INVALIDPARAMS;
    if (d->recording)
    {
        D3dBlockLight *entry = BlockLight(d->recording, index);
        if (!entry) return DDERR_OUTOFMEMORY;
        entry->data = *light;
        entry->has_data = TRUE;
        return D3D_OK;
    }
    return SetLightNow(d, index, light);
}

static HRESULT WINAPI Dev_GetLight(void *face, DWORD index, LPD3DLIGHT7 out)
{
    const D3dLight *light = FindLight(This(face), index, FALSE);
    if (!out || !light) return DDERR_INVALIDPARAMS;
    *out = light->data;
    return D3D_OK;
}

static HRESULT WINAPI Dev_LightEnable(void *face, DWORD index, BOOL enable)
{
    D3dDevice *d = This(face);
    if (d->recording)
    {
        D3dBlockLight *entry = BlockLight(d->recording, index);
        if (!entry) return DDERR_OUTOFMEMORY;
        entry->enabled = enable != 0;
        entry->has_enable = TRUE;
        return D3D_OK;
    }
    return EnableLightNow(d, index, enable != 0);
}

static HRESULT WINAPI Dev_GetLightEnable(void *face, DWORD index, BOOL *enabled)
{
    const D3dLight *light = FindLight(This(face), index, FALSE);
    if (!enabled || !light) return DDERR_INVALIDPARAMS;
    *enabled = light->enabled;
    return D3D_OK;
}

static HRESULT WINAPI Dev_SetRenderState(void *face, D3DRENDERSTATETYPE state, DWORD value)
{
    D3dDevice *d = This(face);
    /* A number that is no render state is taken and dropped, as the original does. */
    if ((DWORD)state == 0 || (DWORD)state >= D3D_RENDERSTATES) return D3D_OK;
    if (Retired(state)) return DDERR_INVALIDPARAMS;
    if (d->recording)
    {
        d->recording->state.render[state] = value;
        d->recording->render_mark[state] = 1;
        return D3D_OK;
    }
    SetRenderNow(d, state, value);
    return D3D_OK;
}

static HRESULT WINAPI Dev_GetRenderState(void *face, D3DRENDERSTATETYPE state, LPDWORD value)
{
    D3dDevice *d = This(face);
    if (!value || (DWORD)state == 0 || (DWORD)state >= D3D_RENDERSTATES || Retired(state)) return DDERR_INVALIDPARAMS;
    *value = d->state.render[state];
    return D3D_OK;
}

static HRESULT WINAPI Dev_GetTextureStageState(void *face, DWORD stage, D3DTEXTURESTAGESTATETYPE state, LPDWORD value)
{
    D3dDevice *d = This(face);
    if (!value || stage >= D3D_STAGES || (DWORD)state < D3DTSS_COLOROP || (DWORD)state > D3DTSS_TEXTURETRANSFORMFLAGS)
        return DDERR_INVALIDPARAMS;
    *value = d->state.stage[stage][state];
    return D3D_OK;
}

static HRESULT WINAPI Dev_SetTextureStageState(void *face, DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD value)
{
    D3dDevice *d = This(face);
    /* A stage or a state the device does not have is taken and dropped, as
       the original does. */
    if (stage >= D3D_STAGES || (DWORD)state < D3DTSS_COLOROP || (DWORD)state > D3DTSS_TEXTURETRANSFORMFLAGS) return D3D_OK;
    if (d->recording)
    {
        d->recording->state.stage[stage][state] = value;
        d->recording->stage_mark[stage][state] = 1;
        if (state == D3DTSS_ADDRESS)
        {
            d->recording->state.stage[stage][D3DTSS_ADDRESSU] = d->recording->state.stage[stage][D3DTSS_ADDRESSV] = value;
            d->recording->stage_mark[stage][D3DTSS_ADDRESSU] = d->recording->stage_mark[stage][D3DTSS_ADDRESSV] = 1;
        }
        return D3D_OK;
    }
    SetStageNow(d, stage, state, value);
    return D3D_OK;
}

static HRESULT WINAPI Dev_GetTexture(void *face, DWORD stage, LPDIRECTDRAWSURFACE7 *out)
{
    D3dDevice *d = This(face);
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (stage >= D3D_STAGES) return DDERR_INVALIDPARAMS;
    if (!d->state.texture[stage]) return D3D_OK;
    DdSurfaceAddRef(d->state.texture[stage]);
    *out = (LPDIRECTDRAWSURFACE7)DdSurfaceFace(d->state.texture[stage], DDS_V7);
    return D3D_OK;
}

static HRESULT WINAPI Dev_SetTexture(void *face, DWORD stage, LPDIRECTDRAWSURFACE7 surface)
{
    D3dDevice *d = This(face);
    DdSurface *texture = DdSurfaceOf(surface);
    if (stage >= D3D_STAGES) return D3D_OK;
    if (texture && texture->dead) return DDERR_INVALIDOBJECT;
    if (d->recording)
    {
        D3dBlock *block = d->recording;
        if (texture) DdSurfaceAddRef(texture);
        if (block->state.texture[stage]) DdSurfaceRelease(block->state.texture[stage]);
        block->state.texture[stage] = texture;
        block->texture_mark[stage] = 1;
        return D3D_OK;
    }
    SetTextureNow(d, stage, texture);
    return D3D_OK;
}

static HRESULT WINAPI Dev_SetClipPlane(void *face, DWORD index, D3DVALUE *plane)
{
    D3dDevice *d = This(face);
    if (!plane || index >= D3D_CLIPPLANES) return DDERR_INVALIDPARAMS;
    if (d->recording)
    {
        memcpy(d->recording->state.clip[index], plane, 16);
        d->recording->clip_mark[index] = 1;
        return D3D_OK;
    }
    memcpy(d->state.clip[index], plane, 16);
    EmitClipPlane(d, index);
    return D3D_OK;
}

static HRESULT WINAPI Dev_GetClipPlane(void *face, DWORD index, D3DVALUE *plane)
{
    if (!plane || index >= D3D_CLIPPLANES) return DDERR_INVALIDPARAMS;
    memcpy(plane, This(face)->state.clip[index], 16);
    return D3D_OK;
}

static HRESULT WINAPI Dev_SetClipStatus(void *face, LPD3DCLIPSTATUS status)
{
    D3DCLIPSTATUS *kept = &This(face)->clip_status;
    if (!status || (status->dwFlags & ~(D3DCLIPSTATUS_STATUS | D3DCLIPSTATUS_EXTENTS2))) return DDERR_INVALIDPARAMS;
    if (status->dwFlags & D3DCLIPSTATUS_STATUS) kept->dwStatus = status->dwStatus;
    if (status->dwFlags & D3DCLIPSTATUS_EXTENTS2)
    {
        kept->minx = status->minx; kept->maxx = status->maxx;
        kept->miny = status->miny; kept->maxy = status->maxy;
    }
    return D3D_OK;
}

static HRESULT WINAPI Dev_GetClipStatus(void *face, LPD3DCLIPSTATUS status)
{
    const D3DCLIPSTATUS *kept = &This(face)->clip_status;
    if (!status) return DDERR_INVALIDPARAMS;
    status->dwFlags = D3DCLIPSTATUS_EXTENTS2;
    status->dwStatus = kept->dwStatus;
    status->minx = kept->minx; status->maxx = kept->maxx;
    status->miny = kept->miny; status->maxy = kept->maxy;
    /* D3D7 does not write minz/maxz in this 2D contract. */
    return D3D_OK;
}

/* ------------------------------------------------------------- state blocks */
static D3dBlock *BlockOf(D3dDevice *d, DWORD handle)
{
    if (!handle || handle > d->block_count) return NULL;
    return d->blocks[handle - 1];
}

static HRESULT KeepBlock(D3dDevice *d, D3dBlock *block, DWORD *handle)
{
    DWORD i;
    D3dBlock **more;
    for (i = 0; i < d->block_count; ++i)
        if (!d->blocks[i])
        {
            d->blocks[i] = block;
            *handle = i + 1;
            return D3D_OK;
        }
    more = (D3dBlock **)realloc(d->blocks, (d->block_count + 1) * sizeof *more);
    if (!more) return DDERR_OUTOFMEMORY;
    d->blocks = more;
    d->blocks[d->block_count++] = block;
    *handle = d->block_count;
    return D3D_OK;
}

static HRESULT WINAPI Dev_BeginStateBlock(void *face)
{
    D3dDevice *d = This(face);
    if (d->recording) return D3DERR_INBEGINSTATEBLOCK;
    d->recording = (D3dBlock *)DdAllocate(sizeof *d->recording);
    if (!d->recording) return DDERR_OUTOFMEMORY;
    d->recording->used = TRUE;
    return D3D_OK;
}

static HRESULT WINAPI Dev_EndStateBlock(void *face, LPDWORD handle)
{
    D3dDevice *d = This(face);
    HRESULT result;
    if (!handle) return DDERR_INVALIDPARAMS;
    if (!d->recording) return D3DERR_NOTINBEGINSTATEBLOCK;
    result = KeepBlock(d, d->recording, handle);
    if (FAILED(result)) FreeBlock(d->recording);
    d->recording = NULL;
    return result;
}

static HRESULT WINAPI Dev_ApplyStateBlock(void *face, DWORD handle)
{
    D3dDevice *d = This(face);
    D3dBlock *block = BlockOf(d, handle);
    DWORD i, k;
    if (d->recording) return D3DERR_INBEGINSTATEBLOCK;
    if (!block) return D3DERR_INVALIDSTATEBLOCK;
    for (i = 1; i < D3D_RENDERSTATES; ++i)
        if (block->render_mark[i]) SetRenderNow(d, i, block->state.render[i]);
    for (i = 0; i < D3D_STAGES; ++i)
    {
        for (k = D3DTSS_COLOROP; k <= D3DTSS_TEXTURETRANSFORMFLAGS; ++k)
            if (block->stage_mark[i][k])
            {
                /* ADDRESS has its own query value. Restore it without
                   overwriting separately captured ADDRESSU/ADDRESSV. */
                if (k == D3DTSS_ADDRESS) d->state.stage[i][k] = block->state.stage[i][k];
                else SetStageNow(d, i, k, block->state.stage[i][k]);
            }
        if (block->texture_mark[i]) SetTextureNow(d, i, block->state.texture[i]);
    }
    for (i = 0; i < D3D_MATRICES; ++i)
        if (block->matrix_mark[i]) SetMatrixNow(d, (int)i, &block->state.matrix[i]);
    if (block->material_mark)
    {
        d->state.material = block->state.material;
        EmitMaterial(d);
    }
    if (block->viewport_mark)
    {
        d->state.viewport = block->state.viewport;
        EmitViewport(d);
    }
    for (i = 0; i < D3D_CLIPPLANES; ++i)
        if (block->clip_mark[i])
        {
            memcpy(d->state.clip[i], block->state.clip[i], 16);
            EmitClipPlane(d, i);
        }
    for (i = 0; i < block->light_count; ++i)
    {
        const D3dBlockLight *light = &block->lights[i];
        if (light->has_data) SetLightNow(d, light->index, &light->data);
        if (light->has_enable) EnableLightNow(d, light->index, light->enabled);
    }
    return D3D_OK;
}

/* The state of the device, where the block holds some, into the block. */
static void Capture(D3dDevice *d, D3dBlock *block)
{
    DWORD i, k;
    for (i = 1; i < D3D_RENDERSTATES; ++i)
        if (block->render_mark[i]) block->state.render[i] = d->state.render[i];
    for (i = 0; i < D3D_STAGES; ++i)
    {
        for (k = D3DTSS_COLOROP; k <= D3DTSS_TEXTURETRANSFORMFLAGS; ++k)
            if (block->stage_mark[i][k]) block->state.stage[i][k] = d->state.stage[i][k];
        if (block->texture_mark[i])
        {
            if (d->state.texture[i]) DdSurfaceAddRef(d->state.texture[i]);
            if (block->state.texture[i]) DdSurfaceRelease(block->state.texture[i]);
            block->state.texture[i] = d->state.texture[i];
        }
    }
    for (i = 0; i < D3D_MATRICES; ++i)
        if (block->matrix_mark[i]) block->state.matrix[i] = d->state.matrix[i];
    if (block->material_mark) block->state.material = d->state.material;
    if (block->viewport_mark) block->state.viewport = d->state.viewport;
    for (i = 0; i < D3D_CLIPPLANES; ++i)
        if (block->clip_mark[i]) memcpy(block->state.clip[i], d->state.clip[i], 16);
    for (i = 0; i < block->light_count; ++i)
    {
        D3dBlockLight *entry = &block->lights[i];
        const D3dLight *light = FindLight(d, entry->index, FALSE);
        if (!light) continue;
        if (entry->has_data) entry->data = light->data;
        if (entry->has_enable) entry->enabled = light->enabled;
    }
}

static HRESULT WINAPI Dev_CaptureStateBlock(void *face, DWORD handle)
{
    D3dDevice *d = This(face);
    D3dBlock *block = BlockOf(d, handle);
    if (d->recording) return D3DERR_INBEGINSTATEBLOCK;
    if (!block) return D3DERR_INVALIDSTATEBLOCK;
    Capture(d, block);
    return D3D_OK;
}

static HRESULT WINAPI Dev_DeleteStateBlock(void *face, DWORD handle)
{
    D3dDevice *d = This(face);
    D3dBlock *block = BlockOf(d, handle);
    if (d->recording) return D3DERR_INBEGINSTATEBLOCK;
    if (!block) return D3DERR_INVALIDSTATEBLOCK;
    d->blocks[handle - 1] = NULL;
    FreeBlock(block);
    return D3D_OK;
}

/* The states of the predefined blocks: D3DSBT_PIXELSTATE is what decides the
   colour of a pixel, D3DSBT_VERTEXSTATE what the vertex pipeline reads. */
static const BYTE pixel_render_states[] = {
    D3DRENDERSTATE_ANTIALIAS,
    D3DRENDERSTATE_ZENABLE, D3DRENDERSTATE_FILLMODE, D3DRENDERSTATE_SHADEMODE, D3DRENDERSTATE_LINEPATTERN, D3DRENDERSTATE_ZWRITEENABLE,
    D3DRENDERSTATE_ALPHATESTENABLE, D3DRENDERSTATE_LASTPIXEL, D3DRENDERSTATE_SRCBLEND, D3DRENDERSTATE_DESTBLEND, D3DRENDERSTATE_ZFUNC,
    D3DRENDERSTATE_ALPHAREF, D3DRENDERSTATE_ALPHAFUNC, D3DRENDERSTATE_DITHERENABLE, D3DRENDERSTATE_STIPPLEDALPHA, D3DRENDERSTATE_FOGSTART,
    D3DRENDERSTATE_FOGEND, D3DRENDERSTATE_FOGDENSITY, D3DRENDERSTATE_EDGEANTIALIAS, D3DRENDERSTATE_ALPHABLENDENABLE, D3DRENDERSTATE_ZBIAS,
    D3DRENDERSTATE_STENCILENABLE, D3DRENDERSTATE_STENCILFAIL, D3DRENDERSTATE_STENCILZFAIL, D3DRENDERSTATE_STENCILPASS,
    D3DRENDERSTATE_STENCILFUNC, D3DRENDERSTATE_STENCILREF, D3DRENDERSTATE_STENCILMASK, D3DRENDERSTATE_STENCILWRITEMASK,
    D3DRENDERSTATE_TEXTUREFACTOR, D3DRENDERSTATE_WRAP0, D3DRENDERSTATE_WRAP1, D3DRENDERSTATE_WRAP2, D3DRENDERSTATE_WRAP3,
    D3DRENDERSTATE_WRAP4, D3DRENDERSTATE_WRAP5, D3DRENDERSTATE_WRAP6, D3DRENDERSTATE_WRAP7, D3DRENDERSTATE_TEXTUREPERSPECTIVE,
    D3DRENDERSTATE_COLORKEYENABLE, D3DRENDERSTATE_COLORKEYBLENDENABLE};
static const BYTE vertex_render_states[] = {
    D3DRENDERSTATE_EXTENTS,
    D3DRENDERSTATE_SHADEMODE, D3DRENDERSTATE_SPECULARENABLE, D3DRENDERSTATE_CULLMODE, D3DRENDERSTATE_FOGENABLE, D3DRENDERSTATE_FOGCOLOR,
    D3DRENDERSTATE_FOGTABLEMODE, D3DRENDERSTATE_FOGSTART, D3DRENDERSTATE_FOGEND, D3DRENDERSTATE_FOGDENSITY, D3DRENDERSTATE_RANGEFOGENABLE,
    D3DRENDERSTATE_AMBIENT, D3DRENDERSTATE_COLORVERTEX, D3DRENDERSTATE_FOGVERTEXMODE, D3DRENDERSTATE_CLIPPING, D3DRENDERSTATE_LIGHTING,
    D3DRENDERSTATE_LOCALVIEWER, D3DRENDERSTATE_EMISSIVEMATERIALSOURCE, D3DRENDERSTATE_AMBIENTMATERIALSOURCE,
    D3DRENDERSTATE_DIFFUSEMATERIALSOURCE, D3DRENDERSTATE_SPECULARMATERIALSOURCE, D3DRENDERSTATE_VERTEXBLEND,
    D3DRENDERSTATE_CLIPPLANEENABLE, D3DRENDERSTATE_NORMALIZENORMALS};

static HRESULT WINAPI Dev_CreateStateBlock(void *face, D3DSTATEBLOCKTYPE type, LPDWORD handle)
{
    D3dDevice *d = This(face);
    D3dBlock *block;
    HRESULT result;
    DWORD i, k;
    if (!handle) return DDERR_INVALIDPARAMS;
    if (d->recording) return D3DERR_INBEGINSTATEBLOCK;
    if (type != D3DSBT_ALL && type != D3DSBT_PIXELSTATE && type != D3DSBT_VERTEXSTATE) return DDERR_INVALIDPARAMS;
    block = (D3dBlock *)DdAllocate(sizeof *block);
    if (!block) return DDERR_OUTOFMEMORY;
    block->used = TRUE;
    if (type == D3DSBT_ALL)
    {
        for (i = 1; i < D3D_RENDERSTATES; ++i) block->render_mark[i] = 1;
        block->render_mark[D3DRENDERSTATE_TEXTUREHANDLE] = block->render_mark[D3DRENDERSTATE_TEXTUREMAPBLEND] = 0;
        memset(block->matrix_mark, 1, sizeof block->matrix_mark);
        memset(block->clip_mark, 1, sizeof block->clip_mark);
        block->material_mark = block->viewport_mark = TRUE;
    }
    else if (type == D3DSBT_PIXELSTATE)
        for (i = 0; i < sizeof pixel_render_states; ++i) block->render_mark[pixel_render_states[i]] = 1;
    else
        for (i = 0; i < sizeof vertex_render_states; ++i) block->render_mark[vertex_render_states[i]] = 1;
    for (i = 0; i < D3D_STAGES; ++i)
    {
        if (type == D3DSBT_VERTEXSTATE)
            block->stage_mark[i][D3DTSS_TEXCOORDINDEX] = block->stage_mark[i][D3DTSS_TEXTURETRANSFORMFLAGS] = 1;
        else
        {
            for (k = D3DTSS_COLOROP; k <= D3DTSS_TEXTURETRANSFORMFLAGS; ++k) block->stage_mark[i][k] = 1;
            /* PIXELSTATE captures stage values, not the bound surfaces. */
            block->texture_mark[i] = type == D3DSBT_ALL;
        }
    }
    if (type != D3DSBT_PIXELSTATE)
        for (i = 0; i < d->light_count; ++i)
        {
            D3dBlockLight *entry = BlockLight(block, d->lights[i].index);
            if (!entry)
            {
                FreeBlock(block);
                return DDERR_OUTOFMEMORY;
            }
            /* VERTEXSTATE captures LightEnable, but leaves GetLight's
               parameters unchanged. The all-state block captures both. */
            entry->has_data = type == D3DSBT_ALL;
            entry->has_enable = TRUE;
        }
    Capture(d, block);
    result = KeepBlock(d, block, handle);
    if (FAILED(result)) FreeBlock(block);
    return result;
}

/* -------------------------------------------------------------------- draws */
/* One draw into the batch. The vertices come packed in the layout of the
   format, or strided; they are copied now, because the program may change
   its memory before the batch runs. */
static HRESULT Submit(D3dDevice *d, DWORD primitive, DWORD fvf, const BYTE *packed, const D3DDRAWPRIMITIVESTRIDEDDATA *strided,
                      DWORD vertex_count, const WORD *indices, DWORD index_count)
{
    D3dLayout layout;
    NEO98D3D_DRAW draw;
    DWORD vertex_bytes, index_bytes, *w;
    BYTE *room;
    BYTE *lit = NULL;
    D3DDRAWPRIMITIVESTRIDEDDATA data;
    BOOL software_light = FALSE;
    if (!D3dLayoutOf(fvf, &layout)) return D3DERR_INVALIDVERTEXFORMAT;
    if (!packed && !(strided && strided->position.lpvData)) return DDERR_INVALIDPARAMS;
    /* Strided vertices are model space; the original refuses transformed ones. */
    if (!packed && layout.transformed) return D3DERR_INVALIDVERTEXTYPE;
    if (vertex_count > D3DMAXNUMVERTICES) return D3DERR_TOOMANYVERTICES;
    if (!vertex_count) return DDERR_INVALIDPARAMS;
    if (indices ? !index_count : FALSE) return DDERR_INVALIDPARAMS;
    if (d->recording) return D3DERR_INBEGINSTATEBLOCK;
    /* The original draws outside of a scene as well, and answers D3D_OK to a
       primitive type it does not have without drawing (measured against
       D3DIM700.DLL, tools/test_arm64_d3d.py). */
    if (primitive < D3DPT_POINTLIST || primitive > D3DPT_TRIANGLEFAN) return D3D_OK;
    if (!IsEqualGUID(&d->guid, &d3d_iid_tnlhal) && !layout.transformed && layout.normal >= 0 && d->state.render[D3DRENDERSTATE_LIGHTING])
    {
        DWORD enabled = 0;
        for (DWORD i = 0; i < d->light_count; ++i)
            if (d->lights[i].enabled) { ++enabled; if (d->lights[i].slot < 0) software_light = TRUE; }
        // The fixed hardware vertex shader has eight lights. The original
        // HAL performs lighting in software and has no active-light limit.
        software_light = software_light || enabled > 8;
        if (software_light)
        {
            if (packed) D3dStridedOf(packed, fvf, &layout, &data); else data = *strided;
            lit = (BYTE *)malloc(vertex_count * 24);
            if (!lit) return DDERR_OUTOFMEMORY;
            HRESULT result = D3dProcessVertices(d, D3DVOP_TRANSFORM | D3DVOP_LIGHT, lit,
                D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR, &data, fvf, vertex_count, 0);
            if (FAILED(result)) { free(lit); return result; }
            // Reuse only the colours. Positions, normals, texture generation,
            // clipping and fog still go through the existing vertex pipeline.
            data.diffuse.lpvData = lit + 16; data.diffuse.dwStride = 24;
            data.specular.lpvData = lit + 20; data.specular.dwStride = 24;
            fvf |= D3DFVF_DIFFUSE | D3DFVF_SPECULAR;
            D3dLayoutOf(fvf, &layout);
            packed = NULL; strided = &data;
        }
    }
    vertex_bytes = vertex_count * layout.stride;
    index_bytes = indices ? index_count * 2 : 0;
    room = Pool(d, ((vertex_bytes + 3) & ~3u) + index_bytes);
    if (!room) { free(lit); return DDERR_OUTOFMEMORY; }
    if (packed) memcpy(room, packed, vertex_bytes);
    else D3dPack(room, strided, 0, &layout, vertex_count);
    free(lit);
    if (indices) memcpy(room + ((vertex_bytes + 3) & ~3u), indices, index_bytes);
    memset(&draw, 0, sizeof draw);
    draw.primitive = primitive;
    draw.fvf = fvf;
    draw.stride = layout.stride;
    draw.vertices = (DXU32)(ULONG_PTR)room;
    draw.vertexCount = vertex_count;
    draw.indices = indices ? (DXU32)(ULONG_PTR)(room + ((vertex_bytes + 3) & ~3u)) : 0;
    draw.indexCount = indices ? index_count : 0;
    if (software_light) EmitRender(d, D3DRENDERSTATE_LIGHTING, FALSE);
    w = Append(d, NEO98D3D_OP_DRAW, sizeof draw / 4, FALSE);
    if (!w) { if (software_light) EmitRender(d, D3DRENDERSTATE_LIGHTING, d->state.render[D3DRENDERSTATE_LIGHTING]); return DDERR_OUTOFMEMORY; }
    memcpy(w, &draw, sizeof draw);
    if (software_light) EmitRender(d, D3DRENDERSTATE_LIGHTING, d->state.render[D3DRENDERSTATE_LIGHTING]);
    return D3D_OK;
}

static HRESULT WINAPI Dev_DrawPrimitive(void *face, D3DPRIMITIVETYPE primitive, DWORD fvf, LPVOID vertices, DWORD count, DWORD flags)
{
    (void)flags;
    if (!vertices) return DDERR_INVALIDPARAMS;
    return Submit(This(face), primitive, fvf, (const BYTE *)vertices, NULL, count, NULL, 0);
}

static HRESULT WINAPI Dev_DrawIndexedPrimitive(void *face, D3DPRIMITIVETYPE primitive, DWORD fvf, LPVOID vertices, DWORD count,
                                               LPWORD indices, DWORD index_count, DWORD flags)
{
    (void)flags;
    if (!vertices || !indices) return DDERR_INVALIDPARAMS;
    return Submit(This(face), primitive, fvf, (const BYTE *)vertices, NULL, count, indices, index_count);
}

static HRESULT WINAPI Dev_DrawPrimitiveStrided(void *face, D3DPRIMITIVETYPE primitive, DWORD fvf, LPD3DDRAWPRIMITIVESTRIDEDDATA data,
                                               DWORD count, DWORD flags)
{
    (void)flags;
    if (!data) return DDERR_INVALIDPARAMS;
    return Submit(This(face), primitive, fvf, NULL, data, count, NULL, 0);
}

static HRESULT WINAPI Dev_DrawIndexedPrimitiveStrided(void *face, D3DPRIMITIVETYPE primitive, DWORD fvf,
                                                      LPD3DDRAWPRIMITIVESTRIDEDDATA data, DWORD count, LPWORD indices, DWORD index_count,
                                                      DWORD flags)
{
    (void)flags;
    if (!data || !indices) return DDERR_INVALIDPARAMS;
    return Submit(This(face), primitive, fvf, NULL, data, count, indices, index_count);
}

static HRESULT WINAPI Dev_DrawPrimitiveVB(void *face, D3DPRIMITIVETYPE primitive, LPDIRECT3DVERTEXBUFFER7 buffer, DWORD start, DWORD count,
                                          DWORD flags)
{
    D3dVertexBuffer *vb = D3dVertexBufferOf(buffer);
    (void)flags;
    if (!vb) return DDERR_INVALIDPARAMS;
    if (vb->locks > 0) return D3DERR_VERTEXBUFFERLOCKED;
    if (start > vb->count || count > vb->count - start) return DDERR_INVALIDPARAMS;
    return Submit(This(face), primitive, vb->fvf, vb->memory + (size_t)start * vb->stride, NULL, count, NULL, 0);
}

static HRESULT WINAPI Dev_DrawIndexedPrimitiveVB(void *face, D3DPRIMITIVETYPE primitive, LPDIRECT3DVERTEXBUFFER7 buffer, DWORD start,
                                                 DWORD count, LPWORD indices, DWORD index_count, DWORD flags)
{
    D3dVertexBuffer *vb = D3dVertexBufferOf(buffer);
    (void)flags;
    if (!vb || !indices) return DDERR_INVALIDPARAMS;
    if (vb->locks > 0) return D3DERR_VERTEXBUFFERLOCKED;
    if (start > vb->count || count > vb->count - start) return DDERR_INVALIDPARAMS;
    return Submit(This(face), primitive, vb->fvf, vb->memory + (size_t)start * vb->stride, NULL, count, indices, index_count);
}

static HRESULT WINAPI Dev_ComputeSphereVisibility(void *face, LPD3DVECTOR centers, LPD3DVALUE radii, DWORD count, DWORD flags,
                                                  LPDWORD results)
{
    if (!centers || !radii || !results || flags) return DDERR_INVALIDPARAMS;
    return D3dSphereVisibility(This(face), centers, radii, count, results);
}

/* --------------------------------------------------------------- the rest */
static HRESULT WINAPI Dev_ValidateDevice(void *face, LPDWORD passes)
{
    /* The rasterizer blends every stage it has in one pass. */
    (void)face;
    if (!passes) return DDERR_INVALIDPARAMS;
    *passes = 1;
    return D3D_OK;
}

static HRESULT WINAPI Dev_PreLoad(void *face, LPDIRECTDRAWSURFACE7 surface)
{
    const DdSurface *texture = DdSurfaceOf(surface);
    (void)face;
    if (!texture || texture->dead || !(texture->caps & DDSCAPS_TEXTURE)) return DDERR_INVALIDPARAMS;
    return D3D_OK;
}

static DdSurface *NextLevel(DdSurface *s)
{
    DdAttachment *a;
    for (a = s->attached; a; a = a->next)
        if ((a->surface->caps & DDSCAPS_MIPMAP) && (a->surface->caps2 & DDSCAPS2_MIPMAPSUBLEVEL)) return a->surface;
    return NULL;
}

/* Load: a rect of the source texture to a point of the destination, level by
   level down both chains; a source with more levels than the destination
   starts at the level of the destination's size. The colour key and the
   palette of the source go along. */
static HRESULT LoadLevels(DdSurface *dest, LPPOINT dest_point, DdSurface *source, LPRECT source_rect)
{
    POINT point = {0, 0};
    RECT rect;
    HRESULT result = D3D_OK;
    if (!dest || !source || dest->dead || source->dead || dest == source) return DDERR_INVALIDPARAMS;
    if (!source_rect && !dest_point)
        while (source && (source->width > dest->width || source->height > dest->height)) source = NextLevel(source);
    if (!source) return DDERR_INVALIDPARAMS;
    SetRect(&rect, 0, 0, (int)source->width, (int)source->height);
    if (source_rect) rect = *source_rect;
    if (dest_point) point = *dest_point;
    if (rect.left < 0 || rect.top < 0 || rect.right > (LONG)source->width || rect.bottom > (LONG)source->height || rect.left >= rect.right ||
        rect.top >= rect.bottom || point.x < 0 || point.y < 0 || point.x + (rect.right - rect.left) > (LONG)dest->width ||
        point.y + (rect.bottom - rect.top) > (LONG)dest->height)
        return DDERR_INVALIDPARAMS;
    if (source->palette && (dest->format.flags & DDPF_PALETTEINDEXED8))
        IDirectDrawSurface7_SetPalette((LPDIRECTDRAWSURFACE7)DdSurfaceFace(dest, DDS_V7), (LPDIRECTDRAWPALETTE)source->palette);
    if (source->key_flags & DDSD_CKSRCBLT)
        IDirectDrawSurface7_SetColorKey((LPDIRECTDRAWSURFACE7)DdSurfaceFace(dest, DDS_V7), DDCKEY_SRCBLT, &source->key_src_blt);
    while (dest && source)
    {
        result = IDirectDrawSurface7_BltFast((LPDIRECTDRAWSURFACE7)DdSurfaceFace(dest, DDS_V7), (DWORD)point.x, (DWORD)point.y,
                                             (LPDIRECTDRAWSURFACE7)DdSurfaceFace(source, DDS_V7), &rect, DDBLTFAST_WAIT);
        if (FAILED(result)) return result;
        dest = NextLevel(dest);
        source = NextLevel(source);
        point.x /= 2;
        point.y /= 2;
        rect.left /= 2;
        rect.top /= 2;
        rect.right = (rect.right + 1) / 2;
        rect.bottom = (rect.bottom + 1) / 2;
        if (rect.right <= rect.left) rect.right = rect.left + 1;
        if (rect.bottom <= rect.top) rect.bottom = rect.top + 1;
    }
    return D3D_OK;
}

static DdSurface *CubeFace(DdSurface *surface, DWORD bit)
{
    DdAttachment *a;
    if (surface->caps2 & bit) return surface;
    for (a = surface->attached; a; a = a->next)
        if ((a->surface->caps2 & bit) && !(a->surface->caps2 & DDSCAPS2_MIPMAPSUBLEVEL)) return a->surface;
    return NULL;
}

static HRESULT WINAPI Dev_Load(void *face, LPDIRECTDRAWSURFACE7 dest_face, LPPOINT dest_point, LPDIRECTDRAWSURFACE7 source_face,
                               LPRECT source_rect, DWORD flags)
{
    DdSurface *dest = DdSurfaceOf(dest_face), *source = DdSurfaceOf(source_face);
    DWORD i;
    (void)face;
    if (!dest || !source || dest->dead || source->dead || dest == source) return DDERR_INVALIDPARAMS;
    if ((dest->caps2 ^ source->caps2) & DDSCAPS2_CUBEMAP) return DDERR_INVALIDPARAMS;
    if (!(dest->caps2 & DDSCAPS2_CUBEMAP)) return LoadLevels(dest, dest_point, source, source_rect);
    /* The original selects cube faces from flags. Zero selects none; each
       selected face carries its own mip chain through the usual copy. */
    for (i = 0; i < 6; ++i)
    {
        const DWORD bit = DDSCAPS2_CUBEMAP_POSITIVEX << i;
        HRESULT result;
        if (!(flags & bit)) continue;
        result = LoadLevels(CubeFace(dest, bit), dest_point, CubeFace(source, bit), source_rect);
        if (FAILED(result)) return result;
    }
    return D3D_OK;
}

/* GetInfo answers counters of the debug runtime; a retail runtime has none
   and says so with S_FALSE. */
static HRESULT WINAPI Dev_GetInfo(void *face, DWORD id, LPVOID info, DWORD size)
{
    (void)face;
    (void)id;
    if (!info || !size) return DDERR_INVALIDPARAMS;
    memset(info, 0, size);
    return S_FALSE;
}

static const struct IDirect3DDevice7Vtbl device_table = {
    .QueryInterface = (void *)Dev_QueryInterface, .AddRef = (void *)Dev_AddRef, .Release = (void *)Dev_Release,
    .GetCaps = (void *)Dev_GetCaps, .EnumTextureFormats = (void *)Dev_EnumTextureFormats, .BeginScene = (void *)Dev_BeginScene,
    .EndScene = (void *)Dev_EndScene, .GetDirect3D = (void *)Dev_GetDirect3D, .SetRenderTarget = (void *)Dev_SetRenderTarget,
    .GetRenderTarget = (void *)Dev_GetRenderTarget, .Clear = (void *)Dev_Clear, .SetTransform = (void *)Dev_SetTransform,
    .GetTransform = (void *)Dev_GetTransform, .SetViewport = (void *)Dev_SetViewport, .MultiplyTransform = (void *)Dev_MultiplyTransform,
    .GetViewport = (void *)Dev_GetViewport, .SetMaterial = (void *)Dev_SetMaterial, .GetMaterial = (void *)Dev_GetMaterial,
    .SetLight = (void *)Dev_SetLight, .GetLight = (void *)Dev_GetLight, .SetRenderState = (void *)Dev_SetRenderState,
    .GetRenderState = (void *)Dev_GetRenderState, .BeginStateBlock = (void *)Dev_BeginStateBlock,
    .EndStateBlock = (void *)Dev_EndStateBlock, .PreLoad = (void *)Dev_PreLoad, .DrawPrimitive = (void *)Dev_DrawPrimitive,
    .DrawIndexedPrimitive = (void *)Dev_DrawIndexedPrimitive, .SetClipStatus = (void *)Dev_SetClipStatus,
    .GetClipStatus = (void *)Dev_GetClipStatus, .DrawPrimitiveStrided = (void *)Dev_DrawPrimitiveStrided,
    .DrawIndexedPrimitiveStrided = (void *)Dev_DrawIndexedPrimitiveStrided, .DrawPrimitiveVB = (void *)Dev_DrawPrimitiveVB,
    .DrawIndexedPrimitiveVB = (void *)Dev_DrawIndexedPrimitiveVB, .ComputeSphereVisibility = (void *)Dev_ComputeSphereVisibility,
    .GetTexture = (void *)Dev_GetTexture, .SetTexture = (void *)Dev_SetTexture, .GetTextureStageState = (void *)Dev_GetTextureStageState,
    .SetTextureStageState = (void *)Dev_SetTextureStageState, .ValidateDevice = (void *)Dev_ValidateDevice,
    .ApplyStateBlock = (void *)Dev_ApplyStateBlock, .CaptureStateBlock = (void *)Dev_CaptureStateBlock,
    .DeleteStateBlock = (void *)Dev_DeleteStateBlock, .CreateStateBlock = (void *)Dev_CreateStateBlock, .Load = (void *)Dev_Load,
    .LightEnable = (void *)Dev_LightEnable, .GetLightEnable = (void *)Dev_GetLightEnable, .SetClipPlane = (void *)Dev_SetClipPlane,
    .GetClipPlane = (void *)Dev_GetClipPlane, .GetInfo = (void *)Dev_GetInfo};
