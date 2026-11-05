/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DDRAW.DLL for ARM64 processes: IDirectDrawClipper. A clipper names a
   window, whose visible region then clips every blit to the surface it is
   attached to, or carries a list of rectangles the program set.
    */
#include "ddraw_private.h"

static DdClipper *This(void *face) { return (DdClipper *)((DdFace *)face)->object; }

DdClipper *DdClipperOf(void *face) { return face ? This(face) : NULL; }
void DdClipperAddRef(DdClipper *clipper) { DdAddRef(&clipper->references); }

void DdClipperRelease(DdClipper *clipper)
{
    if (DdDropRef(&clipper->references) > 0) return;
    free(clipper->list);
    free(clipper);
}

static HRESULT WINAPI Clipper_QueryInterface(void *face, REFIID iid, void **out)
{
    DD_METHOD_LOCK();
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (!iid || (!IsEqualGUID(iid, &dd_iid_unknown) && !IsEqualGUID(iid, &IID_IDirectDrawClipper))) return E_NOINTERFACE;
    DdClipperAddRef(This(face));
    *out = face;
    return S_OK;
}

static ULONG WINAPI Clipper_AddRef(void *face) {
    DD_METHOD_LOCK(); return (ULONG)DdAddRef(&This(face)->references); }

static ULONG WINAPI Clipper_Release(void *face)
{
    DD_METHOD_LOCK();
    DdClipper *clipper = This(face);
    const LONG left = clipper->references - 1;
    DdClipperRelease(clipper);
    return left > 0 ? (ULONG)left : 0;
}

/* The clip list as an RGNDATA: the list that was set, or the visible region
   of the window, cut to `bounds` when given. Without a buffer the answer is
   the size. */
static HRESULT WINAPI Clipper_GetClipList(void *face, LPRECT bounds, LPRGNDATA out, LPDWORD size)
{
    DD_METHOD_LOCK();
    DdClipper *clipper = This(face);
    RGNDATA *all = NULL;
    DWORD bytes = 0, kept = 0, i;
    const RECT *rects;
    HRESULT result = DD_OK;
    if (!size) return DDERR_INVALIDPARAMS;
    if (clipper->window)
    {
        result = DdDrvGetClipList(clipper->window, NULL, &bytes);
        if (result != DD_OK) return result;
        all = (RGNDATA *)malloc(bytes ? bytes : sizeof(RGNDATA));
        if (!all) return DDERR_OUTOFMEMORY;
        result = DdDrvGetClipList(clipper->window, all, &bytes);
        if (result != DD_OK)
        {
            free(all);
            return result;
        }
    }
    else if (clipper->list)
    {
        all = (RGNDATA *)malloc(clipper->list_bytes);
        if (!all) return DDERR_OUTOFMEMORY;
        memcpy(all, clipper->list, clipper->list_bytes);
        bytes = clipper->list_bytes;
    }
    else return DDERR_NOCLIPLIST;
    /* Cut to the rectangle the caller asks about. */
    rects = (const RECT *)all->Buffer;
    if (bounds)
    {
        RECT *write = (RECT *)all->Buffer, box = {0x7fffffff, 0x7fffffff, -0x7fffffff, -0x7fffffff};
        for (i = 0; i < all->rdh.nCount; ++i)
        {
            RECT r = rects[i];
            if (r.left < bounds->left) r.left = bounds->left;
            if (r.top < bounds->top) r.top = bounds->top;
            if (r.right > bounds->right) r.right = bounds->right;
            if (r.bottom > bounds->bottom) r.bottom = bounds->bottom;
            if (r.right <= r.left || r.bottom <= r.top) continue;
            write[kept++] = r;
            if (r.left < box.left) box.left = r.left;
            if (r.top < box.top) box.top = r.top;
            if (r.right > box.right) box.right = r.right;
            if (r.bottom > box.bottom) box.bottom = r.bottom;
        }
        if (!kept) box.left = box.top = box.right = box.bottom = 0;
        all->rdh.nCount = kept;
        all->rdh.nRgnSize = kept * sizeof(RECT);
        all->rdh.rcBound = box;
        bytes = sizeof(RGNDATAHEADER) + kept * sizeof(RECT);
    }
    if (out)
    {
        if (*size < bytes) result = DDERR_REGIONTOOSMALL;
        else memcpy(out, all, bytes);
    }
    *size = bytes;
    free(all);
    return result;
}

static HRESULT WINAPI Clipper_GetHWnd(void *face, HWND *out)
{
    DD_METHOD_LOCK();
    if (!out) return DDERR_INVALIDPARAMS;
    *out = This(face)->window;
    return DD_OK;
}

static HRESULT WINAPI Clipper_Initialize(void *face, LPDIRECTDRAW dd, DWORD flags)
{
    DD_METHOD_LOCK();
    (void)face;
    (void)dd;
    (void)flags;
    return DDERR_ALREADYINITIALIZED;
}

/* The region of a window changes whenever windows move; the clipper works it
   out anew for every blit, so the answer to "did it change" is always yes for
   a clipper with a window. */
static HRESULT WINAPI Clipper_IsClipListChanged(void *face, BOOL *changed)
{
    DD_METHOD_LOCK();
    if (!changed) return DDERR_INVALIDPARAMS;
    *changed = This(face)->window != NULL;
    return DD_OK;
}

static HRESULT WINAPI Clipper_SetClipList(void *face, LPRGNDATA list, DWORD flags)
{
    DD_METHOD_LOCK();
    DdClipper *clipper = This(face);
    RGNDATA *copy = NULL;
    DWORD bytes = 0;
    if (flags) return DDERR_INVALIDPARAMS;
    if (clipper->window) return DDERR_CLIPPERISUSINGHWND;
    if (list)
    {
        if (list->rdh.dwSize < sizeof(RGNDATAHEADER) || list->rdh.iType != RDH_RECTANGLES || list->rdh.nCount > 4096)
            return DDERR_INVALIDCLIPLIST;
        bytes = sizeof(RGNDATAHEADER) + list->rdh.nCount * sizeof(RECT);
        copy = (RGNDATA *)malloc(bytes);
        if (!copy) return DDERR_OUTOFMEMORY;
        memcpy(copy, list, bytes);
    }
    free(clipper->list);
    clipper->list = copy;
    clipper->list_bytes = bytes;
    return DD_OK;
}

static HRESULT WINAPI Clipper_SetHWnd(void *face, DWORD flags, HWND window)
{
    DD_METHOD_LOCK();
    DdClipper *clipper = This(face);
    if (flags) return DDERR_INVALIDPARAMS;
    if (window && !IsWindow(window)) return DDERR_INVALIDPARAMS;
    clipper->window = window;
    /* A window takes the place of a list that was set. */
    if (window)
    {
        free(clipper->list);
        clipper->list = NULL;
        clipper->list_bytes = 0;
    }
    return DD_OK;
}

static const struct IDirectDrawClipperVtbl clipper_table = {
    .QueryInterface = (void *)Clipper_QueryInterface, .AddRef = (void *)Clipper_AddRef, .Release = (void *)Clipper_Release,
    .GetClipList = (void *)Clipper_GetClipList, .GetHWnd = (void *)Clipper_GetHWnd, .Initialize = (void *)Clipper_Initialize,
    .IsClipListChanged = (void *)Clipper_IsClipListChanged, .SetClipList = (void *)Clipper_SetClipList,
    .SetHWnd = (void *)Clipper_SetHWnd};

HRESULT DdCreateClipper(DdClipper **out)
{
    DdClipper *clipper = (DdClipper *)DdAllocate(sizeof *clipper);
    if (!clipper) return DDERR_OUTOFMEMORY;
    clipper->face.table = &clipper_table;
    clipper->face.object = clipper;
    clipper->references = 1;
    *out = clipper;
    return DD_OK;
}
