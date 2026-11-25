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
; h _lopen of a file that is not there answers HFILE_ERROR, and INT 21h
;   AH=59h then answers error 2, file not found
; i _lcreat in a folder that is not there answers HFILE_ERROR, and INT
;   21h AH=59h then answers error 3, path not found
; j SendMessage of WM_MEASUREITEM to the program's window with a
;   MEASUREITEMSTRUCT of the program's own: procedure A sees every field
;   as it was sent, and the width and height it sets come back
; k the same with a DRAWITEMSTRUCT whose rectangle starts left of zero:
;   A sees every field and answers TRUE
; l the same with a COMPAREITEMSTRUCT: A sees every field and answers -1
;   for the smaller item data first
; m the same with a DELETEITEMSTRUCT: A sees every field and answers TRUE
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

        ; h: the extended error of a failed _lopen
        push ds
        push word missing_file
        push word 0                     ; OF_READ
        API 1, 85                       ; _lopen
        mov si, ax
        xor bx, bx
        mov ah, 59h
        DOS
        cmp si, -1
        jne .h_bad
        cmp ax, 2                       ; ERROR_FILE_NOT_FOUND
        sete al
        jmp .h_end
.h_bad: xor al, al
.h_end: MARK 'h'

        ; i: the extended error of a failed _lcreat
        push ds
        push word missing_folder
        push word 0
        API 1, 83                       ; _lcreat
        mov si, ax
        xor bx, bx
        mov ah, 59h
        DOS
        cmp si, -1
        jne .i_bad
        cmp ax, 3                       ; ERROR_PATH_NOT_FOUND
        sete al
        jmp .i_end
.i_bad: xor al, al
.i_end: MARK 'i'

        ; j to m: owner-draw structures of the program's own
        mov ax, [listbox]
        mov [dis + 10], ax
        mov [cis + 4], ax
        mov [deis + 6], ax
        push word [window]
        push word 2ch                   ; WM_MEASUREITEM
        push word 7
        push ds
        push word mis
        API 2, 111                      ; SendMessage
        xor si, si
        cmp byte [seen_measure], 1
        jne .j_end
        cmp word [mis + 6], 123         ; itemWidth
        jne .j_end
        cmp word [mis + 8], 45          ; itemHeight
        jne .j_end
        inc si
.j_end: mov ax, si
        MARK 'j'
        push word [window]
        push word 2bh                   ; WM_DRAWITEM
        push word 7
        push ds
        push word dis
        API 2, 111                      ; SendMessage
        cmp ax, 1
        jne .k_bad
        mov al, [seen_draw]
        jmp .k_end
.k_bad: xor al, al
.k_end: MARK 'k'
        push word [window]
        push word 39h                   ; WM_COMPAREITEM
        push word 7
        push ds
        push word cis
        API 2, 111                      ; SendMessage
        cmp ax, -1
        jne .l_bad
        mov al, [seen_compare]
        jmp .l_end
.l_bad: xor al, al
.l_end: MARK 'l'
        push word [window]
        push word 2dh                   ; WM_DELETEITEM
        push word 7
        push ds
        push word deis
        API 2, 111                      ; SendMessage
        cmp ax, 1
        jne .m_bad
        mov al, [seen_delete]
        jmp .m_end
.m_bad: xor al, al
.m_end: MARK 'm'

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
; lParam. WM_USER+5 answers 41h. An owner-draw message notes whether its
; structure has the bytes of the one the program sent; WM_MEASUREITEM
; sets width 123 and height 45, WM_COMPAREITEM answers -1 and the other two
; TRUE. The rest goes to DefWindowProc.
procedure_a:
        push bp
        mov bp, sp
        push ds
        push ss
        pop ds
        push si
        push di
        push es
        mov ax, [bp + 12]
        cmp ax, 405h
        je .user
        mov si, mis
        mov cx, 12
        mov bx, seen_measure
        cmp ax, 2ch                     ; WM_MEASUREITEM
        je .structure
        mov si, dis
        mov cx, 26
        mov bx, seen_draw
        cmp ax, 2bh                     ; WM_DRAWITEM
        je .structure
        mov si, cis
        mov cx, 18
        mov bx, seen_compare
        cmp ax, 39h                     ; WM_COMPAREITEM
        je .structure
        mov si, deis
        mov cx, 12
        mov bx, seen_delete
        cmp ax, 2dh                     ; WM_DELETEITEM
        je .structure
        push word [bp + 14]
        push word [bp + 12]
        push word [bp + 10]
        push word [bp + 8]
        push word [bp + 6]
        API 2, 107                      ; DefWindowProc
        jmp .done
.structure:
        les di, [bp + 6]
        cld
        repe cmpsb
        jne .answer
        mov byte [bx], 1
.answer:
        les di, [bp + 6]
        mov ax, 1
        cmp word [bp + 12], 2ch
        jne .compare
        mov word [es:di + 6], 123       ; itemWidth
        mov word [es:di + 8], 45        ; itemHeight
.compare:
        cmp word [bp + 12], 39h
        jne .signed
        mov ax, -1
.signed:
        cwd
        jmp .done
.user:  mov ax, 41h
        xor dx, dx
.done:  pop es
        pop di
        pop si
        pop ds
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
seen_measure db 0
seen_draw db 0
seen_compare db 0
seen_delete db 0
; MEASUREITEMSTRUCT: CtlType (ODT_LISTBOX), CtlID, itemID, itemWidth,
; itemHeight, itemData.
mis      dw 2, 7, -1, 0, 0
         dd 12345678h
; DRAWITEMSTRUCT: CtlType, CtlID, itemID, itemAction (ODA_DRAWENTIRE),
; itemState (ODS_SELECTED), hwndItem, hDC, rcItem, itemData.
dis      dw 2, 7, 3, 1, 1, 0, 0, -3, 4, 100, 20
         dd 9abcdef0h
; COMPAREITEMSTRUCT: CtlType, CtlID, hwndItem, itemID1, itemData1,
; itemID2, itemData2.
cis      dw 2, 7, 0, 1
         dd 11h
         dw 2
         dd 22h
; DELETEITEMSTRUCT: CtlType, CtlID, itemID, hwndItem, itemData.
deis     dw 2, 7, 5, 0
         dd 0cafe1234h
missing_file db 'C:\NOSUCH16.TXT', 0
missing_folder db 'C:\NOSUCH16\NEW.TXT', 0
fout     db 'C:\OLDDLG16.TXT', 0
data_end:

section fix
fix_end:
