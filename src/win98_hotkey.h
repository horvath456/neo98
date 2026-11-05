// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Original Win98 RegisterHotKey, UnregisterHotKey and the hot-key delivery.
//
// USER32!RegisterHotKey (ordinal 480) and USER32!UnregisterHotKey
// (ordinal 617) do nothing but lay a 16-bit argument block on the
// stack and enter the flat thunk. Their thunk indices are 1Fh and
// 1Eh. The 16-bit USER routines map the WORD
// window handle to USER's 32-bit window record.
// Both bodies share the list walk. The keyboard path
// at USER asks DOHOTKEYSTUFF, USER ordinal 601, whether a
// key belongs to a hot key; a hit posts WM_HOTKEY and swallows the keystroke.
//
// The list is a singly linked chain of 16-byte records in USER's local heap,
// its head is the DGROUP word at 204h, and every record holds:
//
// +00h next +02h queue (task) +04h id DWORD +08h window WORD
// +0Ah modifiers +0Ch virtual key +0Eh scan code
//
// The environment E supplies the list storage, the task and window queries,
// the keyboard scan-code translation, the physical key state and the two
// posting routes. A record is an opaque token; zero means none.
namespace Win98HotKey {
using U = unsigned;
inline U W(U x) { return x & 65535u; }
inline U Low(U x) { return x & 65535u; }
inline U High(U x) { return (x >> 16) & 65535u; }

// WM_HOTKEY (312h).
constexpr U Message = 0x312;
// The modifier bits the thunk accepts. Windows 98 tests FFF0h and hands anything
// above to KERNEL ordinal 327, which unwinds the thunklet and answers zero.
constexpr U Alt = 1, Control = 2, Shift = 4, Windows = 8;
constexpr U ModifierMask = 0x000f, ModifierError = 0xe002;
// a non-zero handle that does not resolve takes the same exit.
constexpr U HandleError = 0x6040;
// the sentinel that asks DOHOTKEYSTUFF to read the modifiers from the
// physical key state instead of trusting the caller.
constexpr U DeriveModifiers = 0xffff;
// LocalAlloc(LMEM_ZEROINIT, 10h), so +06h and +0Eh start at zero.
constexpr U RecordBytes = 0x10, AllocateFlags = 0x40;
// Windows 98 keeps the key as MAKELONG(vk, modifiers); WM_HOTKEY carries the
// reverse order, MAKELONG(modifiers, vk), because overwrites only the
// low word of the DOHOTKEYSTUFF frame.
inline U Key(U vk, U modifiers) { return W(vk) | (W(modifiers) << 16); }
inline U Parameter(U vk, U modifiers) { return W(modifiers) | (W(vk) << 16); }

// USER. Walks the whole list once. It answers the record that owns
// (queue, window, id) and reports separately whether any record at all,
// regardless of its owner, already carries this modifier and key pair. With
// `remove` set the owning record is unlinked and freed and the answer is 1.
template <class E> U Search(E &e, U queue, U window, U id, U key, U remove, U &duplicate)
{
    duplicate = 0;
    U found = 0, previous = 0;
    for (U record = e.Head(); record; ) {
        if (W(e.Queue(record)) == W(queue)) {
            // a window is compared through its 16-bit handle, and a
            // thread hot key compares the zero the caller passed.
            const U handle = window ? W(e.WindowHandle(window)) : 0;
            if (handle == W(e.Window(record)) && e.Id(record) == id) {
                if (W(remove)) {
                    // unlinks through the head word or the previous
                    // record. The first record always takes the head branch,
                    // which is why the uninitialised previous slot is safe.
                    if (e.Head() == record) e.SetHead(e.Next(record));
                    else e.SetNext(previous, e.Next(record));
                    e.Free(record);
                    return 1;
                }
                found = record;
            }
        }
        // Windows 98 runs for every record, including the one just matched, so
        // re-registering an unchanged key answers duplicate.
        if (W(e.Modifiers(record)) == High(key) && W(e.Vk(record)) == Low(key))
            duplicate = 1;
        previous = record;
        record = e.Next(record);
    }
    return found;
}

// USER, the body behind thunk index 1Fh. `window` is USER's 32-bit
// window record, zero for a thread hot key, and `key` is MAKELONG(vk, mod).
template <class E> U Register(E &e, U window, U id, U key)
{
    const U queue = W(e.CurrentTask());
    // a window that belongs to another task is refused.
    if (window && W(e.WindowTask(window)) != queue) return 0;
    U duplicate = 0;
    const U found = Search(e, queue, window, id, key, 0, duplicate);
    if (duplicate) return 0;  //
    if (found) {
        // Windows 98 keeps the record and only replaces its key.
        e.SetModifiers(found, High(key));
        e.SetVk(found, Low(key));
        e.SetScan(found, e.MapVirtualKey(Low(key)));
    } else {
        const U record = e.Allocate();
        if (!record) return 0;  //
        e.SetQueue(record, queue);
        e.SetWindow(record, window ? W(e.WindowHandle(window)) : 0);
        e.SetModifiers(record, High(key));
        e.SetVk(record, Low(key));
        e.SetScan(record, e.MapVirtualKey(Low(key)));
        e.SetId(record, id);
        e.SetNext(record, e.Head());
        e.SetHead(record);
    }
    e.ListChanged();  // hands the three list heads to the driver hook.
    return 1;
}

// USER, the body behind thunk index 1Eh.
template <class E> U Unregister(E &e, U window, U id)
{
    const U queue = W(e.CurrentTask());
    U duplicate = 0;
    const U result = Search(e, queue, window, id, 0, 1, duplicate);
    e.ListChanged();
    return result;
}

// USER, reached when a window is destroyed: every record naming this
// 16-bit handle is unlinked and freed, whatever task owns it.
template <class E> void FreeWindow(E &e, U handle)
{
    U previous = 0;
    for (U record = e.Head(); record; ) {
        const U next = e.Next(record);
        if (W(e.Window(record)) == W(handle)) {
            if (previous) e.SetNext(previous, next);
            else e.SetHead(next);
            e.Free(record);
        } else previous = record;
        record = next;
    }
}

// USER, the task teardown: every record of the calling task goes.
template <class E> void FreeTask(E &e)
{
    const U queue = W(e.CurrentTask());
    U previous = 0;
    for (U record = e.Head(); record; ) {
        const U next = e.Next(record);
        if (W(e.Queue(record)) == queue) {
            if (previous) e.SetNext(previous, next);
            else e.SetHead(next);
            e.Free(record);
        } else previous = record;
        record = next;
    }
}

// USER, the 16-bit side of thunk index 1Fh. It maps the narrowed
// window handle back to USER's 32-bit window record and gates the modifier
// word. Both gates enter KERNEL ordinal 327, which unwinds to the bail address
// the thunklet pushed and answers zero, so neither reaches the body.
template <class E> U RegisterEntry(E &e, U hwnd, U id, U modifiers, U vk)
{
    U window = 0;
    if (W(hwnd)) {
        window = e.MapHandle(W(hwnd));
        if (!window) { e.ParameterError(HandleError); return 0; }  //
    }
    if (W(modifiers) & ~ModifierMask) { e.ParameterError(ModifierError); return 0; }  //
    return Register(e, window, id, Key(vk, modifiers));
}

// USER, thunk index 1Eh. It validates nothing beyond the handle map.
template <class E> U UnregisterEntry(E &e, U hwnd, U id)
{
    U window = 0;
    if (W(hwnd)) {
        window = e.MapHandle(W(hwnd));
        if (!window) { e.ParameterError(HandleError); return 0; }
    }
    return Unregister(e, window, id);
}

// USER, the lookup DOHOTKEYSTUFF uses: the first record with this
// modifier and key pair, in list order, newest first.
template <class E> U Find(E &e, U vk, U modifiers)
{
    for (U record = e.Head(); record; record = e.Next(record))
        if (W(e.Modifiers(record)) == W(modifiers) && W(e.Vk(record)) == W(vk))
            return record;
    return 0;
}

// USER: the modifier word the keyboard path does not supply is read
// from the physical key state, VK_SHIFT, VK_CONTROL, VK_MENU and the two
// Windows keys.
template <class E> U Modifiers(E &e)
{
    U value = 0;
    if (e.KeyDown(0x10)) value = Shift;
    if (e.KeyDown(0x11)) value |= Control;
    if (e.KeyDown(0x12)) value |= Alt;
    if (e.KeyDown(0x5b) || e.KeyDown(0x5c)) value |= Windows;
    return value;
}

// DOHOTKEYSTUFF, USER ordinal 601. Answers 1 when the keystroke
// belongs to a hot key, which makes the caller swallow it.
template <class E> U DoHotKeyStuff(E &e, U vk, U modifiers)
{
    if (W(modifiers) == DeriveModifiers) modifiers = Modifiers(e);
    const U record = Find(e, W(vk), W(modifiers));
    if (!record) return 0;
    const U parameter = Parameter(W(vk), W(modifiers));
    const U id = e.Id(record);
    const U handle = W(e.Window(record));
    if (!handle) {
        // a thread hot key is posted to the record's own queue with
        // window handle 0, the same route PostThreadMessage takes.
        e.PostThread(W(e.Queue(record)), Message, id, parameter);
        e.SetLastTask(W(e.Queue(record)));
        return 1;
    }
    // resolves the handle again; a window that has gone away swallows
    // the key without posting anything.
    const U window = e.MapHandle(handle);
    if (!window) return 1;
    e.PostWindow(W(e.WindowHandle(window)), Message, id, parameter);
    e.SetLastTask(W(e.WindowTask(window)));
    return 1;
}
}
