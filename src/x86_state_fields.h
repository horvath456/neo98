// SPDX-License-Identifier: GPL-2.0-or-later
// The state of the port, field by field and by name: the part of a saved
// state of a machine with Windows programs that is neither guest memory nor
// registers (src/x86_state.h). New
// infrastructure of this port.
// Nobody writes down which fields a structure has. The layout of every
// variable with static storage of src/ and of every type one of them
// reaches comes from the debug information of the host (tools/state_layout.py
// writes it into a table beside the program file). The writer walks the
// variables by that table, follows every pointer to what it owns, and writes
// each value under the name of its field; the stream carries the names and
// types it was written with. The reader matches by name: a field the stream
// does not have keeps the value the loading process gave it, a field the
// program no longer has is read over, a number of another width is
// converted. So a state does not hang on the build that wrote it.
// A pointer is never written as a number. It becomes what it means: an
// object of the state (and a path of field names into it), a variable, a
// symbol of the program, an address of a guest, a string of the program.
// A pointer that means none of these refuses the save, with its place.
// What a pointer field means when the table cannot tell (void *, a type of
// the host) is a rule in src/state_rules/, and tools/state_layout.py fails
// the build for one without a rule.
#pragma once
#include "x86_state.h"
#include <stddef.h>
namespace DosVm { struct Launch; }
namespace MachineState
{
// The table of this program, read by the host from the file beside it.
// False with a text when it does not belong to this program.
bool SetLayout(const void *bytes, size_t size, char *error, size_t error_bytes);
bool HaveLayout();

// What the machine code (src/x86_state_machine.cpp) tells the writer and the
// reader about memory that is not the port's own.
struct Outside
{
    void *context = nullptr;
    // A host address inside the guest memory of a processor: which one (a
    // number the machine code chooses) and the guest address.
    bool (*guest)(void *context, const void *address, unsigned &processor, unsigned &guest_address) = nullptr;
    // The other way round, once the processors of a loaded machine exist.
    void *(*host)(void *context, unsigned processor, unsigned guest_address) = nullptr;
};

// Writes every variable of the port and what it owns. `first` is the object
// the loading host has made already (its first process); it is named in the
// stream so the reader fills that one instead of making another.
bool SaveFields(Sink &sink, const void *first, const Outside &outside, char *error, size_t error_bytes);
// A pointer the machine code writes itself (the host bytes behind shared
// guest pages), in the terms of the last SaveFields.
// The CPU may join pages of neighbouring blocks into one run: only the part
// up to the end of the block the pointer lies in is written (mapped_bytes),
// and the caller repeats with the rest.
bool SavePointer(Sink &sink, const void *pointer, unsigned long long requested_bytes,
                 unsigned long long &mapped_bytes, char *error, size_t error_bytes);

struct FieldReader;
// Reads the fields into this process. The reader stays open for LoadPointer
// and for the guest addresses, which FinishFields resolves once the
// processors exist.
FieldReader *LoadFields(Source &source, void *first, const Outside &outside, char *error, size_t error_bytes,
                        bool validate_only = false);
bool ValidateFields(Source &source, char *error, size_t error_bytes, unsigned *native_depth = nullptr);
bool ValidateDosUiFields(Source &source, const DosVm::Launch &, char *error, size_t error_bytes, unsigned *native_depth = nullptr);
bool LoadPointer(FieldReader *, Source &source, void *&pointer, char *error, size_t error_bytes);
bool FinishFields(FieldReader *, char *error, size_t error_bytes);

// A region of host memory the port reserves and hands to the processors (the
// window of the session arena): a pointer into it is written as its name
// and an offset.
void Region(const char *name, void *base, unsigned long long bytes);
// Immutable data imported before loading a machine. Its identity is part of
// its name; only the declaration is saved, never its bytes or owning variable.
void ImportedRegion(const char *name, const void *base, unsigned long long bytes);
bool HaveImportedRegion(const char *name, unsigned long long bytes);

// Numbers of the last SaveFields, for the log and the notes.
struct FieldNumbers
{
    unsigned variables = 0, objects = 0, types = 0;
    unsigned long long object_bytes = 0, pointers = 0, to_objects = 0, to_variables = 0, to_symbols = 0, to_guest = 0,
                       strings = 0, suspects = 0, weak = 0;
};
FieldNumbers LastFields();
// How many pointers of the last loaded state pointed one past the end of a
// block. The writer cannot tell such a pointer from one to the start of the
// block the allocator put behind it, so each one wants a look and a rule.
unsigned EndPointers();
// The numbers among the bytes of the last SaveFields that are the address
// of the start of a block, a variable or a symbol of this process and that
// no field typed as a pointer: each place, and how many. With `all` also
// those into the middle of something, which do not count.
unsigned Suspects(void (*each)(void *context, const char *place, bool counts), void *context, bool all);
// The symbol of the program an address of its code lies in (from the layout
// table, which knows every function by name), or null.
const char *SymbolOf(unsigned long long address);
// For a check: SaveFields goes on after a refusal and keeps every one.
void CollectRefusals(bool wanted);
unsigned Refusals(void (*each)(void *context, const char *text), void *context);
// The regions (Region) with the pages the port opened in them.
bool SaveRegions(Sink &sink);
bool LoadRegions(Source &source, char *error, size_t error_bytes);
} // namespace MachineState
