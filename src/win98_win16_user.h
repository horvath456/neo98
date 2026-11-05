// SPDX-License-Identifier: GPL-2.0-or-later
// The 16-bit side of USER and GDI: the shapes the Pascal gates read and write,
// and the message translation between the 16-bit and the 32-bit packing.
// This header holds decisions only. It reads no memory, allocates nothing and
// calls nothing; src/win16_user.cpp does the work with these answers. The
// layouts are the ones the Win16 headers define and the ones the original
// USER.EXE reads on its own side of the WOW edge: USER.EXE of Win98 is a
// 16-bit module whose exported entries widen a Pascal frame and hand it to
// USER32 (segment 17, for instance, pushes the two dwords of a
// message frame and calls out with `lcall`), so the packing below is the
// packing that edge converts.
//
#pragma once
namespace Win98Win16User
{
using U = unsigned;

// ------------------------------------------------------------------ layouts
// Every structure a 16-bit program hands to USER or GDI. A far pointer is two
// words, low offset first, so a DWORD field and a far pointer field have the
// same width and only differ in what the words mean.
enum RectLayout : U
{
    RectLeft = 0, RectTop = 2, RectRight = 4, RectBottom = 6, RectBytes = 8
};
enum PointLayout : U
{
    PointX = 0, PointY = 2, PointBytes = 4
};
// MSG: HWND, WORD message, WORD wParam, DWORD lParam, DWORD time, POINT pt.
enum MessageLayout : U
{
    MessageWindow = 0, MessageMessage = 2, MessageWparam = 4, MessageLparam = 6,
    MessageTime = 10, MessagePoint = 14, MessageBytes = 18
};
// WNDCLASS: WORD style, FARPROC lpfnWndProc, WORD cbClsExtra, WORD cbWndExtra,
// HINSTANCE, HICON, HCURSOR, HBRUSH, LPCSTR menu, LPCSTR class.
enum ClassLayout : U
{
    ClassStyle = 0, ClassProcedure = 2, ClassExtra = 6, ClassWindowExtra = 8,
    ClassInstance = 10, ClassIcon = 12, ClassCursor = 14, ClassBackground = 16,
    ClassMenu = 18, ClassName = 22, ClassBytes = 26
};
// PAINTSTRUCT: HDC, BOOL fErase, RECT rcPaint, BOOL fRestore, BOOL fIncUpdate,
// BYTE rgbReserved[16].
enum PaintLayout : U
{
    PaintDc = 0, PaintErase = 2, PaintRect = 4, PaintRestore = 12,
    PaintIncremental = 14, PaintReserved = 16, PaintBytes = 32
};
// CREATESTRUCT: LPVOID lpCreateParams, HINSTANCE, HMENU, HWND parent, int cy,
// int cx, int y, int x, LONG style, LPCSTR name, LPCSTR class, DWORD exstyle.
enum CreateLayout : U
{
    CreateParameter = 0, CreateInstance = 4, CreateMenu = 6, CreateParent = 8,
    CreateHeight = 10, CreateWidth = 12, CreateY = 14, CreateX = 16,
    CreateStyle = 18, CreateName = 22, CreateClass = 26, CreateExtended = 30,
    CreateBytes = 34
};
// Owner-draw structures keep field order across the boundary. Fields are
// words or doublewords in 16-bit form and doublewords in 32-bit form.
// MEASUREITEMSTRUCT contains CtlType, CtlID, itemID, itemWidth, itemHeight
// and itemData.
enum MeasureItemLayout : U
{
    MeasureType = 0, MeasureId = 2, MeasureItem = 4, MeasureWidth = 6,
    MeasureHeight = 8, MeasureData = 10, MeasureBytes = 14
};
// DRAWITEMSTRUCT: UINT CtlType, CtlID, itemID, itemAction, itemState,
// HWND hwndItem, HDC hDC, RECT rcItem, DWORD itemData.
enum DrawItemLayout : U
{
    DrawType = 0, DrawId = 2, DrawItem = 4, DrawAction = 6, DrawState = 8,
    DrawWindow = 10, DrawDc = 12, DrawRect = 14, DrawData = 22, DrawBytes = 26
};
// DELETEITEMSTRUCT: UINT CtlType, CtlID, itemID, HWND hwndItem, DWORD itemData.
enum DeleteItemLayout : U
{
    DeleteType = 0, DeleteId = 2, DeleteItem = 4, DeleteWindow = 6,
    DeleteData = 8, DeleteBytes = 12
};
// COMPAREITEMSTRUCT: UINT CtlType, CtlID, HWND hwndItem, UINT itemID1,
// DWORD itemData1, UINT itemID2, DWORD itemData2.
enum CompareItemLayout : U
{
    CompareType = 0, CompareId = 2, CompareWindow = 4, CompareFirst = 6,
    CompareFirstData = 8, CompareSecond = 12, CompareSecondData = 14,
    CompareBytes = 18
};
// MINMAXINFO: five POINTs.
enum MinMaxLayout : U
{
    MinMaxReserved = 0, MinMaxMaxSize = 4, MinMaxMaxPosition = 8,
    MinMaxMinTrack = 12, MinMaxMaxTrack = 16, MinMaxBytes = 20
};
// WINDOWPOS: HWND hwnd, HWND hwndInsertAfter, int x, int y, int cx, int cy,
// UINT flags.
enum WindowPosLayout : U
{
    WindowPosWindow = 0, WindowPosAfter = 2, WindowPosX = 4, WindowPosY = 6,
    WindowPosWidth = 8, WindowPosHeight = 10, WindowPosFlags = 12,
    WindowPosBytes = 14
};
// MDICREATESTRUCT: LPCSTR szClass, LPCSTR szTitle, HINSTANCE hOwner, int x,
// int y, int cx, int cy, LONG style, LPARAM lParam. The 32-bit shape has the
// same field order with every field a dword, so only the widths move.
enum MdiCreateLayout : U
{
    MdiCreateClass = 0, MdiCreateTitle = 4, MdiCreateOwner = 8, MdiCreateX = 10,
    MdiCreateY = 12, MdiCreateWidth = 14, MdiCreateHeight = 16, MdiCreateStyle = 18,
    MdiCreateParameter = 22, MdiCreateBytes = 26,
    // The 32-bit shape, for the widened copy.
    MdiCreate32Class = 0, MdiCreate32Title = 4, MdiCreate32Owner = 8, MdiCreate32X = 12,
    MdiCreate32Y = 16, MdiCreate32Width = 20, MdiCreate32Height = 24, MdiCreate32Style = 28,
    MdiCreate32Parameter = 32, MdiCreate32Bytes = 36,
};
// NCCALCSIZE_PARAMS: three RECTs and a far pointer to a WINDOWPOS.
enum CalcSizeLayout : U
{
    CalcSizeRects = 0, CalcSizePosition = 24, CalcSizeBytes = 28
};
// TEXTMETRIC of the 16-bit GDI: eleven signed words, then seven bytes.
enum TextMetricLayout : U
{
    TextMetricHeight = 0, TextMetricAscent = 2, TextMetricDescent = 4,
    TextMetricInternalLeading = 6, TextMetricExternalLeading = 8,
    TextMetricAverageWidth = 10, TextMetricMaxWidth = 12, TextMetricWeight = 14,
    TextMetricOverhang = 16, TextMetricDigitizedX = 18, TextMetricDigitizedY = 20,
    TextMetricFirstChar = 22, TextMetricLastChar = 23, TextMetricDefaultChar = 24,
    TextMetricBreakChar = 25, TextMetricItalic = 26, TextMetricUnderlined = 27,
    TextMetricStruckOut = 28, TextMetricPitchAndFamily = 29, TextMetricCharSet = 30,
    TextMetricBytes = 31
};

// ------------------------------------------------------------- the messages
// The messages whose two parameters are packed differently on the two sides.
// A translation has a direction and a class; the class says what has to be
// moved, and for the classes that carry a structure it says which structure.
enum Message : U
{
    WmActivate = 0x0006,
    WmCtlColor = 0x0019,           // the single 16-bit message
    WmGetMinMaxInfo = 0x0024,
    WmVkeyToItem = 0x002e,
    WmCharToItem = 0x002f,
    WmGetText = 0x000d,
    WmSetText = 0x000c,
    WmWindowPosChanging = 0x0046,
    WmWindowPosChanged = 0x0047,
    WmNcCalcSize = 0x0083,
    WmNcCreate = 0x0081,
    WmDrawItem = 0x002b,
    WmMeasureItem = 0x002c,
    WmDeleteItem = 0x002d,
    WmCompareItem = 0x0039,
    WmCreate = 0x0001,
    WmCommand = 0x0111,
    WmHScroll = 0x0114,
    WmVScroll = 0x0115,
    WmMenuSelect = 0x011f,
    WmMenuChar = 0x0120,
    WmParentNotify = 0x0210,
    WmMdiCreate = 0x0220,
    WmMdiActivate = 0x0222,
    WmMdiGetActive = 0x0229,
    WmMdiSetMenu = 0x0230,
    WmCtlColorMsgBox = 0x0132,     // the first of the six 32-bit messages
    WmCtlColorStatic = 0x0138,     // the last of them
    // The edit control keeps its text in a movable local block on the 16-bit
    // side, and these two messages hand the block over in both directions.
    EmSetHandle = 0x040c,
    EmGetHandle = 0x040d,
    WmDdeFirst = 0x03e0,
    WmDdeLast = 0x03ef,
};
// What a translation has to do with one message.
enum Kind : U
{
    // wParam is a word and lParam travels unchanged. Almost every message.
    KindPlain = 0,
    // The word pair moves between the halves of wParam and lParam. `Swap`
    // below says which halves; the four members are what the 32-bit side
    // packs and what the 16-bit side packs.
    KindSwap,
    // WM_CTLCOLOR: one 16-bit message with the control type in HIWORD(lParam)
    // against six 32-bit messages.
    KindControlColor,
    // lParam is a far pointer to a structure that has to be copied and
    // widened. `structure` says which one.
    KindStructure,
    // lParam is a far pointer to text.
    KindText,
    // WM_MDIACTIVATE: the 32-bit form names both children, the 16-bit form
    // names one of them and a flag.
    KindMdiActivate,
    // WM_PARENTNOTIFY: a creation or destruction carries an id, a mouse
    // event carries a point, and only the first form repacks.
    KindParentNotify,
    // lParam is a far pointer to a structure the 16-bit caller owns, on its
    // way to a native reader. The widened copy lives in the staging ring of
    // the task; `structure` says which shape. This is the opposite direction
    // of KindStructure, which narrows a structure a native caller owns.
    KindStructureUp,
    // WM_MDIGETACTIVE: the 16-bit form takes no parameter and packs the
    // maximized flag into the high half of its answer, the 32-bit form takes
    // a pointer to that flag and answers the window alone.
    KindMdiGetActive,
    // WM_MDISETMENU: the 16-bit form carries a refresh flag in wParam and
    // both menus in lParam, the 32-bit form carries one menu in each.
    KindMdiSetMenu,
    // The DDE messages: lParam is a packed pair of 16-bit handles on both
    // sides, but the 32-bit side may hand over an unpacked one.
    KindDde,
    // Nothing of this port carries the message across yet.
    KindUnported,
};
enum Structure : U
{
    StructureNone = 0, StructureCreate, StructureMinMax, StructureWindowPos,
    StructureCalcSize, StructureRect, StructurePoint, StructureMdiCreate,
    StructureMeasureItem, StructureDrawItem, StructureDeleteItem, StructureCompareItem,
};
// One rule. `low32` and `high32` name what the 32-bit wParam holds, `low16`
// and `high16` what the 16-bit lParam holds; a field is a token of Field.
enum Field : U
{
    FieldNone = 0, FieldWparamLow, FieldWparamHigh, FieldLparamLow, FieldLparamHigh
};
struct Rule
{
    U message, kind, structure;
    // For KindSwap: where the 16-bit wParam, the low word of the 16-bit
    // lParam and its high word come from, read out of the 32-bit pair.
    U word16, low16, high16;
};
// The rules, in message order. A message that is not named here is KindPlain.
// WM_COMMAND 32: wParam = id | notify << 16, lParam = control
// 16: wParam = id, lParam = control | notify << 16
// WM_HSCROLL/VSCROLL 32: wParam = code | position << 16, lParam = bar
// 16: wParam = code, lParam = position | bar << 16
// WM_MENUSELECT 32: wParam = item | flags << 16, lParam = menu
// 16: wParam = item, lParam = flags | menu << 16
// WM_MENUCHAR 32: wParam = character | flags << 16, lParam = menu
// 16: wParam = character, lParam = flags | menu << 16
// WM_ACTIVATE 32: wParam = state | minimized << 16, lParam = other
// 16: wParam = state, lParam = other | minimized << 16
// WM_VKEYTOITEM 32: wParam = key | caret << 16, lParam = list
// WM_CHARTOITEM 16: wParam = key, lParam = caret | list << 16
inline const Rule *Rules(U &count)
{
    static const Rule rules[] = {
        {WmActivate, KindSwap, StructureNone, FieldWparamLow, FieldLparamLow, FieldWparamHigh},
        {WmSetText, KindText, StructureNone, 0, 0, 0},
        {WmGetText, KindText, StructureNone, 0, 0, 0},
        {WmCreate, KindStructure, StructureCreate, 0, 0, 0},
        {WmNcCreate, KindStructure, StructureCreate, 0, 0, 0},
        {WmDrawItem, KindStructure, StructureDrawItem, 0, 0, 0},
        {WmMeasureItem, KindStructure, StructureMeasureItem, 0, 0, 0},
        {WmDeleteItem, KindStructure, StructureDeleteItem, 0, 0, 0},
        {WmCompareItem, KindStructure, StructureCompareItem, 0, 0, 0},
        {WmGetMinMaxInfo, KindStructure, StructureMinMax, 0, 0, 0},
        {WmVkeyToItem, KindSwap, StructureNone, FieldWparamLow, FieldWparamHigh, FieldLparamLow},
        {WmCharToItem, KindSwap, StructureNone, FieldWparamLow, FieldWparamHigh, FieldLparamLow},
        {WmWindowPosChanging, KindStructure, StructureWindowPos, 0, 0, 0},
        {WmWindowPosChanged, KindStructure, StructureWindowPos, 0, 0, 0},
        {WmNcCalcSize, KindStructure, StructureCalcSize, 0, 0, 0},
        {WmCommand, KindSwap, StructureNone, FieldWparamLow, FieldLparamLow, FieldWparamHigh},
        {WmHScroll, KindSwap, StructureNone, FieldWparamLow, FieldWparamHigh, FieldLparamLow},
        {WmVScroll, KindSwap, StructureNone, FieldWparamLow, FieldWparamHigh, FieldLparamLow},
        {WmMenuSelect, KindSwap, StructureNone, FieldWparamLow, FieldWparamHigh, FieldLparamLow},
        {WmMenuChar, KindSwap, StructureNone, FieldWparamLow, FieldWparamHigh, FieldLparamLow},
        {WmParentNotify, KindParentNotify, StructureNone, 0, 0, 0},
        {WmMdiActivate, KindMdiActivate, StructureNone, 0, 0, 0},
        {WmMdiCreate, KindStructureUp, StructureMdiCreate, 0, 0, 0},
        {WmMdiGetActive, KindMdiGetActive, StructureNone, 0, 0, 0},
        {WmMdiSetMenu, KindMdiSetMenu, StructureNone, 0, 0, 0},
    };
    count = sizeof rules / sizeof rules[0];
    return rules;
}
inline const Rule *RuleFor(U message)
{
    U count = 0;
    const Rule *rules = Rules(count);
    for (U i = 0; i < count; ++i)
        if (rules[i].message == message)
            return &rules[i];
    return nullptr;
}
// The 32-bit control-colour messages are six consecutive numbers; the 16-bit
// side has one message and puts the distance in the high word of lParam.
inline bool IsControlColor32(U message)
{
    return message >= WmCtlColorMsgBox && message <= WmCtlColorStatic;
}
inline U ControlColorType(U message) { return message - WmCtlColorMsgBox; }
inline U ControlColor32(U type)
{
    return type <= WmCtlColorStatic - WmCtlColorMsgBox ? WmCtlColorMsgBox + type : 0u;
}
inline bool IsDde(U message) { return message >= WmDdeFirst && message <= WmDdeLast; }
// What a translation of this message needs, without looking at the parameters.
inline U KindOf(U message, bool from32)
{
    if (from32 ? IsControlColor32(message) : message == WmCtlColor)
        return KindControlColor;
    // WM_DDE_INITIATE carries two global ATOMs in lParam on both sides;
    // WM_DDE_TERMINATE has no lParam. Neither uses PackDDElParam's pool or
    // a shared data handle. Keep these broadcasts deliverable to Win16
    // windows too, including SYSDM.CPL hosted by RUNDLL32.
    if (message == WmDdeFirst || message == WmDdeFirst + 1)
        return KindPlain;
    if (IsDde(message))
        return KindDde;
    const Rule *rule = RuleFor(message);
    return rule ? rule->kind : (U)KindPlain;
}
inline U Pick(U field, U wparam, U lparam)
{
    switch (field)
    {
    case FieldWparamLow: return wparam & 0xffff;
    case FieldWparamHigh: return wparam >> 16;
    case FieldLparamLow: return lparam & 0xffff;
    case FieldLparamHigh: return lparam >> 16;
    default: return 0;
    }
}
// The two directions of the word shuffling, for every message whose class is
// KindSwap. They are exact inverses; the tests check that on every rule.
inline void SwapTo16(const Rule &rule, U wparam, U lparam, U &word, U &out)
{
    word = Pick(rule.word16, wparam, lparam);
    out = Pick(rule.low16, wparam, lparam) | (Pick(rule.high16, wparam, lparam) << 16);
}
inline void SwapTo32(const Rule &rule, U word, U lparam, U &wparam, U &out)
{
    U parts[5] = {0, 0, 0, 0, 0};
    parts[rule.word16] = word & 0xffff;
    parts[rule.low16] = lparam & 0xffff;
    parts[rule.high16] = lparam >> 16;
    wparam = parts[FieldWparamLow] | (parts[FieldWparamHigh] << 16);
    out = parts[FieldLparamLow] | (parts[FieldLparamHigh] << 16);
}
// WM_PARENTNOTIFY repacks only for the creation and destruction forms; a
// mouse event keeps its point in lParam and only narrows wParam.
inline bool ParentNotifyCarriesId(U event) { return event == WmCreate || event == 0x0002; }

// ------------------------------------------------- the argument word counts
// The Pascal contract of one ordinal: how many argument words the callee takes
// off the stack. `Unported` marks an ordinal this port does not answer, so the
// gate stops the run with its name instead of returning something that was
// never computed.
constexpr U Unported = 0xffffffffu;
struct Ordinal { U ordinal, words; const char *name; };
// USER.EXE, the ordinals of the programs this port carries. The word count is
// the one the entry of USER.EXE takes off the stack; every entry that USER.EXE
// forwards to USER32 shows it in its own frame.
inline const Ordinal *UserOrdinals(U &count)
{
    static const Ordinal table[] = {
        {1, 6, "MessageBox"},          {5, 1, "InitApp"},
        {6, 1, "PostQuitMessage"},     {13, 0, "GetTickCount"},
        {22, 1, "SetFocus"},           {23, 0, "GetFocus"},
        {117, 1, "WindowFromDC"},
        {29, 3, "ScreenToClient"},     {31, 1, "IsIconic"},
        {32, 3, "GetWindowRect"},      {33, 3, "GetClientRect"},
        {34, 2, "EnableWindow"},       {35, 1, "IsWindowEnabled"},
        {36, 4, "GetWindowText"},      {37, 3, "SetWindowText"},
        {38, 1, "GetWindowTextLength"},{39, 3, "BeginPaint"},
        {40, 3, "EndPaint"},           {41, 15, "CreateWindow"},
        {42, 2, "ShowWindow"},         {45, 1, "BringWindowToTop"},
        {46, 1, "GetParent"},          {47, 1, "IsWindow"},
        {49, 1, "IsWindowVisible"},    {53, 1, "DestroyWindow"},
        {56, 6, "MoveWindow"},         {57, 2, "RegisterClass"},
        {58, 4, "GetClassName"},       {66, 1, "GetDC"},
        {68, 2, "ReleaseDC"},          {69, 1, "SetCursor"},
        {72, 6, "SetRect"},            {73, 2, "SetRectEmpty"},
        {74, 4, "CopyRect"},           {75, 2, "IsRectEmpty"},
        {81, 4, "FillRect"},           {85, 7, "DrawText"},
        {87, 6, "DialogBox"},          {88, 2, "EndDialog"},
        {91, 2, "GetDlgItem"},         {92, 4, "SetDlgItemText"},
        {93, 5, "GetDlgItemText"},     {101, 6, "SendDlgItemMessage"},
        {104, 1, "MessageBeep"},       {107, 5, "DefWindowProc"}, {308, 5, "DefDlgProc"},
        {108, 5, "GetMessage"},        {109, 6, "PeekMessage"},
        {110, 5, "PostMessage"},       {111, 5, "SendMessage"},
        {113, 2, "TranslateMessage"},  {114, 2, "DispatchMessage"},
        {122, 7, "CallWindowProc"},    {124, 1, "UpdateWindow"},
        {125, 4, "InvalidateRect"},    {127, 3, "ValidateRect"},
        {133, 2, "GetWindowWord"},     {134, 3, "SetWindowWord"},
        {135, 2, "GetWindowLong"},     {136, 4, "SetWindowLong"},
        {157, 1, "GetMenu"},           {173, 3, "LoadCursor"},
        {174, 3, "LoadIcon"},          {176, 5, "LoadString"},
        {389, 7, "LoadImage"},          {284, 1, "GetFreeSystemResources"},
        {606, 11, "FormatMessage"},     {482, 3, "EnableScrollBar"},
        // USER : flags and recipients as DWORD and far
        // pointer, message and wParam as words, lParam as DWORD.
        {604, 8, "BroadcastSystemMessage"},
        {179, 1, "GetSystemMetrics"},  {180, 1, "GetSysColor"},  {281, 1, "GetSysColorBrush"},
        {232, 7, "SetWindowPos"},      {239, 8, "DialogBoxParam"},
        {262, 2, "GetWindow"},         {286, 0, "GetDesktopWindow"},
        {287, 1, "GetLastActivePopup"},{420, 0, "wsprintf"},        // cdecl: the caller removes the arguments
        {430, 4, "lstrcmp"},           {431, 2, "AnsiUpper"},
        {432, 2, "AnsiLower"},
        {437, 3, "AnsiUpperBuff"},   {438, 3, "AnsiLowerBuff"},
        {7, 3, "ExitWindows"},         {61, 7, "ScrollWindow"},
        {62, 4, "SetScrollPos"},       {63, 2, "GetScrollPos"},
        {64, 5, "SetScrollRange"},     {65, 6, "GetScrollRange"},
        {71, 1, "ShowCursor"},         {76, 4, "PtInRect"},
        {77, 4, "OffsetRect"},         {78, 4, "InflateRect"},
        {79, 6, "IntersectRect"},      {80, 6, "UnionRect"},
        {84, 4, "DrawIcon"},           {96, 4, "CheckRadioButton"},
        {106, 1, "GetKeyState"},       {118, 2, "RegisterWindowMessage"},
        {119, 0, "GetMessagePos"},     {120, 0, "GetMessageTime"},
        {126, 3, "InvalidateRgn"},     {129, 2, "GetClassWord"},
        {130, 3, "SetClassWord"},      {163, 4, "CreateCaret"},
        {164, 0, "DestroyCaret"},      {165, 2, "SetCaretPos"},
        {166, 1, "HideCaret"},         {167, 1, "ShowCaret"},
        {50, 4, "FindWindow"},         {121, 3, "SetWindowsHook"},
        // PSCRIPT.DRV of the Win98 SE CD.
        {244, 4, "EqualRect"},         {427, 6, "FindWindowEx"},
        {171, 6, "WinHelp"},
        {188, 1, "SetSysModalWindow"}, {235, 6, "DefHookProc"},
        {268, 2, "GlobalAddAtom"},     {269, 1, "GlobalDeleteAtom"},
        {270, 2, "GlobalFindAtom"},    {272, 1, "IsZoomed"},
        {271, 4, "GlobalGetAtomName"},
        {368, 2, "CopyIcon"},          {370, 3, "GetWindowPlacement"},
        {371, 3, "SetWindowPlacement"},{413, 3, "DeleteMenu"},
        {414, 6, "ModifyMenu"},        {433, 1, "IsCharAlpha"},
        {434, 1, "IsCharAlphaNumeric"},{457, 1, "DestroyIcon"},
        {466, 3, "DrawFocusRect"},         {445, 6, "DefFrameProc"},
        {447, 5, "DefMDIChildProc"},   {451, 3, "TranslateMDISysAccel"},
        {471, 4, "lstrcmpi"},
        {24, 3, "RemoveProp"},         {25, 3, "GetProp"},
        {26, 4, "SetProp"},            {59, 1, "SetActiveWindow"},
        {60, 0, "GetActiveWindow"},    {89, 6, "CreateDialog"},
        {90, 3, "IsDialogMessage"},    {97, 3, "CheckDlgButton"},
        {98, 2, "IsDlgButtonChecked"}, {154, 3, "CheckMenuItem"},
        {155, 3, "EnableMenuItem"},    {156, 2, "GetSystemMenu"},
        {159, 2, "GetSubMenu"},        {177, 3, "LoadAccelerators"},
        {178, 4, "TranslateAccelerator"}, {483, 5, "SystemParametersInfo"},
        // The breadth of the fourth part of step 9, in the order in which the
        // NE programs of the tree and of the collection ask for them.
        {10, 5, "SetTimer"},           {12, 2, "KillTimer"},
        {15, 0, "GetCurrentTime"},     {16, 2, "ClipCursor"},
        {17, 2, "GetCursorPos"},       {18, 1, "SetCapture"},
        {19, 0, "ReleaseCapture"},     {28, 3, "ClientToScreen"},
        {30, 2, "WindowFromPoint"},    {43, 1, "CloseWindow"},
        {48, 2, "IsChild"},            {67, 1, "GetWindowDC"},
        {70, 2, "SetCursorPos"},       {83, 4, "FrameRect"},
        {94, 4, "SetDlgItemInt"},      {95, 5, "GetDlgItemInt"},
        {100, 6, "DlgDirList"},        {102, 5, "AdjustWindowRect"},
        {105, 2, "FlashWindow"},       {112, 0, "WaitMessage"},
        {116, 5, "PostAppMessage"},    {131, 2, "GetClassLong"},
        {132, 4, "SetClassLong"},      {137, 1, "OpenClipboard"},
        {138, 0, "CloseClipboard"},    {139, 0, "EmptyClipboard"},
        // Clipboard entry points use their public ordinal and argument sizes.
        // GetClipboardOwner and CountClipboardFormats take no arguments;
        // GetClipboardData takes only the format word, even though its
        // internal helper also receives a zero argument.
        {140, 0, "GetClipboardOwner"}, {141, 2, "SetClipboardData"},
        {142, 1, "GetClipboardData"},  {143, 0, "CountClipboardFormats"},
        {144, 1, "EnumClipboardFormats"},
        {146, 4, "GetClipboardFormatName"},
        {216, 4, "UserSeeUserDo"},
        {257, 3, "GetNextDriver"},       {258, 5, "MapWindowPoints"},   {448, 6, "DrawAnimatedRects"},
        {147, 1, "SetClipboardViewer"}, {148, 0, "GetClipboardViewer"},
        {149, 2, "ChangeClipboardChain"},
        {145, 2, "RegisterClipboardFormat"},
        {150, 3, "LoadMenu"}, {151, 0, "CreateMenu"},          {152, 1, "DestroyMenu"},
        {158, 2, "SetMenu"},           {160, 1, "DrawMenuBar"},
        {161, 6, "GetMenuString"},     {175, 3, "LoadBitmap"},
        {224, 1, "GetWindowTask"},     {228, 3, "GetNextDlgTabItem"},
        {229, 1, "GetTopWindow"},      {230, 2, "GetNextWindow"},
        {236, 0, "GetCapture"},        {243, 0, "GetDialogBaseUnits"},
        {190, 4, "GetUpdateRect"},     {237, 3, "GetUpdateRgn"},
        {250, 3, "GetMenuState"},      {263, 1, "GetMenuItemCount"},
        {251, 6, "SendDriverMessage"}, {252, 6, "OpenDriver"},
        {253, 5, "CloseDriver"},       {254, 1, "GetDriverModuleHandle"},
        {255, 8, "DefDriverProc"},       {256, 3, "GetDriverInfo"},
        {264, 2, "GetMenuItemID"},     {266, 1, "SetMessageQueue"},
        {277, 1, "GetDlgCtrlID"},      {278, 0, "GetDesktopHwnd"},      {282, 3, "SelectPalette"},
        {283, 1, "RealizePalette"},    {290, 5, "RedrawWindow"},
        {403, 3, "UnregisterClass"},   {410, 6, "InsertMenu"},
        {411, 5, "AppendMenu"},        {452, 17, "CreateWindowEx"},
        {454, 7, "AdjustWindowRectEx"},
        {472, 2, "AnsiNext"},          {473, 4, "AnsiPrev"},
        {421, 6, "wvsprintf"},         {407, 9, "CreateIcon"}, {404, 5, "GetClassInfo"},
        {512, 6, "WNetGetConnection"}, {513, 1, "WNetGetCaps"},
        {291, 5, "SetWindowsHookEx"},  {292, 2, "UnhookWindowsHookEx"},
        // Legacy UnhookWindowsHook and directory-dialog entry points.
        // DlgDirSelect and its combo-box variant take eight bytes;
        // DlgDirListComboBox takes twelve; the Ex selection variants take
        // ten.
        {99, 4, "DlgDirSelect"},       {194, 4, "DlgDirSelectComboBox"},
        {195, 6, "DlgDirListComboBox"}, {234, 3, "UnhookWindowsHook"},
        {422, 5, "DlgDirSelectEx"},    {423, 5, "DlgDirSelectComboBoxEx"},
        // DialogBoxIndirectParam takes fourteen bytes and CallNextHookEx
        // takes twelve. DialogBoxIndirect and CreateDialogIndirect supply a
        // zero parameter internally, so their callers provide two fewer
        // words.
        {218, 5, "DialogBoxIndirect"}, {219, 6, "CreateDialogIndirect"},
        {240, 7, "DialogBoxIndirectParam"}, {241, 8, "CreateDialogParam"},
        {242, 8, "CreateDialogIndirectParam"}, {293, 6, "CallNextHookEx"},
        // The communication functions, USER segment 22 (src/win16_comm.inc
        // answers them; the counts are the `retf` of their entries).
        {200, 4, "OpenComm"},          {201, 2, "SetCommState"},
        {202, 3, "GetCommState"},      {203, 3, "GetCommError"},
        {204, 4, "ReadComm"},          {205, 4, "WriteComm"},
        {206, 2, "TransmitCommChar"},  {207, 1, "CloseComm"},
        {208, 2, "SetCommEventMask"},  {209, 2, "GetCommEventMask"},
        {210, 1, "SetCommBreak"},      {211, 1, "ClearCommBreak"},
        {212, 2, "UngetCommChar"},     {213, 4, "BuildCommDCB"},
        {214, 2, "EscapeCommFunction"}, {215, 2, "FlushComm"},
        {395, 3, "GetIconInfo"},
        // Argument WORD counts for the additional Win16 USER gates. Use the
        // API signatures for entries whose cleanup occurs through an indirect
        // call.
        {14, 0, "GetTimerResolution"}, {20, 1, "SetDoubleClickTime"}, {21, 0, "GetDoubleClickTime"},
        {44, 1, "OpenIcon"}, {52, 0, "AnyPopup"}, {54, 4, "EnumWindows"},
        {55, 5, "EnumChildWindows"}, {82, 3, "InvertRect"}, {103, 3, "MapDialogRect"},
        {115, 2, "ReplyMessage"}, {123, 3, "CallMsgFilter"}, {128, 2, "ValidateRgn"}, {153, 6, "ChangeMenu"},
        {162, 4, "HiliteMenuItem"}, {168, 1, "SetCaretBlinkTime"}, {169, 0, "GetCaretBlinkTime"},
        {170, 1, "ArrangeIconicWindows"}, {172, 2, "SwitchToThisWindow"}, {181, 5, "SetSysColors"},
        {183, 2, "GetCaretPos"}, {185, 11, "GrayString"}, {186, 1, "SwapMouseButton"},
        {187, 0, "EndMenu"}, {189, 0, "GetSysModalWindow"}, {191, 3, "ChildWindowFromPoint"},
        {192, 0, "InSendMessage"}, {193, 1, "IsClipboardFormatAvailable"},
        {196, 10, "TabbedTextOut"}, {197, 7, "GetTabbedTextExtent"},
        {198, 2, "CascadeChildWindows"}, {199, 2, "TileChildWindows"}, {220, 2, "LoadMenuIndirect"},
        {221, 10, "ScrollDC"}, {222, 2, "GetKeyboardState"}, {223, 2, "SetKeyboardState"},
        {225, 5, "EnumTaskWindows"}, {227, 3, "GetNextDlgGroupItem"}, {233, 2, "SetParent"},
        {238, 2, "ExcludeUpdateRgn"}, {247, 0, "GetCursor"}, {248, 0, "GetOpenClipboardWindow"},
        {249, 1, "GetAsyncKeyState"}, {259, 1, "BeginDeferWindowPos"}, {260, 8, "DeferWindowPos"},
        {261, 1, "EndDeferWindowPos"}, {265, 2, "ShowOwnedPopups"}, {267, 3, "ShowScrollBar"},
        {288, 0, "GetMessageExtraInfo"}, {294, 1, "LockWindowUpdate"}, {309, 2, "GetClipCursor"},
        {319, 11, "ScrollWindowEx"}, {334, 1, "GetQueueStatus"}, {335, 0, "GetInputState"},
        {358, 1, "IsMenu"}, {359, 4, "GetDCEx"}, {364, 6, "LookupIconIdFromDirectoryEx"},
        {373, 6, "SubtractRect"}, {375, 10, "DrawTextEx"}, {376, 2, "SetMessageExtraInfo"},
        {382, 3, "SetWindowContextHelpId"}, {383, 1, "GetWindowContextHelpId"},
        {384, 3, "SetMenuContextHelpId"}, {385, 1, "GetMenuContextHelpId"}, {390, 6, "CopyImage"},
        {394, 9, "DrawIconEx"}, {397, 2, "RegisterClassEx"}, {398, 5, "GetClassInfoEx"},
        {399, 4, "ChildWindowFromPointEx"}, {402, 3, "GetPriorityClipboardFormat"},
        {406, 9, "CreateCursor"}, {412, 3, "RemoveMenu"}, {415, 0, "CreatePopupMenu"},
        {416, 8, "TrackPopupMenu"}, {417, 0, "GetMenuCheckMarkDimensions"},
        {418, 5, "SetMenuItemBitmaps"}, {428, 7, "TileWindows"}, {429, 7, "CascadeWindows"},
        {435, 1, "IsCharUpper"}, {436, 1, "IsCharLower"}, {441, 5, "InsertMenuItem"},
        {443, 5, "GetMenuItemInfo"}, {446, 5, "SetMenuItemInfo"}, {449, 12, "DrawState"},
        {450, 10, "CreateIconFromResourceEx"}, {458, 1, "DestroyCursor"}, {464, 6, "DragObject"},
        {465, 3, "DragDetect"}, {475, 5, "SetScrollInfo"}, {476, 4, "GetScrollInfo"},
        {477, 2, "GetKeyboardLayoutName"}, {478, 3, "LoadKeyboardLayout"},
        {479, 4, "MenuItemFromPoint"}, {480, 1, "GetUserLocalObjType"}, {501, 7, "WNetOpenJob"},
        {502, 5, "WNetCloseJob"}, {503, 3, "WNetAbortJob"}, {504, 3, "WNetHoldJob"},
        {505, 3, "WNetReleaseJob"}, {506, 3, "WNetCancelJob"}, {507, 4, "WNetSetJobCopies"},
        {508, 6, "WNetWatchQueue"}, {509, 2, "WNetUnwatchQueue"}, {510, 6, "WNetLockQueueData"},
        {511, 2, "WNetUnlockQueueData"}, {514, 1, "WNetDeviceMode"}, {515, 4, "WNetBrowseDialog"},
        {516, 4, "WNetGetUser"}, {517, 6, "WNetAddConnection"}, {518, 3, "WNetCancelConnection"},
        {519, 2, "WNetGetError"}, {520, 5, "WNetGetErrorText"}, {521, 0, "WNetEnable"},
        {522, 0, "WNetDisable"}, {523, 3, "WNetRestoreConnection"}, {524, 5, "WNetWriteJob"},
        {525, 2, "WNetConnectDialog"}, {526, 2, "WNetDisconnectDialog"},
        {527, 2, "WNetConnectionDialog"}, {528, 3, "WNetViewQueueDialog"},
        {529, 4, "WNetPropertyDialog"}, {530, 4, "WNetGetDirectoryType"},
        {531, 4, "WNetDirectoryNotify"}, {532, 4, "WNetGetPropertyText"}, {534, 3, "WNetLogon"},
        {600, 0, "GetShellWindow"}, {608, 0, "GetForegroundWindow"},
        {609, 1, "SetForegroundWindow"}, {620, 4, "ChangeDisplaySettings"},
        {621, 6, "EnumDisplaySettings"}, {640, 9, "MsgWaitForMultipleObjects"},
        {650, 3, "ActivateKeyboardLayout"}, {651, 2, "GetKeyboardLayout"},
        {652, 3, "GetKeyboardLayoutList"}, {654, 2, "UnloadKeyboardLayout"},
        {656, 5, "DrawFrameControl"}, {657, 9, "DrawCaptionTemp"}, {659, 5, "DrawEdge"},
        {660, 5, "DrawCaption"}, {663, 3, "GetMenuDefaultItem"}, {664, 3, "SetMenuDefaultItem"},
        {665, 5, "GetMenuItemRect"}, {666, 5, "CheckMenuRadioItem"}, {667, 7, "TrackPopupMenuEx"},
        {668, 3, "SetWindowRgn"}, {669, 2, "GetWindowRgn"},
    };
    count = sizeof table / sizeof table[0];
    return table;
}
// GDI.EXE.
inline const Ordinal *GdiOrdinals(U &count)
{
    static const Ordinal table[] = {
        {121, 1, "Death"}, {122, 7, "Resurrection"},
        {1, 3, "SetBkColor"},       {2, 2, "SetBkMode"},
        {9, 3, "SetTextColor"},     {19, 3, "LineTo"},
        {20, 3, "MoveTo"},          {33, 6, "TextOut"},
        {45, 2, "SelectObject"},    {56, 15, "CreateFont"},
        {57, 2, "CreateFontIndirect"}, {66, 2, "CreateSolidBrush"},
        {58, 3, "CreateHatchBrush"},   {79, 1, "GetDCOrg"},
        {462, 1, "IsGDIObject"},       {403, 2, "GdiInit2"},
        {461, 2, "SetObjectOwner"},    {489, 10, "CreateDIBSection"},
        {69, 1, "DeleteObject"},    {80, 2, "GetDeviceCaps"},
        {87, 1, "GetStockObject"},  {91, 4, "GetTextExtent"},
        {93, 3, "GetTextMetrics"},
        // The breadth of the fourth part of step 9: the ordinals the NE
        // programs of the tree and of the collection call. A word argument is
        // one word, a LONG and a far pointer are two.
        {3, 2, "SetMapMode"},          {4, 2, "SetROP2"},
        {7, 2, "SetStretchBltMode"},   {11, 3, "SetWindowOrg"},
        {12, 3, "SetWindowExt"},       {13, 3, "SetViewportOrg"},
        {14, 3, "SetViewportExt"},     {24, 5, "Ellipse"},
        {26, 9, "Pie"},                {27, 5, "Rectangle"},
        {348, 9, "Chord"},
        {29, 7, "PatBlt"},             {30, 1, "SaveDC"},
        {31, 5, "SetPixel"},           {34, 10, "BitBlt"},
        {35, 12, "StretchBlt"},        {38, 7, "Escape"},
        {39, 2, "RestoreDC"},          {44, 2, "SelectClipRgn"},
        {47, 4, "CombineRgn"},         {48, 6, "CreateBitmap"},
        {50, 2, "CreateBrushIndirect"}, {51, 3, "CreateCompatibleBitmap"}, {52, 1, "CreateCompatibleDC"},
        {53, 8, "CreateDC"},           {60, 1, "CreatePatternBrush"},
        {61, 4, "CreatePen"}, {62, 2, "CreatePenIndirect"},
        {64, 4, "CreateRectRgn"},      {65, 2, "CreateRectRgnIndirect"},
        {67, 4, "DPtoLP"}, {99, 4, "LPtoDP"}, {148, 3, "SetBrushOrg"}, {439, 16, "StretchDIBits"},
        {68, 1, "DeleteDC"},           {70, 7, "EnumFonts"},
        // These GDI entries share the same implementation and argument
        // layout.
        {330, 7, "EnumFontFamilies"},
        {74, 5, "GetBitmapBits"},
        {77, 3, "GetClipBox"},         {81, 1, "GetMapMode"},
        {82, 4, "GetObject"},          {83, 3, "GetPixel"},
        {104, 3, "RectVisible"},       {106, 5, "SetBitmapBits"},
        {128, 3, "MulDiv"},            {346, 2, "SetTextAlign"},
        {351, 11, "ExtTextOut"},
        {360, 2, "CreatePalette"},     {442, 10, "CreateDIBitmap"},
        // GetPaletteEntries takes ten bytes: handle, starting index, count
        // and far output pointer. Each PALETTEENTRY is four bytes on both
        // sides.
        {363, 5, "GetPaletteEntries"},
        // GetSystemPaletteEntries takes DC, starting index, count and a far
        // pointer, matching GetPaletteEntries' ten-byte layout.
        {375, 5, "GetSystemPaletteEntries"},
        {373, 2, "SetSystemPaletteUse"}, {374, 1, "GetSystemPaletteUse"},
        {367, 5, "AnimatePalette"},      {368, 2, "ResizePalette"},
        {370, 3, "GetNearestPaletteIndex"}, {366, 1, "UpdateColors"},
        // GDI.
        {364, 5, "SetPaletteEntries"},
        // One word identifies the GDI object.
        {150, 1, "UnrealizeObject"},
        {119, 2, "AddFontResource"},   {153, 8, "CreateIC"},
        // StartDoc and SetAbortProc take six argument bytes;
        // the other document APIs take two.
        {377, 3, "StartDoc"},          {378, 1, "EndDoc"},
        {379, 1, "StartPage"},         {380, 1, "EndPage"},
        {381, 3, "SetAbortProc"},      {382, 1, "AbortDoc"},
        // FloodFill takes ten argument bytes and GetNearestColor takes six.
        // GetNearestColor treats its last two words as a single COLORREF.
        {25, 5, "FloodFill"},          {154, 3, "GetNearestColor"},
        // This DIB transfer takes fourteen words, including usage, two far
        // pointers and the DC.
        {443, 14, "SetDIBitsToDevice"},
        // SetDIBits and GetDIBits take nine words: usage, BITMAPINFO and bits
        // far pointers, row count, starting row, bitmap and DC.
        {440, 9, "SetDIBits"},          {441, 9, "GetDIBits"},
        // These eight DC queries take one handle word and return a coordinate
        // pair in DX:AX.
        {95, 1, "GetViewportOrg"},       {94, 1, "GetViewportExt"},
        {96, 1, "GetWindowExt"},        {97, 1, "GetWindowOrg"},
        {15, 3, "OffsetWindowOrg"},     {17, 3, "OffsetViewportOrg"},
        {21, 5, "ExcludeClipRect"},     {22, 5, "IntersectClipRect"},
        {75, 1, "GetBkColor"},          {76, 1, "GetBkMode"},
        {85, 1, "GetROP2"},             {90, 1, "GetTextColor"},
        {92, 4, "GetTextFace"},         {161, 3, "PtInRegion"},
        {172, 5, "SetRectRgn"},
        // Metafile entry-point argument sizes. CreateMetaFile accepts only
        // the filename far pointer; its internal helper supplies an
        // additional zero word.
        {123, 2, "PlayMetaFile"},       {124, 2, "GetMetaFile"},
        {125, 2, "CreateMetaFile"},     {126, 1, "CloseMetaFile"},
        {127, 1, "DeleteMetaFile"},     {151, 3, "CopyMetaFile"},
        {159, 1, "GetMetaFileBits"},    {160, 1, "SetMetaFileBits"},
        {196, 1, "SetMetaFileBitsBetter"}, {410, 1, "IsValidMetaFile"},
        // Printer driver gates; see src/win16_print.cpp. The two calls
        // consume 0x14 and 0x18 stack bytes.
        {281, 10, "DrvSetPrinterData"}, {282, 12, "DrvGetPrinterData"},
        // Print-job entry-point argument sizes.
        {240, 5, "OpenJob"},        {241, 4, "WriteSpool"},
        {243, 1, "CloseJob"},       {244, 2, "DeleteJob"},
        {246, 1, "StartSpoolPage"}, {247, 1, "EndSpoolPage"},
        {253, 1, "DeleteSpoolPage"},
        // The driver TrueType entry point takes twelve argument bytes.
        {300, 6, "EngineEnumerateFont"},
        {301, 2, "EngineDeleteFont"}, {302, 6, "EngineRealizeFont"},
        {315, 8, "EngineRealizeFontExt"},
        {299, 6, "EngineGetCharWidthEx"}, {303, 6, "EngineGetCharWidth"},
        {304, 3, "EngineSetFontContext"}, {305, 11, "EngineGetGlyphBmp"},
        {316, 7, "EngineGetCharWidthStr"}, {317, 11, "EngineGetGlyphBmpExt"},
        // The pass over the whole export table of GDI.EXE
        // (src/win16_gdi_more.inc), with the counts
        // tools/win16_ordinal_inventory.py measures in GDI.EXE of the tree.
        {6, 2, "SetPolyFillMode"}, {8, 2, "SetTextCharacterExtra"}, {10, 3, "SetTextJustification"},
        {16, 5, "ScaleWindowExt"}, {18, 5, "ScaleViewportExt"}, {23, 9, "Arc"},
        {28, 7, "RoundRect"}, {32, 3, "OffsetClipRgn"}, {36, 4, "Polygon"}, {37, 4, "Polyline"},
        {40, 3, "FillRgn"}, {41, 5, "FrameRgn"}, {42, 2, "InvertRgn"}, {43, 2, "PaintRgn"},
        {49, 2, "CreateBitmapIndirect"}, {54, 4, "CreateEllipticRgn"},
        {55, 2, "CreateEllipticRgnIndirect"}, {63, 4, "CreatePolygonRgn"}, {72, 2, "EqualRgn"},
        {78, 1, "GetCurrentPosition"}, {84, 1, "GetPolyFillMode"}, {88, 1, "GetStretchBltMode"},
        {89, 1, "GetTextCharacterExtra"}, {101, 3, "OffsetRgn"}, {103, 3, "PtVisible"},
        {134, 3, "GetRgnBox"}, {136, 2, "RemoveFontResource"}, {149, 1, "GetBrushOrg"},
        {156, 3, "CreateDiscardableBitmap"}, {162, 1, "GetBitmapDimension"},
        {163, 3, "SetBitmapDimension"}, {181, 3, "RectInRegion"}, {193, 4, "SetBoundsRect"},
        {194, 4, "GetBoundsRect"}, {307, 5, "GetCharABCWidths"}, {309, 11, "GetGlyphOutline"},
        {311, 9, "GetFontData"}, {313, 3, "GetRasterizerCaps"}, {332, 4, "GetKerningPairs"},
        {345, 1, "GetTextAlign"}, {349, 3, "SetMapperFlags"}, {350, 5, "GetCharWidth"},
        {361, 3, "GDISelectPalette"}, {362, 1, "GDIRealizePalette"}, {372, 6, "ExtFloodFill"},
        {376, 3, "ResetDC"}, {444, 6, "CreateRoundRectRgn"}, {450, 6, "PolyPolygon"},
        {451, 6, "CreatePolyPolygonRgn"}, {465, 3, "RectVisible_EHH"}, {466, 3, "RectInRegion_EHH"},
        {468, 3, "GetBitmapDimensionEx"}, {469, 3, "GetBrushOrgEx"},
        {470, 3, "GetCurrentPositionEx"}, {471, 6, "GetTextExtentPoint"},
        {472, 3, "GetViewportExtEx"}, {473, 3, "GetViewportOrgEx"}, {474, 3, "GetWindowExtEx"},
        {475, 3, "GetWindowOrgEx"}, {476, 5, "OffsetViewportOrgEx"}, {477, 5, "OffsetWindowOrgEx"},
        {478, 5, "SetBitmapDimensionEx"}, {479, 5, "SetViewportExtEx"},
        {480, 5, "SetViewportOrgEx"}, {481, 5, "SetWindowExtEx"}, {482, 5, "SetWindowOrgEx"},
        {483, 5, "MoveToEx"}, {484, 7, "ScaleViewportExtEx"}, {485, 7, "ScaleWindowExtEx"},
        {486, 3, "GetAspectRatioFilterEx"}, {502, 4, "PolyBezier"}, {503, 4, "PolyBezierTo"},
        {508, 3, "ExtSelectClipRgn"}, {511, 1, "AbortPath"}, {512, 1, "BeginPath"},
        {513, 1, "CloseFigure"}, {514, 1, "EndPath"}, {515, 1, "FillPath"}, {516, 1, "FlattenPath"},
        {518, 1, "PathToRegion"}, {519, 2, "SelectClipPath"}, {520, 1, "StrokeAndFillPath"},
        {521, 1, "StrokePath"}, {522, 1, "WidenPath"}, {523, 10, "ExtCreatePen"},
        {524, 1, "GetArcDirection"}, {525, 2, "SetArcDirection"}, {526, 3, "GetMiterLimit"},
        {527, 5, "SetMiterLimit"}, {529, 1, "CreateHalftonePalette"}, {602, 5, "SetDIBColorTable"},
        {603, 5, "GetDIBColorTable"}, {606, 4, "SetMagicColors"}, {611, 3, "GetRandomRgn"},
        {612, 1, "GetTextCharset"}, {616, 1, "GetFontLanguageInfo"},
    };
    count = sizeof table / sizeof table[0];
    return table;
}
inline U WordsOf(const Ordinal *table, U count, U ordinal, const char *&name)
{
    for (U i = 0; i < count; ++i)
        if (table[i].ordinal == ordinal)
        {
            name = table[i].name;
            return table[i].words;
        }
    name = nullptr;
    return Unported;
}

// ---------------------------------------------------------------- resources
// The resource types of the NE table, as the 16-bit loader names them. The
// integer form has bit 15 set.
enum ResourceType : U
{
    ResourceCursor = 0x8001, ResourceBitmap = 0x8002, ResourceIcon = 0x8003,
    ResourceMenu = 0x8004, ResourceDialog = 0x8005, ResourceString = 0x8006,
    ResourceAccelerator = 0x8009, ResourceGroupCursor = 0x800c, ResourceGroupIcon = 0x800e,
};
// A string resource block holds sixteen counted strings; the identifier picks
// the block and the position inside it, as LoadString at USER does.
inline U StringBlock(U id) { return (id >> 4) + 1; }
inline U StringIndex(U id) { return id & 15; }
} // namespace Win98Win16User
