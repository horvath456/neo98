; SPDX-License-Identifier: GPL-2.0-or-later
; Static console text for the wheel test. No blinking cursor changes pixels
; between the before/after screenshots; the actual DOS text must stay put.
bits 16
org 100h
start:
    mov ax, 0003h
    int 10h
    mov dx, text
    mov ah, 09h
    int 21h
    mov ah, 01h
    mov cx, 2000h
    int 10h
.wait:
    xor ah, ah
    int 16h
    cmp al, 27
    jne .wait
    mov ax, 4c00h
    int 21h
text:
%rep 16
    db 'Wheel and trackpad must leave this DOS text in place.', 13, 10
%endrep
    db '$'
