// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_arrays.h"
namespace Win98Find
{
using U = unsigned;
struct State
{
    U instance = 0, message = 0, help_message = 0, initial_hook = 0, close_caption = 0;
};
struct Environment : Win98Arrays::Memory
{
    virtual U Service(const char *name, const U *args, U count) = 0;
    virtual U Hook(U procedure, U hwnd, U message, U wp, U lp) = 0;
    virtual U Literal(const char *) = 0;
};
// The Find and Replace dialogs of COMDLG32, with their helpers.
U Validate(Environment &, State &, U parameters, bool replace);
U Dialog(Environment &, State &, U hwnd, U message, U wp, U lp);
} // namespace Win98Find
