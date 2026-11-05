/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_crt_locale.h"
#include <cstdio>
#include <vector>
struct Runtime : Win98Locale::Environment
{
    Win98Locale::State state;
    unsigned char arena[2 * 1024 * 1024] = {};
    unsigned next = 0, fail = 0, init_fail = 0;
    std::vector<unsigned> events;
    unsigned Address(const char *p)
    {
        if (!p)
            return 0;
        if (p == state.c_locale)
            return 0x780375bc;
        return 0x200000 + (const unsigned char *)p - arena;
    }
    char *Allocate(unsigned size) override
    {
        unsigned address = 0;
        if (size != 849 && fail)
        {
            if (!--fail)
            {
                events.insert(events.end(), {1, size, 0});
                return nullptr;
            }
        }
        if (next + size <= sizeof arena)
        {
            address = next;
            next = (next + size + 15) & ~15u;
            events.insert(events.end(), {1, size, 0x200000 + address});
            return (char *)arena + address;
        }
        return nullptr;
    }
    void Free(char *p) override { events.insert(events.end(), {2, Address(p)}); }
    void Lock(unsigned id) override { events.insert(events.end(), {3, id}); }
    void Unlock(unsigned id) override { events.insert(events.end(), {4, id}); }
    void Sleep(unsigned ms) override
    {
        events.insert(events.end(), {5, ms});
        --state.read_active;
    }
    bool Qualify(Win98Locale::Parts &p, Win98Locale::Id &id) override
    {
        unsigned locale = (!p.language[0] || !strcmp(p.language, "English") ||
                           !strcmp(p.language, "english") || !strcmp(p.language, "EN"))
                              ? 0x409
                          : !strcmp(p.language, "German") ? 0x407
                                                          : 0;
        unsigned page = !p.page[0] || !strcmp(p.page, "ACP") ? 1252
                        : !strcmp(p.page, "OCP")             ? 437
                        : !strcmp(p.page, "850")             ? 850
                        : !strcmp(p.page, "1252")            ? 1252
                                                             : 0;
        events.insert(events.end(), {6, locale, page});
        if (!locale || !page)
            return false;
        strcpy(p.language, locale == 0x409 ? "English" : "German");
        strcpy(p.country, locale == 0x409 ? "United States" : "Germany");
        std::snprintf(p.page, sizeof p.page, "%u", page);
        id = {(uint16_t)locale, (uint16_t)locale, (uint16_t)page};
        return true;
    }
    int Initialize(unsigned category, Win98Locale::State &s) override
    {
        events.insert(events.end(),
                      {7, category, s.handles[category], s.code_page, s.collate_page, s.set_active});
        return (init_fail & (1u << category)) ? 1 : 0;
    }
};
extern "C"
{
    Runtime *locale_new() { return new Runtime; }
    void locale_delete(Runtime *r) { delete r; }
    unsigned locale_set(Runtime *r, int category, const char *input)
    {
        return r->Address(Win98Locale::Set(*r, r->state, category, input));
    }
    void locale_modes(Runtime *r, unsigned fail, unsigned initfail, unsigned readers)
    {
        r->fail = fail;
        r->init_fail = initfail;
        r->state.read_active = readers;
    }
    void locale_words(Runtime *r, unsigned *out)
    {
        auto &s = r->state;
        for (auto p : s.names)
            *out++ = r->Address(p);
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
        *out++ = s.cached_id.language;
        *out++ = s.cached_id.country;
        *out++ = s.cached_id.page;
        *out++ = s.cached_page;
    }
    void locale_cache(Runtime *r, char *out)
    {
        memcpy(out, r->state.last_input, 132);
        memcpy(out + 132, r->state.canonical, 132);
    }
    const unsigned *locale_events(Runtime *r, unsigned *size)
    {
        *size = r->events.size();
        return r->events.data();
    }
    unsigned char *locale_arena(Runtime *r, unsigned *size)
    {
        *size = r->next;
        return r->arena;
    }
}
