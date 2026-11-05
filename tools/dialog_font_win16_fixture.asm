; SPDX-License-Identifier: GPL-2.0-or-later
; The font of a 16-bit dialog template. The program expects
; Windows 3.10 and opens two modeless dialogs from the same DS_SETFONT
; template with "MS Sans Serif" 8, the second one with DS_3DLOOK:
;
; "Bold16" USER asks for FW_BOLD; GDI simulates the bold of the
; raster face, one pixel wider per character, and the
; units of the dialog are 7 and 13;
; "Plain16" DS_3DLOOK makes it FW_NORMAL, base units 6 and 13.
;
; Each dialog carries one static text in its font. The program then runs its
; message loop until the host quits. tools/test_win16_dialog_font_host.py;
; the New Executable is made as for tools/layer_win16_fixture.asm.
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
        push word [instance]
        push ds
        push word bold
        push word 0
        push cs
        push word procedure
        API 2, 219                      ; CreateDialogIndirect
        push word [instance]
        push ds
        push word plain
        push word 0
        push cs
        push word procedure
        API 2, 219
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
; BOOL FAR PASCAL procedure(HWND, UINT, WPARAM, LPARAM): nothing handled.
procedure:
        xor ax, ax
        retf 10
code_end:

%macro TEMPLATE 3
        dd 0x90C000C0 | %1              ; WS_POPUP WS_VISIBLE WS_CAPTION DS_MODALFRAME DS_SETFONT
        db 1                            ; one control
        dw 10, %2, 200, 60              ; x, y, cx, cy in dialog units
        db 0                            ; no menu
        db 0                            ; the dialog class
        db %3, 0                        ; caption
        dw 8                            ; points
        db 'MS Sans Serif', 0
        dw 8, 8, 184, 40, 100           ; x, y, cx, cy, id
        dd 0x50000000                   ; WS_CHILD WS_VISIBLE SS_LEFT
        db 0x82                         ; STATIC
        db 'Wise Installation Wizard', 0
        db 0                            ; no creation data
%endmacro

section data
data_start:
        times 16 db 0                   ; the words of the local heap and the stack
instance dw 0
message times 32 db 0
bold:   TEMPLATE 0, 10, 'Bold16'
plain:  TEMPLATE 4, 120, 'Plain16'
        times 64 db 0
data_end:

section fix
fix_end:
