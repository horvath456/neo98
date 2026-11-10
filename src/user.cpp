// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_draw_text_gdi.h"
#include "user.h"
#include <new>
#include "gdi_truetype.h"
#include "../sdk/lib/neo98_touch_abi.h"
#include "win98_menu_tracking.h"
#include "win98_menu_bar_info.h"
#include "win98_menu_ownerdraw.h"
#include <stdio.h>
#include <stdlib.h>
#include <new>
#include "render_profile.h"
#define USER_PROFILE(stage) RenderProfile::Scope user_profile_scope(RenderProfile::stage)
#include "user_draw.h"
#include "rect_region.h"
#include "input_state.h"
#include "win98_keyboard_layouts.h"
#include "win98_focus.h"
#include "win98_mouse_capture.h"
#include "user_settings.h"
#include "win98_nonclient.h"
#include "win98_frame_hit.h"
#include "win98_cursor.h"
#include "win98_size_move.h"
#include "win98_drag_rect.h"
#include "win98_scroll_track.h"
#include "win98_frame_control.h"
#include "win98_caption_buttons.h"
#include "win98_button_layout.h"
#include "win98_draw_caption.h"
#include "win98_menu_bar_draw.h"
#include "win98_oem_bitmaps.h"
#include "win98_shell_registration.h"
#include "win98_class_store.h"
#include "win98_window_text.h"
#include "win98_class_defaults.h"
#include "win98_minmax.h"
#include "win98_broadcast.h"
#include "win98_owner_zorder.h"
#include "win98_hotkey.h"
#include "win98_wake_mask.h"
#include "win98_display_modes.h"

namespace User {
bool PopupRouteInput(const InputPacket&);void PaintPopupMenus();void ResetPopupMenus();void PopupWindowDestroyed(HWND);
unsigned ServeControlPaints();
unsigned ServeDragIcons();
bool PendingPopupPaint();unsigned ServePopupPaints();
void FreeWindowHotKeys(HWND);void ResetHotKeys();
namespace {
// Win98 keeps no menu table: a menu is a 46-byte object in USER's local heap
// and its handle is that object's 16-bit offset, so any value but zero can
// turn up. This port needs a table, but the table must not be the limit a
// program runs into. A menu handle is therefore
// W(generation * MenuSlots + slot + 1): the low ten bits carry the slot plus
// one, so the handle is never zero, and the upper six carry a generation
// count, which makes the handle of a freed menu fail to resolve instead of
// naming the menu that took its place. That gives 1023 slots and the
// generations 1 to 63, and every handle stays inside 16 bits. Slots are
// handed out round robin, so a freed slot comes back only after the whole
// table has been walked, and its handle then differs in the generation.
// The shell of a full installation holds around 42 menus at rest; WordPad
// and the Control Panel together need several times that.
constexpr unsigned MenuSlots = 1024, MenuGenerations = 65536 / MenuSlots;
constexpr unsigned MaxWindows = WindowCapacity, MaxMenus = MenuSlots - 1, QueueSize = 256;
static_assert(MaxMenus < MenuSlots, "a menu slot plus one has to fit the slot bits");
struct Window {
    unsigned char *host_bits = nullptr;
    unsigned host_bytes = 0;
    int host_width = 0, host_height = 0, host_x = 0, host_y = 0;
    Gdi::HDC host_dc = 0;
    bool used = false, visible = false, enabled = true, modified = false, redraw = true;
    // USER marks an overlapped window at creation: its first WM_SIZE
    // and WM_MOVE come from ShowWindow, not from CreateWindow.
    bool send_size_move = false;
    unsigned icon_small = 0, icon_large = 0; // WM_SETICON; the caption asks these before the class icon
    // USER asks the window itself with WM_QUERYDRAGICON when neither
    // WM_SETICON nor the class left an icon. The answer belongs to the thread
    // that owns the window, so the composition reads it from here and never
    // calls back into the guest.
    unsigned icon_drag = 0; bool icon_drag_asked = false;
    unsigned instance = 0; // hInstance of CreateWindow, GWL_HINSTANCE / GWW_HINSTANCE
    bool double_clicks = false; // CS_DBLCLKS: USER turns the second press into WM_xBUTTONDBLCLK
    bool button = false, label = false, pressed = false;
    bool button_tracking = false;
    // BUTTON's BM_SETIMAGE state: the handle and the IMAGE_ kind it was set with.
    // A STATIC of the kind SS_ICON keeps its icon here too, with the size the
    // icon header named when it was set.
    unsigned image = 0, image_type = 0; int image_width = 0, image_height = 0;
    bool ready = false, shell_announced = false, track_mouse_leave = false, wait_paint_observed=false;
    bool track_mouse_hover=false,hover_pending=false;
    unsigned hover_time=0,hover_started=0,hover_serial=0;
    int hover_x=0,hover_y=0;
    unsigned first = 0, last = 0, next = 0, previous = 0, thread = 1, system_menu = 0;
    unsigned property_list = 0, process = 0;
    char class_name[256] = {};
    unsigned class_record=0, class_menu_owned=0, extra_size=0,userdata=0,context_help=0;
    unsigned char extra[256]={};
    unsigned common=0;
    mutable bool scroll_allocated=false;mutable Win98Scroll::Window scroll;
    unsigned check = 0, owner = 0, extended = 0, popup = 0, activation_flags = 0;
    Gdi::RECT normal = {}; bool normal_saved = false, restore_maximized = false;
    Gdi::POINT minimum_position{-1, -1}, maximum_position{-1, -1};
    bool maximum_position_saved = false;
    unsigned cbt_destroy_flags = 0;
    unsigned generation = 1, procedure = 0, style = 0, parameter = 0, menu = 0, parent = 0;
    int x = 0, y = 0, width = 0, height = 0;
    unsigned nonclient_flags = 0, menu_items = 0;
    unsigned nonclient_event_flags = 0;
    Gdi::RECT client = {}, caption = {}, menu_bar = {}, menu_item[48] = {};
    // The caption buttons USER measures, in window coordinates, plus the
    // hit code, the system command and the cell each of them carries.
    // remembers the pressed one in the window word at 38h; pressed_hit is that
    // word's live half and pressed_inside its cancel-on-leave state.
    struct CaptionButton { Gdi::RECT rect{}, cell{}; unsigned hit = 0, command = 0, state = 0; };
    CaptionButton button_slot[4]; unsigned button_slots = 0;
    unsigned pressed_hit = 0; bool pressed_inside = false;
    char title[TitleCapacity] = {};
    TextEdit *edit = nullptr;
    unsigned edit_limit=30000;
    Gdi::HFONT edit_default_font=0;
    unsigned short edit_left_margin=0, edit_right_margin=0;
    unsigned edit_wordbreak = 0;
    // Bit 0 of byte +38h of USER's multiline edit record: the control was
    // asked WM_GETDLGCODE with a MSG and so stands in a dialog.
    bool edit_in_dialog = false;
    Gdi::RECT edit_format = {}; bool edit_format_set = false;
    Gdi::HFONT font = 0;
    Gdi::HDC text_dc = 0;
    // USER: the EDIT asks its parent for brush and text colour with
    // WM_CTLCOLOREDIT or WM_CTLCOLORSTATIC. The question runs on the thread
    // of the window (ServeControlPaints); the composition only reads the
    // answer.
    bool edit_color_pending = true, edit_color_known = false;
    unsigned edit_brush = 0, edit_text = 0, edit_fill = 0xffffff;
    unsigned background = 0, erase_serial = 0;
    bool erase_pending = false;
    // USER WND+2Dh has separate send-erase (2) and unhandled-erase (4)
    // bits. The latter survives BeginPaint until another erase is requested.
    bool erase_unhandled = false;
    // Counts how often the window went from hidden to shown (ShownSerial).
    unsigned shown = 1;
    RectRegion update;
    // Window region (original WND+54h) in screen coordinates, with the window
    // origin it was stored at.
    unsigned region = 0; int region_x = 0, region_y = 0;
    // Pending native-content paint and the last painted content generation.
    // Render marks a generation mismatch; the window's owning thread handles
    // it in its message loop.
    bool needs_paint = false;
    unsigned painted_generation = 0;
    // USER's DCE entries for the window, all on the screen surface.
    // own_dc is the DC of a CS_OWNDC window. A lease names the DC it holds;
    // BeginPaint and GetDC get different ones. A paint lease keeps the update
    // region BeginPaint intersected, so a moved window's DC keeps it.
    Gdi::HDC own_dc = 0;
    // The window has already painted to the screen (CanvasInfo).
    bool drawn = false;
    // Invalidation and last painted state for content USER draws itself: the
    // frame, EDIT and SCROLLBAR (PaintWindow).
    bool native_invalid = true;
    unsigned native_signature = 0;
    // A callback-driven control paints its initial frame on its owning thread
    // during PaintControlSurface.
    bool frame_owed = false;
    bool frame_message_owed = true;
    bool frame_update_pending = false;
    // RDW_FRAME can mark client-only rectangles too. Keep that area so
    // validation can remove its paint request without losing a partial frame.
    RectRegion frame_update;
    bool internal_paint = false;
    unsigned leases = 0, lease_capacity = 0, class_style = 0;
    struct Lease { Gdi::HDC dc; int level; bool paint; RectRegion *paint_clip; bool caret_hidden; bool frame = false; bool screen_dc = false; };
    Lease *lease = nullptr;
};
using MenuItem=Win98Menu::Item;
struct Menu : Win98Menu::Menu { bool used=false,retired=false;unsigned process=0; };
unsigned char menu_generations[MaxMenus]={};unsigned menu_cursor=0;
Win98Menu::State menu_state;
Window windows[MaxWindows], desktop; Win98ClassStore::Store class_store;
bool PendingOwnerBars();
unsigned ServeOwnerBars(HWND only=0);
void PaintOwnerBarItem(const Window&,Gdi::POINT,const Gdi::RECT&);
void ReleaseOwnerBar(HWND);
void ReleaseRetiredMenu(HMENU);
void FreeMenuObject(HMENU);
constexpr HWND Desktop = 0xff; // Generation zero is never issued to application windows.
Menu menus[MaxMenus]; Message queue[QueueSize]; unsigned queued = 0;
// The one place that turns a menu handle back into a table slot. Everything
// else goes through FindMenu.
unsigned MenuSlot(unsigned h)
{
    const unsigned slot = (h & (MenuSlots - 1)) - 1;
    return h && slot < MaxMenus && menus[slot].used && menus[slot].handle == h ? slot : MaxMenus;
}
struct InputEvent { bool genuine = false; unsigned wait_observed=0; unsigned target=0,process=0; unsigned extra=0,character=0; unsigned char state[256] = {}; };
InputEvent input_events[QueueSize];
InputState input;
unsigned mouse_buttons = 0;
Win98MessageMetadata::Task standalone_metadata;
void *metadata_context=nullptr;MetadataCurrent metadata_current=nullptr;MetadataClock metadata_clock=nullptr,mouse_timer_clock=nullptr;
unsigned processed_position=0,metadata_queue_marker=0;

unsigned char *framebuffer = nullptr;
unsigned char *published_frame = nullptr;
// The host presents the desktop at one host pixel per guest pixel and sizes
// its window accordingly. Allocate enough storage for the largest supported
// mode.
int published_width = 800, published_height = 600;
unsigned frame_serial = 0;
Gdi::HDC screen = 0; Gdi::HFONT system_font = 0;
bool host_window_surfaces = false;
HWND host_pointer_window = 0;
DibEng::Bitmap HostBitmap(HWND);
Gdi::HDC HostFrameDC(HWND);
void FreeHostSurface(Window &);
// The published frame has a device context of its own, and the only thing
// that ever draws into it is the pointer overlay; see MovePointer.
Gdi::HDC published_dc = 0;
void ResetPointerOverlay();
void DropScreenLease(void *context, unsigned pid);
void *frame_icon_context = nullptr; FrameIcon frame_icon = nullptr;
void *default_icon_context = nullptr; DefaultIconQuery default_icon_query = nullptr;
void *icon_size_context = nullptr; IconSizeQuery icon_size_query = nullptr;
void *drag_icon_context = nullptr; DragIcon drag_icon = nullptr;
void *scroll_brush_context = nullptr; ScrollBrush scroll_brush = nullptr;
HWND pressed_scroll_window = 0;
HWND pressed_button_window = 0;
// A pending paint belongs to its window (Window::lease.paint). Any work it
// blocks belongs to that window's queue, not a system-wide counter.
Settings settings;
HWND focus = 0, menu_window = 0;
// Win98 keeps the capture in the input queue (queue word 18h, USER), and every Win32 thread has a queue of its own. A thread that takes
// the mouse leaves another thread's capture alone: SetCapture answers the
// caller's previous capture, GetCapture the caller's own. With one field for
// all threads, the desktop's list view took the Start menu's capture on a
// click (its drag check calls SetCapture) and the menu band, which watches
// GetCapture in its message hook, never closed on the click outside.
// One record per thread; thread 0 is the queue of native callers.
Win98Capture::Globals capture_globals;
struct CaptureSlot { unsigned thread = 0; bool used = false; Win98Capture::Queue queue; };
CaptureSlot capture_slots[64];
bool automatic_capture=false;unsigned capture_serial=0;
static Win98Capture::Queue *CaptureSlotFor(unsigned thread, bool create)
{
    CaptureSlot *spare = nullptr;
    for (auto &slot : capture_slots)
    {
        if (slot.used && slot.thread == thread) return &slot.queue;
        if (!spare && (!slot.used || (!slot.queue.capture && !slot.queue.flags))) spare = &slot;
    }
    if (!create || !spare) return nullptr;
    *spare = {thread, true, {}};
    return &spare->queue;
}
// The capture of a thread's queue, if its window still exists.
HWND QueueCapture(unsigned thread)
{
    const auto *q = CaptureSlotFor(thread, false);
    return q && q->capture && IsWindow(q->capture) ? q->capture : 0;
}
static unsigned ThreadCaptureFlags(unsigned thread)
{
    const auto *q = CaptureSlotFor(thread, false);
    return q ? q->flags : 0;
}
// A capture held by a thread of the process, the foreground queue's first.
HWND ProcessCaptureOf(unsigned pid)
{
    if (const HWND h = QueueCapture(ForegroundQueue()); h && WindowProcess(h) == pid) return h;
    for (const auto &slot : capture_slots)
        if (slot.used && slot.queue.capture && IsWindow(slot.queue.capture) && WindowProcess(slot.queue.capture) == pid)
            return slot.queue.capture;
    return 0;
}
HWND foreground = 0;
struct ProcessContext {
    unsigned pid=0,owner=0,version=0x400;void*context=nullptr;unsigned flags=0;
    ProcessContext *next=nullptr;
};
ProcessContext *processes=nullptr;
unsigned process_count=0;
// USER's foreground state: ds:0AA6 the foreground queue, 150h the queue that
// took the last key or button press, 13Ch its time, 1BEh the lock time-out
// (3A98h = 15 s in the data segment of USER 4.10.2222; the registry value
// ForegroundLockTimeout would override it, this installation has none).
unsigned foreground_queue=0,last_input_queue=0,last_input_process=0,last_input_time=0;
// The rest of USER's activation globals, among them ds:152h, the queue that
// had the foreground when its active window last changed. They belong to the
// session, not to a process.
Win98Activation::Globals activation_globals;
constexpr unsigned ForegroundLockTimeout=15000;
struct ThreadQueueSlot{unsigned thread=0;Win98Activation::Queue queue;bool pass=false;unsigned activation=0;unsigned mouse_activation=0;bool mouse_focus=false;bool leave=false;unsigned kept_focus=0;} thread_queues[256];
unsigned active_process=0,active_thread=0;
ProcessContext* Process(unsigned pid){for(auto*p=processes;p;p=p->next)if(p->pid==pid&&pid)return p;return nullptr;}

Win98Activation::Queue activation_queue;
void *edit_notify_context = nullptr;EditNotify edit_notify = nullptr;
void *edit_word_context = nullptr;EditWordBreak edit_word_break = nullptr;
void *shell_context = nullptr, *shell_hook_context = nullptr;
ShellEvent shell_event = nullptr;
Win98ShellRegistration::State shell_registration;
void *property_context = nullptr;
PropertyCleanup property_cleanup = nullptr;
HMENU open_menu = 0;

unsigned double_click_time = 500, last_click_window = 0, last_click_time = 0, last_click_message = 0x201;
int last_click_x = 0, last_click_y = 0;
int mouse_x = 0, mouse_y = 0, menu_x = 0, menu_y = 0;
HWND mouse_track_target=0;
unsigned mouse_track_hit=0;
void *control_context = nullptr; OwnerDraw owner_draw = nullptr; ControlColor control_color = nullptr;
void *guest_paint_context = nullptr; GuestPaint guest_paint = nullptr;
void *common_context=nullptr;CommonDraw common_draw=nullptr;
void *caret_context=nullptr;CaretDraw caret_draw=nullptr;CaretDestroy caret_destroy=nullptr;
void *caret_bracket_context=nullptr;CaretBracket caret_bracket=nullptr;
bool HideCaretFor(HWND h){return caret_bracket&&caret_bracket(CallbackContext(caret_bracket_context,h),h,true);}
void ShowCaretFor(HWND h){if(caret_bracket)caret_bracket(CallbackContext(caret_bracket_context,h),h,false);}
bool rendering = false;
bool serving_controls = false;
bool serving_drag_icons = false;
// Display invalidation and content generation. Content changes increment the
// generation; a window with an older generation needs painting, like
// per-window QS_PAINT. Painting itself only calls Mark, without advancing the
// generation, to avoid requesting another paint indefinitely.
struct DirtyFlag {
    bool value = true;
    unsigned generation = 1;
    DirtyFlag &operator=(bool v){ if(v) ++generation; value = v; return *this; }
    void Mark(){ value = true; }
    operator bool() const { return value; }
};
DirtyFlag dirty;
// The screen changed without requiring USER repainting, for example through
// application drawing or cursor movement.
bool screen_touched = true;
bool shift_down = false, control_down = false;
HWND locked_window = 0;
unsigned locked_process = 0, locked_thread = 0;
// Track attempted drawing into locked windows in screen coordinates. Windows
// 98 LockWindowUpdate creates a saved-pixel record and collects drawing
// bounds per DCE. On DCE release/reuse or unlock, intersect those bounds with
// the window and remove the affected area from the saved record. Ignore
// DCX_LOCKWINDOWUPDATE DCs. Unlock invalidates the accumulated area with
// RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN. If the rectangle list fills, use
// its bounding rectangle.
constexpr unsigned LockSumCapacity = 32;
Gdi::RECT lock_sum[LockSumCapacity]; unsigned lock_sums = 0;
void LockSum(const Gdi::RECT &r)
{
    if (!locked_window || r.left >= r.right || r.top >= r.bottom) return;
    if (lock_sums == LockSumCapacity) {
        auto &all = lock_sum[0];
        for (unsigned i = 1; i < lock_sums; ++i) {
            const auto &a = lock_sum[i];
            if (a.left < all.left) all.left = a.left; if (a.top < all.top) all.top = a.top;
            if (a.right > all.right) all.right = a.right; if (a.bottom > all.bottom) all.bottom = a.bottom;
        }
        lock_sums = 1;
    }
    lock_sum[lock_sums++] = r;
}
void RepaintLockedWindow(HWND);
void LockCheckAll();
void LockCheckDC(HWND, Gdi::HDC);
void LockBounds(bool enable);
void DropWindowUpdateLock(bool repaint=false)
{
    const HWND previous=locked_window;
    // Collect all DCE bounds before releasing the lock.
    if(repaint)LockCheckAll();
    locked_window = 0; locked_process = locked_thread = 0;
    LockBounds(false);
    if(repaint)RepaintLockedWindow(previous);
    lock_sums = 0;
}
bool SystemClipRect(Gdi::HDC dc,int left,int top,int right,int bottom)
{
    const Gdi::RECT r={left,top,right,bottom};
    const bool empty=right<=left || bottom<=top;
    const bool okay=Gdi::IntersectClipRegion(dc,empty?nullptr:&r,empty?0:1)!=0;
    if(!okay)dirty=true;
    return okay;
}

void *clipboard_context = nullptr; ClipboardText clipboard_text = nullptr; ClipboardDestroy clipboard_destroy = nullptr;
Window *Find(HWND);
// Win98 gives every SetClipboardData and GetClipboardData its own global
// block. Both calls below return into the guest, and the handler the guest
// runs for WM_DESTROYCLIPBOARD or WM_DRAWCLIPBOARD may take or set the
// clipboard itself; a buffer shared by the session would come back holding
// the bytes of that inner call. Each call therefore carries its own.
bool CopyClipboard(HWND hwnd, TextEdit &e, unsigned *copied = nullptr) { if(copied)*copied=0;auto*w=Find(hwnd);if(w&&w->edit&&w->edit->PasswordCharacter())return false;if(e.SelectionStart()==e.SelectionEnd())return false; char text[TextEdit::Capacity]={}; const unsigned length=e.Copy(text); if(!length)return false; if(clipboard_text && clipboard_text(CallbackContext(clipboard_context,hwnd),hwnd,true,text,length)!=length)return false; if(copied)*copied=length; return IsWindow(hwnd); }
bool ReadClipboard(HWND hwnd, char *text, unsigned size, unsigned &length) { length=0; if(clipboard_text)length=clipboard_text(CallbackContext(clipboard_context,hwnd),hwnd,false,text,size); return !hwnd || IsWindow(hwnd); }

unsigned Length(const char *s) { unsigned n = 0; if (s) while (s[n]) ++n; return n; }
void Copy(char *d, const char *s, unsigned n) { unsigned i = 0; if (s) for (; i + 1 < n && s[i]; ++i) d[i] = s[i]; if (n) d[i] = 0; }
bool Equal(const char *a, const char *b)
{
    if (!a || !b) return false;
    for (unsigned i = 0;; ++i) {
        unsigned ca = (unsigned char)a[i], cb = (unsigned char)b[i];
        if (ca >= 'a' && ca <= 'z') ca -= 32; if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return false; if (!ca) return true;
    }
}
HWND Handle(unsigned i) { return windows[i].generation << 10 | (i + 1); }
Window *Find(HWND h)
{
    if (h == Desktop) return desktop.used ? &desktop : nullptr;
    const unsigned i = (h & 1023) - 1;
    return i < MaxWindows && windows[i].used && windows[i].generation == h >> 10 ? &windows[i] : nullptr;
}
HWND Container(const Window &w) { return w.parent ? w.parent : Desktop; }
bool WithinWindow(HWND root,HWND h)
{
    if (!root || !h) return false;
    for (unsigned depth = 0; depth < MaxWindows && h; ++depth) {
        if (h == root) return true;
        if (h == Desktop) return false;
        const auto *w = Find(h);
        if (!w) return false;
        h = w->parent ? w->parent : Desktop;
    }
    return false;
}
bool Locked(HWND h){return WithinWindow(locked_window,h);}
// Unlocking with NULL invalidates the accumulated area. USER-owned frames,
// controls and desktop background remain pending during the lock and are
// drawn on the next paint pass.
void RepaintLockedWindow(HWND)
{
    const unsigned count = lock_sums;
    lock_sums = 0;
    for (unsigned i = 0; i < count; ++i) {
        const Gdi::RECT r = lock_sum[i];
        RedrawWindow(Desktop, &r, 0, 0x85, nullptr, nullptr);
    }
    dirty=true;
}
void EmptyClip(Gdi::HDC dc)
{
    if (dc) Gdi::IntersectClipRegion(dc, nullptr, 0);
}
void GeometryChanged();
void Unlink(HWND h, Window &w)
{
    GeometryChanged();
    if (auto *parent = Find(Container(w)))
    {
        if (parent->first == h) parent->first = w.next;
        if (parent->last == h) parent->last = w.previous;
    }
    if (auto *previous = Find(w.previous)) previous->next = w.next;
    if (auto *next = Find(w.next)) next->previous = w.previous;
    w.previous = w.next = 0;
}
void LinkAfter(HWND h, Window &w, HWND after)
{
    GeometryChanged();
    auto *parent = Find(Container(w));
    if (!parent) return;
    auto *previous = Find(after);
    w.previous = after;
    w.next = previous ? previous->next : parent->first;
    if (auto *next = Find(w.next)) next->previous = h;
    else parent->last = h;
    if (previous) previous->next = h;
    else parent->first = h;
}
// Find the top-level ancestor: a child without WS_POPUP follows its parent.
// Windows 98 uses this result as the owner when creating a window.
HWND TopLevelOwner(HWND handle)
{
    for (unsigned steps = 0; handle && handle != Desktop && steps <= MaxWindows; ++steps)
    {
        const auto *w = Find(handle);
        if (!w)
            return 0;
        if ((w->style & 0xc0000000) != 0x40000000)
            return handle;
        handle = w->parent;
    }
    return 0;
}
// Inherit WS_EX_TOPMOST from the owner. This keeps Quick Launch tooltips
// within the topmost band so FrontPosition does not place newly created
// windows above the taskbar.
unsigned OwnerTopmost(const Window &w)
{
    const auto *owner = Find(TopLevelOwner(w.owner));
    return owner && (owner->extended & 8) ? 8u : 0u;
}
HWND FrontPosition(const Window &w)
{
    if (Container(w) != Desktop || (w.extended & 8)) return 0;
    HWND previous = 0;
    for (HWND h = desktop.first; h;)
    {
        const auto *candidate = Find(h);
        if (!candidate || !(candidate->extended & 8)) break;
        previous = h;
        h = candidate->next;
    }
    return previous;
}
void NotifyShell(unsigned code, HWND hwnd, unsigned parameter = 0)
{
    // USER keeps one shell hook for the whole system: the process that
    // registered it hears about every window, not only its own.
    if (shell_event) shell_event(shell_hook_context ? shell_hook_context : CallbackContext(shell_context,hwnd), code, hwnd, parameter);
}
static bool shell_fullscreen = false;
static bool ShellFullscreen(HWND h)
{
    Gdi::RECT display = {};
    GetWindowRect(Desktop, &display);
    // USER compares the proposed window rectangle with the display
    // rectangle. USER update its fullscreen count;
    // Windows 98 passes a nonzero lParam to HSHELL_WINDOWACTIVATED while that
    // count is nonzero. SHELL turns this into HSHELL_RUDEAPPACTIVATED.
    // The original count includes the owner behind its active dialog.
    for (unsigned n = 0; h && n < MaxWindows; ++n)
    {
        const auto *w = Find(h);
        if (!w) break;
        if (w->ready && w->visible && !w->parent &&
            !(w->style & (0x40000000u | 0x20000000u)) && !(w->extended & 0x80))
        {
            Gdi::RECT window = {};
            GetWindowRect(h, &window);
            if (window.left <= display.left && window.top <= display.top &&
                window.right >= display.right && window.bottom >= display.bottom) return true;
        }
        h = w->owner;
    }
    return false;
}
static void NotifyShellActivation(HWND h)
{
    const bool fullscreen = ShellFullscreen(h);
    shell_fullscreen = fullscreen;
    NotifyShell(4, h, fullscreen ? 1 : 0);
}
void ShellVisibility(HWND hwnd, Window &w)
{
    if (!w.visible) TouchCancelWindow(hwnd);
    if (!w.visible && foreground == hwnd) foreground = 0;
    // Shown again while it is still the active window of the foreground queue
    // (nothing took over when it was hidden), it is the visible foreground again.
    else if (w.visible && !foreground && !w.parent && activation_queue.active == hwnd) foreground = hwnd;
    // Show a visible parentless window in the taskbar if it has
    // WS_EX_APPWINDOW, or if it has neither an owner nor WS_EX_TOOLWINDOW. An
    // explicit WS_EX_APPWINDOW request also applies to owned windows.
    const bool top = w.ready && w.visible && !w.parent && !(w.style & 0x40000000);
    const bool visible = top && ((w.extended & 0x40000) ||
                                 (!w.owner && !(w.extended & 0x80)));
    if (w.shell_announced == visible) return;
    w.shell_announced = visible;
    NotifyShell(visible ? 1 : 2, hwnd);
}
Menu *FindMenu(HMENU h) { const unsigned slot=MenuSlot(h);return slot<MaxMenus?&menus[slot]:nullptr; }
unsigned MenuFlags(const MenuItem&i){return i.type|i.state;}
const char *MenuText(const MenuItem&i){return i.text?reinterpret_cast<const char *>(i.text):"";}
int ClientTop(const Window &w) { return w.client.top; }
int ClientLeft(const Window &w) { return w.client.left; }
Gdi::RECT Client(const Window &w)
{
    return {0, 0, w.client.right - w.client.left, w.client.bottom - w.client.top};
}
void Layout(HWND h);
bool Custom(const Window &w) { return &w == &desktop || (w.procedure && !w.edit && !w.button && !w.label && !w.common); }
// Controls USER draws through guest callbacks: buttons, labels and common
// controls, except scrollbars. Draw them on their owning thread.
bool ControlContent(const Window &w) { return w.button || w.label || (w.common && w.common != 10); }
// A window procedure of the program instead of the port's class procedure
// (the tokens at 71000000h, x86_app.h NativeProcedure).
bool GuestProcedure(const Window &w) { return w.procedure && (w.procedure & 0xffff0000u) != 0x71000000u; }
Gdi::POINT Origin(const Window &w,unsigned depth=0);
void FreeWindowDCs(Window &w);
// Paint tracing is enabled with NEO98_TRACE_PAINT.
#if defined(__APPLE__) || defined(__linux__)
static const bool trace_paint = getenv("NEO98_TRACE_PAINT") != nullptr;
#define USER_TRACE(...) do { if (trace_paint) fprintf(stderr, __VA_ARGS__); } while (0)
#else
// DrawingChanged asks the flag by name.
static const bool trace_paint = false;
#define USER_TRACE(...) do { } while (0)
#endif
void InvalidateOne(HWND h, const Gdi::RECT *rect, bool erase);
#include "user_window_region.inc"
#include "user_redraw_window.inc"
#include "user_visible.inc"
#include "user_dce.inc"

Gdi::POINT Origin(const Window &w, unsigned depth)
{
    Gdi::POINT p = {w.x, w.y};
    if (depth < MaxWindows) if (auto *parent = Find(w.parent)) {
        const auto base = Origin(*parent, depth + 1);
        p.x += base.x + ClientLeft(*parent); p.y += base.y + ClientTop(*parent);
    }
    return p;
}
// The Win98 EDIT formatting area depends on the border, font height and
// margins. Multiline controls keep at least one complete line, even when
// their client is shorter. The Wine edit tests exercise each border style.
int EditInset(const Window &w){return (w.extended&0x200)||(w.style&0xc00000)==0xc00000?1:(w.style&0x800000)?2:0;}
Gdi::RECT EditFormat(const Window &w){
    if(w.edit_format_set)return w.edit_format;
    const auto r=Client(w);const int inset=EditInset(w);
    const int line=w.edit?w.edit->LineHeight():16;
    const int top=r.bottom>=line+2*inset?inset:0;
    int height=r.bottom-2*top;
    if(w.style&4){height=height/line*line;if(height<line)height=line;}
    else if(height>line)height=line;
    return {inset+int(w.edit_left_margin),top,r.right-inset-int(w.edit_right_margin),top+height};
}
void EditVerticalBar(Window &w)
{
    if (!w.edit || !(w.style & 4) || !(w.style & 0x200000)) return;
    auto &e = *w.edit;
    // USER: range = line count - 1, page = visible lines,
    // position = first line, SIF_DISABLENOSCROLL keeps the requested bar.
    Win98Scroll::Info info{28,0x1f,0,e.LineCount()-1,unsigned(e.VisibleLines()),unsigned(e.FirstLine()),0};
    const auto &bar = w.scroll.bars[1];
    const unsigned page = info.page > e.LineCount() ? e.LineCount() : info.page;
    if (w.scroll_allocated && bar.minimum==0 && bar.maximum==info.maximum &&
        bar.page==page && bar.position==info.position) return;
    ScrollState(Handle(unsigned(&w-windows)),1,1,&info,1);
}
void Metrics(Window &w)
{
    if (!w.edit || !screen) return;
    Gdi::SaveDC(screen); Gdi::SelectObject(screen, w.font ? w.font : w.edit ? Gdi::GetCurrentObject(w.text_dc,6) : system_font);
    int widths[256]; Gdi::SIZE size;
    Gdi::GetCharWidth32A(screen, 0, 255, widths); Gdi::GetTextExtentPoint32A(screen, "M", 1, &size);
    // Install the new line height before deriving the format rectangle.
    w.edit->Metrics(widths,size.cy,30000,Client(w).bottom,Client(w).right);
    const auto r = EditFormat(w);
    const int width=r.right-r.left, height=r.bottom-r.top;
    // A line is not broken with ES_AUTOHSCROLL, nor in a multiline control
    // with a horizontal scroll bar: USER sets ES_AUTOHSCROLL itself when it
    // finds WS_HSCROLL (Notepad without Word Wrap asks for the bar only,

    const bool unbroken=!(w.style&4)||(w.style&0x80)||(w.style&0x00100000);
    w.edit->Metrics(widths,size.cy,unbroken?30000:width,height,width);
    Gdi::RestoreDC(screen, -1);
    EditVerticalBar(w);
}
// These three drawing helpers take an explicit DC so control content targets
// its window surface.
void Fill(Gdi::HDC dc, Gdi::RECT r, unsigned color)
{
    auto brush = Gdi::CreateSolidBrush(Gdi::RGB(color >> 16, color >> 8, color));
    Gdi::FillRect(dc, &r, brush); Gdi::DeleteObject(brush);
}
void Label(Gdi::HDC dc, int x, int y, const char *s, unsigned color = 0)
{
    Gdi::SetTextColor(dc, Gdi::RGB(color >> 16, color >> 8, color));
    Gdi::SetBkMode(dc, Gdi::TRANSPARENT); Gdi::TextOutA(dc, x, y, s, Length(s));
}
// Menu text with its accelerator prefix. The menu renderer hands DrawState
// DST_PREFIXTEXT, whose text goes through the DrawText output
// routine : the letter after '&' is underlined, "&&" is one '&'. Win98
// draws the underline always, not only after ALT. The routine has a
// 256-byte buffer; a longer text is drawn without the prefix.
void PrefixLabel(Gdi::HDC dc, int x, int y, const char *s, unsigned count, unsigned color = 0)
{
    Gdi::SetTextColor(dc, Gdi::RGB(color >> 16, color >> 8, color));
    Gdi::SetBkMode(dc, Gdi::TRANSPARENT);
    if (count && count <= 256)
    {
        unsigned ignored = 0;
        Win98DrawTextGdi::Environment prefixed{ignored, dc};
        prefixed.MetricsFor(0);
        prefixed.Draw(x, y, s, count, 0);
        if (prefixed.okay) return;
    }
    char label[257];
    unsigned n = 0;
    for (unsigned i = 0; i < count && s[i] && n < 256; ++i) if (s[i] != '&') label[n++] = s[i];
    Gdi::TextOutA(dc, x, y, label, n);
}
void Edge(Gdi::HDC dc, Gdi::RECT r, bool inset = false)
{
    Win98Draw::DrawEdge(dc,&r,inset?10:5,15);
}
void Fill(Gdi::RECT r, unsigned color){ Fill(screen, r, color); }
void Label(int x, int y, const char *s, unsigned color = 0){ Label(screen, x, y, s, color); }
void Edge(Gdi::RECT r, bool inset = false){ Edge(screen, r, inset); }
void MenuLabel(const char *in, char *out)
{
    unsigned n = 0;
    for (unsigned i = 0; in[i] && n < 95; ++i) if (in[i] != '&') out[n++] = in[i] == '\t' ? ' ' : in[i];
    out[n] = 0;
}
int MenuWidth(const MenuItem &item)
{
    char text[96]; MenuLabel(MenuText(item), text); Gdi::SIZE size;
    Gdi::GetTextExtentPoint32A(screen, text, Length(text), &size); return size.cx + 20;
}
struct NonclientEnvironment final : Win98Nonclient::Environment
{
    bool notify = false;
    unsigned short Word(unsigned offset) const override
    {
        return offset == 0x2ae ? (notify && WinEventListening() ? 1 : 0) : settings.Word(offset);
    }
    unsigned short ExpectedVersion() const override { return settings.ExpectedVersion(); }
    Win98Nonclient::WindowState ReadWindow(unsigned h) const override
    {
        const auto *w = Find(h);
        return w ? Win98Nonclient::WindowState{w->style, w->extended,
                     notify ? (w->nonclient_flags & ~15u) | w->nonclient_event_flags : w->nonclient_flags, w->menu}
                 : Win98Nonclient::WindowState{};
    }
    void WriteWindow(unsigned h, const Win98Nonclient::WindowState &state) override
    {
        if (auto *w = Find(h))
        {
            w->style = state.style;
            w->extended = state.extended;
            w->nonclient_flags = state.flags;
            if (notify) w->nonclient_event_flags = state.flags & 15u;
            w->menu = state.menu;
        }
    }
    bool MenuValid(unsigned menu) override { return FindMenu(menu) != nullptr; }
    int MenuHeight(unsigned menu, unsigned h, int width, int border, int top) override
    {
        auto *m = FindMenu(menu);
        auto *w = Find(h);
        if (!m || !w || width <= 0)
            return 0;
        // The original USER menu-bar measurement now decides row breaks, item
        // rectangles and owner-draw sizes.
        int bar_width = 0, bar_height = 0;
        if (!MeasureMenuObject(menu, h, width, bar_width, bar_height))
            return 0;
        w->menu_items = m->count < 48 ? m->count : 48;
        for (unsigned i = 0; i < w->menu_items; ++i)
        {
            const auto &item = m->items[i];
            w->menu_item[i] = {border + item.x, top + item.y,
                               border + item.x + item.width, top + item.y + item.height};
        }
        const int height = m->count ? bar_height + Word(0x2ea) : 0;
        w->menu_bar = {border, top, border + width, top + height};
        return height;
    }
    void NotifyReorder(unsigned h) override
    {
        dirty = true;
        RaiseWinEvent(0x8004, h, 0, 0, true); // USER
    }
};
// USER measures each caption button; the slot order is the one
// walks. The rectangle it returns is the hit rectangle, one border wider than
// the button on every side. The original then blits an OEM bitmap of its own
// size at the position Windows 98 returns in DX:AX. NEO98 draws the ported
// DrawFrameControl cell there instead and sizes it from the caption metrics.
void CaptionButtons(Window &w, const NonclientEnvironment &e, unsigned units)
{
    Win98CaptionButton::Metrics m;
    m.border_x = e.Word(0x2e8); m.border_y = e.Word(0x2ea);
    m.dialog_x = e.Word(0x2ec); m.dialog_y = e.Word(0x2ee);
    m.size_x = e.Word(0x31a); m.size_y = e.Word(0x31c);
    m.small_x = e.Word(0x346); m.small_y = e.Word(0x348);
    m.edge_x = e.Word(0x338); m.edge_y = e.Word(0x33a);
    const Gdi::RECT box = {0, 0, w.width, w.height};
    // A Win4 window (bit 22 of the nonclient word) without WS_SYSMENU has no
    // caption buttons at all; the hit test of the same strip
    // answers HTCAPTION there (Win98FrameHit). The buttons were drawn anyway,
    // minimize and maximize of Kaggz's start dialog among them.
    if (!(w.style & 0x80000) && (w.nonclient_flags & 0x00400000))
        return;
    // The system-menu predicate USER walks the menu objects; the native
    // frame asks the window for WS_SYSMENU instead.
    const bool close_enabled = (w.style & 0x80000) != 0;
    const unsigned hits[4] = {Win98CaptionButton::HitClose, Win98CaptionButton::HitMaxButton,
                              Win98CaptionButton::HitMinButton, Win98CaptionButton::HitHelp};
    const bool tool = (w.extended & 0x80) != 0;
    const int width = (tool ? m.small_x : m.size_x) - m.border_x * 2;
    const int height = (tool ? m.small_y : m.size_y) - m.edge_y * 2;
    // The painter draws minimize and maximize together when either
    // box is present and marks the missing one inactive (state bits 4 and
    // 8); itself lays out only a box whose style bit is set. So the
    // missing box is measured with the bit added and does nothing
    // (Calculator: minimize only).
    const unsigned boxes = w.style & 0x30000u;
    for (unsigned index = 0; index < 4 && w.button_slots < 4; ++index)
    {
        Win98CaptionButton::Result result;
        const unsigned slot = Win98CaptionButton::Slot(hits[index]);
        const unsigned own = slot == Win98CaptionButton::Minimize   ? 0x20000u
                             : slot == Win98CaptionButton::Maximize ? 0x10000u
                                                                    : 0u;
        const bool inactive = own && boxes && !(boxes & own);
        if (!Win98CaptionButton::Measure(m, box, inactive ? w.style | own : w.style, w.extended,
                                         slot, units, close_enabled, result))
            continue;
        if (inactive)
        {
            result.disabled = true;
            result.command = 0;
        }
        auto &entry = w.button_slot[w.button_slots++];
        entry.rect = result.rect;
        entry.cell = {result.x, result.y, result.x + width, result.y + height};
        entry.hit = hits[index];
        entry.command = result.command;
        entry.state = Win98CaptionButton::CaptionState(result.cell) | (result.disabled ? Win98Frame::Inactive : 0u);
    }
}
void Layout(HWND h)
{
    auto *w = Find(h);
    if (!w)
        return;
    GeometryChanged();
    const auto previous_client = w->client;
    w->menu_items = 0;
    w->menu_bar = w->caption = {};
    w->button_slots = 0;
    if (h == Desktop)
    {
        w->client = {0, 0, w->width, w->height};
        return;
    }
    NonclientEnvironment e;
    Win98Nonclient::Rect r{0, 0, w->width, w->height};
    Win98Nonclient::Calculate(e, h, r, 0);
    w->client = {r.left, r.top, r.right, r.bottom};
    if (w->nonclient_flags & 8)
    {
        const unsigned units = Win98Nonclient::BorderUnits(e, w->style, w->extended, 1, 0);
        const int bx = units * e.Word(0x2e8), by = units * e.Word(0x2ea);
        const int height = e.Word(w->extended & 0x80 ? 0x344 : 0x2e6);
        w->caption = {bx, by, w->width - bx, by + height - e.Word(0x2ea)};
        CaptionButtons(*w, e, units);
    }
    if ((w->ready || w->visible) &&
        (previous_client.left != w->client.left || previous_client.top != w->client.top ||
         previous_client.right != w->client.right || previous_client.bottom != w->client.bottom))
    {
        w->update.Clear();
        InvalidateRect(h, nullptr, true);
        dirty = true;
    }
}
void NormalizeEdge(Window &w)
{
    if (Win98Nonclient::HasWindowEdge(w.style, w.extended, bool(w.nonclient_flags & 0x00400000)))
        w.extended |= 0x100;
    else
        w.extended &= ~0x100u;
}
// A changed menu changes nothing on the screen by itself in Win98; the bar is
// drawn again with the frame. DrawMenuBar is SetWindowPos(hwnd, 0, 0, 0, 0, 0,
// SWP_NOSIZE | SWP_NOMOVE) for a window that is not a child (USER over
// into the SetWindowPos export ), so the client area keeps its
// pixels unless the bar changed its height and with it the client rectangle.
// Before, every menu change erased the whole client area of every window with
// a menu; a program that draws outside WM_PAINT and only calls DrawMenuBar,
// such as MahJong 2000's board, lost its picture.
void RelayoutMenus()
{
    for (unsigned i = 0; i < MaxWindows; ++i)
        if (windows[i].used && !(windows[i].style & 0x40000000) && windows[i].menu)
        {
            const Gdi::RECT before = windows[i].client;
            const int width = windows[i].width, height = windows[i].height;
            Layout(Handle(i));
            const Gdi::RECT &after = windows[i].client;
            if (before.left == after.left && before.top == after.top && before.right == after.right &&
                before.bottom == after.bottom && width == windows[i].width && height == windows[i].height)
                continue;
            windows[i].update.Clear();
            InvalidateRect(Handle(i), nullptr, true);
        }
    dirty = true;
}
Gdi::RECT At(const Gdi::RECT &r, Gdi::POINT p)
{
    return {r.left + p.x, r.top + p.y, r.right + p.x, r.bottom + p.y};
}
bool Inside(const Gdi::RECT &r, int x, int y)
{
    return x >= r.left && y >= r.top && x < r.right && y < r.bottom;
}
// The metric words NC_HitTest reads out of USER's data segment.
Win98FrameHit::Metrics HitMetrics()
{
    Win98FrameHit::Metrics m;
    m.border_x = settings.Word(0x2e8); m.border_y = settings.Word(0x2ea);
    m.dialog_x = settings.Word(0x2ec); m.dialog_y = settings.Word(0x2ee);
    m.size_x = settings.Word(0x31a);   m.size_y = settings.Word(0x31c);
    m.small_size_x = settings.Word(0x346); m.small_size_y = settings.Word(0x348);
    m.caption_y = settings.Word(0x2e6); m.small_caption_y = settings.Word(0x344);
    m.edge_x = settings.Word(0x338);   m.edge_y = settings.Word(0x33a);
    m.scroll_x = settings.Word(0x2e2); m.scroll_y = settings.Word(0x2e4);
    return m;
}
// USER decides whether the square between two bars is a sizing corner.
// The original walks the parent chain for a child; the native record answers
// from the window's own thick frame and its maximized state.
bool GripSizes(HWND h, const Window &w)
{
    if (w.style & 0x01000000) return false;             // WS_MAXIMIZE never sizes
    if (w.style & 0x40000) return true;                 // WS_THICKFRAME
    if (!(w.style & 0x40000000)) return false;          // not a child: no sizing parent
    for (HWND parent = w.parent; parent;)
    {
        const auto *p = Find(parent);
        if (!p) return false;
        if (p->style & 0x40000) return !(p->style & 0x01000000);
        if (!(p->style & 0x40000000)) return false;
        parent = p->parent;
    }
    return false;
}
bool CaptionIconEligible(HWND h, const Window &w);
void HitWindowRecord(HWND h, const Window &w, Win98FrameHit::Window &out)
{
    const auto o = Origin(w);
    out = {};
    out.style = w.style; out.extended = w.extended; out.flags = w.nonclient_flags;
    out.outer = {o.x, o.y, o.x + w.width, o.y + w.height};
    out.client = {o.x + w.client.left, o.y + w.client.top,
                  o.x + w.client.right, o.y + w.client.bottom};
    NonclientEnvironment e;
    out.border_units = Win98Nonclient::BorderUnits(e, w.style, w.extended, 1, 0);
    // the icon cell answers HTSYSMENU only where allows an
    // icon; elsewhere the caption begins there.
    out.system_icon = (w.style & 0xc00000) != 0x400000 && CaptionIconEligible(h, w);
    out.grip_sizes = GripSizes(h, w);
}
#include "user_scroll_family_draw.inc"
#include "user_caption_draw.inc"
unsigned ClassBackground(const Window &w)
{
    if (w.class_record)
        return class_store.HandleValue(
            class_store.Read16(Win98Classes::Body(class_store, w.class_record) + 16));
    return w.background;
}
void PaintFrame(HWND h, const Window &w, Gdi::POINT p)
{
    USER_PROFILE(UserFrame);
    const Gdi::RECT outer = {p.x, p.y, p.x + w.width, p.y + w.height};
    // USER pre-fills nothing. What covers a window's interior is WM_ERASEBKGND,
    // and DefWindowProc answers it out of the class brush; the client area
    // belongs to the window's own paint. Only the frame body between the
    // border and the client is COLOR_BTNFACE; the caption, the menu bar and
    // the scroll bars draw over it below.
    if (!w.parent)
    {
        const Gdi::RECT inner = At(w.client, p);
        Fill({outer.left, outer.top, outer.right, inner.top}, 0xc0c0c0);
        Fill({outer.left, inner.bottom, outer.right, outer.bottom}, 0xc0c0c0);
        Fill({outer.left, inner.top, inner.left, inner.bottom}, 0xc0c0c0);
        Fill({inner.right, inner.top, outer.right, inner.bottom}, 0xc0c0c0);
    }
    if ((w.extended & 0x20101) || (w.style & 0xc40000))
    {
        if (w.extended & 0x100)
            Edge(outer);
        else if ((w.extended & 0x20000) && !(w.extended & 1) && !(w.style & 0xc40000))
            // WS_EX_STATICEDGE alone: BDR_SUNKENOUTER, shadow above and left,
            // highlight below and right.
            { Gdi::RECT edge = outer; Win98Draw::DrawEdge(screen, &edge, 2, 15); }
        else
        {
            const unsigned border = Equal(w.class_name, "ComboLBox")
                ? Win98Frame::SystemColors()[6] : 0x808080;
            Fill({outer.left, outer.top, outer.right, outer.top + 1}, border);
            Fill({outer.left, outer.bottom - 1, outer.right, outer.bottom}, border);
            Fill({outer.left, outer.top, outer.left + 1, outer.bottom}, border);
            Fill({outer.right - 1, outer.top, outer.right, outer.bottom}, border);
        }
    }
    if (w.nonclient_flags & 8)
        PaintCaption(h, w, p);
    if (w.nonclient_flags & 1)
        PaintMenuBar(w, p);
    if (w.nonclient_flags & 0x00800000)
    {
        auto edge = w.client;
        edge.left -= settings.Word(0x338);
        edge.top -= settings.Word(0x33a);
        edge.right += settings.Word(0x338) + (w.nonclient_flags & 2 ? settings.Word(0x2e2) : 0);
        edge.bottom += settings.Word(0x33a) + (w.nonclient_flags & 4 ? settings.Word(0x2e4) : 0);
        Edge(At(edge, p), true);
    }
    if(w.nonclient_flags&2)ScrollPaint(h,w,1,p);
    if(w.nonclient_flags&4)ScrollPaint(h,w,0,p);
}
void PaintWindow(HWND h, Window &w);
void PaintSiblings(HWND parent)
{
    const auto *container = Find(parent);
    if (!container) return;
    HWND order[MaxWindows]; unsigned count = 0;
    for (HWND h = container->first; h && count < MaxWindows;)
    {
        const auto *w = Find(h); if (!w) break;
        order[count++] = h; h = w->next;
    }
    // Every window draws only inside its visible region, so the order matters
    // only for children without WS_CLIPSIBLINGS that overlap. USER paints the
    // children of a window in the order of the list, the one in front first
    // (the child walk of follows the next pointer at +0),
    // and so the one behind is drawn last and shows. TIMEDATE builds its time
    // field that way: the three small EDITs lie behind the disabled frame
    // EDIT and are still seen.
    for(unsigned index=0;index<count;++index)
    {
        const HWND h = order[parent==Desktop?count-1-index:index];
        auto *w = Find(h);
        if (parent == Desktop && w) USER_TRACE("PAINT top hwnd=%u container=%u visible=%u\n", h, Container(*w), w->visible ? 1u : 0u);
        if (w && Container(*w) == parent) PaintWindow(h, *w);
    }
}
HWND HitWindow(HWND parent, int x, int y, unsigned depth = 0)
{
    const auto *container = Find(parent);
    if (!container || depth >= MaxWindows) return 0;
    for (HWND h = container->first; h;)
    {
        const auto *w = Find(h); if (!w) break;
        const auto p = Origin(*w);
        if (w->button && (w->style & 15) == 7) { h = w->next; continue; }
        // A STATIC without SS_NOTIFY answers HTTRANSPARENT (FrameHitTest), so
        // the search goes on to the window below it: the ICW finish page lays
        // a text over the top of its check box.
        if (w->label && w->parent && !(w->style & 0x100)) { h = w->next; continue; }
        if (Shows(*w) && w->enabled && x >= p.x && y >= p.y &&
            x < p.x + w->width && y < p.y + w->height && RegionHit(*w, x, y))
        {
            const auto client = Client(*w);
            if (x >= p.x + ClientLeft(*w) && y >= p.y + ClientTop(*w) &&
                x < p.x + ClientLeft(*w) + client.right && y < p.y + ClientTop(*w) + client.bottom)
                if (auto child = HitWindow(h, x, y, depth + 1)) return child;
            return h;
        }
        h = w->next;
    }
    return 0;
}
HWND InputWindow(const InputPacket &p)
{
    if (IsWindow(p.host_window)) {
        if (const HWND child = HitWindow(p.host_window, p.x, p.y)) return child;
        return p.host_window;
    }
    return HitWindow(Desktop, p.x, p.y);
}
// USER hands a mouse event to the queue of the window under the pointer or,
// while a button stays down, to the queue of the window the first press went
// to (: asks for the window under the pointer, Windows 98 keep
// the first press in ds:154h and its buttons in ds:15Ch; a move goes the same
// way ). The capture of a queue takes the events of that queue only
// ( on retrieval), for the foreground queue as for any other.
// A drag keeps going to the window that holds the mouse because the first
// press of the drag fixes its queue: the press landed in the capture's queue,
// so every move and the release go there, over the desktop and over the
// windows of other programs alike. Without a button down, a capture takes
// only what happens over the windows of its own thread; a thread in the
// background that still holds the mouse therefore stays out of the browser's
// and the taskbar's clicks.
HWND press_owner = 0;
static HWND QueueWindow(const InputPacket &p)
{
    const bool held = (p.buttons ^ p.changed) != 0 && IsWindow(press_owner);
    return held ? press_owner : InputWindow(p);
}
// The capture that takes the pointer now : the queue of the window of
// the first press while a button is down, else of the window under it.
HWND PointerCapture()
{
    const HWND under = (mouse_buttons && IsWindow(press_owner)) ? press_owner : HitWindow(Desktop, mouse_x, mouse_y);
    const HWND held = QueueCapture(under ? WindowThread(under) : 0);
    return held ? held : under;
}
HWND CaptureFor(const InputPacket &p)
{
    const HWND queue_window = QueueWindow(p);
    return QueueCapture(queue_window ? WindowThread(queue_window) : 0);
}
// USER: the first press of a click (no button held, ds:154h
// still empty) that goes to another queue than the one of the foreground
// queue's capture also posts a press and its release (message + 1) to that
// capture, at the same point, into the capture's queue. The window that
// holds the mouse learns this way that the user clicked elsewhere: a drop
// list or a tracking control closes. The click itself still goes to the
// window under the pointer. A background queue's capture gets nothing, nor
// does the capture of a menu loop (the native menu ends on that press
// itself, PopupRouteInput).
static HWND CaptureClickFor(const InputPacket &p)
{
    if (p.kind != 2 || !(p.changed & p.buttons) || (p.buttons ^ p.changed)) return 0;
    const unsigned owner = ForegroundQueue();
    const HWND capture = owner ? QueueCapture(owner) : 0;
    if (!capture || (ThreadCaptureFlags(owner) & 0x800)) return 0;
    const HWND under = InputWindow(p);
    return under && WindowThread(under) == owner ? 0 : capture;
}
// The caption and the focus rectangle of a BUTTON (win98_button_layout.h).
Win98ButtonLayout::Metrics ButtonMetrics()
{
    return {int(settings.Word(0x338)),int(settings.Word(0x33a)),int(settings.Word(0x2e8)),
            int(settings.Word(0x2ea)),13};
}
// the content rectangle of the kind and the caption inside it.
// A check box or radio button keeps its caption in place while it is pressed;
// its paint passes no pressed state, only the push paint does

Win98ButtonLayout::Rect ButtonCaption(const Window &w,Gdi::HDC dc,const Gdi::RECT &client,
                                      const Gdi::SIZE &text,int &x,int &y)
{
    const auto m=ButtonMetrics();
    const bool win40=(w.nonclient_flags&0x00400000)!=0;
    Gdi::SIZE zero={};
    if(win40)Gdi::GetTextExtentPoint32A(dc,"0",1,&zero);
    const auto content=Win98ButtonLayout::Content(w.style,win40,{client.left,client.top,client.right,client.bottom},
                                                  zero.cx,m);
    Win98ButtonLayout::Caption(w.style,content,text.cx,text.cy+1,
                               w.pressed&&Win98ButtonLayout::PushKind(w.style),m,x,y);
    return content;
}
// The paint draws the focus rectangle with the caption while
// BST_FOCUS is set; WM_SETFOCUS and WM_KILLFOCUS draw it
// alone, and since DrawFocusRect inverts, the second call takes it away
// again. Here a focus change repaints the button (FocusChanged), which leaves
// the same pixels. A button without text, or with BS_BITMAP or BS_ICON but no
// image, ends before it. Win98 shows the rectangle
// always; the keyboard cues of Windows 2000 do not exist there.
void FocusFrame(const Window &w,Gdi::HDC dc,const Gdi::RECT &client,const Win98ButtonLayout::Rect &content,
                int x,int y,const Gdi::SIZE &text)
{
    const bool image=(w.style&0xc0)!=0;
    if(image?!w.image:!w.title[0])return;
    Win98ButtonLayout::Rect r{};
    if(!Win98ButtonLayout::Focus(w.style,{client.left,client.top,client.right,client.bottom},content,
                                 x,y,text.cx,text.cy+1,ButtonMetrics(),r))return;
    const Gdi::RECT frame={r.left,r.top,r.right,r.bottom};
    Gdi::DrawFocusRect(dc,&frame);
}
// Draw native control content on the window's owning thread. WM_CTLCOLORBTN,
// WM_CTLCOLORSTATIC, WM_DRAWITEM and list-view callbacks must enter the guest
// on that thread. The DC uses client coordinates with origin 0,0.
void PaintControlContent(HWND h, Window &w, Gdi::HDC dc)
{
    const Gdi::POINT p{0, 0};
    const auto client = Client(w);
    // A callback can destroy or move the window. Resolve its handle again
    // afterwards; stop painting on a geometry change and leave the paint
    // request pending.
    auto stale=[&,client]{
        const auto *again=Find(h);
        if(!again)return true;
        const auto area=Client(*again);
        return area.right!=client.right||area.bottom!=client.bottom;};
    if (w.button || w.label) {
        USER_TRACE("PAINT control hwnd=%u at=%d,%d client=%d,%d window=%dx%d clientleft=%d clienttop=%d style=%08x\n", h, p.x, p.y, client.right, client.bottom, w.width, w.height, ClientLeft(w), ClientTop(w), w.style);
        const int control_save=Gdi::SaveDC(dc);
        if(!control_save)return;
        if(!SystemClipRect(dc,p.x,p.y,p.x+client.right,p.y+client.bottom))
        {Gdi::RestoreDC(dc,control_save);return;}
        // STATIC exposes its paint clip to the parent's WM_CTLCOLORSTATIC.
        // The system-visible clip alone is not an application clip region.
        if(w.label&&Gdi::IntersectClipRect(dc,0,0,client.right,client.bottom)==0)
        {Gdi::RestoreDC(dc,control_save);return;}
        Gdi::SelectObject(dc, w.font ? w.font : w.edit ? Gdi::GetCurrentObject(w.text_dc,6) : system_font);
        const unsigned kind=w.style&15;
        // USER chooses CTLCOLOR_STATIC for modern check/radio/
        // group controls unless BS_PUSHLIKE is set. A parentless control
        // receives its own color request (USER).
        const unsigned color_target=w.parent?w.parent:h;
        const unsigned color_message=w.button &&
            (kind==0||kind==1||kind==8||kind==11||!ModernWindow(h)||(w.style&0x1000))?0x135:0x138;
        Gdi::SetTextColor(dc,(w.button?IsEnabled(h):w.enabled)?0:0x808080);
        Gdi::SetBkColor(dc,0xc0c0c0);
        const unsigned brush=control_color?control_color(CallbackContext(control_context,color_target),color_target,h,dc,color_message):0;
        if(stale()){Gdi::RestoreDC(dc,-1);return;}
        if(w.button && kind==11 && owner_draw) {
            Gdi::RECT bounds={0,0,client.right,client.bottom};
            owner_draw(CallbackContext(control_context,w.parent),w.parent,h,dc,bounds,(w.pressed?1:0)|(!IsEnabled(h)?4:0)|(focus==h?16:0));
            if(stale()){Gdi::RestoreDC(dc,-1);return;}

        } else {
            // A STATIC greys its text for its own WS_DISABLED only (USER
            // Windows 98 tests the window's style before DrawState with
            // DSS_DISABLED); a modal message box that disables the dialog
            // leaves its labels black.
            Gdi::RECT bounds={p.x,p.y,p.x+client.right,p.y+client.bottom};
            // STATIC rectangle styles fill the client area. Frame styles draw
            // only a one-pixel border and preserve the interior; they do not
            // paint text or erase the background. Both groups use
            // COLOR_3DDKSHADOW, COLOR_3DSHADOW and COLOR_3DHIGHLIGHT,
            // matching Windows 98. Filling frame interiors would cover
            // overlapping buttons.
            const unsigned box=(!w.button&&(w.style&0x1f)>=4&&(w.style&0x1f)<=9)?(w.style&0x1f):0;
            // SS_SIMPLE draws text without erasing its background. An empty
            // label draws nothing, allowing ChooseColor's dialog-painted
            // color cells to remain visible underneath.
            const bool simple_empty=!w.button&&(w.style&0x1f)==0xb&&!w.title[0];
            if(!(w.button && kind==7)&&!box&&!simple_empty){if(brush)Gdi::FillRect(dc,&bounds,brush);else Fill(dc,bounds,0xc0c0c0);}
            bool image_drawn=false;
            // SS_ICON draws the assigned icon at the upper left, or centered
            // with SS_CENTERIMAGE. The window text names the resource and is
            // not displayed. SS_BITMAP uses the corresponding bitmap
            // placement.
            if(!w.button&&(w.style&0x1f)==0xe)
            {
                unsigned info[6]={};
                if(w.image&&Gdi::BitmapInfo(w.image,info))
                    if(const auto memory=Gdi::CreateCompatibleDC(dc)){
                        const int cx=int(info[1]),cy=int(info[2]);
                        int ix=p.x,iy=p.y;
                        if(w.style&0x200){ix+=(client.right-cx)/2;iy+=(client.bottom-cy)/2;}
                        const auto keep=Gdi::SelectObject(memory,w.image);
                        Gdi::BitBlt(dc,ix,iy,cx,cy,memory,0,0,0xcc0020);
                        Gdi::SelectObject(memory,keep);
                        Gdi::DeleteDC(memory);
                    }
                image_drawn=true;
            }
            if(!w.button&&(w.style&0x1f)==3)
            {
                // DrawIconEx stretches the symbol over the whole
                // client area; only SS_CENTERIMAGE keeps its own size
                // and centres it.
                if(w.image&&frame_icon&&w.image_width>0&&w.image_height>0)
                {
                    int ix=p.x,iy=p.y,iw=client.right,ih=client.bottom;
                    if(w.style&0x200){iw=w.image_width;ih=w.image_height;ix+=(client.right-iw)/2;iy+=(client.bottom-ih)/2;}
                    if(iw>0&&ih>0)frame_icon(CallbackContext(frame_icon_context,h),dc,ix,iy,w.image,iw,ih);
                }
                image_drawn=true;
            }
            // Windows 98 sizes an SS_BITMAP window to its bitmap.
            // SS_CENTERIMAGE centers the unscaled bitmap and fills the
            // remainder with its upper-left pixel color. This implementation
            // copies at native size even if the window is subsequently
            // resized.
            if(!w.button&&(w.style&0x1f)==0x0e)
            {
                unsigned info[6]={};
                if(w.image&&Gdi::BitmapInfo(w.image,info))
                    if(const auto memory=Gdi::CreateCompatibleDC(dc)){
                        const int cx=int(info[1]),cy=int(info[2]);
                        const auto keep=Gdi::SelectObject(memory,w.image);
                        int ix=0,iy=0;
                        if(w.style&0x200){
                            ix=(client.right-cx)/2;iy=(client.bottom-cy)/2;
                            if(cx<client.right||cy<client.bottom)Fill(dc,bounds,Gdi::GetPixel(memory,0,0));
                        }
                        const int image_save=Gdi::SaveDC(dc);
                        Gdi::IntersectClipRect(dc,p.x,p.y,p.x+client.right,p.y+client.bottom);
                        Gdi::BitBlt(dc,p.x+ix,p.y+iy,cx,cy,memory,0,0,0xcc0020);
                        if(image_save)Gdi::RestoreDC(dc,image_save);
                        Gdi::SelectObject(memory,keep);
                        Gdi::DeleteDC(memory);
                    }
                image_drawn=true;
            }
            if(box)
            {
                const unsigned *colours=Win98Frame::SystemColors();
                const unsigned rgb=box==4||box==7?colours[21]:box==5||box==8?colours[16]:colours[20];
                if(box<=6)Fill(dc,bounds,rgb);
                else
                {
                    Fill(dc,{bounds.left,bounds.top,bounds.right,bounds.top+1},rgb);
                    Fill(dc,{bounds.left,bounds.bottom-1,bounds.right,bounds.bottom},rgb);
                    Fill(dc,{bounds.left,bounds.top,bounds.left+1,bounds.bottom},rgb);
                    Fill(dc,{bounds.right-1,bounds.top,bounds.right,bounds.bottom},rgb);
                }
                image_drawn=true;
            }
            char label[256];MenuLabel(w.title,label);Gdi::SIZE text={};
            // A button and a static control without SS_NOPREFIX draw their
            // caption with the accelerator prefix: DrawState with
            // DST_PREFIXTEXT and DrawText underline
            // the letter after '&'. The DrawText output
            // routine does that for a caption up to its 256-byte buffer.
            auto caption=[&](int cx,int cy){
                const unsigned length=Length(w.title);bool tab=false;
                for(unsigned i=0;i<length;++i)tab|=w.title[i]=='\t';
                if((w.button||!(w.style&0x80))&&length&&length<=256&&!tab){
                    unsigned ignored=0;Win98DrawTextGdi::Environment prefixed{ignored,dc};
                    prefixed.MetricsFor(0);prefixed.Draw(cx,cy,w.title,length,0);
                    if(prefixed.okay)return;
                }
                Gdi::TextOutA(dc,cx,cy,label,Length(label));
            };
            Gdi::GetTextExtentPoint32A(dc,label,Length(label),&text);
            int x=p.x,y=p.y+(client.bottom-text.cy)/2;
            Win98ButtonLayout::Rect content{0,0,client.right,client.bottom};
            if(w.button&&kind!=7)content=ButtonCaption(w,dc,client,text,x,y);
            if(w.button) {
                if(kind==7) {
                    Edge(dc,{p.x,p.y+text.cy/2,p.x+client.right,p.y+client.bottom},true);x+=8;y=p.y;
                    Gdi::RECT caption={x-2,y,x+text.cx+2,y+text.cy};
                    if(brush)Gdi::FillRect(dc,&caption,brush);else Fill(dc,caption,0xc0c0c0);
                }
                else if((kind==2||kind==3||kind==4||kind==5||kind==6||kind==9)&&!(w.style&0x1000)) {
                    // USER. The state is DFCS_CHECKED (400h) for any
                    // check, DFCS_PUSHED (200h) while pressed, DFCS_INACTIVE
                    // (100h) when disabled. BS_RADIOBUTTON and
                    // BS_AUTORADIOBUTTON add DFCS_BUTTONRADIO (4), a three-state
                    // box in its third state DFCS_BUTTON3STATE (8). The original
                    // blits the matching cell of the check bitmap USER renders
                    // at start-up through DrawFrameControl;
                    // BS_FLAT calls DrawFrameControl directly with DFCS_FLAT and
                    // DFCS_MONO. The same Marlett cells are drawn here.
                    // places the 13x13 cell ([9B4h], [9B6h]): centred
                    // by default, at the right with BS_LEFTTEXT, and for BS_TOP
                    // or BS_BOTTOM inside a line of 5/4 of the text height.
                    unsigned state=(w.check&3)?0x400u:0u;
                    if(w.pressed)state|=0x200;
                    if(!IsEnabled(h))state|=0x100;
                    if(kind==4||kind==9)state|=4;
                    else if((kind==5||kind==6)&&(w.check&3)==2)state|=8;
                    if(w.style&0x8000)state|=0xc000;
                    const int cell=13;
                    int top=(client.bottom-cell)/2;
                    const unsigned vertical=(w.style>>8)&0xc;
                    if(vertical==4||vertical==8){
                        Gdi::SIZE letter={};Gdi::GetTextExtentPoint32A(dc,"M",1,&letter);
                        const int line=letter.cy+letter.cy/4,inset=(line-cell)/2;
                        top=vertical==4?inset:client.bottom-line+inset;
                    }
                    const int left=(w.style&0x20)?client.right-cell:0;
                    Gdi::RECT mark={p.x+left,p.y+top,p.x+left+cell,p.y+top+cell};
                    Win98Frame::DrawFrameControl(dc,&mark,Win98Frame::Button,state);
                } else {
                    // A BS_PUSHLIKE button (the Start button is one) shows its
                    // check state as the pushed-in frame. That holds for a
                    // check box or radio button with BS_PUSHLIKE as well:
                    // asks and paints it through the push
                    // button body, not the mark.
                    const bool pushed=w.pressed||((w.style&0x1000)&&w.check);
                    // BS_DEFPUSHBUTTON: USER frames the button in
                    // COLOR_WINDOWFRAME, one border wide, and draws the push
                    // frame inside it (DM_SETDEFID moves the style).
                    if(kind==1){
                        Fill(dc,{bounds.left,bounds.top,bounds.right,bounds.top+1},0);
                        Fill(dc,{bounds.left,bounds.bottom-1,bounds.right,bounds.bottom},0);
                        Fill(dc,{bounds.left,bounds.top,bounds.left+1,bounds.bottom},0);
                        Fill(dc,{bounds.right-1,bounds.top,bounds.right,bounds.bottom},0);
                        Gdi::RECT inner={bounds.left+1,bounds.top+1,bounds.right-1,bounds.bottom-1};
                        Win98Draw::ButtonFrame(dc,&inner,pushed?0x200:0,0x1000);
                    }else
                    // BS_FLAT gives the push frame the same DFCS_FLAT and
                    // DFCS_MONO edge as the flat check mark above: one line in
                    // COLOR_WINDOWFRAME instead of the raised bevel. BATMETER's
                    // battery buttons on the Power Meter page carry it.
                    Win98Draw::ButtonFrame(dc,&bounds,pushed?0x200:0,(w.style&0x8000)?0xd000:0x1000);
                    // USER. A BS_BITMAP button draws its image instead
                    // of its caption, and a button whose handle is still zero
                    // draws nothing at all inside the frame - which is exactly
                    // the blank Start button. USER insets the content
                    // rectangle by SM_CXEDGE/SM_CYEDGE, and BS_DEFPUSHBUTTON by
                    // one border more. USER then place the image:
                    // BS_LEFT adds SM_CXEDGE again, BS_RIGHT subtracts it from
                    // the right edge, BS_TOP and BS_BOTTOM use SM_CYBORDER, and
                    // an absent alignment bit centres. USER shifts the
                    // pressed image by one border.
                    if(w.style&0xc0){
                        unsigned info[6]={};int cx=0,cy=0;
                        const bool bitmap=(w.style&0xc0)==0x80;
                        // BS_ICON takes the size from the icon header.
                        if(w.image&&bitmap&&Gdi::BitmapInfo(w.image,info)){cx=int(info[1]);cy=int(info[2]);}
                        else if(w.image&&(w.style&0xc0)==0x40&&frame_icon&&icon_size_query)
                            icon_size_query(CallbackContext(icon_size_context,h),w.image,cx,cy);
                        if(cx>0&&cy>0){
                            const int edge_x=int(settings.Word(0x338)),edge_y=int(settings.Word(0x33a));
                            const int border_x=int(settings.Word(0x2e8)),border_y=int(settings.Word(0x2ea));
                            const int extra=(w.style&15)==1?1:0;
                            Gdi::RECT content={edge_x+extra*border_x,edge_y+extra*border_y,
                                               client.right-edge_x-extra*border_x,
                                               client.bottom-edge_y-extra*border_y};
                            const unsigned horizontal=w.style&0x300,vertical=w.style&0xc00;
                            int ix=horizontal==0x100?content.left+edge_x
                                  :horizontal==0x200?content.right-cx-edge_x
                                  :(content.left+content.right-cx)/2;
                            int iy=vertical==0x400?content.top+border_y
                                  :vertical==0x800?content.bottom-cy-border_y
                                  :(content.top+content.bottom-cy)/2;
                            if(w.pressed){ix+=border_x;iy+=border_y;}
                            if(const auto memory=Gdi::CreateCompatibleDC(dc)){
                                // USER draws the image inside the content rectangle it
                                // computed: an image wider than the content never reaches the
                                // button's own edges.
                                const int image_save=Gdi::SaveDC(dc);
                                Gdi::IntersectClipRect(dc,p.x+content.left,p.y+content.top,p.x+content.right,p.y+content.bottom);
                                if(bitmap){
                                    const auto keep=Gdi::SelectObject(memory,w.image);
                                    Gdi::BitBlt(dc,p.x+ix,p.y+iy,cx,cy,memory,0,0,0xcc0020);
                                    Gdi::SelectObject(memory,keep);
                                }else frame_icon(CallbackContext(frame_icon_context,h),dc,p.x+ix,p.y+iy,w.image,cx,cy);
                                Gdi::DeleteDC(memory);
                                if(image_save)Gdi::RestoreDC(dc,image_save);
                            }
                        }
                        image_drawn=true;
                    }
                }
            } else {
                // USER lays the text of SS_LEFT, SS_CENTER and SS_RIGHT out with
                // DrawText: DT_WORDBREAK and DT_EXPANDTABS, from the top of the
                // client rectangle, with the accelerator prefix unless
                // SS_NOPREFIX. SS_LEFTNOWORDWRAP is the same without the word
                // break. One TextOut, centred vertically, cut a paragraph off at
                // both ends of its control (the start dialog of Kaggz).
                // SS_CENTERIMAGE keeps the single centred line below.
                const unsigned type=w.style&0x1f;
                if((type<=2||type==0xc)&&!(w.style&0x200)){
                    unsigned ignored=0;
                    Win98DrawTextGdi::Environment layout{ignored,dc};
                    Win98DrawText::Rect area={Win98DrawText::W(p.x),Win98DrawText::W(p.y),
                                              Win98DrawText::W(p.x+client.right),Win98DrawText::W(p.y+client.bottom)};
                    const unsigned flags=(type==1?1u:type==2?2u:0u)|0x40u|(type==0xc?0u:0x10u)|((w.style&0x80)?0x800u:0u);
                    Gdi::SetBkMode(dc,Gdi::TRANSPARENT);
                    Gdi::IntersectClipRect(dc,area.left,area.top,area.right,area.bottom);
                    const unsigned length=Length(w.title);
                    if(length&&length<8192)Win98DrawText::Layout(layout,w.title,length,false,area,flags,nullptr);
                    image_drawn=true; // nothing more to draw for this control
                }
                else if((w.style&3)==2)x+=client.right-text.cx-3;else if((w.style&3)==1)x+=(client.right-text.cx)/2;
            }
            if(w.pressed&&!w.button){++x;++y;}
            // An image button draws no caption; the original passes DST_BITMAP
            // or DST_ICON to DrawState and never reaches its text branch.
            // A disabled button's caption: USER draws it with DrawState and
            // DSS_DISABLED, which ignores the colour of WM_CTLCOLORBTN: the
            // text once in COLOR_BTNHIGHLIGHT one point lower right, then in
            // COLOR_BTNSHADOW (the wizard's Back and Next).
            if(!image_drawn&&w.button&&!IsEnabled(h)){
                const unsigned *colours=Win98Frame::SystemColors();
                Gdi::SetBkMode(dc,Gdi::TRANSPARENT);
                const unsigned light=colours[20],shade=colours[16];
                Gdi::SetTextColor(dc,Gdi::RGB(light>>16,light>>8,light));caption(x+1,y+1);
                Gdi::SetTextColor(dc,Gdi::RGB(shade>>16,shade>>8,shade));
            }
            if(!image_drawn){Gdi::SetBkMode(dc,Gdi::TRANSPARENT);caption(x,y);}
            if(w.button&&focus==h)FocusFrame(w,dc,client,content,x,y,text);
        }
        Gdi::RestoreDC(dc,-1);
    }
    if(w.common&&w.common!=10&&common_draw){
        const int common_save=Gdi::SaveDC(dc);
        if(!common_save)return;
        if(!SystemClipRect(dc,0,0,client.right,client.bottom))
        {Gdi::RestoreDC(dc,common_save);return;}
        common_draw(CallbackContext(common_context,h),h,dc,client);
        Gdi::RestoreDC(dc,-1);
    }
}
// USER-owned client content: SCROLLBAR and EDIT. The caller has clipped to
// the window's visible region.
void PaintNativeClient(HWND h, Window &w, Gdi::POINT p, Gdi::RECT client, Gdi::RECT area)
{
    if (!w.edit && w.common != 10) return;
    const int client_save = Gdi::SaveDC(screen);
    if (!client_save) { dirty.Mark(); return; }
    if (!SystemClipRect(screen, area.left, area.top, area.right, area.bottom)) { Gdi::RestoreDC(screen, client_save); return; }
    if (w.common == 10) ScrollControlPaint(h, w);
    // The EDIT paints over its caret; it goes off the screen first and comes
    // back when the text stands (HideCaret and ShowCaret around the paint).
    const bool caret_off = w.edit && HideCaretFor(h);
    struct CaretBack { HWND h; bool on; ~CaretBack() { if (on) ShowCaretFor(h); } } caret_back{h, caret_off};
    if (w.edit) {
        const auto cp = Gdi::POINT{p.x + ClientLeft(w), p.y + ClientTop(w)};
        const auto format=EditFormat(w);const int vertical_clip=w.edit_format_set?format.top:(EditInset(w)>1?3:0);
        {
            Gdi::RECT area = {cp.x, cp.y, cp.x + client.right, cp.y + client.bottom};
            if (w.edit_color_known && w.edit_brush && Gdi::GetObjectType(w.edit_brush) == 2)
                Gdi::FillRect(screen, &area, w.edit_brush);
            else Fill(area, w.edit_color_known ? w.edit_fill : 0xffffff);
        }
        const unsigned edit_text_color = w.edit_color_known ? w.edit_text : 0;
        if(!SystemClipRect(screen,cp.x+format.left,cp.y+vertical_clip,cp.x+format.right,cp.y+(w.edit_format_set?format.bottom:client.bottom-vertical_clip)))
        {Gdi::RestoreDC(screen,client_save);return;}
        Gdi::SelectObject(screen, w.font ? w.font : w.edit ? Gdi::GetCurrentObject(w.text_dc,6) : system_font);
        const unsigned a = w.edit->SelectionStart(), b = w.edit->SelectionEnd();
        for (unsigned row = w.edit->FirstLine(); row < w.edit->LineCount() && row <= (unsigned)(w.edit->FirstLine() + w.edit->VisibleLines()); ++row) {
            const auto &line = w.edit->GetLine(row); const int y = cp.y + format.top + ((int)row - w.edit->FirstLine()) * w.edit->LineHeight();
            const unsigned char password = w.edit->PasswordCharacter();
            auto prefix=[&](unsigned count) { return w.edit->PrefixWidth(row, count); };
            auto draw=[&](unsigned first,unsigned last,int x){
                if (password) {
                    char masked[256]; memset(masked, password, sizeof masked);
                    while (first < last) {
                        const unsigned count = last - first < sizeof masked ? last - first : sizeof masked;
                        Gdi::TextOutA(screen, cp.x + format.left + w.edit->LineOffset(row) + x - w.edit->HorizontalPosition(),
                                      y, masked, count);
                        for (unsigned i = 0; i < count; ++i)
                            x += w.edit->CharacterAdvance(password, x);
                        first += count;
                    }
                    return;
                }
                unsigned run=first;
                for(unsigned i=first;i<=last;++i)if(i==last||w.edit->Data()[i]=='\t'){
                    if(i>run)Gdi::TextOutA(screen,cp.x+format.left+w.edit->LineOffset(row)+x-w.edit->HorizontalPosition(),y,w.edit->Data()+run,i-run);
                    for(unsigned j=run;j<=i&&j<last;++j)x+=w.edit->CharacterAdvance((unsigned char)w.edit->Data()[j],x);
                    run=i+1;
                }
            };
            Gdi::SetTextColor(screen, edit_text_color); Gdi::SetBkMode(screen, Gdi::TRANSPARENT);
            // ES_RIGHT and ES_CENTER move the whole line; the tab
            // positions stay relative to its first character.
            const int aligned = w.edit->LineOffset(row);
            draw(line.start,line.start+line.length,0);
            const unsigned start = a > line.start ? a : line.start, end = b < line.start + line.length ? b : line.start + line.length;
            // Without the focus an EDIT hides its selection unless it carries
            // ES_NOHIDESEL (0100h), the rule the style documents.
            if (start < end && (focus == h || (w.style & 0x100))) {
                const Gdi::RECT select = {cp.x + format.left + aligned + prefix(start - line.start) - w.edit->HorizontalPosition(), y,
                    cp.x + format.left + aligned + prefix(end - line.start) - w.edit->HorizontalPosition(), y + w.edit->LineHeight()};
                Fill(select,0x000080);Gdi::SetTextColor(screen,0xffffff);
                draw(start,end,prefix(start-line.start));
            }
        }
        if (focus == h && a == b && (!caret_draw||!caret_draw(CallbackContext(caret_context,h),h,0))) {
            int x = cp.x + format.left + w.edit->CursorX() - w.edit->HorizontalPosition(), y = cp.y + format.top + ((int)w.edit->CursorLine() - w.edit->FirstLine()) * w.edit->LineHeight();
            Fill({x, y, x + 1, y + w.edit->LineHeight()}, 0);
        }
    }
    // EDIT and SCROLLBAR clear their paint requests here.
    w.update.Clear(); w.erase_pending = false;
    Gdi::RestoreDC(screen, client_save);
}
// An ancestor without WS_CLIPCHILDREN can paint over this window. Windows 98
// delivers WM_PAINT to parents before children, so USER-owned child content
// waits for the parent paint.
unsigned NativeSignature(HWND h, const Window &w)
{
    unsigned hash = 2166136261u;
    auto mix = [&](unsigned value) { for (unsigned i = 0; i < 4; ++i) { hash ^= (value >> (i * 8)) & 255; hash *= 16777619u; } };
    mix(w.style); mix(w.extended); mix(unsigned(w.width)); mix(unsigned(w.height)); mix(w.nonclient_flags);
    mix(foreground == h); mix(focus == h); mix(w.enabled); mix(w.pressed_hit);
    for (const char *s = w.title; *s; ++s) mix(unsigned(char(*s)));
    if (w.edit)
    {
        const char *data = w.edit->Data();
        for (unsigned i = 0, n = w.edit->Length(); i < n; ++i) mix(unsigned(char(data[i])));
        mix(w.edit->SelectionStart()); mix(w.edit->SelectionEnd()); mix(unsigned(w.edit->FirstLine()));
        mix(unsigned(w.edit->HorizontalPosition())); mix(w.edit_brush); mix(w.edit_fill); mix(w.edit_text); mix(w.font);
    }
    if (w.scroll_allocated)
    {
        const unsigned char *bytes = reinterpret_cast<const unsigned char *>(&w.scroll);
        for (unsigned i = 0; i < sizeof w.scroll; ++i) mix(bytes[i]);
    }
    // What the bar tracker shows is part of the frame as well: the inverted
    // page region, the pushed arrow and the thumb of a running drag. USER
    // draws each of them straight onto the bar when it changes and
    // restores the bar when the button comes up. Without them a
    // child that shares its pixels kept the inverted page and the pushed
    // arrow after the release, because nothing else had changed.
    if (scroll_press.window == h)
    {
        mix(scroll_press.part); mix(scroll_press.vertical);
        mix(unsigned(scroll_press.left)); mix(unsigned(scroll_press.top));
        mix(unsigned(scroll_press.right)); mix(unsigned(scroll_press.bottom));
    }
    if (arrow_window == h && arrow_part != 0xffff) { mix(0x100u | arrow_vertical); mix(arrow_part); }
    if (scroll_thumb_active && scroll_thumb_window == h) { mix(0x200u | scroll_thumb_vertical); mix(scroll_thumb_position); }
    return hash;
}
// Every ancestor draws: WM_SETREDRAW(FALSE) takes WS_VISIBLE off a window in
// USER, and with it the paints of its whole subtree.
bool RedrawChain(HWND h)
{
    const auto *w = Find(h);
    for (unsigned n = 0; w && w->parent && n < MaxWindows; ++n)
    {
        w = Find(w->parent);
        if (w && !w->redraw) return false;
    }
    return true;
}
bool AncestorPaintPending(HWND h)
{
    const auto *w = Find(h);
    for (unsigned n = 0; w && w->parent && n < MaxWindows; ++n)
    {
        const auto *parent = Find(w->parent);
        if (!parent) return false;
        // A STATIC whose procedure a program replaced paints by WM_PAINT
        // like such a window (PaintControlSurface).
        if ((Custom(*parent) || (parent->label && GuestProcedure(*parent))) && parent->redraw && parent->update.count &&
            !(parent->style & 0x02000000u) && Shows(*parent))
            return true;
        w = parent;
    }
    return false;
}
// Draw USER-owned frames (caption, menu bar, scrollbars and edges), EDIT and
// SCROLLBAR content within the visible screen region. Custom window clients
// belong to WM_PAINT; controls with callbacks belong to their owning thread
// through ServeControlPaints.
struct SurfaceTarget {
    Gdi::HDC previous;
    SurfaceTarget(HWND h) : previous(screen) { if (host_window_surfaces) screen = HostFrameDC(h); }
    ~SurfaceTarget() { screen = previous; }
};
// The frame alone, clipped to the window's visible region.
static void PaintVisibleFrame(HWND h, Window &w)
{
    SurfaceTarget target(h);
    auto *visible = new RectList;
    WindowVisible(h, WindowScreenRect(w), false, *visible);
    const int save = visible->n ? Gdi::SaveDC(screen) : 0;
    if (save)
    {
        const auto p = Origin(w);
        const auto area = At(w.client, p);
        if (Gdi::IntersectClipRegion(screen, visible->r, visible->n) && RegionClip(screen, w, 0, 0))
        {
            Gdi::ExcludeClipRect(screen, area.left, area.top, area.right, area.bottom);
            PaintFrame(h, w, p);
        }
        Gdi::RestoreDC(screen, save);
    }
    delete visible;
}
// Overlapping siblings share pixels if either lacks WS_CLIPSIBLINGS; the last
// paint wins. A group box is excluded because it leaves its interior
// unpainted.
static bool GroupBox(const Window &w) { return w.button && (w.style & 15) == 7; }
static bool SharesPixels(HWND h, const Window &w)
{
    if (!w.parent || GroupBox(w)) return false;
    const auto *container = Find(w.parent);
    if (!container) return false;
    const auto r = WindowScreenRect(w);
    unsigned guard = 0;
    for (HWND s = container->first; s && guard < MaxWindows; ++guard)
    {
        const auto *o = Find(s);
        if (!o) break;
        if (s != h && Shows(*o) && !GroupBox(*o) && !(w.style & o->style & 0x04000000u))
        {
            const auto q = WindowScreenRect(*o);
            if (q.left < r.right && r.left < q.right && q.top < r.bottom && r.top < q.bottom) return true;
        }
        s = o->next;
    }
    return false;
}
void PaintWindow(HWND h, Window &w)
{
    if (!Shows(w) || !w.redraw) return;
    // LockWindowUpdate prevents this window from painting.
    if (Locked(h)) return;
    // A control repaints after its parent. Defer its subtree until then;
    // EndPaint requests the next frame.
    if (AncestorPaintPending(h)) return;
    SurfaceTarget target(h);
    const auto p = Origin(w); const auto client = Client(w);
    USER_TRACE("PAINT hwnd=%u at=%d,%d size=%dx%d parent=%u\n", h, p.x, p.y, w.width, w.height, w.parent);
    // Buttons, labels and common controls repaint on their owning thread
    // after a content change. Controls sharing pixels with a sibling repaint
    // only on invalidation or a state change, matching Windows 98; otherwise
    // unrelated changes would place them above that sibling. SNDVOL32's
    // speaker icon can overlap its balance slider. A subclassed STATIC
    // likewise retains its custom WM_PAINT output until invalidation or a
    // state change.
    // A child without WS_CLIPSIBLINGS shares pixels with overlapping
    // siblings. Repaint only invalid regions so it does not cover siblings
    // painted later. Changes to text, selection, scrollbars, or frame
    // invalidate the control.
    const unsigned signature = NativeSignature(h, w);
    const bool shared = w.parent && !(w.style & 0x04000000u);
    const bool due = !shared || w.native_invalid || signature != w.native_signature;
    // BeginPaint sends WM_NCPAINT on the owning thread before background and
    // client painting. A control with callbacks must draw its frame during
    // that paint, preserving ordering with overlapping siblings. Drawing it
    // here could cover a sibling label, as with the etched field in Add Web
    // Folder.
    // Composition may redraw a frame without invalidating the control's
    // already painted client pixels, including after a same-size move.
    if (ControlContent(w) && (w.native_invalid || signature != w.native_signature || !w.drawn))
    { w.frame_owed = true; w.needs_paint = true; }
    w.native_invalid = false;
    w.native_signature = signature;
    auto *visible = new RectList;
    if (due) WindowVisible(h, WindowScreenRect(w), false, *visible);
    const int window_save = visible->n ? Gdi::SaveDC(screen) : 0;
    if (window_save)
    {
        if (!Gdi::IntersectClipRegion(screen, visible->r, visible->n) || !RegionClip(screen, w, 0, 0))
            dirty.Mark();
        else
        {
            const auto area = At(w.client, p);
            const int frame_save = w.frame_owed ? 0 : Gdi::SaveDC(screen);
            if (frame_save)
            {
                Gdi::ExcludeClipRect(screen, area.left, area.top, area.right, area.bottom);
                PaintFrame(h, w, p);
                Gdi::RestoreDC(screen, frame_save);
            }
            PaintNativeClient(h, w, p, client, area);
        }
        Gdi::RestoreDC(screen, window_save);
    }
    delete visible;
    if (!Find(h)) return;
    PaintSiblings(h);
}
void Changed(HWND h, Window &w, bool modified = true)
{
    w.modified = modified; dirty = true;
    // USER calls same-task SendMessage before returning. The native
    // edit buffer has no movable DS storage to release/reacquire around it.
    // Snapshot every field before the callback: the parent may destroy us.
    // ECNotifyParent runs twice for one change: EN_UPDATE before the control
    // shows the new text, EN_CHANGE after it. A program may do its work on
    // either; WinPac 2 REGISTER.EXE computes its total on EN_UPDATE (its MFC
    // message map ). The parent may destroy the control on
    // the first one, so the second goes only to a live control.
    const auto parent=w.parent?w.parent:w.owner;const unsigned id=w.menu&65535;
    static const unsigned codes[]={0x400u,0x300u};
    for(const unsigned code:codes){
        if(code==0x300u&&!Find(h))break;
        const unsigned command=id|(code<<16);
        if(parent){if(edit_notify)edit_notify(CallbackContext(edit_notify_context,parent),parent,command,h);
            else PostMessage(parent,0x111,command,h);} // Standalone native USER provider.
    }

}
}

#include "user_window_info.inc"
void SetCaretProvider(void*p,CaretDraw draw,CaretDestroy destroy){caret_context=p;caret_draw=draw;caret_destroy=destroy;}
void SetCaretBracket(void*p,CaretBracket bracket){caret_bracket_context=p;caret_bracket=bracket;}
void SetFrameIcon(void*p,FrameIcon draw){frame_icon_context=p;frame_icon=draw;}
void SetDefaultIconQuery(void*p,DefaultIconQuery query){default_icon_context=p;default_icon_query=query;}
void SetIconSizeQuery(void*p,IconSizeQuery query){icon_size_context=p;icon_size_query=query;}
void SetDragIconProvider(void*p,DragIcon query){drag_icon_context=p;drag_icon=query;}
void SetScrollBrush(void*p,ScrollBrush query){scroll_brush_context=p;scroll_brush=query;}
void CaretChanged(){dirty=true;}
bool CaretPending(HWND h,const Gdi::RECT&r){auto*w=Find(h);if(!w)return false;
    for(unsigned i=0;i<w->update.count;++i){const auto&u=w->update.rectangles[i];
        if(r.left<u.right&&r.top<u.bottom&&r.right>u.left&&r.bottom>u.top)return true;}return false;
}
// The window a turn of the wheel is for. USER takes the focus of the
// current queue; with the wheel following the pointer (the setting of the
// host) it is the window under the pointer,
// as on the Mac and as the mouse drivers of the time offered, unless a
// window has the capture or the window under the pointer is disabled (the
// owner of a modal dialog) or the desktop itself.
static bool WheelMenuOpen(); // below the menu tracking
static bool wheel_to_pointer = true;
void SetWheelToPointer(bool on) { wheel_to_pointer = on; }
bool WheelToPointer() { return wheel_to_pointer; }
static HWND WheelWindow(const InputPacket &p)
{
    // An open menu takes the wheel and does nothing with it: the menu loop
    // of Win98 does not hand it on, and nothing scrolls behind a menu

    if (WheelMenuOpen()) return 0;
    if (!wheel_to_pointer || QueueCapture(ForegroundQueue())) return focus;
    const HWND under = HitWindow(Desktop, p.x, p.y);
    if (!under || under == Desktop || !IsEnabled(under) || !IsVisible(under)) return focus;
    // Over a SCROLLBAR control the wheel is its parent's, which the bar
    // scrolls (DefWindowProc finds the bar, src/x86_activation.cpp).
    if (const Window *w = Find(under); w && w->common == 10 && w->parent && w->parent != Desktop) return w->parent;
    return under;
}
void* InputContext(const InputPacket&p){const HWND held=p.kind==2?CaptureFor(p):0;HWND h=p.kind==2?(held?held:InputWindow(p)):p.kind==3?WheelWindow(p):focus;auto*owner=Process(WindowProcess(h));return owner?owner->context:nullptr;}
unsigned CurrentProcess(){return active_process;}unsigned CurrentThread(){return active_thread;}
unsigned WindowProcess(HWND h){auto*w=Find(h);return w?w->process:0;}
void* CurrentContext(void*fallback){auto*p=Process(active_process);if(p)return p->context;if(processes)return processes->context;return fallback;}
void* CallbackContext(void*fallback,HWND h){auto*p=Process(WindowProcess(h));return p?p->context:CurrentContext(fallback);}
unsigned ProcessContextCount(){return process_count;}
void* ProcessContextAt(unsigned index){auto*p=processes;while(p&&index--)p=p->next;return p?p->context:nullptr;}
void* ProcessContextOf(unsigned pid){auto*p=Process(pid);return p?p->context:nullptr;}
// a registering process gets flags 5, bit 4 is the one-time
// permission to take the foreground.
bool AttachProcess(unsigned pid,void*context)
{
    if(!pid||Process(pid))return false;
    auto *p=new(std::nothrow) ProcessContext;
    if(!p)return false;
    // USER allocates a zeroed 20h-byte record and links it.
    // Class-list heads belong to the same WORD-addressed USER heap, so they
    // must be allocated too, rather than overlapping it at a larger index.
    const unsigned owner=class_store.LocalAllocate(0x40,32);
    if(!owner){delete p;return false;}
    *p={pid,owner,0x400,context,5};
    auto **tail=&processes;while(*tail)tail=&(*tail)->next;
    *tail=p;++process_count;return true;
}
void BindTask(unsigned pid,unsigned tid){if(pid&&!Process(pid)){pid=0;tid=0;}active_process=pid;active_thread=tid;auto*p=Process(pid);class_store.current_owner=p?p->owner:0x1800;if(p)settings.expected=p->version;}
// Windows 98 destroys all windows belonging to a thread's queue when that
// queue ends. Process windows therefore disappear at exit rather than waiting
// for later teardown.
void DestroyProcessWindows(unsigned pid){auto*p=Process(pid);if(!p)return;unsigned old_process=active_process,old_thread=active_thread;BindTask(pid,0);
 for(unsigned i=0;i<MaxWindows;++i)if(windows[i].used&&windows[i].process==pid)DestroyWindow(Handle(i));
 BindTask(old_process,old_thread);
}
void ReleaseProcessWindows(unsigned pid){auto*p=Process(pid);if(!p)return;unsigned old_process=active_process,old_thread=active_thread;BindTask(pid,0);
 if(locked_process==pid){DropWindowUpdateLock(true);}
 DropPointerLevel(pid,0);
 DestroyProcessWindows(pid);
 for(unsigned i=0;i<queued;)if(input_events[i].process==pid){for(unsigned j=i+1;j<queued;++j){queue[j-1]=queue[j];input_events[j-1]=input_events[j];}--queued;}else ++i;
 for(auto&m:menus)if(m.used&&m.process==pid)DestroyMenu(m.handle);
 for(unsigned offset=20;offset<=24;offset+=2)while(class_store.Read16(p->owner+offset))Win98Classes::Destroy(class_store,p->owner,p->owner+offset,0);
 class_store.ReleaseModules(p->owner);
 BindTask(old_process,old_thread);
}
// The USER callbacks are one function per kind, shared by every process;
// only the fallback context of a departing process is forgotten, the
// function stays for the windows of the others (CallbackContext resolves
// the window's own process first).
void DropExclusiveMode(unsigned pid);
void DetachProcess(unsigned pid){DropExclusiveMode(pid);if(auto*p=Process(pid)){DropScreenLease(p->context,pid);ForgetPointerOwner(p->context);if(frame_icon_context==p->context)frame_icon_context=nullptr;if(drag_icon_context==p->context)drag_icon_context=nullptr;if(shell_hook_context==p->context)shell_hook_context=nullptr;if(shell_context==p->context)shell_context=nullptr;auto **link=&processes;while(*link!=p)link=&(*link)->next;*link=p->next;
 class_store.LocalFree(p->owner);delete p;--process_count;}if(active_process==pid)BindTask(0,0);if(processes)return;property_cleanup=nullptr;property_context=nullptr;Shutdown();SetClipboardCallbacks(nullptr,nullptr,nullptr);SetCaretProvider(nullptr,nullptr,nullptr);SetCaretBracket(nullptr,nullptr);}
Settings &SystemSettings() { return settings; }
namespace { InstanceVersion instance_version = nullptr; }
void SetInstanceVersionProvider(InstanceVersion provider) { instance_version = provider; }
void ConfigureVersion(unsigned version) { settings.expected = version;if(auto*p=Process(active_process))p->version=version; }

namespace {
void (*system_colour_listener)(unsigned, unsigned) = nullptr;
}
void SetSystemColourListener(void (*listener)(unsigned, unsigned)) { system_colour_listener = listener; }
#include "user_screen.inc"
#include "user_host_surfaces.inc"
bool Initialize()
{
    if (screen) return true;
    settings = Settings{};
    class_store.object_type=[](unsigned h)->unsigned{unsigned b[3];return Gdi::SolidBrushInfo(h,b)?2:0;};
    // GDI refuses to delete a stock object (Windows 98 tests the stock bit of the
    // signature); the port's stock brushes carry no such bit, so the class
    // store asks the list of them first.
    class_store.delete_brush=[](unsigned h){if(!Gdi::ProtectedStockBrush(h))Gdi::DeleteObject(h);};
    class_store.system_registration=true;
    for(const auto &d:Win98ClassDefaults::Records){unsigned f[12]={48,d.style,d.procedure,0,d.extra,0xffffffff,0,0,d.background};if(!class_store.Register(f,d.name,nullptr)){class_store.system_registration=false;Shutdown();return false;}}
    class_store.system_registration=false;
    published_width = Modes::NativeWidth; published_height = Modes::NativeHeight;
    published_capacity = PublishedCapacity();
    published_frame = new unsigned char[published_capacity]{};
    published_dc = Gdi::CreateSurfaceDC({published_frame, published_width, published_height, published_width * 4, 32});
    system_font = Gdi::CreateFontResource(0);
    InstallScreenPaletteSink();
    if (!BuildScreen(Modes::NativeWidth, Modes::NativeHeight, Modes::NativeDepth) ||
        !published_dc || !system_font) { Shutdown(); return false; }
    desktop = Window{};
    desktop.used = desktop.visible = desktop.ready = true;
    desktop.width = 800; desktop.height = 600; desktop.style = 0x96000000; desktop.thread = 0;
    Layout(Desktop);
    Copy(desktop.class_name, "Desktop", sizeof desktop.class_name);
    Gdi::SelectObject(screen, system_font); dirty = true; return true;
}
void Shutdown()
{
    TouchReset();
    shell_fullscreen = false;
    host_hidden_window = 0;
    SetCaptureNotify(nullptr,nullptr);
    edit_notify=nullptr;edit_notify_context=nullptr;
    ResetPopupMenus();
    shell_event = nullptr; shell_context = nullptr; shell_hook_context = nullptr; foreground = 0; activation_queue = {};
    foreground_queue = last_input_queue = last_input_process = last_input_time = 0; for (auto &slot : thread_queues) slot = {};
    activation_globals = {};
    for (unsigned i = 0; i < MaxWindows; ++i) if (windows[i].used) DestroyWindow(Handle(i));
    for(unsigned link=0x1814u,number=0;number<4;++number,link=number==3?0xfau:link+2)while(class_store.Read16(link))Win98Classes::Destroy(class_store,0x1800,link,0);
    if (property_cleanup && desktop.used) property_cleanup(property_context, Desktop);
    property_cleanup = nullptr; property_context = nullptr;
    FreeWindowDCs(desktop); FreeWindowRegion(desktop); ResetShown();
    if(desktop_region)Gdi::DeleteObject(desktop_region); desktop_region=0;
    ReleaseRedrawScratch();
    SetThreadQueueProvider(nullptr);
    ResetHotKeys();
    desktop = Window{}; shell_registration = {};
    DropWindowUpdateLock();
    if (screen) Gdi::DeleteDC(screen); screen = 0;
    if (published_dc) Gdi::DeleteDC(published_dc); published_dc = 0;
    // Wallpaper survives individual tasks, but belongs to this USER session.
    if (settings.wallpaper_bitmap) Gdi::SysDeleteObject(settings.wallpaper_bitmap);
    settings.wallpaper_bitmap = settings.wallpaper_style = 0;
    settings.wallpaper_checked = false;
    ResetPointerOverlay();
    if (system_font) Gdi::DeleteObject(system_font); system_font = 0;
    delete[] published_frame; published_frame = nullptr;
    ReleaseScreen(); queued = focus = open_menu = press_owner = 0;for(auto&slot:capture_slots)slot={};capture_globals={};automatic_capture=false;capture_serial=0; class_store.Reset();
    control_context=nullptr;owner_draw=nullptr;control_color=nullptr;guest_paint_context=nullptr;guest_paint=nullptr;rendering=false;
    common_context=nullptr;common_draw=nullptr;
    last_click_window = last_click_time = 0;
    SetMetadataProvider(nullptr,nullptr,nullptr);standalone_metadata={};processed_position=metadata_queue_marker=0;
    menu_window=0;shift_down=control_down=false;

    input = InputState{}; mouse_buttons = 0; mouse_x = mouse_y = 0;
    mouse_track_target=0;mouse_track_hit=0;
    for (auto &event : input_events) event = InputEvent{};
    while(menu_state.head)FreeMenuObject(menu_state.head);
    for (auto &menu : menus) menu = Menu{};
    // The stock DEFAULT_PALETTE is system-owned: every DC created since
    // Initialize only borrowed it, so it outlives task-owned objects and is
    // released with the rest of the native provider, as at x86 process exit.
    Gdi::ShutdownBitmaps();
}
unsigned RegisterNativeClass(const unsigned *fields,const char *name,const char *menu) { class_store.expected=settings.expected;return class_store.Register(fields,name,menu); }
unsigned GetNativeClassInfo(unsigned instance,const char *name,unsigned *fields) {return class_store.Info(instance,name,fields);}
unsigned UnregisterNativeClass(unsigned instance,const char *name) {return Win98Classes::Unregister(class_store,class_store.NameToken(name),class_store.Module(instance,false));}
void ProtectStockBrush(unsigned handle,bool on){Gdi::ProtectStockBrush(handle,on);}
void DestroyModuleClasses(unsigned instance){const unsigned module=class_store.Module(instance,false);if(module&&module!=0xffff)Win98Classes::DestroyModule(class_store,class_store.Read16(Win98Classes::Local(class_store,0xe28)),module);}
// USER, for SignalProc32 with code 5, which KERNEL32 sends after the
// DLL_PROCESS_DETACH of every 32-bit module it frees from a process: the owner
// of that process loses the classes of the module, so the DLL registers them
// again when the process loads it a second time. A process on the shared
// owner at local E28h keeps them.
void DestroyLibraryClasses(unsigned instance){const unsigned owner=class_store.CurrentOwner(),module=class_store.Module(instance,false);if(module&&module!=0xffff&&owner!=class_store.Read16(Win98Classes::Local(class_store,0xe28)))Win98Classes::DestroyModule(class_store,owner,module);}
void OwnClassMenu(HWND h,HMENU menu){if(auto*w=Find(h))w->class_menu_owned=menu;}
unsigned NativeClassError(){return class_store.error;}
unsigned ClassAtomName(unsigned atom,char*out,unsigned capacity){return Win98Atoms::Name(class_store.atoms,atom,(unsigned char*)out,capacity);}
const char *ClassMenuName(unsigned token){return token>65535?(const char*)class_store.memory+class_store.StringPointer(token):nullptr;}
unsigned ClassAccess(HWND h,int index,unsigned value,unsigned flags) {
 auto*w=Find(h);if(!w||!w->class_record)return 0;
 auto&e=class_store;const unsigned slot=(h&1023)*64,window=e.Window(slot);e.Write16(window+0x24,w->class_record);
 e.Write8(window+0x2e,0x40);e.Write8(window+0x39,0);
 // Build a provider sibling chain so original system-class cloning transfers
 // every live window of this process before returning to the guest. Only a
 // write can clone (USER), so a read needs neither the chain nor the
 // copy of the class records back.
 const bool writes=flags&1;
 if(writes){
 e.Write32(0xaa8,64*(MaxWindows+1));e.Write32(e.Window(64*(MaxWindows+1))+4,0);unsigned previous=0;
 for(unsigned i=0;i<MaxWindows;++i)if(windows[i].used){unsigned p=(i+1)*64;e.window_owners[p/64]=Process(windows[i].process)?Process(windows[i].process)->owner:0x1800;e.Write16(e.Window(p)+0x24,windows[i].class_record);e.Write32(e.Window(p),0);e.Write32(e.Window(p)+4,0);if(previous)e.Write32(e.Window(previous),p);else e.Write32(e.Window(64*(MaxWindows+1))+4,p);previous=p;}
 }
 int n=(short)index;
 if(n>=0&&unsigned(n)+((flags&2)?4u:2u)>e.Read16(Win98Classes::Body(e,w->class_record)+8))return 0;
 if(writes&&(n==-10||n==-12||n==-14||n==-34)){e.error=0;value=e.Handle(value);if(e.error)return 0;}
 unsigned result=Win98Classes::Access(e,window,index,value,flags);
 if(writes)for(unsigned i=0;i<MaxWindows;++i)if(windows[i].used)windows[i].class_record=e.Read16(e.Window((i+1)*64)+0x24);
 if(n==-10||n==-12||n==-14||n==-34)result=e.HandleValue(result);
 if(n==-16)result=e.ModuleValue(result);
 // A class query can run inside an owner-draw callback. Only mutations
 // request another composition; invalidating on reads never lets it settle.
 if(flags&1)dirty=true;return result;
}
unsigned RegisterClass(const char *name,unsigned procedure,unsigned menu,unsigned background,unsigned extra) {
 unsigned f[12]={48,0,procedure?procedure:1,0,extra,1,0,0,background,menu,0,0};unsigned result=RegisterNativeClass(f,name,nullptr);
 if(result&&!procedure){unsigned record=class_store.FindRecord(1,name);class_store.Write32(Win98Classes::Body(class_store,record)+4,0);}return result;
}
unsigned create_window_error = 0;
unsigned CreateWindowError() { return create_window_error; }
unsigned WindowInstance(HWND hwnd) { auto *w = Find(hwnd); return w ? w->instance : 0; }
unsigned SetWindowInstance(HWND hwnd, unsigned instance)
{
    auto *w = Find(hwnd);
    if (!w)
        return 0;
    const unsigned previous = w->instance;
    w->instance = instance;
    return previous;
}
// The edit procedure also initializes windows created from a superclass.
// Their class name and guest procedure need not be the built-in EDIT class.
bool AdoptEdit(HWND h,const char *title)
{
    auto *window=Find(h);if(!window)return false;
    auto &w=*window;if(w.edit)return true;
    if(w.button||w.label||w.common)return false;
    if(!title)title=w.title;
    const unsigned style=w.style;
    w.text_dc = Gdi::CreateCompatibleDC();
    if (!w.text_dc) { return false; }
    const auto default_view=Gdi::SelectedFont(w.text_dc);
    w.edit_default_font=Gdi::CreateFontView(default_view.data,default_view.length);
    if(!w.edit_default_font){Gdi::DeleteDC(w.text_dc);w.text_dc=0;return false;}
    Gdi::SelectObject(w.text_dc,w.edit_default_font);
    w.edit = new TextEdit;
    const unsigned n=Length(title?title:"");auto *initial=new char[n+1];unsigned kept=0;
    for(unsigned k=0;k<n;++k)if(title[k]!='\r'||title[k+1]!='\n')initial[kept++]=title[k];
    initial[kept]=0;w.edit->Reset(initial);delete[] initial;Metrics(w);
    if ((style & 0x24u) == 0x20u) w.edit->SetPasswordCharacter('*');
    // ES_CENTER 1, ES_RIGHT 2; with both, a single line control keeps
    // ES_RIGHT, as USER does when it creates the edit.
    if (style & 2u) w.edit->SetAlignment(TextEdit::AlignRight);
    else if (style & 1u) w.edit->SetAlignment(TextEdit::AlignCenter);
    dirty=true;return true;
}
HWND CreateWindow(const char *name, const char *title, unsigned style, int x, int y, int width, int height, HWND parent, unsigned menu, unsigned parameter, unsigned thread,unsigned instance)
{
    create_window_error = 1400;
    if ((parent && !Find(parent)) || !name ||
        ((style & 0xc0000000u) == 0x40000000u && !parent)) return 0;
    // Parent handles belong to the shared USER window table. Screen savers
    // started by DESK.CPL with /p create a child of the applet's preview window;
    // the child keeps the creating process and its callback context below.
    if (parent == Desktop && !(style & 0x40000000)) parent = 0;
    // Win98 supplies a caption and sibling clipping for an overlapped
    // top-level window, even when the caller passed style zero.
    if (!(style & 0xc0000000u)) style |= 0x04c00000u;
    else if (style & 0x80000000u) style |= 0x04000000u;
    create_window_error = 1407;
    // Under Win98 the 16-bit COMMCTRL.DLL, which COMCTL32 loads through its
    // flat thunk, registers its controls as global classes without the "32",
    // and every process sees them: RUNONCE.EXE's progress dialog names
    // "msctls_progress" and imports no COMCTL32 of its own. The
    // 16-bit COMMCTRL is ours and its controls are COMCTL32's, as for 16-bit
    // dialog templates (src/x86_user_session.cpp), so such a name finds the
    // class of the same control when nothing registered the name itself.
    {
        static const char *const renamed[][2] = {
            {"msctls_hotkey", "msctls_hotkey32"}, {"msctls_trackbar", "msctls_trackbar32"},
            {"msctls_updown", "msctls_updown32"}, {"msctls_progress", "msctls_progress32"},
            {"msctls_statusbar", "msctls_statusbar32"}};
        for (const auto &r : renamed)
            if (Equal(name, r[0]) && !class_store.FindRecord(instance, name)) { name = r[1]; break; }
    }
    const bool popup_class=Equal(name,"#32768");
    const bool edit = Equal(name, "EDIT"), button = Equal(name,"BUTTON"), label = Equal(name,"STATIC"), dialog=Equal(name,"#32770"); unsigned class_fields[12] = {48};
    // "SysTreeView" is the class of the 16-bit COMMCTRL (TV_WNDPROC); a 16-bit
    // dialog of SYSDM names it, and the native tree view answers it, with the
    // 16-bit structures translated in src/win16_common_controls.cpp.
    const bool tree16=Equal(name,"SysTreeView")&&!class_store.FindRecord(instance,name);
    const bool list16=Equal(name,"SysListView")&&!class_store.FindRecord(instance,name); //
    const unsigned common=Equal(name,"SysTreeView32")||tree16?1:Equal(name,"SysListView32")||list16?2:Equal(name,"msctls_statusbar32")?3:Equal(name,"LISTBOX")||Equal(name,"ComboLBox")?4:Equal(name,"ToolbarWindow32")?5:Equal(name,"COMBOBOX")?6:Equal(name,"SysTabControl32")?7:Equal(name,"msctls_updown32")?8:Equal(name,"tooltips_class32")?9:Equal(name,"SCROLLBAR")?10:Equal(name,"msctls_trackbar32")?11:0;
    const unsigned record=class_store.FindRecord(instance,name); const bool c=record&&class_store.Info(instance?instance:class_store.ModuleValue(class_store.Read16(Win98Classes::Body(class_store,record)+10)),name,class_fields);
    if((common==5||common==7||common==8||common==9||common==11)&&!c)return 0;
    if (!c && !edit && !button && !label && !dialog&&!common&&!popup_class) return 0;
    create_window_error = 8;
    if(c&&class_fields[4]>256)return 0;
    for (unsigned i = 0; i < MaxWindows; ++i) if (!windows[i].used) {
        auto &w = windows[i]; const unsigned generation = w.generation; w = Window{}; w.generation = generation; w.used = true;
        Copy(w.class_name,name,sizeof w.class_name);w.class_record=record;w.parent = (style&0xc0000000u)==0x40000000u?parent:0; w.owner = w.parent?0:TopLevelOwner(parent); w.button=button;w.label=label;w.enabled=!(style&0x08000000); w.procedure = c ? class_fields[2] : 0; w.style = style; w.parameter = parameter;
        w.menu = menu;
        w.extra_size = c ? class_fields[4] : dialog ? 30 : 0;
        w.background=c?class_fields[8]:0;
        w.double_clicks=c&&(class_fields[1]&8);
        w.instance=instance;
        w.class_style=c?class_fields[1]:0;
        InitializeWindowDCs(Handle(i),w);
        w.thread=thread;w.process=active_process;
        // USER knows nothing about the common-control classes. It sends every
        // window the messages of its class procedure, and COMCTL32's own code
        // paints SysListView32, ToolbarWindow32, SysTabControl32 and
        // tooltips_class32. NEO98's native implementations of those four stand
        // in for a COMCTL32 that is not in the process; they register their
        // class with a native procedure token. When the original COMCTL32 is
        // loaded, the class carries its real x86 procedure instead, and then
        // the window is an ordinary window: it gets WM_PAINT and the original
        // code draws it. Marking it a native common control would take its
        // WM_PAINT away, which is why the desktop SysListView32 stayed empty
        // although SHELL32 had inserted its five items.
        w.common=(!c||(class_fields[2]&0xffff0000u)==0x71000000u)?common:0; // a token, not a guest DLL at 71xxxxxxh
        if(w.common&&!w.procedure)w.procedure=0x71000000+w.common;
        w.x = x == (int)0x80000000 ? 64 : x; w.y = x == (int)0x80000000 || y == (int)0x80000000 ? 55 : y;
        w.width = width == (int)0x80000000 ? ((style&0xc00000)?672:0) : width; w.height = height == (int)0x80000000 || (width == (int)0x80000000 && (style&0xc00000)) ? 480 : height;
        if (!(style&0xc0000000u)) {
            if(w.width<int(settings.Word(0x322)))w.width=settings.Word(0x322);
            if(w.height<int(settings.Word(0x324)))w.height=settings.Word(0x324);
        }
        // Win98 stores window coordinates and dimensions as signed words.
        // The Win32 thunk narrows values; large values do not reject creation.
        w.x = short(w.x); w.y = short(w.y);
        w.width = short(w.width); w.height = short(w.height);
        // nWidth and nHeight are signed. Windows 98 CreateWindowEx
        // substitutes CW_USEDEFAULT but accepts other negative sizes,
        // creating an invisible window. Keep the upper bound because window
        // coordinates are sixteen bits wide.
        w.visible = style & 0x10000000; Copy(w.title, title, sizeof w.title);
        GeometryChanged();
        // USER marks a window of a module that expects 4.0 or later
        // (GetExpWinVer of the instance), not of the process: a dialog that
        // COMMDLG makes with its own instance is a 4.0 window in a program
        // for Windows 3.1.
        unsigned expected = settings.expected;
        if (instance && instance_version)
            if (void *context = CurrentContext(nullptr))
                if (const unsigned version = instance_version(context, instance))
                    expected = version;
        w.nonclient_flags = expected >= 0x400 ? 0x00400000 : 0;
        NormalizeEdge(w);
        Layout(Handle(i));
        if (edit && !AdoptEdit(Handle(i),title)) { w.used=false; return 0; }
        if(w.visible){w.update.Add(Client(w));w.erase_pending=true;}
        const HWND h = Handle(i); w.popup = h;
        w.extended |= OwnerTopmost(w);
        auto *container = Find(Container(w));
        LinkAfter(h, w, (style & 0x40000000) && container ? container->last : FrontPosition(w));
        if(record){unsigned body=Win98Classes::Body(class_store,record);class_store.Write16(body,class_store.Read16(body)+1);}
        if(ControlContent(w))w.needs_paint=true;
        create_window_error = 0; dirty = true; return h;
    }
    return 0;
}
bool CreationPending(HWND h) { const auto *w = Find(h); return w && !w->ready; }
void SwapCreationBounds(HWND h, Gdi::RECT &rect)
{
    auto *w = Find(h); if (!w || w->ready) return;
    const Gdi::RECT old{w->x, w->y, w->x + w->width, w->y + w->height};
    w->x = rect.left; w->y = rect.top;
    w->width = rect.right - rect.left; w->height = rect.bottom - rect.top;
    rect = old; Layout(h); Metrics(*w);
}
unsigned CbtDestroyFlags(HWND h,unsigned set){auto*w=Find(h);if(!w)return 0;unsigned old=w->cbt_destroy_flags;w->cbt_destroy_flags|=set;return old;}
HWND CbtDetachFirst(HWND h){auto*p=Find(h);if(!p||!p->first)return 0;HWND child=p->first;auto*w=Find(child);if(!w)return 0;Unlink(child,*w);return child;}
// The live position of the running scrollbar tracker, defined with the tracker
// itself in user_frame_track.inc further down.
namespace { bool ScrollTrackPosition(HWND h, unsigned bar, unsigned &position); }
#include "user_scroll_family.inc"
bool DestroyWindow(HWND h)
{
    auto *w = Find(h); if (!w || h == Desktop) return false;
    // USER hides through SetWindowPos before destroying the window.
    // Restore its SPB before teardown callbacks can show another popup.
    if (w->visible) ShowWindow(h, 0);
    w = Find(h); if (!w) return true;
    ReleaseOwnerBar(h);
    TouchDropWindow(h);
    PopupWindowDestroyed(h);
    w = Find(h); if (!w) return true;
    if (h == locked_window) {
        DropWindowUpdateLock(); dirty = true;
    }
    if(caret_destroy)caret_destroy(CallbackContext(caret_context,h),h);
    if (clipboard_destroy) clipboard_destroy(CallbackContext(clipboard_context,h),h);
    w=Find(h); if(!w)return true;
    for (unsigned i = 0; i < MaxWindows; ++i) if (windows[i].used && windows[i].parent == h) DestroyWindow(Handle(i));
    // Destroying a child returns into the guest (WM_DESTROY, WM_NCDESTROY),
    // and that handler may destroy this window too. USER resolves the handle
    // again after every such return; the record below must be the live one.
    w=Find(h); if(!w)return true;
    Win98ShellRegistration::Destroy(shell_registration,

        {w->style,w->owner,w->extended,w->nonclient_flags,
         Win98ShellRegistration::W(w->thread),Win98ShellRegistration::W(h)},h);
    if (w->shell_announced)
    {
        w->shell_announced = false;
        NotifyShell(2, h);
        // A shell hook is a synchronous guest callback. It may destroy this
        // window and reuse its table slot; generation handles make Find(h)
        // reject the replacement, but the pointer held above would still
        // address that slot.
        w = Find(h); if (!w) return true;
    }
    FreeWindowHotKeys(h); // USER sweeps the hot-key list on destruction.
    if (foreground == h) foreground = 0;
    if (activation_queue.active == h) activation_queue.active = 0;
    for (auto &slot : thread_queues)
        if (slot.thread)
        {
            if (slot.queue.active == h) slot.queue.active = 0;
            if (slot.queue.previous == h) slot.queue.previous = 0;
            if (slot.queue.focus == h) slot.queue.focus = 0;
        }
    for (auto &other : windows) if (other.used && other.popup == h) other.popup = 0;
    if (focus == h) focus = 0; CaptureOperation(6,h);
    delete w->edit; if(w->text_dc)Gdi::DeleteDC(w->text_dc);if(w->edit_default_font)Gdi::DeleteObject(w->edit_default_font);FreeWindowDCs(*w);FreeWindowRegion(*w);GeometryChanged();
    const auto system_menu=w->system_menu,window_menu=w->menu,class_menu=w->class_menu_owned;
    w->system_menu=w->class_menu_owned=0;
    if((w->style&0xc0000000u)!=0x40000000u)w->menu=0;
    if(system_menu){DestroyMenu(system_menu);ReleaseRetiredMenu(system_menu);}
    if((w->style&0xc0000000u)!=0x40000000u&&window_menu){DestroyMenu(window_menu);ReleaseRetiredMenu(window_menu);}
    if(class_menu&&class_menu!=window_menu){DestroyMenu(class_menu);ReleaseRetiredMenu(class_menu);}
    if (property_cleanup) property_cleanup(CallbackContext(property_context,h), h);
    Unlink(h, *w);
    if(w->class_record){unsigned body=Win98Classes::Body(class_store,w->class_record);class_store.Write16(body,class_store.Read16(body)-1);}
    const unsigned generation = w->generation % 63 + 1; *w = Window{}; w->generation = generation;
    // Native generation ownership: an unsupported style/owner mutation must
    // not leave the registration pointing at this now-freed record. The core
    // above retains the original USER destruction predicate unchanged.
    if(shell_registration.shell_record==h)shell_registration.shell_record=0;
    dirty = true; return true;
}
void SetShellEvents(void *context, ShellEvent event) { shell_context = context; shell_event = event; }
void SetShellHookOwner(void *context) { shell_hook_context = context; }
void ConstrainShellPosition(HWND h,unsigned &after,unsigned &flags) {
    if(const auto*w=Find(h))Win98ShellRegistration::ConstrainPosition(w->nonclient_flags,after,flags);
}
HWND RegisteredShellWindow() { return Find(shell_registration.shell_record) ? shell_registration.shell_record : 0; }
unsigned RegisterShellWindow(HWND hwnd, void *context, ShellPosition position, ShellMessage send)
{
    if (hwnd && !Find(hwnd)) return 0;
    struct Environment final : Win98ShellRegistration::Environment {
        void *context; ShellPosition position; ShellMessage send; HWND last_read = 0;
        Environment(void *c, ShellPosition p, ShellMessage s) : context(c),position(p),send(s) {}
        Win98ShellRegistration::Window ReadWindow(unsigned h) override {
            last_read = h;
            if (const auto *w=Find(h)) return {w->style,w->owner,w->extended,w->nonclient_flags,
                Win98ShellRegistration::W(w->thread),Win98ShellRegistration::W(h)};
            return {};
        }
        void WriteFlags(unsigned h,unsigned flags) override {if(auto*w=Find(h))w->nonclient_flags=flags;}
        void PositionBottom(unsigned h) override {position(context,h);}
        void Send(Win98ShellRegistration::W handle,Win98ShellRegistration::W message,Win98ShellRegistration::W wp,unsigned lp,Win98ShellRegistration::W flags) override {
            (void)flags;
            // Expand the original WORD handle back to the generation-tagged
            // native record selected immediately before this provider call.
            if (Find(last_read) && Win98ShellRegistration::W(last_read)==handle) send(context,last_read,message,wp,lp);
        }
    } e(context,position,send);
    return Win98ShellRegistration::Set(shell_registration,e,hwnd);
}

void SetPropertyCleanup(void *context, PropertyCleanup cleanup)
{
    property_context = context; property_cleanup = cleanup;
}
unsigned PropertyList(HWND h) { auto *w = Find(h); return w ? w->property_list : 0; }
void SetPropertyList(HWND h, unsigned list) { if (auto *w = Find(h)) w->property_list = list; }
unsigned WindowIcon(HWND h,bool large){auto*w=Find(h);return w?(large?w->icon_large:w->icon_small):0;}
void CalculateClientRect(HWND h, Gdi::RECT &rect)
{
    if (!Find(h)) return;
    NonclientEnvironment environment;
    // Host layout also measures this rectangle. Only the guest's nonclient
    // calculation advances the flags observed by accessibility subscribers.
    environment.notify = true;
    Win98Nonclient::Rect r{rect.left, rect.top, rect.right, rect.bottom};
    Win98Nonclient::Calculate(environment, h, r, 0);
    rect = {r.left, r.top, r.right, r.bottom};
}
void ApplyClientRect(HWND h, const Gdi::RECT &rect)
{
    auto *w = Find(h);
    if (!w || h == Desktop) return;
    const auto origin = Origin(*w);
    w->client = {rect.left - origin.x, rect.top - origin.y,
                 rect.right - origin.x, rect.bottom - origin.y};
    GeometryChanged();
    Metrics(*w);
}
unsigned WindowDragIcon(HWND h){auto*w=Find(h);return w?w->icon_drag:0;}
// USER asks on every DrawCaption, so an answer of zero is not final:
// VB3's ThunderRTMain answers zero until its first form has loaded. A caller
// that found no icon marks the window, and the owner's thread asks again in
// ServeDragIcons.
void WantDragIcon(HWND h){auto*w=Find(h);if(w&&!w->icon_drag)w->icon_drag_asked=false;}
unsigned SetWindowIcon(HWND h,bool large,unsigned icon){auto*w=Find(h);if(!w)return 0;auto&slot=large?w->icon_large:w->icon_small;const unsigned old=slot;slot=icon;dirty=true;return old;}
HWND OwnedWindow(HWND owner){for(unsigned i=0;i<MaxWindows;++i)if(windows[i].used&&windows[i].owner==owner&&!windows[i].parent)return Handle(i);return 0;}
void MarkSizeMove(HWND h){if(auto*w=Find(h))w->send_size_move=true;}
bool TakeSizeMove(HWND h){auto*w=Find(h);if(!w||!w->send_size_move)return false;w->send_size_move=false;return true;}
void CompleteCreation(HWND h)
{
    if (auto *w = Find(h)) { w->ready = true; ShellVisibility(h, *w); }
}
HWND FindWindow(const char *klass,const char *title) {
    for(unsigned i=0;i<MaxWindows;++i){const auto &w=windows[i];
        if(w.used&&!w.parent&&(!klass||Equal(klass,w.class_name))&&(!title||Equal(title,w.title)))return Handle(i);}
    return 0;
}
// USER32!FindWindowA is four instructions that push two zeros in
// front of the caller's two arguments and jump into FindWindowExA, so both share one body. FindWindowExA walks the sibling chain of
// the given parent, or the desktop's children for parent zero, and starts
// behind hwndChildAfter. The native walk uses the same chain GetTopWindow and
// GW_HWNDNEXT answer; an hwndChildAfter that is not a child of the parent is
// not rejected here, where the original answers zero with error 1400.
HWND FindWindowEx(HWND parent,HWND after,const char *klass,const char *title) {
    const HWND root=parent?parent:GetDesktopWindow();
    if(!Find(root)||(after&&!Find(after)))return 0;
    HWND child=after?GetWindow(after,2):GetTopWindow(root);
    for(unsigned n=0;child&&n<MaxWindows;++n){
        auto *w=Find(child);
        if(w&&(!klass||Equal(klass,w->class_name))&&(!title||Equal(title,w->title)))return child;
        child=GetWindow(child,2);}
    return 0;
}
unsigned GetClassName(HWND h,char *out,unsigned capacity){auto *w=Find(h);capacity&=65535;if(!w||!out||!capacity||(capacity&32768))return 0;if(w->class_record)return ClassAtomName(class_store.Read16(w->class_record+6),out,capacity);Copy(out,w->class_name,capacity);return Length(out);}
bool IsWindow(HWND h) { return Find(h); }
bool IsButton(HWND h) { auto *w=Find(h);return w&&w->button; }
bool IsLabel(HWND h) { auto *w=Find(h);return w&&w->label; }
unsigned LabelImage(HWND h) { auto *w=Find(h);return w&&w->label?w->image:0; }
unsigned SetLabelImage(HWND h,unsigned image,int width,int height) {
    auto *w=Find(h);if(!w||!w->label)return 0;
    const unsigned previous=w->image;w->image=image;w->image_type=1;w->image_width=width;w->image_height=height;
    w->needs_paint=true;dirty=true;return previous;
}
// A window of a class that superclasses BUTTON becomes a button the moment its
// procedure forwards to the BUTTON class procedure; from then on the native
// button state and painting apply to it as they do under Win98, where the
// forwarded procedure is the whole implementation.
void AdoptButton(HWND h) { if (auto *w = Find(h)) if (!w->button && !w->label) { w->button = true; dirty = true; } }
// The same for STATIC: a window whose procedure forwards to the STATIC class
// procedure is a static control, and the native label paints it.
void AdoptLabel(HWND h) { if (auto *w = Find(h)) if (!w->button && !w->label && !w->edit && !w->common) { w->label = true; w->needs_paint = true; dirty = true; } }
bool ButtonTracking(HWND h,bool tracking){auto*w=Find(h);if(!w)return false;const bool old=w->button_tracking;w->button_tracking=tracking;return old;}
// Test inspection: has is always true because every window draws to the
// screen; drawn indicates whether it has painted at least once.
bool CanvasInfo(HWND h,bool&has,bool&drawn){auto*w=Find(h);if(!w)return false;has=true;drawn=w->drawn;return true;}
unsigned CommonControl(HWND h){auto *w=Find(h);return w?w->common:0;}
void SetCommonDraw(void *p,CommonDraw draw){common_context=p;common_draw=draw;}
bool IsVisible(HWND h) {if(!Find(h))return false;for(unsigned n=0;h&&n<MaxWindows;++n){auto *w=Find(h);if(!w||!w->visible)return false;h=w->parent;}return true;}
bool IsEnabled(HWND h) {if(!Find(h))return false;for(unsigned n=0;h&&n<MaxWindows;++n){auto *w=Find(h);if(!w||!w->enabled)return false;h=w->parent;}return true;}
HWND Owner(HWND h){auto *w=Find(h);return w?w->owner:0;}
unsigned ExtendedStyle(HWND h){auto *w=Find(h);return w?w->extended:0;}
void SetExtendedStyle(HWND h,unsigned style){if(auto *w=Find(h)){if(h==Desktop)return;
 // Extended styles arrive after User::CreateWindow. Apply inherited styles
 // until creation finishes; later SetWindowLong uses the caller's value.
 if(!w->ready)style|=OwnerTopmost(*w);
 const auto old=w->extended;w->extended=style;NormalizeEdge(*w);Layout(h);Metrics(*w);dirty=true;if((old^style)&8)OrderWindow(h,0,w->ready);ShellVisibility(h,*w);}}
void SetControlCallbacks(void *p,OwnerDraw draw,ControlColor color){control_context=p;owner_draw=draw;control_color=color;}
void SetGuestPaint(void *p,GuestPaint paint){guest_paint_context=p;guest_paint=paint;}
bool Rendering(){return rendering;}
Win98Dialog::Node DialogNode(HWND h) {
    auto *w=Find(h);if(!w)return {};
    return {w->parent,w->first,w->next,Style(h),w->extended};
}
unsigned NextDialogItem(HWND root,HWND current,bool previous,bool group) {
    Win98Dialog::Tree tree{nullptr,[](void*,unsigned h){return DialogNode(h);}};
    return group?Win98Dialog::NextGroup(tree,root,current,previous):Win98Dialog::NextTab(tree,root,current,previous);
}
unsigned DialogChildControl(HWND root,HWND current) {
    Win98Dialog::Tree tree{nullptr,[](void*,unsigned h){return DialogNode(h);}};
    return Win98Dialog::ChildControl(tree,root,current);
}
unsigned DialogNextControl(HWND root,HWND current,unsigned flags) {
    Win98Dialog::Tree tree{nullptr,[](void*,unsigned h){return DialogNode(h);}};
    return Win98Dialog::NextControl(tree,root,current,flags);
}
unsigned ControlMessage(HWND h,unsigned msg,unsigned wp,unsigned lp) {
    auto *w=Find(h);if(!w)return 0;
    if(msg==0x30){auto old=w->font;w->font=wp;dirty=true;return old;}
    if(msg==0x31)return w->font?w->font:system_font;
    // WM_GETDLGCODE. BUTTON checks its style (styles
    // above BS_PUSHBOX answer like BS_USERBUTTON) and adds DLGC_BUTTON (2000h).
    // Push buttons add 20h or 10h, radio buttons 40h, a group box is
    // DLGC_STATIC alone. A check box wants '+', '=' and '-' as
    // characters; Application::ButtonProcedure reads that from the
    // MSG before it gets here.
    if(msg==0x87){
        if(w->button){static const unsigned short codes[]={0x20,0x10,0,0,0x40,0,0,0x100,0,0x40,0x20};
            const unsigned kind=w->style&15;if(kind==7)return 0x100;return 0x2000u|(kind<11?codes[kind]:0);}
        return w->label?0x100:0;}
    if(w->button) {
        if(msg==0xf0)return w->check;
        if(msg==0xf1){
            // USER: only check/radio styles accept a state.
            // Radio buttons also move their own WS_TABSTOP bit, even when
            // the check state did not change. The Win16 boundary uses WORD.
            const unsigned kind=w->style&15,value=wp&65535;
            if(kind==4||kind==9){w->style=(w->style&~0x10000u)|(value?0x10000u:0);w->check=value?1:0;}
            else if(kind==2||kind==3)w->check=value?1:0;
            else if(kind==5||kind==6)w->check=value>2?2:value;
            else return 0;
            dirty=true;return 0;
        }
        if(msg==0xf2)return w->check|(w->pressed?4:0)|(focus==h?8:0)|(w->button_tracking?0x60:0);
        if(msg==0xf3){w->pressed=wp!=0;dirty=true;return 0;}
        if(msg==0xf4){if((w->style&15)!=(wp&15)&&lp&&ControlContent(*w))w->needs_paint=true;w->style=(w->style&~15u)|(wp&15);dirty=true;return 0;}
        if(msg==0xf5){ClickButton(h);return 0;}
        // USER, the BUTTON image pair. Win16 numbers them 406h and 407h;
        // the Win32 numbers reach the same body. USER requires the style
        // bits BS_ICON|BS_BITMAP to equal the byte the requested kind selects
        // in the table at USER, so IMAGE_BITMAP needs exactly BS_BITMAP
        // and both IMAGE_ICON and IMAGE_CURSOR need exactly BS_ICON. A kind
        // above IMAGE_CURSOR or a disagreeing style answers zero and stores
        // nothing. The handle is stored without any validation, and a visible
        // button is invalidated with an erase; nothing repaints synchronously.
        if(msg==0xf6||msg==0xf7){
            static const unsigned char wanted[]={0x80,0x40,0x40};
            if(wp>=3||(w->style&0xc0)!=wanted[wp])return 0;
            if(msg==0xf6)return w->image;
            const unsigned previous=w->image;
            w->image=lp;w->image_type=wp;
            if(w->visible)InvalidateRect(h,nullptr,true);
            dirty=true;
            return previous;
        }
        if(msg==0x102&&wp==' '){ClickButton(h);return 0;}
    }
    return 0;
}
void BeginControlCapture(HWND);
// Hit test for the BUTTON procedure: client coordinates against the window.
bool ButtonInside(HWND h,int cx,int cy) {
    auto *w=Find(h);if(!w||!w->button)return false;
    const auto p=Origin(*w);const int x=p.x+ClientLeft(*w)+cx,y=p.y+ClientTop(*w)+cy;
    return x>=p.x&&y>=p.y&&x<p.x+w->width&&y<p.y+w->height;
}
void ClickButton(HWND h) {
    auto *w=Find(h);if(!w||!w->button||!IsEnabled(h))return;
    const unsigned parent=w->parent,id=w->menu&65535,kind=w->style&15;
    if(kind==3)ControlMessage(h,0xf1,!w->check,0);
    else if(kind==6)ControlMessage(h,0xf1,(w->check+1)%3,0);
    if(kind==9){
        // USER visits GetNextDlgGroupItem, including manual
        // radio buttons in this group, and updates each radio's tab stop.
        unsigned next=h;
        for(unsigned n=0;next&&n<1024;++n){
            if(ControlMessage(next,0x87,0,0)&0x40)ControlMessage(next,0xf1,next==h,0);
            next=NextDialogItem(parent,next,false,true);if(next==h)break;
        }
    }
    dirty=true;
    // As with edit notifications, the parent runs before BM_CLICK returns.
    // Snapshot its arguments before the callback, which can destroy us.
    if(parent){if(edit_notify)edit_notify(CallbackContext(edit_notify_context,parent),parent,id,h);
        else PostMessage(parent,0x111,id,h);}
}
bool IsEdit(HWND h) { auto *w = Find(h); return w && w->edit; }
bool EditInDialog(HWND h,bool mark){auto*w=Find(h);if(!w||!w->edit)return false;if(mark)w->edit_in_dialog=true;return w->edit_in_dialog;}
bool GetEditFormat(HWND h,Gdi::RECT&r){auto*w=Find(h);if(!w||!w->edit)return false;r=EditFormat(*w);return true;}
bool SetEditFormat(HWND h,const Gdi::RECT&r,bool repaint){auto*w=Find(h);if(!w||!w->edit||(w->style&4u)==0||r.right<r.left||r.bottom<r.top)return false;w->edit_format=r;w->edit_format_set=true;Metrics(*w);if(repaint){dirty=true;InvalidateRect(h,nullptr,true);}return true;}
bool ResetEditFormat(HWND h,bool repaint){auto*w=Find(h);if(!w||!w->edit||(w->style&4u)==0)return false;w->edit_format_set=false;Metrics(*w);if(repaint){dirty=true;InvalidateRect(h,nullptr,true);}return true;}
bool SetEditTabStops(HWND h,const int*stops,unsigned count){auto*w=Find(h);if(!w||!w->edit||(w->style&4u)==0||count>32)return false;int pixels[32];Gdi::SIZE size{};Gdi::SaveDC(screen);Gdi::SelectObject(screen,w->font?w->font:w->edit_default_font);Gdi::GetTextExtentPoint32A(screen,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",52,&size);Gdi::RestoreDC(screen,-1);const int base=(size.cx/26+1)/2;for(unsigned i=0;i<count;++i)pixels[i]=stops[i]*base/4;return w->edit->SetTabStops(pixels,count);}
bool EditCaretInfo(HWND h,Gdi::POINT&p,unsigned&height,bool&selected){
    auto*w=Find(h);if(!w||!w->edit)return false;auto&e=*w->edit;
    const auto r=EditFormat(*w);p={r.left+e.CursorX()-e.HorizontalPosition(),r.top+(int(e.CursorLine())-e.FirstLine())*e.LineHeight()};height=e.LineHeight();
    selected=e.SelectionStart()!=e.SelectionEnd();return true;
}
unsigned Procedure(HWND h) { auto *w = Find(h); return w ? w->procedure : 0; }
unsigned SetProcedure(HWND h, unsigned p) { auto *w = Find(h); if (!w || h == Desktop) return 0; const auto old = w->procedure; w->procedure = p; return old; }
unsigned Style(HWND h)
{
    auto *w = Find(h);
    return w ? (w->style & ~0x18000000u) | (w->visible ? 0x10000000 : 0) | (w->enabled ? 0 : 0x08000000) : 0;
}
unsigned SetStyle(HWND h, unsigned style)
{
    auto *w = Find(h);
    if (!w)
        return 0;
    const auto old = Style(h);
    if (h == Desktop) return old;
    w->style = style;
    NormalizeEdge(*w);
    Layout(h);
    w->visible = style & 0x10000000;
    GeometryChanged();
    w->enabled = !(style & 0x08000000);
    if (w->edit) w->edit_color_pending = true; // ES_READONLY, WS_DISABLED
    ShellVisibility(h, *w);
    Metrics(*w);
    dirty = true;
    return old;
}
// SetWindowLong stores GWL_STYLE and GWL_EXSTYLE without recomputing edges,
// layout or metrics. SetWindowPos with SWP_FRAMECHANGED performs that
// recomputation. Visual Basic and Delphi depend on this sequence. The caller
// sends WM_STYLECHANGING and WM_STYLECHANGED.
unsigned SetStyleWords(HWND h, unsigned style, bool extended)
{
    auto *w = Find(h);
    if (!w)
        return 0;
    const auto old = extended ? ExtendedStyle(h) : Style(h);
    if (h == Desktop)
        return old;
    if(old!=style)w->frame_message_owed=true;
    if (extended)
        w->extended = style;
    else
    {
        w->style = style;
        // Keep the cached visibility and enabled-state booleans synchronized
        // with WS_VISIBLE and WS_DISABLED.
        w->visible = (style & 0x10000000) != 0;
        GeometryChanged();
        w->enabled = (style & 0x08000000) == 0;
    }
    dirty = true;
    return old;
}
// SWP_FRAMECHANGED recomputes window geometry from the styles even when
// position and size remain unchanged.
void RelayoutFrame(HWND h)
{
    auto *w = Find(h);
    if (!w || h == Desktop)
        return;
    NormalizeEdge(*w);
    Layout(h);
    Metrics(*w);
    ShellVisibility(h, *w);
    dirty = true;
}
bool ModernWindow(HWND h) { auto *w = Find(h); return w && (w->nonclient_flags & 0x00400000) != 0; }
bool SetOwnerField(HWND h, HWND value)
{
    auto *w = Find(h);
    if (!w || h == Desktop || (w->parent && w->parent != Desktop))
        return false;
    w->owner = value;
    dirty = true;
    return true;
}
unsigned Parameter(HWND h) { auto *w = Find(h); return w ? w->parameter : 0; }
bool WindowLong(HWND h,int index,unsigned &value,bool write){
    auto *w=Find(h);if(!w)return false;
    if(index==-21){unsigned old=w->userdata;if(write)w->userdata=value;value=old;return true;}
    if(index<0||w->extra_size<4||(unsigned)index>w->extra_size-4)return false;
    unsigned old=0;for(unsigned i=0;i<4;++i)old|=(unsigned)w->extra[index+i]<<(i*8);
    if(write)for(unsigned i=0;i<4;++i)w->extra[index+i]=value>>(i*8);value=old;return true;
}
// USER32!GetWindowWord and SetWindowWord share the window-long worker with a
// width of two bytes ( pushes 4, pushes 5). A negative index
// is rejected before the worker runs, so only the extra bytes are reachable.
bool WindowWord(HWND h,int index,unsigned &value,bool write){
    auto *w=Find(h);if(!w)return false;
    if(index<0||w->extra_size<2||(unsigned)index>w->extra_size-2)return false;
    unsigned old=(unsigned)w->extra[index]|((unsigned)w->extra[index+1]<<8);
    if(write){w->extra[index]=(unsigned char)value;w->extra[index+1]=(unsigned char)(value>>8);}
    value=old;return true;
}
bool TrackingMouseLeave(HWND h){auto*w=Find(h);return w&&w->track_mouse_leave;}
unsigned WindowContextHelp(HWND h){auto*w=Find(h);return w?w->context_help:0;}
bool SetWindowContextHelp(HWND h,unsigned value){auto*w=Find(h);if(!w)return false;w->context_help=value;return true;}
HWND Parent(HWND h) { auto *w = Find(h); return w ? w->parent : 0; }
unsigned ControlID(HWND h) { auto *w = Find(h); return w ? w->menu : 0; }
bool SetControlID(HWND h,unsigned id) { auto *w = Find(h); if (!w) return false; w->menu = id; return true; }
namespace {
// Native provider for the implemented USER geometry. The window record,
// the desktop and the single 800x600 monitor are NEO98 objects; the arithmetic and
// the work-area decision come from the original.
struct MinMaxEnvironment final : Win98MinMax::Environment
{
    Window *w;
    HWND handle;
    explicit MinMaxEnvironment(Window *window, HWND h) : w(window), handle(h) {}
    unsigned short Word(unsigned offset) const override { return settings.Word(offset); }
    void MonitorRect(Win98MinMax::Rect &r) const override
    {
        r = {settings.monitor[0], settings.monitor[1], settings.monitor[2], settings.monitor[3]};
    }
    void WorkArea(Win98MinMax::Rect &r) const override
    {
        r = {settings.work[0], settings.work[1], settings.work[2], settings.work[3]};
    }
    bool MonitorReserved() const override { return false; }
    bool ParentIsDesktop() const override { return !w->parent || w->parent == Desktop; }
    void ParentClientRect(Win98MinMax::Rect &r) const override
    {
        Gdi::RECT c = {};
        GetClientRect(w->parent, &c);
        r = {c.left, c.top, c.right, c.bottom};
    }
    unsigned Style() const override { return w->style; }
    unsigned ExtendedStyle() const override { return w->extended; }
    // The original reads an internal WND flag; NEO98 treats an overlapped or popup
    // window with a caption as the application window the flag marks.
    bool AppWindow() const override { return (w->style & 0x00c00000u) != 0; }
    bool Maximizable() const override { return handle && handle != Desktop; }
    void SetWorkAreaMaximized(bool value) override
    {
        if (value) w->nonclient_flags |= 0x200u;
        else w->nonclient_flags &= ~0x200u;
    }
};
} // namespace
bool WorkArea(Gdi::RECT *r)
{
    if (!r) return false;
    *r = {settings.work[0], settings.work[1], settings.work[2], settings.work[3]};
    return true;
}
bool MinMaxDefaults(HWND h, Win98MinMax::Info &info)
{
    auto *w = Find(h);
    if (!w) return false;
    MinMaxEnvironment e(w, h);
    Win98MinMax::Defaults(e, info);
    // USER reads the saved maximized position before the
    // application receives WM_GETMINMAXINFO.
    if (w->maximum_position_saved)
        info.max_position = {w->maximum_position.x, w->maximum_position.y};
    return true;
}
bool MinMaxAdjust(HWND h, Win98MinMax::Info &info)
{
    auto *w = Find(h);
    if (!w) return false;
    MinMaxEnvironment e(w, h);
    // USER: a thick frame returning the remembered x unchanged
    // is moved back to its default frame origin. Preserve a custom callback
    // position, and apply the same delta to y before work-area adjustment.
    if (e.ParentIsDesktop() && w->maximum_position_saved && (w->style & 0x40000u) &&
        info.max_size.x >= settings.monitor[2] - settings.monitor[0] &&
        info.max_size.y >= settings.monitor[3] - settings.monitor[1])
    {
        Win98MinMax::Info defaults = {};
        Win98MinMax::Defaults(e, defaults);
        if (info.max_position.x == w->maximum_position.x && info.max_position.x != defaults.max_position.x)
        {
            info.max_position.y = Win98MinMax::Word16(info.max_position.y + defaults.max_position.x - info.max_position.x);
            info.max_position.x = defaults.max_position.x;
            w->maximum_position_saved = false;
            w->maximum_position = {-1, -1};
        }
    }
    Win98MinMax::Adjust(e, info);
    return true;
}
bool MaximizedRect(HWND h, Gdi::RECT &r)
{
    Win98MinMax::Info info = {};
    if (!MinMaxDefaults(h, info) || !MinMaxAdjust(h, info)) return false;
    r = {info.max_position.x, info.max_position.y, info.max_position.x + info.max_size.x,
         info.max_position.y + info.max_size.y};
    return true;
}
bool ShowWindow(HWND h, int command, bool defer_geometry)
{
    auto *w = Find(h);
    if (!w || h == Desktop || command < 0 || command > 9)
        return false;
    const bool old = w->visible;
    const auto old_style = w->style;
    if (command == 2 || command == 6 || command == 7 || command == 3)
    {
        if (!(old_style & 0x21000000))
        {
            w->normal = {w->x, w->y, w->x + w->width, w->y + w->height};
            w->normal_saved = true;
        }
        if (command != 3)
        {
            if (!(old_style & 0x20000000))
                w->restore_maximized = old_style & 0x01000000;
            w->style = (w->style & ~0x01000000u) | 0x20000000;
        }
        else
        {
            w->style = (w->style & ~0x20000000u) | 0x01000000;
            Gdi::RECT m;
            if (MaximizedRect(h, m))
                MoveWindow(h, m.left, m.top, m.right - m.left, m.bottom - m.top, defer_geometry);
        }
    }
    else if (command == 1 || command == 4 || command == 9)
    {
        const bool maximize = command == 9 && (old_style & 0x20000000) && w->restore_maximized;
        w->style &= ~0x21000000u;
        if (maximize)
        {
            w->style |= 0x01000000;
            Gdi::RECT m;
            if (MaximizedRect(h, m))
                MoveWindow(h, m.left, m.top, m.right - m.left, m.bottom - m.top, defer_geometry);
        }
        else if ((old_style & 0x21000000) && w->normal_saved)
        {
            const auto r = w->normal;
            MoveWindow(h, r.left, r.top, r.right - r.left, r.bottom - r.top, defer_geometry);
        }
    }
    w->visible = command != 0;
    if (w->visible && !old) ++w->shown;
    GeometryChanged();
    USER_TRACE("SHOW hwnd=%u command=%u visible=%u old=%u\n", h, command, w->visible ? 1u : 0u, old ? 1u : 0u);
    Layout(h);
    // USER restores the SPB, or leaves the exposed area
    // invalid, during the hide itself. A subsequent show must not capture
    // pixels from this window, even when it reuses an existing record.
    // An EndDeferWindowPos batch finishes all its copies together.
    if (old && !w->visible && !defer_geometry) SettleGeometry();
    // Win98 shows a window through SetWindowPos with SWP_SHOWWINDOW and
    // HWND_TOP; only the no-activate show commands add SWP_NOZORDER. A
    // top-level window shown after creation therefore reaches the front of its
    // band, and the owned pass then keeps its own popups above it.
    if (w->visible && !old && !w->parent && command != 4 && command != 7 && command != 8)
        OrderWindow(h, 0);
    ShellVisibility(h, *w);
    if (w->visible && (!old || ((old_style ^ w->style) & 0x21000000)))
        InvalidateRect(h, nullptr, true);
    // SetWindowPos with SWP_SHOWWINDOW invalidates the whole area the window
    // now covers, and that area holds its children: every visible descendant
    // owes a full paint, RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN through the
    // original walk. A child is not always invalidated by its own show: a
    // control that writes WS_VISIBLE back with SetWindowLong in WM_CREATE,
    // as COMCTL32's tab control does with lpcs->style|WS_CLIPSIBLINGS, is
    // already visible when CreateWindow shows it, and so its first paint came
    // only from its own small invalidations; DXDIAG's row of tabs stayed
    // empty. Under a hidden ancestor the walk does nothing; the
    // ancestor's own show covers the subtree later.
    if (w->visible && !old)
        RedrawWindow(h, nullptr, 0, 0x85);
    dirty = true;
    return old;
}
// The state half of a minimize, maximize or restore, before the window is
// placed: USER and Windows 98 set WS_MINIMIZE and WS_MAXIMIZE and keep
// what the way back needs (the normal rectangle, and whether a minimized
// window was maximized, placement flag 2 ) before the one
// DeferWindowPos. `command` is 6 for a minimize, 3 for a maximize
// and 1 for the normal state; `target` gets the rectangle of the normal
// state, the icon rectangle for a minimize, and the current one otherwise.
// USER. A nonactivating minimize goes behind the last following
// sibling with the same owner, within its topmost band and before the shell
// bottom window. No eligible sibling means SWP_NOZORDER.
HWND MinimizeInsertAfter(HWND h)
{
    const auto *w = Find(h);
    if (!w) return 0;
    HWND after = 0, at = w->next;
    for (unsigned n = 0; at && n < MaxWindows; ++n)
    {
        const auto *candidate = Find(at);
        if (!candidate || (candidate->nonclient_flags & 0x10000000u) ||
            ((candidate->extended ^ w->extended) & 8)) break;
        if (candidate->owner == w->owner) after = at;
        at = candidate->next;
    }
    return after;
}
bool SetShowState(HWND h, int command, Gdi::RECT &target)
{
    auto *w = Find(h);
    if (!w || h == Desktop) return false;
    const auto old_style = w->style;
    target = {w->x, w->y, w->x + w->width, w->y + w->height};
    if (command == 6 || command == 3)
    {
        if (!(old_style & 0x21000000))
        {
            w->normal = target;
            w->normal_saved = true;
        }
        if (command == 6)
        {
            if (!(old_style & 0x20000000)) w->restore_maximized = old_style & 0x01000000;
            w->style = (w->style & ~0x01000000u) | 0x20000000;
            const int cx = settings.Word(0x350), cy = settings.Word(0x352);
            if (w->minimum_position.x == -1 && w->minimum_position.y == -1)
            {
                w->minimum_position = {3000, 3000};
                if ((w->style & 0xc0000000u) == 0x40000000u)
                    if (const auto *parent = Find(w->parent))
                    {
                        const auto area = Client(*parent);
                        const int dx = settings.Word(0x33c), dy = settings.Word(0x33e);
                        int x = 0, y = area.bottom - cy;
                        for (unsigned attempts = 0; attempts < MaxWindows && dx > 0 && dy > 0; ++attempts)
                        {
                            bool occupied = false;
                            for (const auto &other : windows)
                                if (&other != w && other.used && other.parent == w->parent &&
                                    (other.style & 0x20000000u) &&
                                    other.minimum_position.x == x && other.minimum_position.y == y)
                                    occupied = true;
                            if (!occupied) break;
                            x += dx;
                            if (x + cx > area.right) { x = 0; y -= dy; }
                        }
                        w->minimum_position = {x, y};
                    }
            }
            target = {w->minimum_position.x, w->minimum_position.y,
                      w->minimum_position.x + cx, w->minimum_position.y + cy};
        }
        else w->style = (w->style & ~0x20000000u) | 0x01000000;
    }
    else
    {
        w->style &= ~0x21000000u;
        if ((old_style & 0x21000000) && w->normal_saved) target = w->normal;
    }
    GeometryChanged();
    USER_TRACE("SHOW STATE hwnd=%u command=%u style=%08x\n", h, unsigned(command), w->style);
    dirty = true;
    return true;
}
bool RestoresMaximized(HWND h) { auto *w = Find(h); return w && w->restore_maximized; }
void PlacementPoints(HWND h, Gdi::POINT &minimum, Gdi::POINT &maximum)
{
    if (auto *w = Find(h)) { minimum = w->minimum_position; maximum = w->maximum_position; }
}
bool NormalWindowRect(HWND h, Gdi::RECT *r)
{
    auto *w = Find(h);
    if (!w || !r)
        return false;
    *r = (w->style & 0x21000000) && w->normal_saved
             ? w->normal
             : Gdi::RECT{w->x, w->y, w->x + w->width, w->y + w->height};
    return true;
}
bool SetNormalWindowRect(HWND h, const Gdi::RECT &r)
{
    auto *w = Find(h);
    if (!w || h == Desktop)
        return false;
    const long long width = (long long)r.right - r.left, height = (long long)r.bottom - r.top;
    if (width < 0 || height < 0 || width > 32767 || height > 32767)
        return false;
    if (w->style & 0x21000000)
    {
        w->normal = r;
        w->normal_saved = true;
        return true;
    }
    return MoveWindow(h, r.left, r.top, int(width), int(height));
}
void FinishWindowPositions() { SettleGeometry(); }
bool MoveWindow(HWND h, int x, int y, int width, int height, bool defer_geometry)
{
    auto *w = Find(h); if (!w || h == Desktop || width < 0 || height < 0 || width > 32767 || height > 32767) return false;
    const bool resized=w->width!=width||w->height!=height;
    w->x = x; w->y = y; w->width = width; w->height = height; Layout(h); Metrics(*w);
    if (w->style & 0x20000000u) w->minimum_position = {x, y};
    if (w->style & 0x01000000u)
    {
        const bool top = !w->parent || w->parent == Desktop;
        // USER and 2EC4 update the work-area flag from the
        // requested geometry; USER then saves a nonstandard maximum.
        const bool full = top && x <= settings.work[0] && y <= settings.work[1] &&
            width >= settings.work[2] - settings.work[0] && height >= settings.work[3] - settings.work[1];
        if (full) w->nonclient_flags |= 0x200u;
        else w->nonclient_flags &= ~0x200u;
        w->maximum_position_saved = !full;
        if (full) w->maximum_position = {-1, -1};
        else
        {
            const bool work = top && (w->style & 0x10000u) && (w->style & 0xc00000u);
            w->maximum_position = {x - (top ? work ? settings.work[0] : settings.monitor[0] : 0),
                                   y - (top ? work ? settings.work[1] : settings.monitor[1] : 0)};
        }
    }
    if(resized){
        w->update.Clear();
        // SetWindowPos invalidates exposed geometry with RDW_ALLCHILDREN
        // (USER). A resized ancestor without WS_CLIPCHILDREN
        // erases even grandchildren below a child that has that style.
        // The ordinary InvalidateRect walk stops there and left Explorer's
        // already painted toolbars blank after WorkerA resized.
        RedrawWindow(h,nullptr,0,0x85);
    }
    // EndDeferWindowPos copies all moving windows together. Direct native
    // moves finish here, before another saved window can be shown.
    if (!defer_geometry) SettleGeometry();
    dirty = true; return true;
}
bool GetClientRect(HWND h, Gdi::RECT *r) { auto *w = Find(h); if (!w || !r) return false; *r = Client(*w); return true; }
bool AdjustWindowRect(Gdi::RECT *r, unsigned style, unsigned menu, unsigned extended)
{
    if (!r)
        return false;
    NonclientEnvironment e;
    Win98Nonclient::Rect rectangle{r->left, r->top, r->right, r->bottom};
    const auto result = Win98Nonclient::Adjust(e, rectangle, style, menu, extended);
    *r = {rectangle.left, rectangle.top, rectangle.right, rectangle.bottom};
    return result != 0;
}
bool GetWindowRect(HWND h, Gdi::RECT *r) { auto *w = Find(h); if (!w || !r) return false; auto p = Origin(*w); *r = {p.x,p.y,p.x+w->width,p.y+w->height}; return true; }
bool ScreenToClient(HWND h, Gdi::POINT *p) { auto *w = Find(h); if (!w || !p) return false; auto o = Origin(*w); p->x -= o.x + ClientLeft(*w); p->y -= o.y + ClientTop(*w); return true; }
void CursorPosition(Gdi::POINT *p) { if (p) *p = {mouse_x, mouse_y}; }
bool SetWindowText(HWND h, const char *text)
{
    auto *w = Find(h); if (!w || !text) return false;
    if (w->edit) { if (Length(text) >= TextEdit::Capacity) return false; w->edit->Reset(text); Metrics(*w); w->modified = false; }
    else Copy(w->title, text, sizeof w->title);
    if (w->shell_announced) NotifyShell(6, h);
    dirty = true; return true;
}
unsigned GetWindowText(HWND h, char *out, unsigned capacity)
{
    auto *w = Find(h); if (!w || !out || !capacity) return 0;
    Copy(out, w->edit ? w->edit->Data() : w->title, capacity); return Length(out);
}
unsigned TextLength(HWND h) { auto *w = Find(h); return !w ? 0 : w->edit ? w->edit->Length() : Length(w->title); }
unsigned CaptionLength(HWND h){auto*w=Find(h);return w?Length(w->title):0;}
unsigned CaptionText(HWND h,char*out,unsigned capacity){auto*w=Find(h);if(!w||!out||!capacity)return 0;Copy(out,w->title,capacity);return Length(out);}
bool SetCaptionText(HWND h,const char*text){auto*w=Find(h);if(!w||!text)return false;Copy(w->title,text,sizeof w->title);if(w->shell_announced)NotifyShell(6,h);dirty=true;return true;}

// BUTTON draws or removes its focus rectangle on WM_SETFOCUS and
// WM_KILLFOCUS. The native buttons read the focus when
// they paint, so a focus change repaints the button that loses it and the one
// that gets it.
void FocusChanged(HWND old,HWND now)
{
    if(old==now)return;
    const HWND windows_changed[2]={old,now};
    for(const HWND h:windows_changed)
        if(auto *w=Find(h))
            if(w->button&&ControlContent(*w)){w->needs_paint=true;dirty=true;}
}
HWND SetFocus(HWND h)
{
    if (h == Desktop || (h && !Find(h)))
        return 0;
    const auto old = focus;
    activation_queue.focus = focus;
    Win98Focus::Publish(activation_queue,h);
    focus = h;
    FocusChanged(old, h);
    HWND root = h;
    for (unsigned i = 0; root && Parent(root) && Parent(root) != Desktop && i < MaxWindows; ++i)
        root = Parent(root);
    auto *w = Find(root);
    if (w && w->ready && w->visible && foreground != root)
    {
        // WM_NCACTIVATE redraws both captions, and every DrawCaption asks
        // again; a zero answer from before is not kept.
        WantDragIcon(root);
        WantDragIcon(foreground);
        foreground = root;
        TouchForegroundChanged();
        activation_queue.previous = activation_queue.active;
        activation_queue.active = root;
        HWND owner = root;
        for (unsigned n = 0; Owner(owner) && n < MaxWindows; ++n)
            owner = Owner(owner);
        if (auto *root_window = Find(owner))
            root_window->popup = root;
        OrderWindow(root, 0);
        NotifyShellActivation(root);
    }
    dirty = true;
    return old;
}
HWND GetFocus() { return focus; }
HWND GetForegroundWindow() { return foreground; }
// USER, the answer of the API: the active window of the foreground
// queue ([ds:0AA6]+20h), visible or not. `foreground` above forgets a window
// when it is hidden (ShellVisibility), which is what the port's own drawing
// and input want; a program must still read the hidden window, as it does
// under Windows after another thread hid it ( hands the activation on
// only for the calling queue) or when nothing could take over.
HWND ForegroundActiveWindow()
{
    if (foreground) return foreground;
    const HWND active = activation_queue.active;
    return active && IsWindow(active) ? active : 0;
}
static unsigned idle_since = 0, display_power = 0;
static bool saver_requested = false;
void NoteActivity(unsigned now)
{
    idle_since = now;
    saver_requested = false;
    if (display_power)
    {
        display_power = 0;
        dirty = true;
    }
}
unsigned IdleSince() { return idle_since; }
unsigned DisplayPower() { return display_power; }
void SetDisplayPower(unsigned level)
{
    if (display_power != level)
        dirty = true;
    display_power = level;
}
bool SaverRequested() { return saver_requested; }
void SetSaverRequested(bool requested) { saver_requested = requested; }
Win98Activation::Window ActivationWindow(HWND h)
{
    auto *w = Find(h);
    if (!w)
        return {};
    return {w->next,
            w->first,
            h == Desktop ? 0 : Container(*w),
            w->owner,
            w->popup ? w->popup : h,
            w->thread,
            Style(h),
            w->extended,
            w->activation_flags};
}
void WriteActivationWindow(HWND h, const Win98Activation::Window &node)
{
    if (auto *w = Find(h))
    {
        w->popup = node.popup;
        w->activation_flags = node.flags;
    }
}
Win98Activation::Queue ActivationQueue()
{
    auto result = activation_queue;
    result.focus = focus;
    return result;
}
void WriteActivationQueue(const Win98Activation::Queue &q)
{
    activation_queue = q;
    const bool changed = foreground != q.active;
    if (changed) { WantDragIcon(foreground); WantDragIcon(q.active); } // as above
    foreground = q.active;
    // Activation first writes q.previous while q.active still names the old
    // window. That bookkeeping must not cancel the click's new contact.
    if (changed) TouchForegroundChanged();
    FocusChanged(focus, q.focus);
    focus = q.focus;
    dirty = true;
}
unsigned ForegroundQueue() { return foreground_queue; }
Win98Activation::Globals &ActivationGlobals() { return activation_globals; }
static ThreadQueueSlot *QueueSlot(unsigned thread, bool create)
{
    if (!thread) return nullptr;
    ThreadQueueSlot *empty = nullptr;
    for (auto &slot : thread_queues)
    {
        if (slot.thread == thread) return &slot;
        if (!slot.thread && !empty) empty = &slot;
    }
    if (create && empty) { *empty = {}; empty->thread = thread; return empty; }
    return nullptr;
}
// changes the foreground queue. The foreground record stays: the
// activation that follows works on it and deactivates the old foreground's
// window; the caller parks what the old queue keeps (Application::
// SwitchForeground). A record the new queue had of its own is superseded.
void SetForegroundQueue(unsigned thread)
{
    if (thread == foreground_queue) return;
    foreground_queue = thread;
    if (auto *slot = QueueSlot(thread, false)) slot->queue = {};
}
Win98Activation::Queue ThreadActivation(unsigned thread)
{
    if (thread && thread == foreground_queue) return ActivationQueue();
    auto *slot = QueueSlot(thread, false);
    return slot ? slot->queue : Win98Activation::Queue{};
}
void WriteThreadActivation(unsigned thread, const Win98Activation::Queue &q)
{
    if (thread && thread == foreground_queue) { WriteActivationQueue(q); return; }
    if (auto *slot = QueueSlot(thread, true)) slot->queue = q;
}
// a queue that goes away while it is the foreground leaves no
// foreground behind.
void ForgetThreadQueue(unsigned thread)
{
    if (!thread) return;
    if (thread == foreground_queue)
    {
        foreground_queue = 0;
        activation_queue = {};
        foreground = 0;
        focus = 0;
        dirty = true;
    }
    if (thread == last_input_queue) last_input_queue = 0;
    if (thread == activation_globals.previous_queue) activation_globals.previous_queue = 0;
    if (auto *slot = QueueSlot(thread, false)) *slot = {};
    for (auto &slot : capture_slots) if (slot.used && slot.thread == thread) slot = {};
}
bool TakeQueuePass(unsigned thread)
{
    auto *slot = QueueSlot(thread, false);
    if (!slot || !slot->pass) return false;
    slot->pass = false;
    return true;
}
void GrantQueuePass(unsigned thread) { if (auto *slot = QueueSlot(thread, true)) slot->pass = true; }
bool QueuePass(unsigned thread) { auto *slot = QueueSlot(thread, false); return slot && slot->pass; }
// The focus window a queue had when it lost the foreground. The queue itself
// keeps no focus then (its window got WM_KILLFOCUS); the window gets it back
// when the queue's window takes the foreground again.
void KeepQueueFocus(unsigned thread, unsigned hwnd) { if (auto *slot = QueueSlot(thread, hwnd != 0)) slot->kept_focus = hwnd; }
unsigned KeptQueueFocus(unsigned thread) { auto *slot = QueueSlot(thread, false); return slot ? slot->kept_focus : 0; }
// posts the activation into the new foreground queue; the queue's own
// thread carries it out when it next reads its queue.
void PostQueueActivation(unsigned thread, unsigned hwnd) { if (auto *slot = QueueSlot(thread, true)) slot->activation = hwnd; }
unsigned TakeQueueActivation(unsigned thread)
{
    auto *slot = QueueSlot(thread, false);
    if (!slot || !slot->activation) return 0;
    const unsigned hwnd = slot->activation;
    slot->activation = 0;
    return hwnd;
}
bool HasQueueActivation(unsigned thread) { auto *slot = QueueSlot(thread, false); return slot && (slot->activation || slot->mouse_activation || slot->leave); }
// with no window: the foreground leaves the guest's queues. The old
// foreground queue hears of it when its thread next reads its queue.
void PostQueueLeave(unsigned thread, bool leave) { if (auto *slot = QueueSlot(thread, leave)) slot->leave = leave; }
bool TakeQueueLeave(unsigned thread)
{
    auto *slot = QueueSlot(thread, false);
    if (!slot || !slot->leave) return false;
    slot->leave = false;
    return true;
}
// A press on a window of another thread: USER activates the window when its
// own thread reads the press from its queue ( routes it there, and the
// mouse activation runs in the receiving queue), not on the thread that
// routed the input.
void PostQueueMouseActivation(unsigned thread, unsigned hwnd, bool focus)
{
    if (auto *slot = QueueSlot(thread, true)) { slot->mouse_activation = hwnd; slot->mouse_focus = focus; }
}
bool TakeQueueMouseActivation(unsigned thread, unsigned &hwnd, bool &focus)
{
    auto *slot = QueueSlot(thread, false);
    if (!slot || !slot->mouse_activation) return false;
    hwnd = slot->mouse_activation; focus = slot->mouse_focus;
    slot->mouse_activation = 0; slot->mouse_focus = false;
    return true;
}
bool StartPermission(unsigned pid) { auto *p = Process(pid); return p && (p->flags & 4); }
bool TakeStartPermission(unsigned pid)
{
    auto *p = Process(pid);
    if (!p || !(p->flags & 4)) return false;
    p->flags &= ~4u;
    return true;
}
// Not locked when no press was routed yet or pid took the last one,
// when pid owns the foreground or there is none, when the process still
// holds its start permission (flags 24h), or when the last press is older
// than the time-out.
bool ForegroundUnlocked(unsigned pid)
{
    if (!last_input_queue || last_input_process == pid) return true;
    const unsigned owner = foreground_queue ? WindowProcess(activation_queue.active) : 0;
    if (!foreground_queue || !owner || owner == pid) return true;
    auto *p = Process(pid);
    if (p && (p->flags & 0x24)) return true;
    return MessageClock() - last_input_time > ForegroundLockTimeout;
}
// at the routing of a hardware message: a key or button press
// takes the start permission from every process and names the
// queue that got it.
static void NoteInputPress(HWND h, unsigned time)
{
    for (auto *p=processes;p;p=p->next) p->flags &= ~4u;
    auto *w = Find(h);
    if (!w) return;
    last_input_queue = w->thread;
    last_input_process = w->process;
    last_input_time = time;
    // A queue handling the user's press may explicitly take the foreground
    // through SetFocus as well as through WM_MOUSEACTIVATE. DefView declines
    // the latter and its list view sets focus when it handles the click.
    // Without this pass, the folder rises but the DOS queue keeps activation
    // and its taskbar button stays pressed. Pointer motion grants
    // nothing; the permission is consumed by ForegroundPermission.
    GrantQueuePass(w->thread);
}
// A start from outside the guest (a double click in the Finder, the menu of
// the host) is the user's press on the shell: under Win98 the program is
// started by a click on its icon, and that click is the last input. Without
// it the last press stays with the program the user played before,
// keeps that program unlocked, and it takes the foreground back from the
// program just started (Moorhuhn over MJWIN).
void NoteHostPress(HWND h) { NoteInputPress(h, MessageClock()); }
void NotifyActivation(HWND h)
{
    auto *w = Find(h);
    if (!w || !w->ready || !w->visible || w->parent) return;
    // USER: an active tool window has no button, and the shell hears
    // HSHELL_WINDOWACTIVATED with no window, so the taskbar lifts
    // the button that was pressed. Progman, the desktop, is one: minimizing
    // the last folder window left its button pressed.
    if (w->extended & 0x80) NotifyShellActivation(0);
    else if (!w->owner) NotifyShellActivation(h);
}
void RefreshShellActivation()
{
    // SetWindowPos can change fullscreen coverage without changing the
    // active window. Send the transition after the complete layout batch.
    // Hiding an owned popup clears the visible foreground before activation
    // is handed back. USER's active-window record still names that popup;
    // its fullscreen owner continues to cover the display. Looking only at
    // `foreground` briefly told Explorer to raise its taskbar between the
    // Half-Life system-test popups, after the setup had stopped painting.
    const HWND active = ForegroundActiveWindow();
    const bool fullscreen = ShellFullscreen(active);
    if (fullscreen == shell_fullscreen) return;
    shell_fullscreen = fullscreen;
    NotifyShell(4, active, fullscreen ? 1 : 0);
}

HWND GetDesktopWindow() { return desktop.used ? Desktop : 0; }
unsigned WindowThread(HWND h) { auto *w = Find(h); return w ? w->thread : 0; }
HWND ThreadWindow(unsigned process, unsigned thread)
{
    for (unsigned i=0;i<MaxWindows;++i)
        if (windows[i].used && windows[i].process==process && windows[i].thread==thread)
            return Handle(i);
    return 0;
}
Win98Windows::Node WindowNode(HWND h)
{
    auto *w = Find(h);
    if (!w) return {};
    return {w->next, w->first, h == Desktop ? 0 : Container(*w), w->owner,
            w->thread, Style(h), w->extended};
}
static Win98Windows::Tree WindowTree()
{
    return {nullptr, [](void *, unsigned h) { return WindowNode(h); }, GetDesktopWindow(), MaxWindows + 1};
}
HWND GetWindow(HWND h, unsigned command)
{
    return Find(h) ? Win98Windows::Get(WindowTree(), h, command) : 0;
}
HWND GetTopWindow(HWND h)
{
    return !h || Find(h) ? Win98Windows::Top(WindowTree(), h) : 0;
}
bool WindowSnapshot(HWND parent, unsigned *out, unsigned capacity, unsigned &count)
{
    count = 0;
    auto *w = Find(parent ? parent : GetDesktopWindow());
    return w && Win98Windows::Snapshot(WindowTree(), w->first, parent ? 3 : 2, out, capacity, count);
}
// Native providers for the implemented USER owned-window z-order pass. The
// window records, the deferred-entry list and the window check are native; the
// decisions in win98_owner_zorder.h are the original ones.
struct OwnerZOrderEnvironment
{
    Win98OwnerZOrder::Entry list[MaxWindows];
    unsigned entries = 0;
    unsigned Capacity() const { return MaxWindows; }
    Win98OwnerZOrder::Node Read(HWND h)
    {
        const auto *w = Find(h);
        if (!w) return {};
        // The original stores the desktop's parent as a null pointer.
        return {w->next, w->first, h == Desktop ? HWND(0) : Container(*w), w->owner, 0};
    }
    bool IsWindow(HWND h) { return Find(h) != nullptr; }
    void ClearOwner(HWND h) { if (auto *w = Find(h)) w->owner = 0; }
    // Native USER has no equivalent of the USER ds:00C4h z-order lock.
    bool Disabled() { return false; }
    unsigned Count() const { return entries; }
    void SetCount(unsigned n) { entries = n; }
    Win98OwnerZOrder::Entry Get(unsigned i) { return i < MaxWindows ? list[i] : Win98OwnerZOrder::Entry{}; }
    void Set(unsigned i, const Win98OwnerZOrder::Entry &value) { if (i < MaxWindows) list[i] = value; }
    bool Append(const Win98OwnerZOrder::Entry &value)
    {
        if (entries >= MaxWindows) return false;
        list[entries++] = value;
        return true;
    }
};
// USER runs after every sibling-order change and keeps the windows a
// top-level window owns in front of it. The original hands its group to the
// position engine; this provider performs the sibling relinking directly.
void OwnedZOrder(HWND h)
{
    auto *w = Find(h);
    if (!w || h == Desktop || w->parent) return;
    OwnerZOrderEnvironment e;
    e.list[0] = {h, w->previous, 0};
    e.entries = 1;
    Win98OwnerZOrder::Arrange(e, 0);
    if (e.entries < 2) return;
    for (unsigned i = 0; i < e.entries; ++i)
    {
        const auto entry = e.list[i];
        auto *target = Find(entry.window);
        if (!target || target->parent || entry.after == Win98OwnerZOrder::Bottom) continue;
        if (entry.after && !Find(entry.after)) continue;
        Unlink(entry.window, *target);
        LinkAfter(entry.window, *target, entry.after);
    }
}
// USER, the place HWND_TOP means for a window that is not topmost
// (called from the check of a WINDOWPOS). `front` is the top of the
// band, behind the topmost windows. It is the answer for a window of
// the foreground queue, when there is no foreground, and for a top-level
// window whose queue holds flag 10h or whose process still has its start
// permission. Every other window stays behind the windows of
// the other tasks: the walk from the top passes them and the topmost and
// hidden windows of its own task and ends at the first visible window of its
// own task, or at the window itself, which then keeps its place. So SetFocus,
// SetActiveWindow, BringWindowToTop and ShowWindow of a program in the
// background order its own windows and never cover the foreground program;
// the autorun window of Plus! 98 sets the focus under the pointer and came
// in front of the setup wizard. When the walk finds no window of the
// task, the place is behind the first visible unowned window of the task of
// the foreground's active window.
static HWND BackgroundFrontPosition(HWND h, const Window &w, HWND front)
{
    if (Container(w) != Desktop || !w.thread || !foreground_queue || w.thread == foreground_queue) return front;
    if (QueuePass(w.thread)) return front;
    if (const auto *p = Process(w.process); p && (p->flags & 4)) return front;
    HWND place = front, at = front ? front : desktop.first;
    for (unsigned n = 0; at && at != h && n < MaxWindows; ++n)
    {
        const auto *c = Find(at);
        if (!c) { at = 0; break; }
        if (c->nonclient_flags & 0x10000000u) break;
        if (c->thread == w.thread && !(c->extended & 8) && c->visible) break;
        place = at;
        at = c->next;
    }
    if (at) return place;
    const unsigned task = WindowThread(activation_queue.active);
    place = front;
    at = front ? front : desktop.first;
    for (unsigned n = 0; at && n < MaxWindows; ++n)
    {
        const auto *c = Find(at);
        if (!c || (c->nonclient_flags & 0x10000000u)) break;
        if (c->thread == task && at != h)
        {
            if (c->owner) place = at;
            else if (c->visible) { place = at; break; }
        }
        at = c->next;
    }
    return place;
}
unsigned PrepareOwnedPositions(const unsigned position[7], unsigned (*out)[7], unsigned capacity)
{
    const HWND h = position[0];
    const auto *w = Find(h);
    if (!w || !capacity) return 0;
    if (w->parent || (position[6] & 4u))
    {
        memcpy(out[0], position, 7 * sizeof(unsigned));
        return 1;
    }
    OwnerZOrderEnvironment e;
    HWND after = position[1];
    if (after == ~0u || after == ~1u) after = 0;
    else if (!after && !(w->extended & 8)) after = BackgroundFrontPosition(h, *w, FrontPosition(*w));
    e.list[0] = {h, after, position[6]};
    e.entries = 1;
    // USER expands owners before 18F4 sends any positioning messages.
    // Child sibling lists do not go through this top-level owner pass.
    if (!w->parent && h != Desktop) Win98OwnerZOrder::Arrange(e, 0);
    if (e.entries > capacity) return 0;
    for (unsigned i = 0; i < e.entries; ++i)
    {
        const auto &entry = e.list[i];
        memset(out[i], 0, 7 * sizeof(unsigned));
        out[i][0] = entry.window; out[i][1] = entry.after; out[i][6] = entry.flags;
        if (entry.window == h)
        {
            for (unsigned n = 2; n < 6; ++n) out[i][n] = position[n];
            out[i][6] = (position[6] & ~4u) | (entry.flags & 4u);
            // Retain an actual topmost-band transition for OrderWindow. A
            // repeated request is normalized before WM_WINDOWPOSCHANGING.
            if ((position[1] == ~0u && !(w->extended & 8)) ||
                (position[1] == ~1u && (w->extended & 8))) out[i][1] = position[1];
        }
    }
    return e.entries;
}
bool OrderUnchanged(HWND h, HWND after)
{
    // USER after insertion-handle normalization: a no-op Z-order
    // request gains SWP_NOZORDER, even though WM_WINDOWPOSCHANGING was sent.
    const auto *w = Find(h);
    if (!w || h == Desktop) return false;
    if (after == h) return true;
    if (after == 1) return !(w->extended & 8) && !w->next;
    if (after == ~0u || after == ~1u)
    {
        if (bool(w->extended & 8) != (after == ~0u)) return false;
        after = 0;
    }
    HWND front = 0;
    if (Container(*w) == Desktop && !(w->extended & 8))
        for (HWND at = desktop.first; at;)
        {
            const auto *entry = Find(at);
            if (!entry) break;
            if (at != h) { if (!(entry->extended & 8)) break; front = at; }
            at = entry->next;
        }
    if (!after)
        after = (w->extended & 8) ? front : BackgroundFrontPosition(h, *w, front);
    else
    {
        const auto *entry = Find(after);
        if (!entry || Container(*entry) != Container(*w)) return false;
        if (Container(*w) == Desktop && bool(entry->extended & 8) != bool(w->extended & 8))
        {
            if (w->extended & 8) return false;
            after = front;
        }
    }
    return after == h || after == w->previous;
}
bool OrderWindow(HWND h, HWND after, bool order_owned)
{
    auto *w = Find(h);
    if (!w || h == Desktop) return false;
    unsigned shell_flags=0;ConstrainShellPosition(h,after,shell_flags);
    if (h == after) return true;
    if (after > 1 && after != ~0u && after != ~1u)
    {
        const auto *other = Find(after);
        if (!other || after == Desktop || Container(*other) != Container(*w)) return false;
    }
    const HWND behind = !after && !(w->extended & 8) ? BackgroundFrontPosition(h, *w, FrontPosition(*w)) : 0;
    if (behind == h) return true;
    Unlink(h, *w);
    if (after == ~0u) { w->extended |= 8; after = 0; }
    else if (after == ~1u) { w->extended &= ~8u; after = 0; }
    else if (after == 1)
    {
        w->extended &= ~8u;
        after = Find(Container(*w))->last;
        LinkAfter(h, *w, after); if (order_owned) OwnedZOrder(h); dirty = true; return true;
    }
    if (!after) after = behind ? behind : FrontPosition(*w);
    else if (Container(*w) == Desktop)
    {
        if (!(Find(after)->extended & 8)) w->extended &= ~8u;
        else if (!(w->extended & 8)) after = FrontPosition(*w);
    }
    LinkAfter(h, *w, after);
    if (order_owned) OwnedZOrder(h);
    dirty = true;
    return true;
}
// A button or a label repaints on WM_ENABLE in its class procedure (the
// grey text of a disabled button); the native controls draw here, so the
// change of state asks for their paint (the wizard's Back button).
bool EnableWindow(HWND h, bool enabled) { auto *w = Find(h); if (!w) return false; const bool old = !w->enabled; if(w->enabled!=enabled&&ControlContent(*w))w->needs_paint=true; w->enabled = enabled; if(w->edit)w->edit_color_pending=true; dirty=true;return old; }
HWND GetDlgItem(HWND h,unsigned id){
 if(!IsWindow(h))return 0;
 struct E{unsigned FirstChild(unsigned h){return GetWindow(h,5);}unsigned NextSibling(unsigned h){return GetWindow(h,2);}unsigned ControlID(unsigned h){return User::ControlID(h);}}e;
 return Win98WindowText::DialogItem(e,h,id);
}
#include "user_window_hit.inc"
#include "user_window_region_api.inc"
HWND ChildWindowFromPoint(HWND h,int x,int y){return PointWindow(1,h,x,y,0,nullptr,nullptr,1);}
// USER maps NULL to the desktop; Windows 98 selects a free DCE.
// Guest leases have their own state, including SaveDC, so retaining screen
// DCs cannot consume the compositor's save stack.
Gdi::HDC ScreenLease(HWND h, bool frame)
{
    const HWND target = h ? h : Desktop;
    auto *w = Find(target);
    if (!w) return 0;
    const auto dc = Acquire(target, *w, false, 0, frame || !h);
    if (dc && !h) w->lease[w->leases - 1].screen_dc = true;
    return dc;
}
// Every window DC is a DCE on the screen surface (src/user_dce.inc).
Gdi::HDC GetDC(HWND h)
{
    if (h && !Find(h)) return 0;
    if(auto *w=Find(h))return Acquire(h,*w,false);
    return ScreenLease(h,false);
}
// GetDCEx selects an existing persistent DCE unless CACHE/PARENTCLIP or
// the current class style requests a cache entry. CS_CLASSDC's first window
// can use its class DCE without USESTYLE; subsequent windows need USESTYLE.
Gdi::HDC GetDCEx(HWND h,unsigned flags)
{
    auto *w=Find(h);if(!w)return 0;
    unsigned selection=0;
    if(!(flags&(2u|0x20u))){
        if(flags&0x10000u){
            const unsigned current=ClassAccess(h,-26,0,2);
            if(current&OwnDC){if(!(w->class_style&OwnDC))return 0;selection=OwnDC;}
            else if(current&ClassDC)selection=w->own_dc?OwnDC:ClassDC;
        }else{
            if(w->own_dc)selection=OwnDC;
            else if(auto *e=ClassEntry(*w)){if(e->owner!=h)return 0;selection=ClassDC;}
            else return 0;
        }
    }
    return Acquire(h,*w,false,selection,(flags&1)!=0);
}
// USER with DCX_WINDOW: the origin on the window corner, the clip the
// visible region of the whole window.
Gdi::HDC GetWindowDC(HWND h)
{
    if (h && !Find(h)) return 0;
    return ScreenLease(h,true);
}
// The rectangles of the window's update region, in client coordinates.
unsigned UpdateRectangles(HWND h, Gdi::RECT *out, unsigned capacity)
{
    SettleGeometry();
    auto *w=Find(h);if(!w)return 0;
    const unsigned count=w->update.count<capacity?w->update.count:capacity;
    for(unsigned i=0;i<count;++i)out[i]=w->update.rectangles[i];
    return w->update.count;
}
// The visible children of a window, front to back, for DCX_CLIPCHILDREN, and
// the siblings above it for DCX_CLIPSIBLINGS. Both answer screen rectangles.
unsigned VisibleChildRects(HWND h, Gdi::RECT *out, unsigned capacity)
{
    auto *w=Find(h);if(!w)return 0;unsigned count=0;
    for(HWND child=w->first;child;)
    {
        const auto *c=Find(child);if(!c)break;
        if(c->visible&&!(c->style&0x20000000))
        {
            const auto at=Origin(*c);
            if(count<capacity)out[count]={at.x,at.y,at.x+c->width,at.y+c->height};
            ++count;
        }
        child=c->next;
    }
    return count;
}
unsigned SiblingsAboveRects(HWND h, Gdi::RECT *out, unsigned capacity)
{
    auto *w=Find(h);if(!w)return 0;unsigned count=0;
    for(HWND sibling=w->previous;sibling;)
    {
        const auto *above=Find(sibling);if(!above)break;
        if(above->visible&&!(above->style&0x20000000))
        {
            const auto at=Origin(*above);
            if(count<capacity)out[count]={at.x,at.y,at.x+above->width,at.y+above->height};
            ++count;
        }
        sibling=above->previous;
    }
    return count;
}

// The private surface a popup menu paints into stands for the popup
// window's DC while its owner-draw items are drawn: on Win98 WM_DRAWITEM
// carries the #32768 window's DC, and WindowFromDC names that window.
static Gdi::HDC menu_surface_dc = 0;
static HWND menu_surface_window = 0;
HWND WindowFromDC(Gdi::HDC dc)
{
    if (dc && dc == menu_surface_dc && Find(menu_surface_window)) return menu_surface_window;
    // Win16 callers, including DDRAW16's InquireVisRgn, pass the low word.
    // Resolve its live GDI generation before comparing it with DCE handles.
    dc = Gdi::FullHandle(dc);
    if (!dc) return 0;
    if (desktop.used && DCWindow(desktop, dc)) {
        // Preserve the NULL-window association of GetDC(NULL), as distinct
        // from a caller explicitly asking for the desktop window's DC.
        for (unsigned i = 0; i < desktop.leases; ++i)
            if (desktop.lease[i].dc == dc && desktop.lease[i].screen_dc) return 0;
        return Desktop;
    }
    for (unsigned i = 0; i < MaxWindows; ++i) {
        const auto &w = windows[i];
        if (w.used && DCWindow(w, dc))
            return Handle(i);
    }
    return 0;
}

// EnumDisplayMonitors validates a USER DCE, not just any GDI display DC. A screen lease has the desktop as its window; memory
// DCs, CreateDC("DISPLAY") and the private owner-draw menu surface do not.
HWND DisplayDCWindow(Gdi::HDC dc)
{
    dc=Gdi::FullHandle(dc);if(!dc)return 0;
    if(desktop.used&&DCWindow(desktop,dc))return Desktop;
    for(unsigned i=0;i<MaxWindows;++i)
        if(windows[i].used&&DCWindow(windows[i],dc))return Handle(i);
    return 0;
}

bool SetDCRegion(HWND h,Gdi::HDC dc,unsigned region,bool intersect) { return SetDCRegionBody(h,dc,region,intersect); }

bool ReleaseDC(HWND h, Gdi::HDC dc)
{
    // USER looks the DC up in its DCE list; the window handle does not
    // choose the entry.
    if(auto *w=Find(h))if(OwnsDC(*w,dc))return Release(*w,dc,false);
    if(desktop.used&&OwnsDC(desktop,dc))return Release(desktop,dc,false);
    for(auto &w:windows)if(w.used&&OwnsDC(w,dc))return Release(w,dc,false);
    return false;
}
Gdi::HDC GetUpdateDC(HWND h)
{
    auto *w=Find(h);if(!w)return 0;
    auto dc=Acquire(h,*w,false);if(!dc)return 0;
    if(!Gdi::IntersectClipRegion(dc,w->update.rectangles,w->update.count))
    {Release(*w,dc,false);return 0;}
    if(w->leases)w->lease[w->leases-1].paint_clip=new RectRegion(w->update);
    if (Locked(h)) EmptyClip(dc);
    return dc;
}
// ScrollWindowEx : the scrolled area is the scroll rectangle,
// or the client, cut to the clip rectangle. The pixels already on the
// window's surface move by (dx,dy); the strips they leave behind are the
// update area, invalidated on request. Answers the region complexity.
int ScrollWindowArea(HWND h, int dx, int dy, const Gdi::RECT *scroll, const Gdi::RECT *clip, Gdi::RECT *update, bool invalidate, bool erase, unsigned update_region, bool scroll_children)
{
    NoteWheelActivity(h);
    auto *w = Find(h);
    if (!w) return 0;
    if (update_region)
    {
        const unsigned empty=Gdi::CreateRegionRect(0,0,0,0);
        if(!empty)return 0;
        const int copied=Gdi::CombineRegions(update_region,empty,0,5);
        Gdi::DeleteObject(empty);
        if(!copied)return 0;
    }
    SettleGeometry();
    auto move_children = [&]() {
        if (!scroll_children || (!dx && !dy)) return;
        const bool settled = settled_serial == geometry_serial;
        for (HWND child = GetWindow(h, 5); child; child = GetWindow(child, 2))
        {
            auto *c = Find(child);
            if (!c) break;
            if (scroll && (c->x >= scroll->right || c->x + c->width <= scroll->left ||
                           c->y >= scroll->bottom || c->y + c->height <= scroll->top)) continue;
            MoveWindow(child, c->x + dx, c->y + dy, c->width, c->height, true);
        }
        // ScrollDC already moved the pixels. Record the child geometry without
        // a second move/exposure pass, which would invent an erase request.
        // The exposed strips below use the children's new positions.
        if (settled)
        {
            const bool previous = settle_quietly;
            settle_quietly = true;
            SettleGeometry();
            settle_quietly = previous;
        }
    };
    int region_kind=1;
    auto export_piece=[&](const Gdi::RECT &r){
        if(!update_region)return;
        const unsigned piece=Gdi::CreateRegionRect(r.left,r.top,r.right,r.bottom);
        if(!piece){region_kind=0;return;}
        region_kind=Gdi::CombineRegions(update_region,update_region,piece,2);
        Gdi::DeleteObject(piece);
    };
    Gdi::RECT area = Client(*w);
    if (scroll) { if (scroll->left > area.left) area.left = scroll->left; if (scroll->top > area.top) area.top = scroll->top; if (scroll->right < area.right) area.right = scroll->right; if (scroll->bottom < area.bottom) area.bottom = scroll->bottom; }
    if (clip) { if (clip->left > area.left) area.left = clip->left; if (clip->top > area.top) area.top = clip->top; if (clip->right < area.right) area.right = clip->right; if (clip->bottom < area.bottom) area.bottom = clip->bottom; }
    if (update) *update = {0, 0, 0, 0};
    if (area.right <= area.left || area.bottom <= area.top) { move_children(); return 1; }
    const int width = area.right - area.left, height = area.bottom - area.top;
    const bool whole = dx <= -width || dx >= width || dy <= -height || dy >= height;
    // The pixels move on the screen, inside the visible region of the client
    // (ScrollDC on the window's DC): a pixel is copied when its source and its
    // target are both visible; what is visible now but had no visible source
    // is owed, like the strips the scroll leaves behind.
    RectList *not_copied = nullptr;
    if (!whole)
    {
        SettleGeometry();
        const auto c = ClientScreenRect(*w);
        const Gdi::RECT screen_area{c.left + area.left, c.top + area.top, c.left + area.right, c.top + area.bottom};
        auto *visible = new RectList, *source = new RectList, *target = new RectList;
        WindowVisible(h, screen_area, false, *visible);
        // A locked window cannot scroll. ScrollDC still adds the scrolled
        // rectangle to its DCE bounds.
        if (Locked(h)) { LockSum(screen_area); visible->Clear(); }
        *source = *visible;
        source->Offset(dx, dy);
        target->Intersect(*visible, *source);
        CopyScreenRects(*target, dx, dy);
        // Visible, scrolled, but not copied: its source was covered.
        not_copied = new RectList;
        *not_copied = *visible;
        not_copied->Subtract(*target);
        delete visible; delete source; delete target;
        screen_touched = true;
        if (AnySaved()) UnderlyingChanged(h, screen_area);
    }
    move_children();
    // USER looks at the window's own update region before it scrolls:
    // what is still owed inside the scrolled area belongs to the pixels that
    // move, so it moves with them. Without this the rectangles of the previous
    // step keep pointing at rows that have travelled on, and a repaint fills
    // stripes of the old positions instead of the new rows.
    if ((dx || dy) && w->update.count)
    {
        RectRegion scrolled;
        bool kept = true;
        for (unsigned i = 0; i < w->update.count && kept; ++i)
        {
            const Win98::Rect &r = w->update.rectangles[i];
            Win98::Rect inside;
            if (Win98::IntersectRect(&inside, &r, &area))
            {
                const Win98::Rect shifted = {inside.left + dx, inside.top + dy,
                                             inside.right + dx, inside.bottom + dy};
                Win98::Rect clipped;
                if (Win98::IntersectRect(&clipped, &shifted, &area))
                    kept = scrolled.Append(clipped);
            }
            // Everything outside the scrolled area stays where it is.
            if (kept) kept = RectRegion::Difference(scrolled, r, area);
        }
        if (kept) { scrolled.Coalesce(); w->update = scrolled; }
        else
        {
            // The bounded region cannot hold the split. Owing the whole
            // scrolled area is coarse but never leaves a stale rectangle.
            w->update.Clear();
            w->update.Append(area);
        }
    }
    Gdi::RECT exposed[2]; unsigned count = 0;
    if (whole) exposed[count++] = area;
    else
    {
        if (dy > 0) exposed[count++] = {area.left, area.top, area.right, area.top + dy};
        else if (dy < 0) exposed[count++] = {area.left, area.bottom + dy, area.right, area.bottom};
        if (dx > 0) exposed[count++] = {area.left, area.top, area.left + dx, area.bottom};
        else if (dx < 0) exposed[count++] = {area.right + dx, area.top, area.right, area.bottom};
    }
    if (!count) { delete not_copied; return 1; }
    Gdi::RECT bounds = exposed[0];
    for (unsigned i = 1; i < count; ++i) { if (exposed[i].left < bounds.left) bounds.left = exposed[i].left; if (exposed[i].top < bounds.top) bounds.top = exposed[i].top; if (exposed[i].right > bounds.right) bounds.right = exposed[i].right; if (exposed[i].bottom > bounds.bottom) bounds.bottom = exposed[i].bottom; }
    if (update) *update = bounds;
    for(unsigned i=0;i<count;++i)export_piece(exposed[i]);
    if (invalidate) for (unsigned i = 0; i < count; ++i) InvalidateRect(h, &exposed[i], erase);
    // A part whose source lay under another window has no pixels to show; it
    // is owed as well, whatever the caller asked (the original takes it from
    // the visible region, USER).
    if (not_copied)
    {
        const auto c = ClientScreenRect(*w);
        for (unsigned i = 0; i < not_copied->n; ++i)
        {
            const auto &r = not_copied->r[i];
            const Gdi::RECT local{r.left - c.left, r.top - c.top, r.right - c.left, r.bottom - c.top};
            bool inside_exposed = false;
            for (unsigned k = 0; k < count && !inside_exposed; ++k)
                inside_exposed = local.left >= exposed[k].left && local.right <= exposed[k].right &&
                                 local.top >= exposed[k].top && local.bottom <= exposed[k].bottom;
            if (!inside_exposed) {export_piece(local);InvalidateRect(h, &local, true);}
        }
        delete not_copied;
    }
    return update_region ? region_kind : count == 1 ? 2 : 3;
}
namespace {
// One window's update region grows (the apply step of for one window).
void InvalidateOne(HWND h, const Gdi::RECT *rect, bool erase)
{
    auto *w=Find(h);if(!w)return;
    const auto client=Client(*w);Gdi::RECT clipped;
    if(!Win98::IntersectRect(&clipped,rect?rect:&client,&client))return;
    if(!w->update.Add(clipped))return;
    w->native_invalid=true;
    USER_TRACE("INVALIDATE hwnd=%u rect=%d,%d,%d,%d erase=%u settle=%u\n",h,clipped.left,clipped.top,clipped.right,clipped.bottom,erase?1u:0u,settling?1u:0u);
    w->wait_paint_observed=false;
    if(erase){w->erase_pending=true;++w->erase_serial;}
    // An invalid control rectangle is a paint request, analogous to QS_PAINT
    // for a custom window procedure.
    if(ControlContent(*w))w->needs_paint=true;
    if(w->edit)w->edit_color_pending=true;
    if(!Custom(*w))dirty=true;
    // Repainting underneath invalidates saved pixels above this area.
    if(AnySaved()){const auto c=ClientScreenRect(*w);UnderlyingChanged(h,{c.left+clipped.left,c.top+clipped.top,c.left+clipped.right,c.top+clipped.bottom});}
}
}
// What a thread did to its windows since a turn of the wheel, counted for
// RouteWheel: a program that scrolls makes something invalid or moves bits.
static unsigned wheel_activity[64];
void NoteWheelActivity(HWND h)
{
    if (auto *w = Find(h)) ++wheel_activity[w->thread & 63];
}
bool InvalidateRect(HWND h, const Gdi::RECT *rect, bool erase)
{
    if (h) NoteWheelActivity(h);
    if(!h)
    {
        bool result=true;
        for(unsigned i=0;i<MaxWindows;++i)if(windows[i].used)
            if(!InvalidateRect(Handle(i),rect,erase))result=false;
        dirty=true;return result;
    }
    if(!Find(h))return false;
    // USER invalidates the rectangle with
    // RDW_INVALIDATE, and RDW_ERASE for a true erase argument: the same walk
    // as RedrawWindow. The walk includes children unless the window
    // carries WS_CLIPCHILDREN, so a parent without it makes the children in
    // the rectangle invalid too; its own paint will cover them.
    // A window that is not on the screen is skipped (bit 10h
    // of the window and every ancestor) and nothing changes; showing the
    // window invalidates it whole.
    if(ShowsTree(h))return RedrawWindow(h, rect, 0, erase ? 5u : 1u);
    return true;
}
// USER, called by the UpdateWindow body before it
// sends WM_PAINT to a window with an update region: it walks up the parents
// as long as they lack WS_CLIPCHILDREN and takes the window's
// rectangle out of the update region of every such parent that still owes a
// paint (Windows 98 calls the RedrawWindow walk with RDW_VALIDATE |
// RDW_NOCHILDREN), so the later paint of the parent does not erase over the
// window that has just painted. It stops at the first parent with
// WS_CLIPCHILDREN, whose paint cannot reach the window anyway.
bool AncestorNeedsPaint(HWND h)
{
    // USER: an ancestor's pending paint postpones RDW_ERASENOW.
    // A WS_CLIPCHILDREN ancestor cannot paint over this child and stops the walk.
    auto *w = Find(h);
    for (unsigned n = 0; w && w->parent && n < MaxWindows; ++n)
    {
        w = Find(w->parent);
        if (!w || (w->style & 0x02000000u)) return false;
        if (w->update.count) return true;
    }
    return false;
}
void ClipAncestorUpdates(HWND h)
{
    auto *w = Find(h);
    // USER calls before subtracting this rectangle from an
    // ancestor's update. Windows 98 rejects an invisible window or ancestor.
    // An explicit UpdateWindow of a hidden child can still call WM_PAINT,
    // but must leave its parent's pending paint intact.
    if (!w || h == Desktop || !ShowsTree(h)) return;
    const Gdi::RECT area = WindowScreenRect(*w);
    for (unsigned n = 0; w && w->parent && n < MaxWindows; ++n)
    {
        auto *parent = Find(w->parent);
        if (!parent || (parent->style & 0x02000000u)) return;
        if (parent->update.count)
        {
            const auto c = ClientScreenRect(*parent);
            const Gdi::RECT local{area.left - c.left, area.top - c.top, area.right - c.left, area.bottom - c.top};
            if (!parent->update.Subtract(local)) parent->update.Coalesce();
            if (!parent->update.count) parent->erase_pending = false;
            parent->frame_update.Subtract(local);
            parent->frame_update_pending = parent->frame_update.count != 0;
        }
        w = parent;
    }
}
bool ValidateRect(HWND h,const Gdi::RECT *rect)
{
    auto *w=Find(h);if(!w)return false;
    if(rect){if(!w->update.Subtract(*rect))return false;}else w->update.Clear();
    if(rect)w->frame_update.Subtract(*rect);else w->frame_update.Clear();
    w->frame_update_pending=w->frame_update.count!=0;
    if(!w->update.count)w->erase_pending=false;
    return true;
}
bool GetUpdateRect(HWND h,Gdi::RECT *out)
{
    SettleGeometry();
    auto *w=Find(h);if(!w)return false;
    if(out)*out=w->update.Bounds();
    // A nonclient-only invalidation still owns a WM_PAINT, but exposes an
    // empty client rectangle/region (original Win98 frame_update probe).
    return w->update.count!=0 || w->frame_update_pending;
}
// Original USER decisions on the native update
// regions.
unsigned redraw_error = 0;
bool RedrawWindow(HWND h, const Gdi::RECT *rect, unsigned region, unsigned flags,
                  void *context, RedrawImmediate immediate)
{
    RedrawScratchFrame scratch;
    RedrawEnvironment environment;
    environment.context = context;
    environment.immediate = immediate;
    environment.frame = &scratch;

    Win98RedrawWindow::Rect box{};
    if (rect) box = {rect->left, rect->top, rect->right, rect->bottom};
    // Windows 98 builds the screen region into its first scratch handle, for a
    // rect argument and for a region argument alike, and the walk never
    // replaces it. The apply step therefore always reads that scratch handle;
    // reading the caller's region instead would subtract the client origin
    // from coordinates that are already client relative.
    environment.prepared = scratch.Get(0);

    const unsigned result = Win98RedrawWindow::Body(environment, h, rect ? &box : nullptr, region, flags);
    redraw_error = environment.error_code;
    // What USER draws itself (frames, EDIT, controls) was marked by the apply
    // step; the frames are drawn again with the next pass.
    dirty.Mark();
    return result != 0 && !environment.error_code;
}
unsigned RedrawError() { return redraw_error; }
// Original USER decisions on the native queue. The 16-bit thunk
// has no native counterpart: its message table and its 32-to-16
// conversion are Win16 concerns.
ThreadQueue thread_queue_provider = nullptr;
void SetThreadQueueProvider(ThreadQueue provider) { thread_queue_provider = provider; }
struct PostThreadEnvironment
{
    unsigned thread = 0, message = 0, wparam = 0, process = 0;
    unsigned ThreadQueue(unsigned id)
    {
        thread = id;
        process = active_process;
        if (!id) return 0;
        if (thread_queue_provider)
        {
            // KERNEL ordinal 464 looks a thread up across tasks: the posting
            // process first, then every other one. TAPISRV posts 44Dh to the
            // thread of its client in another process. A thread of
            // a process that has been released answers zero, which is the
            // queue Windows 98 rejects and the FALSE Windows 98 returns.
            if (auto *owner = Process(active_process))
                if (unsigned queue = thread_queue_provider(owner->context, id)) return queue;
            for (auto *other=processes;other;other=other->next)
                if (other->pid != active_process && other->context)
                    if (unsigned queue = thread_queue_provider(other->context, id))
                    {
                        process = other->pid;
                        return queue;
                    }
            return 0;
        }
        if (id == active_thread) return id;
        for (unsigned i = 0; i < MaxWindows; ++i)
            if (windows[i].used && windows[i].thread == id) return id;
        return 0;
    }
    // Windows 98 checks the queue signature; the provider above already answers
    // zero for a thread without a queue, so nothing is left to reject here.
    unsigned ValidateQueue(unsigned handle) { return handle; }
    void FlushPostedMessages() {}
    // receives the queue as a far pointer with offset zero, so the
    // message reaches the queue with window handle 0. Native queues carry the
    // whole 32-bit message and wParam rather than the thunked words.
    unsigned PostEvent(unsigned, unsigned, unsigned, unsigned, unsigned lparam, unsigned)
    {
        if (queued == QueueSize) return 0;
        const auto stamp = Win98MessageMetadata::Post(MessageClock(),
                                                      Win98MessageMetadata::Pack(mouse_x, mouse_y));
        queue[queued] = {0, message, wparam, lparam, stamp.time, (short)mouse_x, (short)mouse_y};
        auto &event = input_events[queued++];
        event = InputEvent{};
        event.target = thread;
        event.process = process;
        return 1;
    }
    // The queue record relink has no native counterpart.
    unsigned QueueWord(unsigned, unsigned) { return 0; }
    unsigned DataWord(unsigned) { return 0; }
    void SetDataWord(unsigned, unsigned) {}
};
bool PostThreadMessage(unsigned thread, unsigned message, unsigned wparam, unsigned lparam)
{
    PostThreadEnvironment environment;
    environment.message = message;
    environment.wparam = wparam;
    const Win98PostThreadMessage::Thunked carried{message & 65535u, wparam & 65535u,
                                                  wparam >> 16, lparam};
    return Win98PostThreadMessage::Body(environment, thread, carried) != 0;
}
// Match USER behavior using native window and thread identities. The list itself is a fixed table here instead of
// USER's local heap; a token is the one-based slot index, which is what the
// original's near offset is.
namespace {
constexpr unsigned MaxHotKeys = 64;
struct HotKeyRecord {
    bool used = false;
    unsigned next = 0, queue = 0, id = 0, window = 0, modifiers = 0, vk = 0, scan = 0;
};
HotKeyRecord hot_keys[MaxHotKeys];
unsigned hot_key_head = 0, hot_key_task = 0, hot_key_error = 0;
struct HotKeyEnvironment
{
    HotKeyRecord &R(unsigned token) { return hot_keys[token - 1]; }
    unsigned Head() { return hot_key_head; }
    void SetHead(unsigned token) { hot_key_head = token; }
    unsigned Next(unsigned t) { return R(t).next; }
    void SetNext(unsigned t, unsigned v) { R(t).next = v; }
    unsigned Queue(unsigned t) { return R(t).queue; }
    void SetQueue(unsigned t, unsigned v) { R(t).queue = v; }
    unsigned Id(unsigned t) { return R(t).id; }
    void SetId(unsigned t, unsigned v) { R(t).id = v; }
    unsigned Window(unsigned t) { return R(t).window; }
    void SetWindow(unsigned t, unsigned v) { R(t).window = v; }
    unsigned Modifiers(unsigned t) { return R(t).modifiers; }
    void SetModifiers(unsigned t, unsigned v) { R(t).modifiers = v; }
    unsigned Vk(unsigned t) { return R(t).vk; }
    void SetVk(unsigned t, unsigned v) { R(t).vk = v; }
    void SetScan(unsigned t, unsigned v) { R(t).scan = v; }
    // LocalAlloc(LMEM_ZEROINIT, 10h).
    unsigned Allocate()
    {
        for (unsigned i = 0; i < MaxHotKeys; ++i)
            if (!hot_keys[i].used) { hot_keys[i] = HotKeyRecord{}; hot_keys[i].used = true; return i + 1; }
        return 0;
    }
    void Free(unsigned t) { R(t) = HotKeyRecord{}; }
    // KERNEL ordinal 625 answers the calling thread's queue. Native queues are
    // per thread, so the thread identifier is that queue.
    unsigned CurrentTask() { return active_thread; }
    // resolves the narrowed handle through USER's window table. The
    // native window record is identified by the handle itself.
    unsigned MapHandle(unsigned hwnd) { return Find(hwnd) ? hwnd : 0; }
    unsigned WindowHandle(unsigned window) { return window; }
    unsigned WindowTask(unsigned window) { auto *w = Find(window); return w ? w->thread : 0; }
    // KEYBOARD ordinal 131. The scan code at +0Eh is read by the ring-0 hot-key
    // consumer only; NEO98 has no VxD, so it records zero.
    unsigned MapVirtualKey(unsigned) { return 0; }
    // hands the list heads to the VxD at device 0442h. No counterpart.
    void ListChanged() {}
    void ParameterError(unsigned code) { hot_key_error = code; }
    bool KeyDown(unsigned vk) { return input.Down(vk); }
    void PostThread(unsigned queue, unsigned message, unsigned wparam, unsigned lparam)
    { PostThreadMessage(queue, message, wparam, lparam); }
    void PostWindow(unsigned hwnd, unsigned message, unsigned wparam, unsigned lparam)
    { PostMessage(hwnd, message, wparam, lparam); }
    void SetLastTask(unsigned queue) { hot_key_task = queue; }
};
}
bool RegisterHotKey(HWND h, unsigned id, unsigned modifiers, unsigned vk)
{
    HotKeyEnvironment environment;
    hot_key_error = 0;
    return Win98HotKey::RegisterEntry(environment, h, id, modifiers, vk) != 0;
}
bool UnregisterHotKey(HWND h, unsigned id)
{
    HotKeyEnvironment environment;
    hot_key_error = 0;
    return Win98HotKey::UnregisterEntry(environment, h, id) != 0;
}
unsigned HotKeyError() { return hot_key_error; }
unsigned HotKeyCount() { unsigned n = 0; for (auto &r : hot_keys) n += r.used; return n; }
unsigned HotKeyOwnerQueue() { return hot_key_task; }
// The hot keys of windows (WM_SETHOTKEY), below at WindowHotKeyInput.
struct WindowHotKeyRecord { HWND window = 0; unsigned key = 0; };
WindowHotKeyRecord window_hot_keys[32];
bool start_key_alone = false;
void ResetHotKeys()
{
    for (auto &r : window_hot_keys) r = {};
    start_key_alone = false;
    for (auto &record : hot_keys) record = HotKeyRecord{};
    hot_key_head = hot_key_task = hot_key_error = 0;
}
// USER, from the DESTROYWINDOW path.
void FreeWindowHotKeys(HWND h)
{
    for (auto &r : window_hot_keys)
        if (r.window == h) r = {};
    if (!hot_key_head) return;
    HotKeyEnvironment environment;
    Win98HotKey::FreeWindow(environment, h);
}
// USER, from the queue teardown.
void FreeTaskHotKeys()
{
    if (!hot_key_head) return;
    HotKeyEnvironment environment;
    Win98HotKey::FreeTask(environment);
    if (hot_key_task == active_thread) hot_key_task = 0;
}
// The hot keys of windows, WM_SETHOTKEY of DefWindowProc: the key word of
// the hot key control (virtual key low, HOTKEYF_SHIFT 1, CONTROL 2, ALT 4,
// EXT 8 high) and its window. A hit posts WM_SYSCOMMAND SC_HOTKEY with the
// window in lParam to the active window of the queue that reads the key, whose default procedure brings the window to the
// front: the foreground queue may hand the foreground on, the window's own
// queue could not. The shell sets one this way for the shortcut key of a PIF
// or a link.
unsigned SetWindowHotKey(HWND h, unsigned key)
{
    if (!Find(h)) return 0;
    key &= 0xffff;
    const unsigned vk = key & 0xff;
    if (key && (vk == 0x1b || vk == 0x0d || vk == 0x20 || vk == 0x09 || vk == 0x08)) return ~0u; // keys that cannot be hot keys
    for (auto &r : window_hot_keys)
        if (r.window && r.window != h && key && (r.key & 0x7ff) == (key & 0x7ff)) return 2;
    for (auto &r : window_hot_keys)
        if (r.window == h) { if (key) r.key = key; else r = {}; return 1; }
    if (!key) return 1;
    for (auto &r : window_hot_keys)
        if (!r.window) { r = {h, key}; return 1; }
    return 0;
}
unsigned WindowHotKey(HWND h)
{
    for (auto &r : window_hot_keys)
        if (r.window == h) return r.key;
    return 0;
}
bool WindowHotKeyInput(const InputPacket &p)
{
    if (p.kind != 1 || (p.parameter & 0x80000000u) || (p.message != 0x100 && p.message != 0x104)) return false;
    const unsigned modifiers = (input.Down(0x10) ? 1u : 0u) | (input.Down(0x11) ? 2u : 0u) | (input.Down(0x12) ? 4u : 0u);
    for (auto &r : window_hot_keys)
        if (r.window && (r.key & 0xff) == (p.key & 0xff) && ((r.key >> 8) & 7) == modifiers)
        {
            if (!Find(r.window)) { r = {}; continue; }
            // without an active window in the reading queue the key
            // is swallowed and nothing is posted.
            const HWND active = foreground_queue ? activation_queue.active : 0;
            if (active && Find(active)) PostMessage(active, 0x112, 0xf150, r.window); // WM_SYSCOMMAND, SC_HOTKEY
            return true;
        }
    return false;
}
// USER asks DOHOTKEYSTUFF for every key-down before the keystroke is
// routed. A hit consumes the key.
// USER tracks a Windows key with no intervening key;
// posts SC_TASKLIST to ds:F6 (the registered shell window),
// falling back to ds:AA8 (the desktop). RegisterShellHook's taskman is a
// different registration and is not the destination of this shortcut.
bool ShellKeyInput(const InputPacket &p)
{
    if (p.kind != 1) return false;
    const bool windows = p.key == 0x5b || p.key == 0x5c;
    const bool up = (p.parameter & 0x80000000u) != 0;
    if (!windows) start_key_alone = false;
    else if (!up) start_key_alone = true;
    if ((!up && p.key == 0x1b && input.Down(0x11)) ||
        (windows && up && start_key_alone)) {
        start_key_alone = false;
        const HWND shell = RegisteredShellWindow();
        PostMessage(shell ? shell : GetDesktopWindow(), 0x112, 0xf130, 0);
        return true;
    }
    return false;
}
bool HotKeyInput(const InputPacket &p)
{
    if (p.kind != 1 || !hot_key_head) return false;
    if (p.parameter & 0x80000000u) return false; // Windows 98 tests the key-up flag
    HotKeyEnvironment environment;
    return Win98HotKey::DoHotKeyStuff(environment, p.key, Win98HotKey::DeriveModifiers) != 0;
}
bool NeedsErase(HWND h){auto *w=Find(h);return w&&w->erase_pending;}
void RememberErase(HWND h,bool handled){if(auto *w=Find(h))w->erase_unhandled=!handled;}
unsigned EraseToken(HWND h){auto *w=Find(h);return w?w->erase_serial:0;}
void Erased(HWND h,unsigned token){auto *w=Find(h);if(w&&token==w->erase_serial)w->erase_pending=false;}
void KeepErase(HWND h){if(auto *w=Find(h)){w->erase_pending=true;++w->erase_serial;}}
unsigned Background(HWND h){auto *w=Find(h);return w?(w->class_record?class_store.HandleValue(class_store.Read16(Win98Classes::Body(class_store,w->class_record)+16)):w->background):0;}
bool TakeFramePaint(HWND h)
{
    auto *w=Find(h);if(!w || !w->frame_message_owed)return false;
    w->frame_message_owed=false;w->frame_update_pending=false;w->frame_update.Clear();return true;
}
void InvalidateFrame(HWND h)
{
    auto *w = Find(h);
    // Iconic windows are omitted from the host's ordinary paint tree, but
    // USER still owes their WM_NCPAINT when the iconic frame changes.
    const bool iconic = w && (w->style & 0x20000000u) && w->visible && h != host_hidden_window &&
                        (!w->parent || ShowsTree(w->parent));
    if (!w || (!ShowsTree(h) && !iconic)) return;
    const auto client = Client(*w);
    if (!ClientLeft(*w) && !ClientTop(*w) && client.right == w->width && client.bottom == w->height) return;
    w->frame_message_owed = true;
    w->frame_update_pending = true;
    w->frame_update.Add({-ClientLeft(*w),-ClientTop(*w),w->width-ClientLeft(*w),w->height-ClientTop(*w)});
    w->wait_paint_observed = false;
    if (ControlContent(*w)) { w->frame_owed = true; w->needs_paint = true; }
    dirty = true;
}
bool TakeInternalPaint(HWND h)
{
    auto *w=Find(h);if(!w)return false;
    const bool pending=w->internal_paint;w->internal_paint=false;return pending;
}
Gdi::HDC BeginPaint(HWND h,Paint &paint)
{
    USER_PROFILE(UserBeginPaint);
    auto *w=Find(h);if(!w)return 0;
    // USER takes the update region off the window before
    // asks for the DC, so a DC that cannot be had still leaves the
    // window valid. Returning early with the region in place made the thread
    // receive the same WM_PAINT forever; Dev-C++'s tab control then drew with
    // a DC of zero in an endless loop (comctl32.dll+0x586ED).
    const auto consume=[&]{w->update.Clear();w->erase_pending=false;w->frame_update_pending=false;w->frame_update.Clear();w->internal_paint=false;};
    const auto dc=Acquire(h,*w,true);if(!dc){consume();return 0;}
    if(!Gdi::IntersectClipRegion(dc,w->update.rectangles,w->update.count))
    {Release(*w,dc,true);consume();return 0;}
    // USER clears bit 4 only when bit 2 requests an erase.
    // Windows 98 returns bit 4 even on later paints without that request.
    if(w->erase_pending)w->erase_unhandled=false;
    paint={};paint.dc=dc;paint.rect=w->update.Bounds();paint.erase=w->erase_unhandled;
    USER_TRACE("BEGINPAINT hwnd=%u rect=%d,%d,%d,%d erase=%u\n",h,paint.rect.left,paint.rect.top,paint.rect.right,paint.rect.bottom,paint.erase?1u:0u);
    // The paint lease keeps its update region: a DC whose window moves or
    // gets covered while the paint is open gets its visible region again and
    // keeps this clip (RefreshWindowDCs).
    if(w->leases&&w->lease[w->leases-1].dc==dc&&!w->lease[w->leases-1].paint_clip)
        w->lease[w->leases-1].paint_clip=new RectRegion(w->update);
    // BeginPaint takes the caret of the window off the screen; EndPaint puts
    // it back (the Release of the paint lease).
    if(HideCaretFor(h)){if(auto *again=Find(h))if(again->leases&&again->lease[again->leases-1].dc==dc)again->lease[again->leases-1].caret_hidden=true;}
    // Consume before calling WM_ERASEBKGND: reinvalidation during that callback
    // belongs to the next paint and must survive EndPaint.
    w->update.Clear();w->erase_pending=false;w->frame_update_pending=false;w->frame_update.Clear();w->internal_paint=false;
    return dc;
}
bool EndPaint(HWND h,const Paint &paint)
{
    auto *w=Find(h);
    if(!w||!Release(*w,paint.dc,true))return false;
    // A subclass can receive WM_PAINT directly (DispatchMessage/UpdateWindow),
    // without PaintControlSurface. Its completed paint also consumes the
    // native control's pending paint; otherwise the empty stock STATIC
    // overwrites it on the next message wait (Half-Life's menu descriptions).
    if (w->label && GuestProcedure(*w) && !w->update.count)
    {
        w->drawn = true;
        w->needs_paint = false;
        w->native_invalid = false;
        w->native_signature = NativeSignature(h, *w);
        w->painted_generation = dirty.generation;
    }
    return true;
}
// An open paint operation in the bound thread's queue, or any queue when no
// thread is bound.
bool PaintInProgress()
{
    for(unsigned i=0;i<MaxWindows;++i){const auto &w=windows[i];
        if(!w.used||(active_thread&&w.thread!=active_thread)||(active_process&&w.process!=active_process))continue;
        for(unsigned l=0;l<w.leases;++l)if(w.lease[l].paint)return true;}
    for(unsigned l=0;l<desktop.leases;++l)if(desktop.lease[l].paint)return true;
    return false;
}
void SetMetadataProvider(void*p,MetadataCurrent task,MetadataClock clock,MetadataClock timer_clock){metadata_context=p;metadata_current=task;metadata_clock=clock;mouse_timer_clock=timer_clock;}
static MetadataTask& CurrentMetadata(){auto*p=metadata_current?metadata_current(CurrentContext(metadata_context)):nullptr;return p?*p:standalone_metadata;}
unsigned MessageClock(){return metadata_clock?metadata_clock(CurrentContext(metadata_context)):0;}
static unsigned MouseTimerClock(){return mouse_timer_clock?mouse_timer_clock(CurrentContext(metadata_context)):MessageClock();}
static void AcceptInputMetadata(const InputPacket&p){
    const auto position=Win98MessageMetadata::Pack(p.x,p.y);
    Win98MessageMetadata::Move(p.kind==2&&p.moved?0x200:p.message,position,processed_position);
    Win98MessageMetadata::Hardware(CurrentMetadata(),{p.milliseconds,position,p.extra_info},processed_position,metadata_queue_marker);
}
unsigned MessageMetadata(unsigned operation,unsigned value){auto&t=CurrentMetadata();return operation==3?Win98MessageMetadata::SetExtra(t,value):Win98MessageMetadata::Get(t,operation);}
// Native task queue provider for original USER new/current wake-bit decisions.
// The change bits a retrieval consumes follow its filter (src/win98_wake_mask.h,
// USER).
unsigned FilterWakeMask(unsigned minimum,unsigned maximum){return Win98WakeMask::Compute(minimum,maximum);}
unsigned WaitQueueStatus(bool observe,unsigned mask){
 SettleGeometry();
 unsigned current=0,changed=0;
 auto &metadata=CurrentMetadata();
 // Bit 1 retains the wake state after consuming WM_QUIT, until the next
 // retrieval. USER returns before ReceivePosted clears those bits.
 if(metadata.quit||(metadata.quit_observed&2)){current|=0x108;if(!(metadata.quit_observed&1))changed|=0x108;
  if(observe&&(mask&0x108))metadata.quit_observed|=1;}
 for(unsigned i=0;i<queued;++i)if((!active_thread||input_events[i].target==active_thread)&&(!active_process||input_events[i].process==active_process)){
  auto&e=input_events[i];unsigned m=queue[i].message;
  unsigned bits=!e.genuine?0x108:(m==0x200||m==0xa0)?2:(m>=0x100&&m<=0x109)?1:4;
  current|=bits;changed|=bits&~e.wait_observed;if(observe)e.wait_observed|=bits&mask;
 }
 for(unsigned i=0;i<MaxWindows;++i){auto&w=windows[i];
  if(w.used&&(!active_thread||w.thread==active_thread)&&(!active_process||w.process==active_process)&&w.redraw&&(Custom(w)||w.label)&&(w.update.count||w.frame_update_pending||w.internal_paint)&&IsVisible(Handle(i))&&RedrawChain(Handle(i))){
   current|=0x20;if(!w.wait_paint_observed)changed|=0x20;if(observe&&(mask&0x20))w.wait_paint_observed=true;
  }
 }
 return(current<<16)|changed;
}
// The messages still queued for the bound thread wake its next wait again,
// as if no PeekMessage had looked at them (RunMenuLoop).
void UnobserveQueue(){
 CurrentMetadata().quit_observed&=~1u;
 for(unsigned i=0;i<queued;++i)if((!active_thread||input_events[i].target==active_thread)&&(!active_process||input_events[i].process==active_process))input_events[i].wait_observed=0;
}
bool PostMessage(HWND h, unsigned msg, unsigned wp, unsigned lp)
{
    if (queued == QueueSize || (h && !Find(h))) return false;
    const auto stamp=Win98MessageMetadata::Post(MessageClock(),Win98MessageMetadata::Pack(mouse_x,mouse_y));
    queue[queued] = {h,msg,wp,lp,stamp.time,(short)mouse_x,(short)mouse_y};
    auto &event = input_events[queued++];
    event=InputEvent{};event.target=h?WindowThread(h):active_thread;event.process=h?WindowProcess(h):active_process;
    return true;
}
void PostQuitMessage(unsigned code)
{
    auto &task=CurrentMetadata();
    if(task.quit)return;
    task.quit=1;task.quit_code=code;task.quit_observed=0;
}
namespace {
// The first window of the bound thread that owes a paint, parent before
// children, front to back.
HWND NextPaintWindow(HWND parent, HWND filter, unsigned depth)
{
    const auto *container = Find(parent);
    if (!container || depth > 64) return 0;
    for (HWND h = container->first; h;)
    {
        const auto *w = Find(h); if (!w) break;
        const HWND next = w->next;
        // WM_SETREDRAW(FALSE) takes WS_VISIBLE off the window in USER; its
        // children do not paint either until it is on again.
        if (w->visible && w->redraw)
        {
            if ((!active_thread || w->thread == active_thread) && (!active_process || w->process == active_process) &&
                (Custom(*w) || w->label) && (w->update.count || w->frame_update_pending || w->internal_paint))
            {
                if(!filter)return h;
                for(HWND ancestor=h;ancestor;ancestor=Parent(ancestor))
                    if(ancestor==filter)return h;
                // An invalid parent client blocks a filtered child paint.
                // A remaining frame-only update cannot cover that child.
                if (w->update.count) { h=next;continue; }
            }
            if (const HWND child = NextPaintWindow(h, filter, depth + 1)) return child;
        }
        h = next;
    }
    return 0;
}
}
static void MouseHoverTick();
bool PeekMessage(Message *out, HWND filter, unsigned minimum, unsigned maximum, bool remove, bool paint)
{
    if (!out) return false;
    MouseHoverTick();
    SettleGeometry();
    WaitQueueStatus(true, FilterWakeMask(minimum, maximum));
    CurrentMetadata().quit_observed&=~2u;
    // Posted messages precede hardware input. In particular, TranslateMessage
    // must deliver WM_CHAR before a later queued Enter key reaches a dialog.
    // Preserve FIFO order within each category and apply the caller's filters
    // before choosing a message; PM_NOREMOVE must not deliver keyboard state.
    for (unsigned hardware = 0; hardware < 2; ++hardware) {
    for (unsigned i = 0; i < queued; ++i) if ((!active_thread||input_events[i].target==active_thread)&&input_events[i].genuine == bool(hardware) &&
        (!filter || queue[i].hwnd == filter || queue[i].message == 0x12) &&
        ((!minimum && !maximum) || (queue[i].message >= minimum && queue[i].message <= maximum) || queue[i].message == 0x12)) {
        if(!hardware){
            struct Receive final:Win98MessageMetadata::PostedEnvironment{
                unsigned index;Message*out;
                Receive(MetadataTask&t,unsigned i,Message*p):PostedEnvironment(t),index(i),out(p){}
                unsigned Quit(unsigned,unsigned)override{return 0;}
                unsigned Find()override{return index+2;}
                Win98MessageMetadata::Stamp Read(unsigned)override{const auto&m=queue[index];return {m.time,Win98MessageMetadata::Pack(m.x,m.y),input_events[index].extra};}
                void Copy(unsigned,unsigned)override{*out=queue[index];}
                void Wake()override{} // Native task wake/sent-message flags remain disabled.
                void Remove(unsigned)override{for(unsigned j=index+1;j<queued;++j){queue[j-1]=queue[j];input_events[j-1]=input_events[j];}--queued;}
                unsigned Count()const override{unsigned n=0;for(unsigned j=0;j<queued;++j)if(!input_events[j].genuine&&(!active_thread||input_events[j].target==active_thread))++n;return n;}
            }environment(CurrentMetadata(),i,out);
            return Win98MessageMetadata::ReceivePosted(environment,remove,1)!=0;
        }
        *out = queue[i];
        auto &metadata=CurrentMetadata();
        const auto stamp=Win98MessageMetadata::Stamp{out->time,Win98MessageMetadata::Pack(out->x,out->y),input_events[i].extra};
        if(hardware){
            Win98MessageMetadata::Move(out->message==0xa0?0x200:out->message,stamp.position,processed_position);
            Win98MessageMetadata::Hardware(metadata,stamp,processed_position,metadata_queue_marker);
            out->x=(short)processed_position;out->y=(short)(processed_position>>16);
            out->time=Win98MessageMetadata::Time(out->time,MessageClock());
        }else {out->time=MessageClock();out->x=(short)processed_position;out->y=(short)(processed_position>>16);}
        if (remove)
        {
            if (input_events[i].genuine)
            {
                input.Deliver(input_events[i].state);
                shift_down = input.Key(0x10) & 0x8000;
                control_down = input.Key(0x11) & 0x8000;
                // A retrieved button event cancels a pending ALT/F10 menu
                // activation. Sent mouse messages and motion do not (Win98).
                if((out->message>=0x201&&out->message<=0x209)||
                   (out->message>=0xa1&&out->message<=0xa9))
                    MenuKeyFlags(input_events[i].target,0x18,0);
            }
            for (unsigned j=i+1;j<queued;++j) { queue[j-1]=queue[j]; input_events[j-1]=input_events[j]; }
            --queued;
        }
        return true;
    }
    if(!hardware){
        auto &task=CurrentMetadata();
        bool posted=false;
        for(unsigned i=0;i<queued;++i)
            if(!input_events[i].genuine&&(!active_thread||input_events[i].target==active_thread))
                {posted=true;break;}
        // USER: a quit flag synthesizes WM_QUIT only when the posted
        // queue is empty, even if its remaining nodes do not match a filter.
        // A WM_QUIT posted explicitly remains an ordinary FIFO node above.
        if(task.quit&&!posted){
            *out={0,0x12,task.quit_code,0,MessageClock(),(short)processed_position,(short)(processed_position>>16)};
            if(remove){task.quit=0;task.quit_observed|=2;}
            return true;
        }
    }
    }
    // An update region keeps WM_PAINT pending until BeginPaint or validation.
    // Internal paint is consumed even by PM_NOREMOVE on Win98.
    // USER hands the paint to a parent before its children (Windows 98 sends
    // WM_PAINT and walks the children ), and the windows
    // of one level front to back. With one screen for all windows the order
    // is visible: a parent without WS_CLIPCHILDREN erases over its children,
    // so they have to paint after it.
    if(paint&&((!minimum&&!maximum)||(minimum<=0xf&&maximum>=0xf)))
        if(const HWND h=NextPaintWindow(Desktop,filter,0))
        {USER_TRACE("PEEKPAINT hwnd=%u filter=%u\n",h,filter);TakeInternalPaint(h);*out={h,0xf,0,0,MessageClock(),(short)processed_position,(short)(processed_position>>16)};return true;}
    return false;
}
// DefWindowProc's WM_SETREDRAW : FALSE takes WS_VISIBLE off a
// visible window and TRUE puts it back on an invisible one, without
// a message and without invalidating anything; the save bits under the
// window go and the DCs follow. The pixels stay where they
// are, so the settle only takes note. While the bit is off, IsWindowVisible
// answers FALSE, a STATIC does not draw at WM_SETTEXT and
// ShowWindow(SW_SHOW) is a real show; Windows Installer changes its ActionText
// that way.
void SetRedrawVisible(HWND h, bool on)
{
    auto *w = Find(h);
    if (!w || h == Desktop || w->visible == on) return;
    SettleGeometry();
    const bool settled = settled_serial == geometry_serial;
    w->visible = on;
    GeometryChanged();
    if (settled)
    {
        settle_quietly = true;
        SettleGeometry();
        settle_quietly = false;
    }
    dirty = true;
}
// The list box, list view and tree view answer WM_SETREDRAW themselves with a
// flag of their own and redraw when it comes back on; WS_VISIBLE stays.
void SetControlRedraw(HWND h, bool on)
{
    auto *w = Find(h);
    if (!w) return;
    w->redraw = on;
    if (on) InvalidateRect(h, nullptr, true);
}
unsigned DefaultMessage(HWND h, unsigned msg, unsigned wp, unsigned lp)
{
    // DefWindowProc WM_NCHITTEST: the complete frame result set, including the
    // sizing border, its corners, the caption buttons and the size box.
    if(msg==0x84)return FrameHitTest(h,(short)(lp&0xffff),(short)(lp>>16));
    if(msg==0x1f){CancelFrameTrack(h);CaptureOperation(7,h);return 0;}
    // The default window procedure accepts WM_QUERYENDSESSION and
    // WM_QUERYOPEN.
    if (msg == 0x81 || msg == 0x86 || msg == 0x11 || msg == 0x13) return 1; // WM_NCCREATE, WM_NCACTIVATE, WM_QUERYENDSESSION, WM_QUERYOPEN
    if (msg == 0x2e || msg == 0x2f) return ~0u; // USER: default listbox key/character handling.
    if (msg == 0x112)
    {
        // The four commands USER attaches to the caption buttons.
        switch (wp & 0xfff0)
        {
        case 0xf060: PostMessage(h, 0x10, 0, 0); return 0;   // SC_CLOSE
        case 0xf020: ShowWindow(h, 6); return 0;             // SC_MINIMIZE
        case 0xf030: ShowWindow(h, 3); return 0;             // SC_MAXIMIZE
        case 0xf120: ShowWindow(h, 9); return 0;             // SC_RESTORE
        // USER: SC_TASKLIST invokes WH_SHELL/HSHELL_TASKMAN.
        // SHELL.DLL forwards that hook to the registered taskbar window.
        case 0xf130: NotifyShell(7, h); return 0;
        default: break;
        }
    }
    if (msg == 0x10) { PostMessage(h, 2, 0, 0); return 0; }
    if (msg == 0x0f) { ValidateRect(h,nullptr);dirty = true;return 0; }
    if (msg == 0xb) { SetRedrawVisible(h, wp != 0); return 0; }
    if (IsEdit(h)) return EditMessage(h,msg,wp,lp);
    return ControlMessage(h,msg,wp,lp);
}
void SetModifiers(bool shift, bool control) { input.Transition(0x10,shift); input.Transition(0x11,control); }
unsigned GetKeyState(unsigned key) { return input.Key(key); }
unsigned GetAsyncKeyState(unsigned key) { return input.Async(key); }
// The swap flag lives at DS:74h of USER's input data. exchanges the
// WORD it was given with it and stores the same WORD as SM_SWAPBUTTON (DS:30Ch
// of USER's data); nothing normalizes it to 0 or 1.
static unsigned short mouse_swap;
unsigned SwapMouseButtons(unsigned value)
{
    const unsigned short previous = mouse_swap;
    mouse_swap = (unsigned short)value;
    settings.SetWord(0x30c, mouse_swap);
    return previous;
}
unsigned KeyboardState(unsigned char *buffer, bool set)
{
    const auto result = input.Copy(buffer,set);
    if (set) { shift_down = input.Key(0x10) & 0x8000; control_down = input.Key(0x11) & 0x8000; }
    return result;
}
unsigned Modifiers() { return (shift_down?1u:0u)|(control_down?2u:0u)|((input.Key(0x12)&0x8000)?4u:0u); }
unsigned EditSelection(HWND h, unsigned *start, unsigned *end)
{
    auto *w=Find(h); if (!w || !w->edit) return 0;
    auto a=w->edit->SelectionStart(),b=w->edit->SelectionEnd(); if(start)*start=a;if(end)*end=b;return (b<<16)|(a&65535);
}
bool SetEditSelection(HWND h, unsigned start, unsigned end)
{
    auto *w=Find(h);if(!w||!w->edit)return false;
    if(start==~0u)start=end=w->edit->SelectionEnd();w->edit->SetSelection(start,end);dirty=true;return true;
}
void SetEditNotify(void *p,EditNotify callback){edit_notify_context=p;edit_notify=callback;}
namespace {
int BreakEditWord(void *context, const char *text, unsigned current, unsigned length, unsigned code) {
    auto *w=static_cast<Window *>(context);
    return w&&w->edit_wordbreak&&edit_word_break
        ? edit_word_break(edit_word_context,w->edit_wordbreak,text,current,length,code)
        : int(current);
}
}
void SetEditWordBreak(void *p,EditWordBreak callback){edit_word_context=p;edit_word_break=callback;}
bool ReplaceEditSelection(HWND h,const char *text,unsigned length,bool undo)
{
    auto *w=Find(h);if(!w||!w->edit)return false;
    auto &e=*w->edit;
    unsigned kept=0;
    for(unsigned i=0;i<e.Length();++i)
        if(i<e.SelectionStart()||i>=e.SelectionEnd())kept+=e.Data()[i]=='\n'?2:1;
    unsigned available=w->edit_limit>kept?w->edit_limit-kept:0, count=0;
    while(count<length){const unsigned bytes=text[count]=='\n'?2:1;if(bytes>available)break;available-=bytes;++count;}
    count=e.FitInsertion(text,count,(w->style&4u)!=0,(w->style&0x80u)||(w->style&0x100000u),(w->style&0x40u)||(w->style&0x200000u));
    const bool truncated=count<length;
    const bool changed=e.Insert(text,count);
    if(!undo)e.ClearUndo();
    if(truncated){
        const auto parent=w->parent?w->parent:w->owner;
        const unsigned command=(w->menu&65535)|(0x501u<<16);
        if(parent){if(edit_notify)edit_notify(CallbackContext(edit_notify_context,parent),parent,command,h);
            else PostMessage(parent,0x111,command,h);}
    }
    if(auto *live=Find(h))if(changed||truncated)Changed(h,*live);
    return changed;
}
void SetClipboardCallbacks(void *p, ClipboardText text, ClipboardDestroy destroy){clipboard_context=p;clipboard_text=text;clipboard_destroy=destroy;}
unsigned ClipboardLength(){char text[TextEdit::Capacity];unsigned length=0;ReadClipboard(0,text,sizeof text,length);return length;}

namespace {
void EditMargins(Window &w,unsigned flags,unsigned left,unsigned right){
    Gdi::TEXTMETRICA tm{};Gdi::GetTextMetricsA(w.text_dc,&tm);
    unsigned left_margin=0,right_margin=0;
    if(const auto *font=GdiTrueType::FontOf(Gdi::SelectedFont(w.text_dc))){
        int abc[256*3];
        if(GdiTrueType::AbcWidths(*font,0,255,abc))for(unsigned i=0;i<256;++i){
            if(abc[i*3]<-int(left_margin))left_margin=unsigned(-abc[i*3]);
            if(abc[i*3+2]<-int(right_margin))right_margin=unsigned(-abc[i*3+2]);
        }
    }
    // Win98 preserves the explicit margins in fields narrower than three
    // average characters. Side bearings come from the selected font.
    const bool room=Client(w).right>=3*tm.tmAveCharWidth;
    const unsigned old_left=w.edit_left_margin,old_right=w.edit_right_margin;
    if(flags&1)w.edit_left_margin=left==65535?(room?left_margin:old_left):left;
    if(flags&2)w.edit_right_margin=right==65535?(room?right_margin:old_right):right;
    if(w.edit_format_set){w.edit_format.left+=int(w.edit_left_margin)-int(old_left);w.edit_format.right-=int(w.edit_right_margin)-int(old_right);}
}
}
bool SetEditFont(HWND h, Gdi::HFONT font) { auto *w=Find(h);if(!w||!w->edit)return false;if(!font)font=w->edit_default_font;if(!Gdi::SelectObject(w->text_dc,font))return false;w->font=font;EditMargins(*w,3,65535,65535);Metrics(*w);dirty=true;return true; }
bool EditModified(HWND h) { auto *w=Find(h);return w && w->modified; }
void SetEditModified(HWND h, bool modified) { if(auto *w=Find(h))w->modified=modified; }
namespace { unsigned EditMessageBody(HWND h, unsigned msg, unsigned wp, unsigned lp); }
// The EDIT moves its caret with every change of text or selection
// (SetCaretPos in its own handlers), not only when it paints: a program that
// asks GetCaretPos right after WM_CHAR gets the new place.
unsigned EditMessage(HWND h, unsigned msg, unsigned wp, unsigned lp)
{
    const unsigned result = EditMessageBody(h, msg, wp, lp);
    if (auto *w=Find(h)) EditVerticalBar(*w);
    if (focus == h && caret_draw && IsEdit(h)) caret_draw(CallbackContext(caret_context, h), h, 0);
    return result;
}
namespace {
unsigned EditMessageBody(HWND h, unsigned msg, unsigned wp, unsigned lp)
{
    if(msg==0x84){auto*w=Find(h);if(w&&(w->label||w->button))return Win98WindowHit::ControlHit(w->label?3:2,Style(h),0);}
    if(msg==0x1f){CancelFrameTrack(h);CaptureOperation(7,h);return 0;}
    auto *w=Find(h);if(!w||!w->edit)return 0;auto &e=*w->edit; bool changed=false;
    // ES_READONLY. The original keeps the same bit twice: in the window style
    // and in byte +38h of the edit record. Every path that changes text tests
    // it first and does nothing when set. Copying and selecting stay allowed.
    const bool readonly=(w->style&0x800u)!=0;
    auto input=[&](const char*text,unsigned count){if(readonly)return false;unsigned kept=e.Length()-(e.SelectionEnd()-e.SelectionStart());unsigned available=w->edit_limit>kept?w->edit_limit-kept:0;if(count>available)count=available;return e.Insert(text,count);};
    switch(msg) {
    // EM_SETREADONLY. hands the style helper the code 508h
    // with wParam, mirrors the same bit into byte +38h of the edit record,
    // repaints and returns 1.
    case 0xcf:
        if(wp)w->style|=0x800u;else w->style&=~0x800u;
        dirty=true;return 1;
    // USER.EXE stores the FAR procedure in the edit record at +3E,
    // and Windows 98 returns the same two words in DX:AX.
    case 0xd0:w->edit_wordbreak=lp;e.SetWordBreak(w,lp?BreakEditWord:nullptr);return 0;
    case 0xd1:return w->edit_wordbreak;
    // USER.EXE's single-line EDIT keeps the display character separately from
    // its text block. ES_PASSWORD starts it at '*'; zero restores ordinary
    // display. The text returned by WM_GETTEXT is never replaced.
    case 0xcc:if(!(w->style&4u)){e.SetPasswordCharacter((unsigned char)wp);if(wp)w->style|=0x20u;else w->style&=~0x20u;dirty=true;}return 0;
    case 0xd2:return (w->style&4u)?0:e.PasswordCharacter();
    case 0xc5:w->edit_limit=wp?wp:((w->style&4u)?65535u:32766u);return 0;
    case 0xd5:return w->edit_limit;
    case 0xd3:
        EditMargins(*w,wp,lp&65535,lp>>16);
        Metrics(*w);dirty=true;return 0;
    case 0xd4:return w->edit_left_margin|(unsigned(w->edit_right_margin)<<16);
    case 0xc:
        // USER resets the modified bit, before EN_UPDATE
        // (23A8) and EN_CHANGE (2405). Keep changes made by either callback.
        // Explorer queries EM_GETMODIFY from CBN_EDITCHANGE and otherwise
        // hides the ComboBoxEx images even for a programmatic WM_SETTEXT.
        if(!(w->style&4u))Changed(h,*w,false);
        return 0;
    case 0xd6: // EM_POSFROMCHAR, native-buffer index.
    {
        if(wp>=e.Length())return ~0u;
        unsigned line=0;
        while(line+1<e.LineCount()&&e.GetLine(line+1).start<=wp)++line;
        const auto r=EditFormat(*w);
        const int x=r.left+e.LineOffset(line)+e.PrefixWidth(line,wp-e.GetLine(line).start)-e.HorizontalPosition();
        const int y=r.top+(int(line)-e.FirstLine())*e.LineHeight();
        return (unsigned(x)&65535)|(unsigned(y)<<16);
    }
    case 0xd7: // EM_CHARFROMPOS, without moving the selection or caret.
    {
        const auto r=EditFormat(*w);
        int line=((short)(lp>>16)-r.top)/e.LineHeight()+e.FirstLine();
        if(line<0)line=0;
        if(unsigned(line)>=e.LineCount())line=e.LineCount()-1;
        const auto &row=e.GetLine(line);
        int x=(short)lp-r.left+e.HorizontalPosition()-e.LineOffset(line), position=0;
        unsigned index=0;
        for(;index<row.length;++index){
            const int width=e.CharacterAdvance((unsigned char)e.Data()[row.start+index],position);
            // Single-line EDIT rounds midpoint ties forward; multiline
            // EDIT keeps the character on the left of an exact midpoint.
            const int midpoint=(w->style&4u)?width/2+1:width/2;
            if(x<position+midpoint)break;
            position+=width;
        }
        return ((row.start+index)&65535)|(unsigned(line)<<16);
    }
    case 0xb:w->redraw=wp!=0;if(w->redraw)dirty=true;return 0;
    case 0x30: SetEditFont(h,wp);return 0;
    case 0x31:return w->font;
    case 0xb0:return EditSelection(h,nullptr,nullptr);
    case 0xb1:return SetEditSelection(h,wp,lp);
    case 0x200:
        if(ThreadCapture(w->thread)!=h)return 0;
        [[fallthrough]];
    case 0x201:
    case 0x203:
    {
        const auto r=EditFormat(*w);
        const unsigned first=e.SelectionStart(),last=e.SelectionEnd();
        e.SelectAt((short)lp-r.left,(short)(lp>>16)-r.top,msg==0x200 || (wp&4),w->style&4u);
        if(msg==0x203){e.Move(TextEdit::WordLeft);e.Move(TextEdit::WordRight,true);}
        dirty=true;
        if(msg==0x200 && (w->style&4u) && focus==h && IsVisible(h) &&
           (first!=e.SelectionStart() || last!=e.SelectionEnd()))AskEditColors(h);
        return 0;
    }
    case 0xb8:return w->modified;
    case 0xb9:w->modified=wp;return 0;
    case 0xba:return e.LineCount();
    case 0xbb:
    {
        unsigned line = wp == ~0u ? e.CursorLine() : wp;
        return line < e.LineCount() ? e.GetLine(line).start : ~0u;
    }
    case 0xc9:
    {
        unsigned index = wp == ~0u ? e.SelectionStart() : wp, line = 0;
        while (line + 1 < e.LineCount() && e.GetLine(line + 1).start <= index)
            ++line;
        return line;
    }
    // EM_LINELENGTH. The single-line body of USER.EXE answers the whole text
    // length out of the edit record (Windows 98 reads the length field +0Ah).
    // The multiline worker turns the character index into its line
    // and answers that line's length, which stops
    // before the line break. wParam FFFFFFFFh counts what the selection leaves
    // over on the lines it touches: on one line the line length minus the
    // selection, across lines the characters before the start plus those after
    // the end, and the lines between them do not count.
    case 0xc1:
    {
        if(!(w->style&4u))return e.Length();
        auto line_of=[&](unsigned index){unsigned line=0;while(line+1<e.LineCount()&&e.GetLine(line+1).start<=index)++line;return line;};
        if(wp!=~0u)return e.GetLine(line_of(wp)).length;
        const unsigned start=e.SelectionStart(),end=e.SelectionEnd();
        const unsigned first=line_of(start),last=line_of(end);
        if(first==last)return e.GetLine(last).length-(end-start);
        return (start-e.GetLine(first).start)+(e.GetLine(last).start+e.GetLine(last).length-end);
    }
    case 0xce:return e.FirstLine();
    case 0xb6:e.ScrollColumns((int)wp);e.Scroll((int)lp);dirty=true;return 1;
    case 0x114:e.ScrollHorizontal(wp&0xffff,wp>>16);dirty=true;return 0;
    case 0x115:e.ScrollVertical(wp&0xffff,wp>>16);dirty=true;return 0;
    case 0xb5:{unsigned result=e.ScrollCommand(wp);if(result)dirty=true;return result;}
    case 0xb7:e.ScrollCaret();dirty=true;return 0;
    case 0xbe:return e.ScrollPosition();
    case 0xc6:return e.CanUndo();
    case 0xcd:e.ClearUndo();return 0;
    case 0x301:{unsigned copied=0;return CopyClipboard(h,e,&copied)?copied:0;}
    case 0x300:if(!readonly&&e.SelectionStart()!=e.SelectionEnd()){if(!CopyClipboard(h,e))return 0;changed=e.Erase(false);}break;
    case 0x302:{if(readonly)return 0;char text[TextEdit::Capacity];unsigned length=0;if(!ReadClipboard(h,text,sizeof text,length))return 0;changed=input(text,length);}break;

    case 0x303:if(!readonly&&e.SelectionStart()!=e.SelectionEnd())changed=e.Erase(true);break;
    case 0x304: case 0xc7:if(!readonly)changed=e.Undo();break;
    case 0x100:
    {
        const unsigned first=e.SelectionStart(),last=e.SelectionEnd();
        switch(wp) {
        case 0x25:e.Move(control_down?TextEdit::WordLeft:TextEdit::Left,shift_down);break;
        case 0x27:e.Move(control_down?TextEdit::WordRight:TextEdit::Right,shift_down);break;
        case 0x26:e.Move(TextEdit::Up,shift_down);break;
        case 0x28:e.Move(TextEdit::Down,shift_down);break;
        case 0x24:e.Move(control_down?TextEdit::DocumentHome:TextEdit::Home,shift_down);break;
        case 0x23:e.Move(control_down?TextEdit::DocumentEnd:TextEdit::End,shift_down);break;
        case 0x21:e.Move(TextEdit::PageUp,shift_down);break;
        case 0x22:e.Move(TextEdit::PageDown,shift_down);break;
        case 0x2e:if(!readonly)changed=e.Erase(true);break;
        default:break;
        }
        dirty=true;
        if(!(w->style&4u) && focus==h && IsVisible(h) &&
           (first!=last || e.SelectionStart()!=e.SelectionEnd()) &&
           (first!=e.SelectionStart() || last!=e.SelectionEnd()))AskEditColors(h);
        break;
    }
    case 0x102:
        if(wp==1)e.SelectAll();
        else if(wp==3){CopyClipboard(h,e);}
        else if(wp==24){if(!readonly&&e.SelectionStart()!=e.SelectionEnd()&&CopyClipboard(h,e))changed=e.Erase(false);}
        else if(wp==22){if(readonly)return 0;char text[TextEdit::Capacity];unsigned length=0;if(!ReadClipboard(h,text,sizeof text,length))return 0;changed=input(text,length);}

        else if(wp==26){if(!readonly)changed=e.Undo();}
        else if(wp==8){if(!readonly)changed=e.Erase(false);}
        // A TAB is a character of the text in the multiline body (
        // inserts it like any other); the single-line body has no case for it
        // and beeps like for every control character it does not name, which Application::Edit does.
        else if(wp==9){if(w->style&4u){const char tab='\t';changed=input(&tab,1);}}
        else if(wp>=32 || ((w->style&4u)&&(wp==13 || wp==10))){char ch=wp==13?'\n':(char)wp;changed=input(&ch,1);}
        dirty=true;break;
    default:return 0;
    }
    if(changed)Changed(h,*w);
    // Both bodies of the USER.EXE EDIT procedure answer WM_KEYDOWN and
    // WM_CHAR with 1. SHELL32's Run dialog forwards Enter
    // through its ComboBox and starts the program only on that answer.
    if(msg==0x100||msg==0x102)return 1;
    return changed;
}
}
namespace {
struct MenuEnvironment : Win98Menu::Environment {
 using U=Win98Menu::U;using W=Win98Menu::W;using P=Win98Menu::P;
 unsigned text_bytes=0;
 Win98Menu::Menu*Find(U h)override{return FindMenu(h);}
 U AllocateMenu()override{for(unsigned step=0;step<MaxMenus;++step){const unsigned n=(menu_cursor+step)%MaxMenus;if(menus[n].used)continue;menu_cursor=(n+1)%MaxMenus;{unsigned char&generation=menu_generations[n];if(++generation>=MenuGenerations)generation=1;menus[n]=Menu{};menus[n].used=true;menus[n].process=active_process;menus[n].handle=W(generation*MenuSlots+n+1);return menus[n].handle;}}return 0;}
 // Win98Menu::Destroy clears the object's handle field before it hands the
 // slot back, so this decodes the slot from the handle instead of asking
 // FindMenu, and accepts the slot only if it still carries this handle or
 // none at all.
 void FreeMenu(U h)override{const unsigned n=(h&(MenuSlots-1))-1;if(n<MaxMenus&&(menus[n].handle==h||!menus[n].handle)){menus[n].used=false;menus[n].handle=0;}if(open_menu==h)open_menu=0;}
 bool ResizeItems(U h,W count)override{auto*m=FindMenu(h);if(!m||count>MenuItemCapacity)return false;MenuItem*p=nullptr;if(count){p=static_cast<MenuItem*>(malloc(sizeof(MenuItem)*count));if(!p)return false;for(unsigned n=0;n<count;++n)new(p+n)MenuItem{};for(unsigned n=0;n<count&&n<m->count;++n)p[n]=m->items[n];}free(m->items);m->items=p;return true;}
 P AllocateText(U bytes)override{if(bytes>65536||text_bytes>4*1024*1024-bytes)return 0;auto*p=static_cast<U*>(malloc(bytes+sizeof(U)));if(!p)return 0;*p=bytes;text_bytes+=bytes;return reinterpret_cast<P>(p+1);}
 void FreeText(P text)override{if(text){auto*p=reinterpret_cast<U*>(text)-1;text_bytes-=*p;free(p);}}
 W TextLength(P text)override{const char*p=reinterpret_cast<const char*>(text);unsigned n=0;while(p[n]&&n<65535)++n;return W(n);}
 void Copy(P destination,P source,W count)override{auto*d=reinterpret_cast<unsigned char*>(destination);auto*s=reinterpret_cast<const unsigned char*>(source);if(d<s)for(U n=0;n<count;++n)d[n]=s[n];else for(U n=count;n;--n)d[n-1]=s[n-1];}
 bool Writable(P p,W)override{return p!=0;}
 void PutZero(P p)override{*reinterpret_cast<char*>(p)=0;}
 bool ModernTask()override{return SystemSettings().expected>=0x400;}
 bool Bitmap(W h)override{unsigned info[6];return Gdi::BitmapInfo(h,info);}
 Win98Menu::Tracking*Active(U)override{return nullptr;} // Native nonmodal menu view has no original popup tracking record.
 void Refresh(U,U,bool)override{dirty=true;}
 bool PreserveSystemClose(U h,W item,W flags)override{if(item!=6||!(flags&0x400))return false;auto*m=FindMenu(h);auto*w=m?User::Find(m->window):nullptr;if(!w||(w->nonclient_flags&0x400000)||(w->extended&0x40)==0)return false;auto*root=FindMenu(w->system_menu);return root&&root->count&&root->items[0].submenu==h;}
 void Enabled(U)override{dirty=true;}
} menu_environment;
bool MenuContains(HMENU root,HMENU wanted,unsigned depth=0){if(root==wanted)return true;auto*m=FindMenu(root);if(!m||depth>=64)return false;for(unsigned n=0;n<m->count;++n)if(m->items[n].submenu&&MenuContains(m->items[n].submenu,wanted,depth+1))return true;return false;}
bool MenuAttached(HMENU h){for(const auto&w:windows)if(w.used&&(((w.style&0xc0000000u)!=0x40000000u&&MenuContains(w.menu,h))||MenuContains(w.system_menu,h)))return true;return false;}
void RetireMenu(HMENU h){auto*m=FindMenu(h);if(!m||m->retired)return;m->retired=true;for(unsigned i=0;i<m->count;++i)if(m->items[i].submenu)RetireMenu(m->items[i].submenu);}
void FreeMenuObject(HMENU h){Win98Menu::Destroy(menu_environment,menu_state,h);}
void ReleaseRetiredMenu(HMENU h){auto*m=FindMenu(h);if(m&&m->retired&&!MenuAttached(h))FreeMenuObject(h);}
}
HMENU CreateMenu(bool popup){return Win98Menu::Create(menu_environment,menu_state,1,popup);}
bool IsMenu(HMENU h){auto*m=FindMenu(h);return m&&!m->retired;}
bool MenuItemInfo(HMENU h,unsigned item,unsigned by_position,Win98Menu::Info&i,unsigned operation,bool layout)
{
 if(!IsMenu(h)||!Win98Menu::Validate(menu_environment,i,operation==0))return false;
 if(operation&&(i.mask&4)&&i.submenu&&!IsMenu(i.submenu))return false;
 if(operation==0&&(i.mask&0x40)&&i.length>32768){auto target=Win98Menu::Lookup(menu_environment,h,item,by_position);if(!target||FindMenu(target.menu)->items[target.index].text)return false;}
 if(operation&&i.mask&4&&i.submenu){auto target=Win98Menu::Lookup(menu_environment,h,item,by_position);if(MenuContains(i.submenu,target?target.menu:h))return false;}
 if(operation==2){auto target=Win98Menu::Lookup(menu_environment,h,item,by_position);auto*m=FindMenu(target?target.menu:h);if(!m||m->count>=MenuItemCapacity)return false;}
 bool result=operation==0?Win98Menu::GetInfo(menu_environment,h,item,by_position,i):operation==1?Win98Menu::SetInfo(menu_environment,menu_state,h,item,by_position,i):Win98Menu::Insert(menu_environment,menu_state,h,item,by_position,i);
 if(operation){if(layout)RelayoutMenus();else dirty=true;}return result;
}
// MENUEX popups carry their own item ID beside the submenu handle; Win32's
// GetMenuItemInfo(MIIM_ID) answers that ID, not the handle the Win16 template
// rule stores.
bool AppendMenuEx(HMENU h,unsigned flags,unsigned id,const char*text,HMENU submenu){auto i=Win98Menu::Legacy(flags&~0x10u,id,reinterpret_cast<Win98Menu::P>(text));if(submenu){i.mask|=4;i.submenu=submenu;}return MenuItemInfo(h,0xffff,0x400,i,2);}
bool AppendMenu(HMENU h,unsigned flags,unsigned id,const char*text,HMENU submenu){auto i=Win98Menu::Legacy(flags,submenu?submenu:id,reinterpret_cast<Win98Menu::P>(text));if(submenu){i.mask|=4;i.submenu=submenu;}return MenuItemInfo(h,0xffff,0x400,i,2);}
bool InsertMenu(HMENU h,unsigned item,unsigned flags,unsigned id,const char*text){auto i=Win98Menu::Legacy(flags,id,reinterpret_cast<Win98Menu::P>(text));return MenuItemInfo(h,item,flags&0x400,i,2);}
bool ModifyMenu(HMENU h,unsigned item,unsigned flags,unsigned id,const char*text){auto i=Win98Menu::Legacy(flags,id,reinterpret_cast<Win98Menu::P>(text));return MenuItemInfo(h,item,flags&0x400,i,1);}
bool DestroyMenu(HMENU h){if(!IsMenu(h))return false;if(MenuAttached(h)){RetireMenu(h);return true;}bool result=Win98Menu::Destroy(menu_environment,menu_state,h);RelayoutMenus();return result;}
bool RemoveMenu(HMENU h,unsigned item,bool position,bool destroy,bool layout){if(!IsMenu(h))return false;bool result=Win98Menu::Remove(menu_environment,menu_state,h,item,position?0x400:0,destroy);if(layout)RelayoutMenus();else dirty=true;return result;}
unsigned MenuItemCount(HMENU h){if(!IsMenu(h))return ~0u;return Win98Menu::Count(menu_environment,h);}
unsigned MenuItemID(HMENU h,unsigned index){if(!IsMenu(h))return ~0u;return Win98Menu::Id(menu_environment,h,index);}
unsigned MenuState(HMENU h,unsigned item,unsigned flags){if(!IsMenu(h))return ~0u;return Win98Menu::Flags(menu_environment,h,item,flags);}
unsigned MenuDefault(HMENU h,unsigned position,unsigned flags){if(!IsMenu(h))return ~0u;return flags&~3u?~0u:Win98Menu::Default(menu_environment,h,position,flags);}
bool SetMenuDefault(HMENU h,unsigned item,bool position){if(!IsMenu(h))return false;bool result=Win98Menu::SetDefault(menu_environment,h,item,position);dirty=true;return result;}
unsigned MenuRadio(HMENU h,unsigned first,unsigned last,unsigned check,unsigned flags){if(!IsMenu(h))return 0;auto result=Win98Menu::Radio(menu_environment,h,first,last,check,flags);dirty=true;return result;}
unsigned MenuContextHelp(HMENU h,unsigned value,bool set){auto*m=FindMenu(h);if(!m||m->retired)return 0;if(set){m->help=value;return 1;}return m->help;}
bool MenuDetails(HMENU h,Win98Menu::MenuInfo&i,bool set){if(!IsMenu(h)||i.size!=28||i.mask&~0x8000001fu)return false;if(set){Win98Menu::SetMenuInfo(menu_environment,h,i);RelayoutMenus();}else Win98Menu::GetMenuInfo(menu_environment,h,i);return true;}
HMENU GetMenu(HWND h) { auto *w=Find(h);return w&&!w->parent?w->menu:0; }
HMENU SystemMenuRoot(HWND h) { auto *w=Find(h);return w?w->system_menu:0; }
bool SetSystemMenuRoot(HWND h, HMENU menu)
{
    auto *w=Find(h);if(!w || h==Desktop || (menu && !IsMenu(menu)))return false;
    const auto previous=w->system_menu;w->system_menu=menu;
    if(previous&&previous!=menu){DestroyMenu(previous);ReleaseRetiredMenu(previous);}
    if(auto*m=FindMenu(menu)){m->window=h;if(m->count)if(auto*sub=FindMenu(m->items[0].submenu))sub->window=h;}return true;
}
bool SetMenu(HWND h, HMENU menu, bool layout)
{
    auto *w=Find(h);
    if(!w || h==Desktop || ((w->style & 0xc0000000u) == 0x40000000u) || (menu && !IsMenu(menu)))return false;
    if(w->class_menu_owned!=menu)w->class_menu_owned=0;
    const HMENU previous=w->menu;
    w->menu=menu;if(auto*m=FindMenu(menu))m->window=h;
    if(previous!=menu)ReleaseRetiredMenu(previous);
    if (layout)
    {
        Layout(h);
        w->update.Clear();
        InvalidateRect(h,nullptr,true);
    }
    dirty=true;
    return true;
}
int DrawMenuBarTemp(HWND owner, Gdi::HDC dc, Gdi::RECT rect, HMENU menu, Gdi::HFONT font)
{ return DrawMenuBarTempBody(owner, dc, rect, menu, font); }
bool DrawMenuBar(HWND h, bool layout)
{
    auto *w=Find(h);if(!w)return false;
    if((w->style&0xc0000000u)!=0x40000000u){
        if (layout) RelayoutMenus();
        // DrawMenuBar completes owner callbacks before returning, including
        // bitmap callbacks inspected immediately by the caller.
        ServeOwnerBars(h);
    }
    return true;
}
HMENU GetSubMenu(HMENU h,unsigned index){if(!IsMenu(h))return 0;return Win98Menu::Submenu(menu_environment,h,index);}
unsigned SetMenuFlags(HMENU h,unsigned item,unsigned flags,bool position,unsigned mask){if(!IsMenu(h))return ~0u;auto result=Win98Menu::ChangeFlags(menu_environment,h,item,Win98Menu::W((flags&~0x400u)|(position?0x400:0)),mask);dirty=true;return result;}
#include "user_menu_tracking.inc"
// A menu is open: the bar's session runs or a popup is tracked (WheelWindow).
static bool WheelMenuOpen() { return bar_session.active || PopupSession() != nullptr; }
namespace {
// CS_SAVEBITS saves pixels beneath a newly shown window and restores them on
// hide if nothing underneath was repainted. Windows 98 popup menu class
// #32768 uses this style. Our menus have no class record, so query them
// explicitly.
bool SavesBits(HWND h, const Window &w)
{
    return (w.class_style & 0x800) || PopupForWindow(h);
}
}
unsigned GetDoubleClickTime() { return double_click_time; }
void SetDoubleClickTime(unsigned time) { double_click_time = !time ? 500 : time > 5000 ? 5000 : time; }
unsigned PhysicalMouseButtons() { return mouse_buttons; }
bool PhysicalKeyDown(unsigned key) { return input.Down(key); }
static void CaptureState(InputPacket &p)
{
    for (unsigned i = 0; i < 256; ++i)
        p.state[i] = input.physical[i];
}
// A step of the wheel or the trackpad: `delta` up and down and `sideways` to
// the right in units of WHEEL_DELTA, `flags` of ScrollFine and its fellows.
InputPacket CaptureWheel(int delta, unsigned milliseconds);
InputPacket CaptureScroll(int delta, int sideways, unsigned flags, unsigned milliseconds)
{
    InputPacket p = CaptureWheel(delta, milliseconds);
    p.buttons = (unsigned)sideways;
    p.changed = flags;
    return p;
}
InputPacket CaptureWheel(int delta, unsigned milliseconds)
{
    InputPacket p;
    p.kind = 3;
    p.message = 0x20a;
    p.parameter = (unsigned(delta) & 65535) << 16 |
                  (mouse_buttons & 3) | ((mouse_buttons & 4) ? 0x10u : 0u) |
                  (input.Down(0x10) ? 4u : 0u) | (input.Down(0x11) ? 8u : 0u);
    p.x = mouse_x;
    p.y = mouse_y;
    p.milliseconds = milliseconds;
    CaptureState(p);
    return p;
}
// BlockInput : the thread that blocked input, SEG33:[FD0h]
// and [2AAh]. Zero when input flows.
static unsigned input_block_thread = 0;
unsigned InputBlocked() { return input_block_thread; }
void SetInputBlocked(unsigned thread) { input_block_thread = thread; }
// When each thread last read its queue: IsHungThread compares
// it with the clock. Keyed by thread id.
static struct { unsigned thread; unsigned long long time; } queue_reads[128];
void NoteQueueRead(unsigned thread, unsigned long long now)
{
    unsigned free_slot = 128;
    for (unsigned i = 0; i < 128; ++i)
    {
        if (queue_reads[i].thread == thread) { queue_reads[i].time = now; return; }
        if (!queue_reads[i].thread && free_slot == 128) free_slot = i;
    }
    if (free_slot < 128) queue_reads[free_slot] = {thread, now};
}
unsigned long long QueueReadTime(unsigned thread)
{
    for (const auto &r : queue_reads)
        if (r.thread == thread) return r.time;
    return 0;
}
// Mouse movement uses a ring of 64 entries of 16 bytes and a write index.
// Windows 98 writes one entry for every move it processes:
// the clipped position in screen coordinates as two words, the system
// millisecond counter and the extra information of the event.
// GetMouseMovePoints reads it.
static MouseMovePoint mouse_moves[64];
static unsigned mouse_move_index = 0;
InputPacket CaptureMouse(int x, int y, unsigned buttons, unsigned milliseconds)
{
    InputPacket p;
    p.host_window = host_pointer_window;
    p.kind = 2;
    p.x = x;
    p.y = y;
    p.buttons = buttons & 7;
    p.changed = mouse_buttons ^ p.buttons;
    p.moved = x != mouse_x || y != mouse_y;
    p.milliseconds = milliseconds;
    mouse_buttons = p.buttons;
    mouse_x = x;
    mouse_y = y;
    input.Transition(1, buttons & 1);
    input.Transition(2, buttons & 2);
    input.Transition(4, buttons & 4);
    CaptureState(p);
    if (p.moved)
    {
        auto &entry = mouse_moves[mouse_move_index];
        entry.x = (short)x;
        entry.y = (short)y;
        entry.time = milliseconds ? milliseconds : MessageClock();
        entry.extra = p.extra_info;
        mouse_move_index = (mouse_move_index + 1) & 63;
    }
    // USER turns the first button into 0201h and the second into
    // 0204h, and exchanges the two while the swap flag is set. The packet
    // therefore carries the logical buttons, and so does the key state it
    // hands the queue, which Win98 sets from those messages. The asynchronous
    // state keeps the physical buttons, as GetAsyncKeyState reports them.
    if (mouse_swap)
    {
        auto logical = [](unsigned v) { return (v & ~3u) | ((v & 1) << 1) | ((v >> 1) & 1); };
        p.buttons = logical(p.buttons);
        p.changed = logical(p.changed);
        const unsigned char left = p.state[1];
        p.state[1] = p.state[2];
        p.state[2] = left;
    }
    // Win98 moves the pointer in the mouse interrupt, before any ring-3 thread
    // sees the packet. Here the host thread that delivers the packet draws it,
    // so a busy or waiting guest cannot hold the pointer back.
    if (p.moved) MovePointer();
    return p;
}
// USER. The newest entry that has the position of `find`, and its
// time unless that is zero, is searched backwards over all 64 slots; that
// entry and the older ones go out, newest first, at most `capacity` of them.
// -1 when no entry matches. Slots never written are zero and match (0, 0).
int MouseMovePoints(const MouseMovePoint &find, MouseMovePoint *out, int capacity)
{
    const unsigned newest = (mouse_move_index - 1) & 63;
    unsigned found = 64;
    for (unsigned step = 0; step < 64; ++step)
    {
        const unsigned i = (newest - step) & 63;
        const auto &e = mouse_moves[i];
        if (e.x == find.x && e.y == find.y && (!find.time || e.time == find.time))
        {
            found = i;
            break;
        }
    }
    if (found == 64)
        return -1;
    int count = found == newest ? 64 : 64 - int((newest - found) & 63);
    if (count > capacity)
        count = capacity;
    for (int i = 0; i < count; ++i)
        out[i] = mouse_moves[(found - unsigned(i)) & 63];
    return count;
}
// USER keeps the raw events in the system queue and routes the next one only
// when a program retrieves input, against the capture and focus of
// that moment. A press routed to a window that has not read it yet therefore
// holds back the release behind it: the program may still take the capture
// on the press. A message for a window that is gone holds nothing back.
bool HardwareInputUnread(unsigned pid)
{
    for (unsigned i = 0; i < queued; ++i)
    {
        const auto &e = input_events[i];
        if (!e.genuine || (pid && e.process != pid)) continue;
        const unsigned m = queue[i].message;
        const bool transition = (m >= 0x100 && m <= 0x109) || (m >= 0x201 && m <= 0x20d) || (m >= 0xa1 && m <= 0xad);
        if (transition && (!queue[i].hwnd || IsWindow(queue[i].hwnd))) return true;
    }
    return false;
}
bool MouseMovePending()
{
    for(unsigned i=0;i<queued;++i)
        if(input_events[i].genuine&&(!active_thread||input_events[i].target==active_thread)&&
           (!active_process||input_events[i].process==active_process)&&
           (queue[i].message==0x200||queue[i].message==0xa0))return true;
    return false;
}
static bool PostInput(const InputPacket &p, HWND h, unsigned m, unsigned wp, unsigned lp)
{
    unsigned index=queued;
    if(m==0x200||m==0xa0)
        for(unsigned i=queued;i;){
            --i;const auto &event=input_events[i];
            if(!event.genuine||event.target!=WindowThread(h)||event.process!=WindowProcess(h))continue;
            // A previously peeked move still merges with the next motion.
            // Posted WM_MOUSEMOVE messages and hardware transitions retain
            // their own queue entries (native mouse_merge probe).
            if(queue[i].message==m&&queue[i].hwnd==h)index=i;
            break;
        }
    if(index==queued){if(!PostMessage(h,m,wp,lp))return false;}
    else {queue[index].wparam=wp;queue[index].lparam=lp;}
    queue[index].time=p.milliseconds;
    queue[index].x = p.x;
    queue[index].y = p.y;
    auto &event = input_events[index];
    event.wait_observed=0;
    event.genuine = true;event.extra=p.extra_info;event.character=p.character;
    for (unsigned i = 0; i < 256; ++i)
        event.state[i] = p.state[i];
    return true;
}
void BeginControlCapture(HWND);
bool TrackMouseLeave(HWND h,bool enable){auto*w=Find(h);if(!w)return false;w->track_mouse_leave=enable;return true;}
void QueryMouseTrack(unsigned &flags,HWND &window,unsigned &hover_time)
{
    flags=0;window=0;hover_time=0;
    for(unsigned i=0;i<MaxWindows;++i){const auto&w=windows[i];
        if(!w.used||(active_thread&&w.thread!=active_thread)||(active_process&&w.process!=active_process))continue;
        if(w.track_mouse_hover||w.track_mouse_leave){
            flags=(w.track_mouse_hover?1u:0u)|(w.track_mouse_leave?2u:0u);
            window=Handle(i);hover_time=w.track_mouse_hover?w.hover_time:0;return;
        }
    }
}
bool TrackMouse(HWND h,unsigned flags,unsigned hover_time)
{
    auto*w=Find(h);if(!w)return false;
    if(flags&0x80000000u){
        if(flags&1)w->track_mouse_hover=false;
        if(flags&2)w->track_mouse_leave=false;
        return true;
    }
    if(mouse_track_target!=h||mouse_track_hit!=Win98FrameHit::Client){
        if(flags&2)PostMessage(h,0x2a3,0,0);
        return true;
    }
    for(auto&other:windows)if(other.used&&other.thread==w->thread&& &other!=w){
        other.track_mouse_hover=false;other.track_mouse_leave=false;
    }
    if(flags&1){
        w->track_mouse_hover=true;w->hover_time=!hover_time||hover_time==~0u?400:hover_time;
        w->hover_started=MouseTimerClock();w->hover_x=mouse_x;w->hover_y=mouse_y;
        w->hover_pending=false;++w->hover_serial;
    }
    if(flags&2)w->track_mouse_leave=true;
    return true;
}
static void MouseHoverTick()
{
    const unsigned now=MouseTimerClock();
    for(unsigned i=0;i<MaxWindows;++i){auto&w=windows[i];
        if(!w.used||!w.track_mouse_hover||w.hover_pending||(active_thread&&w.thread!=active_thread)||
           (active_process&&w.process!=active_process)||now-w.hover_started<w.hover_time)continue;
        // Like USER's driver timer, this becomes a WM_SYSTIMER callback.
        // Merely peeking it does not run the hover callback.
        w.hover_pending=PostMessage(Handle(i),0x118,w.hover_serial,MouseHoverTimerProcedure);
    }
}
void DispatchMouseHoverTimer(HWND h,unsigned serial)
{
    auto*w=Find(h);
    if(!w||!w->track_mouse_hover||!w->hover_pending||w->hover_serial!=serial)return;
    w->hover_pending=false;w->track_mouse_hover=false;
    const auto p=Origin(*w);const int x=mouse_x-p.x-ClientLeft(*w),y=mouse_y-p.y-ClientTop(*w);
    const unsigned wp=(mouse_buttons&1?1u:0u)|(mouse_buttons&2?2u:0u)|(mouse_buttons&4?16u:0u)|
        (input.Key(0x10)&0x8000?4u:0u)|(input.Key(0x11)&0x8000?8u:0u);
    PostMessage(h,0x2a1,wp,(unsigned short)x|(unsigned((unsigned short)y)<<16));
}
#include "user_frame_track.inc"
#include "user_cursor.inc"
#include "user_host_view.inc"
// The window USER hands a mouse event to when nobody holds the capture, and
// the hit code it hands along: the point search of WindowFromPoint (USER, src/win98_window_hit.h) asks every candidate with WM_NCHITTEST, and
// a window that answers HTTRANSPARENT passes the event to the windows below
// it, siblings first. The tray clock and TrayNotifyWnd answer HTTRANSPARENT,
// Shell_TrayWnd answers HTCAPTION over its client area, so a double click on
// the clock is WM_NCLBUTTONDBLCLK at Shell_TrayWnd, which starts Date/Time
// (EXPLORER.EXE 4016DC..40976C). The answer decides between the
// client and the nonclient message. Every window of the search
// is asked, whatever thread owns it: the event is routed on retrieval
// anyway. The desktop itself takes nothing.
static HWND RouteWindow(int x, int y, unsigned &hit, HWND root = 0)
{
    struct Provider
    {
        HWND root;
        Win98WindowHit::Window Read(unsigned h) {
            auto r = HitWindowState(h); r.task = 0;
            if (root && h == User::Desktop) r.child = root;
            if (root && h == root) r.next = 0;
            return r;
        }
        bool Valid(unsigned h) { return IsWindow(h); }
        unsigned Desktop() { return User::Desktop; }
        bool RegionContains(unsigned region, Win98WindowHit::Point p) { return !region || Gdi::RegionContainsPoint(region, int(p.x), int(p.y)); }
        unsigned ComboChild(unsigned) { return 0; }
        bool ModalAllowed(unsigned) { return true; }
        unsigned ModalWindow() { return 0; }
        unsigned CurrentTask() { return 0; }
        int SizeX() { return settings.Word(0x338); }
        int SizeY() { return settings.Word(0x33a); }
        unsigned HitMessage(unsigned h, Win98WindowHit::Point p) { return HitTest(h, p.x, p.y); }
    } e{IsWindow(root) ? root : 0};
    int answer = Win98FrameHit::Nowhere;
    const HWND h = Win98WindowHit::Search(e, User::Desktop, Win98WindowHit::Point{x, y}, 1, &answer);
    hit = unsigned(answer);
    if (!h || h == User::Desktop || !IsWindow(h)) { hit = Win98FrameHit::Nowhere; return 0; }
    return h;
}
static void RouteMouse(const InputPacket &packet, void *context, InputFocus callback, InputActivate activate)
{
    const int x = packet.x, y = packet.y;
    const unsigned buttons = packet.buttons, changed = packet.changed, milliseconds = packet.milliseconds;
    const bool down = changed & buttons & 1, up = changed & ~buttons & 1;
    const unsigned wp = (buttons & 3) | ((buttons & 4) ? 0x10 : 0) | ((packet.state[0x10] & 128) ? 4 : 0) |
                        ((packet.state[0x11] & 128) ? 8 : 0);
    // The press and release USER posts to the foreground queue's capture
    // when the click lands in another queue (CaptureClickFor). The capture
    // takes them as client messages, with the point of the click.
    if (const HWND held = CaptureClickFor(packet))
        if (auto *w = Find(held))
        {
            const unsigned pressed = changed & buttons;
            const unsigned press = pressed & 1 ? 0x201 : pressed & 2 ? 0x204 : 0x207;
            const auto p = Origin(*w);
            const int cx = x - p.x - ClientLeft(*w), cy = y - p.y - ClientTop(*w);
            const unsigned lp = (unsigned short)cx | (unsigned((unsigned short)cy) << 16);
            PostInput(packet, held, press, wp, lp);
            PostInput(packet, held, press + 1, wp, lp);
        }
    // The routed window and its hit code; the capture takes every event of
    // its queue as a client message (CaptureFor).
    const HWND holder = CaptureFor(packet);
    unsigned route_hit = Win98FrameHit::Client;
    const HWND routed = holder ? holder : RouteWindow(x, y, route_hit, packet.host_window);
    mouse_track_target=routed;mouse_track_hit=route_hit;
    auto client_event = [&](HWND h, unsigned msg)
    {
        auto *w = Find(h);
        if (!w)
            return;
        const auto p = Origin(*w);
        const auto r = Client(*w);
        const int cx = x - p.x - ClientLeft(*w), cy = y - p.y - ClientTop(*w);
        if (holder == h || (h == routed && route_hit == Win98FrameHit::Client) ||
            (h != routed && cx >= 0 && cy >= 0 && cx < r.right && cy < r.bottom))
            PostInput(packet, h, msg, wp, (unsigned short)cx | (unsigned((unsigned short)cy) << 16));
    };
    // Whether the routed window takes the event in its client area.
    auto in_client = [&](HWND h) { return holder == h || (h == routed && route_hit == Win98FrameHit::Client); };
    // The nonclient hit code of the routed window; a window reached another
    // way is asked.
    auto nonclient_hit = [&](HWND h) { return h == routed ? route_hit : HitTest(h, x, y); };
    // USER: a second press on the same window within the double-click
    // time and a 4-pixel box is WM_LBUTTONDBLCLK; anything else starts a new
    // click record.
    // USER also compares the stored message: only a second press of
    // the same button is a double click.
    auto press_message_for = [&](HWND h, unsigned press = 0x201)
    {
        unsigned message = press;
        if (milliseconds && last_click_window == h && last_click_message == press &&
            milliseconds - last_click_time <= double_click_time && x - last_click_x >= -4 &&
            x - last_click_x <= 4 && y - last_click_y >= -4 && y - last_click_y <= 4)
        {
            message = press + 2;
            last_click_window = 0;
        }
        else
        {
            last_click_window = h;
            last_click_message = press;
            last_click_time = milliseconds;
            last_click_x = x;
            last_click_y = y;
        }
        return message;
    };
    // Native leave-event provider. Ownership and metadata go through the
    // same target-thread queue as other physical mouse packets.
    if(packet.moved)for(auto &w:windows)if(w.used&&(w.track_mouse_leave||w.track_mouse_hover)){
        const HWND tracked=Handle(unsigned(&w-windows));
        if(tracked!=routed||route_hit!=Win98FrameHit::Client){
            const bool leave=w.track_mouse_leave;w.track_mouse_leave=false;w.track_mouse_hover=false;
            if(leave)PostInput(packet,tracked,0x2a3,0,0);
        }else if(w.track_mouse_hover&&(x-w.hover_x<-2||x-w.hover_x>=2||y-w.hover_y<-2||y-w.hover_y>=2)){
            w.hover_x=x;w.hover_y=y;w.hover_started=MouseTimerClock();
            w.hover_pending=false;++w.hover_serial;
        }
    }
    const auto target = routed;
    // USER asks the window under the pointer which cursor to show, with the
    // hit code it just computed. DefWindowProc picks the sizing arrows on a
    // frame and the class cursor in the client area.
    if (packet.moved && target && !FrameTrackActive())
        PointerMoved(target, x, y);
    if (!IsWindow(target)) return;
    // A move outside the client area is WM_NCMOUSEMOVE (00A0h) with the hit
    // code in wParam and the point on the screen in lParam: USER takes
    // 160h off the client message whenever the hit test is not HTCLIENT, for
    // a move as for a press. Whoever holds the mouse gets WM_MOUSEMOVE.
    if (packet.moved)
        if (auto *w = Find(target))
        {
            if (in_client(target))
                client_event(target, 0x200);
            else
            {
                const unsigned hit = nonclient_hit(target);
                if (IsWindow(target) && hit != Win98FrameHit::Transparent && hit != Win98FrameHit::Nowhere &&
                    hit != Win98FrameHit::Error && hit != Win98FrameHit::Client)
                    PostInput(packet, target, 0xa0, hit, (unsigned short)x | (unsigned((unsigned short)y) << 16));
            }
        }
    // Outside the client area, convert right-button messages to
    // WM_NCRBUTTONDOWN/UP with the hit-test result in wParam and screen
    // coordinates in lParam. DefWindowProc then receives title-bar clicks and
    // can open the system menu. A capture owner still receives client
    // messages.
    auto right_button_event = [&](HWND h, unsigned client_message)
    {
        auto *w = Find(h);
        if (!w)
            return;
        const auto p = Origin(*w);
        const auto r = Client(*w);
        const int cx = x - p.x - ClientLeft(*w), cy = y - p.y - ClientTop(*w);
        // The right button goes through the same double-click check as the
        // middle one : 0206h in a CS_DBLCLKS client area, 00A6h
        // on the frame.
        const bool press = client_message == 0x204;
        (void)cx; (void)cy; (void)r;
        if (in_client(h))
        {
            client_event(h, press && (w->double_clicks || w->common) ? press_message_for(h, 0x204) : client_message);
            return;
        }
        const unsigned hit = nonclient_hit(h);
        if (!IsWindow(h) || hit == Win98FrameHit::Transparent || hit == Win98FrameHit::Nowhere ||
            hit == Win98FrameHit::Error)
            return;
        PostInput(packet, h, (press ? press_message_for(h, 0x204) : client_message) - 0x160, hit,
                  (unsigned short)x | (unsigned((unsigned short)y) << 16));
    };
    // USER treats the three presses alike: 0201h, 0204h and 0207h
    // ask the window with WM_MOUSEACTIVATE and activate its top-level window, unless the queue holds the mouse. The left
    // press does so further down; this is the right and the middle one. 0: deliver and activate, 1: drop, 3: deliver only.
    auto press_decision = [&](HWND h, unsigned message) -> unsigned
    {
        if (holder || !activate || !IsWindow(h)) return 3;
        if (route_hit == Win98FrameHit::Transparent || route_hit == Win98FrameHit::Nowhere ||
            route_hit == Win98FrameHit::Error)
            return 3;
        return activate(context, h, in_client(h) ? unsigned(Win98FrameHit::Client) : route_hit, message);
    };
    auto mouse_activation = [&](HWND hwnd)
    {
        // The activation of the press; the focus stays where the queue of
        // the window keeps it.
        if (callback && IsWindow(hwnd)) callback(context, hwnd, false);
    };
    if (changed & 2)
    {
        const bool press = buttons & 2;
        const unsigned decision = press ? press_decision(target, 0x204) : 3;
        if (decision != 1 && IsWindow(target))
        {
            right_button_event(target, press ? 0x204 : 0x205);
            if (decision == 0) mouse_activation(target);
        }
        if (!IsWindow(target)) return;
    }
    // Middle-button double-click detection compares window, time, message and
    // the SM_CXDOUBLECLK/SM_CYDOUBLECLK rectangle. Client double-clicks
    // require CS_DBLCLKS; nonclient double-clicks do not. Nonclient messages
    // carry the hit-test result and screen coordinates. A capture owner
    // receives client messages.
    auto middle_button_event = [&](HWND h, bool press)
    {
        auto *w = Find(h);
        if (!w)
            return;
        const auto p = Origin(*w);
        const auto r = Client(*w);
        const int cx = x - p.x - ClientLeft(*w), cy = y - p.y - ClientTop(*w);
        (void)cx; (void)cy; (void)r;
        if (in_client(h))
        {
            client_event(h, !press ? 0x208 : w->double_clicks || w->common ? press_message_for(h, 0x207) : 0x207);
            return;
        }
        const unsigned hit = nonclient_hit(h);
        if (!IsWindow(h) || hit == Win98FrameHit::Transparent || hit == Win98FrameHit::Nowhere ||
            hit == Win98FrameHit::Error)
            return;
        PostInput(packet, h, (press ? press_message_for(h, 0x207) : 0x208) - 0x160, hit,
                  (unsigned short)x | (unsigned((unsigned short)y) << 16));
    };
    if (changed & 4)
    {
        const bool press = buttons & 4;
        const unsigned decision = press ? press_decision(target, 0x207) : 3;
        if (decision != 1 && IsWindow(target))
        {
            middle_button_event(target, press);
            if (decision == 0) mouse_activation(target);
        }
        if (!IsWindow(target)) return;
    }
    auto mouse_focus = [&](HWND hwnd, bool focus = true)
    {
        if (callback)
        {
            callback(context, hwnd, focus);
            return;
        }
        if (!focus)
            return;
        HWND old = SetFocus(hwnd);
        if (old != hwnd)
        {
            if (old)
                PostMessage(old, 8, hwnd, 0);
            PostMessage(hwnd, 7, old, 0);
        }
    };
    // USER remembers a pressed caption button in the window word at 38h.
    // It draws the button pushed while the mouse is down, cancels the push as
    // soon as the pointer leaves it, and only sends the command on a release
    // that happens inside.
    if (pressed_button_window)
    {
        auto *tracked = Find(pressed_button_window);
        if (!tracked || !tracked->pressed_hit)
        {
            pressed_button_window = 0;
        }
        else
        {
            const auto origin = Origin(*tracked);
            unsigned command = 0;
            bool inside = false;
            for (unsigned i = 0; i < tracked->button_slots; ++i)
                if (tracked->button_slot[i].hit == tracked->pressed_hit)
                {
                    command = tracked->button_slot[i].command;
                    inside = Inside(tracked->button_slot[i].rect,
                                    x - origin.x, y - origin.y);
                }
            if (tracked->pressed_inside != inside)
            { tracked->pressed_inside = inside; dirty = true; }
            if (up || !(buttons & 1))
            {
                const HWND owner = pressed_button_window;
                tracked->pressed_hit = 0;
                tracked->pressed_inside = false;
                pressed_button_window = 0;
                dirty = true;
                if (up && inside && command && IsWindow(owner))
                    PostMessage(owner, 0x112, command,
                                (unsigned short)x | ((unsigned)(unsigned short)y << 16));
                return;
            }
            return;
        }
    }
    if (up)
    {
        if (in_client(target)) client_event(target, 0x202);
        else if (target && route_hit != Win98FrameHit::Transparent && route_hit != Win98FrameHit::Nowhere &&
                 route_hit != Win98FrameHit::Error)
            PostInput(packet, target, 0xa2, route_hit, (unsigned short)x | (unsigned((unsigned short)y) << 16));
        if(callback&&(IsButton(target)||IsEdit(target)))return; // The control procedure owns capture release.
        if(!automatic_capture||!holder)return;
        auto *w = Find(holder);
        if (w && w->button && w->procedure && (w->procedure < 0x71000000u || w->procedure >= 0x71010000u))
            return; // the procedure chain released the capture in ButtonMouse
        if (w && w->button)
        {
            auto p = Origin(*w);
            unsigned h = holder;
            w->pressed = false;
            dirty = true;
            const bool inside=x>=p.x&&y>=p.y&&x<p.x+w->width&&y<p.y+w->height;
            CaptureOperation(9,holder);
            if(inside&&IsWindow(h))ClickButton(h);
            return;
        }
        CaptureOperation(9,holder);
        return;
    }
    if (holder)
    {
        if(down){auto*c=Find(holder);client_event(holder,c&&(c->double_clicks||c->common)?press_message_for(holder):0x201);}
        if(callback&&IsEdit(holder))return;
        if(!automatic_capture)return;
        if (auto *w = Find(holder))
        {
            if (!w->edit)
                return;
            auto p = Origin(*w);
            const auto r=EditFormat(*w);w->edit->SelectAt(x-p.x-ClientLeft(*w)-r.left,y-p.y-ClientTop(*w)-r.top,true);
            dirty = true;
        }
        return;
    }
    if (!down)
        return;
    if (const HWND selected = target)
    {
        // USER asks the window with WM_MOUSEACTIVATE before the press
        // is handed over (called from the retrieval ): the answer
        // decides whether the window is activated and whether the press
        // reaches it at all. A press that is not delivered (no hit) asks
        // nothing.
        bool keep_inactive = false;
        if (activate && route_hit != Win98FrameHit::Transparent && route_hit != Win98FrameHit::Nowhere &&
            route_hit != Win98FrameHit::Error)
        {
            const unsigned decision = activate(context, selected, in_client(selected) ? unsigned(Win98FrameHit::Client) : route_hit, 0x201);
            if (decision == 1 || !IsWindow(selected))
                return;
            keep_inactive = decision == 3;
        }
        auto *w = Find(selected);
        const auto p = Origin(*w);
        // Only a press in the client area of a class without CS_DBLCLKS drops
        // the click record; on the frame the double-click check always
        // applies.
        if (!w->common && !w->double_clicks && in_client(selected))
            last_click_window = 0;
        if (w->nonclient_flags & 8)
        {
            bool hit_button = false;
            for (unsigned i = 0; i < w->button_slots && !hit_button; ++i)
                if (Inside(w->button_slot[i].rect, x - p.x, y - p.y))
                {
                    w->pressed_hit = w->button_slot[i].hit;
                    w->pressed_inside = true;
                    pressed_button_window = selected;
                    dirty = true;
                    hit_button = true;
                }
            if (hit_button)
                return;
        }
        if ((w->nonclient_flags & 1) && Inside(w->menu_bar, x - p.x, y - p.y) && FindMenu(w->menu))
        {
            // A press on the menu bar is WM_NCLBUTTONDOWN with HTMENU in the
            // queue of the window like any other press on the frame (USER). The menu drops when the program has taken the message
            // and DefWindowProc has turned it into SC_MOUSEMENU (
            // MouseMenu below), not here: a program that looks for presses in
            // its queue first (the title picture of Shanghai II ends at one,
            // PeekMessage 00A1h) must find this one there.
            PostInput(packet, selected, press_message_for(selected, 0x201) - 0x160, Win98FrameHit::Menu,
                      (unsigned short)x | (unsigned((unsigned short)y) << 16));
            if (!keep_inactive) mouse_focus(selected, false);
            return;
        }
        if (!in_client(selected))
        {
            // The window procedure answered WM_NCHITTEST during the point
            // search; Explorer and many applications override it, so the
            // press carries that answer.
            const unsigned hit = route_hit;
            if (!IsWindow(selected)) return;
            if (hit == Win98FrameHit::Transparent || hit == Win98FrameHit::Nowhere ||
                hit == Win98FrameHit::Error)
                return;
            // USER runs every press through the double-click check;
            // outside the client area it always applies, and
            // subtracts 160h: 00A1h or 00A3h.
            PostInput(packet, selected, press_message_for(selected, 0x201) - 0x160, hit,
                      (unsigned short)x | (unsigned((unsigned short)y) << 16));
            // A press on the frame activates the window; the focus stays
            // where the window's queue keeps it. Setting it to the
            // frame took it from Explorer's list view, which then drew its
            // selection without the highlight.
            if (!keep_inactive) mouse_focus(selected, false);
            return;
        }
        if (w->button)
        {
            if ((w->style & 15) == 7)
                return;
            if (callback || (w->procedure && (w->procedure < 0x71000000u || w->procedure >= 0x71010000u)))
            {
                // A guest subclass owns the message; it reaches ButtonMouse by
                // forwarding through CallWindowProc, the way USER's BUTTON does.
                client_event(selected, 0x201);
                if(IsWindow(selected))mouse_focus(selected);
                return;
            }
            client_event(selected, 0x201);
            // WM_LBUTTONDOWN ran in the guest and may have destroyed this
            // control; resolve the handle again before touching the record.
            w = Find(selected); if (!w || !w->button) return;
            w->pressed = true;
            dirty = true;

            BeginControlCapture(selected);
            if(IsWindow(selected))mouse_focus(selected);
            return;
        }
        if (w->edit)
        {
            client_event(selected, press_message_for(selected));
            if(callback)return; // EDIT owns selection, capture and focus, including direct SendMessage.
            w = Find(selected); if (!w || !w->edit) return;
            const auto at = Origin(*w);

            const auto r=EditFormat(*w);w->edit->SelectAt(x-at.x-ClientLeft(*w)-r.left,y-at.y-ClientTop(*w)-r.top,packet.state[0x10]&128);
            dirty = true;
            BeginControlCapture(selected);
            if(IsWindow(selected))mouse_focus(selected);
            return;
        }
        auto press_message = [&]() { return press_message_for(selected); };
        if (w->common)
        {
            const unsigned message = press_message();
            const auto at = Origin(*w);
            PostInput(packet, selected, message, wp,
                      (unsigned short)(x - at.x - ClientLeft(*w)) | ((unsigned)(unsigned short)(y - at.y - ClientTop(*w)) << 16));
            dirty = true;
            mouse_focus(selected);
            return;
        }
        client_event(selected, w->double_clicks ? press_message() : 0x201);
        // A plain window: the port's focus change stands for the activation
        // of the press, which the answer MA_NOACTIVATE declines. The controls
        // above take the focus themselves, as their procedures do.
        if (!keep_inactive) mouse_focus(selected);
    }
}
void Mouse(int x, int y, bool down, bool up, unsigned milliseconds)
{
    unsigned buttons = mouse_buttons;
    if (down)
        buttons |= 1;
    if (up)
        buttons &= ~1u;
    MouseButtons(x, y, buttons, milliseconds);
}
void MouseButtons(int x, int y, unsigned buttons, unsigned milliseconds)
{
    RouteInput(CaptureMouse(x, y, buttons, milliseconds));
}
void Keyboard(unsigned vk, unsigned character, bool shift, bool control)
{
    SetModifiers(shift, control);
    KeyboardRaw(vk, character, true);
}
InputPacket CaptureKey(unsigned vk, unsigned character, bool down, unsigned scan, bool extended)
{
    InputPacket p;
    p.kind = 1;
    p.milliseconds=MessageClock();
    p.x = mouse_x;
    p.y = mouse_y;
    vk &= 255;
    const bool was = input.Transition(vk, down);
    unsigned message_key = vk;
    if (vk >= 0xa0 && vk <= 0xa5)
    {
        message_key = 0x10 + (vk - 0xa0) / 2;
        const auto left = 0xa0 + ((vk - 0xa0) & ~1u);
        input.Transition(message_key, input.Down(left) || input.Down(left + 1));
    }
    const bool alt = input.Down(0x12);
    // USER's keybd_event picks WM_SYS* per key: CONTROL
    // itself never; ALT going down always unless CONTROL is held, and it
    // clears the count of keys pressed under it (DS:89Eh); ALT coming up
    // only when no other key went by since, so ALT+F ends in a plain
    // WM_KEYUP; any other key while ALT is down counts and is a system key
    // unless CONTROL is held. AltGr is CONTROL+ALT on the layouts that have
    // it, so neither it nor its characters are system keys and
    // DefWindowProc never sees an ALT alone in it. F10 becomes a
    // system key when it is retrieved; NEO98 decides it here.
    static unsigned keys_under_alt = 0;
    const bool control = input.Down(0x11);
    bool system = false;
    if (message_key == 0x11) system = false;
    else if (message_key == 0x12)
    {
        if (down) keys_under_alt = 0;
        system = !control && (down || !keys_under_alt);
    }
    else if (alt) { ++keys_under_alt; system = !control; }
    if (message_key == 0x79) system = true;
    p.message = (system ? 0x104 : 0x100) + (down ? 0 : 1);
    p.key = message_key;
    p.character = down ? character : 0;
    p.parameter = 1 | ((scan & 255) << 16) | (extended ? 0x1000000 : 0) | (alt ? 0x20000000 : 0) |
                  ((!down || was) ? 0x40000000 : 0) | (down ? 0 : 0x80000000);
    CaptureState(p);
    return p;
}
// Whether a retrieval with this filter would take the packet. USER looks at
// the raw event in the system queue, works out window and message against
// the capture and focus of the moment and leaves the event raw when the
// filter does not take it; the next retrieval decides again. A
// toolbar that peeks for its own WM_LBUTTONDOWN while it handles the press
// must not fix the release on the window that held the capture before.
bool InputPacketMatches(const InputPacket &p, HWND filter, unsigned minimum, unsigned maximum)
{
    if (PopupSession() || FrameTrackActive()) return true;
    unsigned candidates[4] = {};
    unsigned count = 0;
    HWND target = 0;
    if (p.kind == 2)
    {
        const HWND held = CaptureFor(p);
        target = held ? held : InputWindow(p);
        unsigned base = 0x200;
        if (p.changed & 1) base = p.buttons & 1 ? 0x201 : 0x202;
        else if (p.changed & 2) base = p.buttons & 2 ? 0x204 : 0x205;
        else if (p.changed & 4) base = p.buttons & 4 ? 0x207 : 0x208;
        candidates[count++] = base;
        candidates[count++] = base - 0x160;
        if (base == 0x201 || base == 0x204 || base == 0x207)
        {
            candidates[count++] = base + 2;
            candidates[count++] = base + 2 - 0x160;
        }
    }
    else if (p.kind == 1 || p.kind == 3)
    {
        target = focus;
        candidates[count++] = p.message;
    }
    else return true;
    if (filter && target != filter) return false;
    if (!minimum && !maximum) return true;
    for (unsigned i = 0; i < count; ++i)
        if (candidates[i] >= minimum && candidates[i] <= maximum) return true;
    return false;
}
#include "user_touch.inc"
// One packet of the wheel or of the trackpad (CaptureScroll). A notch of a
// wheel is WM_MOUSEWHEEL with WHEEL_DELTA as ever. The steps of a trackpad
// are smaller than a notch (120 units are 40 pixels of the fingers' way):
// - One gesture has one window: the one chosen when the fingers came down.
// The steps the Mac adds after the fingers left (momentum) go there and end
// when the pointer or the focus is elsewhere by then; a change of direction
// forgets the rest that was collected.
// - Sideways there is no message of Win98. The nearest window from the target
// up with a horizontal standard scroll bar gets WM_HSCROLL, a line for
// every 40 units, as the mouse drivers of the time did it.
// - Up and down the window gets WM_MOUSEWHEEL in whole lines, 40 units each
// (WHEEL_DELTA over the three lines of SPI_GETWHEELSCROLLLINES): a program
// that divides by the lines (Gecko) scrolls line by line, one that adds
// the deltas up (COMCTL32) scrolls at every third. A program that only
// knows whole notches does nothing with them. That shows: when two notches
// of small steps went to a window and its thread neither made anything
// invalid nor moved bits, the window gets the notches it missed and whole
// notches from then on.
namespace
{
struct WheelRoute
{
    HWND target = 0, focus_then = 0;
    unsigned time = 0;
    int rest_y = 0, rest_x = 0;
    bool dropped = false;
    struct Known
    {
        HWND window = 0;
        unsigned mode = 0, activity = 0, since = 0; // mode: 0 not known yet, 1 lines, 2 whole notches
        int sent = 0;
    } known[16];
    unsigned next = 0;
    Known &Of(HWND h)
    {
        for (auto &k : known)
            if (k.window == h) return k;
        Known &k = known[next++ % 16];
        k = Known();
        k.window = h;
        return k;
    }
} wheel_route;
constexpr int WheelLine = 40, WheelNotch = 120;
} // namespace
static void RouteWheel(const InputPacket &p)
{
    auto &route = wheel_route;
    const unsigned flags = p.changed;
    const bool fine = flags & ScrollFine, momentum = flags & ScrollMomentum;
    int dy = (int)(short)(p.parameter >> 16), dx = (int)p.buttons;
    const HWND now = WheelWindow(p);
    const bool fresh = !fine || (flags & ScrollBegin) || !Find(route.target) || p.milliseconds - route.time > 400 ||
                       (!momentum && now != route.target);
    if (fresh)
    {
        route.target = now;
        route.focus_then = focus;
        route.rest_x = route.rest_y = 0;
        route.dropped = false;
    }
    else if (!route.dropped && momentum && (now != route.target || focus != route.focus_then))
        route.dropped = true;
    route.time = p.milliseconds;
    const HWND target = route.target;
    if (!target || route.dropped) return;
    const unsigned keys = p.parameter & 65535, place = (unsigned(p.x) & 65535) | (unsigned(p.y) & 65535) << 16;
    if (dx)
    {
        if (route.rest_x && (dx < 0) != (route.rest_x < 0)) route.rest_x = 0;
        route.rest_x += dx;
        int lines = route.rest_x / WheelLine;
        route.rest_x -= lines * WheelLine;
        HWND owner = 0;
        for (HWND h = target; lines && h && Find(h) && !owner; h = (Style(h) & 0x40000000) ? Parent(h) : 0)
            if (Style(h) & 0x00100000) owner = h; // WS_HSCROLL
        if (owner)
        {
            for (int i = 0, n = lines < 0 ? -lines : lines; i < n && i < 40; ++i)
                PostInput(p, owner, 0x114, lines < 0 ? 0 : 1, 0); // SB_LINELEFT, SB_LINERIGHT
            PostInput(p, owner, 0x114, 8, 0);                     // SB_ENDSCROLL
        }
    }
    if (!dy) return;
    const auto post = [&](int delta) {
        if (delta > 273 * WheelNotch) delta = 273 * WheelNotch;
        if (delta < -273 * WheelNotch) delta = -273 * WheelNotch;
        PostInput(p, target, 0x20a, (unsigned(delta) & 65535) << 16 | keys, place);
    };
    if (!fine)
    {
        post(dy);
        return;
    }
    if (route.rest_y && (dy < 0) != (route.rest_y < 0)) route.rest_y = 0;
    route.rest_y += dy;
    auto &known = route.Of(target);
    const unsigned activity = wheel_activity[WindowThread(target) & 63];
    if (!known.mode && known.sent)
    {
        if (activity != known.activity)
            known.mode = 1;
        else if (known.sent >= 2 * WheelNotch && p.milliseconds - known.since >= 250)
        {
            known.mode = 2;
            post((dy < 0 ? -1 : 1) * (known.sent / WheelNotch) * WheelNotch);
        }
    }
    const int quantum = known.mode == 2 ? WheelNotch : WheelLine;
    const int part = route.rest_y / quantum * quantum;
    if (!part) return;
    route.rest_y -= part;
    if (!known.mode)
    {
        if (!known.sent)
        {
            known.activity = activity;
            known.since = p.milliseconds;
        }
        known.sent += part < 0 ? -part : part;
    }
    post(part);
}
// The window the last gesture went to, for the host's log of it.
HWND WheelTarget() { return Find(wheel_route.target) ? wheel_route.target : 0; }
void RouteInput(const InputPacket &p, void *context, InputFocus callback, InputActivate activate)
{
    if (p.kind == 3)
    {
        RouteWheel(p);
        return;
    }
    if (p.kind == 2 && (p.changed & p.buttons))
    {
        const HWND held = CaptureFor(p);
        NoteInputPress(held ? held : InputWindow(p), p.milliseconds);
        // ds:154h: the first press fixes the queue of the buttons that follow.
        if (!(p.buttons ^ p.changed)) press_owner = held ? held : InputWindow(p);
    }
    else if (p.kind == 1 && (p.message == 0x100 || p.message == 0x104))
        NoteInputPress(focus, p.milliseconds);
    if(BarRouteInput(p))return;
    if(PopupRouteInput(p))return;
    if(FrameTrackRouteInput(p))return;
    if (p.kind == 2)
    {
        RouteMouse(p, context, callback, activate);
        return;
    }
    if (p.kind != 1)
        return;
    // USER consults the hot-key list before the keystroke is routed and
    // swallows it on a hit.
    if (ShellKeyInput(p) || HotKeyInput(p) || WindowHotKeyInput(p)) return;
    if (focus)PostInput(p, focus, p.message, p.key, p.parameter);
}
void KeyboardRaw(unsigned vk, unsigned character, bool down, unsigned scan, bool extended)
{
    RouteInput(CaptureKey(vk, character, down, scan, extended));
}
void KeyboardModifiers(unsigned mask)
{
    static const unsigned vk[] = {0xa2, 0xa0, 0xa4, 0x5b, 0xa3, 0xa1, 0xa5, 0x5c};
    static const unsigned scans[] = {0x1d, 0x2a, 0x38, 0x5b, 0x1d, 0x36, 0x38, 0x5c};
    for (unsigned i = 0; i < 8; ++i)
        if (input.Down(vk[i]) != bool(mask & (1u << i)))
            KeyboardRaw(vk[i], 0, mask & (1u << i), scans[i], i == 3 || i == 4 || i == 6 || i == 7);
}
// The language of the layout KEYBOARD.DRV has loaded. The runtime installs
// one layout for all processes (X86::Application::ConfigureKeyboardLayout).
static unsigned keyboard_language = 0x0409;
void SetKeyboardLanguage(unsigned language) { keyboard_language = language & 65535; }
unsigned KeyboardLanguage() { return keyboard_language; }
// USER: the key of the message through KEYBOARD.DRV's ToAscii
// (ordinal 4, or ToAsciiEx with the thread's layout when several are loaded):
// wParam as the virtual key, the high word of lParam as the scan code with
// its release bit 8000h, the thread's key state and the flags word. Bit 1 of
// the flags is the extended bit of lParam (bit 24 shifted down by 7), bit 0
// says the thread is in menu mode. The count is 0 for nothing, negative for a
// dead key.
int TranslateKey(unsigned vk, unsigned lparam, unsigned char out[2], const unsigned char *state)
{
    out[0] = out[1] = 0;
    const auto *layout = Win98KeyboardTranslate::FindLayout(keyboard_language);
    if (!layout) return 0;
    const unsigned scan = lparam >> 16;
    const unsigned flags = ((scan & 0x100) >> 7) | (PopupSession() ? 1u : 0u);
    return Win98KeyboardTranslate::ToAscii(*layout, Win98KeyboardTranslate::DriverState(), vk & 65535, scan,
                                           state ? state : input.delivered, flags, out);
}
// Two bits of the thread's queue record that DefWindowProc keeps between key
// messages (USER on word 0Ch of the record at ES:[10h]): 8 when ALT
// went down with no other key, 10h when F10 went down. One slot per thread.
namespace { struct MenuKeySlot { unsigned thread = 0, flags = 0; }; MenuKeySlot menu_key_slots[32]; }
unsigned MenuKeyFlags(unsigned thread, unsigned clear, unsigned set)
{
    MenuKeySlot *slot = nullptr, *empty = nullptr;
    for (auto &s : menu_key_slots)
    {
        if (s.thread == thread) { slot = &s; break; }
        if (!s.thread && !empty) empty = &s;
    }
    if (!slot)
    {
        if (!set) return 0;
        slot = empty ? empty : &menu_key_slots[thread % 32];
        *slot = {thread, 0};
    }
    const unsigned previous = slot->flags;
    slot->flags = (slot->flags & ~clear) | set;
    return previous;
}
bool TranslateMessage(const Message *msg)
{
    // USER takes WM_KEYDOWN, WM_KEYUP, WM_SYSKEYDOWN and WM_SYSKEYUP
    // and answers 1 for all four, with or without a character ( jumps
    // to `mov al, 1`); any other message gives 0. USER32's entry
    // (USER32.DLL 0BFF55769) checks the same four before it thunks down and
    // answers 0 for NULL or any other message itself.
    if(!msg)return false;
    const unsigned message=msg->message;
    if(message!=0x100&&message!=0x101&&message!=0x104&&message!=0x105)return false;
    // The character comes from the virtual key, the scan code and the key
    // state, whoever put the key into the queue: a key a program posted
    // itself translates like one from the keyboard.
    unsigned char characters[2];
    int count=TranslateKey(msg->wparam,msg->lparam,characters);
    if(!count)return true;
    // WM_CHAR, or WM_DEADCHAR for a negative count; the system
    // bit 4 of the key message carries over to WM_SYSCHAR/WM_SYSDEADCHAR on
    // a key going down only, so the character an Alt sequence gives on the
    // release of ALT is a plain WM_CHAR.
    unsigned character_message=0x102;
    if(count<0){count=-count;character_message=0x103;}
    if((message&~4u)==0x100)character_message|=message&4;
    // one message per character, to the window the MSG names,
    // whatever window the key was retrieved for: a WH_GETMESSAGE hook or the
    // program may redirect it before it translates.
    for(int i=0;i<count&&i<2;++i)PostMessage(msg->hwnd,character_message,characters[i],msg->lparam);
    return true;
}
// Serve native-control paint requests for the bound thread when its queue has
// no other work, like QS_PAINT. Guest callbacks then run on the window's
// owning thread.
void PaintControlSurface(HWND h)
{
    const unsigned generation = dirty.generation;
    auto *w = Find(h);
    if (!w) return;
    // Mark failed attempts complete too, or the same window would keep its
    // thread runnable forever.
    w->painted_generation = generation;
    w->needs_paint = false;
    if(w->width<=0||w->height<=0)return;
    const auto dc = Acquire(h, *w, true);
    if (!dc) return;
    USER_TRACE("PAINT control hwnd=%u thread=%u generation=%u\n", h, w->thread, generation);
    // Send WM_NCPAINT before WM_ERASEBKGND and WM_PAINT. Include the frame on
    // the first paint even if Render has not yet seen the control, so etched
    // separators appear immediately.
    if (!w->drawn) w->frame_owed = true;
    // This paint covers the whole window, with or without a frame.
    const bool whole = w->frame_owed ||
        (w->client.left == 0 && w->client.top == 0 && w->client.right == w->width && w->client.bottom == w->height);
    if (w->frame_owed)
    {
        w->frame_owed = false;
        PaintVisibleFrame(h, *w);
    }
    // A STATIC whose window procedure a program replaced gets its paint as
    // WM_PAINT through that procedure, as DispatchMessage delivers it under
    // Win98; the procedure paints itself or hands the message on to the
    // class procedure, which paints the label (PaintLabel). Windows
    // Installer paints its Text controls this way.
    // The update region stays for the BeginPaint of that paint.
    // A paint the label owes without one is the immediate GetDC paint of its
    // own messages, which does not go
    // through the window procedure.
    if ((w->label || w->button) && guest_paint && GuestProcedure(*w) && w->update.count)
    {
        Release(*w, dc, true);
        guest_paint(CallbackContext(guest_paint_context, h), h);
        if (auto *again = Find(h))
        {
            again->drawn = true;
            again->painted_generation = generation;
            again->needs_paint = false;
            again->update.Clear();
            again->erase_pending = false;
            // This paint has consumed the label's invalidation, including
            // its native frame bookkeeping; composition must not repaint it.
            if (again->label || (whole && SharesPixels(h, *again)))
            {
                again->native_invalid = false;
                again->native_signature = NativeSignature(h, *again);
            }
        }
        return;
    }
    // This paint of a control with children (the STATIC that holds the
    // EDIT boxes of a Windows Installer MaskedEdit) leaves their
    // area out. Win98 paints such a parent only when it is invalid, and then
    // its children, invalid with it, paint afterwards; this paint also comes
    // with every new frame, after them. A WS_EX_TRANSPARENT child is drawn
    // over its parent, so its area stays in.
    {
        const auto area = ClientScreenRect(*w);
        for (HWND c = w->first; c;)
        {
            const auto *child = Find(c);
            if (!child) break;
            if (Shows(*child) && !(child->extended & 0x20u))
            {
                const auto r = WindowScreenRect(*child);
                Gdi::ExcludeClipRect(dc, r.left - area.left, r.top - area.top, r.right - area.left, r.bottom - area.top);
            }
            c = child->next;
        }
    }
    // Windows 98 draws controls over the existing parent client. Unpainted
    // pixels retain the parent image; group boxes leave their interiors
    // untouched.
    PaintControlContent(h, *w, dc);
    if (auto *again = Find(h))
    {
        again->drawn = true;
        again->painted_generation = generation;
        again->needs_paint = false;
        again->update.Clear();
        again->erase_pending = false;
        // Mark this overlapping control's state as painted. PaintWindow must
        // not repaint it on the next unrelated frame, which would cover the
        // sibling that painted afterwards.
        if (whole && (again->label || SharesPixels(h, *again)))
        {
            again->native_invalid = false;
            again->native_signature = NativeSignature(h, *again);
        }
        Release(*again, dc, true);
    }
}
// UpdateWindow for a native control with callbacks: it paints now, on the
// calling thread, if it owes a paint (USER sends WM_PAINT to a window
// with an update region, first takes it out of its parents' regions).
void UpdateControl(HWND h)
{
    ServeOwnerBars(h);
    auto *w = Find(h);
    if (!w || !ControlContent(*w) || !w->redraw || !IsVisible(h) || Locked(h) || !RedrawChain(h)) return;
    if (!w->needs_paint && !w->update.count) return;
    // Leave painting pending while another thread holds the primary surface
    // lock. Combo boxes and toolbars enter here internally; public
    // UpdateWindow waits at its gate first.
    if (ScreenLockedElsewhere(CurrentProcess(), CurrentThread())) return;
    SettleGeometry();
    ClipAncestorUpdates(h);
    PaintControlSurface(h);
}
bool PendingControlPaint()
{
    SettleGeometry();
    if (PendingPopupInput() || PendingPopupPaint() || PendingOwnerBars()) return true;
    for (unsigned i = 0; i < MaxWindows; ++i)
    {
        const auto &w = windows[i];
        if (!w.used || !ControlContent(w) || !w.redraw) continue;
        if (active_thread && w.thread != active_thread) continue;
        if (active_process && w.process != active_process) continue;
        if (!w.needs_paint && !w.update.count) continue;
        // Count a failed attempt as serviced so this surface cannot keep the
        // thread runnable indefinitely.
        if (w.painted_generation == dirty.generation) continue;
        if (!IsVisible(Handle(i)) || AncestorPaintPending(Handle(i)) || Locked(Handle(i)) || !RedrawChain(Handle(i))) continue;
        return true;
    }
    return false;
}
// A paint that is still open does not stop this. Win98 keeps the update
// region per window and hands out QS_PAINT for every window of a thread that
// waits for messages; a program may run a modal loop inside WM_PAINT, and
// Dev-C++ shows its "Tip of the day" that way from the paint of its main form.
// The port counted open paints for the whole system here, so no control of
// any thread was painted until that paint ended.
// USER (ECGetBrush). A read-only or disabled EDIT of a 4.0 program
// asks with WM_CTLCOLORSTATIC, any other with WM_CTLCOLOREDIT; DefWindowProc
// answers COLOR_3DFACE and COLOR_WINDOW. A disabled one then writes in
// COLOR_GRAYTEXT (single line, or any 4.0 program), unless that is the
// background colour.
void AskEditColors(HWND h)
{
    auto *w = Find(h);
    if (!w || !w->edit) return;
    const auto *owner = Process(w->process);
    const bool win40 = (owner ? owner->version : settings.expected) >= 0x400;
    const bool readonly = w->style & 0x800, disabled = !w->enabled;
    const unsigned message = (readonly || disabled) && win40 ? 0x138 : 0x133;
    const unsigned *colours = Win98Frame::SystemColors();
    auto ref = [](unsigned rgb) { return Gdi::RGB(rgb >> 16, rgb >> 8, rgb); };
    // The measuring DC of the EDIT carries the question; its colours are put
    // back afterwards (no SaveDC, which would copy the clip region).
    const Gdi::HDC dc = w->text_dc;
    const HWND parent = w->parent ? w->parent : w->owner;
    unsigned brush = 0, text = ref(colours[8]);
    if (dc) {
        const unsigned old_text = Gdi::GetTextColor(dc), old_back = Gdi::GetBkColor(dc);
        Gdi::SetTextColor(dc, ref(colours[8]));
        Gdi::SetBkColor(dc, ref(colours[message == 0x138 ? 15 : 5]));
        if (parent && control_color) brush = control_color(CallbackContext(control_context, parent), parent, h, dc, message);
        w = Find(h);
        if (!w || !w->edit) return;
        text = Gdi::GetTextColor(dc);
        if (disabled && (!(w->style & 4) || win40)) {
            const unsigned gray = ref(colours[17]);
            if (gray != Gdi::GetBkColor(dc)) text = gray;
        }
        Gdi::SetTextColor(dc, old_text);
        Gdi::SetBkColor(dc, old_back);
    }
    const unsigned fill = colours[message == 0x138 ? 15 : 5];
    // The composition drew an EDIT it had not asked yet white with black
    // text; only a different answer needs a new frame.
    const bool changed = w->edit_color_known
        ? brush != w->edit_brush || fill != w->edit_fill || text != w->edit_text
        : brush || fill != 0xffffff || text != 0;
    w->edit_brush = brush;
    w->edit_fill = fill;
    w->edit_text = text;
    w->edit_color_known = true;
    w->edit_color_pending = false;
    if (changed) dirty = true;
}
unsigned ServeControlPaints()
{
    if (serving_controls || rendering || !screen) return 0;
    // Leave the request pending for the next pass while another thread holds
    // the primary surface lock.
    if (ScreenLockedElsewhere(CurrentProcess(), CurrentThread())) return 0;
    SettleGeometry();
    serving_controls = true;
    unsigned painted = 0;
    // Callbacks can create or destroy windows. Resolve every handle again
    // before painting.
    for (unsigned i = 0; i < MaxWindows; ++i)
    {
        const auto h = Handle(i);
        const auto *w = Find(h);
        if (w && w->edit && w->edit_color_pending && (!active_thread || w->thread == active_thread) &&
            (!active_process || w->process == active_process) && IsVisible(h))
        {
            AskEditColors(h);
            continue;
        }
        if (!w || !ControlContent(*w)) continue;
        if (!w->redraw) continue;
        // STATIC exposes its update region as WM_PAINT to the application's
        // message loop. Immediate text/image paints without a region still
        // run here; UpdateWindow uses UpdateControl explicitly.
        if (w->label && w->update.count) continue;
        if (active_thread && w->thread != active_thread) continue;
        if (active_process && w->process != active_process) continue;
        if (!w->needs_paint && !w->update.count) continue;
        if (w->painted_generation == dirty.generation) continue;
        if (!IsVisible(h) || Locked(h) || !RedrawChain(h)) continue;
        // At a message wait a control waits for the paint of a parent that
        // would erase over it (WM_PAINT order).
        if (AncestorPaintPending(h)) continue;
        PaintControlSurface(h);
        ++painted;
    }
    painted += ServePopupPaints();
    painted += ServeOwnerBars();
    serving_controls = false;
    ServeDragIcons();
    return painted;
}

// USER step four: a window whose caption has no icon yet is asked for
// one with WM_QUERYDRAGICON (0037h) and a limit of 100 ms. The message belongs to the thread that owns the window, so it
// goes out here, beside the control paints, and the answer stays on the
// window record. The composition itself never calls into a guest.
unsigned ServeDragIcons()
{
    if (!drag_icon || serving_controls || serving_drag_icons || rendering) return 0;
    serving_drag_icons = true;
    unsigned asked = 0;
    for (unsigned i = 0; i < MaxWindows; ++i)
    {
        const auto h = Handle(i);
        const auto *w = Find(h);
        if (!w || !w->used || w->icon_drag_asked) continue;
        if (!w->ready || !w->visible || w->parent) continue;
        // Windows 98 runs for a caption, so a window that has one is asked, and
        // so is a top-level window without an owner: that is the window the
        // taskbar draws with DrawCaptionTempA (EXPLORER.EXE+0x7253), and
        // DrawCaption reaches for it too. VB3's ThunderRTMain is such a
        // window, a popup with WS_SYSMENU and no caption. The icon is
        // only drawn with WS_SYSMENU, so a window without it is never asked.
        if ((w->style & 0x00080000u) == 0) continue;
        if ((w->style & 0x00c00000u) != 0x00c00000u && w->owner) continue;
        if (w->icon_small || w->icon_large || w->icon_drag) continue;
        if (active_thread && w->thread != active_thread) continue;
        if (active_process && w->process != active_process) continue;
        // The class icon comes first and stops the question.
        if (ClassAccess(h, -34, 0, 2) || ClassAccess(h, -14, 0, 2)) continue;
        auto *context = CallbackContext(drag_icon_context, h);
        const unsigned answer = drag_icon(context, h);
        auto *record = Find(h);
        if (!record) continue;
        record->icon_drag_asked = true;
        record->icon_drag = answer;
        if (answer) dirty = true;
        ++asked;
    }
    serving_drag_icons = false;
    return asked;
}
// USER painting and presentation. Applications draw directly into the screen
// surface. Render settles visible regions, draws the desktop, USER-owned
// window content and popup menus, then publishes the host image.
#include "user_desktop.inc"
bool Render()
{
    if(!screen||rendering||serving_controls)return false;
    // Do not draw while another thread holds a DirectDraw primary surface
    // lock. Leave the request pending.
    if(ScreenLockedElsewhere(CurrentProcess(),CurrentThread()))return false;
    SettleGeometry();
    // USER paints in every display mode, clipped to visible regions. An
    // exclusive DirectDraw window covers the screen, leaving no visible area
    // for other windows.
    if(!dirty&&!screen_touched)return false;
    USER_PROFILE(UserRender);
    const int render_save=Gdi::SaveDC(screen);
    if(!render_save)return false;
    USER_TRACE("RENDER dirty=%u\n", dirty ? 1u : 0u);
    rendering=true;
    if(dirty)
    {
        dirty=false;
        Gdi::ResetSurfaceDC(screen);Gdi::SelectObject(screen,system_font);
        // PaintDesktop clears invalid desktop-owned areas with
        // COLOR_BACKGROUND.
        if(desktop.update.count&&!Locked(Desktop))
        {
            auto *visible=new RectList;
            WindowVisible(Desktop,{0,0,desktop.width,desktop.height},true,*visible);
            const int save=Gdi::SaveDC(screen);
            if(save&&visible->n&&Gdi::IntersectClipRegion(screen,visible->r,visible->n)&&
               Gdi::IntersectClipRegion(screen,desktop.update.rectangles,desktop.update.count))
                PaintDesktop(screen);
            if(save)Gdi::RestoreDC(screen,save);
            delete visible;
            desktop.update.Clear();desktop.erase_pending=false;
        }
        PaintSiblings(Desktop);
        PaintPopupMenus();
    }
    if (host_window_surfaces) ComposeHostWindows();
    Gdi::RestoreDC(screen,render_save);
    screen_touched=false;
    // Guest owner-draw and colour callbacks can yield to the host. Publish
    // only after the entire pass, never the buffer they are painting.
    PublishScreen();
    rendering=false;++frame_serial;
    // The pointer is not part of the frame: the Win98 display driver draws it
    // over the finished frame, and so does MovePointer.
    ResetPointerOverlay();MovePointer();
    return true;
}
// LockWindowUpdate (USER through DCX_LOCKWINDOWUPDATE): the window and
// its children draw nothing while the lock stands; their DCs get an empty
// visible region and USER does not paint them either. Releasing the lock
// makes the window invalid, and it draws again (RepaintLockedWindow).
bool LockWindowUpdate(HWND h)
{
    const unsigned process = CurrentProcess(), thread = CurrentThread();
    if (locked_window) {
        if (process != locked_process || thread != locked_thread || h) return false;
        DropWindowUpdateLock(true);
        GeometryChanged();
        return true;
    }
    if (!h || !Find(h)) return false;
    locked_window = h; locked_process = process; locked_thread = thread;
    LockBounds(true);
    GeometryChanged();
    dirty = true;
    return true;
}
void ReleaseWindowUpdateLock(unsigned process, unsigned thread)
{
    if (locked_window && locked_process == process && locked_thread == thread) {
        DropWindowUpdateLock(true);
        GeometryChanged();
    }
}
const unsigned char *Pixels(){MaterializeScreen();return published_frame;}
void PixelsSize(int &width, int &height){width=published_width;height=published_height;}
bool Dirty(){return dirty||screen_touched;}
unsigned FrameSerial(){return frame_serial;}
// The application has drawn through a DC directly into the screen surface.
// Render presents those pixels on the next frame.
void DrawingChanged(Gdi::HDC dc)
{
    USER_PROFILE(UserDrawing);
    if(!dc)return;
    auto touched=[&](HWND h,Window &w){
        if(trace_paint){Gdi::RECT box{};const unsigned kind=Gdi::GetClipBox(dc,&box);USER_TRACE("DRAWN hwnd=%u dc=%x clip=%u:%d,%d,%d,%d\n",h,dc,kind,box.left,box.top,box.right,box.bottom);}
        w.drawn=true;
        screen_touched=true;
        if(!AnySaved())return;
        // Compare saved pixels against collected DCE drawing bounds
        // intersected with the window rectangle. Painting outside the saved
        // area leaves it valid, so hiding the covering window can restore it
        // without invalidation. Paint DCs are also restricted to their update
        // regions. For USER drawing paths without collected bounds, use the
        // whole rectangle.
        Gdi::RECT area=WindowScreenRect(w),cut{};
        for(unsigned i=0;i<w.leases;++i)if(w.lease[i].dc==dc&&w.lease[i].paint_clip){
            const auto client=ClientScreenRect(w);
            auto update=w.lease[i].paint_clip->Bounds();
            const Gdi::RECT on_screen{update.left+client.left,update.top+client.top,update.right+client.left,update.bottom+client.top};
            if(!Win98::IntersectRect(&cut,&area,&on_screen))return;
            area=cut;
        }
        // LockWindowUpdate reads and clears these bounds on DC release. Only
        // read them here.
        Gdi::RECT drawn{};
        if(Gdi::GetBoundsRect(dc,&drawn,locked_window?0x8000u:0x8001u)&2){
            if(!Win98::IntersectRect(&cut,&area,&drawn))return;
            area=cut;
        }
        UnderlyingChanged(h,area);
    };
    // A window DC already draws into screen memory; Render publishes the next
    // frame.
    if(dc!=screen){
        if(OwnsDC(desktop,dc)){touched(Desktop,desktop);return;}
        for(unsigned index=0;index<MaxWindows;++index){auto &w=windows[index];if(w.used&&OwnsDC(w,dc)){touched(Handle(index),w);return;}}
    }
    // An application display DC shares the screen DC's pixels and must count
    // as drawing to the screen.
    if(dc==screen||Gdi::SameSurface(dc,screen)){
        USER_TRACE("DRAWN screen same=%u\n",dc!=screen?1u:0u);
        if(!rendering){PublishScreen();++frame_serial;ResetPointerOverlay();MovePointer();}
        return;}
}
unsigned LiveWindows(){unsigned n=0;for(auto &w:windows)n+=w.used;return n;}
unsigned LiveMenus(){unsigned n=0;for(auto &m:menus)n+=m.used;return n;}
unsigned MenuCapacity(){return MaxMenus;}
// How many slots of the menu table are taken. Only a diagnosis reads this: a
// menu template that is refused has to say what ran out.
unsigned MenuCount(){unsigned n=0;for(unsigned i=0;i<MaxMenus;++i)if(menus[i].used)++n;return n;}
// Visible windows whose update region is still waiting for WM_PAINT: a
// composition at this moment shows an erased or half-drawn area.
unsigned PendingPaintWindows(unsigned *first){SettleGeometry();unsigned n=0;for(unsigned i=0;i<MaxWindows;++i){auto&w=windows[i];if(w.used&&w.update.count&&IsVisible(Handle(i))){if(first&&!n)*first=Handle(i);++n;}}return n;}
#include "user_window_parent.inc"
HWND ThreadCapture(unsigned thread) { return QueueCapture(thread); }
HWND ProcessCapture(unsigned pid) { return ProcessCaptureOf(pid); }
#include "user_mouse_capture.inc"

// The paint of the STATIC class procedure : into the DC of
// WM_PAINT, which the caller took with BeginPaint or passed in wParam.
bool PaintLabel(HWND h, Gdi::HDC dc)
{
    auto *w = Find(h);
    if (!w || !w->label || !dc || !IsVisible(h)) return false;
    PaintControlContent(h, *w, dc);
    if (auto *again = Find(h))
    {
        again->drawn = true;
        again->painted_generation = dirty.generation;
        // A callback may invalidate the label again. Otherwise its WM_PAINT
        // has also served the native content request, not just the region.
        again->needs_paint = again->update.count != 0;
        if (!again->needs_paint)
        {
            again->native_invalid = false;
            again->native_signature = NativeSignature(h, *again);
        }
    }
    return true;
}

bool PaintButton(HWND h, Gdi::HDC dc)
{
    auto *w=Find(h);
    if(!w||!w->button||!dc||!IsVisible(h))return false;
    PaintControlContent(h,*w,dc);
    if(auto *again=Find(h)){again->drawn=true;again->needs_paint=false;}
    return true;
}

}
