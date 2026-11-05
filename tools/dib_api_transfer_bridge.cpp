// SPDX-License-Identifier: GPL-2.0-or-later
#include "gdi.h"
#include <cstdlib>
#include <cstring>
struct TransferMemory final : Gdi::DibIO {
  unsigned char *info, *bits;
  unsigned error = 0;
  unsigned char *Pointer(unsigned p, unsigned n) {
    if (p >= 0x1000 && p - 0x1000 <= 1080 && n <= 1080 - (p - 0x1000))
      return info + (p - 0x1000);
    if (p >= 0x10000 && p - 0x10000 <= 65536 && n <= 65536 - (p - 0x10000))
      return bits + (p - 0x10000);
    return nullptr;
  }
  bool Read(unsigned p, void *out, unsigned n) override {
    auto *v = Pointer(p, n);
    if (!v)
      return false;
    memcpy(out, v, n);
    return true;
  }
  bool Write(unsigned p, const void *in, unsigned n) override {
    auto *v = Pointer(p, n);
    if (!v)
      return false;
    memcpy(v, in, n);
    return true;
  }
  bool Probe(unsigned p, bool) override { return Pointer(p, 1); }
  void Error(unsigned e) override { error = e; }
  void Fault(const char *) override { abort(); }
  bool AllocateDib(unsigned, Gdi::DibLease &) override { return false; }
  TransferMemory(unsigned char *i, unsigned char *b) : info(i), bits(b) {}
};
extern "C" unsigned dib_transfer(const unsigned *a, unsigned char *info,
                                 unsigned char *bits, unsigned char *device) {
  Gdi::BITMAPINFO bi{};
  bi.bmiHeader = {40, int(a[1]), -int(a[2]), 1, (unsigned short)a[3], 0, 0,
                  0,  0,         0,          0};
  void *host = nullptr;
  unsigned bitmap = Gdi::CreateDIBSection(0, &bi, 0, &host);
  if (!bitmap)
    abort();
  unsigned stride = ((a[1] * a[3] + 31) & ~31u) >> 3;
  memcpy(host, device, stride * a[2]);
  TransferMemory io(info, bits);
  unsigned result = 0;
  if (a[0] < 2) {
    unsigned args[] = {0, bitmap, a[4], a[5], 0x10000, 0x1000, 0};
    result = Gdi::DibApi(a[0] ? "GetDIBits" : "SetDIBits", args, io);
  } else {
    unsigned dc = Gdi::CreateCompatibleDC(),
             old = Gdi::SelectObject(dc, bitmap);
    if (!dc || !old)
      abort();
    if (a[0] == 2) {
      unsigned args[] = {dc, 0,    0,    a[1],    a[2],   0,
                         0,  a[4], a[5], 0x10000, 0x1000, 0};
      result = Gdi::DibApi("SetDIBitsToDevice", args, io);
    } else {
      unsigned args[] = {dc,   0,    0,       a[1],   a[2], 0,           0,
                         a[1], a[2], 0x10000, 0x1000, 0,    Gdi::SRCCOPY};
      result = Gdi::DibApi("StretchDIBits", args, io);
    }
    if (!Gdi::SelectObject(dc, old) || !Gdi::DeleteDC(dc))
      abort();
  }
  memcpy(device, host, stride * a[2]);
  if (!Gdi::DeleteObject(bitmap))
    abort();
  return result;
}
#ifdef DIB_API_TRANSFER_STANDALONE
#include <cstdio>
int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  FILE *f = fopen(argv[1], "rb");
  if (!f)
    return 2;
  unsigned a[6], value, n = 0;
  unsigned char info[1080], bits[65536], device[65536], afterinfo[1080],
      afterbits[65536], afterdevice[65536];
  while (fread(a, sizeof a, 1, f) == 1) {
    if (fread(&value, 4, 1, f) != 1 || fread(info, sizeof info, 1, f) != 1 ||
        fread(bits, sizeof bits, 1, f) != 1 ||
        fread(device, sizeof device, 1, f) != 1 ||
        fread(afterinfo, sizeof afterinfo, 1, f) != 1 ||
        fread(afterbits, sizeof afterbits, 1, f) != 1 ||
        fread(afterdevice, sizeof afterdevice, 1, f) != 1)
      return 3;
    if (dib_transfer(a, info, bits, device) != value ||
        memcmp(info, afterinfo, sizeof info) ||
        memcmp(bits, afterbits, sizeof bits) ||
        memcmp(device, afterdevice, sizeof device)) {
      fprintf(stderr, "transfer case %u\n", n);
      return 4;
    }
    ++n;
  }
  fclose(f);
  printf("DIB transfer ASan corpus: %u cases\n", n);
  return 0;
}
#endif
