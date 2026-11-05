/* SPDX-License-Identifier: GPL-2.0-or-later */
/* WMF exports through the public x86 API: record, inspect record layouts, play back and compare pixels with direct drawing. Failure codes 16300..16599. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetLastError(void);
API void SetLastError(U);
API void OutputDebugStringA(const char *);
API int wvsprintfA(char *, const char *, const void *);
API U CreateCompatibleDC(U);
API U DeleteDC(U);
API U DeleteObject(U);
API U SelectObject(U, U);
API U CreateDIBSection(U, const void *, U, void **, U, U);
API U CreateSolidBrush(U);
API U CreatePen(int, int, U);
API U PatBlt(U, int, int, int, int, U);
API U StretchBlt(U, int, int, int, int, U, int, int, int, int, U);
API U GetStockObject(U);
API U SetBkColor(U, U);
API U SetTextColor(U, U);
API U SetBkMode(U, int);
API U MoveToEx(U, int, int, void *);
API U LineTo(U, int, int);
API U Rectangle(U, int, int, int, int);
API U Ellipse(U, int, int, int, int);
API U Polygon(U, const void *, int);
API U Polyline(U, const void *, int);
API U TextOutA(U, int, int, const char *, int);
API U ExtTextOutA(U, int, int, U, const void *, const char *, U, const int *);
API U SetTextAlign(U, U);
API U SetMapMode(U, int);
API U SetWindowExtEx(U, int, int, void *);
API U SetViewportExtEx(U, int, int, void *);
API U SaveDC(U);
API U RestoreDC(U, int);
API U CreateMetaFileA(const char *);
API U CloseMetaFile(U);
API U DeleteMetaFile(U);
API U CopyMetaFileA(U, const char *);
API U GetMetaFileA(const char *);
API U GetMetaFileBitsEx(U, U, void *);
API U SetMetaFileBitsEx(U, const void *);
API U PlayMetaFile(U, U);
API U PlayMetaFileRecord(U, void *, const void *, U);
API U EnumMetaFile(U, U, void *, U);
API U CreateEnhMetaFileA(U, const char *, const void *, const char *);
API U CloseEnhMetaFile(U);
API U DeleteEnhMetaFile(U);
API U CopyEnhMetaFileA(U, const char *);
API U GetEnhMetaFileBits(U, U, void *);
API U SetEnhMetaFileBits(U, const void *);
API U GetWinMetaFileBits(U, U, void *, int, U);
API U SetWinMetaFileBits(U, const void *, U, const void *);
API U GetEnhMetaFileHeader(U, U, void *);
API U GetEnhMetaFilePaletteEntries(U, U, void *);
API U GetEnhMetaFileDescriptionA(U, U, char *);
API U PlayEnhMetaFile(U, U, const void *);
API U PlayEnhMetaFileRecord(U, void *, const void *, U);
API U EnumEnhMetaFile(U, U, void *, U, const void *);
API U gdiPlaySpoolStream(U, U, U, U, U);

static void check(int v, U n) {
  if (!v)
    ExitProcess(n);
}
enum { W = 64, H = 48 };
static unsigned char *canvas[2];
static U dcs[2];
static U MakeSurface(U index) {
  U header[11] = {40, W, (U)-H, 1 | (32 << 16), 0, 0, 0, 0, 0, 0, 0};
  U dc = CreateCompatibleDC(0);
  check(dc != 0, 16300 + index);
  unsigned char *raw = 0;
  U bitmap = CreateDIBSection(dc, header, 0, (void **)&raw, 0, 0);
  check(bitmap != 0 && raw != 0, 16302 + index);
  check(SelectObject(dc, bitmap) != 0, 16304 + index);
  check(PatBlt(dc, 0, 0, W, H, 0xff0062) != 0, 16306 + index);
  canvas[index] = raw;
  dcs[index] = dc;
  return dc;
}
static U u16(const unsigned char *p) { return p[0] | ((U)p[1] << 8); }
static U u32(const unsigned char *p) { return u16(p) | (u16(p + 2) << 16); }

/* Apply the same drawing sequence to the metafile and target surface. */
static const int triangle[6] = {4, 40, 20, 46, 36, 40};
static const int *text_advances;
static void Draw(U dc, U pen, U brush) {
  check(SetBkColor(dc, 0x00ffffff) != 0xffffffff, 16310);
  check(SetBkMode(dc, 2) != 0, 16311);
  check(SetTextColor(dc, 0x00000000) != 0xffffffff, 16312);
  check(SelectObject(dc, pen) != 0, 16313);
  check(SelectObject(dc, brush) != 0, 16314);
  check(MoveToEx(dc, 2, 2, 0) != 0, 16315);
  check(LineTo(dc, 60, 2) != 0, 16316);
  check(Rectangle(dc, 4, 6, 30, 22) != 0, 16317);
  check(Ellipse(dc, 34, 6, 60, 22) != 0, 16318);
  check(SaveDC(dc) != 0, 16319);
  check(SetTextAlign(dc, 0) != 0xffffffff, 16320);
  check(text_advances ? ExtTextOutA(dc, 4, 26, 0, 0, "NEO98", 5, text_advances) != 0
                      : TextOutA(dc, 4, 26, "NEO98", 5) != 0, 16321);
  check(RestoreDC(dc, -1) != 0, 16322);
  check(Polygon(dc, triangle, 3) != 0, 16323);
  check(Polyline(dc, triangle, 3) != 0, 16324);
}
static U records_seen;
static U __stdcall Counter(U dc, void *table, const void *record, U objects,
                           U data) {
  (void)dc;
  (void)table;
  (void)objects;
  (void)data;
  check(u32((const unsigned char *)record) >= 3, 16360);
  ++records_seen;
  return 1;
}
/* The enumeration callback deletes its own metafile. Playback must finish using a snapshot rather than freed record storage. */
static U killer_target, killer_seen;
static U __stdcall Killer(U dc, void *table, const void *record, U objects,
                          U data) {
  (void)dc;
  (void)table;
  (void)objects;
  (void)data;
  check(u32((const unsigned char *)record) >= 3, 16549);
  if (!killer_seen)
    check(DeleteMetaFile(killer_target) != 0, 16550);
  ++killer_seen;
  return 1;
}
static U enhanced_seen;

static U __stdcall EnhancedCounter(U dc, void *table, const void *record, U objects,
                                   U data) {
  (void)dc;
  (void)table;
  (void)objects;
  (void)data;
  /* An EMF record needs at least eight bytes for iType and nSize. */
  check(u32((const unsigned char *)record) >= 1, 16373);
  check(u32((const unsigned char *)record + 4) >= 8, 16374);
  ++enhanced_seen;
  return 1;
}
static U enhanced_killer_target, enhanced_killer_seen;
static U __stdcall EnhancedKiller(U dc, void *table, const void *record, U objects,
                                  U data) {
  (void)dc;
  (void)table;
  (void)objects;
  (void)data;
  check(u32((const unsigned char *)record + 4) >= 8, 16551);
  if (!enhanced_killer_seen)
    check(DeleteEnhMetaFile(enhanced_killer_target) != 0, 16552);
  ++enhanced_killer_seen;
  return 1;
}

/* Paint's selection is retained after its source bitmap changes. */
static void StretchMetafile(void) {
  U info[10] = {40, 7, (U)-5, 1 | (32 << 16)};
  U source = CreateCompatibleDC(0), *pixels = 0;
  U bitmap = CreateDIBSection(source, info, 0, (void **)&pixels, 0, 0);
  check(source && bitmap && pixels, 16570);
  U old = SelectObject(source, bitmap);
  for (U i = 0; i < 35; ++i) pixels[i] = (i * 17057) & 0xffffff;
  U meta = CreateMetaFileA(0);
  check(StretchBlt(meta, 3, 4, 14, 10, source, 0, 0, 7, 5, 0xcc0020), 16571);
  check(StretchBlt(meta, 20, 4, 9, 6, source, 1, 1, 3, 2, 0xcc0020), 16572);
  U file = CloseMetaFile(meta);
  static unsigned char image[4096];
  U bytes = GetMetaFileBitsEx(file, sizeof image, image);
  check(bytes == 18 + 2 * (6 + 20 + 40 + 24 * 5) + 6, 16573);
  check(u16(image + 22) == 0x0b41 && u32(image + 24) == 0xcc0020, 16574);
  check(u32(image + 44) == 40 && u32(image + 48) == 7 && u32(image + 52) == 5, 16575);
  check(u16(image + 58) == 24, 16576);
  for (U row = 0; row < 5; ++row)
    for (U x = 0; x < 7; ++x) {
      unsigned char *p = image + 84 + row * 24 + x * 3;
      check((p[0] | ((U)p[1] << 8) | ((U)p[2] << 16)) == pixels[(4-row)*7+x], 16577);
    }
  check(PatBlt(dcs[0], 0, 0, W, H, 0xff0062), 16578);
  check(PatBlt(dcs[1], 0, 0, W, H, 0xff0062), 16579);
  check(StretchBlt(dcs[1], 3, 4, 14, 10, source, 0, 0, 7, 5, 0xcc0020), 16580);
  check(StretchBlt(dcs[1], 20, 4, 9, 6, source, 1, 1, 3, 2, 0xcc0020), 16581);
  SelectObject(source, old);
  check(DeleteObject(bitmap) && DeleteDC(source), 16582);
  check(PlayMetaFile(dcs[0], file), 16583);
  for (U i = 0; i < W*H*4; ++i)
    if ((i & 3) != 3) check(canvas[0][i] == canvas[1][i], 16584);
  check(DeleteMetaFile(file), 16585);
}

void run_gdi_metafile_test(void) {
  U target = MakeSurface(0), direct = MakeSurface(1);
  U pen = CreatePen(0, 1, 0x00000080);
  U brush = CreateSolidBrush(0x0000d0d0);
  check(pen != 0 && brush != 0, 16330);

  /* CreateMetaFile returns a recording DC. */
  U meta = CreateMetaFileA(0);
  check(meta != 0, 16331);
  Draw(meta, pen, brush);
  U file = CloseMetaFile(meta);
  check(file != 0, 16332);

  /* Header written by CLOSEMETAFILE. */
  U bytes = GetMetaFileBitsEx(file, 0, 0);
  check(bytes > 18 && bytes < 32768, 16333);
  static unsigned char image[32768];
  check(GetMetaFileBitsEx(file, bytes, image) == bytes, 16334);
  check(u16(image) == 1, 16335);          /* mtType: Speicher */
  check(u16(image + 2) == 9, 16336);      /* mtHeaderSize in WORDs */
  check(u16(image + 4) == 0x0300, 16337); /* mtVersion */
  check(u32(image + 6) * 2 == bytes, 16338);
  check(u16(image + 10) == 2, 16339); /* mtNoObjects counts the pen and brush. */

  /* The first record is META_SETBKCOLOR with the color in rdParm. */
  check(u32(image + 18) == 5 && u16(image + 22) == 0x0201, 16340);
  check(u32(image + 24) == 0x00ffffff, 16341);
  /* Find META_RECTANGLE with four edges stored in reverse argument order. */
  U at = 18, found = 0, maximum = 0, count = 0;
  while (at + 6 <= bytes) {
    U size = u32(image + at), function = u16(image + at + 4);
    check(size >= 3 && at + size * 2 <= bytes, 16342);
    if (size > maximum)
      maximum = size;
    if (!function) {
      check(at + 6 == bytes, 16343);
      break;
    }
    ++count;
    if (function == 0x041b) {
      check(size == 7, 16344);
      check((short)u16(image + at + 6) == 22, 16345);
      check((short)u16(image + at + 8) == 30, 16346);
      check((short)u16(image + at + 10) == 6, 16347);
      check((short)u16(image + at + 12) == 4, 16348);
      found = 1;
    }
    at += size * 2;
  }
  check(found != 0, 16349);
  check(u32(image + 12) == maximum, 16350); /* mtMaxRecord */

  /* Compare playback with direct drawing. */
  check(PlayMetaFile(target, file) != 0, 16351);
  Draw(direct, pen, brush);
  for (U y = 0; y < H; ++y)
    for (U x = 0; x < W; ++x) {
      U a = *(U *)(canvas[0] + (y * W + x) * 4) & 0xffffff;
      U b = *(U *)(canvas[1] + (y * W + x) * 4) & 0xffffff;
      check(a == b, 16400);
    }

  /* Import and replay the same serialized image. */
  U again = SetMetaFileBitsEx(bytes, image);
  check(again != 0 && again != file, 16352);
  check(GetMetaFileBitsEx(again, bytes, 0) == bytes, 16353);
  check(PatBlt(target, 0, 0, W, H, 0xff0062) != 0, 16354);
  check(PlayMetaFile(target, again) != 0, 16355);
  for (U y = 0; y < H; ++y)
    for (U x = 0; x < W; ++x)
      check((*(U *)(canvas[0] + (y * W + x) * 4) & 0xffffff) ==
                (*(U *)(canvas[1] + (y * W + x) * 4) & 0xffffff),
            16356);

  /* Windows 98 EnumMetaFile stops before META_EOF. */
  records_seen = 0;
  check(EnumMetaFile(target, file, (void *)Counter, 0) != 0, 16357);
  check(records_seen == count, 16358);

  /* Delete the metafile on the first enumeration callback. All records must still be visited, and the old handle must be invalid afterwards. */
  U doomed = CopyMetaFileA(file, 0);
  check(doomed != 0 && doomed != file, 16553);
  killer_target = doomed;
  killer_seen = 0;
  check(EnumMetaFile(target, doomed, (void *)Killer, 0) != 0, 16554);
  check(killer_seen == count, 16555);
  SetLastError(0);
  check(DeleteMetaFile(doomed) == 0 && GetLastError() == 6, 16556);


  /* PlayMetaFileRecord with a caller-owned handle table. META_SETBKCOLOR requires no objects. */
  U table[4] = {0, 0, 0, 0};
  check(PlayMetaFileRecord(target, table, image + 18, 4) != 0, 16359);

  /* A closed metafile is no longer a DC; unrelated handles must fail. */
  SetLastError(0);
  check(CloseMetaFile(file) == 0 && GetLastError() == 6, 16361);
  SetLastError(0);
  check(GetMetaFileBitsEx(0, 0, 0) == 0 && GetLastError() == 6, 16362);
  SetLastError(0);
  check(PlayMetaFile(target, target) == 0 && GetLastError() == 6, 16363);
  SetLastError(0);
  check(DeleteMetaFile(pen) == 0 && GetLastError() == 6, 16364);

  /* A file-backed metafile writes its bytes to disk, uses the file mtType and can be read with GetMetaFileA. */
  static const char path[] = "C:\\NEO98.WMF";
  U disk = CreateMetaFileA(path);
  check(disk != 0, 16380);
  Draw(disk, pen, brush);
  U saved = CloseMetaFile(disk);
  if (!saved) ExitProcess(17000 + GetLastError());
  static unsigned char stored[32768];
  U stored_bytes = GetMetaFileBitsEx(saved, 0, 0);
  check(stored_bytes == bytes, 16382);
  check(GetMetaFileBitsEx(saved, stored_bytes, stored) == stored_bytes, 16383);
  check(u16(stored) == 2, 16384); /* mtType: file */
  check(DeleteMetaFile(saved) != 0, 16385);
  U reread = GetMetaFileA(path);
  check(reread != 0, 16386);
  check(GetMetaFileBitsEx(reread, 0, 0) == stored_bytes, 16387);
  check(PatBlt(target, 0, 0, W, H, 0xff0062) != 0, 16388);
  check(PlayMetaFile(target, reread) != 0, 16389);
  for (U y = 0; y < H; ++y)
    for (U x = 0; x < W; ++x)
      check((*(U *)(canvas[0] + (y * W + x) * 4) & 0xffffff) ==
                (*(U *)(canvas[1] + (y * W + x) * 4) & 0xffffff),
            16390);
  check(DeleteMetaFile(reread) != 0, 16391);
  SetLastError(0);
  check(GetMetaFileA("C:\\NOTHERE.WMF") == 0 &&
            GetLastError() == 2,
        16392);

  U copy = CopyMetaFileA(file, 0);
  check(copy != 0 && copy != file, 16365);
  check(GetMetaFileBitsEx(copy, bytes, 0) == bytes, 16366);

  check(DeleteMetaFile(copy) != 0, 16367);
  check(DeleteMetaFile(again) != 0, 16368);
  check(DeleteMetaFile(file) != 0, 16369);
  SetLastError(0);
  check(DeleteMetaFile(file) == 0 && GetLastError() == 6, 16370);

  /* ---------------------------------------------------------------- */
#ifndef GDI_METAFILE_WMF_ONLY
  /* Repeat the same checks for enhanced metafiles. */
  check(PatBlt(dcs[0], 0, 0, W, H, 0xff0062) != 0, 16500);
  check(PatBlt(dcs[1], 0, 0, W, H, 0xff0062) != 0, 16501);
  U enh = CreateEnhMetaFileA(0, 0, 0, "NEO98\0Metafile\0");
  check(enh != 0, 16502);
  Draw(enh, pen, brush);
  U emf = CloseEnhMetaFile(enh);
  check(emf != 0, 16503);

  U emf_bytes = GetEnhMetaFileBits(emf, 0, 0);
  check(emf_bytes > 88 && emf_bytes < 32768, 16504);
  static unsigned char record[32768];
  check(GetEnhMetaFileBits(emf, emf_bytes, record) == emf_bytes, 16505);
  check(u32(record) == 1, 16506);          /* iType EMR_HEADER */
  check(u32(record + 40) == 0x464d4520, 16507); /* ' EMF' */
  check(u32(record + 44) == 0x00010000, 16508);
  check(u32(record + 48) == emf_bytes, 16509);
  check(u16(record + 56) >= 2, 16510); /* nHandles: slot zero plus objects */
  /* The header is itself a record, followed by its description. */
  U head = u32(record + 4);
  check(head == 88 + u32(record + 60) * 2 ||
            head == ((88 + u32(record + 60) * 2 + 3) & ~3u),
        16511);
  check(GetEnhMetaFileHeader(emf, 0, 0) == head, 16512);
  static unsigned char copy_head[256];
  check(GetEnhMetaFileHeader(emf, 88, copy_head) == 88, 16513);
  check(u32(copy_head + 40) == 0x464d4520, 16514);
  static char text[64];
  U described = GetEnhMetaFileDescriptionA(emf, sizeof text, text);
  check(described >= 6, 16515);
  check(text[0] == 'A' && text[4] == '8' && text[5] == 0, 16516);
  check(GetEnhMetaFilePaletteEntries(emf, 0, 0) == 0, 16517);

  /* Record chain from header to end record. */
  U emf_at = head, emf_records = 1, saw_rectangle = 0;
  while (emf_at + 8 <= emf_bytes) {
    U type = u32(record + emf_at), size = u32(record + emf_at + 4);
    check(size >= 8 && (size & 3) == 0 && emf_at + size <= emf_bytes, 16518);
    ++emf_records;
    if (type == 43) { /* EMR_RECTANGLE */
      check(size == 24, 16519);
      check((int)u32(record + emf_at + 8) == 4, 16520);
      check((int)u32(record + emf_at + 12) == 6, 16521);
      check((int)u32(record + emf_at + 16) == 30, 16522);
      check((int)u32(record + emf_at + 20) == 22, 16523);
      saw_rectangle = 1;
    }
    if (type == 14) { /* EMR_EOF */
      check(emf_at + size == emf_bytes, 16524);
      break;
    }
    emf_at += size;
  }
  check(saw_rectangle != 0, 16525);
  check(u32(record + 52) == emf_records, 16526);

  /* Playback without mapping matches direct drawing. */
  check(PlayEnhMetaFile(target, emf, 0) != 0, 16527);
  Draw(direct, pen, brush);
  for (U y = 0; y < H; ++y)
    for (U x = 0; x < W; ++x)
      check((*(U *)(canvas[0] + (y * W + x) * 4) & 0xffffff) ==
                (*(U *)(canvas[1] + (y * W + x) * 4) & 0xffffff),
            16528);

  /* Repeat through serialized bytes. */
  U emf_again = SetEnhMetaFileBits(emf_bytes, record);
  check(emf_again != 0 && emf_again != emf, 16529);
  check(GetEnhMetaFileBits(emf_again, 0, 0) == emf_bytes, 16530);
  check(PatBlt(target, 0, 0, W, H, 0xff0062) != 0, 16531);
  check(PlayEnhMetaFile(target, emf_again, 0) != 0, 16532);
  for (U y = 0; y < H; ++y)
    for (U x = 0; x < W; ++x)
      check((*(U *)(canvas[0] + (y * W + x) * 4) & 0xffffff) ==
                (*(U *)(canvas[1] + (y * W + x) * 4) & 0xffffff),
            16533);

  enhanced_seen = 0;
  /* With a DC Win98 requires a destination rectangle. */
  const int enum_rect[4] = {0, 0, W, H};
  check(EnumEnhMetaFile(target, emf, (void *)EnhancedCounter, 0, enum_rect) != 0, 16534);
  check(enhanced_seen == emf_records, 16535);

  /* Repeat deletion during enhanced-metafile enumeration. */
  U emf_doomed = SetEnhMetaFileBits(emf_bytes, record);
  check(emf_doomed != 0 && emf_doomed != emf, 16557);
  enhanced_killer_target = emf_doomed;
  enhanced_killer_seen = 0;
  check(EnumEnhMetaFile(target, emf_doomed, (void *)EnhancedKiller, 0, enum_rect) != 0, 16558);
  check(enhanced_killer_seen == emf_records, 16559);
  SetLastError(0);
  check(DeleteEnhMetaFile(emf_doomed) == 0 && GetLastError() == 6, 16560);


  U emf_table[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  check(PlayEnhMetaFileRecord(target, emf_table, record, 8) != 0, 16536);

  /* rclFrame is physical (0.01 mm); rclBounds is only a pixel bound.
     The recorded frame must play successfully, but rounding its physical
     edges need not reproduce the unscaled bitmap pixel for pixel. */
  int frame[4] = {(int)u32(record + 8), (int)u32(record + 12),
                  (int)u32(record + 16) + 1, (int)u32(record + 20) + 1};
  check(PatBlt(target, 0, 0, W, H, 0xff0062) != 0, 16546);
  check(PlayEnhMetaFile(target, emf, frame) != 0, 16547);

  /* A controlled physical frame covers the complete recording device.
     Playing into half that device must match direct MM_ANISOTROPIC 1:2
     drawing. This deliberately keeps a nonidentity pen realization. */
  U geometry_dc = CreateEnhMetaFileA(0, 0, 0, "NEO98\0Mapped geometry\0");
  check(geometry_dc != 0, 16570);
  Draw(geometry_dc, pen, brush);
  U geometry = CloseEnhMetaFile(geometry_dc);
  check(geometry != 0, 16571);
  U geometry_bytes = GetEnhMetaFileBits(geometry, 0, 0);
  static unsigned char geometry_record[32768];
  check(geometry_bytes <= sizeof geometry_record, 16572);
  check(GetEnhMetaFileBits(geometry, geometry_bytes, geometry_record) == geometry_bytes, 16573);
  /* Win98 GDI passes the EMF's explicit advances to ExtTextOut.
     A raster font may keep its glyph size while these advances are scaled.
     The direct comparison must therefore use the recorded advances too. */
  int recorded_advances[5];
  U found_advances = 0;
  for (U at = u32(geometry_record + 4); at + 8 <= geometry_bytes;) {
    U kind = u32(geometry_record + at), size = u32(geometry_record + at + 4);
    check(size >= 8 && size <= geometry_bytes - at, 16587);
    if (kind == 83 || kind == 84) {
      check(size >= 76 && u32(geometry_record + at + 44) == 5, 16588);
      U offset = u32(geometry_record + at + 72);
      check(offset >= 76 && offset <= size && size - offset >= 20, 16589);
      for (U i = 0; i < 5; ++i) recorded_advances[i] = (int)u32(geometry_record + at + offset + i * 4);
      ++found_advances;
    }
    at += size;
  }
  check(found_advances == 1, 16590);
  U *physical = (U *)(geometry_record + 24);
  physical[0] = physical[1] = 0;
  physical[2] = u32(geometry_record + 80) * 100;
  physical[3] = u32(geometry_record + 84) * 100;
  U mapped = SetEnhMetaFileBits(geometry_bytes, geometry_record);
  check(DeleteEnhMetaFile(geometry) != 0, 16574);
  check(mapped != 0, 16561);
  frame[0] = frame[1] = 0;
  frame[2] = (int)u32(record + 72) / 2;
  frame[3] = (int)u32(record + 76) / 2;
  check(PatBlt(target, 0, 0, W, H, 0xff0062) != 0, 16562);
  check(PatBlt(direct, 0, 0, W, H, 0xff0062) != 0, 16563);
  check(PlayEnhMetaFile(target, mapped, frame) != 0, 16564);
  check(SaveDC(direct) != 0, 16565);
  check(SetMapMode(direct, 8) != 0, 16566);
  check(SetWindowExtEx(direct, 2, 2, 0) != 0, 16567);
  check(SetViewportExtEx(direct, 1, 1, 0) != 0, 16568);
  text_advances = recorded_advances;
  Draw(direct, pen, brush);
  text_advances = 0;
  check(RestoreDC(direct, -1) != 0, 16569);
  for (U y = 0; y < H; ++y)
    for (U x = 0; x < W; ++x) {
      U actual = *(U *)(canvas[0] + (y * W + x) * 4) & 0xffffff;
      U expected = *(U *)(canvas[1] + (y * W + x) * 4) & 0xffffff;
      if (actual != expected) {
        char detail[128];
        U values[] = {x, y, actual, expected};
        wvsprintfA(detail, "EMF scaled mismatch x=%u y=%u actual=%06x expected=%06x", values);
        OutputDebugStringA(detail);
        check(0, 16548);
      }
    }
  check(DeleteEnhMetaFile(mapped) != 0, 16586);

  U emf_copy = CopyEnhMetaFileA(emf, 0);
  check(emf_copy != 0 && emf_copy != emf, 16537);
  check(GetEnhMetaFileBits(emf_copy, 0, 0) == emf_bytes, 16538);
  check(DeleteEnhMetaFile(emf_copy) != 0, 16539);

  /* WMF and EMF use separate handle spaces. */
  SetLastError(0);
  check(PlayMetaFile(target, emf) == 0 && GetLastError() == 6, 16540);
  SetLastError(0);
  check(DeleteEnhMetaFile(again) == 0 && GetLastError() == 6, 16541);
  SetLastError(0);
  check(gdiPlaySpoolStream(0, 0, 0, 0, 0) == 0 && GetLastError() == 50, 16542);

  check(DeleteEnhMetaFile(emf_again) != 0, 16543);
  /* Convert between metafile formats, as used by Delphi. */
  U wmf_bytes = GetWinMetaFileBits(emf, 0, 0, 8, 0);
  check(wmf_bytes > 18 && wmf_bytes < 32768, 16550);
  static unsigned char old_bits[32768];
  check(GetWinMetaFileBits(emf, wmf_bytes, old_bits, 8, 0) == wmf_bytes, 16551);
  check(u16(old_bits) == 1 && u16(old_bits + 2) == 9, 16552); /* mtType, mtHeaderSize */
  check(u32(old_bits + 6) * 2 == wmf_bytes, 16553);           /* mtSize in Worten */
  SetLastError(0);
  check(GetWinMetaFileBits(0xdead, 0, 0, 8, 0) == 0 && GetLastError() == 6, 16554);
  U back = SetWinMetaFileBits(wmf_bytes, old_bits, 0, 0);
  check(back != 0 && back != emf, 16555);
  U back_bytes = GetEnhMetaFileBits(back, 0, 0);
  check(back_bytes > 88, 16556);
  static unsigned char back_record[32768];
  check(back_bytes < sizeof back_record && GetEnhMetaFileBits(back, back_bytes, back_record) == back_bytes, 16557);
  check(u32(back_record + 40) == 0x464d4520 && u32(back_record + 52) > 2, 16558); /* ' EMF', nRecords */
  check(SetWinMetaFileBits(0, 0, 0, 0) == 0, 16559);
  check(DeleteEnhMetaFile(back) != 0, 16560);
  check(DeleteEnhMetaFile(emf) != 0, 16544);
  SetLastError(0);
  check(DeleteEnhMetaFile(emf) == 0 && GetLastError() == 6, 16545);

#endif
  StretchMetafile();

  /* Release DCs before their objects; selected objects cannot be deleted. */
  check(DeleteDC(dcs[0]) != 0 && DeleteDC(dcs[1]) != 0, 16372);
  check(DeleteObject(pen) != 0, 16371);
  check(DeleteObject(brush) != 0, 16375);
}
#ifdef GDI_METAFILE_STANDALONE
void mainCRTStartup(void) {
#ifdef GDI_METAFILE_STRETCH_ONLY
  MakeSurface(0); MakeSurface(1);
  StretchMetafile();
#else
  run_gdi_metafile_test();
#endif
  ExitProcess(0);
}
#endif
