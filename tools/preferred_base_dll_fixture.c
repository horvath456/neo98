/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A DLL linked at 10000000h, the base the linker gives a DLL by default.
   PREF_DLL_ID separates the two copies the test loads into one process. The
   exported queries report values that only come out right when the loader
   applied the relocations of the base it actually used. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall

static U instance_seen, attach_count;
static U counter = 0x5a5a0000u + PREF_DLL_ID;
/* A pointer in writable data needs a relocation entry; its value proves that
   the loader rewrote the image for the base it chose. */
static U *const self = &counter;

__declspec(dllexport) U __stdcall Value(void) { return 0xa5a50000u + PREF_DLL_ID; }
__declspec(dllexport) U __stdcall PointerValue(void) { return (U)self; }
__declspec(dllexport) U __stdcall CounterValue(void) { return *self; }
__declspec(dllexport) U __stdcall Instance(void) { return instance_seen; }
__declspec(dllexport) U __stdcall Attachments(void) { return attach_count; }

U __stdcall DllMain(U module, U reason, U reserved)
{
    (void)reserved;
    if (reason == 1)
    {
        instance_seen = module;
        ++attach_count;
    }
    return 1;
}
