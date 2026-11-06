#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the actual ARM64 leaf with an interrupt at every instruction.

Unicorn only: no Hypervisor.framework process is created. The completion
routine is the same C++ header used by Cpu::Run. Run under serial.py.
"""
if __name__ == '__main__':
    import sys as _sys
    if _sys.platform != 'darwin':
        print('SKIP: ARM64 code; runs on macOS ARM64 only')  # macOS only; skipped elsewhere
        _sys.exit(77)
import ctypes
from pathlib import Path
import re
import struct
import subprocess
import tempfile
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X18, UC_ARM64_REG_X30, UC_ARM64_REG_PC

ROOT = Path(__file__).resolve().parents[1]
LLVM = Path('/opt/homebrew/opt/llvm/bin')
CODE, CLIENT, TEB, RECORD, RETURN = 0x10000, 0x20000, 0x30000, 0xc8010000, 0x40000


def main():
    with tempfile.TemporaryDirectory(prefix='neo98-critical-') as temporary:
        work = Path(temporary)
        obj, raw = work / 'leaf.o', work / 'leaf.bin'
        subprocess.run([str(LLVM / 'clang'), '-target', 'aarch64-none-elf', '-c', str(ROOT / 'tools/arm64_critical_leaf.S'), '-o', str(obj)], check=True)
        subprocess.run([str(LLVM / 'llvm-objcopy'), '-O', 'binary', '--only-section=.text', str(obj), str(raw)], check=True)
        words = re.findall(r'0x([0-9a-f]+)u', (ROOT / 'src/arm64_critical_code.inc').read_text())
        code = struct.pack('<' + 'I' * len(words), *(int(w, 16) for w in words))
        assert code == raw.read_bytes(), 'regenerate arm64_critical_code.inc'
        shim = work / 'shim.cpp'
        shim.write_text('''#include "arm64_critical_leaf.h"
#include <cstring>
struct Memory { unsigned *data; unsigned Write(unsigned long long at,const void *v,unsigned n) {
 if(at<0xc8010000u||at+n>0xc8010020u)return X86::CpuError::WriteUnmapped;
 memcpy((char *)data+at-0xc8010000u,v,n);return X86::CpuError::Ok; }};
extern "C" bool complete(unsigned long long *x,unsigned long long *pc,unsigned base,unsigned *data) {
 Arm64::Registers r; memcpy(r.x,x,sizeof r.x);r.pc=*pc;Memory m{data};
 bool ok=Arm64Critical::Interrupted(r,base,m);*pc=r.pc;return ok; }
''')
        lib = work / 'shim.dylib'
        subprocess.run(['clang++', '-std=c++17', '-dynamiclib', '-I' + str(ROOT / 'src'), str(shim), '-o', str(lib)], check=True)
        finish = ctypes.CDLL(str(lib)).complete
        finish.argtypes = [ctypes.POINTER(ctypes.c_ulonglong), ctypes.POINTER(ctypes.c_ulonglong), ctypes.c_uint, ctypes.POINTER(ctypes.c_uint)]
        finish.restype = ctypes.c_bool
        cases = [
            (0, (0, 0, 1), (1, 7, 0), False),
            (0, (1, 7, 0), (2, 7, 0xffffffff), False),
            (0, (3, 7, 0xfffffffc), (4, 7, 0xfffffffb), False),
            (0, (1, 8, 0), (1, 8, 0), True),
            (1, (1, 7, 0), (0, 0, 1), False),
            (1, (2, 7, 0xfffffffe), (1, 7, 0xffffffff), False),
            (1, (1, 7, 0xffffffff), (1, 7, 0xffffffff), True),
            (1, (1, 7, 0xfffffffe), (1, 7, 0xfffffffe), True),
        ]
        checks = 0
        for which, before, after, slow in cases:
            # Includes every executed instruction and interruptions after RET.
            for interrupt in range(38):
                uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
                for addr in (CODE, CLIENT, TEB, RECORD, RETURN): uc.mem_map(addr, 4096)
                uc.mem_write(CODE, code)
                uc.mem_write(CLIENT, struct.pack('<II', 4, RECORD))
                uc.mem_write(TEB + 0x24, struct.pack('<I', 7))
                rec = [4, before[0], before[1], 0, before[2], 0, 0, CLIENT]
                uc.mem_write(RECORD, struct.pack('<8I', *rec))
                uc.reg_write(UC_ARM64_REG_X0, CLIENT)
                uc.reg_write(UC_ARM64_REG_X18, TEB + 0x800)
                uc.reg_write(UC_ARM64_REG_X30, RETURN)
                start = CODE + which * 0x90
                uc.reg_write(UC_ARM64_REG_PC, start)
                for step in range(100):
                    pc = uc.reg_read(UC_ARM64_REG_PC)
                    if step == interrupt:
                        # X29/X30 are not contiguous with X0 in Unicorn's enum.
                        from unicorn import arm64_const as registers
                        x = (ctypes.c_ulonglong * 31)(*(uc.reg_read(getattr(registers, f'UC_ARM64_REG_X{i}')) for i in range(31)))
                        at = ctypes.c_ulonglong(pc)
                        data = (ctypes.c_uint * 8).from_buffer_copy(uc.mem_read(RECORD, 32))
                        assert finish(x, ctypes.byref(at), start, data)
                        uc.mem_write(RECORD, bytes(data)); uc.reg_write(UC_ARM64_REG_PC, at.value)
                        pc = at.value
                        observed = struct.unpack('<8I', uc.mem_read(RECORD, 32))
                        assert (observed[1], observed[2], observed[4]) in (before, after), (which, interrupt, observed)
                        # Before commit, let another owner acquire/release or a
                        # same-thread callback recurse; the original attempt
                        # must reload the record when it resumes.
                        if pc == start:
                            callback = list(struct.unpack('<8I', uc.mem_read(RECORD, 32)))
                            if before[1] == 7:
                                callback[1] += 1; callback[4] = (callback[4] - 1) & 0xffffffff
                                uc.mem_write(RECORD, struct.pack('<8I', *callback))
                                callback[1] -= 1; callback[4] = (callback[4] + 1) & 0xffffffff
                                uc.mem_write(RECORD, struct.pack('<8I', *callback))
                    if pc in (RETURN, start + 0x80): break
                    uc.emu_start(pc, RETURN, count=1)
                else: raise AssertionError(('leaf did not end', which, interrupt, hex(pc), hex(uc.reg_read(UC_ARM64_REG_X30))))
                data = struct.unpack('<8I', uc.mem_read(RECORD, 32))
                assert (data[1], data[2], data[4]) == after, (which, interrupt, data, after)
                assert pc == (start + 0x80 if slow else RETURN), (which, interrupt, hex(pc))
                assert uc.reg_read(UC_ARM64_REG_X0) == CLIENT
                checks += 1
        print(f'ARM64 CRITICAL INSTRUCTIONS PASS checks={checks}')


if __name__ == '__main__': main()
