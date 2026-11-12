# Building Neo98

Everything goes through one script, `neo98.py`, in the top folder of the
repository. It fetches the dependencies, builds the app and runs it.

## macOS

You need a Mac with Apple Silicon, the Xcode Command Line Tools
(`xcode-select --install`) and a few packages from [Homebrew](https://brew.sh):

```sh
brew install llvm lld cmake ninja pkg-config sdl2
```

Then, from the repository folder:

```sh
python3 neo98.py app    # build/Neo98.app
python3 neo98.py dmg    # build/Neo98.dmg, the disk image for the releases
```

The first build downloads FEX, Unicorn, DOSBox Pure and a few smaller
libraries at pinned versions, checks their hashes and compiles them. That
takes a while. Later builds only recompile what changed.

Release builds leave Media Center out. Set `NEO98_WITH_HOST_MEDIA=1` to add
it to your own builds. Media Center plays music and videos through Kodi if
Kodi is installed on the Mac.

## Installing Windows without the app

The app has its setup assistant. When you work from the command line, the
same installation runs with:

```sh
brew install qemu mtools cabextract tesseract
python3 neo98.py setup --iso '/path/to/Windows 98 Second Edition.iso'
```

Setup asks for the product key without showing it, installs Windows in the
background and puts drive C: in `build/win98-c`. To use a Windows folder
somewhere else, set `NEO98_WIN98_ROOT` to it.

## Running from the command line

```sh
python3 neo98.py run                                # the Windows desktop
python3 neo98.py run --app mspaint                  # Paint
python3 neo98.py run --exe 'C:\WINDOWS\NOTEPAD.EXE' # any program by its Windows path
python3 neo98.py run --seamless                     # every program in its own Mac window
python3 neo98.py run --drive 'D=/path/to/folder' --cdrom /path/to/game.iso
```

`run` builds first when something changed; add `--no-build` to skip that.
Windows programs run on FEX by default. `NEO98_CPU=unicorn` switches to the
Unicorn backend, which is slower but handy for comparing behavior. 16-bit
programs always use Unicorn.

Programs that write to drive C: really change it. When you experiment, point
`--drive-c` at a copy.

## Tests

The tests live in `tools/` as `test_*.py` files, and each one can run on its
own:

```sh
python3 tools/test_release_smoke.py --source build/win98-c --program NOTEPAD.EXE
```

`python3 tools/test_public.py` runs every test that needs no Windows
installation, a few at a time, and prints one table. This is what CI runs on
Linux x64 and macOS. Tests that need drive C: are listed as NEEDS-WIN98
instead of failing. `tools/public_tests.json` lists the tests known to fail on
each platform; the run fails only on other failures.

Tests that start Windows programs need an installed Windows in
`build/win98-c` (or the folder in `NEO98_WIN98_ROOT`). They work on a private
copy of it and keep the sound off.

A few tests build their Windows or DOS fixtures with the MinGW cross compiler
(`i686-w64-mingw32-gcc`) or with `nasm`, and skip without them. On macOS:
`brew install mingw-w64 nasm`; on Ubuntu: `sudo apt install gcc-mingw-w64-i686 nasm`.

## Checks before every commit

`tools/check_release.py` checks that every tracked file may be published:
no product keys, issue numbers, addresses or other provenance notes about
Windows binaries, private tools or evidence files, broken document links or
untranslated prose. CI runs it on every push and pull request. Run it before
each commit with the hook in `.githooks`, once per checkout:

```sh
git config core.hooksPath .githooks
```

The hook checks only the staged changes; `python3 tools/check_release.py`
checks the whole tree. A finding names the file and the rule, never the
matched text.

## Linux

The Linux version is experimental. It runs the x86 code of Windows programs
directly on the processor, so it needs a 64-bit x86 PC and a kernel with
Syscall User Dispatch (Linux 5.11 or newer). It has been tested on Ubuntu
24.04.

On Ubuntu:

```sh
sudo apt install git make clang-18 llvm-18 lld-18 libsdl2-dev libgtk-3-dev \
    cmake python3-venv pkg-config zlib1g-dev xwayland \
    qemu-system-x86 mtools cabextract tesseract-ocr
export NEO98_LLVM_BIN=/usr/lib/llvm-18/bin
python3 neo98.py setup --iso '/path/to/Windows 98 Second Edition.iso'
python3 neo98.py run --gpu none
```

The window uses GTK and SDL 2 under X11 or XWayland.

`--gpu none` leaves out 3D graphics. For 3D, install the Vulkan packages as
well and drop `--gpu none`; Vulkan is the default on Linux:

```sh
sudo apt install libvulkan-dev libshaderc-dev glslang-tools mesa-vulkan-drivers
python3 neo98.py run
```

`python3 neo98.py app --target linux-x64` packs everything into
`build/Neo98.AppDir`, including shaderc, which the Vulkan renderer uses to
compile shaders while a game runs. Start it with `build/Neo98.AppDir/AppRun`,
or run `AppRun --install-launcher` once to add Neo98 to your desktop's app
list. The package needs the same or a newer glibc than the machine that
built it.

The release is built on Ubuntu 22.04, so that it starts there and on
everything newer. That Ubuntu has no shaderc package and an SDL older than
the hosts need: `python3 tools/build_shaderc.py` and
`python3 tools/build_sdl2.py` build both from their pinned sources into
`build/toolchain/usr`, and the build uses that SDL when
`build/toolchain/usr/bin` is first on `PATH`
(`.github/workflows/release.yml` has the packages to install).

shaderc is licensed under Apache 2.0, which works with the GPL version 3 but
not with version 2. The Linux version contains no Unicorn, the only part of
Neo98 that is limited to the GPL version 2, so a Linux package with shaderc
is distributed under the GPL version 3. The Mac app contains Unicorn and
stays under version 2. Neo98's own license, GPL-2.0-or-later, allows both.

## Making a release

`python3 tools/export_release.py DESTINATION` copies the public source tree
into an empty folder and refuses to finish if it finds private paths,
product keys or files that don't belong in the repository.

`python3 tools/make_release_assets.py` builds everything that goes on a
GitHub release: `Neo98.dmg`, the Neo98 source archive, the source archives
of the GPL components in the app (Unicorn, DOSBox Pure and the FreeDOS boot
sector) and `SHA256SUMS`. The GitHub workflow in `.github/workflows` does
the same when a `v*` tag is pushed.

Releases carry a setup toolkit, built by `python3 tools/setup_toolkit.py`:
QEMU (only `qemu-system-i386`), mtools, cabextract, Tesseract and Python,
with every library they need. With it a downloaded release installs Windows
from an ISO without Homebrew, apt or the Xcode tools. Building the toolkit
needs `ninja`, `pkg-config`, GLib, pixman and Tesseract (macOS: `brew install
ninja pkg-config glib pixman tesseract`; Ubuntu: `sudo apt install ninja-build
pkg-config libglib2.0-dev libpixman-1-dev tesseract-ocr patchelf`).

For a Linux release, run `python3 tools/make_release_assets.py --no-sources`,
or `python3 tools/linux_app.py --release` for the package alone. Both release
scripts leave Media Center out, even when `NEO98_WITH_HOST_MEDIA=1` is set.

Without further settings the app is signed ad hoc, which is enough on your
own Mac. A release for other people is signed with a Developer ID
certificate and notarized by Apple:

```sh
xcrun notarytool store-credentials neo98-notary   # once, with an App Store Connect API key
NEO98_SIGN_IDENTITY='Developer ID Application: Your Name (TEAMID)' \
    python3 tools/make_release_assets.py --notarize neo98-notary
```

The script signs the app with the hardened runtime and the entitlements in
`tools/arm64_hypervisor.entitlements`, has Apple notarize it, and does the
same for the disk image.
