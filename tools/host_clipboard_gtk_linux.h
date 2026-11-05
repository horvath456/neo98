// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../src/host_clipboard.h"
#include "host_gtk_linux.h"
#include "linux_clipboard_image.h"
#include <functional>
#include <memory>
namespace LinuxHost
{
class Clipboard
{
    struct Data
    {
        std::string text;
        std::vector<std::string> uris;
        GdkPixbuf *image = nullptr;
        bool has_text = false, cut = false;
        ~Data()
        {
            if (image)
                g_object_unref(image);
        }
    };
    struct State
    {
        GtkClipboard *clipboard = nullptr;
        GObject *owner = nullptr, *external = nullptr;
        unsigned token = 0, pending = 0, kinds = 0;
        bool active = true;
        std::u16string text;
        std::vector<std::string> files;
        std::vector<unsigned char> dib;
    };
    struct Fetch
    {
        std::weak_ptr<State> state;
        unsigned token;
    };
    std::shared_ptr<State> state = std::make_shared<State>();
    X86::HostClipboard::Provider provider = {};
    unsigned generation = 0;
    gulong handler = 0;
    bool private_only = false;
    std::unique_ptr<Data> private_data;
    void Private(Data *data,bool offer)
    {
        private_data.reset(data);
        if (!offer) return;
        state->text.clear(); state->files.clear(); state->dib.clear(); state->kinds = state->pending = 0;
        ++state->token;
        if (data->has_text)
        {
            gunichar2 *wide = g_utf8_to_utf16(data->text.c_str(),data->text.size(),nullptr,nullptr,nullptr);
            if (wide)
            {
                for (size_t i = 0; wide[i]; ++i)
                {
                    if (wide[i] == '\r' || wide[i] == '\n')
                    { state->text += u"\r\n"; if (wide[i] == '\r' && wide[i+1] == '\n') ++i; }
                    else state->text += char16_t(wide[i]);
                }
                g_free(wide); state->kinds |= X86::HostClipboard::Text;
            }
        }
        for (const auto &uri : data->uris)
        { char *path = g_filename_from_uri(uri.c_str(),nullptr,nullptr); if (path) { state->files.emplace_back(path); g_free(path); } }
        if (!state->files.empty()) state->kinds |= X86::HostClipboard::Files;
        if (ClipboardDib(data->image,state->dib)) state->kinds |= X86::HostClipboard::Image;
        if (state->kinds) X86::HostClipboard::Offer(state->kinds);
    }
    std::function<bool(const std::string &, std::string &)> to_guest, to_host;
    static std::shared_ptr<State> Take(void *data)
    {
        std::unique_ptr<Fetch> fetch(static_cast<Fetch *>(data));
        auto state = fetch->state.lock();
        return state && state->active && state->token == fetch->token ? state : nullptr;
    }
    static void Complete(State &state, unsigned kind)
    {
        state.pending &= ~kind;
        if (!state.pending && state.kinds)
            X86::HostClipboard::Offer(state.kinds);
    }
    static void FetchContent(const std::shared_ptr<State> &state)
    {
        if (state->owner && gtk_clipboard_get_owner(state->clipboard) == state->owner)
            return;
        ++state->token;
        gtk_clipboard_request_targets(
            state->clipboard,
            +[](GtkClipboard *clipboard, GdkAtom *targets, gint count, gpointer data) {
                auto state = Take(data);
                if (!state)
                    return;
                if (!targets || count < 0)
                {
                    state->pending = state->kinds = 0;
                    return;
                }
                unsigned wanted = 0;
                if (gtk_targets_include_text(targets, count))
                    wanted |= X86::HostClipboard::Text;
                if (gtk_targets_include_uri(targets, count))
                    wanted |= X86::HostClipboard::Files;
                if (gtk_targets_include_image(targets, count, TRUE))
                    wanted |= X86::HostClipboard::Image;
                state->pending = wanted;
                state->kinds = 0;
                state->text.clear();
                state->files.clear();
                state->dib.clear();
                if (wanted & X86::HostClipboard::Text)
                    gtk_clipboard_request_text(
                        clipboard,
                        +[](GtkClipboard *, const gchar *text, gpointer data) {
                            auto state = Take(data);
                            if (!state)
                                return;
                            if (text)
                            {
                                char *normal = g_utf8_normalize(text, -1, G_NORMALIZE_NFC);
                                glong count = 0;
                                gunichar2 *wide =
                                    normal ? g_utf8_to_utf16(normal, -1, nullptr, &count, nullptr) : nullptr;
                                if (wide)
                                {
                                    for (glong i = 0; i < count; ++i)
                                    {
                                        if (wide[i] == '\r' || wide[i] == '\n')
                                        {
                                            state->text += u"\r\n";
                                            if (wide[i] == '\r' && i + 1 < count && wide[i + 1] == '\n')
                                                ++i;
                                        }
                                        else
                                            state->text += char16_t(wide[i]);
                                    }
                                    state->kinds |= X86::HostClipboard::Text;
                                    g_free(wide);
                                }
                                g_free(normal);
                            }
                            Complete(*state, X86::HostClipboard::Text);
                        },
                        new Fetch{state, state->token});
                if (wanted & X86::HostClipboard::Files)
                    gtk_clipboard_request_uris(
                        clipboard,
                        +[](GtkClipboard *, gchar **uris, gpointer data) {
                            auto state = Take(data);
                            if (!state)
                                return;
                            if (uris)
                                for (unsigned i = 0; uris[i]; ++i)
                                {
                                    char *path = g_filename_from_uri(uris[i], nullptr, nullptr);
                                    if (path)
                                    {
                                        state->files.emplace_back(path);
                                        g_free(path);
                                    }
                                }
                            if (!state->files.empty())
                                state->kinds |= X86::HostClipboard::Files;
                            Complete(*state, X86::HostClipboard::Files);
                        },
                        new Fetch{state, state->token});
                if (wanted & X86::HostClipboard::Image)
                    gtk_clipboard_request_image(
                        clipboard,
                        +[](GtkClipboard *, GdkPixbuf *image, gpointer data) {
                            auto state = Take(data);
                            if (!state)
                                return;
                            if (ClipboardDib(image, state->dib))
                                state->kinds |= X86::HostClipboard::Image;
                            Complete(*state, X86::HostClipboard::Image);
                        },
                        new Fetch{state, state->token});
            },
            new Fetch{state, state->token});
    }
    static GObject *Publish(GtkClipboard *clipboard, Data *data)
    {
        GObject *owner = G_OBJECT(g_object_new(G_TYPE_OBJECT, nullptr));
        g_object_set_data_full(
            owner, "content", data, +[](gpointer data) { delete static_cast<Data *>(data); });
        GtkTargetList *list = gtk_target_list_new(nullptr, 0);
        if (data->has_text)
            gtk_target_list_add_text_targets(list, 1);
        if (!data->uris.empty())
        {
            gtk_target_list_add_uri_targets(list, 2);
            gtk_target_list_add(list, gdk_atom_intern_static_string("x-special/gnome-copied-files"), 0, 3);
        }
        if (data->image)
            gtk_target_list_add_image_targets(list, 4, TRUE);
        gint count = 0;
        GtkTargetEntry *targets = gtk_target_table_new_from_list(list, &count);
        const gboolean okay =
            count &&
            gtk_clipboard_set_with_owner(
                clipboard, targets, count,
                +[](GtkClipboard *, GtkSelectionData *selection, guint kind, gpointer owner) {
                    auto &data = *static_cast<Data *>(g_object_get_data(G_OBJECT(owner), "content"));
                    if (kind == 1)
                        gtk_selection_data_set_text(selection, data.text.c_str(), data.text.size());
                    else if (kind == 2)
                    {
                        std::vector<char *> uris;
                        for (auto &uri : data.uris)
                            uris.push_back(uri.data());
                        uris.push_back(nullptr);
                        gtk_selection_data_set_uris(selection, uris.data());
                    }
                    else if (kind == 3)
                    {
                        std::string text = data.cut ? "cut\n" : "copy\n";
                        for (const auto &uri : data.uris)
                            text += uri + "\n";
                        gtk_selection_data_set(selection, gtk_selection_data_get_target(selection), 8,
                                               reinterpret_cast<const guchar *>(text.data()), text.size());
                    }
                    else if (kind == 4)
                        gtk_selection_data_set_pixbuf(selection, data.image);
                },
                +[](GtkClipboard *, gpointer) {}, owner);
        if (!count)
            gtk_clipboard_clear(clipboard);
        gtk_target_table_free(targets, count);
        gtk_target_list_unref(list);
        if (!okay)
        {
            g_object_unref(owner);
            return nullptr;
        }
        return owner;
    }
    static bool Text(void *context, uint16_t **units, unsigned *count)
    {
        auto &data = *static_cast<Clipboard *>(context)->state;
        if (!(data.kinds & X86::HostClipboard::Text))
            return false;
        *count = data.text.size();
        *units = static_cast<uint16_t *>(malloc((*count + 1) * 2));
        if (!*units)
            return false;
        memcpy(*units, data.text.data(), *count * 2);
        (*units)[*count] = 0;
        return true;
    }
    static bool Files(void *context, char **list, unsigned *bytes)
    {
        auto &clipboard = *static_cast<Clipboard *>(context);
        std::string out;
        if (!clipboard.to_guest)
            return false;
        for (const auto &path : clipboard.state->files)
        {
            std::string guest;
            if (clipboard.to_guest(path, guest))
            {
                out += guest;
                out += '\0';
            }
        }
        if (out.empty())
            return false;
        out += '\0';
        *bytes = out.size();
        *list = static_cast<char *>(malloc(out.size()));
        if (!*list)
            return false;
        memcpy(*list, out.data(), out.size());
        return true;
    }
    static bool Image(void *context, unsigned char **dib, unsigned *bytes)
    {
        const auto &data = static_cast<Clipboard *>(context)->state->dib;
        if (data.empty())
            return false;
        *bytes = data.size();
        *dib = static_cast<unsigned char *>(malloc(data.size()));
        if (!*dib)
            return false;
        memcpy(*dib, data.data(), data.size());
        return true;
    }

  public:
    inline static Clipboard *current = nullptr;
    Clipboard()
    {
        if (!LinuxGtk::Init())
            return;
        const char *name = getenv("NEO98_CLIPBOARD");
        std::string selection =
            name && strcmp(name, "general")
                ? std::string(name) == "private" ? "NEO98_TEST_" + std::to_string(getpid()) : name
                : "CLIPBOARD";
        private_only = name && !strcmp(name,"private") && GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default());
        if (!private_only) state->clipboard = gtk_clipboard_get_for_display(gdk_display_get_default(),
                                                         gdk_atom_intern(selection.c_str(), FALSE));
        if (state->clipboard) handler = g_signal_connect(state->clipboard, "owner-change",
                                   G_CALLBACK(+[](GtkClipboard *, GdkEvent *, gpointer data) {
                                       auto &clipboard = *static_cast<Clipboard *>(data);
                                       FetchContent(clipboard.state);
                                   }),
                                   this);
        provider.context = this;
        provider.Text = Text;
        provider.Files = Files;
        provider.Image = Image;
        X86::HostClipboard::SetProvider(&provider);
        current = this;
        if (state->clipboard) FetchContent(state);
    }
    ~Clipboard()
    {
        X86::HostClipboard::SetProvider(nullptr);
        current = nullptr;
        state->active = false;
        if (!state->clipboard)
            return;
        g_signal_handler_disconnect(state->clipboard, handler);
        GObject *owner = gtk_clipboard_get_owner(state->clipboard);
        if (owner && (owner == state->owner || owner == state->external))
        {
            gtk_clipboard_store(state->clipboard);
            gtk_clipboard_clear(state->clipboard);
        }
        if (state->owner)
            g_object_unref(state->owner);
        if (state->external)
            g_object_unref(state->external);
    }
    void Paths(std::function<bool(const std::string &, std::string &)> guest,
               std::function<bool(const std::string &, std::string &)> host)
    {
        to_guest = std::move(guest);
        to_host = std::move(host);
    }
    void Pump()
    {
        if (!state->clipboard && !private_only)
            return;
        const auto &content = X86::HostClipboard::Guest();
        if (content.generation == generation)
            return;
        generation = content.generation;
        auto *data = new Data;
        if (content.text)
        {
            glong length = 0;
            char *text = g_utf16_to_utf8(reinterpret_cast<const gunichar2 *>(content.text),
                                         content.text_units, nullptr, &length, nullptr);
            if (text)
            {
                data->has_text = true;
                for (glong i = 0; i < length; ++i)
                    if (text[i] != '\r' || i + 1 == length || text[i + 1] != '\n')
                        data->text += text[i];
                g_free(text);
            }
        }
        if (content.files && to_host)
            for (size_t at = 0; at < content.files_bytes;)
            {
                const char *name = content.files + at;
                size_t length = strnlen(name, content.files_bytes - at);
                if (!length || length == content.files_bytes - at)
                    break;
                std::string host;
                if (to_host(std::string(name, length), host))
                {
                    char *uri = g_filename_to_uri(host.c_str(), nullptr, nullptr);
                    if (uri)
                    {
                        data->uris.emplace_back(uri);
                        g_free(uri);
                    }
                }
                at += length + 1;
            }
        data->cut = content.effect == 2;
        if (content.image)
            data->image = ClipboardPixbuf(content.image, content.image_bytes);
        if (private_only) { Private(data,false); return; }
        GObject *owner = Publish(state->clipboard, data);
        if (state->owner)
            g_object_unref(state->owner);
        state->owner = owner;
        ++state->token;
    }
    void SetText(const std::string &text)
    {
        if (private_only) { auto *data = new Data; data->has_text = true; data->text = text; Private(data,true); return; }
        if (state->clipboard)
        {
            gtk_clipboard_set_text(state->clipboard, text.c_str(), text.size());
            FetchContent(state);
        }
    }
    void SetFiles(const std::vector<std::string> &files)
    {
        auto *data = new Data;
        for (const auto &path : files)
        {
            char *uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
            if (uri)
            {
                data->uris.emplace_back(uri);
                g_free(uri);
            }
        }
        if (private_only) { Private(data,true); return; }
        GObject *owner = Publish(state->clipboard, data);
        if (state->external)
            g_object_unref(state->external);
        state->external = owner;
        FetchContent(state);
    }
    bool SetPng(const char *file)
    {
        GdkPixbuf *image = gdk_pixbuf_new_from_file(file, nullptr);
        if (!image)
            return false;
        auto *data = new Data;
        data->image = image;
        if (private_only) { Private(data,true); return true; }
        GObject *owner = Publish(state->clipboard, data);
        if (state->external)
            g_object_unref(state->external);
        state->external = owner;
        FetchContent(state);
        return owner != nullptr;
    }
    std::string ReadText()
    {
        if (private_only) return private_data && private_data->has_text ? private_data->text : "";
        char *text = state->clipboard ? gtk_clipboard_wait_for_text(state->clipboard) : nullptr;
        std::string out = text ? text : "";
        g_free(text);
        return out;
    }
    void Describe(const char *path)
    {
        FILE *file = fopen(path, "w");
        if (!file)
            return;
        char *text = private_only ? (private_data && private_data->has_text ? g_strdup(private_data->text.c_str()) : nullptr)
                                  : gtk_clipboard_wait_for_text(state->clipboard);
        if (text)
        {
            fputs("text ", file);
            for (const unsigned char *p = reinterpret_cast<unsigned char *>(text); *p; ++p)
                fprintf(file, "%02x", *p);
            fputc('\n', file);
            g_free(text);
        }
        char **uris = nullptr;
        if (private_only && private_data)
        { uris = g_new0(char *,private_data->uris.size()+1); for (size_t i=0;i<private_data->uris.size();++i) uris[i]=g_strdup(private_data->uris[i].c_str()); }
        else if (!private_only) uris = gtk_clipboard_wait_for_uris(state->clipboard);
        if (uris)
            for (unsigned i = 0; uris[i]; ++i)
            {
                char *name = g_filename_from_uri(uris[i], nullptr, nullptr);
                if (name)
                {
                    fprintf(file, "file %s\n", name);
                    g_free(name);
                }
            }
        g_strfreev(uris);
        GdkPixbuf *image = private_only ? (private_data && private_data->image ? GDK_PIXBUF(g_object_ref(private_data->image)) : nullptr)
                                       : gtk_clipboard_wait_for_image(state->clipboard);
        if (image)
        {
            const int w = gdk_pixbuf_get_width(image), h = gdk_pixbuf_get_height(image),
                      channels = gdk_pixbuf_get_n_channels(image);
            fprintf(file, "image %d %d", w, h);
            const unsigned char *pixels = gdk_pixbuf_read_pixels(image);
            for (int y = 0; y < h && y * w < 64; ++y)
                for (int x = 0; x < w && y * w + x < 64; ++x)
                {
                    const unsigned char *p = pixels + y * gdk_pixbuf_get_rowstride(image) + x * channels;
                    fprintf(file, " %02x%02x%02x", p[0], p[1], p[2]);
                }
            fputc('\n', file);
            g_object_unref(image);
        }
        fclose(file);
    }
};
} // namespace LinuxHost
