; SPDX-License-Identifier: GPL-2.0-or-later
; A 16-bit message box sounds the event of its icon, as a 32-bit one does:
; USER.1 MessageBox(0, text, "BoxTest", MB_ICONEXCLAMATION), then USER.827
; MessageBoxIndirect with MB_ICONHAND, then the end of the task.
; tools/test_event_sounds_host.py presses Enter in each box; the New
; Executable is made as for tools/layer_win16_fixture.asm.
        bits 16
section header start=0
        dw code_end - code_start, data_end - data_start, (fix_end - fix_start) / 6
section code vstart=0 follows=header align=1
section data vstart=0 follows=code align=1
section fix vstart=0 follows=data align=1
fix_start:

; A far call the loader fills in: module 1 is KERNEL, 2 is USER.
%macro API 2
%%at:   db 0x9a, 0xff, 0xff, 0x00, 0x00
[section fix]
        dw %%at + 1 - code_start, %1, %2
__SECT__
%endmacro

section code
code_start:
        API 1, 91                       ; InitTask
        push di
        API 2, 5                        ; InitApp
        push word 0                     ; no owner
        push ds
        push word text
        push ds
        push word caption
        push word 30h                   ; MB_ICONEXCLAMATION
        API 2, 1                        ; MessageBox
        mov [params + 10], ds           ; the segments of the two strings
        mov [params + 14], ds
        push ds
        push word params
        API 2, 827                      ; MessageBoxIndirect
        mov ax, 4c00h
        API 1, 102                      ; DOS3Call
        jmp $
code_end:

section data
data_start:
        times 16 db 0                   ; the words of the local heap and the stack
text    db 'A box of a 16-bit program', 0
caption db 'BoxTest', 0
other   db 'MessageBoxIndirect of a 16-bit program', 0
; MSGBOXPARAMS: cbSize, hwndOwner, hInstance, lpszText, lpszCaption,
; dwStyle (MB_ICONHAND), lpszIcon, dwContextHelpId, lpfnMsgBoxCallback,
; dwLanguageId.
params  dd 36
        dw 0, 0
        dw other, 0
        dw caption, 0
        dd 10h
        dd 0, 0, 0, 0
        times 512 db 0
data_end:

section fix
fix_end:
