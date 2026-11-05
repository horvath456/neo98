// SPDX-License-Identifier: GPL-2.0-or-later
#include "tree_model.h"
namespace TreeModel {
Item *Model::Find(U h) {
  U i = (h & 65535) - 1;
  return i < Capacity && items[i].node.valid && items[i].generation == h >> 16
             ? &items[i]
             : nullptr;
}
const Item *Model::Find(U h) const {
  return const_cast<Model *>(this)->Find(h);
}
Win98Tree::Node Model::Get(U h) const {
  if (h == Root) {
    Win98Tree::Node n;
    n.first = first;
    n.valid = true;
    return n;
  }
  auto *item = Find(h);
  return item ? item->node : Win98Tree::Node{};
}
U Model::Insert(U parent, U after) {
  if (!parent)
    parent = Root;
  auto *p = Find(parent);
  if (parent != Root && !p)
    return 0;
  U depth = p ? p->node.depth + 1 : 0;
  if (depth > 254)
    return 0;
  U *head = p ? &p->node.first : &first;
  U *link = head;
  if (after != 0xffff0001) {
    if (!after || after == 0xffff0002) {
      while (*link)
        link = &Find(*link)->node.next;
    } else {
      auto *previous = Find(after);
      if (!previous || previous->node.parent != parent)
        return 0;
      link = &previous->node.next;
    }
  }
  for (U i = 0; i < Capacity; ++i)
    if (!items[i].node.valid) {
      auto &item = items[i];
      U gen = item.generation;
      item = Item{};
      item.generation = gen;
      item.node.parent = parent;
      item.node.next = *link;
      item.node.depth = depth;
      item.node.valid = true;
      *link = Handle(i);
      ++count;
      return *link;
    }
  return 0;
}
bool Model::Remove(U h) {
  auto *item = Find(h);
  if (!item || item->node.first)
    return false;
  U parent = item->node.parent;
  U *link = parent == Root ? &first : &Find(parent)->node.first;
  while (*link && *link != h)
    link = &Find(*link)->node.next;
  if (!*link)
    return false;
  *link = item->node.next;
  U generation = item->generation % 0xfffe + 1;
  *item = Item{};
  item->generation = generation;
  --count;
  if (selected == h)
    selected = 0;
  if (drop == h)
    drop = 0;
  if (first_visible == h)
    first_visible = first;
  return true;
}
void Model::Layout() {
  for (auto &item : items)
    item.node.row = 0xffff;
  rows = 0;
  for (U h = first, budget = 0; h && budget < Capacity; ++budget) {
    auto *item = Find(h);
    if (!item)
      break;
    item->node.row = rows;
    rows += item->node.height;
    h = Win98Tree::Next(*this, h, 6);
  }
  if (!Find(first_visible) || Find(first_visible)->node.row == 0xffff)
    first_visible = first;
}
} // namespace TreeModel
