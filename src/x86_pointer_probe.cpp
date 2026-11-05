// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include <string.h>
namespace X86
{
unsigned Application::PointerProbeApi(ApiName name, const unsigned *a)
{
    // The original catches access violations inside each probe. This provider
    // reports a fault without invoking Application::Check/Read/Store, which
    // would terminate the guest. Space supplies native read/write permissions.
    struct Environment
    {
        VirtualMemory::Space &memory;
        Cpu *cpu;
        bool ReadByte(unsigned p, unsigned &value)
        {
            unsigned char byte;
            if (!memory.Access(p, 1, false) || cpu->Read(p, &byte, 1) != CpuError::Ok)
                return false;
            value = byte;
            return true;
        }
        bool AddByte(unsigned p, unsigned increment)
        {
            unsigned char byte;
            if (!memory.Access(p, 1, false) || !memory.Access(p, 1, true) ||
                cpu->Read(p, &byte, 1) != CpuError::Ok)
                return false;
            byte = static_cast<unsigned char>(byte + increment);
            return cpu->Write(p, &byte, 1) == CpuError::Ok;
        }
    } e{virtual_memory, cpu};
    switch (name.id)
    {
    API_CASE("IsBadCodePtr")
        return Win98PointerProbe::Code(e, pointer_probe_state, a[0]);
    API_CASE("IsBadReadPtr") API_CASE("IsBadHugeReadPtr")
        return Win98PointerProbe::Read(e, pointer_probe_state, a[0], a[1]);
    API_CASE("IsBadWritePtr") API_CASE("IsBadHugeWritePtr")
        return Win98PointerProbe::Write(e, pointer_probe_state, a[0], a[1]);
    API_CASE("IsBadStringPtrA")
        return Win98PointerProbe::String(e, a[0], a[1]);
    API_CASE("IsBadStringPtrW")
        return Win98PointerProbe::WideString(a[0], a[1]);
    default:
        break;
    }
    Fail("Unknown pointer-probe API");
    return 0;
}
} // namespace X86
