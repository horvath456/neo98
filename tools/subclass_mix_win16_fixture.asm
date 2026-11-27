; SPDX-License-Identifier: GPL-2.0-or-later
; The 16-bit side of a 32-bit and a 16-bit window procedure in front of each
; other (tools/subclass_mix_fixture.c sets the hook that adds the 32-bit
; one, Sub32). Both classes of the program have the procedure class16, which
; answers message 401h with 1. The program's subclass sub16 answers 401h
; with 16 times what the procedure behind it answered, plus 2, and hands
; everything else down with CallWindowProc.
;
; A: the hook puts Sub32 in front of the window when it is made; then the
;    program puts sub16 in front with SetWindowLong(GWL_WNDPROC). 401h has
;    to go sub16, Sub32, class16: 132h.
; B: the program puts sub16 in front of the window; then the hook puts Sub32
;    in front of it when the program minimises the window. 401h: Sub32,
;    sub16, class16: 123h. Then the program writes the far pointer it read
;    for the window's procedure back with SetWindowLong: Sub32 stays.
; Each side also writes back what it reads (403h, 404h of Sub32).
;
; C:\SUBMIX16.TXT receives, as words: the far pointer of class16 and of
; sub16, then for A the far pointer SetWindowLong answered, the one
; GetWindowLong answers, 401h, 403h, 401h after 404h; for B the same five,
; and 401h after the program wrote back the far pointer it had read. Then
; C:\SUBMIX16.END. tools/test_win16_subclass_mix_host.py; the New Executable
; is made as for tools/layer_win16_fixture.asm.
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

; SendMessage(window, message, 0, 0) into the word at %3.
%macro ASK 3
        push word %1
        push word %2
        push word 0
        push word 0
        push word 0
        API 2, 111                      ; SendMessage
        mov [%3], ax
%endmacro

section code
code_start:
        API 1, 91                       ; InitTask
        mov [instance], di
        push di
        API 2, 5                        ; InitApp
        mov ax, [instance]
        mov [wc_instance], ax
        mov word [class16_ptr], class16
        mov [class16_ptr + 2], cs
        mov word [sub16_ptr], sub16
        mov [sub16_ptr + 2], cs
        mov ax, [class16_ptr]
        mov [wc_procedure], ax
        mov [wc_procedure + 2], cs
        mov [wc_class + 2], ds
        push ds
        push word wc
        API 2, 57                       ; RegisterClass SubMixA
        mov word [wc_class], class_b
        push ds
        push word wc
        API 2, 57                       ; RegisterClass SubMixB

        ; A: Sub32 is in front from the start.
        push ds
        push word class_a
        call make
        mov [window_a], ax
        push ax
        push word -4                    ; GWL_WNDPROC
        push word [sub16_ptr + 2]
        push word [sub16_ptr]
        API 2, 136                      ; SetWindowLong
        mov [old_a], ax
        mov [old_a + 2], dx
        push word [window_a]
        push word -4
        API 2, 135                      ; GetWindowLong
        mov [got_a], ax
        mov [got_a + 2], dx
        ASK [window_a], 0x401, chain_a
        ASK [window_a], 0x403, view_a
        ASK [window_a], 0x404, scratch
        ASK [window_a], 0x401, again_a

        ; B: sub16 first, Sub32 in front of it when the window is minimised.
        push ds
        push word class_b
        call make
        mov [window_b], ax
        push ax
        push word -4
        push word [sub16_ptr + 2]
        push word [sub16_ptr]
        API 2, 136                      ; SetWindowLong
        mov [old_b], ax
        mov [old_b + 2], dx
        push word [window_b]
        push word 7                     ; SW_SHOWMINNOACTIVE: HCBT_MINMAX
        API 2, 42                       ; ShowWindow
        push word [window_b]
        push word -4
        API 2, 135                      ; GetWindowLong
        mov [got_b], ax
        mov [got_b + 2], dx
        ASK [window_b], 0x401, chain_b
        ASK [window_b], 0x403, view_b
        ASK [window_b], 0x404, scratch
        ASK [window_b], 0x401, again_b
        push word [window_b]
        push word -4
        push word [got_b + 2]
        push word [got_b]
        API 2, 136                      ; SetWindowLong with what was read
        ASK [window_b], 0x401, back_b

        push ds
        push word fout
        push word 0
        API 1, 83                       ; _lcreat
        mov si, ax
        push si
        push ds
        push word class16_ptr
        push word results_end - class16_ptr
        API 1, 86                       ; _lwrite
        push si
        API 1, 81                       ; _lclose
        push ds
        push word fend
        push word 0
        API 1, 83                       ; _lcreat
        push ax
        API 1, 81                       ; _lclose
        push word [window_a]
        API 2, 53                       ; DestroyWindow
        push word [window_b]
        API 2, 53                       ; DestroyWindow
        mov ax, 4c00h
        API 1, 102                      ; DOS3Call
        jmp $

; make(class): a hidden overlapped window of the class.
make:
        push bp
        mov bp, sp
        push word [bp + 6]
        push word [bp + 4]
        push word [bp + 6]
        push word [bp + 4]              ; the title
        push word 0x00cf                ; WS_OVERLAPPEDWINDOW, not visible
        push word 0
        push word 40
        push word 30
        push word 200
        push word 120
        push word 0
        push word 0
        push word [instance]
        push word 0
        push word 0
        API 2, 41                       ; CreateWindow
        pop bp
        ret 4

; LRESULT FAR PASCAL class16(HWND, UINT, WPARAM, LPARAM): 1 for 401h,
; DefWindowProc for the rest. The caller has loaded DS.
class16:
        push bp
        mov bp, sp
        cmp word [bp + 12], 0x401
        jne .default
        mov ax, 1
        xor dx, dx
        pop bp
        retf 10
.default:
        push word [bp + 14]
        push word [bp + 12]
        push word [bp + 10]
        push word [bp + 8]
        push word [bp + 6]
        API 2, 107                      ; DefWindowProc
        pop bp
        retf 10

; sub16: CallWindowProc of the procedure SetWindowLong answered for this
; window; for 401h 16 times its answer plus 2.
sub16:
        push bp
        mov bp, sp
        mov bx, old_a
        mov ax, [bp + 14]
        cmp ax, [window_a]
        je .call
        mov bx, old_b
.call:
        push word [bx + 2]
        push word [bx]
        push word [bp + 14]
        push word [bp + 12]
        push word [bp + 10]
        push word [bp + 8]
        push word [bp + 6]
        API 2, 122                      ; CallWindowProc
        cmp word [bp + 12], 0x401
        jne .done
        shl ax, 4
        add ax, 2
        xor dx, dx
.done:
        pop bp
        retf 10
code_end:

section data
data_start:
        times 16 db 0                   ; the words of the local heap and the stack
instance dw 0
window_a dw 0
window_b dw 0
scratch  dw 0
wc:      dw 0                           ; style
wc_procedure dd 0
         dw 0, 0                        ; no class or window extra bytes
wc_instance dw 0
         dw 0, 0                        ; no icon, no cursor
         dw 6                           ; COLOR_WINDOW + 1
         dd 0                           ; no class menu
wc_class dw class_a, 0
class_a  db 'SubMixA', 0
class_b  db 'SubMixB', 0
fout     db 'C:\SUBMIX16.TXT', 0
fend     db 'C:\SUBMIX16.END', 0
class16_ptr dd 0
sub16_ptr   dd 0
old_a    dd 0
got_a    dd 0
chain_a  dw 0
view_a   dw 0
again_a  dw 0
old_b    dd 0
got_b    dd 0
chain_b  dw 0
view_b   dw 0
again_b  dw 0
back_b   dw 0
results_end:
         times 64 db 0
data_end:

section fix
fix_end:
