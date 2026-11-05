; SPDX-License-Identifier: GPL-2.0-or-later
; DOS keyboard waits of tools/test_savestates_dos_host.py. READ_MODE selects
; BIOS INT 16h, DOS single-character input, a DOS line, or a CON file read.
; RESULT.TXT contains what the suspended call finally returned. A second
; BIOS wait keeps the program alive while the host loads an earlier state.
        org 100h
%ifndef READ_MODE
%define READ_MODE 0
%endif
%if READ_MODE == 8
        xor ah, ah
        int 16h                 ; intro needs a real key before line input
%endif
%if READ_MODE == 4
        mov ax, 3516h
        int 21h
        mov [old_int16], bx
        mov [old_int16 + 2], es
        mov ax, 2516h
        mov dx, keyboard_handler
        int 21h
%endif
%if READ_MODE == 6 || READ_MODE == 7
        mov ax, 4f02h
        mov bx, 101h
        int 10h
retrace:
%if READ_MODE == 6
        mov ax, 4f09h
        mov bx, 0080h
        mov cx, 1
        xor dx, dx
        mov di, palette
        push cs
        pop es
%else
        mov ax, 4f07h
        mov bx, 0080h
        xor cx, cx
        xor dx, dx
%endif
        int 10h
        mov ah, 1
        int 16h
        jz retrace
        xor ah, ah
        int 16h
        mov [buffer + 2], al
        mov byte [buffer + 1], 1
%elif READ_MODE == 5
        mov ah, 86h
        mov cx, 07ah
        mov dx, 1200h            ; 8,000,000 microseconds
        int 15h
        mov byte [buffer + 2], 'w'
        mov byte [buffer + 1], 1
%elif READ_MODE == 0
        xor ah, ah
        int 16h
        mov [buffer + 2], al
        mov byte [buffer + 1], 1
%elif READ_MODE == 1 || READ_MODE == 4
        mov ah, 08h
        int 21h
        mov [buffer + 2], al
        mov byte [buffer + 1], 1
%elif READ_MODE == 2 || READ_MODE == 8
        mov ah, 0ah
        mov dx, buffer
        int 21h
%elif READ_MODE == 3
        mov ax, 3d00h
        mov dx, console
        int 21h
        mov bx, ax
        mov ah, 3fh
        mov cx, 30
        mov dx, buffer + 2
        int 21h
        sub al, 2               ; CON returns CR/LF at the end of the line.
        mov [buffer + 1], al
        mov ah, 3eh
        int 21h
%endif
        mov ah, 3ch
        xor cx, cx
        mov dx, filename
        int 21h
        mov bx, ax
        mov ah, 40h
        xor cx, cx
        mov cl, [buffer + 1]
        mov dx, buffer + 2
        int 21h
        mov ah, 3eh
        int 21h
        xor ah, ah
        int 16h
        mov ax, 4c00h
        int 21h
buffer  db 32, 0
        times 32 db 0
filename db 'RESULT.TXT', 0
console db 'CON', 0

%if READ_MODE == 4
; An INT 16h hook uses DOS character input itself. Its nested BIOS read
; chains to the original vector, leaving two CON.Read native calls open.
keyboard_handler:
        cmp byte [cs:in_hook], 0
        jne .chain
        cmp ah, 0
        je .read
        cmp ah, 10h
        jne .chain
.read:  mov byte [cs:in_hook], 1
        mov ah, 08h
        int 21h
        xor ah, ah
        mov byte [cs:in_hook], 0
        iret
.chain: jmp far [cs:old_int16]
in_hook db 0
old_int16 dw 0, 0
%endif

palette db 24, 48, 96, 0
