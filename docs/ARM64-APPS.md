# Native ARM64 programs

Neo98 can run Windows programs that were compiled for ARM64 instead of x86.
They run directly on the Mac's processor, without translation, and share the
desktop, windows and clipboard with the x86 programs around them. The program
file is an ordinary Windows on ARM executable: the same `.exe` also runs on
Windows 11 on ARM.

Inside Neo98, an ARM64 program runs in a small virtual machine of macOS's
Hypervisor framework that holds nothing but the program's memory. Each call
into Windows leaves that machine and lands in the same C++ implementation of
KERNEL32, USER32 and GDI32 that x86 programs use.

## Building a program

The SDK comes with the repository. It needs the Xcode Command Line Tools and
`lld` from Homebrew:

```sh
python3 neo98.py sdk-build hello.c -o hello.exe
python3 neo98.py sdk-build window.c window.rc -o window.exe --gui
```

`--gui` builds a windowed program with `WinMain`; without it you get a
console program with `main`. `.rc` files are compiled as resources, and
`--dll` builds a DLL together with its import library. The examples in
`sdk/samples` are a good place to start: a console program, a window with a
menu, a little Minesweeper in C++ and a video player.

To run the result, copy it to drive C: and open it like any other program,
or start it with `python3 neo98.py run --exe 'C:\hello.exe'`.

## What a program can use

The headers come from `sdk/api/*.api`. These files describe every function,
structure, message and callback the SDK offers, and the build generates
`windows.h` and the import libraries from them. What isn't described there
isn't available. That covers the main parts of KERNEL32, USER32, GDI32,
WINMM and ADVAPI32: files, memory, threads and synchronization, windows,
dialogs, menus, painting, text, timers, sound and the registry. DirectDraw,
Direct3D 7 and DirectSound headers are in `sdk/dx/include`.

The C library is [musl](https://musl.libc.org), linked statically. Only the
part that talks to Windows (files, memory, time, locale) is Neo98's own code,
in `sdk/crt`. A program therefore doesn't need MSVCRT.DLL, which only exists
as x86 code on Windows 98.

## Exceptions

ARM64 programs handle exceptions the way Windows on ARM does. The compiler
writes unwind information for every function into the `.pdata` and `.xdata`
sections. When an exception happens, the unwinder walks the stack with it,
calls the language handlers and unwinds to the frame that takes the
exception.

On Windows, NTDLL does this work. In Neo98 the ARM64 processor backend does
it itself (`src/arm64_seh.inc` and `src/arm64_unwind.cpp`). It provides
`RaiseException`, `RtlCaptureContext`, `RtlLookupFunctionEntry`,
`RtlVirtualUnwind`, `RtlUnwindEx`, `RtlUnwind`, `RtlRestoreContext`,
`RtlPcToFileHeader` and `SetUnhandledExceptionFilter`, declared in
`sdk/api/70-exceptions.api`. The program itself only carries what belongs to
the language: `__C_specific_handler` for C (`sdk/start/seh.c`), and libunwind
and libc++abi for C++.

## C++

Programs with C++ sources are built for the `aarch64-w64-windows-gnu` target,
the MinGW flavor of Windows, while plain C uses the MSVC flavor. The reason
is the C++ runtime: the MinGW target uses libc++, libc++abi and libunwind
from LLVM, which only need a handful of Windows functions, whereas the MSVC
target expects Microsoft's own C++ runtime. Both targets share the processor,
calling convention and unwind format, so C objects of one link with C++
objects of the other. `--target msvc` or `--target gnu` picks one by hand.
One difference you may notice: `__declspec(thread)` doesn't exist on the
MinGW target; use `thread_local` instead.

The C++ libraries are built the first time a program needs them, or ahead of
time with `python3 neo98.py sdk-build --cxx`. Their configuration is in
`sdk/cxx/__config_site`. libc++ normally builds its threads on Windows Vista
functions such as slim reader/writer locks. Windows 98 doesn't have them, so
`sdk/cxx/threads.cpp` implements the same interface with events,
`CreateThread` and thread-local storage.

## The allocator

`malloc`, `calloc`, `realloc` and `free` are musl's mallocng, unchanged.
Neo98 only supplies the layer underneath it, in
`sdk/crt/src/malloc_windows.c` and `sdk/crt/src/mman_windows.c`: memory
mappings become `VirtualAlloc` and `VirtualFree` calls, with 4 KB pages.
Windows rounds every reservation up to 64 KB, but only commits the pages
that were asked for. There is no separate bookkeeping of sub-ranges; size
classes, reuse and freeing are all left to musl.

## CMake

`sdk-build` is meant for small programs. Bigger projects that are built with
CMake get a toolchain file:

```sh
python3 neo98.py sdk-build --cmake-toolchain   # prints the path of the file
cmake -S project -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=<that file>
```

To CMake, the SDK then looks like a MinGW clang. Settings from the
project's x86 build keep working: `target_link_libraries(game winmm ddraw)`
links the import libraries, `-Wl,--subsystem,windows` makes a windowed
program, and `if(MINGW)` is true.
