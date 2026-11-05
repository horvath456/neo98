// SPDX-License-Identifier: GPL-2.0-or-later
// Native functional test for the shell-blocker families: GDI object ownership
// and visibility, USER CopyImage and NotifyWinEvent, KERNEL32 OpenProcess,
// SearchPathA, LCMapStringA and the Win16 module gateway.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#define private public
#include "../src/x86_app.h"
#undef private
#include "fixture_native_modules.h"
#include "../src/win98_gdi_owner.h"
#include "../src/win98_image_copy.h"
#include "../src/win98_lcmap.h"

static std::vector<unsigned char> Load(const char *name)
{
    FILE *f = std::fopen(name, "rb");
    assert(f);
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    assert(n > 0);
    std::rewind(f);
    std::vector<unsigned char> data(n);
    assert(std::fread(data.data(), 1, n, f) == static_cast<size_t>(n));
    std::fclose(f);
    return data;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = Load(argv[1]), font = Load(argv[2]);
    X86::Application app(nullptr);
    assert(FixtureModules::KeepNative(app));
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "SHELLBLK.EXE"));
    app.threads[0].suspended = 1;
    unsigned checks = 0;

    // --- GDI object ownership -------------------------------------------
    const unsigned brush_args[] = {0x00ff00u};
    const unsigned brush = app.GdiApi("CreateSolidBrush", brush_args);
    assert(brush);
    // OwnGdi stamped this process; the original exchange returns that value.
    assert(Gdi::GetObjectOwner(brush) == (app.ProcessId() & 65535));
    ++checks;
    const unsigned own[] = {brush, 0x7fffffffu};
    assert(app.GdiApi("SetObjectOwner", own) == (app.ProcessId() & 65535));
    ++checks;
    // A handle that is not a live process is rejected before the object is
    // touched, and the owner stays where it was.
    const unsigned stranger[] = {brush, 0x12345678u};
    assert(app.GdiApi("SetObjectOwner", stranger) == 0);
    assert(Gdi::GetObjectOwner(brush) == (app.ProcessId() & 65535));
    ++checks;
    // A stale handle never reaches the gate.
    const unsigned dead[] = {0xdead00ffu, 0x7fffffffu};
    assert(app.GdiApi("SetObjectOwner", dead) == 0);
    ++checks;
    assert(Gdi::IsGdiObject(brush) == 0x4f48u - 0x4f46u);   // brush signature
    assert(!Gdi::IsGdiObject(0xdead00ffu));
    ++checks;
    // Private objects survive DeleteObject and still report success.
    assert(Gdi::MakeObjectPrivate(brush, true) == 0);
    assert(Gdi::MakeObjectPrivate(brush, true) == Win98GdiOwner::PrivateFlag);
    assert(Gdi::IsGdiObject(brush) == 0x4f48u - 0x4f46u);   // the flag is masked out
    ++checks;
    const unsigned drop[] = {brush};
    assert(app.GdiApi("DeleteObject", drop) == 1);
    assert(Gdi::ObjectExists(brush));
    ++checks;
    // The system path ignores the private bit.
    assert(Gdi::SysDeleteObject(brush));
    assert(!Gdi::ObjectExists(brush));
    ++checks;
    // An object handed to another owner is not freed by this process.
    const unsigned kept = app.GdiApi("CreateSolidBrush", brush_args);
    assert(kept);
    assert(Gdi::SetObjectOwner(kept, 0x4321) == (app.ProcessId() & 65535));
    assert(!app.OwnsGdiObject(kept));
    ++checks;
    assert(app.OwnsGdiObject(app.GdiApi("CreateSolidBrush", brush_args)));
    ++checks;
    // KERNEL32 ordinal 23 accepts an HMODULE of this
    // process, and GDI32 hands the object to that module: COMCTL32 keeps its
    // shared image list objects alive past the first process that way.
    // The module token cannot be a process id.
    const unsigned module_args[] = {0};
    const unsigned module = app.Api("GetModuleHandleA", module_args);
    assert(module);
    const unsigned shared = app.GdiApi("CreateSolidBrush", brush_args);
    const unsigned to_module[] = {shared, module};
    assert(app.GdiApi("SetObjectOwner", to_module) == (app.ProcessId() & 65535));
    assert(Gdi::GetObjectOwner(shared) == (0x8000u | ((module >> 16) & 0x7fffu)));
    assert(!app.OwnsGdiObject(shared));
    ++checks;

    // --- RectVisible and PtVisible ---------------------------------------
    const unsigned none[] = {0};
    const unsigned dc = app.GdiApi("CreateCompatibleDC", none);
    assert(dc);
    const unsigned make[] = {dc, 40, 20};
    const unsigned bitmap = app.GdiApi("CreateCompatibleBitmap", make);
    assert(bitmap);
    const unsigned pick[] = {dc, bitmap};
    app.GdiApi("SelectObject", pick);
    const unsigned clip[] = {dc, 0, 0, 10, 10};
    app.GdiApi("IntersectClipRect", clip);
    const unsigned rect = app.Allocate(16);
    auto rect_visible = [&](int l, int t, int r, int b) {
        app.Write32(rect, unsigned(l));
        app.Write32(rect + 4, unsigned(t));
        app.Write32(rect + 8, unsigned(r));
        app.Write32(rect + 12, unsigned(b));
        const unsigned args[] = {dc, rect};
        return app.GdiApi("RectVisible", args);
    };
    assert(rect_visible(0, 0, 5, 5) == 1);
    assert(rect_visible(20, 20, 30, 30) == 0);
    ++checks;
    // GDI32 normalizes the copy, so a reversed rectangle still hits.
    assert(rect_visible(5, 5, 0, 0) == 1);
    ++checks;
    // The exclusive edge rule: a rectangle that ends on the clip edge misses.
    assert(rect_visible(10, 10, 20, 20) == 0);
    ++checks;
    const unsigned point_in[] = {dc, 5, 5}, point_out[] = {dc, 11, 5};
    assert(app.GdiApi("PtVisible", point_in) == 1);
    assert(app.GdiApi("PtVisible", point_out) == 0);
    ++checks;
    // The DC gate rejects a non-DC handle without touching LastError storage.
    const unsigned not_a_dc[] = {bitmap, rect};
    assert(app.GdiApi("RectVisible", not_a_dc) == 0);
    ++checks;
    const unsigned drop_dc[] = {dc};
    app.GdiApi("DeleteDC", drop_dc);

    // --- NotifyWinEvent ---------------------------------------------------
    app.last_error = 4242;
    const unsigned event[] = {0x8002, 0, 0, 0};
    assert(app.Api("NotifyWinEvent", event) == 0);
    assert(app.last_error == 4242);
    ++checks;

    // --- CopyImage --------------------------------------------------------
    const unsigned icon_args[] = {0, 32512};   // IDI_APPLICATION
    const unsigned icon = app.Api("LoadIconA", icon_args);
    assert(icon);
    const unsigned bad_type[] = {icon, 3, 0, 0, 0};
    app.last_error = 0;
    assert(app.Api("CopyImage", bad_type) == 0 && app.last_error == 87);
    ++checks;
    const unsigned bad_flags[] = {icon, 1, 0, 0, 0x0100};
    app.last_error = 0;
    assert(app.Api("CopyImage", bad_flags) == 0 && app.last_error == 87);
    ++checks;
    const unsigned same[] = {icon, 1, 0, 0, Win98ImageCopy::CopyReturnOrg};
    assert(app.Api("CopyImage", same) == icon);
    ++checks;
    const unsigned duplicate[] = {icon, 1, 0, 0, 0};
    const unsigned copy = app.Api("CopyImage", duplicate);
    assert(copy && copy != icon && app.FindIcon(copy));
    ++checks;
    const unsigned scaled[] = {icon, 1, 16, 16, 0};
    const unsigned small = app.Api("CopyImage", scaled);
    assert(small && app.FindIcon(small)->data->width == 16);
    ++checks;
    // LR_COPYDELETEORG destroys the source only when the result differs.
    const unsigned move[] = {copy, 1, 0, 0, Win98ImageCopy::CopyDeleteOrg};
    const unsigned moved = app.Api("CopyImage", move);
    assert(moved && moved != copy && !app.FindIcon(copy));
    ++checks;
    const unsigned keep[] = {moved, 1, 0, 0,
                             Win98ImageCopy::CopyDeleteOrg | Win98ImageCopy::CopyReturnOrg};
    assert(app.Api("CopyImage", keep) == moved && app.FindIcon(moved));
    ++checks;
    // A bitmap copy goes through the GDI path and gets its own owner stamp.
    const unsigned bitmap_copy_args[] = {bitmap, 0, 0, 0, 0};
    const unsigned bitmap_copy = app.Api("CopyImage", bitmap_copy_args);
    assert(bitmap_copy && bitmap_copy != bitmap && Gdi::ObjectExists(bitmap_copy));
    assert(Gdi::GetObjectOwner(bitmap_copy) == (app.ProcessId() & 65535));
    ++checks;

    // --- OpenProcess ------------------------------------------------------
    app.last_error = 4242;
    const unsigned missing[] = {0x1fffff, 0, 0x7fff0000u};
    assert(app.Api("OpenProcess", missing) == 0 && app.last_error == 4242);
    ++checks;
    const unsigned self[] = {0x1fffff, 0, app.ProcessId()};
    assert(app.Api("OpenProcess", self) == 0x7fffffffu);
    ++checks;

    // --- SearchPathA ------------------------------------------------------
    const char seed[] = "shell blockers";
    assert(!Files::System().Seed("C:\\WINDOWS\\SYSTEM\\SHELLBLK.TXT", seed, sizeof seed - 1));
    const unsigned name = app.String("SHELLBLK.TXT");
    const unsigned buffer = app.Allocate(300), part = app.Allocate(4);
    const unsigned find[] = {0, name, 0, 260, buffer, part};
    const unsigned length = app.Api("SearchPathA", find);
    assert(length == std::strlen("C:\\WINDOWS\\SYSTEM\\SHELLBLK.TXT"));
    assert(!std::strcmp(app.ReadString(buffer), "C:\\WINDOWS\\SYSTEM\\SHELLBLK.TXT"));
    assert(!std::strcmp(app.ReadString(app.Read32(part)), "SHELLBLK.TXT"));
    ++checks;
    // Too small a buffer reports the size including the terminator.
    const unsigned tight[] = {0, name, 0, 4, buffer, 0};
    assert(app.Api("SearchPathA", tight) == length + 1);
    ++checks;
    // The extension argument is appended only to an extensionless name.
    const unsigned stem = app.String("SHELLBLK"), extension = app.String(".TXT");
    const unsigned with_extension[] = {0, stem, extension, 260, buffer, 0};
    assert(app.Api("SearchPathA", with_extension) == length);
    ++checks;
    const unsigned bad_extension = app.String("TXT");
    const unsigned rejected[] = {0, stem, bad_extension, 260, buffer, 0};
    app.last_error = 0;
    assert(app.Api("SearchPathA", rejected) == 0 && app.last_error == 87);
    ++checks;
    // An explicit path replaces the default order.
    const unsigned list = app.String("C:\\WINDOWS\\TEMP;C:\\WINDOWS\\SYSTEM");
    const unsigned explicit_path[] = {list, name, 0, 260, buffer, 0};
    assert(app.Api("SearchPathA", explicit_path) == length);
    const unsigned wrong_path[] = {app.String("C:\\WINDOWS\\TEMP"), name, 0, 260, buffer, 0};
    app.last_error = 0;
    assert(app.Api("SearchPathA", wrong_path) == 0 && app.last_error == 2);
    ++checks;

    // SearchPathA accepts directory attribute results too. InstallShield
    // uses an explicit path to locate the extracted Disk1 directory.
    assert(!Files::System().MakeDirectory("C:\\WINDOWS\\TEMP\\SPDIR"));
    assert(!Files::System().MakeDirectory("C:\\WINDOWS\\TEMP\\SPDIR\\Disk1"));
    const char *directory = "C:\\WINDOWS\\TEMP\\SPDIR\\Disk1";
    const unsigned directory_length = std::strlen(directory);
    const unsigned directory_path = app.String("C:\\WINDOWS\\TEMP\\SPDIR");
    const unsigned directory_name = app.String("Disk1");
    const unsigned directory_find[] = {directory_path, directory_name, 0, 260, buffer, part};
    assert(app.Api("SearchPathA", directory_find) == directory_length);
    assert(!std::strcmp(app.ReadString(buffer), directory));
    assert(!std::strcmp(app.ReadString(app.Read32(part)), "Disk1"));
    const unsigned qualified_directory[] = {0, app.String(directory), 0, 260, buffer, part};
    assert(app.Api("SearchPathA", qualified_directory) == directory_length);
    const unsigned short_directory[] = {directory_path, directory_name, 0, 4, buffer, part};
    app.Store(buffer, "sentinel", 9); app.Write32(part, 0x12345678);
    assert(app.Api("SearchPathA", short_directory) == directory_length + 1);
    assert(!std::strcmp(app.ReadString(buffer), "sentinel") && app.Read32(part) == 0x12345678);
    const unsigned absent_directory[] = {directory_path, app.String("Absent"), 0, 260, buffer, part};
    assert(app.Api("SearchPathA", absent_directory) == 0 && app.last_error == 2);
    checks += 4;

    // the Win98 API masks NT-only attribute bits before DOS 7143h.
    const unsigned attribute_file = app.String("C:\\WINDOWS\\SYSTEM\\SHELLBLK.TXT");
    for (unsigned attributes : {0x2020u, 0x80002021u, 0x80u}) {
        const unsigned set[] = {attribute_file, attributes};
        assert(app.Api("SetFileAttributesA", set) == 1);
        const unsigned get[] = {attribute_file};
        const unsigned expected = (attributes & 0x27) ? attributes & 0x27 : 0x80;
        assert(app.Api("GetFileAttributesA", get) == expected);
        ++checks;
    }
    const unsigned missing_attributes[] = {app.String("C:\\WINDOWS\\TEMP\\SPDIR\\missing.txt"), 0x2020};
    assert(app.Api("SetFileAttributesA", missing_attributes) == 0 && app.last_error == 2);
    ++checks;

    // --- LCMapStringA -----------------------------------------------------
    const unsigned source = app.String("Shell Blockers"), destination = app.Allocate(64);
    const unsigned upper[] = {0x409, Win98LcMap::Uppercase, source, ~0u, destination, 64};
    const unsigned mapped = app.Api("LCMapStringA", upper);
    assert(mapped == std::strlen("Shell Blockers") + 1);
    assert(!std::strcmp(app.ReadString(destination), "SHELL BLOCKERS"));
    ++checks;
    const unsigned lower[] = {0x409, Win98LcMap::Lowercase, source, ~0u, destination, 64};
    assert(app.Api("LCMapStringA", lower) == mapped);
    assert(!std::strcmp(app.ReadString(destination), "shell blockers"));
    ++checks;
    // The general route: OLEAUT32 asks for LCMAP_LOWERCASE |
    // LCMAP_HALFWIDTH; CP 1252 has no width table, so it is the lowercase copy.
    const unsigned half[] = {0, Win98LcMap::Lowercase | 0x400000, source, ~0u, destination, 64};
    app.Store(destination, "XXXXXXXXXXXXXXXXXXXX", 20);
    assert(app.Api("LCMapStringA", half) == mapped);
    assert(!std::strcmp(app.ReadString(destination), "shell blockers"));
    const unsigned half_measure[] = {0, Win98LcMap::Uppercase | 0x400000, source, 5, 0, 0};
    assert(app.Api("LCMapStringA", half_measure) == 5);
    ++checks;
    // A zero capacity asks for the required length and writes nothing.
    const unsigned measure[] = {0x409, Win98LcMap::Uppercase, source, ~0u, 0, 0};
    assert(app.Api("LCMapStringA", measure) == mapped);
    assert(!std::strcmp(app.ReadString(destination), "shell blockers"));
    ++checks;
    app.last_error = 0;
    const unsigned no_source[] = {0x409, Win98LcMap::Uppercase, 0, ~0u, destination, 64};
    assert(app.Api("LCMapStringA", no_source) == 0 && app.last_error == 87);
    ++checks;
    app.last_error = 0;
    const unsigned both_cases[] = {0x409, Win98LcMap::Uppercase | Win98LcMap::Lowercase, source,
                                   ~0u, destination, 64};
    assert(app.Api("LCMapStringA", both_cases) == 0 && app.last_error == 0x3ec);
    ++checks;
    app.last_error = 0;
    const unsigned unknown_flag[] = {0x409, 0x08, source, ~0u, destination, 64};
    assert(app.Api("LCMapStringA", unknown_flag) == 0 && app.last_error == 0x3ec);
    ++checks;
    app.last_error = 0;
    const unsigned needs_sortkey[] = {0x409, 0x1000, source, ~0u, destination, 64};
    assert(app.Api("LCMapStringA", needs_sortkey) == 0 && app.last_error == 0x3ec);
    ++checks;

    // --- Win16 module gateway --------------------------------------------
    const unsigned library[] = {app.String("SHELL.DLL")};
    assert(app.Api("ordinal_35", library) == 2);
    const unsigned procedure[] = {2, app.String("SHELLABOUT")};
    assert(app.Api("ordinal_37", procedure) == 0);
    const unsigned release[] = {2};
    assert(app.Api("ordinal_36", release) == 0);
    assert(app.win16_module_requests == 3);
    ++checks;

    assert(app.Status() != X86::Application::Failed);
    std::printf("SHELL BLOCKERS NATIVE PASS checks=%u calls=%u\n", checks, app.ApiCalls());
}
