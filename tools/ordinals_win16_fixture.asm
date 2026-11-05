; SPDX-License-Identifier: GPL-2.0-or-later
; A sample of the gates of the pass over the whole export tables
;
; one 16-bit program that calls them and writes
; down what came back. tools/test_win16_ordinals_host.py reads the words.
;
; KERNEL.79 [AnsiUpper] the unnamed string helper
; USER.14 GetTimerResolution DX:AX 1000
; USER.517 WNetAddConnection WN_NOT_SUPPORTED
; USER.76 PtInRect x is the low word
; USER.373 SubtractRect three RECTs widened and back
; GDI.483 MoveToEx, GDI.78 GetCurrentPosition the POINT out, the pair in DX:AX
; GDI.63 CreatePolygonRgn, GDI.134 GetRgnBox a POINT array in, a RECT out
; USER.54 EnumWindows the far callback, word and LONG
; USER.475 SetScrollInfo, USER.476 GetScrollInfo SCROLLINFO of seven words
; USER.415 CreatePopupMenu, USER.441 InsertMenuItem, USER.443 GetMenuItemInfo
; MENUITEMINFO of 26 bytes
; USER.259 to 261 the deferred positioning the HDWP as a word
; SHELL.37 DoEnvironmentSubst SHELL32 on the flat stack
;
; Every call is checked for its word count as well: the stack pointer after
; the last one has to be the one before the first. The New Executable is made
; as for tools/layer_win16_fixture.asm, with GDI as module 3 and SHELL as 4.
        bits 16
section header start=0
        dw code_end - code_start, data_end - data_start, (fix_end - fix_start) / 6
section code vstart=0 follows=header align=1
section data vstart=0 follows=code align=1
section fix vstart=0 follows=data align=1
fix_start:

; A far call the loader fills in: module 1 is KERNEL, 2 is USER, 3 is GDI,
; 4 is SHELL.
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

section code
code_start:
        API 1, 91                       ; InitTask
        mov [instance], di
        push di
        API 2, 5                        ; InitApp
        cld
        mov [sp0], sp
        mov di, res

        ; KERNEL.79, AnsiUpper of a far string, in place
        push ds
        push word text
        API 1, 79
        sub ax, text                    ; the offset of the string comes back
        KEEP

        ; USER.14 GetTimerResolution
        API 2, 14
        KEEP
        mov ax, dx
        KEEP

        ; USER.517 WNetAddConnection(net, password, local)
        push ds
        push word text
        push ds
        push word text
        push ds
        push word text
        API 2, 517
        KEEP

        ; USER.76 PtInRect(rect, point): the point is pushed y first, x
        ; last, so x is the low word
        push ds
        push word tall
        push word 50                    ; y
        push word 5                     ; x
        API 2, 76
        KEEP
        push ds
        push word tall
        push word 5                     ; y
        push word 50                    ; x
        API 2, 76
        KEEP

        ; USER.373 SubtractRect(out, a, b)
        push ds
        push word rout
        push ds
        push word ra
        push ds
        push word rb
        API 2, 373
        KEEP
        mov si, rout
        mov cx, 4
.copy_rect:
        lodsw
        KEEP
        loop .copy_rect

        ; GDI.52 CreateCompatibleDC(0)
        push word 0
        API 3, 52
        mov [hdc], ax
        KEEPNZ
        ; GDI.483 MoveToEx(hdc, 10, 20, null)
        push word [hdc]
        push word 10
        push word 20
        push word 0
        push word 0
        API 3, 483
        KEEP
        ; GDI.483 MoveToEx(hdc, 30, -5, point): the previous point
        push word [hdc]
        push word 30
        push word -5
        push ds
        push word point
        API 3, 483
        KEEP
        mov ax, [point]
        KEEP
        mov ax, [point + 2]
        KEEP
        ; GDI.78 GetCurrentPosition(hdc): DX:AX is y:x
        push word [hdc]
        API 3, 78
        KEEP
        mov ax, dx
        KEEP

        ; GDI.63 CreatePolygonRgn(points, 4, ALTERNATE), GDI.134 GetRgnBox
        push ds
        push word polygon
        push word 4
        push word 1
        API 3, 63
        mov [region], ax
        KEEPNZ
        push word [region]
        push ds
        push word rbox
        API 3, 134
        KEEP
        mov si, rbox
        mov cx, 4
.copy_box:
        lodsw
        KEEP
        loop .copy_box

        ; USER.41 CreateWindow("STATIC", "", WS_POPUP | WS_VSCROLL, 100, 100,
        ; 50, 50, 0, 0, instance, 0): a window to enumerate and to move
        push ds
        push word cstatic
        push ds
        push word empty
        push word 8020h
        push word 0
        push word 100
        push word 100
        push word 50
        push word 50
        push word 0
        push word 0
        push word [instance]
        push word 0
        push word 0
        API 2, 41
        mov [hwnd], ax
        KEEPNZ

        ; USER.54 EnumWindows(procedure, 12345678h)
        push cs
        push word enum_proc
        push word 1234h
        push word 5678h
        API 2, 54
        KEEP
        mov ax, [seen]
        KEEP
        mov ax, [seen_low]
        KEEP
        mov ax, [seen_high]
        KEEP

        ; USER.475 SetScrollInfo(hwnd, SB_VERT, info, TRUE): range 3 to 70,
        ; position 9; USER.476 GetScrollInfo with SIF_ALL into a second one
        push word [hwnd]
        push word 1
        push ds
        push word sinfo
        push word 1
        API 2, 475
        KEEP
        push word [hwnd]
        push word 1
        push ds
        push word sback
        API 2, 476
        KEEP
        mov ax, [sback + 4]
        KEEP
        mov ax, [sback + 6]
        KEEP
        mov ax, [sback + 10]
        KEEP

        ; USER.415 CreatePopupMenu, USER.441 InsertMenuItem(menu, 0, TRUE,
        ; "Item" with id 123h), USER.443 GetMenuItemInfo into a buffer
        API 2, 415
        mov [menu], ax
        KEEPNZ
        mov [item + 20], word mtext
        mov [item + 22], ds
        push word [menu]
        push word 0
        push word 1
        push ds
        push word item
        API 2, 441
        KEEP
        mov [iback + 20], word mbuffer
        mov [iback + 22], ds
        push word [menu]
        push word 0
        push word 1
        push ds
        push word iback
        API 2, 443
        KEEP
        mov ax, [iback + 8]
        KEEP
        mov ax, [iback + 24]
        KEEP

        ; USER.259 BeginDeferWindowPos(1), USER.260 DeferWindowPos(defer,
        ; hwnd, 0, 5, 6, 30, 40, SWP_NOZORDER | SWP_NOACTIVATE), USER.261
        ; EndDeferWindowPos, then USER.32 GetWindowRect
        push word 1
        API 2, 259
        mov [defer], ax
        KEEPNZ
        push word [defer]
        push word [hwnd]
        push word 0
        push word 5
        push word 6
        push word 30
        push word 40
        push word 14h
        API 2, 260
        mov [defer], ax
        KEEPNZ
        push word [defer]
        API 2, 261
        KEEP
        push word [hwnd]
        push ds
        push word rwin
        API 2, 32
        mov si, rwin
        mov cx, 4
.copy_window:
        lodsw
        KEEP
        loop .copy_window

        ; SHELL.37 DoEnvironmentSubst(text, size): AX the length, DX the success
        push ds
        push word envtext
        push word 32
        API 4, 37
        KEEP
        mov ax, dx
        KEEP

        ; The stack pointer against the one before the first call.
        mov ax, [sp0]
        sub ax, sp
        KEEP

        ; C:\ORD16OUT.TXT: the result words, the string and the menu text.
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
        mov ax, 4c00h
        API 1, 102                      ; DOS3Call
        jmp $

; BOOL FAR PASCAL EnumProc(HWND hwnd, LPARAM parameter): [bp+0Ah] the window,
; [bp+8] and [bp+6] the parameter. DS is the task's, as USER hands it.
enum_proc:
        push bp
        mov bp, sp
        mov ax, [bp + 0ah]
        cmp ax, [hwnd]
        jne .other
        mov word [seen], 1
        mov ax, [bp + 6]
        mov [seen_low], ax
        mov ax, [bp + 8]
        mov [seen_high], ax
.other:
        mov ax, 1
        pop bp
        retf 6
code_end:

section data
data_start:
        times 16 db 0                   ; the words of the local heap and the stack
instance dw 0
sp0     dw 0
hdc     dw 0
region  dw 0
hwnd    dw 0
menu    dw 0
defer   dw 0
seen    dw 0
seen_low dw 0
seen_high dw 0
tall    dw 0, 0, 10, 100
ra      dw 0, 0, 100, 100
rb      dw 0, 0, 100, 40
rout    dw -1, -1, -1, -1
rbox    dw -1, -1, -1, -1
rwin    dw -1, -1, -1, -1
point   dw -1, -1
polygon dw 2, 3, 12, 3, 12, 8, 2, 8
; SCROLLINFO: cbSize, fMask, nMin, nMax, nPage, nPos, nTrackPos
sinfo   dw 14, 5, 3, 70, 0, 9, 0        ; SIF_RANGE | SIF_POS
sback   dw 14, 17h, -1, -1, -1, -1, -1  ; SIF_ALL
; MENUITEMINFO: cbSize, fMask, fType, fState, wID, hSubMenu, hbmpChecked,
; hbmpUnchecked, dwItemData, dwTypeData, cch
item    dw 26, 12h, 0, 0, 123h, 0, 0, 0
        dd 0, 0
        dw 4
iback   dw 26, 12h, -1, -1, -1, -1, -1, -1
        dd 0, 0
        dw 16
cstatic db 'STATIC', 0
empty   db 0
mtext   db 'Item', 0
fout    db 'C:\ORD16OUT.TXT', 0
res     times 64 dw 0
text    db 'abc', 0
mbuffer times 16 db '.'
envtext db '%WINDIR%\X', 0
        times 32 - ($ - envtext) db 0
out_end:
        times 512 db 0
data_end:

section fix
fix_end:
