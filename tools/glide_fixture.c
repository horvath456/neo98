/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest side of tools/test_glide_guest.py: a Win32 program that draws with
   Glide 2 through GLIDE2X.DLL, the way Need for Speed II SE binds it:
   LoadLibraryA("glide2x.dll") and GetProcAddress for every decorated name.

   The declarations below are written for this test from the interface of
   Glide 2 : the values of the constants, the layout of
   GrVertex, GrTexInfo and GrLfbInfo_t. No 3dfx header is used.

   A 640x480 window, grSstWinOpen at 640x480, then two frames. The first is
   cleared red and swapped, so the second has to replace it. The second
   draws twelve patches of 100x100 pixels in a grid of 160x160 cells, each
   testing one part of the pixel pipeline (see the table in the test), writes
   one patch through the linear frame buffer and swaps. Then it reads pixels
   of the front buffer back with grLfbReadRegion and a read lock. With
   INTERACTIVE it keeps drawing until Escape, with FRAMETIMES it also keeps
   its frame times (tools/pause_frame_times.h). Every answer goes out through
   OutputDebugStringA as "GLIDE name=value". */
typedef unsigned U;
typedef unsigned char B;
#define API __declspec(dllimport) __stdcall
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API U LoadLibraryA(const char *);
API void *GetProcAddress(U, const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, int);
API U GetMessageA(void *, U, U, U);
API U PeekMessageA(void *, U, U, U, U);
API U TranslateMessage(const void *);
API U DispatchMessageA(const void *);
API void PostQuitMessage(int);
API short GetAsyncKeyState(int);
#ifdef FRAMETIMES
#include "pause_frame_times.h"
#endif

int _fltused = 1;

typedef struct { float sow, tow, oow; } TmuVertex;
typedef struct
{
    float x, y, z, r, g, b, ooz, a, oow;
    TmuVertex tmu[2];
} GrVertex;
typedef struct { int small_lod, large_lod, aspect, format; void *data; } GrTexInfo;
typedef struct { int size; void *lfb; U stride, write_mode, origin; } GrLfbInfo;
typedef struct { B y[16]; short i[4][3], q[4][3]; U packed[12]; } NccTable;

/* The constants this program uses. */
enum
{
    TMU0 = 0, TMU1 = 1,
    FUNCTION_ZERO = 0, FUNCTION_LOCAL = 1, FUNCTION_SCALE_OTHER = 3, FUNCTION_BLEND = 7,
    FACTOR_ZERO = 0, FACTOR_LOCAL = 1, FACTOR_ONE = 8,
    LOCAL_ITERATED = 0, LOCAL_CONSTANT = 1, OTHER_ITERATED = 0, OTHER_TEXTURE = 1, OTHER_CONSTANT = 2,
    CMP_LESS = 1, CMP_GREATER = 4, CMP_ALWAYS = 7,
    BLEND_ZERO = 0, BLEND_SRC_ALPHA = 1, BLEND_ONE = 4, BLEND_ONE_MINUS_SRC_ALPHA = 5,
    DEPTH_DISABLE = 0, DEPTH_Z = 1, DEPTH_W = 2,
    FOG_DISABLE = 0, FOG_TABLE = 2,
    LOD_256 = 0, LOD_64 = 2, LOD_16 = 4, LOD_8 = 5, LOD_1 = 8,
    ASPECT_1x1 = 3,
    FMT_P8 = 5, FMT_RGB565 = 0xa,
    TABLE_PALETTE = 2,
    LEVELS_BOTH = 3,
    BUFFER_FRONT = 0, BUFFER_BACK = 1,
    LFB_READ = 0, LFB_WRITE = 1, LFB_565 = 0, ORIGIN_UPPER_LEFT = 0,
    RES_640x480 = 7, REFRESH_60 = 0, COLOR_ARGB = 0,
    MIPMAP_DISABLE = 0, MIPMAP_NEAREST = 1,
    FILTER_POINT = 0, CLAMP_CLAMP = 1,
};

#define GLIDE_FUNCTIONS(X) \
    X(void, grGlideInit, (void), "_grGlideInit@0") \
    X(void, grGlideGetVersion, (char *), "_grGlideGetVersion@4") \
    X(int, grSstQueryHardware, (void *), "_grSstQueryHardware@4") \
    X(void, grSstSelect, (int), "_grSstSelect@4") \
    X(int, grSstWinOpen, (U, int, int, int, int, int, int), "_grSstWinOpen@28") \
    X(void, grBufferClear, (U, U, U), "_grBufferClear@12") \
    X(void, grBufferSwap, (int), "_grBufferSwap@4") \
    X(void, grDrawTriangle, (const GrVertex *, const GrVertex *, const GrVertex *), "_grDrawTriangle@12") \
    X(void, grColorCombine, (int, int, int, int, int), "_grColorCombine@20") \
    X(void, grAlphaCombine, (int, int, int, int, int), "_grAlphaCombine@20") \
    X(void, grTexCombine, (int, int, int, int, int, int, int), "_grTexCombine@28") \
    X(void, grConstantColorValue, (U), "_grConstantColorValue@4") \
    X(void, grAlphaBlendFunction, (int, int, int, int), "_grAlphaBlendFunction@16") \
    X(void, grAlphaTestFunction, (int), "_grAlphaTestFunction@4") \
    X(void, grAlphaTestReferenceValue, (U), "_grAlphaTestReferenceValue@4") \
    X(void, grChromakeyMode, (int), "_grChromakeyMode@4") \
    X(void, grChromakeyValue, (U), "_grChromakeyValue@4") \
    X(void, grDepthBufferMode, (int), "_grDepthBufferMode@4") \
    X(void, grDepthBufferFunction, (int), "_grDepthBufferFunction@4") \
    X(void, grDepthMask, (int), "_grDepthMask@4") \
    X(void, grGammaCorrectionValue, (float), "_grGammaCorrectionValue@4") \
    X(void, grFogMode, (int), "_grFogMode@4") \
    X(void, grFogColorValue, (U), "_grFogColorValue@4") \
    X(void, grFogTable, (const B *), "_grFogTable@4") \
    X(void, grCullMode, (int), "_grCullMode@4") \
    X(void, grClipWindow, (U, U, U, U), "_grClipWindow@16") \
    X(void, grColorMask, (int, int), "_grColorMask@8") \
    X(U, grTexMinAddress, (int), "_grTexMinAddress@4") \
    X(U, grTexMaxAddress, (int), "_grTexMaxAddress@4") \
    X(U, grTexCalcMemRequired, (int, int, int, int), "_grTexCalcMemRequired@16") \
    X(void, grTexSource, (int, U, U, GrTexInfo *), "_grTexSource@16") \
    X(void, grTexDownloadMipMap, (int, U, U, GrTexInfo *), "_grTexDownloadMipMap@16") \
    X(void, grTexDownloadTable, (int, int, const void *), "_grTexDownloadTable@12") \
    X(void, grTexFilterMode, (int, int, int), "_grTexFilterMode@12") \
    X(void, grTexClampMode, (int, int, int), "_grTexClampMode@12") \
    X(void, grTexMipMapMode, (int, int, int), "_grTexMipMapMode@12") \
    X(int, grLfbLock, (U, int, int, int, int, GrLfbInfo *), "_grLfbLock@24") \
    X(int, grLfbUnlock, (U, int), "_grLfbUnlock@8") \
    X(int, grLfbReadRegion, (int, U, U, U, U, U, void *), "_grLfbReadRegion@28") \
    X(void, grSstWinClose, (void), "_grSstWinClose@0")

#define DECLARE(ret, name, args, export) typedef ret(__stdcall *name##_t) args; static name##_t name;
GLIDE_FUNCTIONS(DECLARE)

static char line[256];
static void text(const char *label, const char *value)
{
    U at = 0, i = 0;
    const char *prefix = "GLIDE ";
    while (prefix[i]) line[at++] = prefix[i++];
    for (i = 0; label[i]; ++i) line[at++] = label[i];
    line[at++] = '=';
    for (i = 0; value[i] && at < sizeof line - 2; ++i) line[at++] = value[i] == ' ' ? '_' : value[i];
    line[at] = 0;
    OutputDebugStringA(line);
}
static void number(const char *label, U value)
{
    static const char digits[] = "0123456789abcdef";
    char v[9];
    U i;
    for (i = 0; i < 8; ++i) v[i] = digits[(value >> (28 - i * 4)) & 15];
    v[8] = 0;
    text(label, v);
}
static void fail(U step)
{
    number("fail", step);
    ExitProcess(1);
}

/* A vertex with colour, alpha, depth and texture coordinates for both
   TMUs; s and t in the 0 to 256 range of Glide, already over w. */
static GrVertex vertex(float x, float y, U rgb, float a, float ooz, float oow, float s, float t)
{
    GrVertex v;
    v.x = x; v.y = y; v.z = 0.0f;
    v.r = (float)((rgb >> 16) & 255); v.g = (float)((rgb >> 8) & 255); v.b = (float)(rgb & 255);
    v.a = a; v.ooz = ooz; v.oow = oow;
    v.tmu[0].sow = s * oow; v.tmu[0].tow = t * oow; v.tmu[0].oow = oow;
    v.tmu[1] = v.tmu[0];
    return v;
}
/* A rectangle as two triangles, with texture coordinates 0 to 256 across it. */
static void rect(float x, float y, float w, float h, U rgb, float a, float ooz, float oow)
{
    GrVertex p0 = vertex(x, y, rgb, a, ooz, oow, 0.0f, 0.0f);
    GrVertex p1 = vertex(x + w, y, rgb, a, ooz, oow, 256.0f, 0.0f);
    GrVertex p2 = vertex(x + w, y + h, rgb, a, ooz, oow, 256.0f, 256.0f);
    GrVertex p3 = vertex(x, y + h, rgb, a, ooz, oow, 0.0f, 256.0f);
    grDrawTriangle(&p0, &p1, &p2);
    grDrawTriangle(&p0, &p2, &p3);
}
static float cell_x(U i) { return (float)((i % 4) * 160 + 30); }
static float cell_y(U i) { return (float)((i / 4) * 160 + 30); }

/* Plain iterated colour, no texture, no tests. */
static void plain(void)
{
    grColorCombine(FUNCTION_LOCAL, FACTOR_ZERO, LOCAL_ITERATED, OTHER_ITERATED, 0);
    grAlphaCombine(FUNCTION_LOCAL, FACTOR_ZERO, LOCAL_ITERATED, OTHER_ITERATED, 0);
    grAlphaBlendFunction(BLEND_ONE, BLEND_ZERO, BLEND_ONE, BLEND_ZERO);
    grAlphaTestFunction(CMP_ALWAYS);
    grChromakeyMode(0);
    grDepthBufferMode(DEPTH_DISABLE);
    grFogMode(FOG_DISABLE);
}
/* The texture of TMU 0 as the colour, alpha iterated. */
static void decal(void)
{
    grColorCombine(FUNCTION_SCALE_OTHER, FACTOR_ONE, LOCAL_ITERATED, OTHER_TEXTURE, 0);
    grAlphaCombine(FUNCTION_LOCAL, FACTOR_ZERO, LOCAL_ITERATED, OTHER_ITERATED, 0);
    grTexCombine(TMU0, FUNCTION_LOCAL, FACTOR_ZERO, FUNCTION_LOCAL, FACTOR_ZERO, 0, 0);
}

/* Texture memory contents, built once. */
static unsigned short halves[8 * 8];      /* 565: left half blue, right half yellow */
static unsigned short keyed[8 * 8];       /* 565: left half magenta, right half green */
static unsigned short solid0[8 * 8];      /* 565 for TMU 0 */
static unsigned short solid1[8 * 8];      /* 565 for TMU 1 */
static unsigned short cyan[8 * 8];
static B mip[64 * 64 + 32 * 32 + 16 * 16 + 8 * 8 + 4 * 4 + 2 * 2 + 1];   /* P8: level n is index n */
static U palette[256];

static void fill(unsigned short *p, U n, unsigned short left, unsigned short right)
{
    U i;
    for (i = 0; i < n; ++i) p[i] = (i % 8) < 4 ? left : right;
}

static U mip_address, second_address, tmu1_address;

static void load_textures(void)
{
    GrTexInfo info;
    U i, at, size, level;
    fill(halves, 64, 0x001f, 0xffe0);
    fill(keyed, 64, 0xf81f, 0x07e0);
    fill(solid0, 64, 0xfc00, 0xfc00);   /* 255, 128, 0 */
    fill(solid1, 64, 0x87ff, 0x87ff);   /* 132, 255, 255 */
    fill(cyan, 64, 0x07ff, 0x07ff);
    for (i = 0; i < 256; ++i) palette[i] = 0;
    palette[0] = 0xff0000; palette[1] = 0x00ff00; palette[2] = 0x0000ff;
    palette[3] = 0xffff00; palette[4] = 0xff00ff; palette[5] = 0x00ffff; palette[6] = 0xffffff;
    for (at = 0, size = 64, level = 0; size; size >>= 1, ++level)
        for (i = 0; i < size * size; ++i) mip[at++] = (B)level;
    grTexDownloadTable(TMU0, TABLE_PALETTE, palette);

    number("min", grTexMinAddress(TMU0));
    number("max", grTexMaxAddress(TMU0));
    /* The mipmap from 64x64 to 1x1 at a start address away from zero, the
       565 textures behind it, each where grTexCalcMemRequired says. */
    mip_address = 0x10000;
    number("mipsize", grTexCalcMemRequired(LOD_1, LOD_64, ASPECT_1x1, FMT_P8));
    info.small_lod = LOD_1; info.large_lod = LOD_64; info.aspect = ASPECT_1x1; info.format = FMT_P8; info.data = mip;
    grTexDownloadMipMap(TMU0, mip_address, LEVELS_BOTH, &info);
    second_address = mip_address + grTexCalcMemRequired(LOD_1, LOD_64, ASPECT_1x1, FMT_P8);
    info.small_lod = LOD_8; info.large_lod = LOD_8; info.format = FMT_RGB565;
    number("size565", grTexCalcMemRequired(LOD_8, LOD_8, ASPECT_1x1, FMT_RGB565));
    info.data = halves; grTexDownloadMipMap(TMU0, second_address, LEVELS_BOTH, &info);
    info.data = keyed; grTexDownloadMipMap(TMU0, second_address + 128, LEVELS_BOTH, &info);
    info.data = solid0; grTexDownloadMipMap(TMU0, second_address + 256, LEVELS_BOTH, &info);
    info.data = cyan; grTexDownloadMipMap(TMU0, second_address + 384, LEVELS_BOTH, &info);
    tmu1_address = 0;
    info.data = solid1; grTexDownloadMipMap(TMU1, tmu1_address, LEVELS_BOTH, &info);
}

static void source565(int tmu, U address)
{
    GrTexInfo info;
    info.small_lod = LOD_8; info.large_lod = LOD_8; info.aspect = ASPECT_1x1; info.format = FMT_RGB565; info.data = 0;
    grTexSource(tmu, address, LEVELS_BOTH, &info);
    grTexFilterMode(tmu, FILTER_POINT, FILTER_POINT);
    grTexClampMode(tmu, CLAMP_CLAMP, CLAMP_CLAMP);
    grTexMipMapMode(tmu, MIPMAP_DISABLE, 0);
}

static void scene(void)
{
    static B fog[64];
    GrLfbInfo lfb;
    U i, x, y;
    /* The background, alpha 0, depth 0: the depth buffer clears only
       while depth writes are on. */
    grDepthMask(1);
    grBufferClear(0x000040, 0, 0);
    grDepthMask(0);

    /* 0: a flat red rectangle, and right of it in the margin a red triangle
       whose slanted edge shows the resolution the driver renders at. */
    plain();
    rect(cell_x(0), cell_y(0), 100, 100, 0xff0000, 255, 0, 1);
    {
        GrVertex a = vertex(135, 30, 0xff0000, 255, 0, 1, 0, 0);
        GrVertex b = vertex(185, 30, 0xff0000, 255, 0, 1, 0, 0);
        GrVertex c = vertex(135, 130, 0xff0000, 255, 0, 1, 0, 0);
        grDrawTriangle(&a, &b, &c);
    }

    /* 1: a texture, left half blue, right half yellow. */
    decal();
    source565(TMU0, second_address);
    rect(cell_x(1), cell_y(1), 100, 100, 0xffffff, 255, 0, 1);

    /* 2: two TMUs, TMU 1 feeding TMU 0, which multiplies. */
    source565(TMU0, second_address + 256);
    source565(TMU1, tmu1_address);
    grTexCombine(TMU1, FUNCTION_LOCAL, FACTOR_ZERO, FUNCTION_LOCAL, FACTOR_ZERO, 0, 0);
    grTexCombine(TMU0, FUNCTION_SCALE_OTHER, FACTOR_LOCAL, FUNCTION_SCALE_OTHER, FACTOR_LOCAL, 0, 0);
    grColorCombine(FUNCTION_SCALE_OTHER, FACTOR_ONE, LOCAL_ITERATED, OTHER_TEXTURE, 0);
    rect(cell_x(2), cell_y(2), 100, 100, 0xffffff, 255, 0, 1);
    grTexCombine(TMU1, FUNCTION_ZERO, FACTOR_ZERO, FUNCTION_ZERO, FACTOR_ZERO, 0, 0);

    /* 3: the Z buffer; the near red one first, the far blue one hides behind it. */
    plain();
    grDepthBufferMode(DEPTH_Z);
    grDepthBufferFunction(CMP_GREATER);
    grDepthMask(1);
    rect(cell_x(3), cell_y(3), 60, 100, 0xff0000, 255, 60000.0f, 1);
    rect(cell_x(3) + 40, cell_y(3), 60, 100, 0x0000ff, 255, 1000.0f, 1);

    /* 4: the W buffer, the same with w 1 and w 10, in a cell whose depth is
       cleared to the farthest value with the colour masked off. */
    grClipWindow((U)cell_x(4), (U)cell_y(4), (U)cell_x(4) + 100, (U)cell_y(4) + 100);
    grColorMask(0, 0);
    grBufferClear(0, 0, 0xffff);
    grColorMask(1, 0);
    grClipWindow(0, 0, 640, 480);
    grDepthBufferMode(DEPTH_W);
    grDepthBufferFunction(CMP_LESS);
    rect(cell_x(4), cell_y(4), 60, 100, 0xff0000, 255, 0, 1.0f);
    rect(cell_x(4) + 40, cell_y(4), 60, 100, 0x0000ff, 255, 0, 0.1f);
    grDepthBufferMode(DEPTH_DISABLE);
    grDepthMask(0);

    /* 5: fog from the table at w 256, whose index is 32: half white. */
    for (i = 0; i < 64; ++i) fog[i] = (B)(i * 4);
    grFogColorValue(0xffffff);
    grFogTable(fog);
    grFogMode(FOG_TABLE);
    rect(cell_x(5), cell_y(5), 100, 100, 0x000000, 255, 0, 1.0f / 256.0f);
    grFogMode(FOG_DISABLE);

    /* 6: the chroma key takes out the magenta half of the texture. */
    decal();
    source565(TMU0, second_address + 128);
    grChromakeyValue(0xff00ff);
    grChromakeyMode(1);
    rect(cell_x(6), cell_y(6), 100, 100, 0xffffff, 255, 0, 1);
    grChromakeyMode(0);

    /* 7: the alpha test drops alpha 100 and keeps alpha 200 against 128. */
    plain();
    grAlphaTestFunction(CMP_GREATER);
    grAlphaTestReferenceValue(128);
    rect(cell_x(7), cell_y(7), 50, 100, 0xffffff, 100, 0, 1);
    rect(cell_x(7) + 50, cell_y(7), 50, 100, 0xffffff, 200, 0, 1);
    grAlphaTestFunction(CMP_ALWAYS);

    /* 9: a mipmap of 64x64 on 16x16 pixels samples the 16x16 level, index 2. */
    decal();
    {
        GrTexInfo info;
        info.small_lod = LOD_1; info.large_lod = LOD_64; info.aspect = ASPECT_1x1; info.format = FMT_P8; info.data = 0;
        grTexSource(TMU0, mip_address, LEVELS_BOTH, &info);
        grTexFilterMode(TMU0, FILTER_POINT, FILTER_POINT);
        grTexMipMapMode(TMU0, MIPMAP_NEAREST, 0);
        for (y = 0; y < 6; ++y)
            for (x = 0; x < 6; ++x)
                rect(cell_x(9) + 2 + x * 16, cell_y(9) + 2 + y * 16, 16, 16, 0xffffff, 255, 0, 1);
        grTexMipMapMode(TMU0, MIPMAP_DISABLE, 0);
    }

    /* 10: the texture behind the mipmap in texture memory. */
    source565(TMU0, second_address + 384);
    rect(cell_x(10), cell_y(10), 100, 100, 0xffffff, 255, 0, 1);

    /* 11: alpha blending, red at alpha 128 over the background. */
    plain();
    grAlphaBlendFunction(BLEND_SRC_ALPHA, BLEND_ONE_MINUS_SRC_ALPHA, BLEND_ONE, BLEND_ZERO);
    rect(cell_x(11), cell_y(11), 100, 100, 0xff0000, 128, 0, 1);
    grAlphaBlendFunction(BLEND_ONE, BLEND_ZERO, BLEND_ONE, BLEND_ZERO);

    /* 8: green written through the linear frame buffer into the back buffer. */
    lfb.size = sizeof lfb;
    if (!grLfbLock(LFB_WRITE, BUFFER_BACK, LFB_565, ORIGIN_UPPER_LEFT, 0, &lfb)) fail(8);
    number("stride", lfb.stride);
    for (y = 0; y < 100; ++y)
    {
        unsigned short *row = (unsigned short *)((B *)lfb.lfb + (y + (U)cell_y(8)) * lfb.stride);
        for (x = 0; x < 100; ++x) row[x + (U)cell_x(8)] = 0x07e0;
    }
    if (!grLfbUnlock(LFB_WRITE, BUFFER_BACK)) fail(9);
}

static U window_proc(U hwnd, U message, U wparam, U lparam)
{
    if (message == 2) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

void mainCRTStartup(void)
{
    struct { U style; void *proc; int extra[2]; U instance, icon, cursor, brush; const char *menu, *name; } wc = {0};
    U message[7];
    U glide, hwnd, i;
    char version[80];
    U hardware[37];
    unsigned short pixels[16];
    GrLfbInfo lfb;

    glide = LoadLibraryA("glide2x.dll");
    number("module", glide);
    if (!glide) fail(1);
    /* A path to a glide2x.dll of the program's own is the same driver. */
    number("samepath", LoadLibraryA("C:\\GAME\\GLIDE2X.DLL") == glide);
#define RESOLVE(ret, name, args, export) name = (name##_t)GetProcAddress(glide, export); if (!name) { text("missing", export); fail(2); }
    GLIDE_FUNCTIONS(RESOLVE)

    wc.proc = (void *)window_proc;
    wc.name = "GlideFixture";
    RegisterClassA(&wc);
    hwnd = CreateWindowExA(0, "GlideFixture", "Glide", 0x80000000u | 0x10000000u, 0, 0, 640, 480, 0, 0, 0, 0);
    number("window", hwnd != 0);

    grGlideInit();
    grGlideGetVersion(version);
    text("version", version);
    for (i = 0; i < 37; ++i) hardware[i] = 0;
    number("query", grSstQueryHardware(hardware));
    number("boards", hardware[0]);
    number("type", hardware[1]);
    number("tmus", hardware[4]);
    number("tmuram", hardware[7]);
    grSstSelect(0);
    if (!grSstWinOpen(hwnd, RES_640x480, REFRESH_60, COLOR_ARGB, ORIGIN_UPPER_LEFT, 2, 1)) fail(3);
    number("open", 1);
    /* The Voodoo 3 driver opens with gamma 1.3; the colours below are the
       frame buffer's own. */
    grGammaCorrectionValue(1.0f);
    load_textures();
    grCullMode(0);

    /* Frame one: all red, which frame two has to replace. */
    grBufferClear(0xff0000, 0, 0);
    grBufferSwap(1);
    scene();
    grBufferSwap(1);
    number("frames", 2);

    /* The front buffer read back: the red patch, the green one written
       through the frame buffer, and the background. */
    if (!grLfbReadRegion(BUFFER_FRONT, 80, 80, 4, 1, 8, pixels)) fail(10);
    number("read_red", pixels[0]);
    if (!grLfbReadRegion(BUFFER_FRONT, 80 + 0, 320 + 80, 1, 1, 2, pixels)) fail(11);
    number("read_green", pixels[0]);
    lfb.size = sizeof lfb;
    if (!grLfbLock(LFB_READ, BUFFER_FRONT, LFB_565, ORIGIN_UPPER_LEFT, 0, &lfb)) fail(12);
    number("read_background", *(unsigned short *)((B *)lfb.lfb + 5 * lfb.stride + 5 * 2));
    grLfbUnlock(LFB_READ, BUFFER_FRONT);
    number("done", 1);

#ifdef INTERACTIVE
    for (;;)
    {
        while (PeekMessageA(message, 0, 0, 0, 1))
        {
            if (message[1] == 0x12) { grSstWinClose(); ExitProcess(0); }
            TranslateMessage(message);
            DispatchMessageA(message);
        }
        if (GetAsyncKeyState(0x1b) & 0x8000) break;
#ifdef FRAMETIMES
        frame_time();
#endif
        scene();
#ifdef MOVING
        /* For the test of saved states: a square that walks, so a picture
           taken later differs when the program still draws. */
        {
            static U walked;
            plain();
            rect((float)(walked % 560), 440, 30, 30, 0xffff00, 255, 0, 1);
            walked += 4;
        }
#endif
        grBufferSwap(1);
    }
    grSstWinClose();
    ExitProcess(0);
#else
    while (GetMessageA(message, 0, 0, 0) > 0)
    {
        TranslateMessage(message);
        DispatchMessageA(message);
    }
    ExitProcess(0);
#endif
}
