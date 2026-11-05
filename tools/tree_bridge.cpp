// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_tree.h"
namespace {
struct View : Win98Tree::View {
  const unsigned *nodes;
  unsigned count;
  View(const unsigned *p, unsigned n) : nodes(p), count(n) {}
  Win98Tree::Node Get(unsigned h) const override {
    if (h < 0x120000 || (h - 0x120000) % 64 || (h - 0x120000) / 64 >= count)
      return {};
    const auto *p = nodes + (h - 0x120000) / 64 * 8;
    return {p[0], p[1], p[2], p[3], p[4], p[5], p[6], bool(p[7])};
  }
};
} // namespace
extern "C" unsigned tree_next(const unsigned *nodes, unsigned count,
                              const unsigned *state, unsigned h,
                              unsigned relation) {
  View view(nodes, count);
  view.root = state[0];
  view.first_visible = state[1];
  view.selected = state[2];
  view.drop = state[3];
  view.rows = state[4];
  return Win98Tree::Next(view, h, relation);
}
