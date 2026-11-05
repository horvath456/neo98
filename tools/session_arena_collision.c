// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
#define EXPORT __declspec(dllexport)
#ifndef COLLISION_VALUE
#define COLLISION_VALUE 0x13579bdfu
#endif
EXPORT U CollisionBytes(void) { return COLLISION_VALUE; }
API U DisableThreadLibraryCalls(U);
EXPORT int __stdcall DllMain(U module, U reason, U reserved)
{
    (void)reserved;
    if (reason == 1) DisableThreadLibraryCalls(module);
    return 1;
}
