; SPDX-License-Identifier: GPL-2.0-or-later
; direct ADC, timed DMA, mask/unmask, auto-init, halt/continue.
bits 16
org 100h
start:
 push cs
 pop ds
 mov ax,350dh
 int 21h
 mov [oldirq],bx
 mov [oldirq+2],es
 mov dx,irq
 mov ax,250dh
 int 21h
 in al,21h
 mov [oldmask],al
 and al,0dfh
 out 21h,al
 mov dx,226h
 mov al,1
 out dx,al
 mov cx,100
resetwait: in al,dx
 loop resetwait
 xor al,al
 out dx,al
 mov cx,0ffffh
resetread:
 mov dx,22eh
 in al,dx
 test al,80h
 jnz resetok
 loop resetread
 mov al,1
 jmp finish
resetok:
 mov dx,22ah
 in al,dx
 cmp al,0aah
 jne resetread
 ; Direct samples one millisecond apart.
 mov di,direct
 mov cx,256
directloop:
 push cx
 mov al,20h
 call dsp
 mov dx,22ah
 in al,dx
 mov [di],al
 inc di
 mov ax,8600h
 xor cx,cx
 mov dx,1000
 int 15h
 pop cx
 loop directloop
 mov dx,directname
 mov si,direct
 mov cx,256
 call write
 ; 11111 Hz, 4096 bytes, DMA channel 1.
 mov al,40h
 call dsp
 mov al,166
 call dsp
 mov ax,cs
 add ax,1000h
 and ax,0f000h
 mov [bufferseg],ax
 mov es,ax
 xor di,di
 mov cx,4096
 mov al,55h
 rep stosb
 mov al,45h
 call dma
 mov word [irqs],0
 mov al,24h
 call dsp
 mov al,0ffh
 call dsp
 mov al,0fh
 call dsp
 cmp word [irqs],0
 je timed
 mov al,21
 jmp finish
timed:
 ; Mask DMA for three ticks: no writes and no completion while masked.
 mov al,5
 out 0ah,al
 call count
 mov [masked],ax
 mov cx,3
 call ticks
 call count
 cmp ax,[masked]
 je unmask
 mov al,22
 jmp finish
unmask:
 mov al,1
 out 0ah,al
 xor ax,ax
 int 1ah
 mov [begintick],dx
 mov bx,1
 call waitirq
 xor ax,ax
 int 1ah
 sub dx,[begintick]
 mov [elapsed],dx
 cmp dx,5
 jae timeok
 mov al,23
 jmp finish
timeok:
 mov dx,singlename
 mov si,0
 mov cx,4096
 call writebuffer
 ; Auto-init block 1024 bytes, DMA circular buffer 4096 bytes.
 mov al,55h
 call dma
 mov al,48h
 call dsp
 mov al,0ffh
 call dsp
 mov al,3
 call dsp
 mov word [irqs],0
 mov al,2ch
 call dsp
 mov bx,2
 call waitirq
 mov al,0d0h
 call dsp
 call count
 mov [masked],ax
 mov cx,3
 call ticks
 call count
 cmp ax,[masked]
 je resume
 mov al,24
 jmp finish
resume:
 mov al,0d4h
 call dsp
%ifdef LONG_CAPTURE
 mov bx,140
%else
 mov bx,5
%endif
 call waitirq
 mov al,0dah
 call dsp
 mov bx,[irqs]
 inc bx
 call waitirq
 mov ax,[irqs]
 mov [finalirq],ax
 mov cx,3
 call ticks
 mov ax,[irqs]
 cmp ax,[finalirq]
 je stopped
 mov al,25
 jmp finish
stopped:
 mov dx,autoname
 xor si,si
 mov cx,4096
 call writebuffer
 mov dx,headername
 mov si,elapsed
 mov cx,4
 call write
 xor al,al
finish:
 mov [exitcode],al
 mov al,0d0h
 call dsp
 mov al,5
 out 0ah,al
 mov al,[oldmask]
 out 21h,al
 lds dx,[oldirq]
 mov ax,250dh
 int 21h
 push cs
 pop ds
 mov al,[exitcode]
 mov ah,4ch
 int 21h
irq:
 push ax
 push dx
 inc word [cs:irqs]
 mov dx,22eh
 in al,dx
 mov al,20h
 out 20h,al
 pop dx
 pop ax
 iret
dsp:
 push ax
 mov dx,22ch
dspwait:
 in al,dx
 test al,80h
 jnz dspwait
 pop ax
 out dx,al
 ret
dma:
 mov ah,al
 mov al,5
 out 0ah,al
 xor al,al
 out 0ch,al
 mov al,ah
 out 0bh,al
 xor al,al
 out 02h,al
 out 02h,al
 mov ax,[bufferseg]
 shr ax,12
 out 83h,al
 mov al,0ffh
 out 03h,al
 mov al,0fh
 out 03h,al
 mov al,1
 out 0ah,al
 ret
count:
 xor al,al
 out 0ch,al
 in al,03h
 mov ah,al
 in al,03h
 xchg al,ah
 ret
ticks:
 push cx
 xor ax,ax
 int 1ah
 mov si,dx
 pop cx
 add si,cx
tickwait:
 sti
 hlt
 xor ax,ax
 int 1ah
 cmp dx,si
 jb tickwait
 ret
waitirq:
%ifdef LONG_CAPTURE
 mov cx,400
%else
 mov cx,200
%endif
waitloop:
 cmp [irqs],bx
 jae waitdone
 push cx
 mov cx,1
 call ticks
 pop cx
 loop waitloop
 mov al,26
 jmp finish
waitdone: ret
writebuffer:
 push ds
 push cx
 xor cx,cx
 mov ah,3ch
 int 21h
 mov bx,ax
 pop cx
 mov ds,[bufferseg]
 mov dx,si
 mov ah,40h
 int 21h
 mov ah,3eh
 int 21h
 pop ds
 ret
write:
 push cx
 xor cx,cx
 mov ah,3ch
 int 21h
 mov bx,ax
 pop cx
 mov dx,si
 mov ah,40h
 int 21h
 mov ah,3eh
 int 21h
 ret
oldirq dd 0
oldmask db 0
exitcode db 0
bufferseg dw 0
irqs dw 0
masked dw 0
begintick dw 0
elapsed dw 0
finalirq dw 0
directname db 'C:\ADC20.RAW',0
singlename db 'C:\ADC24.RAW',0
autoname db 'C:\ADC2C.RAW',0
headername db 'C:\ADC.OUT',0
direct times 256 db 0
