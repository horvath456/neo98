# The keyboard as a game sees it, for tools/test_dos_vm_native.py:
# an INT 9 handler reads every byte of port 60h into a buffer and chains to
# the BIOS handler, the way games with their own keyboard handler do (Mario &
# Luigi, Turbo Pascal). The program prints its line, then waits until the
# make code of Esc (01h) arrives or 8 seconds have passed, writes the bytes it saw to D:\KEYS.BIN and
# ends with 1, or with 0 when Esc never came. Assembled with llvm-mc as a flat .COM.
.code16
.intel_syntax noprefix
.text
start:
    # a .COM: CS = DS = SS, loaded at offset 100h
    # old INT 9 vector into the program
    mov ax, 0x3509
    int 0x21
    mov [A_old9], bx
    mov [A_old9 + 2], es
    # the new handler
    mov dx, offset A_handler
    mov ax, 0x2509
    int 0x21
    # the line
    mov dx, offset A_msg
    mov ah, 9
    int 0x21
    # the BIOS tick count at the start: the program ends 8 seconds later
    # (146 ticks) at the latest
    push ds
    xor ax, ax
    mov ds, ax
    mov si, [0x46c]
    pop ds
wait_esc:
    # the handler sets done when the make code of Esc arrives
    hlt
    cmp byte ptr [A_done], 0
    jne ended
    push ds
    xor ax, ax
    mov ds, ax
    mov ax, [0x46c]
    pop ds
    sub ax, si
    cmp ax, 146
    jb wait_esc
ended:
    # restore INT 9
    push ds
    lds dx, [A_old9]
    mov ax, 0x2509
    int 0x21
    pop ds
    # write the bytes to D:\KEYS.BIN
    mov dx, offset A_file
    xor cx, cx
    mov ah, 0x3c
    int 0x21
    jc quit
    mov bx, ax
    mov cx, [A_count]
    mov dx, offset A_buffer
    mov ah, 0x40
    int 0x21
    mov ah, 0x3e
    int 0x21
quit:
    mov al, [A_done]
    mov ah, 0x4c
    int 0x21

# INT 9: the byte of port 60h into the buffer, then the BIOS handler.
handler:
    push ax
    push bx
    in al, 0x60
    mov bx, cs:[A_count]
    cmp bx, 200
    jae full
    mov cs:[A_buffer + bx], al
    inc word ptr cs:[A_count]
full:
    cmp al, 1
    jne not_esc
    mov byte ptr cs:[A_done], 1
not_esc:
    pop bx
    pop ax
    # JMP FAR CS:[old9] (FF /5 with a 16:16 pointer; llvm-mc's "jmp dword
    # ptr" would be a near jump with a 32-bit operand)
    .byte 0x2e, 0xff, 0x2e
    .word A_old9

old9: .word 0, 0
count: .word 0
done: .byte 0
file: .asciz "D:\\KEYS.BIN"
msg: .ascii "keyboard test: press keys, Esc ends$"
buffer: .space 200

.set A_old9, old9 - start + 0x100
.set A_handler, handler - start + 0x100
.set A_msg, msg - start + 0x100
.set A_done, done - start + 0x100
.set A_file, file - start + 0x100
.set A_count, count - start + 0x100
.set A_buffer, buffer - start + 0x100
