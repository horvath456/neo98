/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The GDI32 region, brush, dimension and flush exports of the import list,
   exercised through the public x86 interface with pixel checks. */
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
API U CreateRectRgn(int, int, int, int);
API U GetRgnBox(U, void *);
API U PtInRegion(U, int, int);
API U PatBlt(U, int, int, int, int, U);
API U GetStockObject(U);
API U GetObjectA(U, int, void *);
API U SetBkColor(U, U);
API U CreateEllipticRgn(int, int, int, int);
API U CreateEllipticRgnIndirect(const void *);
API U CreateRoundRectRgn(int, int, int, int, int, int);
API U FillRgn(U, U, U);
API U FrameRgn(U, U, U, int, int);
API U InvertRgn(U, U);
API U PaintRgn(U, U);
API U CreateHatchBrush(U, U);
API U CreateBrushIndirect(const void *);
API U UnrealizeObject(U);
API U GdiFlush(void);
API U GetGraphicsMode(U);
API U GetBitmapDimensionEx(U, void *);
API U SetBitmapDimensionEx(U, int, int, void *);
API U CreateBitmap(int, int, U, U, const void *);
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
static void clear(U dc) { check(PatBlt(dc, 0, 0, 64, 48, 0xff0062), 16001); }
void run_gdi_region_brush_test(void) {
  U header[11] = {40, 64, -48, 1 | (32 << 16), 0, 0, 0, 0, 0, 0, 0};
  unsigned char *raw = 0;
  U dc = CreateCompatibleDC(0);
  check(dc != 0, 16000);
  U bitmap = CreateDIBSection(dc, header, 0, (void **)&raw, 0, 0);
  check(bitmap && raw, 16002);
  U old = SelectObject(dc, bitmap);
  check(old != 0, 16003);

  /* GdiFlush is MOV EAX,1/RET and GetGraphicsMode MOV EAX,1/RET 4, so both
     answer without looking at anything. */
  check(GdiFlush() == 1, 16004);
  check(GetGraphicsMode(dc) == 1 && GetGraphicsMode(0) == 1, 16005);

  /* CreateEllipticRgn and its indirect form share the vertex kernel. The
     region of a 21 by 21 box contains its centre and misses its corner. */
  U ellipse = CreateEllipticRgn(10, 10, 31, 31);
  check(ellipse != 0, 16006);
  int box[4] = {0, 0, 0, 0};
  check(GetRgnBox(ellipse, box) == 3, 16007);
  check(box[0] == 10 && box[1] == 10 && box[2] == 31 && box[3] == 31, 16008);
  check(PtInRegion(ellipse, 20, 20) && !PtInRegion(ellipse, 10, 10), 16009);
  int rect[4] = {31, 31, 10, 10};
  U indirect = CreateEllipticRgnIndirect(rect);
  check(indirect != 0, 16010);
  int other[4] = {0, 0, 0, 0};
  check(GetRgnBox(indirect, other) == 3 && other[0] == box[0] &&
            other[1] == box[1] && other[2] == box[2] && other[3] == box[3],
        16011);

  /* A round rectangle with a zero corner extent is the plain rectangle
     region, exactly as GDI decides. */
  U flat = CreateRoundRectRgn(4, 4, 20, 14, 0, 6);
  check(flat != 0 && GetRgnBox(flat, other) == 2, 16012);
  check(other[0] == 4 && other[1] == 4 && other[2] == 20 && other[3] == 14,
        16013);
  U round = CreateRoundRectRgn(4, 4, 20, 14, 6, 6);
  check(round != 0 && GetRgnBox(round, other) == 3, 16014);
  check(other[0] == 4 && other[1] == 4 && other[2] == 20 && other[3] == 14,
        16015);
  check(PtInRegion(round, 12, 5) && !PtInRegion(round, 4, 4), 16016);

  /* FillRgn paints every rectangle of the region with the brush it is given
     and leaves the DC's own brush selected. */
  U red = CreateSolidBrush(0x0000ff), green = CreateSolidBrush(0x00ff00);
  check(red && green, 16017);
  U previous = SelectObject(dc, green);
  check(previous != 0, 16018);
  clear(dc);
  U square = CreateRectRgn(2, 2, 10, 10);
  check(square != 0, 16019);
  check(FillRgn(dc, square, red) == 1, 16020);
  check(pixel(raw, 2, 2) == bgr(0x0000ff) && pixel(raw, 9, 9) == bgr(0x0000ff), 16021);
  check(pixel(raw, 1, 1) == 0xffffff && pixel(raw, 10, 10) == 0xffffff, 16022);
  /* PaintRgn uses the brush the DC already has. */
  clear(dc);
  check(PaintRgn(dc, square) == 1, 16023);
  check(pixel(raw, 5, 5) == bgr(0x00ff00), 16024);
  /* InvertRgn is DSTINVERT and ignores both brushes. */
  check(InvertRgn(dc, square) == 1, 16025);
  check(pixel(raw, 5, 5) == bgr(0xff00ff), 16026);
  /* FrameRgn draws a border of the given extent inside the region. */
  clear(dc);
  check(FrameRgn(dc, square, red, 2, 2) == 1, 16027);
  check(pixel(raw, 2, 2) == bgr(0x0000ff) && pixel(raw, 9, 9) == bgr(0x0000ff), 16028);
  check(pixel(raw, 5, 5) == 0xffffff, 16029);

  /* Every one of the four rejects a handle of the wrong kind, in the order
     region, brush, device context. */
  check(!FillRgn(dc, red, red) && !FillRgn(dc, square, square), 16030);
  check(!PaintRgn(bitmap, square) && !InvertRgn(dc, bitmap), 16031);
  check(!FrameRgn(dc, square, bitmap, 1, 1), 16032);

  /* A hatch brush paints its own colour on the mask bits and the DC
     background colour in the gaps. HS_HORIZONTAL is row four of the tile. */
  U hatch = CreateHatchBrush(0, 0x0000ff);
  check(hatch != 0, 16033);
  U info[3] = {0, 0, 0};
  check(GetObjectA(hatch, 12, info) == 12, 16034);
  check(info[0] == 2 && info[1] == 0x0000ff && info[2] == 0, 16035);
  check(SetBkColor(dc, 0x00ff00) != 0xffffffff, 16036);
  clear(dc);
  check(FillRgn(dc, square, hatch) == 1, 16037);
  check(pixel(raw, 4, 4) == bgr(0x0000ff) && pixel(raw, 4, 5) == bgr(0x00ff00), 16038);
  SetLastError(0);
  check(!CreateHatchBrush(6, 0) && GetLastError() == 87, 16039);

  /* CreateBrushIndirect dispatches on lbStyle. */
  U logical[3] = {0, 0x0000ff, 0};
  U solid = CreateBrushIndirect(logical);
  check(solid != 0 && GetObjectA(solid, 12, info) == 12 && info[0] == 0 &&
            info[1] == 0x0000ff,
        16040);
  logical[0] = 1;
  U hollow = CreateBrushIndirect(logical);
  check(hollow != 0 && GetObjectA(hollow, 12, info) == 12 && info[0] == 1,
        16041);
  logical[0] = 2;
  logical[2] = 3;
  U hatched = CreateBrushIndirect(logical);
  check(hatched != 0 && GetObjectA(hatched, 12, info) == 12 && info[0] == 2 &&
            info[2] == 3,
        16042);
  logical[0] = 4;
  SetLastError(0);
  check(!CreateBrushIndirect(logical) && GetLastError() == 87, 16043);

  /* UnrealizeObject accepts a brush and a palette and refuses the rest. */
  check(UnrealizeObject(red) == 1 && UnrealizeObject(hatch) == 1, 16044);
  SetLastError(0);
  check(!UnrealizeObject(square) && GetLastError() == 6, 16045);
  /* Windows 98 returns FALSE for NULL without setting LastError. */
  SetLastError(0x1234);
  check(!UnrealizeObject(0) && GetLastError() == 0x1234, 16046);

  /* The dimension pair is a WORD pair; Set answers the previous one. */
  U ddb = CreateBitmap(4, 4, 1, 1, 0);
  check(ddb != 0, 16047);
  int size[2] = {7, 7};
  check(GetBitmapDimensionEx(ddb, size) == 1 && !size[0] && !size[1], 16048);
  check(SetBitmapDimensionEx(ddb, 40, 50, size) == 1 && !size[0] && !size[1],
        16049);
  check(GetBitmapDimensionEx(ddb, size) == 1 && size[0] == 40 && size[1] == 50,
        16050);
  check(SetBitmapDimensionEx(ddb, -3, 0x18000, size) == 1 && size[0] == 40 &&
            size[1] == 50,
        16051);
  check(GetBitmapDimensionEx(ddb, size) == 1 && size[0] == -3 &&
            size[1] == -32768,
        16052);
  check(SetBitmapDimensionEx(ddb, 1, 2, 0) == 1, 16053);
  SetLastError(0);
  check(!GetBitmapDimensionEx(red, size) && GetLastError() == 6, 16054);
  check(!GetBitmapDimensionEx(ddb, 0), 16055);

  SelectObject(dc, previous);
  SelectObject(dc, old);
  check(DeleteObject(bitmap) && DeleteObject(ddb), 16056);
  check(DeleteObject(ellipse) && DeleteObject(indirect) && DeleteObject(flat) &&
            DeleteObject(round) && DeleteObject(square),
        16057);
  check(DeleteObject(red) && DeleteObject(green) && DeleteObject(hatch) &&
            DeleteObject(solid) && DeleteObject(hollow) &&
            DeleteObject(hatched),
        16058);
  check(DeleteDC(dc), 16059);
}
#ifdef GDI_REGION_BRUSH_STANDALONE
void mainCRTStartup(void) {
  run_gdi_region_brush_test();
  ExitProcess(0);
}
#endif
