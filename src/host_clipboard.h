// SPDX-License-Identifier: GPL-2.0-or-later
// The clipboard of the machine the session runs on, behind the clipboard of
// the session (src/x86_clipboard.cpp). On the Mac that is the NSPasteboard
// (tools/host_pasteboard.mm). This is the seam between USER's clipboard and the host.
// Host to guest. The host only says that its clipboard changed and what kinds
// it holds (Offer). The next thread of the guest that asks for a message or
// calls a clipboard entry takes the clipboard the way a program does
// (EmptyClipboard, then SetClipboardData with a null handle for every format)
// and nobody owns it. The data is fetched from the provider when a program
// calls GetClipboardData, as WM_RENDERFORMAT would fetch it from an owner.
// Guest to host. When a program closes the clipboard after changing it, the
// session reads the text and the file list as a clipboard viewer would
// (WM_RENDERFORMAT for a format set with a null handle) and keeps a copy; the
// host sees the generation move and takes the copy in its own loop.
#pragma once
#include <stdint.h>

namespace X86
{
namespace HostClipboard
{
enum : unsigned { Text = 1, Files = 2, Image = 4 };
struct Provider
{
    void *context;
    // The text of the host as UTF-16 without a terminator, in a malloc block
    // the caller frees.
    bool (*Text)(void *context, uint16_t **units, unsigned *count);
    // The files of the host as guest paths in the ANSI code page, each ending
    // in NUL and the list in a second one, in a malloc block the caller frees.
    bool (*Files)(void *context, char **list, unsigned *bytes);
    // The picture of the host as a packed DIB (BITMAPINFOHEADER, then the
    // bits), in a malloc block the caller frees.
    bool (*Image)(void *context, unsigned char **dib, unsigned *bytes);
};
void SetProvider(const Provider *provider);
// The clipboard of the host changed and holds these kinds; 0 leaves the
// session's clipboard as it is.
void Offer(unsigned kinds);
bool OfferPending();
// What a program of the guest put on the clipboard last. `generation` moves
// with every such change; the pointers hold until the next one.
struct Content
{
    unsigned generation = 0;
    const uint16_t *text = nullptr; // CR LF line ends, no terminator
    unsigned text_units = 0;
    const char *files = nullptr;    // guest paths, ANSI, NUL after each
    unsigned files_bytes = 0;
    unsigned effect = 0;            // "Preferred DropEffect": 2 after Cut, 5 after Copy, 0 without one
    const unsigned char *image = nullptr; // CF_DIB as the program set it
    unsigned image_bytes = 0;
};
const Content &Guest();
} // namespace HostClipboard
} // namespace X86
