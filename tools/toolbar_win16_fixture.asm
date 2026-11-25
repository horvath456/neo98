; SPDX-License-Identifier: GPL-2.0-or-later
; The toolbar of the 16-bit COMMCTRL, as File Manager fills it. Bitmap 1 of
; the program is a strip of two images of 16 by 15 pixels, red and green.
;
; top     CreateToolbarEx with no bitmap and no buttons; then TB_ADDBITMAP
;         with three images of the strip, MAKELONG(instance, 1), and
;         TB_ADDBUTTONS with three 16-bit TBBUTTONs, the third on the image
;         the strip does not have. TB_BUTTONCOUNT, TB_GETBUTTON of the second
;         button and TB_GETITEMRECT of all three ask the toolbar back.
; bottom  CreateToolbarEx (CCS_BOTTOM) with the strip and two buttons in the
;         call itself, bitmap size 16 by 15.
; text    a hidden toolbar (CCS_NORESIZE) with four buttons on strings:
;         TB_ADDSTRING of a list behind a far pointer ("One", "Two") and of
;         string 1 of the program ("|Three|Four|", '|' separates),
;         TB_GETBUTTONTEXT of the buttons on "One" and "Four", of the one
;         without a string, of a command no button has and without a buffer,
;         then TB_GETROWS, TB_SETROWS to two rows with its rectangle, TB_GETROWS
;         and TB_SETROWS back to one row.
;
; The program writes the answers as words into C:\TOOLBR16.TXT: the index
; TB_ADDBITMAP answered, the button count, the twelve bytes of TB_GETBUTTON,
; the three item rectangles of the top toolbar and its client origin on the
; screen, then the two item rectangles of the bottom toolbar and its origin,
; then the answers of the text toolbar: the two TB_ADDSTRING indices, the
; length and the sixteen bytes of the text of "One", the lengths for the
; button without a string, for "Four" with its sixteen bytes, for the missing
; command and without a buffer, the rows before, TB_SETROWS(2) with its
; rectangle, the rows after, TB_SETROWS(1) with its rectangle and the rows
; after that. Then it runs its message loop until the host quits.
; tools/test_win16_toolbar_host.py; the New Executable is made as for
; tools/layer_win16_fixture.asm, with the bitmap as a resource.
        bits 16
section header start=0
        dw code_end - code_start, data_end - data_start, (fix_end - fix_start) / 6
section code vstart=0 follows=header align=1
section data vstart=0 follows=code align=1
section fix vstart=0 follows=data align=1
fix_start:

%macro API 2
%%at:   db 0x9a, 0xff, 0xff, 0x00, 0x00
[section fix]
        dw %%at + 1 - code_start, %1, %2
__SECT__
%endmacro
; SEND message, wParam, lParam high word, lParam low word: SendMessage to
; the toolbar in BX, the answer in AX. The two halves of lParam are operands
; of push.
%macro SEND 4
        push bx
        push bx
        push word %1
        push word %2
        push %3
        push %4
        API 2, 111                      ; SendMessage
        pop bx
%endmacro

section code
code_start:
        API 1, 91                       ; InitTask
        mov [instance], di
        push di
        API 2, 5                        ; InitApp
        mov ax, [instance]
        mov [wc_instance], ax
        mov word [wc_procedure], procedure
        mov [wc_procedure + 2], cs
        mov [wc_class + 2], ds
        push ds
        push word wc
        API 2, 57                       ; RegisterClass
        push ds
        push word class
        push ds
        push word class                 ; the title
        push word 0x10cf                ; WS_VISIBLE WS_OVERLAPPEDWINDOW
        push word 0
        push word 100                   ; x
        push word 80                    ; y
        push word 320                   ; width
        push word 200                   ; height
        push word 0                     ; no parent
        push word 0                     ; no menu
        push word [instance]
        push word 0
        push word 0                     ; no creation parameter
        API 2, 41                       ; CreateWindow
        mov [window], ax
        ; The top toolbar, empty at first.
        push ax
        push word 0x5000                ; WS_CHILD WS_VISIBLE
        push word 0
        push word 1                     ; identifier
        push word 0                     ; no bitmaps
        push word 0
        push word 0
        push word 0
        push word 0                     ; no buttons
        push word 0
        push word 0
        push word 0
        push word 0
        push word 0
        push word 12                    ; sizeof the 16-bit TBBUTTON
        API 3, 20                       ; CreateToolbarEx
        mov bx, ax
        mov [top], ax
        SEND 0x413, 3, word 1, word [instance] ; TB_ADDBITMAP: three images of bitmap 1
        mov [added], ax
        SEND 0x414, 3, ds, word buttons ; TB_ADDBUTTONS
        SEND 0x418, 0, word 0, word 0   ; TB_BUTTONCOUNT
        mov [count], ax
        SEND 0x417, 1, ds, word got     ; TB_GETBUTTON of the second button
        SEND 0x41d, 0, ds, word top_rects ; TB_GETITEMRECT
        SEND 0x41d, 1, ds, word top_rects + 8
        SEND 0x41d, 2, ds, word top_rects + 16
        push word [top]
        push ds
        push word top_origin
        API 2, 28                       ; ClientToScreen
        ; The bottom toolbar, with its strip and its buttons in the call.
        push word [window]
        push word 0x5000                ; WS_CHILD WS_VISIBLE
        push word 3                     ; CCS_BOTTOM
        push word 2                     ; identifier
        push word 2                     ; two bitmaps
        push word [instance]
        push word 1                     ; bitmap 1
        push ds
        push word buttons
        push word 2                     ; two buttons
        push word 0
        push word 0                     ; the default button size
        push word 16
        push word 15                    ; bitmap size
        push word 12
        API 3, 20                       ; CreateToolbarEx
        mov bx, ax
        mov [bottom], ax
        SEND 0x41d, 0, ds, word bottom_rects ; TB_GETITEMRECT
        SEND 0x41d, 1, ds, word bottom_rects + 8
        push word [bottom]
        push ds
        push word bottom_origin
        API 2, 28                       ; ClientToScreen
        ; The text toolbar, hidden, with its buttons but without strings.
        push word [window]
        push word 0x4000                ; WS_CHILD
        push word 0x000c                ; CCS_NORESIZE CCS_NOPARENTALIGN
        push word 3                     ; identifier
        push word 2                     ; two bitmaps
        push word [instance]
        push word 1                     ; bitmap 1
        push ds
        push word text_buttons
        push word 4                     ; four buttons
        push word 0
        push word 0                     ; the default button size
        push word 16
        push word 15                    ; bitmap size
        push word 12
        API 3, 20                       ; CreateToolbarEx
        mov bx, ax
        SEND 0x41c, 0, ds, word strings ; TB_ADDSTRING of "One", "Two"
        mov [string_first], ax
        SEND 0x41c, [instance], word 0, word 1 ; TB_ADDSTRING of string 1
        mov [string_second], ax
        SEND 0x42d, 200, ds, word text_one ; TB_GETBUTTONTEXT
        mov [length_one], ax
        SEND 0x42d, 203, ds, word text_none
        mov [length_none], ax
        SEND 0x42d, 202, ds, word text_four
        mov [length_four], ax
        SEND 0x42d, 999, ds, word text_none
        mov [length_missing], ax
        SEND 0x42d, 200, word 0, word 0
        mov [length_unbuffered], ax
        SEND 0x428, 0, word 0, word 0   ; TB_GETROWS
        mov [rows_before], ax
        SEND 0x427, 2, ds, word rect_two ; TB_SETROWS
        mov [set_two], ax
        SEND 0x428, 0, word 0, word 0
        mov [rows_two], ax
        SEND 0x427, 1, ds, word rect_one
        mov [set_one], ax
        SEND 0x428, 0, word 0, word 0
        mov [rows_one], ax
        push word [window]
        API 2, 124                      ; UpdateWindow
        push ds
        push word fout
        push word 0
        API 1, 83                       ; _lcreat
        mov si, ax
        push si
        push ds
        push word added
        push word results_end - added
        API 1, 86                       ; _lwrite
        push si
        API 1, 81                       ; _lclose
next:   push ds
        push word message
        push word 0
        push word 0
        push word 0
        API 2, 108                      ; GetMessage
        or ax, ax
        jz quit
        push ds
        push word message
        API 2, 113                      ; TranslateMessage
        push ds
        push word message
        API 2, 114                      ; DispatchMessage
        jmp next
quit:   mov ax, 4c00h
        API 1, 102                      ; DOS3Call
        jmp $
; LRESULT FAR PASCAL procedure(HWND, UINT, WPARAM, LPARAM): DefWindowProc.
procedure:
        push bp
        mov bp, sp
        push word [bp + 14]             ; hwnd
        push word [bp + 12]             ; message
        push word [bp + 10]             ; wParam
        push word [bp + 8]              ; lParam, high word
        push word [bp + 6]              ; lParam, low word
        API 2, 107                      ; DefWindowProc
        pop bp
        retf 10
code_end:

section data
data_start:
        times 16 db 0                   ; the words of the local heap and the stack
instance dw 0
window   dw 0
top      dw 0
bottom   dw 0
message  times 32 db 0
wc:      dw 0                           ; style
wc_procedure dd 0
         dw 0, 0                        ; no class or window extra bytes
wc_instance dw 0
         dw 0, 0                        ; no icon, no cursor
         dw 16                          ; COLOR_BTNFACE + 1
         dd 0                           ; no class menu
wc_class dw class, 0
class    db 'Toolbar16', 0
fout     db 'C:\TOOLBR16.TXT', 0
; TBBUTTON of the 16-bit COMMCTRL: iBitmap, idCommand, fsState
; (TBSTATE_ENABLED), fsStyle (TBSTYLE_BUTTON), dwData, iString.
buttons  dw 0, 100
         db 4, 0
         dd 0
         dw -1
         dw 1, 101
         db 4, 0
         dd 12345678h
         dw -1
         dw 2, 102
         db 4, 0
         dd 0
         dw -1
; The text toolbar: images 0 and 1 on the strings 0, 1 and 3, the last
; button on no string.
text_buttons:
         dw 0, 200
         db 4, 0
         dd 0
         dw 0
         dw 1, 201
         db 4, 0
         dd 0
         dw 1
         dw 0, 202
         db 4, 0
         dd 0
         dw 3
         dw 1, 203
         db 4, 0
         dd 0
         dw -1
strings  db 'One', 0, 'Two', 0, 0
added    dw 0                           ; TB_ADDBITMAP
count    dw 0                           ; TB_BUTTONCOUNT
got      times 12 db 0                  ; TB_GETBUTTON of the second button
top_rects times 24 db 0                 ; TB_GETITEMRECT of the top toolbar
top_origin dw 0, 0
bottom_rects times 16 db 0              ; TB_GETITEMRECT of the bottom toolbar
bottom_origin dw 0, 0
string_first dw 0                       ; the text toolbar
string_second dw 0
length_one dw 0
text_one times 16 db 0
length_none dw 0
length_four dw 0
text_four times 16 db 0
length_missing dw 0
length_unbuffered dw 0
rows_before dw 0
set_two  dw 0
rect_two times 8 db 0
rows_two dw 0
set_one  dw 0
rect_one times 8 db 0
rows_one dw 0
results_end:
text_none times 16 db 0
         times 64 db 0
data_end:

section fix
fix_end:
