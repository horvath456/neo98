// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "host_state_window.h"
#include "host_savestate.h"
#include "host_gtk_linux.h"
#include <deque>
namespace HostStateWindow
{
inline GtkWidget *dialog = nullptr, *list = nullptr, *entry = nullptr, *detail = nullptr, *preview = nullptr;
inline GtkWidget *save = nullptr, *load = nullptr, *remove = nullptr, *rename = nullptr, *keep = nullptr;
inline Model model;
inline std::string selected;
inline std::deque<Request> requests;
inline const Row *Selected()
{
    for (const auto &row : model.rows) if (row.id == selected) return &row;
    return nullptr;
}
inline void Selection()
{
    const auto *row = Selected();
    gtk_entry_set_text(GTK_ENTRY(entry), row ? row->name.c_str() : "");
    gtk_widget_set_sensitive(load, row && row->usable);
    gtk_widget_set_sensitive(remove, row != nullptr);
    gtk_widget_set_sensitive(rename, row != nullptr);
    gtk_widget_set_sensitive(keep, row && row->kind == "auto");
    gtk_label_set_text(GTK_LABEL(detail), row ? row->reason.c_str() : model.save_reason.c_str());
    gtk_image_clear(GTK_IMAGE(preview));
    if (row && !row->picture.empty())
    {
        GdkPixbuf *image = gdk_pixbuf_new_from_file_at_scale(row->picture.c_str(), 360, 240, TRUE, nullptr);
        if (image) { gtk_image_set_from_pixbuf(GTK_IMAGE(preview), image); g_object_unref(image); }
    }
}
inline void Update(const Model &value, const std::string &select)
{
    model = value;
    if (!dialog) return;
    if (!select.empty()) selected = select;
    if (!Selected())
    {
        selected.clear();
        for (const auto &row : model.rows) if (row.current) selected = row.id;
        if (selected.empty() && !model.rows.empty()) selected = model.rows.front().id;
    }
    GList *children = gtk_container_get_children(GTK_CONTAINER(list));
    for (auto *node = children; node; node = node->next) gtk_widget_destroy(GTK_WIDGET(node->data));
    g_list_free(children);
    for (const auto &row : model.rows)
    {
        const auto text = std::string(row.current ? "● " : "") + row.name + "\n" +
            HostSaveState::TimeText(row.time, "%d.%m.%Y %H:%M:%S") + (row.kind == "auto" ? " · automatic" : "");
        GtkWidget *item = gtk_list_box_row_new(), *label = gtk_label_new(text.c_str());
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_label_set_xalign(GTK_LABEL(label), 0);
        gtk_container_set_border_width(GTK_CONTAINER(item), 8);
        if (!row.picture.empty())
        {
            GdkPixbuf *image = gdk_pixbuf_new_from_file_at_scale(row.picture.c_str(), 80, 60, TRUE, nullptr);
            if (image) { gtk_box_pack_start(GTK_BOX(box), gtk_image_new_from_pixbuf(image), FALSE, FALSE, 0); g_object_unref(image); }
        }
        gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);
        gtk_container_add(GTK_CONTAINER(item), box);
        for (const auto &parent : model.rows) if (row.parent == parent.id)
            gtk_widget_set_tooltip_text(item, ("Based on " + parent.name).c_str());
        g_object_set_data_full(G_OBJECT(item), "state-id", g_strdup(row.id.c_str()), g_free);
        gtk_container_add(GTK_CONTAINER(list), item);
        if (row.id == selected) gtk_list_box_select_row(GTK_LIST_BOX(list), GTK_LIST_BOX_ROW(item));
    }
    gtk_widget_set_sensitive(save, model.can_save);
    gtk_widget_set_tooltip_text(save, model.save_reason.empty() ? nullptr : model.save_reason.c_str());
    Selection();
    gtk_widget_show_all(list);
}
inline bool IsOpen() { return dialog != nullptr; }
inline void Close()
{
    if (dialog) { GtkWidget *old = dialog; dialog = nullptr; gtk_widget_destroy(old); }
}
inline bool Open(const Model &value)
{
    if (dialog) { Update(value, selected); gtk_window_present(GTK_WINDOW(dialog)); return true; }
    dialog = gtk_dialog_new_with_buttons((value.game + " · States").c_str(),
        LinuxGtk::shell ? GTK_WINDOW(LinuxGtk::shell) : nullptr, GTK_DIALOG_DESTROY_WITH_PARENT,
        "Close", GTK_RESPONSE_CLOSE, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 750, 450);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *columns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    gtk_container_set_border_width(GTK_CONTAINER(columns), 16);
    gtk_box_pack_start(GTK_BOX(box), columns, TRUE, TRUE, 0);
    GtkWidget *scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_widget_set_size_request(scroll, 310, 300);
    list = gtk_list_box_new(); gtk_container_add(GTK_CONTAINER(scroll), list);
    gtk_box_pack_start(GTK_BOX(columns), scroll, TRUE, TRUE, 0);
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_pack_start(GTK_BOX(columns), right, TRUE, TRUE, 0);
    preview = gtk_image_new(); gtk_box_pack_start(GTK_BOX(right), preview, TRUE, TRUE, 0);
    entry = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "State name");
    gtk_box_pack_start(GTK_BOX(right), entry, FALSE, FALSE, 0);
    detail = gtk_label_new(""); gtk_label_set_line_wrap(GTK_LABEL(detail), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(detail), 42);
    gtk_box_pack_start(GTK_BOX(right), detail, FALSE, FALSE, 0);
    auto button = [&](const char *text, Request::Kind kind) {
        GtkWidget *widget = gtk_button_new_with_label(text);
        gtk_box_pack_start(GTK_BOX(right), widget, FALSE, FALSE, 0);
        g_signal_connect(widget, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
            requests.push_back({Request::Kind(GPOINTER_TO_INT(data)), selected, gtk_entry_get_text(GTK_ENTRY(entry))});
        }), GINT_TO_POINTER(kind));
        return widget;
    };
    save = button("Save state", Request::Save); load = button("Load", Request::Load);
    rename = button("Rename", Request::Rename); keep = button("Keep automatic state", Request::Keep);
    remove = gtk_button_new_with_label("Delete…"); gtk_box_pack_start(GTK_BOX(right), remove, FALSE, FALSE, 0);
    g_signal_connect(remove, "clicked", G_CALLBACK(+[](GtkButton *, gpointer) {
        if (!Selected()) return;
        GtkWidget *question = gtk_message_dialog_new(GTK_WINDOW(dialog), GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "Delete state?");
        g_object_set_data_full(G_OBJECT(question), "state-id", g_strdup(selected.c_str()), g_free);
        g_signal_connect(question, "response", G_CALLBACK(+[](GtkDialog *question, gint response, gpointer) {
            if (response == GTK_RESPONSE_YES) requests.push_back({Request::Delete,
                static_cast<const char *>(g_object_get_data(G_OBJECT(question), "state-id")), {}});
            gtk_widget_destroy(GTK_WIDGET(question));
        }), nullptr);
        gtk_widget_show(question);
    }), nullptr);
    g_signal_connect(list, "row-selected", G_CALLBACK(+[](GtkListBox *, GtkListBoxRow *row, gpointer) {
        if (!row) return;
        selected = static_cast<const char *>(g_object_get_data(G_OBJECT(row), "state-id")); Selection();
    }), nullptr);
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog *, gint, gpointer) { requests.push_back({Request::Close, {}, {}}); }), nullptr);
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget *old, gpointer) { if (old == dialog) dialog = nullptr; }), nullptr);
    Update(value, {}); gtk_widget_show_all(dialog); return true;
}
inline bool Next(Request &request)
{
    if (requests.empty()) return false;
    request = std::move(requests.front()); requests.pop_front(); return true;
}
}
