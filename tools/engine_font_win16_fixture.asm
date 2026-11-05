; SPDX-License-Identifier: GPL-2.0-or-later
bits 16
section header start=0
 dw code_end-code_start,data_end-data_start,(fix_end-fix_start)/6
section code vstart=0 follows=header align=1
section data vstart=0 follows=code align=1
section fix vstart=0 follows=data align=1
fix_start:
%macro API 2
%%at: db 0x9a,0xff,0xff,0,0
[section fix]
 dw %%at+1-code_start,%1,%2
__SECT__
%endmacro
section code
code_start:
 API 1,91
 push di
 API 2,5
 mov [sp0],sp
 push ds
 push word dll
 API 1,95
 push ax
 push ds
 push word name
 API 1,50
 mov [method],ax
 mov [method+2],dx
 or ax,dx
 jz finish
 mov word [resolved],1
 push word 2
 push word 0
 push word 128
 API 1,15
 mov [handle],ax
 mov [font+5ah],ax
 mov word [font],3
 mov word [font+27h],font+83h
 mov dword [font+1dh],11223344h
 mov dword [font+52h],55667788h
 push ds
 push word font
 call far [method]
 mov [answer],ax
 mov ax,[font]
 mov [flags],ax
 mov eax,[font+1dh]
 mov [first],eax
 mov eax,[font+52h]
 mov [second],eax
 mov ax,[font+5ah]
 mov [cleared],ax
 push word [handle]
 API 1,20
 or ax,dx
 mov [remaining],ax
 push ds
 push word font
 call far [method]
 mov [repeated],ax
 mov ax,[sp0]
 sub ax,sp
 mov [balance],ax
 push word 0
 API 3,52
 mov [dc],ax
 ; Query precedes validation of the DC.
 push word 0
 push ds
 push word logical
 push dword 0
 push dword 0
 push word 0
 API 3,315
 mov [need],ax
 ; Realization does validate the DC.
 push word 0
 push ds
 push word logical
 push ds
 push word xform
 push ds
 push word font
 push word 0
 API 3,315
 mov [invalid],ax
 push word [dc]
 push ds
 push word logical
 push ds
 push word xform
 push ds
 push word font
 push word 0
 API 3,315
 mov [realized],ax
 push ds
 push word font
 push word 32
 push word 126
 push ds
 push word widths
 API 3,303
 mov [widthresult],ax
 push ds
 push word font
 push word 0
 API 3,304
 mov [contextresult],ax
 push word [dc]
 push ds
 push word font
 push word 65
 push word 1
 push ds
 push word bitmap
 push dword 512
 push ds
 push word metrics
 API 3,305
 mov [glyphbytes],ax
 mov [glyphbytes+2],dx
 ; The engine context's angle persists through EngineGetGlyphBmp.
 push ds
 push word font
 push word 900
 API 3,304
 mov [context900],ax
 push word [dc]
 push ds
 push word font
 push word 65
 push word 1
 push ds
 push word bitmap900
 push dword 512
 push ds
 push word metrics900
 API 3,305
 mov [bytes900],ax
 mov [bytes900+2],dx
 ; The engine context's angle persists through EngineGetGlyphBmp.
 push ds
 push word font
 push word 450
 API 3,304
 mov [context450],ax
 push word [dc]
 push ds
 push word font
 push word 65
 push word 1
 push ds
 push word bitmap450
 push dword 512
 push ds
 push word metrics450
 API 3,305
 mov [bytes450],ax
 mov [bytes450+2],dx
 push ds
 push word font
 push ds
 push word widthtext
 push word 3
 push ds
 push word stringwidths
 API 3,316
 cmp ax,1
 je widthsok
 mov word [stringwidths],0ffffh
widthsok:
 push ds
 push word engineout
 push word 0
 API 1,83
 mov si,ax
 push si
 push ds
 push word need
 push word engineend-need
 API 1,86
 push si
 API 1,81
 push word [dc]
 API 3,68
finish:
 push ds
 push word fout
 push word 0
 API 1,83
 mov si,ax
 push si
 push ds
 push word resolved
 push word 22
 API 1,86
 push si
 API 1,81
 mov ax,4c00h
 API 1,102
 jmp $
code_end:
section data
data_start:
 times 16 db 0
sp0 dw 0
method dd 0
handle dw 0
resolved dw 0
answer dw 0
flags dw 0
first dd 0
second dd 0
cleared dw 0ffffh
remaining dw 0ffffh
repeated dw 0
balance dw 0ffffh
dc dw 0
logical dw -16,0,0,0,400
 db 0,0,0,0,0,0,0,0
 db 'Arial',0
 times 26 db 0
engineout db 'C:\ENGINE.OUT',0
need dw 0
invalid dw 0ffffh
realized dw 0
widthresult dw 0
contextresult dw 0
glyphbytes dd 0
font times 256 db 0
xform times 32 db 0
widths times 95 dw 0
metrics times 24 db 0
bitmap times 512 db 0
context900 dw 0
bytes900 dd 0
metrics900 times 24 db 0
bitmap900 times 512 db 0
context450 dw 0
bytes450 dd 0
metrics450 times 24 db 0
bitmap450 times 512 db 0
stringwidths times 3 dw 0
engineend:
widthtext db 'A W'
dll db 'GDI.EXE',0
name db 'EngineDeleteFont',0
fout db 'C:\FONT16.OUT',0
 times 512 db 0
data_end:
section fix
fix_end:
