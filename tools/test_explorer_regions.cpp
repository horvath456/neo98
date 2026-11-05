// SPDX-License-Identifier: GPL-2.0-or-later
// The optimized region storage must preserve exact subtraction rectangles.
#include "../src/user.cpp"
#include "../src/gdi_truetype.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace GdiTrueType { bool AbcWidths(const Font &, unsigned, unsigned, int *) { std::abort(); } }
using R = Gdi::RECT;
static unsigned random_state = 6061;
static unsigned random_value() { random_state ^= random_state << 13; random_state ^= random_state >> 17; random_state ^= random_state << 5; return random_state; }
static void append(std::vector<R> &list, R r) { if (r.left < r.right && r.top < r.bottom) list.push_back(r); }
static void reference(std::vector<R> &list, R cut)
{
    if (cut.left >= cut.right || cut.top >= cut.bottom) return;
    std::vector<R> untouched, hit;
    for (auto a : list) {
        if (cut.left >= a.right || cut.right <= a.left || cut.top >= a.bottom || cut.bottom <= a.top) untouched.push_back(a);
        else hit.push_back(a);
    }
    for (auto a : hit) {
        int top = cut.top > a.top ? cut.top : a.top, bottom = cut.bottom < a.bottom ? cut.bottom : a.bottom;
        append(untouched,{a.left,a.top,a.right,top}); append(untouched,{a.left,bottom,a.right,a.bottom});
        append(untouched,{a.left,top,cut.left > a.left ? cut.left : a.left,bottom});
        append(untouched,{cut.right < a.right ? cut.right : a.right,top,a.right,bottom});
    }
    list = untouched;
}
static R rectangle() { int x=int(random_value()%80)-20,y=int(random_value()%80)-20; return {x,y,x+int(random_value()%40),y+int(random_value()%40)}; }
static void compare(const User::RectList &a, const std::vector<R> &b)
{
    if (a.n != b.size()) std::abort();
    for (unsigned i=0;i<a.n;++i) if (!User::SameRect(a.r[i],b[i])) std::abort();
}
int main()
{
    for (unsigned scene=0;scene<2000;++scene) {
        User::RectList actual; std::vector<R> expected;
        for (unsigned i=0,n=scene%20+1;i<n;++i) { auto r=rectangle();actual.Append(r);append(expected,r); }
        for (unsigned i=0;i<30;++i) {
            auto cut=rectangle();actual.Subtract(cut);reference(expected,cut);compare(actual,expected);
            User::RectList copy(actual);compare(copy,expected);copy=copy;compare(copy,expected);
            User::RectList assigned;assigned=actual;compare(assigned,expected);
        }
    }
    puts("EXPLORER REGIONS PASS: 60000 subtractions, exact rectangles/order, inline/heap copies, ASan/UBSan");
}
