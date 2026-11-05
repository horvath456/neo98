// SPDX-License-Identifier: GPL-2.0-or-later
// New native control adapter. Item navigation is implemented Win98 code.
#include "native_sort.h"
#include "tree_model.h"
#include "user_draw.h"
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86 {
struct Application::Tree {
  unsigned hwnd = 0, active = 0, font = 0, images = 0, state_images = 0,
           item_height = 18, indent = 19, background = 0xffffff, foreground = 0;
  TreeModel::Model model;
};
Application::Tree *Application::FindTree(unsigned hwnd, bool create) {
  if (!hwnd)
    return nullptr;
  for (auto *tree : trees)
    if (tree && tree->hwnd == hwnd)
      return tree;
  if (create)
    for (auto &tree : trees)
      if (!tree || (!tree->hwnd && !tree->active)) {
        delete tree;
        tree = new Tree;
        tree->hwnd = hwnd;
        return tree;
      }
  return nullptr;
}
void Application::FreeCommon() {
    FreeToolbars();
    FreeLists();
    FreeComboBoxes();
    FreeListBoxes();
    for (auto &tree : trees)
    {
        delete tree;
        tree = nullptr;
    }
}
unsigned Application::TreeNotify(Tree &tree, unsigned code, unsigned action,
                                 unsigned old_item, unsigned new_item) {
  unsigned data[26] = {tree.hwnd, User::ControlID(tree.hwnd), code, action};
  auto fill = [&](unsigned at, unsigned handle) {
    auto *item = tree.model.Find(handle);
    data[at] = 0x1c;
    data[at + 1] = handle;
    if (item) {
      data[at + 2] = item->node.state;
      data[at + 3] = ~0u;
      data[at + 9] = item->parameter;
    }
  };
  fill(4, old_item);
  fill(14, new_item);
  unsigned p = Allocate(sizeof data);
  if (!p)
    return 0;
  Store(p, data, sizeof data);
  unsigned result = CallWindow(User::Parent(tree.hwnd), 0x4e, data[1], p);
  Free(p);
  return result;
}
// The vertical bar, as COMCTL32 keeps it (TV_CalcScrollBars): the range over
// the rows that are shown, a page of whole rows and the row of the first
// visible item as the position. SetScrollInfo shows the bar when the rows do
// not fit and hides it when they do; TVS_NOSCROLL (2000h) keeps it away.
// `show` names an item that has to be inside the page afterwards.
void Application::TreeScroll(Tree &tree, unsigned show) {
  auto &m = tree.model;
  if (!tree.hwnd)
    return;
  Gdi::RECT client = {};
  User::GetClientRect(tree.hwnd, &client);
  const long space = client.bottom - 4;
  const long page = space > (long)tree.item_height ? space / (long)tree.item_height : 1;
  long first = m.Find(m.first_visible) ? (long)m.Get(m.first_visible).row : 0;
  if (auto *item = m.Find(show); item && item->node.row != 0xffff) {
    if ((long)item->node.row < first)
      first = item->node.row;
    else if ((long)item->node.row >= first + page)
      first = (long)item->node.row - page + 1;
  }
  const long last = (long)m.rows > page ? (long)m.rows - page : 0;
  if (first > last)
    first = last;
  if (first < 0)
    first = 0;
  unsigned h = m.first;
  for (long row = 0; h && row < first; ++row)
    h = Win98Tree::Next(m, h, 6);
  if (h)
    m.first_visible = h;
  if (User::Style(tree.hwnd) & 0x2000)
    return;
  const unsigned block = Allocate(28);
  if (!block)
    return;
  const unsigned info[7] = {28, 7 /* SIF_RANGE | SIF_PAGE | SIF_POS */, 0,
                            m.rows ? m.rows - 1 : 0, (unsigned)page, (unsigned)first, 0};
  Store(block, info, sizeof info);
  const unsigned a[] = {tree.hwnd, 1 /* SB_VERT */, block, 1};
  Api("SetScrollInfo", a);
  Free(block);
}
// The rows of every tree view as the control shows them: for the tests of
// the host (the step trees=1 of NEO98_HOST_INPUT).
void Application::DumpTrees(void (*line)(const char *)) {
  for (auto *tree : trees) {
    if (!tree || !tree->hwnd)
      continue;
    auto &m = tree->model;
    Gdi::RECT client = {};
    User::GetClientRect(tree->hwnd, &client);
    char text[400];
    snprintf(text, sizeof text, "tree hwnd=%x rows=%u first=%u page=%ld bar=%u", tree->hwnd, m.rows,
             m.Find(m.first_visible) ? m.Get(m.first_visible).row : 0,
             client.bottom > 4 ? (long)(client.bottom - 4) / (long)tree->item_height : 0,
             User::Style(tree->hwnd) & 0x00200000 ? 1u : 0u);
    line(text);
    for (unsigned h = m.first, n = 0; h && n < TreeModel::Model::Capacity; ++n, h = Win98Tree::Next(m, h, 6)) {
      const auto *item = m.Find(h);
      if (!item)
        break;
      snprintf(text, sizeof text, "tree row=%u depth=%u selected=%u text=%s", item->node.row, item->node.depth,
               h == m.selected ? 1u : 0u, item->text);
      line(text);
    }
  }
}
unsigned Application::SelectTree(Tree &tree, unsigned h, unsigned action) {
  auto &m = tree.model;
  if (h && !m.Find(h))
    return 0;
  unsigned old = m.selected;
  if (old == h)
    return 1;
  if (TreeNotify(tree, (unsigned)-401, action, old, h) || !tree.hwnd)
    return 0;
  if (h && !m.Find(h))
    return 0;
  if (auto *item = m.Find(old))
    item->node.state &= ~2u;
  m.selected = h;
  if (auto *item = m.Find(h))
    item->node.state |= 2;
  TreeNotify(tree, (unsigned)-402, action, old, h);
  if (!tree.hwnd)
    return 1;
  // The selection is brought into the page (TV_SelectItem, TV_ScrollIntoView).
  TreeScroll(tree, h);
  User::InvalidateRect(tree.hwnd, nullptr, false);
  return 1;
}
unsigned Application::ExpandTree(Tree &tree, unsigned h, unsigned action) {
  auto *item = tree.model.Find(h);
  if (!item)
    return 0;
  bool expand = action & 2;
  if ((action & 3) == 3)
    expand = !(item->node.state & 0x20);
  if (bool(item->node.state & 0x20) == expand)
    return 0;
  unsigned operation = expand ? 2 : 1;
  if (TreeNotify(tree, (unsigned)-405, operation, 0, h) || !tree.hwnd)
    return 0;
  item = tree.model.Find(h);
  if (!item)
    return 0;
  if (expand)
    item->node.state |= 0x60;
  else
    item->node.state &= ~0x20u;
  tree.model.Layout();
  TreeNotify(tree, (unsigned)-406, operation, 0, h);
  if (!tree.hwnd)
    return 1;
  // An opened item shows as many of its children as the page holds and
  // stays in the page itself (TV_Expand, TV_ScrollBarsAfterExpand).
  unsigned last = 0;
  if (expand && tree.model.Find(h))
    for (unsigned child = Win98Tree::Next(tree.model, h, 6), n = 0;
         child && n < TreeModel::Model::Capacity && tree.model.Get(child).depth > tree.model.Get(h).depth;
         ++n, child = Win98Tree::Next(tree.model, child, 6))
      last = child;
  if (last)
    TreeScroll(tree, last);
  TreeScroll(tree, h);
  User::InvalidateRect(tree.hwnd, nullptr, false);
  return 1;
}
// Determine a common control's kind from its native class or inherited
// procedure token. Delphi/MFC classes can reuse a stock procedure under
// another class name. Tokens 0x71000000 plus kinds up to eleven identify
// common controls; higher tokens belong to other window procedures.
unsigned Application::CommonControlKind(unsigned hwnd, unsigned procedure) {
    auto token = [](unsigned p) {
        const unsigned kind = p - 0x71000000;
        return (p & 0xff000000u) == 0x71000000u && kind >= 1 && kind <= 11 ? kind : 0;
    };
    // A guest calling a control procedure through CallWindowProc supplies the
    // control kind explicitly, even when the window has its own subclass.
    if (const unsigned called = token(procedure)) return called;
    if (const unsigned marked = User::CommonControl(hwnd)) return marked;
    return token(User::Procedure(hwnd));
}
unsigned Application::CommonMessage(unsigned hwnd, unsigned message,
                                    unsigned wp, unsigned lp, unsigned procedure) {
    const unsigned kind = CommonControlKind(hwnd, procedure);
    // A class inheriting only a common-control procedure receives ordinary
    // WM_PAINT and draws here. Native classes recognized by USER use its
    // control-paint service instead.
    if (message == 0xf && !User::CommonControl(hwnd) && kind) {
        User::Paint paint{};
        Gdi::RECT client{};
        const Gdi::HDC dc = User::BeginPaint(hwnd, paint);
        if (dc && User::GetClientRect(hwnd, &client)) DrawCommon(hwnd, dc, client, kind);
        if (dc) User::EndPaint(hwnd, paint);
        return 0;
    }
    // These native controls currently delegate all three caption messages to
    // DefWindowProc; ComboBox owns its EDIT/list text synchronization.
    if(kind!=6&&(message==0xc||message==0xd||message==0xe))
        return WindowTextDefault(hwnd,message,wp,lp);
    if (kind == 11) return TrackbarMessage(hwnd,message,wp,lp);
    if (kind == 10) return ScrollBarMessage(hwnd,message,wp,lp);
    if (kind == 5) return ToolbarMessage(hwnd,message,wp,lp);
    if (kind == 6) return ComboBoxMessage(hwnd,message,wp,lp);
    if (kind == 4)
        return ListBoxMessage(hwnd, message, wp, lp);
    if (kind == 2)
        return ListMessage(hwnd, message, wp, lp);
    if (kind == 3)
        return StatusMessage(hwnd, message, wp, lp);
    if (kind != 1)
    {
        Fail("Native list/status control not implemented");
        return 0;
    }
  auto *tree = FindTree(hwnd, message == 0x81 || message == 1);
  if (!tree) {
    Fail("Missing native tree control");
    return 0;
  }
  struct Active {
    Tree *tree;
    explicit Active(Tree *p) : tree(p) { ++tree->active; }
    ~Active() { --tree->active; }
  } active(tree);
  auto &m = tree->model;
  auto refresh = [&]() {
    m.Layout();
    TreeScroll(*tree);
    User::InvalidateRect(hwnd, nullptr, false);
  };
  // A new first row from the bar, the wheel or the keyboard.
  auto scroll_to = [&](long first) {
    unsigned h = m.first;
    for (long row = 0; h && row < first; ++row)
      if (const unsigned next = Win98Tree::Next(m, h, 6))
        h = next;
      else
        break;
    const unsigned old = m.first_visible;
    if (h)
      m.first_visible = h;
    TreeScroll(*tree);
    if (m.first_visible != old)
      User::InvalidateRect(hwnd, nullptr, false);
  };
  auto page_rows = [&]() -> long {
    Gdi::RECT r = {};
    User::GetClientRect(hwnd, &r);
    const long space = r.bottom - 4;
    return space > (long)tree->item_height ? space / (long)tree->item_height : 1;
  };
  auto rectangle = [&](unsigned h, bool text, Gdi::RECT &r) {
    auto *item = m.Find(h);
    if (!item || item->node.row == 0xffff)
      return false;
    unsigned first = m.Get(m.first_visible).row;
    int y = ((int)item->node.row - (int)first) * (int)tree->item_height + 2;
    Gdi::RECT client = {};
    User::GetClientRect(hwnd, &client);
    int left = 0, right = client.right;
    if (text) {
      left = (item->node.depth + 1) * tree->indent;
      auto *images = FindImageList(tree->images);
      if (images)
        left += images->width + 3;
      auto dc = User::GetDC(hwnd);
      Gdi::SelectObject(dc, tree->font ? tree->font : system);
      Gdi::SIZE size = {};
      Gdi::GetTextExtentPoint32A(dc, item->text, strlen(item->text), &size);
      User::ReleaseDC(hwnd, dc);
      right = left + size.cx + 4;
    }
    r = {left, y, right, y + (int)tree->item_height};
    return true;
  };
  switch (message) {
  case 0x81:
    return 1;
  case 5: // WM_SIZE: the page changed
    if (m.count)
      TreeScroll(*tree);
    return 0;
  case 1:
  case 0xf:
  case 0x85:
    return 0;
  case 2:
    return CommonMessage(hwnd, 0x1101, 0, TreeModel::Model::Root);
  case 7:
  case 8:
      TreeNotify(*tree, message == 7 ? (unsigned)-7 : (unsigned)-8, 0, 0, 0);
      refresh();
      return 0;
  case 0x82:
    tree->hwnd = 0;
    return 0;
  case 0x30:
    tree->font = wp;
    if (lp)
      refresh();
    return 0;
  case 0x31:
    return tree->font;
  case 0x87:
    // DLGC_WANTARROWS and DLGC_WANTCHARS, as COMCTL32's tree view: Tab,
    // Enter and Esc stay the dialog's.
    return 1 | 0x80;
  case 0xb:
    User::SetControlRedraw(hwnd, wp != 0);
    return 0;
  case 0x1100: {
    unsigned data[12] = {};
    Read(lp, data, sizeof data);
    unsigned mask = data[2], parent = data[0], after = data[1];
    if ((mask & 1) && data[6] == ~0u) {
      Fail("Tree callback text not implemented");
      return 0;
    }
    char label[256] = {};
    if (mask & 1) {
      const char *text = ReadString(data[6]);
      if (strlen(text) >= sizeof label) {
        last_error = 87;
        return 0;
      }
      strcpy(label, text);
    }
    if (after == 0xffff0003) {
      after = 0xffff0001;
      for (unsigned h = m.Get(parent ? parent : TreeModel::Model::Root).first;
           h; h = m.Get(h).next) {
          if (NativeSort::CompareText(m.Find(h)->text, label) > 0)
              break;
          after = h;
      }
    }
    unsigned h = m.Insert(parent, after);
    auto *item = m.Find(h);
    if (!item)
      return 0;
    strcpy(item->text, label);
    if (mask & 8)
      item->node.state = data[4] & data[5];
    if (mask & 2)
      item->image = data[8];
    if (mask & 32)
      item->selected_image = data[9];
    if (mask & 64)
      item->children = data[10];
    if (mask & 4)
      item->parameter = data[11];
    refresh();
    return h;
  }
  case 0x1101: {
    if (!lp || lp == TreeModel::Model::Root) {
      while (m.first) {
        unsigned args[] = {hwnd, 0x1101, 0, m.first};
        CommonMessage(args[0], args[1], args[2], args[3]);
        if (state == Failed)
          return 0;
      }
      return 1;
    }
    auto *item = m.Find(lp);
    if (!item)
      return 0;
    while (item->node.first) {
      CommonMessage(hwnd, 0x1101, 0, item->node.first);
      if (state == Failed)
        return 0;
      item = m.Find(lp);
      if (!item)
        return 1;
    }
    TreeNotify(*tree, (unsigned)-409, 0, lp, 0);
    if (!tree->hwnd)
      return 0;
    if (m.selected == lp)
      SelectTree(*tree, 0);
    if (!tree->hwnd)
      return 0;
    m.Remove(lp);
    refresh();
    return 1;
  }
  case 0x1102:
    if (wp & ~3u) {
      Fail("Tree collapse-reset not implemented");
      return 0;
    }
    return ExpandTree(*tree, lp, wp);
  case 0x1104: {
    Gdi::RECT r;
    unsigned h = Read32(lp);
    if (!rectangle(h, wp != 0, r))
      return 0;
    Store(lp, &r, sizeof r);
    return 1;
  }
  case 0x1105:
    return m.count;
  case 0x1106:
    return tree->indent;
  case 0x1107:
    tree->indent = wp < 8 ? 8 : wp > 256 ? 256 : wp;
    refresh();
    return 0;
  case 0x1108:
    return wp == 0 ? tree->images : wp == 2 ? tree->state_images : 0;
  case 0x1109: {
    if (wp != 0 && wp != 2)
      return 0;
    if (lp && !FindImageList(lp))
      return 0;
    unsigned &target = wp == 0 ? tree->images : tree->state_images;
    unsigned old = target;
    target = lp;
    refresh();
    return old;
  }
  case 0x110a:
    return Win98Tree::Next(m, lp, wp);
  case 0x110b:
    if (wp == 9)
      return SelectTree(*tree, lp);
    if (lp && !m.Find(lp))
      return 0;
    if (wp == 5)
      m.first_visible = lp;
    else if (wp == 8)
      m.drop = lp;
    else
      return 0;
    refresh();
    return 1;
  case 0x1113:
  {
      // Original collects siblings, sorts through DPA, then relinks them.
      // Its recurse argument is unused. Native labels currently use ASCII folding.
      unsigned parent = lp ? lp : TreeModel::Model::Root;
      auto *p = m.Find(parent);
      if (parent != TreeModel::Model::Root && !p)
          return 0;
      unsigned *head = p ? &p->node.first : &m.first, items[512], count = 0;
      for (unsigned h = *head; h && count < 512; h = m.Get(h).next)
          items[count++] = h;
      if (!count)
          return 0;
      bool sorted =
          NativeSort::Sort(items, count, &m,
                           [](void *context, unsigned a, unsigned b)
                           {
                               auto &model = *static_cast<TreeModel::Model *>(context);
                               return NativeSort::CompareText(model.Find(a)->text, model.Find(b)->text);
                           });
      if (!sorted)
          return 0;
      *head = items[0];
      for (unsigned n = 0; n < count; ++n)
          m.Find(items[n])->node.next = n + 1 < count ? items[n + 1] : 0;
      refresh();
      return 1;
  }
  case 0x110c:
  case 0x110d: {
    unsigned data[10];
    Read(lp, data, sizeof data);
    auto *item = m.Find(data[1]);
    if (!item)
      return 0;
    unsigned mask = data[0];
    if (message == 0x110c) {
      if (mask & 8)
        data[2] = item->node.state & data[3];
      if (mask & 2)
        data[6] = item->image;
      if (mask & 32)
        data[7] = item->selected_image;
      if (mask & 64)
        data[8] = item->children;
      if (mask & 4)
        data[9] = item->parameter;
      if (mask & 1)
        PutString(data[4], item->text, data[5]);
      Store(lp, data, sizeof data);
      return 1;
    }
    if (mask & 1) {
      if (data[4] == ~0u) {
        Fail("Tree callback text not implemented");
        return 0;
      }
      const char *text = ReadString(data[4]);
      if (strlen(text) >= sizeof item->text)
        return 0;
      strcpy(item->text, text);
    }
    if (mask & 8)
      item->node.state = (item->node.state & ~data[3]) | (data[2] & data[3]);
    if (mask & 2)
      item->image = data[6];
    if (mask & 32)
      item->selected_image = data[7];
    if (mask & 64)
      item->children = data[8];
    if (mask & 4)
      item->parameter = data[9];
    refresh();
    return 1;
  }
  case 0x1110: {
    Gdi::RECT r = {};
    User::GetClientRect(hwnd, &r);
    return r.bottom > 0 ? r.bottom / tree->item_height : 0;
  }
  case 0x1111: {
    unsigned data[4] = {};
    Read(lp, data, sizeof data);
    int x = data[0], y = data[1];
    data[2] = 1;
    data[3] = 0;
    Gdi::RECT bounds;
    User::GetClientRect(hwnd, &bounds);
    if (x >= 0 && y >= 2 && x < bounds.right && y < bounds.bottom) {
      unsigned h = m.first_visible;
      for (unsigned row = 0; h && row < (unsigned)(y - 2) / tree->item_height;
           ++row)
        h = Win98Tree::Next(m, h, 6);
      if (auto *item = m.Find(h)) {
        data[3] = h;
        unsigned indent = (item->node.depth + 1) * tree->indent;
        data[2] = x < (int)indent ? 0x10 : 4;
      }
    }
    Store(lp, data, sizeof data);
    return data[3];
  }
  case 0x1114: {
    auto *item = m.Find(lp);
    if (!item)
      return 0;
    unsigned parent = item->node.parent;
    for (unsigned n = 0; parent != TreeModel::Model::Root && n < 512; ++n) {
      ExpandTree(*tree, parent, 2);
      parent = m.Get(parent).parent;
    }
    m.Layout();
    TreeScroll(*tree, lp);
    User::InvalidateRect(hwnd, nullptr, false);
    return 1;
  }
  case 0x1119:
    return 0;
  case 0x111b: {
    unsigned old = tree->item_height;
    tree->item_height = wp == ~0u ? 18 : wp < 2 ? 2 : wp > 256 ? 256 : wp;
    refresh();
    return old;
  }
  case 0x111c:
    return tree->item_height;
  case 0x111d: {
    unsigned old = tree->background;
    tree->background = lp == ~0u ? 0xffffff : lp;
    refresh();
    return old;
  }
  case 0x111e: {
    unsigned old = tree->foreground;
    tree->foreground = lp == ~0u ? 0 : lp;
    refresh();
    return old;
  }
  case 0x111f:
    return tree->background;
  case 0x1120:
    return tree->foreground;
  case 0x2006:
    return 0; // CCM_GETUNICODEFORMAT: ANSI control.
  case 0x2005:
    if (wp) {
      Fail("Unicode tree notifications not implemented");
    }
    return 0;
  case 0x203:
  {
      unsigned row = ((short)(lp >> 16) - 2) / (int)tree->item_height + m.Get(m.first_visible).row;
      for (unsigned h = m.first; h; h = Win98Tree::Next(m, h, 6))
          if (m.Get(h).row == row)
          {
              if (!TreeNotify(*tree, (unsigned)-3, 0, 0, h) && tree->hwnd)
                  ExpandTree(*tree, h, 3);
              break;
          }
      return 0;
  }
  case 0x201: {
    int x = (short)lp, y = (short)(lp >> 16);
    if (x < 0 || y < 0)
      return 0;
    unsigned h = m.first_visible;
    for (unsigned row = 0; h && row < (unsigned)y / tree->item_height; ++row)
      h = Win98Tree::Next(m, h, 6);
    auto *item = m.Find(h);
    if (!item)
      return 0;
    unsigned indent = (item->node.depth + 1) * tree->indent;
    if (x < (int)indent && (item->children || item->node.first))
      ExpandTree(*tree, h, 3);
    else
      SelectTree(*tree, h, 1);
    return 0;
  }
  case 0x100: {
    unsigned h = m.selected;
    if (!h)
      h = m.first;
    auto *item = m.Find(h);
    if (!item)
      return 0;
    unsigned next = 0;
    if (wp == 0x26)
      next = Win98Tree::Next(m, h, 7);
    else if (wp == 0x28)
      next = Win98Tree::Next(m, h, 6);
    else if (wp == 0x24)
      next = m.first;
    else if (wp == 0x23)
      next = Win98Tree::Next(m, 0, 10);
    else if (wp == 0x21 || wp == 0x22) {
      // Page Up and Page Down: a page of rows from the selection.
      next = h;
      for (long n = page_rows() - 1; n > 0; --n)
        if (const unsigned step = Win98Tree::Next(m, next, wp == 0x21 ? 7 : 6))
          next = step;
        else
          break;
      if (next == h)
        next = 0;
    }
    else if (wp == 0x27) {
      if (!(item->node.state & 0x20))
        ExpandTree(*tree, h, 2);
      else
        next = item->node.first;
    } else if (wp == 0x25) {
      if (item->node.state & 0x20)
        ExpandTree(*tree, h, 1);
      else
        next = Win98Tree::Next(m, h, 3);
    }
    if (next)
      SelectTree(*tree, next, 2);
    return 0;
  }
  case 0x102:
    return 0;
  case 0x115: { // WM_VSCROLL
    const long first = m.Find(m.first_visible) ? (long)m.Get(m.first_visible).row : 0;
    switch (wp & 0xffff) {
    case 0: scroll_to(first - 1); break;           // SB_LINEUP
    case 1: scroll_to(first + 1); break;           // SB_LINEDOWN
    case 2: scroll_to(first - page_rows()); break; // SB_PAGEUP
    case 3: scroll_to(first + page_rows()); break; // SB_PAGEDOWN
    case 4:                                        // SB_THUMBPOSITION
    case 5: scroll_to((long)(wp >> 16)); break;    // SB_THUMBTRACK
    case 6: scroll_to(0); break;                   // SB_TOP
    case 7: scroll_to((long)m.rows); break;        // SB_BOTTOM
    default: break;
    }
    return 0;
  }
  case 0x20a: // WM_MOUSEWHEEL: three rows a notch, a row for a third of it
    scroll_to((m.Find(m.first_visible) ? (long)m.Get(m.first_visible).row : 0) -
              WheelLines(hwnd, (int)(short)(wp >> 16)));
    return 0;
  default:
    if (message >= 0x400) {
      char error[128];
      snprintf(error, sizeof error, "Unsupported tree message %x wp=%x lp=%x",
               message, wp, lp);
      Fail(error);
      return 0;
    }
    // A press on the scroll bar: DefWindowProc makes it SC_VSCROLL and runs
    // the tracking loop, which answers with WM_VSCROLL.
    // COMCTL32 -> -> delegates WM_NCCALCSIZE
    // to DefWindowProcA. Its guest RECT must lose the border and scrollbar
    // before SetWindowPos commits it; the native-only default cannot write it.
    if (message == 0x83 || message == 0xa1 || message == 0x112)
      return DefaultWindowMessage(hwnd, message, wp, lp);
    return User::DefaultMessage(hwnd, message, wp, lp);
  }
}
void Application::DrawCommon(unsigned hwnd, Gdi::HDC dc,
                             const Gdi::RECT &bounds, unsigned wanted) {
    const unsigned kind = wanted ? wanted : CommonControlKind(hwnd);
    if (kind == 11) { DrawTrackbar(hwnd,dc,bounds);return; }
    if (kind == 10) {
        // Interim SCROLLBAR control face until USER's control is ported.
        Gdi::RECT r = bounds; auto brush = Gdi::CreateSolidBrush(0xc0c0c0); Gdi::FillRect(dc, &r, brush); Gdi::DeleteObject(brush); return;
    }
    if (kind == 9) { DrawTooltip(hwnd,dc,bounds);return; }
    if (kind == 8) { DrawUpDown(hwnd,dc,bounds);return; }
    if (kind == 7) { DrawTab(hwnd,dc,bounds);return; }
    if (kind == 6) { DrawComboBox(hwnd,dc,bounds);return; }
    if (kind == 5) { DrawToolbar(hwnd,dc,bounds);return; }
    if (kind == 4)
    {
        DrawListBox(hwnd, dc, bounds);
        return;
    }
    if (kind == 2)
    {
        DrawList(hwnd, dc, bounds);
        return;
    }
    if (kind == 3)
    {
        DrawStatus(hwnd, dc, bounds);
        return;
    }
  auto *tree = FindTree(hwnd);
  if (!tree)
    return;
  auto &m = tree->model;
  auto fill = [&](Gdi::RECT r, unsigned color) {
    auto brush = Gdi::CreateSolidBrush(color);
    Gdi::FillRect(dc, &r, brush);
    Gdi::DeleteObject(brush);
  };
  fill(bounds, tree->background);
  if (tree->font)
    Gdi::SelectObject(dc, tree->font);
  else
    Gdi::SelectObject(dc, system);
  auto edge = bounds;
  Win98Draw::DrawEdge(dc, &edge, 10, 15);
  Gdi::IntersectClipRect(dc, 2, 2, bounds.right - 2, bounds.bottom - 2);
  int y = 2;
  for (unsigned h = m.first_visible, n = 0; h && y < bounds.bottom && n < 512;
       ++n, h = Win98Tree::Next(m, h, 6), y += tree->item_height) {
    auto *item = m.Find(h);
    if (!item)
      break;
    int x = (item->node.depth + 1) * tree->indent;
    if (item->children || item->node.first) {
      // A square of nine pixels with a flat COLOR_BTNSHADOW frame on all
      // four sides, as COMCTL32 draws it. A sunken DrawEdge left its white
      // right and bottom edge invisible on the white tree, and the sign sat
      // off the middle of the eight pixels that showed.
      Gdi::RECT box = {x - 13, y + 4, x - 4, y + 13};
      fill(box, 0x808080);
      fill({box.left + 1, box.top + 1, box.right - 1, box.bottom - 1}, tree->background);
      fill({x - 11, y + 8, x - 6, y + 9}, tree->foreground);
      if (!(item->node.state & 0x20))
        fill({x - 9, y + 6, x - 8, y + 11}, tree->foreground);
    }
    auto *images = FindImageList(tree->images);
    unsigned icon = h == m.selected ? item->selected_image : item->image;
    if (images && icon < images->count) {
      images->items[icon]->Draw(dc, x, y);
      x += images->width + 3;
    }
    Gdi::SIZE size = {};
    Gdi::GetTextExtentPoint32A(dc, item->text, strlen(item->text), &size);
    bool selected = h == m.selected;
    if (selected)
      fill({x, y, x + size.cx + 4, y + (int)tree->item_height}, 0x800000);
    Gdi::SetBkMode(dc, Gdi::TRANSPARENT);
    Gdi::SetTextColor(dc, selected ? 0xffffff : tree->foreground);
    Gdi::TextOutA(dc, x + 2, y + (tree->item_height - size.cy) / 2, item->text,
                  strlen(item->text));
  }
}
} // namespace X86
