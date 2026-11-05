// SPDX-License-Identifier: GPL-2.0-or-later
// Host bridge for the original comparison of the KERNEL32 ordinal family:
// exposes the implemented ring-3 decisions of win98_kernel_ordinals_a.h to
// tools/test_kernel_ordinals_a_original.py.
#include "../src/win98_kernel_ordinals_a.h"
namespace K = Win98KernelOrdinals;
extern "C"
{
unsigned ordinals_unimp_result(unsigned ecx) { return K::UnimpStubResult(ecx); }
unsigned ordinals_unimp_pop(unsigned ecx) { return K::UnimpStubPop(ecx); }
unsigned ordinals_unimp_error() { return K::UnimpStubError; }
// direction 1 = ANSI to OEM (ordinals 10/11), 0 = OEM to ANSI (12/13);
// count < 0 selects the string form, otherwise the Buff form with `count` bytes.
void ordinals_convert(const unsigned char *high, int to_oem, const unsigned char *in, unsigned char *out, int count)
{
    auto read = [&](unsigned i) { return in[i]; };
    auto write = [&](unsigned i, unsigned char c) { out[i] = c; };
    if (count < 0) K::ConvertString(high, to_oem != 0, read, write);
    else K::ConvertBuffer(high, to_oem != 0, (unsigned)count, read, write);
}
unsigned ordinals_real_mode_pointer(unsigned vp) { return K::RealModePointer(vp); }
unsigned ordinals_ldt_selector(unsigned vp) { return K::LdtSelector(vp); }
unsigned ordinals_handle_rule(unsigned type) { return K::WowHandleRule(type); }
unsigned ordinals_callback_limit() { return K::CallbackArgumentLimit; }
unsigned ordinals_thunk_lock_stored(unsigned word) { return K::ThunkLockStored(word); }
unsigned ordinals_load_string_arguments(unsigned buffer, unsigned capacity) { return K::LoadStringArguments(buffer, capacity); }
unsigned ordinals_thunk_init_direct(unsigned dll32) { return K::ThunkInitDirectPath(dll32); }
unsigned ordinals_thunk_init_direct_result(unsigned block, unsigned load) { return K::ThunkInitDirectResult(block, load); }
}
