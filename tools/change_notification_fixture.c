// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U FindFirstChangeNotificationA(const char *, U, U);
API U FindFirstChangeNotificationW(const void *, U, U);
API U FindNextChangeNotification(U);
API U FindCloseChangeNotification(U);
API U WaitForSingleObject(U, U);
API U WaitForMultipleObjects(U, const U *, U, U);
API U CreateFileA(const char *, U, U, const void *, U, U, U);
API U CloseHandle(U);
API U WriteFile(U, const void *, U, U *, void *);
API U DeleteFileA(const char *);
API U CreateDirectoryA(const char *, const void *);
API U RemoveDirectoryA(const char *);
API U SetFileAttributesA(const char *, U);
API U GetLastError(void);
API void ExitProcess(U);
#define FILE_NAME 1
#define DIR_NAME 2
#define ATTRIBUTES 4
#define SIZE 8
#define LAST_WRITE 16
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static U touch(const char *path)
{
    U h = CreateFileA(path, 0xc0000000, 0, 0, 2, 0x80, 0);
    if (h == ~0u)
        return 0;
    return CloseHandle(h);
}
void mainCRTStartup(void)
{
    // Rejected arguments keep the original returns: -1 with an error for the
    // ANSI entry, 0 with error 120 for the never-implemented wide entry.
    check(FindFirstChangeNotificationA("C:\\NOSUCHDIR", 0, FILE_NAME) == ~0u, 43400);
    check(FindFirstChangeNotificationA("C:\\WINDOWS", 0, 0) == ~0u && GetLastError() == 87, 43401);
    check(FindFirstChangeNotificationA("C:\\WINDOWS", 0, 0x40000000) == ~0u && GetLastError() == 87, 43402);
    check(FindFirstChangeNotificationW(L"C:\\WINDOWS", 0, FILE_NAME) == 0 && GetLastError() == 120, 43403);
    check(FindNextChangeNotification(0x12345678) == 0 && GetLastError() == 6, 43404);

    U watch = FindFirstChangeNotificationA("C:\\WINDOWS\\", 0, FILE_NAME | LAST_WRITE | ATTRIBUTES);
    check(watch != ~0u && watch != 0, 43405);
    // Nothing has changed: the object must not be signalled and the wait must
    // time out with WAIT_TIMEOUT rather than returning immediately.
    check(WaitForSingleObject(watch, 30) == 258, 43406);

    check(touch("C:\\WINDOWS\\CHANGE.TXT"), 43407);
    check(WaitForSingleObject(watch, 0) == 0, 43408);
    // Manual-reset: a completed wait does not clear the notification.
    check(WaitForSingleObject(watch, 0) == 0, 43409);
    check(FindNextChangeNotification(watch) == 1, 43410);
    check(WaitForSingleObject(watch, 20) == 258, 43411);

    U file = CreateFileA("C:\\WINDOWS\\CHANGE.TXT", 0xc0000000, 0, 0, 3, 0x80, 0), done = 0;
    check(file != ~0u && WriteFile(file, "change", 6, &done, 0) && done == 6, 43412);
    check(WaitForSingleObject(watch, 0) == 0 && CloseHandle(file), 43413);
    check(FindNextChangeNotification(watch) == 1 && WaitForSingleObject(watch, 0) == 258, 43414);

    check(SetFileAttributesA("C:\\WINDOWS\\CHANGE.TXT", 1), 43415);
    check(WaitForSingleObject(watch, 0) == 0, 43416);
    check(FindNextChangeNotification(watch) == 1, 43417);
    check(SetFileAttributesA("C:\\WINDOWS\\CHANGE.TXT", 0x80) && DeleteFileA("C:\\WINDOWS\\CHANGE.TXT"), 43418);
    check(WaitForSingleObject(watch, 0) == 0 && FindNextChangeNotification(watch) == 1, 43419);

    // A change below the watched directory reaches only the subtree watch.
    U flat = FindFirstChangeNotificationA("C:\\", 0, FILE_NAME | DIR_NAME);
    U deep = FindFirstChangeNotificationA("C:\\", 1, FILE_NAME | DIR_NAME);
    check(flat != ~0u && deep != ~0u && flat != deep, 43420);
    check(CreateDirectoryA("C:\\WINDOWS\\SUB", 0), 43421);
    check(WaitForSingleObject(deep, 0) == 0 && WaitForSingleObject(flat, 0) == 258, 43422);
    check(FindNextChangeNotification(deep) == 1, 43423);
    check(CreateDirectoryA("C:\\ROOTDIR", 0), 43424);
    check(WaitForSingleObject(flat, 0) == 0 && WaitForSingleObject(deep, 0) == 0, 43425);

    // The same objects are waitable through the ported multiple-object wait.
    U handles[3] = {watch, flat, deep};
    check(WaitForMultipleObjects(3, handles, 0, 0) == 1, 43426);
    check(FindNextChangeNotification(flat) == 1 && FindNextChangeNotification(deep) == 1, 43427);
    check(WaitForMultipleObjects(3, handles, 0, 25) == 258, 43428);
    check(touch("C:\\WINDOWS\\SUB\\LEAF.TXT"), 43429);
    check(WaitForMultipleObjects(3, handles, 0, 0) == 2, 43430);

    // A filter the watch does not request leaves the object unsignalled.
    check(FindNextChangeNotification(deep) == 1, 43431);
    U sized = FindFirstChangeNotificationA("C:\\WINDOWS\\SUB", 0, SIZE);
    check(sized != ~0u, 43432);
    check(SetFileAttributesA("C:\\WINDOWS\\SUB\\LEAF.TXT", 0x80), 43433);
    check(WaitForSingleObject(sized, 0) == 258, 43434);
    file = CreateFileA("C:\\WINDOWS\\SUB\\LEAF.TXT", 0xc0000000, 0, 0, 3, 0x80, 0);
    check(file != ~0u && WriteFile(file, "x", 1, &done, 0) && CloseHandle(file), 43435);
    check(WaitForSingleObject(sized, 0) == 0, 43436);

    check(FindCloseChangeNotification(sized) == 1, 43437);
    check(FindNextChangeNotification(sized) == 0 && GetLastError() == 6, 43438);
    check(FindCloseChangeNotification(sized) == 0 && GetLastError() == 6, 43439);
    check(FindCloseChangeNotification(watch) && FindCloseChangeNotification(flat) &&
              FindCloseChangeNotification(deep), 43440);
    // The closed watches must not keep observing the shared volume.
    check(DeleteFileA("C:\\WINDOWS\\SUB\\LEAF.TXT") && RemoveDirectoryA("C:\\WINDOWS\\SUB") &&
              RemoveDirectoryA("C:\\ROOTDIR"), 43441);
    ExitProcess(0);
}
