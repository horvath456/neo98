// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_opengl_dispatch.h"
namespace OpenGL
{
constexpr unsigned LockArraysEntry = 512, UnlockArraysEntry = 513;
constexpr unsigned ActiveTextureEntry = 515, ClientActiveTextureEntry = 516, MultiTexCoordBase = 768;
struct Extension { const char *name; unsigned entry, words, legacy; };
constexpr Extension Extensions[] = {
    {"glLockArraysEXT", LockArraysEntry, 2, ~0u},
    {"glUnlockArraysEXT", UnlockArraysEntry, 0, ~0u},
    {"glActiveTextureARB", ActiveTextureEntry, 1, ~0u},
    {"glClientActiveTextureARB", ClientActiveTextureEntry, 1, ~0u},
    {"glMultiTexCoord1dARB", MultiTexCoordBase + 0, 3, Win98OpenGL::Slot::TexCoord1d},
    {"glMultiTexCoord1dvARB", MultiTexCoordBase + 1, 2, Win98OpenGL::Slot::TexCoord1dv},
    {"glMultiTexCoord1fARB", MultiTexCoordBase + 2, 2, Win98OpenGL::Slot::TexCoord1f},
    {"glMultiTexCoord1fvARB", MultiTexCoordBase + 3, 2, Win98OpenGL::Slot::TexCoord1fv},
    {"glMultiTexCoord1iARB", MultiTexCoordBase + 4, 2, Win98OpenGL::Slot::TexCoord1i},
    {"glMultiTexCoord1ivARB", MultiTexCoordBase + 5, 2, Win98OpenGL::Slot::TexCoord1iv},
    {"glMultiTexCoord1sARB", MultiTexCoordBase + 6, 2, Win98OpenGL::Slot::TexCoord1s},
    {"glMultiTexCoord1svARB", MultiTexCoordBase + 7, 2, Win98OpenGL::Slot::TexCoord1sv},
    {"glMultiTexCoord2dARB", MultiTexCoordBase + 8, 5, Win98OpenGL::Slot::TexCoord2d},
    {"glMultiTexCoord2dvARB", MultiTexCoordBase + 9, 2, Win98OpenGL::Slot::TexCoord2dv},
    {"glMultiTexCoord2fARB", MultiTexCoordBase + 10, 3, Win98OpenGL::Slot::TexCoord2f},
    {"glMultiTexCoord2fvARB", MultiTexCoordBase + 11, 2, Win98OpenGL::Slot::TexCoord2fv},
    {"glMultiTexCoord2iARB", MultiTexCoordBase + 12, 3, Win98OpenGL::Slot::TexCoord2i},
    {"glMultiTexCoord2ivARB", MultiTexCoordBase + 13, 2, Win98OpenGL::Slot::TexCoord2iv},
    {"glMultiTexCoord2sARB", MultiTexCoordBase + 14, 3, Win98OpenGL::Slot::TexCoord2s},
    {"glMultiTexCoord2svARB", MultiTexCoordBase + 15, 2, Win98OpenGL::Slot::TexCoord2sv},
    {"glMultiTexCoord3dARB", MultiTexCoordBase + 16, 7, Win98OpenGL::Slot::TexCoord3d},
    {"glMultiTexCoord3dvARB", MultiTexCoordBase + 17, 2, Win98OpenGL::Slot::TexCoord3dv},
    {"glMultiTexCoord3fARB", MultiTexCoordBase + 18, 4, Win98OpenGL::Slot::TexCoord3f},
    {"glMultiTexCoord3fvARB", MultiTexCoordBase + 19, 2, Win98OpenGL::Slot::TexCoord3fv},
    {"glMultiTexCoord3iARB", MultiTexCoordBase + 20, 4, Win98OpenGL::Slot::TexCoord3i},
    {"glMultiTexCoord3ivARB", MultiTexCoordBase + 21, 2, Win98OpenGL::Slot::TexCoord3iv},
    {"glMultiTexCoord3sARB", MultiTexCoordBase + 22, 4, Win98OpenGL::Slot::TexCoord3s},
    {"glMultiTexCoord3svARB", MultiTexCoordBase + 23, 2, Win98OpenGL::Slot::TexCoord3sv},
    {"glMultiTexCoord4dARB", MultiTexCoordBase + 24, 9, Win98OpenGL::Slot::TexCoord4d},
    {"glMultiTexCoord4dvARB", MultiTexCoordBase + 25, 2, Win98OpenGL::Slot::TexCoord4dv},
    {"glMultiTexCoord4fARB", MultiTexCoordBase + 26, 5, Win98OpenGL::Slot::TexCoord4f},
    {"glMultiTexCoord4fvARB", MultiTexCoordBase + 27, 2, Win98OpenGL::Slot::TexCoord4fv},
    {"glMultiTexCoord4iARB", MultiTexCoordBase + 28, 5, Win98OpenGL::Slot::TexCoord4i},
    {"glMultiTexCoord4ivARB", MultiTexCoordBase + 29, 2, Win98OpenGL::Slot::TexCoord4iv},
    {"glMultiTexCoord4sARB", MultiTexCoordBase + 30, 5, Win98OpenGL::Slot::TexCoord4s},
    {"glMultiTexCoord4svARB", MultiTexCoordBase + 31, 2, Win98OpenGL::Slot::TexCoord4sv},
};
constexpr const Extension *ExtensionFor(unsigned entry)
{
    if (entry < LockArraysEntry) return nullptr;
    for (const auto &extension : Extensions)
        if (extension.entry == entry) return &extension;
    return nullptr;
}
constexpr unsigned ArgumentBytes(unsigned entry)
{
    if (entry < Win98OpenGL::DispatchCount) return Win98OpenGL::Dispatch[entry].argument_bytes;
    const auto *extension = ExtensionFor(entry);
    return extension ? extension->words * 4 : 0;
}
}
