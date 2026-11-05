/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Direct3D 7, checked from inside a program, for both ways the port has: as
   an ARM64 program over the Direct3D of the SDK (sdk/dx/ddraw/d3d_*.c) and as
   an x86 program over the original D3DIM700.DLL and the Direct3D HAL of the
   display driver. The same source builds against the headers of the SDK and
   against those of mingw-w64.

   The program writes a report: every answer of an interface as a row
   "name=value", the pixels it reads back from the render target as rows
   "PIXEL", and where the picture has one right answer a check of its own
   ("WRONG" marks a pixel that is not what Direct3D defines). After every
   scene it writes the render target, read through Lock, as a picture.
   tools/test_arm64_d3d.py runs both forms and compares reports and pictures.

       D3DFIX.EXE report.txt pictures\ [hal|tnl]

    */
#define COBJMACROS
#define DIRECTDRAW_VERSION 0x0700
#define DIRECT3D_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define SIDE 256
static FILE *report;
static char folder[MAX_PATH];
static HWND window;
static LPDIRECTDRAW7 dd;
static LPDIRECT3D7 d3d;
static LPDIRECT3DDEVICE7 device;
static LPDIRECTDRAWSURFACE7 primary, target, depth;
static const GUID *device_class = &IID_IDirect3DHALDevice;
static unsigned frame[640 * 480];
static int frame_width, frame_height, wrong, scenes;
static DWORD base_block;

static void Say(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vfprintf(report, format, arguments);
    va_end(arguments);
    fflush(report);
}

static void Hr(const char *name, HRESULT result) { Say("%s=%08lx\n", name, (unsigned long)result); }

static unsigned Component(unsigned pixel, unsigned mask)
{
    unsigned shift = 0, width = 0, value;
    if (!mask) return 0;
    while (!(mask & 1)) { mask >>= 1; ++shift; }
    while (mask >> width) ++width;
    value = (pixel >> shift) & mask;
    /* The high bits again below, so that the largest value is 255. */
    return width >= 8 ? value >> (width - 8) : (value << (8 - width)) | (value >> (2 * width > 8 ? 2 * width - 8 : 0));
}

/* The pixels of a surface, read through Lock, into `frame` as 0xRRGGBB and
   into a picture in the folder of the run. */
static void Shoot(LPDIRECTDRAWSURFACE7 surface, const char *name)
{
    DDSURFACEDESC2 desc;
    char path[MAX_PATH];
    FILE *file;
    unsigned x, y, bytes;
    HRESULT result;
    memset(&desc, 0, sizeof desc);
    desc.dwSize = sizeof desc;
    result = IDirectDrawSurface7_Lock(surface, NULL, &desc, DDLOCK_WAIT | DDLOCK_READONLY, NULL);
    if (FAILED(result))
    {
        Say("SHOT %s lock=%08lx\n", name, (unsigned long)result);
        ++wrong;
        return;
    }
    frame_width = (int)desc.dwWidth;
    frame_height = (int)desc.dwHeight;
    bytes = desc.ddpfPixelFormat.dwRGBBitCount / 8;
    for (y = 0; y < desc.dwHeight && y < 480; ++y)
        for (x = 0; x < desc.dwWidth && x < 640; ++x)
        {
            unsigned pixel = 0;
            memcpy(&pixel, (const char *)desc.lpSurface + y * desc.lPitch + x * bytes, bytes);
            frame[y * 640 + x] = (Component(pixel, desc.ddpfPixelFormat.dwRBitMask) << 16) |
                                 (Component(pixel, desc.ddpfPixelFormat.dwGBitMask) << 8) | Component(pixel, desc.ddpfPixelFormat.dwBBitMask);
        }
    IDirectDrawSurface7_Unlock(surface, NULL);
    sprintf(path, "%s%s.PPM", folder, name);
    file = fopen(path, "wb");
    if (file)
    {
        static unsigned char row[640 * 3];
        fprintf(file, "P6\n%d %d\n255\n", frame_width, frame_height);
        for (y = 0; y < (unsigned)frame_height; ++y)
        {
            for (x = 0; x < (unsigned)frame_width; ++x)
            {
                row[x * 3] = (unsigned char)(frame[y * 640 + x] >> 16);
                row[x * 3 + 1] = (unsigned char)(frame[y * 640 + x] >> 8);
                row[x * 3 + 2] = (unsigned char)frame[y * 640 + x];
            }
            fwrite(row, 3, (size_t)frame_width, file);
        }
        fclose(file);
    }
    Say("SHOT %s %dx%d\n", name, frame_width, frame_height);
    ++scenes;
}

/* A pixel of the last picture into the report. */
static void Pixel(const char *name, int x, int y) { Say("PIXEL %s %d,%d=%06x\n", name, x, y, frame[y * 640 + x]); }

/* A pixel that has one right answer, each channel within `slack`. */
static void Want(const char *name, int x, int y, unsigned rgb, int slack)
{
    const unsigned got = frame[y * 640 + x];
    int k, bad = 0;
    for (k = 0; k < 3; ++k)
    {
        const int a = (int)((got >> (k * 8)) & 255), b = (int)((rgb >> (k * 8)) & 255);
        if (a - b > slack || b - a > slack) bad = 1;
    }
    if (bad)
    {
        Say("PIXEL %s %d,%d=%06x WRONG want %06x\n", name, x, y, got, rgb);
        ++wrong;
    }
    else Say("PIXEL %s %d,%d=%06x\n", name, x, y, slack ? rgb : got);
}

/* ----------------------------------------------------------------- vertices */
typedef struct { float x, y, z, rhw; DWORD color, specular; float u, v; } TL;
typedef struct { float x, y, z, rhw; DWORD color, specular; float u, v, u1, v1; } TL2;
typedef struct { float x, y, z; DWORD color; } LV;
typedef struct { float x, y, z, nx, ny, nz, u, v; } NV;
#define FVF_TL2 (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX2)
#define FVF_LV (D3DFVF_XYZ | D3DFVF_DIFFUSE)

static void QuadVertices(TL *v, float x0, float y0, float x1, float y1, float z, DWORD color)
{
    const TL q[6] = {{x0, y0, z, 1, color, 0, 0, 0}, {x1, y0, z, 1, color, 0, 1, 0}, {x0, y1, z, 1, color, 0, 0, 1},
                     {x1, y0, z, 1, color, 0, 1, 0}, {x1, y1, z, 1, color, 0, 1, 1}, {x0, y1, z, 1, color, 0, 0, 1}};
    memcpy(v, q, sizeof q);
}

/* A rectangle of transformed vertices, texture coordinates 0 to 1. */
static HRESULT Quad(float x0, float y0, float x1, float y1, float z, DWORD color)
{
    TL v[6];
    QuadVertices(v, x0, y0, x1, y1, z, color);
    return IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, v, 6, 0);
}

/* A rectangle in model space, x and y from -1 to 1 with y up. */
static HRESULT ModelQuad(float x0, float y0, float x1, float y1, float z, DWORD color)
{
    const LV v[6] = {{x0, y1, z, color}, {x1, y1, z, color}, {x0, y0, z, color}, {x1, y1, z, color}, {x1, y0, z, color}, {x0, y0, z, color}};
    return IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLELIST, FVF_LV, (void *)v, 6, 0);
}

static void RS(D3DRENDERSTATETYPE state, DWORD value) { IDirect3DDevice7_SetRenderState(device, state, value); }
static void TSS(DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD value) { IDirect3DDevice7_SetTextureStageState(device, stage, state, value); }
static DWORD F(float value) { DWORD bits; memcpy(&bits, &value, 4); return bits; }

/* The colour of a pixel comes from the vertex: no texture in the stages. */
static void Untextured(void)
{
    TSS(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    TSS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    TSS(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    TSS(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
}

/* The texel of the texture is the colour of the pixel, unfiltered. */
static void Textured(LPDIRECTDRAWSURFACE7 texture)
{
    IDirect3DDevice7_SetTexture(device, 0, texture);
    TSS(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    TSS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    TSS(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    TSS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    TSS(0, D3DTSS_MAGFILTER, D3DTFG_POINT);
    TSS(0, D3DTSS_MINFILTER, D3DTFN_POINT);
    TSS(0, D3DTSS_MIPFILTER, D3DTFP_NONE);
    TSS(0, D3DTSS_ADDRESS, D3DTADDRESS_CLAMP);
}

/* Every scene starts from the state the device had after its creation, with
   lighting, culling and the Z test off: a state block of everything. */
static void Begin(DWORD color)
{
    IDirect3DDevice7_ApplyStateBlock(device, base_block);
    IDirect3DDevice7_Clear(device, 0, NULL, D3DCLEAR_TARGET | (depth ? D3DCLEAR_ZBUFFER : 0), color, 1.0f, 0);
    IDirect3DDevice7_BeginScene(device);
}

static void End(const char *name)
{
    const HRESULT result = IDirect3DDevice7_EndScene(device);
    if (FAILED(result)) Hr("EndScene", result);
    Shoot(target, name);
}

/* ----------------------------------------------------------------- textures */
enum { ARGB8888, XRGB8888, RGB565, ARGB1555, ARGB4444, PAL8, DXT1, KINDS };
static const char *const kind_names[KINDS] = {"8888", "X888", "565", "1555", "4444", "PAL8", "DXT1"};

static void FormatOf(int kind, DDPIXELFORMAT *f)
{
    memset(f, 0, sizeof *f);
    f->dwSize = sizeof *f;
    f->dwFlags = DDPF_RGB;
    switch (kind)
    {
    case ARGB8888: f->dwFlags |= DDPF_ALPHAPIXELS; f->dwRGBAlphaBitMask = 0xff000000u; /* fall through */
    case XRGB8888: f->dwRGBBitCount = 32; f->dwRBitMask = 0xff0000; f->dwGBitMask = 0xff00; f->dwBBitMask = 0xff; break;
    case RGB565: f->dwRGBBitCount = 16; f->dwRBitMask = 0xf800; f->dwGBitMask = 0x07e0; f->dwBBitMask = 0x001f; break;
    case ARGB1555:
        f->dwFlags |= DDPF_ALPHAPIXELS; f->dwRGBBitCount = 16; f->dwRBitMask = 0x7c00; f->dwGBitMask = 0x03e0; f->dwBBitMask = 0x001f;
        f->dwRGBAlphaBitMask = 0x8000;
        break;
    case ARGB4444:
        f->dwFlags |= DDPF_ALPHAPIXELS; f->dwRGBBitCount = 16; f->dwRBitMask = 0x0f00; f->dwGBitMask = 0x00f0; f->dwBBitMask = 0x000f;
        f->dwRGBAlphaBitMask = 0xf000;
        break;
    case PAL8: f->dwFlags |= DDPF_PALETTEINDEXED8; f->dwRGBBitCount = 8; break;
    default: f->dwFlags = DDPF_FOURCC; f->dwFourCC = MAKEFOURCC('D', 'X', 'T', '1'); break;
    }
}

/* The test pattern: four quarters, red, green, blue and white; the white
   quarter has a quarter of the alpha. `level` recolours a whole mip level. */
static unsigned Pattern(unsigned x, unsigned y, unsigned side, unsigned level)
{
    static const unsigned levels[4] = {0, 0xff00ff00u, 0xff0000ffu, 0xffffffffu};
    const unsigned quarter = (x * 2 >= side ? 1u : 0u) + (y * 2 >= side ? 2u : 0u);
    static const unsigned quarters[4] = {0xffff0000u, 0xff00ff00u, 0xff0000ffu, 0x40ffffffu};
    return level ? levels[level & 3] : quarters[quarter];
}

static unsigned Encode(int kind, unsigned argb)
{
    const unsigned a = argb >> 24, r = (argb >> 16) & 255, g = (argb >> 8) & 255, b = argb & 255;
    switch (kind)
    {
    case RGB565: return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    case ARGB1555: return ((a >> 7) << 15) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
    case ARGB4444: return ((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
    case PAL8: return argb == 0xffff0000u ? 1 : argb == 0xff00ff00u ? 2 : argb == 0xff0000ffu ? 3 : 4;
    default: return argb;
    }
}

static void FillLevel(LPDIRECTDRAWSURFACE7 surface, int kind, unsigned level, unsigned (*texel)(unsigned, unsigned, unsigned, unsigned))
{
    DDSURFACEDESC2 desc;
    unsigned x, y;
    memset(&desc, 0, sizeof desc);
    desc.dwSize = sizeof desc;
    if (FAILED(IDirectDrawSurface7_Lock(surface, NULL, &desc, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL))) { Say("texture lock failed\n"); return; }
    if (kind == DXT1)
    {
        /* One colour per 4x4 block: both colours of the block the same. */
        const unsigned columns = (desc.dwWidth + 3) / 4, rows = (desc.dwHeight + 3) / 4;
        for (y = 0; y < rows; ++y)
            for (x = 0; x < columns; ++x)
            {
                const unsigned short c = (unsigned short)Encode(RGB565, texel(x * 4, y * 4, desc.dwWidth, level));
                unsigned char block[8] = {(unsigned char)c, (unsigned char)(c >> 8), (unsigned char)c, (unsigned char)(c >> 8), 0, 0, 0, 0};
                memcpy((char *)desc.lpSurface + (y * columns + x) * 8, block, 8);
            }
    }
    else
        for (y = 0; y < desc.dwHeight; ++y)
            for (x = 0; x < desc.dwWidth; ++x)
            {
                const unsigned value = Encode(kind, texel(x, y, desc.dwWidth, level)), bytes = desc.ddpfPixelFormat.dwRGBBitCount / 8;
                memcpy((char *)desc.lpSurface + y * desc.lPitch + x * bytes, &value, bytes);
            }
    IDirectDrawSurface7_Unlock(surface, NULL);
}

static LPDIRECTDRAWPALETTE pattern_palette;

/* A texture of the pattern. `caps2` chooses managed or not; `levels` above
   one makes a mip chain whose lower levels have one colour each. */
static LPDIRECTDRAWSURFACE7 Texture(int kind, unsigned side, unsigned levels, DWORD caps, DWORD caps2,
                                    unsigned (*texel)(unsigned, unsigned, unsigned, unsigned))
{
    DDSURFACEDESC2 desc;
    LPDIRECTDRAWSURFACE7 texture = NULL, level, next;
    DDSCAPS2 mip;
    unsigned n;
    HRESULT result;
    memset(&desc, 0, sizeof desc);
    desc.dwSize = sizeof desc;
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth = desc.dwHeight = side;
    desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | caps;
    desc.ddsCaps.dwCaps2 = caps2;
    FormatOf(kind, &desc.ddpfPixelFormat);
    if (levels > 1)
    {
        desc.dwFlags |= DDSD_MIPMAPCOUNT;
        desc.dwMipMapCount = levels;
        desc.ddsCaps.dwCaps |= DDSCAPS_MIPMAP | DDSCAPS_COMPLEX;
    }
    result = IDirectDraw7_CreateSurface(dd, &desc, &texture, NULL);
    if (FAILED(result))
    {
        Say("texture %s side=%u levels=%u create=%08lx\n", kind_names[kind], side, levels, (unsigned long)result);
        return NULL;
    }
    if (kind == PAL8)
    {
        if (!pattern_palette)
        {
            PALETTEENTRY colors[256];
            memset(colors, 0, sizeof colors);
            colors[1].peRed = 255; colors[2].peGreen = 255; colors[3].peBlue = 255;
            colors[4].peRed = colors[4].peGreen = colors[4].peBlue = 255;
            IDirectDraw7_CreatePalette(dd, DDPCAPS_8BIT | DDPCAPS_ALLOW256, colors, &pattern_palette, NULL);
        }
        Hr("texture palette", IDirectDrawSurface7_SetPalette(texture, pattern_palette));
    }
    level = texture;
    IDirectDrawSurface7_AddRef(level);
    for (n = 0; n < levels && level; ++n)
    {
        FillLevel(level, kind, n, texel);
        memset(&mip, 0, sizeof mip);
        mip.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_MIPMAP;
        next = NULL;
        IDirectDrawSurface7_GetAttachedSurface(level, &mip, &next);
        IDirectDrawSurface7_Release(level);
        level = next;
    }
    if (level) IDirectDrawSurface7_Release(level);
    return texture;
}

static unsigned Gray(unsigned x, unsigned y, unsigned side, unsigned level)
{
    (void)x; (void)y; (void)side; (void)level;
    return 0x80808080u;
}

/* ----------------------------------------------------------------- matrices */
static const D3DMATRIX identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static D3DMATRIX RotationY(float angle)
{
    D3DMATRIX m = identity;
    m._11 = (float)cos(angle); m._13 = -(float)sin(angle); m._31 = (float)sin(angle); m._33 = (float)cos(angle);
    return m;
}

static D3DMATRIX Perspective(float fov, float aspect, float zn, float zf)
{
    D3DMATRIX m;
    const float h = (float)(cos(fov / 2) / sin(fov / 2)), q = zf / (zf - zn);
    memset(&m, 0, sizeof m);
    m._11 = h / aspect; m._22 = h; m._33 = q; m._34 = 1; m._43 = -q * zn;
    return m;
}

static D3DMATRIX Translation(float x, float y, float z)
{
    D3DMATRIX m = identity;
    m._41 = x; m._42 = y; m._43 = z;
    return m;
}

static void SayMatrix(const char *name, const D3DMATRIX *m)
{
    const float *e = &m->_11;
    int i;
    Say("%s=", name);
    /* Compare the returned floats, without CRT decimal rounding. Signed
       zero is immaterial here, as in the previous decimal comparison. */
    for (i = 0; i < 16; ++i) {
        DWORD bits;
        const float value = e[i] == 0.0f ? 0.0f : e[i];
        memcpy(&bits, &value, sizeof bits);
        Say("%s%08lx", i ? " " : "", (unsigned long)bits);
    }
    Say("\n");
}

/* ------------------------------------------------------------------- set-up */
static HRESULT CALLBACK SayDevice(LPSTR description, LPSTR name, LPD3DDEVICEDESC7 desc, LPVOID context)
{
    (void)context;
    (void)description;
    Say("device %s hwtnl=%d\n", name, (desc->dwDevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) ? 1 : 0);
    return D3DENUMRET_OK;
}

static HRESULT CALLBACK SayZFormat(LPDDPIXELFORMAT format, LPVOID context)
{
    DDPIXELFORMAT *stencil = (DDPIXELFORMAT *)context;
    Say("zformat flags=%lx bits=%lu stencil=%lu zmask=%lx\n", (unsigned long)format->dwFlags, (unsigned long)format->dwZBufferBitDepth,
        (unsigned long)format->dwStencilBitDepth, (unsigned long)format->dwZBitMask);
    if ((format->dwFlags & DDPF_STENCILBUFFER) && stencil) *stencil = *format;
    return D3DENUMRET_OK;
}

static int texture_kinds[KINDS];
static HRESULT CALLBACK SayTextureFormat(LPDDPIXELFORMAT format, LPVOID context)
{
    int kind;
    (void)context;
    Say("texture format flags=%lx fourcc=%lx bits=%lu masks=%lx,%lx,%lx,%lx\n", (unsigned long)format->dwFlags,
        (unsigned long)format->dwFourCC, (unsigned long)format->dwRGBBitCount, (unsigned long)format->dwRBitMask,
        (unsigned long)format->dwGBitMask, (unsigned long)format->dwBBitMask,
        (unsigned long)((format->dwFlags & DDPF_ALPHAPIXELS) ? format->dwRGBAlphaBitMask : 0));
    for (kind = 0; kind < KINDS; ++kind)
    {
        DDPIXELFORMAT f;
        FormatOf(kind, &f);
        if (kind == DXT1 ? (format->dwFlags & DDPF_FOURCC) && format->dwFourCC == f.dwFourCC
                         : !(format->dwFlags & (DDPF_FOURCC | DDPF_BUMPDUDV)) && format->dwRGBBitCount == f.dwRGBBitCount &&
                               (format->dwFlags & (DDPF_ALPHAPIXELS | DDPF_PALETTEINDEXED8)) == (f.dwFlags & (DDPF_ALPHAPIXELS | DDPF_PALETTEINDEXED8)) &&
                               format->dwRBitMask == f.dwRBitMask && format->dwGBitMask == f.dwGBitMask)
            texture_kinds[kind] = 1;
    }
    return D3DENUMRET_OK;
}

static LRESULT CALLBACK Procedure(HWND h, UINT message, WPARAM wparam, LPARAM lparam) { return DefWindowProcA(h, message, wparam, lparam); }

/* A render target of SIDE x SIDE in the format of the display with a Z
   buffer of the given format attached. */
static HRESULT MakeTarget(LPDIRECTDRAWSURFACE7 *surface, LPDIRECTDRAWSURFACE7 *z, const DDPIXELFORMAT *z_format)
{
    DDSURFACEDESC2 desc;
    HRESULT result;
    memset(&desc, 0, sizeof desc);
    desc.dwSize = sizeof desc;
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.dwWidth = desc.dwHeight = SIDE;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY;
    result = IDirectDraw7_CreateSurface(dd, &desc, surface, NULL);
    if (FAILED(result) || !z) return result;
    desc.dwFlags |= DDSD_PIXELFORMAT;
    desc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER | DDSCAPS_VIDEOMEMORY;
    desc.ddpfPixelFormat = *z_format;
    result = IDirectDraw7_CreateSurface(dd, &desc, z, NULL);
    if (FAILED(result)) return result;
    return IDirectDrawSurface7_AddAttachedSurface(*surface, *z);
}

static DDPIXELFORMAT stencil_format;

static int Open(void)
{
    WNDCLASSA wc;
    DDSURFACEDESC2 desc;
    DDPIXELFORMAT z16;
    D3DDEVICEDESC7 caps;
    D3DVIEWPORT7 viewport;
    HRESULT result;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = Procedure;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "Neo98D3DFixture";
    RegisterClassA(&wc);
    window = CreateWindowExA(0, wc.lpszClassName, "Direct3D", WS_POPUP | WS_VISIBLE, 40, 40, 320, 240, NULL, NULL, wc.hInstance, NULL);
    result = DirectDrawCreateEx(NULL, (void **)&dd, &IID_IDirectDraw7, NULL);
    Hr("DirectDrawCreateEx", result);
    if (FAILED(result)) return 0;
    Hr("SetCooperativeLevel", IDirectDraw7_SetCooperativeLevel(dd, window, DDSCL_NORMAL));
    memset(&desc, 0, sizeof desc);
    desc.dwSize = sizeof desc;
    desc.dwFlags = DDSD_CAPS;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    Hr("primary", IDirectDraw7_CreateSurface(dd, &desc, &primary, NULL));
    result = IDirectDraw7_QueryInterface(dd, &IID_IDirect3D7, (void **)&d3d);
    Hr("QueryInterface IDirect3D7", result);
    if (FAILED(result)) return 0;
    Hr("EnumDevices", IDirect3D7_EnumDevices(d3d, SayDevice, NULL));
    Hr("EnumZBufferFormats", IDirect3D7_EnumZBufferFormats(d3d, device_class, SayZFormat, &stencil_format));
    memset(&z16, 0, sizeof z16);
    z16.dwSize = sizeof z16;
    z16.dwFlags = DDPF_ZBUFFER;
    z16.dwZBufferBitDepth = 16;
    z16.dwZBitMask = 0xffff;
    result = MakeTarget(&target, &depth, &z16);
    Hr("render target with Z buffer", result);
    if (FAILED(result)) return 0;
    result = IDirect3D7_CreateDevice(d3d, device_class, target, &device);
    Hr("CreateDevice", result);
    if (FAILED(result)) return 0;
    memset(&caps, 0, sizeof caps);
    Hr("GetCaps", IDirect3DDevice7_GetCaps(device, &caps));
    Say("caps hwtnl=%d stages=%u textures=%u lights=%lu planes=%u maxtexture=%lu fvf=%lx class=%d\n",
        (caps.dwDevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) ? 1 : 0, caps.wMaxTextureBlendStages, caps.wMaxSimultaneousTextures,
        (unsigned long)caps.dwMaxActiveLights, caps.wMaxUserClipPlanes, (unsigned long)caps.dwMaxTextureWidth,
        (unsigned long)(caps.dwFVFCaps & D3DFVFCAPS_TEXCOORDCOUNTMASK), IsEqualGUID(&caps.deviceGUID, device_class) ? 1 : 0);
    Hr("EnumTextureFormats", IDirect3DDevice7_EnumTextureFormats(device, SayTextureFormat, NULL));
    memset(&viewport, 0, sizeof viewport);
    Hr("GetViewport", IDirect3DDevice7_GetViewport(device, &viewport));
    Say("viewport %lu,%lu %lux%lu z %.2f..%.2f\n", (unsigned long)viewport.dwX, (unsigned long)viewport.dwY, (unsigned long)viewport.dwWidth,
        (unsigned long)viewport.dwHeight, (double)viewport.dvMinZ, (double)viewport.dvMaxZ);
    return 1;
}

/* ----------------------------------------------------------------- answers */
/* What a new device says about its state. */
static void Defaults(void)
{
    static const int states[] = {
        D3DRENDERSTATE_ANTIALIAS, D3DRENDERSTATE_TEXTUREPERSPECTIVE, D3DRENDERSTATE_ZENABLE, D3DRENDERSTATE_FILLMODE,
        D3DRENDERSTATE_SHADEMODE, D3DRENDERSTATE_LINEPATTERN, D3DRENDERSTATE_ZWRITEENABLE, D3DRENDERSTATE_ALPHATESTENABLE,
        D3DRENDERSTATE_LASTPIXEL, D3DRENDERSTATE_SRCBLEND, D3DRENDERSTATE_DESTBLEND, D3DRENDERSTATE_CULLMODE, D3DRENDERSTATE_ZFUNC,
        D3DRENDERSTATE_ALPHAREF, D3DRENDERSTATE_ALPHAFUNC, D3DRENDERSTATE_DITHERENABLE, D3DRENDERSTATE_ALPHABLENDENABLE,
        D3DRENDERSTATE_FOGENABLE, D3DRENDERSTATE_SPECULARENABLE, D3DRENDERSTATE_ZVISIBLE, D3DRENDERSTATE_STIPPLEDALPHA,
        D3DRENDERSTATE_FOGCOLOR, D3DRENDERSTATE_FOGTABLEMODE, D3DRENDERSTATE_FOGSTART, D3DRENDERSTATE_FOGEND, D3DRENDERSTATE_FOGDENSITY,
        D3DRENDERSTATE_EDGEANTIALIAS, D3DRENDERSTATE_COLORKEYENABLE, D3DRENDERSTATE_ZBIAS, D3DRENDERSTATE_RANGEFOGENABLE,
        D3DRENDERSTATE_STENCILENABLE, D3DRENDERSTATE_STENCILFAIL, D3DRENDERSTATE_STENCILZFAIL, D3DRENDERSTATE_STENCILPASS,
        D3DRENDERSTATE_STENCILFUNC, D3DRENDERSTATE_STENCILREF, D3DRENDERSTATE_STENCILMASK, D3DRENDERSTATE_STENCILWRITEMASK,
        D3DRENDERSTATE_TEXTUREFACTOR, D3DRENDERSTATE_WRAP0, D3DRENDERSTATE_WRAP7, D3DRENDERSTATE_CLIPPING, D3DRENDERSTATE_LIGHTING,
        D3DRENDERSTATE_EXTENTS, D3DRENDERSTATE_AMBIENT, D3DRENDERSTATE_FOGVERTEXMODE, D3DRENDERSTATE_COLORVERTEX,
        D3DRENDERSTATE_LOCALVIEWER, D3DRENDERSTATE_NORMALIZENORMALS, D3DRENDERSTATE_COLORKEYBLENDENABLE,
        D3DRENDERSTATE_DIFFUSEMATERIALSOURCE, D3DRENDERSTATE_SPECULARMATERIALSOURCE, D3DRENDERSTATE_AMBIENTMATERIALSOURCE,
        D3DRENDERSTATE_EMISSIVEMATERIALSOURCE, D3DRENDERSTATE_VERTEXBLEND, D3DRENDERSTATE_CLIPPLANEENABLE,
        /* The states of earlier interfaces. */
        D3DRENDERSTATE_TEXTUREHANDLE, D3DRENDERSTATE_TEXTUREADDRESS, D3DRENDERSTATE_TEXTUREMAG, D3DRENDERSTATE_TEXTUREMIN,
        D3DRENDERSTATE_TEXTUREMAPBLEND, D3DRENDERSTATE_BORDERCOLOR, D3DRENDERSTATE_TEXTUREADDRESSU, D3DRENDERSTATE_TEXTUREADDRESSV};
    D3DMATRIX matrix;
    D3DMATERIAL7 material;
    D3DLIGHT7 light;
    D3DVALUE plane[4] = {9, 9, 9, 9};
    LPDIRECTDRAWSURFACE7 texture = (LPDIRECTDRAWSURFACE7)(ULONG_PTR)1;
    BOOL enabled = 7;
    unsigned i, stage;
    for (i = 0; i < sizeof states / sizeof states[0]; ++i)
    {
        DWORD value = 0xdeadbeefu;
        const HRESULT result = IDirect3DDevice7_GetRenderState(device, (D3DRENDERSTATETYPE)states[i], &value);
        Say("default rs %d=%08lx %08lx\n", states[i], (unsigned long)result, (unsigned long)(FAILED(result) ? 0 : value));
    }
    for (stage = 0; stage < 8; ++stage)
        for (i = D3DTSS_COLOROP; i <= D3DTSS_TEXTURETRANSFORMFLAGS; ++i)
        {
            DWORD value = 0xdeadbeefu;
            const HRESULT result = IDirect3DDevice7_GetTextureStageState(device, stage, (D3DTEXTURESTAGESTATETYPE)i, &value);
            Say("default tss %u %u=%08lx %08lx\n", stage, i, (unsigned long)result, (unsigned long)(FAILED(result) ? 0 : value));
        }
    Hr("GetTransform world", IDirect3DDevice7_GetTransform(device, D3DTRANSFORMSTATE_WORLD, &matrix));
    SayMatrix("default world", &matrix);
    Hr("GetTransform projection", IDirect3DDevice7_GetTransform(device, D3DTRANSFORMSTATE_PROJECTION, &matrix));
    SayMatrix("default projection", &matrix);
    Hr("GetTransform texture 7", IDirect3DDevice7_GetTransform(device, D3DTRANSFORMSTATE_TEXTURE7, &matrix));
    memset(&material, 0x55, sizeof material);
    Hr("GetMaterial", IDirect3DDevice7_GetMaterial(device, &material));
    Say("default material diffuse=%.2f power=%.2f\n", (double)material.diffuse.r, (double)material.power);
    Hr("GetLight unset", IDirect3DDevice7_GetLight(device, 0, &light));
    Hr("GetLightEnable unset", IDirect3DDevice7_GetLightEnable(device, 0, &enabled));
    Hr("GetClipPlane", IDirect3DDevice7_GetClipPlane(device, 0, plane));
    Say("default clip plane %.1f %.1f %.1f %.1f\n", (double)plane[0], (double)plane[1], (double)plane[2], (double)plane[3]);
    Hr("GetTexture", IDirect3DDevice7_GetTexture(device, 0, &texture));
    Say("default texture %s\n", texture ? "set" : "null");
}

/* What the device answers to calls that are wrong. */
static void Errors(void)
{
    TL v[6];
    D3DVIEWPORT7 viewport = {0, 0, SIDE * 2, SIDE, 0, 1};
    D3DLIGHT7 light;
    D3DMATRIX matrix = identity;
    DWORD value = 0, handle = 0;
    QuadVertices(v, 0, 0, 8, 8, 0.5f, 0xffffffffu);
    Hr("DrawPrimitive outside a scene", IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, v, 6, 0));
    Hr("EndScene without a scene", IDirect3DDevice7_EndScene(device));
    Hr("BeginScene", IDirect3DDevice7_BeginScene(device));
    Hr("BeginScene in a scene", IDirect3DDevice7_BeginScene(device));
    Hr("DrawPrimitive primitive 0", IDirect3DDevice7_DrawPrimitive(device, (D3DPRIMITIVETYPE)0, D3DFVF_TLVERTEX, v, 6, 0));
    Hr("DrawPrimitive primitive 7", IDirect3DDevice7_DrawPrimitive(device, (D3DPRIMITIVETYPE)7, D3DFVF_TLVERTEX, v, 6, 0));
    Hr("EndScene", IDirect3DDevice7_EndScene(device));
    Hr("SetRenderState 0", IDirect3DDevice7_SetRenderState(device, (D3DRENDERSTATETYPE)0, 1));
    Hr("SetRenderState 300", IDirect3DDevice7_SetRenderState(device, (D3DRENDERSTATETYPE)300, 1));
    Hr("GetRenderState 300", IDirect3DDevice7_GetRenderState(device, (D3DRENDERSTATETYPE)300, &value));
    Hr("SetRenderState TEXTUREHANDLE", IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_TEXTUREHANDLE, 0));
    Hr("SetTextureStageState stage 8", IDirect3DDevice7_SetTextureStageState(device, 8, D3DTSS_COLOROP, D3DTOP_DISABLE));
    Hr("GetTextureStageState stage 8", IDirect3DDevice7_GetTextureStageState(device, 8, D3DTSS_COLOROP, &value));
    Hr("SetTexture stage 8", IDirect3DDevice7_SetTexture(device, 8, NULL));
    Hr("SetTransform 99", IDirect3DDevice7_SetTransform(device, (D3DTRANSFORMSTATETYPE)99, &matrix));
    Hr("SetViewport wider than the target", IDirect3DDevice7_SetViewport(device, &viewport));
    memset(&light, 0, sizeof light);
    Hr("SetLight without a type", IDirect3DDevice7_SetLight(device, 0, &light));
    Hr("Clear without flags", IDirect3DDevice7_Clear(device, 0, NULL, 0, 0, 1.0f, 0));
    Hr("Clear stencil without one", IDirect3DDevice7_Clear(device, 0, NULL, D3DCLEAR_STENCIL, 0, 1.0f, 0));
    Hr("EndStateBlock without Begin", IDirect3DDevice7_EndStateBlock(device, &handle));
}

/* ------------------------------------------------------------------- scenes */
static void SceneTransformed(void)
{
    const TL triangle[3] = {{128, 16, 0.5f, 1, 0xffff0000u, 0, 0, 0}, {240, 240, 0.5f, 1, 0xff00ff00u, 0, 0, 0}, {16, 240, 0.5f, 1, 0xff0000ffu, 0, 0, 0}};
    Begin(0x000040);
    Untextured();
    Hr("Quad", Quad(32, 32, 224, 224, 0.5f, 0xffff0000u));
    End("TL");
    Want("TL", 128, 128, 0xff0000, 0);
    Want("TL", 8, 8, 0x000040, 0);
    Want("TL", 32, 32, 0xff0000, 0);
    Want("TL", 31, 31, 0x000040, 0);
    Want("TL", 223, 223, 0xff0000, 0);
    Want("TL", 224, 224, 0x000040, 0);
    /* Gouraud: the colours of the corners meet in the middle. */
    Begin(0x000000);
    Untextured();
    IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, (void *)triangle, 3, 0);
    End("GOURAUD");
    Pixel("GOURAUD", 128, 160);
    Pixel("GOURAUD", 128, 40);
    Want("GOURAUD", 4, 4, 0x000000, 0);
    /* The stages as a new device has them, without a texture. */
    Begin(0x000000);
    Quad(32, 32, 224, 224, 0.5f, 0xff00ff00u);
    End("NOTEX");
    Pixel("NOTEX", 128, 128);
    /* Flat shading takes the colour of the first vertex. */
    Begin(0x000000);
    Untextured();
    RS(D3DRENDERSTATE_SHADEMODE, D3DSHADE_FLAT);
    IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, (void *)triangle, 3, 0);
    End("FLAT");
    Want("FLAT", 128, 160, 0xff0000, 0);
}

static void SceneCull(void)
{
    /* Clockwise on the screen on the left, counter-clockwise on the right. */
    const TL cw[3] = {{32, 32, 0.5f, 1, 0xffff0000u, 0, 0, 0}, {112, 32, 0.5f, 1, 0xffff0000u, 0, 0, 0}, {32, 112, 0.5f, 1, 0xffff0000u, 0, 0, 0}};
    const TL ccw[3] = {{144, 32, 0.5f, 1, 0xff00ff00u, 0, 0, 0}, {144, 112, 0.5f, 1, 0xff00ff00u, 0, 0, 0}, {224, 32, 0.5f, 1, 0xff00ff00u, 0, 0, 0}};
    static const DWORD modes[3] = {D3DCULL_CCW, D3DCULL_CW, D3DCULL_NONE};
    static const char *const names[3] = {"CULLCCW", "CULLCW", "CULLNONE"};
    int i;
    for (i = 0; i < 3; ++i)
    {
        Begin(0x000000);
        Untextured();
        RS(D3DRENDERSTATE_CULLMODE, modes[i]);
        IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, (void *)cw, 3, 0);
        IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, (void *)ccw, 3, 0);
        End(names[i]);
        Want(names[i], 48, 48, modes[i] == D3DCULL_CW ? 0x000000 : 0xff0000, 0);
        Want(names[i], 160, 48, modes[i] == D3DCULL_CCW ? 0x000000 : 0x00ff00, 0);
    }
}

static void SceneTextures(void)
{
    int kind;
    for (kind = 0; kind < KINDS; ++kind)
    {
        LPDIRECTDRAWSURFACE7 texture;
        char name[16];
        if (!texture_kinds[kind]) { Say("texture %s not offered\n", kind_names[kind]); continue; }
        texture = Texture(kind, 8, 1, 0, DDSCAPS2_TEXTUREMANAGE, Pattern);
        if (!texture) continue;
        sprintf(name, "T%s", kind_names[kind]);
        Begin(0x000040);
        Textured(texture);
        Quad(32, 32, 224, 224, 0.5f, 0xffffffffu);
        End(name);
        Want(name, 80, 80, 0xff0000, 0);
        Want(name, 176, 80, 0x00ff00, 0);
        Want(name, 80, 176, 0x0000ff, 0);
        Want(name, 176, 176, 0xffffff, 0);
        Want(name, 16, 16, 0x000040, 0);
        if (kind == ARGB8888)
        {
            /* The same texture filtered, and times the colour of the vertex. */
            Begin(0x000040);
            Textured(texture);
            TSS(0, D3DTSS_MAGFILTER, D3DTFG_LINEAR);
            Quad(32, 32, 224, 224, 0.5f, 0xffffffffu);
            End("TLINEAR");
            Pixel("TLINEAR", 128, 80);
            Want("TLINEAR", 60, 60, 0xff0000, 0);
            Begin(0x000040);
            Textured(texture);
            TSS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            TSS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            Quad(32, 32, 224, 224, 0.5f, 0xff808080u);
            End("TMODUL");
            Want("TMODUL", 80, 80, 0x800000, 1);
            Want("TMODUL", 176, 176, 0x808080, 1);
            /* The alpha of the texture: the white quarter fails the test. */
            Begin(0x000040);
            Textured(texture);
            RS(D3DRENDERSTATE_ALPHATESTENABLE, TRUE);
            RS(D3DRENDERSTATE_ALPHAREF, 0x80);
            RS(D3DRENDERSTATE_ALPHAFUNC, D3DCMP_GREATEREQUAL);
            Quad(32, 32, 224, 224, 0.5f, 0xffffffffu);
            End("ALPHATST");
            Want("ALPHATST", 80, 80, 0xff0000, 0);
            Want("ALPHATST", 176, 176, 0x000040, 0);
            /* Wrapped twice across the rectangle. */
            Begin(0x000040);
            Textured(texture);
            TSS(0, D3DTSS_ADDRESS, D3DTADDRESS_WRAP);
            {
                TL v[6];
                int i;
                QuadVertices(v, 32, 32, 224, 224, 0.5f, 0xffffffffu);
                for (i = 0; i < 6; ++i) { v[i].u *= 2; v[i].v *= 2; }
                IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, v, 6, 0);
            }
            End("TWRAP");
            Want("TWRAP", 56, 56, 0xff0000, 0);
            Want("TWRAP", 104, 56, 0x00ff00, 0);
            Want("TWRAP", 152, 56, 0xff0000, 0);
            Want("TWRAP", 200, 152, 0x00ff00, 0);
            /* Each direction by itself: U clamps and V wraps, then U mirrors. */
            Begin(0x000040);
            Textured(texture);
            TSS(0, D3DTSS_ADDRESS, D3DTADDRESS_WRAP);
            TSS(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
            {
                TL v[6];
                int i;
                QuadVertices(v, 32, 32, 224, 224, 0.5f, 0xffffffffu);
                for (i = 0; i < 6; ++i) { v[i].u *= 2; v[i].v *= 2; }
                IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, v, 6, 0);
            }
            End("TCLAMPU");
            Want("TCLAMPU", 56, 56, 0xff0000, 0);
            Want("TCLAMPU", 152, 56, 0x00ff00, 0);
            Want("TCLAMPU", 200, 56, 0x00ff00, 0);
            Want("TCLAMPU", 56, 152, 0xff0000, 0);
            Begin(0x000040);
            Textured(texture);
            TSS(0, D3DTSS_ADDRESS, D3DTADDRESS_WRAP);
            TSS(0, D3DTSS_ADDRESSU, D3DTADDRESS_MIRROR);
            {
                TL v[6];
                int i;
                QuadVertices(v, 32, 32, 224, 224, 0.5f, 0xffffffffu);
                for (i = 0; i < 6; ++i) { v[i].u *= 2; v[i].v *= 2; }
                IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, v, 6, 0);
            }
            End("TMIRRORU");
            Want("TMIRRORU", 56, 56, 0xff0000, 0);
            Want("TMIRRORU", 152, 56, 0x00ff00, 0);
            Want("TMIRRORU", 200, 56, 0xff0000, 0);
            Want("TMIRRORU", 56, 152, 0xff0000, 0);
        }
        Say("texture %s release=%lu\n", kind_names[kind], (unsigned long)IDirectDrawSurface7_Release(texture));
    }
}

static void SceneMip(void)
{
    LPDIRECTDRAWSURFACE7 texture = Texture(ARGB8888, 8, 4, 0, DDSCAPS2_TEXTUREMANAGE, Pattern);
    if (!texture) return;
    Begin(0x000040);
    Textured(texture);
    TSS(0, D3DTSS_MIPFILTER, D3DTFP_POINT);
    /* Eight texels on 64, on 8, on 4 and on 2 pixels: levels 0, 0, 1 and 2. */
    Quad(16, 16, 80, 80, 0.5f, 0xffffffffu);
    Quad(96, 16, 104, 24, 0.5f, 0xffffffffu);
    Quad(128, 16, 132, 20, 0.5f, 0xffffffffu);
    Quad(160, 16, 162, 18, 0.5f, 0xffffffffu);
    /* Without a mip filter the first level at any size. */
    TSS(0, D3DTSS_MIPFILTER, D3DTFP_NONE);
    Quad(128, 64, 132, 68, 0.5f, 0xffffffffu);
    End("MIP");
    Want("MIP", 24, 24, 0xff0000, 0);
    Want("MIP", 97, 17, 0xff0000, 0);
    Want("MIP", 129, 17, 0x00ff00, 0);
    Want("MIP", 160, 16, 0x0000ff, 0);
    Want("MIP", 129, 65, 0xff0000, 0);
    IDirectDrawSurface7_Release(texture);
}

static void SceneBlend(void)
{
    Begin(0x000000);
    Untextured();
    Quad(32, 32, 160, 160, 0.5f, 0xffff0000u);
    RS(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
    RS(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
    RS(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
    Quad(96, 96, 224, 224, 0.5f, 0x8000ff00u);
    End("BLEND");
    Want("BLEND", 64, 64, 0xff0000, 0);
    Want("BLEND", 128, 128, 0x7f8000, 1);
    Want("BLEND", 192, 192, 0x008000, 1);
    Begin(0x000000);
    Untextured();
    Quad(32, 32, 160, 160, 0.5f, 0xffff0000u);
    RS(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
    RS(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE);
    RS(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE);
    Quad(96, 96, 224, 224, 0.5f, 0xff00ff00u);
    End("ADD");
    Want("ADD", 128, 128, 0xffff00, 0);
    Begin(0xffffff);
    Untextured();
    RS(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
    RS(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ZERO);
    RS(D3DRENDERSTATE_DESTBLEND, D3DBLEND_SRCCOLOR);
    Quad(32, 32, 224, 224, 0.5f, 0xff804020u);
    End("MULTIPLY");
    Want("MULTIPLY", 128, 128, 0x804020, 1);
}

static void SceneDepth(void)
{
    Begin(0x000000);
    Untextured();
    RS(D3DRENDERSTATE_ZENABLE, D3DZB_TRUE);
    Quad(96, 96, 160, 160, 0.2f, 0xff00ff00u);
    Quad(32, 32, 224, 224, 0.8f, 0xffff0000u);
    /* Without writing Z the white one leaves no depth for the blue one. */
    RS(D3DRENDERSTATE_ZWRITEENABLE, FALSE);
    Quad(32, 32, 64, 64, 0.1f, 0xffffffffu);
    RS(D3DRENDERSTATE_ZWRITEENABLE, TRUE);
    Quad(48, 48, 80, 80, 0.5f, 0xff0000ffu);
    /* Behind everything and drawn all the same: the test is off. */
    RS(D3DRENDERSTATE_ZENABLE, D3DZB_FALSE);
    Quad(192, 192, 240, 240, 0.99f, 0xffffff00u);
    End("DEPTH");
    Want("DEPTH", 128, 128, 0x00ff00, 0);
    Want("DEPTH", 180, 128, 0xff0000, 0);
    Want("DEPTH", 40, 40, 0xffffff, 0);
    Want("DEPTH", 56, 56, 0x0000ff, 0);
    Want("DEPTH", 200, 200, 0xffff00, 0);
    /* The comparison the other way round. */
    Begin(0x000000);
    Untextured();
    RS(D3DRENDERSTATE_ZENABLE, D3DZB_TRUE);
    RS(D3DRENDERSTATE_ZFUNC, D3DCMP_GREATER);
    Quad(32, 32, 224, 224, 0.5f, 0xffff0000u);
    IDirect3DDevice7_Clear(device, 0, NULL, D3DCLEAR_ZBUFFER, 0, 0.25f, 0);
    Quad(96, 96, 160, 160, 0.5f, 0xff00ff00u);
    Quad(96, 96, 128, 128, 0.1f, 0xff0000ffu);
    End("ZGREATER");
    Want("ZGREATER", 64, 64, 0x000000, 0);
    Want("ZGREATER", 144, 144, 0x00ff00, 0);
    Want("ZGREATER", 112, 112, 0x00ff00, 0);
}

static void SceneFog(void)
{
    /* Table fog from the depth of the pixel. */
    Begin(0x000000);
    Untextured();
    RS(D3DRENDERSTATE_FOGENABLE, TRUE);
    RS(D3DRENDERSTATE_FOGCOLOR, 0xffffff);
    RS(D3DRENDERSTATE_FOGTABLEMODE, D3DFOG_LINEAR);
    RS(D3DRENDERSTATE_FOGSTART, F(0.2f));
    RS(D3DRENDERSTATE_FOGEND, F(0.8f));
    Quad(16, 16, 80, 240, 0.1f, 0xff000000u);
    Quad(96, 16, 160, 240, 0.5f, 0xff000000u);
    Quad(176, 16, 240, 240, 0.9f, 0xff000000u);
    End("FOGTABLE");
    Want("FOGTABLE", 48, 128, 0x000000, 0);
    Want("FOGTABLE", 128, 128, 0x808080, 2);
    Want("FOGTABLE", 208, 128, 0xffffff, 0);
    /* Vertex fog of transformed vertices: the alpha of the specular colour. */
    Begin(0x000000);
    Untextured();
    RS(D3DRENDERSTATE_FOGENABLE, TRUE);
    RS(D3DRENDERSTATE_FOGCOLOR, 0xffffff);
    {
        TL v[6];
        int i;
        QuadVertices(v, 32, 32, 224, 224, 0.5f, 0xff000000u);
        for (i = 0; i < 6; ++i) v[i].specular = 0x80000000u;
        IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, v, 6, 0);
    }
    End("FOGVERT");
    Want("FOGVERT", 128, 128, 0x7f7f7f, 1);
}

static void SceneStages(void)
{
    LPDIRECTDRAWSURFACE7 first = Texture(ARGB8888, 8, 1, 0, DDSCAPS2_TEXTUREMANAGE, Pattern);
    LPDIRECTDRAWSURFACE7 second = Texture(ARGB8888, 4, 1, 0, DDSCAPS2_TEXTUREMANAGE, Gray);
    TL2 v[6];
    TL q[6];
    int i;
    if (!first || !second) return;
    QuadVertices(q, 32, 32, 224, 224, 0.5f, 0xffffffffu);
    for (i = 0; i < 6; ++i)
    {
        memcpy(&v[i], &q[i], sizeof q[i]);
        /* The second set names the red quarter of the pattern everywhere. */
        v[i].u1 = 0.25f;
        v[i].v1 = 0.25f;
    }
    /* Stage 1 multiplies the pattern by the gray texture. */
    Begin(0x000040);
    Textured(first);
    IDirect3DDevice7_SetTexture(device, 1, second);
    TSS(1, D3DTSS_COLOROP, D3DTOP_MODULATE);
    TSS(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    TSS(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
    TSS(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    TSS(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
    TSS(1, D3DTSS_TEXCOORDINDEX, 1);
    TSS(1, D3DTSS_MAGFILTER, D3DTFG_POINT);
    TSS(1, D3DTSS_MINFILTER, D3DTFN_POINT);
    IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLELIST, FVF_TL2, v, 6, 0);
    End("STAGE2");
    Want("STAGE2", 80, 80, 0x800000, 1);
    Want("STAGE2", 176, 80, 0x008000, 1);
    Want("STAGE2", 176, 176, 0x808080, 1);
    /* Stage 1 reads the pattern through the second coordinate set and adds. */
    Begin(0x000040);
    Textured(second);
    IDirect3DDevice7_SetTexture(device, 1, first);
    TSS(1, D3DTSS_COLOROP, D3DTOP_ADD);
    TSS(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    TSS(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
    TSS(1, D3DTSS_TEXCOORDINDEX, 1);
    TSS(1, D3DTSS_MAGFILTER, D3DTFG_POINT);
    TSS(1, D3DTSS_MINFILTER, D3DTFN_POINT);
    IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLELIST, FVF_TL2, v, 6, 0);
    End("STAGEADD");
    Want("STAGEADD", 128, 128, 0xff8080, 1);
    /* The texture factor blends texture and vertex colour, stage 1 inverts,
       stage 2 takes the result times the factor's alpha into the colour. */
    Begin(0x000040);
    Textured(first);
    RS(D3DRENDERSTATE_TEXTUREFACTOR, 0x80ffffffu);
    TSS(0, D3DTSS_COLOROP, D3DTOP_BLENDFACTORALPHA);
    TSS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    TSS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    Quad(32, 32, 224, 224, 0.5f, 0xff000000u);
    End("TFACTOR");
    Want("TFACTOR", 80, 80, 0x800000, 1);
    Want("TFACTOR", 176, 176, 0x808080, 1);
    Begin(0x000040);
    Textured(first);
    TSS(1, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    TSS(1, D3DTSS_COLORARG1, D3DTA_CURRENT | D3DTA_COMPLEMENT);
    TSS(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    TSS(1, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
    TSS(2, D3DTSS_COLOROP, D3DTOP_MODULATE);
    TSS(2, D3DTSS_COLORARG1, D3DTA_CURRENT);
    TSS(2, D3DTSS_COLORARG2, D3DTA_TFACTOR | D3DTA_ALPHAREPLICATE);
    TSS(2, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    TSS(2, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
    RS(D3DRENDERSTATE_TEXTUREFACTOR, 0x80000000u);
    Quad(32, 32, 224, 224, 0.5f, 0xffffffffu);
    End("STAGE3");
    Want("STAGE3", 80, 80, 0x008080, 1);
    Want("STAGE3", 176, 80, 0x800080, 1);
    Want("STAGE3", 176, 176, 0x000000, 1);
    IDirectDrawSurface7_Release(first);
    IDirectDrawSurface7_Release(second);
}

static void SceneColorKey(void)
{
    LPDIRECTDRAWSURFACE7 texture = texture_kinds[RGB565] ? Texture(RGB565, 8, 1, 0, DDSCAPS2_TEXTUREMANAGE, Pattern) : NULL;
    DDCOLORKEY key;
    if (!texture) return;
    key.dwColorSpaceLowValue = key.dwColorSpaceHighValue = 0x07e0;
    Hr("SetColorKey", IDirectDrawSurface7_SetColorKey(texture, DDCKEY_SRCBLT, &key));
    Begin(0x000040);
    Textured(texture);
    RS(D3DRENDERSTATE_COLORKEYENABLE, TRUE);
    Quad(32, 32, 224, 224, 0.5f, 0xffffffffu);
    End("COLORKEY");
    Want("COLORKEY", 80, 80, 0xff0000, 0);
    Want("COLORKEY", 176, 80, 0x000040, 0);
    Want("COLORKEY", 80, 176, 0x0000ff, 0);
    IDirectDrawSurface7_Release(texture);
}

/* Every kind of primitive and every way to hand vertices over, each in its
   own cell of the picture. */
static void ScenePrimitives(void)
{
    TL v[6], cell[4];
    WORD list[6] = {0, 1, 2, 1, 3, 2};
    D3DDRAWPRIMITIVESTRIDEDDATA strided;
    static float positions[4][3];
    static DWORD colors[4];
    D3DVERTEXBUFFERDESC description;
    LPDIRECT3DVERTEXBUFFER7 buffer = NULL;
    void *memory = NULL;
    DWORD size = 0;
    int i;
#define CELL(x, y, color) \
    do { \
        const TL c[4] = {{(x), (y), 0.5f, 1, (color), 0, 0, 0}, {(x) + 48, (y), 0.5f, 1, (color), 0, 1, 0}, \
                         {(x), (y) + 48, 0.5f, 1, (color), 0, 0, 1}, {(x) + 48, (y) + 48, 0.5f, 1, (color), 0, 1, 1}}; \
        memcpy(cell, c, sizeof c); \
    } while (0)
    Begin(0x000000);
    Untextured();
    CELL(8, 8, 0xffff0000u);
    Hr("DrawIndexedPrimitive list", IDirect3DDevice7_DrawIndexedPrimitive(device, D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, cell, 4, list, 6, 0));
    CELL(72, 8, 0xff00ff00u);
    Hr("DrawPrimitive strip", IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLESTRIP, D3DFVF_TLVERTEX, cell, 4, 0));
    CELL(136, 8, 0xff0000ffu);
    {
        const TL fan[4] = {cell[0], cell[1], cell[3], cell[2]};
        Hr("DrawPrimitive fan", IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLEFAN, D3DFVF_TLVERTEX, (void *)fan, 4, 0));
    }
    CELL(200, 8, 0xffffff00u);
    {
        WORD strip[4] = {0, 1, 2, 3};
        Hr("DrawIndexedPrimitive strip", IDirect3DDevice7_DrawIndexedPrimitive(device, D3DPT_TRIANGLESTRIP, D3DFVF_TLVERTEX, cell, 4, strip, 4, 0));
    }
    /* Strided: positions and colours in arrays of their own. */
    CELL(8, 72, 0xffff00ffu);
    for (i = 0; i < 4; ++i)
    {
        /* Strided vertices are model space, which is clip space here. */
        positions[i][0] = cell[i].x / 128.0f - 1.0f; positions[i][1] = 1.0f - cell[i].y / 128.0f; positions[i][2] = cell[i].z;
        colors[i] = cell[i].color;
    }
    memset(&strided, 0, sizeof strided);
    strided.position.lpvData = positions;
    strided.position.dwStride = sizeof positions[0];
    strided.diffuse.lpvData = colors;
    strided.diffuse.dwStride = sizeof colors[0];
    Hr("DrawPrimitiveStrided", IDirect3DDevice7_DrawPrimitiveStrided(device, D3DPT_TRIANGLESTRIP, D3DFVF_XYZ | D3DFVF_DIFFUSE, &strided, 4, 0));
    Hr("DrawPrimitiveStrided transformed", IDirect3DDevice7_DrawPrimitiveStrided(device, D3DPT_TRIANGLESTRIP, D3DFVF_XYZRHW | D3DFVF_DIFFUSE, &strided, 4, 0));
    CELL(72, 72, 0xff00ffffu);
    for (i = 0; i < 4; ++i)
    {
        positions[i][0] = cell[i].x / 128.0f - 1.0f; positions[i][1] = 1.0f - cell[i].y / 128.0f;
        colors[i] = cell[i].color;
    }
    Hr("DrawIndexedPrimitiveStrided", IDirect3DDevice7_DrawIndexedPrimitiveStrided(device, D3DPT_TRIANGLELIST, D3DFVF_XYZ | D3DFVF_DIFFUSE,
                                                                                    &strided, 4, list, 6, 0));
    /* A vertex buffer, drawn from its second half and by indices. */
    memset(&description, 0, sizeof description);
    description.dwSize = sizeof description;
    description.dwFVF = D3DFVF_TLVERTEX;
    description.dwNumVertices = 8;
    Hr("CreateVertexBuffer", IDirect3D7_CreateVertexBuffer(d3d, &description, &buffer, 0));
    if (buffer)
    {
        Hr("VertexBuffer Lock", IDirect3DVertexBuffer7_Lock(buffer, DDLOCK_WAIT, &memory, &size));
        Say("vertex buffer size=%lu\n", (unsigned long)size);
        CELL(136, 72, 0xffffffffu);
        memcpy((TL *)memory + 4, cell, sizeof cell);
        CELL(200, 72, 0xff808080u);
        memcpy(memory, cell, sizeof cell);
        Hr("VertexBuffer Unlock", IDirect3DVertexBuffer7_Unlock(buffer));
        Hr("DrawPrimitiveVB", IDirect3DDevice7_DrawPrimitiveVB(device, D3DPT_TRIANGLESTRIP, buffer, 4, 4, 0));
        Hr("DrawIndexedPrimitiveVB", IDirect3DDevice7_DrawIndexedPrimitiveVB(device, D3DPT_TRIANGLELIST, buffer, 0, 4, list, 6, 0));
        memset(&description, 0, sizeof description);
        description.dwSize = sizeof description;
        Hr("GetVertexBufferDesc", IDirect3DVertexBuffer7_GetVertexBufferDesc(buffer, &description));
        Say("vertex buffer fvf=%lx count=%lu\n", (unsigned long)description.dwFVF, (unsigned long)description.dwNumVertices);
        Say("vertex buffer release=%lu\n", (unsigned long)IDirect3DVertexBuffer7_Release(buffer));
    }
    /* Lines and points. */
    QuadVertices(v, 16, 144, 112, 144, 0.5f, 0xffffffffu);
    v[0].x = 16; v[0].y = 144; v[1].x = 112; v[1].y = 144; v[2].x = 16; v[2].y = 160; v[3].x = 112; v[3].y = 160;
    Hr("DrawPrimitive line list", IDirect3DDevice7_DrawPrimitive(device, D3DPT_LINELIST, D3DFVF_TLVERTEX, v, 4, 0));
    v[0].x = 144; v[0].y = 144; v[1].x = 208; v[1].y = 144; v[2].x = 208; v[2].y = 176; v[3].x = 144; v[3].y = 176;
    Hr("DrawPrimitive line strip", IDirect3DDevice7_DrawPrimitive(device, D3DPT_LINESTRIP, D3DFVF_TLVERTEX, v, 4, 0));
    v[0].x = 32; v[0].y = 208; v[1].x = 64; v[1].y = 208; v[2].x = 96; v[2].y = 208;
    Hr("DrawPrimitive points", IDirect3DDevice7_DrawPrimitive(device, D3DPT_POINTLIST, D3DFVF_TLVERTEX, v, 3, 0));
    End("PRIMS");
#undef CELL
    Want("PRIMS", 32, 32, 0xff0000, 0);
    Want("PRIMS", 96, 32, 0x00ff00, 0);
    Want("PRIMS", 160, 32, 0x0000ff, 0);
    Want("PRIMS", 224, 32, 0xffff00, 0);
    Want("PRIMS", 32, 96, 0xff00ff, 0);
    Want("PRIMS", 96, 96, 0x00ffff, 0);
    Want("PRIMS", 160, 96, 0xffffff, 0);
    Want("PRIMS", 224, 96, 0x808080, 0);
    Want("PRIMS", 64, 144, 0xffffff, 0);
    Want("PRIMS", 64, 160, 0xffffff, 0);
    Want("PRIMS", 64, 152, 0x000000, 0);
    Want("PRIMS", 176, 144, 0xffffff, 0);
    Want("PRIMS", 208, 160, 0xffffff, 0);
    Want("PRIMS", 64, 208, 0xffffff, 0);
    Want("PRIMS", 65, 209, 0x000000, 0);
}

/* The vertex pipeline: model space through world, view and projection. */
static void ScenePipeline(void)
{
    const D3DMATRIX world = RotationY(0.5235988f), projection = Perspective(1.0471976f, 1.0f, 1.0f, 100.0f);
    D3DMATRIX view = Translation(0, -0.2f, 4.0f), got;
    static const LV pyramid[12] = {
        {0, 1, 0, 0xffff0000u}, {1, -1, -1, 0xffff0000u}, {-1, -1, -1, 0xffff0000u},
        {0, 1, 0, 0xff00ff00u}, {1, -1, 1, 0xff00ff00u}, {1, -1, -1, 0xff00ff00u},
        {0, 1, 0, 0xff0000ffu}, {-1, -1, 1, 0xff0000ffu}, {1, -1, 1, 0xff0000ffu},
        {0, 1, 0, 0xffffff00u}, {-1, -1, -1, 0xffffff00u}, {-1, -1, 1, 0xffffff00u}};
    D3DVALUE plane[4] = {1, 0, 0, 0};
    D3DVIEWPORT7 viewport = {64, 64, 128, 128, 0, 1}, whole = {0, 0, SIDE, SIDE, 0, 1};
    D3DRECT corner = {{0}, {0}, {80}, {80}}, outside = {{0}, {0}, {32}, {32}};
    Begin(0x000040);
    Untextured();
    RS(D3DRENDERSTATE_ZENABLE, D3DZB_TRUE);
    RS(D3DRENDERSTATE_CULLMODE, D3DCULL_CCW);
    Hr("SetTransform world", IDirect3DDevice7_SetTransform(device, D3DTRANSFORMSTATE_WORLD, (D3DMATRIX *)&world));
    Hr("SetTransform view", IDirect3DDevice7_SetTransform(device, D3DTRANSFORMSTATE_VIEW, &view));
    Hr("SetTransform projection", IDirect3DDevice7_SetTransform(device, D3DTRANSFORMSTATE_PROJECTION, (D3DMATRIX *)&projection));
    Hr("DrawPrimitive pyramid", IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLELIST, FVF_LV, (void *)pyramid, 12, 0));
    End("PYRAMID");
    Want("PYRAMID", 8, 8, 0x000040, 0);
    Want("PYRAMID", 110, 150, 0xff0000, 0);
    Want("PYRAMID", 165, 150, 0x00ff00, 0);
    Pixel("PYRAMID", 128, 100);
    /* MultiplyTransform gets exactly representable inputs. The earlier
       scene's trigonometry belongs to the CRT, not this D3D contract. */
    Begin(0x000040);
    Untextured();
    got = world;
    got._11 = got._33 = 0.75f;
    got._13 = -0.5f; got._31 = 0.5f;
    IDirect3DDevice7_SetTransform(device, D3DTRANSFORMSTATE_WORLD, &got);
    view = Translation(0.25f, 0, 0);
    Hr("MultiplyTransform", IDirect3DDevice7_MultiplyTransform(device, D3DTRANSFORMSTATE_WORLD, &view));
    IDirect3DDevice7_GetTransform(device, D3DTRANSFORMSTATE_WORLD, &got);
    SayMatrix("world after MultiplyTransform", &got);
    IDirect3DDevice7_EndScene(device);
    /* Model space is clip space with the transforms a device starts with:
       a user clip plane keeps the half where x is positive. */
    Begin(0x000040);
    Untextured();
    Hr("SetClipPlane", IDirect3DDevice7_SetClipPlane(device, 0, plane));
    RS(D3DRENDERSTATE_CLIPPLANEENABLE, D3DCLIPPLANE0);
    ModelQuad(-0.75f, -0.75f, 0.75f, 0.75f, 0.5f, 0xffff0000u);
    End("CLIPPLANE");
    Want("CLIPPLANE", 96, 128, 0x000040, 0);
    Want("CLIPPLANE", 160, 128, 0xff0000, 0);
    /* The viewport: the same rectangle lands in the middle quarter, and what
       reaches beyond clip space is cut at the viewport. Clear takes rects. */
    Begin(0x000040);
    Untextured();
    Hr("SetViewport", IDirect3DDevice7_SetViewport(device, &viewport));
    ModelQuad(-2, -2, 2, 2, 0.5f, 0xff00ff00u);
    ModelQuad(-0.5f, -0.5f, 0.5f, 0.5f, 0.25f, 0xffff0000u);
    /* Clear stays inside the viewport: a rect that reaches into it clears
       its part there, one outside of it nothing. */
    Hr("Clear rect", IDirect3DDevice7_Clear(device, 1, &corner, D3DCLEAR_TARGET, 0xffffffffu, 1.0f, 0));
    Hr("Clear rect outside the viewport", IDirect3DDevice7_Clear(device, 1, &outside, D3DCLEAR_TARGET, 0xffff00ffu, 1.0f, 0));
    IDirect3DDevice7_EndScene(device);
    IDirect3DDevice7_SetViewport(device, &whole);
    Shoot(target, "VIEWPORT");
    Want("VIEWPORT", 32, 128, 0x000040, 0);
    Want("VIEWPORT", 72, 72, 0xffffff, 0);
    Want("VIEWPORT", 84, 84, 0x00ff00, 0);
    Want("VIEWPORT", 128, 128, 0xff0000, 0);
    Want("VIEWPORT", 190, 190, 0x00ff00, 0);
    Want("VIEWPORT", 194, 194, 0x000040, 0);
    Want("VIEWPORT", 16, 16, 0x000040, 0);
    Want("VIEWPORT", 40, 40, 0x000040, 0);
}

static void SceneLight(void)
{
    static const NV facing[6] = {{-0.9f, 0.9f, 0.5f, 0, 0, -1, 0, 0}, {-0.1f, 0.9f, 0.5f, 0, 0, -1, 0, 0}, {-0.9f, 0.1f, 0.5f, 0, 0, -1, 0, 0},
                                 {-0.1f, 0.9f, 0.5f, 0, 0, -1, 0, 0}, {-0.1f, 0.1f, 0.5f, 0, 0, -1, 0, 0}, {-0.9f, 0.1f, 0.5f, 0, 0, -1, 0, 0}};
    NV tilted[6], lower[6];
    D3DLIGHT7 light, got;
    D3DMATERIAL7 material;
    BOOL enabled = FALSE;
    int i;
    memcpy(tilted, facing, sizeof facing);
    memcpy(lower, facing, sizeof facing);
    for (i = 0; i < 6; ++i)
    {
        /* Sixty degrees away from the light: half the diffuse light. */
        tilted[i].x += 1.0f;
        tilted[i].nx = 0.8660254f;
        tilted[i].nz = -0.5f;
        lower[i].y -= 1.0f;
    }
    memset(&light, 0, sizeof light);
    light.dltType = D3DLIGHT_DIRECTIONAL;
    light.dcvDiffuse.r = light.dcvDiffuse.g = light.dcvDiffuse.b = 1.0f;
    light.dvDirection.z = 1.0f;
    memset(&material, 0, sizeof material);
    material.diffuse.r = 1.0f; material.diffuse.g = 0.5f; material.diffuse.b = 0.25f; material.diffuse.a = 1.0f;
    material.ambient.r = material.ambient.g = material.ambient.b = 1.0f;
    Begin(0x000040);
    Untextured();
    RS(D3DRENDERSTATE_LIGHTING, TRUE);
    RS(D3DRENDERSTATE_AMBIENT, 0x00202020);
    RS(D3DRENDERSTATE_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);
    Hr("SetMaterial", IDirect3DDevice7_SetMaterial(device, &material));
    Hr("SetLight", IDirect3DDevice7_SetLight(device, 5, &light));
    Hr("GetLightEnable before", IDirect3DDevice7_GetLightEnable(device, 5, &enabled));
    Say("light 5 enabled=%d\n", (int)enabled);
    Hr("LightEnable", IDirect3DDevice7_LightEnable(device, 5, TRUE));
    Hr("GetLight", IDirect3DDevice7_GetLight(device, 5, &got));
    Say("light 5 type=%d direction=%.1f\n", (int)got.dltType, (double)got.dvDirection.z);
    Hr("DrawPrimitive lit", IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLELIST, D3DFVF_VERTEX, (void *)facing, 6, 0));
    IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLELIST, D3DFVF_VERTEX, tilted, 6, 0);
    /* A point light close to the lower rectangle, with a specular highlight. */
    Hr("LightEnable off", IDirect3DDevice7_LightEnable(device, 5, FALSE));
    light.dltType = D3DLIGHT_POINT;
    light.dcvSpecular.r = light.dcvSpecular.g = light.dcvSpecular.b = 1.0f;
    light.dvPosition.x = -0.5f; light.dvPosition.y = -0.5f; light.dvPosition.z = 0.0f;
    light.dvRange = 10.0f;
    light.dvAttenuation1 = 2.0f;
    material.specular.r = material.specular.g = material.specular.b = 1.0f;
    material.power = 8.0f;
    IDirect3DDevice7_SetMaterial(device, &material);
    IDirect3DDevice7_SetLight(device, 9, &light);
    IDirect3DDevice7_LightEnable(device, 9, TRUE);
    RS(D3DRENDERSTATE_SPECULARENABLE, TRUE);
    IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLELIST, D3DFVF_VERTEX, lower, 6, 0);
    IDirect3DDevice7_LightEnable(device, 9, FALSE);
    End("LIGHT");
    Want("LIGHT", 64, 64, 0xff9f60, 2);
    Want("LIGHT", 192, 64, 0x9f6040, 2);
    Pixel("LIGHT", 64, 192);
    Pixel("LIGHT", 24, 152);
    Want("LIGHT", 192, 192, 0x000040, 0);
}

/* A texture as the render target, then as a texture. */
static void SceneRenderToTexture(void)
{
    DDSURFACEDESC2 desc;
    LPDIRECTDRAWSURFACE7 texture = NULL, got = NULL;
    HRESULT result;
    memset(&desc, 0, sizeof desc);
    desc.dwSize = sizeof desc;
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.dwWidth = desc.dwHeight = 64;
    desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY;
    result = IDirectDraw7_CreateSurface(dd, &desc, &texture, NULL);
    Hr("texture as target", result);
    if (FAILED(result)) return;
    IDirect3DDevice7_ApplyStateBlock(device, base_block);
    Hr("SetRenderTarget texture", IDirect3DDevice7_SetRenderTarget(device, texture, 0));
    Hr("Clear texture", IDirect3DDevice7_Clear(device, 0, NULL, D3DCLEAR_TARGET, 0x0000ff, 1.0f, 0));
    IDirect3DDevice7_BeginScene(device);
    Untextured();
    Quad(16, 16, 48, 48, 0.5f, 0xffff0000u);
    IDirect3DDevice7_EndScene(device);
    Hr("SetRenderTarget back", IDirect3DDevice7_SetRenderTarget(device, target, 0));
    Hr("GetRenderTarget", IDirect3DDevice7_GetRenderTarget(device, &got));
    Say("render target is the target=%d\n", got == target ? 1 : 0);
    if (got) IDirectDrawSurface7_Release(got);
    Begin(0x000040);
    Textured(texture);
    Quad(32, 32, 224, 224, 0.5f, 0xffffffffu);
    End("RTT");
    Want("RTT", 128, 128, 0xff0000, 0);
    Want("RTT", 48, 48, 0x0000ff, 0);
    Want("RTT", 16, 16, 0x000040, 0);
    IDirect3DDevice7_SetTexture(device, 0, NULL);
    IDirectDrawSurface7_Release(texture);
}

/* Load copies a texture in system memory into a managed one, level by level. */
static void SceneLoad(void)
{
    LPDIRECTDRAWSURFACE7 source = Texture(ARGB8888, 8, 4, DDSCAPS_SYSTEMMEMORY, 0, Pattern);
    LPDIRECTDRAWSURFACE7 dest = Texture(ARGB8888, 8, 4, 0, DDSCAPS2_TEXTUREMANAGE, Gray);
    if (!source || !dest) return;
    Hr("Load", IDirect3DDevice7_Load(device, dest, NULL, source, NULL, 0));
    Hr("PreLoad", IDirect3DDevice7_PreLoad(device, dest));
    Begin(0x000040);
    Textured(dest);
    TSS(0, D3DTSS_MIPFILTER, D3DTFP_POINT);
    Quad(32, 32, 224, 224, 0.5f, 0xffffffffu);
    Quad(8, 8, 12, 12, 0.5f, 0xffffffffu);
    End("LOAD");
    Want("LOAD", 80, 80, 0xff0000, 0);
    Want("LOAD", 176, 80, 0x00ff00, 0);
    Want("LOAD", 9, 9, 0x00ff00, 0);
    IDirect3DDevice7_SetTexture(device, 0, NULL);
    IDirectDrawSurface7_Release(source);
    IDirectDrawSurface7_Release(dest);
}

static void SceneStateBlocks(void)
{
    D3DMATRIX moved = Translation(0.5f, 0, 0), got;
    D3DLIGHT7 light;
    DWORD block = 0, value = 0, all = 0;
    BOOL enabled = FALSE;
    IDirect3DDevice7_ApplyStateBlock(device, base_block);
    RS(D3DRENDERSTATE_CULLMODE, D3DCULL_CCW);
    Hr("BeginStateBlock", IDirect3DDevice7_BeginStateBlock(device));
    Hr("BeginStateBlock twice", IDirect3DDevice7_BeginStateBlock(device));
    RS(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    TSS(0, D3DTSS_COLOROP, D3DTOP_ADD);
    IDirect3DDevice7_SetTransform(device, D3DTRANSFORMSTATE_WORLD, &moved);
    memset(&light, 0, sizeof light);
    light.dltType = D3DLIGHT_DIRECTIONAL;
    light.dvDirection.y = -1.0f;
    IDirect3DDevice7_SetLight(device, 3, &light);
    IDirect3DDevice7_LightEnable(device, 3, TRUE);
    Hr("EndStateBlock", IDirect3DDevice7_EndStateBlock(device, &block));
    /* Recording does not change the device. */
    IDirect3DDevice7_GetRenderState(device, D3DRENDERSTATE_CULLMODE, &value);
    Say("cull mode after recording=%lu\n", (unsigned long)value);
    IDirect3DDevice7_GetTransform(device, D3DTRANSFORMSTATE_WORLD, &got);
    Say("world after recording=%.1f\n", (double)got._41);
    Hr("GetLightEnable after recording", IDirect3DDevice7_GetLightEnable(device, 3, &enabled));
    Hr("ApplyStateBlock", IDirect3DDevice7_ApplyStateBlock(device, block));
    IDirect3DDevice7_GetRenderState(device, D3DRENDERSTATE_CULLMODE, &value);
    Say("cull mode after apply=%lu\n", (unsigned long)value);
    IDirect3DDevice7_GetTextureStageState(device, 0, D3DTSS_COLOROP, &value);
    Say("colour operation after apply=%lu\n", (unsigned long)value);
    IDirect3DDevice7_GetTransform(device, D3DTRANSFORMSTATE_WORLD, &got);
    Say("world after apply=%.1f\n", (double)got._41);
    enabled = FALSE;
    Hr("GetLightEnable after apply", IDirect3DDevice7_GetLightEnable(device, 3, &enabled));
    Say("light 3 after apply=%d\n", enabled ? 1 : 0);
    /* Capture takes what the device has now for the states of the block. */
    RS(D3DRENDERSTATE_CULLMODE, D3DCULL_CW);
    RS(D3DRENDERSTATE_ZFUNC, D3DCMP_NEVER);
    Hr("CaptureStateBlock", IDirect3DDevice7_CaptureStateBlock(device, block));
    RS(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    RS(D3DRENDERSTATE_ZFUNC, D3DCMP_ALWAYS);
    IDirect3DDevice7_ApplyStateBlock(device, block);
    IDirect3DDevice7_GetRenderState(device, D3DRENDERSTATE_CULLMODE, &value);
    Say("cull mode after capture and apply=%lu\n", (unsigned long)value);
    IDirect3DDevice7_GetRenderState(device, D3DRENDERSTATE_ZFUNC, &value);
    Say("z function, not in the block=%lu\n", (unsigned long)value);
    Hr("DeleteStateBlock", IDirect3DDevice7_DeleteStateBlock(device, block));
    /* A block of everything brings the device back. */
    IDirect3DDevice7_ApplyStateBlock(device, base_block);
    Hr("CreateStateBlock all", IDirect3DDevice7_CreateStateBlock(device, D3DSBT_ALL, &all));
    RS(D3DRENDERSTATE_ZFUNC, D3DCMP_NEVER);
    IDirect3DDevice7_SetTransform(device, D3DTRANSFORMSTATE_WORLD, &moved);
    IDirect3DDevice7_ApplyStateBlock(device, all);
    IDirect3DDevice7_GetRenderState(device, D3DRENDERSTATE_ZFUNC, &value);
    IDirect3DDevice7_GetTransform(device, D3DTRANSFORMSTATE_WORLD, &got);
    Say("after the block of everything: z function=%lu world=%.1f\n", (unsigned long)value, (double)got._41);
    Hr("DeleteStateBlock all", IDirect3DDevice7_DeleteStateBlock(device, all));
    Hr("CreateStateBlock pixel", IDirect3DDevice7_CreateStateBlock(device, D3DSBT_PIXELSTATE, &all));
    IDirect3DDevice7_DeleteStateBlock(device, all);
    Hr("CreateStateBlock vertex", IDirect3DDevice7_CreateStateBlock(device, D3DSBT_VERTEXSTATE, &all));
    IDirect3DDevice7_DeleteStateBlock(device, all);
    IDirect3DDevice7_LightEnable(device, 3, FALSE);
}

/* ProcessVertices: the vertex pipeline with its results in a vertex buffer. */
static void SceneProcessVertices(void)
{
    static const NV model[3] = {{0, 0.8f, 0.5f, 0, 0, -1, 0.5f, 0}, {0.8f, -0.8f, 0.5f, 0, 0, -1, 1, 1}, {-0.8f, -0.8f, 0.5f, 0, 0, -1, 0, 1}};
    D3DVERTEXBUFFERDESC description;
    LPDIRECT3DVERTEXBUFFER7 source = NULL, dest = NULL;
    D3DLIGHT7 light;
    D3DMATERIAL7 material;
    void *memory = NULL;
    HRESULT result;
    int i;
    memset(&description, 0, sizeof description);
    description.dwSize = sizeof description;
    description.dwCaps = D3DVBCAPS_SYSTEMMEMORY;
    description.dwFVF = D3DFVF_VERTEX;
    description.dwNumVertices = 3;
    Hr("CreateVertexBuffer model", IDirect3D7_CreateVertexBuffer(d3d, &description, &source, 0));
    description.dwFVF = D3DFVF_TLVERTEX;
    Hr("CreateVertexBuffer transformed", IDirect3D7_CreateVertexBuffer(d3d, &description, &dest, 0));
    if (!source || !dest) return;
    IDirect3DVertexBuffer7_Lock(source, DDLOCK_WAIT, &memory, NULL);
    memcpy(memory, model, sizeof model);
    IDirect3DVertexBuffer7_Unlock(source);
    IDirect3DDevice7_ApplyStateBlock(device, base_block);
    memset(&light, 0, sizeof light);
    light.dltType = D3DLIGHT_DIRECTIONAL;
    light.dcvDiffuse.r = 1.0f; light.dcvDiffuse.g = 0.5f; light.dcvDiffuse.b = 0.25f;
    light.dvDirection.z = 1.0f;
    memset(&material, 0, sizeof material);
    material.diffuse.r = material.diffuse.g = material.diffuse.b = material.diffuse.a = 1.0f;
    IDirect3DDevice7_SetMaterial(device, &material);
    IDirect3DDevice7_SetLight(device, 1, &light);
    IDirect3DDevice7_LightEnable(device, 1, TRUE);
    RS(D3DRENDERSTATE_LIGHTING, TRUE);
    result = IDirect3DVertexBuffer7_ProcessVertices(dest, D3DVOP_TRANSFORM | D3DVOP_LIGHT, 0, 3, source, 0, device, 0);
    Hr("ProcessVertices", result);
    IDirect3DDevice7_LightEnable(device, 1, FALSE);
    if (SUCCEEDED(result) && SUCCEEDED(IDirect3DVertexBuffer7_Lock(dest, DDLOCK_WAIT | DDLOCK_READONLY, &memory, NULL)))
    {
        const TL *v = (const TL *)memory;
        for (i = 0; i < 3; ++i)
            Say("processed %d: %.1f %.1f %.2f %.2f colour=%06lx uv=%.1f %.1f\n", i, (double)v[i].x, (double)v[i].y, (double)v[i].z,
                (double)v[i].rhw, (unsigned long)(v[i].color & 0xffffff), (double)v[i].u, (double)v[i].v);
        IDirect3DVertexBuffer7_Unlock(dest);
    }
    Begin(0x000040);
    Untextured();
    Hr("DrawPrimitiveVB processed", IDirect3DDevice7_DrawPrimitiveVB(device, D3DPT_TRIANGLELIST, dest, 0, 3, 0));
    End("PROCESS");
    Want("PROCESS", 128, 160, 0xff8040, 2);
    Want("PROCESS", 8, 8, 0x000040, 0);
    Hr("Optimize", IDirect3DVertexBuffer7_Optimize(dest, device, 0));
    Hr("Lock optimized", IDirect3DVertexBuffer7_Lock(dest, DDLOCK_WAIT, &memory, NULL));
    IDirect3DVertexBuffer7_Release(source);
    IDirect3DVertexBuffer7_Release(dest);
}

/* Every render state number: what Get answers, and what Set answers to the
   value the state has. */
static void Sweep(void)
{
    unsigned state;
    for (state = 1; state < 160; ++state)
    {
        DWORD value = 0;
        const HRESULT get = IDirect3DDevice7_GetRenderState(device, (D3DRENDERSTATETYPE)state, &value);
        const HRESULT set = IDirect3DDevice7_SetRenderState(device, (D3DRENDERSTATETYPE)state, FAILED(get) ? 0 : value);
        if (FAILED(get) || FAILED(set)) Say("render state %u get=%08lx set=%08lx\n", state, (unsigned long)get, (unsigned long)set);
    }
}

static void Answers(void)
{
    const D3DMATRIX projection = Perspective(1.0471976f, 1.0f, 1.0f, 100.0f);
    D3DVECTOR centers[4] = {{{0}, {0}, {10}}, {{100}, {0}, {10}}, {{0}, {0}, {-50}}, {{0}, {0}, {1}}};
    D3DVALUE radii[4] = {1, 1, 1, 2};
    DWORD visible[4] = {9, 9, 9, 9}, passes = 99;
    D3DCLIPSTATUS status;
    D3DDEVINFO_TEXTUREMANAGER info;
    LPDIRECT3D7 parent = NULL;
    int i;
    IDirect3DDevice7_ApplyStateBlock(device, base_block);
    IDirect3DDevice7_SetTransform(device, D3DTRANSFORMSTATE_PROJECTION, (D3DMATRIX *)&projection);
    Hr("ComputeSphereVisibility", IDirect3DDevice7_ComputeSphereVisibility(device, centers, radii, 4, 0, visible));
    for (i = 0; i < 4; ++i) Say("sphere %d frustum=%lu\n", i, (unsigned long)(visible[i] & D3DVIS_MASK_FRUSTUM));
    Hr("ValidateDevice", IDirect3DDevice7_ValidateDevice(device, &passes));
    Say("passes=%lu\n", (unsigned long)passes);
    memset(&status, 0, sizeof status);
    Hr("GetClipStatus", IDirect3DDevice7_GetClipStatus(device, &status));
    Hr("GetInfo", IDirect3DDevice7_GetInfo(device, D3DDEVINFOID_TEXTUREMANAGER, &info, sizeof info));
    Hr("EvictManagedTextures", IDirect3D7_EvictManagedTextures(d3d));
    Hr("GetDirect3D", IDirect3DDevice7_GetDirect3D(device, &parent));
    if (parent) IDirect3D7_Release(parent);
}

/* A second device on a target with a Z buffer that has a stencil. */
static void SceneStencil(void)
{
    LPDIRECTDRAWSURFACE7 first_target = target, first_depth = depth, stencil_target = NULL, stencil_depth = NULL;
    LPDIRECT3DDEVICE7 first_device = device, stencil_device = NULL;
    DWORD first_block = base_block;
    HRESULT result;
    if (!stencil_format.dwSize) { Say("no stencil format\n"); return; }
    result = MakeTarget(&stencil_target, &stencil_depth, &stencil_format);
    Hr("target with stencil", result);
    if (FAILED(result)) return;
    result = IDirect3D7_CreateDevice(d3d, device_class, stencil_target, &stencil_device);
    Hr("CreateDevice with stencil", result);
    if (FAILED(result)) return;
    device = stencil_device;
    target = stencil_target;
    depth = stencil_depth;
    RS(D3DRENDERSTATE_ZENABLE, D3DZB_FALSE);
    RS(D3DRENDERSTATE_LIGHTING, FALSE);
    RS(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice7_CreateStateBlock(device, D3DSBT_ALL, &base_block);
    Hr("Clear with stencil", IDirect3DDevice7_Clear(device, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0x000040, 1.0f, 0));
    IDirect3DDevice7_BeginScene(device);
    Untextured();
    /* The left half writes a one into the stencil and nothing into the colour. */
    RS(D3DRENDERSTATE_STENCILENABLE, TRUE);
    RS(D3DRENDERSTATE_STENCILFUNC, D3DCMP_ALWAYS);
    RS(D3DRENDERSTATE_STENCILREF, 1);
    RS(D3DRENDERSTATE_STENCILPASS, D3DSTENCILOP_REPLACE);
    RS(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
    RS(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ZERO);
    RS(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE);
    Quad(0, 0, 128, 256, 0.5f, 0xffffffffu);
    /* Red where the stencil is one. */
    RS(D3DRENDERSTATE_ALPHABLENDENABLE, FALSE);
    RS(D3DRENDERSTATE_STENCILFUNC, D3DCMP_EQUAL);
    RS(D3DRENDERSTATE_STENCILPASS, D3DSTENCILOP_KEEP);
    Quad(32, 32, 224, 224, 0.5f, 0xffff0000u);
    End("STENCIL");
    Want("STENCIL", 64, 128, 0xff0000, 0);
    Want("STENCIL", 192, 128, 0x000040, 0);
    Want("STENCIL", 16, 16, 0x000040, 0);
    Say("stencil device release=%lu\n", (unsigned long)IDirect3DDevice7_Release(stencil_device));
    IDirectDrawSurface7_Release(stencil_depth);
    IDirectDrawSurface7_Release(stencil_target);
    device = first_device;
    target = first_target;
    depth = first_depth;
    base_block = first_block;
}

/* The exclusive mode: a flipping chain in 640x480x16 with a Z buffer. */
static void SceneExclusive(void)
{
    DDSURFACEDESC2 desc;
    DDSCAPS2 caps;
    DDPIXELFORMAT z16;
    LPDIRECTDRAWSURFACE7 front = NULL, back = NULL, z = NULL;
    LPDIRECT3DDEVICE7 first_device = device, flip_device = NULL;
    D3DVIEWPORT7 viewport;
    HRESULT result;
    result = IDirectDraw7_SetCooperativeLevel(dd, window, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
    Hr("SetCooperativeLevel exclusive", result);
    if (FAILED(result)) return;
    IDirectDrawSurface7_Release(primary);
    primary = NULL;
    result = IDirectDraw7_SetDisplayMode(dd, 640, 480, 16, 0, 0);
    Hr("SetDisplayMode", result);
    memset(&desc, 0, sizeof desc);
    desc.dwSize = sizeof desc;
    desc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
    desc.dwBackBufferCount = 1;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX | DDSCAPS_3DDEVICE;
    result = IDirectDraw7_CreateSurface(dd, &desc, &front, NULL);
    Hr("flipping chain", result);
    if (SUCCEEDED(result))
    {
        memset(&caps, 0, sizeof caps);
        caps.dwCaps = DDSCAPS_BACKBUFFER;
        Hr("back buffer", IDirectDrawSurface7_GetAttachedSurface(front, &caps, &back));
    }
    if (back)
    {
        memset(&z16, 0, sizeof z16);
        z16.dwSize = sizeof z16;
        z16.dwFlags = DDPF_ZBUFFER;
        z16.dwZBufferBitDepth = 16;
        z16.dwZBitMask = 0xffff;
        memset(&desc, 0, sizeof desc);
        desc.dwSize = sizeof desc;
        desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
        desc.dwWidth = 640;
        desc.dwHeight = 480;
        desc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER | DDSCAPS_VIDEOMEMORY;
        desc.ddpfPixelFormat = z16;
        Hr("Z buffer of the chain", IDirectDraw7_CreateSurface(dd, &desc, &z, NULL));
        if (z) Hr("attach Z buffer", IDirectDrawSurface7_AddAttachedSurface(back, z));
        result = IDirect3D7_CreateDevice(d3d, device_class, back, &flip_device);
        Hr("CreateDevice on the back buffer", result);
    }
    if (flip_device)
    {
        DWORD zenable = 99;
        device = flip_device;
        IDirect3DDevice7_GetViewport(device, &viewport);
        Say("viewport of the chain %lux%lu\n", (unsigned long)viewport.dwWidth, (unsigned long)viewport.dwHeight);
        IDirect3DDevice7_GetRenderState(device, D3DRENDERSTATE_ZENABLE, &zenable);
        Say("z enable with a Z buffer=%lu\n", (unsigned long)zenable);
        RS(D3DRENDERSTATE_LIGHTING, FALSE);
        RS(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
        Hr("Clear chain", IDirect3DDevice7_Clear(device, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0x000080, 1.0f, 0));
        IDirect3DDevice7_BeginScene(device);
        Untextured();
        Quad(160, 120, 480, 360, 0.3f, 0xff00ff00u);
        Quad(80, 60, 560, 420, 0.7f, 0xffff0000u);
        Hr("EndScene chain", IDirect3DDevice7_EndScene(device));
        Shoot(back, "EXCLUSIV");
        Want("EXCLUSIV", 320, 240, 0x00ff00, 0);
        Want("EXCLUSIV", 100, 240, 0xff0000, 0);
        Want("EXCLUSIV", 20, 20, 0x000084, 4);
        Hr("Flip", IDirectDrawSurface7_Flip(front, NULL, DDFLIP_WAIT));
        /* The device goes on drawing into the back buffer after the flip. */
        Hr("Clear after flip", IDirect3DDevice7_Clear(device, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xffffff, 1.0f, 0));
        IDirect3DDevice7_BeginScene(device);
        Quad(0, 0, 320, 480, 0.5f, 0xff0000ffu);
        IDirect3DDevice7_EndScene(device);
        Shoot(back, "EXCLUSI2");
        Want("EXCLUSI2", 100, 240, 0x0000ff, 0);
        Want("EXCLUSI2", 500, 240, 0xffffff, 0);
        Hr("Flip again", IDirectDrawSurface7_Flip(front, NULL, DDFLIP_WAIT));
        Say("chain device release=%lu\n", (unsigned long)IDirect3DDevice7_Release(flip_device));
        device = first_device;
    }
    if (z) IDirectDrawSurface7_Release(z);
    if (back) IDirectDrawSurface7_Release(back);
    if (front) IDirectDrawSurface7_Release(front);
    Hr("RestoreDisplayMode", IDirectDraw7_RestoreDisplayMode(dd));
    Hr("SetCooperativeLevel normal", IDirectDraw7_SetCooperativeLevel(dd, window, DDSCL_NORMAL));
}

int main(int argc, char **argv)
{
    MSG message;
    if (argc < 3) return 2;
    report = fopen(argv[1], "w");
    if (!report) return 2;
    strcpy(folder, argv[2]);
    if (argc > 3 && !strcmp(argv[3], "tnl")) device_class = &IID_IDirect3DTnLHalDevice;
    if (Open())
    {
        Defaults();
        Errors();
        /* The state the scenes start from. */
        RS(D3DRENDERSTATE_ZENABLE, D3DZB_FALSE);
        RS(D3DRENDERSTATE_LIGHTING, FALSE);
        RS(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
        Hr("CreateStateBlock", IDirect3DDevice7_CreateStateBlock(device, D3DSBT_ALL, &base_block));
        SceneTransformed();
        SceneCull();
        SceneTextures();
        SceneMip();
        SceneBlend();
        SceneDepth();
        SceneFog();
        SceneStages();
        SceneColorKey();
        ScenePrimitives();
        ScenePipeline();
        SceneLight();
        SceneRenderToTexture();
        SceneLoad();
        SceneStateBlocks();
        SceneProcessVertices();
        Answers();
        Sweep();
        SceneStencil();
        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&message);
        Say("device release=%lu\n", (unsigned long)IDirect3DDevice7_Release(device));
        device = NULL;
        IDirectDrawSurface7_Release(depth);
        IDirectDrawSurface7_Release(target);
        SceneExclusive();
        IDirect3D7_Release(d3d);
        if (primary) IDirectDrawSurface7_Release(primary);
        Say("directdraw release=%lu\n", (unsigned long)IDirectDraw7_Release(dd));
    }
    Say("D3D scenes=%d wrong=%d\n", scenes, wrong);
    fclose(report);
    return wrong ? 1 : 0;
}
