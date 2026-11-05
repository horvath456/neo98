// SPDX-License-Identifier: GPL-2.0-or-later
// The ADVAPI32 registry entries that are not plain key and value calls.
// Windows 98 keeps the registry in VMM. ADVAPI32 is a thin shell: most of its
// Reg* entries check their arguments and then call one of the KERNEL32
// ordinals 1 to 6, which all share the ring transition at KERNEL32 and
// pass a service number in EAX. Two entries of that family carry enough ring-3
// logic to reproduce, and they are the two the program collection asks for
// most: RegConnectRegistryA answers the local machine without ever reaching
// the service, and RegNotifyChangeKeyValue owns the event and the wait around
// service 10029h.
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
#include <string.h>
namespace X86
{
// One outstanding RegNotifyChangeKeyValue. The table is process independent
// because Registry::Note is, so every entry names its process; the destructor
// of a process drops its own through FreeRegistryNotifications.
struct RegistryNotification
{
    Application *process = nullptr;
    unsigned event = 0, filter = 0;
    bool subtree = false;
    char path[256] = {};
};
static RegistryNotification notifications[32];
static bool observing = false;
// Registry::Observer. A change names the key it happened in, and for a created
// or removed key that is the new or gone key itself, so a name change matches
// the parent of the reported path.
static void RegistryChanged(unsigned kind, const char *path, const char *)
{
    const unsigned wanted = kind <= 2 ? 1u : 4u;
    unsigned length = (unsigned)strlen(path);
    if (kind <= 2)
    {
        while (length && path[length - 1] != '\\')
            --length;
        if (!length)
            return;
        --length;                                        // the key that holds it
    }
    for (auto &entry : notifications)
    {
        if (!entry.process || !(entry.filter & wanted))
            continue;
        const unsigned watched = (unsigned)strlen(entry.path);
        bool hit = watched == length && !strncmp(entry.path, path, watched);
        if (!hit && entry.subtree)
            hit = watched < length && path[watched] == '\\' && !strncmp(entry.path, path, watched);
        if (!hit)
            continue;
        // A notification fires once. The caller registers again if it wants
        // the next change, exactly as Win32 requires.
        Application *process = entry.process;
        const unsigned event = entry.event;
        entry = {};
        process->SignalRegistryNotification(event);
    }
}
void Application::SignalRegistryNotification(unsigned handle)
{
    auto *h = FindKernelHandle(handle);
    if (h && h->kind == 1)
        events[h->object].signaled = true;
}
void Application::FreeRegistryNotifications()
{
    for (auto &entry : notifications)
        if (entry.process == this)
            entry = {};
}
// The CryptoAPI entrance of ADVAPI32 sits in src/x86_advapi_crypto.cpp.
unsigned Application::AdvapiRegistryApi(ApiName name, const unsigned *a)
{
    switch (name.id)
    {
    // Only the six predefined roots are accepted, the output pointer
    // is probed, and an empty or absent machine name answers with the same
    // predefined key. A named machine goes to Remote_RegConnectRegistry in
    // WINREG.DLL, and without that entry the original returns 1114.
    API_CASE("RegConnectRegistryA")
    {
        if (a[1] < 0x80000000u || a[1] > 0x80000006u)
            return 6;
        if (!a[2] || !virtual_memory.Access(a[2], 4, true))
            return 87;
        const char *machine = a[0] ? ReadString(a[0]) : nullptr;
        if (!machine || !*machine)
        {
            Write32(a[2], a[1]);
            return 0;
        }
        // No remote registry provider. WINREG.DLL is not in the tree, so the
        // original takes its GetProcAddress failure exit.
        return 1114;
    }
    // Without ASYNCHRONOUS the entry makes its own manual reset
    // event, hands it to service 10029h and waits on it alertably, so the call
    // returns when the key changes. With ASYNCHRONOUS the caller's event is
    // registered and the pending answer 997 of the service becomes success.
    API_CASE("RegNotifyChangeKeyValue")
    {
        unsigned node = 0, result = registry.Key(a[0], node);
        if (result)
            return 6;                                    //
        const bool asynchronous = a[4] != 0;
        unsigned event = a[3];
        if (!asynchronous)
        {
            const unsigned create[4] = {0, 1, 0, 0};
            event = ThreadApi("CreateEventA", create);
            if (!event)
                return last_error;                       //
        }
        auto *handle = FindKernelHandle(event);
        if (!handle || handle->kind != 1)
        {
            // The service refuses a handle that is no event. Win98 answers
            // from VMM here; NEO98 answers with the handle error, because the
            // event table is the only thing that can carry the notification.
            if (!asynchronous)
            {
                const unsigned close[1] = {event};
                ThreadApi("CloseHandle", close);
            }
            return 6;
        }
        RegistryNotification *slot = nullptr;
        for (auto &entry : notifications)
            if (!entry.process)
            {
                slot = &entry;
                break;
            }
        char path[256];
        if (!slot || !registry.store.Path(node, path, sizeof path))
            result = 8;
        else
        {
            if (!observing)
                observing = Registry::Observe(RegistryChanged);
            if (!observing)
                result = 8;
            else
            {
                slot->process = this;
                slot->event = event;
                slot->filter = a[2];
                slot->subtree = a[1] != 0;
                strcpy(slot->path, path);
            }
        }
        if (asynchronous)
            return result == 997 ? 0u : result;           //
        if (!result)
        {
            const unsigned wait[3] = {event, 0xffffffffu, 1};
            result = KernelAsyncApi("WaitForSingleObjectEx", wait) == 0xffffffffu ? last_error : 0u;
        }
        for (auto &entry : notifications)
            if (entry.process == this && entry.event == event)
                entry = {};
        const unsigned close[1] = {event};
        ThreadApi("CloseHandle", close);
        return result;
    }
    default:
        break;
    }
    Fail("Unsupported ADVAPI32 registry API");
    return 120;
}
} // namespace X86
