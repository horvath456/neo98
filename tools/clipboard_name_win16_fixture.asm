; SPDX-License-Identifier: GPL-2.0-or-later
; GetClipboardFormatName of a 16-bit program (USER.146): the count
; is the word behind the far pointer of the buffer.
;
; RegisterClipboardFormat("NEO98FORMAT") (USER.145)
; GetClipboardFormatName(format, small, 5) -> 4, "ARM9"
; GetClipboardFormatName(format, large, 32) -> 11, the whole name
;
; C:\CF16OUT.TXT holds the two results as letters ('A' + result) and the two
; buffers, 16 bytes each. tools/test_win16_clipboard_name_host.py; the New
; Executable is made as for tools/layer_win16_fixture.asm.
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
        push di                         ; the instance
        API 2, 5                        ; InitApp
        push ds
        push word fname
        API 2, 145                      ; RegisterClipboardFormat
        mov si, ax
        push si
        push ds
        push word small
        push word 5
        API 2, 146                      ; GetClipboardFormatName
        add al, 'A'
        mov [result], al
        push si
        push ds
        push word large
        push word 32
        API 2, 146
        add al, 'A'
        mov [result + 1], al
        push ds
        push word fout
        push word 0
        API 1, 83                       ; _lcreat
        mov si, ax
        push si
        push ds
        push word result
        push word 34
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
fname   db 'NEO98FORMAT', 0
fout    db 'C:\CF16OUT.TXT', 0
result  db 0, 0
small   times 16 db '.'
large   times 16 db '.'
        times 16 db 0
        times 512 db 0                  ; room where a wrong count would write
data_end:

section fix
fix_end:
