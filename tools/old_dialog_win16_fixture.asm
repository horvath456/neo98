; SPDX-License-Identifier: GPL-2.0-or-later
; Calls of KERNEL and USER the old-style file dialog of a 16-bit COMMDLG
; makes, by a program of its own. Resource 1 of type RT_RCDATA holds
; "ORIGINAL".
;
; a LoadResource, LockResource, the first byte changed to 'X'; a second
;   LoadResource answers the same handle and the block still has the 'X'
; b FreeResource twice, the last load given back; the next LoadResource
;   has the bytes of the file again
; c INT 21h AH=32h of drive C: (DL 3): AL 0, DS:BX on a drive parameter
;   block of drive 2 with 512 bytes a sector and at least one FAT
; d INT 21h AH=32h of drive Y:, which is not there: AL FFh
; e GetClassLong(GCL_WNDPROC) of a list box: CallWindowProc of that
;   procedure adds a string to the list (LB_ADDSTRING answers 0, then
;   LB_GETCOUNT 1)
; f SetClassLong(GCL_WNDPROC) of the program's own class to procedure B
;   answers procedure A, the far pointer the class was registered with;
;   GetClassLong then answers B
; g a window of that class made now has procedure B, which hands each
;   message on to A through CallWindowProc: WM_USER+5, which A answers
;   with 41h and B adds one to, answers 42h
;
; Each step puts its letter into C:\OLDDLG16.TXT as soon as it is done,
; "-" when it failed, so a step that stops the program leaves the letters
; before it. The program ends through DOS3Call 4Ch.
; tools/test_win16_old_dialog_host.py.
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
%macro DOS 0
        API 1, 102                      ; DOS3Call
%endmacro
; MARK letter: AL is nonzero when the step held; writes the letter or '-'.
%macro MARK 1
        mov ah, %1
        call mark
%endmacro

section code
code_start:
        API 1, 91                       ; InitTask
        mov [instance], di
        push di
        API 2, 5                        ; InitApp
        push ds
        push word fout
        push word 0
        API 1, 83                       ; _lcreat
        mov [output], ax

        ; a: a loaded resource keeps what the program wrote while it is loaded
        push word [instance]
        push word 0
        push word 1                     ; MAKEINTRESOURCE(1)
        push word 0
        push word 10                    ; RT_RCDATA
        API 1, 60                       ; FindResource
        mov [found], ax
        push word [instance]
        push ax
        API 1, 61                       ; LoadResource
        mov [first], ax
        push ax
        API 1, 62                       ; LockResource
        mov es, dx
        mov bx, ax
        xor si, si
        cmp byte [es:bx], 'O'
        jne .a_second
        inc si
        mov byte [es:bx], 'X'
.a_second:
        push word [first]
        API 1, 19                       ; GlobalUnlock
        push word [instance]
        push word [found]
        API 1, 61                       ; LoadResource
        cmp ax, [first]
        je .a_same
        xor si, si
.a_same:
        push ax
        API 1, 62                       ; LockResource
        mov es, dx
        mov bx, ax
        cmp byte [es:bx], 'X'
        je .a_kept
        xor si, si
.a_kept:
        push word [first]
        API 1, 19                       ; GlobalUnlock
        mov ax, si
        MARK 'a'

        ; b: after the last FreeResource the bytes come from the file again
        push word [first]
        API 1, 63                       ; FreeResource
        push word [first]
        API 1, 63                       ; FreeResource, the last load
        push word [instance]
        push word [found]
        API 1, 61                       ; LoadResource
        mov [first], ax
        xor si, si
        or ax, ax
        jz .b_end
        push ax
        API 1, 62                       ; LockResource
        mov es, dx
        mov bx, ax
        cmp word [es:bx], 'OR'
        jne .b_unlock
        cmp word [es:bx + 6], 'AL'
        jne .b_unlock
        inc si
.b_unlock:
        push word [first]
        API 1, 19                       ; GlobalUnlock
        push word [first]
        API 1, 63                       ; FreeResource
.b_end:
        mov ax, si
        MARK 'b'

        ; c: the drive parameter block of C:
        push ds
        mov dl, 3
        mov ah, 32h
        DOS
        xor si, si
        or al, al
        jnz .c_end
        cmp byte [bx], 2                ; the drive, 0 for A
        jne .c_end
        cmp word [bx + 2], 512          ; bytes a sector
        jne .c_end
        cmp byte [bx + 8], 0            ; the number of FATs
        je .c_end
        inc si
.c_end: pop ds
        mov ax, si
        MARK 'c'

        ; d: a drive that is not there
        push ds
        mov dl, 25
        mov ah, 32h
        DOS
        pop ds
        cmp al, 0ffh
        sete al
        MARK 'd'

        ; e: the class procedure of LISTBOX, called through CallWindowProc
        mov ax, [instance]
        mov [wc_instance], ax
        mov word [wc_procedure], procedure_a
        mov [wc_procedure + 2], cs
        mov [wc_class + 2], ds
        push ds
        push word wc
        API 2, 57                       ; RegisterClass
        call create
        mov [window], ax
        push ds
        push word listbox_class
        push word 0
        push word 0                     ; no title
        push word 5000h                 ; WS_CHILD WS_VISIBLE
        push word 0
        push word 0                     ; x
        push word 0                     ; y
        push word 100                   ; width
        push word 100                   ; height
        push word [window]
        push word 1                     ; identifier
        push word [instance]
        push word 0
        push word 0                     ; no creation parameter
        API 2, 41                       ; CreateWindow
        mov [listbox], ax
        push ax
        push word -24                   ; GCL_WNDPROC
        API 2, 131                      ; GetClassLong
        push dx
        push ax
        push word [listbox]
        push word 401h                  ; LB_ADDSTRING
        push word 0
        push ds
        push word item_text
        API 2, 122                      ; CallWindowProc
        mov [added], ax
        push word [listbox]
        push word 40ch                  ; LB_GETCOUNT
        push word 0
        push word 0
        push word 0
        API 2, 111                      ; SendMessage
        xor si, si
        cmp ax, 1
        jne .e_end
        cmp word [added], 0
        jne .e_end
        inc si
.e_end: mov ax, si
        MARK 'e'

        ; f: SetClassLong and GetClassLong of the procedure of a 16-bit class
        push word [window]
        push word -24                   ; GCL_WNDPROC
        push cs
        push word procedure_b
        API 2, 132                      ; SetClassLong
        mov [old_proc], ax
        mov [old_proc + 2], dx
        xor si, si
        cmp ax, procedure_a
        jne .f_end
        mov ax, cs
        cmp dx, ax
        jne .f_end
        push word [window]
        push word -24
        API 2, 131                      ; GetClassLong
        cmp ax, procedure_b
        jne .f_end
        mov ax, cs
        cmp dx, ax
        jne .f_end
        inc si
.f_end: mov ax, si
        MARK 'f'

        ; g: a new window of the class runs B, and B hands on to A
        call create
        push ax
        push word 405h                  ; WM_USER+5
        push word 0
        push word 0
        push word 0
        API 2, 111                      ; SendMessage
        cmp ax, 42h
        sete al
        MARK 'g'

        push word [output]
        API 1, 81                       ; _lclose
        mov ax, 4c00h
        DOS
        jmp $

; create: a hidden window of the program's class in AX.
create:
        push ds
        push word class
        push ds
        push word class                 ; the title
        push word 00cfh                 ; WS_OVERLAPPEDWINDOW
        push word 0
        push word 100                   ; x
        push word 80                    ; y
        push word 320                   ; width
        push word 200                   ; height
        push word 0                     ; no parent
        push word 0                     ; no menu
        push word [instance]
        push word 0
        push word 0                     ; no creation parameter
        API 2, 41                       ; CreateWindow
        ret

; LRESULT FAR PASCAL procedure_a(HWND, UINT, WPARAM, LPARAM): [bp+14]
; the window, [bp+12] the message, [bp+10] wParam, [bp+8] and [bp+6]
; lParam. WM_USER+5 answers 41h; the rest goes to DefWindowProc.
procedure_a:
        push bp
        mov bp, sp
        push ds
        push ss
        pop ds
        cmp word [bp + 12], 405h
        jne .default
        mov ax, 41h
        xor dx, dx
        jmp .done
.default:
        push word [bp + 14]
        push word [bp + 12]
        push word [bp + 10]
        push word [bp + 8]
        push word [bp + 6]
        API 2, 107                      ; DefWindowProc
.done:  pop ds
        pop bp
        retf 10

; procedure_b: every message to the procedure SetClassLong gave back, and
; one more to the answer of WM_USER+5.
procedure_b:
        push bp
        mov bp, sp
        push ds
        push ss
        pop ds
        push word [old_proc + 2]
        push word [old_proc]
        push word [bp + 14]
        push word [bp + 12]
        push word [bp + 10]
        push word [bp + 8]
        push word [bp + 6]
        API 2, 122                      ; CallWindowProc
        cmp word [bp + 12], 405h
        jne .done
        inc ax
.done:  pop ds
        pop bp
        retf 10

; mark: AH the letter, AL nonzero when the step held. One byte to the file.
mark:
        or al, al
        jnz .good
        mov ah, '-'
.good:  mov [letter], ah
        push word [output]
        push ds
        push word letter
        push word 1
        API 1, 86                       ; _lwrite
        ret
code_end:

section data
data_start:
        times 16 db 0                   ; the words of the local heap and the stack
instance dw 0
output   dw 0
found    dw 0
first    dw 0
window   dw 0
listbox  dw 0
added    dw 0
old_proc dd 0
letter   db 0, 0
wc:      dw 0                           ; style
wc_procedure dd 0
         dw 0, 0                        ; no class or window extra bytes
wc_instance dw 0
         dw 0, 0                        ; no icon, no cursor
         dw 6                           ; COLOR_WINDOW + 1
         dd 0                           ; no class menu
wc_class dw class, 0
class    db 'OldDialog16', 0
listbox_class db 'LISTBOX', 0
item_text db 'Item', 0
fout     db 'C:\OLDDLG16.TXT', 0
data_end:

section fix
fix_end:
