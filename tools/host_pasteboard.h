// SPDX-License-Identifier: GPL-2.0-or-later
// The pasteboard of the Mac for the clipboard of the guest,
// tools/host_pasteboard.mm (docs/BUILDING.md, "Clipboard"). Text travels
// as UTF-16, files as paths on this Mac. tools/neo98_host.cpp calls all of it
// from its main loop or from a call of the guest on the same thread; nothing
// here knows the guest.
#pragma once
#include <string>
#include <vector>

namespace HostPasteboard
{
enum : unsigned { Text = 1, Files = 2, Image = 4 };
// Chooses the pasteboard. "general" is the one of the Mac, "unique" a private
// one that exists only for this process (the tests: the user's own clipboard
// is never read or written), any other name a named pasteboard. False where
// there is no AppKit; then every other call does nothing.
bool Open(const char *name);
bool Active();
// A pasteboard of this process alone ("unique").
bool Private();
// The name of the pasteboard in use, for a helper that reads the private one.
std::string Name();
// NSPasteboard's change count: it moves whenever anyone writes.
long ChangeCount();
unsigned Kinds();
// The string of the pasteboard, composed characters (NFC).
bool ReadText(std::u16string &text);
bool ReadFiles(std::vector<std::string> &paths);
// The picture of the pasteboard (PNG or TIFF) as a packed DIB: a
// BITMAPINFOHEADER of 24 bits a pixel, then the rows from the bottom up.
// Transparent parts are white.
bool ReadImage(std::vector<unsigned char> &dib);
// Replaces the content; each may be null. `dib` is a packed DIB (1, 4, 8, 16,
// 24 or 32 bits a pixel, not compressed) and goes out as PNG and TIFF.
// Answers the change count after the write, so the host knows its own change.
long Write(const std::u16string *text, const std::vector<std::string> *files,
           const std::vector<unsigned char> *dib = nullptr);
// For the tests: the bytes of a PNG file as another program would copy them.
long WritePng(const std::vector<unsigned char> &png);
// One line per entry for the tests: "text <hex of UTF-8>", "file <path>",
// "image <width> <height> <rrggbb of every pixel, top row first>" (the
// pixels only up to 64 of them).
std::string Describe();
// Releases a private pasteboard.
void Close();
} // namespace HostPasteboard
