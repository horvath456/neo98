; SPDX-License-Identifier: GPL-2.0-or-later
; What the 16-bit parts of the display properties ask of USER and the BIOS
;
; one 16-bit program that uses it and writes down what came back;
; tools/test_win16_settings_host.py reads the words.
;
; temp   USER.661 SetSysColorsTemp with 25 colours and 25 brushes of 16 bits
;        answers a cookie, and GetSysColor(COLOR_MENU) is the new colour;
;        USER.662 DrawMenuBarTemp draws a menu of one item into a rectangle
;        of 16 bits and the second call answers the height of the bar; the
;        cookie with null colours puts the old colours back. INT 10h
;        AX=4F10h BL=00h, the power management of VBE, answers AX=014Fh
;        (function known, failed): the display has none.
; mode   KERNEL.95 LoadLibrary of FRAMEBUF.DRV, the display driver of a mode
;        that the Settings page loads, answers the loaded DISPLAY (KERNEL.47
;        GetModuleHandle), and its ValidateMode (KERNEL.50 GetProcAddress)
;        takes DISPVALMODE: the mode of the screen is 0 (VALMODE_YES), a
;        resolution the display lacks 2 (VALMODE_NO_NOMEM) and a depth it
;        lacks 3 (VALMODE_NO_NODAC).
; spi    USER.483 SystemParametersInfo with the structures of 16 bits: the
;        LOGFONT of the icon titles (50 bytes), NONCLIENTMETRICS with cbSize
;        10Eh and ICONMETRICS with cbSize 3Ah, each followed by a guard word
;        that must stay; SPI_GETBORDER writes one word.
;
; Each part writes the result file again when it is done, so a part that
; stops the run leaves the words of the parts before it. The last word is
; the stack pointer after the last call against the one before the first.
; The New Executable is made as for tools/layer_win16_fixture.asm, with GDI
; as module 3.
        bits 16
section header start=0
        dw code_end - code_start, data_end - data_start, (fix_end - fix_start) / 6
section code vstart=0 follows=header align=1
section data vstart=0 follows=code align=1
section fix vstart=0 follows=data align=1
fix_start:

; A far call the loader fills in: module 1 is KERNEL, 2 is USER, 3 is GDI.
%macro API 2
%%at:   db 0x9a, 0xff, 0xff, 0x00, 0x00
[section fix]
        dw %%at + 1 - code_start, %1, %2
__SECT__
%endmacro
; AX into the next result word.
%macro KEEP 0
        push ds
        pop es
        stosw
%endmacro
; 1 when AX is not zero, 0 otherwise.
%macro KEEPNZ 0
        neg ax
        sbb ax, ax
        neg ax
        KEEP
%endmacro
; SPI action, parameter, buffer: USER.483 SystemParametersInfo without flags.
%macro SPI 3
        push word %1
        push word %2
        push ds
        push word %3
        push word 0
        API 2, 483
%endmacro

section code
code_start:
        API 1, 91                       ; InitTask
        push di
        API 2, 5                        ; InitApp
        cld
        mov [sp0], sp
        mov di, res

        ; temp: the colour of the menu before, then SetSysColorsTemp
        push word 4                     ; COLOR_MENU
        API 2, 180                      ; GetSysColor
        mov [menu_low], ax
        mov [menu_high], dx
        push word 0012h
        push word 3456h
        API 3, 66                       ; CreateSolidBrush
        push di
        push ds
        pop es
        mov di, brushes
        mov cx, 25
        rep stosw
        pop di
        push ds
        push word colours
        push ds
        push word brushes
        push word 25
        API 2, 661                      ; SetSysColorsTemp
        mov [cookie], ax
        KEEPNZ
        push word 4
        API 2, 180                      ; GetSysColor
        KEEP
        mov ax, dx
        KEEP
        ; DrawMenuBarTemp(desktop, screen DC, rect, menu, no font), twice
        API 2, 286                      ; GetDesktopWindow
        mov [desktop], ax
        push word 0
        API 2, 66                       ; GetDC
        mov [hdc], ax
        API 2, 151                      ; CreateMenu
        mov [menu], ax
        push ax
        push word 0                     ; MF_STRING
        push word 100
        push ds
        push word item
        API 2, 411                      ; AppendMenu
        mov cx, 2
.draw:
        push cx
        push word [desktop]
        push word [hdc]
        push ds
        push word rect
        push word [menu]
        push word 0
        API 2, 662                      ; DrawMenuBarTemp
        pop cx
        loop .draw
        KEEP
        push word [desktop]
        push word [hdc]
        API 2, 68                       ; ReleaseDC
        ; the cookie puts the colours back
        push word 0
        push word 0
        push word 0
        push word 0
        push word [cookie]
        API 2, 661                      ; SetSysColorsTemp
        KEEP
        push word 4
        API 2, 180                      ; GetSysColor
        sub ax, [menu_low]
        sub dx, [menu_high]
        or ax, dx
        KEEP
        ; INT 10h AX=4F10h BL=00h: the capabilities of VBE/PM
        mov ax, 4f10h
        xor bx, bx
        int 10h
        KEEP
        call save

        ; mode: FRAMEBUF.DRV is the loaded DISPLAY
        push ds
        push word framebuf
        API 1, 95                       ; LoadLibrary
        mov [driver], ax
        cmp ax, 32
        sbb ax, ax
        inc ax
        KEEP
        push ds
        push word display
        API 1, 47                       ; GetModuleHandle
        sub ax, [driver]
        KEEP
        push word [driver]
        push ds
        push word validate_name
        API 1, 50                       ; GetProcAddress
        mov [validate], ax
        mov [validate + 2], dx
        or ax, dx
        KEEPNZ
        ; DISPVALMODE of the screen: its size and the bits of a pixel
        push word 0                     ; SM_CXSCREEN
        API 2, 179                      ; GetSystemMetrics
        mov [mode_x], ax
        push word 1                     ; SM_CYSCREEN
        API 2, 179
        mov [mode_y], ax
        push word 0
        API 2, 66                       ; GetDC
        mov [hdc], ax
        push ax
        push word 12                    ; BITSPIXEL
        API 3, 80                       ; GetDeviceCaps
        mov [mode_bits], ax
        push word 0
        push word [hdc]
        API 2, 68                       ; ReleaseDC
        mov ax, [validate]
        or ax, [validate + 2]
        jz .no_validate
        push ds
        push word mode
        call far [validate]
        KEEP
        mov word [mode_x], 123          ; a size the display does not have
        mov word [mode_y], 77
        push ds
        push word mode
        call far [validate]
        KEEP
        push word 0
        API 2, 179
        mov [mode_x], ax
        push word 1
        API 2, 179
        mov [mode_y], ax
        mov word [mode_bits], 5         ; a depth it does not have
        push ds
        push word mode
        call far [validate]
        KEEP
.no_validate:
        call save

        ; spi: SPI_GETICONTITLELOGFONT with a LOGFONT of 16 bits
        SPI 1fh, 50, spi_buffer
        KEEP
        mov ax, [spi_buffer]
        KEEP
        mov al, [spi_buffer + 18]
        xor ah, ah
        KEEP
        mov ax, [spi_buffer + 50]
        KEEP
        ; SPI_GETNONCLIENTMETRICS, cbSize 10Eh
        call guard
        mov word [spi_buffer], 10eh
        SPI 29h, 10eh, spi_buffer
        KEEP
        mov ax, [spi_buffer + 2]        ; iBorderWidth
        KEEP
        mov ax, [spi_buffer + 4]        ; iScrollWidth
        KEEP
        mov al, [spi_buffer + 12 + 18]  ; the face of lfCaptionFont
        xor ah, ah
        KEEP
        mov al, [spi_buffer + 220 + 18] ; the face of lfMessageFont
        xor ah, ah
        KEEP
        mov ax, [spi_buffer + 10eh]
        KEEP
        ; SPI_GETICONMETRICS, cbSize 3Ah
        call guard
        mov word [spi_buffer], 3ah
        SPI 2dh, 3ah, spi_buffer
        KEEP
        mov ax, [spi_buffer + 2]        ; iHorzSpacing
        KEEP
        mov al, [spi_buffer + 8 + 18]   ; the face of lfFont
        xor ah, ah
        KEEP
        mov ax, [spi_buffer + 3ah]
        KEEP
        ; SPI_GETBORDER: one word
        call guard
        SPI 5, 0, spi_buffer
        KEEP
        mov ax, [spi_buffer]
        KEEP
        mov ax, [spi_buffer + 2]
        KEEP
        call save

        ; The stack pointer against the one before the first call.
        mov ax, [sp0]
        sub ax, sp
        mov [res + 62], ax
        call save
        mov ax, 4c00h
        API 1, 102                      ; DOS3Call
        jmp $

; The buffer of SystemParametersInfo full of AAh.
guard:
        push di
        push ds
        pop es
        mov di, spi_buffer
        mov cx, 200
        mov ax, 0aaaah
        rep stosw
        pop di
        ret

; C:\SET16OUT.TXT: the result words.
save:
        push di
        push ds
        push word fout
        push word 0
        API 1, 83                       ; _lcreat
        mov si, ax
        push si
        push ds
        push word res
        push word out_end - res
        API 1, 86                       ; _lwrite
        push si
        API 1, 81                       ; _lclose
        pop di
        ret
code_end:

section data
data_start:
        times 16 db 0                   ; the words of the local heap and the stack
sp0     dw 0
cookie  dw 0
menu_low dw 0
menu_high dw 0
desktop dw 0
hdc     dw 0
menu    dw 0
colours times 25 dd 00123456h
brushes times 25 dw 0
rect    dw 0, 0, 300, 20
item    db 'File', 0
driver  dw 0
validate dd 0
; DISPVALMODE: its size, the bits of a pixel, the width and the height
mode    dw 8
mode_bits dw 0
mode_x  dw 0
mode_y  dw 0
framebuf db 'FRAMEBUF.DRV', 0
display db 'DISPLAY', 0
validate_name db 'ValidateMode', 0
spi_buffer times 400 db 0aah
fout    db 'C:\SET16OUT.TXT', 0
res     times 32 dw -1                ; a part that never ran reads as FFFFh
out_end:
        times 512 db 0
data_end:

section fix
fix_end:
