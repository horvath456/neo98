// SPDX-License-Identifier: GPL-2.0-or-later
// CryptoAPI provider dispatch. Load and validate the selected provider, resolve
// entry points, and manage provider-owned handles and references.
#include "x86_app.h"
#include "win98_csp_image.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
enum : unsigned
{
    ContextBytes = 0x80, KeyBytes = 0x38, HashBytes = 0x34,
    CtxProvider = 0x70, CtxSignature = 0x74, CtxUse = 0x78, CtxReferences = 0x7c, CtxModule = 0x6c,
    CtxDuplicateKey = 0x60, CtxDuplicateHash = 0x64,
    KeyContext = 0x28, KeyHandle = 0x2c, KeySignature = 0x30, KeyUse = 0x34,
    HashContext = 0x24, HashHandle = 0x28, HashSignature = 0x2c, HashUse = 0x30,
    ContextMagic = 0x11111111, KeyMagic = 0x22222222, HashMagic = 0x33333333,
    NteBadSignature = 0x80090006u, NteProviderDllFail = 0x8009001du, NteProvTypeNoMatch = 0x8009001bu,
    NteBadKeyset = 0x80090016u, NteProviderFail = 0x8009001eu, NteFailRead = 0x8009001cu,
    NteKeysetEntryBad = 0x8009001au,
};
// The CP entries in the order of the name table.
const char *const Entries[] = {"CPAcquireContext", "CPReleaseContext", "CPGenKey", "CPDeriveKey",
                               "CPDestroyKey", "CPSetKeyParam", "CPGetKeyParam", "CPExportKey",
                               "CPImportKey", "CPEncrypt", "CPDecrypt", "CPCreateHash", "CPHashData",
                               "CPHashSessionKey", "CPDestroyHash", "CPSignHash", "CPVerifySignature",
                               "CPGenRandom", "CPGetUserKey", "CPSetProvParam", "CPGetProvParam",
                               "CPSetHashParam", "CPGetHashParam"};
enum Cp : unsigned
{
    CpAcquire, CpRelease, CpGenKey, CpDeriveKey, CpDestroyKey, CpSetKeyParam, CpGetKeyParam, CpExportKey,
    CpImportKey, CpEncrypt, CpDecrypt, CpCreateHash, CpHashData, CpHashSessionKey, CpDestroyHash, CpSignHash,
    CpVerifySignature, CpGenRandom, CpGetUserKey, CpSetProvParam, CpGetProvParam, CpSetHashParam,
    CpGetHashParam
};
const char ProviderKey[] = "SOFTWARE\\Microsoft\\Cryptography\\Defaults\\Provider";
const char ProviderTypesKey[] = "SOFTWARE\\Microsoft\\Cryptography\\Defaults\\Provider Types";
const char UserProvidersKey[] = "SOFTWARE\\Microsoft\\Cryptography\\Providers";
} // namespace

// the image named by `path` against the signature bytes.
unsigned Application::CryptoVerifyImage(const char *path, const unsigned char *signature, unsigned size)
{
    // The two keys come out of the installed ADVAPI32.DLL.
    static unsigned char keys[2][Win98CspImage::KeyBytes];
    static bool keys_read = false;
    auto read_file = [&](const char *name, unsigned &bytes) -> unsigned {
        const unsigned text = String(name);
        if (!text) return 0;
        const unsigned open[] = {text, 0x80000000u, 1, 0, 3, 0x80, 0};
        const unsigned file = Api("CreateFileA", open);
        Free(text);
        if (file == ~0u) return 0;
        const unsigned size_args[] = {file, 0};
        bytes = Api("GetFileSize", size_args);
        const unsigned buffer = bytes != ~0u ? Allocate(bytes + 1) : 0;
        const unsigned count = Allocate(4);
        bool ok = buffer && count;
        if (ok)
        {
            const unsigned read[] = {file, buffer, bytes, count, 0};
            ok = Api("ReadFile", read) && Read32(count) == bytes;
        }
        const unsigned close[] = {file};
        Api("CloseHandle", close);
        if (count) Free(count);
        if (!ok) { if (buffer) Free(buffer); return 0; }
        return buffer;
    };
    if (!keys_read)
    {
        unsigned bytes = 0;
        const unsigned image = read_file("C:\\WINDOWS\\SYSTEM\\ADVAPI32.DLL", bytes);
        if (!image || bytes < Win98CspImage::KeyOffset2 + Win98CspImage::KeyBytes) return NteFailRead;
        Read(image + Win98CspImage::KeyOffset1, keys[0], Win98CspImage::KeyBytes);
        Read(image + Win98CspImage::KeyOffset2, keys[1], Win98CspImage::KeyBytes);
        Free(image);
        keys_read = true;
    }
    // OpenFile searches the path as the original's does.
    char full[260];
    {
        const unsigned name = String(path), out = Allocate(260), part = Allocate(4);
        const unsigned search[] = {0, name, 0, 260, out, part};
        const bool found = name && out && part && Api("SearchPathA", search);
        if (found) strncpy(full, ReadString(out), sizeof full - 1), full[sizeof full - 1] = 0;
        if (name) Free(name);
        if (out) Free(out);
        if (part) Free(part);
        if (!found) return NteProviderFail;
    }
    unsigned bytes = 0;
    const unsigned image = read_file(full, bytes);
    if (!image) return NteFailRead;
    Win98CspImage::Md5 md5;
    unsigned char chunk[4096];
    for (unsigned done = 0; done < bytes;)
    {
        const unsigned part = bytes - done < sizeof chunk ? bytes - done : (unsigned)sizeof chunk;
        Read(image + done, chunk, part);
        md5.Update(chunk, part);
        done += part;
    }
    Free(image);
    unsigned char digest[16];
    md5.Final(digest);
    if (Win98CspImage::Verify(keys[0], 0, signature, size, digest) ||
        Win98CspImage::Verify(keys[1], 2, signature, size, digest))
        return 0;
    return NteBadSignature;
}

// FuncVerifyImage of the VTableProvStruc : a provider asks for the
// check of another image with its signature.
unsigned Application::CryptoVerifyImageGate(const unsigned *a)
{
    if (!a[0] || !a[1] || !virtual_memory.Access(a[1], 0x80, false)) return 0;
    unsigned char signature[0x88] = {};
    Read(a[1], signature, 0x80);
    return CryptoVerifyImage(ReadString(a[0]), signature, 0x80) == 0;
}
// FuncReturnhWnd : the window CryptSetProvParam(PP_CLIENT_HWND) kept.
unsigned Application::CryptoReturnWindowGate(const unsigned *a)
{
    if (a[0] && virtual_memory.Access(a[0], 4, true)) Write32(a[0], crypto_client_window);
    return 0;
}

// after the registry checks of src/x86_advapi_crypto.cpp.
unsigned Application::CryptoAcquire(const unsigned *a)
{
    const char *given = a[2] ? ReadString(a[2]) : nullptr;
    char name[260] = {};
    const Registry::Value *value = nullptr;
    unsigned node = 0;
    char path[400];
    if (given && *given)
        strncpy(name, given, sizeof name - 1);
    else
    {
        snprintf(path, sizeof path, "%s\\Type %03u", UserProvidersKey, a[3]);
        bool found = CryptoRegistryKey(0x80000001u, path, node) && !registry.store.Query(node, "Name", value) &&
                     value->data;
        if (!found)
        {
            snprintf(path, sizeof path, "%s\\Type %03u", ProviderTypesKey, a[3]);
            found = CryptoRegistryKey(0x80000002u, path, node) && !registry.store.Query(node, "Name", value) &&
                    value->data;
        }
        if (!found) { last_error = NteKeysetEntryBad; return 0; }
        strncpy(name, (const char *)value->data, value->size < sizeof name - 1 ? value->size : sizeof name - 1);
    }
    snprintf(path, sizeof path, "%s\\%s", ProviderKey, name);
    if (!CryptoRegistryKey(0x80000002u, path, node)) { last_error = 0x80090019u; return 0; }
    // The type of the named provider has to be the one asked for.
    if (!registry.store.Query(node, "Type", value) && value->data && value->size >= 4)
    {
        unsigned type = 0;
        memcpy(&type, value->data, 4);
        if (type != a[3]) { last_error = NteProvTypeNoMatch; return 0; }
    }
    char image[260] = {};
    if (registry.store.Query(node, "Image Path", value) || !value->data) { last_error = NteKeysetEntryBad; return 0; }
    strncpy(image, (const char *)value->data, value->size < sizeof image - 1 ? value->size : sizeof image - 1);
    if (registry.store.Query(node, "Signature", value) || !value->data) { last_error = NteKeysetEntryBad; return 0; }
    unsigned char signature[0x100] = {};
    const unsigned signature_size = value->size < sizeof signature ? value->size : (unsigned)sizeof signature;
    memcpy(signature, value->data, signature_size);
    if (const unsigned error = CryptoVerifyImage(image, signature, signature_size))
    {
        last_error = error == NteBadSignature ? NteBadSignature : error;
        return 0;
    }
    const unsigned text = String(image);
    const unsigned load[] = {text};
    const unsigned module = text ? Api("LoadLibraryA", load) : 0;
    if (text) Free(text);
    if (!module) { last_error = NteProviderDllFail; return 0; }
    const unsigned local[] = {0x40, ContextBytes};
    const unsigned context = Api("LocalAlloc", local);
    if (!context) { last_error = 8; return 0; }
    auto entry = [&](const char *function) {
        const unsigned symbol = String(function);
        const unsigned lookup[] = {module, symbol};
        const unsigned address = symbol ? Api("GetProcAddress", lookup) : 0;
        if (symbol) Free(symbol);
        return address;
    };
    for (unsigned i = 0; i < 23; ++i)
    {
        const unsigned address = entry(Entries[i]);
        if (!address)
        {
            const unsigned release[] = {context};
            Api("LocalFree", release);
            const unsigned unload[] = {module};
            Api("FreeLibrary", unload);
            last_error = NteProviderDllFail;
            return 0;
        }
        Write32(context + i * 4, address);
    }
    Write32(context + CtxDuplicateKey, entry("CPDuplicateKey"));
    Write32(context + CtxDuplicateHash, entry("CPDuplicateHash"));
    Write32(context + CtxModule, module);
    // The VTableProvStruc of version 2: FuncVerifyImage, FuncReturnhWnd, the
    // type, and the context information of CryptSetProvParam(11).
    if (!crypto_verify_gate)
    {
        crypto_verify_gate = NewImport("ADVAPI32.DLL", "__NEO98_CryptVerifyImage", 2);
        crypto_window_gate = NewImport("ADVAPI32.DLL", "__NEO98_CryptReturnWindow", 1);
    }
    const unsigned table = Allocate(24), handle = Allocate(4);
    const unsigned fields[6] = {2, crypto_verify_gate, crypto_window_gate, a[3], crypto_context_info,
                                crypto_context_info_size};
    if (!table || !handle || !Store(table, fields, sizeof fields)) { last_error = 8; return 0; }
    Write32(handle, 0);
    const unsigned arguments[] = {handle, a[1], a[4], table};
    const unsigned result = Call(Read32(context), arguments, 4);
    const unsigned provider_handle = Read32(handle);
    Free(table);
    Free(handle);
    // CRYPT_DELETEKEYSET (10h) leaves no context behind.
    if (result == 1 && !(a[4] & 0x10))
    {
        Write32(context + CtxProvider, provider_handle);
        Write32(context + CtxSignature, ContextMagic);
        Write32(context + CtxUse, 1);
        Write32(context + CtxReferences, 1);
        Write32(a[0], context);
        return 1;
    }
    const unsigned error = last_error;
    const unsigned unload[] = {module};
    Api("FreeLibrary", unload);
    const unsigned release[] = {context};
    Api("LocalFree", release);
    last_error = error;
    return result;
}

// The entries that reach a provider. `a` are the caller's arguments.
unsigned Application::CryptoProviderCall(ApiName name, const unsigned *a)
{
    // the signature under a __try, then the use
    // count goes up. A block without the signature is 87.
    auto hold = [&](unsigned block, unsigned offset, unsigned magic) {
        if (!block || !virtual_memory.Access(block + offset, 8, true) || Read32(block + offset) != magic)
        {
            last_error = 87;
            return false;
        }
        Write32(block + offset + 4, Read32(block + offset + 4) + 1);
        return true;
    };
    auto drop = [&](unsigned block, unsigned offset) {
        if (block && virtual_memory.Access(block + offset + 4, 4, true))
            Write32(block + offset + 4, Read32(block + offset + 4) - 1);
    };
    auto call = [&](unsigned function, auto... values) {
        const unsigned arguments[] = {unsigned(values)...};
        if (!function) { last_error = 120; return 0u; }
        return Call(function, arguments, unsigned(sizeof...(values)));
    };
    auto alloc = [&](unsigned bytes) {
        const unsigned local[] = {0x40, bytes};
        const unsigned block = Api("LocalAlloc", local);
        if (!block) last_error = 8;
        return block;
    };
    auto release = [&](unsigned block) { const unsigned args[] = {block}; Api("LocalFree", args); };
    // a key block with the key entries of the context.
    auto new_key = [&](unsigned context) {
        const unsigned block = alloc(KeyBytes);
        if (!block) return 0u;
        for (unsigned i = 0; i < 9; ++i) Write32(block + i * 4, Read32(context + (CpGenKey + i) * 4));
        Write32(block + 0x24, Read32(context + CtxDuplicateKey));
        Write32(block + KeyContext, context);
        return block;
    };
    auto finish_key = [&](unsigned block, unsigned handle, unsigned out) {
        Write32(block + KeyHandle, handle);
        Write32(block + KeySignature, KeyMagic);
        Write32(block + KeyUse, 0);
        Write32(out, block);
    };
    // a hash block.
    auto new_hash = [&](unsigned context) {
        const unsigned block = alloc(HashBytes);
        if (!block) return 0u;
        const unsigned order[] = {CpCreateHash, CpHashData, CpHashSessionKey, CpDestroyHash, CpSignHash,
                                  CpVerifySignature, CpSetHashParam, CpGetHashParam};
        for (unsigned i = 0; i < 8; ++i) Write32(block + i * 4, Read32(context + order[i] * 4));
        Write32(block + 0x20, Read32(context + CtxDuplicateHash));
        Write32(block + HashContext, context);
        return block;
    };
    auto finish_hash = [&](unsigned block, unsigned handle, unsigned out) {
        Write32(block + HashHandle, handle);
        Write32(block + HashSignature, HashMagic);
        Write32(block + HashUse, 0);
        Write32(out, block);
    };
    const unsigned scratch = Allocate(4);
    if (!scratch) { last_error = 8; return 0; }
    struct Scratch { Application &app; unsigned address; ~Scratch() { app.Free(address); } } guard{*this, scratch};
    Write32(scratch, 0);
    // The wide description of CryptSignHashA and CryptVerifySignatureA
    // ( converts it before the hash is looked at).
    auto wide = [&](unsigned text) -> unsigned {
        if (!text) return 0;
        const unsigned length = (unsigned)strlen(ReadString(text)) + 1;
        const unsigned out = Allocate(length * 2);
        if (!out) return 0;
        const unsigned convert[] = {0, 0, text, ~0u, out, length};
        Api("MultiByteToWideChar", convert);
        return out;
    };
    unsigned result = 0;
    switch (name.id)
    {
    API_CASE("CryptReleaseContext")
    {
        // dwFlags must be zero, the reference count above zero.
        if (!hold(a[0], CtxSignature, ContextMagic)) return 0;
        const unsigned context = a[0];
        if (a[1]) { drop(context, CtxSignature); last_error = 87; return 0; }
        const unsigned references = Read32(context + CtxReferences);
        if (!references) { drop(context, CtxSignature); last_error = 87; return 0; }
        Write32(context + CtxReferences, references - 1);
        drop(context, CtxSignature);
        if (references > 1) return 1;
        result = call(Read32(context + CpRelease * 4), Read32(context + CtxProvider), a[1]);
        const unsigned module = Read32(context + CtxModule);
        Write32(context + CtxSignature, 0);
        release(context);
        const unsigned unload[] = {module};
        Api("FreeLibrary", unload);
        return result;
    }
    API_CASE("CryptContextAddRef")
    {
        if (!hold(a[0], CtxSignature, ContextMagic)) return 0;
        Write32(a[0] + CtxReferences, Read32(a[0] + CtxReferences) + 1);
        drop(a[0], CtxSignature);
        return 1;
    }
    API_CASE("CryptGenRandom")
        if (!hold(a[0], CtxSignature, ContextMagic)) return 0;
        result = call(Read32(a[0] + CpGenRandom * 4), Read32(a[0] + CtxProvider), a[1], a[2]);
        drop(a[0], CtxSignature);
        return result;
    API_CASE("CryptGetProvParam") API_CASE("CryptSetProvParam")
    {
        if (!hold(a[0], CtxSignature, ContextMagic)) return 0;
        const bool get = API_IS(name, "CryptGetProvParam");
        result = get ? call(Read32(a[0] + CpGetProvParam * 4), Read32(a[0] + CtxProvider), a[1], a[2], a[3], a[4])
                     : call(Read32(a[0] + CpSetProvParam * 4), Read32(a[0] + CtxProvider), a[1], a[2], a[3]);
        drop(a[0], CtxSignature);
        return result;
    }
    API_CASE("CryptGenKey") API_CASE("CryptGetUserKey")
    {
        if (!hold(a[0], CtxSignature, ContextMagic)) return 0;
        const unsigned context = a[0];
        const bool user = API_IS(name, "CryptGetUserKey");
        const unsigned out = user ? a[2] : a[3];
        if (const unsigned block = new_key(context))
        {
            result = user ? call(Read32(context + CpGetUserKey * 4), Read32(context + CtxProvider), a[1], scratch)
                          : call(Read32(context + CpGenKey * 4), Read32(context + CtxProvider), a[1], a[2], scratch);
            if (result) finish_key(block, Read32(scratch), out);
            else { const unsigned e = last_error; release(block); last_error = e; }
        }
        drop(context, CtxSignature);
        return result;
    }
    API_CASE("CryptImportKey")
    {
        if (!hold(a[0], CtxSignature, ContextMagic)) return 0;
        const unsigned context = a[0];
        unsigned public_key = 0;
        if (a[3] && !hold(a[3], KeySignature, KeyMagic)) { drop(context, CtxSignature); return 0; }
        if (a[3]) public_key = Read32(a[3] + KeyHandle);
        if (const unsigned block = new_key(context))
        {
            result = call(Read32(context + CpImportKey * 4),
                          Read32(context + CtxProvider), a[1], a[2], public_key, a[4], scratch);
            if (result) finish_key(block, Read32(scratch), a[5]);
            else { const unsigned e = last_error; release(block); last_error = e; }
        }
        if (a[3]) drop(a[3], KeySignature);
        drop(context, CtxSignature);
        return result;
    }
    API_CASE("CryptDeriveKey")
    {
        if (!hold(a[0], CtxSignature, ContextMagic)) return 0;
        const unsigned context = a[0];
        if (!hold(a[2], HashSignature, HashMagic)) { drop(context, CtxSignature); return 0; }
        if (const unsigned block = new_key(context))
        {
            result = call(Read32(context + CpDeriveKey * 4),
                          Read32(context + CtxProvider), a[1], Read32(a[2] + HashHandle), a[3], scratch);
            if (result) finish_key(block, Read32(scratch), a[4]);
            else { const unsigned e = last_error; release(block); last_error = e; }
        }
        drop(a[2], HashSignature);
        drop(context, CtxSignature);
        return result;
    }
    API_CASE("CryptCreateHash")
    {
        if (!hold(a[0], CtxSignature, ContextMagic)) return 0;
        const unsigned context = a[0];
        if (a[2] && !hold(a[2], KeySignature, KeyMagic)) { drop(context, CtxSignature); return 0; }
        if (const unsigned block = new_hash(context))
        {
            result = call(Read32(context + CpCreateHash * 4),
                          Read32(context + CtxProvider), a[1], a[2] ? Read32(a[2] + KeyHandle) : 0, a[3], scratch);
            if (result) finish_hash(block, Read32(scratch), a[4]);
            else { const unsigned e = last_error; release(block); last_error = e; }
        }
        if (a[2]) drop(a[2], KeySignature);
        drop(context, CtxSignature);
        return result;
    }
    default:
        break;
    }
    // The entries that take a key or a hash first.
    const bool on_key = API_IS(name, "CryptDestroyKey") || API_IS(name, "CryptSetKeyParam") ||
                        API_IS(name, "CryptGetKeyParam") || API_IS(name, "CryptExportKey") ||
                        API_IS(name, "CryptEncrypt") || API_IS(name, "CryptDecrypt") ||
                        API_IS(name, "CryptDuplicateKey");
    if (on_key)
    {
        if (!hold(a[0], KeySignature, KeyMagic)) return 0;
        const unsigned key = a[0], context = Read32(key + KeyContext);
        if (!hold(context, CtxSignature, ContextMagic)) { drop(key, KeySignature); return 0; }
        const unsigned provider = Read32(context + CtxProvider), handle = Read32(key + KeyHandle);
        unsigned hash = 0, second = 0;
        switch (name.id)
        {
        API_CASE("CryptDestroyKey")
            result = call(Read32(key + 0x08), provider, handle);
            break;
        API_CASE("CryptSetKeyParam")
            result = call(Read32(key + 0x0c), provider, handle, a[1], a[2], a[3]);
            break;
        API_CASE("CryptGetKeyParam")
            result = call(Read32(key + 0x10), provider, handle, a[1], a[2], a[3], a[4]);
            break;
        API_CASE("CryptExportKey")
            if (a[1] && !hold(a[1], KeySignature, KeyMagic)) break;
            second = a[1];
            result = call(Read32(key + 0x14), provider, handle, a[1] ? Read32(a[1] + KeyHandle) : 0, a[2], a[3], a[4], a[5]);
            break;
        API_CASE("CryptEncrypt") API_CASE("CryptDecrypt")
            if (a[1] && !hold(a[1], HashSignature, HashMagic)) break;
            hash = a[1];
            if (API_IS(name, "CryptEncrypt"))
                result = call(Read32(key + 0x1c), provider, handle, a[1] ? Read32(a[1] + HashHandle) : 0, a[2], a[3], a[4], a[5], a[6]);
            else
                result = call(Read32(key + 0x20), provider, handle, a[1] ? Read32(a[1] + HashHandle) : 0, a[2], a[3], a[4], a[5]);
            break;
        API_CASE("CryptDuplicateKey")
            if (const unsigned block = new_key(context))
            {
                result = call(Read32(key + 0x24), provider, handle, a[1], a[2], scratch);
                if (result) finish_key(block, Read32(scratch), a[3]);
                else { const unsigned e = last_error; release(block); last_error = e; }
            }
            break;
        default:
            break;
        }
        const unsigned error = last_error;
        if (hash) drop(hash, HashSignature);
        if (second) drop(second, KeySignature);
        drop(context, CtxSignature);
        drop(key, KeySignature);
        if (result && API_IS(name, "CryptDestroyKey")) { Write32(key + KeySignature, 0); release(key); }
        last_error = error;
        return result;
    }
    if (!hold(a[0], HashSignature, HashMagic)) return 0;
    const unsigned hash = a[0], context = Read32(hash + HashContext);
    if (!hold(context, CtxSignature, ContextMagic)) { drop(hash, HashSignature); return 0; }
    const unsigned provider = Read32(context + CtxProvider), handle = Read32(hash + HashHandle);
    unsigned key = 0, text = 0;
    switch (name.id)
    {
    API_CASE("CryptHashData")
        result = call(Read32(hash + 0x04), provider, handle, a[1], a[2], a[3]);
        break;
    API_CASE("CryptHashSessionKey")
        if (!hold(a[1], KeySignature, KeyMagic)) break;
        key = a[1];
        result = call(Read32(hash + 0x08), provider, handle, Read32(a[1] + KeyHandle), a[2]);
        break;
    API_CASE("CryptDestroyHash")
        result = call(Read32(hash + 0x0c), provider, handle);
        break;
    API_CASE("CryptSignHashA")
        text = wide(a[2]);
        result = call(Read32(hash + 0x10), provider, handle, a[1], text, a[3], a[4], a[5]);
        break;
    API_CASE("CryptVerifySignatureA")
        if (!hold(a[3], KeySignature, KeyMagic)) break;
        key = a[3];
        text = wide(a[4]);
        result = call(Read32(hash + 0x14), provider, handle, a[1], a[2], Read32(a[3] + KeyHandle), text, a[5]);
        break;
    API_CASE("CryptSetHashParam")
        result = call(Read32(hash + 0x18), provider, handle, a[1], a[2], a[3]);
        break;
    API_CASE("CryptGetHashParam")
        result = call(Read32(hash + 0x1c), provider, handle, a[1], a[2], a[3], a[4]);
        break;
    API_CASE("CryptDuplicateHash")
        if (const unsigned block = new_hash(context))
        {
            result = call(Read32(hash + 0x20), provider, handle, a[1], a[2], scratch);
            if (result) finish_hash(block, Read32(scratch), a[3]);
            else { const unsigned e = last_error; release(block); last_error = e; }
        }
        break;
    default:
    {
        char message[120];
        snprintf(message, sizeof message, "%s has no provider path", name.text);
        Fail(message);
        return 0;
    }
    }
    const unsigned error = last_error;
    if (text) Free(text);
    if (key) drop(key, KeySignature);
    drop(context, CtxSignature);
    drop(hash, HashSignature);
    if (result && API_IS(name, "CryptDestroyHash")) { Write32(hash + HashSignature, 0); release(hash); }
    last_error = error;
    return result;
}
} // namespace X86
