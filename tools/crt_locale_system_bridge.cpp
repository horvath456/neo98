/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_crt_locale.h"
#include <cstdlib>
struct Runtime final : Win98Locale::Environment
{
    Win98Locale::State state;
    Win98Locale::SystemData data;
    char *Allocate(unsigned n) override { return (char *)std::malloc(n); }
    void Free(char *p) override { std::free(p); }
    void Lock(unsigned) override {}
    void Unlock(unsigned) override {}
    void Sleep(unsigned) override {}
    bool Qualify(Win98Locale::Parts &p, Win98Locale::Id &i) override
    {
        return Win98Locale::QualifySystem(p, i);
    }
    int Initialize(unsigned c, Win98Locale::State &s) override { return data.Initialize(c, s); }
    ~Runtime()
    {
        for (auto p : state.names)
            if (p != state.c_locale)
                std::free(p);
    }
};
extern "C"
{
    const char *system_locale_info(unsigned locale, unsigned type)
    {
        return Win98Locale::SystemInfo(locale, type);
    }
    void *system_locale_new() { return new Runtime; }
    void system_locale_delete(Runtime *r) { delete r; }
    const char *system_locale_set(Runtime *r, int c, const char *s)
    {
        return Win98Locale::Set(*r, r->state, c, s);
    }
    void system_locale_dump(Runtime *r, unsigned char *out)
    {
        auto &d = r->data;
        memcpy(out, d.ctype, 514);
        out += 514;
        memcpy(out, d.wide_type, 514);
        out += 514;
        memcpy(out, &d.mb_max, 4);
        out += 4;
        for (auto &table : d.numeric)
        {
            memset(out, 0, 128);
            strcpy((char *)out, table);
            out += 128;
        }
        for (auto &table : d.monetary)
        {
            memset(out, 0, 128);
            strcpy((char *)out, table);
            out += 128;
        }
        memcpy(out, d.monetary_flags, 8);
        out += 8;
        for (auto &table : d.time)
        {
            memset(out, 0, 128);
            strcpy((char *)out, table);
            out += 128;
        }
    }
    void system_locale_words(Runtime *r, unsigned *out)
    {
        auto &s = r->state;
        for (auto x : s.handles)
            *out++ = x;
        for (auto x : s.ids)
        {
            *out++ = x.language;
            *out++ = x.country;
            *out++ = x.page;
        }
        *out++ = s.code_page;
        *out++ = s.collate_page;
        *out++ = s.set_active;
        *out++ = s.read_active;
    }
    unsigned system_locale_isctype(Runtime *r, unsigned c, unsigned mask)
    {
        return Win98Locale::IsCType(r->data, c, mask);
    }
    unsigned system_locale_case(Runtime *r, unsigned c, unsigned upper)
    {
        return Win98Locale::Case(r->state, r->data, c, upper);
    }
}
