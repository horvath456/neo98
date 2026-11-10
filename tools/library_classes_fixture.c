// SPDX-License-Identifier: GPL-2.0-or-later
// Own PE32 program and DLL for tools/test_library_classes.py, not Windows
// binaries. Built twice: with LIBRARY the DLL that registers a window class
// for itself, without it the program that loads the DLL twice.
typedef struct { unsigned style; void *procedure; int class_extra, window_extra; void *instance, *icon, *cursor, *brush;
                 const char *menu, *name; } Class;
__declspec(dllimport) unsigned short __stdcall RegisterClassA(const Class *);
__declspec(dllimport) int __stdcall DefWindowProcA(void *, unsigned, unsigned, int);
static int __stdcall Procedure(void *window, unsigned message, unsigned w, int l) { return DefWindowProcA(window, message, w, l); }
static unsigned short Register(void *instance, const char *name)
{
    Class c = {0};
    c.procedure = Procedure; c.instance = instance; c.name = name;
    return RegisterClassA(&c);
}
#ifdef LIBRARY
static void *self;
int __stdcall DllMain(void *instance, unsigned reason, void *reserved) { (void)reserved; if (reason == 1) self = instance; return 1; }
__declspec(dllexport) unsigned __stdcall RegisterLibraryClass(void) { return Register(self, "Neo98LibraryClass"); }
#else
__declspec(dllimport) void *__stdcall LoadLibraryA(const char *);
__declspec(dllimport) int __stdcall FreeLibrary(void *);
__declspec(dllimport) void *__stdcall GetProcAddress(void *, const char *);
__declspec(dllimport) void *__stdcall GetModuleHandleA(const char *);
__declspec(dllimport) void __stdcall ExitProcess(unsigned);
void mainCRTStartup(void)
{
    typedef unsigned (__stdcall *Entry)(void);
    void *program = GetModuleHandleA(0);
    // A class of the program itself outlives every library.
    if (!Register(program, "Neo98ProgramClass")) ExitProcess(20);
    for (unsigned round = 0; round < 3; ++round)
    {
        void *library = LoadLibraryA("CLASSLIB.DLL");
        if (!library) ExitProcess(30 + round);
        Entry entry = (Entry)GetProcAddress(library, "_RegisterLibraryClass@0");
        if (!entry) ExitProcess(40 + round);
        // Windows 98 took the class away with the library, so each load
        // registers it again; within one load the second attempt fails.
        if (!entry()) ExitProcess(50 + round);
        if (entry()) ExitProcess(60 + round);
        if (!FreeLibrary(library)) ExitProcess(70 + round);
    }
    if (Register(program, "Neo98ProgramClass")) ExitProcess(21);
    ExitProcess(7);
}
#endif
