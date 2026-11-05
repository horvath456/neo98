# Sound Blaster digital playback for tools/test_dos_vm_native.py: reset the
# DSP at 220h, a 4 KB square wave (8 bit unsigned) in a 64 KB aligned segment,
# DMA channel 1 in auto-init mode, time constant 166 (11025 Hz), DSP command
# 48h for the block size and 1Ch for 8-bit auto-init output; the program
# prints its line, waits for a key, stops the DMA (DAh), turns the speaker
# off and ends with 0. Assembled with llvm-mc as a flat .COM.
.code16
.intel_syntax noprefix
.text
start:
    # DSP reset: 1 then 0 to 226h, wait for data ready and AAh at 22Ah
    mov dx, 0x226
    mov al, 1
    out dx, al
    mov cx, 100
d1: in al, dx
    loop d1
    xor al, al
    out dx, al
    mov cx, 0xffff
w1: mov dx, 0x22e
    in al, dx
    test al, 0x80
    jnz r1
    loop w1
r1: mov dx, 0x22a
    in al, dx
    # a 64 KB aligned segment above the program for the DMA buffer
    mov ax, cs
    add ax, 0x1000
    and ax, 0xf000
    mov es, ax
    mov bx, ax
    xor di, di
    # 4096 bytes of square wave, 8 bit unsigned: 32 high, 32 low
    mov cx, 64
f1: push cx
    mov al, 0xe0
    mov cx, 32
    rep stosb
    mov al, 0x20
    mov cx, 32
    rep stosb
    pop cx
    loop f1
    # speaker on, time constant 166 (11025 Hz)
    mov al, 0xd1
    call dsp
    mov al, 0x40
    call dsp
    mov al, 166
    call dsp
    # DMA channel 1: mask, clear flip-flop, auto-init read, address, page, count
    mov al, 5
    out 0x0a, al
    xor al, al
    out 0x0c, al
    mov al, 0x59
    out 0x0b, al
    xor al, al
    out 0x02, al
    out 0x02, al
    mov al, bh
    shr al, 4
    out 0x83, al
    mov al, 0xff
    out 0x03, al
    mov al, 0x0f
    out 0x03, al
    mov al, 1
    out 0x0a, al
    # block size 4096 - 1, then 8-bit auto-init output
    mov al, 0x48
    call dsp
    mov al, 0xff
    call dsp
    mov al, 0x0f
    call dsp
    mov al, 0x1c
    call dsp
    # the text, the key
    push cs
    pop ds
    call here
here:
    pop dx
    .att_syntax
    addw $(msg - here), %dx
    .intel_syntax noprefix
    mov ah, 9
    int 0x21
    xor ah, ah
    int 0x16
    # exit auto-init, speaker off
    mov al, 0xda
    call dsp
    mov al, 0xd3
    call dsp
    mov ax, 0x4c00
    int 0x21
dsp:
    push ax
    mov dx, 0x22c
w2: in al, dx
    test al, 0x80
    jnz w2
    pop ax
    out dx, al
    ret
msg: .ascii "sound blaster$"
