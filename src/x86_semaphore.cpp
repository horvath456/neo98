// SPDX-License-Identifier: GPL-2.0-or-later
// Native objects/providers for the implemented KERNEL32 and VWIN32 routines.
#include "x86_app.h"
#include <string.h>
namespace X86
{
unsigned Application::SemaphoreApi(ApiName name, const unsigned *a)
{
    struct Environment
    {
        Application &app;
        unsigned Read32(unsigned p) { return app.Read32(p); }
        unsigned char Read8(unsigned p)
        {
            unsigned char value = 0;
            app.Read(p, &value, 1);
            return value;
        }
        void Write32(unsigned p, unsigned value)
        {
            if (p)
                app.Write32(p, value);
        }
        void Error(unsigned error) { app.last_error = error; }
        unsigned LastError() const { return app.last_error; }
        // These providers cannot yield in the current cooperative process.
        // They must become real locks before parallel native threads are added.
        void LockNamespace() {}
        void UnlockNamespace() {}
        void EnterCritical() {}
        void LeaveCritical() {}
        unsigned EnterCount() { return 0; }
        void LeaveCount(unsigned) {}
        void WakeWaiters(Win98Semaphore::State &) {}
        unsigned CreateHandle(unsigned object, unsigned flags)
        {
            const auto handle = app.NewKernelHandle(4, object - 1);
            if (!handle)
                return ~0u;
            auto *record = app.FindKernelHandle(handle);
            record->inherit = (flags & 0x80000000u) != 0;
            record->access = flags & 0x1f0003;
            return handle;
        }
        unsigned OpenNamed(unsigned pointer, unsigned, unsigned flags)
        {
            if (!pointer)
                return ~0u;
            const char *name = app.ReadString(pointer);
            if (app.state == Failed)
                return 0;
            // an over-long name is ERROR_INVALID_NAME.
            if (strlen(name) > Application::KernelObjectNameLimit)
            {
                Error(123);
                return 0;
            }
            // The name lives in the session-wide namespace, not in this process.
            const unsigned kind = Application::NamedObjectKind(name);
            if (kind && kind != 4)
            {
                Error(6);
                return 0;
            }
            if (const unsigned found = Application::FindNamedObject(4, name))
            {
                const auto handle = CreateHandle(found, flags);
                return handle == ~0u ? 0 : handle;
            }
            return ~0u;
        }
        unsigned AllocateObject(unsigned, unsigned)
        {
            for (unsigned i = 0; i < Application::KernelObjectSlots; ++i)
                if (!app.semaphores[i].references)
                {
                    app.semaphores[i] = {};
                    return i + 1;
                }
            Error(8);
            return 0;
        }
        void InitializeObject(unsigned object, unsigned initial, unsigned maximum)
        {
            app.semaphores[object - 1].data = {initial, maximum};
        }
        void DestroyObject(unsigned object) { app.semaphores[object - 1] = {}; }
        bool BindName(unsigned object, unsigned pointer)
        {
            const char *name = app.ReadString(pointer);
            if (app.state == Failed)
                return false;
            if (strlen(name) > Application::KernelObjectNameLimit)
            {
                Error(123);
                return false;
            }
            auto &semaphore = app.semaphores[object - 1];
            semaphore.named = true;
            strcpy(semaphore.name, name);
            return true;
        }
        void CloseHandle(unsigned handle) { app.ThreadApi("CloseHandle", &handle); }
        unsigned ResolveHandle(unsigned handle, unsigned, unsigned)
        {
            auto *record = app.FindKernelHandle(handle);
            if (!record || record->kind != 4)
            {
                Error(6);
                return 0;
            }
            if (!(record->access & 2))
            {
                Error(5);
                return 0;
            }
            ++app.semaphores[record->object].references;
            return record->object + 1;
        }
        unsigned Signal(unsigned object, unsigned amount, unsigned previous)
        {
            // ARM service policy. The full original Win32 VxD service's
            // parameter checks/error translation are not implemented yet.
            if (!amount || (amount & 0x80000000u))
            {
                Error(87);
                return 0;
            }
            auto &count = app.semaphores[object - 1].data;
            const long long current =
                (long long)count.count - ((count.count & 0x80000000u) ? 0x100000000ll : 0);
            const long long maximum =
                (long long)count.maximum - ((count.maximum & 0x80000000u) ? 0x100000000ll : 0);
            if (current + amount > maximum)
            {
                Error(298);
                return 0;
            }
            if (!Win98Semaphore::ReleaseCount(*this, count, amount, previous))
            {
                Error(298);
                return 0;
            }
            // Runnable observes the count; WaitForSingleObject consumes it.
            return app.state != Failed;
        }
        void ReleaseReference(unsigned object) { --app.semaphores[object - 1].references; }
    } e{*this};
    if (API_IS(name,"ReleaseSemaphore"))
    {
        if (a[2] && !virtual_memory.Access(a[2], 4, true))
        {
            last_error = 87;
            return 0;
        }
        return Win98Semaphore::Release(e, a[0], a[1], a[2]);
    }
    if (API_IS(name,"OpenSemaphoreA"))
    {
        if (a[2])
            ReadString(a[2]);
        if (state == Failed)
            return 0;
        return Win98Semaphore::Open(e, a[0], a[1], a[2]);
    }
    // Replace public x86 pointer probes/SEH with native guest-access validation.
    if (a[0])
    {
        unsigned attributes[3];
        if (!Read(a[0], attributes, sizeof attributes))
            return 0;
    }
    if (a[3])
        ReadString(a[3]);
    if (state == Failed)
        return 0;
    return Win98Semaphore::Create(e, a[0], a[1], a[2], a[3]);
}
} // namespace X86
