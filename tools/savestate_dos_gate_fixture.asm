; SPDX-License-Identifier: GPL-2.0-or-later
; A loader that needs the actual letter 'g', then EXECs SSGAME.COM. The
; host must reproduce the recorded startup keys when loading a state in
; SSGAME, including the PAUSE of the batch file that starts this loader.
        org 100h
        mov bx, (end - $$ + 100h + 15) / 16
        mov ah, 4ah
        int 21h
waitkey:
        mov ah, 08h
        int 21h
        cmp al, 'g'
        jne waitkey
        mov ax, cs
        mov [parameters + 4], ax
        mov dx, program
        mov bx, parameters
        mov ax, 4b00h
        int 21h
        jc failed
        mov ax, 4c00h
        int 21h
failed: mov ax, 4c01h
        int 21h
program db 'C:\SSGAME\SSGAME.COM', 0
parameters dw 0, tail, 0, 0, 0, 0, 0
tail db 0, 13
end:
