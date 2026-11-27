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
