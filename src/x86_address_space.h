// SPDX-License-Identifier: GPL-2.0-or-later
// An address space without a processor (CpuBackend::None). DOS processes keep
// guest pages for shared USER, GDI, and kernel state while DOSBox Pure
// executes their programs. Contexts are empty but support scheduler save and
// restore. Loading an x86 module, such as COMCTL32 for a property sheet,
// creates a CPU and transfers the pages through CopyAddressSpace.
#pragma once
#include "x86_cpu.h"
namespace X86
{
// A new, empty address space.
unsigned CreateAddressSpace(Cpu *&cpu);
// Maps every page of `space` (a CpuBackend::None instance) into `target`
// with the same rights: the pages the space owns as new pages of the target
// with their bytes copied, the pages of caller bytes (MapHost) with the same
// host bytes. `space` is left as it was, so on failure the caller deletes
// the target and goes on with the space.
unsigned CopyAddressSpace(Cpu &space, Cpu &target);
} // namespace X86
