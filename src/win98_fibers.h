// SPDX-License-Identifier: GPL-2.0-or-later
// Behavior compatible with KERNEL32.DLL of the pinned Windows 98
// installation, SHA-256
// 9c8d574f84bfbbb6521dac54d0bfaf7a4c8fee07e468525cdb9da7db418e6c7e, base. No original binary bytes are included here. See
// ConvertThreadToFiber: 0xE0-byte block from the process heap,
//             seeded from the TIB (exception list, stack base/limit, stack
// object at TIB+0x54); TIB+0x10 receives the block. Heap failure
// sets error 8.
// CreateFiber: a Win16-flagged process gets error 50; the block
// is allocated, a stack is created (size 0 takes the module
// default), the context is seeded with EIP at the start thunk, ESP at the stack, EAX the routine and EBX the
// parameter. Stack failure frees the block and returns 0.
// SwitchToFiber: saves EBX/EDI/ESI/EBP, the caller's ESP after its
// argument, EIP, TIB exception list and stack limit into the
// current block, then loads the target's TIB fields, EDI, ESI,
//             EBP, EBX, ESP and jumps to its EIP. The stack base is not saved
// on the way out, only loaded on the way in.
// DeleteFiber: deleting the running fiber calls ExitThread(1);
// otherwise the stack object and the block are released.
// The block is 0x14 bytes of header followed by a 0xCC-byte x86 CONTEXT, so
// the register slots below are CONTEXT offsets plus 0x14.
#pragma once
namespace Win98Fibers
{
constexpr unsigned BlockSize = 0xe0;
constexpr unsigned Data = 0, ExceptionList = 4, StackBase = 8, StackLimit = 0xc, StackObject = 0x10;
constexpr unsigned Edi = 0xb0, Esi = 0xb4, Ebx = 0xb8, Eax = 0xc4, Ebp = 0xc8, Eip = 0xcc, Esp = 0xd8;
constexpr unsigned TibExceptionList = 0, TibStackBase = 4, TibStackLimit = 8, TibFiberData = 0x10, TibStackObject = 0x54;
enum Register { RegEbx, RegEsi, RegEdi, RegEbp, RegEsp, RegEip };
// The environment supplies guest memory, the TIB, registers and the providers
// the original reached through the heap, the stack allocator and ExitThread.
// StartFrame prepares a fresh stack so that the first switch lands in the
// routine: the original's thunk is replaced by the provider's
// return gate that ends the thread with the routine's result.
template <class E> unsigned Convert(E &e, unsigned parameter)
{
    const unsigned block = e.AllocateBlock(BlockSize);
    if (!block)
    {
        e.Error(8);
        return 0;
    }
    const unsigned tib = e.Tib();
    e.Write(block + Data, parameter);
    e.Write(block + StackBase, e.Read(tib + TibStackBase));
    e.Write(block + StackLimit, e.Read(tib + TibStackLimit));
    e.Write(block + StackObject, e.Read(tib + TibStackObject));
    e.Write(block + ExceptionList, e.Read(tib + TibExceptionList));
    e.Write(tib + TibFiberData, block);
    return block;
}
template <class E> unsigned Create(E &e, unsigned stack_size, unsigned start, unsigned parameter)
{
    if (e.Win16Process())
    {
        e.Error(50);
        return 0;
    }
    const unsigned block = e.AllocateBlock(BlockSize);
    if (!block)
    {
        e.Error(8);
        return 0;
    }
    if (!stack_size) stack_size = e.DefaultStackSize();
    unsigned base = 0, limit = 0;
    const unsigned object = e.AllocateStack(stack_size, base, limit);
    if (!object)
    {
        e.FreeBlock(block);
        return 0;
    }
    e.Write(block + StackBase, base);
    e.Write(block + StackLimit, limit);
    e.Write(block + StackObject, object);
    e.Write(block + Data, parameter);
    e.Write(block + ExceptionList, 0xffffffffu);
    unsigned esp = base, eip = start;
    e.StartFrame(base, start, parameter, esp, eip);
    e.Write(block + Eip, eip);
    e.Write(block + Esp, esp);
    e.Write(block + Eax, start);
    e.Write(block + Ebx, parameter);
    return block;
}
// The caller's ESP holds its return address; the original resumes the saved
// fiber with ESP past that address and the single argument.
template <class E> void Switch(E &e, unsigned fiber, unsigned caller_sp, unsigned return_address)
{
    const unsigned tib = e.Tib(), current = e.Read(tib + TibFiberData);
    e.Write(current + Ebx, e.Reg(RegEbx));
    e.Write(current + Edi, e.Reg(RegEdi));
    e.Write(current + Esi, e.Reg(RegEsi));
    e.Write(current + Ebp, e.Reg(RegEbp));
    e.Write(current + Esp, caller_sp + 8);
    e.Write(current + Eip, return_address);
    e.Write(current + ExceptionList, e.Read(tib + TibExceptionList));
    e.Write(current + StackLimit, e.Read(tib + TibStackLimit));
    e.Write(tib + TibExceptionList, e.Read(fiber + ExceptionList));
    e.Write(tib + TibStackBase, e.Read(fiber + StackBase));
    e.Write(tib + TibStackLimit, e.Read(fiber + StackLimit));
    e.Write(tib + TibStackObject, e.Read(fiber + StackObject));
    e.Write(tib + TibFiberData, fiber);
    e.SetReg(RegEdi, e.Read(fiber + Edi));
    e.SetReg(RegEsi, e.Read(fiber + Esi));
    e.SetReg(RegEbp, e.Read(fiber + Ebp));
    e.SetReg(RegEbx, e.Read(fiber + Ebx));
    e.SetReg(RegEsp, e.Read(fiber + Esp));
    e.SetReg(RegEip, e.Read(fiber + Eip));
}
template <class E> void Delete(E &e, unsigned fiber)
{
    if (e.Read(e.Tib() + TibFiberData) == fiber)
    {
        e.ExitThread(1);
        return;
    }
    e.FreeStack(e.Read(fiber + StackObject));
    e.FreeBlock(fiber);
}
} // namespace Win98Fibers
