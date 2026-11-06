// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "host_gtk_linux.h"
#include "host_settings.h"
#include <fcntl.h>
#include <sys/wait.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <csignal>
namespace LinuxSetup
{
inline std::string DataDirectory()
{
    const char *xdg = getenv("XDG_DATA_HOME");
    return (xdg && *xdg ? std::string(xdg) : HostSettings::Home() + "/.local/share") + "/neo98";
}
inline bool Valid(const std::string &root)
{
    for (const char *name : {"WINDOWS/EXPLORER.EXE", "WINDOWS/SYSTEM.DAT", "WINDOWS/USER.DAT", "WINDOWS/FONTS/VGAFIX.FON"})
        if (!std::filesystem::is_regular_file(std::filesystem::path(root) / name)) return false;
    return true;
}
struct Picker
{
    GtkWindow *parent;
    GtkEntry *entry;
    bool directory;
    GtkFileChooserNative *chooser = nullptr;

    void Close()
    {
        if (!chooser) return;
        g_signal_handlers_disconnect_by_data(chooser, this);
        gtk_native_dialog_destroy(GTK_NATIVE_DIALOG(chooser));
        g_object_unref(chooser); chooser = nullptr;
    }
    void Show()
    {
        if (chooser) return;
        chooser = gtk_file_chooser_native_new(directory ? "Choose a Windows 98 folder" : "Choose an ISO, ZIP or 7z file",
            parent, directory ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER : GTK_FILE_CHOOSER_ACTION_OPEN, "Choose", "Cancel");
        // The setup window is modal. Its input grab must pass to this child.
        gtk_native_dialog_set_modal(GTK_NATIVE_DIALOG(chooser), TRUE);
        const char *source = gtk_entry_get_text(entry);
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser),
            g_file_test(source, G_FILE_TEST_IS_DIR) ? source : g_get_home_dir());
        g_signal_connect(chooser, "response", G_CALLBACK(+[](GtkNativeDialog *dialog, gint response, gpointer data) {
            auto &picker = *static_cast<Picker *>(data);
            if (response == GTK_RESPONSE_ACCEPT)
            {
                char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
                if (path) { gtk_entry_set_text(picker.entry, path); g_free(path); }
            }
            picker.Close();
        }), this);
        gtk_native_dialog_show(GTK_NATIVE_DIALOG(chooser));
    }
    ~Picker() { Close(); }
};
inline std::string Script()
{
    if (const char *path = getenv("NEO98_SETUP_SCRIPT"); path && *path) return path;
    const auto exe = std::filesystem::path(HostSettings::Executable());
    const auto packaged = exe.parent_path() / "../share/neo98/setup/tools/linux_setup.py";
    if (std::filesystem::is_regular_file(packaged)) return packaged.string();
    return (exe.parent_path() / "../../tools/linux_setup.py").string();
}
inline bool FirstRun(int argc, char **argv)
{
    bool app_mode = getenv("NEO98_APP_MODE") && !strcmp(getenv("NEO98_APP_MODE"), "1"), force = false, explicit_root = false;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) return true;
        if (!strcmp(argv[i], "--setup-linux")) force = app_mode = true;
        if (!strcmp(argv[i], "--drive-c") || !strcmp(argv[i], "--system-tree") || !strcmp(argv[i], "--game-machine")) explicit_root = true;
    }
    if (!app_mode || (!force && (explicit_root || (getenv("NEO98_WIN98_ROOT") && Valid(getenv("NEO98_WIN98_ROOT")))))) return true;
    const std::string config = HostSettings::Directory() + "/machine-path";
    if (!force)
    {
        std::ifstream file(config); std::string root; std::getline(file, root);
        if (Valid(root)) { setenv("NEO98_WIN98_ROOT", root.c_str(), 1); return true; }
    }
    if (getenv("DISPLAY") && (!getenv("SDL_VIDEODRIVER") || !strcmp(getenv("SDL_VIDEODRIVER"), "x11")))
    { setenv("GDK_BACKEND", "x11", 1); gdk_set_allowed_backends("x11"); }
    if (!LinuxGtk::Init()) { fprintf(stderr, "Setup requires a graphical desktop.\n"); return false; }
    GtkWidget *window = gtk_dialog_new_with_buttons("Set up Neo98", nullptr, GTK_DIALOG_MODAL,
        "Cancel", GTK_RESPONSE_CANCEL, "Start", GTK_RESPONSE_OK, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(window), 650, 510);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(window)); gtk_container_set_border_width(GTK_CONTAINER(box), 20);
    GtkWidget *intro = gtk_label_new("Choose your Windows 98 installation folder or import it from a ZIP archive.\n"
        "Neo98 can install a new machine from a Windows 98 SE ISO, including one inside a ZIP or 7z archive.");
    gtk_label_set_line_wrap(GTK_LABEL(intro), TRUE); gtk_box_pack_start(GTK_BOX(box), intro, FALSE, FALSE, 8);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8); gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 8);
    GtkWidget *folder = gtk_button_new_with_label("Choose an existing folder…"), *image = gtk_button_new_with_label("Choose an ISO, ZIP or 7z file…");
    gtk_box_pack_start(GTK_BOX(row), folder, TRUE, TRUE, 0); gtk_box_pack_start(GTK_BOX(row), image, TRUE, TRUE, 0);
    GtkWidget *source = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(source), "Windows 98 folder, ISO, ZIP or 7z");
    gtk_box_pack_start(GTK_BOX(box), source, FALSE, FALSE, 4);
    GtkWidget *target = gtk_entry_new(); const std::string destination = DataDirectory() + "/Machines/Windows98";
    gtk_entry_set_text(GTK_ENTRY(target), destination.c_str());
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Destination folder for an import or new installation"), FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(box), target, FALSE, FALSE, 4);
    GtkWidget *key = gtk_entry_new(); gtk_entry_set_visibility(GTK_ENTRY(key), FALSE);
    gtk_entry_set_max_length(GTK_ENTRY(key), 64);
    gtk_entry_set_placeholder_text(GTK_ENTRY(key), "Windows 98 product key, required only for ISO installation");
    gtk_box_pack_start(GTK_BOX(box), key, FALSE, FALSE, 8);
    GtkWidget *preview = gtk_image_new();
    gtk_box_pack_start(GTK_BOX(box), preview, FALSE, FALSE, 4);
    GtkWidget *progress = gtk_progress_bar_new(); gtk_box_pack_start(GTK_BOX(box), progress, FALSE, FALSE, 4);
    GtkWidget *scroll = gtk_scrolled_window_new(nullptr, nullptr), *text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE); gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(scroll), text); gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 8);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
    Picker pick_folder{GTK_WINDOW(window), GTK_ENTRY(source), true}, pick_image{GTK_WINDOW(window), GTK_ENTRY(source), false};
    auto pick = +[](GtkButton *, gpointer data) { static_cast<Picker *>(data)->Show(); };
    g_signal_connect(folder, "clicked", G_CALLBACK(pick), &pick_folder); g_signal_connect(image, "clicked", G_CALLBACK(pick), &pick_image);
    int response = 0; g_signal_connect(window, "response", G_CALLBACK(+[](GtkDialog *, gint value, gpointer data) { *static_cast<int *>(data) = value; }), &response);
    gtk_widget_show_all(window);
    // Test input follows the same form and response path as the buttons.
    if (const char *test = getenv("NEO98_LINUX_SETUP_SOURCE"))
    { gtk_entry_set_text(GTK_ENTRY(source), test); if (const char *to = getenv("NEO98_LINUX_SETUP_TARGET")) gtk_entry_set_text(GTK_ENTRY(target), to); response = GTK_RESPONSE_OK; }
    auto append = [&](const std::string &message) {
        GtkTextIter end; gtk_text_buffer_get_end_iter(buffer, &end); gtk_text_buffer_insert(buffer, &end, message.c_str(), int(message.size()));
        if (gtk_text_buffer_get_char_count(buffer) > 100000)
        { GtkTextIter start, until; gtk_text_buffer_get_start_iter(buffer, &start); gtk_text_buffer_get_iter_at_offset(buffer, &until, 20000); gtk_text_buffer_delete(buffer, &start, &until); }
        printf("%s", message.c_str()); fflush(stdout);
    };
    bool success = false; std::string chosen;
    while (true)
    {
        while (!response) g_main_context_iteration(nullptr, TRUE);
        if (response != GTK_RESPONSE_OK) break;
        response = 0;
        const std::string from = gtk_entry_get_text(GTK_ENTRY(source)), to = gtk_entry_get_text(GTK_ENTRY(target));
        if (Valid(from) && std::filesystem::is_regular_file(LegacyData::ReadPath(from + "/.neo98-setup.json")))
        { chosen = from; success = true; break; }
        const std::string mode = std::filesystem::is_directory(from) ? "import" : "auto";
        gchar *preview_directory = g_dir_make_tmp("neo98-preview-XXXXXX", nullptr);
        if (!preview_directory) { append("Could not create the temporary preview folder.\n"); continue; }
        const std::string preview_path = std::string(preview_directory) + "/live.png";
        std::vector<std::string> args{"python3", "-u", Script(), mode, from, "--target", to, "--work", DataDirectory() + "/setup", "--preview", preview_path};
        std::vector<char *> command; for (auto &argument : args) command.push_back(argument.data()); command.push_back(nullptr);
        GPid pid = 0; int input = -1, output = -1, errors = -1; GError *error = nullptr;
        if (!g_spawn_async_with_pipes(nullptr, command.data(), nullptr, GSpawnFlags(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD), nullptr, nullptr,
            &pid, &input, &output, &errors, &error))
        { append(std::string("Could not start setup: ") + error->message + "\n"); g_error_free(error); rmdir(preview_directory); g_free(preview_directory); continue; }
        std::string secret = gtk_entry_get_text(GTK_ENTRY(key)); secret += '\n';
        if (mode == "iso" || mode == "auto")
        {
            struct sigaction ignore = {}, previous = {};
            ignore.sa_handler = SIG_IGN; sigemptyset(&ignore.sa_mask);
            sigaction(SIGPIPE, &ignore, &previous);
            const ssize_t sent = write(input, secret.data(), secret.size()); (void)sent;
            sigaction(SIGPIPE, &previous, nullptr);
        }
        std::fill(secret.begin(), secret.end(), '\0'); gtk_entry_set_text(GTK_ENTRY(key), ""); close(input);
        fcntl(output, F_SETFL, O_NONBLOCK); fcntl(errors, F_SETFL, O_NONBLOCK);
        gtk_widget_set_sensitive(folder, FALSE); gtk_widget_set_sensitive(image, FALSE); gtk_widget_set_sensitive(source, FALSE);
        gtk_widget_set_sensitive(target, FALSE); gtk_dialog_set_response_sensitive(GTK_DIALOG(window), GTK_RESPONSE_OK, FALSE);
        bool cancelled = false, child_done = false; int result = 0;
        auto killed = std::chrono::steady_clock::time_point{};
        auto next_frame = std::chrono::steady_clock::now();
        while (!child_done)
        {
            if (std::chrono::steady_clock::now() >= next_frame)
            {
                GdkPixbuf *frame = gdk_pixbuf_new_from_file_at_scale(preview_path.c_str(), 560, 320, TRUE, nullptr);
                if (frame) { gtk_image_set_from_pixbuf(GTK_IMAGE(preview), frame); g_object_unref(frame); }
                next_frame = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            }
            LinuxGtk::Pump(); gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress));
            if (response && !cancelled) { cancelled = true; kill(pid, SIGTERM); killed = std::chrono::steady_clock::now(); }
            if (cancelled && std::chrono::steady_clock::now() - killed > std::chrono::seconds(5)) kill(pid, SIGKILL);
            for (int fd : {output, errors}) { char bytes[4096]; ssize_t count; while ((count = read(fd, bytes, sizeof bytes)) > 0) append(std::string(bytes, count)); }
            const pid_t waited = waitpid(pid, &result, WNOHANG);
            child_done = waited == pid || (waited < 0 && errno != EINTR);
            if (!child_done) g_usleep(20000);
        }
        for (int fd : {output, errors}) { char bytes[4096]; ssize_t count; while ((count = read(fd, bytes, sizeof bytes)) > 0) append(std::string(bytes, count)); close(fd); }
        g_spawn_close_pid(pid);
        gtk_image_clear(GTK_IMAGE(preview));
        unlink(preview_path.c_str()); unlink((preview_path + ".tmp").c_str());
        rmdir(preview_directory); g_free(preview_directory);
        if (cancelled) break;
        if (WIFEXITED(result) && WEXITSTATUS(result) == 0 && Valid(to)) { chosen = to; success = true; break; }
        if (WIFEXITED(result) && WEXITSTATUS(result) == 3)
        {
            // The key: refused by Windows Setup, or malformed. Setup removed
            // what it made; the next Install starts from the beginning.
            append("Windows Setup did not accept the product key. Check it and click Install again.\n");
            gtk_widget_grab_focus(key);
        }
        else
            append("Setup failed. See the message above for details.\n");
        gtk_widget_set_sensitive(folder, TRUE); gtk_widget_set_sensitive(image, TRUE); gtk_widget_set_sensitive(source, TRUE);
        gtk_widget_set_sensitive(target, TRUE); gtk_dialog_set_response_sensitive(GTK_DIALOG(window), GTK_RESPONSE_OK, TRUE);
        if (getenv("NEO98_LINUX_SETUP_SOURCE")) break;
    }
    pick_folder.Close(); pick_image.Close();
    gtk_widget_destroy(window); LinuxGtk::Pump();
    if (!success) return false;
    chosen = std::filesystem::canonical(chosen).string();
    if (!HostSettings::MakeDirectories(HostSettings::Directory())) return false;
    const std::string temporary = config + "." + std::to_string(getpid());
    { std::ofstream file(temporary); file << chosen << '\n'; file.close(); if (!file) return false; }
    if (rename(temporary.c_str(), config.c_str())) { unlink(temporary.c_str()); return false; }
    setenv("NEO98_WIN98_ROOT", chosen.c_str(), 1); return true;
}
}
