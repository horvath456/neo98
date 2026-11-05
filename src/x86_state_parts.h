// SPDX-License-Identifier: GPL-2.0-or-later
// Between the parts of the state code: the streams (src/x86_state.cpp), the
// fields (src/x86_state_fields.cpp) and the machine
// (src/x86_state_machine.cpp). Everyone else includes src/x86_state.h.
#pragma once
#include "x86_state.h"
namespace MachineState
{
// Address space for bytes that the processors map (the session arena): a
// reservation without pages, known to the fields by its name
// (MachineState::Region), so a pointer into it is the name and an offset.
void *ReserveWindow(const char *name, unsigned long long bytes);
// How many sockets of the host the machine holds, and the same sockets
// without a host socket after a load (src/x86_wsock2.cpp).
unsigned OpenSockets();
void DropSockets();
// The UUID the linker gave this program: the layout table beside the
// program belongs to one build of it.
bool BuildIdentity(unsigned char out[16]);
// Bytes without their zero pages. GetPages fills the buffer; zero says that
// it holds nothing else.
bool PutPages(Sink &sink, const unsigned char *bytes, unsigned long long count);
bool GetPages(Source &source, unsigned char *bytes, unsigned long long count, bool &zero);
bool Put(Sink &sink, const void *bytes, size_t count);
bool Put64(Sink &sink, unsigned long long value);
bool Get(Source &source, void *bytes, size_t count);
bool Get64(Source &source, unsigned long long &value);
} // namespace MachineState
