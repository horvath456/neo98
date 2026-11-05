/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_region.h"
#include "../src/win98_region_api.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using U = unsigned;
struct Block {
  U h, p, n, live;
};
struct Environment {
  unsigned char *m;
  std::vector<Block> blocks{{0x1000, 0x20000, 256, 1}};
  std::vector<U> trace;
  U cursor = 0x20110, attempt = 0, fail = 0;
  U Read8(U p) {
    if (p >= 0x70000)
      abort();
    return m[p];
  }
  U Read16(U p) { return Read8(p) | (Read8(p + 1) << 8); }
  U Read32(U p) { return Read16(p) | (Read16(p + 2) << 16); }
  void Write8(U p, U v) {
    if (p >= 0x70000)
      abort();
    m[p] = v;
  }
  void Write16(U p, U v) {
    Write8(p, v);
    Write8(p + 1, v >> 8);
  }
  void Write32(U p, U v) {
    Write16(p, v);
    Write16(p + 2, v >> 16);
  }
  void Move(U d, U s, U n) {
    for (U i = 0; i < n; ++i)
      Write8(d + i, Read8(s + i));
  }
  U Pointer(U h) { return Read32(0x10000 + h); }
  Block &Find(U h) {
    for (auto &b : blocks)
      if (b.h == h)
        return b;
    abort();
  }
  void Event(U k, U a, U b, U c) { trace.insert(trace.end(), {k, a, b, c}); }
  U CallerOwner() {
    Event(5, 0x3456, 0, 0);
    return 0x3456;
  }
  U Allocate(U n) {
    U h = 0;
    if (++attempt != fail) {
      h = 0x1000 + blocks.size() * 4;
      U p = cursor;
      cursor += n + 16;
      blocks.push_back({h, p, n, 1});
      Write32(0x10000 + h, p);
      std::memset(m + p, 0xcc, n);
    }
    Event(1, n, h, 0);
    return h;
  }
  U Reallocate(U h, U n) {
    U result = 0;
    if (++attempt != fail) {
      auto &b = Find(h);
      U p = cursor;
      cursor += n + 16;
      std::memset(m + p, 0xcc, n);
      std::memcpy(m + p, m + b.p, b.n < n ? b.n : n);
      std::memset(m + b.p, 0xdd, b.n);
      b.p = p;
      b.n = n;
      Write32(0x10000 + h, p);
      result = h;
    }
    Event(2, h, n, result);
    return result;
  }
  U Size(U h) {
    U n = Find(h).n;
    Event(3, h, n, 0);
    return n;
  }
  U Free(U h) {
    auto &b = Find(h);
    b.live = 0;
    std::memset(m + b.p, 0xdd, b.n);
    Event(4, h, 0, 0);
    return 0;
  }
};
extern "C" unsigned region_run(unsigned char *memory, const U *commands,
                               U count, U fail, U *returns, U *events,
                               U event_capacity) {
  Environment e{memory};
  e.fail = fail;
  Win98Region::State s{};
  s.serial = e.Read32(0x454);
  s.scratch_handle = 0x1000;
  s.scratch_bytes = 256;
  s.cached_count = e.Read16(0x37e);
  for (U i = 0; i < 8; ++i)
    s.cached[i] = e.Read16(0x380 + i * 2);
  for (U i = 0; i < count; ++i) {
    auto *c = commands + i * 6;
    U r = 0;
    switch (c[0]) {
    case 116:
      r = Win98RegionApi::Indirect(e, s, c[1]);
      break;
    case 101:
      r = Win98RegionApi::Create(e, s, c[1], c[2], c[3], c[4]);
      break;
    case 102:
      r = Win98RegionApi::Set(e, s, c[1], c[2], c[3], c[4], c[5]);
      break;
    case 103:
      r = Win98RegionApi::Combine(e, s, c[1], c[2], c[3], c[4]);
      break;
    case 104:
      r = Win98RegionApi::Offset(e, c[1], c[2], c[3]);
      break;
    case 105:
      r = Win98RegionApi::Equal(e, c[1], c[2]);
      break;
    case 106:
      r = Win98RegionApi::Point(e, c[1], c[2], c[3]);
      break;
    case 107:
      r = Win98RegionApi::Data(e, c[1], c[2], c[3]);
      break;
    case 109:
      r = Win98RegionApi::Box(e, c[1], c[2]);
      break;
    case 1:
      r = Win98Region::Create(e, s, {c[1], c[2], c[3], c[4]});
      break;
    case 2:
      r = Win98Region::Set(e, s, c[1], {c[2], c[3], c[4], c[5]});
      break;
    case 3:
      r = Win98Region::Combine(e, s, c[1], c[2], c[3], c[4]);
      break;
    case 4:
      r = Win98Region::Offset(e, c[1], c[2], c[3]);
      break;
    case 5:
      r = Win98Region::Equal(e, c[1], c[2]);
      break;
    case 6:
      r = Win98Region::Point(e, c[1], c[2], c[3]);
      break;
    case 7:
      r = Win98Region::Data(e, c[1], c[2], c[3]);
      break;
    case 8:
      r = Win98Region::Destroy(e, s, c[1]);
      break;
    case 10:
      r = Win98Region::ContainsRect(e, c[1], {c[2], c[3], c[4], c[5]});
      break;
    case 11:
      r = Win98Region::Append(e, s, c[1], {c[2], c[3], c[4], c[5]});
      break;
    case 12:
      e.Write32(c[1], c[2]);
      break;
    case 13:
      r = Win98Region::Import(e, s, c[1]);
      break;
    case 15:
      r = Win98Region::Clone(e, s, c[1]);
      break;
    case 14:
      e.fail = e.attempt + c[1];
      break;
    case 9:
      r = Win98Region::Complexity(e, c[1]);
      break;
    }
    returns[i] = r;
  }
  e.Write32(0x454, s.serial);
  e.Write16(0x37e, s.cached_count);
  for (U i = 0; i < 8; ++i)
    e.Write16(0x380 + i * 2, s.cached[i]);
  if (e.trace.size() > event_capacity)
    abort();
  std::memcpy(events, e.trace.data(), e.trace.size() * 4);
  return e.trace.size();
}
#ifdef REGION_STANDALONE
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  FILE *f = fopen(argv[1], "rb");
  if (!f)
    return 2;
  U n = 0, fail = 0, total = 0;
  while (fread(&n, 4, 1, f) == 1) {
    if (fread(&fail, 4, 1, f) != 1)
      return 3;
    std::vector<unsigned char> m(0x70000), want(m.size());
    std::vector<U> cmd(n * 6), r(n), expected(n), events(100000);
    U traces = 0;
    if (fread(m.data(), 1, m.size(), f) != m.size() ||
        fread(cmd.data(), 4, cmd.size(), f) != cmd.size() ||
        fread(expected.data(), 4, n, f) != n ||
        fread(want.data(), 1, want.size(), f) != want.size() ||
        fread(&traces, 4, 1, f) != 1)
      return 4;
    std::vector<U> et(traces);
    if (fread(et.data(), 4, traces, f) != traces)
      return 5;
    U got = region_run(m.data(), cmd.data(), n, fail, r.data(), events.data(),
                       events.size());
    if (r != expected || m != want || got != traces ||
        !std::equal(et.begin(), et.end(), events.begin()))
      return 6;
    ++total;
  }
  fclose(f);
  printf("Region corpus PASS: %u sequences\n", total);
}
#endif
