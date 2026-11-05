/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DDRAW.DLL for ARM64 processes: the objects behind the interfaces of
   DirectDraw 7.

   One implementation carries every version of an interface. An interface
   pointer is a DdFace: the method table of its version, the object and the
   version. A method finds its object through the face it was called on, and
   where versions differ (the surface description, the caps) it asks the
   version or the dwSize of the structure.

   A surface keeps its pixels in a DdStore. A store in system memory is memory
   of the process the DLL allocates, and a blit between two such stores runs
   here (ddblit.h). A store of the driver names a surface of NEO98DX.DLL: the
   primary surface, the back buffers of a flipping chain, video memory, and
   with a rasterizer Z buffers and textures. */
#ifndef NEO98_DDRAW_PRIVATE_H
#define NEO98_DDRAW_PRIVATE_H

#include <windows.h>
#include <ddraw.h>
#include <stdlib.h>
#include <string.h>
#include "neo98dx_abi.h"
#include "ddblit.h"

typedef struct DdFace
{
    const void *table;
    void *object;
    unsigned version;
} DdFace;

struct DdObject;
struct DdSurface;
struct DdClipper;
struct DdPalette;

/* Versions of IDirectDraw and of IDirectDrawSurface, as indices of the faces. */
enum { DD_V1, DD_V2, DD_V4, DD_V7, DD_VERSIONS };
enum { DDS_V1, DDS_V2, DDS_V3, DDS_V4, DDS_V7, DDS_VERSIONS };

/* Another module adds interfaces to the objects of this one: Direct3D asks
   IDirectDraw7 for IDirect3D7 and a surface for IDirect3DTexture2. The
   factory gets the object (a DdObject or a DdSurface) and answers the
   interface with a reference, or E_NOINTERFACE. */
typedef HRESULT (*DdExtensionFactory)(void *object, REFIID iid, void **out);
typedef struct DdExtension
{
    const GUID *iid;
    DdExtensionFactory factory;
} DdExtension;
#define DD_EXTENSION_CAPACITY 16
extern DdExtension dd_extensions[DD_EXTENSION_CAPACITY], dd_surface_extensions[DD_EXTENSION_CAPACITY];
HRESULT DdQueryExtension(const DdExtension *table, void *object, REFIID iid, void **out);

typedef struct DdObject
{
    DdFace face[DD_VERSIONS];
    /* DirectDraw counts the references of each of its interfaces by itself:
       AddRef and Release answer the count of the interface they were called
       on, and the object lives while any of them holds one (measured against
       the original DDRAW.DLL). `references` is the
       sum, and what surfaces and this DLL hold. */
    LONG face_references[DD_VERSIONS];
    LONG references;
    HWND window;
    DWORD level;                    /* DDSCL_ of SetCooperativeLevel, 0 before it */
    BOOL exclusive;                 /* holds the exclusive mode now */
    BOOL inactive;                  /* gave it up when its window lost the foreground */
    BOOL mode_set;                  /* set a display mode of its own */
    DWORD mode_width, mode_height, mode_bits;   /* the mode it works in */
    WNDPROC previous_procedure;     /* of the window it hooked for exclusive mode */
    struct DdSurface *surfaces;     /* every surface it made */
    struct DdSurface *primary;
    unsigned created_version;
    void *legacy;                  /* weak reference to the legacy Direct3D root */
    struct DdObject *next;
} DdObject;

/* The pixels of a surface; a duplicate surface shares them. */
typedef struct DdStore
{
    LONG references;
    DWORD driver;                   /* handle of the driver surface, or 0 */
    BYTE *memory;                   /* system memory */
    BOOL own_memory;                /* allocated here, not handed in with DDSD_LPSURFACE */
    LONG pitch;
    DWORD mode_width, mode_height, mode_bits;   /* the display mode a driver surface was made in */
} DdStore;

typedef struct DdAttachment
{
    struct DdSurface *surface;
    BOOL implicit;                  /* made with its owner, goes with it */
    struct DdAttachment *next;
} DdAttachment;

typedef struct DdPrivate
{
    GUID tag;
    DWORD size, flags;
    void *data;
    struct DdPrivate *next;
} DdPrivate;

typedef struct DdSurface
{
    DdFace face[DDS_VERSIONS];
    DdFace texture[2];              /* IDirect3DTexture and IDirect3DTexture2 */
    DdFace gamma;                   /* IDirectDrawGammaControl */
    void *legacy_device;           /* Device1 aggregated into this surface */
    LONG references;
    DdObject *dd;
    BOOL holds_dd;                  /* made through IDirectDraw4 or 7: keeps the object alive */
    unsigned made_through;          /* the version of IDirectDraw it was made through, DD_V1 to DD_V7 */
    struct DdSurface *next;         /* in dd->surfaces */
    DWORD caps, caps2, caps3, caps4;
    DWORD width, height;
    NEO98DX_FORMAT format;
    BOOL own_format;                /* the description named a pixel format */
    DWORD mip_count, back_buffers;
    DdStore *store;
    struct DdSurface *owner;        /* the surface this one was implicitly made with */
    DdAttachment *attached;
    struct DdClipper *clipper;
    struct DdPalette *palette;
    DWORD key_flags;                /* DDSD_CK... of the keys that are set */
    DDCOLORKEY key_dest_overlay, key_dest_blt, key_src_overlay, key_src_blt;
    LONG locks;
    HDC dc;
    DdPrivate *private_data;
    DWORD uniqueness, priority, lod;
    BOOL dead;                      /* its DirectDraw object is gone */
} DdSurface;

typedef struct DdClipper
{
    DdFace face;
    LONG references;
    HWND window;
    RGNDATA *list;                  /* SetClipList, or null */
    DWORD list_bytes;
} DdClipper;

typedef struct DdPalette
{
    DdFace face;
    LONG references;
    DWORD caps;
    DWORD count;                    /* 2, 4, 16 or 256 entries */
    PALETTEENTRY entries[256];
    struct DdSurface *surfaces[8];  /* the surfaces it is attached to */
} DdPalette;

static inline LONG DdAddRef(LONG *count) { return __atomic_add_fetch(count, 1, __ATOMIC_SEQ_CST); }
static inline LONG DdDropRef(LONG *count) { return __atomic_sub_fetch(count, 1, __ATOMIC_SEQ_CST); }
static inline void *DdAllocate(size_t bytes) { return calloc(1, bytes); }

/* ddraw_main.c */
extern const GUID dd_iid_unknown;
BOOL DdDriverReady(void);
BOOL DdDisplayInfo(NEO98DX_INFO *info);
void DdFormatToPixelFormat(const NEO98DX_FORMAT *format, DDPIXELFORMAT *out);
BOOL DdPixelFormatToFormat(const DDPIXELFORMAT *in, NEO98DX_FORMAT *format);
void DdLock(void);
void DdUnlock(void);
/* All COM methods share DirectDraw's recursive critical section. The SDK
   uses clang for C; cleanup releases it on every return, including errors. */
static inline void DdScopeUnlock(int *held) { (void)held; DdUnlock(); }
#define DD_METHOD_LOCK() \
    DdLock(); int dd_method_lock __attribute__((cleanup(DdScopeUnlock))) = 1

/* Direct3D (d3d_*.c), built into this DLL. A device collects what it draws
   and sends it to the driver in batches; before anybody reads or writes the
   pixels of a driver surface, or a surface goes, the batches run. */
extern LONG d3d_pending;            /* devices with commands that have not run */
void D3dRegister(void);
void D3dFlushAll(void);
static inline void DdFlush3D(void)
{
    if (d3d_pending) D3dFlushAll();
}

/* ddraw_object.c */
HRESULT DdCreateObject(DdObject **out);
void *DdObjectFace(DdObject *dd, unsigned version);
void *DdObjectTake(DdObject *dd, unsigned version);
void DdObjectAddRef(DdObject *dd);
void DdObjectRelease(DdObject *dd);
void DdObjectForgetSurface(DdObject *dd, DdSurface *surface);

/* ddraw_surface.c */
HRESULT DdCreateSurfaceTree(DdObject *dd, const DDSURFACEDESC2 *desc, unsigned version, unsigned made_through, DdSurface **out);
void *DdSurfaceFace(DdSurface *surface, unsigned version);
DdSurface *DdSurfaceOf(void *face);
void DdSurfaceAddRef(DdSurface *surface);
void DdSurfaceRelease(DdSurface *surface);
void D3dLegacySurfaceDestroy(DdSurface *surface);
void DdSurfaceOrphan(DdSurface *surface);
void DdSurfaceDescribe(DdSurface *surface, void *desc_out, DWORD size);
HRESULT DdSurfaceRestore(DdSurface *surface);
HRESULT DdSurfaceIsLost(DdSurface *surface);
HRESULT DdSurfaceDuplicate(DdSurface *original, unsigned version, unsigned made_through, DdSurface **out);
void DdSurfacePaletteChanged(DdSurface *surface, DWORD start, DWORD count);

/* ddraw_clipper.c */
HRESULT DdCreateClipper(DdClipper **out);
DdClipper *DdClipperOf(void *face);
void DdClipperAddRef(DdClipper *clipper);
void DdClipperRelease(DdClipper *clipper);

/* ddraw_palette.c */
HRESULT DdCreatePalette(DWORD flags, const PALETTEENTRY *colors, DdPalette **out);
DdPalette *DdPaletteOf(void *face);
void DdPaletteAddRef(DdPalette *palette);
void DdPaletteRelease(DdPalette *palette);
void DdPaletteAttach(DdPalette *palette, struct DdSurface *surface, BOOL attach);

#endif
