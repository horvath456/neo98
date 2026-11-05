// SPDX-License-Identifier: GPL-2.0-or-later
#include "../third_party/unicorn/include/unicorn/unicorn.h"
#include "pe32.h"
#include "jit_memory.h"
#include "win98_rect_oracle.h"
#include "../build/generated/unicorn_user32.h"
extern "C" void neo98_diagnostic(const char *);
extern "C" int qemu_guest_getrandom(void *, __SIZE_TYPE__);

static void gate(uc_engine *cpu, uint64_t address, uint32_t size, void *data)
{
    (void)address; (void)size;
    ++*static_cast<unsigned *>(data);
    uint32_t eax = 98;
    uc_reg_write(cpu, UC_X86_REG_EAX, &eax);
    uc_emu_stop(cpu);
}

unsigned UnicornSelfTest()
{
    uc_engine *cpu = nullptr;
    unsigned step = 0;
    auto check = [&](uc_err error) {
        ++step;
        if (error) neo98_diagnostic(uc_strerror(error));
        return error == UC_ERR_OK;
    };
#define CHECK(call) do { if (!check(call)) { if(cpu) uc_close(cpu); return step; } } while(0)
#define EXPECT(value) do { ++step; if (!(value)) { if(cpu) uc_close(cpu); return step; } } while(0)
    CHECK(uc_open(UC_ARCH_X86, UC_MODE_32, &cpu));
    CHECK(uc_ctl_set_tcg_buffer_size(cpu, NEO98_JIT_SLOT_BYTES));
    CHECK(uc_mem_map(cpu, 0x10000, 0x1000, UC_PROT_ALL));
    const unsigned lengths[] = {1, 2, 3, 5, 7, 17};
    for (unsigned length : lengths) {
        unsigned char bytes[32]; for (auto &b : bytes) b = 0xa5;
        EXPECT(qemu_guest_getrandom(bytes + 4, length) == 0);
        for (unsigned i = 0; i < sizeof bytes; ++i)
            if (i < 4 || i >= length + 4) EXPECT(bytes[i] == 0xa5);
    }
    // Sum 1..10000 with ordinary x86 arithmetic and a backward branch.
    const unsigned char sum[] = {0x31,0xc0,0xb9,0x10,0x27,0,0,0x01,0xc8,0x49,0x75,0xfb};
    CHECK(uc_mem_write(cpu, 0x10000, sum, sizeof sum));
    CHECK(uc_emu_start(cpu, 0x10000, 0x10000 + sizeof sum, 0, 40000));
    uint32_t eax = 0;
    CHECK(uc_reg_read(cpu, UC_X86_REG_EAX, &eax)); EXPECT(eax == 50005000);
    uc_tb translated = {};
    CHECK(uc_ctl_request_cache(cpu, uint64_t(0x10007), &translated));
    EXPECT(translated.pc == 0x10007 && translated.icount == 3 && translated.size == 5);
    // Host changes already translated code. Unicorn must invalidate it.
    const unsigned char changed[] = {0xb8,0x62,0,0,0};
    CHECK(uc_mem_write(cpu, 0x10000, changed, sizeof changed));
    CHECK(uc_emu_start(cpu, 0x10000, 0x10005, 0, 10));
    CHECK(uc_reg_read(cpu, UC_X86_REG_EAX, &eax)); EXPECT(eax == 98);
    // Guest writes over a future instruction in its own translated block.
    const unsigned char self_modify[] = {0xc6,0x05,0x08,0x03,1,0,0x63,0xb8,0x01,0,0,0};
    CHECK(uc_mem_write(cpu, 0x10300, self_modify, sizeof self_modify));
    CHECK(uc_emu_start(cpu, 0x10300, 0x1030c, 0, 32));
    CHECK(uc_reg_read(cpu, UC_X86_REG_EAX, &eax)); EXPECT(eax == 99);
    CHECK(uc_mem_protect(cpu, 0x10000, 0x1000, UC_PROT_READ | UC_PROT_EXEC));
    const unsigned char store[] = {0xc7,0x05,0,0,1,0,0xef,0xbe,0xad,0xde};
    CHECK(uc_mem_write(cpu, 0x10100, store, sizeof store));
    EXPECT(uc_emu_start(cpu, 0x10100, 0x1010a, 0, 10) == UC_ERR_WRITE_PROT);
    CHECK(uc_mem_read(cpu, 0x10000, &eax, 4)); EXPECT(eax == 0x62b8);
    // Same hook mechanism as the hosted Notepad Windows API bridge.
    uc_hook hook; unsigned calls = 0;
    CHECK(uc_hook_add(cpu, &hook, UC_HOOK_CODE, (void *)gate, &calls, 0x10200, 0x10200));
    CHECK(uc_emu_start(cpu, 0x10200, 0x10201, 0, 10));
    CHECK(uc_reg_read(cpu, UC_X86_REG_EAX, &eax)); EXPECT(eax == 98 && calls == 1);
    EXPECT(uc_emu_start(cpu, 0x10000, 0x10005, 1, 10) == UC_ERR_ARG);
    EXPECT(uc_emu_start(cpu, 0x20000, 0x20001, 0, 10) == UC_ERR_FETCH_UNMAPPED);
    // Restore the page and check that an infinite guest loop yields by budget.
    const unsigned char forever[] = {0xeb,0xfe};
    CHECK(uc_mem_write(cpu, 0x10380, forever, sizeof forever));
    CHECK(uc_emu_start(cpu, 0x10380, 0x10382, 0, 50));
    uint32_t eip = 0;
    CHECK(uc_reg_read(cpu, UC_X86_REG_EIP, &eip)); EXPECT(eip == 0x10380);
    CHECK(uc_close(cpu)); cpu = nullptr;
    // Closing the engine must return its executable pool.
    CHECK(uc_open(UC_ARCH_X86, UC_MODE_32, &cpu));
    CHECK(uc_ctl_set_tcg_buffer_size(cpu, NEO98_JIT_SLOT_BYTES));
    CHECK(uc_mem_map(cpu, 0x10000, 0x1000, UC_PROT_ALL));
    CHECK(uc_mem_write(cpu, 0x10000, changed, sizeof changed));
    CHECK(uc_emu_start(cpu, 0x10000, 0x10005, 0, 10));
    CHECK(uc_close(cpu)); cpu = nullptr;
    // Every slot of the code cache must be usable at the same time. Each engine
    // runs its own program and keeps its own registers across the others' runs.
    EXPECT(neo98_jit_free_slots() == NEO98_JIT_SLOTS);
    uc_engine *engines[NEO98_JIT_SLOTS] = {};
    for (unsigned i = 0; i < NEO98_JIT_SLOTS; ++i) {
        CHECK(uc_open(UC_ARCH_X86, UC_MODE_32, &engines[i]));
        cpu = engines[i];
        CHECK(uc_ctl_set_tcg_buffer_size(cpu, NEO98_JIT_SLOT_BYTES));
        CHECK(uc_mem_map(cpu, 0x10000, 0x1000, UC_PROT_ALL));
        // mov eax, imm32; add eax, eax; each engine gets a different constant.
        const unsigned char program[] = {0xb8, (unsigned char)(i + 1), 0, 0, 0, 0x01, 0xc0};
        CHECK(uc_mem_write(cpu, 0x10000, program, sizeof program));
        EXPECT(neo98_jit_free_slots() == NEO98_JIT_SLOTS - i - 1);
    }
    EXPECT(!neo98_jit_free_slots());
    for (unsigned i = 0; i < NEO98_JIT_SLOTS; ++i) {
        cpu = engines[i];
        CHECK(uc_emu_start(cpu, 0x10000, 0x10007, 0, 10));
    }
    for (unsigned i = 0; i < NEO98_JIT_SLOTS; ++i) {
        cpu = engines[i];
        CHECK(uc_reg_read(cpu, UC_X86_REG_EAX, &eax)); EXPECT(eax == 2 * (i + 1));
    }
    // A released slot goes back to the pool and serves the next engine.
    cpu = engines[1];
    CHECK(uc_close(cpu)); engines[1] = nullptr; cpu = nullptr;
    EXPECT(neo98_jit_free_slots() == 1);
    CHECK(uc_open(UC_ARCH_X86, UC_MODE_32, &engines[1]));
    cpu = engines[1];
    CHECK(uc_ctl_set_tcg_buffer_size(cpu, NEO98_JIT_SLOT_BYTES));
    CHECK(uc_mem_map(cpu, 0x10000, 0x1000, UC_PROT_ALL));
    CHECK(uc_mem_write(cpu, 0x10000, changed, sizeof changed));
    CHECK(uc_emu_start(cpu, 0x10000, 0x10005, 0, 10));
    CHECK(uc_reg_read(cpu, UC_X86_REG_EAX, &eax)); EXPECT(eax == 98);
    EXPECT(!neo98_jit_free_slots());
    for (auto *engine : engines) {
        cpu = nullptr;
        if (engine) CHECK(uc_close(engine));
    }
    EXPECT(neo98_jit_free_slots() == NEO98_JIT_SLOTS);
    return 0;
}

class UnicornAddressSpace final : public Pe32::AddressSpace {
public:
    explicit UnicornAddressSpace(uc_engine *value) : cpu(value) {}
    bool Map(unsigned address, unsigned size, unsigned permissions) override
    { return uc_mem_map(cpu, address, size, permissions) == UC_ERR_OK; }
    bool Store(unsigned address, const void *bytes, Pe32::Size size) override
    { return uc_mem_write(cpu, address, bytes, size) == UC_ERR_OK; }
    bool Protect(unsigned address, unsigned size, unsigned permissions) override
    { return uc_mem_protect(cpu, address, size, permissions) == UC_ERR_OK; }
private:
    uc_engine *cpu;
};

unsigned UnicornOriginalUserTest()
{
    Pe32::Image image;
    if (!image.Parse(OriginalUser32, sizeof OriginalUser32)) { neo98_diagnostic(image.Error()); return 1; }
    uc_engine *cpu = nullptr;
    if (uc_open(UC_ARCH_X86, UC_MODE_32, &cpu) != UC_ERR_OK) return 2;
    struct Close { uc_engine *cpu; ~Close() { uc_close(cpu); } } close{cpu};
    if (uc_ctl_set_tcg_buffer_size(cpu, NEO98_JIT_SLOT_BYTES)) return 3;
    UnicornAddressSpace address_space(cpu);
    if (!image.Load(address_space)) return 4;
    const char *names[] = {"SetRect", "SetRectEmpty", "CopyRect", "IsRectEmpty", "PtInRect",
                           "OffsetRect", "InflateRect", "IntersectRect", "EqualRect"};
    uint32_t functions[9];
    for (unsigned i = 0; i < 9; ++i) if (!(functions[i] = image.Export(names[i]))) return 5 + i;
    if (image.Export("ThisExportDoesNotExist")) return 14;
    constexpr uint32_t memory = 0x100000, stack = 0x208000, stop = 0x300000, gdt_address = 0x400000;
    if (uc_mem_map(cpu, memory, 4096, UC_PROT_READ | UC_PROT_WRITE) ||
        uc_mem_map(cpu, 0x200000, 65536, UC_PROT_READ | UC_PROT_WRITE) ||
        uc_mem_map(cpu, stop, 4096, UC_PROT_READ | UC_PROT_EXEC) ||
        uc_mem_map(cpu, gdt_address, 4096, UC_PROT_READ | UC_PROT_WRITE)) return 15;
    const uint64_t descriptors[] = {0, 0x00cf9a000000ffffull, 0x00cf92000000ffffull};
    uc_x86_mmr gdtr = {}; gdtr.base = gdt_address; gdtr.limit = sizeof descriptors - 1;
    uint32_t code = 8, data = 16;
    if (uc_mem_write(cpu, gdt_address, descriptors, sizeof descriptors) || uc_reg_write(cpu, UC_X86_REG_GDTR, &gdtr) ||
        uc_reg_write(cpu, UC_X86_REG_CS, &code)) return 16;
    const int segments[] = {UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS};
    for (int reg : segments)
        if (uc_reg_write(cpu, reg, &data)) return 17;
    for (unsigned index = 0; index < Win98::OriginalTestCount(); ++index) {
        const auto &v = Win98::OriginalCase(index);
        struct { uint32_t guard1[4]; Win98::Rect rects[3]; uint32_t guard2[4]; } values;
        for (unsigned i = 0; i < 4; ++i) { values.guard1[i] = 0xa5a5a5a5; values.guard2[i] = 0x5a5a5a5a; }
        uint32_t ptr[3];
        for (unsigned i = 0; i < 3; ++i) { values.rects[i] = v.before[i]; ptr[i] = v.pointers[i] < 0 ? 0 : memory + 16 + 16 * v.pointers[i]; }
        uint32_t args[6] = {stop, ptr[0]}, count = 2;
        switch (v.operation) {
        case 0: for (unsigned i = 0; i < 4; ++i) args[2+i] = v.args[i]; count = 6; break;
        case 2: case 8: args[2] = ptr[1]; count = 3; break;
        case 4: case 5: case 6: args[2] = v.args[0]; args[3] = v.args[1]; count = 4; break;
        case 7: args[2] = ptr[1]; args[3] = ptr[2]; count = 4; break;
        }
        uint32_t esp = stack, flags = 2, eax = 0, eip = 0;
        if (uc_mem_write(cpu, memory, &values, sizeof values) || uc_mem_write(cpu, stack, args, count * 4) ||
            uc_reg_write(cpu, UC_X86_REG_ESP, &esp) || uc_reg_write(cpu, UC_X86_REG_EFLAGS, &flags) ||
            uc_emu_start(cpu, functions[v.operation], stop, 0, 256) ||
            uc_reg_read(cpu, UC_X86_REG_EAX, &eax) || uc_reg_read(cpu, UC_X86_REG_EIP, &eip) ||
            uc_reg_read(cpu, UC_X86_REG_ESP, &esp) || uc_mem_read(cpu, memory, &values, sizeof values)) return 100 + index;
        if (eax != v.result || eip != stop || esp != stack + 4 * count) return 100 + index;
        for (unsigned i = 0; i < 4; ++i) if (values.guard1[i] != 0xa5a5a5a5 || values.guard2[i] != 0x5a5a5a5a) return 100 + index;
        for (unsigned i = 0; i < 3; ++i) if (!Win98::EqualRect(&values.rects[i], &v.after[i])) return 100 + index;
    }
    return 0;
}
