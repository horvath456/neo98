// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_crt_fpu.h"
#include "../src/x86_fpu_integer.h"
#include <assert.h>
struct Environment {
    unsigned control, status, tags, pointer, context_flags, saved_status = 0xdeadbeef,
             saved_tags = 0x12345678, writes = 0, waits = 0, controls[4] = {}, count = 0;
    uint64_t mantissa; unsigned exponent;
    void Wait() { ++waits; }
    unsigned Control() { return control; }
    void Control(unsigned v) { control = v; controls[count++] = v; }
    unsigned Status() { return status; }
    void Clear() { status = X86FpuInteger::ClearStatus(status); }
    unsigned ExceptionPointers() { return pointer ? 0x100200 : 0; }
    unsigned Read(unsigned p) { assert(p == 0x100204 || p == 0x100300); return p == 0x100204 ? 0x100300 : context_flags; }
    void Write(unsigned p, unsigned v) { assert(p == 0x100320 || p == 0x100324); if(p == 0x100320)saved_status = v;else saved_tags = v; ++writes; }
    void Initialize() { control = 0x37f; status = 0; tags = 0xffff; }
    uint64_t IntegerPop() { unsigned top = (status >> 11) & 7;auto r = X86FpuInteger::Truncate(mantissa, exponent, ((tags >> (top * 2)) & 3) == 3);tags |= 3u << (top * 2);status = X86FpuInteger::PopStatus(status, r.flags);return r.bits; }
};
extern "C" unsigned fpu_decode(unsigned status) { return Win98CrtFpu::DecodeStatus(status); }
extern "C" void fpu_status(unsigned status, unsigned clear, unsigned *out) {
    Environment e{};e.status = status;out[0] = clear ? Win98CrtFpu::Clear(e) : Win98CrtFpu::Status(e);out[1] = e.status;out[2] = e.waits;
}
extern "C" void fpu_reset(unsigned pointer, unsigned flags, unsigned *out) {
    Environment e{};e.pointer = pointer;e.context_flags = flags;Win98CrtFpu::Reset(e);
    out[0] = e.control;out[1] = e.status;out[2] = e.tags;out[3] = e.saved_status;out[4] = e.saved_tags;out[5] = e.writes;
}
extern "C" void fpu_long(uint64_t mantissa, unsigned exponent, unsigned control, unsigned status, unsigned tags, unsigned *out) {
    Environment e{};e.mantissa = mantissa;e.exponent = exponent;e.control = control;e.status = status;e.tags = tags;auto r = Win98CrtFpu::ToLong(e);
    out[0] = unsigned(r);out[1] = unsigned(r >> 32);out[2] = e.control;out[3] = e.status;out[4] = e.tags;out[5] = e.waits;out[6] = e.controls[0];out[7] = e.controls[1];
}
#ifdef CRT_FPU_CHECK
int main() {
    using namespace X86FpuInteger;
    assert(Truncate(0xa000000000000000ull,0x3fff,false).flags == 32);
    assert(Truncate(0xa000000000000000ull,0xbfff,false).bits == ~uint64_t(0));
    assert(Truncate(0x8000000000000000ull,0x7fff,false).flags == 1);
    assert(Truncate(0,0,true).flags == 0x41);
    assert(Truncate(0x8000000000000000ull,0xc03e,false).flags == 0);
    assert(Pending(0x37e,1));assert(!Pending(0x37f,1));
    assert(ClearStatus(0xffff) == 0x7f00);
    unsigned out[8];for(unsigned i=0;i<8;++i)fpu_long(0xa000000000000000ull,0x3fff,0x77f,i<<11,0, out);
    return 0;
}
#endif
