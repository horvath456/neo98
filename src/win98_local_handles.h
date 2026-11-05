// SPDX-License-Identifier: GPL-2.0-or-later
// LocalSize validates fixed and movable local handles under the process heap
// lock. Movable handles require a valid signature and block pointer; subtract
// their four-byte header from the allocation size. Invalid handles return
// zero and error 6. ConvertToGlobalHandle duplicates into the shared process,
// closes the source, and encodes the result with XOR 0x453a4d3c.
// Already-global handles need only that encoding.
#pragma once

namespace Win98LocalHandles
{
// bit 1 distinguishes a moveable local handle from a pointer.
constexpr unsigned MoveableTag = 2;
// the signature word of a moveable handle entry.
constexpr unsigned MoveableSignature = 0x5342;
// a moveable block reserves four bytes ahead of the data.
constexpr unsigned MoveableHeader = 4;
// every rejected handle reports ERROR_INVALID_HANDLE.
constexpr unsigned InvalidHandle = 6;
// the constant the original mixes into a global handle.
constexpr unsigned GlobalMix = 0x453a4d3c;

inline bool Moveable(unsigned handle)
{
    return (handle & MoveableTag) != 0;
}

struct SizeEnvironment
{
    virtual ~SizeEnvironment() = default;
    // Reads the guarded bytes at [handle+7] and [handle-4]; false means the
    // original would have raised its exception.
    virtual bool Readable(unsigned handle) = 0;
    // The moveable handle entry: its signature word and the block it points at.
    virtual bool Entry(unsigned handle, unsigned &signature, unsigned &block) = 0;
    // The heap block size, or false when the block is not one of ours.
    virtual bool BlockSize(unsigned pointer, unsigned &size) = 0;
    virtual void Error(unsigned code) = 0;
};

inline unsigned Size(SizeEnvironment &e, unsigned handle)
{
    if (!e.Readable(handle))
    {
        e.Error(InvalidHandle);
        return 0;
    }
    unsigned pointer = handle, extra = 0;
    if (Moveable(handle))
    {
        unsigned signature = 0, block = 0;
        if (!e.Entry(handle - MoveableTag, signature, block) || signature != MoveableSignature ||
            !block)
        {
            e.Error(InvalidHandle);
            return 0;
        }
        pointer = block - MoveableHeader;
        extra = MoveableHeader;
    }
    unsigned size = 0;
    if (!e.BlockSize(pointer, size))
    {
        e.Error(InvalidHandle);
        return 0;
    }
    return size >= extra ? size - extra : 0;
}

struct GlobalEnvironment
{
    virtual ~GlobalEnvironment() = default;
    // True when the caller already runs in the process that owns the shared
    // handle table; the original then only mixes the value.
    virtual bool SharedProcess() = 0;
    // Duplicates the object into the shared table and closes the source. Zero
    // means the original's open or duplicate step failed.
    virtual unsigned Share(unsigned handle) = 0;
    // The original always mixes. NEO98's handle layer has no step that undoes
    // the mixing, so its provider answers false and the caller keeps a handle
    // it can still use. That is the one documented deviation here.
    virtual bool Mix() = 0;
};

inline unsigned ToGlobal(GlobalEnvironment &e, unsigned handle)
{
    if (e.SharedProcess())
        return e.Mix() ? handle ^ GlobalMix : handle;
    const unsigned shared = e.Share(handle);
    if (!shared)
        return 0;
    return e.Mix() ? shared ^ GlobalMix : shared;
}
} // namespace Win98LocalHandles
