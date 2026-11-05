; SPDX-License-Identifier: GPL-2.0-or-later
; What a DOS program reads of the machine it runs on: the
; identification of the system BIOS (64 bytes), its version at
; (32 bytes), its date (8 bytes) and the vendor,
; product and revision strings of VBE function 4F00h, each cut at 40 bytes.
; C:\BIOSID.TXT, one a line. tools/test_machine_names_host.py.
        org 0x100
        cld
        mov di, text
        mov bx, 0xe00e
        mov cx, 64
        call rom
        mov bx, 0xe061
        mov cx, 32
        call rom
        mov bx, 0xfff5
        mov cx, 8
        call rom
        ; VBE 2.0 information
        mov dword [info], 'VBE2'
        push di
        mov di, info
        mov ax, 0x4f00
        int 0x10
        pop di
        cmp ax, 0x004f
        jne write
        mov si, info + 0x16             ; OemVendorNamePtr
        call far_text
        mov si, info + 0x1a             ; OemProductNamePtr
        call far_text
        mov si, info + 0x1e             ; OemProductRevPtr
        call far_text
write:  mov ah, 0x3c
        xor cx, cx
        mov dx, name
        int 0x21
        jc done
        mov bx, ax
        mov cx, di
        sub cx, text
        mov dx, text
        mov ah, 0x40
        int 0x21
        mov ah, 0x3e
        int 0x21
done:   mov ax, 0x4c00
        int 0x21

; CX printable bytes of F000:BX, then a line end.
rom:    push ds
        mov ax, 0xf000
        mov ds, ax
        mov si, bx
        call copy
        pop ds
        ret
; The string a far pointer at DS:SI names, at most 40 bytes.
far_text:
        push ds
        lds si, [si]
        mov cx, 40
        call copy
        pop ds
        ret
copy:   lodsb
        cmp al, 0x20
        jb ended
        cmp al, 0x7e
        ja ended
        stosb
        loop copy
ended:  mov ax, 0x0a0d
        stosw
        ret

name    db 'C:\BIOSID.TXT', 0
info    times 512 db 0
text    times 512 db 0
