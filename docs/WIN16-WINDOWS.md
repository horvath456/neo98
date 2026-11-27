# Window classes and procedures of 16-bit programs

A 16-bit program runs on the same windows as a 32-bit one: Neo98 has one
window manager and one class store, and the 16-bit calls of USER
(`src/win16_user.cpp`, `src/win16_user_more.inc`) translate their records
and handles into those of the 32-bit side and back. This page says what the
translation has to keep, for the records of a window class and for window
procedures.

## RegisterClassEx and GetClassInfoEx

The records of 16-bit USER are not those of USER32 with shorter fields:

| field | `WNDCLASS` | `WNDCLASSEX` |
| --- | --- | --- |
| `cbSize` | | dword at 0 |
| `style` | word at 0 | dword at 4 |
| `lpfnWndProc` | far pointer at 2 | far pointer at 8 |
| `cbClsExtra`, `cbWndExtra`, `hInstance`, `hIcon`, `hCursor`, `hbrBackground` | words from 6 to 17 | words from 12 to 23 |
| `lpszMenuName`, `lpszClassName` | far pointers at 18 and 22 | far pointers at 24 and 28 |
| `hIconSm` | | word at 32 |

From the procedure on the extended record is a `WNDCLASS` that begins six
bytes in; only the style lies in front of it, as a dword (`ClassExBase` in
`src/win98_win16_user.h`). Neo98 read the extended record as a `WNDCLASS`
behind `cbSize`: the procedure became the high word of the style and the
offset of the real procedure, the class name was read from the selector of
the menu name, and the class was not registered. Character Map of Windows 95
registers its main class this way and ended before it made a window.

What the two calls answer about icons and cursors are the words the 16-bit
side knows them by: those `LoadIcon`, `LoadCursor` and `GetClassWord` give
(aliases of the native handles, `Win16ImageAlias`). `GetClassInfo` and
`GetClassInfoEx` used to write the low word of the native handle, which no
16-bit call accepts. `GetClassInfoEx` answers the small icon the class was
registered with; it answered none. Its result is the atom of the class, as
USER's (the plain `GetClassInfo` answers TRUE), and it leaves `cbSize` as
the program set it.

Behind its window Character Map names the keystroke of the character under
the mouse with `VkKeyScan` of the 16-bit keyboard driver (KEYBOARD.129),
which the port did not carry: the press on a character ended the program.
It answers what `VkKeyScanA` answers, the virtual key in the low byte and
the shift state in the high one (`tools/test_win16_ordinals_host.py`).

`tools/test_win16_class_ex_host.py` registers a class with
`RegisterClassEx` from a 16-bit fixture (`tools/class_ex_win16_fixture.asm`),
creates a window of it, uses its window bytes and reads the class back with
`GetClassWord` and `GetClassInfoEx`.

## Window procedures across the two sides

A window has one procedure, and either side may read or replace it. What
each side gets for a procedure of the other is something it can store, hand
to its `CallWindowProc` and write back:

- The 16-bit side reads a 32-bit procedure (`GetWindowLong` or the answer of
  `SetWindowLong`, `GWL_WNDPROC`) as a far pointer into the gate region,
  one per procedure (`Win16NativePointer`). `CallWindowProc` (USER.122) and
  `SetWindowLong` take it back as that procedure (`Win16Procedure`); USER of
  Windows 9x hands out a 16-bit thunk the same way.
- The 32-bit side reads a 16-bit procedure as a mark above 71010000h
  (`Win16Mark`); `CallWindowProcA` calls the 16-bit code behind it, and
  written back it stays the same procedure.
- A 16-bit procedure read by the 16-bit side is its own far pointer.

`tools/test_win16_subclass_mix_host.py` puts a 32-bit procedure from a
`WH_CBT` hook DLL (`tools/subclass_mix_fixture.c`) and a 16-bit one
(`tools/subclass_mix_win16_fixture.asm`) in front of the class procedure of
a 16-bit window, in both orders, and checks that a message passes each of
the three once and in that order, and that writing back what was read
changes nothing.

### The 16-bit Microsoft Foundation Classes and a procedure set at creation

The 16-bit MFC (2.x) registers its window classes with its own procedure,
`AfxWndProc`, and takes each window it creates with a `WH_CALLWNDPROC` hook
of its own at the first message the window gets (`WM_GETMINMAXINFO` or
`WM_NCCREATE`): it sets `AfxWndProc` with `SetWindowLong` and keeps the
procedure it was answered as the one `CWnd::DefWindowProc` calls, unless
that is `AfxWndProc` itself. A `WH_CBT` hook that puts a procedure P in
front of such a window at `HCBT_CREATEWND` comes first: P's predecessor is
`AfxWndProc`, the class procedure, and MFC then keeps P as the predecessor
of `AfxWndProc`. A message `AfxWndProc` does not handle goes to P, P hands it
to `AfxWndProc`, and the two call each other without end. That is the rule
of the library, and the thunks of Windows give it the same ring; Neo98
stops it at 32 nested 16-bit callbacks ("Win16 window callback nesting
exceeded") where Windows would run out of stack. A hook that wants such a
window takes it after the library has (at `HCBT_ACTIVATE`, or at a message
after `WM_NCCREATE`): P's predecessor is then `AfxWndProc`, and MFC keeps
`DefWindowProc`.
