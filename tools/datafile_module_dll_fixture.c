/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The DLL the datafile guest maps. Its only job is to carry a resource and an
   import of a DLL that does not exist, so that an ordinary load must fail. */
typedef unsigned U;
__declspec(dllimport) U __stdcall NoSuchAnswer(U);
__declspec(dllexport) U __stdcall Value(U a) { return NoSuchAnswer(a) + 1; }
int __stdcall DllMain(U instance, U reason, U reserved)
{
    (void)instance;
    (void)reason;
    (void)reserved;
    return 1;
}
