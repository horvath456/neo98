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
    record = cell(0x34)
    names_handle = cell()
    passed = text('PASS print validation')
    setup = mode.startswith('setup-') or mode.startswith('driver-')
    no_printer = mode.endswith('no-printer')
    code.extend(b'\x8c\xd8\xa3\x00\x00')
    put(record, 0x34)
    put(record + 0xc, (0x2040 if setup else 0x1000) | (2 if mode == 'print-range' else 0))
    for offset, value in ((0x10, 1), (0x12, 9), (0x14, 1), (0x16, 9), (0x18, 1)):
        put(record + offset, value)
    hook_field = record + (0x24 if setup else 0x20)
    code.extend(b'\xc7\x06' + struct.pack('<H', hook_field) + b'\0\0')
    hook_patch = len(code) - 2
    code.extend(b'\x8c\xc8\xa3' + struct.pack('<H', hook_field + 2))
    pointer(record)
    call(3, 20)
    equal(0, 'PrintDlg returns after Cancel or missing default printer')
    load(visits)
    equal(0 if no_printer else 1, 'Init preparation runs once')
    if no_printer:
        call(3, 26)
        equal(0x1008, 'missing default printer retains PDERR_NODEFAULTPRN')
    message(passed)
    hook = len(code)
    code[hook_patch:hook_patch + 2] = struct.pack('<H', hook)
    code.extend(b'\x55\x89\xe5\x1e\x16\x1f\x81\x7e\x0c\x10\x01\x0f\x85\0\0')
    not_init = len(code) - 2
    code.extend(b'\xff\x06' + struct.pack('<H', visits))
    if mode in ('print-copies', 'print-range'):
        code.extend(b'\xff\x76\x0e')
        word(0x482 if mode == 'print-copies' else 0x480)
        pointer(text('0'))
        call(2, 92)
    if mode.startswith('driver-'):
        # Work on the actual DEVNAMES of the listed printer, which becomes
        # the caller's handle when SetupClose keeps the specific printer.
        code.extend(b'\xff\x76\x0e')
        for value in (0x470, 0x150, 0, 0, 0):
            word(value)
        call(2, 101)
        code.extend(b'\xa3' + struct.pack('<H', names_handle) + b'\x50')
        call(1, 18)
        code.extend(b'\x06\x53\x57\x8e\xc2\x89\xc3\x26\x8b\x3f\x01\xdf')
        # Driver names have at least Generic's seven-byte slot.
        for offset, value in enumerate(b'MISSING\0'):
            code.extend(b'\x26\xc6\x45' + bytes((offset, value)))
        code.extend(b'\x5f\x5b\x07\xff\x36' + struct.pack('<H', names_handle))
        call(1, 19)
        if mode == 'driver-close':
            code.extend(b'\xff\x76\x0e')
            for value in (0x422, 0x423, 0x423):
                word(value)
            call(2, 96)
        if mode == 'driver-command':
            # Force PrinterChanged to discover the missing driver before OK.
            code.extend(b'\xff\x76\x0e')
            for value in (0x111, 0x423, 0, 0):
                word(value)
            call(2, 110)
    # The waiting controller operation must capture its own native rest.
    put(hook_field, 0)
    put(hook_field + 2, 0)
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
        bytes(data), name='PRINTERR', modules=('KERNEL', 'USER', 'COMMDLG')))
    image[0x52:0x54] = struct.pack('<H', 0x3000)
    return bytes(image)
