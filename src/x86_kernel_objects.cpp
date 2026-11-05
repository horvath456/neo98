// SPDX-License-Identifier: GPL-2.0-or-later
// The session-wide namespace of named kernel objects: events and waitable
// timers, mutexes, semaphores and file mappings. See
// the object implementations below for their lifetime rules.
// Win98 keeps these records in KERNEL32's shared arena above 80000000h, which
// every process maps at the same address. A record therefore belongs to no
// process. What a process owns is a handle, an entry in its own table
// (Application::kernel_handles), and the record dies with the last handle.
// NEO98 keeps the same split: the three tables below are session-wide, the
// handle tables stay per Application.
#include "x86_app.h"
#include <string.h>
namespace X86
{
Application::Event Application::events[Application::KernelObjectSlots];
Application::KernelMutex Application::kernel_mutexes[Application::KernelObjectSlots];
Application::Semaphore Application::semaphores[Application::KernelObjectSlots];
// ConvertToGlobalHandle publishes here; see Application::PublishGlobalHandle
// in src/x86_threads.cpp and the comment beside the declaration.
Application::GlobalKernelHandle Application::global_kernel_handles[64];
// One namespace for all four types, as in the original: the search walks the
// records by name and only then looks at the type. A name that belongs to
// another type is not free, and the create that finds it fails.
unsigned Application::FindNamedObject(unsigned kind, const char *name)
{
    if (!name || !*name)
        return 0;
    for (unsigned i = 0; i < KernelObjectSlots; ++i)
    {
        if (kind == 1 && events[i].references && events[i].named && !strcmp(events[i].name, name))
            return i + 1;
        if (kind == 3 && kernel_mutexes[i].references && kernel_mutexes[i].named &&
            !strcmp(kernel_mutexes[i].name, name))
            return i + 1;
        if (kind == 4 && semaphores[i].references && semaphores[i].named && !strcmp(semaphores[i].name, name))
            return i + 1;
    }
    return 0;
}
unsigned Application::NamedObjectKind(const char *name)
{
    if (!name || !*name)
        return 0;
    if (FindNamedObject(1, name))
        return 1;
    if (FindNamedObject(3, name))
        return 3;
    if (FindNamedObject(4, name))
        return 4;
    return MappingNameTaken(name) ? 5 : 0;
}
unsigned Application::LiveKernelObjects()
{
    unsigned n = 0;
    for (unsigned i = 0; i < KernelObjectSlots; ++i)
    {
        if (events[i].references) ++n;
        if (kernel_mutexes[i].references) ++n;
        if (semaphores[i].references) ++n;
    }
    return n;
}
// A process that ends gives up its handles. Without this the session-wide
// records would keep the references of a dead process forever, because the
// tables no longer die with the Application.
void Application::FreeKernelObjectHandles()
{
    for (auto &h : kernel_handles)
    {
        if (h.kind != 1 && h.kind != 3 && h.kind != 4 && h.kind != 5)
            continue;
        const unsigned handle = KernelHandleValue(unsigned(&h - kernel_handles + 1), h.generation);
        ThreadApi("CloseHandle", &handle);
    }
    // A waitable timer this process armed must not keep firing into the freed
    // Application; the record itself may outlive it through another handle.
    for (auto &e : events)
        if (e.timer_owner == this)
        {
            e.timer_owner = nullptr;
            e.active = false;
        }
}
} // namespace X86
