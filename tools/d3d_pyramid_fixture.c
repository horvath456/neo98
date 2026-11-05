/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest side of tools/test_direct3d_guest.py: a Win32 program that draws a
   turning pyramid with Direct3D 7 through the original DDRAW.DLL and
   D3DIM700.DLL, on the Direct3D HAL of the NEO98 display driver.

   DirectDrawCreateEx, exclusive full screen 640x480x32, a flipping primary
   with one back buffer and a 16-bit Z buffer, IDirect3D7 from the DirectDraw
   object, IDirect3DDevice7 as the HAL device on the back buffer, world, view
   and projection transforms, Clear, DrawPrimitive with XYZ|DIFFUSE vertices,
   Flip. Lighting is off, so every face keeps the colour of its vertices, and
   the test can check exact colours in the host image.

   Without INTERACTIVE the program draws a fixed number of frames, ends on a
   known angle and exits without restoring the display mode, so the
   screenshot shows the last frame. With INTERACTIVE it turns until Escape or
   the close box, then restores the mode. With TNL it asks for the
   transform-and-lighting device instead of the HAL device, so the same
   picture comes out of the driver's own vertex pipeline. With FRAMETIMES it
   also keeps its frame times (tools/pause_frame_times.h). Every answer goes
   out through
   OutputDebugStringA as "D3DPYR name=value". */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U LoadLibraryA(const char *);
API U GetProcAddress(U, const char *);
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API void Sleep(U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, int);
API U SetForegroundWindow(U);
API U GetSystemMetrics(int);
API U PeekMessageA(void *, U, U, U, U);
API U TranslateMessage(const void *);
API U DispatchMessageA(const void *);
API void PostQuitMessage(int);
API short GetAsyncKeyState(int);
#ifdef FRAMETIMES
#include "pause_frame_times.h"
#endif

/* The MSVC target wants this symbol from any program that uses floating point. */
int _fltused = 1;

typedef U(__stdcall *CreateEx)(void *, void **, const void *, void *);
typedef U(__stdcall *M1)(void *);
typedef U(__stdcall *M2)(void *, U);
typedef U(__stdcall *M3)(void *, U, U);
typedef U(__stdcall *M4)(void *, U, U, U);
typedef U(__stdcall *M5)(void *, U, U, U, U);
typedef U(__stdcall *M6)(void *, U, U, U, U, U);
typedef U(__stdcall *M7)(void *, U, U, U, U, U, U);

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

static const unsigned char IID_IDirectDraw7[16] = {0xc0, 0x5e, 0xe6, 0x15, 0x9c, 0x3b, 0xd2, 0x11, 0xb9, 0x2f, 0x00, 0x60, 0x97, 0x97, 0xea, 0x5b};
static const unsigned char IID_IDirect3D7[16] = {0x77, 0x9e, 0x04, 0xf5, 0x61, 0x48, 0xd2, 0x11, 0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8};
#ifdef TNL
/* IID_IDirect3DTnLHalDevice: the driver transforms and lights on the GPU. */
static const unsigned char IID_IDirect3DHALDevice[16] = {0x78, 0x9e, 0x04, 0xf5, 0x61, 0x48, 0xd2, 0x11, 0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8};
#else
static const unsigned char IID_IDirect3DHALDevice[16] = {0xe0, 0x3d, 0xe6, 0x84, 0xaa, 0x46, 0xcf, 0x11, 0x81, 0x6f, 0x00, 0x00, 0xc0, 0x20, 0x15, 0x6e};
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
struct Vertex { float x, y, z; U color; };
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
}

static U desc[31], caps_query[4], zdesc[31];
static float matrix[16], world[16];
static U viewport[6];
static U message[7];
static int quit = 0;

static U __stdcall window_procedure(U window, U msg, U wp, U lp)
{
    if (msg == 0x0010 || msg == 0x0002) { quit = 1; PostQuitMessage(0); return 0; }   /* WM_CLOSE, WM_DESTROY */
    return DefWindowProcA(window, msg, wp, lp);
}

void __stdcall mainCRTStartup(void)
{
    U library, window, klass, value, i, frame;
    U class_fields[10];
    void *ddraw = 0, *primary = 0, *back = 0, *zbuffer = 0, *d3d = 0, *device = 0;
    U **vt, **svt, **dvt;
    float angle;

    library = LoadLibraryA("DDRAW.DLL");
    number("D3DPYR ddraw_loaded", library ? 1 : 0);
    if (!library) fail(1);
    {
        CreateEx create = (CreateEx)GetProcAddress(library, "DirectDrawCreateEx");
        if (!create) fail(2);
        value = create(0, &ddraw, IID_IDirectDraw7, 0);
        number("D3DPYR create", value);
        if (value || !ddraw) fail(3);
    }
    vt = *(U ***)ddraw;

    for (i = 0; i < 10; ++i) class_fields[i] = 0;
    class_fields[1] = (U)window_procedure;
    class_fields[7] = 6;
    class_fields[9] = (U) "D3DPyramid";
    klass = RegisterClassA(class_fields);
    window = CreateWindowExA(0, "D3DPyramid", "Direct3D Pyramide", 0x90000000u, 0, 0,
                             GetSystemMetrics(0), GetSystemMetrics(1), 0, 0, 0, 0);
    number("D3DPYR window", window ? 1 : 0);
    if (!window) fail(4);
    ShowWindow(window, 5);
    SetForegroundWindow(window);

    /* DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN | DDSCL_ALLOWREBOOT */
    value = ((M3)vt[20])(ddraw, window, 0x13);
    number("D3DPYR cooperative", value);
    if (value) fail(5);
    value = ((M6)vt[21])(ddraw, 640, 480, 32, 0, 0);
    number("D3DPYR setmode", value);
    if (value) fail(6);

    /* The flipping primary with one back buffer, both 3D capable:
       DDSD_CAPS | DDSD_BACKBUFFERCOUNT, DDSCAPS_PRIMARYSURFACE | FLIP |
       COMPLEX | 3DDEVICE. */
    for (i = 0; i < 31; ++i) desc[i] = 0;
    desc[0] = 0x7c;
    desc[1] = 0x21;
    desc[5] = 1;
    desc[26] = 0x200 | 0x10 | 0x8 | 0x2000;
    value = ((M4)vt[6])(ddraw, (U)desc, (U)&primary, 0);
    number("D3DPYR primary", value);
    if (value || !primary) fail(7);
    svt = *(U ***)primary;
    caps_query[0] = 0x4; caps_query[1] = caps_query[2] = caps_query[3] = 0;
    value = ((M3)svt[12])(primary, (U)caps_query, (U)&back);
    number("D3DPYR backbuffer", value);
    if (value || !back) fail(8);

    /* A 16-bit Z buffer in video memory, attached to the back buffer. */
    for (i = 0; i < 31; ++i) zdesc[i] = 0;
    zdesc[0] = 0x7c;
    zdesc[1] = 0x1 | 0x2 | 0x4 | 0x1000;   /* CAPS, HEIGHT, WIDTH, PIXELFORMAT */
    zdesc[2] = 480;
    zdesc[3] = 640;
    zdesc[18] = 32;         /* ddpfPixelFormat.dwSize */
    zdesc[19] = 0x400;      /* DDPF_ZBUFFER */
    zdesc[21] = 16;         /* dwZBufferBitDepth */
    zdesc[23] = 0xffff;     /* dwZBitMask */
    zdesc[26] = 0x20000 | 0x4000;   /* DDSCAPS_ZBUFFER | DDSCAPS_VIDEOMEMORY */
    value = ((M4)vt[6])(ddraw, (U)zdesc, (U)&zbuffer, 0);
    number("D3DPYR zbuffer", value);
    if (value || !zbuffer) fail(9);
    value = ((M2)(*(U ***)back)[3])(back, (U)zbuffer);
    number("D3DPYR attach_z", value);
    if (value) fail(10);

    /* IDirect3D7 out of the DirectDraw object, the HAL device on the back buffer. */
    value = ((M3)vt[0])(ddraw, (U)IID_IDirect3D7, (U)&d3d);
    number("D3DPYR d3d", value);
    if (value || !d3d) fail(11);
    value = ((M4)(*(U ***)d3d)[4])(d3d, (U)IID_IDirect3DHALDevice, (U)back, (U)&device);
    number("D3DPYR device", value);
    if (value || !device) fail(12);
    dvt = *(U ***)device;

    viewport[0] = 0; viewport[1] = 0; viewport[2] = 640; viewport[3] = 480;
    *(float *)&viewport[4] = 0.0f; *(float *)&viewport[5] = 1.0f;
    value = ((M2)dvt[13])(device, (U)viewport);
    number("D3DPYR viewport", value);
    if (value) fail(13);
    value = ((M3)dvt[20])(device, 137, 0);   /* D3DRENDERSTATE_LIGHTING off */
    number("D3DPYR lighting", value);
    value = ((M3)dvt[20])(device, 7, 1);     /* D3DRENDERSTATE_ZENABLE */
    look_at(matrix, 0.0f, 1.2f, -4.0f);
    value = ((M3)dvt[11])(device, 2, (U)matrix);     /* D3DTRANSFORMSTATE_VIEW */
    number("D3DPYR view", value);
    perspective(matrix, PI / 3, 640.0f / 480.0f, 1.0f, 100.0f);
    value = ((M3)dvt[11])(device, 3, (U)matrix);     /* D3DTRANSFORMSTATE_PROJECTION */
    number("D3DPYR projection", value);
    build_pyramid();

    frame = 0;
    for (;;)
    {
#ifdef INTERACTIVE
        while (PeekMessageA(message, 0, 0, 0, 1))
        {
            if (message[1] == 0x0012) quit = 1;   /* WM_QUIT */
            TranslateMessage(message);
            DispatchMessageA(message);
        }
        if (quit || (GetAsyncKeyState(0x1b) & 0x8000)) break;
#ifdef FRAMETIMES
        frame_time();
#endif
        angle = (float)(frame % 360) * (PI / 180.0f);
#else
        if (frame == 90) break;
        /* The last two frames stand on 30 degrees, the angle the test predicts,
           so both buffers of the flipping chain hold that picture. */
        angle = (frame >= 88 ? 30.0f : (float)(frame * 4 % 360)) * (PI / 180.0f);
#endif
        rotation_y(world, angle);
        value = ((M3)dvt[11])(device, 1, (U)world);   /* D3DTRANSFORMSTATE_WORLD, 1 in DirectX 7 */
        if (value) { number("D3DPYR world", value); fail(14); }
        /* Clear target and Z: dark blue, depth 1.0. */
        {
            U one = 0x3f800000u;
            value = ((M7)dvt[10])(device, 0, 0, 0x3, 0xff000040u, one, 0);
        }
        if (value) { number("D3DPYR clear", value); fail(15); }
        value = ((M1)dvt[5])(device);
        if (value) { number("D3DPYR beginscene", value); fail(16); }
        value = ((M6)dvt[25])(device, 4, 0x42, (U)pyramid, 18, 0);   /* D3DPT_TRIANGLELIST, XYZ|DIFFUSE */
        if (value) { number("D3DPYR draw", value); fail(17); }
        value = ((M1)dvt[6])(device);
        if (value) { number("D3DPYR endscene", value); fail(18); }
        value = ((M3)svt[11])(primary, 0, 1);   /* Flip with DDFLIP_WAIT */
        if (value) { number("D3DPYR flip", value); fail(19); }
        ++frame;
    }
    number("D3DPYR frames", frame);
#ifdef INTERACTIVE
    ((M1)vt[19])(ddraw);   /* RestoreDisplayMode */
#endif
    number("D3DPYR done", 1);
    ExitProcess(0);
}
