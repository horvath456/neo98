; SPDX-License-Identifier: GPL-2.0-or-later
; A window that is created minimized and then restored, the way the 16-bit
; Minesweeper makes its window: CreateWindow with WS_MINIMIZE and a menu,
; then ShowWindow SW_SHOWNORMAL. The window procedure hands every
; message on to DefWindowProc, WM_NCCALCSIZE as well.
;
; After the restore the program writes, as words, the window rectangle, the
; client rectangle, the screen position of the client origin and the style
; into C:\RESTOR16.TXT, then runs its message loop until the host quits.
; tools/test_win16_restore_frame_host.py; the New Executable is made as for
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
        push ds
        push word wc
        API 2, 57                       ; RegisterClass
        API 2, 151                      ; CreateMenu
        mov [menu], ax
        push ax
        push word 0                     ; MF_STRING
        push word 100
        push ds
        push word game
        API 2, 411                      ; AppendMenu
        push ds
        push word class
        push ds
        push word class                 ; the title
        push word 0x20cf                ; WS_MINIMIZE WS_OVERLAPPEDWINDOW
        push word 0
        push word 100                   ; x
        push word 80                    ; y
        push word 240                   ; width
        push word 160                   ; height
        push word 0                     ; no parent
        push word [menu]
        push word [instance]
        push word 0
        push word 0                     ; no creation parameter
        API 2, 41                       ; CreateWindow
        mov [window], ax
        push ax
        push word 1                     ; SW_SHOWNORMAL
        API 2, 42                       ; ShowWindow
        push word [window]
        API 2, 124                      ; UpdateWindow
        push word [window]
        push ds
        push word frame
        API 2, 32                       ; GetWindowRect
        push word [window]
        push ds
        push word client
        API 2, 33                       ; GetClientRect
        push word [window]
        push ds
        push word origin
        API 2, 28                       ; ClientToScreen
        push word [window]
        push word -16                   ; GWL_STYLE
        API 2, 135                      ; GetWindowLong
        mov [style], ax
        mov [style + 2], dx
        push ds
        push word fout
        push word 0
        API 1, 83                       ; _lcreat
        mov si, ax
        push si
        push ds
        push word frame
        push word results_end - frame
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
menu     dw 0
message  times 32 db 0
wc:      dw 0                           ; style
wc_procedure dd 0
         dw 0, 0                        ; no class or window extra bytes
wc_instance dw 0
         dw 0, 0                        ; no icon, no cursor
         dw 6                           ; COLOR_WINDOW + 1
         dd 0                           ; no class menu
wc_class dw class, 0
class    db 'Restore16', 0
game     db '&Game', 0
fout     db 'C:\RESTOR16.TXT', 0
frame    dw 0, 0, 0, 0                  ; GetWindowRect
client   dw 0, 0, 0, 0                  ; GetClientRect
origin   dw 0, 0                        ; ClientToScreen of the client origin
style    dd 0                           ; GetWindowLong GWL_STYLE
results_end:
         times 64 db 0
data_end:

section fix
fix_end:
