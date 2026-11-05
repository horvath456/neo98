// SPDX-License-Identifier: GPL-2.0-or-later
// The guest side of the drawn cursor: the OEM table USER loads at startup, the
// raster of one cursor into the compositor surface, DefWindowProc's
// WM_SETCURSOR choice, and the IDC_APPSTARTING feedback after a launch.
#include "x86_app.h"
#include "win98_cursor.h"
namespace X86 {
const unsigned char *Application::HostDesktopIconMask(unsigned now, int width, int height)
{
    const unsigned shell = User::RegisteredShellWindow();
    auto *owner = shell ? static_cast<Application *>(User::ProcessContextOf(User::WindowProcess(shell))) : nullptr;
    if (!owner) return nullptr;
    if (!owner->host_desktop_pending && (!owner->host_desktop_mask || now - owner->host_desktop_time >= 250)) {
        owner->host_desktop_time = now;
        owner->host_desktop_pending = 1;
        const unsigned view = User::FindWindowEx(shell, 0, "SHELLDLL_DefView", nullptr);
        const unsigned list = view ? User::FindWindowEx(view, 0, "SysListView32", nullptr) : 0;
        User::PostMessage(list ? list : shell, 0, 0, 0);
    }
    return owner->host_desktop_width == width && owner->host_desktop_height == height ? owner->host_desktop_mask : nullptr;
}
void Application::ServeHostDesktopIcons()
{
    if (host_desktop_pending != 1) return;
    const unsigned shell = User::RegisteredShellWindow();
    if (!shell || User::WindowProcess(shell) != process_id) { host_desktop_pending = false; return; }
    const unsigned view = User::FindWindowEx(shell, 0, "SHELLDLL_DefView", nullptr);
    const unsigned list = view ? User::FindWindowEx(view, 0, "SysListView32", nullptr) : 0;
    // Explorer has several UI threads which can wait for one another during
    // startup. Sampling must run on the ListView's own thread, never introduce
    // synchronous sends from its other message loops.
    if (list && User::WindowThread(list) != threads[current_thread].id) return;
    host_desktop_pending = false;
    int width = 0, height = 0, depth = 0;
    User::ScreenMode(width, height, depth);
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) return;
    host_desktop_pending = 2;
    const unsigned bytes = unsigned(width) * unsigned(height);
    const unsigned error = last_error;
    auto *mask = new unsigned char[bytes]{};
    const unsigned module = FindModule("COMCTL32.DLL");
    const unsigned get_icon = ModuleProcedure(module, "ImageList_GetIcon", 0);
    const unsigned scratch = list && get_icon ? Allocate(64) : 0;
    if (scratch) {
        const unsigned images = Send(list, 0x1002, 0, 0); // LVM_GETIMAGELIST, LVSIL_NORMAL
        const unsigned count = Send(list, 0x1004, 0, 0); // LVM_GETITEMCOUNT
        Gdi::RECT client{}; User::ClientScreenRect(list, &client);
        auto fill = [&](const Gdi::RECT &r) {
            for (int y = r.top; y < r.bottom && y < height; ++y)
                for (int x = r.left; x < r.right && x < width; ++x)
                    if (x >= 0 && y >= 0) mask[unsigned(y) * width + x] = 255;
        };
        auto rectangle = [&](unsigned index, unsigned part, Gdi::RECT &r) {
            Write32(scratch + 48, part);
            if (!Send(list, 0x100e, index, scratch + 48) || !Read(scratch + 48, &r, sizeof r)) return false;
            r.left += client.left; r.right += client.left;
            r.top += client.top; r.bottom += client.top;
            return true;
        };
        // Query the real control, including callback image indices and overlay
        // masks. No COMCTL32 private layout or desktop colour is inferred here.
        for (unsigned i = 0; images && i < count && i < 4096 && state != Failed; ++i) {
            unsigned item[10] = {0x0a, i, 0, 0, 0x0f00}; // LVIF_IMAGE | LVIF_STATE
            Store(scratch, item, sizeof item);
            if (!Send(list, 0x1005, 0, scratch) || !Read(scratch, item, sizeof item)) continue;
            Gdi::RECT r{};
            if (rectangle(i, 1, r)) { // LVIR_ICON
                const unsigned args[] = {images, item[7], 1u | (item[3] & 0x0f00)};
                const unsigned handle = Call(get_icon, args, 3);
                auto *icon = FindIcon(handle);
                if (icon && icon->data && icon->data->mask && icon->data->color) {
                    const auto &image = *icon->data;
                    // LVIR_ICON contains the padded icon cell, not just the
                    // image. COMCTL32 centers the image in both dimensions.
                    const int left = r.left + (r.right - r.left - int(image.width)) / 2;
                    const int top = r.top + (r.bottom - r.top - int(image.height)) / 2;
                    for (unsigned y = 0; y < image.height; ++y)
                        for (unsigned x = 0; x < image.width; ++x) {
                            const unsigned at = y * image.width + x;
                            const int dx = left + int(x), dy = top + int(y);
                            if (dx >= 0 && dy >= 0 && dx < width && dy < height &&
                                (!(image.mask[at] & 0xffffffu) || (image.color[at] & 0xffffffu)))
                                mask[unsigned(dy) * width + dx] = 255;
                        }
                }
                if (handle) ImageApi("DestroyIcon", &handle);
            }
            if (rectangle(i, 2, r)) fill(r); // original opaque label background
        }
        Free(scratch);
    }
    delete[] host_desktop_mask;
    host_desktop_mask = mask;
    host_desktop_bytes = bytes;
    host_desktop_width = width; host_desktop_height = height;
    last_error = error;
    host_desktop_pending = 0;
}
unsigned Application::CurrentCursor()
{
    auto &current=threads[current_thread].cursor_handle;
    if(current==~0u)
    {
        // USER's queue field starts at IDC_WAIT, independently of the cursor
        // another thread selected. An explicit NULL remains NULL thereafter.
        const unsigned error=last_error,args[]={0,Win98Cursor::Wait};
        const unsigned handle=ImageApi("LoadCursorA",args);
        if(handle)current=handle;
        last_error=error;
        if(!handle)return 0;
    }
    return current;
}
// The handle of one OEM cursor. Zero before InitializeImageResources ran.
unsigned Application::SystemCursor(unsigned identifier)
{
    const unsigned index = Win98Cursor::Index(identifier);
    return index < 16 ? system_cursors[index] : 0;
}
// The compositor's top layer. The image carries the original AND and XOR
// planes; Draw's mode 3 applies them in that order, which is what USER's own
// blit does. The hotspot moves the image so that the point the user aims with
// lands on the pixel the guest asked for.
bool Application::DrawCursorShape(Gdi::HDC dc, int x, int y, unsigned cursor)
{
    auto *record = FindIcon(cursor);
    if (!record || !record->data) return false;
    return record->data->Draw(dc, x - int(record->hotspot_x), y - int(record->hotspot_y), 3);
}
bool Application::HostPointer(unsigned &identifier, unsigned &handle, int &width, int &height, int &hot_x, int &hot_y,
                              unsigned *argb, unsigned capacity)
{
    identifier = handle = 0;
    width = height = hot_x = hot_y = 0;
    auto *owner = static_cast<Application *>(User::PointerOwner());
    const unsigned cursor = User::PointerShape();
    if (!owner || !cursor || !User::PointerVisible()) return false;
    handle = cursor;
    for (unsigned i = 0; i < Win98Cursor::Count; ++i)
        if (owner->system_cursors[i] == cursor) { identifier = Win98Cursor::Identifier(i); break; }
    auto *record = owner->FindIcon(cursor);
    if (!record || !record->data || !record->data->color || !record->data->mask) return identifier != 0;
    const auto &image = *record->data;
    if (!image.width || !image.height || image.width * image.height > capacity) return identifier != 0;
    width = int(image.width);
    height = int(image.height);
    hot_x = int(record->hotspot_x);
    hot_y = int(record->hotspot_y);
    for (unsigned i = 0; i < image.width * image.height; ++i)
    {
        // The AND plane keeps the screen where it is set; the XOR plane then
        // inverts it where it is set as well.
        const unsigned keep = image.mask[i] & 0xffffffu, colour = image.color[i] & 0xffffffu;
        argb[i] = !keep ? 0xff000000u | colour : colour ? 0xff000000u : 0u;
    }
    return true;
}
bool Application::HostWindowProgram(unsigned hwnd, unsigned &process, char *program, unsigned program_size, int &width,
                                    int &height, unsigned *argb, unsigned capacity)
{
    width = height = 0;
    process = User::IsWindow(hwnd) ? User::WindowProcess(hwnd) : 0;
    auto *owner = process ? static_cast<Application *>(User::ProcessContextOf(process)) : nullptr;
    if (!owner) return false;
    if (program && program_size)
    {
        unsigned n = 0;
        for (; n + 1 < program_size && owner->program_name[n]; ++n) program[n] = owner->program_name[n];
        program[n] = 0;
    }
    // The order the shell takes for a button of the taskbar, large first.
    unsigned icon = User::WindowIcon(hwnd, true);
    if (!icon) icon = User::ClassAccess(hwnd, -14, 0, 2); // GCL_HICON
    if (!icon) icon = User::WindowIcon(hwnd, false);
    if (!icon) icon = User::ClassAccess(hwnd, -34, 0, 2); // GCL_HICONSM
    // No window icon and no class icon: the first icon of the program file,
    // the one Explorer shows for it. The process loads it itself on its next
    // turn through the message queue; until then the program is not reported,
    // so the host sees it with its icon the first time. A process that does
    // not come by within a few calls is reported without.
    if (!icon)
    {
        if (!owner->host_icon_tried)
        {
            // A thread that waits in GetMessage comes by after a WM_NULL.
            if (!owner->host_icon_wanted) User::PostMessage(hwnd, 0, 0, 0);
            owner->host_icon_wanted = true;
            if (++owner->host_icon_asked < 12) return false;
            owner->host_icon_tried = true;
        }
        icon = owner->host_program_icon;
    }
    auto *record = icon ? owner->FindIcon(icon) : nullptr;
    if (!record || !record->data || !record->data->color || !record->data->mask) return true;
    const auto &image = *record->data;
    if (!image.width || !image.height || image.width * image.height > capacity) return true;
    width = int(image.width);
    height = int(image.height);
    for (unsigned i = 0; i < image.width * image.height; ++i)
        argb[i] = (image.mask[i] & 0xffffffu) ? 0u : 0xff000000u | (image.color[i] & 0xffffffu);
    return true;
}
bool Application::HostWindowProgramPath(unsigned hwnd, char *path, unsigned path_size)
{
    const unsigned process = User::IsWindow(hwnd) ? User::WindowProcess(hwnd) : 0;
    auto *owner = process ? static_cast<Application *>(User::ProcessContextOf(process)) : nullptr;
    if (!owner || !path || !path_size || !owner->modules[0].path[0]) return false;
    unsigned n = 0;
    for (; n + 1 < path_size && owner->modules[0].path[n]; ++n) path[n] = owner->modules[0].path[n];
    path[n] = 0;
    return true;
}
// DefWindowProc WM_SETCURSOR. The parent gets first refusal, then the frame hit
// code picks a sizing arrow, and the client area falls back to the class
// cursor. A window that answers nonzero has taken the decision itself.
unsigned Application::DefaultSetCursor(unsigned hwnd, unsigned wparam, unsigned lparam)
{
    const unsigned hit = lparam & 0xffff;
    if ((User::Style(hwnd) & 0x40000000) && User::Parent(hwnd))
        if (CallWindow(User::Parent(hwnd), 0x20, wparam, lparam)) return 1;
    if (!User::IsWindow(hwnd)) return 1;
    unsigned identifier = Win98Cursor::ForHit(hit);
    unsigned cursor = 0;
    if (!identifier)
    {
        cursor = User::ClassAccess(hwnd, -12, 0, 2);   // GCL_HCURSOR
        // The system classes here carry no class cursor; USER gives
        // EDIT IDC_IBEAM and the others IDC_ARROW. A superclass of EDIT
        // copies that cursor, so every edit control shows the text cursor.
        if (!cursor) identifier = User::IsEdit(hwnd) ? Win98Cursor::IBeam : Win98Cursor::Arrow;
    }
    if (identifier) cursor = SystemCursor(identifier);
    if (!cursor) return 0;
    this->cursor = cursor;
    threads[current_thread].cursor_handle = cursor;
    User::SetPointerShape(cursor);
    return 0;
}
// KERNEL32 decides the launch feedback from the new
// process's own STARTUPINFO: a normal GUI process gets it unless it asked for
// STARTF_FORCEOFFFEEDBACK. USER then shows IDC_APPSTARTING until the child
// pumps its queue. The USER-side timeout constant is not confirmed from the
// original; this uses the ten seconds KERNEL32's own WinExec helper waits with.
void Application::BeginLaunchFeedback(unsigned slot, unsigned startup_flags)
{
    if (startup_flags & 0x80) return;               // STARTF_FORCEOFFFEEDBACK
    const unsigned starting = SystemCursor(Win98Cursor::AppStarting);
    if (!starting) return;
    feedback_process = slot + 1;
    feedback_deadline = unsigned(Now() / 1000) + 10000;
    cursor = starting;
    User::SetPointerShape(starting);
}
void Application::UpdateLaunchFeedback()
{
    if (!feedback_process) return;
    bool finished = false;
    auto *child = ChildApplication(feedback_process - 1, finished);
    const bool idle = !child || finished || child->input_idle;
    if (!idle && (int)(unsigned(Now() / 1000) - feedback_deadline) < 0) return;
    feedback_process = 0;
    feedback_deadline = 0;
    const unsigned arrow = SystemCursor(Win98Cursor::Arrow);
    if (arrow && cursor == SystemCursor(Win98Cursor::AppStarting))
    {
        cursor = arrow;
        User::SetPointerShape(arrow);
    }
}
// A process that ends while its table holds the pointer's shape hands the
// shape to a living process (User::HandPointer). An OEM cursor becomes
// the same OEM cursor of the heir; a cursor of the process's own becomes the
// heir's arrow until the next WM_SETCURSOR asks the window under the pointer.
void Application::HandOffPointer()
{
    if (User::PointerOwner() != this) return;
    auto *heir = static_cast<Application *>(User::PointerHeir(this));
    if (!heir) { User::ForgetPointerOwner(this); return; }
    const unsigned shape = User::PointerShape();
    unsigned index = Win98Cursor::Index(Win98Cursor::Arrow);
    for (unsigned i = 0; i < 16; ++i)
        if (shape && system_cursors[i] == shape) { index = i; break; }
    unsigned cursor = index < 16 ? heir->system_cursors[index] : 0;
    if (!cursor) cursor = heir->SystemCursor(Win98Cursor::Arrow);
    heir->cursor = cursor;
    User::HandPointer(heir, cursor);
}
} // namespace X86
