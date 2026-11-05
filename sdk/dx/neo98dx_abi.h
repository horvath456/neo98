/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The structures of NEO98DX.DLL, the driver below the ARM64 DDRAW.DLL and
   DSOUND.DLL. The functions are in
   sdk/api/70-neo98dx.api; the DLLs under sdk/dx and the driver in
   src/x86_dx_driver.cpp and src/x86_dx_sound.cpp include this file.

   Every field is 32 bits wide, so a structure has the same bytes in a Win32
   and in a Win64 layout and crosses without conversion. An address is a
   32-bit field: every pointer of an ARM64 process lies below 4 GiB. */
#ifndef NEO98DX_ABI_H
#define NEO98DX_ABI_H

#define NEO98DX_VERSION 2u

typedef unsigned int DXU32;
typedef int DXS32;

/* The part of a DDPIXELFORMAT a surface is described by. `flags` are DDPF_
   values. A Z buffer has the bits of its stencil in `r`, its depth mask in `g`
   and its stencil mask in `b`, the places of dwStencilBitDepth, dwZBitMask and
   dwStencilBitMask in a DDPIXELFORMAT. */
typedef struct NEO98DX_FORMAT
{
    DXU32 flags, fourcc, bits, r, g, b, a;
} NEO98DX_FORMAT;

/* A surface in a call: a surface of the driver by its handle, or bytes of the
   program (handle zero). */
typedef struct NEO98DX_PLANE
{
    DXU32 handle;
    DXU32 bits;
    DXS32 pitch;
    DXU32 width, height;
    NEO98DX_FORMAT format;
} NEO98DX_PLANE;

#define NEO98DX_CAP_3D 0x00000001u         /* a rasterizer is installed */
#define NEO98DX_CAP_GAMMA 0x00000002u
#define NEO98DX_CAP_DXT 0x00000004u        /* the rasterizer samples DXT1 to DXT5: FourCC surfaces */

typedef struct NEO98DX_INFO
{
    DXU32 size, version;
    DXU32 width, height, pitch, refresh;   /* the display mode now */
    NEO98DX_FORMAT format;
    DXU32 modeSerial;                      /* counts every change of the mode */
    DXU32 modeCount;
    DXU32 caps;
    DXU32 videoMemoryTotal, videoMemoryFree;
    DXU32 desktopWidth, desktopHeight, desktopBits;
    DXU32 exclusiveProcess;                /* the process in exclusive mode, or 0 */
    DXU32 process;                         /* the caller */
} NEO98DX_INFO;

typedef struct NEO98DX_MODE
{
    DXU32 width, height, bits, refresh;
    NEO98DX_FORMAT format;
    DXU32 pitch;
} NEO98DX_MODE;

/* DdDrvCreateSurface. In: caps (DDSCAPS_), caps2, size, format (bits zero:
   the format of the display). Out: handle, the bytes, the pitch, and for the
   primary surface size and format of the display. */
typedef struct NEO98DX_CREATE
{
    DXU32 size;
    DXU32 caps, caps2;
    DXU32 width, height;
    NEO98DX_FORMAT format;
    DXU32 handle;
    DXU32 bits;
    DXS32 pitch;
    DXU32 modeSerial;
} NEO98DX_CREATE;

/* DdDrvLock. flags are DDLOCK_ values. */
typedef struct NEO98DX_LOCK
{
    DXU32 size;
    DXU32 handle;
    DXU32 flags;
    DXU32 hasRect;
    DXS32 rect[4];
    DXU32 bits;      /* out: the first byte of the surface */
    DXS32 pitch;     /* out */
} NEO98DX_LOCK;

#define NEO98DX_BLT_FILL 0x00000001u          /* `fill` into the destination rect */
#define NEO98DX_BLT_KEYSRC 0x00000002u        /* source pixels inside the key stay out */
#define NEO98DX_BLT_KEYDEST 0x00000004u       /* only destination pixels inside the key are written */
#define NEO98DX_BLT_MIRRORX 0x00000008u
#define NEO98DX_BLT_MIRRORY 0x00000010u
#define NEO98DX_BLT_DEPTHFILL 0x00000020u     /* a fill of a Z buffer */
#define NEO98DX_BLT_NOWAIT 0x00000040u        /* do not pace a whole frame */

typedef struct NEO98DX_BLT
{
    DXU32 size;
    NEO98DX_PLANE dst, src;      /* src.width zero: no source */
    DXS32 dstRect[4], srcRect[4];
    DXU32 flags;
    DXU32 fill;
    DXU32 srcKeyLow, srcKeyHigh, dstKeyLow, dstKeyHigh;
    DXU32 clipWindow;            /* a window: the visible region of its client area clips */
    DXU32 clipCount, clipRects;  /* or RECTs in destination coordinates at this address */
    DXU32 clipped;               /* clipWindow or clipRects apply */
} NEO98DX_BLT;

/* DdDrvGetDC: the surface and, for eight bits, its 256 colours as
   0x00RRGGBB at `colors` (zero: the colours of the display). */
typedef struct NEO98DX_DC
{
    DXU32 size;
    NEO98DX_PLANE plane;
    DXU32 colors;
    DXU32 dc;                    /* out */
} NEO98DX_DC;

/* Sound. */
#define NEO98DX_SOUND_PRIMARY 0x00000001u
#define NEO98DX_SOUND_STATIC 0x00000002u
#define NEO98DX_SOUND_BACKGROUND 0x00000004u

typedef struct NEO98DX_SOUND
{
    DXU32 size;
    DXU32 flags;
    DXU32 bytes;
    DXU32 rate, channels, bitsPerSample;
    DXU32 handle;                /* out */
    DXU32 memory;                /* out: the bytes of the buffer */
} NEO98DX_SOUND;

typedef struct NEO98DX_SOUNDNOTIFY
{
    DXU32 offset, event;
} NEO98DX_SOUNDNOTIFY;

typedef struct NEO98DX_SOUNDCAPS
{
    DXU32 size;
    DXU32 voices, voicesFree;    /* buffers that can sound at once */
    DXU32 outputRate, outputChannels, outputBits;
    DXU32 lead;                  /* frames the output takes in one piece */
    DXU32 live;                  /* a host device carries the frames away */
} NEO98DX_SOUNDCAPS;

/* DdDrvFlip: besides the DDFLIP_ values, turn the chain until the surface
   that holds the screen is in front (FlipToGDISurface). */
#define NEO98DX_FLIP_TOGDI 0x80000000u

/* ------------------------------------------------------------------ Direct3D
   The Direct3D of the SDK (sdk/dx/ddraw/d3d_*.c) keeps the state of a device
   itself and collects what it is asked to draw; the driver gets one call per
   batch, D3dDrvRun with a stream of commands, never one per triangle. A
   context of the driver is the rasterizer's side of a device: a render
   target, a Z buffer and the state the commands left in it. */

/* D3dDrvGetCaps: what the rasterizer does, in the fields of a D3DDEVICEDESC7,
   and the formats of textures and Z buffers. */
#define NEO98D3D_CAP_TNL 0x00000001u       /* runs transform and lighting itself */
#define NEO98D3D_MAX_FORMATS 24u

typedef struct NEO98D3D_CAPS
{
    DXU32 size;
    DXU32 flags;
    DXU32 devCaps;
    DXU32 miscCaps, rasterCaps, zCmpCaps, srcBlendCaps, destBlendCaps, alphaCmpCaps, shadeCaps;
    DXU32 textureCaps, textureFilterCaps, textureBlendCaps, textureAddressCaps;
    DXU32 renderBitDepth, zBufferBitDepth;       /* DDBD_ values */
    DXU32 minTextureWidth, minTextureHeight, maxTextureWidth, maxTextureHeight;
    DXU32 maxTextureRepeat, maxTextureAspectRatio, maxAnisotropy;
    DXU32 stencilCaps, fvfCaps, textureOpCaps;
    DXU32 maxTextureBlendStages, maxSimultaneousTextures;
    DXU32 maxActiveLights, maxUserClipPlanes, maxVertexBlendMatrices, vertexProcessingCaps;
    DXU32 maxVertexW;                            /* the bits of a float */
    DXU32 textureFormatCount, zFormatCount;
    NEO98DX_FORMAT textureFormats[NEO98D3D_MAX_FORMATS];
    NEO98DX_FORMAT zFormats[NEO98D3D_MAX_FORMATS];
} NEO98D3D_CAPS;

/* A command: its number, the bytes of the whole command with this header (a
   multiple of four), then its words. Floats travel as their bits. */
typedef struct NEO98D3D_COMMAND
{
    DXU32 op, bytes;
} NEO98D3D_COMMAND;

#define NEO98D3D_OP_RENDERSTATE 1u   /* pairs: D3DRENDERSTATETYPE, value */
#define NEO98D3D_OP_STAGESTATE 2u    /* triples: stage, D3DTEXTURESTAGESTATETYPE, value */
#define NEO98D3D_OP_TEXTURE 3u       /* pairs: stage, driver surface or 0 */
#define NEO98D3D_OP_VIEWPORT 4u      /* x, y, width, height, minZ, maxZ (a D3DVIEWPORT7) */
#define NEO98D3D_OP_TRANSFORM 5u     /* D3DTRANSFORMSTATETYPE, sixteen floats */
#define NEO98D3D_OP_MATERIAL 6u      /* a D3DMATERIAL7, seventeen floats */
#define NEO98D3D_OP_LIGHT 7u         /* slot, a D3DLIGHT7 (26 words) */
#define NEO98D3D_OP_LIGHTENABLE 8u   /* slot, BOOL */
#define NEO98D3D_OP_CLIPPLANE 9u     /* index, four floats */
#define NEO98D3D_OP_CLEAR 10u        /* flags, colour, z, stencil, count, then count rects of four LONGs */
#define NEO98D3D_OP_DRAW 11u         /* an NEO98D3D_DRAW */
#define NEO98D3D_OP_TARGET 12u       /* driver surface of the target, of the Z buffer or 0 */

/* The lights a context keeps; the DLL maps the light numbers of a program,
   which are any DWORD, onto these. */
#define NEO98D3D_LIGHT_SLOTS 32u
#define NEO98D3D_STAGES 8u
#define NEO98D3D_CLIPPLANES 6u

/* One DrawPrimitive: the vertices lie in memory of the process (the DLL
   copied them when the program drew), `stride` bytes apart in the layout of
   `fvf`; `indices` are WORDs or absent. */
typedef struct NEO98D3D_DRAW
{
    DXU32 primitive;             /* D3DPRIMITIVETYPE */
    DXU32 fvf;
    DXU32 stride;
    DXU32 vertices;              /* address of vertex 0 */
    DXU32 vertexCount;
    DXU32 indices;               /* address, or 0 */
    DXU32 indexCount;
} NEO98D3D_DRAW;

#define NEO98DX_STATUS_PLAYING 0x00000001u
#define NEO98DX_STATUS_LOOPING 0x00000004u

#endif
