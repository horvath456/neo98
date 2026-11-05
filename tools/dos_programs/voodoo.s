# A scene on the 3dfx Voodoo for tools/test_dos_voodoo.py, drawn through the
# registers of the card alone, the way a DOS program without Glide does it.
#
# The card is found on the PCI bus (vendor 121Ah, device 0001h) through the
# configuration ports CF8h/CFCh, its 16 MB lie at base address 0. DOS runs in
# real mode here and the machine checks no segment limits, so FS = 0 with a
# 32-bit offset reaches them. The program sets up 640x480, then draws into
# the back buffer:
#
#   fastfillCMD       colour 204060h, depth FFFFh
#   red triangle      depth 4000h, depth test "less"
#   green triangle    depth 8000h, over the red one: hidden where they overlap
#   shaded triangle   red grows by one per pixel to the right
#   textured square   256x256 texels of 565 in squares of 32, yellow and
#                     magenta, at half size (128 pixels)
#   blue square       alpha 128 blended over the red and the green triangle
#   white block       16x16 pixels written through the linear frame buffer
#
# and swaps. It prints "voodoo" and waits for a key, so the test can look at
# the picture; then it reads four pixels of the front buffer back through
# the linear frame buffer, turns the card off and ends with 42 when they are
# what it drew, with 2 to 5 for the first pixel that is not, with 1 without
# a card. Assembled with llvm-mc as a flat .COM.
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
    # the registers up to the texture
    mov di, bp
    .att_syntax
    addw $(setup - base), %di
    .intel_syntax noprefix
    call play
    # 256x256 texels of 565 at LOD 0 of TMU 0: 128 double words a row at
    # 800000h + (row << 9); squares of 32, yellow FFE0h and magenta F81Fh
    xor cx, cx
row:
    xor dx, dx
texel:
    mov ax, cx
    shr ax, 5
    mov bx, dx
    shr bx, 4
    xor ax, bx
    test al, 1
    mov eax, 0xffe0ffe0
    jz even
    mov eax, 0xf81ff81f
even:
    movzx ebx, cx
    shl ebx, 9
    movzx edi, dx
    lea ebx, [ebx + edi * 4 + 0x800000]
    mov fs:[esi + ebx], eax
    inc dx
    cmp dx, 128
    jb texel
    inc cx
    cmp cx, 256
    jb row
    # the scene
    mov di, bp
    .att_syntax
    addw $(scene - base), %di
    .intel_syntax noprefix
    call play
    # 16x16 white pixels at (600, 20) through the linear frame buffer, two
    # pixels a double word: 400000h + row * 2048 + column * 2
    mov cx, 20
lrow:
    mov dx, 600
lpixel:
    movzx ebx, cx
    shl ebx, 11
    movzx edi, dx
    lea ebx, [ebx + edi * 2 + 0x400000]
    mov dword ptr fs:[esi + ebx], 0xffffffff
    add dx, 2
    cmp dx, 616
    jb lpixel
    inc cx
    cmp cx, 36
    jb lrow
    mov di, bp
    .att_syntax
    addw $(swap - base), %di
    .intel_syntax noprefix
    call play
    # the text, the key
    mov dx, bp
    .att_syntax
    addw $(msg - base), %dx
    .intel_syntax noprefix
    mov ah, 9
    int 0x21
    xor ah, ah
    int 0x16
    # the front buffer back through the linear frame buffer (lfbMode 0:
    # read the front buffer): red, the background, white, the green left of
    # the red triangle
    mov bl, 2
    mov ecx, 0x400000 + 100 * 2048 + 100 * 2
    mov dx, 0xf800
    call check
    mov bl, 3
    mov ecx, 0x400000 + 400 * 2048 + 20 * 2
    mov dx, 0x220c
    call check
    mov bl, 4
    mov ecx, 0x400000 + 28 * 2048 + 608 * 2
    mov dx, 0xffff
    call check
    mov bl, 5
    mov ecx, 0x400000 + 200 * 2048 + 300 * 2
    mov dx, 0x07e0
    call check
    mov bl, 42
done:
    # fbiInit0 = 0: the VGA has the monitor again; E0h: clocks off
    mov dword ptr fs:[esi + 0x210], 0
    mov eax, ebp
    or al, 0xe0
    mov dx, 0xcf8
    out dx, eax
    mov dx, 0xcfc
    xor al, al
    out dx, al
    mov al, bl
    mov ah, 0x4c
    int 0x21
# the pixel at ECX of the linear frame buffer is DX, or the program ends with BL
check:
    mov eax, fs:[esi + ecx]
    cmp ax, dx
    je same
    pop ax
    jmp done
same:
    ret
# pairs of register offset and value from DS:DI up to offset FFFFFFFFh
play:
    mov ebx, [di]
    cmp ebx, 0xffffffff
    je played
    mov eax, [di + 4]
    mov fs:[esi + ebx], eax
    add di, 8
    jmp play
played:
    ret
msg: .ascii "voodoo$"

.macro reg offset, value
    .long \offset, \value
.endm
# a triangle with flat colour: the vertices in pixels, the colour, the depth
.macro flat x0, y0, x1, y1, x2, y2, r, g, b, a, z
    .long 0x008, (\x0)<<4, 0x00c, (\y0)<<4, 0x010, (\x1)<<4, 0x014, (\y1)<<4, 0x018, (\x2)<<4, 0x01c, (\y2)<<4
    .long 0x020, (\r)<<12, 0x024, (\g)<<12, 0x028, (\b)<<12, 0x02c, (\z)<<12, 0x030, (\a)<<12
    .long 0x080, 0
.endm

.balign 4
setup:
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
    # TMU 0 (chip select 800h): 565, clamped, point sampled, no perspective;
    # the TMU passes its texel on (zero other, add local, colour and alpha)
    reg 0x800|0x300, 0x08241ac0     # textureMode
    reg 0x800|0x304, 0              # tLOD: level 0 alone
    reg 0x800|0x30c, 0              # texBaseAddr
    .long 0xffffffff
scene:
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
    # fbzMode 4600h: write colour and depth, draw to the back buffer
    reg 0x110, 0x4600
    reg 0x148, 0x00204060           # color1: the colour of the fill
    reg 0x130, 0xffff               # zaColor: the depth of the fill
    reg 0x124, 0                    # fastfillCMD
    # fbzMode 4631h: clip, depth test "less", write colour and depth
    reg 0x110, 0x4631
    flat 40, 40, 280, 40, 40, 280, 255, 0, 0, 255, 0x4000
    flat 60, 60, 420, 220, 60, 300, 0, 255, 0, 255, 0x8000
    # red from 0 to 255 over 256 pixels: dRdX = 1
    reg 0x040, 1<<12
    flat 40, 320, 296, 320, 40, 440, 0, 0, 64, 255, 0x2000
    reg 0x040, 0
    # the texture: fbzColorPath 08000005h takes colour and alpha from it;
    # s and t grow by two texels a pixel, from the upper left corner
    reg 0x104, 0x08000005
    reg 0x054, 2<<18                # dSdX
    reg 0x078, 2<<18                # dTdY
    flat 340, 300, 468, 300, 340, 428, 255, 255, 255, 255, 0x2000
    reg 0x034, 256<<18              # startS at the upper right corner
    flat 468, 300, 468, 428, 340, 428, 255, 255, 255, 255, 0x2000
    reg 0x034, 0
    reg 0x054, 0
    reg 0x078, 0
    reg 0x104, 0
    # blue with alpha 128 blended over what is there: alphaMode 5110h is
    # source alpha and one minus source alpha; no depth test
    reg 0x10c, 0x00005110
    reg 0x110, 0x4201
    flat 160, 100, 240, 100, 160, 180, 0, 0, 255, 128, 0x1000
    flat 240, 100, 240, 180, 160, 180, 0, 0, 255, 128, 0x1000
    reg 0x10c, 0
    # lfbMode 10h: 565 to the back buffer, no pixel pipeline
    reg 0x114, 0x10
    .long 0xffffffff
swap:
    reg 0x128, 0                    # swapbufferCMD
    reg 0x114, 0                    # lfbMode: read the front buffer
    .long 0xffffffff
