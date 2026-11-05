/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Direct3D 7 in the DDRAW.DLL of ARM64 processes: the objects behind
   IDirect3D7, IDirect3DDevice7 and IDirect3DVertexBuffer7.


   A device keeps the whole state of Direct3D here, in the program: render
   states, texture stage states, transforms, material, lights, viewport, clip
   planes and state blocks, so every Get answers without leaving the
   processor. What it is asked to draw it collects: a change of state and a
   draw each append a command to the batch of the device, and the vertices of
   a draw are copied into its pool. The batch crosses into the driver in one
   call (D3dDrvRun of NEO98DX.DLL) when the scene ends, when the batch is
   full, or when somebody is about to look at a surface (Lock, Blt, Flip,
   GetDC of DirectDraw call D3dFlushAll). The driver runs the commands on the
   rasterizer, which transforms, lights, clips and draws. */
#ifndef NEO98_D3D_PRIVATE_H
#define NEO98_D3D_PRIVATE_H

#include "ddraw_private.h"
#include <d3d.h>
#include <math.h>

enum
{
    D3D_RENDERSTATES = 256,
    D3D_STAGES = 8,
    D3D_STAGESTATES = 32,
    /* The matrices of a device: world 0 to 3, view, projection, texture 0 to 7. */
    D3D_MATRIX_WORLD = 0,
    D3D_MATRIX_VIEW = 4,
    D3D_MATRIX_PROJECTION = 5,
    D3D_MATRIX_TEXTURE = 6,
    D3D_MATRICES = 14,
    D3D_CLIPPLANES = 6
};

typedef struct D3dLight
{
    DWORD index;                    /* the number the program gave it */
    D3DLIGHT7 data;
    BOOL enabled;
    int slot;                       /* its place in the driver's context, or -1 */
} D3dLight;

/* What a state block holds of a light. */
typedef struct D3dBlockLight
{
    DWORD index;
    BOOL has_data, has_enable, enabled;
    D3DLIGHT7 data;
} D3dBlockLight;

/* The state of a device, and with marks the part of it a state block holds. */
typedef struct D3dState
{
    DWORD render[D3D_RENDERSTATES];
    DWORD stage[D3D_STAGES][D3D_STAGESTATES];
    D3DMATRIX matrix[D3D_MATRICES];
    DdSurface *texture[D3D_STAGES];     /* each with a reference */
    D3DMATERIAL7 material;
    D3DVIEWPORT7 viewport;
    D3DVALUE clip[D3D_CLIPPLANES][4];
} D3dState;

typedef struct D3dBlock
{
    BOOL used;
    D3dState state;
    BYTE render_mark[D3D_RENDERSTATES];
    BYTE stage_mark[D3D_STAGES][D3D_STAGESTATES];
    BYTE matrix_mark[D3D_MATRICES];
    BYTE texture_mark[D3D_STAGES];
    BYTE clip_mark[D3D_CLIPPLANES];
    BOOL material_mark, viewport_mark;
    D3dBlockLight *lights;
    DWORD light_count, light_capacity;
} D3dBlock;

/* IDirect3D7: an interface of a DirectDraw object. */
typedef struct D3dObject
{
    DdFace face;
    LONG references;
    DdObject *dd;
} D3dObject;

typedef struct D3dDevice
{
    DdFace face;
    LONG references;
    DdObject *dd;                   /* with a reference */
    GUID guid;                      /* the device class it was created as */
    DdSurface *target;              /* with a reference, except an aggregated Device1 */
    BOOL borrowed_target;
    DdSurface *depth;               /* the Z buffer attached to the target, with a reference, or null */
    DWORD context;                  /* of the driver */
    BOOL in_scene;
    D3dState state;
    D3dLight *lights;
    DWORD light_count, light_capacity;
    DWORD slots;                    /* bit n: slot n of the driver holds a light */
    D3dBlock **blocks;              /* a handle is the place in this list plus one */
    DWORD block_count;
    D3dBlock *recording;            /* between BeginStateBlock and EndStateBlock */
    D3DCLIPSTATUS clip_status;
    /* The batch: commands of NEO98DX (NEO98D3D_OP_), and the pool the
       vertices and indices of its draws lie in until the batch has run. */
    BYTE *commands;
    DWORD command_bytes, command_capacity;
    DWORD last_command;             /* offset of the last command, for appending to it */
    BYTE *pool;
    DWORD pool_bytes, pool_capacity;
    HRESULT flush_result;           /* the first failure of a batch since the scene began */
    struct D3dDevice *next;
} D3dDevice;

typedef struct D3dVertexBuffer
{
    DdFace face;
    LONG references;
    DdObject *dd;
    DWORD caps, fvf, count, stride;
    BYTE *memory;
    LONG locks;
} D3dVertexBuffer;

/* d3d_main.c */
BOOL D3dCaps(NEO98D3D_CAPS *caps);
HRESULT D3dObjectFor(void *dd, REFIID iid, void **out);
void D3dDescribe(const NEO98D3D_CAPS *caps, const GUID *device, D3DDEVICEDESC7 *desc, BOOL enumerating);
D3dVertexBuffer *D3dVertexBufferOf(void *face);
extern const GUID d3d_iid_direct3d7, d3d_iid_device7, d3d_iid_vertexbuffer7, d3d_iid_hal, d3d_iid_tnlhal;

/* d3d_device.c */
HRESULT D3dCreateDevice(DdObject *dd, const GUID *guid, DdSurface *target, D3dDevice **out);
D3dDevice *D3dDeviceOf(void *face);

/* d3d_vertex.c: flexible vertex formats and the vertex pipeline in software,
   for ProcessVertices and ComputeSphereVisibility. */
typedef struct D3dLayout
{
    DWORD stride;
    BOOL transformed;
    DWORD weights;
    int normal, reserved, diffuse, specular;    /* byte offsets or -1 */
    DWORD texture_sets;
    int texture[8];
    DWORD texture_floats[8];
} D3dLayout;
BOOL D3dLayoutOf(DWORD fvf, D3dLayout *layout);
void D3dMultiply(D3DMATRIX *out, const D3DMATRIX *a, const D3DMATRIX *b);
HRESULT D3dProcessVertices(D3dDevice *device, DWORD operation, BYTE *dest, DWORD dest_fvf, const D3DDRAWPRIMITIVESTRIDEDDATA *source,
                           DWORD source_fvf, DWORD count, DWORD flags);
HRESULT D3dSphereVisibility(D3dDevice *device, const D3DVECTOR *centers, const D3DVALUE *radii, DWORD count, DWORD *results);
/* The strided description of packed vertices of a format. */
void D3dStridedOf(const BYTE *vertices, DWORD fvf, const D3dLayout *layout, D3DDRAWPRIMITIVESTRIDEDDATA *out);
/* Packs strided vertices into the layout of their format. */
void D3dPack(BYTE *out, const D3DDRAWPRIMITIVESTRIDEDDATA *source, DWORD first, const D3dLayout *layout, DWORD count);

#endif
