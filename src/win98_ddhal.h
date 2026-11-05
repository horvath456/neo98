// SPDX-License-Identifier: GPL-2.0-or-later
// Windows 98 display-driver and DirectDraw/Direct3D HAL interfaces: structure
// sizes, field offsets, capabilities, and DrawPrimitives2 commands. Drivers
// must report supported escapes, compatible interface versions, valid
// callback tables, and surface creation support. Bind guest callbacks in
// src/x86_ddhal.cpp and rendering in src/gpu_backend.h.
#pragma once
namespace Win98DDHal
{
using U = unsigned;

// What the driver answers DDVERSIONINFO: the DirectX 7 driver model, the one
// whose DrawPrimitives2 this HAL implements.
constexpr U HalVersion = 0x700;
// DCI version 1.00, the QUERYESCSUPPORT answer of a driver with a HAL.
constexpr U DciVersion = 0x100;

// DDHALINFO, 0x1cc bytes.
namespace Info
{
constexpr U Size = 0x1cc;
constexpr U Callbacks = 0x04, SurfaceCallbacks = 0x08, PaletteCallbacks = 0x0c;
constexpr U VidMem = 0x10;              // VIDMEMINFO, 0x50 bytes
constexpr U Caps = 0x60;                // DDCORECAPS, 0x13c bytes
constexpr U MonitorFrequency = 0x19c, GetDriverInfo = 0x1a0, ModeIndex = 0x1a4;
constexpr U FourCC = 0x1a8, ModeCount = 0x1ac, ModeInfo = 0x1b0, Flags = 0x1b4;
constexpr U PDevice = 0x1b8, Instance = 0x1bc, D3DGlobalDriverData = 0x1c0;
constexpr U D3DCallbacks = 0x1c4, ExeBufCallbacks = 0x1c8;
// DDHALINFO_ISPRIMARYDISPLAY | DDHALINFO_MODEXILLEGAL | DDHALINFO_GETDRIVERINFOSET;
// DDHALINFO_GETDRIVERINFO2 lets the runtime of DirectX 8 and 9 ask this driver
// through GUID_GetDriverInfo2.
constexpr U FlagsValue = 1 | 2 | 4, GetDriverInfo2 = 8;
} // namespace Info

// VIDMEMINFO relative to Info::VidMem.
namespace VidMemInfo
{
constexpr U Primary = 0x00, Flags = 0x04, DisplayWidth = 0x08, DisplayHeight = 0x0c;
constexpr U DisplayPitch = 0x10, DisplayFormat = 0x14;   // DDPIXELFORMAT, 0x20
constexpr U OffscreenAlign = 0x34, OverlayAlign = 0x38, TextureAlign = 0x3c;
constexpr U ZBufferAlign = 0x40, AlphaAlign = 0x44, HeapCount = 0x48, Heaps = 0x4c;
constexpr U Size = 0x50;
} // namespace VidMemInfo

// VIDMEM, one heap, 0x18 bytes.
namespace Heap
{
constexpr U Flags = 0x00, Start = 0x04, End = 0x08, Caps = 0x0c, CapsAlt = 0x10, HeapPointer = 0x14;
constexpr U Size = 0x18;
constexpr U Linear = 1;   // VIDMEM_ISLINEAR
} // namespace Heap

// DDCORECAPS relative to Info::Caps.
namespace Caps
{
constexpr U Size = 0x13c;
constexpr U DwCaps = 0x04, Caps2 = 0x08, CKeyCaps = 0x0c, FXCaps = 0x10, FXAlphaCaps = 0x14;
constexpr U PalCaps = 0x18, SVCaps = 0x1c, ZBufferBitDepths = 0x38, VidMemTotal = 0x3c;
constexpr U VidMemFree = 0x40, MaxVisibleOverlays = 0x44, CurrVisibleOverlays = 0x48;
constexpr U NumFourCC = 0x4c, Rops = 0x64, SurfaceCaps = 0x84;
constexpr U SVBCaps = 0xac, VSBCaps = 0xd8, SSBCaps = 0x104, MaxVideoPorts = 0x130;
// Each of SVB, VSB and SSB is dwCaps, dwCKeyCaps, dwFXCaps and dwRops[8].
constexpr U BltCKeyCaps = 0x04, BltFXCaps = 0x08, BltRops = 0x0c;
constexpr U CurrVideoPorts = 0x134, SVBCaps2 = 0x138;
} // namespace Caps

// DDCAPS_ bits of dwCaps.
constexpr U DDCAPS_3D = 0x1, DDCAPS_BLT = 0x40, DDCAPS_GDI = 0x400, DDCAPS_PALETTE = 0x8000;
constexpr U DDCAPS_BLTCOLORFILL = 0x4000000, DDCAPS_BLTDEPTHFILL = 0x10000000;
constexpr U DDCAPS_CANBLTSYSMEM = 0x80000000u;
constexpr U DDCAPS_BLTSTRETCH = 0x200, DDCAPS_COLORKEY = 0x400000, DDCAPS_CANCLIP = 0x20000000;
constexpr U DDCAPS_ZBLTS = 0x100000;
// DDCKEYCAPS_SRCBLT: a source colour key on a blit; DESTBLT: a destination key.
constexpr U DDCKEYCAPS_SRCBLT = 0x200, DDCKEYCAPS_DESTBLT = 0x2;
// DDFXCAPS_ of dwFXCaps: what a stretching blit may do.
constexpr U DDFXCAPS_BLTMIRRORLEFTRIGHT = 0x40, DDFXCAPS_BLTMIRRORUPDOWN = 0x80;
constexpr U DDFXCAPS_BLTSHRINKX = 0x400, DDFXCAPS_BLTSHRINKXN = 0x800, DDFXCAPS_BLTSHRINKY = 0x1000;
constexpr U DDFXCAPS_BLTSHRINKYN = 0x2000, DDFXCAPS_BLTSTRETCHX = 0x4000, DDFXCAPS_BLTSTRETCHXN = 0x8000;
constexpr U DDFXCAPS_BLTSTRETCHY = 0x10000, DDFXCAPS_BLTSTRETCHYN = 0x20000;
// DDCAPS2_ bits.
constexpr U DDCAPS2_WIDESURFACES = 0x1000, DDCAPS2_PRIMARYGAMMA = 0x20000, DDCAPS2_CANRENDERWINDOWED = 0x80000;
constexpr U DDCAPS2_FLIPNOVSYNC = 0x400000;
// DDBD_ bit depths.
constexpr U DDBD_8 = 0x800, DDBD_16 = 0x400, DDBD_24 = 0x200, DDBD_32 = 0x100;

// DDSCAPS_ bits.
constexpr U DDSCAPS_ALPHA = 0x2, DDSCAPS_BACKBUFFER = 0x4, DDSCAPS_COMPLEX = 0x8, DDSCAPS_FLIP = 0x10;
constexpr U DDSCAPS_FRONTBUFFER = 0x20, DDSCAPS_OFFSCREENPLAIN = 0x40, DDSCAPS_PALETTE = 0x100;
constexpr U DDSCAPS_PRIMARYSURFACE = 0x200, DDSCAPS_SYSTEMMEMORY = 0x800, DDSCAPS_TEXTURE = 0x1000;
constexpr U DDSCAPS_3DDEVICE = 0x2000, DDSCAPS_VIDEOMEMORY = 0x4000, DDSCAPS_VISIBLE = 0x8000;
constexpr U DDSCAPS_ZBUFFER = 0x20000, DDSCAPS_MIPMAP = 0x400000, DDSCAPS_LOCALVIDMEM = 0x10000000;
constexpr U DDSCAPS_EXECUTEBUFFER = 0x800000;   // DDSCAPS_RESERVED2 in ddrawi.h

// DDPIXELFORMAT, 0x20 bytes.
namespace PixelFormat
{
constexpr U Size = 0x20;
constexpr U Flags = 0x04, FourCC = 0x08, BitCount = 0x0c, RMask = 0x10, GMask = 0x14, BMask = 0x18, AMask = 0x1c;
constexpr U AlphaPixels = 0x1, Alpha = 0x2, FourCCFlag = 0x4, Indexed8 = 0x20, RGB = 0x40, ZBuffer = 0x400;
// DDPF_BUMPLUMINANCE and DDPF_BUMPDUDV: du and dv in the red and green
// masks, the luminance in the blue one.
constexpr U BumpLuminance = 0x40000, BumpDuDv = 0x80000;
constexpr U StencilBuffer = 0x4000;
} // namespace PixelFormat

// DDHALMODEINFO, 0x24 bytes.
namespace ModeInfo
{
constexpr U Size = 0x24;
constexpr U Width = 0x00, Height = 0x04, Pitch = 0x08, BPP = 0x0c, Flags = 0x10;   // WORD wFlags, WORD wRefreshRate
constexpr U RMask = 0x14, GMask = 0x18, BMask = 0x1c, AMask = 0x20;
constexpr U Palettized = 1;   // DDMODEINFO_PALETTIZED
} // namespace ModeInfo

// DDHAL_DDCALLBACKS, 0x30 bytes, with its DDHAL_CB32_ flags.
namespace DriverCallbacks
{
constexpr U Size = 0x30;
constexpr U DestroyDriver = 0x08, CreateSurface = 0x0c, SetColorKey = 0x10, SetMode = 0x14;
constexpr U WaitForVerticalBlank = 0x18, CanCreateSurface = 0x1c, CreatePalette = 0x20;
constexpr U GetScanLine = 0x24, SetExclusiveMode = 0x28, FlipToGDISurface = 0x2c;
constexpr U FlagDestroyDriver = 0x1, FlagCreateSurface = 0x2, FlagSetColorKey = 0x4, FlagSetMode = 0x8;
constexpr U FlagWaitForVerticalBlank = 0x10, FlagCanCreateSurface = 0x20, FlagCreatePalette = 0x40;
constexpr U FlagGetScanLine = 0x80, FlagSetExclusiveMode = 0x100, FlagFlipToGDISurface = 0x200;
} // namespace DriverCallbacks

// DDHAL_DDSURFACECALLBACKS, 0x40 bytes, with its DDHAL_SURFCB32_ flags.
namespace SurfaceCallbacks
{
constexpr U Size = 0x40;
constexpr U DestroySurface = 0x08, Flip = 0x0c, SetClipList = 0x10, Lock = 0x14, Unlock = 0x18;
constexpr U Blt = 0x1c, SetColorKey = 0x20, AddAttachedSurface = 0x24, GetBltStatus = 0x28;
constexpr U GetFlipStatus = 0x2c, UpdateOverlay = 0x30, SetOverlayPosition = 0x34, SetPalette = 0x3c;
constexpr U FlagDestroySurface = 0x1, FlagFlip = 0x2, FlagSetClipList = 0x4, FlagLock = 0x8;
constexpr U FlagUnlock = 0x10, FlagBlt = 0x20, FlagSetColorKey = 0x40, FlagAddAttachedSurface = 0x80;
constexpr U FlagGetBltStatus = 0x100, FlagGetFlipStatus = 0x200, FlagSetPalette = 0x2000;
} // namespace SurfaceCallbacks

// DDHAL_DDPALETTECALLBACKS, 0x10 bytes.
namespace PaletteCallbacks
{
constexpr U Size = 0x10;
constexpr U DestroyPalette = 0x08, SetEntries = 0x0c;
} // namespace PaletteCallbacks

// DDHAL_DDMISCELLANEOUS2CALLBACKS, 0x18 bytes, GUID_Miscellaneous2Callbacks.
namespace Misc2Callbacks
{
constexpr U Size = 0x18;
constexpr U AlphaBlt = 0x08, CreateSurfaceEx = 0x0c, GetDriverState = 0x10, DestroyDDLocal = 0x14;
constexpr U FlagCreateSurfaceEx = 0x2, FlagGetDriverState = 0x4, FlagDestroyDDLocal = 0x8;
} // namespace Misc2Callbacks

// D3DHAL_CALLBACKS is 0x8c bytes. Execute-buffer-era callbacks and reserved
// slots must be zero before exposing the HAL device.
namespace D3DCallbacks
{
constexpr U Size = 0x8c;
constexpr U ContextCreate = 0x04, ContextDestroy = 0x08, ContextDestroyAll = 0x0c, SceneCapture = 0x10;
constexpr U TextureCreate = 0x28, TextureDestroy = 0x2c, TextureSwap = 0x30, TextureGetSurf = 0x34;
constexpr U GetState = 0x88;
} // namespace D3DCallbacks

// D3DHAL_CALLBACKS3, 0x18 bytes, GUID_D3DCallbacks3.
namespace D3DCallbacks3
{
constexpr U Size = 0x18;
constexpr U Flags = 0x04, Clear2 = 0x08, ValidateTextureStageState = 0x10, DrawPrimitives2 = 0x14;
constexpr U FlagClear2 = 0x1, FlagValidateTextureStageState = 0x4, FlagDrawPrimitives2 = 0x8;
} // namespace D3DCallbacks3

// D3DHAL_GLOBALDRIVERDATA, 0xc0 bytes: dwSize, D3DDEVICEDESC_V1 hwCaps (0xac),
// dwNumVertices, dwNumClipVertices, dwNumTextureFormats, lpTextureFormats.
namespace GlobalDriverData
{
constexpr U Size = 0xc0;
constexpr U HwCaps = 0x04, NumVertices = 0xb0, NumClipVertices = 0xb4, NumTextureFormats = 0xb8;
constexpr U TextureFormats = 0xbc;
} // namespace GlobalDriverData

// D3DDEVICEDESC_V1, 0xac bytes.
namespace DeviceDesc
{
constexpr U Size = 0xac;
constexpr U Flags = 0x04, ColorModel = 0x08, DevCaps = 0x0c, TransformCaps = 0x10;   // D3DTRANSFORMCAPS 8
constexpr U Clipping = 0x18, LightingCaps = 0x1c;   // D3DLIGHTINGCAPS 16
constexpr U LineCaps = 0x2c, TriCaps = 0x64;         // D3DPRIMCAPS 0x38 each
constexpr U RenderBitDepth = 0x9c, ZBufferBitDepth = 0xa0, MaxBufferSize = 0xa4, MaxVertexCount = 0xa8;
// D3DDD_ flags: every field is valid.
constexpr U AllFields = 0x7ff;
constexpr U ColorRGB = 2;
// D3DDEVCAPS_ of a DrawPrimitives2 rasterizer without transform and lighting.
constexpr U DevCapsValue = 0x1        // FLOATTLVERTEX
                         | 0x40       // TLVERTEXSYSTEMMEMORY
                         | 0x80       // TLVERTEXVIDEOMEMORY
                         | 0x200      // TEXTUREVIDEOMEMORY
                         | 0x400      // DRAWPRIMTLVERTEX
                         | 0x800      // CANRENDERAFTERFLIP
                         | 0x2000     // DRAWPRIMITIVES2
                         | 0x8000     // DRAWPRIMITIVES2EX
                         | 0x80000;   // HWRASTERIZATION
// D3DDEVCAPS_HWTRANSFORMANDLIGHT: D3DIM hands the driver untransformed
// vertices with the transforms, lights and material, and the backend's vertex
// shader does the fixed function pipeline.
constexpr U HwTransformAndLight = 0x10000;
constexpr U TransformClip = 0x1;                  // D3DTRANSFORMCAPS_CLIP
constexpr U LightCapsValue = 0x1 | 0x2 | 0x4;     // D3DLIGHTCAPS_POINT, SPOT, DIRECTIONAL
constexpr U MaxLights = 8;
} // namespace DeviceDesc

// D3DPRIMCAPS, 0x38 bytes.
namespace PrimCaps
{
constexpr U Size = 0x38;
constexpr U MiscCaps = 0x04, RasterCaps = 0x08, ZCmpCaps = 0x0c, SrcBlendCaps = 0x10, DestBlendCaps = 0x14;
constexpr U AlphaCmpCaps = 0x18, ShadeCaps = 0x1c, TextureCaps = 0x20, TextureFilterCaps = 0x24;
constexpr U TextureBlendCaps = 0x28, TextureAddressCaps = 0x2c, StippleWidth = 0x30, StippleHeight = 0x34;
constexpr U MiscValue = 0x2 | 0x10 | 0x20 | 0x40;                 // MASKZ, CULLNONE, CULLCW, CULLCCW
constexpr U RasterValue = 0x10 | 0x20 | 0x80 | 0x100 | 0x4000 | 0x200000   // ZTEST, SUBPIXEL, FOGVERTEX, FOGTABLE, ZBIAS, ZFOG
                        | 0x40000 | 0x100000;                              // WBUFFER, WFOG
constexpr U CmpValue = 0xff;                                       // every D3DCMP_
constexpr U BlendValue = 0x1fff;                                   // every D3DBLEND_
// Flat and Gouraud: COLORFLATRGB, COLORGOURAUDRGB, SPECULARFLATRGB, SPECULARGOURAUDRGB, ALPHAFLATBLEND, ALPHAGOURAUDBLEND, FOGFLAT, FOGGOURAUD.
constexpr U ShadeValue = 0x2 | 0x8 | 0x80 | 0x200 | 0x1000 | 0x4000 | 0x40000 | 0x80000;
constexpr U TextureValue = 0x1 | 0x4 | 0x8     // PERSPECTIVE, ALPHA, TRANSPARENCY (colour key drops the texel)
                         | 0x80 | 0x800;       // ALPHAPALETTE, CUBEMAP; no key blend
// What the DirectDraw HAL reports adds POW2, as every card of the time did.
// The original DDRAW rejects an automatically attached mip chain of a
// 192x192 texture with DDERR_INVALIDPARAMS before it calls the HAL; Blitz3D
// pads a texture to a power of two only when this bit is set (Wonderland
// 1.12, conveyor.jpg).
constexpr U TextureHalValue = TextureValue | 0x2;
// Point and linear with mip levels: NEAREST, LINEAR, MIPNEAREST, MIPLINEAR, LINEARMIPNEAREST, LINEARMIPLINEAR,
// MINFPOINT, MINFLINEAR, MIPFPOINT, MIPFLINEAR, MAGFPOINT, MAGFLINEAR.
constexpr U FilterValue = 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20 | 0x100 | 0x200 | 0x10000 | 0x20000 | 0x1000000 | 0x2000000
                        | 0x400 | 0x4000000;   // MINFANISOTROPIC, MAGFANISOTROPIC
constexpr U TextureBlendValue = 0x1 | 0x2 | 0x4 | 0x8 | 0x40 | 0x80;   // DECAL, MODULATE, DECALALPHA, MODULATEALPHA, COPY, ADD
constexpr U AddressValue = 0x1 | 0x2 | 0x4 | 0x10;               // WRAP, MIRROR, CLAMP, INDEPENDENTUV
} // namespace PrimCaps

// D3DHAL_D3DEXTENDEDCAPS, 0x74 bytes in the DirectX 7 form (DDRAW 0x1c2f7).
namespace ExtendedCaps
{
constexpr U Size = 0x74;
constexpr U MinTextureWidth = 0x04, MaxTextureWidth = 0x08, MinTextureHeight = 0x0c, MaxTextureHeight = 0x10;
constexpr U MaxTextureRepeat = 0x24, MaxTextureAspectRatio = 0x28, MaxAnisotropy = 0x2c;
constexpr U GuardBandLeft = 0x30, ExtentsAdjust = 0x40, StencilCaps = 0x44, FVFCaps = 0x48;
constexpr U TextureOpCaps = 0x4c, MaxTextureBlendStages = 0x50;   // WORD, WORD wMaxSimultaneousTextures
constexpr U MaxActiveLights = 0x54, MaxVertexW = 0x58, MaxUserClipPlanes = 0x5c, VertexProcessingCaps = 0x60;
constexpr U ClipPlanes = 6, BlendMatrices = 4;
// D3DVTXPCAPS_TEXGEN, MATERIALSOURCE7, VERTEXFOG, DIRECTIONALLIGHTS, POSITIONALLIGHTS, LOCALVIEWER.
constexpr U VertexProcessingValue = 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20;
// D3DTEXOPCAPS_ the fragment shader of the backend answers.
constexpr U TextureOpValue = 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20 | 0x40 | 0x80 | 0x100 | 0x200 | 0x400
                           | 0x800 | 0x1000 | 0x2000 | 0x4000 | 0x8000
                           | 0x20000 | 0x40000 | 0x80000 | 0x100000   // MODULATEALPHA_ADDCOLOR.. MODULATEINVCOLOR_ADDALPHA
                           | 0x200000 | 0x400000                      // BUMPENVMAP, BUMPENVMAPLUMINANCE
                           | 0x800000;                                 // DOTPRODUCT3
constexpr U Stages = 8, MaxAnisotropyValue = 16;
// D3DSTENCILCAPS_KEEP, ZERO, REPLACE, INCRSAT, DECRSAT, INVERT, INCR, DECR.
constexpr U StencilValue = 0xff;
} // namespace ExtendedCaps

// DD_GETDRIVERINFO2DATA at the front of lpvData: dwReserved, dwMagic
// (0xffffffff), dwType and dwExpectedSize; the answer follows the header or
// replaces it, depending on the type.
namespace DriverInfo2
{
constexpr U Magic = 0x04, Type = 0x08, Expected = 0x0c, Payload = 0x10;
constexpr U MagicValue = 0xffffffffu;
// D3DGDI2_TYPE_: the D3DCAPS8 (written over the header), the number of the
// format list, one format, the version of the runtime, the D3DCAPS9.
constexpr U GetCaps8 = 0x1, GetFormatCount = 0x2, GetFormat = 0x3, DxVersion = 0x4, GetCaps9 = 0x10;
// D3DGDI2_TYPE_GETDDIVERSION: the runtime's version, then the DDI version
// the driver speaks (DX9_DDI_VERSION 4).
constexpr U GetDdiVersion = 0x23, Ddi9 = 4;
// D3DGDI2_TYPE_GETD3DQUERYCOUNT and GETD3DQUERY: the query types the driver runs.
constexpr U GetQueryCount = 0x21, GetQuery = 0x22;
constexpr U Caps8Size = 212, Caps9Size = 304;
} // namespace DriverInfo2

// D3DCAPS8, DWORD indices; D3DCAPS9 starts with the same 53.
namespace Caps8
{
constexpr U DeviceType = 0, Caps = 2, Caps2 = 3, Caps3 = 4, PresentationIntervals = 5, CursorCaps = 6, DevCaps = 7;
constexpr U PrimitiveMiscCaps = 8, RasterCaps = 9, ZCmpCaps = 10, SrcBlendCaps = 11, DestBlendCaps = 12;
constexpr U AlphaCmpCaps = 13, ShadeCaps = 14, TextureCaps = 15, TextureFilterCaps = 16, CubeTextureFilterCaps = 17;
constexpr U VolumeTextureFilterCaps = 18, TextureAddressCaps = 19, VolumeTextureAddressCaps = 20, LineCaps = 21;
constexpr U MaxTextureWidth = 22, MaxTextureHeight = 23, MaxVolumeExtent = 24, MaxTextureRepeat = 25;
constexpr U MaxTextureAspectRatio = 26, MaxAnisotropy = 27, MaxVertexW = 28, GuardBandLeft = 29, ExtentsAdjust = 33;
constexpr U StencilCaps = 34, FVFCaps = 35, TextureOpCaps = 36, MaxTextureBlendStages = 37, MaxSimultaneousTextures = 38;
constexpr U VertexProcessingCaps = 39, MaxActiveLights = 40, MaxUserClipPlanes = 41, MaxVertexBlendMatrices = 42;
constexpr U MaxVertexBlendMatrixIndex = 43, MaxPointSize = 44, MaxPrimitiveCount = 45, MaxVertexIndex = 46;
constexpr U MaxStreams = 47, MaxStreamStride = 48, VertexShaderVersion = 49, MaxVertexShaderConst = 50;
constexpr U PixelShaderVersion = 51, MaxPixelShaderValue = 52, Count = 53;
// D3DPTEXTURECAPS_ of DirectX 8: PERSPECTIVE, ALPHA, ALPHAPALETTE, CUBEMAP,
// VOLUMEMAP, MIPMAP, MIPVOLUMEMAP, MIPCUBEMAP; no power of two is required.
constexpr U TextureValue = 0x1 | 0x4 | 0x80 | 0x800 | 0x2000 | 0x4000 | 0x8000 | 0x10000;
// The largest volume a Metal 3D texture holds.
constexpr U VolumeExtent = 2048;
// D3DPTFILTERCAPS_ MIN, MIP and MAG with point, linear and anisotropic.
constexpr U FilterValue = 0x100 | 0x200 | 0x400 | 0x10000 | 0x20000 | 0x1000000 | 0x2000000 | 0x4000000;
// D3DLINECAPS_ TEXTURE, ZTEST, BLEND, ALPHACMP, FOG.
constexpr U LineValue = 0x1f;
// D3DVTXPCAPS_ TEXGEN, MATERIALSOURCE7, DIRECTIONALLIGHTS, POSITIONALLIGHTS, LOCALVIEWER.
constexpr U VertexProcessingValue = 0x1 | 0x2 | 0x8 | 0x10 | 0x20;
// D3DPRESENT_INTERVAL_ONE and IMMEDIATE; D3DCAPS2_FULLSCREENGAMMA and CANRENDERWINDOWED.
constexpr U PresentationValue = 0x1 | 0x80000000u, Caps2Value = 0x20000 | 0x80000;
// ps_1_4, and the range of its registers (the same as src/d3d_shader_msl.h).
constexpr U PixelShaderValue = 0xffff0104u, VertexShaderValue = 0xfffe0101u, VertexConstants = 96;
constexpr float PixelShaderRange = 8.0f;
} // namespace Caps8

// D3DCAPS9 behind the 53 DWORDs it shares with D3DCAPS8.
namespace Caps9
{
constexpr U DevCaps2 = 53, MaxNPatchTessellation = 54, MasterAdapterOrdinal = 56, AdapterOrdinalInGroup = 57;
constexpr U NumberOfAdaptersInGroup = 58, DeclTypes = 59, NumSimultaneousRTs = 60, StretchRectFilterCaps = 61;
constexpr U VS20Caps = 62, PS20Caps = 66, VertexTextureFilterCaps = 71, MaxVShaderInstructionsExecuted = 72;
constexpr U MaxPShaderInstructionsExecuted = 73, MaxVertexShader30InstructionSlots = 74;
constexpr U MaxPixelShader30InstructionSlots = 75, Count = 76;
// D3DDTCAPS_ UBYTE4, UBYTE4N, SHORT2N, SHORT4N; D3DPTFILTERCAPS_ MIN and MAG point and linear.
constexpr U DeclTypesValue = 0x1 | 0x2 | 0x4 | 0x8, StretchRectValue = 0x100 | 0x200 | 0x1000000 | 0x2000000;
// D3DDEVCAPS2_STREAMOFFSET: D3D9 0x42b32c takes a DDI 9 driver without it
// for one without hardware.
constexpr U DevCaps2Value = 0x1;
// DDI 9 drivers must report D3DCAPS2_DYNAMICTEXTURES and
// D3DPRASTERCAPS_SCISSORTEST.
constexpr U Caps2Value = 0x20000000, RasterValue = 0x1000000;
// vs_2_0 and ps_2_0: twelve temporaries, one level of static flow control in
// vertex shaders, 96 pixel shader instructions.
constexpr U VertexShaderValue = 0xfffe0200u, PixelShaderValue = 0xffff0200u, Temporaries = 12, PixelInstructions = 96;
constexpr U FogInFvf = 0x2000;
} // namespace Caps9

// A format of the DirectX 8 list: DDPF_D3DFORMAT with the D3DFORMAT in
// dwFourCC and the D3DFORMAT_OP_ bits in dwOperations, the union of
// dwRBitMask.
constexpr U D3DFormatFlag = 0x200000;
namespace FormatOp
{
constexpr U Texture = 0x1, VolumeTexture = 0x2, CubeTexture = 0x4, OffscreenRenderTarget = 0x8, SameFormatRenderTarget = 0x10;
constexpr U ZStencil = 0x40, ZStencilAnyColor = 0x80, SameFormatUpToAlpha = 0x100, DisplayMode = 0x400;
constexpr U Acceleration = 0x800, OffscreenPlain = 0x4000, BumpMap = 0x10000;
} // namespace FormatOp

// DDHAL_GETDRIVERINFODATA.
namespace DriverInfoData
{
constexpr U Size = 0x00, Flags = 0x04, Guid = 0x08, ExpectedSize = 0x18, Data = 0x1c, ActualSize = 0x20;
constexpr U Result = 0x24;
} // namespace DriverInfoData

// The GUIDs GetDriverInfo is asked for, as the sixteen bytes of memory.
struct Guid
{
    unsigned char bytes[16];
};
constexpr Guid D3DCallbacks3Guid = {{0x30, 0x12, 0xf4, 0xdd, 0x0a, 0xec, 0xd0, 0x11, 0xa9, 0xb6, 0x00, 0xaa, 0x00, 0xc0, 0x99, 0x3e}};
constexpr Guid D3DExtendedCapsGuid = {{0x80, 0x1f, 0xe4, 0x7d, 0x93, 0x9d, 0xd0, 0x11, 0x89, 0xab, 0x00, 0xa0, 0xc9, 0x05, 0x41, 0x29}};
constexpr Guid Misc2CallbacksGuid = {{0x00, 0x2f, 0x6b, 0x40, 0x5a, 0x3e, 0xd1, 0x11, 0xb6, 0x40, 0x00, 0xaa, 0x00, 0xa1, 0xf9, 0x6a}};
// GUID_DDMoreSurfaceCaps: DDMORESURFACECAPS, the DDSCAPS2 the driver keeps in
// video memory (cube maps), then one pair of restrictions per heap.
constexpr Guid MoreSurfaceCapsGuid = {{0x66, 0x04, 0x8a, 0x3b, 0x69, 0xf2, 0xd1, 0x11, 0x88, 0x0b, 0x00, 0xc0, 0x4f, 0xd9, 0x30, 0xc5}};
constexpr U DDSCAPS2_VOLUME = 0x200000, DDSCAPS2_CUBEMAP = 0x200, DDSCAPS2_CUBEMAP_ALLFACES = 0xfc00, DDSCAPS2_CUBEMAP_POSITIVEX = 0x400;
constexpr Guid ZPixelFormatsGuid = {{0x80, 0x98, 0x86, 0x93, 0xcf, 0x36, 0xd1, 0x11, 0x9b, 0x1b, 0x00, 0xaa, 0x00, 0xbb, 0xb8, 0xae}};
constexpr Guid ParseUnknownCommandGuid = {{0xa0, 0xff, 0x04, 0x2e, 0xe4, 0x98, 0xd1, 0x11, 0x8c, 0xe1, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8}};   // DDRAW
// GUID_GetDriverInfo2 of the Win9x runtime: the
// DirectX 8 and 9 questions, asked once DDHALINFO_GETDRIVERINFO2 is set.
constexpr Guid GetDriverInfo2Guid = {{0x9c, 0x16, 0x28, 0xf8, 0xe8, 0xa8, 0xd2, 0x11, 0xa1, 0xf2, 0x00, 0xa0, 0xc9, 0x83, 0xea, 0xf6}};
inline bool SameGuid(const unsigned char *a, const Guid &b)
{
    for (unsigned i = 0; i < 16; ++i)
        if (a[i] != b.bytes[i]) return false;
    return true;
}

// The block compressed texture formats, MAKEFOURCC('D','X','T','n').
namespace FourCC
{
constexpr U Dxt1 = 0x31545844, Dxt2 = 0x32545844, Dxt3 = 0x33545844, Dxt4 = 0x34545844, Dxt5 = 0x35545844;
} // namespace FourCC

// The results the callbacks hand back.
constexpr U DriverNotHandled = 0, DriverHandled = 1;
constexpr U DD_OK = 0;
constexpr U DDERR_GENERIC = 0x80004005u, DDERR_UNSUPPORTED = 0x80004001u, DDERR_INVALIDPARAMS = 0x80070057u;
constexpr U DDERR_CURRENTLYNOTAVAIL = 0x88760028u;   // MAKE_DDHRESULT(40)
constexpr U DDERR_UNSUPPORTEDMODE = 0x8876024eu;     // MAKE_DDHRESULT(590)
constexpr U DDERR_OUTOFMEMORY = 0x8007000eu;
constexpr U DDERR_INVALIDPIXELFORMAT = 0x88760091u;   // MAKE_DDHRESULT(145)
constexpr U D3DERR_COMMAND_UNPARSED = 0x88760bb8u;   // MAKE_DDHRESULT(3000)

// The callback data blocks, by field offset.
namespace Data
{
// DDHAL_CREATESURFACEDATA
constexpr U CreateSurfaceDesc = 0x04, CreateSurfaceList = 0x08, CreateSurfaceCount = 0x0c, CreateSurfaceResult = 0x10;
// DDHAL_CANCREATESURFACEDATA: lpDD 0, lpDDSurfaceDesc 4, bIsDifferentPixelFormat 8, ddRVal c
constexpr U CanCreateDesc = 0x04, CanCreateDifferentFormat = 0x08, CanCreateResult = 0x0c;
// fpVidMem of a surface the driver's CreateSurface sizes and DDRAW then cuts
// out of its heap: dwBlockSizeX bytes times dwBlockSizeY lines, one line for
// a linear heap (ddrawi.h DDHAL_PLEASEALLOC_BLOCKSIZE).
constexpr U PleaseAllocBlockSize = 2;
// DDSURFACEDESC and DDSURFACEDESC2 share these offsets.
constexpr U DescPixelFormat = 0x48, DescCaps = 0x68;
// DDHAL_DESTROYSURFACEDATA, DDHAL_LOCKDATA, DDHAL_UNLOCKDATA, DDHAL_FLIPDATA
constexpr U Surface = 0x04, SurfaceResult = 0x08;
// DDHAL_LOCKDATA: lpDD 0, lpDDSurface 4, bHasRect 8, rArea c (RECTL: left,
// top, right, bottom), lpSurfData 1c, ddRVal 20, Lock 24, dwFlags 28. Only
// ddRVal is measured (DDRAW 0x18488 reads it); the rest is the block of
// ddrawi.h in that order.
constexpr U LockHasRect = 0x08, LockArea = 0x0c, LockResult = 0x20, LockFlags = 0x28;
// DDHAL_FLIPDATA: lpDD, lpSurfCurr, lpSurfTarg, dwFlags, ddRVal, Flip (DDRAW 0x2ec86 reads ddRVal at 0x10).
constexpr U FlipCurrent = 0x04, FlipTarget = 0x08, FlipFlags = 0x0c, FlipResult = 0x10;
// DDHAL_SETMODEDATA
constexpr U ModeIndex = 0x04, ModeResult = 0x08;
// DDHAL_DESTROYDRIVERDATA
constexpr U DestroyDriverResult = 0x04;
// DDHAL_WAITFORVERTICALBLANKDATA: dwFlags 4, bIsInVB 8, hEvent c, ddRVal 10
constexpr U VBlankFlags = 0x04, VBlankInVB = 0x08, VBlankResult = 0x10;
// DDWAITVB_ of dwFlags: what the caller wants the driver to wait for.
constexpr U WaitVbBlockBegin = 0x1, WaitVbBlockBeginEvent = 0x2, WaitVbBlockEnd = 0x4;
constexpr U WaitVbTest = 0x80000000u;   // DDWAITVB_I_TESTVB, only asks
// DDHAL_GETSCANLINEDATA
constexpr U ScanLine = 0x04, ScanLineResult = 0x08;
// DDHAL_FLIPTOGDISURFACEDATA, DDHAL_SETEXCLUSIVEMODEDATA: dword at 4, ddRVal at c
constexpr U ToGdi = 0x04, ToGdiResult = 0x0c, ExclusiveEnter = 0x04, ExclusiveResult = 0x0c;
// DDHAL_GETBLTSTATUSDATA and DDHAL_GETFLIPSTATUSDATA
constexpr U StatusResult = 0x0c;
// DDHAL_ADDATTACHEDSURFACEDATA: lpDDSurface 4, lpSurfAttached 8, ddRVal c
constexpr U AttachedResult = 0x0c;
// DDHAL_SETCLIPLISTDATA
constexpr U ClipListResult = 0x08;
// DDHAL_SETCOLORKEYDATA: lpDDSurface 4, dwFlags 8, ckNew c, ddRVal 14
constexpr U ColorKeyResult = 0x14;
// DDHAL_SETPALETTEDATA: lpDDSurface 4, lpDDPalette 8, ddRVal c
constexpr U SetPaletteResult = 0x0c;
// DDHAL_BLTDATA: lpDDDestSurface 4, rDest 8, lpDDSrcSurface 18, rSrc 1c, dwFlags 2c,
// dwROPFlags 30, bltFX 34 (0x64), ddRVal 98
// DDHAL_BLTDATA: lpDD, lpDDDestSurface, rDest (16), lpDDSrcSurface, rSrc (16),
// dwFlags, dwROPFlags, bltFX (0x64), ddRVal.
constexpr U BltDest = 0x04, BltDestRect = 0x08, BltSource = 0x18, BltSourceRect = 0x1c;
constexpr U BltFlags = 0x2c, BltRop = 0x30, BltFX = 0x34, BltResult = 0x98;
constexpr U BltIsClipped = 0xa0, BltOrigDest = 0xa4, BltOrigSource = 0xb4, BltRectCount = 0xc4, BltRects = 0xc8;
constexpr U BltRopFlag2 = 0x20000;   // DDBLT_ROP: bltFX.dwROP names the operation
// DDBLT_ flags.
constexpr U BltColorFill = 0x400, BltDepthFill = 0x2000000, BltDdFx = 0x800;
constexpr U BltKeySource = 0x8000, BltKeySourceOverride = 0x10000;
constexpr U BltKeyDest = 0x2000, BltKeyDestOverride = 0x4000, BltRopFlag = 0x20000;
constexpr U BltAlphaAny = 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20 | 0x40 | 0x80 | 0x100;
constexpr U BltZBufferAny = 0x1000 | 0x20000000 | 0x40000000 | 0x80000000u;
// Fields of DDBLTFX relative to BltFX.
constexpr U FxDdFx = 0x04, FxRop = 0x08, FxFillColor = 0x50, FxDestKey = 0x54, FxSourceKey = 0x5c;
// DDBLTFX_ of dwDDFX.
constexpr U FxMirrorLeftRight = 0x2, FxMirrorUpDown = 0x4, FxNoTearing = 0x8;
// The raster operations this driver carries out.
constexpr U RopBlackness = 0x00000042, RopWhiteness = 0x00ff0062;
// SRCCOPY, the one raster operation a straight copy is.
constexpr U RopSourceCopy = 0x00cc0020;
// DD_CREATESURFACEEXDATA: dwFlags 0, lpDDLcl 4, lpDDSLcl 8, ddRVal c
constexpr U CreateExLocal = 0x04, CreateExSurface = 0x08, CreateExResult = 0x0c;
// DD_GETDRIVERSTATEDATA: dwFlags 0, dwhContext 4, lpdwStates 8, dwLength c, ddRVal 10
constexpr U DriverStateResult = 0x10;
// DD_DESTROYDDLOCALDATA: dwFlags 0, pDDLcl 4, ddRVal 8
constexpr U DestroyLocal = 0x04, DestroyLocalResult = 0x08;
// D3DHAL_CONTEXTCREATEDATA: lpDDLcl 0, lpDDSLcl 4, lpDDSZLcl 8, dwPID c, dwhContext 10, ddrval 14
constexpr U ContextLocal = 0x00, ContextTarget = 0x04, ContextZ = 0x08, ContextPid = 0x0c;
constexpr U ContextHandle = 0x10, ContextResult = 0x14;
// D3DHAL_CONTEXTDESTROYDATA: dwhContext 0, ddrval 4. D3DHAL_CONTEXTDESTROYALLDATA: dwPID 0, ddrval 4.
constexpr U DestroyContext = 0x00, DestroyContextResult = 0x04;
// D3DHAL_SCENECAPTUREDATA: dwhContext 0, dwFlag 4, ddrval 8
constexpr U SceneFlag = 0x04, SceneResult = 0x08;
// D3DHAL_CLEAR2DATA: dwhContext 0, dwFlags 4, dwFillColor 8, dvFillDepth c, dwFillStencil 10,
// lpRects 14, dwNumRects 18, ddrval 1c
constexpr U ClearFlags = 0x04, ClearColor = 0x08, ClearDepth = 0x0c, ClearStencil = 0x10;
constexpr U ClearRects = 0x14, ClearRectCount = 0x18, ClearResult = 0x1c;
// D3DHAL_VALIDATETEXTURESTAGESTATEDATA: dwhContext 0, dwFlags 4, dwReserved 8, dwNumPasses c, ddrval 10
constexpr U ValidatePasses = 0x0c, ValidateResult = 0x10;
// D3DHAL_DRAWPRIMITIVES2DATA
constexpr U DP2Context = 0x00, DP2Flags = 0x04, DP2VertexType = 0x08, DP2Commands = 0x0c;
constexpr U DP2CommandOffset = 0x10, DP2CommandLength = 0x14, DP2Vertices = 0x18;
constexpr U DP2VertexOffset = 0x1c, DP2VertexLength = 0x20, DP2ReqVertexBufSize = 0x24;
constexpr U DP2ReqCommandBufSize = 0x28, DP2RenderStates = 0x2c, DP2Result = 0x30, DP2ErrorOffset = 0x34;
constexpr U DP2UserMemVertices = 0x1;   // D3DHALDP2_USERMEMVERTICES
} // namespace Data

// DDRAWI_DDRAWSURFACE_LCL, _GBL and _MORE, the surface records of DDRAW.
namespace SurfaceLocal
{
constexpr U More = 0x00, Global = 0x04, AttachList = 0x0c, AttachListFrom = 0x10, Flags = 0x1c;
constexpr U Caps = 0x20, BackBufferCount = 0x30, SourceColorKey = 0x3c;   // DDCOLORKEY: low, high
constexpr U DestColorKey = 0x34;   // ddckCKDestBlt, the key before ddckCKSrcBlt in ddrawi.h
constexpr U HasPixelFormat = 0x2000;   // DDRAWISURF_HASPIXELFORMAT
constexpr U HasSourceColorKey = 0x800;  // DDRAWISURF_HASCKEYSRCBLT
} // namespace SurfaceLocal
// The GBL of this DDRAW starts with dwRefCnt (read off a run: 3, then the
// global flags, the rect list, the heap, lpDD, fpVidMem, lPitch, the size).
namespace SurfaceGlobal
{
constexpr U RefCount = 0x00, GlobalFlags = 0x04, BlockSizeY = 0x08, Heap = 0x0c, BlockSizeX = 0x0c;
constexpr U DirectDraw = 0x10, VidMem = 0x14, Pitch = 0x18;
constexpr U Height = 0x1c;   // WORD wHeight, WORD wWidth
constexpr U Width = 0x1e, PixelFormat = 0x28;
} // namespace SurfaceGlobal
namespace SurfaceMore
{
// CapsEx is dwCaps2 of the DDSCAPSEX; dwCaps4 holds the depth of a volume
// in its low word.
constexpr U Size = 0x00, MipMapCount = 0x18, CapsEx = 0x34, Caps3 = 0x38, Caps4 = 0x3c, Handle = 0x64;
constexpr U LightweightMipMap = 0x400; // DDSCAPS3_LIGHTWEIGHTMIPMAP: implicit full mip chain
} // namespace SurfaceMore
// The bytes of the three records a driver reads: the LCL up to its colour
// keys, the GBL up to the end of its pixel format, the MORE (its dwSize).
// DDRAW hands the DirectX 8 runtime's system memory surfaces to
// CreateSurfaceEx in one heap block of 0x14c bytes that it frees on return.
constexpr U LocalBytes = 0x4c, GlobalBytes = 0x48, MoreBytes = 0x88;
// ATTACHLIST: dwFlags 0, lpLink 4, lpAttached 8, lpIAttached c (read off a run:
// the back buffer's list names its Z buffer and the primary).
namespace Attach
{
constexpr U Link = 0x04, Surface = 0x08;
} // namespace Attach

// D3DHAL_DP2COMMAND: bCommand, bReserved, wPrimitiveCount or wStateCount.
namespace DP2
{
constexpr U Points = 1, IndexedLineList = 2, IndexedTriangleList = 3, RenderState = 8;
constexpr U LineList = 15, LineStrip = 16, IndexedLineStrip = 17, TriangleList = 18, TriangleStrip = 19;
constexpr U IndexedTriangleStrip = 20, TriangleFan = 21, IndexedTriangleFan = 22, TriangleFanImm = 23;
constexpr U LineListImm = 24, TextureStageState = 25, IndexedTriangleList2 = 26, IndexedLineList2 = 27;
constexpr U ViewportInfo = 28, WInfo = 29, SetPalette = 30, UpdatePalette = 31, ZRange = 32;
constexpr U SetMaterial = 33, SetLight = 34, CreateLight = 35, SetTransform = 36, Ext = 37;
constexpr U TexBlt = 38, StateSet = 39, SetPriority = 40, SetRenderTarget = 41, Clear = 42;
constexpr U SetTexLod = 43, SetClipPlane = 44;
// The commands of the DirectX 8 driver model.
constexpr U CreateVertexShader = 45, DeleteVertexShader = 46, SetVertexShader = 47, SetVertexShaderConst = 48;
constexpr U SetStreamSource = 49, SetStreamSourceUM = 50, SetIndices = 51, DrawPrimitive = 52, DrawIndexedPrimitive = 53;
constexpr U CreatePixelShader = 54, DeletePixelShader = 55, SetPixelShader = 56, SetPixelShaderConst = 57;
constexpr U ClippedTriangleFan = 58, DrawPrimitive2 = 59, DrawIndexedPrimitive2 = 60, VolumeBlt = 63, BufferBlt = 64;
constexpr U MultiplyTransform = 65, AddDirtyRect = 66, AddDirtyBox = 67;
// The commands of the DirectX 9 driver model.
constexpr U CreateVertexShaderDecl = 71, DeleteVertexShaderDecl = 72, SetVertexShaderDecl = 73;
constexpr U CreateVertexShaderFunc = 74, DeleteVertexShaderFunc = 75, SetVertexShaderFunc = 76, SetVertexShaderConstI = 77;
constexpr U SetScissorRect = 79, SetStreamSource2 = 80, Blt9 = 81, ColorFill = 82, SetVertexShaderConstB = 83;
// DirectX 9 command and query identifiers.
constexpr U CreateQuery = 84, SetRenderTarget2 = 85, SetDepthStencil = 86, ResponseContinue = 87, ResponseQuery = 88;
constexpr U GenerateMipSubLevels = 89, DeleteQuery = 90, IssueQuery = 91, SetConvolutionKernelMono = 92;
constexpr U SetPixelShaderConstI = 93, SetPixelShaderConstB = 94, SetStreamSourceFreq = 95, SurfaceBlt = 96;
// D3DQUERYTYPE_EVENT, D3DISSUE_END.
constexpr U QueryEvent = 8, IssueEnd = 1;
constexpr U Streams = 16;
} // namespace DP2

// D3DTSS_ numbers and the D3DTA_, D3DTOP_ values the backend interprets.
namespace TSS
{
constexpr U TextureMap = 0, ColorOp = 1, ColorArg1 = 2, ColorArg2 = 3, AlphaOp = 4, AlphaArg1 = 5, AlphaArg2 = 6;
constexpr U TexCoordIndex = 11, Address = 12, AddressU = 13, AddressV = 14, BorderColor = 15;
constexpr U MagFilter = 16, MinFilter = 17, MipFilter = 18, MipMapLodBias = 19, MaxMipLevel = 20, MaxAnisotropy = 21;
constexpr U TextureTransformFlags = 24, AddressW = 25;
} // namespace TSS
namespace RS
{
constexpr U TextureHandle = 1, TextureMapBlend = 21;
constexpr U ZEnable = 7, FillMode = 8, ShadeMode = 9, ZWriteEnable = 14, AlphaTestEnable = 15;
constexpr U SrcBlend = 19, DestBlend = 20, CullMode = 22, ZFunc = 23, AlphaRef = 24, AlphaFunc = 25;
constexpr U DitherEnable = 26, AlphaBlendEnable = 27, FogEnable = 28, SpecularEnable = 29;
constexpr U FogColor = 34, FogTableMode = 35, FogStart = 36, FogEnd = 37, FogDensity = 38, ColorKeyEnable = 41;
constexpr U ZBias = 47, StencilEnable = 52, TextureFactor = 60;
constexpr U RangeFogEnable = 48, Clipping = 136, Lighting = 137, Ambient = 139, FogVertexMode = 140;
constexpr U ColorVertex = 141, LocalViewer = 142, NormalizeNormals = 143, ColorKeyBlendEnable = 144;
constexpr U DiffuseMaterialSource = 145, SpecularMaterialSource = 146, AmbientMaterialSource = 147;
constexpr U EmissiveMaterialSource = 148, VertexBlend = 151, ClipPlaneEnable = 152;
} // namespace RS
} // namespace Win98DDHal
