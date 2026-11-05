// SPDX-License-Identifier: GPL-2.0-or-later
// WINMM's buffered file I/O, mmio, after the original.
// The whole family is 32-bit code in WINMM.DLL at; only
// the notification of an installed IO procedure goes on to MMSYSTEM. An HMMIO
// is a pointer to an MMIOINFO of 48h bytes in guest memory, behind a header of
// 10h bytes: a zero word, the tag 8, the magic 4D6D496Fh ("oImM") and the
// process identifier. Every entry recognises a handle by that magic. The
// state of an open file is nothing but the public MMIOINFO: the buffer
// pointers, the offsets, MMIO_DIRTY and MMIO_ALLOCBUF in dwFlags, and for a
// file of the DOS procedure the file handle in adwInfo[0]. An IO procedure of
// the program gets exactly this structure and may change it, so it lives in
// guest memory and every step here reads and writes it there.
// The two built-in IO procedures, DOS and MEM, are
// native gates of this process, so a program that looks one of them up with
// mmioInstallIOProcA(MMIO_FINDPROC) and calls it reaches the same code.
//
#include "x86_app.h"
#include <string.h>
namespace X86
{
namespace
{
constexpr unsigned Magic = 0x4d6d496f, Tag = 8;
constexpr unsigned FourccDos = 0x20534f44, FourccMem = 0x204d454d;
constexpr unsigned Dirty = 0x10000000, AllocateBuffer = 0x10000, DefaultBuffer = 0x2000;
constexpr unsigned MessageRead = 0, MessageWrite = 1, MessageSeek = 2, MessageOpen = 3,
                   MessageClose = 4, MessageWriteFlush = 5, MessageRename = 6;
constexpr unsigned InvalidHandle = 5, InvalidFlag = 10, InvalidParameter = 11;
constexpr unsigned FileNotFound = 0x101, OutOfMemory = 0x102, CannotClose = 0x104,
                   CannotRead = 0x105, CannotWrite = 0x106, CannotSeek = 0x107,
                   CannotExpand = 0x108, ChunkNotFound = 0x109, Unbuffered = 0x10a,
                   InvalidFile = 0x110;
// MMIOINFO field offsets.
enum : unsigned
{
    Flags = 0, IoProcCode = 4, IoProc = 8, ErrorRet = 0xc, Task = 0x10, BufferSize = 0x14,
    Buffer = 0x18, Next = 0x1c, EndRead = 0x20, EndWrite = 0x24, BufferOffset = 0x28,
    DiskOffset = 0x2c, Info0 = 0x30, Self = 0x44, InfoBytes = 0x48
};
// The IO procedure list. WINMM's data lies in the shared arena,
// so the list is one for the whole system and every node names its process.
// The two static nodes  (DOS) and  (MEM) have process zero and
// a procedure of zero here, which stands for the gate of the asking process.
struct ProcNode
{
    unsigned fcc = 0, procedure = 0, process = 0;
};
constexpr unsigned ProcCapacity = 128;
ProcNode procedure_list[ProcCapacity] = {{FourccDos, 0, 0}, {FourccMem, 0, 0}};
unsigned procedure_count = 2;
} // namespace

bool Application::MmioHandle(unsigned handle)
{
    return handle && handle >= 16 && virtual_memory.Access(handle - 16, 16, true) &&
           Read32(handle - 8) == Magic;
}
// AllocTagged : HeapAlloc with HEAP_ZERO_MEMORY over the size and the
// header, the header filled, the pointer past it returned.
unsigned Application::MmioAllocate(unsigned bytes)
{
    const unsigned block = Allocate(bytes + 16);
    if (!block)
        return 0;
    unsigned char zero[256] = {};
    for (unsigned done = 0; done < bytes + 16; done += sizeof zero)
        Store(block + done, zero, bytes + 16 - done < sizeof zero ? bytes + 16 - done : sizeof zero);
    Write32(block + 4, Tag);
    Write32(block + 8, Magic);
    Write32(block + 12, process_id);
    return block + 16;
}
// FreeTagged clears the header before it frees the block.
void Application::MmioFree(unsigned pointer)
{
    if (!pointer)
        return;
    const unsigned char zero[16] = {};
    Store(pointer - 16, zero, sizeof zero);
    Free(pointer - 16);
}
unsigned Application::MmioGate(bool memory)
{
    unsigned &gate = memory ? mmio_mem_gate : mmio_dos_gate;
    if (!gate)
        gate = NewImport("WINMM.DLL", memory ? "__NEO98_MmioMemIoProc" : "__NEO98_MmioDosIoProc", 4);
    return gate;
}
// Guest-to-guest copy for the buffer moves of mmioRead and mmioWrite.
bool Application::MmioMove(unsigned destination, unsigned source, unsigned bytes)
{
    unsigned char chunk[4096];
    while (bytes)
    {
        const unsigned n = bytes < sizeof chunk ? bytes : (unsigned)sizeof chunk;
        if (!Read(source, chunk, n) || !Store(destination, chunk, n))
            return false;
        source += n;
        destination += n;
        bytes -= n;
    }
    return true;
}
// CallProc : pIOProc((LPSTR)mi, msg, l1, l2).
unsigned Application::MmioCall(unsigned info, unsigned message, unsigned first, unsigned second)
{
    const unsigned procedure = Read32(info + IoProc);
    if (procedure && procedure == mmio_dos_gate)
        return MmioDosProcedure(info, message, first, second);
    if (procedure && procedure == mmio_mem_gate)
        return MmioMemProcedure(info, message, first, second);
    const unsigned arguments[4] = {info, message, first, second};
    return Call(procedure, arguments, 4);
}
// ProcRW : bring the disk position to the buffer position first.
unsigned Application::MmioTransfer(unsigned info, unsigned message, unsigned first, unsigned second)
{
    if (Read32(info + DiskOffset) != Read32(info + BufferOffset) &&
        MmioCall(info, MessageSeek, Read32(info + BufferOffset), 0) == ~0u)
        return ~0u;
    return MmioCall(info, message, first, second);
}
// FindProc : the node of this process first, then any node with the
// code. A built-in node answers the gate of the asking process.
unsigned Application::MmioFindProcedure(unsigned fcc, unsigned process)
{
    const ProcNode *found = nullptr;
    for (unsigned i = 0; i < procedure_count && !found; ++i)
        if (procedure_list[i].fcc == fcc && procedure_list[i].process == process)
            found = &procedure_list[i];
    for (unsigned i = 0; i < procedure_count && !found; ++i)
        if (procedure_list[i].fcc == fcc)
            found = &procedure_list[i];
    if (!found)
        return 0;
    if (found->procedure)
        return found->procedure;
    return MmioGate(found->fcc == FourccMem);
}
// ResolveIOProc.
void Application::MmioResolveProcedure(unsigned name, unsigned info)
{
    if (Read32(info + IoProc))
        return;
    if (!Read32(info + IoProcCode) && name)
    {
        const char *text = ReadString(name);
        const unsigned length = (unsigned)strlen(text);
        unsigned p = length;
        while (p > 0)
        {
            --p;
            if (text[p] == '+')
                break;
        }
        if (length && text[p] == '+')
        {
            while (p > 0 && text[p] != '.' && text[p] != ':' && text[p] != '\\')
                --p;
            if (text[p] == '.')
            {
                ++p;
                char code[5] = {};
                for (unsigned i = 0; i < 4; ++i)
                    code[i] = text[p] == '+' ? 0 : text[p++];
                const unsigned scratch = Allocate(8);
                if (scratch)
                {
                    Store(scratch, code, 5);
                    const unsigned convert[2] = {scratch, 0x10};
                    Write32(info + IoProcCode, MmioApi("mmioStringToFOURCCA", convert));
                    Free(scratch);
                }
            }
        }
    }
    if (!Read32(info + IoProcCode))
        Write32(info + IoProcCode, FourccDos);
    const unsigned task = Read32(info + Task);
    const unsigned procedure = MmioFindProcedure(Read32(info + IoProcCode), task ? task : process_id);
    // An unknown code quietly falls back to the DOS procedure.
    Write32(info + IoProc, procedure ? procedure : MmioGate(false));
}
// mmioFlush.
unsigned Application::MmioFlush(unsigned info, unsigned flags)
{
    const unsigned buffer = Read32(info + Buffer);
    if (Read32(info + IoProcCode) == FourccMem || !buffer)
        return 0;
    if (Read32(info + Flags) & Dirty)
    {
        const unsigned bytes = Read32(info + EndRead) - buffer;
        if (MmioTransfer(info, MessageWriteFlush, buffer, bytes) != bytes)
            return CannotWrite;
        Write32(info + Flags, Read32(info + Flags) & ~Dirty);
    }
    if (flags & 0x10)
    {
        Write32(info + BufferOffset, Read32(info + BufferOffset) + Read32(info + Next) - buffer);
        Write32(info + EndRead, buffer);
        Write32(info + Next, buffer);
    }
    return 0;
}
// mmioSetBuffer. The buffer comes from GlobalAlloc and grows with
// GlobalReAlloc, so a MEM file the program hands a global block can grow.
unsigned Application::MmioSetBuffer(unsigned info, unsigned pointer, unsigned bytes)
{
    const unsigned flags = Read32(info + Flags);
    if (pointer && !virtual_memory.Access(pointer, bytes, (flags & 1) != 0))
        return InvalidParameter;
    if ((flags & AllocateBuffer) && !pointer && int(bytes) > 0)
    {
        if (unsigned r = MmioFlush(info, 0))
            return r;
        unsigned used = 0, read = 0;
        for (;;)
        {
            const unsigned buffer = Read32(info + Buffer);
            used = Read32(info + Next) - buffer;
            read = Read32(info + EndRead) - buffer;
            if (bytes >= used)
                break;
            if (unsigned r = MmioFlush(info, 0x10))
                return r;
        }
        const unsigned old = Read32(info + Buffer);
        const unsigned handle = Api("GlobalHandle", &old);
        Api("GlobalUnlock", &handle);
        const unsigned grow[3] = {handle, bytes, 2};
        const unsigned moved = Api("GlobalReAlloc", grow);
        const unsigned block = moved ? Api("GlobalLock", &moved) : 0;
        if (!block)
            return OutOfMemory;
        Write32(info + BufferSize, bytes);
        Write32(info + Buffer, block);
        Write32(info + Next, block + used);
        Write32(info + EndRead, block + read);
        Write32(info + EndWrite, block + bytes);
        if (bytes < read)
            Write32(info + EndRead, block + bytes);
        return 0;
    }
    if (unsigned r = MmioFlush(info, 0x10))
        return r;
    if (Read32(info + Flags) & AllocateBuffer)
    {
        const unsigned old = Read32(info + Buffer);
        const unsigned handle = Api("GlobalHandle", &old);
        Api("GlobalUnlock", &handle);
        Api("GlobalFree", &handle);
        Write32(info + Flags, Read32(info + Flags) & ~AllocateBuffer);
    }
    unsigned error = 0;
    if (!pointer && int(bytes) > 0)
    {
        const unsigned request[2] = {2, bytes};
        const unsigned handle = Api("GlobalAlloc", request);
        pointer = handle ? Api("GlobalLock", &handle) : 0;
        if (!pointer)
        {
            error = OutOfMemory;
            bytes = 0;
        }
        else
            Write32(info + Flags, Read32(info + Flags) | AllocateBuffer);
    }
    Write32(info + Buffer, pointer);
    Write32(info + BufferSize, bytes);
    Write32(info + EndRead, pointer);
    Write32(info + Next, pointer);
    Write32(info + EndWrite, pointer + bytes);
    return error;
}
// MemExpand. adwInfo[0] is the growth step; zero means the block is
// fixed. MMIO_ALLOCBUF is forced for the resize and the whole flag word put
// back afterwards.
unsigned Application::MmioExpand(unsigned info, unsigned needed)
{
    const unsigned step = Read32(info + Info0);
    if (!step)
        return CannotExpand;
    const unsigned grow = int(needed) < int(step) ? step : needed;
    const unsigned saved = Read32(info + Flags);
    Write32(info + Flags, saved | AllocateBuffer);
    const unsigned result = MmioSetBuffer(info, 0, Read32(info + BufferSize) + grow);
    Write32(info + Flags, saved);
    return result;
}
// mmioAdvance, the part after the handle test.
unsigned Application::MmioAdvance(unsigned info, unsigned user, unsigned flags)
{
    if (!Read32(info + Buffer))
        return Unbuffered;
    if (user)
    {
        if (!virtual_memory.Access(user, InfoBytes, true))
            return InvalidParameter;
        const unsigned set[3] = {info, user, flags};
        MmioApi("mmioSetInfo", set);
    }
    if (Read32(info + IoProcCode) == FourccMem)
    {
        if (!(flags & 1))
            return CannotRead;
        if (Read32(info + EndWrite) - Read32(info + Next) >= Read32(info + Info0))
            return CannotExpand;
        if (unsigned r = MmioExpand(info, 1))
            return r;
    }
    else
    {
        if (unsigned r = MmioFlush(info, 0x10))
            return r;
        if (!(flags & 1))
        {
            const unsigned n =
                MmioTransfer(info, MessageRead, Read32(info + Buffer), Read32(info + BufferSize));
            if (n == ~0u)
                return CannotRead;
            Write32(info + EndRead, Read32(info + EndRead) + n);
        }
    }
    if (user)
    {
        const unsigned get[3] = {info, user, 0};
        MmioApi("mmioGetInfo", get);
    }
    return 0;
}
// The DOS IO procedure. adwInfo[0] is the file handle.
unsigned Application::MmioDosProcedure(unsigned info, unsigned message, unsigned first, unsigned second)
{
    const unsigned handle = Read32(info + Info0);
    switch (message)
    {
    case MessageRead:
    case MessageWrite:
    case MessageWriteFlush:
    {
        const unsigned arguments[3] = {handle, first, second};
        const unsigned n = Api(message == MessageRead ? "_lread" : "_lwrite", arguments);
        if (n != ~0u)
            Write32(info + DiskOffset, Read32(info + DiskOffset) + n);
        return n;
    }
    case MessageSeek:
    {
        const unsigned arguments[3] = {handle, first, second};
        const unsigned r = Api("_llseek", arguments);
        if (r != ~0u)
            Write32(info + DiskOffset, r);
        return r;
    }
    case MessageClose:
    {
        if (first & 0x10)
            return 0;
        return Api("_lclose", &handle) == ~0u ? CannotClose : 0;
    }
    case MessageRename:
    {
        const unsigned arguments[2] = {first, second};
        return Api("MoveFileA", arguments) ? 0 : FileNotFound;
    }
    case MessageOpen:
        return MmioDosOpen(info, first);
    default:
        return 0;
    }
}
// MMIOM_OPEN for the DOS procedure.
unsigned Application::MmioDosOpen(unsigned info, unsigned name)
{
    const unsigned flags = Read32(info + Flags);
    const unsigned scratch = Allocate(0x200);
    if (!scratch)
        return FileNotFound;
    unsigned result = 0;
    const auto current = [&](unsigned file) {
        const unsigned seek[4] = {file, 0, 0, 1};
        Write32(info + DiskOffset, Api("SetFilePointer", seek));
    };
    if (flags & 0x20000)
    {
        // MMIO_GETTEMP. A drive letter picks the root of that drive, any
        // other name the temporary directory; the prefix is three characters.
        if (!virtual_memory.Access(name, 4, false))
            result = InvalidParameter;
        else
        {
            char head[4] = {};
            Read(name, head, 4);
            unsigned prefix_source = name;
            const unsigned directory = scratch, prefix = scratch + 0x108;
            if (head[1] == ':')
            {
                const char root[4] = {head[0], ':', '\\', 0};
                Store(directory, root, 4);
                prefix_source = name + 2;
            }
            else
            {
                const unsigned query[2] = {0x104, directory};
                if (!Api("GetTempPathA", query))
                    Store(directory, ".", 2);
            }
            char text[32] = {};
            const char *source = ReadString(prefix_source);
            strncpy(text, source, sizeof text - 1);
            text[3] = 0;
            Store(prefix, text, 4);
            const unsigned make[4] = {directory, prefix, Read32(info + Info0) & 0xffff, name};
            result = Api("GetTempFileNameA", make) ? 0 : FileNotFound;
        }
    }
    else if (!name)
        current(Read32(info + Info0));
    else if (flags & 0x200)
        result = Api("DeleteFileA", &name) ? 0 : FileNotFound;
    else if ((flags & 0x4000) && !(flags & 0x1000))
        result = Api("GetFileAttributesA", &name) != ~0u ? 0 : FileNotFound;
    else if (flags & 0x100)
    {
        const unsigned full[4] = {name, 0x104, scratch, scratch + 0x108};
        if (!Api("GetFullPathNameA", full))
            result = FileNotFound;
        else
            Store(name, ReadString(scratch), (unsigned)strlen(ReadString(scratch)) + 1);
    }
    else
    {
        unsigned access = (flags & 1) ? 0x40000000u : 0x80000000u;
        if (flags & 2)
            access |= 0xc0000000u;
        unsigned share = 3;
        switch (flags & 0x70)
        {
        case 0x10: share = 0; break;
        case 0x20: share = 1; break;
        case 0x30: share = 2; break;
        default: break;
        }
        unsigned path = name, disposition = 2;
        if (!(flags & 0x1000))
        {
            disposition = 3;
            const unsigned search[6] = {0, name, 0, 0x103, scratch, scratch + 0x108};
            if (!Api("SearchPathA", search))
                result = FileNotFound;
            path = scratch;
        }
        if (!result)
        {
            const unsigned create[7] = {path, access, share, 0, disposition, 0x08000080, 0};
            const unsigned file = Api("CreateFileA", create);
            if (file == ~0u)
                result = FileNotFound;
            else
            {
                Write32(info + Info0, file);
                if (flags & 0x4000)
                    Api("CloseHandle", &file);
                else
                    current(file);
            }
        }
    }
    Free(scratch);
    return result;
}
// The MEM IO procedure. The buffer layer does the work itself.
unsigned Application::MmioMemProcedure(unsigned info, unsigned message, unsigned, unsigned)
{
    if (message <= MessageSeek || message == MessageWriteFlush)
        return ~0u;
    if (message == MessageOpen)
    {
        const unsigned flags = Read32(info + Flags);
        if (flags & 0xfffeef8cu)
            return InvalidFlag;
        if (!(flags & 0x1000))
            Write32(info + EndRead, Read32(info + EndWrite));
    }
    return 0;
}
unsigned Application::MmioApi(ApiName name, const unsigned *a)
{
    switch (name.id)
    {
    // The two built-in IO procedures reached through their gates.
    API_CASE("__NEO98_MmioDosIoProc")
        return MmioDosProcedure(a[0], a[1], a[2], a[3]);
    API_CASE("__NEO98_MmioMemIoProc")
        return MmioMemProcedure(a[0], a[1], a[2], a[3]);
        // IsBadStringPtrA first; then four bytes, a space for every
    // place past the end, CharUpperA with MMIO_TOUPPER. The original hands
    // CharUpperA the sign-extended character, so a byte from 80h up is read as
    // a pointer there; this port passes the byte.
    API_CASE("mmioStringToFOURCCA")
    {
        if (!a[0])
            return 0;
        unsigned value = 0;
        for (unsigned i = 0, at = a[0]; i < 4; ++i)
        {
            unsigned char ch = 0x20;
            if (at)
            {
                if (!Read(at, &ch, 1))
                {
                    last_error = 87;
                    return 0;
                }
                if (!ch)
                {
                    ch = 0x20;
                    at = 0;
                }
                else
                {
                    if (a[1] & 0x10)
                    {
                        const unsigned upper[] = {ch};
                        ch = (unsigned char)Api("CharUpperA", upper);
                    }
                    ++at;
                }
            }
            value |= unsigned(ch) << (8 * i);
        }
        return value;
    }

    API_CASE("mmioOpenA")
    {
        const unsigned user = a[1], flags = a[2];
        if (flags & 0xfffc0000u)
            return 0;
        if (user)
        {
            if (!virtual_memory.Access(user, InfoBytes, true))
                return 0;
            Write32(user + ErrorRet, 0);
            const unsigned procedure = Read32(user + IoProc);
            if (procedure && !virtual_memory.Access(procedure, 1, false))
                return 0;
        }
        const unsigned info = MmioAllocate(InfoBytes);
        if (!info)
        {
            if (user)
                Write32(user + ErrorRet, OutOfMemory);
            return 0;
        }
        if (user)
            MmioMove(info, user, InfoBytes);
        Write32(info + Flags, flags);
        Write32(info + Self, info);
        if (flags & AllocateBuffer)
        {
            if (!Read32(info + BufferSize))
                Write32(info + BufferSize, DefaultBuffer);
            Write32(info + Flags, Read32(info + Flags) & ~AllocateBuffer);
        }
        MmioResolveProcedure(a[0], info);
        const unsigned pointer = Read32(info + Buffer);
        Write32(info + Buffer, 0);
        unsigned r = MmioSetBuffer(info, pointer, Read32(info + BufferSize));
        if (!r)
            r = MmioCall(info, MessageOpen, a[0], 0);
        if (r)
        {
            // The original leaks the buffer when the open fails; so does this.
            if (user)
                Write32(user + ErrorRet, r);
            MmioFree(info);
            return 0;
        }
        if (Read32(info + Flags) & 0x24300)
        {
            MmioSetBuffer(info, 0, 0);
            MmioFree(info);
            return 1;
        }
        Write32(info + BufferOffset, Read32(info + DiskOffset));
        return info;
    }
    // dwFlags is not looked at, neither are the two names.
    API_CASE("mmioRenameA")
    {
        const unsigned info = Allocate(InfoBytes);
        if (!info)
            return OutOfMemory;
        unsigned char zero[InfoBytes] = {};
        Store(info, zero, InfoBytes);
        if (a[2])
        {
            const unsigned procedure =
                virtual_memory.Access(a[2], InfoBytes, false) ? Read32(a[2] + IoProc) : 0;
            if (!virtual_memory.Access(a[2], InfoBytes, false) ||
                (procedure && !virtual_memory.Access(procedure, 1, false)))
            {
                Free(info);
                return InvalidParameter;
            }
            MmioMove(info, a[2], InfoBytes);
        }
        MmioResolveProcedure(a[0], info);
        const unsigned result = MmioCall(info, MessageRename, a[0], a[1]);
        Free(info);
        return result;
    }
        API_CASE("mmioInstallIOProcA")
    {
        const unsigned fcc = a[0], procedure = a[1], flags = a[2];
        if ((flags & 0xeff8ffffu) || !fcc)
            return 0;
        if (flags & 0x10000)
        {
            if (!procedure || !virtual_memory.Access(procedure, 1, false))
                return 0;
            if (procedure_count == ProcCapacity)
            {
                Fail("mmioInstallIOProcA: the IO procedure list of this port is full");
                return 0;
            }
            for (unsigned i = procedure_count; i > 0; --i)
                procedure_list[i] = procedure_list[i - 1];
            procedure_list[0] = {fcc, procedure, process_id};
            ++procedure_count;
            return procedure;
        }
        if (procedure)
            return 0;
        if (flags & 0x20000)
        {
            // RemoveProc : the node of this process only, so the two
            // static nodes can never go.
            for (unsigned i = 0; i < procedure_count; ++i)
                if (procedure_list[i].fcc == fcc && procedure_list[i].process == process_id)
                {
                    const unsigned removed = procedure_list[i].procedure;
                    for (unsigned j = i; j + 1 < procedure_count; ++j)
                        procedure_list[j] = procedure_list[j + 1];
                    procedure_list[--procedure_count] = {};
                    return removed;
                }
            return 0;
        }
        if (flags & 0x40000)
            return MmioFindProcedure(fcc, process_id);
        return 0;
    }
    default:
        break;
    }
    // Every other entry takes an HMMIO first.
    const unsigned info = a[0];
    const bool valid = MmioHandle(info);
    switch (name.id)
    {

    API_CASE("mmioClose")
    {
        if (!valid)
            return InvalidHandle;
        if (unsigned r = MmioFlush(info, 0))
            return r;
        if (unsigned r = MmioCall(info, MessageClose, a[1], 0))
            return r;
        MmioSetBuffer(info, 0, 0);
        MmioFree(info);
        return 0;
    }

    API_CASE("mmioRead")
    {
        unsigned target = a[1];
        int count = int(a[2]);
        if (!valid || (count && !virtual_memory.Access(target, unsigned(count), true)))
            return ~0u;
        unsigned total = 0;
        for (;;)
        {
            int n = int(Read32(info + EndRead) - Read32(info + Next));
            if (n > count)
                n = count;
            if (n > 0)
            {
                MmioMove(target, Read32(info + Next), unsigned(n));
                target += n;
                count -= n;
                Write32(info + Next, Read32(info + Next) + n);
                total += n;
            }
            if (Read32(info + IoProcCode) == FourccMem || !count)
                return total;
            if (int(Read32(info + BufferSize)) < count)
            {
                if (MmioFlush(info, 0x10))
                    return ~0u;
                const unsigned got = MmioTransfer(info, MessageRead, target, unsigned(count));
                Write32(info + BufferOffset, Read32(info + DiskOffset));
                return got == ~0u ? ~0u : total + got;
            }
            if (MmioAdvance(info, 0, 0))
                return ~0u;
            if (Read32(info + Next) == Read32(info + EndRead))
                return total;
        }
    }

    API_CASE("mmioWrite")
    {
        unsigned source = a[1];
        int count = int(a[2]);
        if (!valid || (count && !virtual_memory.Access(source, unsigned(count), false)))
            return ~0u;
        unsigned total = 0;
        for (;;)
        {
            int n = int(Read32(info + EndWrite) - Read32(info + Next));
            if (n < count && Read32(info + IoProcCode) == FourccMem)
            {
                if (MmioExpand(info, unsigned(count - n)))
                    return ~0u;
                n = int(Read32(info + EndWrite) - Read32(info + Next));
            }
            if (n > count)
                n = count;
            if (n > 0)
            {
                MmioMove(Read32(info + Next), source, unsigned(n));
                source += n;
                count -= n;
                Write32(info + Next, Read32(info + Next) + n);
                total += n;
                Write32(info + Flags, Read32(info + Flags) | Dirty);
            }
            if (Read32(info + EndRead) < Read32(info + Next))
                Write32(info + EndRead, Read32(info + Next));
            if (!count)
                return total;
            if (int(Read32(info + BufferSize)) < count)
            {
                if (MmioFlush(info, 0x10))
                    return ~0u;
                const unsigned put = MmioTransfer(info, MessageWrite, source, unsigned(count));
                Write32(info + BufferOffset, Read32(info + DiskOffset));
                return put == ~0u ? ~0u : total + put;
            }
            if (MmioAdvance(info, 0, 1))
                return ~0u;
        }
    }
    // SEEK_END subtracts lOffset from the end, as the original does.
    API_CASE("mmioSeek")
    {
        if (!valid)
            return ~0u;
        const unsigned buffer = Read32(info + Buffer);
        const int delta = int(Read32(info + BufferOffset) - buffer);
        const int now = int(Read32(info + Next)) + delta, end = int(Read32(info + EndRead)) + delta;
        const int offset = int(a[1]);
        int position = 0;
        switch (a[2])
        {
        case 0: position = offset; break;
        case 1: position = now + offset; break;
        case 2:
        {
            int size = end;
            if (Read32(info + IoProcCode) != FourccMem)
            {
                size = int(MmioCall(info, MessageSeek, 0, 2));
                if (size == -1)
                    return ~0u;
                if (size < end)
                    size = end;
            }
            position = size - offset;
            break;
        }
        default: position = 0; break;
        }
        if (position >= int(Read32(info + BufferOffset)) && position <= end)
        {
            Write32(info + Next, buffer + unsigned(position - int(Read32(info + BufferOffset))));
            return unsigned(position);
        }
        if (Read32(info + IoProcCode) == FourccMem)
            return ~0u;
        if (MmioFlush(info, 0))
            return ~0u;
        Write32(info + BufferOffset, unsigned(position));
        Write32(info + EndRead, buffer);
        Write32(info + Next, buffer);
        return unsigned(position);
    }
    // the whole MMIOINFO, private flags included.
    API_CASE("mmioGetInfo")
    {
        if (!valid)
            return InvalidHandle;
        if (!virtual_memory.Access(a[1], InfoBytes, true))
            return InvalidParameter;
        MmioMove(a[1], info, InfoBytes);
        return 0;
    }

    API_CASE("mmioSetInfo")
    {
        if (!valid)
            return InvalidHandle;
        if (!virtual_memory.Access(a[1], InfoBytes, false))
            return InvalidParameter;
        const unsigned pointer = Read32(a[1] + Buffer);
        if (pointer && !virtual_memory.Access(pointer, Read32(a[1] + BufferSize), (a[2] & 1) != 0))
            return InvalidParameter;
        const unsigned procedure = Read32(a[1] + IoProc);
        if (!procedure || !virtual_memory.Access(procedure, 1, false))
            return InvalidParameter;
        MmioMove(info, a[1], InfoBytes);
        if (Read32(info + EndRead) < Read32(info + Next))
            Write32(info + EndRead, Read32(info + Next));
        return 0;
    }
    // the fourth argument is not looked at.
    API_CASE("mmioSetBuffer")
        return valid ? MmioSetBuffer(info, a[1], a[2]) : InvalidHandle;
    API_CASE("mmioFlush")
        return valid ? MmioFlush(info, a[1]) : InvalidHandle;
    API_CASE("mmioAdvance")
        return valid ? MmioAdvance(info, a[1], a[2]) : InvalidHandle;
    // zero for a bad handle, no range check, no seek.
    API_CASE("mmioSendMessage")
        return valid ? MmioCall(info, a[1], a[2], a[3]) : 0;
        // No handle test of its own; mmioRead has one.
    API_CASE("mmioDescend")
    {
        const unsigned chunk = a[1], parent = a[2], flags = a[3];
        if (parent && int(Read32(parent + 4)) < 0)
            return InvalidParameter;
        unsigned id = 0, type = 0;
        if (flags & 0x10)
            id = Read32(chunk);
        else if (flags & 0x20)
        {
            id = 0x46464952;
            type = Read32(chunk + 8);
        }
        else if (flags & 0x40)
        {
            id = 0x5453494c;
            type = Read32(chunk + 8);
        }
        Write32(chunk + 0x10, 0);
        for (;;)
        {
            const unsigned read8[3] = {info, chunk, 8};
            if (MmioApi("mmioRead", read8) != 8)
                return ChunkNotFound;
            const unsigned where[3] = {info, 0, 1};
            const unsigned position = MmioApi("mmioSeek", where);
            Write32(chunk + 0xc, position);
            if (position == ~0u)
                return CannotSeek;
            if (int(Read32(chunk + 4)) < 0)
                return InvalidFile;
            if (parent && Read32(parent + 4) + Read32(parent + 0xc) <= position - 8)
                return ChunkNotFound;
            const unsigned ckid = Read32(chunk);
            if (ckid == 0x46464952 || ckid == 0x5453494c)
            {
                const unsigned read4[3] = {info, chunk + 8, 4};
                if (MmioApi("mmioRead", read4) != 4)
                    return ChunkNotFound;
            }
            else
                Write32(chunk + 8, 0);
            if ((!id || ckid == id) && (!type || Read32(chunk + 8) == type))
                return 0;
            const unsigned up[3] = {info, chunk, 0};
            if (unsigned r = MmioApi("mmioAscend", up))
                return r;
        }
    }
    // A written chunk gets its size, padded with a zero byte that
    // the size does not count; MMIO_DIRTY stays set.
    API_CASE("mmioAscend")
    {
        const unsigned chunk = a[1];
        if (int(Read32(chunk + 4)) < 0)
            return InvalidParameter;
        if (Read32(chunk + 0x10) & Dirty)
        {
            const unsigned where[3] = {info, 0, 1};
            const unsigned position = MmioApi("mmioSeek", where);
            if (position == ~0u)
                return CannotSeek;
            const int size = int(position - Read32(chunk + 0xc));
            if (size < 0)
                return CannotWrite;
            if (size & 1)
            {
                const unsigned pad = Allocate(4);
                if (pad)
                    Write32(pad, 0);
                const unsigned write[3] = {info, pad, 1};
                const unsigned put = pad ? MmioApi("mmioWrite", write) : 0;
                Free(pad);
                if (put != 1)
                    return CannotWrite;
            }
            if (Read32(chunk + 4) == unsigned(size))
                return 0;
            Write32(chunk + 4, unsigned(size));
            const unsigned back[3] = {info, Read32(chunk + 0xc) - 4, 0};
            if (MmioApi("mmioSeek", back) == ~0u)
                return CannotSeek;
            const unsigned write[3] = {info, chunk + 4, 4};
            if (MmioApi("mmioWrite", write) != 4)
                return CannotWrite;
        }
        const unsigned size = Read32(chunk + 4);
        const unsigned end[3] = {info, Read32(chunk + 0xc) + size + (size & 1), 0};
        return MmioApi("mmioSeek", end) == ~0u ? CannotSeek : 0;
    }
    // dwDataOffset is the position plus 8 even for RIFF and LIST.
    API_CASE("mmioCreateChunk")
    {
        const unsigned chunk = a[1], flags = a[2];
        const unsigned where[3] = {info, 0, 1};
        const unsigned position = MmioApi("mmioSeek", where);
        if (position == ~0u)
            return CannotSeek;
        Write32(chunk + 0xc, position + 8);
        unsigned bytes = 8;
        if (flags & 0x20)
        {
            Write32(chunk, 0x46464952);
            bytes = 12;
        }
        else if (flags & 0x40)
        {
            Write32(chunk, 0x5453494c);
            bytes = 12;
        }
        const unsigned write[3] = {info, chunk, bytes};
        if (MmioApi("mmioWrite", write) != bytes)
            return CannotWrite;
        Write32(chunk + 0x10, Dirty);
        return 0;
    }
    default:
        break;
    }
    Fail(name);
    return 0;
}
} // namespace X86
