// SPDX-License-Identifier: GPL-2.0-or-later
// The TrueType driver of Win98, run as the original x86 code of GDI32.DLL.
// Win98 rasterizes TrueType in 32-bit code of GDI32.DLL.
// GDI.EXE reaches it through the font-driver thunks FDTHKCONNECTIONDATASL
// (GDI, stubs, 32-bit target table ). This
// machine maps GDI32.DLL of the installation into a Unicorn engine of its own,
// does not run DllMain (which only connects the thunks) and serves the imports
// the driver uses from a small native provider. The native GDI calls the
// driver entries the way GDI.EXE calls them.
// The machine belongs to no process. Under Win98 GDI's TrueType data lives in
// the shared arena too. One host thread uses it at a time (the host runs its
// guests as fibers).
#pragma once
namespace TrueTypeMachine
{
using U = unsigned;
// Reads a file of the Win98 tree ("C:\\WINDOWS\\FONTS\\ARIAL.TTF"). The data is
// allocated with malloc and belongs to the caller.
using Reader = bool (*)(const char *path, unsigned char *&data, U &size);
void SetReader(Reader);
// One registry value, named the way src/registry_persist.cpp names keys
// ("HKEY_LOCAL_MACHINE\\Software\\..."). The driver reads the fonts folder and
// the Gasp lists. `size` carries the room on the way in
// and the length on the way out; false when the value does not exist.
using Query = bool (*)(const char *key, const char *name, unsigned char *data, U &size);
void SetQuery(Query);
// The locale GetSystemDefaultLCID answers inside the machine.
void SetLocale(U);
// Starts the machine on first use: maps GDI32.DLL and calls the driver's
// initialisation (entry 29). False when the tree has no GDI32.DLL,
// the JIT has no slot left or the driver failed; Failure says which.
bool Start();
bool Started();
const char *Failure();

// The driver entries (index into the table, 32-bit target).
enum Entry : U
{
    FileView = 0xbff29e30,        // 20
    FindFile = 0xbff29978,        // 21
    FixedSqrt = 0xbff29cf9,       // 22
    Vdmx = 0xbff2c94c,            // 23
    Glyph = 0xbff29f47,           // 24
    Widths = 0xbff2bdb2,          // 25
    FreeContext = 0xbff2c92e,     // 26
    SetTransform = 0xbff2c6e8,    // 27
    NewContext = 0xbff2c55a,      // 28
    Initialize = 0xbff2a9ea,      // 29
    GlyphIndices = 0xbff2adeb,    // 30
    FileHeader = 0xbff2b0eb,      // 31
    FontData = 0xbff2abf4,        // 32
    CloseFile = 0xbff2ab77,       // 33
    OpenFile = 0xbff2aac3,        // 34
};

// Calls one entry with stdcall arguments. `ok` false: the machine stopped
// (fault, missing import); the result is then zero.
U Call(U entry, const U *arguments, U count, bool &ok);
// Scratch memory of the machine's heap for arguments and results.
U Allocate(U bytes);
void Release(U address);
bool Read(U address, void *out, U bytes);
bool Write(U address, const void *in, U bytes);
U Read32(U address);
// A zero-terminated string in machine memory.
U String(const char *text);
// Saved states: the engine of the machine and its hooks on a new one
// (src/x86_state_machine.cpp).
} // namespace TrueTypeMachine
namespace X86
{
class Cpu;
}
namespace TrueTypeMachine
{
X86::Cpu *&StateCpu();
bool StateBind();
} // namespace TrueTypeMachine
