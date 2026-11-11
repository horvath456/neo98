// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Bounded decoder for PE RT_DIALOG data, standard and extended templates.
// New loader infrastructure. USER traversal and coordinate rules live separately.
namespace DialogResource
{
struct Item
{
    unsigned style, extended, id, class_id, title_id;
    int x, y, width, height;
    // Windows sets no limit on the text of a control; the German SQL Server
    // client configuration (CLICONFG.DLL dialog 103) has 257 characters in a
    // STATIC, and a template the decoder refuses creates no dialog at all.
    char klass[64], title[1024];
};
struct Template
{
    unsigned consumed = 0, style = 0, extended = 0, count = 0, menu = 0, point = 0, weight = 0, italic = 0,
             charset = 0;
    // A DLGTEMPLATEEX (dlgVer 1, signature FFFFh); `extended` is the
    // extended window style of either form.
    bool extended_template = false;
    int x = 0, y = 0, width = 0, height = 0;
    // A menu named by a string (SNDREC32's main dialog names "APPMENU");
    // `menu` is the identifier of one named by number.
    char klass[64] = {}, title[256] = {}, face[64] = {}, menu_name[64] = {};
    Item items[128];
    bool Parse(const unsigned char *, unsigned);
};
} // namespace DialogResource
