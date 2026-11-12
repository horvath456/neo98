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
| [SDL](https://www.libsdl.org) (SDL 3 with sdl2-compat on the Mac, SDL 2.30 in the Linux package) | Window, input, sound and game controllers | zlib |
| [stb](https://github.com/nothings/stb) | TrueType text in DOS windows and Linux media pictures | MIT |
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

## Setup toolkit

To install Windows from your own ISO without installing anything, the Mac app
(`Contents/Resources/toolkit`) and the Linux package (`usr/lib/neo98/toolkit`)
carry a few separate programs. Neo98 starts them as programs; they are not
linked into Neo98 and keep their own licenses (copies in `toolkit/licenses`).
`tools/setup_toolkit.json` pins their versions and checksums.

| Project | What Neo98 uses it for | License |
| --- | --- | --- |
| [QEMU](https://www.qemu.org) `qemu-system-i386`, with [SeaBIOS](https://www.seabios.org) and the VGA BIOS | Running Windows 98 Setup once | GPL-2.0; BIOS images LGPL-3.0 and LGPL-2.1 |
| [mtools](https://www.gnu.org/software/mtools/) | Writing the FAT disk and boot floppy of Setup | GPL-3.0-or-later |
| [cabextract](https://www.cabextract.org.uk) with libmspack | Unpacking boot files from the CAB archives on your CD | GPL-3.0-or-later, libmspack LGPL-2.1 |
| [Tesseract](https://github.com/tesseract-ocr/tesseract) with [Leptonica](https://github.com/DanBloomberg/leptonica) and the English model | Reading Setup's dialogs to answer them | Apache-2.0, Leptonica BSD-2-Clause |
| [Python](https://www.python.org) ([python-build-standalone](https://github.com/astral-sh/python-build-standalone)) | Running the setup assistant | PSF-2.0 |

The libraries these programs need (GLib, pixman, and on Linux the libraries
of Tesseract) are copied from the build machine with their licenses.

## AppImage

`Neo98-x86_64.AppImage` begins with the unchanged runtime of the
[AppImage project](https://github.com/AppImage/type2-runtime) (MIT), the
small program that mounts the package and starts it. It is linked statically
with [squashfuse](https://github.com/vasi/squashfuse) (BSD-2-Clause),
[libfuse](https://github.com/libfuse/libfuse) (LGPL-2.1),
[zstd](https://github.com/facebook/zstd) (BSD-3-Clause),
[zlib](https://zlib.net) (zlib), [mimalloc](https://github.com/microsoft/mimalloc)
(MIT) and [musl](https://musl.libc.org) (MIT).
`tools/linux_app.py` pins its release, checksum and source revision; the
source code and the build recipe are in that repository at that revision.
`Neo98-linux-x64.tar.gz` is the same package without the runtime.

## Source code for the GPL parts

Unicorn, DOSBox Pure and the FreeDOS boot sector are licensed under the GPL,
like Neo98 itself. That's why every release on GitHub links to their
complete source code, at exactly the versions used for that release and
including Neo98's changes: `unicorn-source.tar.gz`,
`dosbox-pure-source.tar.gz` and `freedos-boot-source.tar.gz`. The unchanged
source archives of QEMU, mtools and cabextract from the setup toolkit are
there as well. They are published in a release of their own, "Third-party
sources", in this repository; releases built from the same versions and
changes share it. Neo98's own source code is the source archive of each
release.

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

The Wayland protocol files in `tools/host_wayland/protocols` come from the
Wayland project and are licensed under MIT.

## Microsoft software

Neo98 contains no Microsoft software. The files it needs from Windows, such
as system DLLs, fonts and icons, come from the user's own Windows 98 CD, and
Microsoft's license terms apply to them.
