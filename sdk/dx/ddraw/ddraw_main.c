/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DDRAW.DLL for ARM64 processes: the exports a program imports, and what the
   objects share. */
#include "ddraw_private.h"

const GUID dd_iid_unknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

DdExtension dd_extensions[DD_EXTENSION_CAPACITY], dd_surface_extensions[DD_EXTENSION_CAPACITY];

static CRITICAL_SECTION dd_section;
static BOOL dd_section_ready;

void DdLock(void)
{
    if (dd_section_ready) EnterCriticalSection(&dd_section);
}

void DdUnlock(void)
{
    if (dd_section_ready) LeaveCriticalSection(&dd_section);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        InitializeCriticalSection(&dd_section);
        dd_section_ready = TRUE;
        D3dRegister();
    }
    return TRUE;
}

/* The driver below: NEO98DX.DLL of the same build. */
BOOL DdDriverReady(void)
{
    return DxDrvVersion() == NEO98DX_VERSION;
}

BOOL DdDisplayInfo(NEO98DX_INFO *info)
{
    memset(info, 0, sizeof *info);
    return DdDrvGetInfo(info, sizeof *info) == DD_OK;
}

void DdFormatToPixelFormat(const NEO98DX_FORMAT *format, DDPIXELFORMAT *out)
{
    memset(out, 0, sizeof *out);
    out->dwSize = sizeof *out;
    out->dwFlags = format->flags;
    out->dwFourCC = format->fourcc;
    out->dwRGBBitCount = format->bits;
    out->dwRBitMask = format->r;
    out->dwGBitMask = format->g;
    out->dwBBitMask = format->b;
    out->dwRGBAlphaBitMask = (format->flags & DDPF_ALPHAPIXELS) ? format->a : 0;
}

/* The formats a surface can have here: palette indices of eight bits, RGB of
   8 to 32 bits with any masks, Z buffers of 16 and 32 bits, and DXT1 to DXT5
   when the rasterizer of the host samples them. */
BOOL DdPixelFormatToFormat(const DDPIXELFORMAT *in, NEO98DX_FORMAT *format)
{
    memset(format, 0, sizeof *format);
    if (in->dwSize != sizeof *in) return FALSE;
    if (in->dwFlags & DDPF_FOURCC)
    {
        NEO98DX_INFO info;
        const DWORD code = in->dwFourCC;
        if (code != MAKEFOURCC('D', 'X', 'T', '1') && code != MAKEFOURCC('D', 'X', 'T', '2') && code != MAKEFOURCC('D', 'X', 'T', '3') &&
            code != MAKEFOURCC('D', 'X', 'T', '4') && code != MAKEFOURCC('D', 'X', 'T', '5'))
            return FALSE;
        if (!DdDisplayInfo(&info) || !(info.caps & NEO98DX_CAP_DXT)) return FALSE;
        format->flags = DDPF_FOURCC;
        format->fourcc = code;
        return TRUE;
    }
    if (in->dwFlags & (DDPF_YUV | DDPF_PALETTEINDEXED1 | DDPF_PALETTEINDEXED2 | DDPF_PALETTEINDEXED4 |
                       DDPF_PALETTEINDEXEDTO8 | DDPF_COMPRESSED))
        return FALSE;
    format->flags = in->dwFlags;
    format->bits = in->dwRGBBitCount;
    if (in->dwFlags & DDPF_ZBUFFER)
    {
        if (format->bits != 16 && format->bits != 32) return FALSE;
        format->r = in->dwStencilBitDepth;
        format->g = in->dwZBitMask;
        format->b = in->dwStencilBitMask;
        return TRUE;
    }
    if (format->bits != 8 && format->bits != 16 && format->bits != 24 && format->bits != 32) return FALSE;
    if (in->dwFlags & DDPF_PALETTEINDEXED8) return format->bits == 8;
    if (!(in->dwFlags & (DDPF_RGB | DDPF_LUMINANCE | DDPF_ALPHA | DDPF_BUMPDUDV))) return FALSE;
    format->r = in->dwRBitMask;
    format->g = in->dwGBitMask;
    format->b = in->dwBBitMask;
    format->a = (in->dwFlags & DDPF_ALPHAPIXELS) ? in->dwRGBAlphaBitMask : 0;
    return TRUE;
}

HRESULT DdQueryExtension(const DdExtension *table, void *object, REFIID iid, void **out)
{
    unsigned i;
    for (i = 0; i < DD_EXTENSION_CAPACITY; ++i)
        if (table[i].iid && IsEqualGUID(table[i].iid, iid)) return table[i].factory(object, iid, out);
    return E_NOINTERFACE;
}

/* The connection of Direct3D: a factory for an interface the objects of
   DirectDraw hand out through QueryInterface. `surface` picks the table of
   the surfaces. Exported for the Direct3D module of the SDK, not for
   programs. */
HRESULT WINAPI DdRegisterExtension(const GUID *iid, DdExtensionFactory factory, BOOL surface)
{
    DdExtension *table = surface ? dd_surface_extensions : dd_extensions;
    unsigned i;
    if (!iid || !factory) return DDERR_INVALIDPARAMS;
    for (i = 0; i < DD_EXTENSION_CAPACITY; ++i)
        if (!table[i].iid || IsEqualGUID(table[i].iid, iid))
        {
            table[i].iid = iid;
            table[i].factory = factory;
            return DD_OK;
        }
    return DDERR_OUTOFMEMORY;
}

/* The device a program may name: none (the display), the two creation hints,
   or the GUID of the display, which is all zero here. */
static BOOL KnownDevice(const GUID *guid)
{
    static const GUID none;
    if (!guid || guid == (const GUID *)DDCREATE_HARDWAREONLY || guid == (const GUID *)DDCREATE_EMULATIONONLY) return TRUE;
    return IsEqualGUID(guid, &none);
}

static HRESULT Create(GUID *guid, void **out, unsigned version, IUnknown *outer)
{
    DdObject *dd = NULL;
    HRESULT result;
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    if (!KnownDevice(guid)) return DDERR_INVALIDDIRECTDRAWGUID;
    if (!DdDriverReady()) return DDERR_NODIRECTDRAWSUPPORT;
    result = DdCreateObject(&dd);
    if (FAILED(result)) return result;
    /* The reference of the new object is the one of the interface asked for. */
    dd->face_references[version] = 1;
    dd->created_version = version;
    *out = DdObjectFace(dd, version);
    return DD_OK;
}

HRESULT WINAPI DirectDrawCreate(GUID *guid, LPDIRECTDRAW *out, IUnknown *outer)
{
    return Create(guid, (void **)out, DD_V1, outer);
}

HRESULT WINAPI DirectDrawCreateEx(GUID *guid, LPVOID *out, REFIID iid, IUnknown *outer)
{
    if (!iid || !IsEqualGUID(iid, &IID_IDirectDraw7)) return DDERR_INVALIDPARAMS;
    return Create(guid, out, DD_V7, outer);
}

HRESULT WINAPI DirectDrawCreateClipper(DWORD flags, LPDIRECTDRAWCLIPPER *out, IUnknown *outer)
{
    DdClipper *clipper = NULL;
    HRESULT result;
    if (!out || flags) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    result = DdCreateClipper(&clipper);
    if (FAILED(result)) return result;
    *out = (LPDIRECTDRAWCLIPPER)&clipper->face;
    return DD_OK;
}

/* One device: the display. */
static char driver_description[] = "Primary Display Driver", driver_name[] = "display";

HRESULT WINAPI DirectDrawEnumerateA(LPDDENUMCALLBACKA callback, LPVOID context)
{
    if (!callback) return DDERR_INVALIDPARAMS;
    callback(NULL, driver_description, driver_name, context);
    return DD_OK;
}

HRESULT WINAPI DirectDrawEnumerateExA(LPDDENUMCALLBACKEXA callback, LPVOID context, DWORD flags)
{
    if (!callback || (flags & ~(DDENUM_ATTACHEDSECONDARYDEVICES | DDENUM_DETACHEDSECONDARYDEVICES | DDENUM_NONDISPLAYDEVICES)))
        return DDERR_INVALIDPARAMS;
    callback(NULL, driver_description, driver_name, context, NULL);
    return DD_OK;
}
