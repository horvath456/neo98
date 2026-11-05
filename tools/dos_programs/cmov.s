# The instructions of the Pentium Pro for tools/test_dos_cmov.py: CMOVcc with
# 16 and 32 bit registers and memory operands, with the condition true and
# false; FCOMI, FCOMIP, FUCOMIP and FCMOVcc of the x87; and one addition in
# MMX. The program ends with the number of the first wrong step, or with 42
# plus what CPUID reports: 1 for CMOV (bit 15 of EDX), 2 for MMX (bit 23).
# Assembled with llvm-mc as a flat .COM.
.code16
.intel_syntax noprefix
.text
start:
    push cs
    pop ds
    call base
base:
    pop si
    .att_syntax
    addw $(data - base), %si
    .intel_syntax noprefix
    # 1: CMOVZ with ZF set, 16 bit registers
    mov bl, 1
    mov ax, 1
    mov cx, 0x1234
    xor dx, dx
    cmovz ax, cx
    cmp ax, 0x1234
    jne fail
    # 2: CMOVNZ with ZF set moves nothing
    inc bl
    mov ax, 5
    xor dx, dx
    cmovnz ax, cx
    cmp ax, 5
    jne fail
    # 3: CMOVC with CF set, 32 bit registers
    inc bl
    mov eax, 0x11111111
    mov ecx, 0x89abcdef
    stc
    cmovc eax, ecx
    cmp eax, 0x89abcdef
    jne fail
    # 4: CMOVC with CF clear keeps all 32 bits
    inc bl
    mov eax, 0x11111111
    clc
    cmovc eax, ecx
    cmp eax, 0x11111111
    jne fail
    # 5: CMOVL from memory, 16 bit
    inc bl
    xor ax, ax
    mov dx, 1
    cmp dx, 2
    cmovl ax, [si]
    cmp ax, 0x5678
    jne fail
    # 6: CMOVGE from memory with the condition false, then CMOVNGE, 32 bit
    inc bl
    mov eax, 7
    mov dx, 1
    cmp dx, 2
    cmovge eax, [si + 2]
    cmp eax, 7
    jne fail
    mov dx, 1
    cmp dx, 2
    cmovl eax, [si + 2]
    cmp eax, 0xcafe1234
    jne fail
    # 7: CMOVA and CMOVBE after 1 against 2
    inc bl
    xor ax, ax
    mov dx, 0x7777
    mov cx, 1
    cmp cx, 2
    cmova ax, dx
    cmovbe bp, dx
    cmp ax, 0
    jne fail
    cmp bp, 0x7777
    jne fail
    # 8: CMOVO and CMOVS after 7Fh + 1, CMOVNO and CMOVNS move nothing
    inc bl
    xor cx, cx
    xor di, di
    mov bp, 3
    mov dx, 9
    mov al, 0x7f
    add al, 1
    cmovo cx, dx
    cmovs di, dx
    cmovno bp, dx
    cmovns bp, dx
    cmp cx, 9
    jne fail
    cmp di, 9
    jne fail
    cmp bp, 3
    jne fail
    # 9: CMOVP after a result with even parity, CMOVNP moves nothing
    inc bl
    xor cx, cx
    mov bp, 3
    xor ax, ax
    cmovp cx, dx
    cmovnp bp, dx
    cmp cx, 9
    jne fail
    cmp bp, 3
    jne fail
    # 10: FCOMI of 0 against 1: below, so CF set and ZF clear
    inc bl
    finit
    fld1
    fldz
    fcomi st, st(1)
    jnc fail
    jz fail
    # 11: FCMOVB with CF set makes ST(0) one; FCOMIP against one is equal
    inc bl
    fcmovb st, st(1)
    fld1
    fcomip st, st(1)
    jnz fail
    jc fail
    # 12: FCMOVNE with ZF set moves nothing: ST(0) stays zero, below one
    inc bl
    fldz
    xor ax, ax
    fcmovne st, st(1)
    fcomi st, st(1)
    jnc fail
    # 13: FCMOVNBE with CF set moves nothing, FCMOVBE moves; FUCOMIP is equal
    inc bl
    fcmovnbe st, st(1)
    fcomi st, st(1)
    jnc fail
    fcmovbe st, st(1)
    fucomip st, st(1)
    jnz fail
    finit
    # 14: MMX: PADDB of 01020304h and 10203040h
    inc bl
    mov eax, 0x01020304
    movd mm0, eax
    mov eax, 0x10203040
    movd mm1, eax
    paddb mm0, mm1
    movd ecx, mm0
    emms
    cmp ecx, 0x11223344
    jne fail
    # CPUID 1: bit 15 of EDX is CMOV, bit 23 MMX
    mov eax, 1
    cpuid
    mov bl, 42
    test edx, 0x8000
    jz nocmov
    inc bl
nocmov:
    test edx, 0x800000
    jz fail
    add bl, 2
fail:
    mov al, bl
    mov ah, 0x4c
    int 0x21
data:
    .word 0x5678
    .long 0xcafe1234
