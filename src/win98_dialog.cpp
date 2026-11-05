// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_dialog.h"
namespace Win98Dialog
{
namespace
{
constexpr unsigned Child = 0x40000000, Visible = 0x10000000, Disabled = 0x08000000;
constexpr unsigned Tabstop = 0x10000, Group = 0x20000, ControlParent = 0x10000;
struct Walk
{
    const Tree &tree;
    unsigned remaining = 4096;
    Node Get(unsigned h) const
    {
        return h ? tree.get(tree.context, h) : Node{};
    }
    unsigned Normalize(unsigned current, unsigned root)
    {
        unsigned last = 0;
        while (current && remaining)
        {
            --remaining;
            const auto node = Get(current);
            if ((node.style & 0xc0000000) != Child || current == root)
                break;
            last = current;
            current = node.parent;
            if (Get(current).extended & ControlParent)
                break;
        }
        return last;
    }
    bool Descendant(unsigned current, unsigned root)
    {
        while (current && remaining)
        {
            --remaining;
            if (current == root)
                return true;
            current = Get(current).parent;
        }
        return false;
    }
    unsigned Forward(unsigned flags, unsigned current, unsigned root)
    {
        if (!remaining)
            return 0;
        --remaining;
        while (current)
        {
            if (Get(current).next)
            {
                current = Get(current).next;
                break;
            }
            current = Normalize(Get(current).parent, root);
            if (!current || current == root)
                break;
        }
        if (!current || current == root)
            current = Get(root).first;
        if (!current)
            return root;
        const auto node = Get(current);
        if (node.extended & ControlParent)
        {
            if ((!(flags & 1) || (node.style & Visible)) && (!(flags & 2) || !(node.style & Disabled)))
                return Forward(flags, 0, current);
            return Forward(flags, current, root);
        }
        return current;
    }
    unsigned Backward(unsigned flags, unsigned current, unsigned root)
    {
        if (!current)
            return 0;
        unsigned last = Forward(flags, 0, root);
        while (remaining)
        {
            unsigned next = Forward(flags, last, root);
            if (!next || next == current)
                return last;
            last = next;
        }
        return 0;
    }
    unsigned GroupStep(unsigned current, unsigned root, bool previous)
    {
        if (previous)
        {
            if (!current || !(Get(current).style & Group))
                return Backward(3, current, root);
            unsigned last = current;
            while (remaining)
            {
                unsigned next = Forward(3, last, root);
                if (!next || (Get(next).style & Group) || next == current)
                    return last;
                last = next;
            }
        }
        else
        {
            unsigned next = Forward(3, current, root);
            if (!current || !(Get(next).style & Group))
                return next;
            unsigned last = current;
            while (remaining)
            {
                if (Get(last).style & Group)
                    return last;
                unsigned before = Backward(3, last, root);
                if (!before || before == current)
                    return last;
                last = before;
            }
        }
        return 0;
    }
};
} // namespace
int Scale(int coordinate, int unit, unsigned divisor)
{
    return divisor ? (int)(((long long)coordinate * unit + (divisor >> 1)) / (int)divisor) : coordinate;
}
bool MapRect(Gdi::RECT *rect, int xunit, int yunit)
{
    if (!rect)
        return false;
    rect->left = Scale(rect->left, xunit, 4);
    rect->right = Scale(rect->right, xunit, 4);
    rect->top = Scale(rect->top, yunit, 8);
    rect->bottom = Scale(rect->bottom, yunit, 8);
    return true;
}
unsigned NextTab(const Tree &tree, unsigned root, unsigned current, bool previous)
{
    Walk walk{tree};
    current = current == root ? 0 : walk.Normalize(current, root);
    if (current && !walk.Descendant(current, root))
        return 0;
    unsigned result = previous ? walk.Backward(3, current, root) : walk.Forward(3, current, root);
    while (result && result != current && result != root && walk.remaining)
    {
        if (!current)
            current = result;
        if ((walk.Get(result).style & (Visible | Disabled | Tabstop)) == (Visible | Tabstop))
            break;
        result = previous ? walk.Backward(3, result, root) : walk.Forward(3, result, root);
    }
    return walk.remaining ? result : 0;
}
unsigned NextGroup(const Tree &tree, unsigned root, unsigned current, bool previous)
{
    Walk walk{tree};
    current = walk.Normalize(current == root ? 0 : current, root);
    unsigned result = current;
    while (walk.remaining)
    {
        result = walk.GroupStep(result, root, previous);
        if (!result || result == current)
            break;
        if (!current)
            current = result;
        if ((walk.Get(result).style & (Visible | Disabled)) == Visible)
            break;
    }
    return walk.remaining ? result : 0;
}
unsigned ChildControl(const Tree &tree, unsigned root, unsigned current)
{
    Walk walk{tree};
    return walk.Normalize(current, root);
}
unsigned NextControl(const Tree &tree, unsigned root, unsigned current, unsigned flags)
{
    Walk walk{tree};
    const unsigned result = walk.Forward(flags, current, root);
    return walk.remaining ? result : 0;
}
} // namespace Win98Dialog
#ifndef NEO98_DIALOG_NO_REFERENCES
#ifndef NEO98_RUNTIME_CONTENT
#include "../build/generated/dialog_reference.h"
namespace Win98Dialog
{
unsigned OriginalTestCount()
{
    return sizeof DialogNavigation / sizeof *DialogNavigation +
           sizeof DialogRectangles / sizeof *DialogRectangles;
}
unsigned OriginalSelfTest()
{
    unsigned index = 0;
    for (const auto &test : DialogNavigation)
    {
        Tree tree{(void *)DialogTrees[test[0]], [](void *p, unsigned h) {
                      return h && h <= 12 ? static_cast<const Node *>(p)[h - 1] : Node{};
                  }};
        unsigned actual =
            test[3] ? NextGroup(tree, 1, test[1], test[2] != 0) : NextTab(tree, 1, test[1], test[2] != 0);
        if (actual != test[4])
            return index + 1;
        ++index;
    }
    for (const auto &test : DialogRectangles)
    {
        Gdi::RECT r = {test[0], test[1], test[2], test[3]};
        MapRect(&r, test[4], test[5]);
        if (r.left != test[6] || r.top != test[7] || r.right != test[8] || r.bottom != test[9])
            return index + 1;
        ++index;
    }
    return 0;
}
} // namespace Win98Dialog
#endif
#endif
