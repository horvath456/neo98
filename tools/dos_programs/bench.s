# BENCH.COM, a fixed DOS workload for the speed of the DOS machine
# (tools/bench_dos_vm.py). It reads the BIOS tick
# count (INT 1Ah, 18.2 per second of machine time), runs a number of rounds,
# reads the ticks again, prints "TICKS=........ SUM=........" (hexadecimal)
# on the screen, writes the same line to BENCH.TXT in the current directory
# and ends with the low byte of the checksum as its exit code. One round is
# a sieve of Eratosthenes over a 64 KB segment (a byte per number, primes
# below 65536 counted into the checksum) and 20000 steps of 32-bit integer
# arithmetic (multiply, add, rotate, xor, divide). The number of rounds is
# the decimal argument of the command line, 100 without one. The checksum
# is the same for every core and speed, so a run that ends with another one
# has computed something else. Assembled with llvm-mc as a flat .COM.
.code16
.intel_syntax noprefix
.text
start:
    .att_syntax
    callw here
    .intel_syntax noprefix
here:
    pop bp
    cld
    # the rounds: a decimal number in the command tail at 81h
    mov si, 0x81
    xor bx, bx
    xor cx, cx
digits:
    mov al, [si]
    inc si
    cmp al, ' '
    je digits_next
    cmp al, '0'
    jb digits_done
    cmp al, '9'
    ja digits_done
    sub al, '0'
    mov cl, al
    mov ax, bx
    mov dx, 10
    mul dx
    add ax, cx
    mov bx, ax
    jmp digits
digits_next:
    test bx, bx
    jz digits
digits_done:
    test bx, bx
    jnz rounds_set
    mov bx, 100
rounds_set:
    mov [bp + O_ROUNDS], bx
    # ticks at the start
    xor ah, ah
    int 0x1a
    mov [bp + O_T0], dx
    mov [bp + O_T0 + 2], cx
    xor esi, esi                 # the checksum
    mov ebx, 12345               # the arithmetic state
round:
    # the sieve: 64 KB of ones in the segment 64 KB above the program
    mov ax, cs
    add ax, 0x1000
    mov es, ax
    xor di, di
    mov cx, 0x8000
    mov ax, 0x0101
    rep stosw
    mov byte ptr es:[0], 0
    mov byte ptr es:[1], 0
    mov cx, 2
sieve_outer:
    mov di, cx
    cmp byte ptr es:[di], 0
    je sieve_next
    mov ax, cx
    mul cx                       # dx:ax = i*i, below 65536 for i < 256
    mov di, ax
sieve_inner:
    mov byte ptr es:[di], 0
    add di, cx
    jnc sieve_inner
sieve_next:
    inc cx
    cmp cx, 256
    jb sieve_outer
    # count the primes
    xor di, di
    xor edx, edx
count:
    movzx eax, byte ptr es:[di]
    add edx, eax
    inc di
    jnz count
    add esi, edx
    # the arithmetic: 20000 steps of a 32-bit mix
    mov cx, 20000
mix:
    imul ebx, ebx, 1103515245
    add ebx, 12345
    mov eax, ebx
    rol eax, 7
    xor eax, esi
    xor edx, edx
    mov edi, 1000003
    div edi
    add esi, edx
    loop mix
    dec word ptr [bp + O_ROUNDS]
    jnz round
    # ticks at the end, across midnight not handled
    xor ah, ah
    int 0x1a
    mov ax, dx
    mov dx, cx
    sub ax, [bp + O_T0]
    sbb dx, [bp + O_T0 + 2]
    # the line: TICKS=hhhhhhhh SUM=hhhhhhhh CR LF $
    push cs
    pop es
    lea di, [bp + O_LINE_TICKS]
    push ax
    mov ax, dx
    call hex16
    pop ax
    call hex16
    lea di, [bp + O_LINE_SUM]
    mov eax, esi
    ror eax, 16
    call hex16
    ror eax, 16
    call hex16
    lea dx, [bp + O_LINE]
    mov ah, 9
    int 0x21
    # BENCH.TXT: create, write the line without the $, close
    lea dx, [bp + O_NAME]
    mov ah, 0x3c
    xor cx, cx
    int 0x21
    jc done
    mov bx, ax
    lea dx, [bp + O_LINE]
    mov cx, offset LINE_BYTES
    mov ah, 0x40
    int 0x21
    mov ah, 0x3e
    int 0x21
done:
    mov ax, si
    mov ah, 0x4c
    int 0x21
# AX as four hexadecimal digits at ES:DI
hex16:
    push cx
    mov cx, 4
hex_digit:
    rol ax, 4
    push ax
    and al, 15
    add al, '0'
    cmp al, '9'
    jbe hex_store
    add al, 7
hex_store:
    stosb
    pop ax
    loop hex_digit
    pop cx
    ret
rounds: .word 0
t0: .long 0
line: .ascii "TICKS="
line_ticks: .ascii "00000000 SUM="
line_sum: .ascii "00000000\r\n"
line_end: .ascii "$"
name: .asciz "BENCH.TXT"
# offsets from `here`, the address BP holds
.set O_ROUNDS, rounds - here
.set O_T0, t0 - here
.set O_LINE_TICKS, line_ticks - here
.set O_LINE_SUM, line_sum - here
.set O_LINE, line - here
.set O_NAME, name - here
.set LINE_BYTES, line_end - line
