// SPDX-License-Identifier: GPL-2.0-or-later
// Native connection for the implemented KERNEL32 GetProcessDword. The
// dispatch lives in win98_process_dword.h. NEO98 has no Win98 process
// data, so only the fields its own process manager can answer are served.
#include "win98_process_dword.h"
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
unsigned Application::ProcessDwordApi(const unsigned *a)
{
    struct DatabaseEnvironment final : Win98ProcessDword::Environment
    {
        Application &app;
        explicit DatabaseEnvironment(Application &value) : app(value) {}
        // NEO98 has no process data. The identifier itself stands in for
        // it, which is enough to distinguish the caller from a child and to
        // reject an unknown process the way the original does.
        unsigned Database(unsigned identifier) override
        {
            if (!identifier)
                return app.ProcessId();
            bool live = false;
            return app.FindProcessIdentity(identifier, live) ? identifier : 0;
        }
        bool Read(unsigned database, int field, unsigned &value) override
        {
            // GetCurrentProcessId maps the database through the
            // same helper this case uses, so the field is the
            // process identifier. NEO98 uses the identifier as the data.
            if (field == Win98ProcessDword::ProcessIdentity)
            {
                value = database;
                return true;
            }
            // Field -48 returns the task's Win16 hInstance. Windows returns
            // zero when a process has no task data, but SHELL32 treats values
            // of 32 or less as failure when opening folders. Use the process
            // image base as its instance: nonzero, above 32 and unique per
            // process. Fields -56 and -44 use the same task database but
            // remain unsupported here.
            if (field == Win98ProcessDword::TaskSlot1c)
            {
                auto *other = static_cast<Application *>(User::ProcessContextOf(database));
                value = (other ? other : &app)->image.Base();
                return true;
            }
            // Field -4 reads the process flags dword at database + 0x20. The
            // local KERNEL32 shows which bits live there: SetFileApisToANSI
            // clears 0x40, SetFileApisToOEM sets it and AreFileApisANSI reads
            // it, while TerminateProcess sets 0x80. NEO98 models the file API
            // bit in its own process file state and reports only that one; the
            // remaining bits of the original dword have no counterpart.
            // Another process answers too: SHELL32 asks it of the process
            // behind a DDE server window and tests 8, the Win16 process bit
            // (ShellExecute with ddeexec). The bits this port keeps
            // are 8 (Win16 process), 40h (file APIs in OEM, from the file
            // state) and 100h (RegisterServiceProcess).
            if (field == Win98ProcessDword::Value20)
            {
                auto *target = database == app.ProcessId() ? &app
                                                           : static_cast<Application *>(User::ProcessContextOf(database));
                if (!target)
                    return false;
                auto *state = target->ProcessFileState();
                value = (state ? state->process_flags & 0x40u : 0u) | (target->process_flags & 0x100u) |
                        (target->Win16Process() ? 8u : 0u);
                return true;
            }
            (void)value;
            return false;
        }
    } environment(*this);
    bool known = false;
    const unsigned result = Win98ProcessDword::Get(environment, a[0], a[1], known);
    if (!known)
    {
        char message[160];
        snprintf(message, sizeof message,
                 "GetProcessDword field %d needs the Win98 process database", int(a[1]));
        // The diagnostic export-stub mode exists to see past a gap like this,
        // so it records the field and hands back the neutral zero instead of
        // stopping. The default path keeps the named stop.
        if (stub_mode)
        {
            if (log)
                log(message);
            return 0;
        }
        Fail(message);
        return 0;
    }
    return result;
}
} // namespace X86
