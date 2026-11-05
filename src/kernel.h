// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/bcmframebuffer.h>
#include <circle/serial.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/input/mouse.h>
#include "raster.h"
#include "winmine.h"
#include "gdi.h"
#include "text_edit.h"
#include "x86_app.h"
#include "usb_keys.h"
#include <circle/input/keymap.h>

class Kernel {
public:
    Kernel();
    bool Initialize();
    void Run();

private:
    void Paint();
    void Status();
    void Present(Rect rect);
    void Text(int x, int y, const char *text, unsigned color = 0xff000000);
    void Button(Rect rect, const char *label);
    void Command(char key);
    void Log(const char *text);
    void PaintMines();
    void MineInput(unsigned buttons, int dx, int dy, int wheel);
    void MineCommand(char key);
    void MineLog(const char *action);
    void DrawCursor();
    void Sprite(const unsigned *bitmap, int width, int source_y, int height, int x, int y, int scale);
    void MineLayout();
    bool InitializeGdi();
    void PaintGdi();
    void GdiInput(unsigned down, unsigned up, int wheel);
    void GdiCommand(char key);
    void GdiDamage(Rect);
    void GdiWindowContent(int index);
    void EditorContent();
    void EditorMetrics();
    void EditorCommand(const char *, unsigned modifiers);
    void EditorInput(unsigned down, unsigned up, int wheel);
    void EditorChanged(const char *action);
    bool InitializeApplication();
    void PumpApplication();
    void PresentApplicationFrame();
    // USB plug and play, queued keyboard and mouse reports, key repeat, the
    // serial console and the per-second tick. Both the startup loop and Run
    // call this, so input reaches an application before its first idle.
    void ServiceInput();
    void PublishApplicationState(const char *state, const char *reason, unsigned rounds);
    unsigned VisibleTopLevelWindow();
    void ApplicationKey(const char *, unsigned modifiers);
    void ApplicationRawKeys(unsigned modifiers, const unsigned char keys[6]);
    void ApplicationRawKey(unsigned usage, bool down);
    void ApplicationKeyRepeat();
    void ApplicationMouse(unsigned buttons, int dx, int dy, int wheel);
    static void KeyPressed(const char *text);
    static void KeyStatus(unsigned char modifiers, const unsigned char keys[6]);
    static void KeyboardRemoved(CDevice *, void *);
    static void MouseStatus(unsigned buttons, int dx, int dy, int wheel);
    static void MouseRemoved(CDevice *, void *);

    // Circle construction order matters: services precede their consumers.
    CActLED led;
    CKernelOptions options;
    CDeviceNameService devices;
    CBcmFrameBuffer framebuffer;
    CSerialDevice serial;
    CExceptionHandler exceptions;
    CInterruptSystem interrupts;
    CTimer timer;
    CLogger logger;
    CUSBHCIDevice usb;
    CUSBKeyboardDevice *keyboard = nullptr;
    CMouseDevice *mouse = nullptr;
    Surface surface = {};
    Surface display = {};
    unsigned counter = 0;
    unsigned palette = 0;
    unsigned exception_level = 0;
    bool running = true;
    bool full_dirty = true;
    bool status_dirty = false;
    bool taskbar_dirty = false;
    // Preserve each cooked key sequence and its modifiers across the IRQ boundary.
    struct KeyReport { char text[8]; unsigned modifiers, sequence; bool raw; unsigned char keys[6]; };
    KeyReport key_reports[64] = {};
    volatile unsigned key_read = 0, key_write = 0, key_modifiers = 0;
    unsigned char previous_raw_keys[6] = {};
    unsigned char application_raw_keys[6] = {};
    UsbKeys::Key application_held_keys[128] = {};
    CKeyMap *application_keymap = nullptr;
    unsigned application_modifiers = 0, repeat_usage = 0, repeat_time = 0;
    unsigned application_leds = ~0u;
    struct MouseReport { unsigned buttons; int dx, dy, wheel; unsigned sequence; };
    volatile unsigned input_sequence = 0;
    MouseReport mouse_reports[32] = {};
    volatile unsigned mouse_read = 0, mouse_write = 0;
    int mouse_x = 400, mouse_y = 300;
    unsigned mouse_buttons = 0;
    X86::Application *application = nullptr;
    bool application_mode = false, application_cursor_visible = true;
    bool application_running_published = false;
    unsigned application_frame = 0, application_state_hash = 0;
    unsigned input_seconds = 0;
    unsigned application_selection = 0, application_focus = 0;
    bool mines_view = false, mine_dirty = true, mine_full = true;
    bool gdi_view = true, gdi_dirty = true;
    bool editor_view = true, editor_selecting = false;
    Gdi::HDC screen_dc = 0;
    struct GdiWindow { int x, y, width, height; Gdi::HDC dc; Gdi::HBITMAP bitmap; };
    GdiWindow gdi_windows[3] = {};
    int gdi_order[2] = {0, 1}, gdi_drag = -1, gdi_drag_x = 0, gdi_drag_y = 0;
    unsigned gdi_operation = 0, gdi_scroll = 0;
    Gdi::HFONT editor_fonts[6] = {};
    unsigned editor_font = 2;
    TextEdit editor;
    char editor_clipboard[TextEdit::Capacity] = {};
    unsigned editor_clipboard_length = 0;
    Rect gdi_damage = {0, 0, 800, 600};
    bool mine_capture = false, face_pressed = false, menu_open = false;
    Winmine::Game game;
    unsigned char painted_cells[864] = {};
    int painted_seconds = -1, painted_remaining = -1000, painted_face = -1;
    int mine_scale = 2, mine_left = 0, mine_top = 0, mine_width = 0, mine_height = 0;
    static Kernel *instance;
};
