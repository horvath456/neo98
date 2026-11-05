// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "opengl_extensions.h"
#include "win98_opengl_batch.h"
namespace OpenGLBatch
{
constexpr unsigned FlushEntry = 514, Capacity = 512, HeaderBytes = 16;
constexpr unsigned ExtraSlots[] = {OpenGL::LockArraysEntry, OpenGL::UnlockArraysEntry,
                                   OpenGL::ActiveTextureEntry, OpenGL::ClientActiveTextureEntry};
constexpr unsigned Slots = Win98OpenGL::DispatchCount + 4;
constexpr Win98OpenGL::DispatchEntry ExtraEntries[] = {
    {"glLockArraysEXT", 8}, {"glUnlockArraysEXT", 0},
    {"glActiveTextureARB", 4}, {"glClientActiveTextureARB", 4}
};
constexpr unsigned Slot(unsigned index)
{
    return index < Win98OpenGL::DispatchCount ? index : ExtraSlots[index - Win98OpenGL::DispatchCount];
}
constexpr unsigned Index(unsigned slot)
{
    if (slot < Win98OpenGL::DispatchCount) return slot;
    for (unsigned i = 0; i < 4; ++i)
        if (ExtraSlots[i] == slot) return Win98OpenGL::DispatchCount + i;
    return ~0u;
}
constexpr const Win98OpenGL::DispatchEntry *Entry(unsigned slot)
{
    const unsigned index = Index(slot);
    return index < Win98OpenGL::DispatchCount ? &Win98OpenGL::Dispatch[index]
         : index < Slots ? &ExtraEntries[index - Win98OpenGL::DispatchCount] : nullptr;
}
struct Command { unsigned slot, args[4]; };
static_assert(sizeof(Command) == 20);
constexpr bool Eligible(unsigned slot)
{
    using S = Win98OpenGL::Slot;
    switch (slot)
    {
    case OpenGL::ActiveTextureEntry: case OpenGL::ClientActiveTextureEntry:
    case OpenGL::LockArraysEntry: case OpenGL::UnlockArraysEntry:
    case S::Enable: case S::Disable: case S::EnableClientState: case S::DisableClientState:
    case S::BindTexture: case S::VertexPointer: case S::ColorPointer: case S::TexCoordPointer:
    case S::NormalPointer: case S::EdgeFlagPointer: case S::IndexPointer:
    case S::AlphaFunc: case S::BlendFunc: case S::DepthFunc: case S::DepthMask:
    case S::DepthRange: case S::ColorMask: case S::CullFace: case S::FrontFace:
    case S::ShadeModel: case S::PolygonMode: case S::PolygonOffset: case S::Viewport:
    case S::Scissor: case S::MatrixMode: case S::LoadIdentity: case S::Translatef:
    case S::Rotatef: case S::Scalef: case S::Color3ub: case S::Color4ub:
    case S::Color3f: case S::Color4f: case S::TexEnvf: case S::TexEnvi:
    case S::TexParameterf: case S::TexParameteri: case S::DrawBuffer: case S::ReadBuffer:
    case S::ClearColor: case S::ClearDepth: case S::ClearStencil:
    case S::StencilFunc: case S::StencilMask: case S::StencilOp:
        return true;
    default: return false;
    }
}
constexpr bool ArgumentsFit()
{
    for (unsigned i = 0; i < Slots; ++i)
        if (Index(Slot(i)) != i || (Eligible(Slot(i)) && Entry(Slot(i))->argument_bytes > 16)) return false;
    return true;
}
static_assert(ArgumentsFit(), "Batch records hold four argument words");
}
