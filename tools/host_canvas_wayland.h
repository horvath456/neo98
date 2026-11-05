// SPDX-License-Identifier: GPL-2.0-or-later
// GTK owns the Wayland surface. SDL supplies the existing event contract.
#pragma once
#include <SDL.h>
#include <gtk/gtk.h>
#include <linux/input-event-codes.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <array>
#include <vector>
namespace LinuxGtk
{
inline bool wayland_canvas = false, canvas_pointer = false;
inline std::vector<unsigned> canvas_pixels;
inline int canvas_width = 0, canvas_height = 0;
inline double canvas_scroll = 0;
inline std::array<bool, SDL_NUM_SCANCODES> canvas_keys{};
inline SDL_Scancode PhysicalKey(unsigned hardware)
{
    if (hardware < 8) return SDL_SCANCODE_UNKNOWN;
    switch (hardware - 8)
    {
#define KEY(k,s) case KEY_##k: return SDL_SCANCODE_##s
        KEY(ESC,ESCAPE); KEY(1,1); KEY(2,2); KEY(3,3); KEY(4,4); KEY(5,5);
        KEY(6,6); KEY(7,7); KEY(8,8); KEY(9,9); KEY(0,0); KEY(MINUS,MINUS);
        KEY(EQUAL,EQUALS); KEY(BACKSPACE,BACKSPACE); KEY(TAB,TAB);
        KEY(Q,Q); KEY(W,W); KEY(E,E); KEY(R,R); KEY(T,T); KEY(Y,Y); KEY(U,U);
        KEY(I,I); KEY(O,O); KEY(P,P); KEY(LEFTBRACE,LEFTBRACKET); KEY(RIGHTBRACE,RIGHTBRACKET);
        KEY(ENTER,RETURN); KEY(LEFTCTRL,LCTRL); KEY(A,A); KEY(S,S); KEY(D,D); KEY(F,F);
        KEY(G,G); KEY(H,H); KEY(J,J); KEY(K,K); KEY(L,L); KEY(SEMICOLON,SEMICOLON);
        KEY(APOSTROPHE,APOSTROPHE); KEY(GRAVE,GRAVE); KEY(LEFTSHIFT,LSHIFT); KEY(BACKSLASH,BACKSLASH);
        KEY(Z,Z); KEY(X,X); KEY(C,C); KEY(V,V); KEY(B,B); KEY(N,N); KEY(M,M);
        KEY(COMMA,COMMA); KEY(DOT,PERIOD); KEY(SLASH,SLASH); KEY(RIGHTSHIFT,RSHIFT);
        KEY(KPASTERISK,KP_MULTIPLY); KEY(LEFTALT,LALT); KEY(SPACE,SPACE); KEY(CAPSLOCK,CAPSLOCK);
        KEY(F1,F1); KEY(F2,F2); KEY(F3,F3); KEY(F4,F4); KEY(F5,F5); KEY(F6,F6);
        KEY(F7,F7); KEY(F8,F8); KEY(F9,F9); KEY(F10,F10); KEY(F11,F11); KEY(F12,F12);
        KEY(NUMLOCK,NUMLOCKCLEAR);
        KEY(SCROLLLOCK,SCROLLLOCK); KEY(KP7,KP_7); KEY(KP8,KP_8); KEY(KP9,KP_9);
        KEY(KPMINUS,KP_MINUS); KEY(KP4,KP_4); KEY(KP5,KP_5); KEY(KP6,KP_6);
        KEY(KPPLUS,KP_PLUS); KEY(KP1,KP_1); KEY(KP2,KP_2); KEY(KP3,KP_3);
        KEY(KP0,KP_0); KEY(KPDOT,KP_PERIOD); KEY(102ND,NONUSBACKSLASH); KEY(KPENTER,KP_ENTER);
        KEY(RIGHTCTRL,RCTRL); KEY(KPSLASH,KP_DIVIDE); KEY(SYSRQ,PRINTSCREEN); KEY(RIGHTALT,RALT);
        KEY(HOME,HOME); KEY(UP,UP); KEY(PAGEUP,PAGEUP); KEY(LEFT,LEFT); KEY(RIGHT,RIGHT);
        KEY(END,END); KEY(DOWN,DOWN); KEY(PAGEDOWN,PAGEDOWN); KEY(INSERT,INSERT); KEY(DELETE,DELETE);
        KEY(LEFTMETA,LGUI); KEY(RIGHTMETA,RGUI); KEY(COMPOSE,APPLICATION); KEY(PAUSE,PAUSE);
#undef KEY
    default: return SDL_SCANCODE_UNKNOWN;
    }
}
inline Uint16 CanvasModifiers(GdkModifierType state)
{
    Uint16 mods = 0;
    const std::pair<SDL_Scancode, Uint16> modifiers[] = {
        {SDL_SCANCODE_LCTRL,KMOD_LCTRL},{SDL_SCANCODE_RCTRL,KMOD_RCTRL},
        {SDL_SCANCODE_LSHIFT,KMOD_LSHIFT},{SDL_SCANCODE_RSHIFT,KMOD_RSHIFT},
        {SDL_SCANCODE_LALT,KMOD_LALT},{SDL_SCANCODE_RALT,KMOD_RALT},
        {SDL_SCANCODE_LGUI,KMOD_LGUI},{SDL_SCANCODE_RGUI,KMOD_RGUI}};
    for (auto key : modifiers) if (canvas_keys[key.first]) mods |= key.second;
    if (state & GDK_LOCK_MASK) mods |= KMOD_CAPS;
    if (state & GDK_MOD2_MASK) mods |= KMOD_NUM;
    return mods;
}
inline void CanvasKey(const GdkEventKey &key, unsigned window_id = 0)
{
    const auto scan = PhysicalKey(key.hardware_keycode);
    if (scan == SDL_SCANCODE_UNKNOWN) return;
    SDL_Event event = {};
    event.type = key.type == GDK_KEY_PRESS ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.windowID = window_id ? window_id : SDL_GetWindowID(desktop);
    event.key.repeat = event.type == SDL_KEYDOWN && canvas_keys[scan];
    canvas_keys[scan] = event.type == SDL_KEYDOWN;
    event.key.state = canvas_keys[scan] ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.scancode = scan;
    event.key.keysym.sym = SDL_GetKeyFromScancode(scan);
    event.key.keysym.mod = CanvasModifiers(GdkModifierType(key.state));
    SDL_PushEvent(&event);
}
inline void CanvasPoint(double x,double y,int &out_x,int &out_y)
{
    const int w = gtk_widget_get_allocated_width(canvas), h = gtk_widget_get_allocated_height(canvas);
    const double scale = canvas_width && canvas_height ? std::min(double(w)/canvas_width,double(h)/canvas_height) : 1;
    out_x = int((x-(w-canvas_width*scale)/2)/scale);
    out_y = int((y-(h-canvas_height*scale)/2)/scale);
}
inline void CanvasFocus(bool focused)
{
    SDL_Event event = {}; event.type = SDL_WINDOWEVENT;
    event.window.windowID = SDL_GetWindowID(desktop);
    event.window.event = focused ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST;
    if (!focused) canvas_keys.fill(false);
    SDL_PushEvent(&event);
}
inline void InstallWaylandCanvas()
{
    wayland_canvas = true; embedded = true;
    gtk_widget_set_can_focus(canvas,TRUE);
    gtk_widget_add_events(canvas,GDK_POINTER_MOTION_MASK|GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|
        GDK_SCROLL_MASK|GDK_SMOOTH_SCROLL_MASK|GDK_ENTER_NOTIFY_MASK|GDK_LEAVE_NOTIFY_MASK|GDK_FOCUS_CHANGE_MASK);
    g_signal_connect(shell,"key-press-event",G_CALLBACK(+[](GtkWidget *,GdkEventKey *key,gpointer)->gboolean {
        if (!gtk_widget_has_focus(canvas)) return FALSE;
        CanvasKey(*key); return TRUE;
    }),nullptr);
    g_signal_connect(shell,"key-release-event",G_CALLBACK(+[](GtkWidget *,GdkEventKey *key,gpointer)->gboolean {
        if (!gtk_widget_has_focus(canvas)) return FALSE;
        CanvasKey(*key); return TRUE;
    }),nullptr);
    g_signal_connect(canvas,"motion-notify-event",G_CALLBACK(+[](GtkWidget *,GdkEventMotion *motion,gpointer)->gboolean {
        SDL_Event event = {}; event.type = SDL_MOUSEMOTION; event.motion.windowID = SDL_GetWindowID(desktop);
        CanvasPoint(motion->x,motion->y,event.motion.x,event.motion.y); SDL_PushEvent(&event); return TRUE;
    }),nullptr);
    auto button = +[](GtkWidget *,GdkEventButton *button,gpointer)->gboolean {
        if (button->type != GDK_BUTTON_PRESS && button->type != GDK_BUTTON_RELEASE) return TRUE;
        if (button->button > 3) return FALSE;
        gtk_widget_grab_focus(canvas);
        SDL_Event event = {}; event.type = button->type == GDK_BUTTON_PRESS ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        event.button.windowID = SDL_GetWindowID(desktop);
        event.button.button = button->button == 2 ? SDL_BUTTON_MIDDLE : button->button == 3 ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
        event.button.state = event.type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
        CanvasPoint(button->x,button->y,event.button.x,event.button.y); SDL_PushEvent(&event); return TRUE;
    };
    g_signal_connect(canvas,"button-press-event",G_CALLBACK(button),nullptr);
    g_signal_connect(canvas,"button-release-event",G_CALLBACK(button),nullptr);
    g_signal_connect(canvas,"scroll-event",G_CALLBACK(+[](GtkWidget *,GdkEventScroll *wheel,gpointer)->gboolean {
        SDL_Event event = {}; event.type = SDL_MOUSEWHEEL; event.wheel.windowID = SDL_GetWindowID(desktop);
        if (wheel->direction == GDK_SCROLL_SMOOTH)
        { canvas_scroll -= wheel->delta_y; event.wheel.y = int(canvas_scroll); canvas_scroll -= event.wheel.y; }
        else event.wheel.y = wheel->direction == GDK_SCROLL_UP ? 1 : wheel->direction == GDK_SCROLL_DOWN ? -1 : 0;
        event.wheel.preciseY = wheel->direction == GDK_SCROLL_SMOOTH ? float(-wheel->delta_y) : float(event.wheel.y);
        SDL_PushEvent(&event); return TRUE;
    }),nullptr);
    g_signal_connect(canvas,"enter-notify-event",G_CALLBACK(+[](GtkWidget *,GdkEvent *,gpointer)->gboolean {
        canvas_pointer = true; return FALSE;
    }),nullptr);
    g_signal_connect(canvas,"leave-notify-event",G_CALLBACK(+[](GtkWidget *,GdkEvent *,gpointer)->gboolean {
        canvas_pointer = false; return FALSE;
    }),nullptr);
    g_signal_connect(shell,"focus-in-event",G_CALLBACK(+[](GtkWidget *,GdkEvent *,gpointer)->gboolean { CanvasFocus(true); return FALSE; }),nullptr);
    g_signal_connect(shell,"focus-out-event",G_CALLBACK(+[](GtkWidget *,GdkEvent *,gpointer)->gboolean { CanvasFocus(false); return FALSE; }),nullptr);
    g_signal_connect(canvas,"draw",G_CALLBACK(+[](GtkWidget *widget,cairo_t *cr,gpointer)->gboolean {
        cairo_set_source_rgb(cr,0,0,0); cairo_paint(cr);
        if (!canvas_pixels.empty())
        {
            const int w = gtk_widget_get_allocated_width(widget);
            const int h = gtk_widget_get_allocated_height(widget);
            const double scale = std::min(double(w)/canvas_width,double(h)/canvas_height);
            cairo_surface_t *image = cairo_image_surface_create_for_data(reinterpret_cast<unsigned char *>(canvas_pixels.data()),
                CAIRO_FORMAT_RGB24,canvas_width,canvas_height,canvas_width*4);
            cairo_translate(cr,(w-canvas_width*scale)/2,(h-canvas_height*scale)/2); cairo_scale(cr,scale,scale);
            cairo_set_source_surface(cr,image,0,0); cairo_pattern_set_filter(cairo_get_source(cr),CAIRO_FILTER_NEAREST);
            cairo_paint(cr); cairo_surface_destroy(image);
        }
        return TRUE;
    }),nullptr);
    gtk_widget_grab_focus(canvas);
    printf("NEO98 display=wayland canvas=gtk\n"); fflush(stdout);
}
inline void CanvasCursor(bool visible)
{
    if (!wayland_canvas || !gtk_widget_get_window(canvas)) return;
    static int showing = -1;
    if (showing == int(visible)) return;
    showing = visible;
    GdkCursor *cursor = visible ? nullptr : gdk_cursor_new_for_display(gdk_display_get_default(),GDK_BLANK_CURSOR);
    gdk_window_set_cursor(gtk_widget_get_window(canvas),cursor);
    if (cursor) g_object_unref(cursor);
}
inline void Present(SDL_Renderer *renderer,const unsigned char *pixels,int w,int h)
{
    if (!wayland_canvas) { SDL_RenderPresent(renderer); return; }
    if (!pixels || w <= 0 || h <= 0) return;
    canvas_width = w; canvas_height = h; canvas_pixels.resize(size_t(w)*h);
    memcpy(canvas_pixels.data(),pixels,canvas_pixels.size()*4);
    gtk_widget_queue_draw(canvas);
}
}
