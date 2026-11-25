; SPDX-License-Identifier: GPL-2.0-or-later
; The clocks of the 16-bit side count from the start of Windows, like those
; of the 32-bit side: USER.13 GetTickCount, USER.15 GetCurrentTime and
; MMSYSTEM.607 timeGetTime, each a DWORD in DX:AX, written to C:\CLOCK16.TXT.
; tools/test_clock_start_host.py; the New Executable is made as for
; tools/layer_win16_fixture.asm, with MMSYSTEM as module 3.
        bits 16
section header start=0
        dw code_end - code_start, data_end - data_start, (fix_end - fix_start) / 6
section code vstart=0 follows=header align=1
section data vstart=0 follows=code align=1
section fix vstart=0 follows=data align=1
fix_start:

; A far call the loader fills in: module 1 is KERNEL, 2 is USER, 3 is MMSYSTEM.
%macro API 2
%%at:   db 0x9a, 0xff, 0xff, 0x00, 0x00
[section fix]
        dw %%at + 1 - code_start, %1, %2
__SECT__
%endmacro

section code
code_start:
        API 1, 91                       ; InitTask
        push di
        API 2, 5                        ; InitApp
        API 2, 13                       ; GetTickCount
        mov [res], ax
        mov [res + 2], dx
        API 2, 15                       ; GetCurrentTime
        mov [res + 4], ax
        mov [res + 6], dx
        API 3, 607                      ; timeGetTime
        mov [res + 8], ax
        mov [res + 10], dx
        push ds
        push word fout
        push word 0
        API 1, 83                       ; _lcreat
        mov si, ax
        push si
        push ds
        push word res
        push word 12
        API 1, 86                       ; _lwrite
        push si
        API 1, 81                       ; _lclose
        mov ax, 4c00h
        API 1, 102                      ; DOS3Call
        jmp $
code_end:

section data
data_start:
        times 16 db 0                   ; the words of the local heap and the stack
fout    db 'C:\CLOCK16.TXT', 0
res     times 6 dw -1
        times 512 db 0
data_end:

section fix
fix_end:
