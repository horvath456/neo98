; SPDX-License-Identifier: GPL-2.0-or-later
; The DOS guest of tools/test_savestates_dos_host.py : a
; small "game" whose whole state can be read from outside, so a test can tell
; whether a loaded state is the machine that was saved.
;
; It starts in C:\SSGAME, enters SUB, and holds:
; a score in memory, shown as a bar in mode 13h (nothing else on the
; screen moves, so two pictures of the same score are the same picture);
; C:\SSGAME\LOG.TXT open for writing, one line "Axx" per point;
; a search over C:\SSGAME\FIND\*.TXT of which it has taken two names;
; a tone of 440 Hz from the PC speaker.
; Keys:
; a one point: the score goes up, a line goes into LOG.TXT
; d deletes C:\SSGAME\KEEP.TXT and writes "second" into DATA.TXT
; c enters C:\SSGAME\OTHER
; f takes the next name of the search
; w writes R0.TXT into the current directory, the next time R1.TXT and
; so on: the score, the position in LOG.TXT, whether KEEP.TXT is
; there, the first letter of DATA.TXT, the names the search gave
; q closes LOG.TXT and ends with the score as its return code
        org 100h
        cld
        mov ax, 0013h
        int 10h
        mov ah, 3bh
        mov dx, dsub
        int 21h
        ; LOG.TXT stays open.
        mov ah, 3ch
        xor cx, cx
        mov dx, flog
        int 21h
        mov [hlog], ax
        ; KEEP.TXT and DATA.TXT.
        mov dx, fkeep
        mov si, tkeep
        mov cx, 4
        call putfile
        mov dx, fdata
        mov si, tfirst
        mov cx, 5
        call putfile
        ; The search: two names taken, the rest waits.
        mov ah, 4eh
        xor cx, cx
        mov dx, ffind
        int 21h
        call addname
        mov ah, 4fh
        int 21h
        call addname
        ; The tone.
        mov al, 0b6h
        out 43h, al
        mov ax, 2712
        out 42h, al
        mov al, ah
        out 42h, al
        in al, 61h
        or al, 3
        out 61h, al
        call draw
main:   mov ah, 01h
        int 16h
        jnz key
        hlt
        jmp main
key:    xor ah, ah
        int 16h
        or al, 20h
        cmp al, 'a'
        je point
        cmp al, 'd'
        je spoil
        cmp al, 'c'
        je other
        cmp al, 'f'
        je next
        cmp al, 'w'
        je report
        cmp al, 'q'
        je quit
        jmp main

point:  inc byte [score]
        mov al, [score]
        mov di, line + 1
        call hex2
        mov ah, 40h
        mov bx, [hlog]
        mov cx, 5
        mov dx, line
        int 21h
        call draw
        jmp main

spoil:  mov ah, 41h
        mov dx, fkeep
        int 21h
        mov dx, fdata
        mov si, tsecond
        mov cx, 6
        call putfile
        jmp main

other:  mov ah, 3bh
        mov dx, dother
        int 21h
        jmp main

next:   mov ah, 4fh
        int 21h
        call addname
        jmp main

report: mov di, rscore
        mov al, [score]
        call hex2
        ; The position in LOG.TXT.
        mov ax, 4201h
        mov bx, [hlog]
        xor cx, cx
        xor dx, dx
        int 21h
        mov di, rpos
        push ax
        mov al, ah
        call hex2
        pop ax
        call hex2
        ; KEEP.TXT.
        mov byte [rkeep], '0'
        mov ax, 3d00h
        mov dx, fkeep
        int 21h
        jc nokeep
        mov bx, ax
        mov ah, 3eh
        int 21h
        mov byte [rkeep], '1'
nokeep: ; DATA.TXT.
        mov byte [rdata], '-'
        mov ax, 3d00h
        mov dx, fdata
        int 21h
        jc nodata
        mov bx, ax
        mov ah, 3fh
        mov cx, 1
        mov dx, rdata
        int 21h
        mov ah, 3eh
        int 21h
nodata: mov ah, 3ch
        xor cx, cx
        mov dx, freport
        int 21h
        jc main
        mov bx, ax
        mov ah, 40h
        mov cx, rhead_end - rhead
        mov dx, rhead
        int 21h
        mov ah, 40h
        mov cx, [names_end]
        sub cx, names
        mov dx, names
        int 21h
        mov ah, 40h
        mov cx, 2
        mov dx, crlf
        int 21h
        mov ah, 3eh
        int 21h
        inc byte [freport + 1]
        jmp main

quit:   mov ah, 3eh
        mov bx, [hlog]
        int 21h
        in al, 61h
        and al, 0fch
        out 61h, al
        mov ax, 0003h
        int 10h
        mov ah, 4ch
        mov al, [score]
        int 21h

; Creates the file DX names with the CX bytes at SI.
putfile:
        push cx
        mov ah, 3ch
        xor cx, cx
        int 21h
        pop cx
        jc putend
        mov bx, ax
        mov ah, 40h
        mov dx, si
        int 21h
        mov ah, 3eh
        int 21h
putend: ret

; After FindFirst or FindNext: the name in the DTA (PSP:9Eh) goes to the
; list with a comma, "!" when the call failed.
addname:
        mov di, [names_end]
        jnc found
        mov al, '!'
        stosb
        jmp short added
found:  mov si, 9eh
copy:   lodsb
        or al, al
        jz comma
        stosb
        jmp copy
comma:  mov al, ','
        stosb
added:  mov [names_end], di
        ret

; AL as two hex digits at DI.
hex2:   push ax
        shr al, 4
        call hex1
        pop ax
        and al, 0fh
hex1:   add al, '0'
        cmp al, '9'
        jbe digit
        add al, 7
digit:  stosb
        ret

; A frame of colour 7 and inside it a bar ten pixels wide per point.
draw:   push es
        mov ax, 0a000h
        mov es, ax
        xor di, di
        mov cx, 320 * 200
        mov al, 1
        rep stosb
        mov di, 320 * 20 + 10
        mov cx, 300
        mov al, 7
        rep stosb
        mov di, 320 * 60 + 10
        mov cx, 300
        rep stosb
        mov bx, 24
row:    mov ax, 320
        mul bx
        add ax, 10
        mov di, ax
        xor ch, ch
        mov cl, [score]
        mov ax, cx
        shl cx, 3
        add cx, ax
        add cx, ax
        cmp cx, 300
        jbe fits
        mov cx, 300
fits:   mov al, [score]
        and al, 7
        add al, 9
        rep stosb
        inc bx
        cmp bx, 56
        jb row
        pop es
        ret

score:  db 0
hlog:   dw 0
line:   db 'A00', 13, 10
dsub:   db 'C:\SSGAME\SUB', 0
dother: db 'C:\SSGAME\OTHER', 0
flog:   db 'C:\SSGAME\LOG.TXT', 0
fkeep:  db 'C:\SSGAME\KEEP.TXT', 0
fdata:  db 'C:\SSGAME\DATA.TXT', 0
ffind:  db 'C:\SSGAME\FIND\*.TXT', 0
freport: db 'R0.TXT', 0
tkeep:  db 'keep'
tfirst: db 'first'
tsecond: db 'second'
crlf:   db 13, 10
rhead:  db 'score='
rscore: db '00 pos='
rpos:   db '0000 keep='
rkeep:  db '0 data='
rdata:  db '- found='
rhead_end:
names_end: dw names
names:  times 256 db 0
