# Where the DOS mouse driver sees the pointer, for tools/test_dos_vm_native.py
# VGA mode 13h, a white band over the first 20 rows so the picture
# is seen, INT 33h reset and show; after a key INT 33h AX=3 gives the place
# (CX 0 to 639 on the 320 pixels of mode 13h, DX 0 to 199), which goes to
# D:\MOUSE.BIN as two words; back to text mode, end with 0. Assembled with
# llvm-mc as a flat .COM.
.code16
.intel_syntax noprefix
.text
start:
    mov ax, 0x13
    int 0x10
    push es
    mov ax, 0xa000
    mov es, ax
    xor di, di
    mov cx, 320 * 20
    mov al, 15
    rep stosb
    pop es
    xor ax, ax
    int 0x33
    mov ax, 1
    int 0x33
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
file: .asciz "D:\\MOUSE.BIN"
.set A_place, place - start + 0x100
.set A_file, file - start + 0x100
