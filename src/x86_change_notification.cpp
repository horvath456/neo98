// SPDX-License-Identifier: GPL-2.0-or-later
// Native objects and RAM-volume watcher for the implemented KERNEL32 routines.
#include "x86_app.h"
#include "win98_change_notification.h"
#include <string.h>
namespace X86
{
namespace
{
// Native provider policy for the IFS filter argument. KERNEL32 forwards the
// raw value; the Win98 IFSMGR request 71A3 is not implemented here, so this
// is our own bound: the documented FILE_NOTIFY_CHANGE_* bits, none of them
// empty. FILE_NOTIFY_CHANGE_LAST_ACCESS, _CREATION and _SECURITY are accepted
// but never raised by the RAM volume.
constexpr unsigned KnownFilters = 0x17f;
} // namespace
bool Application::WatchesDirectory(const ChangeNotification &watch, unsigned directory) const
{
    if (directory == watch.node)
        return true;
    if (!watch.subtree)
        return false;
    auto &store = Files::System();
    for (unsigned i = 0, node = directory; i < 256; ++i)
    {
        const auto *entry = store.Get(node);
        if (!entry || entry->parent == Files::Invalid)
            return false;
        node = entry->parent;
        if (node == watch.node)
            return true;
    }
    return false;
}
// The ring-0 event of a matching object becomes signaled and stays signaled
// until FindNextChangeNotification re-arms the search. Changes this process
// made itself count, exactly as they do in Windows.
void Application::NoteFileChange(unsigned directory, unsigned change)
{
    for (auto &watch : change_notifications)
    {
        if (!watch.references || !watch.dos || watch.signaled)
            continue;
        if (!(watch.filter & change))
            continue;
        if (WatchesDirectory(watch, directory))
            watch.signaled = true;
    }
}
void Application::ObserveFileChange(void *context, unsigned directory, unsigned change)
{
    static_cast<Application *>(context)->NoteFileChange(directory, change);
}
void Application::CloseChangeNotification(unsigned object)
{
    auto &watch = change_notifications[object - 1];
    watch = ChangeNotification{};
}
void Application::FreeChangeNotifications()
{
    if (change_observer_installed)
    {
        Files::System().RemoveObserver(this);
        change_observer_installed = false;
    }
    for (auto &watch : change_notifications)
        watch = ChangeNotification{};
}
struct Application::ChangeEnvironment
{
    Application &app;
    // The original buffer is a 0x104-byte stack local of the KERNEL32 body.
    // Native path resolution never reaches guest memory, so this provider owns
    // it as a host array behind the same address arithmetic.
    unsigned char scratch[0x104] = {};
    static constexpr unsigned Base = 0x1000;
    unsigned Scratch() const { return Base; }
    unsigned Read8(unsigned p) const { return p - Base < sizeof scratch ? scratch[p - Base] : 0; }
    void Write8(unsigned p, unsigned value)
    {
        if (p - Base < sizeof scratch)
            scratch[p - Base] = (unsigned char)value;
    }
    // The cooperative scheduler owns this critical section and the heap lock.
    void Lock() {}
    void Unlock() {}
    void LockHeap() {}
    void UnlockHeap() {}
    void Error(unsigned error) { app.last_error = error; }
    unsigned LastError() const { return app.last_error; }
    // / the thread byte at offset 45h. The runtime is configured for
    // ANSI file APIs, so the OEM branch and its cleanup stay unexercised.
    unsigned FileApiMode(unsigned) const { return 0; }
    void ClearFileApiState(unsigned) {}
    unsigned FullPath(unsigned path, unsigned capacity, unsigned buffer, unsigned)
    {
        const char *input = app.ReadString(path);
        if (app.state == Failed)
            return 0;
        char absolute[260];
        const unsigned resolved = app.files.FullPath(input, absolute);
        // Only an over-long result reaches the original 206 branch; every other
        // resolution failure produces the original's empty-result error 161.
        if (resolved)
            return resolved == 206 ? capacity : 0;
        const unsigned length = (unsigned)strlen(absolute);
        if (length + 1 > capacity)
            return length + 1;
        for (unsigned i = 0; i <= length; ++i)
            Write8(buffer + i, (unsigned char)absolute[i]);
        return length;
    }
    unsigned AllocateObject(unsigned, unsigned)
    {
        for (unsigned i = 0; i < 16; ++i)
            if (!app.change_notifications[i].references)
            {
                app.change_notifications[i] = ChangeNotification{};
                return i + 1;
            }
        Error(8);
        return 0;
    }
    void FreeObject(unsigned object) { app.CloseChangeNotification(object); }
    // Offset 4 of the original object. The native waitable state lives in the
    // record itself, so the identity that reaches the IFS request is the token.
    unsigned SignalObject(unsigned object) const { return object; }
    unsigned DosHandle(unsigned object) const { return app.change_notifications[object - 1].dos; }
    void SetDosHandle(unsigned object, unsigned value) { app.change_notifications[object - 1].dos = value; }
    bool IfsOpen(unsigned path, unsigned subtree, unsigned filter, unsigned low, unsigned high,
                 unsigned &result)
    {
        const unsigned object = low | (high << 16);
        if (!filter || (filter & ~KnownFilters))
        {
            result = 87;
            return false;
        }
        char absolute[260] = {};
        unsigned i = 0;
        for (; i < sizeof absolute - 1; ++i)
        {
            absolute[i] = (char)Read8(path + i);
            if (!absolute[i])
                break;
        }
        absolute[i] = 0;
        unsigned node = 0;
        const unsigned error = Files::System().Lookup(absolute, node);
        if (error)
        {
            result = error;
            return false;
        }
        const auto *entry = Files::System().Get(node);
        if (!entry || !(entry->attributes & 16))
        {
            result = 267;
            return false;
        }
        if (!app.change_observer_installed)
        {
            if (!Files::System().AddObserver(&app, &Application::ObserveFileChange))
            {
                result = 8;
                return false;
            }
            app.change_observer_installed = true;
        }
        auto &watch = app.change_notifications[object - 1];
        watch.node = node;
        watch.filter = filter;
        watch.subtree = subtree != 0;
        watch.signaled = false;
        result = object;
        return true;
    }
    bool IfsNext(unsigned handle, unsigned &)
    {
        app.change_notifications[handle - 1].signaled = false;
        return true;
    }
    bool IfsClose(unsigned handle, unsigned &)
    {
        auto &watch = app.change_notifications[handle - 1];
        watch.node = 0;
        watch.filter = 0;
        watch.subtree = false;
        watch.signaled = false;
        return true;
    }
    unsigned CreateHandle(unsigned object)
    {
        const unsigned handle = app.NewKernelHandle(8, object - 1);
        return handle ? handle : ~0u;
    }
    unsigned Resolve(unsigned handle, unsigned mask)
    {
        auto *record = app.FindKernelHandle(handle);
        if (!record || record->kind != 8 || !(mask & 0x100))
        {
            Error(6);
            return 0;
        }
        ++app.change_notifications[record->object].references;
        return record->object + 1;
    }
    void ReleaseReference(unsigned object)
    {
        auto &watch = app.change_notifications[object - 1];
        if (watch.references)
            --watch.references;
    }
};
unsigned Application::ChangeNotificationApi(ApiName name, const unsigned *a)
{
    ChangeEnvironment e{*this};
    if (API_IS(name,"FindNextChangeNotification"))
        return Win98ChangeNotification::Next(e, a[0]);
    if (API_IS(name,"FindFirstChangeNotificationW"))
        return Win98ChangeNotification::FirstWide(e);
    // Replace the public read probe and its Win98 fault dispatch with native
    // guest-access validation; the original reads only the first path byte.
    if (a[0])
    {
        unsigned char first = 0;
        if (!Read(a[0], &first, 1))
            return ~0u;
    }
    return Win98ChangeNotification::First(e, a[0], a[1], a[2]);
}
} // namespace X86
