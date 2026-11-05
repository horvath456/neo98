; SPDX-License-Identifier: GPL-2.0-or-later
; The 16-bit Windows guest of tools/test_wave_in_win16.py: MMSYSTEM waveIn
; (ordinals 501 to 513) on the test microphone of src/sound_microphone.cpp,
; which the runner sets to the 441 Hz sine.
;
; 1 waveInGetNumDevs is one (none: the program ends with 99)
; 2 waveInGetDevCaps(0, caps, 2Eh): "NEO98 Wave In", two channels
; 3 waveInOpen 11.025 Hz, 8 bit, mono, no callback
; 4 waveInPrepareHeader, waveInAddBuffer of one second, waveInStart
; 5 the header comes back with WHDR_DONE and dwBytesRecorded = 11025
; 6 peak and zero crossings of the second: 441 Hz, 99 or 100 of 128
; 7 waveInGetPosition in TIME_BYTES, waveInGetID
; 8 waveInReset, waveInUnprepareHeader, waveInClose
; 9 the same stream with CALLBACK_WINDOW on a STATIC window: WIM_OPEN,
; WIM_DATA with the far address of the caller's header, WIM_CLOSE
;
; The program ends through INT 21h, AH 4Ch, with the number of the first
; check that failed, zero when all passed. No 16-bit compiler and no linker:
; nasm writes three flat parts behind a header of their sizes, and
; tools/win16_fixture.py makes the New Executable of them.
        bits 16
section header start=0
        dw code_end - code_start, data_end - data_start, (fix_end - fix_start) / 6
section code vstart=0 follows=header align=1
section data vstart=0 follows=code align=1
section fix vstart=0 follows=data align=1
fix_start:

; A far call the loader fills in: module 1 is KERNEL, 2 is USER, 3 MMSYSTEM.
%macro API 2
%%at:   db 0x9a, 0xff, 0xff, 0x00, 0x00
[section fix]
        dw %%at + 1 - code_start, %1, %2
__SECT__
%endmacro
; Ends the task with the code when the zero flag is clear.
%macro NEED_ZERO 1
        jz %%good
        mov ax, 4c00h | %1
        int 21h
%%good:
%endmacro
; Waits until the word at the address has bit 0 set (WHDR_DONE), at most ten
; seconds, giving the processor up between two looks.
%macro WAIT_DONE 2
        API 2, 13                       ; GetTickCount
        mov si, ax
%%look: test byte [%1], 1
        jnz %%done
        API 1, 29                       ; Yield
        API 2, 13
        sub ax, si
        cmp ax, 10000
        jb %%look
        mov ax, 4c00h | %2
        int 21h
%%done:
%endmacro

section code
code_start:
        API 1, 91                       ; InitTask
        push di
        API 2, 5                        ; InitApp
        mov word [header + 2], ds       ; lpData is ds:buffer
        mov word [small + 2], ds
        ; 1
        API 3, 501                      ; waveInGetNumDevs
        or ax, ax
        jnz have_device
        mov ax, 4c00h | 99
        int 21h
have_device:
        cmp ax, 1
        NEED_ZERO 1
        ; 2
        push word 0
        push ds
        push word caps
        push word 2eh
        API 3, 502                      ; waveInGetDevCaps
        or ax, ax
        NEED_ZERO 2
        cmp word [caps + 8], 'AR'
        NEED_ZERO 2
        cmp byte [caps + 14], 'W'
        NEED_ZERO 2
        cmp word [caps + 2ch], 2
        NEED_ZERO 2
        ; 3
        push ds
        push word handle
        push word 0                     ; device 0
        push ds
        push word format
        push word 0                     ; dwCallback
        push word 0
        push word 0                     ; dwInstance
        push word 0
        push word 0                     ; dwFlags
        push word 0
        API 3, 504                      ; waveInOpen
        or ax, ax
        NEED_ZERO 3
        cmp word [handle], 0
        jnz opened
        mov ax, 4c00h | 3
        int 21h
opened:
        ; 4
        push word [handle]
        push ds
        push word header
        push word 32
        API 3, 506                      ; waveInPrepareHeader
        or ax, ax
        NEED_ZERO 4
        push word [handle]
        push ds
        push word header
        push word 32
        API 3, 508                      ; waveInAddBuffer
        or ax, ax
        NEED_ZERO 4
        test byte [header + 16], 10h    ; WHDR_INQUEUE
        jnz queued
        mov ax, 4c00h | 4
        int 21h
queued:
        push word [handle]
        API 3, 509                      ; waveInStart
        or ax, ax
        NEED_ZERO 4
        ; 5
        WAIT_DONE header + 16, 5
        cmp word [header + 8], 11025    ; dwBytesRecorded
        NEED_ZERO 5
        cmp word [header + 10], 0
        NEED_ZERO 5
        test byte [header + 16], 10h
        NEED_ZERO 5
        ; 6: the largest byte and the times the samples cross 128
        mov si, buffer
        mov cx, 11025
        xor bx, bx                      ; crossings
        xor dl, dl                      ; peak
        mov dh, [si]
        and dh, 80h                     ; the side of the first sample
peak:   lodsb
        cmp al, dl
        jbe lower
        mov dl, al
lower:  and al, 80h
        cmp al, dh
        je same
        inc bx
        mov dh, al
same:   loop peak
        mov [peak_value], dl
        mov [crossings], bx
        cmp dl, 128 + 99
        jb bad_peak
        cmp dl, 128 + 100
        jbe good_peak
bad_peak:
        mov ax, 4c00h | 6
        int 21h
good_peak:
        cmp bx, 2 * 441 - 4
        jb bad_cross
        cmp bx, 2 * 441 + 4
        jbe good_cross
bad_cross:
        mov ax, 4c00h | 16
        int 21h
good_cross:
        ; 7
        mov word [time], 4              ; TIME_BYTES
        push word [handle]
        push ds
        push word time
        push word 6
        API 3, 512                      ; waveInGetPosition
        or ax, ax
        NEED_ZERO 7
        cmp word [time], 4
        NEED_ZERO 7
        cmp word [time + 2], 11025
        jae position_good
        mov ax, 4c00h | 7
        int 21h
position_good:
        mov word [identifier], 0ffffh
        push word [handle]
        push ds
        push word identifier
        API 3, 513                      ; waveInGetID
        or ax, ax
        NEED_ZERO 17
        cmp word [identifier], 0
        NEED_ZERO 17
        ; 8
        push word [handle]
        API 3, 511                      ; waveInReset
        or ax, ax
        NEED_ZERO 8
        push word [handle]
        push ds
        push word header
        push word 32
        API 3, 507                      ; waveInUnprepareHeader
        or ax, ax
        NEED_ZERO 8
        push word [handle]
        API 3, 505                      ; waveInClose
        or ax, ax
        NEED_ZERO 8
        push word [handle]
        API 3, 509                      ; waveInStart on a closed handle
        cmp ax, 5
        NEED_ZERO 8
        ; 9: CALLBACK_WINDOW
        push ds
        push word class_name
        push ds
        push word title
        push word 0
        push word 0
        push word 0
        push word 0
        push word 0
        push word 0
        push word 0
        push word 0
        push word 0
        push word 0
        push word 0
        API 2, 41                       ; CreateWindow
        mov [window], ax
        or ax, ax
        jnz have_window
        mov ax, 4c00h | 9
        int 21h
have_window:
        push ds
        push word handle
        push word 0
        push ds
        push word format
        push word 0                     ; dwCallback: the window
        push word [window]
        push word 0                     ; dwInstance
        push word 0
        push word 1                     ; CALLBACK_WINDOW
        push word 0
        API 3, 504                      ; waveInOpen
        or ax, ax
        NEED_ZERO 9
        call peek
        cmp word [message + 2], 3beh    ; WIM_OPEN
        NEED_ZERO 10
        mov ax, [message + 4]
        cmp ax, [handle]
        NEED_ZERO 10
        push word [handle]
        push ds
        push word small
        push word 32
        API 3, 506
        or ax, ax
        NEED_ZERO 11
        push word [handle]
        push ds
        push word small
        push word 32
        API 3, 508
        or ax, ax
        NEED_ZERO 11
        push word [handle]
        API 3, 509
        or ax, ax
        NEED_ZERO 11
        WAIT_DONE small + 16, 12
        call peek
        cmp word [message + 2], 3c0h    ; WIM_DATA
        NEED_ZERO 13
        cmp word [message + 6], small   ; lParam is the caller's far header
        NEED_ZERO 13
        mov ax, ds
        cmp [message + 8], ax
        NEED_ZERO 13
        cmp word [small + 8], 1102      ; dwBytesRecorded
        NEED_ZERO 13
        push word [handle]
        API 3, 511
        push word [handle]
        push ds
        push word small
        push word 32
        API 3, 507
        or ax, ax
        NEED_ZERO 14
        push word [handle]
        API 3, 505
        or ax, ax
        NEED_ZERO 14
        call peek
        cmp word [message + 2], 3bfh    ; WIM_CLOSE
        NEED_ZERO 15
        mov ax, 4c00h
        int 21h
        jmp $

; PeekMessage(&message, window, 3BEh, 3C0h, PM_REMOVE) until something comes,
; at most ten seconds.
peek:   API 2, 13
        mov si, ax
.again: push ds
        push word message
        push word [window]
        push word 3beh
        push word 3c0h
        push word 1
        API 2, 109                      ; PeekMessage
        or ax, ax
        jnz .got
        API 1, 29
        API 2, 13
        sub ax, si
        cmp ax, 10000
        jb .again
        mov ax, 4c00h | 18
        int 21h
.got:   ret
code_end:

section data
data_start:
        times 16 db 0                   ; the words of the local heap and the stack
format  dw 1, 1                         ; PCM, mono
        dd 11025, 11025                 ; rate, bytes a second
        dw 1, 8, 0                      ; align, bits, cbSize
class_name db 'STATIC', 0
title   db 'wave in', 0
handle  dw 0
window  dw 0
identifier dw 0
peak_value db 0
        db 0
crossings dw 0
time    times 8 db 0
message times 18 db 0
caps    times 48 db 0
; WAVEHDR16: lpData, dwBufferLength, dwBytesRecorded, dwUser, dwFlags,
; dwLoops, lpNext, reserved.
header  dw buffer, 0
        dd 11025, 0, 0, 0, 0, 0, 0
small   dw buffer, 0
        dd 1102, 0, 0, 0, 0, 0, 0
buffer  times 11025 db 0
data_end:

section fix
fix_end:
