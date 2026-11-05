// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Native COMCTL32 tree navigation, with a native node-access adapter.
namespace Win98Tree {
using U = unsigned;
struct Node {
  U parent = 0, next = 0, first = 0, state = 0, depth = 0, row = 0xffff,
    height = 1;
  bool valid = false;
};
struct View {
  virtual Node Get(U item) const = 0;
  U root = 0, first_visible = 0, selected = 0, drop = 0, rows = 0;

protected:
  ~View() = default;
};
U Next(const View &, U item, U relationship);
} // namespace Win98Tree
