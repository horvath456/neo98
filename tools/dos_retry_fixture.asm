; SPDX-License-Identifier: GPL-2.0-or-later
; First launch exits with an error; the retry succeeds.
bits 16
org 0x100
    mov dx, marker
    mov ax, 0x3d00
    int 0x21
    jnc success
    xor cx, cx
    mov ah, 0x3c
    int 0x21
    jc failure
    mov bx, ax
    mov ah, 0x3e
    int 0x21
failure:
    mov ax, 0x4c01
    int 0x21
success:
    mov bx, ax
    mov ah, 0x3e
    int 0x21
    mov ax, 0x4c00
    int 0x21
marker: db 'RETRY.OK', 0
