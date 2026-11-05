; SPDX-License-Identifier: GPL-2.0-or-later
; The Win16 guest of tools/test_game_machine_files_host.py makes file calls
; on C:\GMBASE, where the normal machine supplies the initial files.
;
; a _lcreat W16NEW.TXT, _lwrite, _lclose (KERNEL.83, 86, 81)
; b _lopen W16.TXT of the drive below, _llseek to its
; end, _lwrite (KERNEL.85, 84)
; c delete W16DEL.TXT of the drive below (DOS3Call 41h)
; C _lopen of it fails afterwards
; d rename W16REN.TXT to W16MOVED.TXT (DOS3Call 56h)
; r _lopen W16MOVED.TXT and _lread its first byte, "r" (KERNEL.82)
;
; Each step puts its letter into C:\GMBASE\W16OUT.TXT, "-" when it failed;
; then follows every name the search of DOS3Call 4Eh/4Fh reports for
; C:\GMBASE\*.*, one a line. The program ends through DOS3Call 4Ch.
;
; No 16-bit compiler and no linker: nasm writes three flat parts behind a
; header of their sizes, and tools/win16_fixture.py makes the New Executable
; of them (code segment, automatic data segment, relocation records of the
; far calls: offset of the pointer, module, ordinal).
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
%macro DOS 0
        API 1, 102                      ; DOS3Call
%endmacro
; The letter, or "-" when the carry is set. DI is kept by every API call.
%macro STEP 1
        mov al, %1
        jnc %%good
        mov al, '-'
%%good: stosb
%endmacro
; The carry from "AX is HFILE_ERROR".
%macro FAILED 0
        cmp ax, 0xffff
        clc
        jne %%okay
        stc
%%okay:
%endmacro

section code
code_start:
        API 1, 91                       ; InitTask
        push di                         ; the instance
        API 2, 5                        ; InitApp
        push ds
        pop es
        cld
        mov di, outbuf
        ; a
        push ds
        push word fnew
        push word 0
        API 1, 83                       ; _lcreat
        FAILED
        jc a_end
        mov si, ax
        push si
        push ds
        push word tnew
        push word 7
        API 1, 86                       ; _lwrite
        push ax
        push si
        API 1, 81                       ; _lclose
        pop ax
        cmp ax, 7
        clc
        je a_end
        stc
a_end:  push ds
        pop es
        STEP 'a'
        ; b
        push ds
        push word fbase
        push word 2                     ; READ_WRITE
        API 1, 85                       ; _lopen
        FAILED
        jc b_end
        mov si, ax
        push si
        push word 0
        push word 0
        push word 2                     ; from the end
        API 1, 84                       ; _llseek
        push si
        push ds
        push word tapp
        push word 3
        API 1, 86                       ; _lwrite
        push ax
        push si
        API 1, 81                       ; _lclose
        pop ax
        cmp ax, 3
        clc
        je b_end
        stc
b_end:  push ds
        pop es
        STEP 'b'
        ; c, C
        mov dx, fdel
        mov ah, 41h
        DOS
        push ds
        pop es
        STEP 'c'
        push ds
        push word fdel
        push word 0
        API 1, 85                       ; _lopen has to fail
        push ds
        pop es
        cmp ax, 0xffff
        clc
        je c_end
        stc
c_end:  STEP 'C'
        ; d
        push di
        mov dx, fren
        mov di, fmoved
        mov ah, 56h
        DOS
        pop di
        push ds
        pop es
        STEP 'd'
        ; r
        push ds
        push word fmoved
        push word 0
        API 1, 85                       ; _lopen
        FAILED
        jc r_end
        mov si, ax
        push si
        push ds
        push word byte1
        push word 1
        API 1, 82                       ; _lread
        push si
        API 1, 81                       ; _lclose
        cmp byte [byte1], 'r'
        clc
        je r_end
        stc
r_end:  push ds
        pop es
        STEP 'r'
        ; the listing
        mov dx, dta
        mov ah, 1ah
        DOS
        mov dx, pattern
        mov cx, 10h
        mov ah, 4eh
        DOS
list:   push ds
        pop es
        jc listed
        mov ax, 0a0dh
        stosw
        mov si, dta + 1eh
name:   lodsb
        or al, al
        jz next
        stosb
        jmp name
next:   mov ah, 4fh
        DOS
        jmp list
listed: mov ax, 0a0dh
        stosw
        push ds
        push word fout
        push word 0
        API 1, 83                       ; _lcreat
        mov si, ax
        push si
        push ds
        push word outbuf
        mov ax, di
        sub ax, outbuf
        push ax
        API 1, 86                       ; _lwrite
        push si
        API 1, 81                       ; _lclose
        mov ax, 4c00h
        DOS
        jmp $
code_end:

section data
data_start:
        times 16 db 0                   ; the words of the local heap and the stack
fnew    db 'C:\GMBASE\W16NEW.TXT', 0
fbase   db 'C:\GMBASE\W16BASE.TXT', 0
fdel    db 'C:\GMBASE\W16DEL.TXT', 0
fren    db 'C:\GMBASE\W16REN.TXT', 0
fmoved  db 'C:\GMBASE\W16MOVED.TXT', 0
fout    db 'C:\GMBASE\W16OUT.TXT', 0
pattern db 'C:\GMBASE\*.*', 0
tnew    db 'sixteen'
tapp    db '+16'
byte1   db 0
dta     times 64 db 0
outbuf     times 3072 db 0
data_end:

section fix
fix_end:
