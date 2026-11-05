// SPDX-License-Identifier: GPL-2.0-or-later
// Guest ABI/data publication for the native Win98 CRT locale implementation.
#include "win98_crt_locale.h"
#include "win98_crt_time_format.h"
#include "win98_nls.h"
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "../build/generated/nls_tables.h"
#endif
#include "x86_app.h"
#include "win98_datetime_format.h"
#include "win98_datetime_formats.h"
#include "win98_kernel_nls_names.h"
#include <string.h>
#include <stdio.h>
namespace X86
{
struct Application::CrtLocale final : Win98Locale::Environment
{
    Application &app;
    Win98Locale::State locale;
    Win98Locale::SystemData data;
    unsigned guest = 0;
    static constexpr unsigned CType = 64, WideType = 580, AllName = 1104, CategoryNames = 1956, Conv = 2624,
                              Strings = 2688, Classic = 4096, Bytes = 8192;
    explicit CrtLocale(Application &a) : app(a) {}
    ~CrtLocale()
    {
        for (auto *name : locale.names)
            if (name != locale.c_locale)
                delete[] name;
    }
    char *Allocate(unsigned bytes) override { return new char[bytes]; }
    void Free(char *p) override { delete[] p; }
    void Lock(unsigned id) override { app.CrtLock(true, id); }
    void Unlock(unsigned id) override { app.CrtLock(false, id); }
    void Sleep(unsigned ms) override
    {
        const unsigned args[] = {ms};
        app.ThreadApi("Sleep", args);
    }
    bool Qualify(Win98Locale::Parts &parts, Win98Locale::Id &id) override
    {
        return Win98Locale::QualifySystem(parts, id);
    }
    int Initialize(unsigned category, Win98Locale::State &s) override { return data.Initialize(category, s); }
    unsigned Name(unsigned category) const
    {
        return guest + (category ? CategoryNames + (category - 1) * 132 : AllName);
    }
    bool Publish()
    {
        if (!guest)
            guest = app.Allocate(Bytes);
        if (!guest)
            return false;
        app.Write32(guest, guest + CType + 2); // _pctype points after the EOF slot.
        app.Write32(guest + 4, guest + WideType + 2);
        app.Write32(guest + 8, data.mb_max);
        app.Store(guest + 16, locale.handles, sizeof locale.handles);
        app.Write32(guest + 40, locale.code_page);
        app.Write32(guest + 44, locale.collate_page);
        app.Store(guest + CType, data.ctype, sizeof data.ctype);
        app.Store(guest + WideType, data.wide_type, sizeof data.wide_type);
        app.Store(guest + Classic, Win98Locale::ClassicType(), sizeof data.ctype);
        for (unsigned i = 0; i < 6; ++i)
            app.PutString(Name(i), locale.names[i] ? locale.names[i] : "", i ? 132 : 0x351);
        unsigned strings[10];
        for (unsigned i = 0; i < 10; ++i)
        {
            strings[i] = guest + Strings + i * 128;
            const char *text = i < 3 ? data.numeric[i] : data.monetary[i - 3];
            app.Store(strings[i], text, 128);
        }
        app.Store(guest + Conv, strings, sizeof strings);
        app.Store(guest + Conv + 40, data.monetary_flags, sizeof data.monetary_flags);
        return app.state != Failed;
    }
};
#include "x86_crt_time_format.inc"

void Application::FreeLocale()
{
    delete crt_locale;
    crt_locale = nullptr;
}
unsigned Application::LocaleCodePage() const { return crt_locale ? crt_locale->locale.code_page : 0; }
unsigned Application::LocaleApi(ApiName name, const unsigned *a)
{
    if (!crt_locale)
        crt_locale = new CrtLocale(*this);
    if (!crt_locale)
        return 0;
    auto &e = *crt_locale;
    // These value-returning calls do not need any guest-side locale buffers.
    // They must continue to work when the guest heap cannot publish pointers.
    if (API_IS(name,"_isctype"))
        return Win98Locale::IsCType(e.data, a[0], a[1]);
    if (API_IS(name,"toupper") || API_IS(name,"tolower"))
        return Win98Locale::Case(e.locale, e.data, a[0], API_IS(name,"toupper"));
    if (!e.guest && !e.Publish())
        return 0;
    switch (name.id)
    {
    API_CASE("setlocale")
    {
        if (a[0] > 5)
            return 0;
        // The lock can suspend this fiber. Keep input separate from ReadString's
        // reusable application buffers while another thread runs.
        char input[4096];
        if (a[1])
        {
            const char *source = ReadString(a[1]);
            if (state == Failed)
                return 0;
            const auto length = strlen(source);
            if (length >= sizeof input)
                return 0;
            memcpy(input, source, length + 1);
        }
        char *result = Win98Locale::Set(e, e.locale, static_cast<int>(a[0]), a[1] ? input : nullptr);
        if (!e.Publish())
            return 0;
        if (!result)
            return 0;
        for (unsigned i = 0; i < 6; ++i)
            if (result == e.locale.names[i])
                return e.Name(i);
        Fail("Unpublished CRT locale result");
        return 0;
    }
    API_CASE("localeconv")
        return e.guest + CrtLocale::Conv;
    API_CASE("__p__pctype")
        return e.guest;
    API_CASE("__p__pwctype")
        return e.guest + 4;
    API_CASE("__p___mb_cur_max")
        return e.guest + 8;
    default:
        break;
    }
    Fail(name);
    return 0;
}
unsigned Application::LocaleExport(const char *name)
{
    unsigned offset;
    if (!strcmp(name, "_pctype"))
        offset = 0;
    else if (!strcmp(name, "_pwctype"))
        offset = 4;
    else if (!strcmp(name, "__mb_cur_max"))
        offset = 8;
    else if (!strcmp(name, "__lc_handle"))
        offset = 16;
    else if (!strcmp(name, "__lc_codepage"))
        offset = 40;
    else if (!strcmp(name, "__lc_collate_cp"))
        offset = 44;
    else if (!strcmp(name, "_ctype"))
        offset = CrtLocale::Classic;
    else
        return 0;
    LocaleApi("localeconv", nullptr);
    return crt_locale && crt_locale->guest ? crt_locale->guest + offset : 0;
}
unsigned Application::LocaleInfo(const unsigned *a)
{
    if (!InitializeNls())
        return 0;
    const unsigned lcid = Win98SystemLocale::ResolveLocale(a[0], system_locale.system,
                                                          [this] { return UserDefaultLocale(); });
    // SystemInfo's legacy default aliases belong to its CRT profile provider.
    // The public API must look up only the LCID resolved by the original prefix.
    const char *value = LocaleValue(lcid, a[1]);
    if (!value || a[3] > 0x7fffffff || (a[3] && !a[2]))
    {
        last_error = 87;
        return 0;
    }
    unsigned number = 0;
    const bool numeric = a[1] & 0x20000000;
    const unsigned type = a[1] & 0x1fffffff;
    // Original GetLocaleInfoA field-selection branches set the conversion radix
    // in [EBP-0Ch]; Windows 98 rejects RETURN_NUMBER for every other field.
    unsigned radix = 0;
    if (type == 1 || type == 9)
        radix = 16;
    else if (type == 5 || type == 0xa || type == 0xb || type == 0xd || type == 0x11 || type == 0x12 ||
             (type >= 0x19 && type <= 0x1c) || (type >= 0x21 && type <= 0x27) ||
             (type >= 0x52 && type <= 0x57) || type == 0x1004 || type == 0x1005 ||
             (type >= 0x1009 && type <= 0x100d) || (type >= 0x1010 && type <= 0x1012))
        radix = 10;
    if (numeric && !radix)
    {
        last_error = 1004;
        return 0;
    }
    const unsigned n = numeric ? sizeof number : strlen(value) + 1;
    if (!a[3])
        return n;
    if (a[3] < n)
    {
        // The ANSI copy keeps exactly cchData bytes on overflow, without a
        // terminator. RETURN_NUMBER instead leaves the buffer untouched.
        if(!numeric&&!Store(a[2],value,a[3]))return 0;
        last_error = 122;
        return 0;
    }
    if (numeric)
    {
        for (const char *p = value; *p; ++p)
        {
            const unsigned digit = *p >= '0' && *p <= '9'   ? *p - '0'
                                   : *p >= 'a' && *p <= 'f' ? *p - 'a' + 10
                                   : *p >= 'A' && *p <= 'F' ? *p - 'A' + 10
                                                            : 255;
            if (digit >= radix || number > (~0u - digit) / radix)
            {
                last_error = 87;
                return 0;
            }
            number = number * radix + digit;
        }
        return Store(a[2], &number, sizeof number) ? n : 0;
    }
    return Store(a[2], value, n) ? n : 0;
}
#include "x86_nls_extended.inc"
} // namespace X86
#include "x86_number_format.inc"

#include "x86_datetime_format.inc"
