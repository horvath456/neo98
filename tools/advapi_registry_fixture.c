/* SPDX-License-Identifier: GPL-2.0-or-later
 * ADVAPI32 RegConnectRegistryA and RegNotifyChangeKeyValue over the public API. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateEventA(void *, U, U, const char *);
API U WaitForSingleObject(U, U);
API U ResetEvent(U);
API U CloseHandle(U);
API U CreateThread(void *, U, void *, void *, U, U *);
API void Sleep(U);
API U RegConnectRegistryA(const char *, U, U *);
API U RegNotifyChangeKeyValue(U, U, U, U, U);
API U RegCreateKeyExA(U, const char *, U, const char *, U, U, U, U *, U *);
API U RegOpenKeyA(U, const char *, U *);
API U RegCloseKey(U);
API U RegSetValueExA(U, const char *, U, U, const void *, U);
API U RegDeleteValueA(U, const char *);
API U RegDeleteKeyA(U, const char *);
API U RegQueryValueExA(U, const char *, U *, U *, void *, U *);
API U CryptAcquireContextA(U *, const char *, const char *, U, U);
API U CryptReleaseContext(U, U);
API U CryptContextAddRef(U, U *, U);
API U CryptDestroyHash(U);
API U CryptDestroyKey(U);
API U CryptGenRandom(U, U, void *);
API U CryptGetProvParam(U, U, void *, U *, U);
API U CryptSetProvParam(U, U, const void *, U);
API U CryptHashData(U, const void *, U, U);
API U CryptSignHashA(U, U, const char *, U, void *, U *);
API U CryptEnumProvidersA(U, U *, U, U *, char *, U *);
API U CryptEnumProviderTypesA(U, U *, U, U *, char *, U *);
API U CryptGetDefaultProviderA(U, U *, U, char *, U *);
API U CryptSetProviderA(const char *, U);
API U CryptSetProviderExA(const char *, U, U *, U);
API U GetLastError(void);
API void SetLastError(U);
#define NAME 1u
#define LAST_SET 4u
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static U branch = 0;
/* The synchronous form blocks until the key changes, so a second thread makes
 * the change the waiting main thread is meant to */
static U __stdcall writer(void *argument)
{
    U key = *(U *)argument, value = 4711;
    Sleep(5);
    return RegSetValueExA(key, "Late", 0, 4, &value, 4);
}
void run_advapi_registry_test(void)
{
    U key = 0;
    /* RegConnectRegistryA. Only the six predefined roots are keys here. */
    check(RegConnectRegistryA(0, 0x80000002, &key) == 0 && key == 0x80000002, 401);
    check(RegConnectRegistryA("", 0x80000001, &key) == 0 && key == 0x80000001, 402);
    check(RegConnectRegistryA(0, 0x80000007, &key) == 6, 403);
    check(RegConnectRegistryA(0, 0x40000000, &key) == 6, 404);
    check(RegConnectRegistryA(0, 0x80000002, 0) == 87, 405);
    check(RegConnectRegistryA("\\\\OTHER", 0x80000002, &key) == 1114, 406);

    U root = 0, child = 0, created = 0;
    check(!RegCreateKeyExA(0x80000001, "Software\\NEO98Notify", 0, 0, 0, 0xf003f, 0, &root, &created), 410);
    check(!RegCreateKeyExA(root, "Child", 0, 0, 0, 0xf003f, 0, &child, &created), 411);
    U event = CreateEventA(0, 1, 0, 0);
    check(event != 0, 412);

    /* A value below the watched key reaches a subtree watch, not a flat one. */
    U number = 1;
    check(!RegNotifyChangeKeyValue(root, 0, NAME | LAST_SET, event, 1), 413);
    check(WaitForSingleObject(event, 0) == 258, 414);
    check(!RegSetValueExA(child, "Number", 0, 4, &number, 4), 415);
    check(WaitForSingleObject(event, 0) == 258, 416);
    check(!RegSetValueExA(root, "Number", 0, 4, &number, 4), 417);
    check(WaitForSingleObject(event, 0) == 0, 418);

    /* One registration fires once. */
    check(ResetEvent(event), 419);
    number = 2;
    check(!RegSetValueExA(root, "Number", 0, 4, &number, 4), 420);
    check(WaitForSingleObject(event, 0) == 258, 421);

    check(!RegNotifyChangeKeyValue(root, 1, NAME | LAST_SET, event, 1), 422);
    check(!RegSetValueExA(child, "Number", 0, 4, &number, 4), 423);
    check(WaitForSingleObject(event, 0) == 0, 424);

    /* The filter selects the kind of change. A value is no name change. */
    check(ResetEvent(event), 425);
    check(!RegNotifyChangeKeyValue(root, 1, NAME, event, 1), 426);
    check(!RegSetValueExA(child, "Number", 0, 4, &number, 4), 427);
    check(WaitForSingleObject(event, 0) == 258, 428);
    U second = 0;
    check(!RegCreateKeyExA(root, "Other", 0, 0, 0, 0xf003f, 0, &second, &created) && created == 1, 429);
    check(WaitForSingleObject(event, 0) == 0, 430);

    /* A deleted value is a LAST_SET change, a deleted key a name change. */
    check(ResetEvent(event), 431);
    check(!RegNotifyChangeKeyValue(root, 1, LAST_SET, event, 1), 432);
    check(!RegDeleteValueA(child, "Number"), 433);
    check(WaitForSingleObject(event, 0) == 0, 434);
    check(ResetEvent(event), 435);
    check(!RegNotifyChangeKeyValue(root, 1, NAME, event, 1), 436);
    check(!RegCloseKey(second) && !RegDeleteKeyA(root, "Other"), 437);
    check(WaitForSingleObject(event, 0) == 0, 438);

    /* Rejected arguments. An invalid key answers 6, and so does an event
     * handle that names no event. */
    check(RegNotifyChangeKeyValue(0x50001234, 1, NAME, event, 1) == 6, 439);
    check(RegNotifyChangeKeyValue(root, 1, NAME, root, 1) == 6, 440);
    check(RegNotifyChangeKeyValue(0x50001234, 1, NAME, 0, 0) == 6, 441);

    /* The synchronous form returns when the other thread writes. */
    branch = child;
    U id = 0, thread = CreateThread(0, 0, (void *)writer, &branch, 0, &id);
    check(thread != 0, 442);
    check(!RegNotifyChangeKeyValue(child, 0, LAST_SET, 0, 0), 443);
    check(WaitForSingleObject(thread, 5000) == 0, 444);
    check(CloseHandle(thread), 445);

    check(!RegDeleteValueA(root, "Number") && !RegDeleteValueA(child, "Late"), 446);
    check(!RegCloseKey(child) && !RegDeleteKeyA(root, "Child"), 447);
    check(!RegCloseKey(root) && !RegDeleteKeyA(0x80000001, "Software\\NEO98Notify"), 448);
    check(CloseHandle(event), 449);

    /* The CryptoAPI entrance, ADVAPI32. It decides out of the
     * registry, which carries the Cryptography keys of the installation: type
     * 1 is the Microsoft Base Cryptographic Provider (RSA.DLL, loaded and
     * checked as original code), type 24 is not registered. A type outside
     * 1..999 never gets that far. Without a key container RSABASE answers
     * NTE_BAD_KEYSET; CRYPT_VERIFYCONTEXT needs none. */
    U context = 0x5a5a5a5a;
    SetLastError(0);
    check(!CryptAcquireContextA(&context, 0, 0, 0, 0) && GetLastError() == 0x80090014, 450);
    check(!CryptAcquireContextA(&context, 0, 0, 1000, 0) && GetLastError() == 0x80090014, 451);
    check(!CryptAcquireContextA(&context, 0, 0, 1, 0) && GetLastError() == 0x80090016, 452);
    check(!CryptAcquireContextA(&context, 0, 0, 24, 0xf0000000) && GetLastError() == 0x80090017, 453);
    check(!CryptAcquireContextA(&context, "Container", "Microsoft Base Cryptographic Provider v1.0",
                                1, 0) && GetLastError() == 0x80090016, 454);
    check(!CryptAcquireContextA(&context, 0, "", 1, 0) && GetLastError() == 0x80090016, 455);
    check(CryptAcquireContextA(&context, 0, 0, 1, 0xf0000000) && context != 0x5a5a5a5a &&
              CryptReleaseContext(context, 0), 456);

    /* Every entry that takes a context, a key or a hash recognises it by the
     * signature ADVAPI32 writes into its own block, and answers 87 for anything
     * else. This port hands none of the three out, so 87 is the answer. */
    check(!CryptReleaseContext(0, 0) && GetLastError() == 87, 457);
    check(!CryptReleaseContext(0x11111111, 0) && GetLastError() == 87, 458);
    U size = 4, value = 0;
    check(!CryptGetProvParam(0, 5, &value, &size, 0) && GetLastError() == 87, 459);
    check(!CryptGenRandom(0, 4, &value) && GetLastError() == 87, 460);
    check(!CryptDestroyKey(0x5a5a5a5a) && GetLastError() == 87, 461);
    check(!CryptDestroyHash(0x5a5a5a5a) && GetLastError() == 87, 462);
    check(!CryptHashData(0, &value, 4, 0) && GetLastError() == 87, 463);
    check(!CryptSignHashA(0, 2, 0, 0, 0, &size) && GetLastError() == 87, 464);
    /* The two reserved arguments of CryptContextAddRef come before the
     * context. */
    U reserved = 1;
    check(!CryptContextAddRef(0, &reserved, 0) && GetLastError() == 87, 465);
    check(!CryptContextAddRef(0, 0, 1) && GetLastError() == 87, 466);

    /* Two parameters of CryptSetProvParam never reach a provider, and for those
     * the original ignores hProv: the client window and the counted block of
     * parameter 11. Everything else needs the context. */
    U window = 0x1234;
    check(CryptSetProvParam(0, 1, &window, 0), 467);
    U info[2];
    info[0] = 4;
    info[1] = (U)&window;
    check(CryptSetProvParam(0, 11, info, 0), 468);
    info[0] = 0;
    check(CryptSetProvParam(0, 11, info, 0), 469);
    check(!CryptSetProvParam(0, 5, &value, 0) && GetLastError() == 87, 470);

    /* The provider registration. Reserved pointers are 87, unknown flags are
     * NTE_BAD_FLAGS. The two enumerations walk the registered providers and
     * types, key by key, and end with ERROR_NO_MORE_ITEMS. */
    U type = 0;
    char name[64];
    size = sizeof name;
    check(!CryptEnumProvidersA(0, &reserved, 0, &type, name, &size) && GetLastError() == 87, 471);
    check(!CryptEnumProvidersA(0, 0, 1, &type, name, &size) && GetLastError() == 0x80090009, 472);
    U index = 0, base = 0;
    for (;; ++index)
    {
        size = sizeof name;
        if (!CryptEnumProvidersA(index, 0, 0, &type, name, &size)) break;
        if (type == 1 && size == 43 && name[10] == 'B') base = 1;
    }
    check(base && GetLastError() == 259, 473);
    size = 4;
    check(!CryptEnumProvidersA(0, 0, 0, &type, name, &size) && GetLastError() == 234 && size > 4, 495);
    base = 0;
    for (index = 0;; ++index)
    {
        size = sizeof name;
        if (!CryptEnumProviderTypesA(index, 0, 0, &type, name, &size)) break;
        if (type == 1) base = 1;
    }
    check(base && GetLastError() == 259, 474);
    size = 99;
    check(!CryptGetDefaultProviderA(0, 0, 0, 0, &size) && GetLastError() == 87, 475);
    check(size == 0, 476);
    size = 99;
    check(!CryptGetDefaultProviderA(1, 0, 4, 0, &size) && GetLastError() == 0x80090009, 477);
    check(!CryptGetDefaultProviderA(1, 0, 3, 0, &size) && GetLastError() == 0x80090009, 478);
    check(CryptGetDefaultProviderA(1, 0, 1, 0, &size) && size == 43, 479);   /* the machine default */
    check(!CryptGetDefaultProviderA(1, 0, 2, 0, &size) && GetLastError() == 87, 480);
    check(!CryptSetProviderExA("NEO98 Provider", 1, 0, 0) && GetLastError() == 87, 481);
    check(!CryptSetProviderExA("NEO98 Provider", 1, 0, 8) && GetLastError() == 0x80090009, 482);
    check(!CryptSetProviderExA("NEO98 Provider", 1, &reserved, 0) && GetLastError() == 87, 483);
    /* A registered name with its own type becomes the user default and goes
     * away again with CRYPT_DELETE_DEFAULT; a wrong type is 87. */
    check(!CryptSetProviderExA("Microsoft Base Cryptographic Provider v1.0", 24, 0, 2) &&
              GetLastError() == 87, 496);
    check(CryptSetProviderExA("Microsoft Base Cryptographic Provider v1.0", 1, 0, 2), 497);
    size = sizeof name;
    check(CryptGetDefaultProviderA(1, 0, 2, name, &size) && size == 43, 498);
    check(CryptSetProviderExA("Microsoft Base Cryptographic Provider v1.0", 1, 0, 6), 499);
    size = sizeof name;
    check(!CryptGetDefaultProviderA(1, 0, 2, name, &size) && GetLastError() == 87, 500);
    check(!CryptSetProviderA(0, 1) && GetLastError() == 87, 484);
    check(!CryptSetProviderA("NEO98 Provider", 0) && GetLastError() == 87, 485);
    check(!CryptSetProviderA("NEO98 Provider", 1000) && GetLastError() == 87, 486);

    /* CryptSetProviderA is the one entry of the family that works without a
     * provider: it writes the name into HKCU Providers\Type 099, and the value
     * is there afterwards. The key goes away again so that the next
     * CryptAcquireContextA for that type finds nothing, as it did above. */
    check(CryptSetProviderA("NEO98 Provider", 99), 487);
    U written = 0;
    check(!RegOpenKeyA(0x80000001, "SOFTWARE\\Microsoft\\Cryptography\\Providers\\Type 099",
                       &written), 488);
    size = sizeof name;
    type = 0;
    check(!RegQueryValueExA(written, "Name", 0, &type, name, &size), 489);
    check(type == 1 && size == 15 && name[0] == 'A' && name[13] == 'r' && !name[14], 490);
    size = sizeof name;
    check(CryptGetDefaultProviderA(99, 0, 2, name, &size) && size == 15, 491);
    check(!RegCloseKey(written), 492);
    check(!RegDeleteKeyA(0x80000001, "SOFTWARE\\Microsoft\\Cryptography\\Providers\\Type 099"), 493);
    SetLastError(0);
    check(!CryptAcquireContextA(&context, 0, 0, 99, 0) && GetLastError() == 0x80090017, 494);
}
#ifdef ADVAPI_REGISTRY_STANDALONE
void mainCRTStartup(void)
{
    run_advapi_registry_test();
    ExitProcess(0);
}
#endif
