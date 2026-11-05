// SPDX-License-Identifier: GPL-2.0-or-later
// Native service adapter and resource loading for implemented COMDLG32 code.
#ifndef NEO98_RUNTIME_CONTENT
#include "../build/generated/notepad_program.h"
#endif
#include "x86_app.h"
#include "win98_file_dialog.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
// The handle of the native COMDLG32 module; src/x86_modules.cpp gives it out.
constexpr unsigned FindProcedureToken = 0x71000010, CommonInstance = 0x71ff0000;
} // namespace
const unsigned char *Application::CommonResource(unsigned type, unsigned id, unsigned &size)
{
    size = 0;
    // Keep both the file and the parsed PE for this process. Resource pointers
    // remain valid while a modeless Find/Replace dialog is alive.
    if (!common_dialog_read)
    {
        common_dialog_read = true;
        common_dialog_image = new Pe32::Image;
        if (!common_dialog_image) return nullptr;
#ifdef NEO98_RUNTIME_CONTENT
        unsigned handle = 0, status = 0, done = 0;
        if (!files.Open("C:\\WINDOWS\\SYSTEM\\COMDLG32.DLL", 1, 3, 3, 0, handle, status))
        {
            if (!files.Size(handle, common_dialog_size) && common_dialog_size &&
                common_dialog_size <= 16u * 1024 * 1024)
            {
                common_dialog_bytes = new unsigned char[common_dialog_size];
                if (common_dialog_bytes && !files.Read(handle, common_dialog_bytes, common_dialog_size, done) &&
                    done == common_dialog_size &&
                    common_dialog_image->Parse(common_dialog_bytes, common_dialog_size))
                    common_dialog_valid = true;
            }
            files.Close(handle);
        }
#else
        common_dialog_valid = common_dialog_image->Parse(OriginalCommonDialogs, sizeof OriginalCommonDialogs);
#endif
        if (!common_dialog_valid)
        {
            delete common_dialog_image; common_dialog_image = nullptr;
            delete[] common_dialog_bytes; common_dialog_bytes = nullptr;
            common_dialog_size = 0;
        }
    }
    return common_dialog_valid ? common_dialog_image->Resource(type, id, &size) : nullptr;
}
struct Application::FindEnvironment final : Win98Find::Environment
{
    Application &app;
    unsigned literals[8] = {}, count = 0;
    explicit FindEnvironment(Application &a) : app(a) {}
    ~FindEnvironment()
    {
        for (unsigned i = 0; i < count; ++i)
            app.Free(literals[i]);
    }
    bool Read(unsigned p, void *out, unsigned n) override { return app.Read(p, out, n); }
    bool Write(unsigned p, const void *data, unsigned n) override { return app.Store(p, data, n); }
    unsigned Resize(unsigned heap, unsigned p, unsigned n) override
    {
        if (heap != 1 || p)
            return 0;
        unsigned args[] = {64, n};
        return app.Api("LocalAlloc", args);
    }
    bool Free(unsigned heap, unsigned p) override { return heap == 1 && app.Free(p); }
    int Compare(unsigned, unsigned, unsigned, unsigned) override { return 0; }
    unsigned Hook(unsigned procedure, unsigned hwnd, unsigned message, unsigned wp, unsigned lp) override
    {
        // The hook of a 16-bit caller: WM_INITDIALOG names its own
        // FINDREPLACE, every other message goes through the 16-bit packing.
        if (app.Win16IsMark(procedure))
        {
            if (message == 0x110)
                if (const unsigned narrow = app.Win16FindNarrow(lp))
                    return app.Win16Invoke(app.Win16MarkPointer(procedure), hwnd, message, wp, narrow) & 0xffff;
            return app.Win16Deliver(procedure, hwnd, message, wp, lp);
        }
        unsigned args[] = {hwnd, message, wp, lp};
        return app.Call(procedure, args, 4);
    }
    unsigned Literal(const char *text) override
    {
        if (count == 8)
        {
            app.Fail("Find dialog literal capacity exceeded");
            return 0;
        }
        return literals[count++] = app.String(text);
    }
    unsigned Service(const char *name, const unsigned *a, unsigned) override
    {
        if (!strcmp(name, "SetDialogError"))
        {
            app.CommonDialogError(a[0]);
            return 0;
        }
        // A notification for a 16-bit caller carries the far pointer of its
        // FINDREPLACE.
        if (!strcmp(name, "SendMessageA"))
            if (const unsigned narrow = app.Win16FindNarrow(a[3]))
            {
                const unsigned b[] = {a[0], a[1], a[2], narrow};
                return app.Api(name, b);
            }
        if (!strcmp(name, "GetCompatibilityFlags"))
            return 0; // All current processes use the ordinary 32-bit notification path.
        if (!strcmp(name, "GetPropA") || !strcmp(name, "SetPropA") || !strcmp(name, "RemovePropA"))
        {
            auto *dialog = app.FindDialog(a[0]);
            if (!dialog || a[1] != 0xa000)
                return 0;
            unsigned old = dialog->find_data;
            if (!strcmp(name, "SetPropA"))
            {
                dialog->find_data = a[2];
                return 1;
            }
            if (!strcmp(name, "RemovePropA"))
                dialog->find_data = 0;
            return old;
        }
        if (!strcmp(name, "LoadStringA") && a[0] == CommonInstance)
        {
            unsigned size = 0;
            const unsigned char *data = app.CommonResource(6, a[1] / 16 + 1, size);
            unsigned at = 0;
            if (!data)
                return 0;
            for (unsigned i = 0; i < 16; ++i)
            {
                if (at + 2 > size)
                    return 0;
                unsigned n = data[at] | (unsigned)data[at + 1] << 8;
                at += 2;
                if (n > (size - at) / 2)
                    return 0;
                if (i == a[1] % 16)
                {
                    char text[256];
                    if (n >= sizeof text)
                        return 0;
                    for (unsigned j = 0; j < n; ++j)
                        text[j] = data[at + j * 2 + 1] ? '?' : data[at + j * 2];
                    text[n] = 0;
                    return app.PutString(a[2], text, a[3]);
                }
                at += n * 2;
            }
            return 0;
        }
        return app.Api(name, a);
    }
};
unsigned Application::FindProcedure(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    FindEnvironment environment(*this);
    return Win98Find::Dialog(environment, find_state, hwnd, message, wp, lp);
}
unsigned Application::FindApi(unsigned parameters, bool replace)
{
    if (!parameters)
    {
        CommonDialogError(2);
        return 0;
    }
    find_state.instance = CommonInstance;
    if (!find_state.help_message)
    {
        unsigned name = String("commdlg_help");
        find_state.help_message = Api("RegisterWindowMessageA", &name);
        Free(name);
    }
    if (!find_state.close_caption)
    {
        find_state.close_caption = Allocate(9);
        if (!find_state.close_caption)
        {
            CommonDialogError(9);
            return 0;
        }
    }
    FindEnvironment environment(*this);
    if (!Win98Find::Validate(environment, find_state, parameters, replace))
        return 0;
    unsigned flags = Read32(parameters + 12), length = 0;
    const unsigned char *data = nullptr;
    unsigned char *owned = nullptr;
    if (flags & 0x200)
    {
        unsigned module = Read32(parameters + 8), name = Read32(parameters + 36);
        if ((!module || module == image.Base()) && name < 65536)
            data = image.Resource(5, name, &length);
        if (!data)
        {
            CommonDialogError(6);
            return 0;
        }
    }
    else if (flags & 0x2000)
    {
        unsigned handle = Read32(parameters + 8);
        length = Api("GlobalSize", &handle);
        if (!length || length > 65536)
        {
            CommonDialogError(handle ? 8 : 4);
            return 0;
        }
        owned = new unsigned char[length];
        const unsigned pointer = owned ? Api("GlobalLock", &handle) : 0;
        if (pointer && Read(pointer, owned, length))
            data = owned;
        if (pointer) Api("GlobalUnlock", &handle);
    }
    else
        data = CommonResource(5, replace ? 1541 : 1540, length);
    if (!data)
    {
        delete[] owned;
        CommonDialogError(flags & 0x2000 ? 8 : 6);
        return 0;
    }
    unsigned context = environment.Resize(1, 0, 12);
    if (!context)
    {
        delete[] owned;
        CommonDialogError(9);
        return 0;
    }
    CommonDialogError(0);
    if (flags & 0x100)
        find_state.initial_hook = Read32(parameters + 32);
    unsigned values[] = {1, (unsigned)replace, parameters};
    Store(context, values, sizeof values);
    unsigned hwnd = CreateDialogTemplate(data, length, Read32(parameters + 4), FindProcedureToken, context);
    delete[] owned;
    if (!hwnd)
    {
        find_state.initial_hook = 0;
        Free(context);
    }
    return hwnd;
}
#include "x86_file_dialog.inc"
} // namespace X86
