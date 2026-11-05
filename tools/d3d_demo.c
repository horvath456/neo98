/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DEMO3D.EXE: a Direct3D 7 demo for a person at the host, built by
   tools/d3d_demo.py. It runs through the original DDRAW.DLL and D3DIM700.DLL
   on the Direct3D HAL of the NEO98 display driver and shows its frame rate.

   The scene uses what a DirectX 7 game of 1999 used: a lit, textured torus
   with a specular highlight, a ring of lit cubes, a floor with a mipmapped
   (DXT1 when offered) texture under linear vertex fog that mirrors the torus
   through the stencil buffer, additive particles, and a text overlay whose
   glyphs GDI drew into a texture through IDirectDrawSurface7::GetDC.
   D3DIM transforms and lights every vertex itself (the HAL is not a
   transform-and-lighting device); the driver rasterizes on Metal.

   Command line, all optional: 16 or 32 for the colour depth, WIDTHxHEIGHT for
   the mode, frames=N to stop after N frames (for a measurement run), detail=N
   for the torus (0 low, 1 medium, 2 high), novsync to flip without waiting
   for the vertical blank, hal to take the rasterizing HAL device even when
   the driver offers transform and lighting, still to draw one fixed picture
   (for comparing the two devices), wbuffer to start with the W buffer,
   plain to start without cube map and bump map, nodxt to keep the floor texture
   uncompressed, window to draw into a 480x360 window on the desktop instead
   of taking the screen.

   Keys: Esc quits, V toggles waiting for the vertical blank, F the fog,
   T the torus detail, G the gamma ramp (100, 150, 70 percent), E the cube map
   reflection, B the bump map, W the W buffer, Space pauses
   the motion. gamma=N on the command line starts with that ramp.

   Every second the program writes "D3DDEMO fps=..." through
   OutputDebugStringA, so a run in the probe runner can be measured. */
typedef unsigned U;
typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API U LoadLibraryA(const char *);
API U GetProcAddress(U, const char *);
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API const char *GetCommandLineA(void);
API U timeGetTime(void);
API U timeBeginPeriod(U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, int);
API U SetForegroundWindow(U);
API U SetCursor(U);
API U PeekMessageA(void *, U, U, U, U);
API U TranslateMessage(const void *);
API U DispatchMessageA(const void *);
API void PostQuitMessage(int);
API U MessageBoxA(U, const char *, const char *, U);
API U AdjustWindowRect(void *, U, U);
API U GetClientRect(U, void *);
API U ClientToScreen(U, void *);
API U GetStockObject(int);
API U SelectObject(U, U);
API U SetTextColor(U, U);
API U SetBkColor(U, U);
API int SetBkMode(U, int);
API U TextOutA(U, int, int, const char *, int);
API U GetTextMetricsA(U, void *);

/* The MSVC target wants this symbol from any program that uses floating point. */
int _fltused = 1;
void *memset(void *d, int c, unsigned n)
{
    unsigned char *p = (unsigned char *)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}
void *memcpy(void *d, const void *s, unsigned n)
{
    unsigned char *p = (unsigned char *)d;
    const unsigned char *q = (const unsigned char *)s;
    while (n--) *p++ = *q++;
    return d;
}

typedef U(__stdcall *CreateEx)(void *, void **, const void *, void *);
typedef U(__stdcall *M1)(void *);
typedef U(__stdcall *M2)(void *, U);
typedef U(__stdcall *M3)(void *, U, U);
typedef U(__stdcall *M4)(void *, U, U, U);
typedef U(__stdcall *M5)(void *, U, U, U, U);
typedef U(__stdcall *M6)(void *, U, U, U, U, U);
typedef U(__stdcall *M7)(void *, U, U, U, U, U, U);
typedef U(__stdcall *M8)(void *, U, U, U, U, U, U, U);
#define CALL(obj, index, type) ((type)(*(U ***)(obj))[index])

/* ---- text and numbers without a C library ---- */
static char *put_text(char *at, const char *text)
{
    while (*text) *at++ = *text++;
    *at = 0;
    return at;
}
static char *put_number(char *at, U value)
{
    char digits[12];
    int n = 0;
    do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) *at++ = digits[--n];
    *at = 0;
    return at;
}
/* value/10 with one decimal, value given in tenths */
static char *put_tenths(char *at, U tenths)
{
    at = put_number(at, tenths / 10);
    *at++ = '.';
    *at++ = (char)('0' + tenths % 10);
    *at = 0;
    return at;
}
static void say(const char *text)
{
    char line[160];
    put_text(put_text(line, "D3DDEMO "), text);
    OutputDebugStringA(line);
}
static void fail(const char *step, U value)
{
    char line[160], *at = put_text(line, step);
    static const char hex[] = "0123456789abcdef";
    int i;
    at = put_text(at, " failed, hr=0x");
    for (i = 0; i < 8; ++i) *at++ = hex[(value >> (28 - i * 4)) & 15];
    *at = 0;
    say(line);
    MessageBoxA(0, line, "Direct3D demo", 0x10);
    ExitProcess(1);
}

/* A step that may fail on some drivers: said, not fatal. */
static void report(const char *step, U value)
{
    char line[160], *at = put_text(line, step);
    static const char hex[] = "0123456789abcdef";
    int i;
    at = put_text(at, " refused, hr=0x");
    for (i = 0; i < 8; ++i) *at++ = hex[(value >> (28 - i * 4)) & 15];
    *at = 0;
    say(line);
}

/* ---- math ---- */
static const float PI = 3.14159265f;
static float fsin(float x) { float r; __asm__("fsin" : "=t"(r) : "0"(x)); return r; }
static float fcos(float x) { float r; __asm__("fcos" : "=t"(r) : "0"(x)); return r; }
static float fsqrt(float x) { float r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }

typedef struct { float m[16]; } Matrix;
static void identity(Matrix *m)
{
    int i;
    for (i = 0; i < 16; ++i) m->m[i] = 0.0f;
    m->m[0] = m->m[5] = m->m[10] = m->m[15] = 1.0f;
}
static void multiply(Matrix *out, const Matrix *a, const Matrix *b)
{
    Matrix r;
    int i, j, k;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 4; ++j)
        {
            float s = 0.0f;
            for (k = 0; k < 4; ++k) s += a->m[i * 4 + k] * b->m[k * 4 + j];
            r.m[i * 4 + j] = s;
        }
    *out = r;
}
static void rotation_x(Matrix *m, float a)
{
    const float c = fcos(a), s = fsin(a);
    identity(m);
    m->m[5] = c; m->m[6] = s;
    m->m[9] = -s; m->m[10] = c;
}
static void rotation_y(Matrix *m, float a)
{
    const float c = fcos(a), s = fsin(a);
    identity(m);
    m->m[0] = c; m->m[2] = -s;
    m->m[8] = s; m->m[10] = c;
}
static void rotation_z(Matrix *m, float a)
{
    const float c = fcos(a), s = fsin(a);
    identity(m);
    m->m[0] = c; m->m[1] = s;
    m->m[4] = -s; m->m[5] = c;
}
static void translation(Matrix *m, float x, float y, float z)
{
    identity(m);
    m->m[12] = x; m->m[13] = y; m->m[14] = z;
}
static void scaling(Matrix *m, float s)
{
    identity(m);
    m->m[0] = m->m[5] = m->m[10] = s;
}
/* Left-handed look-at, up is +y. Also returns the camera's right and up
   vectors for the particle billboards. */
static float cam_right[3], cam_up[3];
static void look_at(Matrix *m, float ex, float ey, float ez, float tx, float ty, float tz)
{
    float zx = tx - ex, zy = ty - ey, zz = tz - ez, len = fsqrt(zx * zx + zy * zy + zz * zz);
    float xx, xy, xz, yx, yy, yz, xlen;
    zx /= len; zy /= len; zz /= len;
    xx = zz; xy = 0.0f; xz = -zx;
    xlen = fsqrt(xx * xx + xz * xz);
    xx /= xlen; xz /= xlen;
    yx = zy * xz - zz * xy; yy = zz * xx - zx * xz; yz = zx * xy - zy * xx;
    identity(m);
    m->m[0] = xx; m->m[1] = yx; m->m[2] = zx;
    m->m[4] = xy; m->m[5] = yy; m->m[6] = zy;
    m->m[8] = xz; m->m[9] = yz; m->m[10] = zz;
    m->m[12] = -(xx * ex + xy * ey + xz * ez);
    m->m[13] = -(yx * ex + yy * ey + yz * ez);
    m->m[14] = -(zx * ex + zy * ey + zz * ez);
    cam_right[0] = xx; cam_right[1] = xy; cam_right[2] = xz;
    cam_up[0] = yx; cam_up[1] = yy; cam_up[2] = yz;
}
static void perspective(Matrix *m, float fov, float aspect, float zn, float zf)
{
    const float h = fcos(fov / 2) / fsin(fov / 2), w = h / aspect, q = zf / (zf - zn);
    int i;
    for (i = 0; i < 16; ++i) m->m[i] = 0.0f;
    m->m[0] = w; m->m[5] = h; m->m[10] = q; m->m[11] = 1.0f; m->m[14] = -q * zn;
}
static U random_state = 12345;
static float random01(void)
{
    random_state = random_state * 1103515245u + 12345u;
    return (float)((random_state >> 8) & 0xffff) / 65535.0f;
}

/* ---- geometry ---- */
/* D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1 */
#define FVF_LIT_TEX 0x112
typedef struct { float x, y, z, nx, ny, nz, u, v; } LitVertex;
/* D3DFVF_XYZ | D3DFVF_NORMAL */
#define FVF_LIT 0x12
typedef struct { float x, y, z, nx, ny, nz; } PlainVertex;
/* D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1 */
#define FVF_COLOR_TEX 0x142
typedef struct { float x, y, z; U color; float u, v; } ColorVertex;
/* D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 */
#define FVF_SCREEN 0x144
typedef struct { float x, y, z, rhw; U color; float u, v; } ScreenVertex;

#define TORUS_MAX_RINGS 96
#define TORUS_MAX_SIDES 40
static LitVertex torus_vertices[(TORUS_MAX_RINGS + 1) * (TORUS_MAX_SIDES + 1)];
static W torus_indices[TORUS_MAX_RINGS * TORUS_MAX_SIDES * 6];
static U torus_vertex_count, torus_index_count;
static void build_torus(int rings, int sides)
{
    const float R = 1.0f, r = 0.42f;
    int i, j;
    U n = 0;
    for (i = 0; i <= rings; ++i)
    {
        const float a = (float)i / rings * 2 * PI, ca = fcos(a), sa = fsin(a);
        for (j = 0; j <= sides; ++j)
        {
            const float b = (float)j / sides * 2 * PI, cb = fcos(b), sb = fsin(b);
            LitVertex *v = &torus_vertices[n++];
            v->nx = ca * cb; v->ny = sb; v->nz = sa * cb;
            v->x = (R + r * cb) * ca; v->y = r * sb; v->z = (R + r * cb) * sa;
            v->u = (float)i / rings * 4.0f;
            v->v = (float)j / sides;
        }
    }
    torus_vertex_count = n;
    n = 0;
    for (i = 0; i < rings; ++i)
        for (j = 0; j < sides; ++j)
        {
            const W a = (W)(i * (sides + 1) + j), b = (W)((i + 1) * (sides + 1) + j);
            /* clockwise seen from outside */
            torus_indices[n++] = a; torus_indices[n++] = (W)(a + 1); torus_indices[n++] = b;
            torus_indices[n++] = b; torus_indices[n++] = (W)(a + 1); torus_indices[n++] = (W)(b + 1);
        }
    torus_index_count = n;
}

static PlainVertex cube_vertices[24];
static W cube_indices[36];
static void build_cube(void)
{
    /* six faces: normal, then two axes spanning the face */
    static const float faces[6][9] = {
        {0, 0, -1, 1, 0, 0, 0, 1, 0}, {0, 0, 1, -1, 0, 0, 0, 1, 0},
        {1, 0, 0, 0, 0, 1, 0, 1, 0},  {-1, 0, 0, 0, 0, -1, 0, 1, 0},
        {0, 1, 0, 1, 0, 0, 0, 0, 1},  {0, -1, 0, 1, 0, 0, 0, 0, -1}};
    static const float corners[4][2] = {{-1, -1}, {-1, 1}, {1, 1}, {1, -1}};
    int f, c, n = 0, k = 0;
    for (f = 0; f < 6; ++f)
    {
        const float *d = faces[f];
        for (c = 0; c < 4; ++c)
        {
            PlainVertex *v = &cube_vertices[n + c];
            v->x = d[0] + corners[c][0] * d[3] + corners[c][1] * d[6];
            v->y = d[1] + corners[c][0] * d[4] + corners[c][1] * d[7];
            v->z = d[2] + corners[c][0] * d[5] + corners[c][1] * d[8];
            v->nx = d[0]; v->ny = d[1]; v->nz = d[2];
        }
        cube_indices[k++] = (W)n; cube_indices[k++] = (W)(n + 1); cube_indices[k++] = (W)(n + 2);
        cube_indices[k++] = (W)n; cube_indices[k++] = (W)(n + 2); cube_indices[k++] = (W)(n + 3);
        n += 4;
    }
}

#define FLOOR_CELLS 24
static LitVertex floor_vertices[(FLOOR_CELLS + 1) * (FLOOR_CELLS + 1)];
static W floor_indices[FLOOR_CELLS * FLOOR_CELLS * 6];
static void build_floor(void)
{
    const float size = 24.0f, y = -1.6f;
    int i, j, n = 0, k = 0;
    for (i = 0; i <= FLOOR_CELLS; ++i)
        for (j = 0; j <= FLOOR_CELLS; ++j)
        {
            LitVertex *v = &floor_vertices[n++];
            v->x = ((float)j / FLOOR_CELLS - 0.5f) * size;
            v->z = ((float)i / FLOOR_CELLS - 0.5f) * size;
            v->y = y;
            v->nx = 0; v->ny = 1; v->nz = 0;
            v->u = (float)j / FLOOR_CELLS * 8.0f;
            v->v = (float)i / FLOOR_CELLS * 8.0f;
        }
    for (i = 0; i < FLOOR_CELLS; ++i)
        for (j = 0; j < FLOOR_CELLS; ++j)
        {
            const W a = (W)(i * (FLOOR_CELLS + 1) + j), b = (W)(a + FLOOR_CELLS + 1);
            floor_indices[k++] = a; floor_indices[k++] = b; floor_indices[k++] = (W)(a + 1);
            floor_indices[k++] = (W)(a + 1); floor_indices[k++] = b; floor_indices[k++] = (W)(b + 1);
        }
}

#define PARTICLES 240
typedef struct { float x, y, z, vx, vy, vz, life; U color; } Particle;
static Particle particles[PARTICLES];
static ColorVertex particle_vertices[PARTICLES * 4];
static W particle_indices[PARTICLES * 6];
static void spawn(Particle *p)
{
    static const U colors[4] = {0xff60a0ff, 0xffff8040, 0xff80ff80, 0xffffe060};
    const float a = random01() * 2 * PI, speed = 0.6f + random01() * 0.8f;
    p->x = 0.0f; p->y = -1.4f; p->z = 0.0f;
    p->vx = fcos(a) * speed * 0.5f;
    p->vz = fsin(a) * speed * 0.5f;
    p->vy = 2.8f + random01() * 1.6f;
    p->life = 1.0f + random01() * 1.6f;
    p->color = colors[random_state & 3];
}

/* ---- DirectDraw and Direct3D ---- */
static const unsigned char IID_IDirectDraw7[16] = {0xc0, 0x5e, 0xe6, 0x15, 0x9c, 0x3b, 0xd2, 0x11, 0xb9, 0x2f, 0x00, 0x60, 0x97, 0x97, 0xea, 0x5b};
static const unsigned char IID_IDirect3D7[16] = {0x77, 0x9e, 0x04, 0xf5, 0x61, 0x48, 0xd2, 0x11, 0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8};
static const unsigned char IID_IDirect3DHALDevice[16] = {0xe0, 0x3d, 0xe6, 0x84, 0xaa, 0x46, 0xcf, 0x11, 0x81, 0x6f, 0x00, 0x00, 0xc0, 0x20, 0x15, 0x6e};
/* IID_IDirectDrawGammaControl {69C11C3E-B46B-11D1-AD7A-00C04FC29B4E} */
static const unsigned char IID_IDirectDrawGammaControl[16] = {0x3e, 0x1c, 0xc1, 0x69, 0x6b, 0xb4, 0xd1, 0x11, 0xad, 0x7a, 0x00, 0xc0, 0x4f, 0xc2, 0x9b, 0x4e};
static const unsigned char IID_IDirect3DTnLHalDevice[16] = {0x78, 0x9e, 0x04, 0xf5, 0x61, 0x48, 0xd2, 0x11, 0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8};

static void *ddraw, *primary, *back, *zbuffer, *d3d, *device;
static void *torus_texture, *floor_texture, *glow_texture, *font_texture;
/* The rarer features, each null when the driver refuses it: a cube map for
   the torus's reflection, a bump map for the floor's ripples, and an
   eight-bit particle texture whose palette carries alpha. */
static void *cube_texture, *bump_texture, *glow8_texture;
static int env_on = 1, bump_on = 1, wbuffer = 0;
static int plain = 0;
static int use_block = 0;       /* "stateblock": the floor's world and material come from a state block */
static U floor_block = 0;   /* "plain": none of the rarer features, for comparisons */
static int width = 640, height = 480, depth = 32;
static int glyph_width = 8, glyph_height = 16;
static int fog = 1, vsync = 1, paused = 0, detail = 1, quit = 0, force_hal = 0, tnl = 0, still = 0, dxt = 1, floor_dxt = 0;
static int stencil = 0;
static int windowed = 0;         /* "window": Direct3D into a window, blitted through a clipper */
static U main_window;
static void *gamma = 0;          /* IDirectDrawGammaControl of the primary, when offered */
static int gamma_percent = 100;  /* 100 is the identity; the G key cycles 100, 150, 70 */   /* the Z buffer has a stencil: the floor mirrors the torus */
static const int detail_rings[3] = {32, 64, 96}, detail_sides[3] = {16, 28, 40};

/* DDSURFACEDESC2, 0x7c bytes, as DWORDs. */
static void clear_desc(U *d)
{
    int i;
    for (i = 0; i < 31; ++i) d[i] = 0;
    d[0] = 0x7c;
}

/* A 32-bit ARGB texture in video memory, with mipmaps when levels > 1. */
static void *create_texture(int w, int h, int levels)
{
    U desc[31], value;
    void *surface = 0;
    clear_desc(desc);
    desc[1] = 0x1 | 0x2 | 0x4 | 0x1000 | (levels > 1 ? 0x20000 : 0);   /* CAPS HEIGHT WIDTH PIXELFORMAT [MIPMAPCOUNT] */
    desc[2] = (U)h;
    desc[3] = (U)w;
    desc[6] = (U)levels;
    desc[18] = 32;
    desc[19] = 0x40 | 0x1;      /* DDPF_RGB | DDPF_ALPHAPIXELS */
    desc[21] = 32;
    desc[22] = 0xff0000; desc[23] = 0xff00; desc[24] = 0xff; desc[25] = 0xff000000u;
    desc[26] = 0x1000 | 0x4000 | (levels > 1 ? 0x400000 | 0x8 : 0);   /* TEXTURE VIDEOMEMORY [MIPMAP COMPLEX] */
    value = CALL(ddraw, 6, M4)(ddraw, (U)desc, (U)&surface, 0);
    if (value || !surface) fail("CreateSurface (texture)", value);
    return surface;
}

typedef U (*Texel)(int x, int y, int w, int h);
static void fill_level(void *surface, Texel texel)
{
    U desc[31], value;
    int x, y, w, h;
    clear_desc(desc);
    value = CALL(surface, 25, M5)(surface, 0, (U)desc, 0x1 | 0x800, 0);   /* Lock, DDLOCK_WAIT | WRITEONLY */
    if (value) fail("Lock (texture)", value);
    h = (int)desc[2]; w = (int)desc[3];
    for (y = 0; y < h; ++y)
    {
        U *row = (U *)((unsigned char *)desc[9] + y * (int)desc[4]);
        for (x = 0; x < w; ++x) row[x] = texel(x, y, w, h);
    }
    CALL(surface, 32, M2)(surface, 0);   /* Unlock */
}
/* One DXT1 block out of sixteen texels: the darkest and the brightest as the
   two end colours, every texel to the nearest of the four colours between. */
static unsigned short to565(U c) { return (unsigned short)(((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f)); }
static void encode_block(const U *texels, unsigned char *out)
{
    int i, lo = 0, hi = 0, llo = 1 << 30, lhi = -1;
    U palette[4];
    unsigned short c0, c1;
    U bits = 0;
    for (i = 0; i < 16; ++i)
    {
        const U t = texels[i];
        const int l = (int)((t >> 16) & 255) * 3 + (int)((t >> 8) & 255) * 6 + (int)(t & 255);
        if (l < llo) { llo = l; lo = i; }
        if (l > lhi) { lhi = l; hi = i; }
    }
    c0 = to565(texels[hi]);
    c1 = to565(texels[lo]);
    if (c0 == c1) { if (c1) --c1; else ++c0; }
    if (c0 < c1) { unsigned short x = c0; c0 = c1; c1 = x; }
    {
        const int r0 = (c0 >> 11) * 255 / 31, g0 = ((c0 >> 5) & 63) * 255 / 63, b0 = (c0 & 31) * 255 / 31;
        const int r1 = (c1 >> 11) * 255 / 31, g1 = ((c1 >> 5) & 63) * 255 / 63, b1 = (c1 & 31) * 255 / 31;
        palette[0] = (U)(r0 << 16 | g0 << 8 | b0);
        palette[1] = (U)(r1 << 16 | g1 << 8 | b1);
        palette[2] = (U)(((2 * r0 + r1) / 3) << 16 | ((2 * g0 + g1) / 3) << 8 | (2 * b0 + b1) / 3);
        palette[3] = (U)(((r0 + 2 * r1) / 3) << 16 | ((g0 + 2 * g1) / 3) << 8 | (b0 + 2 * b1) / 3);
    }
    for (i = 0; i < 16; ++i)
    {
        int k, best = 0, best_d = 1 << 30;
        for (k = 0; k < 4; ++k)
        {
            const int dr = (int)((texels[i] >> 16) & 255) - (int)((palette[k] >> 16) & 255);
            const int dg = (int)((texels[i] >> 8) & 255) - (int)((palette[k] >> 8) & 255);
            const int db = (int)(texels[i] & 255) - (int)(palette[k] & 255);
            const int d = dr * dr + dg * dg + db * db;
            if (d < best_d) { best_d = d; best = k; }
        }
        bits |= (U)best << (i * 2);
    }
    out[0] = (unsigned char)c0; out[1] = (unsigned char)(c0 >> 8);
    out[2] = (unsigned char)c1; out[3] = (unsigned char)(c1 >> 8);
    out[4] = (unsigned char)bits; out[5] = (unsigned char)(bits >> 8);
    out[6] = (unsigned char)(bits >> 16); out[7] = (unsigned char)(bits >> 24);
}
static void fill_dxt1_level(void *surface, Texel texel)
{
    U desc[31], value, block[16];
    int bx, by, x, y, w, h, bw, bh;
    clear_desc(desc);
    value = CALL(surface, 25, M5)(surface, 0, (U)desc, 0x1 | 0x800, 0);
    if (value) fail("Lock (DXT1)", value);
    h = (int)desc[2]; w = (int)desc[3];
    bw = (w + 3) / 4; bh = (h + 3) / 4;
    for (by = 0; by < bh; ++by)
        for (bx = 0; bx < bw; ++bx)
        {
            for (y = 0; y < 4; ++y)
                for (x = 0; x < 4; ++x)
                {
                    const int tx = bx * 4 + x < w ? bx * 4 + x : w - 1, ty = by * 4 + y < h ? by * 4 + y : h - 1;
                    block[y * 4 + x] = texel(tx, ty, w, h);
                }
            encode_block(block, (unsigned char *)desc[9] + (by * bw + bx) * 8);
        }
    CALL(surface, 32, M2)(surface, 0);
}
static void fill_dxt1(void *surface, Texel texel)
{
    U caps[4] = {0x1000 | 0x400000, 0, 0, 0};
    void *level = surface, *next;
    fill_dxt1_level(level, texel);
    while (!CALL(level, 12, M3)(level, (U)caps, (U)&next) && next)
    {
        fill_dxt1_level(next, texel);
        if (level != surface) CALL(level, 2, M1)(level);
        level = next;
    }
    if (level != surface) CALL(level, 2, M1)(level);
}

/* Fills every mip level; each level gets the same function at its own size,
   which is a correct box of the pattern for these smooth patterns. */
static void fill_texture(void *surface, Texel texel)
{
    U caps[4] = {0x1000 | 0x400000, 0, 0, 0};   /* DDSCAPS_TEXTURE | DDSCAPS_MIPMAP */
    void *level = surface, *next;
    fill_level(level, texel);
    while (!CALL(level, 12, M3)(level, (U)caps, (U)&next) && next)
    {
        fill_level(next, texel);
        if (level != surface) CALL(level, 2, M1)(level);
        level = next;
    }
    if (level != surface) CALL(level, 2, M1)(level);
}

/* A DXT1 texture: the same as create_texture, with the FourCC. Null when the
   driver does not offer it. */
static void *create_dxt1(int w, int h, int levels)
{
    U desc[31], value;
    void *surface = 0;
    clear_desc(desc);
    desc[1] = 0x1 | 0x2 | 0x4 | 0x1000 | (levels > 1 ? 0x20000 : 0);
    desc[2] = (U)h;
    desc[3] = (U)w;
    desc[6] = (U)levels;
    desc[18] = 32;
    desc[19] = 0x4;             /* DDPF_FOURCC */
    desc[20] = 0x31545844;      /* 'DXT1' */
    desc[26] = 0x1000 | 0x4000 | (levels > 1 ? 0x400000 | 0x8 : 0);
    value = CALL(ddraw, 6, M4)(ddraw, (U)desc, (U)&surface, 0);
    return value ? 0 : surface;
}

static U clamp255(int v) { return v < 0 ? 0u : v > 255 ? 255u : (U)v; }
static U rgb(int r, int g, int b) { return 0xff000000u | clamp255(r) << 16 | clamp255(g) << 8 | clamp255(b); }
/* The torus: warm plasma bands. */
static U torus_texel(int x, int y, int w, int h)
{
    const float u = (float)x / w, v = (float)y / h;
    const float p = fsin(u * 2 * PI * 3 + fsin(v * 2 * PI) * 2) + fsin(v * 2 * PI * 2 + u * 6);
    return rgb((int)(180 + 70 * p), (int)(110 + 60 * fsin(p * 2)), (int)(60 + 50 * p));
}
/* The floor: a checker of tiles with a thin border, so mipmapping shows. */
static U floor_texel(int x, int y, int w, int h)
{
    const int tx = x * 8 / w, ty = y * 8 / h;
    const int bx = x * 64 / w % 8, by = y * 64 / h % 8;
    const int dark = (tx + ty) & 1;
    int base = dark ? 70 : 150;
    if (w >= 64 && (bx == 0 || by == 0)) base = 40;
    return rgb(base, base + 10, base + 30);
}
/* The particle: a round glow, colour white, alpha falling off. */
static U glow_texel(int x, int y, int w, int h)
{
    const float dx = ((float)x + 0.5f) / w * 2 - 1, dy = ((float)y + 0.5f) / h * 2 - 1;
    float a = 1.0f - fsqrt(dx * dx + dy * dy);
    int v;
    if (a < 0) a = 0;
    v = (int)(a * a * 255);
    return (U)v << 24 | (U)v << 16 | (U)v << 8 | (U)v;
}

/* The font: GDI draws the glyphs of the fixed system font into the texture
   through the surface DC, then the texels turn into white with alpha. */
static void build_font(void)
{
    U desc[31], value, dc = 0, metrics[16];
    int c, x, y;
    font_texture = create_texture(256, 128, 1);
    clear_desc(desc);
    value = CALL(font_texture, 25, M5)(font_texture, 0, (U)desc, 0x1, 0);
    if (value) fail("Lock (font)", value);
    for (y = 0; y < 128; ++y)
    {
        U *row = (U *)((unsigned char *)desc[9] + y * (int)desc[4]);
        for (x = 0; x < 256; ++x) row[x] = 0xff000000u;
    }
    CALL(font_texture, 32, M2)(font_texture, 0);
    value = CALL(font_texture, 17, M2)(font_texture, (U)&dc);   /* GetDC */
    if (value || !dc) fail("GetDC (font)", value);
    SelectObject(dc, GetStockObject(16));   /* SYSTEM_FIXED_FONT */
    SetTextColor(dc, 0xffffff);
    SetBkColor(dc, 0);
    SetBkMode(dc, 2);   /* OPAQUE */
    if (GetTextMetricsA(dc, metrics))
    {
        glyph_height = (int)metrics[0];
        glyph_width = (int)metrics[5];
        if (glyph_height > 16) glyph_height = 16;
        if (glyph_width > 16) glyph_width = 16;
    }
    for (c = 32; c < 128; ++c)
    {
        const char ch = (char)c;
        TextOutA(dc, (c - 32) % 16 * 16, (c - 32) / 16 * 16, &ch, 1);
    }
    CALL(font_texture, 26, M2)(font_texture, dc);   /* ReleaseDC */
    clear_desc(desc);
    value = CALL(font_texture, 25, M5)(font_texture, 0, (U)desc, 0x1, 0);
    if (value) fail("Lock (font)", value);
    for (y = 0; y < 128; ++y)
    {
        U *row = (U *)((unsigned char *)desc[9] + y * (int)desc[4]);
        for (x = 0; x < 256; ++x)
        {
            const U t = row[x], r = (t >> 16) & 255, g = (t >> 8) & 255, b = t & 255;
            const U a = r > g ? (r > b ? r : b) : (g > b ? g : b);
            row[x] = a << 24 | 0xffffff;
        }
    }
    CALL(font_texture, 32, M2)(font_texture, 0);
}

/* A cube map: DDSCAPS2_CUBEMAP with all six faces, 32-bit ARGB. */
static void *create_cube(int size)
{
    U desc[31], value;
    void *surface = 0;
    clear_desc(desc);
    desc[1] = 0x1 | 0x2 | 0x4 | 0x1000;
    desc[2] = (U)size;
    desc[3] = (U)size;
    desc[18] = 32;
    desc[19] = 0x40 | 0x1;
    desc[21] = 32;
    desc[22] = 0xff0000; desc[23] = 0xff00; desc[24] = 0xff; desc[25] = 0xff000000u;
    desc[26] = 0x1000 | 0x8 | 0x4000;     /* TEXTURE COMPLEX VIDEOMEMORY */
    desc[27] = 0x200 | 0xfc00;            /* DDSCAPS2_CUBEMAP, all faces */
    value = CALL(ddraw, 6, M4)(ddraw, (U)desc, (U)&surface, 0);
    return value ? 0 : surface;
}
/* The sky around the scene, seen along a direction: blue above, dark below,
   a warm sun to one side. */
static int cube_face;
static U sky(float x, float y, float z)
{
    const float len = fsqrt(x * x + y * y + z * z);
    float up, sun, r, g, b;
    x /= len; y /= len; z /= len;
    up = y;
    sun = x * 0.6f + y * 0.5f + z * 0.62f;
    sun = sun > 0.9f ? (sun - 0.9f) * 10.0f : 0.0f;
    if (up > 0) { r = 60 + 80 * (1 - up); g = 110 + 80 * (1 - up); b = 200 + 40 * (1 - up); }
    else { r = 60 + 40 * (1 + up); g = 50 + 40 * (1 + up); b = 40 + 30 * (1 + up); }
    r += 255 * sun; g += 220 * sun; b += 150 * sun;
    return rgb((int)r, (int)g, (int)b);
}
static U cube_texel(int px, int py, int w, int h)
{
    const float u = ((float)px + 0.5f) / (float)w * 2 - 1, v = ((float)py + 0.5f) / (float)h * 2 - 1;
    switch (cube_face)
    {
    case 0: return sky(1, -v, -u);
    case 1: return sky(-1, -v, u);
    case 2: return sky(u, 1, v);
    case 3: return sky(u, -1, -v);
    case 4: return sky(u, -v, 1);
    default: return sky(-u, -v, -1);
    }
}
static void fill_cube(void *surface)
{
    for (cube_face = 0; cube_face < 6; ++cube_face)
    {
        U caps[4] = {0x1000, 0x200 | (0x400u << cube_face), 0, 0};
        void *face = surface;
        if (cube_face && (CALL(surface, 12, M3)(surface, (U)caps, (U)&face) || !face)) return;
        fill_level(face, cube_texel);
        if (face != surface) CALL(face, 2, M1)(face);
    }
}
/* A bump map in V8U8 (DDPF_BUMPDUDV): ripples as signed du, dv. */
static void *create_bump(int size)
{
    U desc[31], value;
    void *surface = 0;
    clear_desc(desc);
    desc[1] = 0x1 | 0x2 | 0x4 | 0x1000;
    desc[2] = (U)size;
    desc[3] = (U)size;
    desc[18] = 32;
    desc[19] = 0x80000;   /* DDPF_BUMPDUDV */
    desc[21] = 16;
    desc[22] = 0x00ff;    /* du */
    desc[23] = 0xff00;    /* dv */
    desc[26] = 0x1000 | 0x4000;
    value = CALL(ddraw, 6, M4)(ddraw, (U)desc, (U)&surface, 0);
    return value ? 0 : surface;
}
static void fill_bump(void *surface)
{
    U desc[31], value;
    int x, y, w, h;
    clear_desc(desc);
    value = CALL(surface, 25, M5)(surface, 0, (U)desc, 0x1 | 0x800, 0);
    if (value) fail("Lock (bump)", value);
    h = (int)desc[2]; w = (int)desc[3];
    for (y = 0; y < h; ++y)
    {
        unsigned short *row = (unsigned short *)((unsigned char *)desc[9] + y * (int)desc[4]);
        for (x = 0; x < w; ++x)
        {
            const float a = (float)x / (float)w * 2 * PI * 3, b = (float)y / (float)h * 2 * PI * 2;
            const int du = (int)(fsin(a + fsin(b) * 1.5f) * 110.0f), dv = (int)(fcos(b + fcos(a) * 1.2f) * 110.0f);
            row[x] = (unsigned short)((du & 255) | ((dv & 255) << 8));
        }
    }
    CALL(surface, 32, M2)(surface, 0);
}
/* An eight-bit texture with a palette that carries alpha (DDPCAPS_ALPHA):
   entry i is white with alpha i, the texel the glow's alpha. */
static void *create_glow8(int size)
{
    U desc[31], value, entries[256];
    void *surface = 0, *palette = 0;
    int i;
    clear_desc(desc);
    desc[1] = 0x1 | 0x2 | 0x4 | 0x1000;
    desc[2] = (U)size;
    desc[3] = (U)size;
    desc[18] = 32;
    desc[19] = 0x40 | 0x20;   /* DDPF_RGB | DDPF_PALETTEINDEXED8 */
    desc[21] = 8;
    desc[26] = 0x1000 | 0x4000;
    value = CALL(ddraw, 6, M4)(ddraw, (U)desc, (U)&surface, 0);
    if (value || !surface) { report("CreateSurface (glow8)", value); return 0; }
    for (i = 0; i < 256; ++i) entries[i] = 0xffffffu | (U)i << 24;   /* PALETTEENTRY r, g, b, flags */
    value = CALL(ddraw, 5, M5)(ddraw, 0x4 | 0x400, (U)entries, (U)&palette, 0);   /* DDPCAPS_8BIT | DDPCAPS_ALPHA */
    if (value) report("CreatePalette (glow8)", value);
    if (!value && palette) value = CALL(surface, 31, M2)(surface, (U)palette);
    if (value || !palette)
    {
        report("palette (glow8)", value);
        CALL(surface, 2, M1)(surface);
        return 0;
    }
    return surface;
}
static void fill_glow8(void *surface)
{
    U desc[31], value;
    int x, y, w, h;
    clear_desc(desc);
    value = CALL(surface, 25, M5)(surface, 0, (U)desc, 0x1 | 0x800, 0);
    if (value) fail("Lock (glow8)", value);
    h = (int)desc[2]; w = (int)desc[3];
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x)
            ((unsigned char *)desc[9])[y * (int)desc[4] + x] = (unsigned char)(glow_texel(x, y, w, h) & 255);
    CALL(surface, 32, M2)(surface, 0);
}

static void fill_all_textures(void)
{
    if (cube_texture) fill_cube(cube_texture);
    if (bump_texture) fill_bump(bump_texture);
    if (glow8_texture) fill_glow8(glow8_texture);
    fill_texture(torus_texture, torus_texel);
    if (floor_dxt) fill_dxt1(floor_texture, floor_texel);
    else fill_texture(floor_texture, floor_texel);
    fill_texture(glow_texture, glow_texel);
}

/* ---- the overlay ---- */
#define TEXT_MAX 400
static ScreenVertex text_vertices[TEXT_MAX * 4];
static W text_indices[TEXT_MAX * 6];
static U text_quads;
static void text_quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, U color)
{
    ScreenVertex *v = &text_vertices[text_quads * 4];
    W *i = &text_indices[text_quads * 6];
    const W base = (W)(text_quads * 4);
    int k;
    if (text_quads >= TEXT_MAX) return;
    for (k = 0; k < 4; ++k) { v[k].z = 0.0f; v[k].rhw = 1.0f; v[k].color = color; }
    v[0].x = x0; v[0].y = y0; v[0].u = u0; v[0].v = v0;
    v[1].x = x1; v[1].y = y0; v[1].u = u1; v[1].v = v0;
    v[2].x = x1; v[2].y = y1; v[2].u = u1; v[2].v = v1;
    v[3].x = x0; v[3].y = y1; v[3].u = u0; v[3].v = v1;
    i[0] = base; i[1] = (W)(base + 1); i[2] = (W)(base + 2);
    i[3] = base; i[4] = (W)(base + 2); i[5] = (W)(base + 3);
    ++text_quads;
}
static void text(int x, int y, const char *s, U color)
{
    for (; *s; ++s, x += glyph_width)
    {
        const int c = (unsigned char)*s;
        float u, v;
        if (c <= 32 || c >= 128) continue;
        u = (float)((c - 32) % 16 * 16) / 256.0f;
        v = (float)((c - 32) / 16 * 16) / 128.0f;
        text_quad((float)x - 0.5f, (float)y - 0.5f, (float)(x + glyph_width) - 0.5f, (float)(y + glyph_height) - 0.5f,
                  u, v, u + (float)glyph_width / 256.0f, v + (float)glyph_height / 128.0f, color);
    }
}

/* The ramp for a brightness: out = in^(100/percent), the curve a game's
   brightness slider sets. */
static float fln(float x) { float r; __asm__("fldln2\n\tfxch\n\tfyl2x" : "=t"(r) : "0"(x)); return r; }
static float fexp(float y)
{
    /* exp(y) = exp(y / 16) ^ 16, the series converges fast for small y */
    float t = y / 16.0f, term = 1.0f, sum = 1.0f;
    int i;
    for (i = 1; i < 12; ++i) { term *= t / (float)i; sum += term; }
    for (i = 0; i < 4; ++i) sum *= sum;
    return sum;
}
static float fpow(float x, float y) { return x <= 0.0f ? 0.0f : fexp(y * fln(x)); }
static void set_gamma(int percent)
{
    static unsigned short ramp[768];
    int i;
    if (!gamma) return;
    for (i = 0; i < 256; ++i)
    {
        const float v = fpow((float)i / 255.0f, 100.0f / (float)percent);
        const unsigned short w = (unsigned short)(v * 65535.0f);
        ramp[i] = ramp[256 + i] = ramp[512 + i] = w;
    }
    {
        const U value = CALL(gamma, 4, M3)(gamma, 0, (U)ramp);   /* SetGammaRamp */
        if (value) fail("SetGammaRamp", value);
    }
    gamma_percent = percent;
}

/* ---- rendering ---- */
static void render_state(U state, U value) { CALL(device, 20, M3)(device, state, value); }
static void stage_state(U stage, U type, U value) { CALL(device, 37, M4)(device, stage, type, value); }
static void set_texture(U stage, void *texture) { CALL(device, 35, M3)(device, stage, (U)texture); }
static void set_world(const Matrix *m) { CALL(device, 11, M3)(device, 1, (U)m->m); }
static U triangles, draws;
static void draw_indexed(U fvf, const void *vertices, U vertex_count, const W *indices, U index_count)
{
    const U value = CALL(device, 26, M8)(device, 4, fvf, (U)vertices, vertex_count, (U)indices, index_count, 0);
    if (value)
    {
        /* which draw: the vertex format tells the objects apart */
        char line[64], *at = put_text(line, "DrawIndexedPrimitive fvf ");
        put_number(at, fvf);
        fail(line, value);
    }
    triangles += index_count / 3;
    ++draws;
}
static float as_float_bits_holder;
static U float_bits(float f) { as_float_bits_holder = f; return *(U *)&as_float_bits_holder; }

static void set_material(float r, float g, float b, float specular, float power, float emissive)
{
    float m[17];
    int i;
    for (i = 0; i < 17; ++i) m[i] = 0.0f;
    m[0] = r; m[1] = g; m[2] = b; m[3] = 1.0f;                 /* diffuse */
    m[4] = r * 0.35f; m[5] = g * 0.35f; m[6] = b * 0.35f; m[7] = 1.0f;   /* ambient */
    m[8] = m[9] = m[10] = specular; m[11] = 1.0f;              /* specular */
    m[12] = r * emissive; m[13] = g * emissive; m[14] = b * emissive; m[15] = 1.0f;
    m[16] = power;
    CALL(device, 16, M2)(device, (U)m);
}

/* The floor material again, with a diffuse alpha: lit vertices take their
   alpha from it, and the texture stage modulates it. */
static void set_floor_alpha(float alpha)
{
    float m[17];
    int i;
    for (i = 0; i < 17; ++i) m[i] = 0.0f;
    m[0] = 0.9f; m[1] = 0.9f; m[2] = 0.95f; m[3] = alpha;
    m[4] = 0.9f * 0.35f; m[5] = 0.9f * 0.35f; m[6] = 0.95f * 0.35f; m[7] = 1.0f;
    m[11] = 1.0f; m[15] = 1.0f; m[16] = 1.0f;
    CALL(device, 16, M2)(device, (U)m);
}

static void setup_lights(void)
{
    float light[26];
    int i;
    for (i = 0; i < 26; ++i) light[i] = 0.0f;
    /* light 0: a directional key light */
    *(U *)&light[0] = 3;   /* D3DLIGHT_DIRECTIONAL */
    light[1] = 1.0f; light[2] = 0.95f; light[3] = 0.85f; light[4] = 1.0f;    /* diffuse */
    light[5] = 1.0f; light[6] = 1.0f; light[7] = 1.0f; light[8] = 1.0f;      /* specular */
    light[16] = -0.4f; light[17] = -0.8f; light[18] = 0.45f;                  /* direction */
    CALL(device, 18, M3)(device, 0, (U)light);
    CALL(device, 44, M3)(device, 0, 1);
    /* light 1: a blue point light that circles the scene, set per frame */
    render_state(139, 0xff303038);   /* D3DRENDERSTATE_AMBIENT */
}
static void move_point_light(float t)
{
    float light[26];
    int i;
    for (i = 0; i < 26; ++i) light[i] = 0.0f;
    *(U *)&light[0] = 1;   /* D3DLIGHT_POINT */
    light[1] = 0.3f; light[2] = 0.5f; light[3] = 1.0f; light[4] = 1.0f;
    light[5] = 0.3f; light[6] = 0.5f; light[7] = 1.0f; light[8] = 1.0f;
    light[13] = fcos(t * 0.9f) * 3.0f; light[14] = 0.8f; light[15] = fsin(t * 0.9f) * 3.0f;
    light[19] = 20.0f;   /* range */
    light[21] = 1.0f;    /* attenuation0 */
    light[22] = 0.15f;   /* attenuation1 */
    CALL(device, 18, M3)(device, 1, (U)light);
    CALL(device, 44, M3)(device, 1, 1);
}

static void update_particles(float dt)
{
    int i;
    for (i = 0; i < PARTICLES; ++i)
    {
        Particle *p = &particles[i];
        p->life -= dt;
        if (p->life <= 0) { spawn(p); continue; }
        p->vy -= 3.2f * dt;
        p->x += p->vx * dt; p->y += p->vy * dt; p->z += p->vz * dt;
        if (p->y < -1.55f) { p->y = -1.55f; p->vy = -p->vy * 0.45f; }
    }
}
static void build_particle_quads(void)
{
    int i, k;
    for (i = 0; i < PARTICLES; ++i)
    {
        const Particle *p = &particles[i];
        const float s = 0.12f;
        const float rx = cam_right[0] * s, ry = cam_right[1] * s, rz = cam_right[2] * s;
        const float ux = cam_up[0] * s, uy = cam_up[1] * s, uz = cam_up[2] * s;
        U fade = p->life > 0.5f ? 255u : (U)(p->life * 510.0f);
        U color = (p->color & 0xffffff) | fade << 24;
        ColorVertex *v = &particle_vertices[i * 4];
        /* additive blending uses the colour, so darken it with the fade too */
        color = ((((color >> 16) & 255) * fade / 255) << 16) | ((((color >> 8) & 255) * fade / 255) << 8) |
                ((color & 255) * fade / 255) | 0xff000000u;
        v[0].x = p->x - rx + ux; v[0].y = p->y - ry + uy; v[0].z = p->z - rz + uz; v[0].u = 0; v[0].v = 0;
        v[1].x = p->x + rx + ux; v[1].y = p->y + ry + uy; v[1].z = p->z + rz + uz; v[1].u = 1; v[1].v = 0;
        v[2].x = p->x + rx - ux; v[2].y = p->y + ry - uy; v[2].z = p->z + rz - uz; v[2].u = 1; v[2].v = 1;
        v[3].x = p->x - rx - ux; v[3].y = p->y - ry - uy; v[3].z = p->z - rz - uz; v[3].u = 0; v[3].v = 1;
        for (k = 0; k < 4; ++k) v[k].color = color;
    }
}

/* The floor's texture stages: with the bump map, stage 0 perturbs the
   coordinates stage 1 samples the floor with; the matrix sways with time. */
static float scene_time;
static void floor_stages(int on)
{
    if (on && bump_on)
    {
        const float k = 0.035f, a = scene_time * 0.8f;
        set_texture(0, bump_texture);
        stage_state(0, 1, 22);                 /* COLOROP BUMPENVMAP */
        stage_state(0, 7, float_bits(k * fcos(a)));
        stage_state(0, 8, float_bits(k * fsin(a)));
        stage_state(0, 9, float_bits(-k * fsin(a)));
        stage_state(0, 10, float_bits(k * fcos(a)));
        set_texture(1, floor_texture);
        stage_state(1, 1, 4);                  /* MODULATE */
        stage_state(1, 2, 2);
        stage_state(1, 3, 1);                  /* CURRENT, the lit diffuse */
        stage_state(1, 4, 4);
        stage_state(1, 5, 2);
        stage_state(1, 6, 1);
        stage_state(1, 11, 0);                 /* the floor's coordinates */
        stage_state(1, 18, 3);                 /* MIPFILTER LINEAR */
        stage_state(2, 1, 1);
        return;
    }
    stage_state(0, 1, 4);                      /* MODULATE again */
    stage_state(1, 1, 1);                      /* DISABLE */
    set_texture(1, 0);
    set_texture(0, on ? floor_texture : 0);
}
/* The torus's reflection: stage 1 samples the cube map along the
   reflection vector in camera space and blends it in by the texture factor. */
static void torus_stages(int on)
{
    if (on && env_on)
    {
        set_texture(1, cube_texture);
        stage_state(1, 1, 14);                 /* BLENDFACTORALPHA */
        stage_state(1, 2, 2);                  /* TEXTURE */
        stage_state(1, 3, 1);                  /* CURRENT */
        stage_state(1, 4, 3);                  /* ALPHA: SELECTARG2 */
        stage_state(1, 6, 1);
        stage_state(1, 11, 0x30000 | 1);       /* CAMERASPACEREFLECTIONVECTOR */
        stage_state(1, 24, 3);                 /* D3DTTFF_COUNT3 */
        stage_state(1, 18, 1);
        stage_state(2, 1, 1);
        render_state(60, 0x70ffffff);          /* TEXTUREFACTOR, alpha 0.44 */
        return;
    }
    stage_state(1, 1, 1);
    stage_state(1, 11, 1);
    stage_state(1, 24, 0);
    set_texture(1, 0);
}

/* The floor's world matrix and material, directly or out of the state
   block recorded at the start. */
static void floor_world_material(void)
{
    if (floor_block)
    {
        const U value = CALL(device, 39, M2)(device, floor_block);   /* ApplyStateBlock */
        if (value) fail("ApplyStateBlock", value);
        return;
    }
    {
        Matrix m;
        identity(&m);
        set_world(&m);
        set_material(0.9f, 0.9f, 0.95f, 0.0f, 1.0f, 0.0f);
    }
}

static void draw_scene(float t)
{
    scene_time = t;
    render_state(7, wbuffer ? 2 : 1);   /* ZENABLE: D3DZB_USEW or D3DZB_TRUE */
    Matrix m, a, b;
    int i;

    render_state(137, 1);   /* LIGHTING */
    render_state(29, 1);    /* SPECULARENABLE */
    render_state(28, (U)fog);   /* FOGENABLE */
    move_point_light(t);

    /* the torus, turning above the floor */
    Matrix torus;
    rotation_x(&a, t * 0.7f);
    rotation_z(&b, t * 0.45f);
    multiply(&torus, &a, &b);
    rotation_y(&a, t * 0.3f);
    multiply(&torus, &torus, &a);
    translation(&a, 0.0f, 0.4f, 0.0f);
    multiply(&torus, &torus, &a);

    floor_world_material();
    floor_stages(1);
    stage_state(0, 18, 3);  /* MIPFILTER LINEAR */
    if (stencil)
    {
        /* The mirror: the floor marks its pixels in the stencil without
           touching colour or depth, the mirrored torus draws only there, and
           the floor goes over it at 70 percent. */
        render_state(52, 1);            /* STENCILENABLE */
        render_state(56, 8);            /* STENCILFUNC ALWAYS */
        render_state(57, 1);            /* STENCILREF */
        render_state(55, 3);            /* STENCILPASS REPLACE */
        render_state(14, 0);            /* ZWRITEENABLE off */
        render_state(27, 1);            /* ALPHABLENDENABLE */
        render_state(19, 1);            /* SRCBLEND ZERO */
        render_state(20, 2);            /* DESTBLEND ONE */
        draw_indexed(FVF_LIT_TEX, floor_vertices, (FLOOR_CELLS + 1) * (FLOOR_CELLS + 1), floor_indices,
                     FLOOR_CELLS * FLOOR_CELLS * 6);
        render_state(27, 0);
        render_state(14, 1);
        render_state(56, 3);            /* STENCILFUNC EQUAL */
        render_state(55, 1);            /* STENCILPASS KEEP */
        {
            /* y' = 2 * floor - y: down to the floor, flip, back up */
            Matrix mirror;
            translation(&a, 0.0f, 1.6f, 0.0f);
            identity(&b);
            b.m[5] = -1.0f;
            multiply(&mirror, &a, &b);
            translation(&a, 0.0f, -1.6f, 0.0f);
            multiply(&mirror, &mirror, &a);
            multiply(&m, &torus, &mirror);
        }
        set_world(&m);
        set_material(1.0f, 1.0f, 1.0f, 0.9f, 24.0f, 0.0f);
        floor_stages(0);
        set_texture(0, torus_texture);
        stage_state(0, 18, 1);
        torus_stages(1);
        draw_indexed(FVF_LIT_TEX, torus_vertices, torus_vertex_count, torus_indices, torus_index_count);
        torus_stages(0);
        render_state(52, 0);            /* STENCILENABLE off */
        floor_world_material();
        floor_stages(1);
        stage_state(0, 18, 3);
        render_state(27, 1);
        render_state(19, 5);            /* SRCALPHA */
        render_state(20, 6);            /* INVSRCALPHA */
    }
    /* the floor, mipmapped, under fog */
    floor_world_material();
    if (stencil) set_floor_alpha(0.7f);
    draw_indexed(FVF_LIT_TEX, floor_vertices, (FLOOR_CELLS + 1) * (FLOOR_CELLS + 1), floor_indices,
                 FLOOR_CELLS * FLOOR_CELLS * 6);
    render_state(27, 0);
    floor_stages(0);
    stage_state(0, 18, 1);  /* MIPFILTER NONE */

    set_world(&torus);
    set_material(1.0f, 1.0f, 1.0f, 0.9f, 24.0f, 0.0f);
    set_texture(0, torus_texture);
    torus_stages(1);
    draw_indexed(FVF_LIT_TEX, torus_vertices, torus_vertex_count, torus_indices, torus_index_count);
    torus_stages(0);

    /* the ring of cubes: no texture coordinates, so no texture stage at all
       and the lit colour as it is; once a stage generates coordinates,
       D3DIM refuses any enabled stage that names a set the vertices do not
       carry */
    set_texture(0, 0);
    stage_state(0, 1, 1);   /* COLOROP DISABLE */
    stage_state(0, 4, 1);
    for (i = 0; i < 12; ++i)
    {
        const float angle = t * 0.5f + (float)i * (2 * PI / 12);
        const float hue = (float)i / 12 * 2 * PI;
        scaling(&m, 0.28f);
        rotation_y(&a, t * 1.3f + (float)i);
        multiply(&m, &m, &a);
        rotation_x(&a, t * 0.9f + (float)i * 0.5f);
        multiply(&m, &m, &a);
        translation(&a, fcos(angle) * 3.2f, 0.2f + fsin(t * 1.7f + (float)i) * 0.5f, fsin(angle) * 3.2f);
        multiply(&m, &m, &a);
        set_world(&m);
        set_material(0.55f + 0.45f * fcos(hue), 0.55f + 0.45f * fcos(hue + 2.1f), 0.55f + 0.45f * fcos(hue + 4.2f),
                     0.6f, 12.0f, 0.1f);
        draw_indexed(FVF_LIT, cube_vertices, 24, cube_indices, 36);
    }

    stage_state(0, 1, 4);   /* MODULATE again */
    stage_state(0, 4, 4);

    /* the particles: unlit, additive, no Z writes */
    identity(&m);
    set_world(&m);
    render_state(137, 0);   /* LIGHTING off */
    render_state(28, 0);    /* no fog on light */
    render_state(14, 0);    /* ZWRITEENABLE off */
    render_state(27, 1);    /* ALPHABLENDENABLE */
    render_state(19, 5);    /* SRCBLEND SRCALPHA: the texture's alpha is the glow */
    render_state(20, 2);    /* DESTBLEND ONE */
    set_texture(0, glow8_texture ? glow8_texture : glow_texture);
    build_particle_quads();
    draw_indexed(FVF_COLOR_TEX, particle_vertices, PARTICLES * 4, particle_indices, PARTICLES * 6);
    render_state(14, 1);
    render_state(27, 0);
}

static void draw_overlay(U fps_tenths, U frame_tenths_ms)
{
    char line[128], *at;
    const U white = 0xffffffff, yellow = 0xffffe060, grey = 0xffb0b0b0;
    const int x = 10, dy = glyph_height + 2;
    int y = 10;
    text_quads = 0;
    at = put_text(line, "FPS ");
    at = put_tenths(at, fps_tenths);
    at = put_text(at, "   ");
    at = put_tenths(at, frame_tenths_ms);
    put_text(at, " ms");
    text(x, y, line, yellow);
    y += dy;
    at = put_text(line, tnl ? "Direct3D 7 TnL HAL on Metal  " : "Direct3D 7 HAL on Metal  ");
    at = put_number(at, (U)width);
    at = put_text(at, "x");
    at = put_number(at, (U)height);
    at = put_text(at, "x");
    put_number(at, (U)depth);
    text(x, y, line, white);
    y += dy;
    at = put_text(line, "Triangles ");
    at = put_number(at, triangles);
    at = put_text(at, "  draws ");
    at = put_number(at, draws);
    at = put_text(at, floor_dxt ? "  floor DXT1" : "  floor ARGB");
    put_text(at, stencil ? "  stencil mirror" : "");
    text(x, y, line, white);
    y += dy;
    at = put_text(line, "[E]nv cube ");
    at = put_text(at, !cube_texture ? "-" : env_on ? "on" : "off");
    at = put_text(at, "  [B]ump ");
    at = put_text(at, !bump_texture ? "-" : bump_on ? "on" : "off");
    at = put_text(at, "  [W]-buffer ");
    at = put_text(at, wbuffer ? "on" : "off");
    put_text(at, glow8_texture ? "  palette alpha" : "");
    text(x, y, line, white);
    y += dy;
    if (gamma)
    {
        at = put_text(line, "[G]amma ");
        at = put_number(at, (U)gamma_percent);
        put_text(at, "%");
        text(width - 13 * glyph_width - 10, 10, line, grey);
    }
    at = put_text(line, "[V]sync ");
    at = put_text(at, vsync ? "on" : "off");
    at = put_text(at, "  [F]og ");
    at = put_text(at, fog ? "on" : "off");
    at = put_text(at, "  [T]orus ");
    at = put_number(at, (U)detail);
    put_text(at, "  [Space] pause  [Esc] quit");
    text(x, y, line, grey);

    render_state(137, 0);   /* LIGHTING off */
    render_state(28, 0);    /* FOGENABLE off */
    render_state(7, 0);     /* ZENABLE off */
    render_state(27, 1);    /* ALPHABLENDENABLE */
    render_state(19, 5);    /* SRCBLEND SRCALPHA */
    render_state(20, 6);    /* DESTBLEND INVSRCALPHA */
    set_texture(0, font_texture);
    stage_state(0, 16, 1);  /* MAGFILTER POINT */
    stage_state(0, 17, 1);  /* MINFILTER POINT */
    if (text_quads)
    {
        const U value = CALL(device, 26, M8)(device, 4, FVF_SCREEN, (U)text_vertices, text_quads * 4,
                                             (U)text_indices, text_quads * 6, 0);
        if (value) fail("DrawIndexedPrimitive (text)", value);
    }
    stage_state(0, 16, 2);  /* MAGFILTER LINEAR */
    stage_state(0, 17, 2);  /* MINFILTER LINEAR */
    render_state(27, 0);
    render_state(7, 1);
}

static U __stdcall window_procedure(U window, U msg, U wp, U lp)
{
    if (msg == 0x0010 || msg == 0x0002) { quit = 1; PostQuitMessage(0); return 0; }   /* WM_CLOSE, WM_DESTROY */
    if (msg == 0x0100)   /* WM_KEYDOWN */
    {
        if (wp == 0x1b) quit = 1;
        else if (wp == 'V') vsync = !vsync;
        else if (wp == 'F') fog = !fog;
        else if (wp == 'G') set_gamma(gamma_percent == 100 ? 150 : gamma_percent == 150 ? 70 : 100);
        else if (wp == 'E') env_on = cube_texture && !env_on;
        else if (wp == 'B') bump_on = bump_texture && !bump_on;
        else if (wp == 'W') wbuffer = !wbuffer;
        else if (wp == ' ') paused = !paused;
        else if (wp == 'T') { detail = (detail + 1) % 3; build_torus(detail_rings[detail], detail_sides[detail]); }
        return 0;
    }
    if (msg == 0x0020 && !windowed) { SetCursor(0); return 1; }   /* WM_SETCURSOR: no pointer over the full screen demo */
    return DefWindowProcA(window, msg, wp, lp);
}

static int parse_number(const char **s)
{
    int v = 0;
    while (**s >= '0' && **s <= '9') v = v * 10 + (*(*s)++ - '0');
    return v;
}
static U frame_limit = 0;
static void parse_command_line(void)
{
    const char *s = GetCommandLineA();
    /* skip the program name, quoted or not */
    if (*s == '"') { ++s; while (*s && *s != '"') ++s; if (*s) ++s; }
    else while (*s && *s != ' ') ++s;
    while (*s)
    {
        while (*s == ' ') ++s;
        if (*s >= '0' && *s <= '9')
        {
            const int v = parse_number(&s);
            if (*s == 'x') { ++s; width = v; height = parse_number(&s); }
            else if (v == 16 || v == 32) depth = v;
        }
        else if (s[0] == 'f' && s[1] == 'r' && s[2] == 'a' && s[3] == 'm' && s[4] == 'e' && s[5] == 's' && s[6] == '=')
        { s += 7; frame_limit = (U)parse_number(&s); }
        else if (s[0] == 'd' && s[1] == 'e' && s[2] == 't' && s[3] == 'a' && s[4] == 'i' && s[5] == 'l' && s[6] == '=')
        { s += 7; detail = parse_number(&s) % 3; }
        else if (s[0] == 'n' && s[1] == 'o' && s[2] == 'v' && s[3] == 's' && s[4] == 'y' && s[5] == 'n' && s[6] == 'c')
        { s += 7; vsync = 0; }
        else if (s[0] == 'h' && s[1] == 'a' && s[2] == 'l' && (s[3] == ' ' || !s[3]))
        { s += 3; force_hal = 1; }
        else if (s[0] == 's' && s[1] == 't' && s[2] == 'i' && s[3] == 'l' && s[4] == 'l')
        { s += 5; still = 1; }
        else if (s[0] == 'w' && s[1] == 'i' && s[2] == 'n' && s[3] == 'd' && s[4] == 'o' && s[5] == 'w')
        { s += 6; windowed = 1; width = 480; height = 360; }
        else if (s[0] == 'n' && s[1] == 'o' && s[2] == 'd' && s[3] == 'x' && s[4] == 't')
        { s += 5; dxt = 0; }
        else if (s[0] == 'w' && s[1] == 'b' && s[2] == 'u' && s[3] == 'f' && s[4] == 'f' && s[5] == 'e' && s[6] == 'r')
        { s += 7; wbuffer = 1; }
        else if (s[0] == 'p' && s[1] == 'l' && s[2] == 'a' && s[3] == 'i' && s[4] == 'n')
        { s += 5; plain = 1; }
        else if (s[0] == 's' && s[1] == 't' && s[2] == 'a' && s[3] == 't' && s[4] == 'e' && s[5] == 'b')
        { s += 10; use_block = 1; }
        else if (s[0] == 'g' && s[1] == 'a' && s[2] == 'm' && s[3] == 'm' && s[4] == 'a' && s[5] == '=')
        { s += 6; gamma_percent = parse_number(&s); }
        while (*s && *s != ' ') ++s;
    }
}

void __stdcall mainCRTStartup(void)
{
    U library, window, value, i, frame = 0, desc[31], zdesc[31], caps[4], class_fields[10], message[7];
    U last_report, reported_frames = 0, fps_tenths = 0, frame_tenths_ms = 0, previous;
    Matrix view, projection;
    float t = 0.0f;

    parse_command_line();
    timeBeginPeriod(1);
    library = LoadLibraryA("DDRAW.DLL");
    if (!library) fail("LoadLibrary DDRAW.DLL", 0);
    {
        CreateEx create = (CreateEx)GetProcAddress(library, "DirectDrawCreateEx");
        if (!create) fail("GetProcAddress DirectDrawCreateEx", 0);
        value = create(0, &ddraw, IID_IDirectDraw7, 0);
        if (value || !ddraw) fail("DirectDrawCreateEx", value);
    }

    for (i = 0; i < 10; ++i) class_fields[i] = 0;
    class_fields[1] = (U)window_procedure;
    class_fields[9] = (U) "NEO98Demo3D";
    RegisterClassA(class_fields);
    if (windowed)
    {
        /* A normal overlapped window whose client area is the picture. */
        int r[4] = {0, 0, width, height};
        AdjustWindowRect(r, 0x00cf0000u, 0);   /* WS_OVERLAPPEDWINDOW */
        window = CreateWindowExA(0, "NEO98Demo3D", "NEO98 Direct3D Demo (window)", 0x10cf0000u, 40, 30,
                                 r[2] - r[0], r[3] - r[1], 0, 0, 0, 0);
    }
    else
        window = CreateWindowExA(0, "NEO98Demo3D", "NEO98 Direct3D Demo", 0x90000000u, 0, 0, width, height, 0, 0, 0, 0);
    if (!window) fail("CreateWindowEx", 0);
    main_window = window;
    ShowWindow(window, 5);
    SetForegroundWindow(window);

    if (windowed)
    {
        void *clipper = 0;
        value = CALL(ddraw, 20, M3)(ddraw, window, 0x8);   /* DDSCL_NORMAL */
        if (value) fail("SetCooperativeLevel (normal)", value);
        /* The primary is the whole screen; a clipper on the window keeps the
           blits inside what the window shows. */
        clear_desc(desc);
        desc[1] = 0x1;
        desc[26] = 0x200;   /* PRIMARYSURFACE */
        value = CALL(ddraw, 6, M4)(ddraw, (U)desc, (U)&primary, 0);
        if (value || !primary) fail("CreateSurface (primary)", value);
        value = CALL(ddraw, 4, M4)(ddraw, 0, (U)&clipper, 0);   /* CreateClipper */
        if (value || !clipper) fail("CreateClipper", value);
        value = CALL(clipper, 8, M3)(clipper, 0, window);        /* SetHWnd */
        if (value) fail("SetHWnd", value);
        value = CALL(primary, 28, M2)(primary, (U)clipper);      /* SetClipper */
        if (value) fail("SetClipper", value);
        /* The 3D target: an offscreen surface in the display's format. */
        clear_desc(desc);
        desc[1] = 0x1 | 0x2 | 0x4;
        desc[2] = (U)height;
        desc[3] = (U)width;
        desc[26] = 0x40 | 0x2000 | 0x4000;   /* OFFSCREENPLAIN 3DDEVICE VIDEOMEMORY */
        value = CALL(ddraw, 6, M4)(ddraw, (U)desc, (U)&back, 0);
        if (value || !back) fail("CreateSurface (window back buffer)", value);
        {
            /* The mode is the desktop's; the HUD names it. */
            U mode[31];
            clear_desc(mode);
            if (!CALL(ddraw, 12, M2)(ddraw, (U)mode)) depth = (int)mode[21];   /* GetDisplayMode */
        }
        say("windowed");
    }
    else
    {
        value = CALL(ddraw, 20, M3)(ddraw, window, 0x13);   /* EXCLUSIVE | FULLSCREEN | ALLOWREBOOT */
        if (value) fail("SetCooperativeLevel", value);
        value = CALL(ddraw, 21, M6)(ddraw, (U)width, (U)height, (U)depth, 0, 0);
        if (value) fail("SetDisplayMode", value);

        clear_desc(desc);
        desc[1] = 0x21;   /* CAPS | BACKBUFFERCOUNT */
        desc[5] = 1;
        desc[26] = 0x200 | 0x10 | 0x8 | 0x2000;   /* PRIMARYSURFACE FLIP COMPLEX 3DDEVICE */
        value = CALL(ddraw, 6, M4)(ddraw, (U)desc, (U)&primary, 0);
        if (value || !primary) fail("CreateSurface (primary)", value);
        caps[0] = 0x4; caps[1] = caps[2] = caps[3] = 0;   /* DDSCAPS_BACKBUFFER */
        value = CALL(primary, 12, M3)(primary, (U)caps, (U)&back);
        if (value || !back) fail("GetAttachedSurface (back buffer)", value);
    }
    /* The gamma ramp of the primary, when the driver has one (DDCAPS2_PRIMARYGAMMA). */
    if (CALL(primary, 0, M3)(primary, (U)IID_IDirectDrawGammaControl, (U)&gamma)) gamma = 0;
    say(gamma ? "gamma control" : "no gamma control");
    if (gamma && gamma_percent != 100) set_gamma(gamma_percent);

    /* 24 bits of depth and 8 of stencil when the driver has them, else 16 bits
       of depth. */
    clear_desc(zdesc);
    zdesc[1] = 0x1 | 0x2 | 0x4 | 0x1000;
    zdesc[2] = (U)height;
    zdesc[3] = (U)width;
    zdesc[18] = 32;
    zdesc[19] = 0x400 | 0x4000;   /* DDPF_ZBUFFER | DDPF_STENCILBUFFER */
    zdesc[21] = 32;
    zdesc[22] = 8;
    zdesc[23] = 0x00ffffff;
    zdesc[24] = 0xff000000u;
    zdesc[26] = 0x20000 | 0x4000;   /* ZBUFFER | VIDEOMEMORY */
    value = CALL(ddraw, 6, M4)(ddraw, (U)zdesc, (U)&zbuffer, 0);
    stencil = !value && zbuffer;
    if (!stencil)
    {
        zdesc[19] = 0x400;
        zdesc[21] = 16;
        zdesc[22] = 0;
        zdesc[23] = 0xffff;
        zdesc[24] = 0;
        value = CALL(ddraw, 6, M4)(ddraw, (U)zdesc, (U)&zbuffer, 0);
    }
    if (value || !zbuffer) fail("CreateSurface (Z buffer)", value);
    say(stencil ? "Z buffer D24S8" : "Z buffer D16");
    value = CALL(back, 3, M2)(back, (U)zbuffer);
    if (value) fail("AddAttachedSurface (Z buffer)", value);

    value = CALL(ddraw, 0, M3)(ddraw, (U)IID_IDirect3D7, (U)&d3d);
    if (value || !d3d) fail("QueryInterface IDirect3D7", value);
    /* The transform-and-lighting device when the driver offers it, the
       rasterizing HAL device otherwise (or with "hal" on the command line). */
    value = 1;
    if (!force_hal) value = CALL(d3d, 4, M4)(d3d, (U)IID_IDirect3DTnLHalDevice, (U)back, (U)&device);
    tnl = !value && device;
    if (!tnl) value = CALL(d3d, 4, M4)(d3d, (U)IID_IDirect3DHALDevice, (U)back, (U)&device);
    if (value || !device) fail("CreateDevice (HAL)", value);
    say(tnl ? "device TnL HAL" : "device HAL");

    {
        U viewport[6];
        viewport[0] = 0; viewport[1] = 0; viewport[2] = (U)width; viewport[3] = (U)height;
        viewport[4] = float_bits(0.0f); viewport[5] = float_bits(1.0f);
        value = CALL(device, 13, M2)(device, (U)viewport);
        if (value) fail("SetViewport", value);
    }
    perspective(&projection, PI / 3.2f, (float)width / (float)height, 0.5f, 60.0f);
    CALL(device, 11, M3)(device, 3, (U)projection.m);

    torus_texture = create_texture(256, 256, 1);
    /* The floor in DXT1 when the driver offers it, 32-bit ARGB otherwise. */
    floor_texture = dxt ? create_dxt1(256, 256, 9) : 0;
    floor_dxt = floor_texture != 0;
    if (!floor_texture) floor_texture = create_texture(256, 256, 9);
    say(floor_dxt ? "floor DXT1" : "floor ARGB");
    glow_texture = create_texture(64, 64, 1);
    cube_texture = create_cube(64);
    bump_texture = create_bump(64);
    glow8_texture = create_glow8(64);
    say(cube_texture ? "cube map" : "no cube map");
    say(bump_texture ? "bump map" : "no bump map");
    say(glow8_texture ? "palette alpha" : "no palette alpha");
    env_on = cube_texture != 0 && !plain;
    bump_on = bump_texture != 0 && !plain;
    fill_all_textures();
    build_font();

    build_torus(detail_rings[detail], detail_sides[detail]);
    build_cube();
    build_floor();
    for (i = 0; i < PARTICLES; ++i)
    {
        particle_indices[i * 6 + 0] = (W)(i * 4); particle_indices[i * 6 + 1] = (W)(i * 4 + 1);
        particle_indices[i * 6 + 2] = (W)(i * 4 + 2); particle_indices[i * 6 + 3] = (W)(i * 4);
        particle_indices[i * 6 + 4] = (W)(i * 4 + 2); particle_indices[i * 6 + 5] = (W)(i * 4 + 3);
        spawn(&particles[i]);
        particles[i].life *= random01();
    }

    render_state(7, 1);     /* ZENABLE */
    render_state(22, 1);    /* CULLMODE NONE for the billboards and text; lit meshes are closed anyway */
    render_state(143, 1);   /* NORMALIZENORMALS, the cubes are scaled */
    render_state(34, 0xff101828);   /* FOGCOLOR, the clear colour */
    render_state(140, 3);   /* FOGVERTEXMODE LINEAR */
    render_state(35, 0);    /* FOGTABLEMODE NONE */
    render_state(36, float_bits(6.0f));    /* FOGSTART */
    render_state(37, float_bits(22.0f));   /* FOGEND */
    stage_state(0, 1, 4);   /* COLOROP MODULATE */
    stage_state(0, 2, 2);   /* COLORARG1 TEXTURE */
    stage_state(0, 3, 0);   /* COLORARG2 DIFFUSE */
    stage_state(0, 4, 4);   /* ALPHAOP MODULATE */
    stage_state(0, 5, 2);
    stage_state(0, 6, 0);
    stage_state(0, 16, 2);  /* MAGFILTER LINEAR */
    stage_state(0, 17, 2);  /* MINFILTER LINEAR */
    setup_lights();
    if (use_block)
    {
        /* Record the floor's world matrix and material once; on the
           transform-and-lighting device the driver keeps them in the block. */
        Matrix m;
        U value = CALL(device, 22, M1)(device);   /* BeginStateBlock */
        if (value) fail("BeginStateBlock", value);
        identity(&m);
        set_world(&m);
        set_material(0.9f, 0.9f, 0.95f, 0.0f, 1.0f, 0.0f);
        value = CALL(device, 23, M2)(device, (U)&floor_block);   /* EndStateBlock */
        if (value || !floor_block) fail("EndStateBlock", value);
        say("state block");
    }
    say("started");

    previous = last_report = timeGetTime();
    while (!quit)
    {
        U now, elapsed;
        float dt;
        while (PeekMessageA(message, 0, 0, 0, 1))
        {
            if (message[1] == 0x0012) quit = 1;   /* WM_QUIT */
            TranslateMessage(message);
            DispatchMessageA(message);
        }
        if (quit) break;

        now = timeGetTime();
        dt = (float)(int)(now - previous) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
        previous = now;
        if (still) t = 2.0f;   /* one fixed picture, to compare devices */
        else if (!paused)
        {
            t += dt;
            update_particles(dt);
        }
        look_at(&view, fcos(t * 0.15f) * 7.0f, 2.2f + fsin(t * 0.23f) * 0.8f, fsin(t * 0.15f) * 7.0f, 0.0f, 0.0f, 0.0f);
        CALL(device, 11, M3)(device, 2, (U)view.m);

        /* Clear target and Z, and the stencil when there is one. */
        value = CALL(device, 10, M7)(device, 0, 0, stencil ? 0x7 : 0x3, 0xff101828u, float_bits(1.0f), 0);
        if (value) fail("Clear", value);
        value = CALL(device, 5, M1)(device);
        if (value) fail("BeginScene", value);
        triangles = draws = 0;
        draw_scene(t);
        draw_overlay(fps_tenths, frame_tenths_ms);
        value = CALL(device, 6, M1)(device);
        if (value) fail("EndScene", value);
        if (windowed)
        {
            /* The client area on the screen, and the picture into it. */
            int r[4], origin[2] = {0, 0};
            GetClientRect(main_window, r);
            ClientToScreen(main_window, origin);
            r[0] += origin[0]; r[2] += origin[0]; r[1] += origin[1]; r[3] += origin[1];
            if (vsync) CALL(ddraw, 22, M3)(ddraw, 1, 0);   /* WaitForVerticalBlank BLOCKBEGIN */
            value = CALL(primary, 5, M6)(primary, (U)r, (U)back, 0, 0x1000000, 0);   /* Blt, DDBLT_WAIT */
        }
        else
            value = CALL(primary, 11, M3)(primary, 0, vsync ? 0x1 : 0x1 | 0x8);   /* Flip, DDFLIP_WAIT [| NOVSYNC] */
        if (value == 0x887601c2u)   /* DDERR_SURFACELOST */
        {
            CALL(ddraw, 25, M1)(ddraw);   /* RestoreAllSurfaces */
            fill_all_textures();
            build_font();
        }
        else if (value) fail("Flip", value);
        ++frame;
        ++reported_frames;

        elapsed = timeGetTime() - last_report;
        if (elapsed >= 500)
        {
            char line[96], *at;
            fps_tenths = reported_frames * 10000 / elapsed;
            frame_tenths_ms = elapsed * 10 / reported_frames;
            at = put_text(line, "fps=");
            at = put_tenths(at, fps_tenths);
            at = put_text(at, " frame=");
            at = put_number(at, frame);
            at = put_text(at, " triangles=");
            put_number(at, triangles);
            say(line);
            reported_frames = 0;
            last_report += elapsed;
        }
        if (frame_limit && frame >= frame_limit) break;
    }
    say("done");
    if (gamma) set_gamma(100);
    if (!windowed) CALL(ddraw, 19, M1)(ddraw);   /* RestoreDisplayMode */
    ExitProcess(0);
}
