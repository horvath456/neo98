/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* legacy object contracts and rendering through Device1/2/3. */
#define main previous_main
#include "arm64_d3d_fixture.c"
#undef main
static IDirectDraw4 *legacy_draw;
static BOOL rgb_device, first_device;
static D3DRECT full_rect = {.x1 = 0, .y1 = 0, .x2 = 256, .y2 = 256};
static int OpenLegacy(void)
{
    IDirectDraw *first = NULL;
    IDirectDrawSurface4 *render = NULL;
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = Procedure;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "LegacyD3D";
    RegisterClassA(&wc);
    window = CreateWindowExA(0, wc.lpszClassName, "Legacy D3D", WS_POPUP | WS_VISIBLE, 40, 40, 320, 240, NULL, NULL, wc.hInstance, NULL);
    HRESULT hr = DirectDrawCreate(NULL, &first, NULL);
    Hr("DirectDrawCreate", hr);
    if (FAILED(hr))
        return 0;
    hr = IDirectDraw_QueryInterface(first, &IID_IDirectDraw4, (void **)&legacy_draw);
    IDirectDraw_Release(first);
    Hr("DirectDraw4", hr);
    if (FAILED(hr))
        return 0;
    Hr("cooperative", IDirectDraw4_SetCooperativeLevel(legacy_draw, window, DDSCL_NORMAL));
    DDSURFACEDESC2 d = {0};
    d.dwSize = sizeof d;
    d.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    d.dwWidth = d.dwHeight = 256;
    d.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE | (rgb_device ? DDSCAPS_SYSTEMMEMORY : DDSCAPS_VIDEOMEMORY);
    hr = IDirectDraw4_CreateSurface(legacy_draw, &d, &render, NULL);
    Hr("legacy target", hr);
    /* Shoot uses only Lock/Unlock, whose signatures and slots are identical
       on Surface4 and Surface7. No version-7 interface is requested. */
    target = (IDirectDrawSurface7 *)render;
    return SUCCEEDED(hr);
}
static void CheckCenter(const char *name, DWORD color)
{
    Pixel(name, 128, 128);
    if (frame[128 * 640 + 128] != color)
    {
        Say("WRONG %s expected=%06lx\n", name, (unsigned long)color);
        ++wrong;
    }
}
static void Words(const char *name, const void *value, unsigned size)
{
    const DWORD *p = value;
    for (unsigned n = 0; n < size / 4; ++n)
        Say("%s %u=%08lx\n", name, n, (unsigned long)p[n]);
}
static HRESULT CALLBACK LegacyEnumDevice(GUID *guid, char *description, char *name, D3DDEVICEDESC *hw, D3DDEVICEDESC *sw, void *context)
{
    (void)description;
    (void)name;
    (void)sw;
    (void)context;
    if (IsEqualGUID(guid, &IID_IDirect3DHALDevice))
    {
        Say("legacy enum HAL=1\n");
        Words("legacy enum hw", hw, hw->dwSize);
    }
    if (IsEqualGUID(guid, &IID_IDirect3DRGBDevice))
        Say("legacy enum RGB=1\n");
    return D3DENUMRET_OK;
}
static void RunFirst(IDirect3DDevice *device, IDirectDrawSurface *surface, IDirect3D3 *root3)
{
    IDirect3D *root = NULL;
    IDirect3DViewport *viewport = NULL;
    IDirect3DExecuteBuffer *buffer = NULL;
    IDirect3DDevice *again = NULL;
    IUnknown *a = NULL, *b = NULL;
    IDirect3DDevice2 *second = NULL;
    Hr("first query Device2", IDirect3DDevice_QueryInterface(device, &IID_IDirect3DDevice2, (void **)&second));
    if (second)
        IDirect3DDevice2_Release(second);
    Hr("first query again",
       IDirectDrawSurface_QueryInterface(surface, rgb_device ? &IID_IDirect3DRGBDevice : &IID_IDirect3DHALDevice, (void **)&again));
    Say("first device stable=%u\n", again == device);
    if (again)
        IDirect3DDevice_Release(again);
    Hr("first unknown", IDirect3DDevice_QueryInterface(device, &IID_IUnknown, (void **)&a));
    Hr("first surface unknown", IDirectDrawSurface_QueryInterface(surface, &IID_IUnknown, (void **)&b));
    Say("first surface identity=%u\n", a == b);
    IDirect3DDevice *from_identity = NULL;
    Hr("first identity device", IUnknown_QueryInterface(a, &IID_IDirect3DDevice, (void **)&from_identity));
    Say("first identity device same=%u\n", from_identity == device);
    if (from_identity)
        IDirect3DDevice_Release(from_identity);
    if (a)
        IUnknown_Release(a);
    if (b)
        IUnknown_Release(b);
    Hr("first root", IDirect3D3_QueryInterface(root3, &IID_IDirect3D, (void **)&root));
    if (!root)
        return;
    Hr("first viewport", IDirect3D_CreateViewport(root, &viewport, NULL));
    if (!viewport)
    {
        IDirect3D_Release(root);
        return;
    }
    Hr("first attach", IDirect3DDevice_AddViewport(device, viewport));
    D3DVIEWPORT vp = {0};
    vp.dwSize = sizeof vp;
    vp.dwWidth = vp.dwHeight = 256;
    vp.dvScaleX = vp.dvScaleY = 128;
    vp.dvMaxX = vp.dvMaxY = vp.dvMaxZ = 1;
    Hr("first set viewport", IDirect3DViewport_SetViewport(viewport, &vp));
    Hr("first clear", IDirect3DViewport_Clear(viewport, 1, &full_rect, D3DCLEAR_TARGET));
    D3DSTATS stats = {0};
    stats.dwSize = sizeof stats;
    Hr("first stats before", IDirect3DDevice_GetStats(device, &stats));
    Words("first stats before", &stats, sizeof stats);
    D3DEXECUTEBUFFERDESC desc = {0};
    desc.dwSize = sizeof desc;
    desc.dwFlags = D3DDEB_BUFSIZE;
    desc.dwBufferSize = 1024;
    Hr("first buffer", IDirect3DDevice_CreateExecuteBuffer(device, &desc, &buffer, NULL));
    if (buffer)
    {
        Hr("first lock", IDirect3DExecuteBuffer_Lock(buffer, &desc));
        TL vertices[6];
        QuadVertices(vertices, 32, 32, 224, 224, .5f, 0xff0000ff);
        memcpy(desc.lpData, vertices, sizeof vertices);
        BYTE *stream = (BYTE *)desc.lpData + sizeof vertices;
        D3DINSTRUCTION *instruction = (void *)stream;
        *instruction = (D3DINSTRUCTION){D3DOP_STATERENDER, sizeof(D3DSTATE), 3};
        D3DSTATE *state = (void *)(instruction + 1);
        memset(state, 0, 3 * sizeof *state);
        state[0].drstRenderStateType = D3DRENDERSTATE_CULLMODE;
        state[0].dwArg[0] = D3DCULL_NONE;
        state[1].drstRenderStateType = D3DRENDERSTATE_ZENABLE;
        state[2].drstRenderStateType = D3DRENDERSTATE_TEXTUREHANDLE;
        instruction = (void *)(state + 3);
        *instruction = (D3DINSTRUCTION){D3DOP_PROCESSVERTICES, sizeof(D3DPROCESSVERTICES), 1};
        D3DPROCESSVERTICES *process = (void *)(instruction + 1);
        memset(process, 0, sizeof *process);
        process->dwFlags = D3DPROCESSVERTICES_COPY;
        process->dwCount = 6;
        instruction = (void *)(process + 1);
        *instruction = (D3DINSTRUCTION){D3DOP_TRIANGLE, sizeof(D3DTRIANGLE), 2};
        D3DTRIANGLE *tri = (void *)(instruction + 1);
        memset(tri, 0, 2 * sizeof *tri);
        tri[0].v1 = 0;
        tri[0].v2 = 1;
        tri[0].v3 = 2;
        tri[1].v1 = 3;
        tri[1].v2 = 4;
        tri[1].v3 = 5;
        instruction = (void *)(tri + 2);
        *instruction = (D3DINSTRUCTION){D3DOP_EXIT, 0, 0};
        Hr("first unlock", IDirect3DExecuteBuffer_Unlock(buffer));
        D3DEXECUTEDATA data = {0};
        data.dwSize = sizeof data;
        data.dwVertexCount = 6;
        data.dwInstructionOffset = sizeof vertices;
        data.dwInstructionLength = (BYTE *)(instruction + 1) - stream;
        Hr("first data", IDirect3DExecuteBuffer_SetExecuteData(buffer, &data));
        Hr("first begin", IDirect3DDevice_BeginScene(device));
        Hr("first execute", IDirect3DDevice_Execute(device, buffer, viewport, D3DEXECUTE_UNCLIPPED));
        Hr("first end", IDirect3DDevice_EndScene(device));
        Shoot(target, "FIRST");
        CheckCenter("FIRST", 0xff);
        memset(&stats, 0, sizeof stats);
        stats.dwSize = sizeof stats;
        Hr("first stats after", IDirect3DDevice_GetStats(device, &stats));
        Words("first stats after", &stats, sizeof stats);
        IDirect3DExecuteBuffer_Release(buffer);
    }
    Hr("first detach", IDirect3DDevice_DeleteViewport(device, viewport));
    IDirect3DViewport_Release(viewport);
    IDirect3D_Release(root);
}

int main(int argc, char **argv)
{
    IDirect3D3 *old = NULL;
    IDirect3DDevice3 *three = NULL;
    IDirect3DDevice2 *two = NULL;
    IDirect3DDevice *one = NULL;
    IDirectDrawSurface4 *surface = NULL;
    IDirect3DViewport3 *view = NULL;
    IDirect3DMaterial3 *material = NULL;
    IDirect3DLight *light = NULL;
    if (argc < 3)
        return 2;
    report = fopen(argv[1], "w");
    if (!report)
        return 2;
    strcpy(folder, argv[2]);
    rgb_device = argc > 3 && strstr(argv[3], "rgb") != NULL;
    first_device = argc > 3 && strncmp(argv[3], "one", 3) == 0;
    if (OpenLegacy())
    {
        Hr("legacy D3D3", IDirectDraw4_QueryInterface(legacy_draw, &IID_IDirect3D3, (void **)&old));
        if (old)
        {
            IUnknown *root_identity = NULL, *draw_identity = NULL;
            Hr("root identity", IDirect3D3_QueryInterface(old, &IID_IUnknown, (void **)&root_identity));
            Hr("draw identity", IDirectDraw4_QueryInterface(legacy_draw, &IID_IUnknown, (void **)&draw_identity));
            Say("root draw identity=%u\n", root_identity == draw_identity);
            if (root_identity)
                IUnknown_Release(root_identity);
            if (draw_identity)
                IUnknown_Release(draw_identity);
            Hr("legacy enumerate", IDirect3D3_EnumDevices(old, LegacyEnumDevice, NULL));
            D3DFINDDEVICESEARCH search = {0};
            D3DFINDDEVICERESULT found = {0};
            search.dwSize = sizeof search;
            search.dwFlags = D3DFDS_GUID;
            search.guid = IID_IDirect3DHALDevice;
            found.dwSize = sizeof found;
            Hr("legacy find", IDirect3D3_FindDevice(old, &search, &found));
            Say("legacy find HAL=%u\n", IsEqualGUID(&found.guid, &IID_IDirect3DHALDevice));
            Hr("legacy surface4", IDirectDrawSurface7_QueryInterface(target, &IID_IDirectDrawSurface4, (void **)&surface));
            if (!first_device)
            {
                IDirect3DDevice3 *bad = NULL;
                Hr("legacy null class", IDirect3D3_CreateDevice(old, NULL, surface, &bad, NULL));
                if (bad)
                    IDirect3DDevice3_Release(bad);
                bad = NULL;
                Hr("legacy null target", IDirect3D3_CreateDevice(old, &IID_IDirect3DHALDevice, NULL, &bad, NULL));
                if (bad)
                    IDirect3DDevice3_Release(bad);
            }
            if (first_device)
            {
                IDirectDrawSurface *old_surface = NULL;
                Hr("fresh Surface1", IDirectDrawSurface4_QueryInterface(surface, &IID_IDirectDrawSurface, (void **)&old_surface));
                if (old_surface)
                {
                    Hr("fresh Device1", IDirectDrawSurface_QueryInterface(
                                            old_surface, rgb_device ? &IID_IDirect3DRGBDevice : &IID_IDirect3DHALDevice, (void **)&one));
                }
                if (one)
                {
                    Hr("fresh Device3", IDirect3DDevice_QueryInterface(one, &IID_IDirect3DDevice3, (void **)&three));
                    RunFirst(one, old_surface, old);
                    D3DMATRIX matrix = identity, copy = {0};
                    D3DMATRIXHANDLE handle = 0;
                    matrix._11 = 7;
                    Hr("first retained matrix create", IDirect3DDevice_CreateMatrix(one, &handle));
                    Hr("first retained matrix set", IDirect3DDevice_SetMatrix(one, handle, &matrix));
                    IDirect3DDevice *prior = one;
                    IDirect3DDevice_Release(one);
                    one = NULL;
                    Hr("first recover", IDirectDrawSurface_QueryInterface(
                                            old_surface, rgb_device ? &IID_IDirect3DRGBDevice : &IID_IDirect3DHALDevice, (void **)&one));
                    Say("first recovered same=%u\n", one == prior);
                    if (one)
                    {
                        Hr("first retained matrix get", IDirect3DDevice_GetMatrix(one, handle, &copy));
                        Words("first retained matrix", &copy, sizeof copy);
                        Hr("first retained matrix delete", IDirect3DDevice_DeleteMatrix(one, handle));
                        IDirect3DDevice_Release(one);
                        one = NULL;
                    }
                }
                if (old_surface)
                    IDirectDrawSurface_Release(old_surface);
            }
            else
                Hr("legacy Device3",
                   IDirect3D3_CreateDevice(old, rgb_device ? &IID_IDirect3DRGBDevice : &IID_IDirect3DHALDevice, surface, &three, NULL));
            Hr("legacy viewport", IDirect3D3_CreateViewport(old, &view, NULL));
            Hr("legacy material", IDirect3D3_CreateMaterial(old, &material, NULL));
            Hr("legacy light", IDirect3D3_CreateLight(old, &light, NULL));
            if (view)
            {
                D3DVIEWPORT2 p = {0};
                p.dwSize = sizeof p;
                Hr("viewport default", IDirect3DViewport3_GetViewport2(view, &p));
                Words("viewport default", &p, sizeof p);
                if (three)
                    Hr("add viewport", IDirect3DDevice3_AddViewport(three, view));
                p.dwX = 0;
                p.dwY = 0;
                p.dwWidth = p.dwHeight = 256;
                p.dvClipX = -1;
                p.dvClipY = 1;
                p.dvClipWidth = p.dvClipHeight = 2;
                p.dvMinZ = 0;
                p.dvMaxZ = 1;
                Hr("viewport set", IDirect3DViewport3_SetViewport2(view, &p));
                memset(&p, 0, sizeof p);
                p.dwSize = sizeof p;
                Hr("viewport get", IDirect3DViewport3_GetViewport2(view, &p));
                Words("viewport", &p, sizeof p);
                D3DVIEWPORT p1 = {0};
                p1.dwSize = sizeof p1;
                Hr("viewport1 from2", IDirect3DViewport3_GetViewport(view, &p1));
                Words("viewport1 from2", &p1, sizeof p1);
            }
            if (three)
            {
                D3DDEVICEDESC hw = {0}, sw = {0};
                hw.dwSize = sw.dwSize = sizeof hw;
                Hr("legacy caps", IDirect3DDevice3_GetCaps(three, &hw, &sw));
                Words("legacy hw", &hw, sizeof hw);
                Words("legacy sw", &sw, sizeof sw);
                D3DSTATS stats = {0};
                stats.dwSize = sizeof stats;
                Hr("legacy stats", IDirect3DDevice3_GetStats(three, &stats));
                Words("legacy stats", &stats, sizeof stats);
                Hr("legacy Device2", IDirect3DDevice3_QueryInterface(three, &IID_IDirect3DDevice2, (void **)&two));
                Hr("legacy Device1", IDirect3DDevice3_QueryInterface(three, &IID_IDirect3DDevice, (void **)&one));
                if (view)
                {
                    Hr("current viewport", IDirect3DDevice3_SetCurrentViewport(three, view));
                }
                if (material)
                {
                    D3DMATERIAL m = {0};
                    D3DMATERIALHANDLE handle = 0;
                    m.dwSize = sizeof m;
                    Hr("material default", IDirect3DMaterial3_GetMaterial(material, &m));
                    Words("material default", &m, sizeof m);
                    m.diffuse.r = m.ambient.r = m.diffuse.a = 1;
                    Hr("material set", IDirect3DMaterial3_SetMaterial(material, &m));
                    Hr("material handle", IDirect3DMaterial3_GetHandle(material, three, &handle));
                    Say("material handle valid=%u\n", handle != 0);
                    Hr("material bind", IDirect3DDevice3_SetLightState(three, D3DLIGHTSTATE_MATERIAL, handle));
                }
                if (light)
                {
                    D3DLIGHT2 l = {0};
                    l.dwSize = sizeof l;
                    Hr("light default", IDirect3DLight_GetLight(light, (D3DLIGHT *)&l));
                    Words("light default", &l, sizeof l);
                    l.dltType = D3DLIGHT_DIRECTIONAL;
                    l.dcvColor.r = l.dcvColor.g = l.dcvColor.b = 1;
                    l.dvDirection.z = 1;
                    l.dwFlags = D3DLIGHT_ACTIVE;
                    Hr("light set", IDirect3DLight_SetLight(light, (D3DLIGHT *)&l));
                    if (view)
                    {
                        Hr("light add", IDirect3DViewport3_AddLight(view, light));
                        IDirect3DLight *next = NULL;
                        Hr("light head", IDirect3DViewport3_NextLight(view, NULL, &next, D3DNEXT_HEAD));
                        Say("light head same=%u\n", next == light);
                        if (next)
                            IDirect3DLight_Release(next);
                        next = NULL;
                        Hr("light next", IDirect3DViewport3_NextLight(view, light, &next, D3DNEXT_NEXT));
                        Say("light next null=%u\n", next == NULL);
                        if (next)
                            IDirect3DLight_Release(next);
                    }
                }
                if (view)
                {
                    D3DLVERTEX input[3] = {0};
                    input[0].z = .5f;
                    input[1].x = .5f;
                    input[1].y = .25f;
                    input[1].z = .25f;
                    input[2].x = -.5f;
                    input[2].y = -.5f;
                    input[2].z = .5f;
                    D3DTLVERTEX output[3] = {0};
                    D3DHVERTEX homogeneous[3] = {0};
                    DWORD offscreen = 0;
                    D3DTRANSFORMDATA data = {0};
                    data.dwSize = sizeof data;
                    data.lpIn = input;
                    data.dwInSize = sizeof input[0];
                    data.lpOut = output;
                    data.dwOutSize = sizeof output[0];
                    data.lpHOut = homogeneous;
                    Hr("viewport transform", IDirect3DViewport3_TransformVertices(view, 3, &data, D3DTRANSFORM_UNCLIPPED, &offscreen));
                    Words("viewport transformed", output, sizeof output);
                    Words("viewport homogeneous", homogeneous, sizeof homogeneous);
                    Say("viewport offscreen=%lu\n", (unsigned long)offscreen);
                    D3DLIGHTINGELEMENT lights[3] = {0};
                    for (unsigned n = 0; n < 3; ++n)
                    {
                        lights[n].dvPosition.x = input[n].x;
                        lights[n].dvPosition.y = input[n].y;
                        lights[n].dvPosition.z = input[n].z;
                        lights[n].dvNormal.z = -1;
                    }
                    memset(output, 0, sizeof output);
                    D3DLIGHTDATA lighting = {0};
                    lighting.dwSize = sizeof lighting;
                    lighting.lpIn = lights;
                    lighting.dwInSize = sizeof lights[0];
                    lighting.lpOut = output;
                    lighting.dwOutSize = sizeof output[0];
                    Hr("viewport lighting", IDirect3DViewport3_LightElements(view, 3, &lighting));
                    Words("viewport lit", output, sizeof output);
                }
                IDirect3DDevice3_SetRenderState(three, D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
                IDirect3DDevice3_SetRenderState(three, D3DRENDERSTATE_ZENABLE, FALSE);
                if (view)
                    IDirect3DViewport3_Clear2(view, 1, &full_rect, D3DCLEAR_TARGET, 0, 1, 0);
                TL v[6];
                QuadVertices(v, 32, 32, 224, 224, .5f, 0xff00ff00);
                Hr("legacy begin3", IDirect3DDevice3_BeginScene(three));
                Hr("legacy draw3", IDirect3DDevice3_DrawPrimitive(three, D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, v, 6, 0));
                Hr("legacy end3", IDirect3DDevice3_EndScene(three));
                Shoot(target, "LEGACY3");
                CheckCenter("LEGACY3", 0x00ff00);
                if (two)
                {
                    QuadVertices(v, 32, 32, 224, 224, .5f, 0xffff0000);
                    Hr("legacy begin2", IDirect3DDevice2_BeginScene(two));
                    Hr("legacy draw2", IDirect3DDevice2_DrawPrimitive(two, D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, v, 6, 0));
                    Hr("legacy end2", IDirect3DDevice2_EndScene(two));
                    Shoot(target, "LEGACY2");
                    CheckCenter("LEGACY2", 0xff0000);
                }
                if (three)
                {
                    QuadVertices(v, 32, 32, 224, 224, .5f, 0xffffff00);
                    Hr("immediate scene", IDirect3DDevice3_BeginScene(three));
                    Hr("immediate begin", IDirect3DDevice3_Begin(three, D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, 0));
                    for (unsigned n = 0; n < 6; ++n)
                        Hr("immediate vertex", IDirect3DDevice3_Vertex(three, &v[n]));
                    Hr("immediate end", IDirect3DDevice3_End(three, 0));
                    Hr("immediate scene end", IDirect3DDevice3_EndScene(three));
                    Shoot(target, "IMMED");
                    CheckCenter("IMMED", 0xffff00);
                    D3DVERTEXBUFFERDESC vd = {0};
                    IDirect3DVertexBuffer *vb = NULL;
                    vd.dwSize = sizeof vd;
                    vd.dwCaps = D3DVBCAPS_SYSTEMMEMORY;
                    vd.dwFVF = D3DFVF_TLVERTEX;
                    vd.dwNumVertices = 6;
                    Hr("legacy vb create", IDirect3D3_CreateVertexBuffer(old, &vd, &vb, 0, NULL));
                    if (vb)
                    {
                        void *bytes = NULL;
                        DWORD size = 0;
                        Hr("legacy vb lock", IDirect3DVertexBuffer_Lock(vb, 0, &bytes, &size));
                        Say("legacy vb bytes=%lu\n", (unsigned long)size);
                        QuadVertices(v, 32, 32, 224, 224, .5f, 0xff00ffff);
                        if (bytes)
                            memcpy(bytes, v, sizeof v);
                        Hr("legacy vb unlock", IDirect3DVertexBuffer_Unlock(vb));
                        Hr("legacy vb scene", IDirect3DDevice3_BeginScene(three));
                        Hr("legacy vb draw", IDirect3DDevice3_DrawPrimitiveVB(three, D3DPT_TRIANGLELIST, vb, 0, 6, 0));
                        Hr("legacy vb end", IDirect3DDevice3_EndScene(three));
                        Shoot(target, "LEGACYVB");
                        CheckCenter("LEGACYVB", 0x00ffff);
                        IDirect3DVertexBuffer_Release(vb);
                    }
                }
                if (three)
                {
                    D3DVERTEX lit[6] = {0};
                    const float xy[6][2] = {{-.75f, .75f}, {.75f, .75f}, {-.75f, -.75f}, {-.75f, -.75f}, {.75f, .75f}, {.75f, -.75f}};
                    for (unsigned n = 0; n < 6; ++n)
                    {
                        lit[n].x = xy[n][0];
                        lit[n].y = xy[n][1];
                        lit[n].z = .5f;
                        lit[n].nz = -1;
                    }
                    Hr("legacy lit lighting", IDirect3DDevice3_SetRenderState(three, D3DRENDERSTATE_LIGHTING, TRUE));
                    Hr("legacy lit color", IDirect3DDevice3_SetRenderState(three, D3DRENDERSTATE_COLORVERTEX, FALSE));
                    Hr("legacy lit scene", IDirect3DDevice3_BeginScene(three));
                    Hr("legacy lit draw", IDirect3DDevice3_DrawPrimitive(three, D3DPT_TRIANGLELIST, D3DFVF_VERTEX, lit, 6, 0));
                    Hr("legacy lit end", IDirect3DDevice3_EndScene(three));
                    Shoot(target, "LEGACYLT");
                    CheckCenter("LEGACYLT", 0xff0000);
                    Hr("viewport clear zero", IDirect3DViewport3_Clear2(view, 0, NULL, D3DCLEAR_TARGET, 0, 1, 0));
                    Shoot(target, "CLEARZERO");
                    CheckCenter("CLEARZERO", 0xff0000);
                }
                if (view)
                {
                    D3DVIEWPORT2 saved, clip = {0};
                    saved.dwSize = sizeof saved;
                    IDirect3DViewport3_GetViewport2(view, &saved);
                    IDirect3DViewport3_Clear2(view, 1, &full_rect, D3DCLEAR_TARGET, 0, 1, 0);
                    clip.dwSize = sizeof clip;
                    clip.dwX = 32;
                    clip.dwY = 16;
                    clip.dwWidth = 192;
                    clip.dwHeight = 224;
                    clip.dvClipX = -.25f;
                    clip.dvClipY = .75f;
                    clip.dvClipWidth = clip.dvClipHeight = 1;
                    clip.dvMaxZ = 1;
                    Hr("viewport clip set", IDirect3DViewport3_SetViewport2(view, &clip));
                    D3DVIEWPORT converted = {0};
                    converted.dwSize = sizeof converted;
                    Hr("viewport clip convert", IDirect3DViewport3_GetViewport(view, &converted));
                    Words("viewport clip converted", &converted, sizeof converted);
                    D3DMATRIX projection;
                    Hr("viewport logical projection", IDirect3DDevice3_GetTransform(three, D3DTRANSFORMSTATE_PROJECTION, &projection));
                    Words("viewport logical projection", &projection, sizeof projection);
                    D3DVERTEX vertices[6] = {0};
                    const float xy[6][2] = {{-.125f, -.125f}, {-.125f, .375f}, {.375f, .375f},
                                            {-.125f, -.125f}, {.375f, .375f},  {.375f, -.125f}};
                    for (unsigned n = 0; n < 6; ++n)
                    {
                        vertices[n].x = xy[n][0];
                        vertices[n].y = xy[n][1];
                        vertices[n].z = .5f;
                        vertices[n].nz = -1;
                    }
                    Hr("viewport clip begin", IDirect3DDevice3_BeginScene(three));
                    Hr("viewport clip draw", IDirect3DDevice3_DrawPrimitive(three, D3DPT_TRIANGLELIST, D3DFVF_VERTEX, vertices, 6, 0));
                    Hr("viewport clip end", IDirect3DDevice3_EndScene(three));
                    Shoot(target, "VIEWCLIP");
                    CheckCenter("VIEWCLIP", 0xff0000);
                    Pixel("clip outside left", 32, 128);
                    Pixel("clip outside top", 128, 80);
                    Hr("viewport clip restore", IDirect3DViewport3_SetViewport2(view, &saved));
                }
                if (two)
                {
                    DDSURFACEDESC2 td = {0};
                    IDirectDrawSurface4 *ts = NULL;
                    IDirect3DTexture2 *texture = NULL;
                    D3DTEXTUREHANDLE handle = 0;
                    td.dwSize = sizeof td;
                    td.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
                    td.dwWidth = td.dwHeight = 2;
                    td.ddsCaps.dwCaps = DDSCAPS_TEXTURE | (rgb_device ? DDSCAPS_SYSTEMMEMORY : DDSCAPS_VIDEOMEMORY);
                    td.ddpfPixelFormat.dwSize = sizeof td.ddpfPixelFormat;
                    td.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
                    td.ddpfPixelFormat.dwRGBBitCount = 32;
                    td.ddpfPixelFormat.dwRBitMask = 0xff0000;
                    td.ddpfPixelFormat.dwGBitMask = 0xff00;
                    td.ddpfPixelFormat.dwBBitMask = 0xff;
                    td.ddpfPixelFormat.dwRGBAlphaBitMask = 0xff000000;
                    Hr("legacy texture create", IDirectDraw4_CreateSurface(legacy_draw, &td, &ts, NULL));
                    if (ts)
                    {
                        DDSURFACEDESC2 lock = {0};
                        lock.dwSize = sizeof lock;
                        Hr("legacy texture lock", IDirectDrawSurface4_Lock(ts, NULL, &lock, DDLOCK_WAIT, NULL));
                        if (lock.lpSurface)
                            for (unsigned y = 0; y < 2; ++y)
                                for (unsigned x = 0; x < 2; ++x)
                                    ((DWORD *)((BYTE *)lock.lpSurface + y * lock.lPitch))[x] = 0xff204080;
                        Hr("legacy texture unlock", IDirectDrawSurface4_Unlock(ts, NULL));
                        Hr("legacy texture query", IDirectDrawSurface4_QueryInterface(ts, &IID_IDirect3DTexture2, (void **)&texture));
                        if (texture)
                        {
                            Hr("legacy texture handle", IDirect3DTexture2_GetHandle(texture, two, &handle));
                            Say("legacy texture handle valid=%u\n", handle != 0);
                            Hr("legacy texture bind", IDirect3DDevice2_SetRenderState(two, D3DRENDERSTATE_TEXTUREHANDLE, handle));
                            Hr("legacy texture blend",
                               IDirect3DDevice2_SetRenderState(two, D3DRENDERSTATE_TEXTUREMAPBLEND, D3DTBLEND_DECAL));
                            QuadVertices(v, 32, 32, 224, 224, .5f, 0xffffffff);
                            Hr("legacy texture scene", IDirect3DDevice2_BeginScene(two));
                            Hr("legacy texture draw", IDirect3DDevice2_DrawPrimitive(two, D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, v, 6, 0));
                            Hr("legacy texture end", IDirect3DDevice2_EndScene(two));
                            Shoot(target, "LEGACYTX");
                            CheckCenter("LEGACYTX", 0x204080);
                            IDirect3DDevice2_SetRenderState(two, D3DRENDERSTATE_TEXTUREHANDLE, 0);
                            IDirect3DTexture2_Release(texture);
                        }
                        IDirectDrawSurface4_Release(ts);
                    }
                }
                if (one)
                {
                    IDirect3DDevice *direct = NULL, *again = NULL;
                    Hr("surface device query", IDirectDrawSurface4_QueryInterface(surface, &IID_IDirect3DHALDevice, (void **)&direct));
                    if (direct)
                    {
                        Hr("surface device query again",
                           IDirectDrawSurface4_QueryInterface(surface, &IID_IDirect3DHALDevice, (void **)&again));
                        Say("surface device same=%u\n", direct == one);
                        Say("surface device stable=%u\n", direct == again);
                        IUnknown *a = NULL, *b = NULL;
                        IDirect3DDevice_QueryInterface(direct, &IID_IUnknown, (void **)&a);
                        IDirectDrawSurface4_QueryInterface(surface, &IID_IUnknown, (void **)&b);
                        Say("surface device identity=%u\n", a == b);
                        if (a)
                            IUnknown_Release(a);
                        if (b)
                            IUnknown_Release(b);
                        if (again)
                            IDirect3DDevice_Release(again);
                        IDirect3DDevice_Release(direct);
                    }
                    D3DMATRIXHANDLE h = 0;
                    D3DMATRIX matrix = identity, copy = {0};
                    Hr("matrix create", IDirect3DDevice_CreateMatrix(one, &h));
                    Say("matrix handle valid=%u\n", h != 0);
                    Hr("matrix set", IDirect3DDevice_SetMatrix(one, h, &matrix));
                    Hr("matrix get", IDirect3DDevice_GetMatrix(one, h, &copy));
                    Words("matrix", &copy, sizeof copy);
                    Hr("matrix delete", IDirect3DDevice_DeleteMatrix(one, h));
                    D3DEXECUTEBUFFERDESC desc = {0};
                    IDirect3DExecuteBuffer *execute = NULL;
                    desc.dwSize = sizeof desc;
                    desc.dwFlags = D3DDEB_BUFSIZE | D3DDEB_CAPS;
                    desc.dwCaps = D3DDEBCAPS_SYSTEMMEMORY;
                    desc.dwBufferSize = 4096;
                    Hr("execute create", IDirect3DDevice_CreateExecuteBuffer(one, &desc, &execute, NULL));
                    if (execute)
                    {
                        D3DEXECUTEDATA data = {0};
                        data.dwSize = sizeof data;
                        DWORD offset = 0;
                        Hr("execute validate", IDirect3DExecuteBuffer_Validate(execute, &offset, NULL, NULL, 0));
                        Hr("execute optimize", IDirect3DExecuteBuffer_Optimize(execute, 0));
                        Hr("execute data default", IDirect3DExecuteBuffer_GetExecuteData(execute, &data));
                        Words("execute data", &data, sizeof data);
                        Hr("execute lock", IDirect3DExecuteBuffer_Lock(execute, &desc));
                        Say("execute buffer bytes=%lu\n", (unsigned long)desc.dwBufferSize);
                        QuadVertices(v, 32, 32, 224, 224, .5f, 0xff0000ff);
                        memcpy(desc.lpData, v, sizeof v);
                        BYTE *stream = (BYTE *)desc.lpData + sizeof v;
                        D3DINSTRUCTION *instruction = (D3DINSTRUCTION *)stream;
                        instruction->bOpcode = D3DOP_PROCESSVERTICES;
                        instruction->bSize = sizeof(D3DPROCESSVERTICES);
                        instruction->wCount = 1;
                        D3DPROCESSVERTICES *op = (void *)(instruction + 1);
                        memset(op, 0, sizeof *op);
                        op->dwFlags = D3DPROCESSVERTICES_COPY;
                        op->dwCount = 6;
                        instruction = (void *)(op + 1);
                        instruction->bOpcode = D3DOP_TRIANGLE;
                        instruction->bSize = sizeof(D3DTRIANGLE);
                        instruction->wCount = 2;
                        D3DTRIANGLE *tri = (void *)(instruction + 1);
                        memset(tri, 0, 2 * sizeof *tri);
                        tri[0].v1 = 0;
                        tri[0].v2 = 1;
                        tri[0].v3 = 2;
                        tri[1].v1 = 3;
                        tri[1].v2 = 4;
                        tri[1].v3 = 5;
                        instruction = (void *)(tri + 2);
                        instruction->bOpcode = D3DOP_EXIT;
                        instruction->bSize = 0;
                        instruction->wCount = 0;
                        Hr("execute unlock", IDirect3DExecuteBuffer_Unlock(execute));
                        data.dwSize = sizeof data;
                        data.dwVertexCount = 6;
                        data.dwInstructionOffset = sizeof v;
                        data.dwInstructionLength = (BYTE *)(instruction + 1) - stream;
                        Hr("execute data set", IDirect3DExecuteBuffer_SetExecuteData(execute, &data));
                        if (view)
                        {
                            IDirect3DViewport *v1 = NULL;
                            IDirect3DViewport3_QueryInterface(view, &IID_IDirect3DViewport, (void **)&v1);
                            if (v1)
                            {
                                D3DVIEWPORT vp = {0};
                                vp.dwSize = sizeof vp;
                                vp.dwWidth = vp.dwHeight = 256;
                                vp.dvScaleX = vp.dvScaleY = 128;
                                vp.dvMaxX = vp.dvMaxY = 1;
                                vp.dvMaxZ = 1;
                                Hr("execute viewport", IDirect3DViewport_SetViewport(v1, &vp));
                                D3DVIEWPORT2 p2 = {0};
                                p2.dwSize = sizeof p2;
                                Hr("viewport2 from1", IDirect3DViewport3_GetViewport2(view, &p2));
                                Words("viewport2 from1", &p2, sizeof p2);
#ifndef _M_ARM64
                                FILE *debug = fopen("EADDR.TXT", "w");
                                if (debug)
                                {
                                    fprintf(debug, "%p\n", one->lpVtbl->Execute);
                                    fclose(debug);
                                }
#endif
                                Hr("execute begin", IDirect3DDevice_BeginScene(one));
                                Hr("execute run", IDirect3DDevice_Execute(one, execute, v1, D3DEXECUTE_UNCLIPPED));
                                Hr("execute end", IDirect3DDevice_EndScene(one));
                                Shoot(target, "EXECUTE");
                                CheckCenter("EXECUTE", 0x0000ff);
                                IDirect3DViewport_Release(v1);
                            }
                        }
                        IDirect3DExecuteBuffer_Release(execute);
                    }
                    IDirect3DDevice_Release(one);
                }
                if (two)
                    IDirect3DDevice2_Release(two);
                if (view)
                {
                    if (light)
                        IDirect3DViewport3_DeleteLight(view, light);
                    IDirect3DDevice3_DeleteViewport(three, view);
                }
                IDirect3DDevice3_Release(three);
            }
            if (view)
                IDirect3DViewport3_Release(view);
            if (material)
                IDirect3DMaterial3_Release(material);
            if (light)
                IDirect3DLight_Release(light);
            if (surface)
                IDirectDrawSurface4_Release(surface);
            IDirect3D3_Release(old);
        }
        if (depth)
            IDirectDrawSurface7_Release(depth);
        IDirectDrawSurface7_Release(target);
        IDirectDraw4_Release(legacy_draw);
    }
    Say("D3D scenes=%d wrong=%d\n", scenes, wrong);
    fclose(report);
    return wrong != 0;
}
