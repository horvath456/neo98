// SPDX-License-Identifier: GPL-2.0-or-later
// Where the host's main thread stands while the machine stands.
//
// The whole machine runs on the main thread of the host, between the events
// of the Mac and the present of the picture. When that thread stands for
// 100 ms or more, no Windows thread runs and the sound is held
// (Sound::Mixer::Beat). A slow turn of the log says which part of the turn
// took the time; this watch says where in it: a thread of its own notices
// the missing beat, stops the main thread for a moment, follows its frame
// pointers and writes the frames into the log, at 100 ms of a stand and
// every 250 ms after:
//
//     NEO98 HOST stall stack age=120 frames=23
//     NEO98 HOST stall frame 0 HIToolbox!ReceiveNextEventCommon+276
//     NEO98 HOST stall frame 5 Neo98+0x2c41f0
//
// A frame of the program itself names its offset in the image, which atos
// resolves against the unstripped build/native-app/neo98-host. macOS on
// ARM64 only; elsewhere the watch does nothing.
#pragma once
#include "../src/sound.h"

#if defined(__APPLE__) && defined(__aarch64__)
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <pthread.h>
#include <thread>

namespace HostStallStack
{
class Watch
{
  public:
    // On the main thread: that thread is the one watched.
    void Start()
    {
        if (started.exchange(true)) return;
        main_thread = pthread_mach_thread_np(pthread_self());
        std::thread([this] { Run(); }).detach();
    }

  private:
    static constexpr long long StandMilliseconds = 100;
    static constexpr int Capacity = 48;
    std::atomic<bool> started{false};
    mach_port_t main_thread = MACH_PORT_NULL;

    // A long stand is sampled again every quarter of a second, four times
    // at most, so its stack shows what it went through.
    void Run()
    {
        long long next = StandMilliseconds;
        int samples = 0;
        for (;;)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            const long long age = Sound::Shared().SinceBeat();
            if (age < 0) continue;
            if (age < StandMilliseconds / 2) { next = StandMilliseconds; samples = 0; continue; }
            if (age < next || samples >= 4) continue;
            ++samples;
            next = age + 250;
            Sample(age);
        }
    }

    static uintptr_t Strip(uintptr_t address) { return address & 0x0000ffffffffffffull; }

    void Sample(long long age)
    {
        uintptr_t frames[Capacity];
        int count = 0;
        // Nothing that could take a lock the main thread holds runs while
        // it is stopped: no allocation, no output, no dladdr.
        if (thread_suspend(main_thread) != KERN_SUCCESS) return;
        arm_thread_state64_t state;
        mach_msg_type_number_t state_count = ARM_THREAD_STATE64_COUNT;
        if (thread_get_state(main_thread, ARM_THREAD_STATE64, (thread_state_t)&state, &state_count) == KERN_SUCCESS)
        {
            frames[count++] = Strip(arm_thread_state64_get_pc(state));
            frames[count++] = Strip(arm_thread_state64_get_lr(state));
            uintptr_t fp = Strip(arm_thread_state64_get_fp(state));
            while (fp && count < Capacity)
            {
                uintptr_t pair[2] = {};
                mach_vm_size_t read = 0;
                if (mach_vm_read_overwrite(mach_task_self(), fp, sizeof pair, (mach_vm_address_t)pair, &read) !=
                        KERN_SUCCESS ||
                    read != sizeof pair || !pair[1])
                    break;
                frames[count++] = Strip(pair[1]);
                const uintptr_t next = Strip(pair[0]);
                if (next <= fp) break;
                fp = next;
            }
        }
        thread_resume(main_thread);
        printf("NEO98 HOST stall stack age=%lld frames=%d\n", age, count);
        // The program's own image is stripped in the app: its frames go by
        // offset, not by the nearest symbol that happens to be exported.
        Dl_info own{};
        dladdr((const void *)&Watch::Strip, &own);
        for (int i = 0; i < count; ++i)
        {
            Dl_info info{};
            const char *image = "?";
            if (dladdr((const void *)frames[i], &info) && info.dli_fname)
            {
                image = strrchr(info.dli_fname, '/') ? strrchr(info.dli_fname, '/') + 1 : info.dli_fname;
                if (info.dli_sname && info.dli_saddr && info.dli_fbase != own.dli_fbase)
                {
                    printf("NEO98 HOST stall frame %d %s!%s+%lu\n", i, image, info.dli_sname,
                           (unsigned long)(frames[i] - (uintptr_t)info.dli_saddr));
                    continue;
                }
            }
            printf("NEO98 HOST stall frame %d %s+0x%lx\n", i, image,
                   (unsigned long)(frames[i] - (uintptr_t)info.dli_fbase));
        }
        fflush(stdout);
    }
};
} // namespace HostStallStack
#else
namespace HostStallStack
{
class Watch
{
  public:
    void Start() {}
};
} // namespace HostStallStack
#endif
