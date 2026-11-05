// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Native Win98 COMCTL32 DSA/DPA algorithms. Addresses remain 32-bit;
// allocation and memory access belong to the NEO98 address-space adapter.
namespace Win98Arrays {
using U = unsigned;
struct Memory {
  virtual bool Read(U address, void *out, U bytes) = 0;
  virtual bool Write(U address, const void *data, U bytes) = 0;
  virtual U Resize(U heap, U address,
                   U bytes) = 0; // null address allocates zero-filled memory
  virtual bool Free(U heap, U address) = 0;
  virtual int Compare(U procedure, U left, U right, U parameter) = 0;
  U Word(U address) {
    U n = 0;
    Read(address, &n, 4);
    return n;
  }
  void Word(U address, U n) { Write(address, &n, 4); }
  bool Move(U destination, U source, U bytes);

protected:
  ~Memory() = default;
};
// Win98 ordinals 320..340, excluding callbacks/notifications 341 and above.
U Call(Memory &, U ordinal, const U *arguments);
} // namespace Win98Arrays
