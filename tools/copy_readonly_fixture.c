/* SPDX-License-Identifier: GPL-2.0-or-later */
/* CopyFile of a read-only file: the copy takes FILE_ATTRIBUTE_READONLY and
   is still filled through the handle that created it.
   The Internet Explorer 5 part of the Office XP System Files Update copies
   its cabinets from the CD this way. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U ReadFile(U, void *, U, U *, U);
API U CloseHandle(U);
API U CopyFileA(const char *, const char *, U);
API U GetFileAttributesA(const char *);
API U SetFileAttributesA(const char *, U);
API U DeleteFileA(const char *);
API void ExitProcess(U);
static const char source[] = "C:\\ROSOURCE.TXT", target[] = "C:\\ROTARGET.TXT";
static void clean(void)
{
    SetFileAttributesA(source, 0x80); DeleteFileA(source);
    SetFileAttributesA(target, 0x80); DeleteFileA(target);
}
void mainCRTStartup(void)
{
    static const char text[] = "read-only cabinet";
    char back[32] = {0};
    U done = 0;
    clean();
    U h = CreateFileA(source, 0x40000000, 0, 0, 2, 0x80, 0);
    if (h == ~0u || !WriteFile(h, text, sizeof text, &done, 0) || done != sizeof text) ExitProcess(1);
    CloseHandle(h);
    if (!SetFileAttributesA(source, 0x21)) ExitProcess(2);
    if (!CopyFileA(source, target, 0)) { clean(); ExitProcess(3); }
    if ((GetFileAttributesA(target) & 0x1f) != 1) { clean(); ExitProcess(4); }
    h = CreateFileA(target, 0x80000000, 1, 0, 3, 0x80, 0);
    if (h == ~0u || !ReadFile(h, back, sizeof back, &done, 0) || done != sizeof text) { clean(); ExitProcess(5); }
    CloseHandle(h);
    for (U i = 0; i < sizeof text; ++i) if (back[i] != text[i]) { clean(); ExitProcess(6); }
    /* A read-only target refuses a second copy over it. */
    if (CopyFileA(source, target, 0)) { clean(); ExitProcess(7); }
    clean();
    ExitProcess(0);
}
