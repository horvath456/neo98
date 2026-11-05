# A UART as a DOS program drives it, for tools/test_dos_uart_native.py: the
# 16550 of COM2 at 2F8h with IRQ 3, or with the symbol PORT1 set before this
# text the one of COM1 at 3F8h with IRQ 4, programmed directly (115200 8N1, FIFO on with trigger
# level 1, DTR, RTS and OUT2), received through the program's own interrupt
# handler into a ring, sent polled with LSR bit 5 before every byte. That is
# how the loaders of the reference system's adventures talk to its second screen
# (the far end is the test's).
#
#   send "AT"              expect a line "OK"                else end with 2
#   send "BLOB"            3000 bytes, byte i is 7 * i + 3   3 wrong byte, 4 too few
#   send "SINK", 2000 bytes (13 * i + 5), expect "GOOD"      else 5
#   MSR shows CTS, DSR and carrier                           else 6
#   every byte came through the interrupt handler            else 7
#   send "BYE", write the counts of the handler to D:\UART.BIN, end with 42
#
# Assembled with llvm-mc as a flat .COM: CS = DS = SS, loaded at offset 100h.
.code16
.intel_syntax noprefix
.text
.ifdef PORT1
.set BASE, 0x3f8
.set VECTOR, 0x0c          # IRQ 4
.set OPEN, 0xef
.else
.set BASE, 0x2f8
.set VECTOR, 0x0b          # IRQ 3
.set OPEN, 0xf7
.endif
.set RING, 0x4000          # 4096 bytes of the program's segment, past its code
start:
    mov ax, 0x3500 + VECTOR
    int 0x21
    mov [A_old], bx
    mov [A_old + 2], es
    mov dx, offset A_handler
    mov ax, 0x2500 + VECTOR
    int 0x21
    # divisor 1 behind DLAB, then 8N1
    mov dx, BASE + 3
    mov al, 0x80
    out dx, al
    mov dx, BASE
    mov al, 1
    out dx, al
    mov dx, BASE + 1
    xor al, al
    out dx, al
    mov dx, BASE + 3
    mov al, 3
    out dx, al
    # FIFO on, both cleared, an interrupt with the first byte
    mov dx, BASE + 2
    mov al, 0x07
    out dx, al
    # DTR, RTS, OUT2 (the interrupt line of the chip)
    mov dx, BASE + 4
    mov al, 0x0b
    out dx, al
    # what stands in the registers from before
    mov dx, BASE
    in al, dx
    mov dx, BASE + 5
    in al, dx
    mov dx, BASE + 6
    in al, dx
    # interrupt on received data, the line open at the controller
    mov dx, BASE + 1
    mov al, 1
    out dx, al
    in al, 0x21
    and al, OPEN
    out 0x21, al

    # the handshake
    mov si, offset A_at
    call puts
    call getc
    jc fail2
    cmp al, 'O'
    jne fail2
    call getc
    jc fail2
    cmp al, 'K'
    jne fail2
    call rest_of_line

    # 3000 bytes in one piece
    mov si, offset A_blob
    call puts
    mov cx, 3000
    mov bl, 3
blob:
    call getc
    jc fail4
    cmp al, bl
    jne fail3
    add bl, 7
    loop blob

    # 2000 bytes out
    mov si, offset A_sink
    call puts
    mov cx, 2000
    mov bl, 5
sink:
    mov al, bl
    call putc
    add bl, 13
    loop sink
    call getc
    jc fail5
    cmp al, 'G'
    jne fail5
    call rest_of_line

    # the input lines of a standing connection
    mov dx, BASE + 6
    in al, dx
    and al, 0xb0
    cmp al, 0xb0
    jne fail6
    # every byte read so far came through the handler
    cmp word ptr [A_bytes], 3000
    jb fail7

    mov si, offset A_bye
    call puts
empty:
    mov dx, BASE + 5
    in al, dx
    test al, 0x40
    jz empty
    mov dx, offset A_file
    xor cx, cx
    mov ah, 0x3c
    int 0x21
    jc done42
    mov bx, ax
    mov cx, 4
    mov dx, offset A_irqs
    mov ah, 0x40
    int 0x21
    mov ah, 0x3e
    int 0x21
done42:
    mov al, 42
    jmp finish
fail2:
    mov al, 2
    jmp finish
fail3:
    mov al, 3
    jmp finish
fail4:
    mov al, 4
    jmp finish
fail5:
    mov al, 5
    jmp finish
fail6:
    mov al, 6
    jmp finish
fail7:
    mov al, 7
finish:
    push ax
    # no more interrupts from the chip, then the vector back; the mask of the
    # controller stays as it is
    mov dx, BASE + 1
    xor al, al
    out dx, al
    mov dx, BASE + 4
    mov al, 3
    out dx, al
    push ds
    lds dx, [A_old]
    mov ax, 0x2500 + VECTOR
    int 0x21
    pop ds
    pop ax
    mov ah, 0x4c
    int 0x21

# AL to the line: wait for an empty holding register, then write.
putc:
    push dx
    push ax
putc_wait:
    mov dx, BASE + 5
    in al, dx
    test al, 0x20
    jz putc_wait
    pop ax
    mov dx, BASE
    out dx, al
    pop dx
    ret

# The zero-terminated text at SI.
puts:
    lodsb
    test al, al
    jz puts_done
    call putc
    jmp puts
puts_done:
    ret

# The next byte of the ring in AL; carry when none came for three seconds
# (55 ticks of the BIOS clock).
getc:
    push bx
    push dx
    push es
    xor ax, ax
    mov es, ax
    mov dx, es:[0x46c]
getc_wait:
    cli
    mov bx, [A_tail]
    cmp bx, [A_head]
    jne getc_have
    sti
    hlt
    mov ax, es:[0x46c]
    sub ax, dx
    cmp ax, 55
    jb getc_wait
    stc
    jmp getc_out
getc_have:
    sti
    mov al, [RING + bx]
    inc bx
    and bx, 0x0fff
    mov [A_tail], bx
    clc
getc_out:
    pop es
    pop dx
    pop bx
    ret

rest_of_line:
    call getc
    jc line_done
    cmp al, 10
    jne rest_of_line
line_done:
    ret

# The interrupt: every byte the chip holds into the ring, then the end of interrupt.
handler:
    push ax
    push bx
    push dx
next:
    mov dx, BASE + 5
    in al, dx
    test al, 1
    jz served
    mov dx, BASE
    in al, dx
    mov bx, cs:[A_head]
    mov cs:[RING + bx], al
    inc bx
    and bx, 0x0fff
    mov cs:[A_head], bx
    inc word ptr cs:[A_bytes]
    jmp next
served:
    inc word ptr cs:[A_irqs]
    mov al, 0x20
    out 0x20, al
    pop dx
    pop bx
    pop ax
    iret

old: .word 0, 0
head: .word 0
tail: .word 0
irqs: .word 0
bytes: .word 0
at: .asciz "AT\r\n"
blob_text: .asciz "BLOB\r\n"
sink_text: .asciz "SINK\r\n"
bye: .asciz "BYE\r\n"
.ifdef PORT1
file: .asciz "D:\\UART1.BIN"
.else
file: .asciz "D:\\UART2.BIN"
.endif

.set A_old, old - start + 0x100
.set A_handler, handler - start + 0x100
.set A_head, head - start + 0x100
.set A_tail, tail - start + 0x100
.set A_irqs, irqs - start + 0x100
.set A_bytes, bytes - start + 0x100
.set A_at, at - start + 0x100
.set A_blob, blob_text - start + 0x100
.set A_sink, sink_text - start + 0x100
.set A_bye, bye - start + 0x100
.set A_file, file - start + 0x100
