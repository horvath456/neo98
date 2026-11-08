// SPDX-License-Identifier: GPL-2.0-or-later
// Native Linux controls. The menu queues requests; it never enters guest code.
#pragma once
#include <algorithm>
#include <clocale>
#include <tuple>
#include "host_menu.h"
#include "../src/neo98_oem_logo.h"
#include <SDL.h>
#include <SDL_syswm.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <gdk/gdkx.h>
#include <gdk/gdkwayland.h>
#include <gtk/gtk.h>
#include <map>
#include <functional>
#include <string>
#include <vector>
#undef Status
#undef None
#undef Below
#undef Above

namespace LinuxGtk
{
inline GtkWidget *shell = nullptr, *canvas = nullptr, *status = nullptr, *window_menu = nullptr;
inline SDL_Window *desktop = nullptr;
inline bool ready = false, updating = false, seamless = false, embedded = false;
inline bool menu_updated = false;
inline bool redraw = false;
inline bool persistent_settings = true;
inline std::map<std::string, GtkWidget *> checks;
inline HostMenu::State current;
}
#include "host_canvas_wayland.h"
namespace LinuxGtk
{

inline bool Init()
{
    if (ready)
        return true;
    g_set_prgname("neo98");
    g_set_application_name("Neo98");
    // The embedded SDL surface and GTK must use the same window system.
    if (SDL_GetCurrentVideoDriver() && !strcmp(SDL_GetCurrentVideoDriver(), "x11"))
    {
        setenv("GDK_BACKEND", "x11", 1);
        gdk_set_allowed_backends("x11");
    }
    else if (SDL_GetCurrentVideoDriver() && !strcmp(SDL_GetCurrentVideoDriver(), "wayland"))
    {
        setenv("GDK_BACKEND", "wayland", 1);
        gdk_set_allowed_backends("wayland");
    }
    ready = gtk_init_check(nullptr, nullptr);
    // GTK takes the locale of the environment. The runtime formats and
    // reads numbers with the C library as Win98 does, with a point.
    setlocale(LC_NUMERIC, "C");
    return ready;
}
inline void Pump()
{
    if (ready)
    {
        while (g_main_context_iteration(nullptr, FALSE))
        {
        }
        gdk_display_flush(gdk_display_get_default());
    }
}
inline void Notice(const char *title, const char *message)
{
    printf("NEO98 HOST notice %s: %s\n", title, message);
    fflush(stdout);
    if (!Init())
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title, message, desktop);
        return;
    }
    GtkWidget *dialog =
        gtk_message_dialog_new(shell ? GTK_WINDOW(shell) : nullptr, GTK_DIALOG_DESTROY_WITH_PARENT,
                               GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "%s", message);
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog *dialog, gint, gpointer) {
                         gtk_widget_destroy(GTK_WIDGET(dialog));
                     }),
                     nullptr);
    gtk_widget_show(dialog);
}
inline void Choose(const std::string &action)
{
    const char *title = action == "drive-d"                             ? "Mount a folder as drive D:"
                        : action == "cdrom" || action == "cdrom-folder" ? "Mount CD-ROM"
                                                                        : "Open program";
    GtkFileChooserNative *dialog = gtk_file_chooser_native_new(title, shell ? GTK_WINDOW(shell) : nullptr,
                                                               action == "drive-d" || action == "cdrom-folder"
                                                                   ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
                                                                   : GTK_FILE_CHOOSER_ACTION_OPEN,
                                                               "Open", "Cancel");
    auto *name = new std::string(action == "cdrom-folder" ? "cdrom" : action);
    g_signal_connect_data(
        dialog, "response", G_CALLBACK(+[](GtkNativeDialog *dialog, gint response, gpointer data) {
            if (response == GTK_RESPONSE_ACCEPT)
            {
                char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
                if (path)
                {
                    HostMenu::Perform(static_cast<std::string *>(data)->c_str(), path);
                    g_free(path);
                }
            }
            gtk_native_dialog_destroy(dialog);
            g_object_unref(dialog);
        }),
        name, +[](gpointer data, GClosure *) { delete static_cast<std::string *>(data); }, GConnectFlags(0));
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}
inline void ComDialog(unsigned port)
{
    GtkWidget *dialog =
        gtk_dialog_new_with_buttons(("COM" + std::to_string(port)).c_str(),
                                    shell ? GTK_WINDOW(shell) : nullptr, GTK_DIALOG_DESTROY_WITH_PARENT,
                                    "Cancel", GTK_RESPONSE_CANCEL, "Apply", GTK_RESPONSE_OK, nullptr);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(box), 16);
    GtkWidget *label = gtk_label_new("Device or TCP endpoint, for example /dev/ttyUSB0 or "
                                     "tcp:127.0.0.1:7898.\nLeave empty to disconnect.");
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), current.com[port - 1].c_str());
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 45);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 6);
    g_object_set_data(G_OBJECT(dialog), "entry", entry);
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog *dialog, gint response, gpointer data) {
                         if (response == GTK_RESPONSE_OK)
                         {
                             auto *entry = GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), "entry"));
                             HostMenu::Perform(("com" + std::to_string(GPOINTER_TO_UINT(data))).c_str(),
                                               gtk_entry_get_text(entry));
                         }
                         gtk_widget_destroy(GTK_WIDGET(dialog));
                     }),
                     GUINT_TO_POINTER(port));
    gtk_widget_show_all(dialog);
}
inline void Preferences()
{
    GtkWidget *dialog =
        gtk_dialog_new_with_buttons("Neo98 settings", shell ? GTK_WINDOW(shell) : nullptr,
                                    GTK_DIALOG_DESTROY_WITH_PARENT, "Close", GTK_RESPONSE_CLOSE, nullptr);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(box), 20);
    auto toggle = [&](const char *label, const char *action, bool value) {
        GtkWidget *button = gtk_check_button_new_with_label(label);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), value);
        gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 4);
        g_signal_connect_data(
            button, "toggled", G_CALLBACK(+[](GtkToggleButton *, gpointer name) {
                HostMenu::Perform(static_cast<const char *>(name), nullptr);
            }),
            g_strdup(action), +[](gpointer name, GClosure *) { g_free(name); }, GConnectFlags(0));
    };
    toggle("Show guest windows on the Linux desktop", "seamless", current.seamless);
    toggle("Show Windows 98 taskbar", "seamless-taskbar", current.seamless_taskbar);
    toggle("One host window per guest window", "seamless-windows", current.seamless_windows);
    toggle("Show Windows 98 desktop icons", "seamless-icons", current.seamless_icons);
    toggle("Show programs in Dock", "seamless-dock", current.seamless_dock);
    toggle("Pause when focus is lost", "pause-unfocused", current.pause_unfocused);
    toggle("Mouse wheel scrolls the focused guest window", "wheel-focus", current.wheel_focus);
    toggle("Network connected", "network", current.online);
    toggle("Allow LAN connections for new game ports", "network-listen", current.net_listen.compare(0, 4, "127.") != 0);
    toggle("Wi-Fi in Windows", "network-wifi", current.wifi);
    toggle("Microphone in Windows", "microphone", current.microphone);
    GtkWidget *label = gtk_label_new(
        persistent_settings ? "Changes apply immediately and are saved for this machine."
                            : "Changes apply immediately. This RAM session does not save settings.");
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 12);
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog *dialog, gint, gpointer) {
                         gtk_widget_destroy(GTK_WIDGET(dialog));
                     }),
                     nullptr);
    gtk_widget_show_all(dialog);
}
inline void AskOpen(const std::string &path, std::function<void(int, bool)> answer)
{
    GtkWidget *dialog = gtk_message_dialog_new(shell ? GTK_WINDOW(shell) : nullptr,
        GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
        "How do you want to open %s?", path.c_str());
    gtk_window_set_title(GTK_WINDOW(dialog), "Open program");
    gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Cancel", GTK_RESPONSE_CANCEL,
        "Open normally", 1, "Open as game machine", 2, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), 1);
    GtkWidget *never = gtk_check_button_new_with_label("Do not show this dialog again");
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), never, FALSE, FALSE, 8);
    struct Answer { GtkWidget *never; std::function<void(int,bool)> callback; };
    auto *context = new Answer{never, std::move(answer)};
    g_signal_connect_data(dialog, "response", G_CALLBACK(+[](GtkDialog *dialog, gint response, gpointer data) {
        auto &answer = *static_cast<Answer *>(data);
        answer.callback(response, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(answer.never)));
        gtk_widget_destroy(GTK_WIDGET(dialog));
    }), context, +[](gpointer data, GClosure *) { delete static_cast<Answer *>(data); }, GConnectFlags(0));
    gtk_widget_show_all(dialog);
}
inline void ChooseGame(const std::vector<std::pair<std::string,std::string>> &games)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Choose a game machine", shell ? GTK_WINDOW(shell) : nullptr,
        GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel", GTK_RESPONSE_CANCEL, nullptr);
    for (const auto &game : games)
    {
        GtkWidget *button = gtk_button_new_with_label(game.first.c_str());
        gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), button, FALSE, FALSE, 4);
        struct Choice { GtkWidget *dialog; std::string folder; };
        auto *choice = new Choice{dialog, game.second};
        g_signal_connect_data(button, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
            auto &choice = *static_cast<Choice *>(data); GtkWidget *dialog = choice.dialog;
            HostMenu::Perform("game-launch", choice.folder.c_str()); gtk_widget_destroy(dialog);
        }), choice, +[](gpointer data, GClosure *) { delete static_cast<Choice *>(data); }, GConnectFlags(0));
    }
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog *dialog, gint, gpointer) { gtk_widget_destroy(GTK_WIDGET(dialog)); }), nullptr);
    gtk_widget_show_all(dialog);
}
inline void Activate(const std::string &action)
{
    if (updating)
        return;
    if (action == "open" || action == "game-create" || action == "drive-d" || action == "cdrom" ||
        action == "cdrom-folder")
        Choose(action);
    else if (action == "preferences")
        Preferences();
    else if (action == "dos-settings")
        HostMenu::Perform("dos-settings", current.dos_program.c_str());
    else if (action.size() == 4 && action.compare(0, 3, "com") == 0)
        ComDialog(unsigned(action[3] - '0'));
    else if (action == "about")
        Notice("About Neo98",
               "Windows 98 programs on your Linux desktop.\n\nCtrl+Shift+M opens the "
               "machine menu.\nCtrl+Shift+S toggles seamless mode.\nF11 toggles fullscreen.");
    else
        HostMenu::Perform(action.c_str(), nullptr);
}
inline GtkWidget *Item(GtkWidget *menu, const char *label, const std::string &action, bool check = false)
{
    GtkWidget *item =
        check ? gtk_check_menu_item_new_with_mnemonic(label) : gtk_menu_item_new_with_mnemonic(label);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    auto *name = new std::string(action);
    g_signal_connect_data(
        item, "activate",
        G_CALLBACK(+[](GtkMenuItem *, gpointer data) { Activate(*static_cast<std::string *>(data)); }), name,
        +[](gpointer data, GClosure *) { delete static_cast<std::string *>(data); }, GConnectFlags(0));
    g_object_set_data_full(G_OBJECT(item), "neo98-action", g_strdup(action.c_str()), g_free);
    if (check)
        checks[action] = item;
    if (action == "state-save" || action == "state-load" || action == "states" || action == "dos-settings") checks[action] = item;
    return item;
}
inline void Separator(GtkWidget *menu)
{
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
}
inline GtkWidget *Submenu(GtkWidget *bar, const char *label)
{
    GtkWidget *item = gtk_menu_item_new_with_mnemonic(label), *menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), item);
    return menu;
}
// The keys of the host for an item, as the line of menu=describe names them.
inline const char *ItemKeys(const std::string &action)
{
    return action == "pause" ? "shift+ctrl+p" : action == "seamless" ? "shift+ctrl+s" : action == "fullscreen" ? "f11" : "";
}
// menu=describe: every item of the menu bar, a line each, as the macOS host
// writes it: "Menu > Item [action] keys (disabled) (enabled)", the last for
// a check mark.
inline void DescribeItems(GtkWidget *menu, const std::string &path)
{
    GList *children = gtk_container_get_children(GTK_CONTAINER(menu));
    for (GList *at = children; at; at = at->next)
    {
        GtkWidget *item = GTK_WIDGET(at->data);
        if (!GTK_IS_MENU_ITEM(item) || GTK_IS_SEPARATOR_MENU_ITEM(item) || !gtk_widget_get_visible(item)) continue;
        std::string label = gtk_menu_item_get_label(GTK_MENU_ITEM(item)) ? gtk_menu_item_get_label(GTK_MENU_ITEM(item)) : "";
        label.erase(std::remove(label.begin(), label.end(), '_'), label.end());
        const std::string title = path.empty() ? label : path + " > " + label;
        if (GtkWidget *submenu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(item)))
        {
            DescribeItems(submenu, title);
            continue;
        }
        std::string line = title;
        const char *action = static_cast<const char *>(g_object_get_data(G_OBJECT(item), "neo98-action"));
        if (action)
        {
            line += std::string(" [") + action + "]";
            if (*ItemKeys(action)) line += std::string(" ") + ItemKeys(action);
        }
        if (!gtk_widget_get_sensitive(item)) line += " (disabled)";
        if (GTK_IS_CHECK_MENU_ITEM(item) && gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item))) line += " (enabled)";
        printf("NEO98 HOST menu-item %s\n", line.c_str());
    }
    g_list_free(children);
    fflush(stdout);
}
inline GtkWidget *menu_bar = nullptr;
inline GtkWidget *MenuBar()
{
    menu_updated = false;
    GtkWidget *bar = gtk_menu_bar_new(), *menu = Submenu(bar, "_File");
    menu_bar = bar;
    Item(menu, "_Open program…", "open");
    Item(menu, "Open as _game machine…", "game-create");
    Item(menu, "Game machines…", "game-list");
    Item(menu, "Ask about game machines when opening", "open-ask", true);
    Item(menu, "_DOS program settings…", "dos-settings");
    Separator(menu);
    Item(menu, "_Quit", "quit");
    menu = Submenu(bar, "_Edit");
    Item(menu, "_Cut", "cut");
    Item(menu, "_Copy", "copy");
    Item(menu, "_Paste", "paste");
    menu = Submenu(bar, "_Machine");
    Item(menu, "_Pause", "pause", true);
    Item(menu, "Pause when _focus is lost", "pause-unfocused", true);
    Item(menu, "_Settings…", "preferences");
    Item(menu, "_Restart", "reboot");
    Item(menu, "Send Ctrl+Alt+Delete", "ctrl-alt-delete");
    Item(menu, "_Release mouse", "mouse-release");
    Item(menu, "Mouse wheel follows _focus", "wheel-focus", true);
    Item(menu, "_Microphone in Windows", "microphone", true);
    Separator(menu);
    Item(menu, "_Save state", "state-save");
    Item(menu, "_Load latest state", "state-load");
    Item(menu, "_States…", "states");
    Separator(menu);
    Item(menu, "_Write crash report", "report");
    menu = Submenu(bar, "_View");
    Item(menu, "_Seamless", "seamless", true);
    Item(menu, "Show _taskbar", "seamless-taskbar", true);
    Item(menu, "One _host window per guest window", "seamless-windows", true);
    Item(menu, "Show _desktop icons", "seamless-icons", true);
    Item(menu, "Show programs in _Dock", "seamless-dock", true);
    Separator(menu);
    Item(menu, "_Fullscreen", "fullscreen", true);
    Item(menu, "Scale _1×", "scale1", true);
    Item(menu, "Scale _2×", "scale2", true);
    menu = Submenu(bar, "_Drives");
    Item(menu, "Folder as drive _D:…", "drive-d");
    Item(menu, "Mount _CD-ROM image…", "cdrom");
    Item(menu, "_Folder as CD-ROM…", "cdrom-folder");
    Item(menu, "_Eject CD-ROM", "eject");
    GtkWidget *serial = Submenu(menu, "_Serial ports");
    for (unsigned port = 1; port <= 4; ++port)
        Item(serial, ("COM" + std::to_string(port) + "…").c_str(), "com" + std::to_string(port));
    menu = Submenu(bar, "_Network");
    Item(menu, "_Connected", "network", true);
    Item(menu, "Allow _LAN connections", "network-listen", true);
    gtk_widget_set_tooltip_text(checks["network-listen"], "Applies to newly opened game ports. Restart running game servers.");
    Item(menu, "_Wi-Fi in Windows", "network-wifi", true);
    window_menu = Submenu(bar, "_Window");
    Item(window_menu, "_Minimize", "minimize");
    Item(window_menu, "_Start menu", "start-menu");
    menu = Submenu(bar, "_Help");
    Item(menu, "_About Neo98", "about");
    return bar;
}
inline void Check(const char *name, bool value)
{
    auto found = checks.find(name);
    if (found != checks.end())
    {
        const bool previous = updating; updating = true;
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(found->second), value);
        updating = previous;
    }
}
inline void Update(const HostMenu::State &state)
{
    // GTK setters can queue layout and draw work even when called from a
    // guest pump that did not change the menu. Keep pumping events, but
    // update the controls only when the state they display changes.
    auto controls = [](const HostMenu::State &s) {
        return std::tie(s.dos_program, s.states, s.states_last, s.states_blocked, s.paused,
                        s.pause_unfocused, s.online, s.net_listen, s.seamless, s.seamless_taskbar,
                        s.seamless_windows, s.seamless_icons, s.seamless_dock, s.wheel_focus,
                        s.fullscreen, s.scale, s.guest_windows);
    };
    const bool changed = !menu_updated || controls(current) != controls(state);
    const bool windows_changed = current.guest_windows != state.guest_windows;
    current = state;
    if (!ready || !changed)
        return;
    menu_updated = true;
    updating = true;
    if (auto found = checks.find("dos-settings"); found != checks.end())
        gtk_widget_set_sensitive(found->second, !state.dos_program.empty());
    for (const char *name : {"state-save", "state-load", "states"})
        if (auto found = checks.find(name); found != checks.end())
        {
            const bool enabled = state.states && (std::string(name) == "state-save" ? state.states_blocked.empty()
                : std::string(name) == "state-load" ? state.states_last : true);
            gtk_widget_set_sensitive(found->second, enabled);
            gtk_widget_set_tooltip_text(found->second, std::string(name) == "state-save" && !state.states_blocked.empty()
                ? state.states_blocked.c_str() : nullptr);
        }
    Check("pause", state.paused);
    Check("pause-unfocused", state.pause_unfocused);
    Check("network", state.online);
    Check("network-listen", state.net_listen.compare(0, 4, "127.") != 0);
    Check("network-wifi", state.wifi);
    Check("microphone", state.microphone);
    Check("seamless", state.seamless);
    Check("seamless-taskbar", state.seamless_taskbar);
    Check("seamless-windows", state.seamless_windows);
    Check("seamless-icons", state.seamless_icons);
    for (const char *name : {"seamless-windows", "seamless-icons"})
        if (checks.count(name)) gtk_widget_set_sensitive(checks[name], !wayland_canvas);
    Check("seamless-dock", state.seamless_dock);
    Check("wheel-focus", state.wheel_focus);
    Check("fullscreen", state.fullscreen);
    Check("scale1", state.scale == 1);
    Check("scale2", state.scale == 2);
    if (status)
        gtk_label_set_text(GTK_LABEL(status), state.paused   ? "Paused"
                                              : state.online ? "Ready · Network connected"
                                                             : "Ready · Network disconnected");
    if (window_menu && windows_changed)
    {
        GList *children = gtk_container_get_children(GTK_CONTAINER(window_menu));
        for (GList *p = g_list_nth(children, 2); p; p = p->next)
            gtk_widget_destroy(GTK_WIDGET(p->data));
        g_list_free(children);
        if (!state.guest_windows.empty())
            Separator(window_menu);
        for (const auto &entry : state.guest_windows)
        {
            GtkWidget *item = gtk_menu_item_new_with_label(entry.second.c_str());
            gtk_menu_shell_append(GTK_MENU_SHELL(window_menu), item);
            g_signal_connect(item, "activate", G_CALLBACK(+[](GtkMenuItem *, gpointer data) {
                                 HostMenu::Perform("window", std::to_string(GPOINTER_TO_UINT(data)).c_str());
                             }),
                             GUINT_TO_POINTER(entry.first));
        }
        gtk_widget_show_all(window_menu);
    }
    updating = false;
}
inline void FocusCanvas()
{
    if (!embedded || seamless || !gtk_widget_get_visible(shell))
        return;
    if (wayland_canvas) { gtk_widget_grab_focus(canvas); return; }
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(desktop, &info) && info.subsystem == SDL_SYSWM_X11)
        XSetInputFocus(info.info.x11.display, info.info.x11.window, RevertToParent, CurrentTime);
}
inline bool Create(SDL_Window *window)
{
    desktop = window;
    if (!Init())
        return false;
    shell = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(shell), "Neo98");
    // Use our own pixels. A themed icon can synchronously start an external
    // image decoder while the window is realized, before the host loop can
    // serve input or its deadline. Reuse the port's System Properties artwork.
    unsigned char logo[Neo98OemLogo::Bytes];
    Neo98OemLogo::Draw(logo);
    const unsigned side = Neo98OemLogo::Height;
    if (GdkPixbuf *icon = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, side, side))
    {
        auto *pixels = gdk_pixbuf_get_pixels(icon);
        const unsigned stride = gdk_pixbuf_get_rowstride(icon);
        for (unsigned y = 0; y < side; ++y)
            for (unsigned x = 0; x < side; ++x)
                for (unsigned k = 0; k < 3; ++k)
                    pixels[y * stride + x * 3 + k] = logo[Neo98OemLogo::Header +
                        (side - 1 - y) * Neo98OemLogo::RowBytes +
                        (x + (Neo98OemLogo::Width - side) / 2) * 3 + 2 - k];
        gtk_window_set_icon(GTK_WINDOW(shell), icon);
        g_object_unref(icon);
    }
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(shell), box);
    gtk_box_pack_start(GTK_BOX(box), MenuBar(), FALSE, FALSE, 0);
    canvas = gtk_drawing_area_new();
    gtk_widget_set_has_window(canvas, TRUE);
    gtk_box_pack_start(GTK_BOX(box), canvas, TRUE, TRUE, 0);
    status = gtk_label_new("Ready");
    gtk_widget_set_margin_start(status, 12);
    gtk_widget_set_margin_end(status, 12);
    gtk_widget_set_margin_top(status, 4);
    gtk_widget_set_margin_bottom(status, 4);
    gtk_label_set_xalign(GTK_LABEL(status), 0);
    gtk_box_pack_start(GTK_BOX(box), status, FALSE, FALSE, 0);
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    gtk_window_set_default_size(GTK_WINDOW(shell), w, h + 55);
    g_signal_connect(shell, "delete-event",
                     G_CALLBACK(+[](GtkWidget *window, GdkEvent *, gpointer) -> gboolean {
                         if (seamless)
                             gtk_widget_hide(window);
                         else
                             HostMenu::Perform("quit", nullptr);
                         return TRUE;
                     }),
                     nullptr);
    g_signal_connect(shell, "focus-in-event", G_CALLBACK(+[](GtkWidget *, GdkEvent *, gpointer) -> gboolean {
                         FocusCanvas();
                         return FALSE;
                     }),
                     nullptr);
    GtkTargetEntry drop_target{const_cast<char *>("text/uri-list"), 0, 1};
    gtk_drag_dest_set(shell, GTK_DEST_DEFAULT_ALL, &drop_target, 1, GDK_ACTION_COPY);
    g_signal_connect(shell, "drag-data-received",
                     G_CALLBACK(+[](GtkWidget *, GdkDragContext *context, gint, gint,
                                    GtkSelectionData *selection, guint, guint time, gpointer) {
                         char **uris = gtk_selection_data_get_uris(selection);
                         bool opened = false;
                         if (uris)
                             for (unsigned i = 0; uris[i]; ++i)
                             {
                                 char *file = g_filename_from_uri(uris[i], nullptr, nullptr);
                                 if (file)
                                 {
                                     HostMenu::Perform("open", file);
                                     opened = true;
                                     g_free(file);
                                 }
                             }
                         g_strfreev(uris);
                         gtk_drag_finish(context, opened, FALSE, time);
                     }),
                     nullptr);
    gtk_widget_show_all(shell);
    Pump();
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    const bool wm = SDL_GetWindowWMInfo(window, &info);
    if (wm && info.subsystem == SDL_SYSWM_X11 && GDK_IS_X11_DISPLAY(gdk_display_get_default()))
    {
        SDL_SetWindowBordered(window, SDL_FALSE);
        // Withdraw the SDL top-level before embedding. Mutter otherwise
        // reparents the managed window back into its own frame.
        XWithdrawWindow(info.info.x11.display, info.info.x11.window, DefaultScreen(info.info.x11.display));
        XSync(info.info.x11.display, False);
        // XSync waits for the server, not for the WM to finish withdrawing.
        // Openbox (and other reparenting WMs) can otherwise unmap the SDL
        // window after we have embedded and mapped it. WM_STATE is removed
        // when the WM releases its client; without a WM it is already absent.
        const Atom wm_state = XInternAtom(info.info.x11.display, "WM_STATE", False);
        const Uint32 deadline = SDL_GetTicks() + 2000;
        for (;;)
        {
            Atom type = 0; int format = 0;
            unsigned long count = 0, remaining = 0;
            unsigned char *data = nullptr;
            XGetWindowProperty(info.info.x11.display, info.info.x11.window, wm_state, 0, 2, False,
                               AnyPropertyType, &type, &format, &count, &remaining, &data);
            const bool withdrawn = !type || (format == 32 && count &&
                *reinterpret_cast<unsigned long *>(data) == WithdrawnState);
            if (data) XFree(data);
            if (withdrawn) break;
            if (SDL_TICKS_PASSED(SDL_GetTicks(), deadline))
            {
                fprintf(stderr, "NEO98 window manager did not release the desktop window\n");
                gtk_widget_destroy(shell); shell = canvas = status = nullptr;
                SDL_ShowWindow(window);
                return false;
            }
            SDL_Delay(1);
        }
        XSetWindowAttributes attributes = {};
        attributes.override_redirect = True;
        XChangeWindowAttributes(info.info.x11.display, info.info.x11.window, CWOverrideRedirect, &attributes);
        XReparentWindow(info.info.x11.display, info.info.x11.window,
                        gdk_x11_window_get_xid(gtk_widget_get_window(canvas)), 0, 0);
        XMapWindow(info.info.x11.display, info.info.x11.window);
        XFlush(info.info.x11.display);
        embedded = true;
        g_signal_connect(
            canvas, "size-allocate", G_CALLBACK(+[](GtkWidget *, GtkAllocation *allocation, gpointer) {
                if (embedded && !seamless)
                {
                    const int factor = gtk_widget_get_scale_factor(canvas);
                    SDL_SetWindowSize(desktop, allocation->width * factor, allocation->height * factor);
                    redraw = true;
                }
            }),
            nullptr);
        g_signal_connect(canvas, "draw", G_CALLBACK(+[](GtkWidget *, cairo_t *, gpointer) -> gboolean {
                             redraw = true;
                             return FALSE;
                         }),
                         nullptr);
        GtkAllocation allocation;
        gtk_widget_get_allocation(canvas, &allocation);
        const int factor = gtk_widget_get_scale_factor(canvas);
        SDL_SetWindowSize(window, allocation.width * factor, allocation.height * factor);
        FocusCanvas();
    }
    else if (wm && info.subsystem == SDL_SYSWM_WAYLAND && GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default()))
        InstallWaylandCanvas();
    else
    {
        SDL_ShowWindow(window);
        gtk_widget_hide(canvas);
        gtk_window_resize(GTK_WINDOW(shell), 560, 80);
        gtk_widget_hide(shell);
    }
    return true;
}
inline void Show()
{
    if (!shell)
        return;
    gtk_widget_show(shell);
    guint32 time = GDK_CURRENT_TIME;
    if (GDK_IS_X11_DISPLAY(gdk_display_get_default()))
    {
        time = gdk_x11_get_server_time(gtk_widget_get_window(shell));
        gdk_x11_window_set_user_time(gtk_widget_get_window(shell), time);
    }
    gtk_window_present_with_time(GTK_WINDOW(shell), time);
    if (seamless || !embedded)
    {
        gtk_widget_hide(canvas);
        gtk_window_resize(GTK_WINDOW(shell), 560, 80);
    }
}
inline void SetSeamless(bool value)
{
    seamless = value;
    if (!shell)
        return;
    if (value)
    {
        gtk_widget_hide(canvas);
        gtk_widget_hide(shell);
    }
    else if (embedded)
    {
        gtk_widget_show(canvas);
        gtk_window_resize(GTK_WINDOW(shell), 800, 655);
        Show();
        FocusCanvas();
    }
}
inline void Resize(int width, int height)
{
    if (embedded && !seamless)
    {
        gtk_window_resize(GTK_WINDOW(shell), width, height + 55);
        gtk_widget_queue_resize(shell);
    }
    else if (desktop)
        SDL_SetWindowSize(desktop, width, height);
}
inline void Fullscreen(bool value)
{
    if (embedded && !seamless)
    {
        if (value)
            gtk_window_fullscreen(GTK_WINDOW(shell));
        else
            gtk_window_unfullscreen(GTK_WINDOW(shell));
    }
    else if (desktop)
        SDL_SetWindowFullscreen(desktop, value ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}
// Kodi's own window to the front through the window manager (EWMH), when
// one is on the screen. It has the class "Kodi".
inline void ActivateKodi(Display *display)
{
    const Window root = DefaultRootWindow(display);
    const Atom list = XInternAtom(display, "_NET_CLIENT_LIST", True), active = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
    if (!list)
        return;
    Atom type;
    int format;
    unsigned long count = 0, after = 0;
    unsigned char *data = nullptr;
    if (XGetWindowProperty(display, root, list, 0, 4096, False, XA_WINDOW, &type, &format, &count, &after, &data) != Success || !data)
        return;
    const Window *windows = reinterpret_cast<const Window *>(data);
    for (unsigned long i = 0; i < count; ++i)
    {
        XClassHint hint{};
        if (!XGetClassHint(display, windows[i], &hint))
            continue;
        const bool kodi = (hint.res_class && !strcasecmp(hint.res_class, "Kodi")) || (hint.res_name && !strcasecmp(hint.res_name, "kodi"));
        if (hint.res_name)
            XFree(hint.res_name);
        if (hint.res_class)
            XFree(hint.res_class);
        if (!kodi)
            continue;
        XEvent event{};
        event.xclient.type = ClientMessage;
        event.xclient.window = windows[i];
        event.xclient.message_type = active;
        event.xclient.format = 32;
        event.xclient.data.l[0] = 2;  // from a pager: the user's own wish
        event.xclient.data.l[1] = CurrentTime;
        XSendEvent(display, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
        XFlush(display);
        break;
    }
    XFree(data);
}
// Kodi plays a video (tools/host_media/service.h): the NEO98 window leaves
// the screen to Kodi's window, and comes back on top afterwards.
inline void Yield(bool away)
{
    if (seamless)
        return;
    if (embedded && shell)
    {
        if (away)
            gtk_widget_hide(shell);
        else
        {
            Show();
            FocusCanvas();
        }
    }
    else if (desktop)
    {
        if (away)
            SDL_HideWindow(desktop);
        else
        {
            SDL_ShowWindow(desktop);
            SDL_RaiseWindow(desktop);
        }
    }
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (away && desktop && SDL_GetWindowWMInfo(desktop, &info) && info.subsystem == SDL_SYSWM_X11)
        ActivateKodi(info.info.x11.display);
    Pump();
}
inline bool Active()
{
    return shell && gtk_window_is_active(GTK_WINDOW(shell));
}
inline void Close()
{
    // SDL still owns its child. Detach it before GTK destroys the parent.
    if (embedded && desktop)
    {
        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(desktop, &info) && info.subsystem == SDL_SYSWM_X11)
        {
            XUnmapWindow(info.info.x11.display, info.info.x11.window);
            XReparentWindow(info.info.x11.display, info.info.x11.window,
                            DefaultRootWindow(info.info.x11.display), 0, 0);
            XFlush(info.info.x11.display);
        }
    }
    desktop = nullptr;
    embedded = wayland_canvas = canvas_pointer = false;
    canvas_pixels.clear(); canvas_keys.fill(false);
    if (shell)
        gtk_widget_destroy(shell);
    shell = canvas = status = window_menu = nullptr;
    checks.clear();
}
} // namespace LinuxGtk
