/* SPDX-License-Identifier: GPL-2.0-or-later */
/* x86 guest of tools/test_host_media_native.py: talks to the host media
   provider through \\.\NEO98MEDIA the way Media Center 98 does
   (src/host_media_abi.h). The command line names the part:

     none a computer without Kodi: the state says so in words, a folder
              fails with the same words, playing is refused
     catalog the fixed catalogue of tools/test_kodi_server.py: the top with
              the add-ons and the source, Netflix > TV Shows > Stranger Things
              > Season 1 with three episodes, labels without Kodi's markup,
              a poster as a small DIB, the add-ons and Widevine found
     play an episode plays: the player state follows Kodi's
              notifications, pause, seek and stop reach Kodi, and the screen
              goes to Kodi and comes back
     nonetflix Kodi without the Netflix add-on: the Netflix folder explains
              what is missing instead of failing silently

   Every check exits with its line number. */
#include "../src/host_media_abi.h"
#define API __declspec(dllimport)
#define S __stdcall
typedef unsigned U;
API void S ExitProcess(U);
API U S GetLastError(void);
API U S CloseHandle(U);
API U S CreateFileA(const char *, U, U, U, U, U, U);
API U S DeviceIoControl(U, U, void *, U, void *, U, U *, U);
API U S Sleep(U);
API U S GetTickCount(void);
API const char *S GetCommandLineA(void);
API void *S VirtualAlloc(void *, U, U, U);
#define CHECK(x) do { if (!(x)) ExitProcess(__LINE__); } while (0)
void *memset(void *p, int v, unsigned n) { unsigned char *q = p; while (n--) *q++ = (unsigned char)v; return p; }
void *memcpy(void *p, const void *s, unsigned n) { unsigned char *q = p; const unsigned char *r = s; while (n--) *q++ = *r++; return p; }
static U length(const char *s) { U n = 0; while (s[n]) ++n; return n; }
static U starts(const char *s, const char *prefix) { while (*prefix) if (*s++ != *prefix++) return 0; return 1; }
static U same(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static U contains(const char *s, const char *part) { for (; *s; ++s) if (starts(s, part)) return 1; return 0; }
static void copy(char *d, const char *s) { while ((*d++ = *s++)) {} }

static U device;
static HM_Status status;
static HM_Request request;
static HM_Listing *listing;
static HM_Picture *picture;

static void read_status(void)
{
    U done = 0;
    CHECK(DeviceIoControl(device, HM_IOCTL_STATUS, 0, 0, &status, sizeof status, &done, 0) && done == sizeof status);
    CHECK(status.version == HM_VERSION);
}
/* Waits until the connection is settled (not idle, not starting). */
static void settle(void)
{
    const U until = GetTickCount() + 20000;
    for (read_status(); status.state == HM_IDLE || status.state == HM_STARTING; read_status())
    {
        CHECK((int)(GetTickCount() - until) < 0);
        Sleep(20);
    }
}
static void prepare(U action, const char *path)
{
    memset(&request, 0, sizeof request);
    request.version = HM_VERSION; request.action = action;
    copy(request.path, path);
}
/* One page of a folder, asked again until it has loaded. */
static void browse(const char *where, U first)
{
    /* The path may lie in the listing the answer overwrites. */
    char path[HM_PATH];
    copy(path, where);
    const U until = GetTickCount() + 20000;
    for (;;)
    {
        U done = 0;
        prepare(0, path); request.first = first;
        CHECK(DeviceIoControl(device, HM_IOCTL_BROWSE, &request, sizeof request, listing, sizeof *listing, &done, 0) && done == sizeof *listing);
        if (listing->state != HM_LIST_LOADING) return;
        CHECK((int)(GetTickCount() - until) < 0);
        Sleep(20);
    }
}
static const HM_Entry *entry(const char *label)
{
    for (U i = 0; i < listing->count; ++i) if (same(listing->entry[i].label, label)) return &listing->entry[i];
    return 0;
}
static U control(U action, const char *path, int value)
{
    U done = 0;
    prepare(action, path); request.value = value;
    return DeviceIoControl(device, HM_IOCTL_CONTROL, &request, sizeof request, 0, 0, &done, 0) ? 0 : GetLastError();
}
/* Waits until the status fulfils a condition. */
#define AWAIT(condition) do { const U until_ = GetTickCount() + 20000; for (read_status(); !(condition); read_status()) { CHECK((int)(GetTickCount() - until_) < 0); Sleep(20); } } while (0)

static void none(void)
{
    settle();
    CHECK(status.state == HM_NO_KODI);
    CHECK(starts(status.message, "Kodi is not installed on this"));
    CHECK(contains(status.message, "kodi.tv"));
    browse("", 0);
    CHECK(listing->state == HM_LIST_FAILED && listing->count == 0 && same(listing->message, status.message));
    CHECK(control(HM_PLAY, "plugin://plugin.video.netflix/play/1", 0) == 21);
    /* A buffer too small for the state, and a request of another version. */
    U done = 0;
    CHECK(!DeviceIoControl(device, HM_IOCTL_STATUS, 0, 0, &status, 16, &done, 0) && GetLastError() == 122);
    prepare(HM_START, ""); request.version = 99;
    CHECK(!DeviceIoControl(device, HM_IOCTL_CONTROL, &request, sizeof request, 0, 0, &done, 0) && GetLastError() == 87);
}

static void catalog(void)
{
    settle();
    CHECK(status.state == HM_READY);
    CHECK(starts(status.kodi, "Kodi 21."));
    CHECK(status.features == (HM_HAS_NETFLIX | HM_HAS_INPUTSTREAM | HM_HAS_HELPER | HM_HAS_WIDEVINE));
    browse("", 0);
    CHECK(listing->state == HM_LIST_READY && listing->total == 3 && listing->count == 3 && same(listing->title, "Kodi"));
    const HM_Entry *netflix = entry("Netflix");
    CHECK(netflix && netflix->kind == HM_KIND_ADDON && (netflix->flags & HM_ENTRY_FOLDER) && same(netflix->path, "plugin://plugin.video.netflix/"));
    CHECK(entry("YouTube") && entry("Movies") && entry("Movies")->kind == HM_KIND_SOURCE);
    browse(netflix->path, 0);
    CHECK(listing->state == HM_LIST_READY && entry("My List") && entry("TV Shows"));
    browse("plugin://plugin.video.netflix/directory/tvshows/", 0);
    const HM_Entry *show = entry("Stranger Things");
    CHECK(show && show->kind == HM_KIND_SHOW && (show->flags & HM_ENTRY_FOLDER) && starts(show->art, "image://http%3a%2f%2f127.0.0.1"));
    CHECK(contains(show->detail, "Hawkins"));
    /* Kodi's markup: "[B]The Crown[/B] [COLOR red]New[/COLOR]". */
    CHECK(entry("The Crown New"));
    /* The poster as a 96x144 DIB on the window colour: red in the middle,
       the box filled where the picture does not reach. */
    prepare(0, show->art); request.width = 96; request.height = 144; request.value = 0xc0c0c0;
    const U until = GetTickCount() + 20000;
    for (;;)
    {
        U done = 0;
        CHECK(DeviceIoControl(device, HM_IOCTL_PICTURE, &request, sizeof request, picture, sizeof *picture, &done, 0) && done == sizeof *picture);
        if (picture->state != HM_PICTURE_LOADING) break;
        CHECK((int)(GetTickCount() - until) < 0);
        Sleep(20);
    }
    CHECK(picture->state == HM_PICTURE_READY && picture->width == 96 && picture->height == 144);
    CHECK(picture->pixels[72 * 96 + 48] == 0xd01010);
    /* The test poster is 100x100: 96x96 in the middle, grey above. */
    CHECK(picture->pixels[0] == 0xc0c0c0 && picture->pixels[143 * 96 + 95] == 0xc0c0c0);
    browse(show->path, 0);
    CHECK(listing->state == HM_LIST_READY && entry("Season 1") && entry("Season 1")->kind == HM_KIND_SEASON);
    browse(entry("Season 1")->path, 0);
    CHECK(listing->state == HM_LIST_READY && listing->total == 3);
    const HM_Entry *first = entry("1. Chapter One: The Vanishing of Will Byers");
    CHECK(first && first->kind == HM_KIND_EPISODE && (first->flags & HM_ENTRY_PLAYABLE) && !(first->flags & HM_ENTRY_FOLDER));
    /* A long folder comes in pages of HM_PAGE: 40 films of Netflix's first
       page and its entry for the next one. */
    browse("plugin://plugin.video.netflix/directory/long/", 0);
    CHECK(listing->state == HM_LIST_READY && listing->total == 41 && listing->count == HM_PAGE && same(listing->entry[0].label, "Item 1"));
    browse("plugin://plugin.video.netflix/directory/long/", 32);
    CHECK(listing->first == 32 && listing->count == 9 && same(listing->entry[7].label, "Item 40"));
    /* A folder that fails in Kodi, and an add-on that returns nothing. */
    browse("plugin://plugin.video.netflix/directory/broken/", 0);
    CHECK(listing->state == HM_LIST_FAILED && starts(listing->message, "Kodi could not open this folder"));
    browse("plugin://plugin.video.netflix/directory/empty/", 0);
    CHECK(listing->state == HM_LIST_READY && listing->total == 0 && starts(listing->message, "The add-on returned nothing"));
}

static void play(void)
{
    settle();
    CHECK(status.state == HM_READY && status.player == HM_STOPPED && status.screen == HM_SCREEN_NEO98);
    CHECK(control(HM_PAUSE, "", 0) == 21);
    CHECK(control(HM_PLAY, "plugin://plugin.video.netflix/play/80057281", 0) == 0);
    AWAIT(status.player == HM_PLAYING && status.screen == HM_SCREEN_KODI);
    CHECK(same(status.title, "Chapter One: The Vanishing of Will Byers"));
    AWAIT(status.total == 2940 && status.time >= 1);
    CHECK(control(HM_PAUSE, "", 0) == 0);
    AWAIT(status.player == HM_PAUSED);
    CHECK(control(HM_SEEK, "", 30) == 0);
    AWAIT(status.time >= 30);
    CHECK(control(HM_PAUSE, "", 0) == 0);
    AWAIT(status.player == HM_PLAYING);
    CHECK(control(HM_STOP, "", 0) == 0);
    AWAIT(status.player == HM_STOPPED && status.screen == HM_SCREEN_NEO98);
    /* A video Kodi cannot open: the screen stays, the state says why. */
    CHECK(control(HM_PLAY, "plugin://plugin.video.netflix/play/missing", 0) == 0);
    AWAIT(status.player == HM_STOPPED && starts(status.message, "Kodi could not play this video"));
    AWAIT(status.screen == HM_SCREEN_NEO98);
}

static void nonetflix(void)
{
    settle();
    CHECK(status.state == HM_READY && !(status.features & HM_HAS_NETFLIX) && !(status.features & HM_HAS_WIDEVINE));
    browse("plugin://plugin.video.netflix/", 0);
    CHECK(listing->state == HM_LIST_FAILED && starts(listing->message, "The Netflix add-on is not installed in Kodi."));
    CHECK(contains(listing->message, "NEO98 does not include it"));
}

void mainCRTStartup(void)
{
    const char *line = GetCommandLineA();
    while (*line && *line != ' ') ++line;
    while (*line == ' ') ++line;
    listing = VirtualAlloc(0, sizeof *listing, 0x3000, 4);
    picture = VirtualAlloc(0, sizeof *picture, 0x3000, 4);
    CHECK(listing && picture);
    device = CreateFileA("\\\\.\\NEO98MEDIA", 0, 0, 0, 3, 0, 0);
    CHECK(device != ~0u);
    if (same(line, "none")) none();
    else if (same(line, "catalog")) catalog();
    else if (same(line, "play")) play();
    else if (same(line, "nonetflix")) nonetflix();
    else CHECK(0);
    CHECK(CloseHandle(device));
    (void)length;
    ExitProcess(0);
}
