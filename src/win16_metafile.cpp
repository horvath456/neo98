// SPDX-License-Identifier: GPL-2.0-or-later
// 16-bit metafiles. On Windows 98, an HMETAFILE is a global-heap handle
// containing the metafile image. SetMetaFileBits returns that handle
// unchanged; GetMetaFileBits validates and returns it or zero. Validation
// checks mtType (1 or 2), mtHeaderSize (9), and mtVersion (0x0300 or 0x0100).
// DeleteMetaFile frees the block without validation. PlayMetaFile locks and
// reads the block, while SetMetaFileBitsBetter copies it and leaves ownership
// with the caller.
// Use the same global block as the 16-bit handle. When native playback is
// needed, import its bytes through SetMetaFileBitsEx and release the
// temporary native handle afterwards. Recording, playback and record formats
// are implemented in src/x86_gdi_metafile.cpp.
#include "win16_machine.h"
#include "win98_ne_task.h"
#include "win98_win16_user.h"
#include "x86_app.h"

namespace X86
{
namespace
{
// The three header words checked by Windows 98 metafile validation and their
// offsets in the image.
constexpr unsigned TypeWord = 0, HeaderWord = 2, VersionWord = 4;
} // namespace

// Read and validate the metafile header.
bool Application::Win16MetafileValid(unsigned handle)
{
    if (!handle || !win16.state)
        return false;
    auto *block = Win16GlobalBlock(handle);
    if (!block || !block->memory || block->size < 18)
        return false;
    const unsigned type = Win16Word(block->memory + TypeWord);
    const unsigned header = Win16Word(block->memory + HeaderWord);
    const unsigned version = Win16Word(block->memory + VersionWord);
    return (type == 1 || type == 2) && header == 9 && (version == 0x300 || version == 0x100);
}
// Create a native metafile from a global block. The caller releases it with
// Win16MetafileRelease.
unsigned Application::Win16MetafileNative(unsigned handle)
{
    if (!Win16MetafileValid(handle))
        return 0;
    auto *block = Win16GlobalBlock(handle);
    // mtSize counts words and gives the image length. GlobalAlloc rounds to
    // sixteen bytes, so the block can be larger.
    const unsigned words = Win16Word(block->memory + 6) | (Win16Word(block->memory + 8) << 16);
    unsigned bytes = words * 2;
    if (!bytes || bytes > block->size)
        bytes = block->size;
    const unsigned a[] = {bytes, block->memory};
    return Api("SetMetaFileBitsEx", a);
}
void Application::Win16MetafileRelease(unsigned native)
{
    if (!native)
        return;
    const unsigned a[] = {native};
    Api("DeleteMetaFile", a);
}
// Convert a native metafile into a 16-bit global-heap block containing its
// image. The native metafile is no longer needed afterwards.
unsigned Application::Win16MetafileWrap(unsigned native)
{
    if (!native)
        return 0;
    const unsigned measure[] = {native, 0, 0};
    const unsigned bytes = Api("GetMetaFileBitsEx", measure);
    if (!bytes)
    {
        Win16MetafileRelease(native);
        return 0;
    }
    const unsigned handle = Win16GlobalAlloc(Win98NeTask::Moveable, bytes);
    auto *block = handle ? Win16GlobalBlock(handle) : nullptr;
    if (!block || !block->memory)
    {
        if (handle)
            Win16GlobalFree(handle);
        Win16MetafileRelease(native);
        return 0;
    }
    const unsigned copy[] = {native, block->size, block->memory};
    const unsigned written = Api("GetMetaFileBitsEx", copy);
    Win16MetafileRelease(native);
    if (written != bytes)
    {
        Win16GlobalFree(handle);
        return 0;
    }
    return handle;
}
bool Application::Win16MetafileCall(unsigned ordinal, const Win16Frame &frame, unsigned &result,
                                    unsigned words)
{
    auto arg = [&](unsigned position) { return frame.Word(words - 1 - position); };
    auto pointer = [&](unsigned position) { return frame.Pointer(words - 2 - position); };
    result = 0;
    switch (ordinal)
    {
    case 125: // CreateMetaFile(name): a recording device context, GDI.
    {
        const unsigned a[] = {pointer(0)};
        result = Api("CreateMetaFileA", a);
        return true;
    }
    case 126: // CloseMetaFile(hdc), GDI.
    {
        const unsigned a[] = {arg(0)};
        result = Win16MetafileWrap(Api("CloseMetaFile", a));
        return true;
    }
    case 124: // GetMetaFile(name), GDI.
    {
        const unsigned a[] = {pointer(0)};
        result = Win16MetafileWrap(Api("GetMetaFileA", a));
        return true;
    }
    case 151: // CopyMetaFile(metafile, name), GDI.
    {
        const unsigned native = Win16MetafileNative(arg(0));
        if (!native)
            return true;
        const unsigned a[] = {native, pointer(1)};
        const unsigned copy = Api("CopyMetaFileA", a);
        Win16MetafileRelease(native);
        result = Win16MetafileWrap(copy);
        return true;
    }
    case 123: // PlayMetaFile(hdc, metafile), GDI.
    {
        const unsigned native = Win16MetafileNative(arg(1));
        if (!native)
            return true;
        const unsigned a[] = {arg(0), native};
        result = Api("PlayMetaFile", a);
        Win16MetafileRelease(native);
        return true;
    }
    case 127: // DeleteMetaFile(metafile), GDI.
    {
        const unsigned handle = arg(0);
        if (!handle)
            return true;
        Win16GlobalFree(handle);
        result = 1;
        return true;
    }
    case 159: // GetMetaFileBits(metafile), GDI.
        result = Win16MetafileValid(arg(0)) ? arg(0) : 0;
        return true;
    case 160: // SetMetaFileBits(memory), GDI: the handle is the answer.
        result = arg(0);
        return true;
    case 196: // SetMetaFileBitsBetter(memory), GDI: GDI keeps a copy.
    {
        const unsigned handle = arg(0);
        auto *block = handle ? Win16GlobalBlock(handle) : nullptr;
        if (!block || !block->memory || !block->size)
            return true;
        // Allocation can move the block table. Save the address and size
        // first.
        const unsigned source = block->memory, size = block->size;
        const unsigned copy = Win16GlobalAlloc(Win98NeTask::Moveable, size);
        auto *target = copy ? Win16GlobalBlock(copy) : nullptr;
        if (!target || !target->memory)
        {
            if (copy)
                Win16GlobalFree(copy);
            return true;
        }
        const unsigned destination = target->memory;
        auto *bytes = new unsigned char[size];
        const bool copied = Read(source, bytes, size) && Store(destination, bytes, size);
        delete[] bytes;
        if (!copied)
        {
            Win16GlobalFree(copy);
            return true;
        }
        result = copy;
        return true;
    }
    case 410: // IsValidMetaFile(metafile), GDI.
        result = Win16MetafileValid(arg(0)) ? 1 : 0;
        return true;
    default:
        return false;
    }
}
} // namespace X86
