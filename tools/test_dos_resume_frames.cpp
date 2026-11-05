// SPDX-License-Identifier: GPL-2.0-or-later
// The actual native-frame reader must validate all payloads before applying
// even the first frame. No running DOS machine or drive clone is needed.
#include "dosbox.h"
#include "cpu.h"
#include "mem.h"
#include "callback.h"
#include "dosbox_pure_resume.h"
static Bitu call_stop = 1, call_idle = 2;
#include "dosbox_pure_resume.inc"
#include <cstdio>

CPUBlock cpu = {};
CPU_Regs cpu_regs = {};
Segments Segs = {};
static Bit8u memory[2 << 20] = {};
HostPt MemBase = memory;
Bitu MEM_TotalPages() { return sizeof memory / 4096; }
bool PAGING_NEO98_InFault() { return false; }
void GFX_ShowMsg(const char*, ...) {}
static unsigned passed = 0;
static void Check(bool okay, const char *name)
{
    if (!okay) { fprintf(stderr, "FAIL %s\n", name); exit(1); }
    ++passed;
}
int main()
{
    SegSet16(cs, CB_SEG);
    SegSet16(ss, 0x100);
    reg_eip = CB_SOFFSET + 8 * CB_SIZE;
    reg_sp = 0x100;
    phys_writew(PhysMake(0x100, 0x100), 0x200);
    phys_writew(PhysMake(0x100, 0x102), 0x100);
    Check(DBP_Neo98ResumeLegacySafe(), "ordinary guest BIOS input remains compatible");
    phys_writew(PhysMake(0x100, 0x100), CB_SOFFSET + CB_MAX * CB_SIZE + 0x16 * 6 + 2);
    phys_writew(PhysMake(0x100, 0x102), CB_SEG);
    Check(!DBP_Neo98ResumeLegacySafe(), "a saved private BIOS return proves missing native locals");
    phys_writew(PhysMake(0x100, 0x100), CB_SOFFSET + CB_MAX * CB_SIZE + 0x21 * 6 + 2);
    Check(DBP_Neo98ResumeLegacySafe(), "the original outer shell EXEC remains compatible");
    reg_eip = CB_SOFFSET + call_idle * CB_SIZE;
    Check(!DBP_Neo98ResumeLegacySafe(), "a saved native idle callback needs its old locals");
    reg_eip = CB_SOFFSET + call_stop * CB_SIZE;
    Check(!DBP_Neo98ResumeLegacySafe(), "a private far-return callback needs its native caller");
    reg_eip = CB_SOFFSET + 8 * CB_SIZE;
    reg_sp = 0xffff;
    const Bit16u wrapped_ip = CB_SOFFSET + CB_MAX * CB_SIZE + 0x16 * 6 + 2;
    phys_writeb(PhysMake(0x100, 0xffff), Bit8u(wrapped_ip));
    phys_writeb(PhysMake(0x100, 0xffff) + 1, Bit8u(wrapped_ip >> 8));
    phys_writeb(PhysMake(0x100, 1), Bit8u(CB_SEG));
    phys_writeb(PhysMake(0x100, 2), Bit8u(CB_SEG >> 8));
    Check(!DBP_Neo98ResumeLegacySafe(), "the second 16-bit stack word wraps at the segment boundary");
    cpu.pmode = true;
    Check(DBP_Neo98ResumeLegacySafe(), "protected guest code is not mistaken for a real-mode trampoline");
    cpu.pmode = false;
    int nested = 0;
    const auto initial_frames = neo98_resume_frames.size();
    try
    {
        Neo98NestedRun outer(nested);
        Neo98ResumeFrame call(0x121, [](DBPArchive&, bool) {});
        Neo98NestedRun inner(nested);
        Neo98ResumeFrame fault(0x600, [](DBPArchive&, bool) {});
        Check(nested == 2 && neo98_resume_frames.size() == initial_frames + 2,
              "both native calls are registered before forced shutdown");
        throw Neo98DosShutdown();
    }
    catch (const Neo98DosShutdown&) { }
    Check(!nested && neo98_resume_frames.size() == initial_frames,
          "forced shutdown unwinds counters and native continuation frames");
    Bit32u a = 10, b = 20;
    Neo98ResumeFrame first(0x121, [&](DBPArchive& ar, bool apply) {
        auto value = a; ar << value; if (apply && !ar.had_error) a = value;
    });
    Neo98ResumeFrame later(0x500, [&](DBPArchive& ar, bool apply) {
        auto value = b; ar << value; if (apply && !ar.had_error) b = value;
    });
    const auto good = Neo98ResumeBytes();
    a = 1; b = 2;
    Check(DBP_Neo98ResumeLoad(good.data(), good.size(), false) == 1 && a == 1 && b == 2,
          "dry validation leaves all live locals unchanged");
    auto bad = good;
    // Header (8), first frame (8+4), second frame header (8).
    Bit32u length = 3;
    memcpy(bad.data() + 24, &length, 4);
    bad.pop_back();
    Check(DBP_Neo98ResumeLoad(bad.data(), bad.size(), false) == -1 && a == 1 && b == 2,
          "a truncated later frame is refused before the first local changes");
    Check(DBP_Neo98ResumeLoad(bad.data(), bad.size(), true) == -1 && a == 1 && b == 2,
          "apply also validates every frame before modifying any local");
    Check(DBP_Neo98ResumeLoad(good.data(), good.size(), true) == 1 && a == 10 && b == 20,
          "a valid continuation applies all saved locals");
    Bit16u used = 2;
    Bit8u data[8] = {'a', 'b'};
    Neo98ResumeFrame partial(0x600, [&](DBPArchive& ar, bool apply) {
        auto count = used; ar << count;
        if (count > sizeof data) { ar.had_error = DBPArchive::ERR_LAYOUT; return; }
        Bit8u buffer[8] = {};
        if (ar.mode != DBPArchive::MODE_LOAD) memcpy(buffer, data, count);
        if (count) ar.SerializeBytes(buffer, count);
        if (apply && !ar.had_error) { used = count; memcpy(data, buffer, count); }
    });
    const auto line = Neo98ResumeBytes();
    used = 0; memset(data, 0, sizeof data);
    Check(DBP_Neo98ResumeLoad(line.data(), line.size(), false) == 1 && !used && !data[0],
          "a variable saved prefix validates against a fresh empty input call");
    Check(DBP_Neo98ResumeLoad(line.data(), line.size(), true) == 1 && used == 2 && data[0] == 'a' && data[1] == 'b',
          "the variable prefix is applied after successful validation");
    auto excess = line; Bit32u count = 65; memcpy(excess.data()+4, &count, 4);
    Check(DBP_Neo98ResumeLoad(excess.data(), excess.size(), false) == -1,
          "the 64-frame input bound is enforced");
    printf("DOS resume frames: %u passed\n", passed);
}
