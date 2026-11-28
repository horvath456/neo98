# Who paints a window's frame

On Windows a frame is painted through the window: when its frame has to be
painted (it is shown, a part of it comes into view, it is invalidated with
`RDW_FRAME`, `SWP_FRAMECHANGED` changes it), the window gets `WM_NCPAINT`
in its own thread, with its update region in screen coordinates as `wParam`
(1 when the whole window is to be painted), and `DefWindowProc` paints the
frame within that region. `WM_NCACTIVATE` paints the caption the same way,
and so do `WM_SETTEXT` and `WM_SETICON` in `DefWindowProc`. A window that
paints over its frame after `DefWindowProc` (through `GetWindowDC`, or
`GetDCEx` with `DCX_WINDOW` and the region) keeps what it painted until the
frame has to be painted again, and then draws it again on the next
`WM_NCPAINT`. A window that answers `WM_NCPAINT` and `WM_NCACTIVATE` without
`DefWindowProc` has no frame at all: what was on the screen stays there.

Neo98 composes the screen itself (`Render`, `PaintWindow` in `src/user.cpp`).
Every composition painted every frame (`PaintFrame`) outside any message, and
`WM_NCPAINT` came only from `BeginPaint` for a frame that was owed, always
with 1. A program's own caption was gone with the next composition, and a
window without `DefWindowProc` for its frame had one anyway.

## What Neo98 does

The frame of a window of a program's class (one that gets `WM_PAINT` from its
queue, `Custom` in `src/user.cpp`) is painted by the window's thread, as on
Windows (`ThreadFrame`):

- The composition never paints such a frame; it keeps the pixels that are
  there. A window that moves takes its pixels along (the copy of the
  geometry reconciliation, `SettleGeometry`).
- What comes into view of a window and was not carried along goes into its
  update region: the client part as before, the frame part now as well
  (`ExposeFrame`), and the window is owed `WM_NCPAINT`.
- A change of what the frame shows that `DefWindowProc` did not paint
  itself (its size, style and parts, the menu bar, the scroll bars, the
  caption buttons pressed, the system colours and fonts: `FrameSignature`)
  owes `WM_NCPAINT` for the whole frame, as `SWP_FRAMECHANGED` and
  `DrawMenuBar` do on Windows. `RDW_FRAME` and `RedrawWindow` owe it for
  the region they name.
- `WM_NCPAINT` comes from `BeginPaint`, `UpdateWindow`, `RedrawWindow` with
  `RDW_UPDATENOW`, `GetUpdateRect` and `GetUpdateRgn` with erasing, and
  `SetWindowPos`, as before. Its `wParam` is the window's update region,
  frame and client, in screen coordinates (`TakeFramePaint`), or 1 when that
  region is the whole window; the region is deleted after the message
  (`DropFramePaint`) unless the window gave it to `GetDCEx`, which deletes it
  with its DC.
- `DefWindowProc` of `WM_NCPAINT` paints the frame clipped to that region and
  to the visible region (`DefaultFramePaint`); of `WM_NCACTIVATE`, `WM_SETTEXT`
  and `WM_SETICON` the whole frame. The caption of such a window shows active
  by the frame-on bit, which `WM_NCACTIVATE` sets, as on Windows
  (`CaptionActive`); a top-level window that the composition paints shows it
  by being the foreground window, as before.

The port's own controls (edit, list box, combo box, scroll bar, buttons,
labels, the common controls) keep their frames from the composition or from
the port on their thread, and so does a minimized window, which the
composition draws as its caption bar.

`tools/test_own_caption_host.py` (`tools/own_caption_fixture.c`): a window
that fills its caption after `DefWindowProc` keeps it when its client area is
painted again, when a popup covers part of it with and without the
activation, and when the popup goes; its first `WM_NCPAINT` carries 1, the one
after the popup the region the popup uncovered in screen coordinates; before
`DefWindowProc` the uncovered part of the caption still shows the popup; a
window that answers `WM_NCPAINT` and `WM_NCACTIVATE` itself has no frame.
2 of 9 checks with the composition painting every frame, 9 of 9 now.

## What it costs

Measured with `NEO98_RENDER_PROFILE=1` over four seconds of a window of 300
by 200 pixels that moves by a pixel every 10 ms across the caption of a
larger window behind it (73 steps on a busy machine), with and without a
caption of its own:

| | frame paints | USER's render time | `WM_NCPAINT` to the window behind |
| --- | --- | --- | --- |
| the composition paints every frame | 146 | 41 to 55 ms | 2 |
| the window's thread paints its frame | 76 | 17 ms | 74, one per step |

The window behind gets a `WM_NCPAINT` with the strip of its caption that each
step uncovers, as on Windows, and paints just that strip in its thread; the
moving window takes its frame along and is painted no more. What the
composition still does for such a frame is a hash of a few hundred bytes
(`FrameSignature`).

## What is left

- The port's own controls (see above) get no `WM_NCPAINT` for the frame the
  composition paints; a program that subclasses an edit control to draw its
  border keeps it only until the next composition.
- USER of Windows paints the scroll bars, the pressed caption buttons and
  the selected item of the menu bar at once, in the calling thread, without
  `WM_NCPAINT`; here they owe the window a `WM_NCPAINT` for its whole frame,
  painted when its thread next paints.
- Windows sends a window of another thread `WM_SYNCPAINT` when `SetWindowPos`
  uncovers it, so it paints its frame even while it only answers sent
  messages; here that frame waits for the thread's next `WM_PAINT`.
