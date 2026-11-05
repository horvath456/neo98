; SPDX-License-Identifier: GPL-2.0-or-later
; VER.2 GetFileResourceSize, VER.3 GetFileResource and VER.20
; GetFileVersionInfoRaw from a 16-bit program. SETUPX takes these
; with the five other VER routines through GetProcAddress and drops VER.DLL
; altogether when one name is missing; Office XP's System Files Update logged
; "MISSING WIN16 EXPORT VER!GetFileResourceSize" for every file it copied.
;
; The program asks for the version resource (type 16, id 1) of an NE file
; (C:\WINDOWS\SYSTEM\VER.DLL) and of a PE file (VERSION.DLL), for a file that
; does not exist and for a type neither file has, then calls the three
; entries once more through GetProcAddress. Everything that came back goes
; in order into C:\VERRES.BIN; tools/test_ver_resource_win16_host.py reads
; the file and compares it with what it finds in the two DLLs itself.
;
; The handle words start as A5A5A5A5h so the test sees whether VER.DLL
; wrote them. The New Executable is made as for
; tools/find_attributes_win16_fixture.asm, with VER as module 3.
        bits 16
section header start=0
        dw code_end - code_start, data_end - data_start, (fix_end - fix_start) / 6
section code vstart=0 follows=header align=1
section data vstart=0 follows=code align=1
section fix vstart=0 follows=data align=1
fix_start:

; A far call the loader fills in: module 1 is KERNEL, 2 is USER, 3 is VER.
; ES may come back changed, and the results are stored through ES:DI.
%macro API 2
%%at:   db 0x9a, 0xff, 0xff, 0x00, 0x00
[section fix]
        dw %%at + 1 - code_start, %1, %2
__SECT__
        push ds
        pop es
%endmacro
%macro DOS 0
        API 1, 102                      ; DOS3Call
%endmacro
; DX:AX into the next two result words.
%macro KEEP32 0
        stosw
        mov ax, dx
        stosw
%endmacro
; The handle word into the result and back to A5A5A5A5h.
%macro KEEPHANDLE 0
        mov eax, [handle]
        stosd
        mov dword [handle], 0a5a5a5a5h
%endmacro
; GetFileResourceSize(file, type, id, &handle) through VER.2
%macro SIZE 3
        push ds
        push word %1
        push dword %2
        push dword %3
        push ds
        push word handle
        API 3, 2
        KEEP32
        KEEPHANDLE
%endmacro
; Fill the 1024 bytes of `buffer` with CCh.
%macro CLEAR 0
        push di
        mov di, buffer
        mov cx, 512
        mov ax, 0cccch
        rep stosw
        pop di
%endmacro
; The 1024 bytes of `buffer` into the result.
%macro KEEPBUFFER 0
        push si
        mov si, buffer
        mov cx, 512
        rep movsw
        pop si
%endmacro

section code
code_start:
        API 1, 91                       ; InitTask
        push di                         ; the instance
        API 2, 5                        ; InitApp
        push ds
        pop es
        cld
        mov di, outbuf

        ; 1 the NE file: size and the file offset of the resource
        SIZE nefile, 16, 1
        ; 2 GetFileResource with the offset just found, 64 bytes
        mov eax, [outbuf + 4]
        mov [found], eax
        CLEAR
        push ds
        push word nefile
        push dword 16
        push dword 1
        push dword [found]
        push dword 64
        push ds
        push word buffer
        API 3, 3
        stosw
        KEEPBUFFER
        ; 3 GetFileResource without an offset: VER.DLL searches again and
        ; never copies more than the resource holds, 1024 asked
        CLEAR
        push ds
        push word nefile
        push dword 16
        push dword 1
        push dword 0
        push dword 1024
        push ds
        push word buffer
        API 3, 3
        stosw
        KEEPBUFFER
        ; 4 the PE file: size, and the handle comes back as zero
        SIZE pefile, 16, 1
        ; 5 the PE file's resource, 1024 asked
        CLEAR
        push ds
        push word pefile
        push dword 16
        push dword 1
        push dword 0
        push dword 1024
        push ds
        push word buffer
        API 3, 3
        stosw
        KEEPBUFFER
        ; 6 a file that does not exist: zero, the handle untouched
        SIZE nosuch, 16, 1
        ; 7 a type the NE file lacks: zero, the handle untouched
        SIZE nefile, 7777h, 1
        ; 8 a type the PE file lacks: zero, the handle written as zero
        SIZE pefile, 7777h, 1
        ; 9 GetFileVersionInfoRaw of the PE file: the block as it is stored
        CLEAR
        push ds
        push word pefile
        push dword 0
        push dword 1024
        push ds
        push word buffer
        API 3, 20
        stosw
        KEEPBUFFER

        ; 10 the three names through GetProcAddress, as SETUPX asks, on the
        ; module LoadLibrary("ver.dll") answers
        push ds
        push word vername
        API 1, 95                       ; LoadLibrary
        mov [module], ax
        stosw
        mov si, names
.next:  cmp byte [si], 0
        je .done
        push word [module]
        push ds
        push si
        API 1, 50                       ; GetProcAddress
        mov [proc], ax
        mov [proc + 2], dx
        KEEP32
.skip:  lodsb
        or al, al
        jne .skip
        jmp .next
.done:
        ; 11 the last pointer is GetFileResourceSize again; call it
        mov eax, [first]
        or eax, eax
        je .none
        push ds
        push word nefile
        push dword 16
        push dword 1
        push ds
        push word handle
        call far [first]
        push ds
        pop es
        jmp .kept
.none:  xor ax, ax
        xor dx, dx
.kept:  KEEP32
        KEEPHANDLE

        push ds
        push word fout
        push word 0
        API 1, 83                       ; _lcreat
        mov si, ax
        push si
        push ds
        push word outbuf
        mov ax, di
        sub ax, outbuf
        push ax
        API 1, 86                       ; _lwrite
        push si
        API 1, 81                       ; _lclose
        mov ax, 4c00h
        DOS
        jmp $
code_end:

section data
data_start:
        times 16 db 0                   ; the words of the local heap and the stack
nefile  db 'C:\WINDOWS\SYSTEM\VER.DLL', 0
pefile  db 'C:\WINDOWS\SYSTEM\VERSION.DLL', 0
nosuch  db 'C:\WINDOWS\SYSTEM\NOSUCH.DLL', 0
vername db 'ver.dll', 0
fout    db 'C:\VERRES.BIN', 0
names   db 'GetFileResource', 0, 'GetFileVersionInfoRaw', 0, 'GetFileResourceSize', 0, 0
handle  dd 0a5a5a5a5h
found   dd 0
module  dw 0
proc:
first   dd 0
buffer  times 1024 db 0
outbuf  times 6000 db 0
data_end:

section fix
fix_end:
