# SPDX-License-Identifier: GPL-2.0-or-later
"""NE guest with one deliberately waiting native controller hook."""
import struct
from win16_fixture import build


def fixture(controller, veto=False):
    color_ok = controller.startswith('color-ok')
    data, code, fixes, failures = bytearray(256), bytearray(), [], []
    def cell(n=2):
        at=len(data)
        data.extend(bytes(n))
        return at
    def text(s):
        at=len(data)
        data.extend(s.encode('cp1252')+b'\0')
        return at
    def word(n):
        code.extend(b'\x68'+struct.pack('<H', n&65535))
    def ptr(n):
        code.extend(b'\x1e')
        word(n)
    def long(n):
        word(n>>16)
        word(n)
    def call(module, ordinal):
        code.extend(b'\x9a\xff\xff\0\0')
        fixes.append(struct.pack('<BBHHH', 3, 1, len(code)-4, module, ordinal))
    def put(at, n):
        code.extend(b'\xc7\x06'+struct.pack('<HH', at, n&65535))
    def put_ds(at):
        code.extend(b'\x8c\xd8\xa3'+struct.pack('<H', at))
    def load(at):
        code.extend(b'\xa1'+struct.pack('<H', at))
    def equal(n, label):
        code.extend(b'\x3d'+struct.pack('<H', n&65535)+b'\x0f\x85\0\0')
        failures.append((len(code)-2, text('FAIL '+label)))
    def message(at):
        word(0)
        ptr(at)
        ptr(at)
        word(0)
        call(2, 1)
        code.extend(b'\xeb\xfe')
    def jne():
        code.extend(b'\x0f\x85\0\0')
        return len(code)-2
    def branch(at):
        code[at:at+2]=struct.pack('<h', len(code)-at-2)
    visits, effects, target=cell(), cell(), cell()
    passed=text('PASS controller phase')
    held=text('Controller '+controller)
    hook_places=[]
    def hook_field(at):
        code.extend(b'\xc7\x06'+struct.pack('<H', at)+b'\0\0')
        hook_places.append(len(code)-2)
        code.extend(b'\x8c\xc8\xa3'+struct.pack('<H', at+2))
    code.extend(b'\x8c\xd8\xa3\x00\x00')
    if controller.startswith('color'):
        record=cell(0x20)
        custom=cell(64)
        data[custom:custom+64]=struct.pack('<16I', *([0xffffff]*16))
        put(record, 0x20)
        put(record+8, 0xff00)
        put(record+0xc, custom)
        put_ds(record+0xe)
        put(record+0x10, 0x11)
        hook_field(record+0x18)
        if color_ok:
            ptr(text('commdlg_ColorOK'))
            call(2, 118)
            code.extend(b'\xa3'+struct.pack('<H', target))
        else:
            put(target, 0x111)
        ptr(record)
        call(3, 5)
        equal(1 if color_ok else 0, 'ChooseColor result')
        load(record+8)
        equal(0x2211 if color_ok and not veto else 0xff00, 'RGB retained')
        load(record+0xa)
        equal(0x33 if color_ok and not veto else 0, 'RGB high word')
        if color_ok:
            load(custom)
            equal(0x5544 if not veto else 0xffff, 'custom colour retained')
            load(custom+2)
            equal(0x66 if not veto else 0xff, 'custom colour high word')
    elif controller=='font':
        record=cell(0x2e)
        lf=cell(50)
        data[lf:lf+50]=struct.pack('<hhhhh', -16, 0, 0, 0, 700)+bytes(8)+b'Arial'.ljust(32, b'\0')
        put(record, 0x2e)
        put(record+8, lf)
        put_ds(record+0xa)
        put(record+0xe, 0x149)
        hook_field(record+0x1a)
        put(target, 0x111)
        ptr(record)
        call(3, 15)
        equal(0, 'ChooseFont result')
    else:
        record=cell(0x34)
        put(record, 0x34)
        setup=controller=='setup'
        put(record+0xc, 0x2040 if setup else 0x1000)
        put(record+0x10, 1)
        put(record+0x12, 9)
        put(record+0x14, 1)
        put(record+0x16, 9)
        put(record+0x18, 1)
        hook_field(record+(0x24 if setup else 0x20))
        put(target, 0x111)
        ptr(record)
        call(3, 20)
        equal(0, 'PrintDlg result')
    load(visits)
    equal(2 if veto else 1, 'one hook invocation per requested command')
    load(effects)
    equal(1, 'one guest effect across a fresh load')
    message(passed)
    hook=len(code)
    for at in hook_places:
        code[at:at+2]=struct.pack('<H', hook)
    code.extend(b'\x55\x89\xe5\x1e\x16\x1f')
    code.extend(b'\x81\x7e\x0c\x10\x01')
    not_init=jne()
    if controller=='color-ok-direct':
        # The API clears lpfnHook when CC_ENABLEHOOK is initially absent.
        # Disable ordinary hooks from the final Init callback instead.
        put(record+0x10, 0x1)
    code.extend(b'\xb8\x01\x00\xe9\0\0')
    init_out=len(code)-2
    branch(not_init)
    if color_ok and veto:
        # Before the second intentional OK, the first ColorOK's guest writes
        # must still be present. A replay of Close would have overwritten them.
        code.extend(b'\x81\x7e\x0c\x11\x01')
        not_command=jne()
        code.extend(b'\x83\x7e\x0a\x01')
        not_ok=jne()
        code.extend(b'\x83\x3e'+struct.pack('<H', visits)+b'\x01')
        not_after_veto=jne()
        load(record+8)
        equal(0x2211, 'ColorOK RGB write survives veto continuation')
        load(record+0xa)
        equal(0x33, 'ColorOK RGB high word survives veto continuation')
        load(custom)
        equal(0x5544, 'ColorOK custom colour survives veto continuation')
        load(custom+2)
        equal(0x66, 'ColorOK custom high word survives veto continuation')
        branch(not_command)
        branch(not_ok)
        branch(not_after_veto)
    code.extend(b'\x8b\x46\x0c\x3b\x06'+struct.pack('<H', target))
    not_target=jne()
    if not color_ok:
        code.extend(b'\x83\x7e\x0a\x02')
        not_cancel=jne()
    code.extend(b'\xff\x06'+struct.pack('<H', visits))
    code.extend(b'\x83\x3e'+struct.pack('<H', visits)+b'\x01')
    not_first=jne()
    code.extend(b'\xff\x06'+struct.pack('<H', effects))
    if color_ok:
        put(record+8, 0x2211)
        put(record+0xa, 0x33)
        put(custom, 0x5544)
        put(custom+2, 0x66)
    code.extend(b'\xff\x76\x0e')
    ptr(held)
    ptr(held)
    word(0)
    call(2, 1)
    code.extend(b'\xb8'+struct.pack('<H', int(veto))+b'\xe9\0\0')
    first_out=len(code)-2
    branch(not_first)
    branch(not_target)
    if not color_ok:
        branch(not_cancel)
    code.extend(b'\x31\xc0')
    branch(init_out)
    branch(first_out)
    code.extend(b'\x1f\x5d\xca\x0a\x00')
    for at, label in failures:
        branch(at)
        code.extend(b'\x36\xa1\x00\x00\x8e\xd8')
        message(label)
    image=bytearray(build(lambda:(bytes(code), struct.pack('<H', len(fixes))+b''.join(fixes)), bytes(data),
                          name='CTRLPH', modules=('KERNEL', 'USER', 'COMMDLG')))
    image[0x52:0x54]=struct.pack('<H', 0x3000)
    return bytes(image)
