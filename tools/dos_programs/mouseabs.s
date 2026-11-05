# SPDX-License-Identifier: GPL-2.0-or-later
# Explicit ranges used by Allegro INT33 Voodoo games must override VGA text dimensions.
.code16
.intel_syntax noprefix
.text
start:
    xor ax, ax
    int 0x33
    mov ax, 7
    xor cx, cx
    mov dx, 5112
    int 0x33
    mov ax, 8
    xor cx, cx
    mov dx, 3832
    int 0x33
    mov dx, offset A_message
    mov ah, 9
    int 0x21
    xor ah, ah
    int 0x16
    mov ax, 3
    int 0x33
    mov [A_place], cx
    mov [A_place + 2], dx
    mov dx, offset A_file
    xor cx, cx
    mov ah, 0x3c
    int 0x21
    jc quit
    mov bx, ax
    mov cx, 4
    mov dx, offset A_place
    mov ah, 0x40
    int 0x21
    mov ah, 0x3e
    int 0x21
quit:
    mov ax, 3
    int 0x10
    mov ax, 0x4c00
    int 0x21
place: .word 0, 0
file: .asciz "D:\\MOUSEABS.BIN"
.set A_place, place - start + 0x100
.set A_file, file - start + 0x100

message: .ascii "Explicit INT33 range in VGA text mode$"
.set A_message, message - start + 0x100
