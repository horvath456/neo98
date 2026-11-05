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
API U SaveDC(U);
API U RestoreDC(U, int);
API U IntersectClipRect(U, int, int, int, int);
API U SetViewportOrgEx(U, int, int, void *);
API U Arc(U, int, int, int, int, int, int, int, int);
API U Pie(U, int, int, int, int, int, int, int, int);
API U Chord(U, int, int, int, int, int, int, int, int);
API U RoundRect(U, int, int, int, int, int, int);
API U ArcTo(U, int, int, int, int, int, int, int, int);
/* Last two words carry FLOAT bits; the original unsupported export never reads them. */
API U AngleArc(U, int, int, U, U, U);
#include "gdi_arcs_golden.h"
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
static void clear(U dc) { check(PatBlt(dc, 0, 0, 32, 24, 0xff0062), 17101); }
void run_gdi_arcs_test(void) {
  U dc = CreateCompatibleDC(0), black = CreateSolidBrush(0),
    pen = CreatePen(0, 1, 0), wide = CreatePen(0, 3, 0),
    nullpen = GetStockObject(8), hollow = GetStockObject(5);
  check(dc && black && pen && wide && nullpen && hollow, 17100);
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
      op = SelectObject(dc, pen);
    check(bitmap && raw && old && ob && op, 17102);
    clear(dc);
    check(MoveToEx(dc, 17, 19, 0) && Arc(dc, 2, 3, 22, 19, 40, 9, 10, -30),
          17103);
    U changed = 0;
    for (U y = 0; y < 24; ++y)
      for (U x = 0; x < 32; ++x)
        changed += pixel(raw, depth, x, y) == 0;
    check(changed > 4 && changed < 35 && pixel(raw, depth, 11, 10) != 0, 17104);
    int position[2] = {};
    check(GetCurrentPositionEx(dc, position) && position[0] == 17 &&
              position[1] == 19,
          17105);
    SelectObject(dc, nullpen);
    for (U shape = 0; shape < 3; ++shape) {
      clear(dc);
      U result = shape == 0   ? Pie(dc, 2, 3, 22, 19, 40, 9, 10, -30)
                 : shape == 1 ? Chord(dc, 2, 3, 22, 19, 40, 9, 10, -30)
                              : RoundRect(dc, 2, 3, 22, 19, 8, 6);
      check(result, 17106 + shape);
      for (U y = 0; y < 24; ++y)
        for (U x = 0; x < 32; ++x)
          check((pixel(raw, depth, x, y) == 0) ==
                    ((arc_masks[shape][y] >> x) & 1),
                17110 + shape);
    }
    clear(dc);
    check(SaveDC(dc) && SetViewportOrgEx(dc, 3, 2, 0) &&
              IntersectClipRect(dc, 8, 4, 18, 14),
          17113);
    check(Pie(dc, 2, 3, 22, 19, 40, 9, 10, -30) && RestoreDC(dc, -1), 17114);
    for (U y = 0; y < 24; ++y)
      for (U x = 0; x < 32; ++x) {
        U expected = x >= 11 && x < 21 && y >= 6 && y < 16
                         ? ((arc_masks[0][y - 2] >> (x - 3)) & 1)
                         : 0;
        check((pixel(raw, depth, x, y) == 0) == expected, 17115);
      }
    clear(dc);
    SelectObject(dc, wide);
    check(!Pie(dc, 2, 3, 22, 19, 40, 9, 10, -30) && GetLastError() == 50,
          17116);
    check(!ArcTo(dc, 2, 3, 22, 19, 40, 9, 10, -30) && GetLastError() == 120,
          17117);
    check(!AngleArc(dc, 12, 10, 8, 0, 0x42b40000) && GetLastError() == 120, 17118);
    for (U y = 0; y < 24; ++y)
      for (U x = 0; x < 32; ++x)
        check(pixel(raw, depth, x, y) != 0, 17119);
    SelectObject(dc, nullpen);
    check(RoundRect(dc, 2, 3, 22, 19, 0, 6), 17120);
    for (U y = 0; y < 24; ++y)
      for (U x = 0; x < 32; ++x)
        check((pixel(raw, depth, x, y) == 0) ==
                  (x >= 2 && x < 21 && y >= 3 && y < 18),
              17121);
    clear(dc);
    SelectObject(dc, hollow);
    SelectObject(dc, pen);
    check(RoundRect(dc, 2, 3, 22, 19, 8, 6) && pixel(raw, depth, 11, 10) != 0,
          17122);
    changed = 0;
    for (U y = 0; y < 24; ++y)
      for (U x = 0; x < 32; ++x)
        changed += pixel(raw, depth, x, y) == 0;
    check(changed > 30 && changed < 80, 17123);
    check(GetCurrentPositionEx(dc, position) && position[0] == 17 &&
              position[1] == 19,
          17124);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    SelectObject(dc, old);
    check(DeleteObject(bitmap), 17125);
  }
  check(!ArcTo(0, 0, 0, 0, 0, 0, 0, 0, 0) && GetLastError() == 120, 17126);
  check(DeleteObject(black) && DeleteObject(pen) && DeleteObject(wide) &&
            DeleteDC(dc),
        17127);
}
#ifdef GDI_ARCS_STANDALONE
void mainCRTStartup(void) {
  run_gdi_arcs_test();
  ExitProcess(0);
}
#endif
