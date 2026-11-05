/* SPDX-License-Identifier: GPL-2.0-or-later
 * Guest contract for the writable host volume: an x86 program writes in the
 * first run and finds everything again in the second. The run number comes
 * from the command line, so both runs are the same program.
 * Error numbers 39400..39499. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetLastError(void);
API char *GetCommandLineA(void);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U ReadFile(U, void *, U, U *, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API U GetFileSize(U, U *);
API U SetFilePointer(U, int, int *, U);
API U FlushFileBuffers(U);
API U GetFileAttributesA(const char *);
API U SetFileAttributesA(const char *, U);
API U CreateDirectoryA(const char *, U);
API U RemoveDirectoryA(const char *);
API U DeleteFileA(const char *);
API U MoveFileA(const char *, const char *);
API U FindFirstFileA(const char *, void *);
API U FindNextFileA(U, void *);
API U FindClose(U);
API U GetShortPathNameA(const char *, char *, U);
API U GetLongPathNameA(const char *, char *, U);
API U SetCurrentDirectoryA(const char *);
API U GetCurrentDirectoryA(U, char *);
API U GetFullPathNameA(const char *, U, char *, char **);
API U GetDriveTypeA(const char *);
API U GetLogicalDrives(void);
API U GetVolumeInformationA(const char *, char *, U, U *, U *, U *, char *, U);
API U GetDiskFreeSpaceA(const char *, U *, U *, U *, U *);
API U GetFileTime(U, void *, void *, void *);
API U RegCreateKeyExA(U, const char *, U, const char *, U, U, U, U *, U *);
API U RegSetValueExA(U, const char *, U, U, const void *, U);
API U RegQueryValueExA(U, const char *, U *, U *, void *, U *);
API U RegFlushKey(U);
API U RegCloseKey(U);

static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b)
        ++a, ++b;
    return *a == *b;
}
static U length(const char *a)
{
    U n = 0;
    while (a[n])
        ++n;
    return n;
}

/* The command line ends in " 1" or " 2". */
static U pass_number(void)
{
    const char *p = GetCommandLineA();
    U last = 0;
    while (*p)
    {
        if (*p >= '1' && *p <= '9')
            last = (U)(*p - '0');
        ++p;
    }
    return last;
}

static void first_run(void)
{
    U handle, done = 0, data[80];
    U key = 0, disposition = 0;
    check(CreateDirectoryA("C:\\Programme dieser Maschine", 0), 39400);
    handle = CreateFileA("C:\\Programme dieser Maschine\\Erste Datei.txt", 0xc0000000, 0, 0, 2, 128, 0);
    check(handle != ~0u, 39401);
    check(WriteFile(handle, "neo98", 5, &done, 0) && done == 5, 39402);
    check(FlushFileBuffers(handle) && CloseHandle(handle), 39403);
    /* A second long name in the same directory gets the next numeric tail. */
    handle = CreateFileA("C:\\Programme dieser Maschine\\Erste Datei zwei.txt", 0x40000000, 0, 0, 2, 128, 0);
    check(handle != ~0u && WriteFile(handle, "zwei", 4, &done, 0) && CloseHandle(handle), 39404);
    /* Rename, attribute and delete, all of which have to survive. */
    handle = CreateFileA("C:\\WINDOWS\\TEMP\\umbenennen.txt", 0x40000000, 0, 0, 2, 128, 0);
    check(handle != ~0u && WriteFile(handle, "x", 1, &done, 0) && CloseHandle(handle), 39405);
    check(MoveFileA("C:\\WINDOWS\\TEMP\\umbenennen.txt", "C:\\WINDOWS\\TEMP\\umbenannt.txt"), 39406);
    check(SetFileAttributesA("C:\\WINDOWS\\TEMP\\umbenannt.txt", 1), 39407);
    handle = CreateFileA("C:\\WINDOWS\\TEMP\\weg.txt", 0x40000000, 0, 0, 2, 128, 0);
    check(handle != ~0u && CloseHandle(handle) && DeleteFileA("C:\\WINDOWS\\TEMP\\weg.txt"), 39408);
    /* An open handle blocks a second open and the delete, as on Win98. */
    handle = CreateFileA("C:\\WINDOWS\\TEMP\\gesperrt.txt", 0xc0000000, 0, 0, 2, 128, 0);
    check(handle != ~0u, 39409);
    check(CreateFileA("C:\\WINDOWS\\TEMP\\gesperrt.txt", 0x80000000, 1, 0, 3, 0, 0) == ~0u && GetLastError() == 32,
          39410);
    check(!DeleteFileA("C:\\WINDOWS\\TEMP\\gesperrt.txt") && GetLastError() == 32, 39411);
    check(CloseHandle(handle) && DeleteFileA("C:\\WINDOWS\\TEMP\\gesperrt.txt"), 39412);
    /* The registry keeps a key of its own. */
    check(!RegCreateKeyExA(0x80000002u, "Software\\NEO98\\HostVolume", 0, 0, 0, 0, 0, &key, &disposition),
          39413);
    check(!RegSetValueExA(key, "Lauf", 0, 1, "erster", 7), 39414);
    check(!RegFlushKey(key) && !RegCloseKey(key), 39415);
    (void)data;
}

static void second_run(void)
{
    U handle, done = 0, size = 0, data[80];
    char text[260], name[260];
    U key = 0, type = 0, capacity = sizeof text;
    /* Everything the first run wrote is here, byte for byte. */
    handle = CreateFileA("C:\\Programme dieser Maschine\\Erste Datei.txt", 0x80000000, 1, 0, 3, 0, 0);
    check(handle != ~0u, 39430);
    check(GetFileSize(handle, 0) == 5, 39431);
    check(ReadFile(handle, text, 5, &done, 0) && done == 5 && text[0] == 'a' && text[4] == '8', 39432);
    /* Three timestamps come back; the creation time is not zero. */
    check(GetFileTime(handle, data, data + 2, data + 4) && (data[0] || data[1]), 39433);
    check(CloseHandle(handle), 39434);
    /* The 8.3 aliases are the ones of the first run and reach the same files. */
    check(GetShortPathNameA("C:\\Programme dieser Maschine\\Erste Datei.txt", text, 260) &&
              equal(text, "C:\\PROGRA~1\\ERSTED~1.TXT"),
          39435);
    check(GetShortPathNameA("C:\\Programme dieser Maschine\\Erste Datei zwei.txt", name, 260) &&
              equal(name, "C:\\PROGRA~1\\ERSTED~2.TXT"),
          39436);
    handle = CreateFileA(text, 0x80000000, 1, 0, 3, 0, 0);
    check(handle != ~0u && GetFileSize(handle, 0) == 5 && CloseHandle(handle), 39437);
    check(GetLongPathNameA(text, name, 260) &&
              equal(name, "C:\\Programme dieser Maschine\\Erste Datei.txt"),
          39438);
    /* The lookup ignores case, on a host file system that does not. */
    handle = CreateFileA("c:\\PROGRAMME DIESER MASCHINE\\erste datei.TXT", 0x80000000, 1, 0, 3, 0, 0);
    check(handle != ~0u && CloseHandle(handle), 39439);
    /* Rename, attribute and delete survived as well. */
    check(GetFileAttributesA("C:\\WINDOWS\\TEMP\\umbenennen.txt") == ~0u && GetLastError() == 2, 39440);
    check((GetFileAttributesA("C:\\WINDOWS\\TEMP\\umbenannt.txt") & 1) == 1, 39441);
    check(GetFileAttributesA("C:\\WINDOWS\\TEMP\\weg.txt") == ~0u, 39442);
    check(SetFileAttributesA("C:\\WINDOWS\\TEMP\\umbenannt.txt", 128) && DeleteFileA("C:\\WINDOWS\\TEMP\\umbenannt.txt"),
          39443);
    /* FindFirst reports the directory in its stored order: a directory other
       than the root starts with its "." and ".." entries, as a FAT directory
       does (src/x86_files.cpp), and the files follow with their long names. */
    handle = FindFirstFileA("C:\\Programme dieser Maschine\\*.*", data);
    check(handle != ~0u && equal((char *)data + 44, "."), 39444);
    check(FindNextFileA(handle, data) && equal((char *)data + 44, ".."), 39470);
    check(FindNextFileA(handle, data) && equal((char *)data + 44, "Erste Datei.txt"), 39471);
    check(FindNextFileA(handle, data) && equal((char *)data + 44, "Erste Datei zwei.txt"), 39445);
    check(!FindNextFileA(handle, data) && GetLastError() == 18 && FindClose(handle), 39446);
    /* One current directory per drive, as Win98 keeps it per process. */
    check(SetCurrentDirectoryA("D:\\Daten") && SetCurrentDirectoryA("C:\\WINDOWS\\TEMP"), 39447);
    check(GetCurrentDirectoryA(260, text) && equal(text, "C:\\WINDOWS\\TEMP"), 39448);
    check(GetFullPathNameA("D:zahl.txt", 260, name, 0) && equal(name, "D:\\Daten\\zahl.txt"), 39449);
    check(GetFullPathNameA("hier.txt", 260, name, 0) && equal(name, "C:\\WINDOWS\\TEMP\\hier.txt"), 39450);
    /* D: is a hard disk, E: the mounted CD-ROM, and the CD refuses writes. */
    check(GetDriveTypeA("C:\\") == 3 && GetDriveTypeA("D:\\") == 3 && GetDriveTypeA("E:\\") == 5, 39451);
    check((GetLogicalDrives() & 0x1c) == 0x1c, 39452);
    check(CreateFileA("E:\\neu.txt", 0x40000000, 0, 0, 2, 128, 0) == ~0u && GetLastError() == 19, 39453);
    handle = CreateFileA("E:\\LIESMICH.TXT", 0x80000000, 1, 0, 3, 0, 0);
    check(handle != ~0u && ReadFile(handle, text, 260, &done, 0) && done == 3 && CloseHandle(handle),
          39454);
    /* Volume label, serial and the reported medium. */
    {
        U serial = 0, maximum = 0, flags = 0;
        check(GetVolumeInformationA("D:\\", text, 260, &serial, &maximum, &flags, name, 260), 39455);
        check(equal(text, "DRIVE-D") && serial != 0, 39456);
    }
    {
        U sectors = 0, bytes = 0, free_units = 0, total_units = 0;
        check(GetDiskFreeSpaceA("C:\\", &sectors, &bytes, &free_units, &total_units), 39457);
        /* The old query stops counting where FAT32 does, so a host volume over
           two gigabytes reports the cap for both figures. */
        check(sectors * bytes == 4096 && free_units > 0 && total_units >= free_units &&
                  total_units <= 0x7ffff000u / 4096u,
              39458);
    }
    /* A file larger than the RAM volume's per-file limit. */
    handle = CreateFileA("C:\\WINDOWS\\TEMP\\gross.bin", 0xc0000000, 0, 0, 2, 128, 0);
    check(handle != ~0u, 39459);
    {
        static char block[32768];
        U i;
        for (i = 0; i < 32768; ++i)
            block[i] = (char)0x5a;
        for (i = 0; i < 320; ++i)
            check(WriteFile(handle, block, 32768, &done, 0) && done == 32768, 39460);
    }
    check(GetFileSize(handle, 0) == 320u * 32768u, 39461);
    check(SetFilePointer(handle, 320 * 32768 - 1, 0, 0) == 320u * 32768u - 1, 39462);
    check(ReadFile(handle, text, 1, &done, 0) && done == 1 && text[0] == (char)0x5a, 39463);
    check(CloseHandle(handle) && DeleteFileA("C:\\WINDOWS\\TEMP\\gross.bin"), 39464);
    /* The registry value of the first run is back. */
    check(!RegCreateKeyExA(0x80000002u, "Software\\NEO98\\HostVolume", 0, 0, 0, 0, 0, &key, &size), 39465);
    check(!RegQueryValueExA(key, "Lauf", 0, &type, text, &capacity) && type == 1 && equal(text, "erster"),
          39466);
    check(!RegSetValueExA(key, "Lauf", 0, 1, "zweiter", 8), 39467);
    check(!RegFlushKey(key) && !RegCloseKey(key), 39468);
    check(length(text) == 6, 39469);
    /* Tidy up what only this run made. */
    RemoveDirectoryA("C:\\WINDOWS\\TEMP\\leer");
}

void run_host_volume_test(void)
{
    if (pass_number() == 2)
        second_run();
    else
        first_run();
}
#ifdef HOST_VOLUME_STANDALONE
void mainCRTStartup(void)
{
    run_host_volume_test();
    ExitProcess(0);
}
#endif
