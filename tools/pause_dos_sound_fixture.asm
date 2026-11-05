; SPDX-License-Identifier: GPL-2.0-or-later
; The DOS guest of tools/test_pause_sound_host.py : a tone of
; 440 Hz from the PC speaker (timer 2, mode 3) for 110 ticks of the BIOS timer,
; six seconds, then silence and the end. With a letter as its argument it
; writes that letter to the screen first, so several machines can be told
; apart.
        org 100h
        mov al, 0b6h
        out 43h, al
        mov ax, 2712            ; 1193182 / 440
        out 42h, al
        mov al, ah
        out 42h, al
        in al, 61h
        or al, 3
        out 61h, al
        mov ah, 00h
        int 1ah
        mov [first], dx
again:  mov ah, 00h
        int 1ah
        sub dx, [first]
        cmp dx, 110
        jb again
        in al, 61h
        and al, 0fch
        out 61h, al
        mov ax, 4c00h
        int 21h
first:  dw 0
