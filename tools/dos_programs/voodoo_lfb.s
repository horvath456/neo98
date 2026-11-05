# The linear frame buffer of the 3dfx Voodoo read back, for
# tools/test_dos_voodoo_lfb.py: a program that draws and then looks at what it
# drew without waiting for the picture to change, the way grLfbReadRegion of
# Glide and a depth probe of a lens flare do it.
#
# The card is found and set up as in voodoo.s (640x480, two colour buffers
# and the auxiliary one). Half a second later the program plays a script of register writes,
# of blocks written through the linear frame buffer and of reads through it.
# A real card empties its FIFO before a read, so every read sees all that was
# drawn before it:
#
#   the back buffer and the depth right after a fill and a triangle
#   the front buffer, which was filled before
#   a block written through the linear frame buffer, colour and depth
#   triangles drawn after such a block, over it and behind its depth
#   both buffers right after swapbufferCMD
#   a triangle drawn into the front buffer
#   the fill of the next frame, colour and depth
#
# Every value read goes into D:\LFB.BIN, 16 bits each in the order of the
# script, so the test can show them all. The program prints "lfb", waits for
# a key (the test takes its picture), turns the card off and ends with 42
# when every read was what the script names, with 100 plus the number of the
# first read that was not, with 1 without a card. Assembled with llvm-mc as a
# flat .COM.
.code16
.intel_syntax noprefix
.text
start:
    xor ax, ax
    mov fs, ax
    push cs
    pop ds
    xor bx, bx
find:
    movzx eax, bx
    shl eax, 11
    or eax, 0x80000000
    mov ebp, eax
    mov dx, 0xcf8
    out dx, eax
    mov dx, 0xcfc
    in eax, dx
    cmp eax, 0x0001121a
    je found
    inc bx
    cmp bx, 32
    jb find
    mov ax, 0x4c01
    int 0x21
found:
    # base address 0
    mov eax, ebp
    or al, 0x10
    mov dx, 0xcf8
    out dx, eax
    mov dx, 0xcfc
    in eax, dx
    and eax, 0xfffffff0
    mov esi, eax
    # initEnable (40h) = 1: fbiInit registers writable; C0h: clocks on
    mov eax, ebp
    or al, 0x40
    mov dx, 0xcf8
    out dx, eax
    mov dx, 0xcfc
    mov al, 1
    out dx, al
    mov eax, ebp
    or al, 0xc0
    mov dx, 0xcf8
    out dx, eax
    mov dx, 0xcfc
    xor al, al
    out dx, al
    call base
base:
    pop bp
    mov di, bp
    mov dx, bp
    .att_syntax
    addw $(script - base), %di
    addw $(results - base), %dx
    .intel_syntax noprefix
    # CH: the exit code so far; CL: the number of the next read
    mov cx, 0x2a00
# The script from DS:DI: pairs of register offset and value. FFFFFFFEh is a
# read (address in the linear frame buffer, the pixel expected), FFFFFFFDh a
# block of 16x16 pixels (address, the double word written), FFFFFFFCh a wait
# of so many ticks of the BIOS clock, FFFFFFFFh the end.
play:
    mov ebx, [di]
    mov eax, [di + 4]
    cmp ebx, 0xffffffff
    je played
    cmp ebx, 0xfffffffe
    je read
    cmp ebx, 0xfffffffd
    je block
    cmp ebx, 0xfffffffc
    je wait
    mov fs:[esi + ebx], eax
    add di, 8
    jmp play
wait:
    # EAX ticks of the BIOS clock at 0040:006C
    mov ebx, fs:[0x46c]
tick:
    cmp ebx, fs:[0x46c]
    je tick
    dec eax
    jnz wait
    add di, 8
    jmp play
read:
    mov eax, fs:[esi + eax]
    mov bx, dx
    mov [bx], ax
    add dx, 2
    cmp ax, [di + 8]
    je good
    cmp ch, 42
    jne good
    mov ch, cl
    add ch, 100
good:
    inc cl
    add di, 12
    jmp play
block:
    # 16 rows of 8 double words, two pixels each; a row is 2048 bytes
    mov ebx, eax
    mov eax, [di + 8]
    push cx
    mov ch, 16
brow:
    mov cl, 8
    push ebx
bcolumn:
    mov fs:[esi + ebx], eax
    add ebx, 4
    dec cl
    jnz bcolumn
    pop ebx
    add ebx, 2048
    dec ch
    jnz brow
    pop cx
    add di, 12
    jmp play
played:
    # the values read into the file
    push cx
    mov bx, bp
    .att_syntax
    addw $(results - base), %bx
    .intel_syntax noprefix
    sub dx, bx
    push dx
    push bx
    mov dx, bp
    .att_syntax
    addw $(name - base), %dx
    .intel_syntax noprefix
    xor cx, cx
    mov ah, 0x3c
    int 0x21
    pop dx
    pop cx
    jc nofile
    mov bx, ax
    mov ah, 0x40
    int 0x21
    mov ah, 0x3e
    int 0x21
nofile:
    # the text, the key
    mov dx, bp
    .att_syntax
    addw $(msg - base), %dx
    .intel_syntax noprefix
    mov ah, 9
    int 0x21
    xor ah, ah
    int 0x16
    pop bx
    # fbiInit0 = 0: the VGA has the monitor again; E0h: clocks off
    mov dword ptr fs:[esi + 0x210], 0
    mov eax, ebp
    or al, 0xe0
    mov dx, 0xcf8
    out dx, eax
    mov dx, 0xcfc
    xor al, al
    out dx, al
    mov al, bh
    mov ah, 0x4c
    int 0x21
msg: .ascii "lfb$"
name: .asciz "D:\\LFB.BIN"

.macro reg offset, value
    .long \offset, \value
.endm
# a triangle with flat colour: the vertices in pixels, the colour, the depth
.macro flat x0, y0, x1, y1, x2, y2, r, g, b, a, z
    .long 0x008, (\x0)<<4, 0x00c, (\y0)<<4, 0x010, (\x1)<<4, 0x014, (\y1)<<4, 0x018, (\x2)<<4, 0x01c, (\y2)<<4
    .long 0x020, (\r)<<12, 0x024, (\g)<<12, 0x028, (\b)<<12, 0x02c, (\z)<<12, 0x030, (\a)<<12
    .long 0x080, 0
.endm
# the pixel at x (even), y of the buffer lfbMode reads is `value`
.macro rd x, y, value
    .long 0xfffffffe, 0x400000 + (\y) * 2048 + (\x) * 2, \value
.endm
# 16x16 pixels from x (even), y through the linear frame buffer, `value` for
# every two of them
.macro blk x, y, value
    .long 0xfffffffd, 0x400000 + (\y) * 2048 + (\x) * 2, \value
.endm

# lfbMode: the write format in bits 0 to 3 (0 is 565, 15 the depth alone),
# the buffer written in bits 4 and 5 and the buffer read in bits 6 and 7
# (0 front, 1 back, 2 auxiliary).
# fbzMode: 1 clip, 10h depth test, 20h "less", 200h write colour, 400h write
# depth, 4000h draw to the back buffer (0 to the front buffer).
.balign 4
script:
    reg 0x210, 2                    # fbiInit0: graphics reset
    reg 0x210, 0
    reg 0x214, (640/64)<<4          # fbiInit1: video tiles of 64 pixels a row
    reg 0x218, 150<<11              # fbiInit2: two colour buffers of 150 pages before the auxiliary one
    reg 0x21c, 0                    # fbiInit3
    reg 0x200, 0                    # fbiInit4
    reg 0x208, (33<<16)|48          # backPorch
    reg 0x220, (704<<16)|95         # hSync
    reg 0x224, (523<<16)|2          # vSync
    reg 0x20c, (480<<16)|640        # videoDimensions
    reg 0x210, 1                    # fbiInit0: the card has the monitor
    reg 0x118, 640                  # clipLeftRight
    reg 0x11c, 480                  # clipLowYHighY
    # every gradient zero
    reg 0x040, 0
    reg 0x044, 0
    reg 0x048, 0
    reg 0x04c, 0
    reg 0x050, 0
    reg 0x054, 0
    reg 0x058, 0
    reg 0x05c, 0
    reg 0x060, 0
    reg 0x064, 0
    reg 0x068, 0
    reg 0x06c, 0
    reg 0x070, 0
    reg 0x074, 0
    reg 0x078, 0
    reg 0x07c, 0
    reg 0x034, 0                    # startS
    reg 0x038, 0                    # startT
    reg 0x03c, 0x40000000           # startW: 1
    reg 0x104, 0                    # fbzColorPath: the iterated colour
    reg 0x108, 0                    # fogMode
    reg 0x10c, 0                    # alphaMode
    reg 0x144, 0                    # color0
    # The card has shown some pictures before the program looks: the first
    # reads of a program come long after it switched the card on.
    .long 0xfffffffc, 9

    # the front buffer blue, colour alone
    reg 0x110, 0x0200
    reg 0x148, 0x000000ff           # color1: the colour of the fill
    reg 0x124, 0                    # fastfillCMD
    # the back buffer 204060h with the depth FFFFh, a red triangle at 4000h
    reg 0x110, 0x4600
    reg 0x148, 0x00204060
    reg 0x130, 0xffff               # zaColor: the depth of the fill
    reg 0x124, 0
    reg 0x110, 0x4631
    flat 40, 40, 280, 40, 40, 280, 255, 0, 0, 255, 0x4000
    # read with no swap between: back buffer, depth, front buffer
    reg 0x114, 0x40
    rd 100, 100, 0xf800             # 0: the red triangle
    rd 400, 400, 0x220c             # 1: the fill
    reg 0x114, 0x80
    rd 100, 100, 0x4000             # 2: the depth of the triangle
    rd 400, 400, 0xffff             # 3: the depth of the fill
    reg 0x114, 0x00
    rd 100, 100, 0x001f             # 4: the front buffer

    # a white block into the back buffer through the linear frame buffer
    reg 0x114, 0x50
    blk 600, 20, 0xffffffff
    rd 608, 28, 0xffff              # 5: the block
    rd 100, 100, 0xf800             # 6: the triangle is still there
    # drawn after the block: green at 8000h behind the red, blue over half
    # of the block without depth
    flat 60, 60, 420, 220, 60, 300, 0, 255, 0, 255, 0x8000
    rd 300, 200, 0x07e0             # 7: the green triangle
    rd 100, 100, 0xf800             # 8: red in front of it
    reg 0x110, 0x4201
    flat 600, 20, 616, 20, 600, 36, 0, 0, 255, 255, 0x1000
    rd 602, 22, 0x001f              # 9: blue over the block
    rd 614, 34, 0xffff              # 10: the rest of the block
    reg 0x114, 0x80
    rd 300, 200, 0x8000             # 11: the depth of the green triangle
    rd 602, 22, 0xffff              # 12: neither the block nor the blue wrote depth

    # the depth 1000h written through the linear frame buffer, and a yellow
    # triangle at 2000h drawn after it: hidden behind the block
    reg 0x114, 0x9f
    blk 500, 100, 0x10001000
    rd 508, 108, 0x1000             # 13: the depth written
    rd 520, 108, 0xffff             # 14: right of it
    reg 0x110, 0x4631
    flat 480, 80, 560, 80, 480, 160, 255, 255, 0, 255, 0x2000
    rd 484, 140, 0x2000             # 15: the depth of the yellow triangle
    rd 508, 108, 0x1000             # 16: the block in front of it
    reg 0x114, 0x40
    rd 508, 108, 0x220c             # 17: the fill, where the block hides the triangle
    rd 484, 140, 0xffe0             # 18: the yellow triangle

    # right after the swap
    reg 0x128, 0                    # swapbufferCMD
    reg 0x114, 0x00
    rd 100, 100, 0xf800             # 19: the front buffer is what was drawn
    rd 484, 140, 0xffe0             # 20
    reg 0x114, 0x40
    rd 100, 100, 0x001f             # 21: the back buffer is the blue one
    # a white triangle into the front buffer
    reg 0x110, 0x0201
    flat 40, 300, 140, 300, 40, 400, 255, 255, 255, 255, 0x1000
    reg 0x114, 0x00
    rd 60, 320, 0xffff              # 22
    # the next frame: its fill, colour and depth
    reg 0x110, 0x4600
    reg 0x148, 0x00ff00ff
    reg 0x130, 0x1234
    reg 0x124, 0
    reg 0x114, 0x40
    rd 10, 10, 0xf81f               # 23
    reg 0x114, 0x80
    rd 10, 10, 0x1234               # 24
    rd 100, 100, 0x1234             # 25: the red triangle's depth is gone
    # a green block into the front buffer through the linear frame buffer
    reg 0x114, 0x00
    blk 300, 400, 0x07e007e0
    rd 308, 408, 0x07e0             # 26
    rd 100, 100, 0xf800             # 27: the front buffer beside it
    .long 0xffffffff
results:
    .space 128
