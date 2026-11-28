; SPDX-License-Identifier: GPL-2.0-or-later
; A 16-bit program starts Calculator four times: by its bare name and by its
; full path, each through SHELL's ShellExecute (SHELL.20, the way File
; Manager's Run does it) and through KERNEL's WinExec (KERNEL.166). After
; each start it waits for the window of Calculator, closes it and waits until
; it is gone. C:\START16.TXT gets, for each start, the answer of the call
; (a word, above 32 when the program started) and whether the window came
; (a word, 1 or 0): eight words. tools/test_start_win16_host.py.
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

section code
code_start:
        API 1, 91                       ; InitTask
        push di
        API 2, 5                        ; InitApp
        push ds
        pop es
        cld
        mov di, outbuf
        mov si, bare
        call shell
        mov si, full
        call shell
        mov si, bare
        call winexec
        mov si, full
        call winexec
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
        API 1, 102                      ; DOS3Call
        jmp $

; ShellExecute(0, NULL, ds:si, NULL, NULL, SW_SHOWNORMAL)
shell:
        push word 0                     ; hwnd
        push word 0
        push word 0                     ; operation
        push ds
        push si                         ; file
        push word 0
        push word 0                     ; parameters
        push word 0
        push word 0                     ; directory
        push word 1                     ; SW_SHOWNORMAL
        API 3, 20                       ; ShellExecute
        stosw
        jmp came

; WinExec(ds:si, SW_SHOWNORMAL)
winexec:
        push ds
        push si
        push word 1
        API 1, 166                      ; WinExec
        stosw

; Whether the window of Calculator came within 20 seconds; it is closed
; again and its going waited for, as long again at most.
came:
        API 2, 13                       ; GetTickCount
        mov [t0], ax
.look:  API 1, 29                       ; Yield
        push ds
        push word klass
        push word 0
        push word 0
        API 2, 50                       ; FindWindow
        or ax, ax
        jnz .found
        API 2, 13                       ; GetTickCount
        sub ax, [t0]
        cmp ax, 20000
        jb .look
        xor ax, ax
        stosw
        ret
.found: mov bx, ax
        mov ax, 1
        stosw
        push bx
        push word 10h                   ; WM_CLOSE
        push word 0
        push word 0
        push word 0
        API 2, 110                      ; PostMessage
        API 2, 13                       ; GetTickCount
        mov [t0], ax
.gone:  API 1, 29                       ; Yield
        push ds
        push word klass
        push word 0
        push word 0
        API 2, 50                       ; FindWindow
        or ax, ax
        jz .done
        API 2, 13                       ; GetTickCount
        sub ax, [t0]
        cmp ax, 20000
        jb .gone
.done:  ret
code_end:

section data
data_start:
        times 16 db 0                   ; the words of the local heap and the stack
bare     db 'calc', 0
full     db 'c:\windows\calc.exe', 0
klass    db 'SciCalc', 0
fout     db 'C:\START16.TXT', 0
t0       dw 0
outbuf   times 32 db 0
data_end:

section fix
fix_end:
