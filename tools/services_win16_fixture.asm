; SPDX-License-Identifier: GPL-2.0-or-later
; KERNEL and GDI services of the 16-bit side beyond the plain gates
;
; one 16-bit program that uses them and writes down what came back;
; tools/test_win16_services_host.py reads the words.
;
; flat   KERNEL.454 __FLATCS and KERNEL.455 __FLATDS, absolute entries the
;        loader writes into the code: ring-three selectors with base 0 and
;        a limit of 4 GiB; a word of the data segment read through __FLATDS
;        at its linear address
; dpmi   INT 31h 000Ch sets the eight bytes of an allocated descriptor and
;        000Bh reads them back; 8022h for a selector that is not allocated,
;        8021h for a system descriptor
; fonts  GDI.613 EnumFontFamiliesEx with a 16-bit LOGFONT of
;        DEFAULT_CHARSET: the far callback gets the LONG, ENUMLOGFONTEX with
;        the script name behind the style and NEWTEXTMETRICEX with an empty
;        font signature
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
; A far pointer in the code the loader fills in with an absolute entry; both
; words get its value.
%macro ENTRY 3
%1:     dw 0xffff, 0x0000
[section fix]
        dw %1 - code_start, %2, %3
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
; AX when the carry is set, 0 when it is clear.
%macro KEEPCF 0
        jc %%failed
        xor ax, ax
%%failed:
        KEEP
%endmacro

section code
code_start:
        API 1, 91                       ; InitTask
        push di
        API 2, 5                        ; InitApp
        cld
        mov [sp0], sp
        mov di, res
        ; The linear address of the marker word, from KERNEL.186
        ; GetSelectorBase of the data segment.
        push ds
        API 1, 186
        add ax, marker
        adc dx, 0
        mov [linear], ax
        mov [linear + 2], dx

        ; flat: __FLATDS, its limit and rights, the marker through it
        mov ax, [cs:flatds + 2]
        mov [flat_data], ax
        KEEPNZ
        xor eax, eax
        lsl eax, [flat_data]
        KEEP
        shr eax, 16
        KEEP
        lar ax, [flat_data]
        mov al, ah
        and ax, 0feh                    ; the accessed bit is the CPU's
        KEEP
        xor ax, ax
        xor ebx, ebx
        lsl ebx, [flat_data]
        cmp ebx, 0ffffffffh
        jne .no_flat_read
        mov es, [flat_data]
        mov ebx, [linear]
        mov ax, [es:ebx]
.no_flat_read:
        KEEP
        ; __FLATCS
        mov ax, [cs:flatcs + 2]
        mov [flat_code], ax
        KEEPNZ
        xor eax, eax
        lsl eax, [flat_code]
        KEEP
        shr eax, 16
        KEEP
        lar ax, [flat_code]
        mov al, ah
        and ax, 0feh
        KEEP
        mov ax, [flat_code]
        cmp ax, [flat_data]
        mov ax, 0
        je .same_flat
        inc ax
.same_flat:
        KEEP
        call save

        ; dpmi: 0000h one descriptor, 000Ch over the data segment with a
        ; limit of FFFh, the marker through it, 000Bh back, then the refusals
        xor ax, ax
        mov cx, 1
        int 31h
        mov [dpmi_selector], ax
        KEEPCF
        mov ax, [linear]
        sub ax, marker
        mov [descriptor + 2], ax
        mov ax, [linear + 2]
        sbb ax, 0
        mov [descriptor + 4], al
        mov [descriptor + 7], ah
        push di
        push ds
        pop es
        mov di, descriptor
        mov bx, [dpmi_selector]
        mov ax, 000ch
        int 31h
        pop di
        KEEPCF
        push es
        mov es, [dpmi_selector]
        mov ax, [es:marker]
        pop es
        KEEP
        push di
        push ds
        pop es
        mov di, readback
        mov bx, [dpmi_selector]
        mov ax, 000bh
        int 31h
        pop di
        KEEPCF
        mov ax, [readback]
        KEEP
        mov ax, [readback + 2]
        sub ax, [descriptor + 2]
        KEEP
        mov al, [readback + 4]
        mov ah, [readback + 7]
        mov bl, [descriptor + 4]
        mov bh, [descriptor + 7]
        sub ax, bx
        KEEP
        push di
        push ds
        pop es
        mov di, readback
        xor bx, bx
        mov ax, 000bh
        int 31h
        pop di
        KEEPCF
        mov byte [descriptor + 5], 82h  ; an LDT descriptor, a system one
        push di
        push ds
        pop es
        mov di, descriptor
        mov bx, [dpmi_selector]
        mov ax, 000ch
        int 31h
        pop di
        KEEPCF
        mov bx, [dpmi_selector]
        mov ax, 0001h
        int 31h
        call save

        ; fonts: GDI.52 CreateCompatibleDC(0), GDI.613
        ; EnumFontFamiliesEx(hdc, logfont, procedure, 12345678h, 0)
        push word 0
        API 3, 52
        mov [hdc], ax
        KEEPNZ
        push word [hdc]
        push ds
        push word logfont
        push cs
        push word font_proc
        push word 1234h
        push word 5678h
        push word 0
        push word 0
        API 3, 613
        KEEP
        mov ax, [calls]
        KEEP
        mov ax, [seen_low]
        KEEP
        mov ax, [seen_high]
        KEEP
        mov ax, [types]
        KEEP
        mov ax, [signature]
        KEEP

        ; The stack pointer against the one before the first call.
        mov ax, [sp0]
        sub ax, sp
        KEEP
        call save
        mov ax, 4c00h
        API 1, 102                      ; DOS3Call
        jmp $

; C:\SVC16OUT.TXT: the result words and the names of the first font.
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

; int FAR PASCAL FontProc(ENUMLOGFONTEX far *, NEWTEXTMETRICEX far *, int type,
; LPARAM): [bp+10h] the names, [bp+0Ch] the metrics, [bp+0Ah] the type,
; [bp+8] and [bp+6] the LONG. DS is the task's, as GDI hands it.
font_proc:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        pop es
        inc word [calls]
        mov ax, [bp + 6]
        mov [seen_low], ax
        mov ax, [bp + 8]
        mov [seen_high], ax
        mov ax, [bp + 0ah]
        or [types], ax
        test ax, 4                      ; TRUETYPE_FONTTYPE
        jz .names
        ; FONTSIGNATURE, 24 bytes behind the 41 of NEWTEXTMETRIC
        push ds
        lds si, [bp + 0ch]
        add si, 41
        mov cx, 12
        xor dx, dx
.signature:
        lodsw
        or dx, ax
        loop .signature
        pop ds
        or [signature], dx
.names:
        cmp word [calls], 1
        jne .done
        ; the face (18 of LOGFONT) and the script (146 of ENUMLOGFONTEX)
        push ds
        lds si, [bp + 10h]
        add si, 18
        mov di, face
        mov cx, 16
        rep movsw
        pop ds
        push ds
        lds si, [bp + 10h]
        add si, 50 + 64 + 32
        mov di, script
        mov cx, 16
        rep movsw
        pop ds
.done:
        mov ax, 1
        pop di
        pop si
        pop bp
        retf 14

        ENTRY flatcs, 1, 454
        ENTRY flatds, 1, 455
code_end:

section data
data_start:
        times 16 db 0                   ; the words of the local heap and the stack
sp0     dw 0
hdc     dw 0
linear  dd 0
flat_data dw 0
flat_code dw 0
dpmi_selector dw 0
calls   dw 0
seen_low dw 0
seen_high dw 0
types   dw 0
signature dw 0
marker  dw 5aa5h
; limit FFFh, base filled in, present ring-three writable data, 16-bit
descriptor db 0ffh, 0fh, 0, 0, 0, 0f2h, 0, 0
readback times 8 db 0
; LOGFONT: no height, width, escapement, orientation or weight; DEFAULT_CHARSET
; at 13; no face, so every family once per script
logfont times 13 db 0
        db 1
        times 50 - 14 db 0
fout    db 'C:\SVC16OUT.TXT', 0
res     times 32 dw -1                ; a part that never ran reads as FFFFh
face    times 32 db 0
script  times 32 db 0
out_end:
        times 512 db 0
data_end:

section fix
fix_end:
