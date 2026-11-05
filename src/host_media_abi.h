/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Fixed-width, pointer-free interface between the host media provider
   (tools/host_media) and the Win32 program Media Center 98, over the device
   \\.\NEO98MEDIA. The guest gets catalogues, small pictures and the state of
   the player and Kodi's dialogs, never a decoded video frame.
    */
#ifndef NEO98_HOST_MEDIA_ABI_H
#define NEO98_HOST_MEDIA_ABI_H
#define HM_VERSION 1u
/* Out: HM_Status. */
#define HM_IOCTL_STATUS  0x414d0001u
/* In: HM_Request (path, first). Out: HM_Listing, one page of the folder. */
#define HM_IOCTL_BROWSE  0x414d0002u
/* In: HM_Request (path the art of an entry, width and height of the box,
   value the background colour 0xRRGGBB). Out: HM_Picture. */
#define HM_IOCTL_PICTURE 0x414d0003u
/* In: HM_Request (action, path, value). */
#define HM_IOCTL_CONTROL 0x414d0004u
/* Out: HM_Dialog, the dialog Kodi shows now. */
#define HM_IOCTL_DIALOG  0x414d0005u
#define HM_PATH 512
#define HM_LABEL 128
#define HM_DETAIL 256
#define HM_MESSAGE 512
#define HM_PAGE 32
#define HM_PICTURE_WIDTH 160
#define HM_PICTURE_HEIGHT 240
#define HM_TEXT 1024
#define HM_CHOICES 32
#define HM_BUTTON 64
/* The connection to Kodi: not tried yet; no Kodi on this computer; Kodi
   started, waiting for its JSON-RPC port; connected; Kodi does not answer
   (the reason in message). */
enum HM_State { HM_IDLE=0, HM_NO_KODI, HM_STARTING, HM_READY, HM_FAILED };
/* HM_OPENING lasts until Kodi shows the first picture (Player.OnAVStart)
   and its clock runs, so Kodi's spinner never shows. */
enum HM_Player { HM_STOPPED=0, HM_OPENING, HM_PLAYING, HM_PAUSED };
/* Who has the screen: NEO98; Kodi while it plays a video; or NEO98 with the
   video inside the program's window (the Mac host shows Kodi's window there
   through ScreenCaptureKit, where the program painted HM_VIDEO_KEY). */
enum HM_Screen { HM_SCREEN_NEO98=0, HM_SCREEN_KODI, HM_SCREEN_WINDOW };
/* The colour the program paints where the video goes, 0x00RRGGBB. Like the
   colour key of a Win98 video overlay: whatever covers the area (a menu, a
   window) hides the video there. Exact in 16-bit and 32-bit colour. */
#define HM_VIDEO_KEY 0x00100010u
/* What the host found in Kodi, so the program can explain what is missing.
   NEO98 installs none of these. */
enum HM_Feature { HM_HAS_NETFLIX=1, HM_HAS_INPUTSTREAM=2, HM_HAS_HELPER=4, HM_HAS_WIDEVINE=8,
                  /* The host can show Kodi's picture in the guest (the Mac host, screen recording allowed). */
                  HM_CAN_SHOW=16,
                  /* Kodi's dialogs come to the program: NEO98's add-on service.neo98 runs in Kodi. */
                  HM_HAS_DIALOGS=32 };
/* HM_START connects (and starts Kodi); HM_PLAY plays path; HM_PAUSE toggles
   pause; HM_SEEK moves by value seconds; HM_STOP stops; HM_SHOW_VIDEO gives
   the screen to a running video again; HM_REFRESH forgets the catalogue;
   HM_VIDEO_AREA tells where the video goes on the screen while it plays in
   the window: first x | y << 16, width and height, value 1 if it shows.
   HM_ANSWER answers the dialog first (its serial): value the button (OK 0;
   No 0, Yes 1, the third button 2), the chosen entry of a list, or
   HM_ANSWER_CANCEL; path the text of an input dialog. HM_PREPARE: the
   program selected the video path, which may play next (the host looks up
   its stream ahead). HM_SEEK_TO goes to value seconds. HM_FULLSCREEN
   (value 1 or 0): the video area fills the program's screen, and the host
   fills its own screen with the video (the video in the window only).
   HM_VOLUME sets Kodi's volume to value (0-100), HM_MUTE mutes (value 1)
   or not. */
enum HM_Action { HM_START=1, HM_PLAY, HM_PAUSE, HM_SEEK, HM_STOP, HM_SHOW_VIDEO, HM_REFRESH, HM_VIDEO_AREA, HM_ANSWER, HM_PREPARE,
                 HM_SEEK_TO, HM_FULLSCREEN, HM_VOLUME, HM_MUTE };
#define HM_ANSWER_CANCEL (-1)
/* A dialog of Kodi (an add-on's question, a sign-in, a choice of profile):
   a message with OK, a question with No and Yes (and a third button), a
   list, a line of text (HM_DIALOG_HIDDEN a password, HM_DIALOG_NUMBER a
   number or PIN), or a progress Kodi closes itself (HM_DIALOG_CANCEL when
   it can be cancelled). */
enum HM_DialogKind { HM_DIALOG_NONE=0, HM_DIALOG_OK, HM_DIALOG_YESNO, HM_DIALOG_SELECT, HM_DIALOG_INPUT, HM_DIALOG_PROGRESS };
enum HM_DialogFlags { HM_DIALOG_HIDDEN=1, HM_DIALOG_NUMBER=2, HM_DIALOG_CANCEL=4 };
enum HM_ListState { HM_LIST_LOADING=0, HM_LIST_READY, HM_LIST_FAILED };
enum HM_PictureState { HM_PICTURE_LOADING=0, HM_PICTURE_READY, HM_PICTURE_NONE };
/* What an entry is, for its icon when it has no picture. */
enum HM_Kind { HM_KIND_FOLDER=0, HM_KIND_ADDON, HM_KIND_SOURCE, HM_KIND_SHOW, HM_KIND_SEASON, HM_KIND_MOVIE, HM_KIND_EPISODE, HM_KIND_VIDEO };
/* HM_ENTRY_NEXT: the add-on's "Next page" folder; the program loads it
   when the user scrolls to the end, instead of showing it. */
enum HM_EntryFlags { HM_ENTRY_FOLDER=1, HM_ENTRY_PLAYABLE=2, HM_ENTRY_NEXT=4 };
#pragma pack(push,4)
typedef struct HM_Status {
    unsigned version, state, player, screen, features, generation;
    /* Position and length of the video, in seconds. */
    unsigned time, total;
    char kodi[64], title[HM_LABEL], message[HM_MESSAGE];
    /* The serial of the dialog Kodi shows (HM_IOCTL_DIALOG), 0 for none. */
    unsigned dialog;
    /* Kodi's volume (0-100) and whether it is muted. */
    unsigned volume, muted;
} HM_Status;
typedef struct HM_Request { unsigned version, action, first, width, height; int value; char path[HM_PATH]; } HM_Request;
typedef struct HM_Entry { unsigned flags, kind; char label[HM_LABEL], path[HM_PATH], art[HM_PATH], detail[HM_DETAIL]; } HM_Entry;
typedef struct HM_Listing {
    unsigned state, total, first, count;
    char title[HM_LABEL], message[HM_MESSAGE];
    HM_Entry entry[HM_PAGE];
} HM_Listing;
/* button: the labels of Kodi's buttons (OK; No, Yes, third), empty for
   the usual ones; input: the text an input dialog starts with; percent of
   a progress. */
typedef struct HM_Dialog {
    unsigned serial, kind, flags, count, percent;
    char heading[HM_LABEL], text[HM_TEXT], input[HM_LABEL], button[3][HM_BUTTON];
    char choice[HM_CHOICES][HM_LABEL];
} HM_Dialog;
/* 32-bit pixels 0x00RRGGBB, top-down, width * height of them. */
typedef struct HM_Picture { unsigned state, width, height; unsigned pixels[HM_PICTURE_WIDTH * HM_PICTURE_HEIGHT]; } HM_Picture;
#pragma pack(pop)
#ifdef __cplusplus
static_assert(sizeof(HM_Request)==24+HM_PATH && sizeof(HM_Entry)==8+HM_LABEL+2*HM_PATH+HM_DETAIL, "host media ABI");
static_assert(sizeof(HM_Dialog)==20+2*HM_LABEL+HM_TEXT+3*HM_BUTTON+HM_CHOICES*HM_LABEL, "host media ABI");
#endif
#endif
