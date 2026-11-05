# SPDX-License-Identifier: GPL-2.0-or-later
"""An actual NE LibMain waits between two checked guest effects."""
import struct
from win16_fixture import build


def library(phase="libmain", failure=False):
    data, code, fixes = bytearray(64), bytearray(), []
    def text(value):
        at = len(data)
        data.extend(value.encode('cp1252') + b'\0')
        return at
    def word(value):
        code.extend(b'\x68' + struct.pack('<H', value))
    def pointer(at):
        code.extend(b'\x1e')
        word(at)
    def call(ordinal):
        code.extend(b'\x9a\xff\xff\0\0')
        fixes.append(struct.pack('<BBHHH', 3, 1, len(code) - 4, 2, ordinal))
    def message(at):
        word(0)
        pointer(at)
        pointer(at)
        word(0)
        call(1)
    visits = 32
    held, failed = text('Hosted LibMain waiting' if phase == 'libmain' else 'Hosted DLLENTRYPOINT waiting'), text('FAIL repeated Hosted initializer')
    exported = 0
    if phase == 'entrypoint':
        code.extend(b'\xb8\x01\0\xcb')
        exported = len(code)
        # DLL_PROCESS_ATTACH is the low reason word at SP+16; detach returns immediately.
        code.extend(b'\x55\x89\xe5\x83\x7e\x12\x01\x5d\x74\x06\xb8\x01\0\xca\x10\0')
    code.extend(b'\xff\x06' + struct.pack('<H', visits))
    message(held)
    code.extend(b'\xff\x06' + struct.pack('<H', visits))
    ret = b'\xcb' if phase == 'libmain' else b'\xca\x10\0'
    code.extend(b'\x83\x3e' + struct.pack('<H', visits) + b'\x02\x75' + bytes([3 + len(ret)]) + b'\xb8' + struct.pack('<H', 0 if failure else 1) + ret)
    message(failed)
    code.extend(b'\xeb\xfe')
    image = build(lambda: (bytes(code), struct.pack('<H', len(fixes)) + b''.join(fixes)),
        bytes(data), name='HELD16', modules=('KERNEL', 'USER'), library=True, entry=0, exports=(('DLLENTRYPOINT', exported),) if phase == 'entrypoint' else ())
    if phase == 'entrypoint':
        image = bytearray(image)
        image[64 + 0x3e:64 + 0x40] = struct.pack('<H', 0x400)
    return bytes(image)
