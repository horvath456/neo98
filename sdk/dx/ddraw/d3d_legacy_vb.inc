/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Device3 vertex buffers keep their own interface and unwrap device/source
   arguments before entering the existing vertex-buffer implementation. */
static LegacyVertexBuffer *LegacyVb(void *face)
{
    return face && ((DdFace *)face)->table == vertexbuffer_table ? (LegacyVertexBuffer *)Base(face) : NULL;
}
static HRESULT WINAPI RootVertexBuffer(void *face, LPD3DVERTEXBUFFERDESC desc, LPDIRECT3DVERTEXBUFFER *out, DWORD flags, IUnknown *outer)
{
    DD_METHOD_LOCK();
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    IDirect3D7 *root = NULL;
    IDirect3DVertexBuffer7 *core = NULL;
    HRESULT hr = D3dObjectFor(((Legacy *)Base(face))->dd, &IID_IDirect3D7, (void **)&root);
    if (FAILED(hr))
        return hr;
    hr = IDirect3D7_CreateVertexBuffer(root, desc, &core, flags);
    IDirect3D7_Release(root);
    if (FAILED(hr))
        return hr;
    LegacyVertexBuffer *b = calloc(1, sizeof *b);
    if (!b)
    {
        IDirect3DVertexBuffer7_Release(core);
        return DDERR_OUTOFMEMORY;
    }
    Init(&b->base, L_VERTEXBUFFER, &vertexbuffer_table, 1);
    b->core = core;
    *out = Face(&b->base, 0);
    return D3D_OK;
}
static HRESULT WINAPI LegacyVbLock(void *face, DWORD flags, void **data, DWORD *size)
{
    DD_METHOD_LOCK();
    return IDirect3DVertexBuffer7_Lock(LegacyVb(face)->core, flags, data, size);
}
static HRESULT WINAPI LegacyVbUnlock(void *face)
{
    DD_METHOD_LOCK();
    return IDirect3DVertexBuffer7_Unlock(LegacyVb(face)->core);
}
static HRESULT WINAPI LegacyVbDesc(void *face, LPD3DVERTEXBUFFERDESC desc)
{
    DD_METHOD_LOCK();
    return IDirect3DVertexBuffer7_GetVertexBufferDesc(LegacyVb(face)->core, desc);
}
static HRESULT WINAPI LegacyVbOptimize(void *face, LPDIRECT3DDEVICE3 device, DWORD flags)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = LegacyDeviceOf(device);
    if (!d)
        return DDERR_INVALIDPARAMS;
    return IDirect3DVertexBuffer7_Optimize(LegacyVb(face)->core, d->core, flags);
}
static HRESULT WINAPI LegacyVbProcess(void *face, DWORD operation, DWORD dest, DWORD count, LPDIRECT3DVERTEXBUFFER source, DWORD index,
                                      LPDIRECT3DDEVICE3 device, DWORD flags)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = LegacyDeviceOf(device);
    LegacyVertexBuffer *s = LegacyVb(source);
    if (!d || !s)
        return DDERR_INVALIDPARAMS;
    return IDirect3DVertexBuffer7_ProcessVertices(LegacyVb(face)->core, operation, dest, count, s->core, index, d->core, flags);
}
static HRESULT WINAPI DeviceDrawVB(void *face, D3DPRIMITIVETYPE type, LPDIRECT3DVERTEXBUFFER buffer, DWORD start, DWORD count, DWORD flags)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    LegacyVertexBuffer *b = LegacyVb(buffer);
    if (!b)
        return DDERR_INVALIDPARAMS;
    HRESULT hr = Ready(d);
    return FAILED(hr) ? hr : IDirect3DDevice7_DrawPrimitiveVB(d->core, type, b->core, start, count, flags);
}
static HRESULT WINAPI DeviceDrawIndexedVB(void *face, D3DPRIMITIVETYPE type, LPDIRECT3DVERTEXBUFFER buffer, WORD *indices, DWORD count,
                                          DWORD flags)
{
    DD_METHOD_LOCK();
    LegacyDevice *d = (LegacyDevice *)Base(face);
    LegacyVertexBuffer *b = LegacyVb(buffer);
    if (!b)
        return DDERR_INVALIDPARAMS;
    HRESULT hr = Ready(d);
    return FAILED(hr) ? hr
                      : IDirect3DDevice7_DrawIndexedPrimitiveVB(d->core, type, b->core, 0, D3dVertexBufferOf(b->core)->count, indices,
                                                                count, flags);
}
