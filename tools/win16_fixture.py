#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Builds the hand-made NE program the Win16 loader test runs.

No 16-bit compiler exists in this tree, so the fixture is written out byte by
byte: a New Executable with two segments, an automatic data segment with a
local heap and a stack, relocations of the two classes a program really uses
(imported ordinal and internal reference) and an entry point whose code calls
KERNEL and then USER. Every field is the one src/win98_ne_image.h reads, so a
change on either side breaks the test instead of passing quietly.
"""
import struct


def counted(text):
    raw = text.encode('ascii')
    return bytes([len(raw)]) + raw


def code_segment():
    """The 16-bit startup of the fixture, with the offsets of its relocations."""
    body = bytearray()
    fixups = []

    def farcall(kind, target, value):
        """A far call whose operand a relocation record fills in."""
        body.extend(b'\x9a\xff\xff\x00\x00')
        fixups.append((len(body) - 4, kind, target, value))

    farcall('import', 1, 91)          # KERNEL.91 InitTask
    body.extend(b'\x0b\xc0')          # or ax, ax
    jump_at = len(body)
    body.extend(b'\x74\x00')          # je end
    body.extend(b'\xb8\x02\x00')      # mov ax, 2 GMEM_MOVEABLE
    body.extend(b'\x50')              # push ax
    body.extend(b'\x31\xc0')          # xor ax, ax
    body.extend(b'\x50')              # push ax size, high word
    body.extend(b'\xb8\x34\x12')      # mov ax, 0x1234
    body.extend(b'\x50')              # push ax size, low word
    farcall('import', 1, 15)          # KERNEL.15 GlobalAlloc
    body.extend(b'\x89\xc6')          # mov si, ax keep the handle
    body.extend(b'\x56')              # push si
    farcall('import', 1, 18)          # KERNEL.18 GlobalLock
    body.extend(b'\x56')              # push si
    farcall('import', 1, 20)          # KERNEL.20 GlobalSize
    # A global block larger than 64 KiB uses consecutive descriptors
    # separated by __AHINCR. Each advances the base and reduces the remaining
    # limit by 64 KiB. Write through the second selector and inspect both
    # limits.
    body.extend(b'\xb8\x02\x00')      # mov ax, 2 GMEM_MOVEABLE
    body.extend(b'\x50')              # push ax
    body.extend(b'\xb8\x02\x00')      # mov ax, 2 size, high word
    body.extend(b'\x50')              # push ax
    body.extend(b'\x31\xc0')          # xor ax, ax
    body.extend(b'\x50')              # push ax size, low word
    farcall('import', 1, 15)          # KERNEL.15 GlobalAlloc
    body.extend(b'\x89\xc7')          # mov di, ax keep the handle
    body.extend(b'\x57')              # push di
    farcall('import', 1, 18)          # KERNEL.18 GlobalLock
    body.extend(b'\x8e\xc2')          # mov es, dx the first tile
    body.extend(b'\x26\xc6\x06\x00\x00\xa5')  # mov byte es:[0], 0xa5
    body.extend(b'\x8c\xc0')          # mov ax, es
    body.extend(b'\x83\xc0\x08')      # add ax, 8 __AHINCR
    body.extend(b'\x50')              # push ax
    body.extend(b'\x8e\xc0')          # mov es, ax the second tile
    body.extend(b'\x26\xc6\x06\x00\x00\x5a')  # mov byte es:[0], 0x5a
    farcall('import', 1, 188)         # KERNEL.188 GetSelectorLimit, tile two
    body.extend(b'\x8c\xc0')          # mov ax, es
    body.extend(b'\x83\xe8\x08')      # sub ax, 8
    body.extend(b'\x50')              # push ax
    farcall('import', 1, 188)         # KERNEL.188 GetSelectorLimit, tile one
    internal_at = len(body) + 1
    farcall('internal', 1, 0)         # a far call inside this segment
    farcall('import', 2, 5)           # USER.5 InitApp
    end = len(body)
    body.extend(b'\xeb\xfe')          # jmp $
    body[jump_at + 1] = end - jump_at - 2
    while len(body) % 16:
        body.append(0x90)
    internal_target = len(body)
    body.extend(b'\xcb')              # retf
    while len(body) % 16:
        body.append(0x90)
    records = []
    for source, kind, target, value in fixups:
        if kind == 'import':
            records.append(struct.pack('<BBHHH', 3, 1, source, target, value))
        else:
            records.append(struct.pack('<BBHHH', 3, 0, source, 1, internal_target))
    body[internal_at:internal_at + 2] = struct.pack('<H', 0xffff)
    return bytes(body), struct.pack('<H', len(records)) + b''.join(records)


# The second fixture: one Win16 task that starts another one and talks to it.
# The data segment carries the three strings and the answer buffer behind the
# instance words KRNL386 writes at 0000h..0010h.
TASK_COMMAND, TASK_CLASS, TASK_TITLE, TASK_BUFFER = 0x20, 0x40, 0x50, 0x60


def task_data():
    data = bytearray(0xc0)
    def put(at, text):
        raw = text.encode('cp1252') + b'\0'
        data[at:at + len(raw)] = raw
    put(TASK_COMMAND, 'C:\\WINDOWS\\WINVER.EXE')
    put(TASK_CLASS, '#32770')
    put(TASK_TITLE, 'TASKS ')
    return bytes(data)


def task_segment():
    """A task that starts WINVER.EXE, waits for its window and asks it its text.

    Nothing of this is a shortcut past the federation: WinExec makes the second
    Win16 task, Yield is where this one gives the processor up, FindWindow
    looks into the window list both tasks share, and SendMessage crosses from
    one task to the other. The answer goes into a message box, so the window
    list of the run carries what came back.
    """
    body = bytearray()
    fixups = []

    def farcall(target, value):
        body.extend(b'\x9a\xff\xff\x00\x00')
        fixups.append((len(body) - 4, target, value))

    def push_immediate(value):
        body.extend(b'\xb8' + struct.pack('<H', value))   # mov ax, value
        body.extend(b'\x50')                              # push ax

    farcall(1, 91)                       # KERNEL.91 InitTask
    body.extend(b'\x0b\xc0')             # or ax, ax
    fail_at = len(body)
    body.extend(b'\x74\x00')             # je end
    body.extend(b'\x57')                  # push di the instance
    farcall(2, 5)                        # USER.5 InitApp
    # WinExec(command, SW_SHOWNORMAL)
    body.extend(b'\x1e')                  # push ds
    push_immediate(TASK_COMMAND)
    push_immediate(1)
    farcall(1, 166)                      # KERNEL.166 WinExec
    # do { Yield; } while (!FindWindow("#32770", 0));
    # Yield comes first, so the loop gives the processor up at least once even
    # when the other task is already there.
    loop_at = len(body)
    farcall(1, 29)                       # KERNEL.29 Yield
    body.extend(b'\x1e')                  # push ds
    push_immediate(TASK_CLASS)
    push_immediate(0)                    # a null far pointer for the title
    push_immediate(0)
    farcall(2, 50)                       # USER.50 FindWindow
    body.extend(b'\x0b\xc0')             # or ax, ax
    found_at = len(body)
    body.extend(b'\x75\x00')             # jne found
    body.extend(b'\xe9\x00\x00')         # jmp loop
    body[len(body) - 2:] = struct.pack('<h', loop_at - len(body))
    body[found_at + 1] = len(body) - found_at - 2
    body.extend(b'\x89\xc6')             # mov si, ax the other task's window
    # SendMessage(window, WM_GETTEXTLENGTH, 0, 0). The message carries no
    # pointer, so the answer says by itself that the call crossed into the
    # other task: it is the length of that task's window title.
    body.extend(b'\x56')                  # push si
    push_immediate(0x000e)
    push_immediate(0)
    push_immediate(0)
    push_immediate(0)
    farcall(2, 111)                      # USER.111 SendMessage
    # The answer becomes the last character of the caption, so the window list
    # of the run carries what the other task answered.
    body.extend(b'\x04\x30')             # add al, '0'
    body.extend(b'\xa2' + struct.pack('<H', TASK_TITLE + 6))       # mov [title+6], al
    body.extend(b'\xc6\x06' + struct.pack('<H', TASK_TITLE + 7) + b'\x00')  # mov [title+7], 0
    # MessageBox(0, buffer, "TASKS", MB_OK)
    push_immediate(0)
    body.extend(b'\x1e')                  # push ds
    push_immediate(TASK_BUFFER)
    body.extend(b'\x1e')                  # push ds
    push_immediate(TASK_TITLE)
    push_immediate(0)
    farcall(2, 1)                        # USER.1 MessageBox
    end = len(body)
    body.extend(b'\xeb\xfe')             # jmp $
    body[fail_at + 1] = end - fail_at - 2
    while len(body) % 16:
        body.append(0x90)
    records = []
    for source, target, value in fixups:
        records.append(struct.pack('<BBHHH', 3, 1, source, target, value))
    return bytes(body), struct.pack('<H', len(records)) + b''.join(records)


# The third fixture: the SOUND module. It opens the sound, sets an accent the
# driver accepts and one it refuses, queues a note, counts it, asks for the
# device a second time and closes it. Every answer becomes one character of the
# window title, so the window list of the run carries the whole contract.
SOUND_TITLE, SOUND_TEXT, SOUND_OUTPUT, SOUND_HWND, SOUND_CAPS, SOUND_FORMAT, SOUND_HEADER, \
    SOUND_CLASS, SOUND_WINDOW, SOUND_MSG = 0x20, 0x50, 0x70, 0x74, 0x80, 0xb8, 0xc8, 0x110, 0x118, 0x120


def sound_data():
    data = bytearray(384)
    data[SOUND_TEXT:SOUND_TEXT + 6] = b'SOUND\x00'
    data[SOUND_OUTPUT:SOUND_OUTPUT + 2] = struct.pack('<H', 0xa55a)
    data[SOUND_OUTPUT + 2:SOUND_OUTPUT + 4] = struct.pack('<H', 0xbeef)
    data[SOUND_FORMAT:SOUND_FORMAT + 18] = struct.pack('<HHIIHHH', 1, 1, 11025, 11025, 1, 8, 0)
    data[SOUND_HEADER:SOUND_HEADER + 32] = struct.pack('<HHIIIIIHHI', 0xf0, 0, 8, 0, 0, 0, 0, 0, 0, 0)
    data[0xf0:0xf8] = bytes([128]) * 8
    data[SOUND_CLASS:SOUND_CLASS + 7] = b'STATIC\0'
    data[SOUND_WINDOW:SOUND_WINDOW + 5] = b'wave\0'
    return bytes(data)


def sound_segment():
    body = bytearray()
    fixups = []

    def farcall(target, value):
        body.extend(b'\x9a\xff\xff\x00\x00')
        fixups.append((len(body) - 4, target, value))

    def push_immediate(value):
        body.extend(b'\xb8' + struct.pack('<H', value))   # mov ax, value
        body.extend(b'\x50')                              # push ax

    def store(slot):
        body.extend(b'\x04\x30')                          # add al, '0'
        body.extend(b'\xa2' + struct.pack('<H', SOUND_TITLE + slot))

    farcall(1, 91)                       # KERNEL.91 InitTask
    body.extend(b'\x57')                 # push di
    farcall(2, 5)                        # USER.5 InitApp
    farcall(3, 1)                        # SOUND.1 OpenSound -> one voice
    store(0)
    for word in (1, 120, 0, 0, 0):       # SetVoiceAccent, an accent it takes
        push_immediate(word)
    farcall(3, 5)
    store(1)
    for word in (1, 10, 0, 0, 0):        # a tempo below 32, refused with -8
        push_immediate(word)
    farcall(3, 5)
    body.extend(b'\xf7\xd8')             # neg ax
    store(2)
    for word in (1, 60, 4, 0):           # SetVoiceNote, one quarter note
        push_immediate(word)
    farcall(3, 4)
    push_immediate(1)                    # CountVoiceNotes -> zero, nothing started
    farcall(3, 13)
    store(3)
    farcall(3, 1)                        # OpenSound again -> -1, it is open
    body.extend(b'\xf7\xd8')             # neg ax
    store(4)
    farcall(3, 2)                        # CloseSound
    push_immediate(1)                    # CountVoiceNotes of a closed device
    farcall(3, 13)
    store(5)
    push_immediate(0)                    # waveOutGetDevCaps(0, caps, 50)
    body.extend(b'\x1e')                 # push ds, high half of the far pointer
    push_immediate(SOUND_CAPS)
    push_immediate(50)
    farcall(4, 402)
    store(6)                             # success is zero
    body.extend(b'\xa0' + struct.pack('<H', SOUND_CAPS + 44))  # wChannels is two
    store(7)
    body.extend(b'\xa0' + struct.pack('<H', SOUND_CAPS + 46))  # dwSupport is at 2Eh
    body.extend(b'\x3c\x2c')             # cmp al, 2Ch
    body.extend(b'\x0f\x94\xc0')         # sete al
    store(8)
    body.extend(b'\x1e')                  # waveOutOpen(&output, 0, &format, 0, 0, QUERY)
    push_immediate(SOUND_OUTPUT)
    push_immediate(0)
    body.extend(b'\x1e')
    push_immediate(SOUND_FORMAT)
    push_immediate(0)
    push_immediate(0)
    push_immediate(0)
    push_immediate(0)
    push_immediate(0)                     # high word of WAVE_FORMAT_QUERY
    push_immediate(1)                     # low word
    farcall(4, 404)
    store(9)
    body.extend(b'\x81\x3e' + struct.pack('<H', SOUND_OUTPUT) + b'\x5a\xa5')
    body.extend(b'\x0f\x94\xc0')         # query leaves the caller's output sentinel unchanged
    store(10)
    # A real open owns a WORD handle. Prepare and unprepare translate the
    # caller's 20h-byte WAVEHDR16 to a durable native header, and close frees
    # both mappings.
    body.extend(b'\x8c\xd8\xa3' + struct.pack('<H', SOUND_HEADER + 2))
    body.extend(b'\x1e'); push_immediate(SOUND_OUTPUT)
    push_immediate(0); body.extend(b'\x1e'); push_immediate(SOUND_FORMAT)
    for _ in range(6): push_immediate(0)
    farcall(4, 404); store(11)
    body.extend(b'\x83\x3e' + struct.pack('<H', SOUND_OUTPUT) + b'\x00')
    body.extend(b'\x0f\x95\xc0'); store(12)
    body.extend(b'\x81\x3e' + struct.pack('<H', SOUND_OUTPUT + 2) + b'\xef\xbe')
    body.extend(b'\x0f\x94\xc0'); store(13)
    body.extend(b'\xff\x36' + struct.pack('<H', SOUND_OUTPUT))
    body.extend(b'\x1e'); push_immediate(SOUND_HEADER); push_immediate(32)
    farcall(4, 406); store(14)
    body.extend(b'\xf6\x06' + struct.pack('<H', SOUND_HEADER + 16) + b'\x02')
    body.extend(b'\x0f\x95\xc0'); store(15)
    body.extend(b'\xff\x36' + struct.pack('<H', SOUND_OUTPUT))
    body.extend(b'\x1e'); push_immediate(SOUND_HEADER); push_immediate(32)
    farcall(4, 407); store(16)
    body.extend(b'\xf6\x06' + struct.pack('<H', SOUND_HEADER + 16) + b'\x02')
    body.extend(b'\x0f\x94\xc0'); store(17)
    body.extend(b'\xff\x36' + struct.pack('<H', SOUND_OUTPUT))
    farcall(4, 405); store(18)
    # A native STATIC window gives CALLBACK_WINDOW a real owner queue without
    # adding a fixture-specific window procedure.
    body.extend(b'\x1e'); push_immediate(SOUND_CLASS)
    body.extend(b'\x1e'); push_immediate(SOUND_WINDOW)
    for _ in range(11): push_immediate(0)
    farcall(2, 41)
    body.extend(b'\xa3' + struct.pack('<H', SOUND_HWND))
    # Reopen with CALLBACK_WINDOW and submit the same header. Reset retires it
    # asynchronously; GetMessage observes the original FAR header in WOM_DONE.
    body.extend(b'\x1e'); push_immediate(SOUND_OUTPUT)
    push_immediate(0); body.extend(b'\x1e'); push_immediate(SOUND_FORMAT)
    push_immediate(0); body.extend(b'\xff\x36' + struct.pack('<H', SOUND_HWND))
    push_immediate(0); push_immediate(0); push_immediate(1); push_immediate(0)
    farcall(4, 404); store(19)
    body.extend(b'\x1e'); push_immediate(SOUND_MSG)
    body.extend(b'\xff\x36' + struct.pack('<H', SOUND_HWND))
    push_immediate(0); push_immediate(0)
    farcall(2, 108)
    body.extend(b'\x81\x3e' + struct.pack('<H', SOUND_MSG + 2) + b'\xbb\x03')
    body.extend(b'\x0f\x94\xc0'); store(20)
    body.extend(b'\xff\x36' + struct.pack('<H', SOUND_OUTPUT))
    body.extend(b'\x1e'); push_immediate(SOUND_HEADER); push_immediate(32)
    farcall(4, 406); store(21)
    body.extend(b'\xff\x36' + struct.pack('<H', SOUND_OUTPUT))
    body.extend(b'\x1e'); push_immediate(SOUND_HEADER); push_immediate(32)
    farcall(4, 408); store(22)
    body.extend(b'\xf6\x06' + struct.pack('<H', SOUND_HEADER + 16) + b'\x10')
    body.extend(b'\x0f\x95\xc0'); store(23)
    body.extend(b'\xff\x36' + struct.pack('<H', SOUND_OUTPUT))
    farcall(4, 411); store(24)
    body.extend(b'\x1e'); push_immediate(SOUND_MSG)
    body.extend(b'\xff\x36' + struct.pack('<H', SOUND_HWND))
    push_immediate(0); push_immediate(0)
    farcall(2, 108)
    body.extend(b'\x81\x3e' + struct.pack('<H', SOUND_MSG + 2) + b'\xbd\x03')
    body.extend(b'\x0f\x94\xc0'); store(25)
    body.extend(b'\xa1' + struct.pack('<H', SOUND_MSG + 4))
    body.extend(b'\x3b\x06' + struct.pack('<H', SOUND_OUTPUT))
    body.extend(b'\x0f\x94\xc0'); store(26)
    body.extend(b'\x81\x3e' + struct.pack('<H', SOUND_MSG + 6) + struct.pack('<H', SOUND_HEADER))
    body.extend(b'\x0f\x94\xc0'); store(27)
    body.extend(b'\x8c\xd8\x3b\x06' + struct.pack('<H', SOUND_MSG + 8))
    body.extend(b'\x0f\x94\xc0'); store(28)
    body.extend(b'\xf6\x06' + struct.pack('<H', SOUND_HEADER + 16) + b'\x01')
    body.extend(b'\x0f\x95\xc0'); store(29)
    body.extend(b'\xf6\x06' + struct.pack('<H', SOUND_HEADER + 16) + b'\x10')
    body.extend(b'\x0f\x94\xc0'); store(30)
    body.extend(b'\xff\x36' + struct.pack('<H', SOUND_OUTPUT))
    body.extend(b'\x1e'); push_immediate(SOUND_HEADER); push_immediate(32)
    farcall(4, 407); store(31)
    body.extend(b'\xff\x36' + struct.pack('<H', SOUND_OUTPUT)); farcall(4, 405); store(32)
    body.extend(b'\x1e'); push_immediate(SOUND_MSG)
    body.extend(b'\xff\x36' + struct.pack('<H', SOUND_HWND))
    push_immediate(0); push_immediate(0); farcall(2, 108)
    body.extend(b'\x81\x3e' + struct.pack('<H', SOUND_MSG + 2) + b'\xbc\x03')
    body.extend(b'\x0f\x94\xc0'); store(33)
    body.extend(b'\x1e'); push_immediate(SOUND_MSG)
    body.extend(b'\xff\x36' + struct.pack('<H', SOUND_HWND))
    push_immediate(0); push_immediate(0); push_immediate(1); farcall(2, 109)
    body.extend(b'\x0b\xc0\x0f\x94\xc0'); store(34)
    body.extend(b'\xc6\x06' + struct.pack('<H', SOUND_TITLE + 35) + b'\x00')
    push_immediate(0)                    # MessageBox(0, text, title, 0)
    body.extend(b'\x1e')
    push_immediate(SOUND_TEXT)
    body.extend(b'\x1e')
    push_immediate(SOUND_TITLE)
    push_immediate(0)
    farcall(2, 1)
    body.extend(b'\xeb\xfe')             # jmp $
    while len(body) % 16:
        body.append(0x90)
    records = []
    for source, target, value in fixups:
        records.append(struct.pack('<BBHHH', 3, 1, source, target, value))
    return bytes(body), struct.pack('<H', len(records)) + b''.join(records)


def build(segment=None, data=None, name='FIXTURE', modules=('KERNEL', 'USER'), library=False,
          exports=(), entry=0):
    """A program, or with `library` a DLL. `exports` are (name, offset) pairs
    of the code segment, exported by ordinal 1, 2,... through the resident
    name table; `entry` is the offset of the entry point (LibMain)."""
    code, relocations = (segment or code_segment)()
    data = data if data is not None else bytes(64)
    align = 4
    header = bytearray(64)
    tables = bytearray()
    # Entry table: one bundle of fixed entries in segment one, or empty.
    entry_offset = 64
    if exports:
        tables += bytes([len(exports), 1])
        for _, offset in exports:
            tables += b'\x01' + struct.pack('<H', offset)  # exported
    tables += b'\x00'
    resident_offset = 64 + len(tables)
    tables += counted(name) + struct.pack('<H', 0)
    for ordinal, (export, _) in enumerate(exports, 1):
        tables += counted(export) + struct.pack('<H', ordinal)
    tables += b'\x00'
    imported_offset = 64 + len(tables)
    names = {}
    imported = bytearray(b'\x00')
    for module in modules:
        names[module] = len(imported)
        imported += counted(module)
    imported += b'\x00'
    tables += imported
    module_offset = 64 + len(tables)
    tables += b''.join(struct.pack('<H', names[module]) for module in modules)
    segment_offset = 64 + len(tables)
    tables += bytes(16)
    head = bytearray(64)
    head[0:2] = b'MZ'
    head[0x3c:0x40] = struct.pack('<I', 64)
    body_start = 64 + 64 + len(tables)
    first = (body_start + 15) // 16 * 16
    code_sector = first // (1 << align)
    data_sector = (first + ((len(code) + len(relocations) + 15) // 16 * 16)) // (1 << align)
    segments = struct.pack('<HHHH', code_sector, len(code), 0x0100, len(code))
    segments += struct.pack('<HHHH', data_sector, len(data), 0x0001, 0x0100)
    tables[segment_offset - 64:segment_offset - 64 + 16] = segments
    header[0:2] = b'NE'
    header[2] = 5
    header[3] = 10
    header[0x04:0x06] = struct.pack('<H', entry_offset)
    header[0x06:0x08] = struct.pack('<H', 1)
    header[0x0c:0x0e] = struct.pack('<H', 0x8001 if library else 0x0302)
    header[0x0e:0x10] = struct.pack('<H', 2)       # automatic data segment
    header[0x10:0x12] = struct.pack('<H', 0x0100)  # heap
    header[0x12:0x14] = struct.pack('<H', 0x0400)  # stack
    header[0x14:0x18] = struct.pack('<HH', entry, 1)   # entry IP:CS
    header[0x18:0x1c] = struct.pack('<HH', 0, 0 if library else 2)   # stack SP:SS
    header[0x1c:0x1e] = struct.pack('<H', 2)
    header[0x1e:0x20] = struct.pack('<H', len(modules))
    header[0x22:0x24] = struct.pack('<H', segment_offset)
    header[0x24:0x26] = struct.pack('<H', resident_offset)
    header[0x26:0x28] = struct.pack('<H', resident_offset)
    header[0x28:0x2a] = struct.pack('<H', module_offset)
    header[0x2a:0x2c] = struct.pack('<H', imported_offset)
    header[0x32:0x34] = struct.pack('<H', align)
    header[0x36] = 2
    header[0x3e:0x40] = struct.pack('<H', 0x030a)
    image = bytearray(head + header + tables)
    image += bytes(first - len(image))
    image += code + relocations
    image += bytes(data_sector * (1 << align) - len(image))
    image += data
    return bytes(image)


def assemble(source, program, name, environment=None, modules=('KERNEL', 'USER')):
    """The New Executable of a nasm source that writes three flat parts behind
    a header of their sizes: code, data and the relocation records of the far
    calls (offset of the pointer, module, ordinal); tools/layer_win16_fixture.asm.
    `modules` names the modules the records count from one."""
    import subprocess
    from pathlib import Path
    program = Path(program)
    raw = program.with_suffix('.bin')
    subprocess.run(['nasm', '-f', 'bin', str(source), '-o', str(raw)], env=environment, check=True)
    blob = raw.read_bytes()
    code_size, data_size, count = struct.unpack_from('<HHH', blob)
    code = bytearray(blob[6:6 + code_size])
    data = blob[6 + code_size:6 + code_size + data_size]
    fixes = blob[6 + code_size + data_size:]
    assert len(fixes) == count * 6, (len(fixes), count)
    while len(code) % 16:
        code.append(0x90)
    records = b''.join(struct.pack('<BBHHH', 3, 1, *struct.unpack_from('<HHH', fixes, i * 6)) for i in range(count))
    program.write_bytes(build(lambda: (bytes(code), struct.pack('<H', count) + records), data, name, tuple(modules)))
    return program


def build_tasks():
    return build(task_segment, task_data(), 'TASKS')


def build_sound():
    return build(sound_segment, sound_data(), 'SNDFIX', ('KERNEL', 'USER', 'SOUND', 'MMSYSTEM'))


def voice_segment():
    """The note queue of MMSOUND.DRV end to end.

    Every answer is compared in the program itself; the first one that differs
    ends the task with its number as the exit code, so exit code zero means
    every answer was the one MMSOUND gives. What the speaker played is checked
    by tools/test_win16_sound_voice.cpp in the frames of the mixer."""
    body = bytearray()
    fixups = []
    KERNEL, USER, SOUND = 1, 2, 3

    def farcall(module, ordinal):
        body.extend(b'\x9a\xff\xff\x00\x00')
        fixups.append((len(body) - 4, module, ordinal))

    def call(module, ordinal, *words):
        for word in words:
            body.extend(b'\xb8' + struct.pack('<H', word & 0xffff) + b'\x50')
        farcall(module, ordinal)

    def fail(index):
        body.extend(b'\xb8' + struct.pack('<H', 0x4c00 | index) + b'\xcd\x21')

    def expect(value, index):
        body.extend(b'\x3d' + struct.pack('<H', value & 0xffff) + b'\x74\x05')
        fail(index)

    def expect_nonzero(index):
        body.extend(b'\x0b\xc0\x75\x05')     # or ax, ax; jnz +5
        fail(index)

    def wait(milliseconds):
        farcall(USER, 13)                    # GetTickCount
        body.extend(b'\x89\xc6')             # mov si, ax
        top = len(body)
        farcall(KERNEL, 29)                  # Yield
        farcall(USER, 13)
        body.extend(b'\x29\xf0')             # sub ax, si
        body.extend(b'\x3d' + struct.pack('<H', milliseconds))
        body.extend(b'\x72' + bytes([(top - (len(body) + 2)) & 0xff]))  # jb top

    farcall(KERNEL, 91)                      # InitTask
    body.extend(b'\x57')
    farcall(USER, 5)                         # InitApp
    call(SOUND, 1); expect(1, 1)             # OpenSound: one voice
    # Normal articulation, tempo 120: a quarter is 0x17700/480 = 200 ticks,
    # seven eighths sound (175) and the rest pause (25), each halved with
    # rounding up : 88 and 13 callbacks of five milliseconds.
    call(SOUND, 5, 1, 120, 128, 0, 0); expect(0, 2)
    call(SOUND, 4, 1, 34, 4, 0); expect(0, 3)          # A4, 440 Hz
    call(SOUND, 13, 1); expect(0, 4)                   # nothing runs yet: zero
    # Staccato: three quarters sound, 150 and 50 ticks, 75 and 25 callbacks.
    call(SOUND, 5, 1, 120, 128, 2, 0)
    call(SOUND, 4, 1, 46, 4, 0); expect(0, 5)          # A5, 880 Hz
    # Legato: all sound. An eighth is 100 ticks, 50 callbacks.
    call(SOUND, 5, 1, 120, 128, 1, 0)
    call(SOUND, 4, 1, 41, 8, 0); expect(0, 6)          # E5, 659 Hz
    call(SOUND, 4, 1, 0, 4, 0); expect(0, 7)           # a quarter rest
    call(SOUND, 4, 1, 34, 4, 0); expect(0, 8)          # A4 legato, 100 callbacks
    call(SOUND, 4, 1, 0x55, 4, 0); expect(0xfffb, 9)   # note above 84: -5
    call(SOUND, 8, 1, 20, 0, 4); expect(0xfff3, 10)    # 20 Hz: -13
    call(SOUND, 16, 1, 2); expect(0, 11)               # SetVoiceThreshold
    call(SOUND, 9)                                     # StartSound
    call(SOUND, 13, 1); expect_nonzero(12)             # it runs now
    call(SOUND, 3, 1, 60); expect(0xfffd, 13)          # no resize while playing
    call(SOUND, 11, 1); expect(0, 14)                  # until two notes are left
    call(SOUND, 13, 1); expect(2, 15)
    call(SOUND, 11, 0); expect(0, 16)                  # until the queue is empty
    call(SOUND, 13, 1); expect(0, 17)
    call(SOUND, 15); expect(0, 18)                     # the status was taken
    wait(200)
    # SetVoiceSound: 1000 Hz for 40 ticks, halved to 20 callbacks, 100 ms.
    call(SOUND, 8, 1, 1000, 0, 40); expect(0, 22)
    call(SOUND, 9)
    call(SOUND, 11, 0); expect(0, 23)
    wait(200)
    # StopSound in the middle of a whole note (2 s) silences the speaker.
    call(SOUND, 4, 1, 34, 1, 0); expect(0, 19)
    call(SOUND, 9)
    wait(300)
    call(SOUND, 10)
    call(SOUND, 13, 1); expect(0, 20)
    wait(300)
    # The task ends in the middle of another whole note, without CloseSound.
    call(SOUND, 4, 1, 46, 1, 0); expect(0, 21)
    call(SOUND, 9)
    wait(300)
    body.extend(b'\xb8\x00\x4c\xcd\x21')              # exit code zero
    while len(body) % 16:
        body.append(0x90)
    records = [struct.pack('<BBHHH', 3, 1, source, module, ordinal)
               for source, module, ordinal in fixups]
    return bytes(body), struct.pack('<H', len(records)) + b''.join(records)


def build_sound_voice():
    return build(voice_segment, bytes(64), 'SNDVOICE', ('KERNEL', 'USER', 'SOUND'))


ENUM_CAPTION = 0x20


def enumfonts_data():
    data = bytearray(0x80)
    data[ENUM_CAPTION:ENUM_CAPTION + 7] = b'ENUM 0\0'
    return bytes(data)


def enumfonts_segment(ordinal=70):
    """Calls GDI.70 or GDI.330 and checks the original far callback frame in guest code.

    Both entries share GDI and hand over ENUMLOGFONT with its
    full name behind the 50 bytes of LOGFONT16."""
    body = bytearray()
    fixups = []

    def farcall(module, ordinal):
        body.extend(b'\x9a\xff\xff\x00\x00')
        fixups.append((len(body) - 4, module, ordinal))

    def push(value):
        body.extend(b'\xb8' + struct.pack('<H', value) + b'\x50')

    farcall(1, 91)                       # InitTask
    body.extend(b'\x57')                 # instance
    farcall(2, 5)                        # InitApp
    push(0)
    farcall(3, 52)                       # CreateCompatibleDC(NULL)
    body.extend(b'\x50')                 # HDC
    push(0); push(0)                     # NULL face
    body.extend(b'\x0e')                 # callback selector
    callback_push = len(body) + 1
    push(0xffff)                         # callback offset
    push(0x1234); push(0x5678)           # LPARAM
    farcall(3, ordinal)                  # EnumFonts or EnumFontFamilies
    push(0); push(0)                     # owner
    push(0); push(0)                     # empty message text
    body.extend(b'\x1e'); push(ENUM_CAPTION)
    push(0)
    farcall(2, 1)                        # MessageBox
    body.extend(b'\xeb\xfe')             # keep the window alive

    callback = len(body)
    body[callback_push:callback_push + 2] = struct.pack('<H', callback)
    # int FAR PASCAL callback(LOGFONT16 far *, TEXTMETRIC16 far *, WORD, LPARAM)
    body.extend(b'\x55\x89\xe5')        # push bp; mov bp,sp
    body.extend(b'\x81\x7e\x06\x78\x56')  # cmp [bp+6],5678
    failures = [len(body) + 1]; body.extend(b'\x75\x00')
    body.extend(b'\x81\x7e\x08\x34\x12')  # cmp [bp+8],1234
    failures.append(len(body) + 1); body.extend(b'\x75\x00')
    body.extend(b'\x83\x7e\x0a\x01')  # cmp [bp+a],1 (RASTER_FONTTYPE)
    failures.append(len(body) + 1); body.extend(b'\x75\x00')
    body.extend(b'\xc4\x76\x10')       # les si,[bp+10] LOGFONT16
    body.extend(b'\x26\x80\x7c\x12\x00')  # cmp byte es:[si+18],0
    failures.append(len(body) + 1); body.extend(b'\x74\x00')
    body.extend(b'\x26\x80\x7c\x32\x00')  # cmp byte es:[si+50],0 elfFullName
    failures.append(len(body) + 1); body.extend(b'\x74\x00')
    body.extend(b'\xc4\x76\x0c')       # les si,[bp+c] TEXTMETRIC16
    body.extend(b'\x26\x83\x3c\x00')  # cmp word es:[si],0
    failures.append(len(body) + 1); body.extend(b'\x74\x00')
    body.extend(b'\xfe\x06' + struct.pack('<H', ENUM_CAPTION + 5))
    done = len(body)
    for displacement in failures:
        body[displacement] = done - displacement - 1
    body.extend(b'\x31\xc0\x5d\xca\x0e\x00')  # stop enumeration; retf 14
    while len(body) % 16:
        body.append(0x90)
    records = [struct.pack('<BBHHH', 3, 1, source, module, ordinal)
               for source, module, ordinal in fixups]
    return bytes(body), struct.pack('<H', len(records)) + b''.join(records)


def build_enumfonts(ordinal=70):
    return build(lambda: enumfonts_segment(ordinal), enumfonts_data(),
                 'ENUMFONT' if ordinal == 70 else 'ENUMFAM', ('KERNEL', 'USER', 'GDI'))


ALIGN_CAPTION = 0x20


def textalign_data():
    data = bytearray(0x60)
    data[ALIGN_CAPTION:ALIGN_CAPTION + 8] = b'ALIGN 0\0'
    return bytes(data)


def textalign_segment():
    """Checks GDI.346's previous value and invalid-alignment contract."""
    body = bytearray()
    fixups = []

    def farcall(module, ordinal):
        body.extend(b'\x9a\xff\xff\x00\x00')
        fixups.append((len(body) - 4, module, ordinal))

    def push(value):
        body.extend(b'\xb8' + struct.pack('<H', value) + b'\x50')

    def set_align(value, expected):
        body.extend(b'\x56')             # HDC saved in SI
        push(value)
        farcall(3, 346)
        body.extend(b'\x3d' + struct.pack('<H', expected))
        failures.append(len(body) + 1)
        body.extend(b'\x75\x00')

    farcall(1, 91)
    body.extend(b'\x57')
    farcall(2, 5)
    push(0)
    farcall(3, 52)                       # CreateCompatibleDC(NULL)
    body.extend(b'\x89\xc6')             # SI = HDC
    failures = []
    set_align(2, 0)                      # TA_RIGHT replaces TA_LEFT
    set_align(6, 2)                      # TA_CENTER returns TA_RIGHT
    set_align(0x20, 0xffff)              # bits outside 001Fh are invalid
    set_align(0, 6)                      # invalid call did not change state
    body.extend(b'\xfe\x06' + struct.pack('<H', ALIGN_CAPTION + 6))
    done = len(body)
    for displacement in failures:
        body[displacement] = done - displacement - 1
    push(0); push(0)
    push(0); push(0)
    body.extend(b'\x1e'); push(ALIGN_CAPTION)
    push(0)
    farcall(2, 1)
    body.extend(b'\xeb\xfe')
    while len(body) % 16:
        body.append(0x90)
    records = [struct.pack('<BBHHH', 3, 1, source, module, ordinal)
               for source, module, ordinal in fixups]
    return bytes(body), struct.pack('<H', len(records)) + b''.join(records)


def build_textalign():
    return build(textalign_segment, textalign_data(), 'ALIGN', ('KERNEL', 'USER', 'GDI'))


DOS_CAPTION, DOS_D_DIRECTORY, DOS_PRESENT, DOS_MISSING, DOS_PATTERN, DOS_BUFFER = 0x20, 0x30, 0x40, 0x50, 0x60, 0x80
DOS_NEW, DOS_BYTES = 0xc0, 0xe0


def dos_data():
    data = bytearray(0x100)
    data[DOS_CAPTION:DOS_CAPTION + 6] = b'DOS 0\0'
    data[DOS_D_DIRECTORY:DOS_D_DIRECTORY + 7] = b'D:\\ONE\0'
    data[DOS_PRESENT:DOS_PRESENT + 15] = b'D:\\PRESENT.TXT\0'
    data[DOS_MISSING:DOS_MISSING + 15] = b'D:\\MISSING.TXT\0'
    data[DOS_PATTERN:DOS_PATTERN + 13] = b'D:\\NOTHERE.*\0'
    data[DOS_NEW:DOS_NEW + 11] = b'D:\\NEW.TXT\0'
    data[DOS_BYTES:DOS_BYTES + 4] = b'ABCD'
    return bytes(data)


def dos_segment():
    """Exercise DOS3Call drive state, IOCTL validation and extended errors."""
    body = bytearray()
    fixups = []
    failures = []

    def farcall(module, ordinal):
        body.extend(b'\x9a\xff\xff\x00\x00')
        fixups.append((len(body) - 4, module, ordinal))

    def push(value):
        body.extend(b'\xb8' + struct.pack('<H', value) + b'\x50')

    def mov_ax(value):
        body.extend(b'\xb8' + struct.pack('<H', value))

    def mov_dx(value):
        body.extend(b'\xba' + struct.pack('<H', value))

    def fail_if(opcode):
        body.extend(b'\x0f' + bytes([opcode]) + b'\x00\x00')
        failures.append(len(body) - 2)

    def doscall():
        farcall(1, 102)

    farcall(1, 91)                       # KERNEL.91 InitTask
    body.extend(b'\x57')                 # instance
    farcall(2, 5)                        # USER.5 InitApp

    # Park D:\ONE as D:'s directory without selecting D.
    mov_dx(DOS_D_DIRECTORY); mov_ax(0x3b00); doscall(); fail_if(0x82)  # jc fail
    mov_dx(3); mov_ax(0x0e00); doscall()  # select D (DL=3)
    body.extend(b'\x3c\x04'); fail_if(0x82)  # drive range includes C and D
    mov_ax(0x1900); doscall(); body.extend(b'\x3c\x03'); fail_if(0x85)

    # The selected drive uses its parked current directory.
    body.extend(b'\xbe' + struct.pack('<H', DOS_BUFFER))
    body.extend(b'\x31\xd2'); mov_ax(0x4700); doscall()
    body.extend(b'\x81\x3e' + struct.pack('<H', DOS_BUFFER) + b'ON')
    fail_if(0x85)
    body.extend(b'\x80\x3e' + struct.pack('<H', DOS_BUFFER + 2) + b'E')
    fail_if(0x85)
    body.extend(b'\xc6\x06' + struct.pack('<H', DOS_CAPTION + 4) + b'\x32')

    # Selecting an absent Z: leaves D current. Its IOCTL request does fail.
    mov_dx(25); mov_ax(0x0e00); doscall()
    mov_ax(0x1900); doscall(); body.extend(b'\x3c\x03'); fail_if(0x85)
    body.extend(b'\xbb\x1a\x00'); mov_ax(0x4408); doscall(); fail_if(0x83) # jnc fail
    body.extend(b'\x3d\x0f\x00'); fail_if(0x85)
    body.extend(b'\xc6\x06' + struct.pack('<H', DOS_CAPTION + 4) + b'\x33')

    # A mounted fixed disk is local, has no logical alias and is not removable.
    body.extend(b'\xbb\x04\x00'); mov_ax(0x4408); doscall(); fail_if(0x82)
    body.extend(b'\x3d\x01\x00'); fail_if(0x85)
    body.extend(b'\xbb\x04\x00'); mov_ax(0x4409); doscall(); fail_if(0x82)
    body.extend(b'\xf7\xc2\x00\x10'); fail_if(0x85)
    body.extend(b'\xbb\x04\x00'); mov_ax(0x440e); doscall(); fail_if(0x82)
    body.extend(b'\x3c\x00'); fail_if(0x85)
    body.extend(b'\xc6\x06' + struct.pack('<H', DOS_CAPTION + 4) + b'\x34')

    # A real disk-file handle reports a zero device word.
    mov_dx(DOS_PRESENT); mov_ax(0x3d00); doscall(); fail_if(0x82)
    body.extend(b'\x89\xc3'); mov_ax(0x4400); doscall(); fail_if(0x82)
    body.extend(b'\x83\xfa\x00'); fail_if(0x85)
    body.extend(b'\xc6\x06' + struct.pack('<H', DOS_CAPTION + 4) + b'\x35')

    # Failed open followed immediately by AH=59 returns DOS's table entry:
    # file not found, class NotFnd, ask user, disk locus.
    mov_dx(DOS_MISSING); mov_ax(0x3d00); doscall(); fail_if(0x83)
    body.extend(b'\x3d\x02\x00'); fail_if(0x85)
    body.extend(b'\x31\xdb'); mov_ax(0x5900); doscall(); fail_if(0x82)
    body.extend(b'\x3d\x02\x00'); fail_if(0x85)
    body.extend(b'\x81\xfb\x03\x08'); fail_if(0x85)
    body.extend(b'\x80\xfd\x02'); fail_if(0x85)
    body.extend(b'\xc6\x06' + struct.pack('<H', DOS_CAPTION + 4) + b'\x36')

    # Find-first failure refreshes the same extended error state. WINFILE uses
    # this path for empty directories and invalid aliases.
    mov_dx(DOS_PATTERN); body.extend(b'\x31\xc9'); mov_ax(0x4e00); doscall(); fail_if(0x83)
    body.extend(b'\x3d\x02\x00'); fail_if(0x85)
    body.extend(b'\x31\xdb'); mov_ax(0x5900); doscall()
    body.extend(b'\x3d\x02\x00'); fail_if(0x85)
    body.extend(b'\x81\xfb\x03\x08'); fail_if(0x85)
    body.extend(b'\x80\xfd\x02'); fail_if(0x85)

    # the DOS file functions share the file management of _lopen.
    # Two opens with "deny none" (AL=40h) of the same file both succeed; a
    # write of zero bytes truncates at the current position.
    mov_dx(DOS_PRESENT); mov_ax(0x3d40); doscall(); fail_if(0x82)
    body.extend(b'\x89\xc6')                                  # mov si, ax
    mov_dx(DOS_PRESENT); mov_ax(0x3d40); doscall(); fail_if(0x82)
    body.extend(b'\x89\xc3'); mov_ax(0x3e00); doscall(); fail_if(0x82)
    body.extend(b'\x89\xf3'); mov_ax(0x3e00); doscall(); fail_if(0x82)
    mov_dx(DOS_NEW); body.extend(b'\x31\xc9'); mov_ax(0x3c00); doscall(); fail_if(0x82)
    body.extend(b'\x89\xc3')                                  # mov bx, ax
    mov_dx(DOS_BYTES); body.extend(b'\xb9\x04\x00'); mov_ax(0x4000); doscall(); fail_if(0x82)
    body.extend(b'\x3d\x04\x00'); fail_if(0x85)
    body.extend(b'\x31\xc9\xba\x02\x00'); mov_ax(0x4200); doscall(); fail_if(0x82)
    body.extend(b'\x31\xc9'); mov_ax(0x4000); doscall(); fail_if(0x82)
    body.extend(b'\x31\xc9\x31\xd2'); mov_ax(0x4202); doscall(); fail_if(0x82)
    body.extend(b'\x3d\x02\x00'); fail_if(0x85)
    mov_ax(0x3e00); doscall(); fail_if(0x82)

    # Switching away and back restores D:\ONE.
    mov_dx(2); mov_ax(0x0e00); doscall(); mov_ax(0x1900); doscall()
    body.extend(b'\x3c\x02'); fail_if(0x85)
    mov_dx(3); mov_ax(0x0e00); doscall()
    body.extend(b'\xbe' + struct.pack('<H', DOS_BUFFER))
    body.extend(b'\x31\xd2'); mov_ax(0x4700); doscall()
    body.extend(b'\x81\x3e' + struct.pack('<H', DOS_BUFFER) + b'ON')
    fail_if(0x85)
    body.extend(b'\xc6\x06' + struct.pack('<H', DOS_CAPTION + 4) + b'\x31')

    show = len(body)
    for displacement in failures:
        body[displacement:displacement + 2] = struct.pack('<h', show - displacement - 2)
    push(0); push(0)
    push(0); push(0)
    body.extend(b'\x1e'); push(DOS_CAPTION)
    push(0); farcall(2, 1)               # MessageBox
    body.extend(b'\xeb\xfe')
    while len(body) % 16:
        body.append(0x90)
    records = [struct.pack('<BBHHH', 3, 1, source, module, ordinal)
               for source, module, ordinal in fixups]
    return bytes(body), struct.pack('<H', len(records)) + b''.join(records)


def build_dos():
    return build(dos_segment, dos_data(), 'DOSFIX', ('KERNEL', 'USER'))


REGION_CAPTION, REGION_RECT, REGION_INDIRECT, REGION_DIRECT, REGION_DEST = 0x20, 0x30, 0x40, 0x42, 0x44


def region_data():
    data = bytearray(0x80)
    data[REGION_CAPTION:REGION_CAPTION + 9] = b'REGION 0\0'
    data[REGION_RECT:REGION_RECT + 8] = struct.pack('<hhhh', -7, 3, 20, 31)
    return bytes(data)


def region_segment():
    """Check GDI.65's signed RECT16 conversion through a selected clip region."""
    body = bytearray()
    fixups = []
    failures = []

    def farcall(module, ordinal):
        body.extend(b'\x9a\xff\xff\x00\x00')
        fixups.append((len(body) - 4, module, ordinal))

    def push(value):
        body.extend(b'\xb8' + struct.pack('<H', value) + b'\x50')

    def fail_if(opcode):
        body.extend(b'\x0f' + bytes([opcode]) + b'\x00\x00')
        failures.append(len(body) - 2)

    farcall(1, 91)
    body.extend(b'\x57'); farcall(2, 5)
    body.extend(b'\x1e'); push(REGION_RECT)
    farcall(3, 65)
    body.extend(b'\x0b\xc0'); fail_if(0x84)  # jz fail
    body.extend(b'\xa3' + struct.pack('<H', REGION_INDIRECT))
    for value in (-7, 3, 20, 31):
        push(value & 0xffff)
    farcall(3, 64)
    body.extend(b'\xa3' + struct.pack('<H', REGION_DIRECT))
    for value in (0, 0, 0, 0):
        push(value)
    farcall(3, 64)
    body.extend(b'\xa3' + struct.pack('<H', REGION_DEST))
    # XOR with the region built by GDI.64 is empty only when all four signed
    # coordinates passed through GDI.65 unchanged.
    for slot in (REGION_DEST, REGION_INDIRECT, REGION_DIRECT):
        body.extend(b'\xff\x36' + struct.pack('<H', slot))
    push(3); farcall(3, 47)                # CombineRgn(..., RGN_XOR)
    body.extend(b'\x3d\x01\x00'); fail_if(0x85)  # NULLREGION
    # A null far pointer is rejected without inventing a region.
    push(0); push(0); farcall(3, 65)
    body.extend(b'\x0b\xc0'); fail_if(0x85)
    body.extend(b'\xc6\x06' + struct.pack('<H', REGION_CAPTION + 7) + b'\x31')
    show = len(body)
    for displacement in failures:
        body[displacement:displacement + 2] = struct.pack('<h', show - displacement - 2)
    push(0); push(0); push(0); push(0)
    body.extend(b'\x1e'); push(REGION_CAPTION); push(0); farcall(2, 1)
    body.extend(b'\xeb\xfe')
    while len(body) % 16:
        body.append(0x90)
    records = [struct.pack('<BBHHH', 3, 1, source, module, ordinal)
               for source, module, ordinal in fixups]
    return bytes(body), struct.pack('<H', len(records)) + b''.join(records)


def build_region():
    return build(region_segment, region_data(), 'REGION', ('KERNEL', 'USER', 'GDI'))


WINFLAGS_CAPTION, WINFLAGS_INFO = 0x20, 0x40


def winflags_data():
    data = bytearray(0x80)
    data[WINFLAGS_CAPTION:WINFLAGS_CAPTION + 11] = b'WINFLAGS 0\0'
    return bytes(data)


def winflags_segment():
    """GetWinFlags has WF_PAGING, and DPMI 0500h names a paging file."""
    body = bytearray()
    fixups = []
    failures = []

    def farcall(module, ordinal):
        body.extend(b'\x9a\xff\xff\x00\x00')
        fixups.append((len(body) - 4, module, ordinal))

    def push(value):
        body.extend(b'\xb8' + struct.pack('<H', value) + b'\x50')

    def fail_if(opcode):
        body.extend(b'\x0f' + bytes([opcode]) + b'\x00\x00')
        failures.append(len(body) - 2)

    farcall(1, 91)
    body.extend(b'\x57'); farcall(2, 5)
    farcall(1, 132)                             # GetWinFlags
    body.extend(b'\xa9\x00\x08'); fail_if(0x84)  # test ax, 0800h; jz fail
    body.extend(b'\xa9\x00\x10'); fail_if(0x85)  # test ax, 1000h; jnz fail (no DOS paging)
    body.extend(b'\x1e\x07')                    # push ds; pop es
    body.extend(b'\xbf' + struct.pack('<H', WINFLAGS_INFO))  # mov di, INFO
    body.extend(b'\xb8\x00\x05\xcd\x31')        # mov ax, 0500h; int 31h
    fail_if(0x82)                               # jc fail
    # +20h the paging file in pages: neither 0 nor FFFFFFFFh.
    body.extend(b'\xa1' + struct.pack('<H', WINFLAGS_INFO + 0x20))   # mov ax, [+20h]
    body.extend(b'\x0b\x06' + struct.pack('<H', WINFLAGS_INFO + 0x22))  # or ax, [+22h]
    fail_if(0x84)
    body.extend(b'\xa1' + struct.pack('<H', WINFLAGS_INFO + 0x20))
    body.extend(b'\x23\x06' + struct.pack('<H', WINFLAGS_INFO + 0x22))  # and ax, [+22h]
    body.extend(b'\x3d\xff\xff'); fail_if(0x84)   # cmp ax, FFFFh; jz fail
    # +18h the physical pages and +00h the largest free block, both set.
    body.extend(b'\xa1' + struct.pack('<H', WINFLAGS_INFO + 0x18))
    body.extend(b'\x0b\x06' + struct.pack('<H', WINFLAGS_INFO + 0x1a))
    fail_if(0x84)
    body.extend(b'\xa1' + struct.pack('<H', WINFLAGS_INFO))
    body.extend(b'\x0b\x06' + struct.pack('<H', WINFLAGS_INFO + 2))
    fail_if(0x84)
    body.extend(b'\xc6\x06' + struct.pack('<H', WINFLAGS_CAPTION + 9) + b'\x31')
    show = len(body)
    for displacement in failures:
        body[displacement:displacement + 2] = struct.pack('<h', show - displacement - 2)
    push(0); push(0); push(0); push(0)
    body.extend(b'\x1e'); push(WINFLAGS_CAPTION); push(0); farcall(2, 1)
    body.extend(b'\xeb\xfe')
    while len(body) % 16:
        body.append(0x90)
    records = [struct.pack('<BBHHH', 3, 1, source, module, ordinal)
               for source, module, ordinal in fixups]
    return bytes(body), struct.pack('<H', len(records)) + b''.join(records)


def build_winflags():
    return build(winflags_segment, winflags_data(), 'WINFLAGS', ('KERNEL', 'USER'))


MCI_CAPTION, MCI_OPEN, MCI_MISSING, MCI_PRESENT = 0x20, 0x30, 0x50, 0x60


def mci_data():
    data = bytearray(0x80)
    data[MCI_CAPTION:MCI_CAPTION + 6] = b'MCI 0\0'
    data[MCI_OPEN:MCI_OPEN + 20] = struct.pack('<IHHHHHHHH', 0, 0xa55a, 0xbeef,
                                               522, 0, MCI_MISSING, 0, 0, 0)
    data[MCI_MISSING:MCI_MISSING + 14] = b'D:\\ABSENT.WAV\0'
    data[MCI_PRESENT:MCI_PRESENT + 12] = b'D:\\TONE.WAV\0'
    return bytes(data)


def mci_segment():
    """Check MMSYSTEM.701's packed MCI_OPEN_PARMS16 and TYPE_ID pointer."""
    body = bytearray()
    fixups = []

    def farcall(module, ordinal):
        body.extend(b'\x9a\xff\xff\x00\x00')
        fixups.append((len(body) - 4, module, ordinal))

    def push(value):
        body.extend(b'\xb8' + struct.pack('<H', value) + b'\x50')

    def open_call():
        push(0); push(0x803); push(0); push(0x1200)
        body.extend(b'\x1e'); push(MCI_OPEN); farcall(3, 701)

    farcall(1, 91)
    body.extend(b'\x57'); farcall(2, 5)
    body.extend(b'\x8c\xd8\xa3' + struct.pack('<H', MCI_OPEN + 14))  # element selector = DS
    body.extend(b'\xc7\x06' + struct.pack('<H', MCI_OPEN + 8) + struct.pack('<H', 999))
    open_call()
    failures = []
    body.extend(b'\x3d' + struct.pack('<H', 263))  # MCIERR_INVALID_DEVICE_NAME
    failures.append(len(body) + 2); body.extend(b'\x0f\x85\x00\x00')
    body.extend(b'\x81\x3e' + struct.pack('<H', MCI_OPEN + 4) + b'\x5a\xa5')
    failures.append(len(body) + 2); body.extend(b'\x0f\x85\x00\x00')
    body.extend(b'\xc7\x06' + struct.pack('<H', MCI_OPEN + 8) + struct.pack('<H', 522))
    open_call()
    body.extend(b'\x3d' + struct.pack('<H', 275))  # MCIERR_FILE_NOT_FOUND
    failures.append(len(body) + 2); body.extend(b'\x0f\x85\x00\x00')
    body.extend(b'\x81\x3e' + struct.pack('<H', MCI_OPEN + 4) + b'\x5a\xa5')
    failures.append(len(body) + 2); body.extend(b'\x0f\x85\x00\x00')
    body.extend(b'\xc7\x06' + struct.pack('<H', MCI_OPEN + 12) + struct.pack('<H', MCI_PRESENT))
    open_call()
    body.extend(b'\x0b\xc0'); failures.append(len(body) + 2); body.extend(b'\x0f\x85\x00\x00')
    body.extend(b'\xa1' + struct.pack('<H', MCI_OPEN + 4))
    body.extend(b'\x3d\x5a\xa5'); failures.append(len(body) + 2); body.extend(b'\x0f\x84\x00\x00')
    body.extend(b'\x83\x3e' + struct.pack('<H', MCI_OPEN + 6) + b'\x00')
    failures.append(len(body) + 2); body.extend(b'\x0f\x85\x00\x00')
    body.extend(b'\x50'); push(0x804); push(0); push(0); push(0); push(0)
    farcall(3, 701)
    body.extend(b'\x0b\xc0'); failures.append(len(body) + 2); body.extend(b'\x0f\x85\x00\x00')
    body.extend(b'\xc6\x06' + struct.pack('<H', MCI_CAPTION + 4) + b'\x31')
    show = len(body)
    for displacement in failures:
        body[displacement:displacement + 2] = struct.pack('<h', show - displacement - 2)
    push(0); push(0); push(0); push(0)
    body.extend(b'\x1e'); push(MCI_CAPTION); push(0); farcall(2, 1)
    body.extend(b'\xeb\xfe')
    while len(body) % 16:
        body.append(0x90)
    records = [struct.pack('<BBHHH', 3, 1, source, module, ordinal)
               for source, module, ordinal in fixups]
    return bytes(body), struct.pack('<H', len(records)) + b''.join(records)


def build_mci():
    return build(mci_segment, mci_data(), 'MCIFIX', ('KERNEL', 'USER', 'MMSYSTEM'))


if __name__ == '__main__':
    import sys
    if '--tasks' in sys.argv[1:]:
        sys.stdout.buffer.write(build_tasks())
    elif '--sound' in sys.argv[1:]:
        sys.stdout.buffer.write(build_sound())
    elif '--enumfonts' in sys.argv[1:]:
        sys.stdout.buffer.write(build_enumfonts())
    elif '--textalign' in sys.argv[1:]:
        sys.stdout.buffer.write(build_textalign())
    elif '--dos' in sys.argv[1:]:
        sys.stdout.buffer.write(build_dos())
    elif '--region' in sys.argv[1:]:
        sys.stdout.buffer.write(build_region())
    elif '--mci' in sys.argv[1:]:
        sys.stdout.buffer.write(build_mci())
    else:
        sys.stdout.buffer.write(build())
