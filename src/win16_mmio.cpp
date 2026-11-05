// SPDX-License-Identifier: GPL-2.0-or-later
// The mmio entries of the 16-bit MMSYSTEM (ordinals 1210 to 1224) over the
// mmio of WINMM this port already has (src/x86_mmio.cpp). MCIAVI.DRV reads
// its AVI file through them. MMSYSTEM is ours by the DLL rule; its 16-bit
// shapes differ from the 32-bit ones only in MMIOINFO (words where WINMM has
// UINTs and HANDLEs, far pointers) and in the handle, which is a word here.
// MMCKINFO has the same five dwords on both sides.
// MMIOINFO of the 16-bit side, 42h bytes: dwFlags, fccIOProc, pIOProc (far),
// wErrorRet (word), htask (word), cchBuffer, pchBuffer, pchNext, pchEndRead,
// pchEndWrite (huge far pointers), lBufOffset, lDiskOffset, adwInfo[3],
// dwReserved1, dwReserved2, hmmio (word).
#include "win16_machine.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
constexpr unsigned InvalidHandle = 5, InvalidParameter = 11;
} // namespace

unsigned Application::Win16MmioNative(unsigned handle)
{
    return handle && handle <= sizeof win16_mmio / sizeof win16_mmio[0] ? win16_mmio[handle - 1] : 0;
}

bool Application::Win16MmioCall(unsigned ordinal, const Win16Frame &frame, unsigned &result, unsigned &words)
{
    result = 0;
    switch (ordinal)
    {
    case 1210: // mmioOpen(szFilename, lpmmioinfo, dwOpenFlags)
    {
        words = 6;
        const unsigned name = frame.Pointer(4), info16 = frame.Pointer(2), flags = frame.Long(0);
        unsigned slot = 0;
        for (unsigned i = 0; i < sizeof win16_mmio / sizeof win16_mmio[0]; ++i)
            if (!win16_mmio[i])
            {
                slot = i + 1;
                break;
            }
        if (!slot)
        {
            Fail("The 16-bit mmio handle table is full (32 files)");
            return false;
        }
        unsigned info = 0;
        if (info16)
        {
            // An IO procedure of the 16-bit caller would have to be called with
            // the 16-bit MMIOINFO; that is not built.
            if (Read32(info16 + 8))
            {
                Fail("mmioOpen of the 16-bit side with an IO procedure of the caller is not built");
                return false;
            }
            info = Allocate(0x48);
            if (!info)
                return true;
            unsigned char zero[0x48] = {};
            Store(info, zero, sizeof zero);
            Write32(info + 0x00, Read32(info16 + 0x00));             // dwFlags
            Write32(info + 0x04, Read32(info16 + 0x04));             // fccIOProc
            Write32(info + 0x10, Win16Word(info16 + 0x0e));          // htask
            Write32(info + 0x14, Read32(info16 + 0x10));             // cchBuffer
            const unsigned buffer = Read32(info16 + 0x14);
            Write32(info + 0x18, buffer >> 16 ? selectors.Linear(buffer) : 0); // pchBuffer
            for (unsigned k = 0; k < 3; ++k)
                Write32(info + 0x30 + k * 4, Read32(info16 + 0x30 + k * 4)); // adwInfo
        }
        const unsigned open[] = {name, info, flags};
        const unsigned native = Api("mmioOpenA", open);
        if (info16)
        {
            Win16Word(info16 + 0x0c, Read32(info + 0x0c) & 0xffff); // wErrorRet
            Free(info);
        }
        // MMIO_PARSE and MMIO_EXIST answer TRUE instead of a handle.
        if (flags & 0x4100)
        {
            result = native;
            return true;
        }
        if (!native)
            return true;
        win16_mmio[slot - 1] = native;
        if (info16)
            Win16Word(info16 + 0x40, slot);
        result = slot;
        return true;
    }
    case 1211: // mmioClose(hmmio, wFlags)
    {
        words = 2;
        const unsigned handle = frame.Word(1), native = Win16MmioNative(handle);
        if (!native)
        {
            result = InvalidHandle;
            return true;
        }
        const unsigned close[] = {native, frame.Word(0)};
        result = Api("mmioClose", close);
        win16_mmio[handle - 1] = 0;
        return true;
    }
    case 1212: // mmioRead(hmmio, pch, cch)
    case 1213: // mmioWrite(hmmio, pch, cch)
    {
        words = 5;
        const unsigned native = Win16MmioNative(frame.Word(4));
        if (!native)
        {
            result = ~0u;
            return true;
        }
        const unsigned far = frame.Long(2);
        const unsigned io[] = {native, far >> 16 ? selectors.Linear(far) : 0, frame.Long(0)};
        result = Api(ordinal == 1212 ? "mmioRead" : "mmioWrite", io);
        return true;
    }
    case 1214: // mmioSeek(hmmio, lOffset, iOrigin)
    {
        words = 4;
        const unsigned native = Win16MmioNative(frame.Word(3));
        if (!native)
        {
            result = ~0u;
            return true;
        }
        const unsigned seek[] = {native, frame.Long(1), (unsigned)(int)(short)frame.Word(0)};
        result = Api("mmioSeek", seek);
        return true;
    }
    case 1223: // mmioDescend(hmmio, lpck, lpckParent, wFlags)
    {
        words = 6;
        const unsigned native = Win16MmioNative(frame.Word(5));
        if (!native)
        {
            result = InvalidHandle;
            return true;
        }
        const unsigned descend[] = {native, frame.Pointer(3), frame.Pointer(1), frame.Word(0)};
        result = Api("mmioDescend", descend);
        return true;
    }
    case 1224: // mmioAscend(hmmio, lpck, wFlags)
    {
        words = 4;
        const unsigned native = Win16MmioNative(frame.Word(3));
        if (!native)
        {
            result = InvalidHandle;
            return true;
        }
        const unsigned ascend[] = {native, frame.Pointer(1), frame.Word(0)};
        result = Api("mmioAscend", ascend);
        return true;
    }
    default:
        break;
    }
    (void)InvalidParameter;
    return false;
}
} // namespace X86
