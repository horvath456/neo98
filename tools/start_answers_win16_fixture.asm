; SPDX-License-Identifier: GPL-2.0-or-later
; What a 16-bit program gets back when it starts another, and what it gets
; for a drive that does not exist (tools/test_start_win16_host.py).
;
; Built twice from this source: ANSWER16.EXE starts CHILD16.EXE (module
; CHILD16) and Calculator through KERNEL's WinExec (KERNEL.166) and
; LoadModule (KERNEL.45) and SHELL's ShellExecute (SHELL.20). A child,
; started with "k" as its command tail, writes its instance (DI of
; InitTask) and its task (GetCurrentTask) into C:\S16\CHILD.BIN and ends;
; the parent waits for the file. For Calculator it records the window's
; task (GetWindowTask), its GWW_HINSTANCE and, when the task is a readable
; selector, the instance and module words of the task's database (1Ch,
; 1Eh) and its signature (FAh, "TD"), and GetModuleUsage of the answer
; while the window is there and after it is gone.
;
; Then the three ways for a file on a drive that does not exist (Q:), in a
; folder that does not exist and a file that does not exist; OpenFile
; (OF_EXIST) on the missing drive and folder; and DOS3Call for the
; missing drive: open (3Dh), change directory (3Bh), find first (4Eh),
; make directory (39h), attributes (4300h), the long-name attributes
; (7143h), the free space of the drive (36h) and its current directory
; (47h), each as AX and the carry flag.
;
; Every line goes to C:\S16\RESULT.TXT and, when it opens, to COM1; the
; last is MEASURE_DONE (the same program measures the real Windows).
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

; KERNEL 1, USER 2, SHELL 3
section code
code_start:
        API 1, 91                       ; InitTask
        mov [instance], di
        mov [tail], bx
        mov [tail + 2], es
        push word 0
        API 1, 30                       ; WaitEvent(0)
        les bx, [tail]
        mov cx, 16                      ; a "k" in the command tail
.look:  mov al, [es:bx]
        or al, al
        jz parent
        cmp al, 13
        je parent
        or al, 20h
        cmp al, 'k'
        je child
        inc bx
        loop .look
        jmp parent

; ------------------------------------------------------------ the child
child:  API 1, 36                       ; GetCurrentTask
        mov [task], ax
        push ds
        push word childfile
        push word 0
        API 1, 83                       ; _lcreat
        mov si, ax
        push si
        push ds
        push word instance
        push word 4
        API 1, 86                       ; _lwrite
        push si
        API 1, 81                       ; _lclose
        jmp quit

; ------------------------------------------------------------ the parent
parent:
        push word [instance]
        API 2, 5                        ; InitApp
        mov [lm_block + 4], ds
        mov [lm_block + 8], ds
        push ds
        push word outname
        push word 0
        API 1, 83                       ; _lcreat
        mov [outfile], ax
        push ds
        push word comname
        push word 1                     ; OF_WRITE
        API 1, 85                       ; _lopen
        mov [com], ax
        mov si, s_start
        call put

        mov word [label], l_we
        call clear_child
        push ds
        push word cmd_child
        push word 1
        API 1, 166                      ; WinExec
        call child_line

        mov word [label], l_lm
        call clear_child
        push ds
        push word path_child
        push ds
        push word lm_block
        API 1, 45                       ; LoadModule
        call child_line

        mov word [label], l_se
        call clear_child
        push word 0
        push word 0
        push word 0
        push ds
        push word path_child
        push ds
        push word tail_child
        push ds
        push word dir_s16
        push word 1
        API 3, 20                       ; ShellExecute
        call child_line

        mov word [label], l_cwe
        push ds
        push word calc
        push word 1
        API 1, 166                      ; WinExec
        call calc_line

        mov word [label], l_clm
        push ds
        push word path_calc
        push ds
        push word lm_block
        API 1, 45                       ; LoadModule
        call calc_line

        mov word [label], l_cse
        push word 0
        push word 0
        push word 0
        push ds
        push word calc
        push word 0
        push word 0
        push word 0
        push word 0
        push word 1
        API 3, 20                       ; ShellExecute
        call calc_line

        mov word [label], l_drive
        mov word [target], miss_drive
        call errors
        mov word [label], l_folder
        mov word [target], miss_folder
        call errors
        mov word [label], l_file
        mov word [target], miss_file
        call errors

        call openfile_line
        call dos_line

        mov si, s_done
        call put
        mov si, s_measured
        call put
        push word [outfile]
        API 1, 81                       ; _lclose
        cmp word [com], -1
        je quit
        push word [com]
        API 1, 81                       ; _lclose
quit:   mov ax, 4c00h
        API 1, 102                      ; DOS3Call
        jmp $

; put: the zero-ended text at ds:si into the file and COM1.
put:    push ds
        push si
        API 1, 90                       ; lstrlen
        mov cx, ax
        push cx
        push word [outfile]
        push ds
        push si
        push cx
        API 1, 86                       ; _lwrite
        pop cx
        cmp word [com], -1
        je .done
        push word [com]
        push ds
        push si
        push cx
        API 1, 86                       ; _lwrite
.done:  ret

; clear_child: delete C:\S16\CHILD.BIN.
clear_child:
        push ds
        push word childfile
        push ds
        push word ofs
        push word 0200h                 ; OF_DELETE
        API 1, 74                       ; OpenFile
        mov word [child_data], 0
        mov word [child_data + 2], 0
        ret

; child_line: AX is the answer; the child's two words, read when it wrote
; them (within 20 seconds), go with it into a line.
child_line:
        mov [answer], ax
        API 2, 13                       ; GetTickCount
        mov [t0], ax
.look:  API 1, 29                       ; Yield
        push ds
        push word childfile
        push word 0
        API 1, 85                       ; _lopen
        cmp ax, -1
        je .wait
        mov si, ax
        push si
        push ds
        push word child_data
        push word 4
        API 1, 82                       ; _lread
        push ax
        push si
        API 1, 81                       ; _lclose
        pop ax
        cmp ax, 4
        je .got
.wait:  API 2, 13                       ; GetTickCount
        sub ax, [t0]
        cmp ax, 20000
        jb .look
.got:   push word [child_data + 2]
        push word [child_data]
        push word [answer]
        push ds
        push word [label]
        push ds
        push word f_child
        push ds
        push word line
        API 2, 420                      ; wsprintf
        add sp, 18
        mov si, line
        jmp put

; calc_line: AX is the answer; Calculator's window within 20 seconds, its
; task, GWW_HINSTANCE and task database, then the window is closed and its
; going waited for.
calc_line:
        mov [answer], ax
        xor ax, ax
        mov [w_task], ax
        mov [w_inst], ax
        mov [tdb_inst], ax
        mov [tdb_mod], ax
        mov [tdb_sig], ax
        mov [came], ax
        mov [usage_open], ax
        mov [usage_gone], ax
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
        jmp .print
.found: mov [hwnd], ax
        mov word [came], 1
        push word [answer]
        API 1, 48                       ; GetModuleUsage
        mov [usage_open], ax
        push word [hwnd]
        API 2, 224                      ; GetWindowTask
        mov [w_task], ax
        push word [hwnd]
        push word -6                    ; GWW_HINSTANCE
        API 2, 133                      ; GetWindowWord
        mov [w_inst], ax
        mov ax, [w_task]
        verr ax
        jnz .closed
        lsl cx, ax
        jnz .closed
        cmp cx, 0fbh
        jb .closed
        push es
        mov es, ax
        mov ax, [es:1ch]
        mov [tdb_inst], ax
        mov ax, [es:1eh]
        mov [tdb_mod], ax
        mov ax, [es:0fah]
        mov [tdb_sig], ax
        pop es
.closed:
        push word [hwnd]
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
        jz .print
        API 2, 13                       ; GetTickCount
        sub ax, [t0]
        cmp ax, 20000
        jb .gone
        push word [answer]
        API 1, 48                       ; GetModuleUsage
        mov [usage_gone], ax
.print: push word [usage_gone]
        push word [usage_open]
        push word [tdb_sig]
        push word [tdb_mod]
        push word [tdb_inst]
        push word [w_inst]
        push word [w_task]
        push word [came]
        push word [answer]
        push ds
        push word [label]
        push ds
        push word f_calc
        push ds
        push word line
        API 2, 420                      ; wsprintf
        add sp, 30
        mov si, line
        jmp put

; errors: the three ways for [target], which does not start.
errors:
        push ds
        push word [target]
        push word 1
        API 1, 166                      ; WinExec
        mov [r_we], ax
        push ds
        push word [target]
        push ds
        push word lm_block
        API 1, 45                       ; LoadModule
        mov [r_lm], ax
        push word 0
        push word 0
        push word 0
        push ds
        push word [target]
        push word 0
        push word 0
        push word 0
        push word 0
        push word 1
        API 3, 20                       ; ShellExecute
        mov [r_se], ax
        push word [r_se]
        push word [r_lm]
        push word [r_we]
        push ds
        push word [label]
        push ds
        push word f_errors
        push ds
        push word line
        API 2, 420                      ; wsprintf
        add sp, 18
        mov si, line
        jmp put

; openfile_line: OpenFile(OF_EXIST) on the missing drive and folder.
openfile_line:
        push ds
        push word of_drive
        push ds
        push word ofs
        push word 4000h                 ; OF_EXIST
        API 1, 74                       ; OpenFile
        mov [r_we], ax
        mov ax, [ofs + 2]
        mov [r_lm], ax
        push ds
        push word of_folder
        push ds
        push word ofs
        push word 4000h                 ; OF_EXIST
        API 1, 74                       ; OpenFile
        mov [r_se], ax
        push word [ofs + 2]
        push word [r_se]
        push word [r_lm]
        push word [r_we]
        push ds
        push word f_openfile
        push ds
        push word line
        API 2, 420                      ; wsprintf
        add sp, 16
        mov si, line
        jmp put

; dos: DOS3Call with AX and DX (CX, BX, SI as set); [di], [di+2] get AX and
; the carry flag.
dos:    API 1, 102                      ; DOS3Call
        mov [di], ax
        mov word [di + 2], 0
        jnc .ok
        mov word [di + 2], 1
.ok:    add di, 4
        ret

dos_line:
        mov di, dos_out
        mov ax, 3d00h
        mov dx, dos_file
        call dos
        mov ax, 3b00h
        mov dx, dos_root
        call dos
        mov ax, 4e00h
        mov cx, 16h
        mov dx, dos_all
        call dos
        mov ax, 3900h
        mov dx, dos_dir
        call dos
        mov ax, 4300h
        mov dx, dos_file
        call dos
        mov ax, 7143h
        xor bx, bx
        mov dx, dos_file
        call dos
        mov ax, 3600h
        mov dx, 17                      ; Q:
        call dos
        mov ax, 4700h
        mov dx, 17
        mov si, dos_buffer
        call dos
        mov si, dos_out + 28
        mov cx, 8
.push:  push word [si + 2]
        push word [si]
        sub si, 4
        loop .push
        push ds
        push word f_dos
        push ds
        push word line
        API 2, 420                      ; wsprintf
        add sp, 40
        mov si, line
        jmp put
code_end:

section data
data_start:
        times 16 db 0                   ; the words of the local heap and the stack
instance dw 0
task     dw 0
tail     dw 0, 0
outfile  dw 0
com      dw -1
label    dw 0
target   dw 0
answer   dw 0
t0       dw 0
hwnd     dw 0
came     dw 0
w_task   dw 0
w_inst   dw 0
tdb_inst dw 0
tdb_mod  dw 0
tdb_sig  dw 0
usage_open dw 0
usage_gone dw 0
r_we     dw 0
r_lm     dw 0
r_se     dw 0
child_data dw 0, 0
lm_block dw 0                           ; environment
         dw lm_tail, 0                  ; command tail
         dw lm_show, 0                  ; show
         dw 0, 0
lm_tail  db 2, ' k', 13
lm_show  dw 2, 1
outname  db 'C:\S16\RESULT.TXT', 0
comname  db 'COM1', 0
childfile db 'C:\S16\CHILD.BIN', 0
cmd_child db 'C:\S16\CHILD16.EXE k', 0
path_child db 'C:\S16\CHILD16.EXE', 0
tail_child db 'k', 0
dir_s16  db 'C:\S16', 0
calc     db 'calc', 0
path_calc db 'C:\WINDOWS\CALC.EXE', 0
klass    db 'SciCalc', 0
miss_drive db 'Q:\NOFILE.EXE', 0
miss_folder db 'C:\S16\NODIR\NOFILE.EXE', 0
miss_file db 'C:\S16\NOFILE.EXE', 0
of_drive db 'Q:\NOFILE.TXT', 0
of_folder db 'C:\S16\NODIR\NOFILE.TXT', 0
dos_file db 'Q:\NOFILE.TXT', 0
dos_root db 'Q:\', 0
dos_all  db 'Q:\*.*', 0
dos_dir  db 'Q:\NEWDIR', 0
l_we     db 'child-winexec', 0
l_lm     db 'child-loadmodule', 0
l_se     db 'child-shellexecute', 0
l_cwe    db 'calc-winexec', 0
l_clm    db 'calc-loadmodule', 0
l_cse    db 'calc-shellexecute', 0
l_drive  db 'missing-drive', 0
l_folder db 'missing-folder', 0
l_file   db 'missing-file', 0
f_child  db '%s|answer %04x|instance %04x|task %04x', 13, 10, 0
f_calc   db '%s|answer %04x|window %u|task %04x|gww %04x|tdb %04x,%04x,%04x|usage %u,%u', 13, 10, 0
f_errors db '%s|WE %u|LM %u|SE %u', 13, 10, 0
f_openfile db 'openfile|drive %d,%u|folder %d,%u', 13, 10, 0
f_dos    db 'dos|3D %x,%u|3B %x,%u|4E %x,%u|39 %x,%u|4300 %x,%u|7143 %x,%u|36 %x,%u|47 %x,%u', 13, 10, 0
s_start  db 'start', 13, 10, 0
s_done   db 'done', 13, 10, 0
s_measured db 'MEASURE_DONE', 13, 10, 0
ofs      times 136 db 0
dos_out  times 32 db 0
dos_buffer times 64 db 0
line     times 200 db 0
data_end:

section fix
fix_end:
