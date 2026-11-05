// SPDX-License-Identifier: GPL-2.0-or-later
// Public KERNEL32 LCMapStringA. The validation and route decisions are in
// win98_lcmap.h; the case mapping itself is the already implemented
// route in win98_character_case.h, the general route is in
// win98_lcmap_general.h. The SBCS sort/removal routes use the
// original code-page weights through win98_lcmap_sort.h.
#include "win98_lcmap.h"
#include "win98_lcmap_sort.h"
#include "win98_lcmap_general.h"
#include "win98_character_case.h"
namespace X86 {
unsigned Application::LcMapStringApi(const unsigned *a)
{
    const unsigned locale = a[0], flags = a[1], source = a[2], destination = a[4];
    const int count = int(a[3]), capacity = int(a[5]);
    if (!Win98LcMap::Arguments(source, count, destination, capacity))
    {
        last_error = Win98LcMap::InvalidParameter;
        return 0;
    }
    if (source == destination && !Win98LcMap::InPlaceAllowed(Win98LcMap::Normalized(flags)))
    {
        last_error = Win98LcMap::InvalidFlags;
        return 0;
    }
    if (const unsigned error = Win98LcMap::CheckFlags(flags))
    {
        last_error = error;
        return 0;
    }
    const auto route = Win98LcMap::Select(flags);
    if (!InitializeNls())
        return 0;
    struct Guest : CharacterTypeEnvironment
    {
        explicit Guest(Application &a) : CharacterTypeEnvironment(a) {}
        unsigned LocaleNumber(unsigned) { return app.nls_system->locale.number; }
        unsigned CaseByte(unsigned descriptor, bool lower, unsigned c)
        {
            const auto *t = Table(descriptor);
            if (!t) { app.Fail("Native case mapping locale table missing"); return c; }
            return t->data[t->Offset(lower ? 7 : 6) + (c & 255)];
        }
        unsigned CaseWord(unsigned descriptor, bool lower, unsigned index)
        {
            const auto *t = Table(descriptor);
            return t ? t->Half(t->Offset(lower ? 7 : 6) + index * 2) : 0;
        }
        bool Lead(unsigned descriptor, unsigned c) { return LocaleLead(descriptor, c); }
        void Write8(unsigned address, unsigned value)
        {
            const unsigned char byte = (unsigned char)value;
            app.Store(address, &byte, 1);
        }
    } e(*this);
    if (route == Win98LcMap::LowercaseRoute || route == Win98LcMap::UppercaseRoute)
    {
        const unsigned result = Win98CharacterCase::KernelCase(
            e, locale, route == Win98LcMap::LowercaseRoute, source, unsigned(count), destination,
            unsigned(capacity));
        return Status() == Failed ? 0 : result;
    }
    // resolves the locale to its descriptor before the route table,
    // as the case route does in KernelCase; a negative count becomes the
    // terminated length plus one.
    const unsigned system = e.SystemLocale();
    const unsigned resolved = Win98SystemLocale::ResolveLocale(locale, system, [&] { return e.UserLocale(); });
    const unsigned descriptor = resolved == system ? e.SystemTable()
                                : (resolved & 0xfff00000u) ? 0 : e.LookupLocale(resolved);
    if (!descriptor)
    {
        last_error = Win98LcMap::InvalidParameter;
        return 0;
    }
    const unsigned length = count < 0 ? e.Length(source) + 1 : unsigned(count);
    if (route == Win98LcMap::SortKeyRoute || route == Win98LcMap::RemovalRoute)
    {
        struct Sort : Guest
        {
            explicit Sort(Application &app) : Guest(app) {}
            unsigned Weight(unsigned d,unsigned c) { const auto *t=Table(d);const unsigned p=t->Offset(13)+c*4;
                return t->Half(p)|(t->Half(p+2)<<16); }
            unsigned Expansion(unsigned d,unsigned i,unsigned part) {const auto *t=Table(d);return t->Half(t->Offset(14)+i*4+part*2);}
            unsigned Type(unsigned d,unsigned c) {return TypeWord(d,1,c);}
            unsigned Unaccent(unsigned d,unsigned c) {const auto *t=Table(d);return t->data[t->Offset(12)+c];}
            bool Backward(unsigned) {return false;}
            void Unsupported(const char *what) {app.Fail(what);}
        } sort(*this);
        const auto *table=sort.Table(descriptor);
        if(!table || nls_system->locale.page!=1252 || nls_system->locale.number!=0x409) {
            Fail("LCMapStringA sort/removal locale provider is not installed");return 0;
        }
        const unsigned result=route==Win98LcMap::SortKeyRoute
            ? Win98LcMapSort::Sort(sort,descriptor,Win98LcMap::Normalized(flags),source,length,destination,unsigned(capacity))
            : Win98LcMapSort::Remove(sort,descriptor,Win98LcMap::Normalized(flags),source,length,destination,unsigned(capacity));
        return Status()==Failed?0:result;
    }
    struct General
    {
        Application &app;
        Guest &e;
        unsigned locale;
        bool HasTable(unsigned descriptor, unsigned index)
        {
            const auto *t = e.Table(descriptor);
            return t && t->Offset(index);
        }
        unsigned TableWord(unsigned descriptor, unsigned index, unsigned entry)
        {
            const auto *t = e.Table(descriptor);
            return t ? t->Half(t->Offset(index) + entry * 2) : 0;
        }
        bool Multibyte(unsigned descriptor) { return e.Multibyte(descriptor); }
        unsigned CodePage(unsigned) { return app.nls_system->locale.page; }
        unsigned Read8(unsigned address) { return e.Read8(address); }
        void Write8(unsigned address, unsigned value) { e.Write8(address, value); }
        void Error(unsigned value) { app.last_error = value; }
        void Unported(const char *what) { app.Fail(what); }
        unsigned Scratch(unsigned bytes) { return app.Allocate(bytes ? bytes : 1); }
        void FreeScratch(unsigned address) { app.Free(address); }
        void Case(bool lower, unsigned from, unsigned bytes, unsigned to, unsigned room)
        {
            Win98CharacterCase::KernelCase(e, locale, lower, from, bytes, to, room);
        }
    } general{*this, e, locale};
    const unsigned result = Win98LcMapGeneral::Map(general, descriptor, Win98LcMap::Normalized(flags), source,
                                                   length, destination, unsigned(capacity));
    return Status() == Failed ? 0 : result;
}
}
