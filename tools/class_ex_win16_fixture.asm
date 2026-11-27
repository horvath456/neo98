; SPDX-License-Identifier: GPL-2.0-or-later
; A class registered with RegisterClassEx of 16-bit USER (USER.397), the way
; a 16-bit program written for Windows 95 registers its main class. The
; WNDCLASSEX of 16-bit USER starts with two dwords, cbSize and style; from
; the procedure on its fields follow as in a WNDCLASS, and the small icon
; comes after the class name.
;
; The program registers "ClassEx16" with CS_HREDRAW CS_VREDRAW CS_DBLCLKS,
; four class bytes, six window bytes, a cursor and a small icon, creates a
; window of the class (its procedure counts WM_NCCREATE and WM_CREATE),
; writes and reads a word of the window bytes, asks GetClassWord for the
; style and the class bytes, and fills a WNDCLASSEX with GetClassInfoEx
; (USER.398). C:\CLSEX16.TXT receives, as words: the atom, the window, the
; two counts, the window word read back, the class style, the class bytes,
; the result of GetClassInfoEx, the cursor and the small icon LoadCursor and
; LoadIcon gave, then the 34 bytes of the WNDCLASSEX of GetClassInfoEx.
; tools/test_win16_class_ex_host.py; the New Executable is made as for
; tools/layer_win16_fixture.asm.
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
        push word 0
        push word 0
        push word 0x7f00                ; IDC_ARROW
        API 2, 173                      ; LoadCursor
        mov [wc_cursor], ax
        mov [loaded_cursor], ax
        push word 0
        push word 0
        push word 0x7f04                ; IDI_ASTERISK
        API 2, 174                      ; LoadIcon
        mov [wc_small], ax
        mov [loaded_small], ax
        push ds
        push word wc
        API 2, 397                      ; RegisterClassEx
        mov [atom], ax
        push ds
        push word class
        push ds
        push word class                 ; the title
        push word 0x00cf                ; WS_OVERLAPPEDWINDOW
        push word 0
        push word 60                    ; x
        push word 50                    ; y
        push word 240                   ; width
        push word 160                   ; height
        push word 0                     ; no parent
        push word 0                     ; no menu
        push word [instance]
        push word 0
        push word 0                     ; no creation parameter
        API 2, 41                       ; CreateWindow
        mov [window], ax
        push ax
        push word 4                     ; the third word of the window bytes
        push word 0x5a17
        API 2, 134                      ; SetWindowWord
        push word [window]
        push word 4
        API 2, 133                      ; GetWindowWord
        mov [window_word], ax
        push word [window]
        push word -26                   ; GCW_STYLE
        API 2, 129                      ; GetClassWord
        mov [class_style], ax
        push word [window]
        push word -20                   ; GCW_CBCLSEXTRA
        API 2, 129                      ; GetClassWord
        mov [class_bytes], ax
        push word [instance]
        push ds
        push word class
        push ds
        push word info
        API 2, 398                      ; GetClassInfoEx
        mov [info_result], ax
        push ds
        push word fout
        push word 0
        API 1, 83                       ; _lcreat
        mov si, ax
        push si
        push ds
        push word atom
        push word results_end - atom
        API 1, 86                       ; _lwrite
        push si
        API 1, 81                       ; _lclose
        push word [window]
        API 2, 53                       ; DestroyWindow
        mov ax, 4c00h
        API 1, 102                      ; DOS3Call
        jmp $
; LRESULT FAR PASCAL procedure(HWND, UINT, WPARAM, LPARAM): counts
; WM_NCCREATE and WM_CREATE, and hands every message to DefWindowProc. The
; caller has loaded DS.
procedure:
        push bp
        mov bp, sp
        mov ax, [bp + 12]
        cmp ax, 0x0081                  ; WM_NCCREATE
        jne .create
        inc word [nccreates]
.create:
        cmp ax, 0x0001                  ; WM_CREATE
        jne .default
        inc word [creates]
.default:
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
message  times 32 db 0
wc:      dd 34                          ; cbSize
         dd 0x000b                      ; CS_HREDRAW CS_VREDRAW CS_DBLCLKS
wc_procedure dd 0
         dw 4, 6                        ; class bytes, window bytes
wc_instance dw 0
         dw 0                           ; no icon
wc_cursor dw 0
         dw 6                           ; COLOR_WINDOW + 1
         dd 0                           ; no class menu
wc_class dw class, 0
wc_small dw 0
class    db 'ClassEx16', 0
fout     db 'C:\CLSEX16.TXT', 0
atom        dw 0
window      dw 0
nccreates   dw 0
creates     dw 0
window_word dw 0
class_style dw 0
class_bytes dw 0
info_result dw 0
loaded_cursor dw 0
loaded_small dw 0
info        times 34 db 0xee
results_end:
         times 64 db 0
data_end:

section fix
fix_end:
