/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Position-independent x86 stdcall helper. The ICD emits a small per-entry
   thunk passing this helper the original arguments and its thread's queue.
   No pointers in API arguments are dereferenced here. Only scalar setters
   and client-array pointer declarations may use this queue. */
typedef unsigned U;
struct Command { U slot, args[4]; };
struct Queue { U count, flush, reserved[2]; struct Command commands[512]; };
void __stdcall Neo98GlRecord(struct Queue *q, U slot, U count, const U *args)
{
    U next = q->count;
    if (next >= 512)
    {
        ((void (__stdcall *)(void))q->flush)();
        next = q->count;
        if (next >= 512) return;
    }
    q->commands[next].slot = slot;
    for (U i = 0; i < count && i < 4; ++i) q->commands[next].args[i] = args[i];
    q->count = next + 1;
}
