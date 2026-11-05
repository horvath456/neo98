/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include <windows.h>
#include <stdio.h>
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL %d\n", __LINE__); } } while (0)
int main(void)
{
    char path[MAX_PATH];
    GetTempPathA(MAX_PATH - 16, path);
    lstrcatA(path, "HEAPDLL.DLL");
    HRSRC resource = FindResourceA(NULL, MAKEINTRESOURCE(1), RT_RCDATA);
    HGLOBAL loaded = resource ? LoadResource(NULL, resource) : NULL;
    const void *data = loaded ? LockResource(loaded) : NULL;
    DWORD size = resource ? SizeofResource(NULL, resource) : 0, written = 0;
    if (!data || !size) return 1;
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 2;
    CHECK(WriteFile(file, data, size, &written, NULL) && written == size);
    CloseHandle(file);
    for (int i = 0; i < 10; ++i)
    {
        HMODULE dll = LoadLibraryA(path);
        CHECK(dll != NULL);
        if (!dll) break;
        void *(*make)(void) = (void *(*)(void))GetProcAddress(dll, "MakeBlock");
        void (*release)(void *) = (void (*)(void *))GetProcAddress(dll, "FreeBlock");
        CHECK(make && release);
        if (!make || !release) { FreeLibrary(dll); break; }
        unsigned char *p = make(), *retained = make();
        CHECK(p && retained && p[0] == 42 && p[999] == 7);
        if (!p || !retained) { FreeLibrary(dll); break; }
        release(p);
        MEMORY_BASIC_INFORMATION info;
        CHECK(VirtualQuery(p, &info, sizeof info) == sizeof info && info.State == MEM_COMMIT);
        CHECK(FreeLibrary(dll));
        CHECK(VirtualQuery(p, &info, sizeof info) == sizeof info && info.State == MEM_FREE);
        CHECK(VirtualQuery(retained, &info, sizeof info) == sizeof info && info.State == MEM_FREE);
    }
    DeleteFileA(path);
    printf("HEAP_UNLOAD checks=%d failures=%d\n", checks, failures);
    return failures;
}
