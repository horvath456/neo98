// SPDX-License-Identifier: GPL-2.0-or-later
// The CryptoAPI of ADVAPI32: everything the original decides before a
// cryptographic service provider is involved.
// Windows 98 keeps the whole CryptoAPI in ADVAPI32 and none of the algorithms.
// Every entry either reads the registry or hands the work to the provider named
// there. This port loads no provider (PLAN.md step 5)
// and the imported registry carries no Cryptography subtree, so every entry
// here takes the exit the original takes on a machine without an installed CSP.
// Three object shapes carry the whole family. ADVAPI32 gives the caller a
// pointer into its own heap and recognises it again by a signature at a fixed
// offset: 11111111h at +74h for a context (validator ), 22222222h at
// +30h for a key, 33333333h at +2Ch for a hash. A pointer
// without the signature is answered with SetLastError(87) and FALSE, and so is
// a pointer that faults on the read, because all three validators run inside a
// __try whose handler falls into the same exit. That is the answer of every
// entry that takes one of the three, because this port hands none of them out.
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
// Offset and value of the three signatures, and the error the original reports
// for a block that does not carry one.
enum
{
    CryptoContextOffset = 0x74,
    CryptoKeyOffset = 0x30,
    CryptoHashOffset = 0x2c,
    CryptoContextSignature = 0x11111111,
    CryptoKeySignature = 0x22222222,
    CryptoHashSignature = 0x33333333,
};
// The key the provider registration lives under. CryptAcquireContextA,
// CryptGetDefaultProviderA and CryptEnumProviderTypesA build the type key by
// appending three decimal digits; the original writes eight digits with the
// converter and then starts reading five characters in, which is
// why a type above 999 never gets here.
static const char ProviderKey[] = "SOFTWARE\\Microsoft\\Cryptography\\Defaults\\Provider";
static const char ProviderTypesKey[] =
    "SOFTWARE\\Microsoft\\Cryptography\\Defaults\\Provider Types";
static const char UserProvidersKey[] = "SOFTWARE\\Microsoft\\Cryptography\\Providers";
// Windows error numbers and NTE codes this family answers with.
enum
{
    ErrorInvalidParameter = 87,
    ErrorNotEnoughMemory = 8,
    ErrorMoreData = 234,
    NteBadFlags = 0x80090009u,
    NteBadProvType = 0x80090014u,
    NteProvTypeNotDef = 0x80090017u,
    NteKeysetNotDef = 0x80090019u,
    NteFail = 0x80090020u,
};
// One key of the registry, read only, by root and path. Missing returns the
// number the caller decides on; the node is the port's own store index.
bool Application::CryptoRegistryKey(unsigned root, const char *path, unsigned &node)
{
    unsigned anchor = 0;
    if (registry.Key(root, anchor))
        return false;
    bool created = false;
    return registry.store.Open(anchor, path, false, node, created) == 0;
}
// The answer of every entry that takes a context, a key or a hash. A block that
// carries the signature can only come from a provider, so the run stops instead
// of calling through the function table the original finds in that block.
unsigned Application::CryptoObjectEntry(ApiName name, const unsigned *a, unsigned offset,
                                        unsigned signature)
{
    const unsigned object = a[0];
    // A block with the signature came from CryptAcquireContextA and its
    // provider (src/x86_crypto_provider.cpp).
    if (object && virtual_memory.Access(object + offset, 4, false) &&
        Read32(object + offset) == signature)
        return CryptoProviderCall(name, a);
    last_error = ErrorInvalidParameter;
    return 0;
}
// The provider name of a registered type, for CryptGetDefaultProviderA. The
// original reads the value Name of the type key twice: once for the length,
// once into the caller's buffer.
unsigned Application::AdvapiCryptoApi(ApiName name, const unsigned *a)
{
    // A key of HKLM or HKCU, opened the way the original opens it: read access
    // only. False means the key is not there.
    auto opened = [&](unsigned root, const char *path) {
        const unsigned block = Allocate(272);
        if (!block)
            return false;
        if (!Store(block, path, unsigned(strlen(path)) + 1)) { Free(block); return false; }
        const unsigned arguments[] = {root, block, 0, 0x20019, block + 268};
        const bool found = Api("RegOpenKeyExA", arguments) == 0;
        if (found)
        {
            const unsigned close[] = {Read32(block + 268)};
            Api("RegCloseKey", close);
        }
        Free(block);
        return found;
    };
    switch (name.id)
    {
    // The original decides entirely out of the registry before it
    // ever loads a provider: the provider type has to be between 1 and 999, a call without a provider name looks for
    // `SOFTWARE\Microsoft\Cryptography\Providers\Type <n>` under HKCU and then
    // for `...\Defaults\Provider Types\Type <n>` under HKLM and answers
    // NTE_PROV_TYPE_NOT_DEF when neither is there, and a call with a
    // name opens `...\Defaults\Provider\<name>` under HKLM and answers
    // NTE_KEYSET_NOT_DEF when that key is missing. Only after those
    // keys does it read Image Path and Signature, load the provider and check
    // its RSA signature, and that part has no provider here.
    API_CASE("CryptAcquireContextA")
    {
        if (!a[3] || a[3] > 999)
        {
            last_error = NteBadProvType;
            return 0;
        }
        const char *provider = a[2] ? ReadString(a[2]) : nullptr;
        char path[260];
        if (provider && *provider)
        {
            snprintf(path, sizeof path, "%s\\%s", ProviderKey, provider);
            if (!opened(0x80000002u, path))
            {
                last_error = NteKeysetNotDef;
                return 0;
            }
        }
        else
        {
            snprintf(path, sizeof path, "%s\\Type %03u", UserProvidersKey, a[3]);
            const bool user = opened(0x80000001u, path);
            snprintf(path, sizeof path, "%s\\Type %03u", ProviderTypesKey, a[3]);
            if (!user && !opened(0x80000002u, path))
            {
                last_error = NteProvTypeNotDef;
                return 0;
            }
        }
        // The registry names a provider: check its image, load it and ask it
        // for a context (src/x86_crypto_provider.cpp).
        if (!a[0] || !virtual_memory.Access(a[0], 4, true))
        {
            last_error = ErrorInvalidParameter;
            return 0;
        }
        return CryptoAcquire(a);
    }
    // The two reserved arguments have to be zero before the context
    // is looked at at all (the shared error-87 exit).
    API_CASE("CryptContextAddRef")
        if (a[1] || a[2])
        {
            last_error = ErrorInvalidParameter;
            return 0;
        }
        return CryptoObjectEntry(name, a, CryptoContextOffset, CryptoContextSignature);
    // The entries that take a context. CryptReleaseContext additionally wants a
    // reference count above zero at +7Ch and CryptSetProvParam gets
    // here only for a parameter that reaches the provider.
    API_CASE("CryptCreateHash")
    API_CASE("CryptDeriveKey")
    API_CASE("CryptGenKey")
    API_CASE("CryptGenRandom")
    API_CASE("CryptGetProvParam")
    API_CASE("CryptGetUserKey")
    API_CASE("CryptImportKey")
    API_CASE("CryptReleaseContext")
        return CryptoObjectEntry(name, a, CryptoContextOffset, CryptoContextSignature);
    // The entries that take a key.
    API_CASE("CryptDecrypt")
    API_CASE("CryptDestroyKey")
    API_CASE("CryptDuplicateKey")
    API_CASE("CryptEncrypt")
    API_CASE("CryptExportKey")
    API_CASE("CryptGetKeyParam")
    API_CASE("CryptSetKeyParam")
        return CryptoObjectEntry(name, a, CryptoKeyOffset, CryptoKeySignature);
    // The entries that take a hash. CryptSignHashA and CryptVerifySignatureA
    // convert their ANSI description to wide characters first and
    // only then reach the hash validator; both steps end in
    // the same answer for a hash this port cannot have handed out.
    API_CASE("CryptDestroyHash")
    API_CASE("CryptDuplicateHash")
    API_CASE("CryptGetHashParam")
    API_CASE("CryptHashData")
    API_CASE("CryptHashSessionKey")
    API_CASE("CryptSetHashParam")
    API_CASE("CryptSignHashA")
    API_CASE("CryptVerifySignatureA")
        return CryptoObjectEntry(name, a, CryptoHashOffset, CryptoHashSignature);
        // Two parameters never reach a provider, and for those two the
    // original ignores hProv completely. PP_CLIENT_HWND (1) keeps the window
    // handle in a global that the FuncReturnhWnd callback of the
    // VTableProvStruc hands to the provider later, and parameter 11
    // keeps a copy of a counted block that CryptAcquireContextA passes on as
    // pbContextInfo. Both answer TRUE. Everything else needs the
    // context.
    API_CASE("CryptSetProvParam")
    {
        if (a[1] == 1)
        {
            if (!a[2] || !virtual_memory.Access(a[2], 4, false))
            {
                last_error = ErrorInvalidParameter;
                return 0;
            }
            crypto_client_window = Read32(a[2]);
            return 1;
        }
        if (a[1] == 11)
        {
            if (!a[2] || !virtual_memory.Access(a[2], 8, false))
            {
                last_error = ErrorInvalidParameter;
                return 0;
            }
            const unsigned size = Read32(a[2]), source = Read32(a[2] + 4);
            // Windows 98 keeps the copy in its own heap (LocalAlloc) and reports ERROR_NOT_ENOUGH_MEMORY when that fails.
            // The copy lives in the guest heap here, so it goes away with the
            // process like the original's does.
            const unsigned block = size ? Allocate(size) : 0;
            if (size && !block)
            {
                last_error = ErrorNotEnoughMemory;
                return 0;
            }
            unsigned char chunk[256];
            for (unsigned done = 0; done < size;)
            {
                const unsigned part =
                    size - done < sizeof chunk ? size - done : unsigned(sizeof chunk);
                if (!Read(source + done, chunk, part) || !Store(block + done, chunk, part))
                {
                    Free(block);
                    last_error = ErrorInvalidParameter;
                    return 0;
                }
                done += part;
            }
            if (crypto_context_info)
                Free(crypto_context_info);
            crypto_context_info = block;
            crypto_context_info_size = size;
            return 1;
        }
        return CryptoObjectEntry(name, a, CryptoContextOffset, CryptoContextSignature);
    }
    // A provider type outside 1 to 999 and a missing name are 87.
    // Otherwise the entry creates `Providers\Type <n>` under HKCU and writes
    // the name into the value Name. That is registry work this port can do, and
    // it is the one CryptoAPI entry that changes something without a provider:
    // the next CryptAcquireContextA for that type finds the key.
    API_CASE("CryptSetProviderA")
    {
        if (!a[1] || a[1] > 999 || !a[0])
        {
            last_error = ErrorInvalidParameter;
            return 0;
        }
        const char *provider = ReadString(a[0]);
        if (!provider)
        {
            last_error = ErrorInvalidParameter;
            return 0;
        }
        char path[260];
        snprintf(path, sizeof path, "%s\\Type %03u", UserProvidersKey, a[1]);
        unsigned node = 0;
        bool created = false;
        unsigned anchor = 0, result = registry.Key(0x80000001u, anchor);
        if (!result)
            result = registry.store.Open(anchor, path, true, node, created);
        if (!result)
            result = registry.store.Set(node, "Name", 1, provider, unsigned(strlen(provider)) + 1);
        if (result)
        {
            last_error = result;
            return 0;
        }
        return 1;
    }
    // The extended form takes name and type plus a reserved pointer
    // and flags, and before it writes anything it insists that the name is a
    // registered provider: HKLM `Defaults\Provider\<name>` has to open
    // and its value Type has to match dwProvType, else
    // the answer is 87. Without a Cryptography subtree no name gets that far.
    API_CASE("CryptSetProviderExA")
    {
        if (!a[1] || a[1] > 999 || !a[0] || a[2])
        {
            last_error = ErrorInvalidParameter;
            return 0;
        }
        if ((a[3] & ~7u) || ((a[3] & 1) && (a[3] & 2)))
        {
            last_error = NteBadFlags;
            return 0;
        }
        const char *provider = ReadString(a[0]);
        if (!provider)
        {
            last_error = ErrorInvalidParameter;
            return 0;
        }
        char path[260];
        snprintf(path, sizeof path, "%s\\%s", ProviderKey, provider);
        unsigned node = 0;
        const Registry::Value *type = nullptr;
        if (!CryptoRegistryKey(0x80000002u, path, node) || registry.store.Query(node, "Type", type) ||
            !type->data || type->size < 4 || (type->data[0] | type->data[1] << 8 | type->data[2] << 16 |
                                               (unsigned)type->data[3] << 24) != a[1])
        {
            last_error = ErrorInvalidParameter;
            return 0;
        }
        // CRYPT_USER_DEFAULT writes HKCU
        // Providers\Type nnn, otherwise HKLM Defaults\Provider Types\Type nnn;
        // the key is created, then CRYPT_DELETE_DEFAULT deletes it again and
        // everything else sets its value Name to the provider.
        const bool user = (a[3] & 2) != 0;
        snprintf(path, sizeof path, "%s\\Type %03u", user ? UserProvidersKey : ProviderTypesKey, a[1]);
        unsigned anchor = 0;
        bool created = false;
        if (registry.Key(user ? 0x80000001u : 0x80000002u, anchor) ||
            registry.store.Open(anchor, path, true, node, created))
        {
            last_error = ErrorInvalidParameter;
            return 0;
        }
        const unsigned result = (a[3] & 4) ? registry.store.DeleteKey(anchor, path)
                                           : registry.store.Set(node, "Name", 1, provider,
                                                                unsigned(strlen(provider)) + 1);
        if (result)
        {
            last_error = result;
            return 0;
        }
        return 1;
    }
    // The default provider of a type: the type key under HKLM for
    // CRYPT_MACHINE_DEFAULT and under HKCU for CRYPT_USER_DEFAULT, then the
    // value Name. A missing key or value is 87, a
    // buffer that is too small is ERROR_MORE_DATA, and the size reaches the
    // caller on every path, including the rejected arguments: Windows 98 writes
    // it after the error exits too, which is zero when nothing was read.
    API_CASE("CryptGetDefaultProviderA")
    {
        unsigned size = 0, error = 0;
        const bool machine = (a[2] & 1) != 0;
        char path[260];
        const Registry::Value *value = nullptr;
        unsigned node = 0;
        if (!a[0] || a[0] > 999 || a[1])
            error = ErrorInvalidParameter;
        else if ((a[2] & ~3u) || ((a[2] & 1) && (a[2] & 2)))
            error = NteBadFlags;
        else
        {
            snprintf(path, sizeof path, "%s\\Type %03u",
                     machine ? ProviderTypesKey : UserProvidersKey, a[0]);
            if (!CryptoRegistryKey(machine ? 0x80000002u : 0x80000001u, path, node) ||
                registry.store.Query(node, "Name", value) || !value->data)
                error = ErrorInvalidParameter;
            else
                size = value->size;
        }
        // The size goes back before anything else, exactly where the original
        // writes it. An unwritable pointer is the fault its __try turns into 87.
        if (!a[4] || !virtual_memory.Access(a[4], 4, true))
        {
            last_error = ErrorInvalidParameter;
            return 0;
        }
        const unsigned capacity = Read32(a[4]);
        Write32(a[4], size);
        if (error)
        {
            last_error = error;
            return 0;
        }
        if (!a[3])
            return 1;
        if (capacity < size)
        {
            last_error = ErrorMoreData;
            return 0;
        }
        if (!Store(a[3], value->data, size))
        {
            last_error = ErrorInvalidParameter;
            return 0;
        }
        return 1;
    }
    // Both walk a key of HKLM with RegEnumKeyEx and
    // answer NTE_FAIL when the anchor is missing. CryptEnumProvidersA
    // returns the key name as provider name and its value Type; the name
    // buffer is optional, a short one is ERROR_MORE_DATA with the size.
    // CryptEnumProviderTypesA wants key names "Type nnn" (NTE_FAIL for any
    // other or for type 0), returns the number and the value TypeName, and
    // writes the size back on every exit.
    API_CASE("CryptEnumProvidersA")
    API_CASE("CryptEnumProviderTypesA")
    {
        const bool types = API_IS(name, "CryptEnumProviderTypesA");
        const unsigned index = a[0], type_out = a[3], out = a[4], size_out = a[5];
        if (a[1])
        {
            last_error = ErrorInvalidParameter;
            return 0;
        }
        if (a[2])
        {
            last_error = NteBadFlags;
            return 0;
        }
        unsigned node = 0, child = 0;
        if (!CryptoRegistryKey(0x80000002u, types ? ProviderTypesKey : ProviderKey, node))
        {
            last_error = NteFail;
            return 0;
        }
        if (unsigned error = registry.store.EnumKey(node, index, child))
        {
            last_error = error;                          // ERROR_NO_MORE_ITEMS,
            return 0;
        }
        const char *key = registry.store.nodes[child].name;
        const bool writable = virtual_memory.Access(type_out, 4, true) && virtual_memory.Access(size_out, 4, true);
        if (!writable)
        {
            last_error = ErrorInvalidParameter;          // the __try of both
            return 0;
        }
        if (!types)
        {
            const Registry::Value *type = nullptr;
            if (registry.store.Query(child, "Type", type) || !type->data || type->size < 4)
            {
                last_error = NteFail;
                return 0;
            }
            Write32(type_out, type->data[0] | type->data[1] << 8 | type->data[2] << 16 |
                                  (unsigned)type->data[3] << 24);
            const unsigned size = unsigned(strlen(key)) + 1;
            if (out)
            {
                if (Read32(size_out) < size)
                {
                    Write32(size_out, size);
                    last_error = ErrorMoreData;
                    return 0;
                }
                if (!Store(out, key, size))
                {
                    last_error = ErrorInvalidParameter;
                    return 0;
                }
            }
            Write32(size_out, size);
            return 1;
        }
        if (strncmp(key, "Type ", 5))
        {
            last_error = NteFail;
            Write32(size_out, 0);
            return 0;
        }
        unsigned number = 0;
        for (const char *d = key + 5; *d; ++d) number = number * 10 + unsigned(*d - '0');   //
        if (!number)
        {
            last_error = NteFail;
            Write32(size_out, 0);
            return 0;
        }
        Write32(type_out, number);
        const Registry::Value *type_name = nullptr;
        if (registry.store.Query(child, "TypeName", type_name) || !type_name->data)
        {
            Write32(size_out, 0);                        // no TypeName: success with nothing
            return 1;
        }
        const unsigned size = type_name->size;
        if (out)
        {
            if (Read32(size_out) < size)
            {
                Write32(size_out, size);
                last_error = ErrorMoreData;
                return 0;
            }
            if (!Store(out, type_name->data, size))
            {
                Write32(size_out, size);
                last_error = ErrorInvalidParameter;
                return 0;
            }
        }
        Write32(size_out, size);
        return 1;
    }
    default:
        break;
    }
    Fail(name);
    return 0;
}
} // namespace X86
