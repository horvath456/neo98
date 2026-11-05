/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The GDI32 path family, the curve entries and the two flood fills of the
   import list, exercised through the public x86 interface with pixel checks. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetLastError(void);
API void SetLastError(U);
API U CreateCompatibleDC(U);
API U DeleteDC(U);
API U DeleteObject(U);
API U SelectObject(U, U);
API U CreateDIBSection(U, const void *, U, void **, U, U);
API U CreateSolidBrush(U);
API U CreatePen(int, int, U);
API U GetStockObject(U);
API U PatBlt(U, int, int, int, int, U);
API U MoveToEx(U, int, int, void *);
API U LineTo(U, int, int);
API U Polygon(U, const void *, int);
API U Polyline(U, const void *, int);
API U GetRgnBox(U, void *);
API U PtInRegion(U, int, int);
API U SaveDC(U);
API U RestoreDC(U, int);
API U Rectangle(U, int, int, int, int);

API U BeginPath(U);
API U EndPath(U);
API U AbortPath(U);
API U CloseFigure(U);
API U FillPath(U);
API U StrokePath(U);
API U StrokeAndFillPath(U);
API U FlattenPath(U);
API U WidenPath(U);
API U PathToRegion(U);
API U SelectClipPath(U, int);
API int GetPath(U, void *, void *, int);
API U SetArcDirection(U, int);
API U GetArcDirection(U);
API U PolyBezier(U, const void *, U);
API U PolyBezierTo(U, const void *, U);
API U PolylineTo(U, const void *, U);
API U PolyPolyline(U, const void *, const U *, U);
API U FloodFill(U, int, int, U);
API U ExtFloodFill(U, int, int, U, U);

static void check(int v, U n) {
  if (!v)
    ExitProcess(n);
}
static U pixel(const unsigned char *p, U x, U y) {
  return *(const U *)(p + y * 64 * 4 + x * 4) & 0xffffff;
}
/* A 32 bpp DIB stores blue, green, red; a COLORREF is red, green, blue. */
static U bgr(U color) {
  return ((color & 255) << 16) | (color & 0xff00) | ((color >> 16) & 255);
}
static U header[11] = {40, 64, (U)-48, 1 | (32 << 16), 0, 0, 0, 0, 0, 0, 0};

/* The nine points the original GDI.EXE segment 32 produces for the cubic from
   (10,10) over (10,40) and (50,40) to (50,10); tools/test_gdi_paths_original.py
   reads them out of the original code itself. */
static const int curve_points[9][2] = {{10, 10}, {12, 20}, {16, 27}, {23, 31},
                                       {30, 33}, {37, 31}, {44, 27}, {48, 20},
                                       {50, 10}};

void run_gdi_paths_test(void) {
  unsigned char *raw = 0, *reference = 0;
  U dc = CreateCompatibleDC(0), other = CreateCompatibleDC(0);
  check(dc && other, 17000);
  U bitmap = CreateDIBSection(dc, header, 0, (void **)&raw, 0, 0);
  U mirror = CreateDIBSection(other, header, 0, (void **)&reference, 0, 0);
  check(bitmap && raw && mirror && reference, 17001);
  check(SelectObject(dc, bitmap) && SelectObject(other, mirror), 17002);
  U black = CreateSolidBrush(0), red = CreateSolidBrush(0x0000ff);
  check(black && red, 17003);

  /* GDI answers 1 while bit 2 of DC+7Ah is clear; Windows 98 rejects a
     direction outside 1..2 with the parameter error. */
  check(GetArcDirection(dc) == 1, 17004);
  check(SetArcDirection(dc, 2) == 1, 17005);
  check(GetArcDirection(dc) == 2, 17006);
  check(SetArcDirection(dc, 1) == 2, 17007);
  SetLastError(0);
  check(SetArcDirection(dc, 0) == 0 && GetLastError() == 87, 17008);
  check(SetArcDirection(dc, 3) == 0, 17009);

  /* Windows 98 answers FALSE without a path, Windows 98 answers TRUE either way. */
  check(EndPath(dc) == 0, 17010);
  check(AbortPath(dc) == 1, 17011);
  check(CloseFigure(dc) == 0, 17012);
  check(FillPath(dc) == 0 && StrokePath(dc) == 0, 17013);
  check(StrokeAndFillPath(dc) == 0 && FlattenPath(dc) == 0, 17014);
  check(PathToRegion(dc) == 0 && WidenPath(dc) == 0, 17015);
  check(GetPath(dc, 0, 0, 0) == -1, 17016);

  /* RECTANGLE answers FALSE while a path is open and records nothing.
     The path itself stays empty, so EndPath still reports the open path. */
  check(BeginPath(dc), 17017);
  check(Rectangle(dc, 1, 1, 9, 9) == 0, 17018);
  check(EndPath(dc) == 1, 17019);
  check(GetPath(dc, 0, 0, 0) == -1, 17020);

  /* A filled and stroked path against the same polygon drawn directly. The
     path records the polygon before rendering it. */
  {
    static const int shape[5][2] = {{8, 6}, {50, 9}, {44, 38}, {20, 41}, {12, 22}};
    U i;
    check(PatBlt(dc, 0, 0, 64, 48, 0x00ff0062), 17021);   /* WHITENESS */
    check(PatBlt(other, 0, 0, 64, 48, 0x00ff0062), 17022);
    check(SelectObject(dc, red) && SelectObject(other, red), 17023);
    check(BeginPath(dc), 17024);
    check(Polygon(dc, shape, 5), 17025);
    check(EndPath(dc), 17026);
    check(StrokeAndFillPath(dc), 17027);
    check(Polygon(other, shape, 5), 17028);
    for (i = 0; i < 64 * 48; ++i)
      check(pixel(raw, i % 64, i / 64) == pixel(reference, i % 64, i / 64),
            17029);
    /* The fill really happened: the interior carries the brush colour. */
    check(pixel(raw, 30, 25) == bgr(0x0000ff), 17030);
  }

  /* The curve stage. A recorded PolyBezierTo keeps its control points and the
     type PT_BEZIERTO until FlattenPath turns it into the line run of
     GDI. */
  {
    static const int control[3][2] = {{10, 40}, {50, 40}, {50, 10}};
    int points[64][2];
    unsigned char types[64];
    int n, i;
    check(BeginPath(dc), 17031);
    check(MoveToEx(dc, 10, 10, 0), 17032);
    check(PolyBezierTo(dc, control, 3), 17033);
    check(EndPath(dc), 17034);
    n = GetPath(dc, 0, 0, 0);
    check(n == 4, 17035);
    check(GetPath(dc, points, types, 64) == 4, 17036);
    check(types[0] == 6 && types[1] == 4 && types[2] == 4 && types[3] == 4,
          17037);
    check(points[3][0] == 50 && points[3][1] == 10, 17038);
    /* A buffer that is too small answers -1 and writes nothing. */
    check(GetPath(dc, points, types, 2) == -1, 17039);
    check(FlattenPath(dc), 17040);
    n = GetPath(dc, points, types, 64);
    check(n == 9, 17041);
    for (i = 0; i < 9; ++i)
      check(points[i][0] == curve_points[i][0] &&
                points[i][1] == curve_points[i][1],
            17042);
    check(types[0] == 6 && types[8] == 2, 17043);
    check(AbortPath(dc) == 1 && GetPath(dc, 0, 0, 0) == -1, 17044);
  }

  /* PolyBezier and PolyBezierTo reject a point count that is not 3n+1 or 3n;
      and do the same with the parameter error. */
  {
    static const int four[4][2] = {{0, 0}, {5, 5}, {10, 5}, {15, 0}};
    SetLastError(0);
    check(PolyBezier(dc, four, 3) == 0 && GetLastError() == 87, 17045);
    check(PolyBezierTo(dc, four, 4) == 0, 17046);
    check(PolyBezier(dc, four, 4) == 1, 17047);
  }

  /* PolylineTo starts at the current point, PolyPolyline draws one run per
     contour. Both answer FALSE for a contour of fewer than two points. */
  {
    static const int run[2][2] = {{40, 4}, {40, 20}};
    static const int runs[4][2] = {{2, 44}, {20, 44}, {24, 44}, {34, 44}};
    static const U counts[2] = {2, 2};
    check(PatBlt(dc, 0, 0, 64, 48, 0x00ff0062), 17048);
    check(SelectObject(dc, GetStockObject(7)) != 0, 17049); /* BLACK_PEN */
    check(MoveToEx(dc, 4, 4, 0), 17050);
    check(PolylineTo(dc, run, 2), 17051);
    check(pixel(raw, 20, 4) == 0, 17052);
    check(pixel(raw, 40, 12) == 0, 17053);
    check(PolyPolyline(dc, runs, counts, 2), 17054);
    check(pixel(raw, 10, 44) == 0 && pixel(raw, 30, 44) == 0, 17055);
    check(pixel(raw, 22, 44) == 0xffffff, 17056);
    SetLastError(0);
    check(PolyPolyline(dc, runs, counts, 0) == 0 && GetLastError() == 87, 17057);
  }

  /* PathToRegion hands the path over as a region and drops it. */
  {
    int box[4] = {0, 0, 0, 0};
    U region;
    check(BeginPath(dc), 17058);
    check(MoveToEx(dc, 10, 10, 0) && LineTo(dc, 30, 10) && LineTo(dc, 30, 30) &&
              LineTo(dc, 10, 30),
          17059);
    check(CloseFigure(dc) && EndPath(dc), 17060);
    region = PathToRegion(dc);
    check(region != 0, 17061);
    check(GetPath(dc, 0, 0, 0) == -1, 17062);
    check(GetRgnBox(region, box) == 2, 17063);
    check(box[0] == 10 && box[1] == 10 && box[2] == 30 && box[3] == 30, 17064);
    check(PtInRegion(region, 20, 20) && !PtInRegion(region, 5, 5), 17065);
    check(DeleteObject(region), 17066);
  }

  /* SelectClipPath rejects a mode outside 1..5 and otherwise narrows the clip
     to the path. */
  {
    SetLastError(0);
    check(SelectClipPath(dc, 0) == 0 && GetLastError() == 87, 17067);
    check(PatBlt(dc, 0, 0, 64, 48, 0x00ff0062), 17068);
    check(BeginPath(dc), 17069);
    check(MoveToEx(dc, 16, 16, 0) && LineTo(dc, 32, 16) && LineTo(dc, 32, 32) &&
              LineTo(dc, 16, 32),
          17070);
    check(CloseFigure(dc) && EndPath(dc), 17071);
    check(SaveDC(dc), 17072);
    check(SelectClipPath(dc, 5) != 0, 17073);
    check(SelectObject(dc, black) != 0, 17074);
    check(PatBlt(dc, 0, 0, 64, 48, 0x00f00021), 17075); /* PATCOPY */
    check(pixel(raw, 20, 20) == 0, 17076);
    check(pixel(raw, 4, 4) == 0xffffff, 17077);
    check(RestoreDC(dc, -1), 17078);
  }

  /* Both flood fills. FLOODFILLBORDER stops at the border colour,
     FLOODFILLSURFACE spreads while the pixel keeps the given colour. A seed
     that is already the border colour answers FALSE without painting. */
  {
    check(PatBlt(dc, 0, 0, 64, 48, 0x00ff0062), 17079);
    check(SelectObject(dc, GetStockObject(7)) != 0, 17080);
    check(SelectObject(dc, GetStockObject(5)) != 0, 17081); /* HOLLOW_BRUSH */
    check(MoveToEx(dc, 8, 8, 0) && LineTo(dc, 40, 8) && LineTo(dc, 40, 32) &&
              LineTo(dc, 8, 32) && LineTo(dc, 8, 8),
          17082);
    check(SelectObject(dc, red) != 0, 17083);
    check(FloodFill(dc, 20, 20, 0) == 1, 17084);
    check(pixel(raw, 20, 20) == bgr(0x0000ff), 17085);
    check(pixel(raw, 50, 20) == 0xffffff, 17086);
    check(pixel(raw, 20, 8) == 0, 17087);
    check(FloodFill(dc, 20, 8, 0) == 0, 17088);
    SetLastError(0);
    check(ExtFloodFill(dc, 20, 20, 0, 2) == 0 && GetLastError() == 87, 17089);
    check(ExtFloodFill(dc, 50, 40, 0xffffff, 1) == 1, 17090);
    check(pixel(raw, 50, 40) == bgr(0x0000ff), 17091);
    check(pixel(raw, 20, 20) == bgr(0x0000ff), 17092);
    check(ExtFloodFill(dc, 20, 8, 0xffffff, 1) == 0, 17093);
  }

  /* Native Win98 rejects the default zero-width pen and keeps the path. */
  {
    check(BeginPath(dc), 17094);
    check(MoveToEx(dc, 4, 4, 0) && LineTo(dc, 20, 4), 17095);
    check(EndPath(dc), 17096);
    check(WidenPath(dc) == 0, 17097);
    check(GetPath(dc, 0, 0, 0) == 2, 17098);
    U pen=CreatePen(0,1,0),old=SelectObject(dc,pen);check(pen&&old,17104);
    check(WidenPath(dc)==1&&GetPath(dc,0,0,0)>2,17105);
    check(SelectObject(dc,old)&&DeleteObject(pen),17106);
    /* DeleteDC drops the path, as GDI does for the original DC. */
  }

  check(SelectObject(dc, GetStockObject(0)) != 0, 17099);   /* WHITE_BRUSH */
  check(SelectObject(other, GetStockObject(0)) != 0, 17102);
  check(DeleteObject(black) && DeleteObject(red), 17103);
  check(DeleteDC(dc) && DeleteDC(other), 17100);
  check(DeleteObject(bitmap) && DeleteObject(mirror), 17101);
}

#ifdef GDI_PATHS_STANDALONE
void mainCRTStartup(void) {
  run_gdi_paths_test();
  ExitProcess(0);
}
#endif
