/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The device level and font exports of the GDI32 import list, checked through
   the public x86 interface. Every answer here is the one Windows 98 gives on a
   machine with the display driver and without a printer, an OpenGL provider, a
   gamma capable driver or a single outline font. */
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
API U GetStockObject(U);
API U GetTextExtentPoint32A(U, const char *, int, void *);

API int Escape(U, int, int, const void *, void *);
API int ExtEscape(U, int, int, const void *, int, void *);
API int StartDocA(U, const void *);
API int StartDocW(U, const void *);
API int EndDoc(U);
API int StartPage(U);
API int EndPage(U);
API int AbortDoc(U);
API U SetAbortProc(U, void *);
API U ResetDCA(U, const void *);
API U GetObjectW(U, int, void *);
API U ChoosePixelFormat(U, const void *);
API int DescribePixelFormat(U, int, U, void *);
API U SetPixelFormat(U, int, const void *);
API int GetPixelFormat(U);
API U SwapBuffers(U);
API U GetDeviceGammaRamp(U, void *);
API U SetDeviceGammaRamp(U, const void *);
API int SetICMMode(U, int);

API U GetTextExtentExPointA(U, const char *, int, int, int *, int *, void *);
API U SetMapperFlags(U, U);
API U GetCharacterPlacementA(U, const char *, int, int, void *, U);
API U GetCharacterPlacementW(U, const void *, int, int, void *, U);
API U GetGlyphOutlineA(U, U, U, void *, U, void *, const void *);
API U GetFontData(U, U, U, void *, U);
API U GetOutlineTextMetricsA(U, U, void *);
API U GetKerningPairsA(U, U, void *);

static void check(int v, U n) {
  if (!v)
    ExitProcess(n);
}
static U header[11] = {40, 64, (U)-48, 1 | (32 << 16), 0, 0, 0, 0, 0, 0, 0};

void run_gdi_device_test(void) {
  unsigned char *raw = 0;
  U dc = CreateCompatibleDC(0);
  check(dc != 0, 18000);
  U bitmap = CreateDIBSection(dc, header, 0, (void **)&raw, 0, 0);
  check(bitmap && raw, 18001);
  check(SelectObject(dc, bitmap) != 0, 18002);

  /* Escape. GDI32 classifies the number before the thunk. The refused
     numbers never see the driver and never touch LastError. */
  {
    int query = 0;
    SetLastError(4711);
    check(Escape(dc, 4, 0, 0, 0) == -1, 18003);   /* SETCOLORTABLE */
    check(Escape(dc, 6, 0, 0, 0) == -1, 18004);   /* FLUSHOUTPUT */
    check(Escape(dc, 0x0c03, 0, 0, 0) == -1, 18005); /* DCICOMMAND */
    check(Escape(dc, 1, 0, 0, 0) == -1, 18006);   /* NEWFRAME, version gate */
    check(Escape(dc, 10, 0, 0, 0) == -1, 18007);  /* STARTDOC */
    check(GetLastError() == 4711, 18008);
    /* QUERYESCSUPPORT reaches the driver, and DIBENG knows five numbers. Its
       answer is not one: Windows 98 loads the marker 0x5250 for every number its
       Control entry knows, and only the marker itself answers one.
       DirectDraw reads this value and expects the DCI version
       0xff or 0x100 there, so the 0x5250 is what keeps it off the HAL path. */
    query = 8;
    check(Escape(dc, 8, 4, &query, 0) == 0x5250, 18009);
    query = 0x0c01;
    check(Escape(dc, 8, 4, &query, 0) == 0x5250, 18010);
    query = 0x0c03;
    check(Escape(dc, 8, 4, &query, 0) == 0x5250, 18066);
    query = 0x5250;
    check(Escape(dc, 8, 4, &query, 0) == 1, 18067);
    query = 0x25;
    check(Escape(dc, 8, 4, &query, 0) == 0, 18011);
    /* A refused number answers 0 for QUERYESCSUPPORT itself and -1 otherwise,
       which is what Windows 98 computes with ADC EAX,-1. */
    check(Escape(0, 8, 4, &query, 0) == -1, 18012);
    /* QUERYDIBSUPPORT: three bits at 32 bits per pixel. */
    check(Escape(dc, 0x0c01, 0, 0, 0) == 7, 18013);
    /* A number the driver does not know answers zero, not -1. */
    check(Escape(dc, 0x1000, 0, 0, 0) == 0, 18014);
    check(ExtEscape(dc, 0x1000, 0, 0, 0, 0) == 0, 18015);
    check(GetLastError() == 4711, 18016);
  }

  /* The print document group on a display context. */
  {
    static const U info[4] = {16, 0, 0, 0};
    SetLastError(4712);
    check(StartDocA(dc, info) == -1, 18017);
    check(StartPage(dc) == -1, 18018);
    check(EndPage(dc) == -1, 18019);
    check(EndDoc(dc) == -1, 18020);
    check(AbortDoc(dc) == -1, 18021);
    check(SetAbortProc(dc, 0) == 1, 18022);
    check(ResetDCA(dc, 0) == 0, 18023);
    check(GetLastError() == 4712, 18024);
  }

  /* The wide forms carry no code in this image: KERNEL32 ordinal 17 sets
     ERROR_CALL_NOT_IMPLEMENTED and answers zero. */
  {
    U space[16];
    SetLastError(0);
    check(StartDocW(dc, space) == 0 && GetLastError() == 120, 18025);
    SetLastError(0);
    check(GetObjectW(bitmap, 24, space) == 0 && GetLastError() == 120, 18026);
    SetLastError(0);
    check(GetCharacterPlacementW(dc, space, 0, 0, space, 0) == 0 &&
              GetLastError() == 120,
          18027);
  }

  /* The pixel format group: GetModuleHandleA("OPENGL32") finds nothing, so all
     five answer zero and leave LastError alone. */
  {
    U format[10];
    SetLastError(4713);
    check(ChoosePixelFormat(dc, format) == 0, 18028);
    check(DescribePixelFormat(dc, 1, 40, format) == 0, 18029);
    check(SetPixelFormat(dc, 1, format) == 0, 18030);
    check(GetPixelFormat(dc) == 0, 18031);
    check(SwapBuffers(dc) == 0, 18032);
    check(GetLastError() == 4713, 18033);
  }

  /* The gamma ramp: a bad buffer is the parameter error. This display driver
     carries the capability bit 20h at GDIINFO+5Eh (6d453a3, the ramp in front
     of the host image, src/x86_gdi_device.cpp), so GDI does not end
     in ERROR_NOT_SUPPORTED: the ramp reads back and writes unchanged. */
  {
    static unsigned short ramp[3 * 256];
    SetLastError(0);
    check(GetDeviceGammaRamp(dc, 0) == 0 && GetLastError() == 87, 18034);
    check(GetDeviceGammaRamp(dc, ramp) == 1 && ramp[255] >= ramp[0], 18035);
    check(SetDeviceGammaRamp(dc, ramp) == 1, 18036);
    SetLastError(0);
    check(SetICMMode(dc, 9) == 0 && GetLastError() == 87, 18037);
    SetLastError(0);
    check(SetICMMode(dc, 1) == 0 && GetLastError() == 8, 18038);
  }

  /* GetTextExtentExPointA measures prefixes, so alpDx is cumulative. */
  {
    static const char text[] = "Window";
    int widths[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int fit = 0, whole[2] = {0, 0}, size[2] = {0, 0};
    check(GetTextExtentPoint32A(dc, text, 6, whole), 18039);
    check(GetTextExtentExPointA(dc, text, 6, 0, 0, widths, size), 18040);
    check(size[0] == whole[0] && size[1] == whole[1], 18041);
    check(widths[5] == whole[0], 18042);
    check(widths[0] > 0 && widths[0] < widths[1] && widths[1] < widths[2],
          18043);
    /* With lpnFit the limit counts, and a prefix exactly as wide still fits. */
    check(GetTextExtentExPointA(dc, text, 6, widths[2], &fit, 0, size), 18044);
    check(fit == 3, 18045);
    check(size[0] == whole[0], 18046);
    check(GetTextExtentExPointA(dc, text, 6, 0, &fit, 0, size), 18047);
    check(fit == 0, 18048);
  }

  /* SetMapperFlags exchanges one DWORD and rejects every other bit. */
  {
    check(SetMapperFlags(dc, 1) == 0, 18049);
    check(SetMapperFlags(dc, 0) == 1, 18050);
    check(SetMapperFlags(dc, 2) == 0xffffffff, 18051);
    check(SetMapperFlags(dc, 0) == 0, 18052);
  }

  /* GetCharacterPlacementA on the generic Latin path. */
  {
    static const char text[] = "abc";
    U results[9];
    char out[8];
    U order[8];
    int dx[8], caret[8];
    char classes[8];
    unsigned short glyphs[8];
    U i;
    for (i = 0; i < 9; ++i)
      results[i] = 0;
    results[0] = sizeof results;
    results[1] = (U)out;
    results[2] = (U)order;
    results[3] = (U)dx;
    results[4] = (U)caret;
    results[5] = (U)classes;
    results[6] = (U)glyphs;
    check(GetCharacterPlacementA(dc, text, 3, 0, results, 0) != 0, 18053);
    check(results[7] == 3 && results[8] == 3, 18054);
    check(out[0] == 'a' && out[1] == 'b' && out[2] == 'c', 18055);
    check(order[0] == 0 && order[1] == 1 && order[2] == 2, 18056);
    check(classes[0] == 1 && classes[1] == 1 && classes[2] == 1, 18057);
    check(glyphs[0] == 'a' && glyphs[1] == 'b' && glyphs[2] == 'c', 18058);
    check(caret[0] == 0 && caret[1] == dx[0] && caret[2] == dx[0] + dx[1],
          18059);
  }

  /* The four outline queries. NEO98 installs only raster fonts, so all four
     take the failure exit of GDI segment 13 and the adapter sets error 87. */
  {
    U space[64];
    SetLastError(0);
    check(GetGlyphOutlineA(dc, 'A', 2, space, 0, 0, space) == 0xffffffff &&
              GetLastError() == 87,
          18060);
    SetLastError(0);
    check(GetFontData(dc, 0, 0, 0, 0) == 0xffffffff && GetLastError() == 87,
          18061);
    SetLastError(0);
    check(GetOutlineTextMetricsA(dc, 0, 0) == 0 && GetLastError() == 87, 18062);
    SetLastError(0);
    check(GetKerningPairsA(dc, 0, 0) == 0 && GetLastError() == 87, 18063);
  }

  check(DeleteDC(dc), 18064);
  check(DeleteObject(bitmap), 18065);
}

#ifdef GDI_DEVICE_STANDALONE
void mainCRTStartup(void) {
  run_gdi_device_test();
  ExitProcess(0);
}
#endif
