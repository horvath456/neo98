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
%macro LOOKUP 2
 push word [module]
 push ds
 push word %1
 API 1,50
 mov [%2],ax
 mov [%2+2],dx
 or ax,dx
 jz finish
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
 mov [module],ax
 cmp ax,32
 jb finish
 LOOKUP open_name,open_proc
 LOOKUP primary_name,primary_proc
 LOOKUP close_name,close_proc
 LOOKUP offscreen_name,offscreen_proc
 LOOKUP overlay_name,overlay_proc
 LOOKUP enum_name,enum_proc
 push word [module]
 push word 0
 push word 32
 API 1,50
 mov [begin_proc],ax
 mov [begin_proc+2],dx
 or ax,dx
 jz finish
 push word [module]
 push word 0
 push word 31
 API 1,50
 mov [end_proc],ax
 mov [end_proc+2],dx
 or ax,dx
 jz finish
 mov word [resolved],1
 call far [open_proc]
 mov [provider],ax
 push ax
 push ds
 push word surface
 call far [primary_proc]
 mov [answer],ax
 or ax,ax
 jnz close_provider
 les bx,[surface]
 mov ax,[es:bx+18h]
 mov [width],ax
 mov [rect+4],ax
 mov ax,[es:bx+1ch]
 mov [height],ax
 mov [rect+6],ax
 mov ax,[es:bx+20h]
 mov [stride],ax
 mov ax,[es:bx+24h]
 mov [depth],ax
 mov ax,[es:bx+2ch]
 mov [screen],ax
 push word [surface+2]
 push word [surface]
 push word 0
 push word 0
 push word [width]
 push word [height]
 call far [begin_proc]
 mov [public_begin],ax
 push word [surface+2]
 push word [surface]
 call far [end_proc]
 mov [public_end],ax
 les bx,[surface]
 push word [surface+2]
 push word [surface]
 push ds
 push word rect
 call far [es:bx+3ch]
 mov [begin_answer],ax
 mov ax,[screen]
 mov es,ax
 mov edi,70000
 mov byte [es:edi],5ah
 xor ax,ax
 mov al,[es:edi]
 mov [pixel],ax
 les bx,[surface]
 push word [surface+2]
 push word [surface]
 call far [es:bx+40h]
 mov [end_answer],ax
 les bx,[surface]
 push word [surface+2]
 push word [surface]
 call far [es:bx+44h]
 mov [destroy_answer],ax
 les bx,[surface]
 push word [surface+2]
 push word [surface]
 push ds
 push word rect
 call far [es:bx+3ch]
 mov [expired_answer],ax
close_provider:
 push word [provider]
 call far [close_proc]
 times 19 push word 0
 call far [offscreen_proc]
 mov [offscreen_answer],ax
 times 5 push word 0
 call far [overlay_proc]
 mov [overlay_answer],ax
 times 9 push word 0
 call far [enum_proc]
 mov [enum_answer],ax
 mov ax,[sp0]
 sub ax,sp
 mov [balance],ax
finish:
 push ds
 push word fout
 push word 0
 API 1,83
 mov si,ax
 push si
 push ds
 push word resolved
 push word 42
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
module dw 0
open_proc dd 0
primary_proc dd 0
close_proc dd 0
offscreen_proc dd 0
overlay_proc dd 0
enum_proc dd 0
begin_proc dd 0
end_proc dd 0
resolved dw 0
provider dw 0
answer dw 0
surface dd 12345678h
balance dw 0ffffh
offscreen_answer dw 0
overlay_answer dw 0
enum_answer dw 0
width dw 0
height dw 0
stride dw 0
depth dw 0
screen dw 0
begin_answer dw 0ffffh
pixel dw 0
end_answer dw 0ffffh
destroy_answer dw 0ffffh
expired_answer dw 0
public_begin dw 0ffffh
public_end dw 0ffffh
rect dw 0,0,0,0
dll db 'DCIMAN.DLL',0
open_name db 'DCIOpenProvider',0
primary_name db 'DCICreatePrimary',0
close_name db 'DCICloseProvider',0
offscreen_name db 'DCICreateOffscreen',0
overlay_name db 'DCICreateOverlay',0
enum_name db 'DCIEnum',0
fout db 'C:\DCI16.OUT',0
 times 512 db 0
data_end:
section fix
fix_end:
