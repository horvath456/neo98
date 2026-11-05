// SPDX-License-Identifier: GPL-2.0-or-later
// Guest programs for tools/test_host_clipboard.py. One image; the file name it
// was started under says what it does, and its input and output are the files
// of the same name with.IN and.OUT beside it:
//
// CLIPGET writes what the clipboard holds: the formats in order, CF_TEXT,
// CF_OEMTEXT, CF_UNICODETEXT and CF_LOCALE as hex, the names of a
// CF_HDROP, a CF_DIB as hex and the "Preferred DropEffect".
// CLIPSET SetClipboardData(CF_TEXT) with the bytes of CLIPSET.IN.
// CLIPUNI SetClipboardData(CF_UNICODETEXT) with the bytes of CLIPUNI.IN.
// CLIPDIB SetClipboardData(CF_DIB) with the bytes of CLIPDIB.IN.
// CLIPLATE SetClipboardData(CF_TEXT, NULL) from a window that renders
// CLIPLATE.IN when WM_RENDERFORMAT asks; notes the request.
// CLIPDROP CF_HDROP with the names of CLIPDROP.IN (one per line) and
// "Preferred DropEffect" 5, as Explorer's Copy.
// CLIPCUT the same with 2, as Explorer's Cut.
typedef unsigned U;
typedef void *H;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleFileNameA(H, char *, U);
API H CreateFileA(const char *, U, U, void *, U, U, H);
API int ReadFile(H, void *, U, U *, void *);
API int WriteFile(H, const void *, U, U *, void *);
API int CloseHandle(H);
API H GlobalAlloc(U, U);
API void *GlobalLock(H);
API int GlobalUnlock(H);
API U GlobalSize(H);
API int OpenClipboard(H);
API int CloseClipboard(void);
API int EmptyClipboard(void);
API H GetClipboardData(U);
API H SetClipboardData(U, H);
API U EnumClipboardFormats(U);
API U RegisterClipboardFormatA(const char *);
API unsigned short RegisterClassA(const void *);
API H CreateWindowExA(U, const char *, const char *, U, int, int, int, int, H, H, H, void *);
API int GetMessageA(void *, H, U, U);
API int DispatchMessageA(const void *);
API int DefWindowProcA(H, U, U, U);
API U SetTimer(H, U, U, void *);
API void PostQuitMessage(int);
API H GetModuleHandleA(const char *);

// The compiler may call these for a structure it clears or copies.
void *memset(void *target, int value, U count)
{
    volatile unsigned char *p = target;
    while (count--) *p++ = (unsigned char)value;
    return target;
}
void *memcpy(void *target, const void *source, U count)
{
    volatile unsigned char *p = target;
    const unsigned char *q = source;
    while (count--) *p++ = *q++;
    return target;
}
static char path[300], out[65536], in[32768];
static U out_size, in_size;

static void put(const char *text) { while (*text && out_size < sizeof out) out[out_size++] = *text++; }
static void put_number(U value)
{
    char digits[12];
    int n = 0;
    do digits[n++] = (char)('0' + value % 10); while ((value /= 10) != 0);
    while (n && out_size < sizeof out) out[out_size++] = digits[--n];
}
static void put_hex(const unsigned char *bytes, U count)
{
    static const char digits[] = "0123456789abcdef";
    for (U i = 0; i < count && out_size + 2 < sizeof out; ++i) {
        out[out_size++] = digits[bytes[i] >> 4];
        out[out_size++] = digits[bytes[i] & 15];
    }
}
// The path of this image with another extension.
static const char *beside(const char *extension)
{
    static char name[300];
    U dot = 0, i = 0;
    for (; path[i]; ++i) { name[i] = path[i]; if (path[i] == '.') dot = i; }
    for (i = 0; extension[i]; ++i) name[dot + 1 + i] = extension[i];
    name[dot + 1 + i] = 0;
    return name;
}
static void read_input(void)
{
    H file = CreateFileA(beside("IN"), 0x80000000u, 1, 0, 3, 0, 0);
    if (file == (H)-1) return;
    ReadFile(file, in, sizeof in - 2, &in_size, 0);
    CloseHandle(file);
    in[in_size] = in[in_size + 1] = 0;
}
static void write_output(void)
{
    U done = 0;
    H file = CreateFileA(beside("OUT"), 0x40000000u, 0, 0, 2, 0, 0);
    if (file == (H)-1) return;
    WriteFile(file, out, out_size, &done, 0);
    CloseHandle(file);
}
static H block(const void *bytes, U count, U extra)
{
    H handle = GlobalAlloc(0x2042, count + extra);
    unsigned char *p = handle ? GlobalLock(handle) : 0;
    if (!p) return 0;
    for (U i = 0; i < count; ++i) p[i] = ((const unsigned char *)bytes)[i];
    GlobalUnlock(handle);
    return handle;
}
static U text_bytes(const unsigned char *p, U limit)
{
    U n = 0;
    while (n < limit && p[n]) ++n;
    return n;
}
static void get(void)
{
    const U effect = RegisterClipboardFormatA("Preferred DropEffect");
    if (!OpenClipboard(0)) { put("open failed\r\n"); return; }
    put("formats");
    for (U format = 0; (format = EnumClipboardFormats(format)) != 0;) {
        put(" ");
        if (format == effect) put("effect"); else put_number(format);
    }
    put("\r\n");
    static const struct { U format; const char *name; } kinds[] = {{1, "text "}, {7, "oem "}, {13, "unicode "}, {16, "locale "}};
    for (U i = 0; i < 4; ++i) {
        H handle = GetClipboardData(kinds[i].format);
        unsigned char *p = handle ? GlobalLock(handle) : 0;
        if (!p) continue;
        U size = GlobalSize(handle), count = size;
        if (kinds[i].format == 13) { count = 0; while (count + 1 < size && (p[count] || p[count + 1])) count += 2; }
        else if (kinds[i].format == 16) count = 4;
        else count = text_bytes(p, size);
        put(kinds[i].name); put_hex(p, count); put("\r\n");
        GlobalUnlock(handle);
    }
    H drop = GetClipboardData(15);
    unsigned char *p = drop ? GlobalLock(drop) : 0;
    if (p) {
        const U size = GlobalSize(drop), start = *(U *)p, wide = *(U *)(p + 16);
        put("dropfiles wide="); put_number(wide); put("\r\n");
        for (U at = start; !wide && at < size && p[at];) {
            const U n = text_bytes(p + at, size - at);
            put("drop "); put_hex(p + at, n); put("\r\n");
            at += n + 1;
        }
        GlobalUnlock(drop);
    }
    H dib = GetClipboardData(8);
    p = dib ? GlobalLock(dib) : 0;
    if (p) {
        U size = GlobalSize(dib), stride = ((*(U *)(p + 4) * *(unsigned short *)(p + 14) + 31) / 32) * 4;
        U used = *(U *)p + stride * *(U *)(p + 8);
        put("dib "); put_hex(p, used < size ? used : size); put("\r\n");
        GlobalUnlock(dib);
    }
    H preferred = GetClipboardData(effect);
    p = preferred ? GlobalLock(preferred) : 0;
    if (p) { put("effect "); put_number(*(U *)p); put("\r\n"); GlobalUnlock(preferred); }
    CloseClipboard();
    put("done\r\n");
}
static void set(U format)
{
    if (!OpenClipboard(0)) { put("open failed\r\n"); return; }
    EmptyClipboard();
    put(SetClipboardData(format, block(in, in_size, 2)) ? "set\r\n" : "set failed\r\n");
    CloseClipboard();
}
static void drop(U effect)
{
    // DROPFILES, then the names with a NUL each and a second one at the end.
    static unsigned char files[32768 + 24];
    U size = 20;
    files[0] = 20;
    for (U i = 0; i < in_size; ++i) {
        if (in[i] == '\r') continue;
        files[size++] = in[i] == '\n' ? 0 : (unsigned char)in[i];
    }
    if (files[size - 1]) files[size++] = 0;
    files[size++] = 0;
    if (!OpenClipboard(0)) { put("open failed\r\n"); return; }
    EmptyClipboard();
    put(SetClipboardData(15, block(files, size, 0)) ? "set\r\n" : "set failed\r\n");
    SetClipboardData(RegisterClipboardFormatA("Preferred DropEffect"), block(&effect, 4, 0));
    CloseClipboard();
}
static int __stdcall late_window(H window, U message, U wparam, U lparam)
{
    if (message == 0x305) { // WM_RENDERFORMAT
        put("render "); put_number(wparam); put("\r\n");
        if (wparam == 1) SetClipboardData(1, block(in, in_size, 2));
        return 0;
    }
    if (message == 0x113) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(window, message, wparam, lparam);
}
static void late(void)
{
    struct { U style; void *procedure; int a, b; H instance, icon, cursor, brush; const char *menu, *name; } kind = {0};
    kind.procedure = (void *)late_window;
    kind.instance = GetModuleHandleA(0);
    kind.name = "NEO98ClipLate";
    RegisterClassA(&kind);
    H window = CreateWindowExA(0, kind.name, "late", 0, 0, 0, 10, 10, 0, 0, kind.instance, 0);
    if (!window || !OpenClipboard(window)) { put("open failed\r\n"); return; }
    EmptyClipboard();
    SetClipboardData(1, 0);
    put("offered\r\n");
    CloseClipboard();
    put("closed\r\n");
    SetTimer(window, 1, 2500, 0);
    U message[8];
    while (GetMessageA(message, 0, 0, 0) > 0) DispatchMessageA(message);
}
// The name of this image without its folder, its extension and a digit at
// the end: the test starts CLIPGET1.EXE, CLIPGET2.EXE and so on.
static int named(const char *name)
{
    U end = 0, start = 0;
    for (U i = 0; path[i]; ++i) { if (path[i] == '\\') start = i + 1; if (path[i] == '.') end = i; }
    U i = 0;
    for (; name[i]; ++i) if ((path[start + i] & ~0x20) != name[i]) return 0;
    while (path[start + i] >= '0' && path[start + i] <= '9') ++i;
    return start + i == end;
}
void fixture_entry(void)
{
    GetModuleFileNameA(0, path, 260);
    read_input();
    if (named("CLIPGET")) get();
    else if (named("CLIPSET")) set(1);
    else if (named("CLIPUNI")) set(13);
    else if (named("CLIPDIB")) set(8);
    else if (named("CLIPLATE")) late();
    else if (named("CLIPDROP")) drop(5);
    else if (named("CLIPCUT")) drop(2);
    else put("unknown name\r\n");
    write_output();
    ExitProcess(0);
}
