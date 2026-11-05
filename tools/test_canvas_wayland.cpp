// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_menu_linux.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <thread>
#include <chrono>
int main(int argc,char **argv)
{
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)==0);
    SDL_Window *window=SDL_CreateWindow("NEO98 hidden event target",0,0,800,600,SDL_WINDOW_HIDDEN);
    assert(window && LinuxGtk::Create(window));
    assert(LinuxGtk::wayland_canvas && GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default()));
    std::vector<unsigned> picture(800*600,0xfff4e0c0);
    for(int y=70;y<210;++y) for(int x=60;x<280;++x) picture[y*800+x]=0xff003366;
    LinuxGtk::Present(nullptr,reinterpret_cast<unsigned char *>(picture.data()),800,600);
    for(int i=0;i<40;++i) { LinuxGtk::Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    assert(LinuxGtk::canvas_width==800 && LinuxGtk::canvas_height==600);
    const int w=gtk_widget_get_allocated_width(LinuxGtk::canvas), h=gtk_widget_get_allocated_height(LinuxGtk::canvas);
    assert(w>0 && h>0);
    cairo_surface_t *image=cairo_image_surface_create(CAIRO_FORMAT_RGB24,w,h);
    cairo_t *cr=cairo_create(image); gtk_widget_draw(LinuxGtk::canvas,cr); cairo_destroy(cr);
    assert(cairo_surface_write_to_png(image,argv[1])==CAIRO_STATUS_SUCCESS);
    cairo_surface_destroy(image);
    gtk_widget_grab_focus(LinuxGtk::canvas);
    assert(gtk_widget_has_focus(LinuxGtk::canvas));
    SDL_Event event; while(SDL_PollEvent(&event)) {}
    auto key=[&](unsigned hardware,bool down,SDL_Scancode scan,unsigned mods) {
        GdkEventKey key={}; key.type=down?GDK_KEY_PRESS:GDK_KEY_RELEASE; key.hardware_keycode=hardware;
        key.window=gtk_widget_get_window(LinuxGtk::canvas);
        gboolean handled=FALSE;
        g_signal_emit_by_name(LinuxGtk::shell,down?"key-press-event":"key-release-event",&key,&handled);
        fprintf(stderr,"key hardware=%u scan=%u handled=%u\n",hardware,scan,handled);
        assert(handled && SDL_PeepEvents(&event,1,SDL_GETEVENT,SDL_FIRSTEVENT,SDL_LASTEVENT)==1);
        assert(event.type==(down?SDL_KEYDOWN:SDL_KEYUP) && event.key.keysym.scancode==scan && event.key.keysym.mod==mods);
    };
    key(KEY_RIGHTCTRL+8,true,SDL_SCANCODE_RCTRL,KMOD_RCTRL);
    key(KEY_A+8,true,SDL_SCANCODE_A,KMOD_RCTRL);
    key(KEY_A+8,false,SDL_SCANCODE_A,KMOD_RCTRL);
    key(KEY_RIGHTCTRL+8,false,SDL_SCANCODE_RCTRL,0);
    // Physical Y must reach the same USB key on a German host keyboard.
    key(KEY_Y+8,true,SDL_SCANCODE_Y,0); key(KEY_Y+8,false,SDL_SCANCODE_Y,0);
    const double scale=std::min(double(w)/800,double(h)/600);
    GdkEventButton button={}; button.type=GDK_BUTTON_PRESS; button.button=1;
    button.window=gtk_widget_get_window(LinuxGtk::canvas);
    button.x=(w-800*scale)/2+125*scale; button.y=(h-600*scale)/2+98*scale;
    gboolean handled=FALSE;
    g_signal_emit_by_name(LinuxGtk::canvas,"button-press-event",&button,&handled);
    assert(handled && SDL_PeepEvents(&event,1,SDL_GETEVENT,SDL_FIRSTEVENT,SDL_LASTEVENT)==1);
    assert(event.type==SDL_MOUSEBUTTONDOWN && event.button.x==125 && event.button.y==98);
    LinuxGtk::Fullscreen(true); LinuxGtk::Pump(); LinuxGtk::Fullscreen(false); LinuxGtk::Pump();
    LinuxGtk::Close(); SDL_DestroyWindow(window); SDL_Quit();
    puts("WAYLAND CANVAS PASS: native GTK surface, rendered image, physical keys, modifiers, scaled mouse, fullscreen");
}
