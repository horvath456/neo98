// SPDX-License-Identifier: GPL-2.0-or-later
// The USER32 exports that were still missing from the export contract
// after the import census of 23 September 2026.
#include "x86_app.h"
#include <string.h>
namespace X86
{
unsigned Application::User32CompletionApi(ApiName name, const unsigned *a)
{
    switch (name.id)
    {
    // Ordinal 383 and 602 share their addresses with the A forms: USER32
    // exports as IsDialogMessage and IsDialogMessageA, and
    // as TranslateAccelerator and TranslateAcceleratorA.
    API_CASE("IsDialogMessage")
        return Api("IsDialogMessageA", a);
    API_CASE("TranslateAccelerator")
        return Api("TranslateAcceleratorA", a);
        // The wide form asks the A form for at most 104h bytes in a
    // buffer on its own stack and converts the answer with
    // MultiByteToWideChar(CP_ACP, 0, buffer, -1, lpszFileName, cchFileNameMax).
    // Its answer is the answer of that conversion, so it counts the
    // terminator, and its LastError is the one the conversion leaves. When
    // the A form fails, the original converts whatever its stack held; the
    // port starts from an empty buffer, so it converts the empty string.
    API_CASE("GetWindowModuleFileNameW")
    {
        const unsigned buffer = Allocate(0x104);
        if (!buffer)
        {
            Fail("GetWindowModuleFileNameW: no guest memory for the ANSI buffer");
            return 0;
        }
        const unsigned char zero[0x104] = {};
        Store(buffer, zero, sizeof zero);
        const unsigned query[3] = {a[0], buffer, 0x104};
        Api("GetWindowModuleFileNameA", query);
        const unsigned convert[6] = {0, 0, buffer, ~0u, a[1], a[2]};
        const unsigned result = Api("MultiByteToWideChar", convert);
        Free(buffer);
        return result;
    }
    // cbSize must be 16 and the resolution GMMP_USE_DISPLAY_POINTS;
    // otherwise the stub answers 57h in EAX, without SetLastError, so the
    // caller reads 87 points. Then CL 0 to USER with the point, the
    // buffer and the buffer size as a word; the answer is widened with its
    // sign.
    API_CASE("GetMouseMovePoints")
    {
        if (a[0] != 16 || a[4] != 1)
            return 0x57;
        int find[4] = {};
        if (!Read(a[1], find, sizeof find))
            return ~0u;
        User::MouseMovePoint wanted{find[0], find[1], unsigned(find[2]), 0}, points[64];
        const int capacity = int(short(a[3] & 0xffff));
        const int count = User::MouseMovePoints(wanted, points, capacity > 64 ? 64 : capacity);
        for (int i = 0; i < count; ++i)
        {
            const int entry[4] = {points[i].x, points[i].y, int(points[i].time), int(points[i].extra)};
            if (!Store(a[2] + unsigned(i) * 16, entry, sizeof entry))
                return ~0u;
        }
        return unsigned(count);
    }
    // is not a function a program can call. USER.EXE reaches it with
    // a far call during its own initialization, ES:ESI naming the shared USER
    // heap, and it leaves through RETF. A near call from a Win32 program
    // writes the program's ES:ESI into USER32's shared table pointer and
    // returns through a far frame it never pushed. NEO98 has no USER.EXE that
    // would make the call, so a call stops the process with this diagnosis.
    API_CASE("InitSharedTable")
        Fail("USER32!InitSharedTable is the initialization entry USER.EXE calls with a far call; "
             "a Win32 caller would corrupt USER's shared table (BFC040DA)");
        return 0;
    // is the 16-to-32 transition for a 32-bit window procedure. It
    // expects EBX to hold the 16-bit stack frame and leaves with LSS SP and
    // RETF into 16-bit code. The native port delivers every message itself,
    // so nothing arrives here; a direct call stops with this diagnosis.
    API_CASE("WNDPROC_CALLBACK")
        Fail("USER32!WNDPROC_CALLBACK is the 16-to-32 window procedure transition of USER.EXE "
             "and cannot be called from a Win32 program (BFC03B13)");
        return 0;
    default:
        break;
    }
    Fail(name);
    return 0;
}
} // namespace X86
