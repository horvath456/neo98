// SPDX-License-Identifier: GPL-2.0-or-later
// Original COMDLG32 Find/Replace procedure. Services and memory belong to the adapter.
#include "win98_find.h"
namespace Win98Find
{
namespace
{
template <typename... Args> U Call(Environment &e, const char *name, Args... values)
{
    const U args[] = {U(values)..., 0};
    return e.Service(name, args, sizeof...(values));
}
U Control(Environment &e, U hwnd, U id) { return Call(e, "GetDlgItem", hwnd, id); }
U Half(Environment &e, U p)
{
    unsigned short n = 0;
    e.Read(p, &n, 2);
    return n;
}
void Error(Environment &e, U error) { Call(e, "SetDialogError", error); }
void Initialize(Environment &e, U hwnd, U p, bool replace)
{
    Call(e, "SetDlgItemTextA", hwnd, 0x480, e.Word(p + 16));
    Call(e, "SendMessageA", hwnd, 0x111, 0x3000480, 0);
    if (!(e.Word(p + 12) & 0x80))
    {
        U h = Control(e, hwnd, 0x40e);
        Call(e, "ShowWindow", h, 0);
        Call(e, "EnableWindow", h, 0);
    }
    for (U n = 0; n < 2; ++n)
    {
        U flags = e.Word(p + 12), hide = n ? 0x8000 : 0x10000, disable = n ? 0x800 : 0x1000;
        U id = n ? 0x411 : 0x410;
        if (flags & hide)
        {
            U h = Control(e, hwnd, id);
            Call(e, "ShowWindow", h, 0);
            Call(e, "EnableWindow", h, 0);
        }
        else if (flags & disable)
            Call(e, "EnableWindow", Control(e, hwnd, id), 0);
        Call(e, "CheckDlgButton", hwnd, id, (e.Word(p + 12) >> (n + 1)) & 1);
    }
    U flags = e.Word(p + 12);
    if (flags & 0x4000)
    {
        Call(e, "ShowWindow", Control(e, hwnd, 0x430), 0);
        for (U id = 0x420; id <= 0x421; ++id)
        {
            U h = Control(e, hwnd, id);
            Call(e, "ShowWindow", h, 0);
            Call(e, "EnableWindow", h, 0);
        }
    }
    else if (flags & 0x400)
        for (U id = 0x420; id <= 0x421; ++id)
            Call(e, "EnableWindow", Control(e, hwnd, id), 0);
    if (!replace)
        Call(e, "CheckRadioButton", hwnd, 0x420, 0x421, (e.Word(p + 12) & 1) | 0x420);
    else
    {
        Call(e, "SetDlgItemTextA", hwnd, 0x481, e.Word(p + 20));
        Call(e, "SendMessageA", hwnd, 0x111, 0x3000481, 0);
    }
}
void Gather(Environment &e, U hwnd, U p, U action, bool replace)
{
    e.Word(p + 12, e.Word(p + 12) & 0xffffffc0);
    if (Call(e, "IsDlgButtonChecked", hwnd, 0x410))
        e.Word(p + 12, e.Word(p + 12) | 2);
    if (Call(e, "IsDlgButtonChecked", hwnd, 0x411))
        e.Word(p + 12, e.Word(p + 12) | 4);
    e.Word(p + 12, e.Word(p + 12) | action);
    Call(e, "GetDlgItemTextA", hwnd, 0x480, e.Word(p + 16), Half(e, p + 24));
    if (!replace)
    {
        if (Call(e, "IsDlgButtonChecked", hwnd, 0x420))
            return;
    }
    else
        Call(e, "GetDlgItemTextA", hwnd, 0x481, e.Word(p + 20), Half(e, p + 26));
    e.Word(p + 12, e.Word(p + 12) | 1);
}
U Notify(Environment &e, State &s, U p)
{
    U flags = Call(e, "GetCompatibilityFlags", 0, ~3u);
    return Call(e, "SendMessageA", e.Word(p + 4), flags & 8 ? 0x340 : s.message, 0, p);
}
void Close(Environment &e, State &s, U hwnd, U p)
{
    e.Word(p + 12, (e.Word(p + 12) & 0xffffffc7) | 0x40);
    Notify(e, s, p);
    U compatibility = Call(e, "GetCompatibilityFlags", 0, ~3u);
    if ((compatibility & 8) && (e.Word(p + 12) & 0x100) && e.Word(p + 32))
        e.Hook(e.Word(p + 32), hwnd, 2, 0, 0);
    Call(e, "RemovePropA", hwnd, 0xa000);
    Call(e, "DestroyWindow", hwnd);
}
} // namespace
U Validate(Environment &e, State &s, U p, bool replace)
{
    if (!p)
        return 0;
    U error = 0;
    if (e.Word(p) != 40)
        error = 1;
    else if (!Call(e, "IsWindow", e.Word(p + 4)))
        error = 0xffff;
    else if (!e.Word(p + 16) || (replace && !e.Word(p + 20)) || !Half(e, p + 24))
        error = 0x4001;
    else
    {
        if (!(e.Word(p + 12) & 0x100))
            e.Word(p + 32, 0);
        else if (!e.Word(p + 32))
            error = 11;
        if (!error && replace && !Call(e, "LoadStringA", s.instance, 0x40d, s.close_caption, 9))
            error = 5;
        if (!error)
        {
            s.message = Call(e, "RegisterWindowMessageA", e.Literal("commdlg_FindReplace"));
            if (s.message)
                return 1;
            error = 12;
        }
    }
    Error(e, error);
    return 0;
}
U Dialog(Environment &e, State &s, U hwnd, U message, U wp, U lp)
{
    U context = Call(e, "GetPropA", hwnd, 0xa000), p = wp;
    if (context)
    {
        p = e.Word(context + 8);
        if (p && (e.Word(p + 12) & 0x100))
        {
            U result = e.Hook(e.Word(p + 32), hwnd, message, wp, lp);
            if (result)
                return result;
        }
    }
    else if (s.initial_hook && message != 0x110)
    {
        U result = e.Hook(s.initial_hook, hwnd, message, wp, lp);
        if (result)
            return result;
    }
    if (message == 0x10)
        Call(e, "SendMessageA", hwnd, 0x111, 2, 0);
    else if (message == 0x53 || message == 0x7b)
    {
        if (Call(e, "IsWindowEnabled", hwnd))
            Call(e, "WinHelpA", message == 0x53 ? e.Word(lp + 12) : wp, 0, message == 0x53 ? 12 : 10,
                 0x7fe11838);
    }
    else if (message == 0x110)
    {
        Call(e, "SetPropA", hwnd, 0xa000, lp);
        s.initial_hook = 0;
        U parameters = e.Word(lp + 8);
        Initialize(e, hwnd, parameters, e.Word(lp + 4) != 0);
        U result = 1;
        if (e.Word(parameters + 12) & 0x100)
            result = e.Hook(e.Word(parameters + 32), hwnd, 0x110, wp, parameters);
        if (result)
        {
            Call(e, "ShowWindow", hwnd, 1);
            Call(e, "UpdateWindow", hwnd);
        }
        return result;
    }
    else
    {
        if (message != 0x111 || !context || !p)
            return 0;
        U id = wp & 65535;
        if (id == 1)
        {
            Gather(e, hwnd, p, 8, e.Word(context + 4) != 0);
            Notify(e, s, p);
        }
        else if (id == 2 || id == 3)
        {
            Close(e, s, hwnd, p);
            e.Free(1, context);
        }
        else if (id == 0x400 || id == 0x401)
        {
            // Original compares the complete WPARAM for this action choice.
            Gather(e, hwnd, p, wp == 0x400 ? 0x10 : 0x20, e.Word(context + 4) != 0);
            if (Notify(e, s, p) == 1)
                Call(e, "SetWindowTextA", Control(e, hwnd, 2), s.close_caption);
        }
        else if (id == 0x480)
        {
            if ((wp >> 16) == 0x300)
            {
                U length = Call(e, "SendDlgItemMessageA", hwnd, 0x480, 0xe, 0, 0);
                Call(e, "EnableWindow", Control(e, hwnd, 1), length);
                if (e.Word(context + 4) == 1)
                    for (U button = 0x400; button <= 0x401; ++button)
                        Call(e, "EnableWindow", Control(e, hwnd, button), length);
                length = Call(e, "SendDlgItemMessageA", hwnd, 0x480, 0xe, 0, 0);
                Call(e, "EnableWindow", Control(e, hwnd, 1), length);
            }
        }
        else if (id == 0x40e)
        {
            if (s.help_message && e.Word(p + 4))
                Call(e, "SendMessageA", e.Word(p + 4), s.help_message, hwnd, p);
        }
        else
            return 0;
    }
    return 1;
}
} // namespace Win98Find
