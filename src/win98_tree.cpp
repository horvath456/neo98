// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_tree.h"
namespace Win98Tree {
namespace {
// Original : descend if expanded, then ascend to a next sibling.
U Forward(const View &v, U h) {
  auto node = v.Get(h);
  if (node.first && (node.state & 0x20))
    return node.first;
  for (U budget = 0; h && budget < 4096; ++budget) {
    node = v.Get(h);
    if (!node.valid)
      return 0;
    if (node.next)
      return node.next;
    h = node.parent;
  }
  return 0;
}
U PreviousSibling(const View &v, U h) {
  auto node = v.Get(h);
  U item = v.Get(node.parent).first, previous = 0;
  for (U budget = 0; item && item != h && budget < 4096; ++budget) {
    previous = item;
    item = v.Get(item).next;
  }
  return item == h ? previous : 0;
}
// Original.
U Backward(const View &v, U h) {
  auto node = v.Get(h);
  U item = PreviousSibling(v, h);
  if (!item)
    return node.depth ? node.parent : 0;
  for (U budget = 0; budget < 4096; ++budget) {
    auto current = v.Get(item);
    if (!current.first || !(current.state & 0x20))
      return item;
    item = current.first;
    while (v.Get(item).next) {
      item = v.Get(item).next;
      if (++budget >= 4096)
        return 0;
    }
  }
  return 0;
}
// Original, reached for TVGN_LASTVISIBLE.
U FindRow(const View &v, U item, U row) {
  for (U budget = 0; item && budget < 4096; ++budget) {
    U cursor = item;
    while (cursor && v.Get(cursor).row <= row) {
      auto node = v.Get(cursor);
      if (row < node.row + node.height)
        return cursor;
      item = cursor;
      cursor = node.next;
      if (++budget >= 4096)
        return 0;
    }
    item = v.Get(item).first;
  }
  return 0;
}
} // namespace
// Original TVM_GETNEXTITEM dispatcher. Malformed cycles are bounded.
U Next(const View &v, U h, U relation) {
  if (!relation || (relation == 4 && (!h || h == 0xffff0000)))
    return v.Get(v.root).first;
  if (relation == 5)
    return v.first_visible;
  if (relation == 8)
    return v.drop;
  if (relation == 9)
    return v.selected;
  if (relation == 10)
    return FindRow(v, v.Get(v.root).first, v.rows - 1);
  auto node = v.Get(h);
  if (!node.valid)
    return 0;
  switch (relation) {
  case 1:
    return node.next;
  case 2:
    return PreviousSibling(v, h);
  case 3:
    return node.depth ? node.parent : 0;
  case 4:
    return node.first;
  case 6:
    return Forward(v, h);
  case 7:
    return Backward(v, h);
  default:
    return 0;
  }
}
} // namespace Win98Tree
