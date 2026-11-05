// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside USER's native window provider. The drawn cursor: its shape,
// its counter, its clip rectangle and the WM_SETCURSOR the pointer sends on
// every move. The pixels come from the original USER.EXE cursor resources; the
// adapter rasters them through the shared image provider.
namespace {
void *cursor_context = nullptr;
CursorDraw cursor_draw = nullptr;
// USER keeps the ShowCursor level in the message queue, not in one global
// word: counts at [queue+3Ah] (and the thread's own tally at
// [thread+54h]), and the pointer follows the level of the
// cursor queue [0E38h] only. makes the queue of
// the capture window, or else of the window under the pointer, the cursor
// queue, and DestroyQueue forgets a dying cursor queue. A program
// that hides the pointer and ends therefore leaves nothing behind.
// The port has one queue per thread, so a level belongs to (process, thread).
struct PointerLevel { unsigned process = 0, thread = 0; int value = 0; bool used = false; };
PointerLevel pointer_levels[128];
unsigned cursor_shape = 0;      // the handle SetCursor last stored
bool cursor_composited = false; // off by default, so screenshots do not move
Win98Cursor::Rect cursor_clip = {0, 0, 800, 600};
bool cursor_clipped = false;
unsigned cursor_setcursor_window = 0, cursor_setcursor_hit = 0;
// The overlay: where the shape currently sits in the published frame, and the
// process whose icon table holds the shape.
void *cursor_owner_context = nullptr;
bool pointer_drawn = false;
int pointer_drawn_x = 0, pointer_drawn_y = 0;
unsigned pointer_drawn_shape = 0;
} // namespace

void SetCursorProvider(void *context, CursorDraw draw) { cursor_context = context; cursor_draw = draw; }
void ShowPointer(bool on) { if (cursor_composited != on) { cursor_composited = on; MovePointer(); } }
bool PointerComposited() { return cursor_composited; }
unsigned SetPointerShape(unsigned cursor)
{
    const unsigned old = cursor_shape;
    cursor_owner_context = CurrentContext(cursor_context);
    if (old != cursor) { cursor_shape = cursor; MovePointer(); }
    return old;
}
unsigned PointerShape() { return cursor_shape; }
namespace {
PointerLevel *FindPointerLevel(unsigned process, unsigned thread, bool create)
{
    PointerLevel *free_entry = nullptr;
    for (auto &l : pointer_levels)
    {
        if (l.used && l.process == process && l.thread == thread) return &l;
        if (!free_entry && (!l.used || !l.value)) free_entry = &l;
    }
    if (!create || !free_entry) return nullptr;
    *free_entry = {process, thread, 0, true};
    return free_entry;
}
// The level of the cursor queue: Windows 98 takes the capture window first, else
// WindowFromPoint at the pointer; a window without a queue level is at zero.
int CursorQueueLevel()
{
    const HWND h = PointerCapture();
    const auto *w = h ? Find(h) : nullptr;
    const auto *l = w ? FindPointerLevel(w->process, w->thread, false) : nullptr;
    return l ? l->value : 0;
}
} // namespace
int ShowPointerCount(bool show)
{
    auto *l = FindPointerLevel(active_process, active_thread, true);
    Win98Cursor::Counter counter;
    if (l) counter.value = l->value;
    const int result = Win98Cursor::Show(counter, show);
    if (l) l->value = result;
    MovePointer();
    return result;
}
// the queue dies with its thread, and with it the level. A thread of
// zero drops every level of the process.
void DropPointerLevel(unsigned process, unsigned thread)
{
    for (auto &l : pointer_levels)
        if (l.used && l.process == process && (!thread || l.thread == thread)) l = {};
    MovePointer();
}
int PointerCount() { return CursorQueueLevel(); }
bool PointerVisible()
{
    Win98Cursor::Counter counter;
    counter.value = CursorQueueLevel();
    return Win98Cursor::Visible(counter) && cursor_shape != 0;
}
bool ClipPointer(const Gdi::RECT *r, int *moved_x, int *moved_y)
{
    Win98Cursor::Rect want;
    if (r) want = {r->left, r->top, r->right, r->bottom};
    int width = 800, height = 600, depth = 32;
    ScreenMode(width, height, depth);
    const bool okay = Win98Cursor::Clip(cursor_clip, cursor_clipped, r ? &want : nullptr,
                                        {0, 0, width, height});
    if (cursor_clipped)
    {
        int x = mouse_x, y = mouse_y;
        Win98Cursor::Confine(cursor_clip, x, y);
        if (x != mouse_x || y != mouse_y)
        {
            // The caller queues the move as input when it can; routing it
            // here would ask the window under the new point (WM_NCHITTEST)
            // inside ClipCursor and wait for another thread.
            if (moved_x && moved_y) { *moved_x = x; *moved_y = y; }
            else MouseButtons(x, y, PhysicalMouseButtons());
        }
    }
    return okay;
}
// Clamp both host mouse movement and guest SetCursorPos to the ClipCursor
// rectangle.
void ConfinePointer(int &x, int &y)
{
    if (cursor_clipped) Win98Cursor::Confine(cursor_clip, x, y);
}
bool PointerClip(Gdi::RECT *r)
{
    if (!r) return false;
    *r = {cursor_clip.left, cursor_clip.top, cursor_clip.right, cursor_clip.bottom};
    return true;
}
bool PointerClipActive() { return cursor_clipped; }
// The pointer is not part of the composition. Under Win98 the display driver
// owns it: it keeps the pixels the shape covers, restores them before the
// pointer leaves a place and draws the shape at the new one, all in the mouse
// interrupt and without repainting a single window. NEO98 does the same over
// the published frame, so a move costs two small blits and depends on no guest
// thread. The composition in Render stays pointer-free and is the source the
// overlay restores from.
namespace {
// The box a 32 by 32 shape can occupy around the hot spot, clipped to screen.
void ClippedPointerBox(int x, int y, int width, int height, int &left, int &top, int &right, int &bottom)
{
    const auto clip = [](long long value, int limit) { return value < 0 ? 0 : value > limit ? limit : int(value); };
    left = clip(static_cast<long long>(x) - 31, width);
    top = clip(static_cast<long long>(y) - 31, height);
    right = clip(static_cast<long long>(x) + 33, width);
    bottom = clip(static_cast<long long>(y) + 33, height);
}
void PointerBox(int x, int y, int &left, int &top, int &right, int &bottom)
{
    ClippedPointerBox(x, y, published_width, published_height, left, top, right, bottom);
}
void ResetPointerOverlay() { pointer_drawn = false; }
// Draw the pointer in every mode, including exclusive DirectDraw, unless the
// application hides it through ShowCursor or SetCursor. Compose it over a
// patch of the mode surface and scale it with the underlying image. Republish
// the same surface rectangle to remove the overlay.
void ModePointerBox(int x, int y, int &left, int &top, int &right, int &bottom)
{
    int width = 800, height = 600, depth = 32;
    ScreenMode(width, height, depth);
    ClippedPointerBox(x, y, width, height, left, top, right, bottom);
}
void MoveModePointer()
{
    const bool want = cursor_composited && cursor_draw && PointerVisible();
    if (!pointer_drawn && !want) return;
    if (pointer_drawn && want && pointer_drawn_x == mouse_x && pointer_drawn_y == mouse_y &&
        pointer_drawn_shape == cursor_shape)
        return;
    int left, top, right, bottom;
    if (pointer_drawn)
    {
        ModePointerBox(pointer_drawn_x, pointer_drawn_y, left, top, right, bottom);
        PublishModeRect(left, top, right, bottom, nullptr, 0);
        pointer_drawn = false;
    }
    if (want)
    {
        static unsigned char patch[64 * 64 * 4];
        ModePointerBox(mouse_x, mouse_y, left, top, right, bottom);
        if (left < right && top < bottom)
        {
            ReadModeRect(left, top, right, bottom, patch, 64 * 4);
            const auto dc = Gdi::CreateSurfaceDC({patch, 64, 64, 64 * 4, 32});
            if (dc)
            {
                if (cursor_draw(cursor_owner_context ? cursor_owner_context : CurrentContext(cursor_context),
                                dc, mouse_x - left, mouse_y - top, cursor_shape))
                {
                    PublishModeRect(left, top, right, bottom, patch, 64 * 4);
                    pointer_drawn = true;
                    pointer_drawn_x = mouse_x;
                    pointer_drawn_y = mouse_y;
                    pointer_drawn_shape = cursor_shape;
                }
                Gdi::DeleteDC(dc);
            }
        }
    }
    ++frame_serial;
}
} // namespace
void MovePointer()
{
    if (!published_frame || !framebuffer || rendering) return;
    // A pending scanout already owns the next frame. Its materialization
    // draws the current cursor after converting the current screen bytes;
    // drawing a patch now would force another full conversion per flip.
    if (published_deferred) return;
    // Every mode that is not 32 bits in the size of the host frame goes
    // converted into it, a program's as well as a desktop's; the
    // pointer goes with it.
    {
        int width = 800, height = 600, depth = 32;
        ScreenMode(width, height, depth);
        if (!PublishedDirect(width, height, depth)) { MoveModePointer(); return; }
    }
    const bool want = cursor_composited && cursor_draw && published_dc && PointerVisible();
    if (!pointer_drawn && !want) return;
    if (pointer_drawn && want && pointer_drawn_x == mouse_x && pointer_drawn_y == mouse_y &&
        pointer_drawn_shape == cursor_shape)
        return;
    if (pointer_drawn)
    {
        int left, top, right, bottom;
        PointerBox(pointer_drawn_x, pointer_drawn_y, left, top, right, bottom);
        for (int row = top; row < bottom; ++row)
            memcpy(published_frame + (row * published_width + left) * 4, framebuffer + (row * published_width + left) * 4,
                   unsigned(right - left) * 4);
        pointer_drawn = false;
    }
    if (want && cursor_draw(cursor_owner_context ? cursor_owner_context : CurrentContext(cursor_context),
                            published_dc, mouse_x, mouse_y, cursor_shape))
    {
        pointer_drawn = true;
        pointer_drawn_x = mouse_x;
        pointer_drawn_y = mouse_y;
        pointer_drawn_shape = cursor_shape;
    }
    ++frame_serial;
}
// USER sends WM_SETCURSOR to the window under the pointer on every move, with
// the hit code in the low half of lParam and the mouse message in the high
// half. DefWindowProc gives the parent first refusal and then picks.
//
// The original repeats both WM_NCHITTEST and WM_SETCURSOR for every pixel the
// pointer travels. Two synchronous guest callbacks per motion packet would cost
// more than they buy here, so the native frame answer decides when anything can
// have changed, and only then does the window procedure get asked.
void PointerMoved(HWND h, int x, int y)
{
    if (!frame_send || !IsWindow(h)) return;
    const unsigned native = FrameHitTest(h, x, y);
    if (cursor_setcursor_window == h && cursor_setcursor_hit == native) return;
    cursor_setcursor_window = h;
    cursor_setcursor_hit = native;
    const unsigned hit = HitTest(h, x, y);
    if (!IsWindow(h)) return;
    TrackSend(h, 0x20, h, (hit & 0xffff) | (0x200u << 16));
}
void ResetPointerCache() { cursor_setcursor_window = 0; cursor_setcursor_hit = 0; }
// Win98 has one cursor object for all processes; the OEM cursors are shared
// and outlive every program. The port keeps a copy of them per process and
// draws the shape out of the table of the process that set it last. When that
// process ends, its table goes with it: the pointer then drew through a freed
// Application and vanished. The process that ends hands the shape to
// a living one, preferably the one under the pointer, which USER would ask
// with WM_SETCURSOR at the next move anyway.
void *PointerOwner() { return cursor_owner_context; }
void *PointerHeir(void *leaving)
{
    const HWND h = PointerCapture();
    if (auto *p = Process(WindowProcess(h))) if (p->context && p->context != leaving) return p->context;
    for (auto *p=processes;p;p=p->next) if (p->context && p->context != leaving) return p->context;
    return nullptr;
}
void HandPointer(void *context, unsigned cursor)
{
    cursor_owner_context = context;
    cursor_shape = context ? cursor : 0;
    ResetPointerCache();
    MovePointer();
}
void ForgetPointerOwner(void *context)
{
    if (!context || cursor_owner_context != context) return;
    cursor_owner_context = nullptr;
    cursor_shape = 0;
    ResetPointerCache();
    MovePointer();
}
