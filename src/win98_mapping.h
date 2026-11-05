// SPDX-License-Identifier: GPL-2.0-or-later
// Win98 SE KERNEL32 mapping-object/view algorithms.
#pragma once
namespace Win98Mapping
{
using U = unsigned;
struct Object
{
    // id is available to a native handle provider; the original algorithms do not use it.
    U id = 0, name = 0, size = 0, base = 0, index = 0;
    U backing = ~0u, backing_info = 0, file = 0xffff, old_size = 0;
    U protection = 0, pager = 0, file_owner = 0;
};
struct View { U address = 0; View *next = nullptr; Object *object = nullptr; };
struct File { U dos = 0, backing = ~0u, name = 0; };
struct State { Object **slots = nullptr; U capacity = 0; View *views = nullptr; };
// Environment contract (all methods may set Error/LastError in their provider):
// Read32(U), Error(U), Lock, Unlock, OpenNamed(U name,U type,U flags)->U;
// ResolveFile(U handle,U type,U access)->File*, AllocateObject(U bytes,U type)->Object*;
// BindName(Object&,U)->U, DuplicateBackingName(U)->U, FreeBackingName(U);
// FileSize(U)->U, TranslateFile(U &dos,U &flags)->U status, FileOwner->U,
// RetainFileOwner(U), SetFilePointer(U handle,U position)->U, SetEndOfFile(U)->bool;
// PagerRead/Write/Copy->U, AllocateTable(U count)->Object** (zero initialized),
// FreeTable(Object**), CreateHandle(Object&,U flags)->U (~0u on failure),
// CloseFile(Object&,U), FreeObject(Object&), ReleaseName(U);
// ResolveMapping(U handle,U type,U access)->Object*, Reserve(U,U,U)->U,
// Commit(U page,U count,U pager,U backing,U flags)->U, ReleasePages(U,U),
// AllocateView->View*, FreeView(View&), AddReference(Object&), ReleaseReference(Object&),
// UnmapPages(U page,U count). Providers must retain objects until all handles/views close.
// Pointers are native. Table sizes count pointers, despite 4-byte pointers in the original.

template <class E> U Security(E &e, U security)
{
    if (!security) return 0;
    if (e.Read32(security) != 12) return ~0u;
    return e.Read32(security + 8) ? 0x80000000u : 0;
}
// File helpers and VxD metadata allocation remain providers.
template <class E>
U Create(E &e, State &s, U handle, U security, U protection, U high, U size, U name)
{
    e.Lock();
    e.Error(0);
    const U inherit = Security(e, security);
    File *file = nullptr;
    Object *o = nullptr;
    U dos = 0, previous_size = 0, file_flags = 0, pager = 0, index = 0, result = 0;
    if (inherit == ~0u) { e.Error(87); goto failed; }
    if (handle != ~0u)
    {
        file = e.ResolveFile(handle, 0x80, (protection & 4) ? 0x130 : 0x20);
        if (!file) { e.Error(87); goto failed; }
    }
    if (name)
    {
        result = e.OpenNamed(name, 13, inherit);
        if (result != ~0u)
        {
            if (result) e.Error(183);
            e.Unlock();
            return result;
        }
    }
    if (high) { e.Error(8); goto failed; }
    o = e.AllocateObject(0x28, 13);
    // The original null-allocation branch reaches a null dereference in its
    // cleanup. The ARM adaptation returns allocation failure safely instead.
    if (!o) goto failed;
    if (name)
    {
        o->name = e.BindName(*o, name);
        if (!o->name) goto free_object;
    }
    if (file && file->dos < 0x200 && file->name && file->backing != ~0u && (protection & 2))
    {
        o->backing = file->backing;
        o->backing_info = e.DuplicateBackingName(file->name);
        if (!o->backing_info) goto free_object;
    }
    else
    {
        o->backing_info = 0;
        o->backing = ~0u;
    }
    if (protection & 0x4000000)
    {
        dos = 0xffff;
        pager = 0xff;
    }
    else if (handle == ~0u)
    {
        dos = 0xffff;
        pager = 1;
        if (!size) { e.Error(87); goto free_object; }
    }
    else
    {
        previous_size = e.FileSize(handle);
        if (previous_size == ~0u) { e.Error(6); goto free_object; }
        if (!size)
        {
            size = previous_size;
            if (!size) { e.Error(1006); goto free_object; }
        }
        dos = file->dos & 0xffff;
        if (o->backing != ~0u)
            file_flags = 1;
        else
        {
            if (e.TranslateFile(dos, file_flags)) { e.Error(6); goto free_object; }
            if (file_flags & 2)
            {
                o->file_owner = e.FileOwner() & 0xffff;
                e.RetainFileOwner(o->file_owner);
            }
        }
        pager = (protection & 8) ? e.PagerCopy()
                : ((file_flags & 1) && (protection & 4)) ? e.PagerWrite() : e.PagerRead();
        if (o->backing != ~0u) dos = 0;
        if (previous_size < size)
        {
            e.Unlock();
            const bool extended = e.SetFilePointer(handle, size) != ~0u && e.SetEndOfFile(handle);
            e.Lock();
            if (!extended) goto close_file;
        }
    }
    for (index = 0; index < s.capacity && s.slots[index]; ++index) {}
    if (index == s.capacity)
    {
        Object **slots = e.AllocateTable(s.capacity + 3);
        if (!slots) goto restore_file;
        for (U i = 0; i < s.capacity; ++i) slots[i] = s.slots[i];
        Object **old = s.slots;
        s.slots = slots;
        s.capacity += 3;
        e.FreeTable(old);
    }
    s.slots[index] = o;
    o->index = index;
    o->base = 0;
    o->size = size;
    o->old_size = previous_size; // Original date-less value is an uninitialized stack DWORD.
    o->protection = protection & 255;
    o->pager = pager & 255;
    o->file = dos;
    result = e.CreateHandle(*o, inherit);
    if (result != ~0u) { e.Unlock(); return result; }
    s.slots[index] = nullptr;
restore_file:
    if (handle != ~0u && previous_size < size)
        if (e.SetFilePointer(handle, previous_size) != ~0u) e.SetEndOfFile(handle);
close_file:
    if (handle != ~0u && dos != 0xffff) e.CloseFile(*o, dos);
free_object:
    if (o->backing_info) e.FreeBackingName(o->backing_info);
    e.FreeObject(*o);
failed:
    e.Unlock();
    return 0;
}
// This original body does not inspect desiredAccess.
template <class E> U Open(E &e, U, U inherit, U name)
{
    e.Lock();
    U result = e.OpenNamed(name, 13, inherit ? 0x80000000u : 0);
    if (result == ~0u) { e.Error(123); result = 0; }
    e.Unlock();
    return result;
}
// Windows 98 passes desired=0.
template <class E>
U Map(E &e, State &s, U handle, U access, U high, U offset, U length, U desired = 0)
{
    if (offset & 0xffff) { e.Error(87); return 0; }
    e.Lock();
    Object *o = e.ResolveMapping(handle, 0x1000, 0);
    U result = 0, address = 0;
    if (!o) goto done;
    if (high || offset >= 0x7fc00000 || length >= 0x7fc00000 || offset + length > o->size)
        goto memory_error;
    if (!length) length = o->size - offset;
    if (access == 0xf001f) access = 2;
    if (access & 1)
    {
        if (o->pager != e.PagerCopy()) goto parameter_error;
    }
    else if (o->pager == e.PagerCopy())
    {
        if (access & 2) goto parameter_error;
        goto access_valid;
    }
    if ((access & 2) && !(o->protection & 4)) goto parameter_error;
access_valid:
    if (!o->base)
    {
        U page = 0x80060000;
        const U whole_size = o->size;
        if (desired)
        {
            if (desired < 0x80000000 || desired > 0xbfffffff) goto address_error;
            page = ((desired - offset) & ~0xf000u) >> 12;
        }
        address = e.Reserve(page, (whole_size + 4095) >> 12, o->pager == 255 ? 0 : 0x10);
        if (address == ~0u) goto memory_error;
        o->base = address;
        address += offset;
    }
    else
    {
        address = o->base + offset;
        if (desired && address != desired) goto address_error;
    }
    if (o->pager != 255)
    {
        const U flags = 0x60040000u | ((access & 3) ? 0x20000u : 0);
        if (!e.Commit(address >> 12, (length + 4095) >> 12, o->pager,
                      (o->index << 18) + (offset >> 12), flags)) goto memory_error;
    }
    {
        View *view = e.AllocateView();
        if (!view) goto done;
        address = o->base + offset;
        view->address = address;
        view->object = o;
        view->next = s.views;
        s.views = view;
        e.AddReference(*o);
        result = address;
    }
    goto done;
parameter_error:
    e.Error(87); goto done;
address_error:
    e.Error(487); goto done;
memory_error:
    e.Error(8);
done:
    e.Unlock();
    return result;
}
// Unlink before dropping the object's view reference.
template <class E> U Unmap(E &e, State &s, U address)
{
    e.Lock();
    View **link = &s.views;
    while (*link && (*link)->address != address) link = &(*link)->next;
    View *view = *link;
    if (!view)
    {
        e.Error(487);
        e.Unlock();
        return 0;
    }
    Object &o = *view->object;
    if (o.pager == e.PagerRead() || o.pager == e.PagerWrite())
        e.UnmapPages(address >> 12, (o.base - view->address + o.size + 4095) >> 12);
    *link = view->next;
    e.ReleaseReference(o);
    e.FreeView(*view);
    e.Unlock();
    return 1;
}
// CloseFile contains DOS/VxD/file-owner cleanup boundaries.
template <class E> void Destroy(E &e, State &s, Object &o)
{
    e.Lock();
    if (o.name) e.ReleaseName(o.name);
    if (o.base) e.ReleasePages(o.base, 0x10);
    s.slots[o.index] = nullptr;
    if (o.file != 0xffff) e.CloseFile(o, o.file);
    if (o.backing_info) e.FreeBackingName(o.backing_info);
    e.FreeObject(o);
    e.Unlock();
}
} // namespace Win98Mapping
