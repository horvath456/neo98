; SPDX-License-Identifier: GPL-2.0-or-later
; The status bar of the 16-bit COMMCTRL, as File Manager fills it.
;
; CreateStatusWindow (COMMCTRL.6) at the bottom of a window, then the
; messages with the arguments of 16 bits: SB_SETPARTS with two parts of
; 16-bit ints (the first ends at 150, the second at the right edge), SB_SETTEXT
; of a far string into each part, SB_GETTEXT of the second part into a far
; buffer, SB_GETTEXTLENGTH of the first, SB_GETPARTS into an array of 16-bit
; ints behind a guard word, SB_GETRECT of the first part into a RECT of words
; and SB_GETBORDERS into three words.
;
; The program writes the answers as words into C:\STATUS16.TXT: the window,
; the SB_SETPARTS answer, the two SB_SETTEXT answers, the two words of
; SB_GETTEXT and its sixteen bytes, the SB_GETTEXTLENGTH answer, the
; SB_GETPARTS answer, its two words and the guard word, the SB_GETRECT answer
; and its rectangle, the SB_GETBORDERS answer and its three words, and the
; client origin of the status bar on the screen. Then it runs its message
; loop until the host quits. tools/test_win16_status_host.py; the New
; Executable is made as for tools/layer_win16_fixture.asm.
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
; the status bar in BX, the answer in DX:AX.
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
        push word 100                   ; y
        push word 400                   ; width
        push word 200                   ; height
        push word 0                     ; no parent
        push word 0                     ; no menu
        push word [instance]
        push word 0
        push word 0                     ; no creation parameter
        API 2, 41                       ; CreateWindow
        mov [window], ax
        ; CreateStatusWindow(style, text, parent, id)
        push word 0x5000                ; WS_CHILD WS_VISIBLE
        push word 0
        push ds
        push word first_text
        push word [window]
        push word 7
        API 3, 6
        mov bx, ax
        mov [status], ax
        SEND 0x404, 2, ds, word parts   ; SB_SETPARTS
        mov [set_parts], ax
        SEND 0x401, 0, ds, word first_text ; SB_SETTEXT of part 0
        mov [set_first], ax
        SEND 0x401, 1, ds, word second_text ; SB_SETTEXT of part 1
        mov [set_second], ax
        SEND 0x402, 1, ds, word got_text ; SB_GETTEXT of part 1
        mov [get_text], ax
        mov [get_text + 2], dx
        SEND 0x403, 0, word 0, word 0   ; SB_GETTEXTLENGTH of part 0
        mov [text_length], ax
        SEND 0x406, 2, ds, word got_parts ; SB_GETPARTS
        mov [get_parts], ax
        SEND 0x40a, 0, ds, word got_rect ; SB_GETRECT of part 0
        mov [get_rect], ax
        SEND 0x407, 0, ds, word got_borders ; SB_GETBORDERS
        mov [get_borders], ax
        push word [status]
        push ds
        push word origin
        API 2, 28                       ; ClientToScreen
        push word [window]
        API 2, 124                      ; UpdateWindow
        push ds
        push word fout
        push word 0
        API 1, 83                       ; _lcreat
        mov si, ax
        push si
        push ds
        push word status
        push word results_end - status
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
message  times 32 db 0
wc:      dw 0                           ; style
wc_procedure dd 0
         dw 0, 0                        ; no class or window extra bytes
wc_instance dw 0
         dw 0, 0                        ; no icon, no cursor
         dw 16                          ; COLOR_BTNFACE + 1
         dd 0                           ; no class menu
wc_class dw class, 0
class    db 'Status16', 0
fout     db 'C:\STATUS16.TXT', 0
first_text  db 'First part', 0
second_text db 'Second part', 0
parts    dw 150, -1                     ; the right edges of the parts
status   dw 0                           ; the results, in this order
set_parts dw 0
set_first dw 0
set_second dw 0
get_text dw 0, 0
got_text times 16 db 0
text_length dw 0
get_parts dw 0
got_parts dw 0aaaah, 0aaaah
guard    dw 0aaaah
get_rect dw 0
got_rect dw 0, 0, 0, 0
get_borders dw 0
got_borders dw 0, 0, 0
origin   dw 0, 0
results_end:
         times 512 db 0
data_end:

section fix
fix_end:
