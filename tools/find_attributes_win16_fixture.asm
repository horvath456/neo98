; SPDX-License-Identifier: GPL-2.0-or-later
; DOS3Call 4Eh with the label bit among others. SETUPX asks with
; CX = 1Fh whether C:\WINDOWS\SETUP.INI exists before it writes the Start
; menu groups of an INF; the port answered with the label of C: and SETUPX
; failed with "INF not found" (205) and left out the AddReg of the INF.
;
; a 4Eh, CX = 1Fh, C:\WINDOWS\NOSUCH.INI: carry, AX = 2 or 18
; b 4Eh, CX = 1Fh, C:\WINDOWS\WIN.INI: no carry, WIN.INI in the record
;
; Each step puts its letter into C:\FIND16.TXT, "-" when it failed. The
; program ends through DOS3Call 4Ch. tools/test_find_attributes_win16_host.py.
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

section code
code_start:
        API 1, 91                       ; InitTask
        push di                         ; the instance
        API 2, 5                        ; InitApp
        push ds
        pop es
        cld
        mov dx, dta
        mov ah, 1ah
        DOS
        mov di, outbuf
        ; a: nothing of that name, so the search fails
        mov dx, missing
        mov cx, 1fh
        mov ah, 4eh
        DOS
        push ds
        pop es
        mov bx, ax
        mov al, '-'
        jnc a_end
        cmp bx, 2
        je a_good
        cmp bx, 18
        jne a_end
a_good: mov al, 'a'
a_end:  stosb
        ; b: an existing file is found, under its own name
        mov dx, existing
        mov cx, 1fh
        mov ah, 4eh
        DOS
        push ds
        pop es
        mov al, '-'
        jc b_end
        cmp word [dta + 1eh], 'WI'
        jne b_end
        cmp word [dta + 20h], 'N.'
        jne b_end
        mov al, 'b'
b_end:  stosb
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
missing  db 'C:\WINDOWS\NOSUCH.INI', 0
existing db 'C:\WINDOWS\WIN.INI', 0
fout     db 'C:\FIND16.TXT', 0
dta      times 64 db 0
outbuf   times 64 db 0
data_end:

section fix
fix_end:
