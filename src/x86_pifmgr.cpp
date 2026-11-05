// SPDX-License-Identifier: GPL-2.0-or-later
// The native PIFMGR behind the four SHELL.DLL forwarders of
// SHELL32's stubs of ordinals 9, 10, 11 and 13 reach the flat thunk indices
// 5, 4, 3 and 2; the implemented forwarders of src/win98_shell_pif.h load
// PIFMGR.DLL through the environment of Application::ShellPifCall, and that
// environment now answers the load with an instance and the far calls with
// this file. The property services themselves are src/win98_pifmgr.h; here
// are the handle table of the process, the arguments of the 32-bit caller
// and the file store.
#include "x86_app.h"
#include "win98_pifmgr.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
struct Application::PifMgrState
{
    Win98PifMgr::Record records[16];
};
namespace
{
constexpr unsigned HandleBase = 0x0140, HandleStep = 0x20;
}
// The file store of the process behind the services.
struct Application::PifMgrEnvironment final : Win98PifMgr::Environment
{
    Application &app;
    explicit PifMgrEnvironment(Application &a) : app(a) {}
    bool ReadFile(const char *path, unsigned char *out, unsigned capacity, unsigned &size) override
    {
        size = 0;
        unsigned handle = 0, status = 0;
        if (app.files.Open(path, 1, 3, 3, 0x80, handle, status)) return false;
        unsigned total = 0;
        bool ok = !app.files.Size(handle, total) && total <= capacity;
        unsigned done = 0;
        if (ok) ok = !app.files.Read(handle, out, total, done) && done == total;
        app.files.Close(handle);
        if (ok) size = total;
        return ok;
    }
    bool WriteFile(const char *path, const unsigned char *data, unsigned length) override
    {
        unsigned handle = 0, status = 0;
        if (app.files.Open(path, 2, 0, 2, 0x80, handle, status)) return false;
        unsigned done = 0;
        const bool ok = !app.files.Write(handle, data, length, done) && done == length;
        app.files.Close(handle);
        return ok;
    }
    bool FileExists(const char *path) override
    {
        unsigned handle = 0, status = 0;
        if (app.files.Open(path, 1, 3, 3, 0x80, handle, status)) return false;
        app.files.Close(handle);
        return true;
    }
    void FullPath(const char *path, char (&out)[Win98PifMgr::PathCapacity]) override
    {
        if (app.files.FullPath(path, out)) snprintf(out, sizeof out, "%s", path);
    }
    void WindowsDirectory(char (&out)[Win98PifMgr::PathCapacity]) override { snprintf(out, sizeof out, "C:\\WINDOWS"); }
};

// The properties of a program for the DOS process itself, the way WINOLDAP
// opens them before it builds the MS-DOS window.
bool Application::PifMgrOpenPath(const char *path, Win98PifMgr::Record &record)
{
    PifMgrEnvironment environment(*this);
    return Win98PifMgr::Open(record, environment, path, "", 0, 0) != 0;
}

void Application::FreePifMgr()
{
    delete pifmgr;
    pifmgr = nullptr;
}

Win98PifMgr::Record *Application::PifMgrRecord(unsigned handle)
{
    if (!pifmgr) return nullptr;
    handle &= 0xffff;
    if (handle < HandleBase || (handle - HandleBase) % HandleStep) return nullptr;
    const unsigned index = (handle - HandleBase) / HandleStep;
    if (index >= 16 || !pifmgr->records[index].used) return nullptr;
    return &pifmgr->records[index];
}

// One of the four PIFMGR ordinals with the block of 16-bit words the SHELL32
// stub pushed (0x22a77, 0x22aad), in push order: the stubs map
// their pointers with SMapLS_IP_EBP_8, _12 and _16 and push the mapped
// 16:16 value as two words, selector first; a value without a high word, an
// ordinal group or a null, is pushed as it is. So the blocks are
// (pszApp 2, pszPIF 2, hInf, flOpt), (hProps, lpszGroup 2, lpProps 2,
// cbProps, flOpt) twice, and (hProps, flOpt). The selector table gives the
// flat address of a mapped pointer back.
unsigned Application::PifMgrCall(unsigned ordinal, const unsigned *words)
{
    if (!pifmgr) pifmgr = new PifMgrState;
    if (!pifmgr) return 0;
    const auto flat = [&](unsigned selector, unsigned offset) {
        return selector ? selectors.Linear((selector << 16) | (offset & 0xffff)) : (offset & 0xffff);
    };
    unsigned a[5] = {};
    if (ordinal == Win98ShellPif::OpenProperties)
    {
        a[0] = flat(words[0], words[1]);
        a[1] = flat(words[2], words[3]);
        a[2] = words[4];
        a[3] = words[5];
    }
    else if (ordinal == Win98ShellPif::GetProperties || ordinal == Win98ShellPif::SetProperties)
    {
        a[0] = words[0];
        a[1] = flat(words[1], words[2]);
        a[2] = flat(words[3], words[4]);
        a[3] = words[5];
        a[4] = words[6];
    }
    else
    {
        a[0] = words[0];
        a[1] = words[1];
    }
    PifMgrEnvironment environment(*this);
    char text[400];
    switch (ordinal)
    {
    case Win98ShellPif::OpenProperties:
    {
        char application[260], pif[260] = {};
        snprintf(application, sizeof application, "%s", a[0] ? ReadString(a[0]) : "");
        if (a[1]) snprintf(pif, sizeof pif, "%s", ReadString(a[1]));
        unsigned index = 0;
        while (index < 16 && pifmgr->records[index].used) ++index;
        unsigned handle = 0;
        if (index < 16 && application[0] &&
            Win98PifMgr::Open(pifmgr->records[index], environment, application, pif, a[2], a[3]))
            handle = HandleBase + index * HandleStep;
        if (log)
        {
            snprintf(text, sizeof text, "PIFMGR open app=%s pif=%s inf=%x flags=%x handle=%04x record=%s flags=%04x",
                     application, pif, a[2], a[3], handle, handle ? pifmgr->records[index].pif : "",
                     handle ? pifmgr->records[index].flags : 0);
            log(text);
        }
        return handle;
    }
    case Win98ShellPif::GetProperties:
    case Win98ShellPif::SetProperties:
    {
        auto *record = PifMgrRecord(a[0]);
        const unsigned group = a[1], cb = a[3] & 0xffff, flags = a[4] & 0xffff;
        unsigned result = 0;
        // A group named by a string is a section of the file, the way
        // GETPROPERTIES and SETPROPERTIES take a far pointer in lpszGroup:
        // APPWIZ.CPL's MS-DOS configuration wizard writes "CONFIG SYS 4.0"
        // and "AUTOEXECBAT 4.0" through here.
        if (record && group >= 0x10000 && cb && cb <= Win98PifMgr::ImageCapacity)
        {
            char name[32] = {};
            snprintf(name, sizeof name, "%s", ReadString(group));
            unsigned char *buffer = new unsigned char[cb];
            if (buffer && name[0])
            {
                memset(buffer, 0, cb);
                if (ordinal == Win98ShellPif::GetProperties)
                {
                    result = Win98PifMgr::GetNamed(*record, name, buffer, cb);
                    if (result > cb) result = cb;
                    if (result && !Store(a[2], buffer, result)) result = 0;
                }
                else if (Fetch(a[2], buffer, cb))
                    result = Win98PifMgr::SetNamed(*record, environment, name, buffer, cb, flags);
            }
            delete[] buffer;
            if (log)
            {
                snprintf(text, sizeof text, "PIFMGR %s handle=%04x section=\"%s\" buffer=%08x cb=%u flags=%x result=%u",
                         ordinal == Win98ShellPif::GetProperties ? "get" : "set", a[0] & 0xffff, name, a[2], cb, flags, result);
                log(text);
            }
            return result;
        }
        if (record && group && group < 0x10000 && cb && cb <= Win98PifMgr::ImageCapacity)
        {
            unsigned char *buffer = new unsigned char[cb];
            if (buffer)
            {
                memset(buffer, 0, cb);
                if (ordinal == Win98ShellPif::GetProperties)
                {
                    result = Win98PifMgr::Get(*record, group, buffer, cb, flags);
                    if (result && !Store(a[2], buffer, result)) result = 0;
                }
                else if (Fetch(a[2], buffer, cb))
                    result = Win98PifMgr::Set(*record, environment, group, buffer, cb, flags);
                delete[] buffer;
            }
        }
        if (log)
        {
            snprintf(text, sizeof text, "PIFMGR %s handle=%04x group=%u buffer=%08x cb=%u flags=%x result=%u",
                     ordinal == Win98ShellPif::GetProperties ? "get" : "set", a[0] & 0xffff, group, a[2], cb, flags, result);
            log(text);
        }
        return result;
    }
    case Win98ShellPif::CloseProperties:
    {
        auto *record = PifMgrRecord(a[0]);
        const unsigned result = record ? Win98PifMgr::Close(*record, environment, a[1] & 0xffff) : 0;
        if (log)
        {
            snprintf(text, sizeof text, "PIFMGR close handle=%04x flags=%x known=%u", a[0] & 0xffff, a[1] & 0xffff, record != nullptr);
            log(text);
        }
        return result;
    }
    default:
        return 0;
    }
}

// The handle services the property pages use (src/x86_pif_pages.cpp): the
// same records and the same file store as the four forwarders, without the
// argument block of a 32-bit stub. Open is OPENPROPERTIES(pszApp, "", 0, 0),
// the way PIFPROPGETPAGES opens the file it was handed.
unsigned Application::PifMgrOpenHandle(const char *path)
{
    if (!pifmgr) pifmgr = new PifMgrState;
    if (!pifmgr || !path || !*path) return 0;
    unsigned index = 0;
    while (index < 16 && pifmgr->records[index].used) ++index;
    if (index >= 16) return 0;
    PifMgrEnvironment environment(*this);
    if (!Win98PifMgr::Open(pifmgr->records[index], environment, path, "", 0, 0)) return 0;
    return HandleBase + index * HandleStep;
}
unsigned Application::PifMgrGroup(unsigned handle, unsigned group, unsigned char *out, unsigned cb)
{
    auto *record = PifMgrRecord(handle);
    memset(out, 0, cb);
    return record ? Win98PifMgr::Get(*record, group, out, cb, 0) : 0;
}
unsigned Application::PifMgrSetGroup(unsigned handle, unsigned group, const unsigned char *in, unsigned cb)
{
    auto *record = PifMgrRecord(handle);
    if (!record) return 0;
    PifMgrEnvironment environment(*this);
    return Win98PifMgr::Set(*record, environment, group, in, cb, Win98PifMgr::SetNoFlush);
}
bool Application::PifMgrFlushHandle(unsigned handle)
{
    auto *record = PifMgrRecord(handle);
    if (!record) return false;
    PifMgrEnvironment environment(*this);
    return Win98PifMgr::Flush(*record, environment, false);
}
void Application::PifMgrCloseHandle(unsigned handle)
{
    auto *record = PifMgrRecord(handle);
    if (!record) return;
    PifMgrEnvironment environment(*this);
    Win98PifMgr::Close(*record, environment, 0);
}
unsigned Application::PifMgrSetNamed(unsigned handle, const char *name, const unsigned char *in, unsigned cb)
{
    auto *record = PifMgrRecord(handle);
    if (!record) return 0;
    PifMgrEnvironment environment(*this);
    return Win98PifMgr::SetNamed(*record, environment, name, in, cb, Win98PifMgr::SetNoFlush);
}
} // namespace X86
