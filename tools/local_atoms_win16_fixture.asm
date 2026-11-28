; SPDX-License-Identifier: GPL-2.0-or-later
; The local atoms of KERNEL live in the program's own data segment, as
; KRNL386 keeps them: the word at DS:8 points to the table (a word of the
; number of buckets, then the first entry of each), an entry is a fixed block
; of the local heap {WORD next, WORD usage, BYTE length, name}, and a string
; atom is the near pointer of its entry shifted right by two, ORed with
; C000h. Programs read the name there themselves: SETUPX keeps the file names
; of its copy queue as local atoms and copies them out of its segment through
; LocalLock(atom << 2) + 5. With the atoms in a table outside the segment
; every name SETUPX read was empty.
;
; a AddAtom without a table makes one of 37 buckets at DS:8; InitAtomTable
;   then answers that table
; b the atom is C000h or more, GetAtomHandle is the atom shifted left by
;   two, and there are the length, a usage of 1 and the name as it was given
; c AddAtom of the name in other letters answers the same atom, usage 2
; d FindAtom in other letters answers it too
; e GetAtomName gives the name as it was given and its length; with five
;   bytes the first four characters
; f DeleteAtom answers zero twice, the second takes the entry away and
;   FindAtom then answers zero
; g integer atoms: MAKEINTATOM(123) and "#456" answer 123 and 456 without
;   an entry, GetAtomName(456) is "#456"
; h two more names have two atoms, each name at its own entry
;
; Each step puts its letter into C:\ATOM16.TXT, "-" when it failed. The
; program ends through DOS3Call 4Ch. tools/test_win16_local_atoms_host.py.
        bits 16
section header start=0
        dw code_end - code_start, data_end - data_start, (fix_end - fix_start) / 6
section code vstart=0 follows=header align=1
section data vstart=0 follows=code align=1
section fix vstart=0 follows=data align=1
fix_start:

; A far call the loader fills in: module 1 is KERNEL, 2 is USER.
%macro API 2
%%at:   db 0x9a, 0xff, 0xff, 0x00, 0x00
[section fix]
        dw %%at + 1 - code_start, %1, %2
__SECT__
%endmacro
%define InitAtomTable 68
%define FindAtom 69
%define AddAtom 70
%define DeleteAtom 71
%define GetAtomName 72
%define GetAtomHandle 73

; AL is the letter of the step when ZF is set, "-" otherwise; into the file.
%macro RESULT 1
        mov al, %1
        je %%good
        mov al, '-'
%%good: call put
%endmacro

; ZF set when the entry at SI holds the name at %1, %2 characters long.
%macro ENTRY 2
        cmp byte [si + 4], %2
        jne %%end
        push ds
        pop es
        lea si, [si + 5]
        mov di, %1
        mov cx, %2 + 1
        cld
        repe cmpsb
%%end:
%endmacro

section code
code_start:
        API 1, 91                       ; InitTask
        push di
        API 2, 5                        ; InitApp

        ; a
        push ds
        push word name1
        API 1, AddAtom
        mov [atom], ax
        mov bx, [8]
        or bx, bx
        jz a_fail
        cmp word [bx], 37
        jne a_end
        push word 10
        API 1, InitAtomTable
        cmp ax, [8]
        jmp a_end
a_fail: or sp, sp                       ; no table: ZF clear
a_end:  RESULT 'a'

        ; b
        push word [atom]
        API 1, GetAtomHandle
        mov si, ax
        mov ax, [atom]
        cmp ax, 0c000h
        jb b_fail
        shl ax, 2
        cmp ax, si
        jne b_end
        cmp word [si + 2], 1
        jne b_end
        ENTRY name1, 12
        jmp b_end
b_fail: or sp, sp
b_end:  RESULT 'b'

        ; c
        push ds
        push word name2
        API 1, AddAtom
        cmp ax, [atom]
        jne c_end
        mov si, ax
        shl si, 2
        cmp word [si + 2], 2
c_end:  RESULT 'c'

        ; d
        push ds
        push word name3
        API 1, FindAtom
        cmp ax, [atom]
        RESULT 'd'

        ; e
        push word [atom]
        push ds
        push word buffer
        push word 64
        API 1, GetAtomName
        cmp ax, 12
        jne e_end
        push ds
        pop es
        mov si, buffer
        mov di, name1
        mov cx, 13
        cld
        repe cmpsb
        jne e_end
        push word [atom]
        push ds
        push word buffer
        push word 5
        API 1, GetAtomName
        cmp ax, 4
        jne e_end
        push ds
        pop es
        mov si, buffer
        mov di, kern
        mov cx, 5
        cld
        repe cmpsb
e_end:  RESULT 'e'

        ; f
        push word [atom]
        API 1, DeleteAtom
        or ax, ax
        jne f_end
        mov si, [atom]
        shl si, 2
        cmp word [si + 2], 1
        jne f_end
        push word [atom]
        API 1, DeleteAtom
        or ax, ax
        jne f_end
        push ds
        push word name1
        API 1, FindAtom
        or ax, ax
f_end:  RESULT 'f'

        ; g
        push word 0
        push word 123
        API 1, AddAtom
        cmp ax, 123
        jne g_end
        push ds
        push word hash456
        API 1, AddAtom
        cmp ax, 456
        jne g_end
        push word 456
        push ds
        push word buffer
        push word 64
        API 1, GetAtomName
        cmp ax, 4
        jne g_end
        push ds
        pop es
        mov si, buffer
        mov di, hash456
        mov cx, 5
        cld
        repe cmpsb
g_end:  RESULT 'g'

        ; h
        push ds
        push word name4
        API 1, AddAtom
        mov [atom4], ax
        push ds
        push word name5
        API 1, AddAtom
        mov [atom5], ax
        cmp ax, [atom4]
        je h_fail
        cmp ax, 0c000h
        jb h_fail
        mov si, [atom4]
        shl si, 2
        ENTRY name4, 10
        jne h_end
        mov si, [atom5]
        shl si, 2
        ENTRY name5, 10
        jmp h_end
h_fail: or sp, sp
h_end:  RESULT 'h'

        push ds
        push word fout
        push word 0
        API 1, 83                       ; _lcreat
        mov si, ax
        push si
        push ds
        push word outbuf
        mov ax, [pos]
        sub ax, outbuf
        push ax
        API 1, 86                       ; _lwrite
        push si
        API 1, 81                       ; _lclose
        mov ax, 4c00h
        API 1, 102                      ; DOS3Call
        jmp $

put:    mov bx, [pos]
        mov [bx], al
        inc word [pos]
        ret
code_end:

section data
data_start:
        times 16 db 0                   ; the words of the local heap and the stack
name1   db 'Kernel32.dll', 0
name2   db 'KERNEL32.DLL', 0
name3   db 'kernel32.DLL', 0
kern    db 'Kern', 0
hash456 db '#456', 0
name4   db 'MSNP32.DLL', 0
name5   db 'vredir.vxd', 0
fout    db 'C:\ATOM16.TXT', 0
atom    dw 0
atom4   dw 0
atom5   dw 0
pos     dw outbuf
outbuf  times 16 db 0
buffer  times 64 db 0
data_end:

section fix
fix_end:
