# Who paints a window's frame

On Windows a frame is painted through the window: when its frame has to be
painted (it is shown, activated, sized, its title or menu bar changes, a
part of it comes into view), the window gets `WM_NCPAINT` with the region,
in its own thread, and `DefWindowProc` paints the frame; `WM_NCACTIVATE`
paints the caption the same way. A window that paints over its frame after
`DefWindowProc` (through `GetWindowDC` or `GetDCEx` with `DCX_WINDOW`) keeps
what it painted until the frame has to be painted again, and then draws it
again on the next `WM_NCPAINT`. A window that does not hand `WM_NCPAINT` on
keeps its old frame.

Neo98 composes the screen itself (`Render`, `PaintWindow` in `src/user.cpp`):
every composition painted every frame (`PaintFrame`), outside any message,
and `WM_NCPAINT` came only from `BeginPaint` for a frame that was owed. A
program's own caption was gone with the next composition.

## What Neo98 does now

A window that has taken a window DC of itself (`NoteOwnFrame`, from
`GetWindowDC` and `GetDCEx` with `DCX_WINDOW`) draws on its own frame, and
for it the frame works as on Windows:

- The composition keeps the frame pixels that are there. It paints the frame
  again only when what the frame shows has changed (`FrameSignature`: style,
  size and parts, title, icon, active or not, caption buttons, the selected
  system icon, the items of the menu bar, the scroll bars, the system colours
  and fonts, `RDW_FRAME` and frame invalidations) or when the window can be
  seen in a different shape, measured from its own corner (a window that
  moves takes its pixels along; a part that comes into view needs paint).
- When it does paint that frame, the window is owed `WM_NCPAINT` (wParam 1,
  the whole window), which its next `BeginPaint` sends, so the window draws
  its own parts again.
- `DefWindowProc` of `WM_NCPAINT` and `WM_NCACTIVATE` paints the frame at
  once, in the window's thread, and that is the frame the composition
  keeps.

Every other window gets its frame from the composition as before.

`tools/test_own_caption_host.py` (`tools/own_caption_fixture.c`): a window
that fills its caption after `DefWindowProc` keeps it when its client area
is painted again, when a popup covers part of it and takes the activation,
and when the popup goes: 0 of 4 checks before, 4 of 4 now.

## What it costs

Measured with `NEO98_RENDER_PROFILE=1` over five seconds of a window that
moves by a pixel every 10 ms, on a busy machine: a window that paints its
own caption is composed 89 times as before, and its frame is painted once
instead of 89 times (USER's render time 20 ms in all, against 30); a window
that does not is unchanged (89 compositions, 30 ms). The signatures are
hashes of a few hundred bytes per composition of such a window.

## What is left

- Windows that never took a window DC still have their frames painted by
  the composition without a `WM_NCPAINT`; a window that answers
  `WM_NCPAINT` without calling `DefWindowProc` and without drawing through a
  window DC (to have no frame drawn at all) still gets the frame.
- When the composition paints an own frame again, it paints it before the
  window's `WM_NCPAINT` and `DefWindowProc` paints it once more; Windows
  paints it only in `DefWindowProc`. A program that draws only part of its
  caption sees USER's caption for one composition in between.
- `WM_NCPAINT` carries 1 (the whole frame), not the region that needs paint.
- A frame the composition keeps is not repainted for a change the
  signature does not know of; what it knows is listed above.
