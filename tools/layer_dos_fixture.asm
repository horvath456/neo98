; SPDX-License-Identifier: GPL-2.0-or-later
; The DOS guest of tools/test_game_machine_files_host.py uses INT 21h on
; C:\GMBASE, where the normal machine supplies files and directories.
;
; a create DOSNEW.TXT and write it (3Ch, 40h, 3Eh)
; b open DOS.TXT of the drive below, append (3Dh, 42h, 40h)
; c delete DOSDEL.TXT of the drive below (41h)
; C it is not found afterwards (3Dh answers 2)
; d rename DOSREN.TXT to DOSMOVED.TXT (56h)
; e make the directory DOSDIR (39h)
; E create DOSDIR\IN.TXT
; f DOSRD of the drive below is not removed while RD.TXT is in it (3Ah)
; F with RD.TXT deleted it goes
; k DOSRD is made again
; G and RD.TXT of the drive below does not show in it
; g XDEL.TXT, which a Windows program deleted, is not found
; (x when it is: the run without the Windows program)
; h XWIN.TXT, which a Windows program wrote, reads "w..."
; (y when it is not there)
;
; Each step puts its letter into C:\GMBASE\DOSOUT.TXT, "-" when it failed;
; then follows every name FindFirst/FindNext report for C:\GMBASE\*.*, one
; a line (4Eh, 4Fh, with directories). The program ends with code 0.
        org 100h
%macro STEP 1
        mov al, %1
        jnc %%good
        mov al, '-'
%%good: stosb
%endmacro
        cld
        mov di, outbuf
        ; a
        mov ah, 3ch
        xor cx, cx
        mov dx, fnew
        int 21h
        jc a_end
        mov bx, ax
        mov ah, 40h
        mov cx, 7
        mov dx, tnew
        int 21h
        pushf
        mov ah, 3eh
        int 21h
        popf
a_end:  STEP 'a'
        ; b
        mov ax, 3d02h
        mov dx, fbase
        int 21h
        jc b_end
        mov bx, ax
        mov ax, 4202h
        xor cx, cx
        xor dx, dx
        int 21h
        mov ah, 40h
        mov cx, 4
        mov dx, tapp
        int 21h
        pushf
        mov ah, 3eh
        int 21h
        popf
b_end:  STEP 'b'
        ; c, C
        mov ah, 41h
        mov dx, fdel
        int 21h
        STEP 'c'
        mov dx, fdel
        call missing
        STEP 'C'
        ; d
        push di
        mov ah, 56h
        mov dx, fren
        mov di, fmoved
        int 21h
        pop di
        STEP 'd'
        ; e, E
        mov ah, 39h
        mov dx, fdir
        int 21h
        STEP 'e'
        mov ah, 3ch
        xor cx, cx
        mov dx, fdirin
        int 21h
        jc e_end
        mov bx, ax
        mov ah, 3eh
        int 21h
e_end:  STEP 'E'
        ; f: the directory is not empty, so removing it has to fail
        mov ah, 3ah
        mov dx, frd
        int 21h
        cmc
        STEP 'f'
        mov ah, 41h
        mov dx, frdin
        int 21h
        mov ah, 3ah
        mov dx, frd
        int 21h
        STEP 'F'
        mov ah, 39h
        mov dx, frd
        int 21h
        STEP 'k'
        mov dx, frdin
        call missing
        STEP 'G'
        ; g
        mov dx, fxdel
        call missing
        mov al, 'g'
        jnc g_put
        mov al, 'x'
g_put:  stosb
        ; h
        mov ax, 3d00h
        mov dx, fxwin
        int 21h
        jc h_no
        mov bx, ax
        mov ah, 3fh
        mov cx, 1
        mov dx, byte1
        int 21h
        mov ah, 3eh
        int 21h
        cmp byte [byte1], 'w'
        jne h_no
        mov al, 'h'
        jmp h_put
h_no:   mov al, 'y'
h_put:  stosb
        ; the listing
        mov ah, 1ah
        mov dx, dta
        int 21h
        mov ah, 4eh
        mov cx, 10h
        mov dx, pattern
        int 21h
list:   jc listed
        mov ax, 0a0dh
        stosw
        mov si, dta + 1eh
name:   lodsb
        or al, al
        jz next
        stosb
        jmp name
next:   mov ah, 4fh
        int 21h
        jmp list
listed: mov ax, 0a0dh
        stosw
        mov ah, 3ch
        xor cx, cx
        mov dx, fout
        int 21h
        jc done
        mov bx, ax
        mov cx, di
        sub cx, outbuf
        mov dx, outbuf
        mov ah, 40h
        int 21h
        mov ah, 3eh
        int 21h
done:   mov ax, 4c00h
        int 21h

; Carry clear when the file DS:DX names is not found (error 2 or 3).
missing:
        mov ax, 3d00h
        int 21h
        jc .failed
        mov bx, ax
        mov ah, 3eh
        int 21h
        stc
        ret
.failed:
        cmp ax, 2
        je .gone
        cmp ax, 3
        je .gone
        stc
        ret
.gone:  clc
        ret

fnew    db 'C:\GMBASE\DOSNEW.TXT', 0
fbase   db 'C:\GMBASE\DOSBASE.TXT', 0
fdel    db 'C:\GMBASE\DOSDEL.TXT', 0
fren    db 'C:\GMBASE\DOSREN.TXT', 0
fmoved  db 'C:\GMBASE\DOSMOVED.TXT', 0
fdir    db 'C:\GMBASE\DOSDIR', 0
fdirin  db 'C:\GMBASE\DOSDIR\IN.TXT', 0
frd     db 'C:\GMBASE\DOSRD', 0
frdin   db 'C:\GMBASE\DOSRD\RD.TXT', 0
fxdel   db 'C:\GMBASE\XDEL.TXT', 0
fxwin   db 'C:\GMBASE\XWIN.TXT', 0
fout    db 'C:\GMBASE\DOSOUT.TXT', 0
pattern db 'C:\GMBASE\*.*', 0
tnew    db 'dos new'
tapp    db '+dos'
byte1   db 0
dta     times 64 db 0
outbuf:
