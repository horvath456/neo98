// SPDX-License-Identifier: GPL-2.0-or-later
// The dynamic pointer arrays of the 16-bit COMMCTRL.DLL, ordinals 328 to 339.
// Included by src/win16_shell.cpp.
//
// SETUPX takes them with GetProcAddress (DPA_Create and the others) and keeps
// the class and driver lists of the device installer in them; without them
// DiBuildClassDrvList built nothing and the modem wizard showed an empty
// model list. The supported operations are:
//
// DPA_Create DPA_Destroy DPA_Grow
// DPA_Clone DPA_GetPtr DPA_GetPtrIndex
// DPA_InsertPtr DPA_SetPtr DPA_DeletePtr
// DPA_DeleteAllPtrs DPA_Sort (heap sort)
// DPA_Search
//
// The array is a 12-byte block the caller reaches by far pointer (macros like
// DPA_GetPtrCount read it directly): +0 cp, +2 far pointer to the pointers,
// +6 heap, +8 cpAlloc, +0Ah cpGrow (at least 8). COMMCTRL takes its blocks
// from its own shared far heap (zero-filled, GlobalAlloc above
// F000h bytes); here every block is a fixed zero-filled global block, so a
// pointer is selector:0. Nothing but the numbers of the pointers differs.
namespace
{
int Signed16(unsigned v) { return (short)(v & 0xffff); }
// The Callback slot of the gate region a callback returns to, the same
// address src/win16_user.cpp and src/x86_win16_hosted.cpp stop at.
constexpr unsigned DpaCallbackReturn = 0x7000fff0;
} // namespace

unsigned Application::Win16DpaAlloc(unsigned bytes)
{
    const unsigned handle = Win16GlobalAlloc(0x40 /* GMEM_FIXED | GMEM_ZEROINIT */, bytes ? bytes : 1);
    Win16Block *block = handle ? Win16GlobalBlock(handle) : nullptr;
    return block ? block->selector << 16 : 0;
}
bool Application::Win16DpaFree(unsigned far_pointer)
{
    return far_pointer && Win16GlobalFree(far_pointer >> 16);
}
// a new block of the size with the old contents in front; the old
// block goes back. A null pointer is a plain allocation.
unsigned Application::Win16DpaReAlloc(unsigned far_pointer, unsigned bytes)
{
    if (!far_pointer)
        return Win16DpaAlloc(bytes);
    const unsigned fresh = Win16DpaAlloc(bytes);
    if (!fresh)
        return 0;
    const unsigned old_bytes = Win16Limit(far_pointer >> 16) + 1;
    const unsigned count = old_bytes < bytes ? old_bytes : bytes;
    unsigned char chunk[512];
    const unsigned from = Win16Linear(far_pointer >> 16, far_pointer), to = Win16Linear(fresh >> 16, 0);
    for (unsigned at = 0; at < count; at += sizeof chunk)
    {
        const unsigned n = count - at < sizeof chunk ? count - at : sizeof chunk;
        Read(from + at, chunk, n);
        Store(to + at, chunk, n);
    }
    Win16DpaFree(far_pointer);
    return fresh;
}
// A 16-bit Pascal routine called from a gate: the words in push order below
// the caller's stack, the answer in DX:AX. The compare procedures of Sort
// and Search load their own data segment, as COMMCTRL calls them with its own.
unsigned Application::Win16CallPascal(unsigned far_pointer, const unsigned *push, unsigned count, unsigned ds)
{
    const unsigned tail = TakeTail();
    Win16TaskStack task_stack(*this);
    if (!far_pointer || !win16.state || state == Failed || state == Exited)
        return 0;
    if (++depth > 32)
    {
        Fail("Win16 callback nesting exceeded");
        --depth;
        return 0;
    }
    CpuContext *context = nullptr;
    if (!Check(cpu->NewContext(context)))
    {
        --depth;
        return 0;
    }
    Check(cpu->Save(context));
    const unsigned old_pending = pending;
    const bool old_waiting = waiting;
    waiting = false;
    const unsigned stack = Register(Reg::Ss);
    unsigned sp = (Register(Reg::Esp) & 0xffff) & ~1u;
    const unsigned saved_sp = Register(Reg::Esp);
    const unsigned base = Win16Linear(stack, 0);
    sp -= 4 + count * 2;
    for (unsigned i = 0; i < count; ++i)
        Win16Word(base + sp + 4 + (count - 1 - i) * 2, push[i]);
    Win16Word(base + sp, DpaCallbackReturn & 0xffff);
    Win16Word(base + sp + 2, Win16GateSelector());
    Register(Reg::Esp, sp);
    Register(Reg::Edx, 0);
    // A callback of the task (an AbortProc) gets the task's data
    // segment in DS and AX, as USER hands its callbacks.
    if (ds == ~0u)
        ds = win16.state->instance_handle;
    if (ds)
    {
        Register(Reg::Ds, ds);
        Register(Reg::Eax, ds);
    }
    Register(Reg::Cs, far_pointer >> 16);
    Register(Reg::Eip, far_pointer & 0xffff);
    ++callbacks;
    auto &thread = threads[current_thread];
    const bool page_release = !task_stack.flat && tail == TailRecord && thread.host_frame_count &&
        thread.host_frames[thread.host_frame_count - 1].kind == HostWin16PageDestroy;
    thread.call_marks[depth] = {0, 0, old_pending, context, page_release ? TailRecord : TailNone,
        thread.host_frame_count, 1, saved_sp, reinterpret_cast<unsigned long long>(__builtin_frame_address(0))};
    bool returned = false;
    while (state != Failed && state != Exited && !threads[current_thread].done)
    {
        if (!Step())
            break;
        if (pending == DpaCallbackReturn)
        {
            returned = true;
            break;
        }
        if (pending && !Service())
            break;
    }
    const unsigned result = (Register(Reg::Eax) & 0xffff) | ((Register(Reg::Edx) & 0xffff) << 16);
    thread.call_marks[depth] = {};
    if (!returned && state != Failed && state != Exited && !threads[current_thread].done)
    {
        char reason[160];
        snprintf(reason, sizeof reason, "Win16 callback interrupted procedure=%04x:%04x", far_pointer >> 16,
                 far_pointer & 0xffff);
        Fail(reason);
    }
    Check(cpu->Restore(context));
    cpu->FreeContext(context);
    pending = old_pending;
    waiting = old_waiting;
    --depth;
    return result;
}

bool Application::Win16DpaCall(unsigned ordinal, const Win16Frame &frame, unsigned &result, unsigned &words)
{
    // The fields of the array behind a far pointer.
    struct Array
    {
        Application &app;
        unsigned at;
        int Count() { return Signed16(app.Win16Word(at)); }
        void Count(int v) { app.Win16Word(at, (unsigned)v & 0xffff); }
        unsigned Items() { return app.Read32(at + 2); }
        void Items(unsigned v) { app.Write32(at + 2, v); }
        int Allocated() { return Signed16(app.Win16Word(at + 8)); }
        void Allocated(int v) { app.Win16Word(at + 8, (unsigned)v & 0xffff); }
        int GrowBy() { return Signed16(app.Win16Word(at + 0xa)); }
        unsigned Slot(int i) { return app.Win16Linear(Items() >> 16, (Items() & 0xffff) + i * 4); }
        unsigned Get(int i) { return app.Read32(Slot(i)); }
        void Set(int i, unsigned v) { app.Write32(Slot(i), v); }
    };
    const auto array = [&](unsigned far_pointer) { return Array{*this, Win16Linear(far_pointer >> 16, far_pointer)}; };
    // a new array of 12 bytes.
    const auto create = [&](int grow, unsigned heap) -> unsigned {
        const unsigned block = Win16DpaAlloc(12);
        if (!block)
            return 0;
        const unsigned at = Win16Linear(block >> 16, 0);
        Win16Word(at, 0);
        Write32(at + 2, 0);
        Win16Word(at + 6, heap);
        Win16Word(at + 8, 0);
        Win16Word(at + 0xa, (unsigned)(grow < 8 ? 8 : grow) & 0xffff);
        return block;
    };
    // room for at least `wanted` pointers, in steps of cpGrow.
    const auto grow = [&](unsigned far_pointer, int wanted) -> bool {
        Array a = array(far_pointer);
        if (a.Allocated() >= wanted)
            return true;
        const int step = a.GrowBy() ? a.GrowBy() : 8;
        const int size = (wanted + step - 1) / step * step;
        const unsigned items = Win16DpaReAlloc(a.Items(), (unsigned)(size * 4) & 0xffff);
        if (!items)
            return false;
        a.Items(items);
        a.Allocated(size);
        return true;
    };
    // KERNEL hmemcpy over the pointer array; the moves overlap.
    const auto move = [&](Array &a, int to, int from, int count) {
        if (count <= 0)
            return;
        if (to < from)
            for (int i = 0; i < count; ++i)
                a.Set(to + i, a.Get(from + i));
        else
            for (int i = count - 1; i >= 0; --i)
                a.Set(to + i, a.Get(from + i));
    };
    switch (ordinal)
    {
    case 328: // DPA_Create(cpGrow)
        words = 1;
        result = create(Signed16(frame.Word(0)), 0);
        return state != Failed;
    case 329: // DPA_Destroy(hdpa)
    {
        words = 2;
        const unsigned hdpa = frame.Long(0);
        result = 1;
        if (!hdpa)
            return true;
        Array a = array(hdpa);
        if (a.Items() && !Win16DpaFree(a.Items()))
        {
            result = 0;
            return true;
        }
        result = Win16DpaFree(hdpa) ? 1 : 0;
        return true;
    }
    case 330: // DPA_Grow(hdpa, cp)
        words = 3;
        result = grow(frame.Long(1), Signed16(frame.Word(0))) ? 1 : 0;
        return state != Failed;
    case 331: // DPA_Clone(hdpa, hdpaNew)
    {
        words = 4;
        const unsigned hdpa = frame.Long(2);
        unsigned target = frame.Long(0);
        Array a = array(hdpa);
        bool made = false;
        if (!target)
        {
            target = create(a.GrowBy(), Win16Word(a.at + 6));
            if (!target)
            {
                result = 0;
                return state != Failed;
            }
            made = true;
        }
        if (!grow(target, a.Allocated()))
        {
            if (made)
            {
                Win16DpaFree(array(target).Items());
                Win16DpaFree(target);
            }
            result = 0;
            return state != Failed;
        }
        Array b = array(target);
        b.Count(a.Count());
        for (int i = 0; i < a.Count(); ++i)
            b.Set(i, a.Get(i));
        result = target;
        return true;
    }
    case 332: // DPA_GetPtr(hdpa, i)
    {
        words = 3;
        const int i = Signed16(frame.Word(0));
        Array a = array(frame.Long(1));
        result = i >= 0 && i < a.Count() ? a.Get(i) : 0;
        return true;
    }
    case 333: // DPA_GetPtrIndex(hdpa, p)
    {
        words = 4;
        const unsigned p = frame.Long(0);
        Array a = array(frame.Long(2));
        result = 0xffff;
        if (!a.Items())
            return true;
        for (int i = 0; i < a.Count(); ++i)
            if (a.Get(i) == p)
            {
                result = (unsigned)i;
                break;
            }
        return true;
    }
    case 334: // DPA_InsertPtr(hdpa, i, p)
    {
        words = 5;
        const unsigned hdpa = frame.Long(3), p = frame.Long(0);
        int i = Signed16(frame.Word(2));
        result = 0xffff;
        if (i < 0)
            return true;
        Array a = array(hdpa);
        if (i > a.Count())
            i = a.Count();
        if (a.Count() + 1 > a.Allocated() && !grow(hdpa, a.Count() + 1))
            return state != Failed;
        move(a, i + 1, i, a.Count() - i);
        a.Set(i, p);
        a.Count(a.Count() + 1);
        result = (unsigned)i;
        return true;
    }
    case 335: // DPA_SetPtr(hdpa, i, p)
    {
        words = 5;
        const unsigned hdpa = frame.Long(3), p = frame.Long(0);
        const int i = Signed16(frame.Word(2));
        result = 0;
        if (i < 0)
            return true;
        Array a = array(hdpa);
        if (a.Count() <= i)
        {
            if (!grow(hdpa, i + 1))
                return state != Failed;
            a.Count(i + 1);
        }
        a.Set(i, p);
        result = 1;
        return true;
    }
    case 336: // DPA_DeletePtr(hdpa, i)
    {
        words = 3;
        const int i = Signed16(frame.Word(0));
        Array a = array(frame.Long(1));
        result = 0;
        if (i < 0 || a.Count() <= i)
            return true;
        const unsigned p = a.Get(i);
        move(a, i, i + 1, a.Count() - i - 1);
        a.Count(a.Count() - 1);
        // shrink by one step when more than a step is free.
        if (a.Allocated() - a.Count() > a.GrowBy())
        {
            a.Items(Win16DpaReAlloc(a.Items(), (unsigned)((a.Allocated() - a.GrowBy()) * 4) & 0xffff));
            a.Allocated(a.Allocated() - a.GrowBy());
        }
        result = p;
        return state != Failed;
    }
    case 337: // DPA_DeleteAllPtrs(hdpa)
    {
        words = 2;
        Array a = array(frame.Long(0));
        result = 0;
        if (a.Items() && !Win16DpaFree(a.Items()))
            return true;
        a.Items(0);
        a.Allocated(0);
        a.Count(0);
        result = 1;
        return true;
    }
    case 338: // DPA_Sort(hdpa, pfnCompare, lParam): the heap sort of
    {
        words = 6;
        const unsigned compare = frame.Long(2), parameter = frame.Long(0);
        Array a = array(frame.Long(4));
        const auto order = [&](unsigned first, unsigned second) {
            const unsigned push[] = {first >> 16, first & 0xffff, second >> 16, second & 0xffff,
                                     parameter >> 16, parameter & 0xffff};
            return Signed16(Win16CallPascal(compare, push, 6));
        };
        const auto swap = [&](int x, int y) {
            const unsigned t = a.Get(x);
            a.Set(x, a.Get(y));
            a.Set(y, t);
        };
        // on the 1-based heap.
        const auto sift = [&](int n, int i) {
            const int half = n / 2;
            for (int k = i; k <= half && state != Failed;)
            {
                const int r1 = order(a.Get(k - 1), a.Get(2 * k - 1));
                if (2 * k == n)
                {
                    if (r1 < 0)
                        swap(k - 1, 2 * k - 1);
                    return;
                }
                const int r2 = order(a.Get(2 * k - 1), a.Get(2 * k));
                if (r1 < 0 && r2 >= 0)
                {
                    swap(k - 1, 2 * k - 1);
                    k = 2 * k;
                    continue;
                }
                if (order(a.Get(k - 1), a.Get(2 * k)) >= 0 || r2 >= 0)
                    return;
                swap(k - 1, 2 * k);
                k = 2 * k + 1;
            }
        };
        const int n = a.Count();
        for (int i = n / 2; i >= 1; --i)
            sift(n, i);
        for (int k = n; k >= 2 && state != Failed; --k)
        {
            swap(0, k - 1);
            sift(k - 1, 1);
        }
        result = 1;
        return state != Failed;
    }
    case 339: // DPA_Search(hdpa, pFind, iStart, pfnCompare, lParam, options)
    {
        words = 10;
        const unsigned options = frame.Word(0), parameter = frame.Long(1), compare = frame.Long(3);
        const int start = Signed16(frame.Word(5));
        const unsigned find = frame.Long(6);
        Array a = array(frame.Long(8));
        const auto order = [&](unsigned item) {
            const unsigned push[] = {find >> 16, find & 0xffff, item >> 16, item & 0xffff, parameter >> 16,
                                     parameter & 0xffff};
            return Signed16(Win16CallPascal(compare, push, 6));
        };
        const int count = a.Count();
        if (!(options & 1)) // DPAS_SORTED clear: from iStart on
        {
            result = 0xffff;
            for (int i = start; i < count && state != Failed; ++i)
                if (!order(a.Get(i)))
                {
                    result = (unsigned)i;
                    break;
                }
            return state != Failed;
        }
        int low = 0, high = count - 1, middle = 0, last = 0;
        bool found = false;
        while (high >= low && state != Failed)
        {
            middle = (high + low) / 2;
            const int r = order(a.Get(middle));
            if (r < 0)
                high = middle - 1;
            else if (r > 0)
                low = middle + 1;
            else
            {
                // the first of the equal ones.
                found = true;
                while (middle > 0 && !order(a.Get(middle - 1)))
                    --middle;
                break;
            }
            last = r;
        }
        int answer = -1;
        if (found)
            answer = middle;
        else if (options & 6) // DPAS_INSERTBEFORE, DPAS_INSERTAFTER
            answer = last > 0 ? low : middle;
        result = (unsigned)answer & 0xffff;
        return state != Failed;
    }
    default:
        break;
    }
    return false;
}
