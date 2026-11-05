// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_tree.h"
// New storage for native controls; traversal comes from win98_tree.cpp.
namespace TreeModel {
using U = unsigned;
struct Item {
  Win98Tree::Node node;
  U generation = 1, parameter = 0, image = ~0u, selected_image = ~0u,
    children = 0;
  char text[256] = {};
};
class Model : public Win98Tree::View {
public:
  static constexpr U Capacity = 512, Root = 0xffff0000;
  Model() { root = Root; }
  Item *Find(U);
  const Item *Find(U) const;
  Win98Tree::Node Get(U) const override;
  U Insert(U parent, U after);
  bool Remove(U);
  void Layout();
  U Handle(U index) const {
    return (items[index].generation << 16) | (index + 1);
  }
  U count = 0, first = 0;
  Item items[Capacity];
};
} // namespace TreeModel
