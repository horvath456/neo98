<p align="center"><img src="docs/images/icon.png" width="128" alt="Neo98 icon"></p>

<h1 align="center">Neo98</h1>

<p align="center">Windows 98 programs and games on your Apple Silicon Mac and on Linux.</p>

<p align="center"><a href="https://github.com/horvath456/neo98/releases/latest"><b>Download</b></a> · <a href="#getting-started">Getting started</a> · <a href="#building-from-source">Building from source</a></p>

![Paint, Explorer, Notepad and the Windows 98 taskbar on a Mac desktop in seamless mode](docs/images/seamless.png)

Neo98 brings the software of the late 90s back to your computer. You install your old Windows 98 CD once. After that, Explorer, Paint and the games you played back then start with a double-click, right next to your other apps.

## Features

- **No virtual PC.** Neo98 brings its own implementation of the core of Windows. On the Mac it translates each program's x86 code to ARM while it runs; on a Linux PC the code runs directly on the processor.
- **The real Windows 98.** Explorer, the Start menu, the dialogs and the system programs come from your own CD and run unchanged, so everything looks and behaves the way you remember it.
- **Seamless mode.** The Windows desktop steps aside and every program gets its own window on your Mac or Linux desktop. Or keep the whole Windows desktop in one window, or in fullscreen.
- **3D games** that use Direct3D 5 to 9, DirectDraw, OpenGL or 3dfx Glide.
- **DOS games and programs**, through DOSBox Pure.
- **Save states.** Game machines give a game its own window and its own copy of its files, and let you save the game's state at any moment and continue later.
- **At home on your desktop.** Open `.exe` files and CD images from Finder or your file manager, copy and paste between Windows and your other apps, and use any folder as drive D:.
- **Sound, game controllers, the microphone and the internet** for Windows programs.
- **Easy setup.** Choose your CD image and type in your product key; Neo98 installs Windows by itself.

On Linux it looks like this, with the desktop in a window, in seamless mode and in a game:

<table>
<tr>
<td width="33%"><img src="docs/images/linux-desktop-apps.gif" alt="Neo98 on Linux: drawing in Windows 98 Paint next to Notepad on the Windows 98 desktop"></td>
<td width="33%"><img src="docs/images/linux-seamless.gif" alt="Windows 98 Notepad and Calculator as separate windows on a Linux desktop in seamless mode, adding 2 and 3"></td>
<td width="33%"><img src="docs/images/linux-wonderland.gif" alt="Playing the 3D game Wonderland in Neo98 on Linux"></td>
</tr>
</table>

The project is young. Plenty of programs already work, some don't yet. If you run into one that doesn't, please [let me know](#when-something-doesnt-work).

## Getting started

You need:

- A Mac with Apple Silicon (M1 or newer) and macOS 13 or later, or a 64-bit x86 PC with Linux 5.11 or newer and a desktop with X11 or XWayland (tested on Ubuntu 24.04; 3D games need a Vulkan driver)
- A Windows 98 Second Edition CD image in English or German. It has to be the full version, not the upgrade. An `.iso` file works, and so does an `.iso` inside a `.zip` or `.7z` archive.
- The product key that came with your copy of Windows
- About 2 GB of free disk space

Neo98 contains no Microsoft software. Setup takes everything it needs from your CD image. Everything else comes with the download.

### On the Mac

**1. Install Neo98.** Download `Neo98.dmg` from the [latest release](https://github.com/horvath456/neo98/releases/latest), open it and drag Neo98 into your Applications folder.

**2. Open Neo98.** The first time, macOS asks whether you really want to open an app from the internet. Click **Open**.

**3. Install Windows 98.** The setup assistant appears. Choose your CD image, type in your product key and click **Install**.

<p align="center"><img src="docs/images/setup.png" width="560" alt="The setup assistant asks for the Windows 98 CD image, the product key and the install location"></p>

**4. Let Windows Setup run.** Setup now runs by itself, and you can watch along in the window. Windows restarts a few times on the way. The whole thing takes about 20 minutes.

<p align="center"><img src="docs/images/setup-progress.png" width="560" alt="Windows 98 Setup running inside the Neo98 setup assistant"></p>

When Setup is done, the Windows 98 desktop appears. From now on, Neo98 starts right there.

### On Linux

Download `Neo98-x86_64.AppImage` from the [latest release](https://github.com/horvath456/neo98/releases/latest), make it executable and start it:

```sh
chmod +x Neo98-x86_64.AppImage
./Neo98-x86_64.AppImage                    # opens the setup the first time
./Neo98-x86_64.AppImage --install-launcher # optional: adds Neo98 to your app menu
```

The same package is there as `Neo98-linux-x64.tar.gz`, for a system without FUSE: unpack it and start `NEO98.AppDir/AppRun`.

Setup works the same way as on the Mac: choose your CD image, type in your product key, and Windows installs itself. The package needs a glibc at least as new as Ubuntu 24.04's.

<p align="center"><img src="docs/images/linux-desktop.png" width="560" alt="Windows 98 Notepad and Paint inside the Neo98 window on Linux, with its GTK menu bar"></p>

## Using Neo98

**Starting programs.** Double-click a Windows program in Finder, or choose **File → Open program…** in Neo98. The Start menu works too, just like on a real PC.

**Your files.** Drive C: is an ordinary folder on your Mac, `Neo98/C` in your home folder. Copy programs and files into it with Finder. **Machine → Choose drive D:…** turns any folder into drive D:.

**CDs.** Many games want their CD in the drive. Choose **Machine → Mount ISO or folder as CD-ROM…**, or double-click the CD image in Finder.

**Seamless mode.** **View → Seamless** hides the Windows desktop and puts every program in its own window.

**Game machines.** When you open a game from Finder, Neo98 asks whether to run it normally or in a game machine. A game machine has its own window and keeps the game's files, settings and saves apart from your other programs. **Machine → Save state** (Shift-Command-S) freezes the game exactly where it is, and you can continue from there later.

**The mouse.** Some games capture the mouse pointer. Press Control-Option to get it back.

**On Linux** the commands are in the menu bar of the Neo98 window. Drive D: and CDs are in the **Drives** menu, and **Machine → Release mouse** gets the mouse back. Ctrl+Shift+M opens the machine menu, Ctrl+Shift+S switches seamless mode and F11 fullscreen.

## Games

Neo98 is tested with a collection of game demos and shareware releases from the late 90s and early 2000s. Here are a few of them:

![Star Wars: Rogue Squadron 3D, Midtown Madness, Max Payne, StarCraft, Age of Empires and Hexen II running in Neo98](docs/images/games.jpg)

## Questions

**How is this different from a virtual machine like 86Box, PCem or UTM?** Those emulate a whole PC and boot Windows on it. Neo98 runs Windows programs directly, as apps on your Mac or Linux desktop, without a virtual PC in between. That makes it fast and lets Windows programs share your desktop, clipboard and files.

**And from Wine?** Wine reimplements all of Windows from scratch and focuses on newer programs. Neo98 uses your own Windows 98, so everything from Explorer to the dialogs is the real thing from 1999, and it is made for the programs and games of that time.

**Do I need my own Windows?** Yes. Neo98 contains no Microsoft software. You need a Windows 98 SE CD image and the product key that came with it.

**Which games run?** Many, including 3D games through Direct3D, OpenGL and 3dfx Glide, and DOS games. See [Games](#games) for a few examples, and please [tell me](https://github.com/horvath456/neo98/issues) about the ones that don't work.

## When something doesn't work

Not every program runs yet, and some run with glitches. If you find one, please [open an issue](https://github.com/horvath456/neo98/issues). Say which program it is, where it came from and what happened. If Neo98 wrote a crash report, attach it; you'll find them in `~/Library/Logs/Neo98/crash-reports` on the Mac and in `~/.local/state/neo98/crash-reports` on Linux. Please don't attach Windows files or your product key.

Some limits you should know about:

- Only the English and the German Windows 98 Second Edition CD can be installed for now. The German one is new and has seen little testing.
- Saving a game's state doesn't work in every situation yet.
- The Linux version is newer than the Mac version and has seen fewer programs so far.

## Building from source

### macOS

You need the Xcode Command Line Tools (`xcode-select --install`) and a few packages from [Homebrew](https://brew.sh):

```sh
brew install llvm lld cmake ninja pkg-config sdl2 qemu mtools cabextract tesseract
git clone https://github.com/horvath456/neo98.git
cd neo98
python3 neo98.py app
```

This builds `build/Neo98.app`. The first build fetches and compiles a few dependencies, so give it some time.

### Linux

On Ubuntu 24.04:

```sh
sudo apt install git make clang-18 llvm-18 lld-18 libsdl2-dev libgtk-3-dev \
    cmake python3-venv pkg-config zlib1g-dev xwayland \
    qemu-system-x86 mtools cabextract tesseract-ocr \
    libvulkan-dev libshaderc-dev glslang-tools mesa-vulkan-drivers
git clone https://github.com/horvath456/neo98.git
cd neo98
export NEO98_LLVM_BIN=/usr/lib/llvm-18/bin
python3 neo98.py app --target linux-x64
build/Neo98.AppDir/AppRun
```

This builds the same package as the release, in `build/Neo98.AppDir`.

[docs/BUILDING.md](docs/BUILDING.md) has the details, including running Neo98 from the command line. [ARCHITECTURE.md](ARCHITECTURE.md) explains how Neo98 works inside, and [docs/ARM64-APPS.md](docs/ARM64-APPS.md) shows how to write native ARM64 programs for it.

## License

Neo98 is free software under the GNU General Public License, version 2 or (at your option) any later version. See [COPYING](COPYING).

Neo98 builds on the work of other open source projects; [THIRD_PARTY.md](THIRD_PARTY.md) lists them. Every release links to the source code of the GPL components inside the app.

Neo98 is an independent project and isn't affiliated with Microsoft, which owns the Windows trademark.
