/* SPDX-License-Identifier: GPL-2.0-or-later
 * CD audio from an x86 guest: the drive of a CD image, its data track and the
 * MCI cdaudio device. tools/test_cd_audio_guest.py builds the image (a data
 * track with README.TXT and an audio track of two seconds of 440 Hz), mounts
 * it with --cdrom and checks the samples the mixer captured.
 *
 * The exit code is 0 or the number of the check that failed. */
typedef unsigned long DWORD;
typedef unsigned int UINT;
typedef void *HANDLE;
#define WINAPI __stdcall
__declspec(dllimport) void WINAPI ExitProcess(UINT);
__declspec(dllimport) UINT WINAPI GetDriveTypeA(const char *);
__declspec(dllimport) HANDLE WINAPI CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
__declspec(dllimport) int WINAPI ReadFile(HANDLE, void *, DWORD, DWORD *, void *);
__declspec(dllimport) int WINAPI CloseHandle(HANDLE);
__declspec(dllimport) int WINAPI GetVolumeInformationA(const char *, char *, DWORD, DWORD *, DWORD *, DWORD *,
                                                       char *, DWORD);
__declspec(dllimport) void WINAPI OutputDebugStringA(const char *);
__declspec(dllimport) DWORD WINAPI mciSendCommandA(UINT, UINT, DWORD, DWORD);

#define MCI_OPEN 0x0803
#define MCI_CLOSE 0x0804
#define MCI_PLAY 0x0806
#define MCI_SET 0x080D
#define MCI_STATUS 0x0814
#define MCI_WAIT 0x00000002
#define MCI_FROM 0x00000004
#define MCI_OPEN_TYPE 0x00002000
#define MCI_SET_TIME_FORMAT 0x00000400
#define MCI_STATUS_ITEM 0x00000100
#define MCI_TRACK 0x00000010
#define MCI_STATUS_NUMBER_OF_TRACKS 0x00000003
#define MCI_STATUS_LENGTH 0x00000001
#define MCI_CDA_STATUS_TYPE_TRACK 0x00004001
#define MCI_CDA_TRACK_AUDIO 1088
#define MCI_FORMAT_TMSF 10
#define MCI_FORMAT_MSF 2

struct Open { DWORD callback; UINT device; const char *type, *element, *alias; };
struct Set { DWORD callback, time_format, audio; };
struct Status { DWORD callback, value, item, track; };
struct Play { DWORD callback, from, to; };

static void fail(UINT check) { ExitProcess(check); }

void mainCRTStartup(void)
{
    char label[32] = {0}, text[16] = {0};
    DWORD serial = 0, length = 0, flags = 0, got = 0;
    HANDLE file;
    struct Open open = {0};
    struct Set set = {0};
    struct Status status = {0};
    struct Play play = {0};
    UINT device;

    if (GetDriveTypeA("D:\\") != 5) fail(1);
    if (!GetVolumeInformationA("D:\\", label, sizeof label, &serial, &length, &flags, 0, 0)) fail(2);
    const char expected_label[] = "NEO98CD";
    for (UINT i = 0; i < sizeof expected_label; ++i)
        if (label[i] != expected_label[i]) fail(3);
    file = CreateFileA("D:\\README.TXT", 0x80000000, 1, 0, 3, 0, 0);
    if (file == (HANDLE)-1) fail(4);
    if (!ReadFile(file, text, sizeof text - 1, &got, 0) || got != 8) fail(5);
    CloseHandle(file);
    if (text[0] != 'H' || text[6] != 'C' || text[7] != 'D') fail(6);

    open.type = "cdaudio";
    if (mciSendCommandA(0, MCI_OPEN, MCI_OPEN_TYPE | MCI_WAIT, (DWORD)&open)) fail(10);
    device = open.device;
    status.item = MCI_STATUS_NUMBER_OF_TRACKS;
    if (mciSendCommandA(device, MCI_STATUS, MCI_STATUS_ITEM | MCI_WAIT, (DWORD)&status)) fail(11);
    if (status.value != 2) fail(12);
    status.item = MCI_CDA_STATUS_TYPE_TRACK;
    status.track = 2;
    if (mciSendCommandA(device, MCI_STATUS, MCI_STATUS_ITEM | MCI_TRACK | MCI_WAIT, (DWORD)&status)) fail(13);
    if (status.value != MCI_CDA_TRACK_AUDIO) fail(14);
    set.time_format = MCI_FORMAT_MSF;
    if (mciSendCommandA(device, MCI_SET, MCI_SET_TIME_FORMAT | MCI_WAIT, (DWORD)&set)) fail(15);
    status.item = MCI_STATUS_LENGTH;
    status.track = 2;
    if (mciSendCommandA(device, MCI_STATUS, MCI_STATUS_ITEM | MCI_TRACK | MCI_WAIT, (DWORD)&status)) fail(16);
    /* Two seconds less one frame, in MSF (minute, second, frame from the low
       byte up): MCICDA takes the last frame of the track as its end. */
    if ((status.value & 0xffffff) != 0x4a0100) fail(17);
    set.time_format = MCI_FORMAT_TMSF;
    if (mciSendCommandA(device, MCI_SET, MCI_SET_TIME_FORMAT | MCI_WAIT, (DWORD)&set)) fail(18);
    play.from = 2; /* track 2, 0:00:00 */
    if (mciSendCommandA(device, MCI_PLAY, MCI_FROM | MCI_WAIT, (DWORD)&play)) fail(19);
    if (mciSendCommandA(device, MCI_CLOSE, MCI_WAIT, 0)) fail(20);
    OutputDebugStringA("CDAUDIO fixture done");
    ExitProcess(0);
}
