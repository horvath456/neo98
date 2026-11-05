/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The additional imported exports, seen from
 * an x86 guest. One counter block, one error code per failing check, so a
 * failure names the check that failed.
 *
 * tools/test_imports2_guest.py builds and runs it. */
typedef unsigned U;
typedef struct { int left, top, right, bottom; } Rect;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void *VirtualAlloc(void *, U, U, U);
API U GetLastError(void);
API void SetLastError(U);
API U CreateCompatibleDC(U);
API U DeleteDC(U);
API U CreateDIBSection(U, const void *, U, void **, U, U);
API U SelectObject(U, U);
API U DeleteObject(U);
API U CreateRectRgn(int, int, int, int);
API U SetRectRgn(U, int, int, int, int);
API U GetRgnBox(U, Rect *);
API U CombineRgn(U, U, U, U);
API U SelectClipRgn(U, U);
API U GetClipBox(U, Rect *);
API U SetViewportOrgEx(U, int, int, void *);
API U ScrollDC(U, int, int, const Rect *, const Rect *, U, Rect *);
API U EnumObjects(U, int, void *, U);
API U AddFontResourceA(const char *);
API U RemoveFontResourceA(const char *);
API U CreateScalableFontResourceA(U, const char *, const char *, const char *);
API U CreateColorSpaceA(const void *);
API U DeleteColorSpace(U);
API U GetLogColorSpaceA(U, void *, U);
API U SetColorSpace(U, U);
API U GetColorSpace(U);
API U SetICMProfileA(U, const char *);
API void *GetModuleHandleA(const char *);
API void *GetProcAddress(void *, const char *);
API U DriverCallback(U, U, U, U, U, U, U);
API U waveInGetErrorTextA(U, char *, U);
API U midiInGetErrorTextA(U, char *, U);
API U VkKeyScanA(U);
API U VkKeyScanExA(U, U);
API U GetKeyboardLayout(U);
API void *CreateEventA(void *, U, U, const char *);
API U WaitForSingleObject(void *, U);
API U CloseHandle(void *);

#define RECORDS 0x30000000u
enum { R_CHECKS = 0, R_BAD, R_LAST_FAIL, R_COUNTERS };
static U *records;
static U checks, bad, last_fail;

static void ok(int condition, U id)
{
    ++checks;
    if (!condition)
    {
        ++bad;
        if (!last_fail) last_fail = id;
    }
}
static int same(const Rect *r, int l, int t, int right, int b)
{
    return r->left == l && r->top == t && r->right == right && r->bottom == b;
}

static U *pixels;
static U dc, bitmap, previous;
static void paint(void)
{
    U y, x;
    for (y = 0; y < 32; ++y)
        for (x = 0; x < 32; ++x)
            pixels[y * 32 + x] = y * 32 + x;
}
/* Every cell of the 32 by 32 surface either still carries its own number or the
 * number of the cell the copy brought, which is the one (dx,dy) back. */
static void copied(Rect area, int dx, int dy, U id)
{
    int y, x;
    for (y = 0; y < 32; ++y)
        for (x = 0; x < 32; ++x)
        {
            U want = (U)(y * 32 + x);
            if (x >= area.left && x < area.right && y >= area.top && y < area.bottom)
                want = (U)((y - dy) * 32 + (x - dx));
            ok(pixels[y * 32 + x] == want, id);
        }
}

/* EnumObjects: the callback records what it was handed and can stop the walk. */
static U seen, stop_after, kind;
static U first_style, first_colour, last_style, last_colour, last_hatch, foreign_lparam;
static int __stdcall object_callback(U *record, U lparam)
{
    if (lparam != 0x1234abcd) foreign_lparam = 1;
    if (!seen) { first_style = record[0]; first_colour = kind == 1 ? record[3] : record[1]; }
    ++seen;
    last_style = record[0];
    last_colour = kind == 1 ? record[3] : record[1];
    last_hatch = kind == 1 ? 0 : record[2];
    return seen == stop_after ? 0 : (int)(seen + 100);
}

static void enum_objects(void)
{
    /* Four colours, so the pen walk is five styles by four and the brush walk
     * one solid pass and six hatch passes by four. */
    int header[14] = {40, 8, -8, 0x080001, 0, 64, 0, 0, 4, 0,
                      0x00000000, 0x00ff0000, 0x0000ff00, 0x000000ff};
    int direct[10] = {40, 8, -8, 0x200001, 0, 256, 0, 0, 0, 0};
    U *bits = 0;
    U memory = CreateCompatibleDC(0);
    U palettized = CreateDIBSection(memory, header, 0, (void **)&bits, 0, 0);
    U plain = CreateDIBSection(memory, direct, 0, (void **)&bits, 0, 0);
    U old;
    ok(memory && palettized && plain, 9200);
    if (!memory || !palettized || !plain) return;

    /* The device context, the object type and the callback are all checked
     * before anything is enumerated. */
    SetLastError(0);
    ok(!EnumObjects(0, 1, (void *)object_callback, 0) && GetLastError() == 6, 9201);
    old = SelectObject(memory, plain);
    ok(old != 0, 9202);
    SetLastError(0);
    ok(!EnumObjects(memory, 0, (void *)object_callback, 0) && GetLastError() == 87, 9203);
    SetLastError(0);
    ok(!EnumObjects(memory, 3, (void *)object_callback, 0) && GetLastError() == 87, 9204);
    SetLastError(0);
    ok(!EnumObjects(memory, 1, 0, 0) && GetLastError() == 87, 9205);

    /* A surface deeper than eight bits has no colour table, so DIBENG walks
     * nothing and the answer is zero. */
    seen = 0; stop_after = 0; kind = 1; foreign_lparam = 0;
    ok(!EnumObjects(memory, 1, (void *)object_callback, 0x1234abcd), 9206);
    ok(seen == 0, 9207);

    ok(SelectObject(memory, palettized) == plain, 9210);
    seen = 0; stop_after = 0; kind = 1; foreign_lparam = 0;
    ok(EnumObjects(memory, 1, (void *)object_callback, 0x1234abcd) == 120, 9211);
    ok(seen == 20, 9212);
    ok(!foreign_lparam, 9213);
    ok(first_style == 0 && first_colour == 0, 9214);
    ok(last_style == 4 && last_colour == 0x00ff0000, 9215);

    seen = 0; stop_after = 0; kind = 2; foreign_lparam = 0;
    ok(EnumObjects(memory, 2, (void *)object_callback, 0x1234abcd) == 128, 9220);
    ok(seen == 28, 9221);
    ok(first_style == 0 && first_colour == 0 && last_hatch == 0, 9222);
    ok(last_style == 2 && last_colour == 0x00ff0000, 9223);

    /* A callback that answers zero stops the walk, and zero is the answer. */
    seen = 0; stop_after = 6; kind = 1;
    ok(!EnumObjects(memory, 1, (void *)object_callback, 0x1234abcd), 9230);
    ok(seen == 6, 9231);

    ok(SelectObject(memory, old) == palettized, 9240);
    ok(DeleteObject(palettized) && DeleteObject(plain) && DeleteDC(memory), 9241);
}

/* Font failures and the colour space family with the Win98 SE ICM module. */
static void fonts_and_colour(void)
{
    U dc = CreateCompatibleDC(0);
    char buffer[64];
    U space[16];
    U i;
    ok(dc != 0, 9300);
    for (i = 0; i < 16; ++i) space[i] = 0;
    space[0] = 0x50534f43;            /* 'PSOC', lcsSignature */
    space[1] = 0x400;                 /* lcsVersion */
    space[2] = sizeof space;

    SetLastError(0x4711);
    ok(!AddFontResourceA("NOSUCH.FON") && GetLastError() == 0x4711, 9301);
    ok(!RemoveFontResourceA("NOSUCH.FON") && GetLastError() == 0x4711, 9302);
    ok(!CreateScalableFontResourceA(0, "NOSUCH.FOT", "NOSUCH.TTF", 0), 9303);

    SetLastError(0);
    ok(!CreateColorSpaceA(space) && GetLastError() == 87, 9310);
    SetLastError(0);
    ok(!DeleteColorSpace(0) && GetLastError() == 87, 9311);
    SetLastError(0);
    ok(!GetLogColorSpaceA(0, buffer, sizeof buffer) && GetLastError() == 87, 9312);
    SetLastError(0);
    ok(!SetColorSpace(0, 0) && GetLastError() == 6, 9313);
    SetLastError(0);
    ok(!SetColorSpace(dc, 0) && GetLastError() == 87, 9314);
    SetLastError(0);
    ok(!GetColorSpace(0) && GetLastError() == 6, 9315);
    SetLastError(0);
    ok(GetColorSpace(dc) != 0 && GetLastError() == 0, 9316);

    /* SetICMProfileA does not ask about colour management. It wants a name it
     * can read and a file that is there; a bare name is looked for in the
     * COLOR folder of the Windows directory, which this tree has not. */
    SetLastError(0);
    ok(!SetICMProfileA(0, "X.ICM") && GetLastError() == 6, 9320);
    SetLastError(0);
    ok(!SetICMProfileA(dc, 0) && GetLastError() == 87, 9321);
    SetLastError(0);
    ok(!SetICMProfileA(dc, "NOSUCH.ICM") && GetLastError() == 2, 9322);
    SetLastError(0);
    ok(SetICMProfileA(dc, "C:\\WINDOWS\\CALC.EXE") == 1, 9323);
    ok(DeleteDC(dc), 9324);
}

/* The five nameless printer bridges, ordinals 100 to 104. Four of them answer
 * minus one while no printer driver can be loaded; ordinal 100 is the
 * trampoline into such a driver and is not called here, because it would stop
 * the run with its diagnosis. */
typedef U (__stdcall *Bridge4)(U, U, U, U);
typedef U (__stdcall *Bridge5)(U, U, U, U, U);
typedef U (__stdcall *Bridge7)(U, U, U, U, U, U, U);
static void printer_bridges(void)
{
    void *gdi = GetModuleHandleA("GDI32.DLL");
    Bridge4 ordinal101, ordinal103;
    Bridge7 ordinal102;
    Bridge5 ordinal104;
    ok(gdi != 0, 9400);
    if (!gdi) return;
    ordinal101 = (Bridge4)GetProcAddress(gdi, (const char *)101);
    ordinal102 = (Bridge7)GetProcAddress(gdi, (const char *)102);
    ordinal103 = (Bridge4)GetProcAddress(gdi, (const char *)103);
    ordinal104 = (Bridge5)GetProcAddress(gdi, (const char *)104);
    ok(ordinal101 && ordinal102 && ordinal103 && ordinal104, 9401);
    if (!ordinal101 || !ordinal102 || !ordinal103 || !ordinal104) return;
    ok(ordinal101(0, 0, 0, 0) == 0xffffffff, 9402);
    ok(ordinal102(0, 0, 0, 0, 0, 0, 0) == 0xffffffff, 9403);
    ok(ordinal103(0, 0, 0, 0) == 0xffffffff, 9404);
    ok(ordinal104(0, 0, 0, 0, 0) == 0xffffffff, 9405);
    ok(GetProcAddress(gdi, (const char *)100) != 0, 9406);
}

/* WINMM: the way back from a driver to the program, and the two error texts
 * that share their worker with the output forms. */
static U driver_seen, driver_args[5];
static U __stdcall driver_proc(U device, U message, U user, U one, U two)
{
    ++driver_seen;
    driver_args[0] = device; driver_args[1] = message; driver_args[2] = user;
    driver_args[3] = one; driver_args[4] = two;
    return 0x1234;
}
static void winmm_callbacks(void)
{
    char text[128];
    void *event;
    U i;
    /* DCB_FUNCTION calls the procedure with five arguments and answers TRUE,
     * whatever the procedure answered. */
    driver_seen = 0;
    ok(DriverCallback((U)driver_proc, 3, 11, 22, 33, 44, 55) == 1, 9500);
    ok(driver_seen == 1, 9501);
    ok(driver_args[0] == 11 && driver_args[1] == 22 && driver_args[2] == 33 &&
       driver_args[3] == 44 && driver_args[4] == 55, 9502);
    /* Every flag the switch does not name answers FALSE and does nothing. */
    driver_seen = 0;
    ok(!DriverCallback((U)driver_proc, 0, 0, 0, 0, 0, 0), 9503);
    ok(!DriverCallback((U)driver_proc, 4, 0, 0, 0, 0, 0), 9504);
    ok(!DriverCallback((U)driver_proc, 6, 0, 0, 0, 0, 0), 9505);
    ok(driver_seen == 0, 9506);
    /* DCB_EVENT sets the event and answers what SetEvent answered. */
    event = CreateEventA(0, 1, 0, 0);
    ok(event != 0, 9507);
    if (event)
    {
        ok(WaitForSingleObject(event, 0) == 0x102, 9508);
        ok(DriverCallback((U)event, 5, 0, 0, 0, 0, 0) == 1, 9509);
        ok(WaitForSingleObject(event, 0) == 0, 9510);
        ok(CloseHandle(event) != 0, 9511);
    }
    /* The input error texts answer out of the same block as the output ones. */
    for (i = 0; i < 128; ++i) text[i] = 0;
    ok(waveInGetErrorTextA(2, text, sizeof text) == 0 && text[0], 9520);
    ok(waveInGetErrorTextA(64, text, sizeof text) == 9, 9521);
    ok(waveInGetErrorTextA(32, text, sizeof text) == 0, 9522);
    ok(midiInGetErrorTextA(2, text, sizeof text) == 0 && text[0], 9523);
    ok(midiInGetErrorTextA(32, text, sizeof text) == 9, 9524);
    ok(midiInGetErrorTextA(64, text, sizeof text) == 0, 9525);

    /* VkKeyScanExA is VkKeyScanA with the layout named instead of implied. */
    {
        U layout = GetKeyboardLayout(0);
        ok(layout != 0, 9530);
        ok(VkKeyScanExA('A', layout) == VkKeyScanA('A'), 9531);
        ok(VkKeyScanExA('z', layout) == VkKeyScanA('z'), 9532);
        ok(VkKeyScanExA(0, layout) == VkKeyScanA(0), 9533);
        /* A layout of another language is not installed here. */
        ok(VkKeyScanExA('A', 0x12345678) == 0xffff, 9534);
    }
}

static void scroll_dc(void)
{
    Rect update, box, scroll, clip;
    U region = CreateRectRgn(0, 0, 1, 1), complex_region, band;
    int header[10] = {40, 32, -32, 0x200001, 0, 4096, 0, 0, 0, 0};
    Rect blit;
    dc = CreateCompatibleDC(0);
    bitmap = CreateDIBSection(dc, header, 0, (void **)&pixels, 0, 0);
    ok(dc && bitmap && pixels && region, 9000);
    if (!dc || !bitmap || !pixels || !region) return;
    previous = SelectObject(dc, bitmap);
    ok(previous != 0, 9001);
    ok(GetClipBox(dc, &box) == 2 && same(&box, 0, 0, 32, 32), 9002);

    /* A handle that is no device context, and an update region that is no
     * region: USER leaves through the parameter-validation path. */
    SetLastError(0);
    ok(!ScrollDC(0, 0, 4, 0, 0, 0, 0) && GetLastError() == 6, 9003);
    SetLastError(0);
    ok(!ScrollDC(dc, 0, 4, 0, 0, bitmap, 0) && GetLastError() == 6, 9004);

    /* Straight down. The strip the pixels left behind is the update area, and
     * the copy reaches below the scroll rectangle because the destination is
     * only cut by the clip box. */
    paint();
    scroll.left = 4; scroll.top = 4; scroll.right = 20; scroll.bottom = 20;
    update.left = update.top = update.right = update.bottom = -1;
    ok(ScrollDC(dc, 0, 4, &scroll, 0, region, &update) == 1, 9010);
    ok(same(&update, 4, 4, 20, 8), 9011);
    ok(GetRgnBox(region, &box) == 2 && same(&box, 4, 4, 20, 8), 9012);
    blit.left = 4; blit.top = 8; blit.right = 20; blit.bottom = 24;
    copied(blit, 0, 4, 9013);

    /* Straight right. */
    paint();
    ok(ScrollDC(dc, 3, 0, &scroll, 0, region, &update) == 1, 9020);
    ok(same(&update, 4, 4, 7, 20), 9021);
    blit.left = 7; blit.top = 4; blit.right = 23; blit.bottom = 20;
    copied(blit, 3, 0, 9022);

    /* Diagonal: the update region is complex, but the public ScrollDC
     * return value is still BOOL. */
    paint();
    ok(ScrollDC(dc, 3, 4, &scroll, 0, region, &update) == 1, 9030);
    ok(same(&update, 4, 4, 20, 20), 9031);
    ok(GetRgnBox(region, &box) == 3 && same(&box, 4, 4, 20, 20), 9032);
    blit.left = 7; blit.top = 8; blit.right = 23; blit.bottom = 24;
    copied(blit, 3, 4, 9033);

    /* Far enough that source and destination do not meet: the whole scroll
     * rectangle is owed. */
    paint();
    scroll.right = 12; scroll.bottom = 12;
    ok(ScrollDC(dc, 0, 20, &scroll, 0, region, &update) == 1, 9040);
    ok(same(&update, 4, 4, 12, 12), 9041);
    blit.left = 4; blit.top = 24; blit.right = 12; blit.bottom = 32;
    copied(blit, 0, 20, 9042);

    /* A clip rectangle cuts the clip box first, so it bounds the copy as well
     * as the update area. */
    paint();
    scroll.right = 20; scroll.bottom = 20;
    clip.left = 0; clip.top = 0; clip.right = 32; clip.bottom = 16;
    ok(ScrollDC(dc, 0, 4, &scroll, &clip, region, &update) == 1, 9050);
    ok(same(&update, 4, 4, 20, 8), 9051);
    blit.left = 4; blit.top = 8; blit.right = 20; blit.bottom = 16;
    copied(blit, 0, 4, 9052);

    /* A clip rectangle beside the clip box: nothing moves, both outputs are
     * cleared and the answer is still TRUE. */
    paint();
    clip.left = 100; clip.top = 100; clip.right = 110; clip.bottom = 110;
    ok(ScrollDC(dc, 0, 4, &scroll, &clip, region, &update) == 1, 9060);
    ok(same(&update, 0, 0, 0, 0), 9061);
    ok(GetRgnBox(region, &box) == 1 && same(&box, 0, 0, 0, 0), 9062);
    blit.left = blit.top = blit.right = blit.bottom = 0;
    copied(blit, 0, 0, 9063);

    /* Without a scroll rectangle the clip box is the scrolled area. */
    paint();
    ok(ScrollDC(dc, 0, 4, 0, 0, region, &update) == 1, 9070);
    ok(same(&update, 0, 0, 32, 4), 9071);
    blit.left = 0; blit.top = 4; blit.right = 32; blit.bottom = 32;
    copied(blit, 0, 4, 9072);

    /* The worker works in device units: with a viewport origin the same pixels
     * move and the update rectangle comes back in logical units. */
    paint();
    ok(SetViewportOrgEx(dc, 2, 3, 0), 9080);
    scroll.left = 2; scroll.top = 1; scroll.right = 18; scroll.bottom = 17;
    ok(ScrollDC(dc, 0, 4, &scroll, 0, region, &update) == 1, 9081);
    ok(same(&update, 2, 1, 18, 5), 9082);
    blit.left = 4; blit.top = 8; blit.right = 20; blit.bottom = 24;
    copied(blit, 0, 4, 9083);
    ok(SetViewportOrgEx(dc, 0, 0, 0), 9084);

    /* A complex clip region takes the region path of the worker: two bands of
     * clip, two bands of copy, and an update region of two bands. */
    complex_region = CreateRectRgn(0, 0, 32, 10);
    band = CreateRectRgn(0, 20, 32, 32);
    ok(complex_region && band && CombineRgn(complex_region, complex_region, band, 2) == 3, 9090);
    ok(SelectClipRgn(dc, complex_region) == 3, 9091);
    ok(GetClipBox(dc, &box) == 3 && same(&box, 0, 0, 32, 32), 9092);
    paint();
    scroll.left = 4; scroll.top = 4; scroll.right = 28; scroll.bottom = 28;
    ok(ScrollDC(dc, 0, 4, &scroll, 0, region, &update) == 1, 9093);
    ok(same(&update, 4, 4, 28, 24), 9094);
    ok(GetRgnBox(region, &box) == 3 && same(&box, 4, 4, 28, 24), 9095);
    {
        int y, x;
        for (y = 0; y < 32; ++y)
            for (x = 0; x < 32; ++x)
            {
                U want = (U)(y * 32 + x);
                if (x >= 4 && x < 28 && ((y >= 8 && y < 10) || (y >= 24 && y < 32)))
                    want = (U)((y - 4) * 32 + x);
                ok(pixels[y * 32 + x] == want, 9096);
            }
    }
    ok(SelectClipRgn(dc, 0) == 2, 9097);
    ok(DeleteObject(complex_region) && DeleteObject(band), 9098);

    ok(SelectObject(dc, previous) != 0, 9099);
    ok(DeleteObject(bitmap) && DeleteDC(dc) && DeleteObject(region), 9100);
    /* The device context is gone: the validation of refuses it. */
    SetLastError(0);
    ok(!ScrollDC(dc, 0, 4, 0, 0, 0, 0) && GetLastError() == 6, 9101);
}

int main(void)
{
    records = (U *)VirtualAlloc((void *)RECORDS, 65536, 0x3000, 4);
    if (!records) ExitProcess(9);
    scroll_dc();
    enum_objects();
    fonts_and_colour();
    printer_bridges();
    winmm_callbacks();
    records[R_CHECKS] = checks;
    records[R_BAD] = bad;
    records[R_LAST_FAIL] = last_fail;
    ExitProcess(0);
    return 0;
}
