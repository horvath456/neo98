# Third-party software

Neo98 builds on a lot of open source work. Thank you to everyone behind these
projects.

The exact versions, download addresses and checksums are pinned in
`fex/dependencies.json`, `unicorn/dependencies.json` and
`sdk/dependencies.json`. The license texts are in [LICENSES](LICENSES), and
the app carries a copy of them in `Neo98.app/Contents/Resources`.

## Inside the app

| Project | What Neo98 uses it for | License |
| --- | --- | --- |
| [FEX](https://github.com/FEX-Emu/FEX) | Translating x86 code to ARM64 | MIT |
| [Unicorn](https://github.com/unicorn-engine/unicorn), based on [QEMU](https://www.qemu.org) | Running 16-bit code, and as a second CPU backend | GPL-2.0, with parts under LGPL-2.1, BSD and MIT |
| [DOSBox Pure](https://github.com/schellingb/dosbox-pure) | DOS programs | GPL-2.0-or-later |
| [SDL](https://www.libsdl.org) (SDL 3 with sdl2-compat) | Window, input, sound and game controllers | zlib |
| [stb_truetype](https://github.com/nothings/stb) | TrueType text in DOS windows | MIT |
| [FreeDOS](https://github.com/FDOS/kernel) boot sector | Starting Windows Setup from CDs that can't boot on their own | GPL-2.0-or-later |

FEX brings a few libraries of its own:
[fmt](https://github.com/fmtlib/fmt) (MIT),
[range-v3](https://github.com/ericniebler/range-v3) (BSL-1.0),
[unordered_dense](https://github.com/martinus/unordered_dense) (MIT),
[VIXL](https://github.com/FEX-Emu/vixl) (BSD-3-Clause),
[xxHash](https://github.com/Cyan4973/xxHash) (BSD-2-Clause),
[SoftFloat](https://www.jhauser.us/arithmetic/SoftFloat.html) (BSD-3-Clause),
[Cephes](https://www.netlib.org/cephes/) (BSD) and
[tiny-json](https://github.com/rafagafe/tiny-json) (MIT).

Neo98 uses patched builds of Unicorn and DOSBox Pure. The patches are part of
the Neo98 source code.

Media Center is only included in builds made with `NEO98_WITH_HOST_MEDIA=1`,
not in releases. It plays music and videos through [Kodi](https://kodi.tv)
when Kodi is installed on the Mac. Kodi itself isn't part of Neo98.

## Source code for the GPL parts

Unicorn, DOSBox Pure and the FreeDOS boot sector are licensed under the GPL,
like Neo98 itself. That's why every release on GitHub comes with their
complete source code, at exactly the versions used for that release and
including Neo98's changes: `unicorn-source.tar.gz`,
`dosbox-pure-source.tar.gz` and `freedos-boot-source.tar.gz`, next to
`Neo98-source.tar.gz`.

## In the Linux version

The Linux version runs x86 code directly on the processor and contains
neither FEX nor Unicorn. For 3D graphics it adds:

| Project | What Neo98 uses it for | License |
| --- | --- | --- |
| [Vulkan loader](https://github.com/KhronosGroup/Vulkan-Loader) | Talking to the graphics driver (a system library) | Apache-2.0 |
| [shaderc](https://github.com/google/shaderc), with [glslang](https://github.com/KhronosGroup/glslang) and [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools) | Compiling shaders while a game runs | Apache-2.0, glslang also BSD and MIT |

Apache 2.0 works with the GPL version 3 but not with version 2. Since nothing
in the Linux package is limited to the GPL version 2, a Linux package with
shaderc is distributed under the GPL version 3
([LICENSES/GPL-3.0.txt](LICENSES/GPL-3.0.txt)). The Mac app contains Unicorn
and stays under version 2.

## In the SDK for ARM64 programs

Programs built with Neo98's SDK ([docs/ARM64-APPS.md](docs/ARM64-APPS.md))
get their C and C++ libraries from these projects:

| Project | What it provides | License |
| --- | --- | --- |
| [musl](https://musl.libc.org) | The C library | MIT |
| [LLVM](https://llvm.org) libc++, libc++abi, libunwind and compiler-rt | The C++ library and compiler support | Apache-2.0 with LLVM exception |
| [pl_mpeg](https://github.com/phoboslab/pl_mpeg) | MPEG video in the sample player | MIT |
| [tz database](https://www.iana.org/time-zones) | Time zones for C++ programs | Public domain |

## Only for development and tests

Build and test scripts download these, but they never end up in the app:

- Tests from [Wine](https://www.winehq.org) and [ReactOS](https://reactos.org) (LGPL and GPL), to compare Neo98 with Windows
- The SDL test suite (LGPL)
- [Circle](https://github.com/rsta2/circle) (GPL-3.0) and the Arm GNU Toolchain, for the Raspberry Pi version, which is on hold. It is never built together with Unicorn.

The Wayland protocol files in `tools/host_wayland/protocols` come from the
Wayland project and are licensed under MIT.

## Microsoft software

Neo98 contains no Microsoft software. The files it needs from Windows, such
as system DLLs, fonts and icons, come from the user's own Windows 98 CD, and
Microsoft's license terms apply to them.
