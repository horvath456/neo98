/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// DLL-local thread state is destroyed by DLL_THREAD_DETACH.
struct Local {
    int id;
    int *values;
    ~Local() { if (values[0] < 2) values[++values[0]] = id; }
};
extern "C" __declspec(dllexport) void Touch(int *values)
{
    thread_local Local first{1, values}, second{2, values};
    (void)first.id;
    (void)second.id;
}
