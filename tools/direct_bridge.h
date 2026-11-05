// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#define DIRECT_HOST_SP 128
#define DIRECT_HOST_RBX 136
#define DIRECT_HOST_RBP 144
#define DIRECT_HOST_R12 152
#define DIRECT_HOST_R13 160
#define DIRECT_HOST_R14 168
#define DIRECT_HOST_R15 176
#define DIRECT_HOST_FS 184
#define DIRECT_HOST_GS 192
#define DIRECT_HOST_DS 200
#define DIRECT_HOST_ES 208
#define DIRECT_HOST_FSS 216
#define DIRECT_HOST_GSS 224
#define DIRECT_HOST_SS 232
#define DIRECT_STACK_BASE 240
#define DIRECT_CODE_BASE 248
#define DIRECT_HOST_FP 256
#define DIRECT_GUEST_FP 768
#define DIRECT_EVENT 1280
#define DIRECT_HOST_FLAGS 1344
#ifndef __ASSEMBLER__
#include <cstddef>
#include <cstdint>
#include <sys/user.h>
#include <ucontext.h>
#include <csignal>
#include <atomic>
struct alignas(16) Neo98DirectFrame
{
    uint64_t gpr[8] = {}, ip = 0, flags = 0, segments[6] = {};
    uint64_t host_sp = 0, host_rbx = 0, host_rbp = 0, host_r12 = 0;
    uint64_t host_r13 = 0, host_r14 = 0, host_r15 = 0;
    uint64_t host_fs = 0, host_gs = 0, host_ds = 0, host_es = 0;
    uint64_t host_fss = 0, host_gss = 0, host_ss = 0;
    uint64_t stack_base = 0, code_base = 0;
    user_fpregs_struct host_fp = {}, guest_fp = {};
    uint64_t event = 0, trap = 0, error = 0, address = 0, signal = 0, signal_code = 0;
    const std::atomic<bool> *stop_requested = nullptr;
    uint64_t want_slice = 0;
    uint64_t host_flags = 0;
};
static_assert(offsetof(Neo98DirectFrame, host_sp) == DIRECT_HOST_SP);
static_assert(offsetof(Neo98DirectFrame, host_fp) == DIRECT_HOST_FP);
static_assert(offsetof(Neo98DirectFrame, guest_fp) == DIRECT_GUEST_FP);
static_assert(offsetof(Neo98DirectFrame, event) == DIRECT_EVENT);
static_assert(offsetof(Neo98DirectFrame, host_flags) == DIRECT_HOST_FLAGS);
static_assert(offsetof(ucontext_t, uc_mcontext) + offsetof(mcontext_t, gregs) + REG_CSGSFS * sizeof(greg_t) ==
              184);
extern "C" void neo98_direct_enter(Neo98DirectFrame *);
extern "C" const unsigned char neo98_direct_enter_end[];
extern "C" void neo98_direct_finish();
extern "C" const unsigned char neo98_direct_gate_start[], neo98_direct_gate_end[];
extern "C" Neo98DirectFrame *neo98_direct_active_frame;
extern "C" uint64_t neo98_direct_active_tid;
extern "C" void neo98_direct_signal(int, siginfo_t *, void *);
extern "C" int neo98_direct_handle_signal(int, siginfo_t *, void *, Neo98DirectFrame *);
#endif
