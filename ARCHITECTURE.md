# How Neo98 works

A Windows 98 program needs three things: its own code in memory, the Windows
system around it, and a PC underneath. Neo98 provides all three on a Mac,
without booting Windows. This page explains the main parts and where they
live in the source.

## Loading a program

Neo98 reads the program file itself. 32-bit programs are PE files
(`src/pe32.*`), 16-bit programs are NE files (`src/ne_loader.*`). The loader
maps the program and its DLLs into a 4 GB address space laid out like
Windows 98's, applies relocations and connects the imports. Resources such as
dialogs, menus and icons are read from the same files.

## Running x86 code

On the Mac, FEX translates the program's x86 code to ARM64 in blocks and
caches the result (`tools/host_fex_cpu.cpp`). Unicorn, which is based on
QEMU, runs 16-bit code and serves as a second backend for 32-bit code.
On Linux x64 the code runs directly on the processor. All of them sit behind
one interface, `X86::Cpu` in `src/x86_cpu.h`, so the rest of Neo98 doesn't
care which one runs.

When a program calls a Windows function that Neo98 implements, execution
leaves the translated code and continues in C++. Guest addresses are always
32 bits wide, and no Windows handle ever exposes a pointer of the host.

## Which parts of Windows are Neo98's own

`src/dll_routes.h` decides this for every DLL. Neo98 has its own
implementation of KERNEL32, USER32, GDI32, ADVAPI32 and WINMM, of the 16-bit
system modules they share their state with (KERNEL, USER and GDI), and of
everything that talks to hardware on a real PC: the display driver, the
DirectDraw and DirectSound drivers, and the OpenGL and Glide drivers.

Every other DLL comes from your Windows installation and runs as ordinary x86
code, just like the program itself: the shell, the common controls and
dialogs, the C runtime, the user-mode parts of DirectX and so on. That's why
Explorer and the file dialogs look and behave exactly like on Windows 98.

Neo98 also ships no Windows data. Messages, icons, cursors, menus, code
pages and similar tables are read from `C:\WINDOWS` when Neo98 starts
(`tools/host_windows_data.h`). If a file is missing, Neo98 stops with a
message that says so.

## Processes and threads

Each Windows process gets its own address space, modules, handles and
threads. Windows threads run as fibers on a single host thread and take turns
at waits and messages (`src/fiber*`, `src/x86_threads.cpp`). This keeps the
order of events as Windows 98 programs expect it. 16-bit programs share one
task, as they did on Windows 98.

## Graphics, sound and input

Everything a program draws ends up in Neo98's display driver. 2D drawing is
done in software, the way Windows 98's own display drivers did it. Direct3D,
OpenGL and Glide go to the GPU: on the Mac through Metal
(`tools/host_metal.mm`), with Direct3D shaders translated to the Metal
shading language (`src/d3d_shader_msl.cpp`). Sound from all programs is mixed
in `src/sound*` and played through SDL. SDL also provides the window and
input; the Mac menu bar is native (`tools/host_menu.mm`).

## Drives, registry and machines

Drive C: and any other drive is a normal folder on the host. The registry
starts from the hives in `C:\WINDOWS`, and Neo98 writes changes into a
journal file next to the drive instead of rewriting the hives.

A game machine (`tools/host_game_machine.h`) puts a private write layer over
drive C:. The game sees the normal drive, but everything it writes stays in
the game machine. Save states store a machine's memory and execution state
so a game can continue exactly where it stopped.

DOS programs run in DOSBox Pure, which Neo98 drives through its libretro
interface (`tools/host_dos_vm.cpp`). Their settings live in the usual PIF
property pages.

Programs compiled for Windows on ARM run directly on the Mac's processor,
right next to x86 programs on the same desktop. [docs/ARM64-APPS.md](docs/ARM64-APPS.md)
explains how that works and how to build such programs.

## Finding your way around

| Folder or file | What's in it |
| --- | --- |
| `src/pe32.*`, `src/ne_loader.*` | Loading programs and DLLs |
| `src/x86_app.*`, `src/x86_modules.cpp` | Processes, modules and the dispatch of API calls |
| `src/x86_cpu*`, `tools/host_fex_cpu.cpp` | CPU backends |
| `src/user*`, `src/gdi*`, `src/dib*` | Windows, controls, painting and text |
| `src/win98_*` | Windows 98 behavior that doesn't depend on the host |
| `src/virtual_memory.*`, `src/x86_memory.cpp` | Memory reservations and page protection |
| `src/files.*`, `src/registry*` | Drives, files and the registry |
| `tools/neo98_host.cpp`, `tools/host_*` | The Mac and Linux side: window, menus, input, sound, graphics, devices |
| `tools/mac_app.py`, `tools/native_app.py` | Building the host and the app |
| `sdk/` | The SDK for native ARM64 Windows programs |
| `tools/test_*.py` | Tests |

## Tests

Most tests start a real Windows program on a private copy of drive C: and
check what it does: which windows appear, what they draw, what ends up in
files and in the registry. Others test single parts of Neo98 directly in C++.
[docs/BUILDING.md](docs/BUILDING.md) shows how to run them.
