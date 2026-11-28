/* SPDX-License-Identifier: GPL-2.0-or-later */
/* An owned window made topmost after its owner, for
   tools/test_owned_topmost_host.py. Two cases, each with a plain window P
   made first and an owner O and its owned popup W after it:

   A: O is made topmost (SetWindowPos HWND_TOPMOST), then W.
   B: O stays an ordinary window, W is made topmost.
   C: O is made topmost (W with it), then W is made non-topmost
      (HWND_NOTOPMOST), which takes O out of the band too.

   After each step the program writes down, for P, O and W, whether
   WS_EX_TOPMOST is set and the place in the order of the top-level windows
   (0 the first); C:\OWNTOP.TXT, one line per step. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API int SetWindowPos(U, U, int, int, int, int, U);
API U GetWindowLongA(U, int);
API U GetWindow(U, U);
API U GetDesktopWindow(void);
API int DestroyWindow(U);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

struct Class { U style, proc; int class_extra, window_extra; U instance, icon, cursor, brush; const char *menu, *name; };
static char text[2000];

static U __stdcall Procedure(U window, U message, U wparam, U lparam)
{
    return DefWindowProcA(window, message, wparam, lparam);
}
static int Place(U window)
{
    int n = 0;
    for (U h = GetWindow(GetDesktopWindow(), 5); h; h = GetWindow(h, 2), ++n) /* GW_CHILD, GW_HWNDNEXT */
        if (h == window)
            return n;
    return -1;
}
static void Note(const char *step, U plain, U owner, U owned)
{
    if (lstrlenA(text) < 1800)
        wsprintfA(text + lstrlenA(text), "%s P %u %d O %u %d W %u %d\r\n", step,
                  (GetWindowLongA(plain, -20) >> 3) & 1, Place(plain), (GetWindowLongA(owner, -20) >> 3) & 1,
                  Place(owner), (GetWindowLongA(owned, -20) >> 3) & 1, Place(owned));
}
static U Make(const char *title, U owner, int x)
{
    return CreateWindowExA(0, "OwnTop", title, owner ? 0x90c00000u : 0x10cf0000u, x, 60, 200, 150, owner, 0,
                           GetModuleHandleA(0), 0);
}
void __stdcall mainCRTStartup(void)
{
    struct Class c = {0, (U)Procedure, 0, 0, GetModuleHandleA(0), 0, 0, 6, 0, "OwnTop"};
    U written = 0, file, plain, owner, owned;
    RegisterClassA(&c);
    /* A */
    plain = Make("Plain A", 0, 20);
    owner = Make("Owner A", 0, 120);
    owned = Make("Owned A", owner, 220);
    Note("A made", plain, owner, owned);
    SetWindowPos(owner, ~0u, 0, 0, 0, 0, 0x13); /* HWND_TOPMOST, SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE */
    Note("A owner topmost", plain, owner, owned);
    SetWindowPos(plain, 0, 0, 0, 0, 0, 0x13);   /* HWND_TOP: P before the ordinary windows */
    Note("A plain to top", plain, owner, owned);
    SetWindowPos(owned, ~0u, 0, 0, 0, 0, 0x13);
    Note("A owned topmost", plain, owner, owned);
    DestroyWindow(owned);
    DestroyWindow(owner);
    DestroyWindow(plain);
    /* B */
    plain = Make("Plain B", 0, 20);
    owner = Make("Owner B", 0, 120);
    owned = Make("Owned B", owner, 220);
    Note("B made", plain, owner, owned);
    SetWindowPos(owned, ~0u, 0, 0, 0, 0, 0x13);
    Note("B owned topmost", plain, owner, owned);
    SetWindowPos(plain, 0, 0, 0, 0, 0, 0x13);
    Note("B plain to top", plain, owner, owned);
    DestroyWindow(owned);
    DestroyWindow(owner);
    DestroyWindow(plain);
    /* C */
    plain = Make("Plain C", 0, 20);
    owner = Make("Owner C", 0, 120);
    owned = Make("Owned C", owner, 220);
    SetWindowPos(owner, ~0u, 0, 0, 0, 0, 0x13);
    Note("C owner topmost", plain, owner, owned);
    SetWindowPos(owned, ~1u, 0, 0, 0, 0, 0x13); /* HWND_NOTOPMOST */
    Note("C owned not topmost", plain, owner, owned);
    file = CreateFileA("C:\\OWNTOP.TXT", 0x40000000u, 0, 0, 2, 0x80, 0);
    WriteFile(file, text, (U)lstrlenA(text), &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
