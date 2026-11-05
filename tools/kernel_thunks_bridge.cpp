// SPDX-License-Identifier: GPL-2.0-or-later
// Host bridge for tools/test_kernel_thunks_original.py: runs the implemented
// ring-3 decisions of src/win98_thunk_bridges.h over a host byte buffer that
// stands in for guest memory, so the Python driver can compare them with the
// original KERNEL32 routines running under Unicorn.
#include "../src/win98_thunk_bridges.h"
#include "../src/win98_segment_map.h"
#include <cstdint>
#include <cstring>
namespace
{
struct Memory
{
    uint8_t *bytes;
    uint32_t base, size;
    uint32_t Read32(uint32_t address)
    {
        uint32_t value = 0;
        if (address >= base && address + 4 <= base + size)
            memcpy(&value, bytes + (address - base), 4);
        return value;
    }
    void Write32(uint32_t address, uint32_t value)
    {
        if (address >= base && address + 4 <= base + size)
            memcpy(bytes + (address - base), &value, 4);
    }
};
} // namespace
extern "C"
{
// regs: eax, ecx, edx, ebx, esp, ebp, esi, edi (in and out).
void kernel_thunks_prolog(uint8_t *bytes, uint32_t base, uint32_t size, uint32_t *regs, uint32_t return_address,
                          uint32_t previous, uint32_t handler, uint32_t *record)
{
    Memory memory{bytes, base, size};
    Win98ThunkBridges::Registers in;
    in.eax = regs[0]; in.ecx = regs[1]; in.edx = regs[2]; in.ebx = regs[3];
    in.esp = regs[4]; in.ebp = regs[5]; in.esi = regs[6]; in.edi = regs[7];
    const auto out = Win98ThunkBridges::Prolog(memory, in, return_address, previous, handler);
    regs[0] = out.eax; regs[1] = out.ecx; regs[2] = out.edx; regs[3] = out.ebx;
    regs[4] = out.esp; regs[5] = out.ebp; regs[6] = out.esi; regs[7] = out.edi;
    *record = Win98ThunkBridges::HandlerRecord(out.ebp);
}
// Returns the resumed EIP; regs as above, "bytes_popped" is the FT_Exitn size.
uint32_t kernel_thunks_exit(uint8_t *bytes, uint32_t base, uint32_t size, uint32_t *regs, uint32_t bytes_popped,
                            uint32_t *previous_handler)
{
    Memory memory{bytes, base, size};
    Win98ThunkBridges::Registers in;
    in.ebp = regs[5]; in.ebx = regs[3]; in.eax = regs[0]; in.ecx = regs[1]; in.edx = regs[2]; in.esi = regs[6]; in.edi = regs[7];
    *previous_handler = memory.Read32(in.ebp - Win98ThunkBridges::PreviousHandler);
    const auto out = Win98ThunkBridges::Exit(memory, in);
    const uint32_t ret = memory.Read32(out.esp);
    regs[0] = out.eax; regs[3] = out.ebx; regs[4] = out.esp + 4 + bytes_popped; regs[5] = out.ebp; regs[6] = out.esi; regs[7] = out.edi;
    return ret;
}
uint32_t kernel_thunks_callback(uint8_t *bytes, uint32_t base, uint32_t size, uint32_t sp, uint32_t count, uint32_t ret_stub)
{
    Memory memory{bytes, base, size};
    return Win98ThunkBridges::Callback(memory, sp, count, ret_stub);
}
uint32_t kernel_thunks_fault_result(uint32_t saved_ecx, uint32_t saved_eax) { return Win98ThunkBridges::FaultResult(saved_ecx, saved_eax); }
uint32_t kernel_thunks_fault_dwords(uint32_t saved_ecx) { return Win98ThunkBridges::FaultArgumentDwords(saved_ecx); }
void kernel_thunks_ls_template(uint32_t entry, uint32_t target, uint32_t data, uint8_t *out)
{
    unsigned char code[Win98ThunkBridges::LsLength];
    Win98ThunkBridges::LsTemplate(code, entry, target, data);
    memcpy(out, code, sizeof code);
}
void kernel_thunks_sl_template(uint32_t target, uint32_t data, uint8_t *out)
{
    unsigned char code[Win98ThunkBridges::SlLength];
    Win98ThunkBridges::SlTemplate(code, target, data);
    memcpy(out, code, sizeof code);
}
uint32_t kernel_thunks_entry_of(uint32_t page, uint32_t pointer) { return Win98ThunkBridges::EntryOf(page, pointer); }
uint32_t kernel_thunks_segmented_entry_of(uint32_t selector, uint32_t segmented) { return Win98ThunkBridges::SegmentedEntryOf(selector, segmented); }
uint32_t kernel_thunks_entry_address(uint32_t page, uint32_t index) { return Win98ThunkBridges::EntryAddress(page, index); }
uint32_t kernel_thunks_is_result(uint32_t entry) { return Win98ThunkBridges::IsResult(entry); }
uint32_t kernel_thunks_ls_target(uint32_t entry, uint32_t relative) { return Win98ThunkBridges::LsTemplateTarget(entry, relative); }
uint32_t kernel_thunks_segmented(uint32_t selector, uint32_t offset) { return Win98ThunkBridges::SegmentedAddress(selector, offset); }
uint32_t kernel_thunks_selector_word(uint32_t pointer) { return Win98SegmentMap::SelectorWord(pointer); }
uint32_t kernel_thunks_unknown_module_ls(uint32_t instance) { return Win98ThunkBridges::UnknownModuleLS(instance); }
uint32_t kernel_thunks_instance_segmented(uint32_t instance) { return Win98ThunkBridges::InstanceIsSegmented(instance); }
uint32_t kernel_thunks_map(uint32_t pointer) { Win98SegmentMap::Table table; return table.Map(pointer); }
uint32_t kernel_thunks_linear(uint32_t segmented) { Win98SegmentMap::Table table; return table.Linear(segmented); }
uint32_t kernel_thunks_ut_slot(uint32_t n) { return Win98ThunkBridges::UtMappedSlot(n); }
uint32_t kernel_thunks_sign_extend(uint32_t value) { return Win98ThunkBridges::SignExtendWord(value); }
uint32_t kernel_thunks_quick_bytes(uint32_t ebp, uint32_t esp) { return Win98ThunkBridges::QuickArgumentBytes(ebp, esp); }
}
