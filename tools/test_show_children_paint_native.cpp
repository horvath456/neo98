// SPDX-License-Identifier: GPL-2.0-or-later
// showing a window invalidates every visible descendant. COMCTL32's
// tab control writes lpcs->style|WS_CLIPSIBLINGS back with SetWindowLong in
// WM_CREATE, so it carries WS_VISIBLE before CreateWindow shows it and its own
// show changes nothing. Its first full paint must come from the show of the
// dialog around it (SetWindowPos SWP_SHOWWINDOW invalidates the covered area
// with the children); before the fix DXDIAG's row of tabs stayed empty.
#include "../src/gdi.h"
#include "../src/gdi_truetype.h"
#include "../src/user.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using U = unsigned;

// This isolated invalidation fixture uses raster fonts only. Keep an
// unexpected outline-font dependency fatal rather than supplying fake metrics.
namespace GdiTrueType {
bool AbcWidths(const Font &, unsigned, unsigned, int *)
{
    std::fputs("Unexpected TrueType query in raster invalidation fixture\n", stderr);
    std::abort();
}
}

static bool owes_whole_client(U h)
{
    Gdi::RECT update{}, client{};
    if (!User::GetUpdateRect(h, &update) || !User::GetClientRect(h, &client))
        return false;
    return update.left == client.left && update.top == client.top &&
           update.right == client.right && update.bottom == client.bottom;
}

static bool owes_nothing(U h)
{
    Gdi::RECT update{};
    return !User::GetUpdateRect(h, &update);
}

int main()
{
    assert(User::Initialize());
    assert(User::RegisterClass("ShowChildrenNative", 1, 0, 0));
    // A hidden dialog with a hidden page, like DXDIAG's main window while
    // WM_INITDIALOG runs.
    const U dialog = User::CreateWindow("ShowChildrenNative", "dialog",
                                        0x80c80000u, 30, 30, 400, 300, 0, 0, 0);
    const U tabs = User::CreateWindow("ShowChildrenNative", "tabs",
                                      0x40010000u, 10, 10, 380, 240, dialog, 1, 0);
    const U page = User::CreateWindow("ShowChildrenNative", "page",
                                      0x40000000u, 14, 34, 372, 212, dialog, 2, 0);
    const U label = User::CreateWindow("ShowChildrenNative", "label",
                                       0x50000000u, 4, 4, 100, 20, page, 3, 0);
    const U hidden = User::CreateWindow("ShowChildrenNative", "hidden",
                                        0x40000000u, 4, 30, 100, 20, page, 4, 0);
    assert(dialog && tabs && page && label && hidden);
    for (U h : {dialog, tabs, page, label, hidden})
        User::CompleteCreation(h);

    // The tab control's WM_CREATE: the style words with WS_VISIBLE and
    // WS_CLIPSIBLINGS, written without any redraw.
    User::SetStyleWords(tabs, 0x54010000u, false);
    assert(User::Style(tabs) & 0x10000000u);
    // Its inserts invalidate while the dialog is hidden: nothing is owed.
    User::RedrawWindow(tabs, nullptr, 0, 0x41);
    assert(owes_nothing(tabs));
    // CreateWindow's final show of the already visible control is a no-op.
    User::ShowWindow(tabs, 8);
    assert(owes_nothing(tabs));

    // The page is shown under the hidden dialog: only its bit changes.
    User::ShowWindow(page, 8);

    // The dialog shows: every visible descendant owes its whole client.
    User::ShowWindow(dialog, 1);
    assert(owes_whole_client(dialog));
    assert(owes_whole_client(tabs));
    assert(owes_whole_client(page));
    assert(owes_whole_client(label));
    assert(owes_nothing(hidden));

    // Painting clears the debt; a later show of an already visible window
    // does not invalidate again.
    for (U h : {dialog, tabs, page, label})
    {
        User::Paint paint{};
        const U dc = User::BeginPaint(h, paint);
        assert(dc);
        assert(User::EndPaint(h, paint));
        assert(owes_nothing(h));
    }
    User::ShowWindow(dialog, 5);
    assert(owes_nothing(tabs));

    // A subtree shown later under a visible parent gets its paint as well.
    User::ShowWindow(hidden, 5);
    assert(owes_whole_client(hidden));

    // Hiding and showing the dialog again repaints the children.
    User::ShowWindow(dialog, 0);
    User::ShowWindow(dialog, 5);
    assert(owes_whole_client(tabs));
    assert(owes_whole_client(label));

    std::printf("show children paint native PASS\n");
    return 0;
}
