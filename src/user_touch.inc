// SPDX-License-Identifier: GPL-2.0-or-later
// Included by user.cpp. All state belongs to the shared USER session.
namespace {
struct TouchWindow {
    HWND hwnd = 0;
    unsigned message = 0, count = 0;
    bool notified = false;
    NEO98_TOUCH_EVENT events[NEO98_TOUCH_QUEUE_SIZE] = {};
};
struct TouchContact {
    bool used = false, ignored = false, mouse = false;
    unsigned long long device = 0, finger = 0;
    unsigned id = 0;
    HWND hwnd = 0;
    int x = 0, y = 0;
};
TouchWindow touch_windows[32];
TouchContact touch_contacts[NEO98_TOUCH_MAX_CONTACTS];
unsigned touch_id = 0, touch_sequence = 0;
bool (*touchscreen_probe)() = nullptr;
TouchWindow *TouchFind(HWND h)
{
    for (auto &w : touch_windows) if (h && w.hwnd == h) return &w;
    return nullptr;
}
HWND TouchRoot(HWND h)
{
    for (unsigned n = 0; h && Parent(h) && Parent(h) != Desktop && n < MaxWindows; ++n) h = Parent(h);
    return h;
}
void TouchQueue(TouchWindow &w, NEO98_TOUCH_EVENT event)
{
    if (w.count == NEO98_TOUCH_QUEUE_SIZE) {
        w.count = 0;
        event = {};
        event.phase = NEO98_TOUCH_CANCEL_ALL;
        for (auto &c : touch_contacts) if (c.used && c.hwnd == w.hwnd) c.ignored = true;
    }
    event.sequence = ++touch_sequence;
    w.events[w.count++] = event;
    if (!w.notified) w.notified = PostMessage(w.hwnd, w.message, 0, 0);
}
} // namespace
void SetTouchscreenProbe(bool (*probe)()) { touchscreen_probe = probe; }
bool HasTouchscreen() { return touchscreen_probe && touchscreen_probe(); }
bool TouchRegister(HWND h, unsigned message)
{
    if (!IsWindow(h) || h == Desktop || WindowProcess(h) != CurrentProcess() ||
        WindowThread(h) != CurrentThread() || message < 0x8000 || message > 0xffff) return false;
    if (auto *w = TouchFind(h)) return w->message == message;
    for (auto &w : touch_windows) if (!w.hwnd) { w = {}; w.hwnd = h; w.message = message; return true; }
    return false;
}
bool TouchUnregister(HWND h)
{
    if (WindowProcess(h) != CurrentProcess() || WindowThread(h) != CurrentThread() || !TouchFind(h)) return false;
    TouchDropWindow(h);
    return true;
}
int TouchPeek(HWND h, void *events, unsigned capacity)
{
    auto *w = TouchFind(h);
    if (!w || WindowProcess(h) != CurrentProcess() || WindowThread(h) != CurrentThread()) return -1;
    const unsigned count = w->count < capacity ? w->count : capacity;
    for (unsigned i = 0; i < count; ++i) static_cast<NEO98_TOUCH_EVENT *>(events)[i] = w->events[i];
    return int(count);
}
void TouchConsume(HWND h, unsigned count)
{
    auto *w = TouchFind(h);
    if (!w || count > w->count) return;
    w->count -= count;
    if (!w->count) w->notified = false;
    for (unsigned i = 0; i < w->count; ++i) w->events[i] = w->events[i + count];
}
void TouchCancelWindow(HWND h)
{
    auto *w = TouchFind(h);
    bool active = false;
    for (auto &c : touch_contacts) if (c.used && !c.ignored && c.hwnd == h) { c.ignored = true; active = true; }
    if (w && active) { NEO98_TOUCH_EVENT e = {}; e.phase = NEO98_TOUCH_CANCEL_ALL; TouchQueue(*w, e); }
}
void TouchDropWindow(HWND h)
{
    for (auto &c : touch_contacts) if (c.used && c.hwnd == h) c.ignored = true;
    if (auto *w = TouchFind(h)) *w = {};
}
void TouchForegroundChanged()
{
    const HWND front = TouchRoot(GetForegroundWindow());
    for (auto &w : touch_windows) if (w.hwnd && TouchRoot(w.hwnd) != front) TouchCancelWindow(w.hwnd);
}
void TouchCancelAll(bool forget_contacts)
{
    for (auto &w : touch_windows) if (w.hwnd) TouchCancelWindow(w.hwnd);
    for (auto &c : touch_contacts) if (c.used && c.mouse && !c.ignored) {
        c.ignored = true;
        Mouse(c.x, c.y, false, true);
    }
    // A host focus/device loss may never deliver physical UP. Unknown later
    // MOVE/UP events still cannot begin a contact after these slots are freed.
    if (forget_contacts) for (auto &c : touch_contacts) c = {};
}
void TouchReset()
{
    for (auto &w : touch_windows) w = {};
    for (auto &c : touch_contacts) c = {};
    touch_id = touch_sequence = 0;
}
void TouchPump()
{
    // A full USER message queue can reject a wake-up. Retry after the guest
    // has drained other messages, even if the finger has already gone up.
    for (auto &w : touch_windows)
        if (w.hwnd && w.count && !w.notified) w.notified = PostMessage(w.hwnd, w.message, 0, 0);
}
TouchMouse TouchInput(unsigned long long device, unsigned long long finger, unsigned phase,
                      int x, int y, unsigned time, int pressure)
{
    TouchMouse mouse;
    if (phase < NEO98_TOUCH_DOWN || phase > NEO98_TOUCH_CANCEL) return mouse;
    TouchContact *contact = nullptr;
    for (auto &c : touch_contacts) if (c.used && c.device == device && c.finger == finger) { contact = &c; break; }
    if (InputBlocked()) {
        TouchCancelAll();
        if (contact && (phase == NEO98_TOUCH_UP || phase == NEO98_TOUCH_CANCEL)) *contact = {};
        return mouse;
    }
    if (phase == NEO98_TOUCH_DOWN) {
        if (contact) return mouse; // duplicate down is not a new contact
        bool primary = true;
        for (auto &c : touch_contacts) if (c.used) primary = false;
        for (auto &c : touch_contacts) if (!c.used) { contact = &c; break; }
        if (!contact) { TouchCancelAll(); return mouse; }
        *contact = {};
        contact->used = true; contact->device = device; contact->finger = finger;
        if (!++touch_id) ++touch_id;
        contact->id = touch_id;
        HWND h = HitWindow(Desktop, x, y);
        while (h && !TouchFind(h) && Parent(h) && Parent(h) != Desktop) h = Parent(h);
        Gdi::RECT client = {};
        if (TouchFind(h) && ClientScreenRect(h, &client) && x >= client.left && x < client.right &&
            y >= client.top && y < client.bottom && IsEnabled(h)) {
            contact->hwnd = h;
            NoteInputPress(h, time);
            mouse.activate = TouchRoot(h);
        } else if (TouchFind(h)) contact->ignored = true; // nonclient is not game touch
        else if (!h) contact->ignored = true;
        else contact->mouse = primary;
    }
    if (!contact) return mouse; // an outside/unknown move cannot start a contact
    contact->x = x; contact->y = y;
    if (!contact->ignored && contact->hwnd) {
        if (auto *w = TouchFind(contact->hwnd)) {
            Gdi::POINT point = {x, y}; ScreenToClient(contact->hwnd, &point);
            NEO98_TOUCH_EVENT e = {contact->id, phase, point.x, point.y, time, 0, 0, 0};
            if (pressure >= 0) { e.pressure = unsigned(pressure > 1024 ? 1024 : pressure); e.flags = NEO98_TOUCH_PRESSURE_VALID; }
            TouchQueue(*w, e);
        }
    } else if (!contact->ignored && contact->mouse) {
        mouse.emit = true; mouse.x = x; mouse.y = y;
        mouse.buttons = phase == NEO98_TOUCH_UP || phase == NEO98_TOUCH_CANCEL ? 0 : 1;
    }
    if (phase == NEO98_TOUCH_UP || phase == NEO98_TOUCH_CANCEL) *contact = {};
    return mouse;
}
