// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_windows.h"
using namespace Win98Windows;
extern "C" unsigned windows_query(const Node *nodes, unsigned count, unsigned desktop, unsigned blocked,
                                  unsigned operation, unsigned hwnd, unsigned command, unsigned *output)
{
    struct Context
    {
        const Node *nodes;
        unsigned count;
    } context{nodes, count};
    Tree tree{&context,
              [](void *p, unsigned h)
              {
                  const auto &c = *static_cast<Context *>(p);
                  return h <= c.count ? c.nodes[h] : Node{};
              },
              desktop, count + 1, blocked != 0};
    if (operation == 0)
        return Get(tree, hwnd, command);
    if (operation == 1)
        return Top(tree, hwnd);
    if (operation == 2)
        return tree.desktop;
    unsigned n = 0;
    return Snapshot(tree, hwnd, command, output, 128, n) ? n : ~0u;
}
