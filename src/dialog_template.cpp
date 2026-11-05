// SPDX-License-Identifier: GPL-2.0-or-later
#include "dialog_template.h"
namespace DialogResource
{
namespace
{
struct Reader
{
    const unsigned char *data;
    unsigned size, at = 0;
    bool valid = true;
    unsigned Byte()
    {
        if (at >= size)
        {
            valid = false;
            return 0;
        }
        return data[at++];
    }
    unsigned Word()
    {
        unsigned lo = Byte();
        return lo | (Byte() << 8);
    }
    unsigned Long()
    {
        unsigned lo = Word();
        return lo | (Word() << 16);
    }
    int Signed()
    {
        return (short)Word();
    }
    void Align()
    {
        at = (at + 3) & ~3u;
        if (at > size)
            valid = false;
    }
    unsigned String(char *out, unsigned capacity)
    {
        if (!capacity)
        {
            valid = false;
            return 0;
        }
        out[0] = 0;
        unsigned word = Word();
        if (word == 65535)
            return Word();
        unsigned n = 0;
        while (valid && word)
        {
            if (n + 1 >= capacity)
            {
                valid = false;
                break;
            }
            out[n++] = word < 256 ? (char)word : '?';
            word = Word();
        }
        out[n] = 0;
        return 0;
    }
};
} // namespace
bool Template::Parse(const unsigned char *data, unsigned length)
{
    *this = Template{};
    if (!data || length < 18)
        return false;
    Reader r{data, length};
    unsigned version = r.Word(), signature = r.Word();
    bool extended_template = signature == 65535;
    if (extended_template)
    {
        if (version != 1)
            return false;
        r.Long();
        extended = r.Long();
        style = r.Long();
        this->extended_template = true;
    }
    else
    {
        r.at = 0;
        style = r.Long();
        extended = r.Long();
    }
    count = r.Word();
    if (count > 128)
        return false;
    x = r.Signed();
    y = r.Signed();
    width = r.Signed();
    height = r.Signed();
    menu = r.String(menu_name, sizeof menu_name);
    unsigned class_id = r.String(klass, sizeof klass);
    if (class_id)
        return false;
    r.String(title, sizeof title);
    if (style & 0x40)
    {
        point = r.Word();
        if (extended_template)
        {
            weight = r.Word();
            italic = r.Byte();
            charset = r.Byte();
        }
        r.String(face, sizeof face);
    }
    for (unsigned i = 0; i < count && r.valid; ++i)
    {
        r.Align();
        auto &item = items[i];
        if (extended_template)
        {
            r.Long();
            item.extended = r.Long();
            item.style = r.Long();
        }
        else
        {
            item.style = r.Long();
            item.extended = r.Long();
        }
        item.x = r.Signed();
        item.y = r.Signed();
        item.width = r.Signed();
        item.height = r.Signed();
        item.id = extended_template ? r.Long() : r.Word();
        item.class_id = r.String(item.klass, sizeof item.klass);
        item.title_id = r.String(item.title, sizeof item.title);
        unsigned extra = r.Word();
        if (extra)
        {
            if (!extended_template)
            {
                if (extra < 2)
                    return false;
                extra -= 2;
            }
            if (!r.valid || r.at > r.size || extra > r.size - r.at)
                return false;
            r.at += extra;
        }
        if (item.width < 0 || item.height < 0)
            return false;
    }
    consumed = r.at;
    return r.valid && width >= 0 && height >= 0;
}
} // namespace DialogResource
