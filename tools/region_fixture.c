/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent x86 contract for original regions and the native memory-DC clip
 * provider. */
typedef unsigned U;
typedef struct {
  int left, top, right, bottom;
} Rect;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateRectRgn(int, int, int, int);
API U CreateRectRgnIndirect(const Rect *);
API U SetRectRgn(U, int, int, int, int);
API U CombineRgn(U, U, U, U);
API U OffsetRgn(U, int, int);
API U GetRgnBox(U, Rect *);
API U PtInRegion(U, int, int);
API U RectInRegion(U, const Rect *);
API U EqualRgn(U, U);
API U GetRegionData(U, U, void *);
API U ExtCreateRegion(const void *, U, const void *);
API U DeleteObject(U);
API U CreateCompatibleDC(U);
API U DeleteDC(U);
API U CreateBitmap(int, int, U, U, const void *);
API U CreateSolidBrush(U);
API U SelectObject(U, U);
API U SelectClipRgn(U, U);
API U ExtSelectClipRgn(U, U, U);
API U GetClipRgn(U, U);
API int GetRandomRgn(U, U, int);
API U OffsetClipRgn(U, int, int);
API U SaveDC(U);
API U RestoreDC(U, int);
API U PatBlt(U, int, int, int, int, U);
API U GetPixel(U, int, int);
API U GetLastError(void);
API void SetLastError(U);
API U GetModuleHandleA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U,
                      U, U, U);
API U DefWindowProcA(U, U, U, U);
API U DestroyWindow(U);
API U GetDC(U);
API U ReleaseDC(U, U);
API U FillRect(U, const Rect *, U);
API U BeginPaint(U, void *);
API U EndPaint(U, const void *);
API U InvalidateRect(U, const Rect *, U);
static U __stdcall region_proc(U h, U m, U w, U l) {
  return DefWindowProcA(h, m, w, l);
}
static void check(int ok, U code) {
  if (!ok)
    ExitProcess(code);
}
static int same(const Rect *r, int l, int t, int rr, int b) {
  return r->left == l && r->top == t && r->right == rr && r->bottom == b;
}
void run_region_family_test(void) {
  Rect box, query = {-5, -5, 5, 5};
  U a = CreateRectRgn(10, 20, -10, -20), b = CreateRectRgnIndirect(&query),
    r = CreateRectRgn(0, 0, 0, 0);
  check(a && b && r, 7000);
  check(GetRgnBox(a, &box) == 2 && same(&box, -10, -20, 10, 20), 7001);
  check(PtInRegion(a, -10, -20) && !PtInRegion(a, 10, 0) &&
            !PtInRegion(a, 0, 20),
        7002);
  check(RectInRegion(a, &query), 7003);
  check(CombineRgn(r, a, b, 1) == 2 && EqualRgn(r, b), 7004);
  check(CombineRgn(r, a, b, 4) == 3 && !PtInRegion(r, 0, 0) &&
            PtInRegion(r, -9, 0),
        7005);
  U data[8 + 4 * 100];
  check(GetRegionData(r, 0, 0) == 96, 7006);
  check(GetRegionData(r, 95, (void *)0xfffffff0) == 96, 7007);
  check(GetRegionData(r, sizeof data, data) == 96 && data[0] == 32 &&
            data[1] == 1 && data[2] == 4 && data[3] == 64,
        7008);
  U roundtrip = ExtCreateRegion(0, 96, data);
  check(roundtrip && EqualRgn(roundtrip, r), 7009);
  check(DeleteObject(roundtrip), 7010);
  check(CombineRgn(r, a, b, 2) == 2 && EqualRgn(r, a), 7011);
  check(CombineRgn(r, a, b, 3) == 3 && !PtInRegion(r, 0, 0), 7012);
  check(CombineRgn(r, a, 0, 5) == 2 && EqualRgn(r, a), 7013);
  check(OffsetRgn(r, 65537, -2) == 2 && GetRgnBox(r, &box) == 2 &&
            same(&box, -9, -22, 11, 18),
        7014);
  check(SetRectRgn(r, 5, 4, 1, 2) && GetRgnBox(r, &box) == 2 &&
            same(&box, 1, 2, 5, 4),
        7015);
  check(DeleteObject(a) && DeleteObject(b) && DeleteObject(r), 7016);
  check(!PtInRegion(a, 0, 0), 7017);
  U replacement = CreateRectRgn(0, 0, 10, 10);
  check(replacement && replacement != a && !PtInRegion(a, 1, 1) &&
            DeleteObject(replacement),
        7018);
  data[0] = 32;
  data[1] = 1;
  data[2] = 100;
  data[3] = 1600;
  data[4] = 0;
  data[5] = 0;
  data[6] = 299;
  data[7] = 10;
  for (U i = 0; i < 100; ++i) {
    data[8 + i * 4] = i * 3;
    data[9 + i * 4] = 0;
    data[10 + i * 4] = i * 3 + 1;
    data[11 + i * 4] = 10;
  }
  U bars = ExtCreateRegion(0, sizeof data, data),
    snapshot = CreateRectRgn(0, 0, 0, 0);
  check(bars && snapshot, 7019);
  check(GetRegionData(bars, 0, 0) == sizeof data, 7020);
  U dc = CreateCompatibleDC(0);
  static U initial_pixels[400 * 12];
  U bitmap = CreateBitmap(400, 12, 1, 32, initial_pixels),
    brush = CreateSolidBrush(255);
  check(dc && bitmap && brush, 7021);
  U oldbitmap = SelectObject(dc, bitmap), oldbrush = SelectObject(dc, brush);
  check(oldbitmap && oldbrush, 7022);
  check(GetClipRgn(dc, snapshot) == 0, 7023);
  check(SelectClipRgn(dc, bars) == 3 && SaveDC(dc) == 1, 7024);
  check(SetRectRgn(bars, 0, 0, 1, 1) && DeleteObject(bars), 7025);
  check(PatBlt(dc, 0, 0, 400, 12, 0x00f00021), 7026);
  /* Inspect excluded pixels through an unclipped DC. */
  check(SelectClipRgn(dc, 0) == 2 && GetClipRgn(dc, snapshot) == 0, 7028);
  check(GetPixel(dc, 0, 0) == 255 && GetPixel(dc, 1, 0) == 0 &&
            GetPixel(dc, 297, 9) == 255 && GetPixel(dc, 297, 10) == 0,
        7027);
  check(RestoreDC(dc, -1) && GetClipRgn(dc, snapshot) == 1, 7029);
  check(GetRegionData(snapshot, 0, 0) == sizeof data, 7030);
  check(OffsetClipRgn(dc, 1, 1) == 3 && GetClipRgn(dc, snapshot) == 1 &&
            PtInRegion(snapshot, 1, 1) && !PtInRegion(snapshot, 0, 0),
        7031);
  /* GetRandomRgn, GDI32: GetClipRgn is this routine with 1. 1 is the
   * clip region, 2 the meta region (none here), 3 the two combined, 4 the
   * visible region; any other number answers 0 and a bad handle -1 with
   * error 6. MSHTML asks for 4 while it paints the folder web view. */
  U random = CreateRectRgn(0, 0, 0, 0);
  check(random && GetRandomRgn(dc, random, 1) == 1 && EqualRgn(random, snapshot), 7054);
  check(GetRandomRgn(dc, random, 0) == 0 && GetRandomRgn(dc, random, 5) == 0, 7055);
  check(GetRandomRgn(dc, random, 2) == 0, 7056);
  check(GetRandomRgn(dc, random, 3) == 1 && EqualRgn(random, snapshot), 7057);
  Rect visible = {0, 0, 0, 0};
  check(GetRandomRgn(dc, random, 4) == 1 && GetRgnBox(random, &visible) &&
            visible.right > visible.left && visible.bottom > visible.top,
        7058);
  SetLastError(0);
  check(GetRandomRgn(dc, 0x12345, 1) == -1 && GetLastError() == 6, 7059);
  SetLastError(0);
  check(GetRandomRgn(0x12345, random, 4) == -1 && GetLastError() == 6, 7060);
  check(DeleteObject(random), 7061);
  U cut = CreateRectRgn(0, 0, 150, 12);
  check(cut && ExtSelectClipRgn(dc, cut, 1) == 3 &&
            GetClipRgn(dc, snapshot) == 1,
        7032);
  check(GetRegionData(snapshot, 0, 0) == 32 + 50 * 16, 7033);
  check(!ExtSelectClipRgn(dc, 0, 1), 7034);
  check(DeleteObject(cut) && DeleteObject(snapshot), 7035);
  check(SelectObject(dc, oldbitmap) == bitmap &&
            SelectObject(dc, oldbrush) == brush,
        7036);
  check(DeleteObject(bitmap) && DeleteObject(brush) && DeleteDC(dc), 7037);
  /* Real USER retained canvas and BeginPaint's independent system clip. */
  const char *class_name = "Neo98RegionWindow";
  U cls[10] = {0, (U)region_proc, 0, 0, GetModuleHandleA(0), 0, 0, 0,
               0, (U)class_name};
  check(RegisterClassA(cls), 7039);
  U window = CreateWindowExA(0, class_name, "Region port", 0x90000000, 40, 60,
                             80, 50, 0, 0, cls[4], 0);
  check(window, 7040);
  U wdc = GetDC(window), red = CreateSolidBrush(255),
    green = CreateSolidBrush(0x0000ff00);
  U shape = CreateRectRgn(0, 0, 10, 10), got = CreateRectRgn(0, 0, 0, 0);
  check(wdc && red && green && shape && got, 7041);
  check(SelectClipRgn(wdc, shape) == 2 && GetClipRgn(wdc, got) == 1 &&
            EqualRgn(shape, got),
        7042);
  Rect all = {0, 0, 80, 50};
  check(FillRect(wdc, &all, red), 7043);
  check(SelectClipRgn(wdc, 0) == 2, 7044);
  check(GetPixel(wdc, 1, 1) == 255 && GetPixel(wdc, 12, 12) != 255, 7045);
  check(ReleaseDC(window, wdc), 7046);
  U paint[16];
  U pdc = BeginPaint(window, paint);
  check(pdc && EndPaint(window, paint), 7047);
  Rect invalid = {20, 10, 30, 20};
  check(InvalidateRect(window, &invalid, 0), 7048);
  pdc = BeginPaint(window, paint);
  check(pdc && GetClipRgn(pdc, got) == 0 && SelectClipRgn(pdc, 0) == 2, 7049);
  check(FillRect(pdc, &all, green) && EndPaint(window, paint), 7050);
  wdc = GetDC(window);
  check(wdc && GetPixel(wdc, 21, 11) == 0x0000ff00 &&
            GetPixel(wdc, 19, 11) != 0x0000ff00 &&
            GetPixel(wdc, 31, 11) != 0x0000ff00,
        7051);
  check(ReleaseDC(window, wdc) && DestroyWindow(window), 7052);
  check(DeleteObject(red) && DeleteObject(green) && DeleteObject(shape) &&
            DeleteObject(got),
        7053);
  /* This package deliberately does not claim the GDI36 affine transform helper.
   */
  U transform[6] = {0x3f800000, 0, 0, 0x3f800000, 0, 0};
  SetLastError(999);
  check(!ExtCreateRegion(transform, sizeof data, data) && GetLastError() == 50,
        7038);
}
