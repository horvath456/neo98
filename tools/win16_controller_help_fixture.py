# SPDX-License-Identifier: GPL-2.0-or-later
"""NE owner window receiving a waiting commdlg_help callback exactly once."""
import struct
from win16_fixture import build


def fixture(controller):
    data, code, fixes, failures = bytearray(256), bytearray(), [], []
    def cell(size=2):
        at = len(data)
        data.extend(bytes(size))
        return at
    def text(value):
        at = len(data)
        data.extend(value.encode('cp1252') + b'\0')
        return at
    def word(value):
        code.extend(b'\x68' + struct.pack('<H', value & 65535))
    def pointer(at):
        code.extend(b'\x1e')
        word(at)
    def long(value):
        word(value >> 16)
        word(value)
    def call(module, ordinal):
        code.extend(b'\x9a\xff\xff\0\0')
        fixes.append(struct.pack('<BBHHH', 3, 1, len(code) - 4, module, ordinal))
    def put(at, value):
        code.extend(b'\xc7\x06' + struct.pack('<HH', at, value & 65535))
    def segment(at, register='ds'):
        code.extend((b'\x8c\xd8' if register == 'ds' else b'\x8c\xc8') + b'\xa3' + struct.pack('<H', at))
    def procedure(at):
        put(at, 0)
        patch = len(code) - 2
        segment(at + 2, 'cs')
        return patch
    def equal(value, label):
        code.extend(b'\x3d' + struct.pack('<H', value) + b'\x0f\x85\0\0')
        failures.append((len(code) - 2, text('FAIL ' + label)))
    def message(at, loop=False):
        word(0)
        pointer(at)
        pointer(at)
        word(0)
        call(2, 1)
        if loop:
            code.extend(b'\xeb\xfe')
    def branch(at):
        code[at:at + 2] = struct.pack('<h', len(code) - at - 2)
    visits, effects, target = cell(), cell(), cell()
    cls, owner = cell(26), cell()
    klass = text('HelpOwner')
    held = text('Controller help ' + controller)
    passed = text('PASS controller help')
    code.extend(b'\x8c\xd8\xa3\x00\x00')
    wnd_patch = procedure(cls + 2)
    segment(cls + 10)
    put(cls + 22, klass)
    segment(cls + 24)
    pointer(cls)
    call(2, 57)
    pointer(klass)
    pointer(klass)
    long(0)
    for value in (0, 0, 100, 100, 0, 0):
        word(value)
    code.extend(b'\x1e')
    long(0)
    call(2, 41)
    code.extend(b'\xa3' + struct.pack('<H', owner))
    pointer(text('commdlg_help'))
    call(2, 118)
    code.extend(b'\xa3' + struct.pack('<H', target))
    if controller == 'color':
        record, custom = cell(0x20), cell(64)
        put(record, 0x20)
        put(record + 8, 0xff00)
        put(record + 0xc, custom)
        segment(record + 0xe)
        put(record + 0x10, 0x19)
        hook_patch = procedure(record + 0x18)
        ordinal = 5
    elif controller == 'font':
        record, log = cell(0x2e), cell(50)
        data[log:log + 50] = struct.pack('<hhhhh', -16, 0, 0, 0, 700) + bytes(8) + b'Arial'.ljust(32, b'\0')
        put(record, 0x2e)
        put(record + 8, log)
        segment(record + 0xa)
        put(record + 0xe, 0x14d)
        hook_patch = procedure(record + 0x1a)
        ordinal = 15
    else:
        record = cell(0x34)
        put(record, 0x34)
        put(record + 0xc, 0x2840 if controller == 'setup' else 0x1800)
        for offset, value in ((0x10, 1), (0x12, 9), (0x14, 1), (0x16, 9), (0x18, 1)):
            put(record + offset, value)
        hook_offset = 0x24 if controller == 'setup' else 0x20
        hook_patch = procedure(record + hook_offset)
        ordinal = 20
    code.extend(b'\xa1' + struct.pack('<H', owner) + b'\xa3' + struct.pack('<H', record + 4))
    pointer(record)
    call(3, ordinal)
    equal(0, 'dialog returns after Cancel')
    code.extend(b'\xa1' + struct.pack('<H', visits))
    equal(1, 'Init preparation runs once')
    code.extend(b'\xa1' + struct.pack('<H', effects))
    equal(1, 'owner Help effect occurs once')
    code.extend(b'\xff\x36' + struct.pack('<H', owner))
    call(2, 53)
    message(passed, True)
    hook = len(code)
    code[hook_patch:hook_patch + 2] = struct.pack('<H', hook)
    code.extend(b'\x55\x89\xe5\x1e\x16\x1f\x81\x7e\x0c\x10\x01\x0f\x85\0\0')
    not_init = len(code) - 2
    code.extend(b'\xff\x06' + struct.pack('<H', visits))
    # Remove ordinary message hooks after initialization. Help's own native
    # continuation must provide capture ownership, even without those hooks.
    if controller == 'color':
        put(record + 0x10, 0x9)
    elif controller == 'font':
        put(record + 0xe, 0x145)
    else:
        put(record + hook_offset, 0)
        put(record + hook_offset + 2, 0)
    code.extend(b'\xff\x76\x0e')
    word(0x111)
    word(0x40e)
    long(0)
    call(2, 110)
    code.extend(b'\xb8\x01\x00\xe9\0\0')
    init_out = len(code) - 2
    branch(not_init)
    code.extend(b'\x31\xc0')
    branch(init_out)
    code.extend(b'\x1f\x5d\xca\x0a\x00')
    wnd = len(code)
    code[wnd_patch:wnd_patch + 2] = struct.pack('<H', wnd)
    code.extend(b'\x55\x89\xe5\x1e\x16\x1f\x8b\x46\x0c\x3b\x06' + struct.pack('<H', target) + b'\x0f\x85\0\0')
    not_help = len(code) - 2
    code.extend(b'\xff\x06' + struct.pack('<H', effects))
    message(held)
    code.extend(b'\x31\xc0\x31\xd2\xe9\0\0')
    help_out = len(code) - 2
    branch(not_help)
    # This hidden owner needs only to accept creation and receive Help.
    code.extend(b'\x31\xc0\x31\xd2\x81\x7e\x0c\x81\x00\x75\x03\xb8\x01\x00')
    branch(help_out)
    code.extend(b'\x1f\x5d\xca\x0a\x00')
    for at, label in failures:
        branch(at)
        code.extend(b'\x36\xa1\x00\x00\x8e\xd8')
        message(label, True)
    image = bytearray(build(lambda: (bytes(code), struct.pack('<H', len(fixes)) + b''.join(fixes)),
        bytes(data), name='CTRLHELP', modules=('KERNEL', 'USER', 'COMMDLG')))
    image[0x52:0x54] = struct.pack('<H', 0x3000)
    return bytes(image)
