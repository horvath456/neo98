// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "../build/generated/nls_tables.h"
#endif
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "../build/generated/nls_unicode.h"
#endif
#include "win98_nls.h"
#include "win98_character_type.h"
#include "win98_user_characters.h"
#include "win98_nls_composite.h"
#include "win98_nls_decompose.h"
#include "win98_crt_locale.h"
#include "win98_wstrings.h"
#include "x86_app.h"
#include <string.h>
namespace X86
{
unsigned Application::WideStringApi(ApiName name, const unsigned *a)
{
    struct Words
    {
        Application &app;
        bool Read16(unsigned address, unsigned &value)
        {
            if (!app.virtual_memory.Access(address, 2, false))
                return false;
            unsigned short word = 0;
            if (!app.Read(address, &word, sizeof word))
                return false;
            value = word;
            return true;
        }
        bool Write16(unsigned address, unsigned value)
        {
            if (!app.virtual_memory.Access(address, 2, true))
                return false;
            const unsigned short word = value;
            return app.Store(address, &word, sizeof word);
        }
        void Error(unsigned value) { app.last_error = value; }
    } e{*this};
    if (API_IS(name,"lstrlenW"))
        return Win98WStrings::Length(e, a[0]);
    if (API_IS(name,"lstrcpyW"))
        return Win98WStrings::Copy(e, a[0], a[1]);
    if (API_IS(name,"lstrcatW"))
        return Win98WStrings::Cat(e, a[0], a[1]);
    return Win98WStrings::UnsupportedCopyN(e, a[0], a[1], a[2]);
}
struct Application::NlsEnvironment final : Win98Nls::Environment
{
    Application &app;
    explicit NlsEnvironment(Application &a) : app(a) {}
    bool Read(unsigned p, void *out, unsigned n) override { return app.Read(p, out, n); }
    bool Write(unsigned p, const void *data, unsigned n) override { return app.Store(p, data, n); }
    unsigned Resize(unsigned, unsigned, unsigned) override { return 0; }
    bool Free(unsigned, unsigned) override { return false; }
    int Compare(unsigned, unsigned, unsigned, unsigned) override { return 0; }
    unsigned Ansi() override { return app.system_locale.ansi; }
    unsigned Oem() override { return app.system_locale.oem; }
    const Win98NlsComposite::UnicodeTable *Unicode() override
    {
        static Win98NlsComposite::UnicodeTable table;
        static bool loaded = false;
        if (!loaded)
        {
            if (!table.Load(OriginalUnicodeComposition, sizeof OriginalUnicodeComposition))
            {
                app.Fail("Invalid original Unicode composition data");
                return nullptr;
            }
            loaded = true;
        }
        return &table;
    }
    const Win98NlsDecompose::UnicodeTable *Decomposition() override
    {
        static Win98NlsDecompose::UnicodeTable table;
        static bool loaded = false;
        if (!loaded)
        {
            if (!table.Load(OriginalUnicodeComposition, sizeof OriginalUnicodeComposition))
            {
                app.Fail("Invalid original Unicode decomposition data");
                return nullptr;
            }
            loaded = true;
        }
        return &table;
    }
    void Error(unsigned code) override { app.last_error = code; }
    void Unsupported(const char *text) override { app.Fail(text); }
    const Win98Nls::Table *CodePage(unsigned page) override
    {
        static const Win98Nls::Table tables[] = {{OriginalCodePage1252, sizeof OriginalCodePage1252},
                                                 {OriginalCodePage437, sizeof OriginalCodePage437},
                                                 {OriginalCodePage850, sizeof OriginalCodePage850},
                                                 {OriginalCodePage866, sizeof OriginalCodePage866}};
        unsigned index = page == 1252 ? 0 : page == 437 ? 1 : page == 850 ? 2 : page == 866 ? 3 : 4;
        if (index == 4)
            return nullptr;
        if (!tables[index].Valid())
        {
            app.Fail("Invalid original Win98 NLS table");
            return nullptr;
        }
        return &tables[index];
    }
};
unsigned Application::NlsApi(unsigned ordinal, const unsigned *args)
{
    if (!InitializeNls())
        return 0;
    // The public wrappers probe before the conversion, under the SEH frame
    // whose handler Windows 98 answers zero with error 87: MultiByteToWideChar
    // reads the first source byte, WideCharToMultiByte
    // the first source character, the default character and the used-default
    // flag. SHLWAPI's ShellExecuteExWrapW converts lpClass of SHDOCVW's
    // SHELLEXECUTEINFOW without looking at fMask, and that field is stack
    // garbage when Active Setup starts a stub.
    if (ordinal == 594 || ordinal == 811)
    {
        const bool wide = ordinal == 811;
        if (!virtual_memory.Access(args[2], wide ? 2 : 1, false) ||
            (wide && args[6] && !virtual_memory.Access(args[6], 1, false)) ||
            (wide && args[7] && !virtual_memory.Access(args[7], 4, false)))
        {
            last_error = 87;
            return 0;
        }
    }
    NlsEnvironment e(*this);
    return Win98Nls::Call(e, ordinal, args);
}
struct Application::NlsSystem
{
    Application &app;
    unsigned lock = 0;
    bool entered = false;
    struct Page
    {
        unsigned handle = 0, number = 0;
        const Win98Nls::Table *data = nullptr;
    } pages[4];
    struct Locale
    {
        unsigned handle = 0, number = 0, page = 0, page_handle = 0;
        const char *language = nullptr, *country = nullptr;
    } locale;
    explicit NlsSystem(Application &a) : app(a) {}
    // KERNEL32 runs the NLS startup once, when it initializes, and keeps the
    // heap (created with HEAP_SHARED), the table records and the chosen code
    // pages in its shared data; every later process finds them there. NEO98
    // runs a KERNEL32 per process, so the session keeps the one result here.
    // Starting it again for every process left one shared heap per process
    // in the session, the shared heaps outlived their processes while
    // EXPLORER held them, and after about 90 processes (the Active Setup
    // stubs of a first start) HeapCreate had no session block left.
    static bool session_ready;
    static Win98SystemLocale::State session_state;
    static Page session_pages[4];
    static Locale session_locale;
    unsigned ReadRegistry(unsigned root, const char *path, const char *name, char *&text,
                          unsigned capacity, unsigned &allocated)
    {
        allocated = 0;
        if (root < 0x80000000u || root > 0x80000006u || !capacity || !text)
            return 87;
        unsigned node = 0;
        bool created = false;
        auto &registry = app.registry.store;
        auto error = registry.Open(registry.roots[root - 0x80000000u], path, false, node, created);
        if (error)
            return error;
        const Registry::Value *value = nullptr;
        error = registry.Query(node, name, value);
        if (error)
            return error;
        if (value->type != 1)
            return 13;
        // Bounded RAM-registry provider. The original registry helper's
        // overflow-buffer allocation and Windows hive IO remain separate.
        if (value->size >= capacity)
            return 234;
        if (value->size)
            memcpy(text, value->data, value->size);
        text[value->size] = 0;
        return 0;
    }
    static bool Equal(const unsigned char *bytes, unsigned size, const char *text)
    {
        if (strlen(text) != size)
            return false;
        for (unsigned i = 0; i < size; ++i)
        {
            unsigned a = bytes[i], b = (unsigned char)text[i];
            if (a >= 'a' && a <= 'z')
                a -= 32;
            if (b >= 'a' && b <= 'z')
                b -= 32;
            if (a != b)
                return false;
        }
        return true;
    }
    unsigned ReadIni(const char *section, const char *key, const char *fallback, char *text,
                     unsigned capacity, const char *file)
    {
        if (!capacity || !text)
            return 0;
        unsigned length = strlen(fallback);
        if (length >= capacity)
            length = capacity - 1;
        memcpy(text, fallback, length);
        text[length] = 0;
        if (strcmp(file, "system.ini"))
            return length;
        unsigned id = 0;
        if (app.files.store.Lookup("C:\\WINDOWS\\SYSTEM.INI", id))
            return length;
        const auto *node = app.files.store.Get(id);
        const unsigned char *bytes = node ? app.files.store.Bytes(id) : nullptr;
        if (!node || !bytes || (node->attributes & 16))
            return length;
        // Native bounded section/key reader for NLS startup. It reads the
        // actual RAM-file bytes; general Win32 profile-file behavior is unported.
        bool inside = false;
        for (unsigned cursor = 0; cursor < node->size;)
        {
            unsigned first = cursor;
            while (cursor < node->size && node->data[cursor] != '\n')
                ++cursor;
            unsigned end = cursor;
            if (cursor < node->size)
                ++cursor;
            while (first < end && (node->data[first] == ' ' || node->data[first] == '\t'))
                ++first;
            while (end > first && (node->data[end - 1] == ' ' || node->data[end - 1] == '\t' ||
                                   node->data[end - 1] == '\r'))
                --end;
            if (first == end || node->data[first] == ';')
                continue;
            if (node->data[first] == '[')
            {
                inside = end - first >= 2 && node->data[end - 1] == ']' &&
                         Equal(node->data + first + 1, end - first - 2, section);
                continue;
            }
            if (!inside)
                continue;
            unsigned equal = first;
            while (equal < end && node->data[equal] != '=')
                ++equal;
            if (equal == end)
                continue;
            unsigned key_end = equal;
            while (key_end > first && (node->data[key_end - 1] == ' ' || node->data[key_end - 1] == '\t'))
                --key_end;
            if (!Equal(node->data + first, key_end - first, key))
                continue;
            first = equal + 1;
            while (first < end && (node->data[first] == ' ' || node->data[first] == '\t'))
                ++first;
            length = end - first;
            if (length >= capacity)
                length = capacity - 1;
            memcpy(text, node->data + first, length);
            text[length] = 0;
            return length;
        }
        return length;
    }
    void Free(unsigned, unsigned, char *) { app.Fail("Unexpected allocated NLS registry buffer"); }
    unsigned CreateHeap(unsigned flags, unsigned initial, unsigned maximum)
    {
        if (app.state == Failed)
            return 0;
        const unsigned args[] = {flags, initial, maximum};
        return app.HeapApi("HeapCreate", args);
    }
    unsigned Allocate(unsigned heap, unsigned flags, unsigned bytes)
    {
        const unsigned args[] = {heap, flags, bytes};
        return app.HeapApi("HeapAlloc", args);
    }
    unsigned GlobalAllocate(unsigned flags, unsigned bytes)
    {
        // The native fixed-block global-allocation provider uses the same RAM
        // arena as the current public GlobalAlloc(flags=0) adapter.
        return flags ? 0 : app.Allocate(bytes);
    }
    void PublishLists()
    {
        const auto &state = app.system_locale;
        if (state.tableStorage)
        {
            app.Write32(state.tableStorage, state.codepageBuckets);
            app.Write32(state.tableStorage + 4, state.localeBuckets);
        }
    }
    unsigned LoadCodePage(unsigned page, unsigned &out)
    {
        PublishLists();
        NlsEnvironment data(app);
        const auto *table = data.CodePage(page);
        if (!table)
            return 2;
        for (auto &record : pages)
            if (record.handle && record.number == page)
            {
                out = record.handle;
                return 0;
            }
        for (auto &record : pages)
            if (!record.handle)
            {
                const unsigned handle = Allocate(app.system_locale.heap, 8, 84);
                if (!handle)
                    return 14;
                record = {handle, page, table};
                // Native descriptor metadata. The bytes remain in the imported
                // immutable table; the host pointer stays in this typed record.
                app.Write32(handle, page);
                app.Write32(handle + 4, table->size);
                out = handle;
                return 0;
            }
        return 14;
    }
    unsigned LoadLocale(unsigned lcid, unsigned &out, bool create)
    {
        if (lcid != 0x409)
            return 2;
        const char *id = Win98Locale::SystemInfo(lcid, 1);
        const char *cp = Win98Locale::SystemInfo(lcid, 0x1004);
        const char *language = Win98Locale::SystemInfo(lcid, 0x1001);
        const char *country = Win98Locale::SystemInfo(lcid, 0x1002);
        if (!id || !cp || !language || !country || Win98SystemLocale::ParseHex(id) != lcid)
            return 2;
        const unsigned page = Win98SystemLocale::ParseDecimal(cp);
        NlsEnvironment data(app);
        if (!data.CodePage(page))
            return 2;
        if (!locale.handle)
        {
            if (!create)
                return 2;
            const unsigned handle = Allocate(app.system_locale.heap, 8, 68);
            if (!handle)
                return 14;
            locale = {handle, lcid, page, 0, language, country};
            app.Write32(handle, lcid);
            app.Write32(handle + 4, page);
        }
        out = locale.handle;
        return 0;
    }
    unsigned LocaleCodePage(unsigned descriptor)
    {
        return locale.handle == descriptor ? locale.page : 0;
    }
    unsigned BuildLocale(unsigned lcid, unsigned page, unsigned &descriptor, bool create)
    {
        if (create || descriptor != locale.handle || lcid != locale.number || page != locale.page)
            return 2;
        const unsigned error = LoadCodePage(page, locale.page_handle);
        if (!error)
            app.Write32(descriptor + 8, locale.page_handle);
        return error;
    }
    unsigned InitializeUnicode()
    {
        NlsEnvironment data(app);
        return data.Unicode() && data.Decomposition() ? 0 : 2;
    }
    unsigned InitializeOverrides(unsigned pointer, unsigned)
    {
        // The allocated structure remains invalid until the original user
        // override loader is ported. No registry override fields are fabricated.
        app.Write32(pointer + 0xb40, 0);
        return 0;
    }
    void Error(unsigned value) { app.last_error = value; }
    void BootstrapLock() { app.CrtLock(true); }
    void BootstrapUnlock() { app.CrtLock(false); }
    void InitializeLock()
    {
        lock = app.Allocate(8);
        if (lock)
        {
            const unsigned args[] = {lock};
            app.CriticalApi("InitializeCriticalSection", args);
        }
        else
            app.Fail("NLS critical-section storage allocation failed");
    }
    void EnterLock()
    {
        if (lock && app.state != Failed)
        {
            const unsigned args[] = {lock};
            app.CriticalApi("EnterCriticalSection", args);
            entered = app.state != Failed;
        }
    }
    void LeaveLock()
    {
        if (entered)
        {
            entered = false;
            const unsigned args[] = {lock};
            app.CriticalApi("LeaveCriticalSection", args);
        }
    }
};
bool Application::NlsSystem::session_ready = false;
Win98SystemLocale::State Application::NlsSystem::session_state;
Application::NlsSystem::Page Application::NlsSystem::session_pages[4];
Application::NlsSystem::Locale Application::NlsSystem::session_locale;
void Application::ForgetSessionNls(unsigned heap)
{
    if (NlsSystem::session_ready && NlsSystem::session_state.heap == heap)
        NlsSystem::session_ready = false;
}
bool Application::InitializeNls()
{
    if (system_locale.initialized)
        return state != Failed;
    if (nls_system)
        return false;
    nls_system = new NlsSystem(*this);
    if (nls_system && NlsSystem::session_ready && AdoptSessionHeap(NlsSystem::session_state.heap))
    {
        // The startup of the session already ran; this process uses its
        // heap and records, as a Win98 process uses KERNEL32's.
        system_locale = NlsSystem::session_state;
        for (unsigned i = 0; i < 4; ++i)
            nls_system->pages[i] = NlsSystem::session_pages[i];
        nls_system->locale = NlsSystem::session_locale;
        CrtLock(true);
        nls_system->InitializeLock();
        CrtLock(false);
        default_locale.user = system_locale.user;
        return state != Failed;
    }
    if (!nls_system || !Win98SystemLocale::Start(*nls_system, system_locale) || state == Failed)
    {
        Fail("Original NLS startup could not initialize the configured tables");
        return false;
    }
    NlsSystem::session_ready = true;
    NlsSystem::session_state = system_locale;
    for (unsigned i = 0; i < 4; ++i)
        NlsSystem::session_pages[i] = nls_system->pages[i];
    NlsSystem::session_locale = nls_system->locale;
    default_locale.user = system_locale.user;
    return true;
}
void Application::FreeNls()
{
    // Native metadata only. Its actual heap blocks and critical section are
    // owned by the existing application-wide heap/critical teardown routines.
    delete nls_system;
    nls_system = nullptr;
}
unsigned Application::UserDefaultLocale()
{
    if (!InitializeNls())
        return 0;
    struct Environment
    {
        NlsSystem &system;
        unsigned ReadUserLocale(char *&text, unsigned capacity, unsigned &allocated)
        {
            return system.ReadRegistry(0x80000001, Win98SystemLocale::UserPath(), "Locale", text, capacity,
                                       allocated);
        }
        unsigned ParseHex(const char *text) { return Win98SystemLocale::ParseHex(text); }
        void FreeLocale(char *text) { system.Free(system.app.system_locale.heap, 0, text); }
        unsigned SystemLocale() { return Win98SystemLocale::GetSystem(system.app.system_locale); }
    } e{*nls_system};
    default_locale.user = system_locale.user;
    const unsigned result = Win98DefaultLocale::GetUser(e, default_locale);
    system_locale.user = default_locale.user;
    return result;
}
unsigned Application::DefaultLocaleApi(ApiName name)
{
    if (!InitializeNls())
        return 0;
    switch (name.id)
    {
    API_CASE("GetUserDefaultLCID")
        return UserDefaultLocale();
    API_CASE("GetUserDefaultLangID")
        return UserDefaultLocale() & 0xffff;
    API_CASE("GetSystemDefaultLCID")
        return Win98SystemLocale::GetSystem(system_locale);
    API_CASE("GetSystemDefaultLangID")
        return Win98SystemLocale::GetSystemLang(system_locale);
    API_CASE("GetThreadLocale")
        return Win98SystemLocale::GetThread(system_locale);
    API_CASE("InvalidateNLSCache")
    {
        const unsigned result = Win98SystemLocale::Invalidate(system_locale);
        default_locale.user = system_locale.user;
        return result;
    }
    default:
        break;
    }
    Fail("Unknown default locale API");
    return 0;
}
unsigned Application::AnsiWide(unsigned byte)
{
    NlsEnvironment e(*this);
    const auto *table = e.CodePage(e.Ansi());
    return table ? table->Wide(byte) : 0;
}
unsigned Application::AnsiNarrow(unsigned character)
{
    NlsEnvironment e(*this);
    const auto *table = e.CodePage(e.Ansi());
    return table ? table->Narrow(character) : '?';
}
struct Application::CharacterTypeEnvironment {
    Application &app;
    NlsEnvironment tables;
    bool scalar = false;
    unsigned byte = 0, result = 0;
    explicit CharacterTypeEnvironment(Application &a) : app(a), tables(a) {}
    unsigned SystemLocale() { return app.system_locale.system; }
    unsigned UserLocale() { return app.UserDefaultLocale(); }
    unsigned SystemTable() { return app.system_locale.systemTable; }
    unsigned LookupLocale(unsigned lcid) {
        unsigned result = 0;
        return app.nls_system->LoadLocale(lcid, result, false) ? 0 : result;
    }
    const Win98Nls::Table *Table(unsigned descriptor) {
        const auto &locale = app.nls_system->locale;
        return locale.handle == descriptor ? tables.CodePage(locale.page) : nullptr;
    }
    unsigned Read8(unsigned address) {
        if (scalar) return byte;
        unsigned char value = 0; app.Read(address, &value, 1); return value;
    }
    void Write16(unsigned address, unsigned value) {
        if (scalar) { result = value; return; }
        const unsigned short word = value; app.Store(address, &word, 2);
    }
    unsigned Length(unsigned source) {
        for (unsigned i = 0; i < 1048576; ++i) {
            if (!Read8(source + i) || app.state == Failed) return i;
        }
        app.Fail("Native NLS character-type string limit exceeded"); return 0;
    }
    unsigned TypeWord(unsigned descriptor, unsigned type, unsigned index) {
        const auto *table = Table(descriptor);
        if (!table) { app.Fail("Missing native locale character-type table"); return 0; }
        return table->Half(table->Offset(type == 1 ? 15 : type == 2 ? 16 : 17) + index * 2);
    }
    // The shared locale loader currently installs the original US/SBCS locale.
    // The implemented generic DBCS core can use a future locale-table provider.
    bool Multibyte(unsigned) { return false; }
    bool LocaleLead(unsigned, unsigned) { return false; }
    bool SystemLead(unsigned byte) {
        const auto *table = tables.CodePage(app.system_locale.ansi);
        return table && table->Offset(4) && table->Half(table->Offset(4) + byte * 2);
    }
    void Error(unsigned value) { app.last_error = value; }
};
unsigned Application::StringTypeApi(const unsigned *a)
{
    // Original public wrappers probe the first input byte and output WORD.
    // Native page rights replace the x86 SEH frame; the query's writes stay ordered.
    if (!a[2] || !a[4] || !virtual_memory.Access(a[2], 1, false) ||
        !virtual_memory.Access(a[4], 2, true) || (int(a[3]) > 1048576))
    {
        last_error = 87;
        return 0;
    }
    if (!InitializeNls()) return 0;
    CharacterTypeEnvironment e(*this);
    return Win98CharacterType::Query(e, a[0], a[1], a[2], a[3], a[4]);
}
unsigned Application::CharacterApi(unsigned operation, const unsigned *a)
{
    struct E {
        Application &app;
        bool Type(unsigned byte, unsigned &result) {
            if (!app.InitializeNls()) return false;
            CharacterTypeEnvironment type(app);
            type.scalar = true; type.byte = byte;
            // Scalar native storage replaces the original temporary stack byte/WORD.
            // The same original NLS query and installed locale table remain in use.
            const unsigned good = Win98CharacterType::Query(type, 0x800, 1, 1, 1, 2);
            result = type.result;
            return good;
        }
        bool BadRead(unsigned p) { return !p || !app.virtual_memory.Access(p, 1, false); }
        unsigned Read8(unsigned p) { unsigned char c = 0; app.Read(p, &c, 1); return c; }
        bool Lead(unsigned page, unsigned byte) {
            const unsigned args[] = {page, byte};
            return app.NlsApi(538, args);
        }
        void Error(unsigned error) { app.last_error = error; }
    } e{*this};
    if (operation < 4) return Win98UserCharacters::Predicate(e, operation, a[0]);
    if (operation == 4) return Win98UserCharacters::Next(e, a[0], false, 0);
    if (operation == 5) return Win98UserCharacters::Previous(e, a[0], a[1], false, 0);
    if (operation == 6) return Win98UserCharacters::Next(e, a[1], true, a[0]);
    return Win98UserCharacters::Previous(e, a[1], a[2], true, a[0]);
}
} // namespace X86

#include "x86_character_case.inc"

#include "x86_lcmap.inc"

#include "x86_compare_string.inc"

#include "x86_computer_name.inc"
