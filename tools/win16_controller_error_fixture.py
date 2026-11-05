# SPDX-License-Identifier: GPL-2.0-or-later
"""NE guests that reach the native controllers' real validation messages."""
import struct
from win16_fixture import build


def fixture(mode):
    data, code, fixups, failures = bytearray(256), bytearray(), [], []
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
    def call(module, ordinal):
        code.extend(b'\x9a\xff\xff\0\0')
        fixups.append(struct.pack('<BBHHH', 3, 1, len(code) - 4, module, ordinal))
    def put(at, value):
        code.extend(b'\xc7\x06' + struct.pack('<HH', at, value & 65535))
    def put_ds(at):
        code.extend(b'\x8c\xd8\xa3' + struct.pack('<H', at))
    def load(at):
        code.extend(b'\xa1' + struct.pack('<H', at))
    def equal(value, label):
        code.extend(b'\x3d' + struct.pack('<H', value) + b'\x0f\x85\0\0')
        failures.append((len(code) - 2, text('FAIL ' + label)))
    def message(at):
        word(0)
        pointer(at)
        pointer(at)
        word(0)
        call(2, 1)
        code.extend(b'\xeb\xfe')
    visits = cell()
    record = cell(0x2e)
    log = cell(50)
    data[log:log + 50] = struct.pack('<hhhhh', -16, 0, 0, 0, 700) + bytes(8) + b'Arial'.ljust(32, b'\0')
    passed = text('PASS controller validation')
    code.extend(b'\x8c\xd8\xa3\x00\x00')
    put(record, 0x2e)
    put(record + 8, log)
    put_ds(record + 0xa)
    flags = 0x8001 if mode == 'font-empty' else 0x149 | (0x10000 if mode in ('font-face', 'font-style') else 0)
    put(record + 0xe, flags)
    put(record + 0x10, flags >> 16)
    code.extend(b'\xc7\x06' + struct.pack('<H', record + 0x1a) + b'\0\0')
    hook_at = len(code) - 2
    code.extend(b'\x8c\xc8\xa3' + struct.pack('<H', record + 0x1c))
    pointer(record)
    call(3, 15)
    equal(0, 'ChooseFont returns after Cancel or an empty font list')
    load(visits)
    equal(0 if mode == 'font-empty' else 1, 'Init preparation runs once')
    if mode == 'font-empty':
        call(3, 26)
        equal(0x2001, 'the empty font list returns CFERR_NOFONTS')
    message(passed)
    hook = len(code)
    code[hook_at:hook_at + 2] = struct.pack('<H', hook)
    # Pascal hook arguments: lParam BP+6, wParam BP+10, message BP+12, HWND BP+14.
    code.extend(b'\x55\x89\xe5\x1e\x16\x1f\x81\x7e\x0c\x10\x01\x0f\x85\0\0')
    not_init = len(code) - 2
    code.extend(b'\xff\x06' + struct.pack('<H', visits))
    field = {'font-size': 0x472, 'font-face': 0x470, 'font-style': 0x471}.get(mode)
    if field:
        code.extend(b'\xff\x76\x0e')
        word(field)
        pointer(text({'font-size': 'invalid', 'font-face': 'NoSuchFace', 'font-style': 'NoSuchStyle'}[mode]))
        call(2, 92)
    code.extend(b'\xb8\x01\x00\xe9\0\0')
    init_out = len(code) - 2
    code[not_init:not_init + 2] = struct.pack('<h', len(code) - not_init - 2)
    code.extend(b'\x31\xc0')
    code[init_out:init_out + 2] = struct.pack('<h', len(code) - init_out - 2)
    code.extend(b'\x1f\x5d\xca\x0a\x00')
    for at, label in failures:
        code[at:at + 2] = struct.pack('<h', len(code) - at - 2)
        code.extend(b'\x36\xa1\x00\x00\x8e\xd8')
        message(label)
    image = bytearray(build(lambda: (bytes(code), struct.pack('<H', len(fixups)) + b''.join(fixups)),
        bytes(data), name='CTRLERR', modules=('KERNEL', 'USER', 'COMMDLG')))
    image[0x52:0x54] = struct.pack('<H', 0x3000)
    return bytes(image)
