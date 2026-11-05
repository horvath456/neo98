// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// NEO98 owns cooperative host execution contexts. No Circle scheduler dependency.
// This preserves a native Windows-service call stack while an API waits for input.
class Fiber
{
  public:
    using Entry = void (*)(void *);
    Fiber() = default;
    ~Fiber();
    Fiber(const Fiber &) = delete;
    Fiber &operator=(const Fiber &) = delete;
    bool Start(Entry, void *, unsigned stack_bytes = 1024 * 1024);
    void Resume();
    void Yield();
    bool Finished() const
    {
        return finished;
    }
    bool Active() const
    {
        return active;
    }
    void Run();
    // The stack, for a saved state: it is not part of one (src/x86_state.h).
    const unsigned char *StackBase() const
    {
        return stack;
    }
    unsigned StackBytes() const
    {
        return size;
    }
    // A fiber made in the place of one that had run to its end (a loaded
    // state): it is never resumed.
    void Retire()
    {
        finished = true;
    }
    // Where a fiber that has yielded stands on its own stack: its frame
    // pointer and the address it goes on at. For the one who wants to know
    // which host functions a waiting thread is inside (MachineState::Chains).
    void SavedFrame(unsigned long long &frame, unsigned long long &address) const
    {
        frame = callee.words[10];
        address = callee.words[11];
    }
    // Whether the entry has begun to run.
    bool Started() const
    {
        return started;
    }

  private:
    struct alignas(16) Context
    {
        unsigned long long words[24] = {};
    } caller, callee;
    Entry entry = nullptr;
    void *argument = nullptr;
    unsigned char *stack = nullptr;
    unsigned size = 0;
    bool finished = false, active = false, started = false;
    void *caller_fake = nullptr, *callee_fake = nullptr;
    const void *caller_bottom = nullptr;
    __SIZE_TYPE__ caller_size = 0;
};
