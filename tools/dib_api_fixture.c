/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
typedef struct {
  U size;
  int width, height;
  unsigned short planes, depth;
  U compression, bytes;
  int xppm, yppm;
  U colors, important;
} DibHeader;
typedef struct {
  U base, allocation, allocation_protection, size, state, protection, type;
} MemoryInfo;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetLastError(void);
API void SetLastError(U);
API U VirtualQuery(const void *, MemoryInfo *, U);
API U CreateDIBSection(U, const void *, U, void **, U, U);
API U CreateDIBitmap(U, const void *, U, const void *, const void *, U);
API U GetDIBits(U, U, U, U, void *, void *, U);
API U SetDIBits(U, U, U, U, const void *, const void *, U);
API U SetDIBitsToDevice(U, int, int, U, U, int, int, U, U, const void *,
                        const void *, U);
API U StretchDIBits(U, int, int, int, int, int, int, int, int, const void *,
                    const void *, U, U);
API U GetDIBColorTable(U, U, U, void *);
API U SetDIBColorTable(U, U, U, const void *);
API U CreateCompatibleDC(U);
API U SelectObject(U, U);
API U DeleteObject(U);
API U DeleteDC(U);
API U GetPixel(U, int, int);
API U BitBlt(U, int, int, int, int, U, int, int, U);
API U SaveDC(U);
API U RestoreDC(U, int);
API U GetObjectA(U, int, void *);
API U CreateBitmap(int, int, U, U, const void *);
API U SetTextColor(U, U);
API U SetBkColor(U, U);
API U GetStockObject(int);
API U StretchBlt(U, int, int, int, int, U, int, int, int, int, U);
API int SetStretchBltMode(U, int);
static void check(int condition, U code) {
  if (!condition)
    ExitProcess(code);
}
void run_dib_api_test(void) {
  DibHeader h = {40, 2, 2, 1, 32, 0, 0, 0, 0, 0, 0};
  void *raw = 0;
  U bitmap = CreateDIBSection(0, &h, 0, &raw, 0, 0);
  check(bitmap && raw, 8400);
  U *bits = (U *)raw;
  bits[0] = 0x00112233;
  bits[1] = 0x00445566;
  bits[2] = 0x00778899;
  bits[3] = 0x00aabbcc;
  MemoryInfo query;
  check(VirtualQuery(raw, &query, sizeof query) == sizeof query &&
            query.state == 0x1000 && query.type == 0x20000 &&
            query.protection == 4,
        8401);
  U dc = CreateCompatibleDC(0), old = SelectObject(dc, bitmap);
  check(dc && old, 8402);
  check(GetPixel(dc, 0, 0) == 0x998877 && GetPixel(dc, 1, 1) == 0x665544, 8403);
  check(SaveDC(dc) == 1 && !DeleteObject(bitmap), 8404);
  check(SelectObject(dc, old) == bitmap && !DeleteObject(bitmap), 8405);
  check(RestoreDC(dc, -1) && SelectObject(dc, old) == bitmap, 8406);
  U copy[4] = {0};
  check(GetDIBits(dc, bitmap, 0, 2, copy, &h, 0) == 2 && copy[0] == bits[0] &&
            copy[3] == bits[3],
        8407);
  U bottom[] = {0xaaff0000, 0xaa00ff00};
  check(SetDIBits(dc, bitmap, 0, 1, bottom, &h, 0) == 1 &&
            bits[0] == bottom[0] && bits[1] == bottom[1] &&
            bits[2] == 0x00778899,
        8408);
  U object[21] = {0};
  check(GetObjectA(bitmap, 84, object) == 84 && object[1] == 2 &&
            object[2] == 2 && object[5] == (U)raw && object[6] == 40,
        8409);
  check(GetObjectA(bitmap, 60, object) == 60, 8410);
  SetLastError(0);
  check(!GetObjectA(bitmap, 0, object) && GetLastError() == 87, 8411);
  check(DeleteObject(bitmap), 8412);
  SetLastError(0);
  check(!GetObjectA(bitmap, sizeof object, object) && GetLastError() == 87, 8494);
  // Session DIB mappings are released on the next section allocation.
  // That allocation may reuse the released range.
  void *next_raw = 0;
  U next_bitmap = CreateDIBSection(0, &h, 0, &next_raw, 0, 0);
  check(next_bitmap && next_raw, 8495);
  check(VirtualQuery(raw, &query, sizeof query) == sizeof query &&
            (next_raw == raw ? query.state == 0x1000 && query.allocation == (U)next_raw
                             : query.state == 0x10000),
        8413);
  check(DeleteObject(next_bitmap), 8496);
  // CBM_CREATEDIB reuses the original public section constructor.
  U input[] = {0x01020304, 0x05060708, 0x09101112, 0x13141516};
  bitmap = CreateDIBitmap(dc, &h, 6, input, &h, 0);
  check(bitmap, 8415);
  check(GetDIBits(dc, bitmap, 0, 2, copy, &h, 0) == 2 && copy[3] == input[3],
        8416);
  check(DeleteObject(bitmap), 8417);
  bitmap = CreateDIBitmap(dc, &h, 6, 0, &h, 0);
  check(bitmap && DeleteObject(bitmap), 8418);
  check(DeleteDC(dc), 8414);
  // A palettized, top-down DIBSection can be selected and converted by BitBlt.
  struct {
    DibHeader header;
    U colors[16];
  } indexed = {{40, 2, -2, 1, 4, 0, 0, 0, 0, 0, 0}, {0}};
  indexed.colors[1] = 0xff0000;
  indexed.colors[2] = 0x00ff00;
  bitmap = CreateDIBSection(0, &indexed, 0, &raw, 0, 0);
  check(bitmap && raw, 8419);
  unsigned char *nibbles = (unsigned char *)raw;
  nibbles[0] = 0x12;
  nibbles[4] = 0x21;
  dc = CreateCompatibleDC(0);
  old = SelectObject(dc, bitmap);
  check(GetPixel(dc, 0, 0) == 0xff && GetPixel(dc, 1, 0) == 0xff00, 8420);
  U blue = 0x000000ff, color = 0;
  check(SetDIBColorTable(dc, 1, 1, &blue) == 1 &&
            GetDIBColorTable(dc, 1, 1, &color) == 1 && color == blue,
        8421);
  void *target_bits = 0;
  U target = CreateDIBSection(0, &h, 0, &target_bits, 0, 0),
    target_dc = CreateCompatibleDC(0),
    target_old = SelectObject(target_dc, target);
  check(target && target_dc && target_old, 8422);
  check(BitBlt(target_dc, 0, 0, 2, 2, dc, 0, 0, 0xcc0020) &&
            GetPixel(target_dc, 0, 0) == 0xff0000,
        8423);
  check(StretchDIBits(target_dc, 0, 0, 2, 2, 0, 0, 2, 2, input, &h, 0,
                      0xcc0020) == 2,
        8424);
  check(GetPixel(target_dc, 0, 0) == 0x121110, 8425);
  check(SetDIBitsToDevice(target_dc, 0, 0, 2, 2, 0, 0, 0, 1, input, &h, 0) ==
                1 &&
            GetPixel(target_dc, 0, 1) == 0x040302,
        8426);
  /* A stretch with SRCINVERT samples like the SRCCOPY one: the
     pixel at 0,0 is its own sample, 121110h, so it becomes zero. */
  check(StretchDIBits(target_dc, 0, 0, 4, 4, 0, 0, 2, 2, input, &h, 0,
                      0x660046) == 2 &&
            GetPixel(target_dc, 0, 0) == 0,
        8427);
  check(SelectObject(dc, old) == bitmap && DeleteObject(bitmap) && DeleteDC(dc),
        8428);
  check(SelectObject(target_dc, target_old) == target && DeleteObject(target) &&
            DeleteDC(target_dc),
        8429);
  // A palettized DIB section is a destination the DIB engine can serve: its
  // colour table names the index every source colour becomes. COMCTL32 fills
  // its 4-bit image strip this way. A 4-bit DDB cannot be selected into a
  // DC of the 32-bit display; the DIB section is independent of that depth.
  struct {
    DibHeader header;
    U colors[16];
  } table = {{40, 2, 2, 1, 4, 0, 0, 0, 0, 0, 0}, {0}};
  table.colors[1] = 0x00ff0000;
  table.colors[2] = 0x0000ff00;
  table.colors[3] = 0x000000ff;
  void *table_bits = 0;
  U indexed_target = CreateDIBSection(0, &table, 0, &table_bits, 0, 0),
    indexed_dc = CreateCompatibleDC(0),
    indexed_old = SelectObject(indexed_dc, indexed_target);
  check(indexed_target && table_bits && indexed_dc && indexed_old, 8440);
  U direct[4] = {0x00ff0000, 0x0000ff00, 0x000000ff, 0};
  check(StretchDIBits(indexed_dc, 0, 0, 2, 2, 0, 0, 2, 2, direct, &h, 0,
                      0xcc0020) == 2,
        8441);
  check(GetPixel(indexed_dc, 0, 0) == 0x00ff0000 &&
            GetPixel(indexed_dc, 1, 0) == 0 &&
            GetPixel(indexed_dc, 0, 1) == 0x000000ff &&
            GetPixel(indexed_dc, 1, 1) == 0x0000ff00,
        8442);
  U palettized_ddb = CreateBitmap(2, 2, 1, 4, 0), ddb_dc = CreateCompatibleDC(0);
  check(palettized_ddb && ddb_dc, 8443);
  check(!SelectObject(ddb_dc, palettized_ddb), 8444);
  check(DeleteObject(palettized_ddb) && DeleteDC(ddb_dc), 8445);
  // A monochrome DDB into the same palettized section: DIBENG expands the mask
  // with the destination's text and background colour, and the palettized
  // surface keeps the two nearest colour-table indices.
  const unsigned char mask[] = {0x40, 0, 0x80, 0};
  U mono = CreateBitmap(2, 2, 1, 1, mask), mono_dc = CreateCompatibleDC(0),
    mono_old = SelectObject(mono_dc, mono);
  check(mono && mono_dc && mono_old, 8446);
  check(SetTextColor(indexed_dc, 0x000000ff) != 0xffffffff &&
            SetBkColor(indexed_dc, 0x0000ff00) != 0xffffffff,
        8447);
  check(BitBlt(indexed_dc, 0, 0, 2, 2, mono_dc, 0, 0, 0xcc0020), 8448);
  check(GetPixel(indexed_dc, 0, 0) == 0x000000ff &&
            GetPixel(indexed_dc, 1, 0) == 0x0000ff00 &&
            GetPixel(indexed_dc, 0, 1) == 0x0000ff00 &&
            GetPixel(indexed_dc, 1, 1) == 0x000000ff,
        8449);
  check(SelectObject(mono_dc, mono_old) == mono && DeleteObject(mono) &&
            DeleteDC(mono_dc),
        8450);
  check(SelectObject(indexed_dc, indexed_old) == indexed_target &&
            DeleteObject(indexed_target) && DeleteDC(indexed_dc),
        8451);
  raw = (void *)0x12345678;
  check(!CreateDIBSection(0, &h, 0, &raw, 0, 1) && raw == (void *)0x12345678,
        8430);
  for (U i = 0; i < 80; ++i) {
    bitmap = CreateDIBSection(0, &h, 0, &raw, 0, 0);
    check(bitmap && raw && DeleteObject(bitmap), 8431);
  }
}

/* BI_RLE4 and BI_RLE8. The two streams below are written by hand, one
   wire token at a time, so a failing check names a token and not a picture.
   Both images are 6 by 3 and both leave pixels untouched: the bottom row ends
   before its last column, the middle row starts with a delta, and only the top
   row is complete. An absolute run needs at least three pixels, because the
   counts 0, 1 and 2 are the escape codes. The colours are chosen so that the
   red and blue halves match, because a colour table entry is a BGR quad while
   GetPixel answers in RGB. */
void run_dib_rle_test(void) {
  struct {
    DibHeader header;
    U colors[256];
  } info = {{40, 6, 3, 1, 8, 1, 0, 0, 0, 0, 0}, {0}};
  info.colors[1] = 0x00ff00ff;
  info.colors[2] = 0x0000ff00;
  info.colors[3] = 0x00330033;
  info.colors[4] = 0x00447744;
  unsigned char rle8[] = {2, 1,                /* run of two, index 1 */
                          0, 3, 2, 3, 4, 0,    /* absolute run of three, pad */
                          0, 0,                /* end of line */
                          0, 2, 2, 0,          /* delta, two columns right */
                          1, 4,                /* run of one, index 4 */
                          0, 0,                /* end of line */
                          6, 2,                /* run of six, index 2 */
                          0, 1};               /* end of bitmap */
  struct {
    DibHeader header;
    U colors[16];
  } four = {{40, 6, 3, 1, 4, 2, 0, 0, 0, 0, 0}, {0}};
  four.colors[1] = 0x00ff00ff;
  four.colors[2] = 0x0000ff00;
  four.colors[3] = 0x00330033;
  four.colors[4] = 0x00447744;
  unsigned char rle4[] = {2, 0x11,             /* run of two, nibbles 1 and 1 */
                          0, 3, 0x23, 0x40,    /* absolute run of three */
                          0, 0,                /* end of line */
                          0, 2, 2, 0,          /* delta, two columns right */
                          1, 0x40,             /* run of one, nibble 4 */
                          0, 0,                /* end of line */
                          6, 0x22,             /* run of six, nibbles 2 */
                          0, 1};               /* end of bitmap */
  DibHeader plain = {40, 6, 3, 1, 32, 0, 0, 0, 0, 0, 0};
  U *surface = 0;
  U target = CreateDIBSection(0, &plain, 0, (void **)&surface, 0, 0),
    dc = CreateCompatibleDC(0), old = SelectObject(dc, target);
  check(target && surface && dc && old, 8460);
  for (U i = 0; i < 18; ++i)
    surface[i] = 0x00125612;
  info.header.bytes = sizeof rle8;
  check(StretchDIBits(dc, 0, 0, 6, 3, 0, 0, 6, 3, rle8, &info, 0, 0xcc0020) ==
            3,
        8461);
  /* Row order on screen is top down: the stream's last row is the top one. */
  check(GetPixel(dc, 0, 0) == 0x0000ff00 && GetPixel(dc, 5, 0) == 0x0000ff00,
        8462);
  check(GetPixel(dc, 0, 1) == 0x00125612 && GetPixel(dc, 1, 1) == 0x00125612 &&
            GetPixel(dc, 2, 1) == 0x00447744 &&
            GetPixel(dc, 3, 1) == 0x00125612,
        8463);
  check(GetPixel(dc, 0, 2) == 0x00ff00ff && GetPixel(dc, 1, 2) == 0x00ff00ff &&
            GetPixel(dc, 2, 2) == 0x0000ff00 &&
            GetPixel(dc, 3, 2) == 0x00330033 &&
            GetPixel(dc, 4, 2) == 0x00447744 &&
            GetPixel(dc, 5, 2) == 0x00125612,
        8464);
  for (U i = 0; i < 18; ++i)
    surface[i] = 0x00651265;
  four.header.bytes = sizeof rle4;
  check(StretchDIBits(dc, 0, 0, 6, 3, 0, 0, 6, 3, rle4, &four, 0, 0xcc0020) ==
            3,
        8465);
  check(GetPixel(dc, 0, 0) == 0x0000ff00 && GetPixel(dc, 5, 0) == 0x0000ff00,
        8466);
  check(GetPixel(dc, 0, 1) == 0x00651265 && GetPixel(dc, 1, 1) == 0x00651265 &&
            GetPixel(dc, 2, 1) == 0x00447744 &&
            GetPixel(dc, 3, 1) == 0x00651265,
        8467);
  check(GetPixel(dc, 0, 2) == 0x00ff00ff && GetPixel(dc, 1, 2) == 0x00ff00ff &&
            GetPixel(dc, 2, 2) == 0x0000ff00 &&
            GetPixel(dc, 3, 2) == 0x00330033 &&
            GetPixel(dc, 4, 2) == 0x00447744 &&
            GetPixel(dc, 5, 2) == 0x00651265,
        8468);
  /* SetDIBits takes the same stream into a device bitmap. */
  U ddb = CreateBitmap(6, 3, 1, 32, 0);
  check(ddb && SetDIBits(dc, ddb, 0, 3, rle8, &info, 0) == 3, 8469);
  U ddb_dc = CreateCompatibleDC(0), ddb_old = SelectObject(ddb_dc, ddb);
  check(ddb_dc && ddb_old && GetPixel(ddb_dc, 0, 2) == 0x00ff00ff &&
            GetPixel(ddb_dc, 2, 1) == 0x00447744 &&
            GetPixel(ddb_dc, 5, 0) == 0x0000ff00,
        8470);
  check(SelectObject(ddb_dc, ddb_old) == ddb && DeleteObject(ddb) &&
            DeleteDC(ddb_dc),
        8471);
  /* Negative probes. A depth that does not pair with the compression stays
     ERROR_INVALID_FUNCTION, a compressed top down image is refused the way
     DIBENG refuses it, and GetDIBits does not compress. */
  four.header.compression = 1;
  SetLastError(0);
  check(!StretchDIBits(dc, 0, 0, 6, 3, 0, 0, 6, 3, rle4, &four, 0, 0xcc0020) &&
            GetLastError() == 50,
        8472);
  four.header.compression = 2;
  four.header.height = -3;
  SetLastError(0);
  check(!StretchDIBits(dc, 0, 0, 6, 3, 0, 0, 6, 3, rle4, &four, 0, 0xcc0020) &&
            GetLastError() == 87,
        8473);
  four.header.height = 3;
  SetLastError(0);
  check(!GetDIBits(dc, target, 0, 3, 0, &info, 0) && GetLastError() == 50, 8474);
  check(SelectObject(dc, old) == target && DeleteObject(target) && DeleteDC(dc),
        8475);
  /* A descriptor query works for any DDB. A second query with a depth
     requires a display-compatible DDB; a DIB section supplies its own format. */
  {
    dc = CreateCompatibleDC(0);
    check(dc, 8493);
    U query[13] = {40};
    U high = CreateBitmap(1, 1, 1, 16, 0);
    check(high && GetDIBits(dc, high, 0, 1, 0, query, 0) == 1 && query[1] == 1 &&
              query[2] == 1 && query[3] >> 16 == 16 && query[4] == 3,
          8476);
    check(!GetDIBits(dc, high, 0, 1, 0, query, 0) && !query[10] &&
              !query[11] && !query[12], 8497);
    check(DeleteObject(high), 8498);
    DibHeader high_header = {40, 1, 1, 1, 16, 0, 0, 0, 0, 0, 0};
    void *high_bits = 0;
    high = CreateDIBSection(0, &high_header, 0, &high_bits, 0, 0);
    for (U i = 0; i < 13; ++i) query[i] = 0;
    query[0] = 40;
    check(high && high_bits && GetDIBits(dc, high, 0, 1, 0, query, 0) == 1 &&
              query[1] == 1 && query[2] == 1 && query[3] >> 16 == 16 &&
              query[4] == 3, 8499);
    check(GetDIBits(dc, high, 0, 1, 0, query, 0) == 1 && query[4] == 3 &&
              query[10] == 0x7c00 && query[11] == 0x3e0 && query[12] == 0x1f,
          8477);
    check(DeleteObject(high) && DeleteDC(dc), 8478);
  }
  /* An operation without a pattern does not ask for the brush: SRCINVERT
     from a 32-bit bitmap into a 24-bit section whose DC holds the hollow
     brush is the last step of Delphi's TransparentStretchBlt. And a
     24-bit section stretches into another one, as StretchDraw of a decoded
     JPEG does. */
  {
    DibHeader narrow = {40, 2, 1, 1, 24, 0, 0, 0, 0, 0, 0};
    DibHeader wide = {40, 4, 2, 1, 24, 0, 0, 0, 0, 0, 0};
    static const U colours[2] = {0x00102030, 0x00405060};
    void *narrow_raw = 0, *wide_raw = 0;
    const U section = CreateDIBSection(0, &narrow, 0, &narrow_raw, 0, 0);
    const U picture = CreateDIBSection(0, &wide, 0, &wide_raw, 0, 0);
    const U device = CreateBitmap(2, 1, 1, 32, colours);
    const U to = CreateCompatibleDC(0), from = CreateCompatibleDC(0);
    check(section && narrow_raw && picture && wide_raw && device && to && from, 8479);
    unsigned char *p = (unsigned char *)narrow_raw, *q = (unsigned char *)wide_raw;
    p[0] = p[1] = p[2] = 0x0f;
    p[3] = 0xff; p[4] = 0x00; p[5] = 0xff;
    const U old_to = SelectObject(to, section), old_from = SelectObject(from, device);
    const U old_brush = SelectObject(to, GetStockObject(5));
    check(old_to && old_from && old_brush, 8480);
    check(BitBlt(to, 0, 0, 2, 1, from, 0, 0, 0x00660046), 8481);
    check(p[0] == (0x0f ^ 0x30) && p[1] == (0x0f ^ 0x20) && p[2] == (0x0f ^ 0x10) &&
              p[3] == (0xff ^ 0x60) && p[4] == 0x50 && p[5] == (0xff ^ 0x40),
          8482);
    for (int row = 0; row < 2; ++row)
      for (int column = 0; column < 4; ++column) {
        unsigned char *pixel = q + row * 12 + column * 3;
        pixel[0] = column >= 2 ? 255 : 0;
        pixel[1] = 0;
        pixel[2] = column < 2 ? 255 : 0;
      }
    check(SelectObject(from, picture) == device, 8483);
    SetStretchBltMode(to, 3);
    check(StretchBlt(to, 0, 0, 2, 1, from, 0, 0, 4, 2, 0x00cc0020), 8484);
    check(p[0] == 0 && p[1] == 0 && p[2] == 255 && p[3] == 255 && p[4] == 0 && p[5] == 0, 8485);
    SelectObject(to, old_brush);
    SelectObject(to, old_to);
    SelectObject(from, old_from);
    check(DeleteDC(to) && DeleteDC(from) && DeleteObject(section) && DeleteObject(picture) &&
              DeleteObject(device),
          8486);
  }
}
