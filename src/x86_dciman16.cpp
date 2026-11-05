// SPDX-License-Identifier: GPL-2.0-or-later
// DCIMAN16 provider: marshal WORD arguments and far pointers from DCIMAN32
// into src/win98_dci.h operations. The last argument is adjacent to the Win16
// return frame; earlier arguments follow above it.
#include "win98_dci.h"
#include "x86_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace X86
{
namespace
{
namespace D = Win98Dci;
// What the port answers per catalogue ordinal, beside the argument word count.
enum Answer
{
    Reconstructed, // the routine runs; see the switch below
    Void,          // the original returns nothing and the stub reads no result
    FailGeneric,   // -1, the module's own answer for an unusable surface
    FailUnsupported, // -4, hard wired in the original for these three
    Surface,         // the primary surface of the display driver
};
struct Row
{
    unsigned char ordinal, words;
    Answer answer;
    const char *what;
};
// Word counts are the pushes of the 32-bit stub, and each matches the retf of
// its 16-bit target.
const Row rows[] = {
    // Private driver callback gates, not DCIMAN exports. DIBENG
    // takes (surface, RECT16*); and Windows 98 take only the surface.
    {80, 4, Surface, "driver BeginAccess"},
    {81, 2, Surface, "driver EndAccess"},
    {82, 2, Surface, "driver DestroySurface"},
    {D::OpenProviderOrdinal, 0, Reconstructed, "DCIOpenProvider"},
    {3, 3, Reconstructed, "DCICreatePrimary"},
    {4, 19, FailUnsupported, "DCICreateOffscreen"},
    {5, 5, FailUnsupported, "DCICreateOverlay"},
    {6, 9, FailUnsupported, "DCIEnum"},
    {D::CloseProviderOrdinal, 1, Reconstructed, "DCICloseProvider"},
    {D::SetSrcDestClipOrdinal, 8, FailGeneric, "DCISetSrcDestClip"},
    {D::WinWatchOpenOrdinal, 1, Reconstructed, "WinWatchOpen"},
    {D::WinWatchCloseOrdinal, 1, Reconstructed, "WinWatchClose"},
    {D::WinWatchDidStatusChangeOrdinal, 1, Reconstructed, "WinWatchDidStatusChange"},
    {D::WinWatchGetClipListOrdinal, 6, Reconstructed, "WinWatchGetClipList"},
    {D::GetWindowRegionDataOrdinal, 5, Reconstructed, "GetWindowRegionData"},
    {D::GetDCRegionDataOrdinal, 5, Reconstructed, "GetDCRegionData"},
    {D::DestroyOrdinal, 2, Surface, "DCIDestroy"},
    {D::EndAccessOrdinal, 2, Surface, "DCIEndAccess"},
    {D::BeginAccessOrdinal, 6, Surface, "DCIBeginAccess"},
    {D::DrawOrdinal, 2, FailGeneric, "DCIDraw"},
    {D::SetClipListOrdinal, 4, FailGeneric, "DCISetClipList"},
    {D::SetDestinationOrdinal, 6, FailGeneric, "DCISetDestination"},
    {D::CreatePrimary32Ordinal, 3, Surface, "DCICreatePrimary32"},
    {D::EnumThunkOrdinal, 9, FailUnsupported, "DCIEnum"},
    {D::CreateOverlayThunkOrdinal, 5, FailUnsupported, "DCICreateOverlay"},
    {D::CreateOffscreenThunkOrdinal, 19, FailUnsupported, "DCICreateOffscreen"},
};
const Row *Find(unsigned ordinal)
{
    for (const auto &row : rows)
        if (row.ordinal == ordinal)
            return &row;
    return nullptr;
}
} // namespace

unsigned Application::DciMan16Words(unsigned ordinal)
{
    const Row *row = Find(ordinal);
    return row ? unsigned(row->words) : ~0u;
}

// The catalogue ordinals of DCIMAN.DLL. `stack` addresses the last word the
// 32-bit stub pushed.
unsigned Application::DciMan16(unsigned ordinal, unsigned stack)
{
    const Row *row = Find(ordinal);
    if (!row)
        return 0;
    auto word = [&](unsigned at) {
        unsigned short value = 0;
        Read(stack + at, &value, 2);
        return unsigned(value);
    };
    auto word_at = [&](unsigned address) {
        unsigned short value = 0;
        Read(address, &value, 2);
        return unsigned(value);
    };
    auto note = [&](const char *text, unsigned value) {
        if (!log)
            return;
        char line[224];
        snprintf(line, sizeof line, "DCIMAN16 ordinal=%u %s %s result=%08x", ordinal, row->what,
                 text, value);
        log(line);
    };
    if (ordinal == 3)
    {
        // DCIMAN Windows 98 returns the driver's far pointer. DIBENG
        // exposes the screen offset/selector, and
        // installs the three driver methods. Win16 clients call these
        // pointers directly, without going through DCIMAN's public methods.
        const unsigned output = selectors.Linear((word(2) << 16) | word(0));
        if (!output || !virtual_memory.Access(output, 4, true)) return D::Generic;
        Write32(output, 0);
        const unsigned scratch = dci_surface_out ? dci_surface_out : (dci_surface_out = Allocate(16));
        if (!scratch || DciCreatePrimarySurface(scratch)) return D::Generic;
        auto refused = [&]() { DciDestroySurface(dci_surface); return D::Generic; };
        if (!dci_win16_surface) dci_win16_surface = Allocate(4096);
        if (!dci_win16_surface) return refused();
        if (!dci_win16_selector)
            dci_win16_selector = Win16Segment(dci_win16_surface, 4095, false, false);
        const unsigned bytes = Read32(dci_surface + 0x20) * Read32(dci_surface + 0x1c);
        if (!bytes) return refused();
        if (!dci_screen_selector)
            dci_screen_selector = Win16Segment(User::ScreenGuestAddress(), bytes - 1, false, false);
        if (!dci_win16_selector || !dci_screen_selector) return refused();
        // WLOADER uses 32-bit offsets with the returned 16-bit selector.
        // Rebind it when the single display device gets a new mode.
        auto *screen = selectors.Find(dci_screen_selector << 16);
        screen->base = User::ScreenGuestAddress();
        screen->limit = bytes - 1;
        if (!Win16Descriptor(dci_screen_selector)) return refused();
        unsigned char info[0x48];
        if (!Read(dci_surface, info, sizeof info) || !Store(dci_win16_surface, info, sizeof info))
            return refused();
        Write32(dci_win16_surface + 0x28, 0);
        Write32(dci_win16_surface + 0x2c, dci_screen_selector);
        for (unsigned method = 0; method != 3; ++method)
        {
            const unsigned gate = Win16Gate("DCIMAN", nullptr, 80 + method);
            const unsigned pointer = gate ? Win16GatePointer(gate) : 0;
            if (!pointer) return refused();
            Write32(dci_win16_surface + 0x3c + method * 4, pointer);
        }
        Write32(output, dci_win16_selector << 16);
        note("16-bit screen selector", 0);
        return 0;
    }
    if (ordinal >= 80 && ordinal <= 82)
    {
        const unsigned at = ordinal == 80 ? 4 : 0;
        const unsigned surface = selectors.Linear(Read32(stack + at));
        if (!surface || surface != dci_win16_surface) return D::Generic;
        if (ordinal == 81) return DciEndAccess(dci_surface);
        if (ordinal == 82) return DciDestroySurface(dci_surface);
        const unsigned rect = selectors.Linear(Read32(stack));
        short r[4];
        if (!rect || !Read(rect, r, sizeof r)) return D::Generic;
        return DciBeginAccess(dci_surface, r[0], r[1], int(r[2]) - r[0], int(r[3]) - r[1]);
    }
    // DCI surface methods normally dispatch through driver-supplied far
    // pointers. Neo98's display driver is native, so call the implementations
    // in x86_ddraw_driver.cpp directly and leave guest-callable method fields
    // null.
    if (row->answer == Surface)
    {
        auto pointer = [&](unsigned at) {
            return selectors.Linear((word(at + 2) << 16) | word(at));
        };
        auto signed_word = [&](unsigned at) {
            const unsigned v = word(at);
            return int(v & 0x8000 ? int(v) - 65536 : int(v));
        };
        // Windows 98 copies the structure of the driver into the caller's one:
        // the source size is raised to 48h and must not pass 54h, as many
        // bytes as the caller's dwSize names are copied, dwReserved2 (34h)
        // gets the far pointer of the source, the two access methods turn
        // into -1 when the driver has them, DestroySurface (44h) always
        // does, and without a surface selector (2Ch) the caller loses
        // DCI_1632_ACCESS (bit 40h of dwDCICaps). A primary surface also gets
        // -1 in the three fields at 48h..53h.
        auto publish = [&](unsigned source, unsigned destination) -> unsigned {
            if (!source || !destination)
                return D::Generic;
            unsigned size = Read32(source);
            if (size < 0x48)
                Write32(source, size = 0x48);
            if (size > 0x54)
                return D::Generic;
            unsigned char bytes[65536 / 64];
            const unsigned count = word_at(destination);
            for (unsigned at = 0; at < count; at += sizeof bytes)
            {
                const unsigned part = count - at < sizeof bytes ? count - at : unsigned(sizeof bytes);
                if (!Read(source + at, bytes, part) || !Store(destination + at, bytes, part))
                    return D::Generic;
            }
            if (!dci_selector)
                dci_selector = selectors.CreateSegment(source, 0xfff, false);
            if (!dci_selector || source != dci_surface)
                return D::Generic;
            Write32(destination + 0x34, dci_selector << 16);
            if (Read32(source + 0x3c))
            {
                Write32(destination + 0x3c, ~0u);
                Write32(destination + 0x40, ~0u);
            }
            Write32(destination + 0x44, ~0u);
            if (word_at(source + 0x2c))
            {
                // turn a surface selector into a flat offset;
                // the display driver of this port never hands one out.
                Fail("DCIMAN: a DCI surface with a selector is not ported");
                return D::Generic;
            }
            unsigned char caps = 0;
            Read(destination + 4, &caps, 1);
            caps &= 0xbf;
            Store(destination + 4, &caps, 1);
            unsigned char kind = 0;
            Read(source + 4, &kind, 1);
            if ((kind & 15) == 1)
                for (unsigned at = 0x48; at < 0x54; at += 4)
                    Write32(destination + at, ~0u);
            return 0;
        };
        // A structure whose DestroySurface field
        // is -1 is a copy, and the driver's own one is behind dwReserved2.
        auto surface = [&](unsigned at) {
            const unsigned given = pointer(at);
            if (given && given == dci_win16_surface) return dci_surface;
            if (given && Read32(given + 0x44) == ~0u)
                return selectors.Linear(Read32(given + 0x34));
            return given;
        };
        unsigned answer = D::Generic;
        if (ordinal == D::CreatePrimary32Ordinal)
        {
            // DCICreatePrimary32(hdc, lpSurface), : DCICreatePrimary
            // into a far pointer on its own stack, then the copy of
            // into the caller's structure, whose dwSize the caller set.
            // The HDC is the provider of DCIOpenProvider; the surface belongs
            // to the screen, so it is not read further.
            const unsigned scratch = dci_surface_out ? dci_surface_out : (dci_surface_out = Allocate(16));
            answer = scratch ? DciCreatePrimarySurface(scratch) : D::Generic;
            if (!answer)
                answer = publish(Read32(scratch), pointer(0));
        }
        else if (ordinal == D::BeginAccessOrdinal)
        {
            answer = DciBeginAccess(surface(8), signed_word(6), signed_word(4), signed_word(2),
                                    signed_word(0));
            // a positive answer means the driver changed its
            // structure, and the caller's copy is made again.
            if (answer && !(answer & 0x8000) && pointer(8) != surface(8))
                publish(surface(8), pointer(8));
        }
        else if (ordinal == D::EndAccessOrdinal)
            answer = DciEndAccess(surface(0));
        else
            answer = DciDestroySurface(surface(0));
        note(answer ? "the display driver refused" : "the display driver served it", answer);
        return answer;
    }
    switch (row->answer)
    {
    case Void:
        // and Windows 98 call a method of a structure only when the
        // pointer in it is not null, and their stubs read no result. Without a
        // surface there is nothing to release.
        note("nothing to release", 0);
        return 0;
    case FailUnsupported:
        note("the original answers -4 without asking the driver", D::Unsupported);
        return D::Unsupported;
    case FailGeneric:
        // No surface can exist without the display driver of PLAN.md step 12,
        // so every surface call is already outside the contract. -1 is the
        // module's own answer for an unusable surface; a zero
        // would arm the direct access path of the caller.
        note("no DCI surface exists", D::Generic);
        return D::Generic;
    default:
        break;
    }
    if (ordinal == D::OpenProviderOrdinal)
    {
        // One guest scratch page for the strings the native profile and device
        // entries want to see in the process's own memory.
        struct Scratch
        {
            Application &app;
            unsigned address = 0;
            explicit Scratch(Application &a) : app(a) { address = a.Allocate(4096); }
            ~Scratch() { if (address) app.Free(address); }
            unsigned Put(const char *text, unsigned slot)
            {
                if (!address || !text) return 0;
                unsigned length = 0;
                while (text[length]) ++length;
                if (length >= 256) return 0;
                const unsigned at = address + slot * 256;
                app.Store(at, text, length + 1);
                return at;
            }
            unsigned Slot(unsigned slot) const { return address ? address + slot * 256 : 0; }
        } scratch(*this);
        struct System final : D::Environment
        {
            Application &app;
            Scratch &scratch;
            System(Application &a, Scratch &s) : app(a), scratch(s) {}
            unsigned ProfileInt(const char *section, const char *key, unsigned fallback,
                                const char *file) override
            {
                const unsigned s = scratch.Put(section, 0), k = scratch.Put(key, 1),
                               f = scratch.Put(file, 2);
                if (!s || !k || !f) return fallback;
                const unsigned arguments[] = {s, k, fallback, f};
                return app.Api("GetPrivateProfileIntA", arguments);
            }
            bool ProfileString(const char *section, const char *key, char *out, unsigned size,
                               const char *file) override
            {
                if (size) out[0] = 0;
                const unsigned s = scratch.Put(section, 0), k = scratch.Put(key, 1),
                               f = scratch.Put(file, 2), e = scratch.Put("", 3),
                               buffer = scratch.Slot(4);
                if (!s || !k || !f || !buffer) return false;
                const unsigned arguments[] = {s, k, e, buffer, 128, f};
                if (!app.Api("GetPrivateProfileStringA", arguments)) return false;
                const char *text = app.ReadString(buffer);
                unsigned i = 0;
                for (; text && text[i] && i + 1 < size; ++i) out[i] = text[i];
                if (size) out[i] = 0;
                return out[0] != 0;
            }
            unsigned CreateDriverDC(const char *driver) override
            {
                const unsigned name = scratch.Put(driver, 0);
                if (!name) return 0;
                // and wrap the call in SetErrorMode so a missing
                // driver file opens no box.
                const unsigned quiet[] = {0x8000};
                const unsigned previous = app.Api("SetErrorMode", quiet);
                const unsigned create[] = {name, 0, 0, 0};
                const unsigned hdc = app.GdiApi("CreateDCA", create);
                const unsigned restore[] = {previous};
                app.Api("SetErrorMode", restore);
                return hdc;
            }
            unsigned QueryDciCommand(unsigned hdc) override
            {
                const unsigned input = scratch.Slot(5);
                if (!input) return 0;
                const unsigned short command = (unsigned short)D::DciCommand;
                app.Store(input, &command, 2);
                const unsigned arguments[] = {hdc, D::QueryEscSupport, 2, input, 0};
                const unsigned answer = app.GdiApi("Escape", arguments);
                return answer == ~0u ? 0u : answer;
            }
            void DeleteDC(unsigned hdc) override
            {
                const unsigned arguments[] = {hdc};
                app.GdiApi("DeleteDC", arguments);
            }
            void Note(const char *candidate, unsigned hdc, unsigned supported) override
            {
                if (!app.log) return;
                char line[192];
                snprintf(line, sizeof line,
                         "DCIMAN16 provider candidate=%s hdc=%08x dcicommand=%u", candidate, hdc,
                         supported);
                app.log(line);
            }
        } system(*this, scratch);
        const unsigned hdc = D::OpenProvider(system);
        note(hdc ? "the display driver knows DCICOMMAND" : "no display driver knows DCICOMMAND",
             hdc);
        // The 16-bit caller keeps the DC in a word; a handle of this port
        // above F800h travels as the token DDRAW16 uses for the same
        // contract (src/x86_ddraw16.cpp).
        return DirectDraw16WordHandle(hdc);
    }
    if (ordinal == D::CloseProviderOrdinal)
    {
        // deletes the DC it was given and does nothing for a null one.
        const unsigned hdc = DirectDraw16NativeHandle(word(0));
        if (hdc)
        {
            const unsigned arguments[] = {hdc};
            GdiApi("DeleteDC", arguments);
            DirectDraw16ReleaseHandle(word(0));
        }
        note("provider closed", hdc);
        return 0;
    }
    // The window watch, of DCIMAN.DLL. A watch is a local
    // record with the signature 42424242h; its handle is the record, here
    // the slot number plus one. The original sets the "changed" word from a
    // WH_CALLWNDPROC hook when a window moves; this port compares
    // the visible region with the one it handed out last instead, which
    // gives the same answer (DciWatch in src/x86_app.h). OPENGL32 of the ISO
    // installation opens a watch in wglSetPixelFormat once DCI has a primary surface and fails without one.
    auto watch_of = [&](unsigned handle) -> DciWatch * {
        return handle && handle <= 16 && dci_watches[handle - 1].used ? &dci_watches[handle - 1] : nullptr;
    };
    if (ordinal == D::WinWatchOpenOrdinal)
    {
        // LocalAlloc(LPTR, 22h), the window at +6, "changed" = 1.
        for (unsigned i = 0; i < 16; ++i)
            if (!dci_watches[i].used)
            {
                dci_watches[i] = DciWatch{};
                dci_watches[i].used = true;
                dci_watches[i].changed = true;
                dci_watches[i].hwnd = word(0);
                note("watch opened", i + 1);
                return i + 1;
            }
        note("no free watch record", 0);
        return 0;
    }
    if (ordinal == D::WinWatchCloseOrdinal)
    {
        // unlink, free the cached region data (+20h) and the record.
        if (DciWatch *watch = watch_of(word(0)))
            FreeDciWatch(*watch);
        note("watch closed", 0);
        return 0;
    }
    if (ordinal == D::WinWatchDidStatusChangeOrdinal)
    {
        // the "changed" word, zero for a handle without the signature.
        DciWatch *watch = watch_of(word(0));
        if (!watch)
            return 0;
        if (!watch->changed)
        {
            unsigned char *data = nullptr;
            unsigned bytes = 0;
            if (DciWatchRegion(watch->hwnd, data, bytes))
            {
                watch->changed = bytes != watch->seen_bytes || memcmp(data, watch->seen, bytes) != 0;
                free(data);
            }
        }
        note("status", watch->changed);
        return watch->changed ? 1 : 0;
    }
    if (ordinal == D::WinWatchGetClipListOrdinal)
    {
        // (hww, prc, cbSize, prd): [bp+10h] the watch, [bp+0Ch] the
        // rectangle, [bp+0Ah] the size, [bp+6] the RGNDATA of the caller.
        auto far = [&](unsigned at) { return selectors.Linear((word(at + 2) << 16) | word(at)); };
        DciWatch *watch = watch_of(word(10));
        if (!watch)
            return 0;
        const unsigned prd = (word(0) || word(2)) ? far(0) : 0, size = word(4);
        const unsigned prc = (word(6) || word(8)) ? far(6) : 0;
        int wanted[4] = {};
        if (prc)
            for (unsigned i = 0; i < 4; ++i)
                wanted[i] = int(Read32(prc + 4 * i));
        // EqualRect with the rectangle of the last list, or
        // IsRectEmpty of it without one: a different request is a change.
        bool empty = watch->rect[0] >= watch->rect[2] || watch->rect[1] >= watch->rect[3];
        if (prc ? memcmp(wanted, watch->rect, sizeof wanted) != 0 : !empty)
            watch->changed = true;
        unsigned char *data = nullptr;
        unsigned bytes = 0;
        if (!DciWatchRegion(watch->hwnd, data, bytes))
            return 0;
        if (bytes != watch->seen_bytes || memcmp(data, watch->seen, bytes) != 0)
            watch->changed = true;
        if (watch->changed || !watch->cache)
        {
            free(watch->seen);
            watch->seen = data;
            watch->seen_bytes = bytes;
            free(watch->cache);
            // the cache holds 40h bytes more than the data.
            watch->cache = static_cast<unsigned char *>(calloc(bytes + 0x40, 1));
            watch->cache_bytes = bytes + 0x40;
            if (!watch->cache)
                return 0;
            memcpy(watch->cache, data, bytes);
            data = nullptr;
            if (prc)
            {
                memcpy(watch->rect, wanted, sizeof wanted);
                // GetClientRect; unless the rectangle is the whole
                // client area, it goes to the screen and
                // every rectangle of the list is cut to it; an empty one
                // leaves nCount one lower, nRgnSize stays.
                const unsigned scratch = Allocate(16);
                bool whole = false;
                if (scratch)
                {
                    const unsigned client[] = {watch->hwnd, scratch};
                    Api("GetClientRect", client);
                    whole = int(Read32(scratch)) == wanted[0] && int(Read32(scratch + 4)) == wanted[1] &&
                            int(Read32(scratch + 8)) == wanted[2] && int(Read32(scratch + 12)) == wanted[3];
                    if (!whole)
                    {
                        for (unsigned i = 0; i < 4; ++i)
                            Write32(scratch + 4 * i, unsigned(wanted[i]));
                        const unsigned top_left[] = {watch->hwnd, scratch}, bottom_right[] = {watch->hwnd, scratch + 8};
                        Api("ClientToScreen", top_left);
                        Api("ClientToScreen", bottom_right);
                        int bound[4];
                        for (unsigned i = 0; i < 4; ++i)
                            bound[i] = int(Read32(scratch + 4 * i));
                        unsigned char *header = watch->cache;
                        const unsigned count = header[8] | header[9] << 8 | header[10] << 16 | unsigned(header[11]) << 24;
                        unsigned kept = 0;
                        for (unsigned n = 0; n < count && 0x20 + 16 * (n + 1) <= bytes; ++n)
                        {
                            int r[4];
                            memcpy(r, watch->cache + 0x20 + 16 * n, 16);
                            if (r[0] < bound[0]) r[0] = bound[0];
                            if (r[1] < bound[1]) r[1] = bound[1];
                            if (r[2] > bound[2]) r[2] = bound[2];
                            if (r[3] > bound[3]) r[3] = bound[3];
                            if (r[2] > r[0] && r[3] > r[1])
                                memcpy(watch->cache + 0x20 + 16 * kept++, r, 16);
                        }
                        header[8] = kept;
                        header[9] = kept >> 8;
                        header[10] = kept >> 16;
                        header[11] = kept >> 24;
                    }
                    Free(scratch);
                }
            }
            else
                memset(watch->rect, 0, sizeof watch->rect);   // SetRectEmpty
        }
        free(data);
        watch->changed = false;
        // nRgnSize of the header plus 21h; without a buffer that is the
        // answer, with one as many bytes as fit are copied and counted.
        const unsigned char *header = watch->cache;
        const unsigned needed = (header[12] | header[13] << 8 | header[14] << 16 | unsigned(header[15]) << 24) + 0x21;
        if (!prd)
        {
            note("clip list size", needed);
            return needed;
        }
        const unsigned copied = needed < size ? needed : size;
        const unsigned available = copied < watch->cache_bytes ? copied : watch->cache_bytes;
        Store(prd, watch->cache, available);
        note("clip list copied", copied);
        return copied;
    }
    // GetWindowRegionData and GetDCRegionData are not
    // reached by any caller the port knows; they answer the way the original
    // answers without a region: nothing copied.
    note("no region data in this port", 0);
    return 0;
}
// The visible region of the window as GetRegionData writes it (the RGNDATA
// that fetches through GetDCEx, InquireVisRgn and GetRegionData).
bool Application::DciWatchRegion(unsigned hwnd, unsigned char *&data, unsigned &bytes)
{
    data = nullptr;
    bytes = 0;
    const unsigned region = WindowVisibleRegion(hwnd);
    if (!region)
        return false;
    const unsigned size_query[] = {region, 0, 0};
    const unsigned needed = GdiApi("GetRegionData", size_query);
    const unsigned scratch = needed ? Allocate(needed) : 0;
    bool ok = false;
    if (scratch)
    {
        const unsigned fill[] = {region, needed, scratch};
        if (GdiApi("GetRegionData", fill) && (data = static_cast<unsigned char *>(malloc(needed))) != nullptr)
        {
            Read(scratch, data, needed);
            bytes = needed;
            ok = true;
        }
        Free(scratch);
    }
    const unsigned drop[] = {region};
    Api("DeleteObject", drop);
    return ok;
}
void Application::FreeDciWatch(DciWatch &watch)
{
    free(watch.cache);
    free(watch.seen);
    watch = DciWatch{};
}
} // namespace X86
