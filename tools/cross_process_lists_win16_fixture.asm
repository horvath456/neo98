; SPDX-License-Identifier: GPL-2.0-or-later
; A 16-bit program sends list box, combo box and edit messages with
; pointers to the controls of a 32-bit program, the server of
; tools/cross_process_lists_fixture.c, which starts it (WinExec) once its own
; messages are done. The 16-bit numbers are WM_USER and up:
;
; FindWindow("XProcServer", NULL), GetDlgItem 101 (the combo box), 102 (the
; list box), 103 (the edit)
; CB_ADDSTRING (403h) "W16", CB_FINDSTRINGEXACT (418h) -1 "w16",
; CB_GETLBTEXT (408h) of that index into a buffer of 16 points
; LB_ADDSTRING (401h) "L16", LB_GETTEXT (40Ah) of that index
; EM_GETLINE (414h) line 1 into a buffer whose first word is 20
;
; C:\XP16OUT.TXT holds the words: found (the server and its three controls
; are there), the combo box's answers, its buffer of 16 bytes, the list's
; answers and buffer, the edit's answer and buffer (16 bytes each).
; tools/test_cross_process_lists_host.py; the New Executable is made as for
; tools/layer_win16_fixture.asm.
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

; SendMessage(window, message, wParam, ds:text): the answer in ax
%macro SEND 4
        push word %1
        push word %2
        push word %3
        push ds
        push word %4
        API 2, 111                      ; SendMessage
%endmacro

section code
code_start:
        API 1, 91                       ; InitTask
        push di                         ; the instance
        API 2, 5                        ; InitApp
        push ds
        push word classname
        push word 0
        push word 0
        API 2, 50                       ; FindWindow
        mov [server], ax
        or ax, ax
        jz write
        push ax
        push word 101
        API 2, 91                       ; GetDlgItem
        mov [combo], ax
        push word [server]
        push word 102
        API 2, 91
        mov [list], ax
        push word [server]
        push word 103
        API 2, 91
        mov [edit], ax
        mov ax, [combo]
        and ax, [list]
        and ax, [edit]
        jz write
        mov word [found], 1

        SEND [combo], 0x403, 0, w16
        mov [combo_add], ax
        SEND [combo], 0x418, 0xffff, w16lower
        mov [combo_find], ax
        SEND [combo], 0x408, [combo_find], combo_text
        mov [combo_get], ax

        SEND [list], 0x401, 0, l16
        mov [list_add], ax
        SEND [list], 0x40a, [list_add], list_text
        mov [list_get], ax

        mov word [edit_text], 20
        SEND [edit], 0x414, 1, edit_text
        mov [edit_get], ax

write:
        push ds
        push word fout
        push word 0
        API 1, 83                       ; _lcreat
        mov si, ax
        push si
        push ds
        push word found
        push word results_end - found
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
classname db 'XProcServer', 0
fout    db 'C:\XP16OUT.TXT', 0
w16     db 'W16', 0
w16lower db 'w16', 0
l16     db 'L16', 0
server  dw 0
combo   dw 0
list    dw 0
edit    dw 0
found   dw 0
combo_add dw 0x5555
combo_find dw 0x5555
combo_get dw 0x5555
combo_text times 16 db '#'
list_add dw 0x5555
list_get dw 0x5555
list_text times 16 db '#'
edit_get dw 0x5555
edit_text times 16 db '#'
results_end:
        times 512 db 0
data_end:

section fix
fix_end:
