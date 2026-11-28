/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The keys of a property sheet: a modal PropertySheet "Sheet keys" with three
   pages, each an edit field and a button. Every page writes down PSN_SETACTIVE
   ("page N") and when its edit field gets and loses the focus ("edit N",
   "left N"). When PropertySheet returns the log goes to C:\SHEETKEY.TXT.
   With the argument m the sheet is modeless (PSH_MODELESS), and the
   program's own loop hands every message to PSM_ISDIALOGMESSAGE first, as a
   program with a modeless sheet does; the log then goes to C:\SHEETMOD.TXT
   when the sheet has no page any more (Cancel). tools/test_sheet_keys_host.py. */
typedef unsigned U;
typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API int PropertySheetA(const void *);
API U SetWindowLongA(U, int, U);
API U GetWindowLongA(U, int);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
API const char *GetCommandLineA(void);
API int GetMessageA(U *, U, U, U);
API int TranslateMessage(const U *);
API U DispatchMessageA(const U *);
API U SendMessageA(U, U, U, U);
API int DestroyWindow(U);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

static char text[2000];
static void Note(const char *format, U value)
{
    if (lstrlenA(text) < 1900)
        wsprintfA(text + lstrlenA(text), format, value);
}
static U __stdcall Page(U window, U message, U wparam, U lparam)
{
    if (message == 0x110) /* WM_INITDIALOG: the page's number from PROPSHEETPAGE.lParam */
        SetWindowLongA(window, 8, ((U *)lparam)[7]);
    if (message == 0x4e && (int)((U *)lparam)[2] == -200) /* PSN_SETACTIVE */
        Note("page %u\r\n", GetWindowLongA(window, 8));
    if (message == 0x111 && (wparam & 0xffff) == 10 && (wparam >> 16) == 0x100) /* EN_SETFOCUS */
        Note("edit %u\r\n", GetWindowLongA(window, 8));
    if (message == 0x111 && (wparam & 0xffff) == 10 && (wparam >> 16) == 0x200) /* EN_KILLFOCUS */
        Note("left %u\r\n", GetWindowLongA(window, 8));
    return 0;
}
/* A DLGTEMPLATE of a child page with an edit field (10) and a button (11). */
static __attribute__((aligned(4))) W page_template[] = {
    /* style WS_CHILD | DS_3DLOOK | DS_CONTROL, no extended style, 2 items */
    0x0404, 0x4000, 0, 0, 2, 0, 0, 160, 80, 0, 0, 0,
    /* the edit field: WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL */
    0x0080, 0x5081, 0, 0, 10, 10, 100, 14, 10, 0xffff, 0x0081, 0, 0,
    0, /* the next item begins on a DWORD */
    /* the button: WS_CHILD|WS_VISIBLE|WS_TABSTOP */
    0x0000, 0x5001, 0, 0, 10, 40, 60, 14, 11, 0xffff, 0x0080, 'O', 'k', 0, 0,
};
struct Page { U size, flags, instance; const void *resource; U icon; const char *title; void *procedure; U parameter, callback, reference; };
struct Header { U size, flags, parent, instance, icon; const char *caption; U pages, start; const struct Page *page; U callback; };
void __stdcall mainCRTStartup(void)
{
    static const char *titles[] = {"First", "Second", "Third"};
    struct Page pages[3];
    struct Header header = {40, 0x88, 0, GetModuleHandleA(0), 0, "Sheet keys", 3, 0, pages, 0}; /* PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW */
    U written = 0, file;
    for (int i = 0; i < 3; ++i)
    {
        struct Page page = {40, 9, header.instance, page_template, 0, titles[i], (void *)Page, (U)i, 0, 0}; /* PSP_DLGINDIRECT | PSP_USETITLE */
        pages[i] = page;
    }
    const char *line = GetCommandLineA();
    int modeless = 0;
    for (; *line; ++line)
        if (line[0] == ' ' && line[1] == 'm')
            modeless = 1;
    if (modeless)
    {
        U message[7], sheet;
        header.flags |= 0x400; /* PSH_MODELESS */
        sheet = (U)PropertySheetA(&header);
        while (GetMessageA(message, 0, 0, 0) > 0)
        {
            if (!SendMessageA(sheet, 0x475, 0, (U)message)) /* PSM_ISDIALOGMESSAGE */
            {
                TranslateMessage(message);
                DispatchMessageA(message);
            }
            if (!SendMessageA(sheet, 0x476, 0, 0)) /* PSM_GETCURRENTPAGEHWND */
                break;
        }
        DestroyWindow(sheet);
    }
    else
        PropertySheetA(&header);
    file = CreateFileA(modeless ? "C:\\SHEETMOD.TXT" : "C:\\SHEETKEY.TXT", 0x40000000u, 0, 0, 2, 0x80, 0);
    WriteFile(file, text, (U)lstrlenA(text), &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
