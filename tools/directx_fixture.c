/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest side of tools/test_directx_guest.py: the public 32-bit entries of the
   original DDRAW.DLL, DCIMAN32.DLL and DSOUND.DLL, all three of which reach
   their 16-bit half through a flat thunk block. The fixture loads them by
   hand, so a module that does not load is a result and not a load failure of
   the fixture itself. Every observation is printed through OutputDebugStringA
   so the runner reads them out of the probe log; the exit code names the first
   step that did not even get far enough to observe anything. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U LoadLibraryA(const char *);
API U GetProcAddress(U, const char *);
API U FreeLibrary(U);
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API U GetLastError(void);

typedef U(__stdcall *Create)(void *, void **, void *);
typedef U(__stdcall *Enumerate)(void *, void *);
typedef U(__stdcall *OpenProvider)(void);
typedef U(__stdcall *CloseProvider)(U);
typedef U(__stdcall *CreatePrimary)(U, void **);

static char line[256];
static void put(const char *text)
{
    U n = 0;
    while (text[n])
        ++n;
    OutputDebugStringA(text);
}
static void number(const char *label, U value)
{
    static const char digits[] = "0123456789abcdef";
    U at = 0, i = 0;
    while (label[i])
        line[at++] = label[i++];
    line[at++] = '=';
    for (i = 0; i < 8; ++i)
        line[at++] = digits[(value >> (28 - i * 4)) & 15];
    line[at] = 0;
    put(line);
}
/* The enumeration callback counts what DirectDrawEnumerateA offers. */
static U devices = 0;
static U __stdcall enumerated(void *guid, char *description, char *name, void *context)
{
    ++devices;
    return 1;
}
/* EnumDisplayModes hands a DDSURFACEDESC: dwSize, dwFlags, dwHeight,
   dwWidth are the first four dwords. */
static U modes = 0, mode_width = 0, mode_height = 0;
static U __stdcall enumerated_mode(U *description, void *context)
{
    ++modes;
    if (description)
    {
        number("DIRECTX_MODE width", description[3]);
        number("DIRECTX_MODE height", description[2]);
        /* DDPIXELFORMAT begins at 0x48; dwRGBBitCount is its fourth DWORD. */
        number("DIRECTX_MODE depth", description[21]);
    }
    if (description && description[3] > mode_width)
    {
        mode_width = description[3];
        mode_height = description[2];
    }
    return 1;
}
void __stdcall mainCRTStartup(void)
{
    U library = 0, dciman = 0, dsound = 0, value = 0;
    void *object = 0;

    /* DDRAW: the module whose DLL_PROCESS_ATTACH used to fail. */
    library = LoadLibraryA("DDRAW.DLL");
    number("DIRECTX ddraw_loaded", library ? 1 : 0);
    if (!library)
        ExitProcess(1);
    {
        Enumerate enumerate = (Enumerate)GetProcAddress(library, "DirectDrawEnumerateA");
        Create create = (Create)GetProcAddress(library, "DirectDrawCreate");
        number("DIRECTX ddraw_enumerate_found", enumerate ? 1 : 0);
        number("DIRECTX ddraw_create_found", create ? 1 : 0);
        if (!enumerate || !create)
            ExitProcess(2);
        value = enumerate((void *)enumerated, 0);
        number("DIRECTX ddraw_enumerate", value);
        number("DIRECTX ddraw_devices", devices);
        object = 0;
        value = create(0, &object, 0);
        number("DIRECTX ddraw_create", value);
        number("DIRECTX ddraw_object", object ? 1 : 0);
        if (!value && object)
        {
            /* IDirectDraw, ddraw.h: QueryInterface 0, AddRef 1, Release 2,
               Compact 3, CreateClipper 4, CreatePalette 5, CreateSurface 6,
               DuplicateSurface 7, EnumDisplayModes 8, EnumSurfaces 9,
               FlipToGDISurface 10, GetCaps 11. */
            U **vtable = *(U ***)object;
            typedef U(__stdcall * Caps)(void *, void *, void *);
            typedef U(__stdcall * Modes)(void *, U, void *, void *, void *);
            /* DDCAPS of DirectX 6 and later is 380 bytes; DDRAW.DLL checks
               dwSize against the sizes it knows and answers E_INVALIDARG for
               anything else. */
            static U driver[95], helper[95];
            driver[0] = sizeof driver;
            helper[0] = sizeof helper;
            value = ((Caps)vtable[11])(object, driver, helper);
            number("DIRECTX ddraw_getcaps", value);
            /* dwCaps of the HEL half; DDCAPS_BLT is 0x00000040. */
            number("DIRECTX ddraw_helcaps", helper[2]);
            value = ((Modes)vtable[8])(object, 0, 0, 0, (void *)enumerated_mode);
            number("DIRECTX ddraw_enummodes", value);
            number("DIRECTX ddraw_modes", modes);
            ((U(__stdcall *)(void *))vtable[2])(object);
        }
    }

    /* DCIMAN32: the provider the screen savers reach through OPENGL32. */
    dciman = LoadLibraryA("DCIMAN32.DLL");
    number("DIRECTX dciman_loaded", dciman ? 1 : 0);
    if (!dciman)
        ExitProcess(3);
    {
        OpenProvider open = (OpenProvider)GetProcAddress(dciman, "DCIOpenProvider");
        CloseProvider close = (CloseProvider)GetProcAddress(dciman, "DCICloseProvider");
        CreatePrimary primary = (CreatePrimary)GetProcAddress(dciman, "DCICreatePrimary");
        number("DIRECTX dciman_entries", open && close && primary ? 1 : 0);
        if (!open || !close || !primary)
            ExitProcess(4);
        value = open();
        number("DIRECTX dci_open", value ? 1 : 0);
        if (value)
        {
            void *surface = 0;
            U result = primary(value, &surface);
            number("DIRECTX dci_createprimary", result);
            number("DIRECTX dci_surface", surface ? 1 : 0);
            close(value);
        }
        /* a DC handle past F800h does not fit the word of the 16-bit
           side. Twenty-four create/delete pairs raise the generation of the
           slot the provider's DC takes next; the provider then answers a
           token, and DCICloseProvider deletes the DC behind it. */
        {
            typedef U(__stdcall * CompatibleDC)(U);
            typedef U(__stdcall * DeleteDC)(U);
            U gdi = (U)LoadLibraryA("GDI32.DLL"), i, second;
            CompatibleDC make = (CompatibleDC)GetProcAddress(gdi, "CreateCompatibleDC");
            DeleteDC remove = (DeleteDC)GetProcAddress(gdi, "DeleteDC");
            for (i = 0; make && remove && i < 24; ++i)
                remove(make(0));
            second = open();
            number("DIRECTX dci_token", second > 0xf800 ? 1 : 0);
            if (second)
                close(second);
        }
        /* the window watch OPENGL32 opens in wglSetPixelFormat once
           DCI has a primary surface. A visible popup of 200x150 at 100,100,
           nothing above it: its clip list is that one rectangle on the
           screen; a move is a change of status. */
        {
            typedef U(__stdcall * Make)(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
            typedef U(__stdcall * Place)(U, U, int, int, int, int, U);
            typedef U(__stdcall * Remove)(U);
            typedef U(__stdcall * WatchOpen)(U);
            typedef U(__stdcall * WatchOne)(U);
            typedef void(__stdcall * WatchClose)(U);
            typedef U(__stdcall * ClipList)(U, void *, U, void *);
            U user = LoadLibraryA("USER32.DLL"), window = 0, watch = 0, size = 0, copied = 0;
            Make make = (Make)GetProcAddress(user, "CreateWindowExA");
            Place place = (Place)GetProcAddress(user, "SetWindowPos");
            Remove remove = (Remove)GetProcAddress(user, "DestroyWindow");
            WatchOpen watch_open = (WatchOpen)GetProcAddress(dciman, "WinWatchOpen");
            WatchOne changed = (WatchOne)GetProcAddress(dciman, "WinWatchDidStatusChange");
            WatchClose watch_close = (WatchClose)GetProcAddress(dciman, "WinWatchClose");
            ClipList clip = (ClipList)GetProcAddress(dciman, "WinWatchGetClipList");
            static U data[64];
            if (make && place && remove && watch_open && changed && watch_close && clip)
                window = make(0, "STATIC", "", 0x90000000, 100, 100, 200, 150, 0, 0, 0, 0);
            number("DIRECTX watch_window", window ? 1 : 0);
            if (window)
            {
                watch = watch_open(window);
                number("DIRECTX watch_open", watch ? 1 : 0);
                number("DIRECTX watch_fresh", changed(watch));
                size = clip(watch, 0, 0, 0);
                number("DIRECTX watch_size", size);
                copied = clip(watch, 0, sizeof data, data);
                number("DIRECTX watch_copied", copied == size ? 1 : 0);
                /* RGNDATAHEADER: dwSize, iType, nCount, nRgnSize, rcBound. */
                number("DIRECTX watch_count", data[2]);
                number("DIRECTX watch_left", data[8]);
                number("DIRECTX watch_top", data[9]);
                number("DIRECTX watch_right", data[10]);
                number("DIRECTX watch_bottom", data[11]);
                number("DIRECTX watch_quiet", changed(watch));
                place(window, 0, 150, 120, 0, 0, 0x15); /* SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE */
                number("DIRECTX watch_moved", changed(watch));
                watch_close(watch);
                number("DIRECTX watch_closed", changed(watch));
                remove(window);
            }
        }
    }

    /* DSOUND: it has no 16-bit half at all, and its answer without a device
       is the one the check records. */
    dsound = LoadLibraryA("DSOUND.DLL");
    number("DIRECTX dsound_loaded", dsound ? 1 : 0);
    if (dsound)
    {
        Create create = (Create)GetProcAddress(dsound, "DirectSoundCreate");
        number("DIRECTX dsound_create_found", create ? 1 : 0);
        if (create)
        {
            object = 0;
            value = create(0, &object, 0);
            number("DIRECTX dsound_create", value);
            number("DIRECTX dsound_object", object ? 1 : 0);
        }
        FreeLibrary(dsound);
    }
    FreeLibrary(dciman);
    FreeLibrary(library);
    put("DIRECTX done");
    ExitProcess(0);
}
