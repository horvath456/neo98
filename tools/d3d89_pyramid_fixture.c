/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest side of tools/test_direct3d89_guest.py: the pyramid of
   tools/d3d_pyramid_fixture.c drawn with Direct3D 8 (default) or Direct3D 9
   (with DX9) through the original D3D8.DLL or D3D9.DLL of DirectX 9.0c, on
   the Direct3D HAL of the NEO98 display driver.

   Direct3DCreate8 or Direct3DCreate9, GetDeviceCaps, CreateDevice in
   exclusive full screen 640x480 X8R8G8B8 with an automatic D16 depth buffer
   and software vertex processing (HARDWARE with TNL), lighting off, view,
   projection and world transforms, Clear, DrawPrimitiveUP with XYZ|DIFFUSE
   vertices, Present. Every answer goes out through OutputDebugStringA as
   "D3DPYR name=value"; the last frames stand on 30 degrees like the DX7
   fixture, so the same prediction checks the picture. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U LoadLibraryA(const char *);
API U GetProcAddress(U, const char *);
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
#ifdef HOLD_FRAME
API void Sleep(U);
#endif
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, int);
API U SetForegroundWindow(U);
API U GetSystemMetrics(int);

/* The MSVC target wants this symbol from any program that uses floating point. */
int _fltused = 1;

typedef void *(__stdcall *Create)(U);
typedef U(__stdcall *M1)(void *);
typedef U(__stdcall *M2)(void *, U);
typedef U(__stdcall *M3)(void *, U, U);
typedef U(__stdcall *M4)(void *, U, U, U);
typedef U(__stdcall *M5)(void *, U, U, U, U);
typedef U(__stdcall *M6)(void *, U, U, U, U, U);
typedef U(__stdcall *M7)(void *, U, U, U, U, U, U);
typedef U(__stdcall *M8)(void *, U, U, U, U, U, U, U);
typedef U(__stdcall *M9)(void *, U, U, U, U, U, U, U, U);
typedef U(__stdcall *M10)(void *, U, U, U, U, U, U, U, U, U);

static char line[256];
static void number(const char *label, U value)
{
    static const char digits[] = "0123456789abcdef";
    U at = 0, i = 0;
    while (label[i]) line[at++] = label[i++];
    line[at++] = '=';
    for (i = 0; i < 8; ++i) line[at++] = digits[(value >> (28 - i * 4)) & 15];
    line[at] = 0;
    OutputDebugStringA(line);
}
static void fail(U code) { number("D3DPYR fail", code); ExitProcess(code); }

#ifdef DX9
/* IDirect3D9 and IDirect3DDevice9 */
enum { GetCaps = 14, CreateDevice = 16, Present = 17, BeginScene = 41, EndScene = 42, Clear = 43,
       SetTransform = 44, SetViewport = 47, SetRenderState = 57, DrawPrimitiveUP = 83, SetFormat = 89,
       CreateVertexBuffer = 26, CreateIndexBuffer = 27, SetStreamSource = 100, SetIndices = 104,
       DrawPrimitive = 81, DrawIndexedPrimitive = 82, DrawIndexedPrimitiveUP = 84,
       CreateTexture = 23, SetTexture = 65, TextureLockRect = 19, TextureUnlockRect = 20,
       CreatePixelShader = 106, SetPixelShader = 107, SetPixelShaderConstant = 109, SetTextureStageState = 67,
       CreateVertexDeclaration = 86, SetVertexDeclaration = 87, CreateVertexShader = 91, SetVertexShader = 92,
       SetVertexShaderConstant = 94, GetBackBuffer = 18, CreateRenderTarget = 28, StretchRect = 34, ColorFill = 35,
       CreateQuery = 118, QueryIssue = 6, QueryGetData = 7, CreateVolumeTexture = 24 };
#define DLL "D3D9.DLL"
#define ENTRY "Direct3DCreate9"
#define SDK 32
#define PARAMETERS 14
#else
/* IDirect3D8 and IDirect3DDevice8 */
enum { GetCaps = 13, CreateDevice = 15, Present = 15, BeginScene = 34, EndScene = 35, Clear = 36,
       SetTransform = 37, SetViewport = 40, SetRenderState = 50, DrawPrimitiveUP = 72, SetFormat = 76,
       CreateVertexBuffer = 23, CreateIndexBuffer = 24, SetStreamSource = 83, SetIndices = 85,
       DrawPrimitive = 70, DrawIndexedPrimitive = 71, DrawIndexedPrimitiveUP = 73,
       CreateTexture = 20, SetTexture = 61, TextureLockRect = 16, TextureUnlockRect = 17,
       CreatePixelShader = 87, SetPixelShader = 88, SetPixelShaderConstant = 91, SetTextureStageState = 63,
       CreateVertexShader = 75, SetVertexShader = 76, SetVertexShaderConstant = 79, CreateVolumeTexture = 21 };
#define DLL "D3D8.DLL"
#define ENTRY "Direct3DCreate8"
#define SDK 220
#define PARAMETERS 13
#endif

/* sin and cos without a C library: range reduction and a Taylor series. */
static const float PI = 3.14159265f;
static float fsin(float x)
{
    float x2, term, sum;
    int i;
    while (x > PI) x -= 2 * PI;
    while (x < -PI) x += 2 * PI;
    x2 = x * x;
    term = x;
    sum = x;
    for (i = 1; i < 8; ++i)
    {
        term = -term * x2 / (float)((2 * i) * (2 * i + 1));
        sum += term;
    }
    return sum;
}
static float fcos(float x) { return fsin(x + PI / 2); }

/* D3DMATRIX, row major, as the transforms want it. */
static void identity(float *m)
{
    int i;
    for (i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}
static void rotation_y(float *m, float angle)
{
    const float c = fcos(angle), s = fsin(angle);
    identity(m);
    m[0] = c; m[2] = -s;
    m[8] = s; m[10] = c;
}
/* Left-handed look-at from the eye to the origin, up is +y. */
static float fsqrt(float v)
{
    float r = v > 1.0f ? v : 1.0f;
    int i;
    for (i = 0; i < 20; ++i) r = 0.5f * (r + v / r);
    return r;
}
static void look_at(float *m, float ex, float ey, float ez)
{
    float zx = -ex, zy = -ey, zz = -ez, len = fsqrt(zx * zx + zy * zy + zz * zz);
    float xx, xy, xz, yx, yy, yz, xlen;
    zx /= len; zy /= len; zz /= len;
    /* x = up cross z, up = (0,1,0) */
    xx = zz; xy = 0.0f; xz = -zx;
    xlen = fsqrt(xx * xx + xz * xz);
    xx /= xlen; xz /= xlen;
    /* y = z cross x */
    yx = zy * xz - zz * xy; yy = zz * xx - zx * xz; yz = zx * xy - zy * xx;
    identity(m);
    m[0] = xx; m[1] = yx; m[2] = zx;
    m[4] = xy; m[5] = yy; m[6] = zy;
    m[8] = xz; m[9] = yz; m[10] = zz;
    m[12] = -(xx * ex + xy * ey + xz * ez);
    m[13] = -(yx * ex + yy * ey + yz * ez);
    m[14] = -(zx * ex + zy * ey + zz * ez);
}
/* out = a * b, row major. */
static void multiply(const float *a, const float *b, float *out)
{
    int r, c, k;
    for (r = 0; r < 4; ++r)
        for (c = 0; c < 4; ++c)
        {
            float sum = 0.0f;
            for (k = 0; k < 4; ++k) sum += a[r * 4 + k] * b[k * 4 + c];
            out[r * 4 + c] = sum;
        }
}
static void perspective(float *m, float fov, float aspect, float zn, float zf)
{
    const float h = fcos(fov / 2) / fsin(fov / 2), w = h / aspect, q = zf / (zf - zn);
    int i;
    for (i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0] = w; m[5] = h; m[10] = q; m[11] = 1.0f; m[14] = -q * zn;
}

/* The pyramid: apex at the top, a square base, four coloured sides and a
   magenta bottom. Each triangle is clockwise seen from outside, which is the
   front side under the default cull mode. */
#ifndef VERTEX_PAD
#define VERTEX_PAD 0
#endif
#ifdef VOLUME
/* XYZ | DIFFUSE | TEX1 with three coordinates: the faces take their colour
   from a slice of a volume texture. */
struct Vertex { float x, y, z; U color; float u, v, w; };
#define FVF 0x10142
#elif defined(TEXTURED)
/* XYZ | DIFFUSE | TEX1: the faces are white and take their colour from a
   texel of the texture. */
struct Vertex { float x, y, z; U color; float u, v; };
#define FVF 0x142
#else
struct Vertex {
#if VERTEX_PAD
    U padding[VERTEX_PAD / 4];
#endif
    float x, y, z; U color;
};
#define FVF 0x42
#endif
#define STRIDE sizeof(struct Vertex)
static struct Vertex pyramid[18];
static void build_pyramid(void)
{
    const float ax = 0.0f, ay = 1.0f, az = 0.0f;
    static const float corner[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
    /* faces: front (z=-1) red, right (x=+1) green, back (z=+1) blue, left (x=-1) yellow */
    static const U colors[4] = {0xffff0000u, 0xff00ff00u, 0xff0000ffu, 0xffffff00u};
    static const int order[4][2] = {{1, 0}, {2, 1}, {3, 2}, {0, 3}};
    int f, v = 0;
    for (f = 0; f < 4; ++f)
    {
        pyramid[v].x = ax; pyramid[v].y = ay; pyramid[v].z = az; pyramid[v].color = colors[f]; ++v;
        pyramid[v].x = corner[order[f][0]][0]; pyramid[v].y = -1.0f; pyramid[v].z = corner[order[f][0]][1]; pyramid[v].color = colors[f]; ++v;
        pyramid[v].x = corner[order[f][1]][0]; pyramid[v].y = -1.0f; pyramid[v].z = corner[order[f][1]][1]; pyramid[v].color = colors[f]; ++v;
    }
    /* the base, seen from below: two triangles, clockwise from below */
    pyramid[v].x = -1.0f; pyramid[v].y = -1.0f; pyramid[v].z = -1.0f; pyramid[v].color = 0xffff00ffu; ++v;
    pyramid[v].x = 1.0f; pyramid[v].y = -1.0f; pyramid[v].z = 1.0f; pyramid[v].color = 0xffff00ffu; ++v;
    pyramid[v].x = 1.0f; pyramid[v].y = -1.0f; pyramid[v].z = -1.0f; pyramid[v].color = 0xffff00ffu; ++v;
    pyramid[v].x = -1.0f; pyramid[v].y = -1.0f; pyramid[v].z = -1.0f; pyramid[v].color = 0xffff00ffu; ++v;
    pyramid[v].x = -1.0f; pyramid[v].y = -1.0f; pyramid[v].z = 1.0f; pyramid[v].color = 0xffff00ffu; ++v;
    pyramid[v].x = 1.0f; pyramid[v].y = -1.0f; pyramid[v].z = 1.0f; pyramid[v].color = 0xffff00ffu; ++v;
#ifdef TEXTURED
    /* Face f samples the centre of texel f of the 2x2 texture. */
    for (v = 0; v < 18; ++v)
    {
        const int f = v < 12 ? v / 3 : 0;
        pyramid[v].color = 0xffffffffu;
        pyramid[v].u = (f & 1) ? 0.75f : 0.25f;
        pyramid[v].v = (f & 2) ? 0.75f : 0.25f;
#ifdef VOLUME
        /* Face f samples the middle of slice f of the four. */
        pyramid[v].u = pyramid[v].v = 0.5f;
        pyramid[v].w = ((float)f + 0.5f) / 4.0f;
#endif
    }
#endif
}

static U caps[128], present[PARAMETERS];
static void *vertex_buffer, *index_buffer, *blit_target, *query;
static U query_asks;
static const unsigned short base_indices[12] = {3, 4, 5, 9, 10, 11, 12, 13, 14, 15, 16, 17};
static float matrix[16], world[16], view_matrix[16], projection_matrix[16], combined[16], rows[16];
static U viewport[6];

static U __stdcall window_procedure(U window, U msg, U wp, U lp)
{
    return DefWindowProcA(window, msg, wp, lp);
}

static void *device;
static U **dvt;

/* Faces by number: 0 front, 1 right, 2 back, 3 left; the base is the six
   vertices from 12. draw_user draws the face `plain` with DrawPrimitiveUP and
   the face `indexed` and the base (when `base` says 1) with
   DrawIndexedPrimitiveUP. */
static int draw_user(U plain, U indexed, U base)
{
    static unsigned short indices[6];
    U value, i;
    value = ((M5)dvt[DrawPrimitiveUP])(device, 4, 1, (U)&pyramid[plain * 3], STRIDE);   /* D3DPT_TRIANGLELIST */
    if (value) { number("D3DPYR draw", value); return 0; }
    for (i = 0; i < 3; ++i) indices[i] = (unsigned short)(indexed * 3 + i);
    value = ((M9)dvt[DrawIndexedPrimitiveUP])(device, 4, 0, 18, 1, (U)indices, 101, (U)pyramid, STRIDE);
    if (value) { number("D3DPYR draw_indexed_up", value); return 0; }
    if (base & 1)
    {
        for (i = 0; i < 6; ++i) indices[i] = (unsigned short)(12 + i);
        value = ((M9)dvt[DrawIndexedPrimitiveUP])(device, 4, 12, 6, 2, (U)indices, 101, (U)pyramid, STRIDE);
        if (value) { number("D3DPYR draw_indexed_up", value); return 0; }
    }
    return 1;
}
/* The same with the buffers: the face `plain` with DrawPrimitive out of the
   vertex buffer, the face `indexed` with DrawIndexedPrimitive through the
   index buffer (its first three indices are the right face, the next three
   the left face, then the base), and the base when `base` says 2. */
static int draw_buffers(U plain, U indexed, U base)
{
    U value;
#ifdef DX9
    value = ((M5)dvt[SetStreamSource])(device, 0, (U)vertex_buffer, 0, STRIDE);
#else
    value = ((M4)dvt[SetStreamSource])(device, 0, (U)vertex_buffer, STRIDE);
#endif
    if (value) { number("D3DPYR stream", value); return 0; }
    value = ((M4)dvt[DrawPrimitive])(device, 4, plain * 3, 1);
    if (value) { number("D3DPYR draw_buffer", value); return 0; }
#ifdef DX9
    value = ((M2)dvt[SetIndices])(device, (U)index_buffer);
#else
    value = ((M3)dvt[SetIndices])(device, (U)index_buffer, 0);
#endif
    if (value) { number("D3DPYR indices", value); return 0; }
#ifdef DX9
    value = ((M7)dvt[DrawIndexedPrimitive])(device, 4, 0, indexed * 3, 3, indexed == 1 ? 0 : 3, 1);
#else
    value = ((M6)dvt[DrawIndexedPrimitive])(device, 4, indexed * 3, 3, indexed == 1 ? 0 : 3, 1);
#endif
    if (value) { number("D3DPYR draw_indexed", value); return 0; }
    if (base & 2)
    {
#ifdef DX9
        value = ((M7)dvt[DrawIndexedPrimitive])(device, 4, 0, 12, 6, 6, 2);
#else
        value = ((M6)dvt[DrawIndexedPrimitive])(device, 4, 12, 6, 6, 2);
#endif
        if (value) { number("D3DPYR draw_indexed", value); return 0; }
    }
    return 1;
}

void __stdcall mainCRTStartup(void)
{
    U library, window, value, i, frame;
    U class_fields[10];
    void *d3d = 0;
    U **vt;
    float angle;

    library = LoadLibraryA(DLL);
    number("D3DPYR d3d_loaded", library ? 1 : 0);
    if (!library) fail(1);
    {
        Create create = (Create)GetProcAddress(library, ENTRY);
        if (!create) fail(2);
        d3d = create(SDK);
        number("D3DPYR create", d3d ? 0 : 1);
        if (!d3d) fail(3);
    }
    vt = *(U ***)d3d;

    for (i = 0; i < 10; ++i) class_fields[i] = 0;
    class_fields[1] = (U)window_procedure;
    class_fields[7] = 6;
    class_fields[9] = (U) "D3DPyramid";
    RegisterClassA(class_fields);
    window = CreateWindowExA(0, "D3DPyramid", "Direct3D Pyramide", 0x90000000u, 0, 0,
                             GetSystemMetrics(0), GetSystemMetrics(1), 0, 0, 0, 0);
    number("D3DPYR window", window ? 1 : 0);
    if (!window) fail(4);
    ShowWindow(window, 5);
    SetForegroundWindow(window);

    number("D3DPYR module", library);
    number("D3DPYR getcaps_entry", (U)vt[GetCaps]);
    /* The HAL device of the default adapter (D3DDEVTYPE_HAL 1). */
    value = ((M4)vt[GetCaps])(d3d, 0, 1, (U)caps);
    number("D3DPYR caps", value);
    if (value) fail(5);
    number("D3DPYR devcaps", caps[7]);          /* DevCaps */
    number("D3DPYR caps2", caps[3]);
    {
        /* The desktop mode, and whether the HAL renders into a window of it. */
        static U mode[4];
        value = ((M3)vt[8])(d3d, 0, (U)mode);
        number("D3DPYR desktop_format", mode[3]);
        value = ((M6)vt[9])(d3d, 0, 1, mode[3], mode[3], 1);
        number("D3DPYR windowed_type", value);
    }

    for (i = 0; i < PARAMETERS; ++i) present[i] = 0;
    present[0] = 640; present[1] = 480;
    present[2] = 22;   /* D3DFMT_X8R8G8B8 */
    present[3] = 1;    /* one back buffer */
#ifdef DX9
    present[6] = 1;    /* D3DSWAPEFFECT_DISCARD */
    present[7] = window;
    present[9] = 1;    /* EnableAutoDepthStencil */
    present[10] = 80;  /* D3DFMT_D16 */
#else
    present[5] = 1;
    present[6] = window;
    present[8] = 1;
    present[9] = 80;
#endif
    {
#ifdef TNL
        const U behavior = 0x40;   /* D3DCREATE_HARDWARE_VERTEXPROCESSING */
#else
        const U behavior = 0x20;   /* D3DCREATE_SOFTWARE_VERTEXPROCESSING */
#endif
        value = ((M7)vt[CreateDevice])(d3d, 0, 1, window, behavior, (U)present, (U)&device);
    }
    number("D3DPYR device", value);
    if (value || !device) fail(6);
    dvt = *(U ***)device;

    viewport[0] = 0; viewport[1] = 0; viewport[2] = 640; viewport[3] = 480;
    *(float *)&viewport[4] = 0.0f; *(float *)&viewport[5] = 1.0f;
    value = ((M2)dvt[SetViewport])(device, (U)viewport);
    number("D3DPYR viewport", value);
    if (value) fail(7);
    value = ((M3)dvt[SetRenderState])(device, 137, 0);   /* D3DRS_LIGHTING off */
    number("D3DPYR lighting", value);
    value = ((M3)dvt[SetRenderState])(device, 7, 1);     /* D3DRS_ZENABLE */
    look_at(matrix, 0.0f, 1.2f, -4.0f);
    for (i = 0; i < 16; ++i) view_matrix[i] = matrix[i];
    value = ((M3)dvt[SetTransform])(device, 2, (U)matrix);     /* D3DTS_VIEW */
    number("D3DPYR view", value);
    perspective(matrix, PI / 3, 640.0f / 480.0f, 1.0f, 100.0f);
    for (i = 0; i < 16; ++i) projection_matrix[i] = matrix[i];
    value = ((M3)dvt[SetTransform])(device, 3, (U)matrix);     /* D3DTS_PROJECTION */
    number("D3DPYR projection", value);
    /* SetVertexShader with an FVF in DirectX 8, SetFVF in DirectX 9. */
    value = ((M2)dvt[SetFormat])(device, FVF);
    number("D3DPYR format", value);
    if (value) fail(8);
    build_pyramid();

    /* The whole pyramid in a vertex buffer, and the six indices of the base
       (vertices 12 to 17) in a 16-bit index buffer, both written once. */
    {
        U *bytes = 0;
#ifdef DX9
        value = ((M7)dvt[CreateVertexBuffer])(device, sizeof pyramid, 8, FVF, 1, (U)&vertex_buffer, 0);
#else
        value = ((M6)dvt[CreateVertexBuffer])(device, sizeof pyramid, 8, FVF, 1, (U)&vertex_buffer);
#endif
        number("D3DPYR vertex_buffer", value);
        if (value || !vertex_buffer) fail(15);
        value = ((M5)(*(U ***)vertex_buffer)[11])(vertex_buffer, 0, 0, (U)&bytes, 0);
        if (value || !bytes) fail(16);
        for (i = 0; i < sizeof pyramid / 4; ++i) bytes[i] = ((U *)pyramid)[i];
        ((M1)(*(U ***)vertex_buffer)[12])(vertex_buffer);
#ifdef DX9
        value = ((M7)dvt[CreateIndexBuffer])(device, sizeof base_indices, 8, 101, 1, (U)&index_buffer, 0);
#else
        value = ((M6)dvt[CreateIndexBuffer])(device, sizeof base_indices, 8, 101, 1, (U)&index_buffer);
#endif
        number("D3DPYR index_buffer", value);
        if (value || !index_buffer) fail(15);
        value = ((M5)(*(U ***)index_buffer)[11])(index_buffer, 0, 0, (U)&bytes, 0);
        if (value || !bytes) fail(16);
        for (i = 0; i < sizeof base_indices / 4; ++i) bytes[i] = ((U *)base_indices)[i];
        ((M1)(*(U ***)index_buffer)[12])(index_buffer);
    }

#ifdef TEXTURED
    /* A managed 2x2 A8R8G8B8 texture with the four face colours, one texel
       each; the runtime keeps it in system memory and loads it into the
       driver's video memory copy. */
    {
        static const U texels[4] = {0xffff0000u, 0xff00ff00u, 0xff0000ffu, 0xffffff00u};
        void *texture = 0;
        U rect[2] = {0, 0};
#ifdef VOLUME
        /* A managed 2x2x4 A8R8G8B8 volume, slice f all in the colour of face f. */
        U box[3] = {0, 0, 0}, z;
#ifdef DX9
        value = ((M10)dvt[CreateVolumeTexture])(device, 2, 2, 4, 1, 0, 21, 1, (U)&texture, 0);
#else
        value = ((M9)dvt[CreateVolumeTexture])(device, 2, 2, 4, 1, 0, 21, 1, (U)&texture);
#endif
        number("D3DPYR volume_texture", value);
        if (value || !texture) fail(22);
        value = ((M5)(*(U ***)texture)[TextureLockRect])(texture, 0, (U)box, 0, 0);
        number("D3DPYR volume_pitches", box[0] | box[1] << 16);
        if (value || !box[2]) fail(23);
        for (z = 0; z < 4; ++z)
            for (i = 0; i < 4; ++i) *(U *)(box[2] + z * box[1] + (i / 2) * box[0] + (i % 2) * 4) = texels[z];
        ((M2)(*(U ***)texture)[TextureUnlockRect])(texture, 0);
#elif defined(DX9)
        value = ((M9)dvt[CreateTexture])(device, 2, 2, 1, 0, 21, 1, (U)&texture, 0);
#else
        value = ((M8)dvt[CreateTexture])(device, 2, 2, 1, 0, 21, 1, (U)&texture);
#endif
#ifndef VOLUME
        number("D3DPYR texture", value);
        if (value || !texture) fail(22);
        value = ((M5)(*(U ***)texture)[TextureLockRect])(texture, 0, (U)rect, 0, 0);
        if (value || !rect[1]) fail(23);
        for (i = 0; i < 4; ++i) *(U *)(rect[1] + (i / 2) * rect[0] + (i % 2) * 4) = texels[i];
        ((M2)(*(U ***)texture)[TextureUnlockRect])(texture, 0);
#endif
        value = ((M3)dvt[SetTexture])(device, 0, (U)texture);
        number("D3DPYR set_texture", value);
        if (value) fail(24);
    }
#endif
#ifdef SHADER
    /* A pixel shader that gives the texel times the white diffuse colour,
       through a constant set by the program and one defined in the shader,
       with a co-issued pair (version 1.1) or texld (version 1.4). */
    {
#if SHADER == 14
        static const U code[] = {
            0xffff0104u,
            0x00000042u, 0x800f0000u, 0xb0e40000u,   /* texld r0, t0 */
            0x00000005u, 0x800f0000u, 0x80e40000u, 0x90e40000u,   /* mul r0, r0, v0 */
            0x00000005u, 0x800f0000u, 0x80e40000u, 0xa0e40002u,   /* mul r0, r0, c2 */
            0x0000ffffu};
#else
        static const U code[] = {
            0xffff0101u,
            0x00000051u, 0xa00f0001u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u,   /* def c1, 0, 0, 0, 1 */
            0x00000042u, 0xb00f0000u,   /* tex t0 */
            0x00000005u, 0x800f0000u, 0xb0e40000u, 0x90e40000u,   /* mul r0, t0, v0 */
            0x00000005u, 0x800f0000u, 0x80e40000u, 0xa0e40002u,   /* mul r0, r0, c2 */
            0x00000002u, 0x80070000u, 0x80e40000u, 0xa0e40001u,   /* add r0.rgb, r0, c1 */
            0x40000001u, 0x80080000u, 0xa0ff0001u,                /* + mov r0.a, c1.a */
            0x0000ffffu};
#endif
        static const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        U shader = 0;
        value = ((M4)dvt[SetPixelShaderConstant])(device, 2, (U)white, 1);
        number("D3DPYR shader_constant", value);
        if (value) fail(25);
        value = ((M3)dvt[CreatePixelShader])(device, (U)code, (U)&shader);
        number("D3DPYR pixel_shader", value);
        if (value || !shader) fail(26);
        value = ((M2)dvt[SetPixelShader])(device, shader);
        number("D3DPYR set_pixel_shader", value);
        if (value) fail(27);
        /* The texture stages would draw white (D3DTOP_SELECTARG2 of the
           diffuse colour); only the shader gives the faces their colours. */
        ((M4)dvt[SetTextureStageState])(device, 0, 1, 3);
    }
#endif
#ifdef DECL
    /* The fixed function pipeline fed through a vertex declaration instead
       of an FVF: position FLOAT3 and colour D3DCOLOR. */
    {
#ifdef DX9
        static const unsigned short elements[12] = {0, VERTEX_PAD, 0x0002, 0x0000, 0, VERTEX_PAD + 12, 0x0004, 0x000a, 0xff, 0, 17, 0};
        void *declaration = 0;
        value = ((M3)dvt[CreateVertexDeclaration])(device, (U)elements, (U)&declaration);
        number("D3DPYR vertex_declaration", value);
        if (value || !declaration) fail(28);
        value = ((M2)dvt[SetVertexDeclaration])(device, (U)declaration);
#else
        static const U declaration[] = {0x20000000u,
#if VERTEX_PAD
            0x50000000u | ((VERTEX_PAD / 4) << 16),
#endif
            0x40020000u, 0x40040005u, 0xffffffffu};
        U program = 0;
        value = ((M5)dvt[CreateVertexShader])(device, (U)declaration, 0, (U)&program, 0);
        number("D3DPYR vertex_shader", value);
        if (value || !program) fail(29);
        value = ((M2)dvt[SetVertexShader])(device, program);
#endif
        number("D3DPYR set_vertex_shader", value);
        if (value) fail(30);
    }
#endif
#ifdef VSHADER
    /* A vertex shader of version 1.1: m4x4 oPos, v0, c0 with the transposed
       world, view and projection in c0 to c3, and the vertex colour. With
       VSHADER 20 one of version 2.0 that also passes the texture coordinates
       and sums c10 to c12, a third each, in a loop over aL into the factor
       of the colour. */
    {
#ifdef DX9
#if VSHADER == 20
        static const U code[] = {
            0xfffe0200u,
            0x0200001fu, 0x80000000u, 0x900f0000u,   /* dcl_position v0 */
            0x0200001fu, 0x8000000au, 0x900f0001u,   /* dcl_color v1 */
            0x0200001fu, 0x80000005u, 0x900f0002u,   /* dcl_texcoord v2 */
            0x05000051u, 0xa00f000au, 0x3eaaaaabu, 0x3eaaaaabu, 0x3eaaaaabu, 0x3eaaaaabu,   /* def c10 */
            0x05000051u, 0xa00f000bu, 0x3eaaaaabu, 0x3eaaaaabu, 0x3eaaaaabu, 0x3eaaaaabu,   /* def c11 */
            0x05000051u, 0xa00f000cu, 0x3eaaaaabu, 0x3eaaaaabu, 0x3eaaaaabu, 0x3eaaaaabu,   /* def c12 */
            0x05000030u, 0xf00f0000u, 3u, 10u, 1u, 0u,   /* defi i0, 3, 10, 1, 0 */
            0x03000014u, 0xc00f0000u, 0x90e40000u, 0xa0e40000u,   /* m4x4 oPos, v0, c0 */
            0x02000001u, 0x800f0001u, 0xa0e4000du,               /* mov r1, c13 (zero) */
            0x0200001bu, 0xf0e40800u, 0xf0e40000u,               /* loop aL, i0 */
            0x04000002u, 0x800f0001u, 0x80e40001u, 0xa0e42000u, 0xf0e40800u,   /* add r1, r1, c[aL] */
            0x0000001du,                                         /* endloop */
            0x03000005u, 0xd00f0000u, 0x90e40001u, 0x80e40001u,  /* mul oD0, v1, r1 */
            0x02000001u, 0xe0030000u, 0x90e40002u,               /* mov oT0.xy, v2 */
            0x0000ffffu};
        /* D3DVERTEXELEMENT9: position FLOAT3, colour D3DCOLOR, coordinates FLOAT2, end. */
        static const unsigned short elements[16] = {0, 0, 0x0002, 0x0000, 0, 12, 0x0004, 0x000a, 0, 16, 0x0001, 0x0005,
                                                    0xff, 0, 17, 0};
#else
        static const U code[] = {
            0xfffe0101u,
            0x0000001fu, 0x80000000u, 0x900f0000u,   /* dcl_position v0 */
            0x0000001fu, 0x8000000au, 0x900f0005u,   /* dcl_color v5 */
            0x00000014u, 0xc00f0000u, 0x90e40000u, 0xa0e40000u,   /* m4x4 oPos, v0, c0 */
            0x00000001u, 0xd00f0000u, 0x90e40005u,   /* mov oD0, v5 */
            0x0000ffffu};
        /* D3DVERTEXELEMENT9: position FLOAT3 at 0, colour D3DCOLOR at 12, end. */
        static const unsigned short elements[12] = {0, VERTEX_PAD, 0x0002, 0x0000, 0, VERTEX_PAD + 12, 0x0004, 0x000a, 0xff, 0, 17, 0};
#endif
        void *declaration = 0, *program = 0;
        value = ((M3)dvt[CreateVertexDeclaration])(device, (U)elements, (U)&declaration);
        number("D3DPYR vertex_declaration", value);
        if (value || !declaration) fail(28);
        value = ((M3)dvt[CreateVertexShader])(device, (U)code, (U)&program);
        number("D3DPYR vertex_shader", value);
        if (value || !program) fail(29);
        ((M2)dvt[SetVertexDeclaration])(device, (U)declaration);
        value = ((M2)dvt[SetVertexShader])(device, (U)program);
#else
        static const U declaration[] = {0x20000000u,
#if VERTEX_PAD
            0x50000000u | ((VERTEX_PAD / 4) << 16),
#endif
            0x40020000u, 0x40040005u, 0xffffffffu};
        static const U code[] = {
            0xfffe0101u,
            0x00000014u, 0xc00f0000u, 0x90e40000u, 0xa0e40000u,   /* m4x4 oPos, v0, c0 */
            0x00000001u, 0xd00f0000u, 0x90e40005u,   /* mov oD0, v5 */
            0x0000ffffu};
        U program = 0;
        value = ((M5)dvt[CreateVertexShader])(device, (U)declaration, (U)code, (U)&program, 0);
        number("D3DPYR vertex_shader", value);
        if (value || !program) fail(29);
        value = ((M2)dvt[SetVertexShader])(device, program);
#endif
        number("D3DPYR set_vertex_shader", value);
        if (value) fail(30);
    }
#endif
#ifdef QUERY
    /* An event query, issued after every frame and asked with
       D3DGETDATA_FLUSH until it is signalled; occlusion is not offered. */
    value = ((M3)dvt[CreateQuery])(device, 9, 0);   /* D3DQUERYTYPE_OCCLUSION */
    number("D3DPYR occlusion_query", value);
    value = ((M3)dvt[CreateQuery])(device, 8, (U)&query);   /* D3DQUERYTYPE_EVENT */
    number("D3DPYR event_query", value);
    if (value || !query) fail(35);
#endif
#ifdef BLIT
    value = ((M9)dvt[CreateRenderTarget])(device, 64, 64, 22, 0, 0, 0, (U)&blit_target, 0);
    number("D3DPYR render_target", value);
    if (value || !blit_target) fail(34);
#endif

    for (frame = 0; frame < 90; ++frame)
    {
        angle = (frame >= 88 ? 30.0f : (float)(frame * 4 % 360)) * (PI / 180.0f);
        rotation_y(world, angle);
#ifdef VSHADER
        multiply(world, view_matrix, combined);
        multiply(combined, projection_matrix, matrix);
        for (i = 0; i < 16; ++i) rows[i] = matrix[(i % 4) * 4 + i / 4];
        value = ((M4)dvt[SetVertexShaderConstant])(device, 0, (U)rows, 4);
        if (value) { number("D3DPYR vertex_constants", value); fail(31); }
#endif
        value = ((M3)dvt[SetTransform])(device, 256, (U)world);   /* D3DTS_WORLD */
        if (value) { number("D3DPYR world", value); fail(9); }
        {
            U one = 0x3f800000u;
#ifdef BLIT
            /* The background out of blits: black from Clear, then a small
               render target filled with it and stretched over the back
               buffer. */
            value = ((M7)dvt[Clear])(device, 0, 0, 0x3, 0xff000000u, one, 0);
            if (!value) value = ((M4)dvt[ColorFill])(device, (U)blit_target, 0, 0xff000040u);
            if (value) { number("D3DPYR color_fill", value); fail(32); }
            {
                void *back = 0;
                value = ((M5)dvt[GetBackBuffer])(device, 0, 0, 0, (U)&back);
                if (!value) value = ((M6)dvt[StretchRect])(device, (U)blit_target, 0, (U)back, 0, 0);
                if (value) { number("D3DPYR stretch_rect", value); fail(33); }
                ((M1)(*(U ***)back)[2])(back);
            }
#else
            value = ((M7)dvt[Clear])(device, 0, 0, 0x3, 0xff000040u, one, 0);
#endif
        }
        if (value) { number("D3DPYR clear", value); fail(10); }
        value = ((M1)dvt[BeginScene])(device);
        if (value) { number("D3DPYR beginscene", value); fail(11); }
        /* The two visible faces, front and right, come out of the buffers
           with BUFFERS and out of user memory without; the three hidden
           parts take the other way, so every path runs in both builds. */
#ifdef BUFFERS
        if (!draw_buffers(0, 1, 2)) fail(19);
        if (!draw_user(2, 3, 0)) fail(12);
#else
        if (!draw_user(0, 1, 1)) fail(12);
        if (!draw_buffers(2, 3, 0)) fail(19);
#endif

        value = ((M1)dvt[EndScene])(device);
        if (value) { number("D3DPYR endscene", value); fail(13); }
#ifdef QUERY
        {
            U **qvt = *(U ***)query, signalled = 0, asks = 0;
            value = ((M2)qvt[QueryIssue])(query, 1);   /* D3DISSUE_END */
            if (value) { number("D3DPYR query_issue", value); fail(36); }
            do value = ((M4)qvt[QueryGetData])(query, (U)&signalled, 4, 1);
            while (value == 1 && ++asks < 100000);
            if (value || !signalled) { number("D3DPYR query_data", value); fail(37); }
            if (asks > query_asks) query_asks = asks;
        }
#endif
        value = ((M5)dvt[Present])(device, 0, 0, 0, 0);
        if (value) { number("D3DPYR present", value); fail(14); }
    }
#ifdef QUERY
    number("D3DPYR query_asks", query_asks);
#endif
    number("D3DPYR frames", frame);
    number("D3DPYR done", 1);
#ifdef HOLD_FRAME
    Sleep(10000);
#endif
    ExitProcess(0);
}
