/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exercise the original D3DIM700 RGB device without a host GPU. Every frame
   checks the back buffer before Flip and the front buffer afterwards. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U LoadLibraryA(const char *);
API U GetProcAddress(U, const char *);
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
int _fltused = 1;
#ifndef DEPTH
#define DEPTH 32
#endif
typedef U (__stdcall *CreateEx)(void *, void **, const void *, void *);
typedef U (__stdcall *M1)(void *);
typedef U (__stdcall *M2)(void *, U);
typedef U (__stdcall *M3)(void *, U, U);
typedef U (__stdcall *M4)(void *, U, U, U);
typedef U (__stdcall *M5)(void *, U, U, U, U);
typedef U (__stdcall *M6)(void *, U, U, U, U, U);
typedef U (__stdcall *M7)(void *, U, U, U, U, U, U);
static const unsigned char draw_iid[16] = {0xc0,0x5e,0xe6,0x15,0x9c,0x3b,0xd2,0x11,0xb9,0x2f,0,0x60,0x97,0x97,0xea,0x5b};
static const unsigned char d3d_iid[16] = {0x77,0x9e,0x04,0xf5,0x61,0x48,0xd2,0x11,0xa4,7,0,0xa0,0xc9,6,0x29,0xa8};
static const unsigned char rgb_iid[16] = {0x60,0x5c,0x66,0xa4,0x73,0x26,0xcf,0x11,0xa3,0x1a,0,0xaa,0,0xb9,0x33,0x56};
static char line[96];
static void number(const char *name, U value)
{
    static const char digits[] = "0123456789abcdef";
    U n = 0, i = 0;
    while (name[i]) line[n++] = name[i++];
    line[n++] = '=';
    for (i = 0; i < 8; ++i) line[n++] = digits[(value >> (28 - i * 4)) & 15];
    line[n] = 0;
    OutputDebugStringA(line);
}
static void require(U value, U step)
{
    if (!value) return;
    number("RGB failure", step);
    number("RGB result", value);
    ExitProcess(step);
}
static U __stdcall procedure(U h, U m, U w, U l) { return DefWindowProcA(h, m, w, l); }
static void inspect(void *surface, U background, U triangle, U step)
{
    U desc[31], i, **v = *(U ***)surface;
    for (i = 0; i < 31; ++i) desc[i] = 0;
    desc[0] = 124;
    require(((M5)v[25])(surface, 0, (U)desc, 0x11, 0), step);
    U bits = desc[9], pitch = desc[4];
    U corner = DEPTH == 16 ? *(unsigned short *)bits : *(U *)bits;
    U center = DEPTH == 16 ? *(unsigned short *)(bits + 128 * pitch + 128 * 2)
                          : *(U *)(bits + 128 * pitch + 128 * 4);
    if (corner != background || center != triangle)
    {
        number("RGB corner", corner);
        number("RGB triangle", center);
        require(1, step + 1);
    }
    require(((M2)v[32])(surface, 0), step + 2);
}
void mainCRTStartup(void)
{
    static U klass[10], desc[31], caps[4], viewport[6];
    struct Vertex { float x, y, z, rhw; U color; };
    static struct Vertex vertices[3] = {
        {64,64,.5f,1,0xffff0000}, {576,64,.5f,1,0xffff0000}, {64,416,.5f,1,0xffff0000}};
    void *draw = 0, *primary = 0, *back = 0, *d3d = 0, *device = 0;
    U library = LoadLibraryA("DDRAW.DLL"), window, frame, **v, **s, **d;
    require(!library, 1);
    CreateEx create = (CreateEx)GetProcAddress(library, "DirectDrawCreateEx");
    require(!create, 2);
    require(create(0, &draw, draw_iid, 0), 3);
    v = *(U ***)draw;
    klass[1] = (U)procedure; klass[9] = (U)"RGBFlip";
    require(!RegisterClassA(klass), 4);
    window = CreateWindowExA(0, "RGBFlip", "RGB software rendering", 0x90000000u,
                             0, 0, 800, 600, 0, 0, 0, 0);
    require(!window, 5);
    require(((M3)v[20])(draw, window, 0x13), 6);
    require(((M6)v[21])(draw, 640, 480, DEPTH, 0, 0), 7);
    desc[0] = 124; desc[1] = 0x21; desc[5] = 1;
    desc[26] = 0x200 | 0x10 | 0x8 | 0x2000;
    require(((M4)v[6])(draw, (U)desc, (U)&primary, 0), 8);
    s = *(U ***)primary;
    caps[0] = 4;
    require(((M3)s[12])(primary, (U)caps, (U)&back), 9);
    require(((M2)(*(U ***)back)[14])(back, (U)caps), 10);
    number("RGB back_caps", caps[0]);
    require(((M3)v[0])(draw, (U)d3d_iid, (U)&d3d), 11);
    require(((M4)(*(U ***)d3d)[4])(d3d, (U)rgb_iid, (U)back, (U)&device), 12);
    d = *(U ***)device;
    viewport[2] = 640; viewport[3] = 480; viewport[5] = 0x3f800000;
    require(((M2)d[13])(device, (U)viewport), 13);
    require(((M3)d[20])(device, 137, 0), 14); /* LIGHTING */
    require(((M3)d[20])(device, 7, 0), 15);   /* ZENABLE */
    require(((M3)d[20])(device, 22, 1), 16);  /* CULL_NONE */
    for (frame = 0; frame < 30; ++frame)
    {
        U green = frame & 1, i;
        U background = green ? 0xff00ff00u : 0xff0000ffu;
        U triangle = green ? 0xffff0000u : 0xffffff00u;
        U packed_background = DEPTH == 16 ? (green ? 0x3e0 : 0x1f) : background & 0xffffff;
        U packed_triangle = DEPTH == 16 ? (green ? 0x7c00 : 0x7fe0) : triangle & 0xffffff;
        for (i = 0; i < 3; ++i) vertices[i].color = triangle;
        require(((M7)d[10])(device, 0, 0, 1, background, 0x3f800000, 0), 17);
        require(((M1)d[5])(device), 18);
        require(((M6)d[25])(device, 4, 0x44, (U)vertices, 3, 0), 19);
        require(((M1)d[6])(device), 20);
        inspect(back, packed_background, packed_triangle, 21);
        require(((M3)s[11])(primary, 0, 1), 24);
        inspect(primary, packed_background, packed_triangle, 25);
    }
    number("RGB frames", frame);
    ExitProcess(0);
}
