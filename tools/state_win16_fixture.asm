; SPDX-License-Identifier: GPL-2.0-or-later
; The 16-bit Windows guest of the saved states (tools/test_state_host.py,
): a window with a counter on a timer, and the
; places a 16-bit program can stand in when a state is taken.
;
; M a message box, opened inside the window procedure
; D a dialog with two buttons, opened inside the window procedure; its
; button "Box" opens a message box inside the dialog procedure
; L a loop of its own inside the window procedure (PeekMessage and
; DispatchMessage), the way a game runs that never returns to its
; first message loop; E ends it
; Q ends the program
; F opens the native common file dialog
;
; Every answer goes into a file D:\W16x.TXT, "result=NN", x the letter of
; the step (B box, D dialog, L loop, C click, Q quit). The window draws a
; black bar as long as the low byte of the counter, and below it one square
; for every answered step, so a picture says where the program stands.
;
; No 16-bit compiler and no linker: nasm writes three flat parts behind a
; header of their sizes, and tools/win16_fixture.py makes the New Executable
; of them (tools/layer_win16_fixture.asm has the same shape).
        bits 16
section header start=0
        dw code_end - code_start, data_end - data_start, (fix_end - fix_start) / 6
section code vstart=0 follows=header align=1
section data vstart=0 follows=code align=1
section fix vstart=0 follows=data align=1
fix_start:

; A far call the loader fills in: module 1 is KERNEL, 2 is USER, 3 is GDI.
%macro API 2
%%at:   db 0x9a, 0xff, 0xff, 0x00, 0x00
[section fix]
        dw %%at + 1 - code_start, %1, %2
__SECT__
%endmacro
%macro FAR 1                            ; a far pointer into the data segment
        push ds
        push word %1
%endmacro

section code
code_start:
        API 1, 91                       ; InitTask: DI is the instance
        mov [instance], di
        push di
        API 2, 5                        ; InitApp
        mov word [wc_proc], WndProc
        mov [wc_proc + 2], cs
        mov ax, [instance]
        mov [wc_instance], ax
        mov word [wc_name], class_name
        mov [wc_name + 2], ds
        push word 0                     ; WHITE_BRUSH
        API 3, 87                       ; GetStockObject
        mov [wc_brush], ax
        FAR wc
        API 2, 57                       ; RegisterClass
%ifdef STATE_BAR_MENU
        API 2, 151                      ; CreateMenu
        mov [bar_menu], ax
        API 2, 151                      ; CreateMenu; AppendMenu makes it a submenu
        mov [first_menu], ax
        push ax
        push word 0
        push word 7
        FAR first_item
        API 2, 411                      ; AppendMenu
        API 2, 151
        mov [second_menu], ax
        push ax
        push word 0
        push word 8
        FAR second_item
        API 2, 411
        push word [bar_menu]
        push word 0x10
        push word [first_menu]
        FAR first_label
        API 2, 411
        push word [bar_menu]
        push word 0x10
        push word [second_menu]
        FAR second_label
        API 2, 411
%endif
        FAR class_name
        FAR title
        push word 0x10cf                ; WS_OVERLAPPEDWINDOW | WS_VISIBLE
        push word 0
        push word 40
        push word 40
        push word 380
        push word 260
        push word 0
%ifdef STATE_BAR_MENU
        push word [bar_menu]
%else
        push word 0
%endif
        push word [instance]
        push word 0
        push word 0
        API 2, 41                       ; CreateWindow
        mov [window], ax
        push ax
        push word 5
        API 2, 42                       ; ShowWindow
        push word [window]
        API 2, 22                       ; SetFocus
        push word [window]
        push word 1
        push word 50
        push word 0
        push word 0
        API 2, 10                       ; SetTimer
pump:   FAR msg
        push word 0
        push word 0
        push word 0
        API 2, 108                      ; GetMessage
        or ax, ax
        jz ended
        FAR msg
        API 2, 113                      ; TranslateMessage
        FAR msg
        API 2, 114                      ; DispatchMessage
        jmp pump
ended:  mov ax, 4c00h
        API 1, 102                      ; DOS3Call
        jmp $

; AL the letter of the file, BX the value (below 100).
Note:   push si
        mov [note_name + 6], al
        mov ax, bx
        mov bl, 10
        div bl
        add ax, 0x3030
        mov [note_text + 7], ax
        FAR note_name
        push word 0
        API 1, 83                       ; _lcreat
        mov si, ax
        push si
        FAR note_text
        push word 11
        API 1, 86                       ; _lwrite
        push si
        API 1, 81                       ; _lclose
        inc word [steps]
        pop si
        ret

; The window procedure: hwnd [bp+14], message [bp+12], wParam [bp+10],
; lParam [bp+6]. The caller has loaded DS.
WndProc:
        push bp
        mov bp, sp
        push si
        push di
        mov ax, [bp + 12]
%ifdef STATE_BAR_MENU
        cmp ax, 0x111
        je near w_barcommand
        cmp ax, 0x212
        je near w_menuexit
%endif
        cmp ax, 0x113
        je near w_timer
        cmp ax, 0xf
        je near w_paint
        cmp ax, 0x100
        je near w_key
        cmp ax, 1
        je near w_create
        cmp ax, 0x201
        je near w_click
        cmp ax, 2
        je near w_destroy
w_default:
        push word [bp + 14]
        push word [bp + 12]
        push word [bp + 10]
        push word [bp + 8]
        push word [bp + 6]
        API 2, 107                      ; DefWindowProc
        jmp w_out
w_zero: xor ax, ax
        xor dx, dx
w_out:  pop di
        pop si
        pop bp
        retf 10
%ifdef STATE_BAR_MENU
w_barcommand:
        mov bx, [bp + 10]
        mov al, 'U'
        call Note
        jmp w_again
w_menuexit:
        mov bx, 1
        mov al, 'X'
        call Note
        jmp w_zero
%endif
w_timer:
        inc word [counter]
w_again:
        push word [bp + 14]
        push word 0
        push word 0
        push word 0
        API 2, 125                      ; InvalidateRect
        jmp w_zero
w_paint:
        push word [bp + 14]
        FAR ps
        API 2, 39                       ; BeginPaint
        mov si, ax
        push si                         ; the bar: white, then black as far as the counter
        push word 0
        push word 0
        push word 256
        push word 40
        push word 0x00ff
        push word 0x0062
        API 3, 29                       ; PatBlt, WHITENESS
        push si
        push word 0
        push word 0
        mov ax, [counter]
        xor ah, ah
        push ax
        push word 40
        push word 0
        push word 0x0042
        API 3, 29                       ; PatBlt, BLACKNESS
        push si                         ; a square for every answered step
        push word 0
        push word 60
        mov ax, [steps]
        mov cl, 4
        shl ax, cl
        push ax
        push word 16
        push word 0
        push word 0x0042
        API 3, 29
        push word [bp + 14]
        FAR ps
        API 2, 40                       ; EndPaint
        jmp w_zero
w_click:
        inc word [clicks]
        mov al, 'C'
        mov bx, [clicks]
        call Note
        jmp w_again
w_destroy:
        push word 0
        API 2, 6                        ; PostQuitMessage
        jmp w_zero
w_key:  mov ax, [bp + 10]
        cmp ax, 'I'
        je near w_initdialog
        cmp ax, 'N'
        je near w_nativedialog
        cmp ax, 'H'
        je near w_newwindow
        cmp ax, 'M'
        je w_box
        cmp ax, 'D'
        je w_dialog
        cmp ax, 'L'
        je w_loop
        cmp ax, 'F'
        je near w_file
        cmp ax, 'Q'
        jne near w_zero
        mov al, 'Q'
        mov bx, 1
        call Note
        push word 0
        API 2, 6                        ; PostQuitMessage
        jmp w_zero
w_box:  push word [bp + 14]
        FAR box_text
        FAR box_title
        push word 1                     ; MB_OKCANCEL
        API 2, 1                        ; MessageBox
        mov bx, ax
        mov al, 'B'
        call Note
        jmp w_again
w_dialog:
        push word 2                     ; GMEM_MOVEABLE
        push word 0
        push word dialog_end - dialog_template
        API 1, 15                       ; GlobalAlloc
        mov di, ax
        push di
        API 1, 18                       ; GlobalLock: DX:AX
        push di
        mov es, dx
        mov di, ax
        mov si, dialog_template
        mov cx, dialog_end - dialog_template
        cld
        rep movsb
        pop di
        push word [instance]
        push di
        push word [bp + 14]
        push cs
        push word DlgProc
        API 2, 218                      ; DialogBoxIndirect
        mov bx, ax
        mov al, 'D'
        call Note
        mov word [init_box], 0
        jmp w_again
w_initdialog:
        mov word [init_box], 1
        jmp w_dialog
w_nativedialog:
        mov word [init_box], 2
        jmp w_dialog
w_newwindow:
        mov word [creating], 1
        FAR class_name
        FAR title
        push word 0x80c8
        push word 0
        push word 450
        push word 40
        push word 200
        push word 180
        push word [bp + 14]
        push word 0
        push word [instance]
        push word 0
        push word 0
        API 2, 41
        xor bx, bx
        or ax, ax
        jz w_notcreated
        inc bx
w_notcreated:
        mov al, 'H'
        call Note
        push word [bp + 14]
        API 2, 22
        jmp w_again
w_create:
        cmp word [creating], 0
        je near w_default
        mov word [creating], 0
        push word [bp + 14]
        FAR box_text
        FAR box_title
        push word 1
        API 2, 1
        mov bx, ax
        mov al, 'B'
        call Note
        jmp w_zero
w_file:
        mov ax, [bp + 14]
        mov [ofn + 4], ax
        mov [ofn + 10], ds
        mov [ofn + 26], ds
        mov [ofn + 42], ds
        FAR ofn
        API 4, 1                        ; COMMDLG.GetOpenFileName
        mov bx, ax
        mov al, 'F'
        call Note
        jmp w_again
w_loop: FAR msg2
        push word 0
        push word 0
        push word 0
        push word 1                     ; PM_REMOVE
        API 2, 109                      ; PeekMessage
        or ax, ax
        jz w_spin
        cmp word [msg2 + 2], 0x100
        jne w_hand
        cmp word [msg2 + 4], 'E'
        je w_left
w_hand: FAR msg2
        API 2, 113                      ; TranslateMessage
        FAR msg2
        API 2, 114                      ; DispatchMessage
w_spin: inc word [spins]
        jmp w_loop
w_left: mov al, 'L'
        mov bx, 1
        call Note
        jmp w_again

; The dialog procedure, the same frame.
DlgProc:
        push bp
        mov bp, sp
        push si
        push di
        mov ax, [bp + 12]
        cmp ax, 0x110
        je d_init
        cmp ax, 0x111
        jne d_zero
        mov si, [bp + 10]
        cmp si, 3
        jne d_end
        push word [bp + 14]
        FAR box_text
        FAR box_title
        push word 1
        API 2, 1                        ; MessageBox inside the dialog procedure
        add si, ax                      ; 3 and the answer of the box
d_end:  add si, 40
        push word [bp + 14]
        push si
        API 2, 88                       ; EndDialog
d_one:  mov ax, 1
        jmp d_out
d_zero: xor ax, ax
d_out:  xor dx, dx
        pop di
        pop si
        pop bp
        retf 10
d_init:
        cmp word [init_box], 2
        je d_native
        cmp word [init_box], 0
        je d_one
        push word [bp + 14]
        FAR box_text
        FAR box_title
        push word 1
        API 2, 1
        mov bx, ax
        mov al, 'I'
        call Note
        jmp d_one
d_native:
        mov word [init_box], 3
        push word [bp + 14]
        push word -4
        API 2, 135                      ; GetWindowLong, the native default dialog procedure
        mov [native_proc], ax
        mov [native_proc + 2], dx
        push dx
        push ax
        push word [bp + 14]
        push word [bp + 12]
        push word [bp + 10]
        push word [bp + 8]
        push word [bp + 6]
        API 2, 122                      ; CallWindowProc invokes the dialog procedure again
        xor bx, bx
        or ax, ax
        jz d_native_zero
        inc bx
d_native_zero:
        mov al, 'N'
        call Note
        jmp d_one
code_end:

section data
data_start:
        times 16 db 0                   ; the words of the local heap and the stack
instance dw 0
window  dw 0
counter dw 0
steps   dw 0
clicks  dw 0
spins   dw 0
creating dw 0
init_box dw 0
native_proc dd 0
wc:     dw 0                            ; WNDCLASS: style
wc_proc dd 0
        dw 0, 0
wc_instance dw 0
        dw 0, 0
wc_brush dw 0
        dd 0
wc_name dd 0
class_name db 'StateFixture16', 0
title   db 'State fixture 16', 0
box_text db 'A modal box of the 16-bit fixture.', 0
box_title db 'StateFixture box', 0
ofn:    dd 72
        dw 0, 0                         ; owner, instance
        dw file_filter, 0
        dd 0, 0, 1                     ; custom filter, bytes, filter index
        dw file_name, 0
        dd 260
        dd 0, 0                        ; file title, bytes
        dw file_directory, 0
        dd 0                           ; title
        dd 12                          ; OFN_HIDEREADONLY | OFN_NOCHANGEDIR
        dw 0, 0                        ; file offset, extension
        dd 0, 0, 0, 0                 ; extension, data, hook, template
file_filter db 'All files', 0, '*.*', 0, 0
file_name times 260 db 0
file_directory db 'D:\', 0
note_name db 'D:\W16x.TXT', 0
note_text db 'result=00', 13, 10
dialog_template:
        dd 0x90c80080                   ; DS_MODALFRAME | WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU
        db 2
        dw 20, 20, 120, 50
        db 0, 0
        db 'StateFixture dialog', 0
        dw 10, 20, 45, 14, 1
        dd 0x50010001                   ; BS_DEFPUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP
        db 0x80
        db 'OK', 0
        db 0
        dw 65, 20, 45, 14, 3
        dd 0x50010000
        db 0x80
        db 'Box', 0
        db 0
dialog_end:
msg     times 18 db 0
msg2    times 18 db 0
ps      times 32 db 0
%ifdef STATE_BAR_MENU
bar_menu dw 0
first_menu dw 0
second_menu dw 0
first_label db '&First', 0
second_label db '&Second', 0
first_item db 'Resume first', 0
second_item db 'Resume second', 0
%endif
data_end:

section fix
fix_end:
