/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetLastError(void);
API U CreateCompatibleDC(U);
API U DeleteDC(U);
API U DeleteObject(U);
API U SelectObject(U, U);
API U CreateDIBSection(U, const void *, U, void **, U, U);
API U CreateSolidBrush(U);
API U CreatePen(U, int, U);
API U PatBlt(U, int, int, int, int, U);
API U MoveToEx(U, int, int, void *);
API U GetCurrentPositionEx(U, void *);
API U GetStockObject(U);
API U GetObjectA(U, int, void *);
API U SaveDC(U);
API U RestoreDC(U, int);
API U IntersectClipRect(U, int, int, int, int);
API U SetViewportOrgEx(U, int, int, void *);
API U SetROP2(U, U);
API U Rectangle(U, int, int, int, int);
API U Ellipse(U, int, int, int, int);
API U Polygon(U, const void *, U);
API U PolyPolygon(U, const void *, const U *, U);
API U GetPolyFillMode(U);
API U SetPolyFillMode(U, U);
static void check(int v, U n) {
  if (!v)
    ExitProcess(n);
}
static U pixel(const unsigned char *p, U depth, U x, U y) {
  p += y * ((32 * depth + 31) / 32) * 4;
  if (depth == 1)
    return (p[x / 8] >> (7 - x % 8)) & 1;
  if (depth == 4)
    return (p[x / 2] >> ((1 - x % 2) * 4)) & 15;
  p += x * (depth / 8);
  U v = 0;
  for (U i = 0; i < depth / 8; ++i)
    v |= (U)p[i] << (i * 8);
  return v;
}
static void clear(U dc) { check(PatBlt(dc, 0, 0, 32, 24, 0xff0062), 15901); }
void run_gdi_shapes_test(void) {
  U dc = CreateCompatibleDC(0), black = CreateSolidBrush(0),
    pen = CreatePen(0, 1, 0), wide = CreatePen(0, 3, 0),
    nullpen = GetStockObject(8), hollow = GetStockObject(5);
  check(dc && black && pen && wide && nullpen && hollow, 15900);
  U brushinfo[3] = {};
  check(GetObjectA(hollow, 12, brushinfo) == 12 && brushinfo[0] == 1, 15902);
  check(GetPolyFillMode(dc) == 1 && SetPolyFillMode(dc, 2) == 1 &&
            GetPolyFillMode(dc) == 2,
        15903);
  check(!SetPolyFillMode(dc, 3) && GetPolyFillMode(dc) == 2 &&
            SetPolyFillMode(dc, 0x10001) == 2,
        15904);
  check(SaveDC(dc) && SetPolyFillMode(dc, 2) == 1 && RestoreDC(dc, -1) &&
            GetPolyFillMode(dc) == 1,
        15905);
  for (U n = 0; n < 7; ++n) {
    U depth = n == 0             ? 1
              : n == 1           ? 4
              : n == 2           ? 8
              : n == 3 || n == 6 ? 16
              : n == 4           ? 24
                                 : 32;
    U header[14] = {40, 32, (U)-24, (depth << 16) | 1, 0, 0, 0, 0,
                    0,  0,  0,      0xffffff};
    if (depth <= 8)
      header[8] = 2;
    if (n == 6) {
      header[4] = 3;
      header[10] = 0xf800;
      header[11] = 0x07e0;
      header[12] = 0x001f;
    }
    void *raw = 0;
    U bitmap = CreateDIBSection(0, header, 0, &raw, 0, 0),
      old = SelectObject(dc, bitmap), ob = SelectObject(dc, black),
      op = SelectObject(dc, nullpen);
    check(bitmap && raw && old && ob && op, 15906);
    clear(dc);
    check(MoveToEx(dc, 17, 19, 0) && Rectangle(dc, 2, 3, 12, 13), 15907);
    for (U y = 0; y < 24; ++y)
      for (U x = 0; x < 32; ++x)
        check((pixel(raw, depth, x, y) == 0) ==
                  (x >= 2 && x < 11 && y >= 3 && y < 12),
              15908);
    int position[2] = {};
    check(GetCurrentPositionEx(dc, position) && position[0] == 17 &&
              position[1] == 19,
          15909);
    clear(dc);
    SelectObject(dc, pen);
    SelectObject(dc, hollow);
    check(Rectangle(dc, 12, 13, 2, 3), 15910);
    for (U y = 0; y < 24; ++y)
      for (U x = 0; x < 32; ++x) {
        int edge = ((y == 3 || y == 12) && x >= 2 && x < 12) ||
                   ((x == 2 || x == 11) && y >= 3 && y < 13);
        check((pixel(raw, depth, x, y) == 0) == edge, 15911);
      }
    clear(dc);
    SelectObject(dc, black);
    SelectObject(dc, nullpen);
    int triangle[] = {2, 2, 14, 2, 2, 14};
    check(Polygon(dc, triangle, 3), 15912);
    check(pixel(raw, depth, 3, 3) == 0 && pixel(raw, depth, 12, 12) != 0,
          15913);
    clear(dc);
    int nested[] = {2, 2, 18, 2, 18, 18, 2, 18, 6, 6, 14, 6, 14, 14, 6, 14};
    U counts[] = {4, 4};
    check(PolyPolygon(dc, nested, counts, 2), 15914);
    check(pixel(raw, depth, 3, 3) == 0 && pixel(raw, depth, 8, 8) != 0, 15915);
    check(SetPolyFillMode(dc, 2) == 1 && PolyPolygon(dc, nested, counts, 2) &&
              pixel(raw, depth, 8, 8) == 0 && SetPolyFillMode(dc, 1) == 2,
          15916);
    clear(dc);
    check(Ellipse(dc, 2, 3, 22, 19), 15917);
    check(pixel(raw, depth, 12, 10) == 0 && pixel(raw, depth, 2, 3) != 0 &&
              pixel(raw, depth, 22, 10) != 0,
          15918);
    U blackcount = 0;
    for (U y = 0; y < 24; ++y)
      for (U x = 0; x < 32; ++x)
        blackcount += pixel(raw, depth, x, y) == 0;
    check(blackcount > 180 && blackcount < 320, 15919);
    clear(dc);
    check(SaveDC(dc) && SetViewportOrgEx(dc, 3, 4, 0) &&
              IntersectClipRect(dc, 3, 3, 7, 7),
          15920);
    int square[] = {0, 0, 12, 0, 12, 12, 0, 12};
    check(Polygon(dc, square, 4) && RestoreDC(dc, -1), 15921);
    for (U y = 0; y < 24; ++y)
      for (U x = 0; x < 32; ++x)
        check((pixel(raw, depth, x, y) == 0) ==
                  (x >= 6 && x < 10 && y >= 7 && y < 11),
              15922);
    clear(dc);
    SelectObject(dc, wide);
    SelectObject(dc, hollow);
    check(Rectangle(dc, 4, 4, 20, 20), 15923);
    for (U y = 0; y < 24; ++y)
      for (U x = 0; x < 32; ++x) {
        int outer = x >= 3 && x < 21 && y >= 3 && y < 21;
        int inner = x >= 6 && x < 18 && y >= 6 && y < 18;
        check((pixel(raw, depth, x, y) == 0) == (outer && !inner), 15931);
      }
    clear(dc);
    U even = CreatePen(0, 2, 0);
    check(even && SelectObject(dc, even) == wide && Rectangle(dc, 4, 4, 20, 20), 15935);
    for (U y = 0; y < 24; ++y)
      for (U x = 0; x < 32; ++x) {
        int outer = x >= 3 && x < 20 && y >= 3 && y < 20;
        int inner = x >= 5 && x < 18 && y >= 5 && y < 18;
        check((pixel(raw, depth, x, y) == 0) == (outer && !inner), 15936);
      }
    check(SelectObject(dc, wide) == even && DeleteObject(even), 15937);
    clear(dc);
    check(Ellipse(dc, 4, 4, 24, 20), 15932);
    check(pixel(raw, depth, 3, 11) == 0 && pixel(raw, depth, 5, 11) == 0 &&
          pixel(raw, depth, 14, 11) != 0 && pixel(raw, depth, 2, 11) != 0, 15933);
    clear(dc);
    check(SetROP2(dc, 6) == 13 && Rectangle(dc, 4, 4, 20, 20) &&
          Rectangle(dc, 4, 4, 20, 20) && Ellipse(dc, 4, 4, 24, 20) &&
          Ellipse(dc, 4, 4, 24, 20) && SetROP2(dc, 13) == 6, 15934);
    check(Rectangle(dc, 1, 1, 1, 10) && Polygon(dc, square, 1), 15930);
    for (U y = 0; y < 24; ++y)
      for (U x = 0; x < 32; ++x)
        check(pixel(raw, depth, x, y) != 0, 15924);
    SelectObject(dc, nullpen);
    SelectObject(dc, black);
    check(SetROP2(dc, 6) == 13 && Polygon(dc, square, 4) &&
              Polygon(dc, square, 4) && SetROP2(dc, 13) == 6,
          15925);
    for (U y = 0; y < 24; ++y)
      for (U x = 0; x < 32; ++x)
        check(pixel(raw, depth, x, y) != 0, 15926);
    check(Rectangle(dc, 3, 3, 3, 15) && Ellipse(dc, 2, 2, 2, 4), 15927);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    SelectObject(dc, old);
    check(DeleteObject(bitmap), 15928);
  }
  check(DeleteObject(black) && DeleteObject(pen) && DeleteObject(wide) &&
            DeleteDC(dc),
        15929);
}
#ifdef GDI_SHAPES_STANDALONE
void mainCRTStartup(void) {
  run_gdi_shapes_test();
  ExitProcess(0);
}
#endif
