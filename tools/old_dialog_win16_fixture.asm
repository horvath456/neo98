; SPDX-License-Identifier: GPL-2.0-or-later
; Calls of KERNEL and USER the old-style file dialog of a 16-bit COMMDLG
; makes, by a program of its own. Resource 1 of type RT_RCDATA holds
; "ORIGINAL".
;
; a LoadResource, LockResource, the first byte changed to 'X'; a second
;   LoadResource answers the same handle and the block still has the 'X'
; b FreeResource twice, the last load given back; the next LoadResource
;   has the bytes of the file again
; c INT 21h AH=32h of drive C: (DL 3): AL 0, DS:BX on a drive parameter
;   block of drive 2 with 512 bytes a sector and at least one FAT
; d INT 21h AH=32h of drive Y:, which is not there: AL FFh
;
; Each step puts its letter into C:\OLDDLG16.TXT as soon as it is done,
; "-" when it failed, so a step that stops the program leaves the letters
; before it. The program ends through DOS3Call 4Ch.
; tools/test_win16_old_dialog_host.py.
        bits 16
section header start=0
        dw code_end - code_start, data_end - data_start, (fix_end - fix_start) / 6
section code vstart=0 follows=header align=1
section data vstart=0 follows=code align=1
section fix vstart=0 follows=data align=1
fix_start:

%macro API 2
%%at:   db 0x9a, 0xff, 0xff, 0x00, 0x00
[section fix]
        dw %%at + 1 - code_start, %1, %2
__SECT__
%endmacro
%macro DOS 0
        API 1, 102                      ; DOS3Call
%endmacro
; MARK letter: AL is nonzero when the step held; writes the letter or '-'.
%macro MARK 1
        mov ah, %1
        call mark
%endmacro

section code
code_start:
        API 1, 91                       ; InitTask
        mov [instance], di
        push di
        API 2, 5                        ; InitApp
        push ds
        push word fout
        push word 0
        API 1, 83                       ; _lcreat
        mov [output], ax

        ; a: a loaded resource keeps what the program wrote while it is loaded
        push word [instance]
        push word 0
        push word 1                     ; MAKEINTRESOURCE(1)
        push word 0
        push word 10                    ; RT_RCDATA
        API 1, 60                       ; FindResource
        mov [found], ax
        push word [instance]
        push ax
        API 1, 61                       ; LoadResource
        mov [first], ax
        push ax
        API 1, 62                       ; LockResource
        mov es, dx
        mov bx, ax
        xor si, si
        cmp byte [es:bx], 'O'
        jne .a_second
        inc si
        mov byte [es:bx], 'X'
.a_second:
        push word [first]
        API 1, 19                       ; GlobalUnlock
        push word [instance]
        push word [found]
        API 1, 61                       ; LoadResource
        cmp ax, [first]
        je .a_same
        xor si, si
.a_same:
        push ax
        API 1, 62                       ; LockResource
        mov es, dx
        mov bx, ax
        cmp byte [es:bx], 'X'
        je .a_kept
        xor si, si
.a_kept:
        push word [first]
        API 1, 19                       ; GlobalUnlock
        mov ax, si
        MARK 'a'

        ; b: after the last FreeResource the bytes come from the file again
        push word [first]
        API 1, 63                       ; FreeResource
        push word [first]
        API 1, 63                       ; FreeResource, the last load
        push word [instance]
        push word [found]
        API 1, 61                       ; LoadResource
        mov [first], ax
        xor si, si
        or ax, ax
        jz .b_end
        push ax
        API 1, 62                       ; LockResource
        mov es, dx
        mov bx, ax
        cmp word [es:bx], 'OR'
        jne .b_unlock
        cmp word [es:bx + 6], 'AL'
        jne .b_unlock
        inc si
.b_unlock:
        push word [first]
        API 1, 19                       ; GlobalUnlock
        push word [first]
        API 1, 63                       ; FreeResource
.b_end:
        mov ax, si
        MARK 'b'

        ; c: the drive parameter block of C:
        push ds
        mov dl, 3
        mov ah, 32h
        DOS
        xor si, si
        or al, al
        jnz .c_end
        cmp byte [bx], 2                ; the drive, 0 for A
        jne .c_end
        cmp word [bx + 2], 512          ; bytes a sector
        jne .c_end
        cmp byte [bx + 8], 0            ; the number of FATs
        je .c_end
        inc si
.c_end: pop ds
        mov ax, si
        MARK 'c'

        ; d: a drive that is not there
        push ds
        mov dl, 25
        mov ah, 32h
        DOS
        pop ds
        cmp al, 0ffh
        sete al
        MARK 'd'

        push word [output]
        API 1, 81                       ; _lclose
        mov ax, 4c00h
        DOS
        jmp $

; mark: AH the letter, AL nonzero when the step held. One byte to the file.
mark:
        or al, al
        jnz .good
        mov ah, '-'
.good:  mov [letter], ah
        push word [output]
        push ds
        push word letter
        push word 1
        API 1, 86                       ; _lwrite
        ret
code_end:

section data
data_start:
        times 16 db 0                   ; the words of the local heap and the stack
instance dw 0
output   dw 0
found    dw 0
first    dw 0
letter   db 0, 0
fout     db 'C:\OLDDLG16.TXT', 0
data_end:

section fix
fix_end:
