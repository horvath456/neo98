; SPDX-License-Identifier: GPL-2.0-or-later
; The DOS guest of tools/test_pause_host.py. It reads the
; battery clock (INT 1Ah, AH=02h) and the time of DOS (INT 21h, AH=2Ch), lets
; 146 ticks of the BIOS timer pass (eight seconds), reads both again and
; writes the eight bytes to D:\PDOS.BIN: hour, minute, second of the battery
; clock in BCD at the start and at the end, then minute and second of DOS at
; the start and at the end, then the largest step of the tick count between
; two reads. ANSWER names another file, for several machines at once
; (tools/test_pause_graphics_host.py).
        org 100h
        mov ah, 02h
        int 1ah
        mov [rtc0], ch
        mov [rtc0+1], cl
        mov [rtc0+2], dh
        mov ah, 2ch
        int 21h
        mov [dos0], cl
        mov [dos0+1], dh
        mov ah, 00h
        int 1ah
        mov [first], dx
        mov [last], dx
again:  mov ah, 00h
        int 1ah
        mov ax, dx
        sub ax, [last]
        mov [last], dx
        cmp ax, [step]
        jbe small
        mov [step], ax
small:  mov ax, dx
        sub ax, [first]
        cmp ax, 146
        jb again
        mov ah, 02h
        int 1ah
        mov [rtc1], ch
        mov [rtc1+1], cl
        mov [rtc1+2], dh
        mov ah, 2ch
        int 21h
        mov [dos1], cl
        mov [dos1+1], dh
        mov ah, 3ch
        xor cx, cx
        mov dx, fname
        int 21h
        jc done
        mov bx, ax
        mov ah, 40h
        mov cx, 12
        mov dx, rtc0
        int 21h
        mov ah, 3eh
        int 21h
done:   mov ax, 4c00h
        int 21h
%ifndef ANSWER
%define ANSWER 'D:\PDOS.BIN'
%endif
fname:  db ANSWER, 0
first:  dw 0
last:   dw 0
rtc0:   db 0, 0, 0
rtc1:   db 0, 0, 0
dos0:   db 0, 0
dos1:   db 0, 0
step:   dw 0
