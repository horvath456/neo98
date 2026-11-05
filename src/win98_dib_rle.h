// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Run length decoder for BI_RLE4 and BI_RLE8 wire images.
//
// Win98 keeps no compressed surface: DIBENG refuses a compressed descriptor
// (CreateDIBPDevice passes maximumCompression 0, implemented in
// win98_dib_layout.h as Validate/CreateDevice), while the transfer descriptor
// accepts compression 1 and 2 and marks the missing stride by setting the sign
// bit of scans (DIBENG, Win98Dib::TransferDevice). GDI therefore expands
// the stream into an ordinary scanline image before it blits. The three rules
// below are read out of the original binaries:
//
// GDI For a compression other than 0 and 3 the byte count of
// the wire image is biSizeImage, and a zero biSizeImage
// is treated as one byte. Win98Dib::ProbeBits already
// carries this arm.
// GDI The source surface builder accepts a compression up to
// 2 with a non negative biHeight and a depth of 1, 4, 8
// or 24, and fails otherwise. A compressed top down DIB
// does not exist.
// DIBENG The RLE4 writer of the DIB engine emits the stream
// this decoder reads. Windows 98 writes an encoded pair as
// count byte and packed nibble pair, Windows 98 writes the
// escape word 0x0500, that is byte 0 followed by the
// absolute count, Windows 98 writes the absolute payload,
// and rounds the payload length up to a
// word by adding the low bit of its byte count to the
// output pointer. The odd absolute run therefore carries
// a pad byte.
//
// The escape codes themselves are the published wire format: a leading zero
// selects end of line (0), end of bitmap (1), a delta (2, followed by a column
// and a row step) and otherwise an absolute run of that many pixels.
//
// A pixel that a delta, an end of line or a short end of bitmap never reaches
// stays untouched. GDI keeps that promise because it blits only the covered
// part of the expanded image, so the decoder reports coverage separately
// instead of filling the gaps with a colour.
namespace Win98DibRle {
using U = unsigned;
// Target geometry of the expanded image. The rows follow the wire order of an
// uncompressed bottom up DIB: row zero of the stream is row zero of the buffer.
struct Target {
  unsigned char *pixels;  // stride bytes per row, height rows
  unsigned char *coverage; // width bytes per row, height rows, 0 or 1
  U width, height, stride, depth;
};
inline void Put(const Target &t, U x, U y, U value) {
  if (x >= t.width || y >= t.height)
    return;
  unsigned char *row = t.pixels + y * t.stride;
  if (t.depth == 4) {
    const U shift = (1 - (x & 1)) * 4;
    row[x >> 1] = (row[x >> 1] & ~(15u << shift)) | ((value & 15) << shift);
  } else
    row[x] = value & 255;
  t.coverage[y * t.width + x] = 1;
}
// Expands one stream. Returns false only when the caller asked for a depth this
// decoder does not carry; a truncated or malformed stream stops like the
// original and leaves the remaining pixels uncovered.
inline bool Decode(const unsigned char *stream, U bytes, const Target &t) {
  if (t.depth != 4 && t.depth != 8)
    return false;
  U at = 0, x = 0, y = 0;
  while (at + 1 < bytes) {
    const U count = stream[at], value = stream[at + 1];
    at += 2;
    if (count) {
      for (U i = 0; i < count; ++i)
        Put(t, x + i, y,
            t.depth == 4 ? ((i & 1) ? (value & 15) : (value >> 4)) : value);
      x += count;
      continue;
    }
    if (value == 0) {
      x = 0;
      ++y;
      continue;
    }
    if (value == 1)
      break;
    if (value == 2) {
      if (at + 1 >= bytes)
        break;
      x += stream[at];
      y += stream[at + 1];
      at += 2;
      continue;
    }
    // Absolute mode. RLE8 spends one byte per pixel, RLE4 one nibble, and the
    // payload is padded to a word boundary.
    const U payload = t.depth == 4 ? (value + 1) / 2 : value;
    if (at + payload > bytes)
      break;
    for (U i = 0; i < value; ++i) {
      const unsigned char byte = stream[at + (t.depth == 4 ? i / 2 : i)];
      Put(t, x + i, y,
          t.depth == 4 ? ((i & 1) ? (byte & 15) : (byte >> 4)) : byte);
    }
    x += value;
    at += payload + (payload & 1);
  }
  return true;
}
} // namespace Win98DibRle
