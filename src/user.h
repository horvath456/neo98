// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_menu.h"
#include "win98_message_metadata.h"
#include "win98_scroll_family.h"
#include "gdi.h"
#include "text_edit.h"
#include "win98_dialog.h"
#include "win98_paint.h"
#include "win98_windows.h"
#include "win98_activation.h"
#include "win98_window_hit.h"
#include "win98_window_region.h"
#include "win98_redraw_window.h"
#include "win98_post_thread_message.h"
#include "win98_minmax.h"

// Native USER infrastructure for the application ABI. This object/queue/window
// implementation is new code; it matches Windows 98 window behavior.
namespace User {
constexpr unsigned WindowCapacity = 1022;
// The text of a window without an edit buffer, terminator included. Win98's
// DefWindowProc keeps WM_SETTEXT text in the USER heap without a limit of its
// own; the static text of SHELL32's warning for bad applications is 439
// characters long, so 256 bytes lost it.
constexpr unsigned TitleCapacity = 1024;
using HWND = unsigned;
using HMENU = unsigned;
struct Message { unsigned hwnd, message, wparam, lparam, time; int x, y; };
static_assert(sizeof(Message) == 28, "32-bit MSG wire layout");
unsigned RegisterNativeClass(const unsigned fields[12],const char *name,const char *menu=nullptr);
unsigned GetNativeClassInfo(unsigned instance,const char *name,unsigned fields[12]);
unsigned UnregisterNativeClass(unsigned instance,const char *name);
// The classes of a 16-bit module that is freed.
void DestroyModuleClasses(unsigned instance);
// A stock brush of a process, which a class that goes must not delete.
void ProtectStockBrush(unsigned handle,bool on);
unsigned NativeClassError();
void OwnClassMenu(HWND,HMENU);
unsigned ClassAtomName(unsigned atom,char *out,unsigned capacity);
const char *ClassMenuName(unsigned token);
unsigned ClassAccess(HWND,int index,unsigned value,unsigned flags);
// Project-owned shared desktop process/task routing. PID0 is standalone native USER.
bool AttachProcess(unsigned pid,void*context);
unsigned ProcessContextCount();
void* ProcessContextAt(unsigned index);
void NoteHostPress(HWND h); // a start from the host counts as the last press, on this window
void* ProcessContextOf(unsigned pid); // the Application bound to a process id
void BindTask(unsigned pid,unsigned tid);
void ReleaseProcessWindows(unsigned pid);
void DestroyProcessWindows(unsigned pid);
// The lock of the screen's pixels while a program has the DirectDraw primary
// surface locked (src/user_screen.inc). One for the session; only drawing on the screen waits.
bool DrawsOnScreen(Gdi::HDC dc);
bool ScreenLockActive();
void LockScreen(unsigned process, unsigned thread);
void UnlockScreen(unsigned process, unsigned thread);
bool ScreenHeldBy(unsigned thread);
bool ScreenLockedElsewhere(unsigned process, unsigned thread);
bool ScreenMayDraw(unsigned process, unsigned thread);
void WaitScreen(unsigned process, unsigned thread);
void DoneScreenWait(unsigned process, unsigned thread);
bool ScreenGrantsPending(unsigned thread);
bool ParkScreen(unsigned thread, bool parked);
void DropScreenThread(unsigned process, unsigned thread);
void DetachProcess(unsigned pid);
unsigned WindowProcess(HWND);
unsigned CurrentProcess();
unsigned CurrentThread();
void* CallbackContext(void*fallback,HWND);
void* CurrentContext(void*fallback);
bool Initialize();
void ConfigureVersion(unsigned expected_version);
// The expected Windows version of the module behind an instance handle, the
// GetExpWinVer USER asks in CreateWindowEx before it sets bit 40h of byte 2Eh
// of the new window. Zero means unknown; the process version applies then.
using InstanceVersion = unsigned (*)(void *, unsigned instance);
void SetInstanceVersionProvider(InstanceVersion);
void Shutdown();
unsigned RegisterClass(const char *name, unsigned procedure, unsigned menu, unsigned background,unsigned extra=0);
bool WindowLong(HWND,int index,unsigned &value,bool write);
bool WindowWord(HWND,int index,unsigned &value,bool write);
bool TrackingMouseLeave(HWND);
unsigned WindowContextHelp(HWND);
bool SetWindowContextHelp(HWND,unsigned);
unsigned CreateWindowError();
// The instance CreateWindow was given; GWL_HINSTANCE and GWW_HINSTANCE read it.
unsigned WindowInstance(HWND);
unsigned SetWindowInstance(HWND, unsigned);
HWND CreateWindow(const char *name, const char *title, unsigned style, int x, int y,
                  int width, int height, HWND parent, unsigned menu, unsigned parameter, unsigned thread = 1,unsigned instance = 0);
HWND GetDesktopWindow();
HWND GetWindow(HWND, unsigned command);
HWND GetTopWindow(HWND);
Win98Windows::Node WindowNode(HWND);
bool WindowSnapshot(HWND parent, unsigned *out, unsigned capacity, unsigned &count);
// Native USER geometry and the shared monitor work area.
bool WorkArea(Gdi::RECT *);
bool MinMaxDefaults(HWND, Win98MinMax::Info &);
bool MinMaxAdjust(HWND, Win98MinMax::Info &);
bool MaximizedRect(HWND, Gdi::RECT &);
unsigned WindowThread(HWND);
HWND ThreadWindow(unsigned process, unsigned thread);
bool OrderWindow(HWND, HWND after, bool order_owned = true);
unsigned PrepareOwnedPositions(const unsigned position[7], unsigned (*out)[7], unsigned capacity);
void ParentStorage(HWND,HWND,unsigned,unsigned);
unsigned ParentLinkService(unsigned,HWND,HWND,HWND);
// CompleteCreation follows successful WM_NCCREATE/WM_CREATE/WM_SIZE callbacks.
void CompleteCreation(HWND);
bool CreationPending(HWND);
void SwapCreationBounds(HWND, Gdi::RECT &);
void CalculateClientRect(HWND, Gdi::RECT &);
void ApplyClientRect(HWND, const Gdi::RECT &);
void MarkSizeMove(HWND);
HWND OwnedWindow(HWND owner); // first top-level window owned by it
unsigned WindowIcon(HWND, bool large);
// Last WM_QUERYDRAGICON result, or zero if never requested.
unsigned WindowDragIcon(HWND);
void WantDragIcon(HWND); // an answer of zero is asked again (USER asks on every draw)
unsigned SetWindowIcon(HWND, bool large, unsigned icon);
bool TakeSizeMove(HWND);
using ShellEvent = void (*)(void *, unsigned code, HWND, unsigned parameter);
void SetShellHookOwner(void *context);
void SetShellEvents(void *, ShellEvent);
using ShellPosition = void (*)(void *, HWND);
using ShellMessage = void (*)(void *, HWND, unsigned, unsigned, unsigned);
unsigned RegisterShellWindow(HWND, void *, ShellPosition, ShellMessage);
HWND RegisteredShellWindow();
void ConstrainShellPosition(HWND, unsigned &after, unsigned &flags);
using PropertyCleanup = void (*)(void *, HWND);
void SetPropertyCleanup(void *, PropertyCleanup);
unsigned PropertyList(HWND);
void SetPropertyList(HWND, unsigned);
unsigned CbtDestroyFlags(HWND,unsigned);
HWND CbtDetachFirst(HWND);
unsigned ScrollState(HWND,unsigned operation,unsigned bar,Win98Scroll::Info*,unsigned value,bool *layout=nullptr);
// USER: the parts of the nine USER32 information queries
// that only the window record knows.
bool ClientScreenRect(HWND, Gdi::RECT *);
bool CaptionBar(HWND, Gdi::RECT *);
unsigned CaptionPressed(HWND);
struct ScrollBarLayoutView {
    Gdi::RECT bar{};
    int line = 0, thumb_start = 0, thumb_end = 0;
    unsigned disabled = 0, pressed = 0, vertical = 0;
    bool styled = false, present = false, empty_first = false, empty_last = false;
};
bool ScrollBarLayout(HWND, int object, ScrollBarLayoutView &);
bool DestroyWindow(HWND);
// Locks redraw and drawing for one window subtree until the owning task calls
// LockWindowUpdate(0). A second window or another task cannot replace it.
bool LockWindowUpdate(HWND);
void ReleaseWindowUpdateLock(unsigned process, unsigned thread);
unsigned Procedure(HWND);
unsigned SetProcedure(HWND, unsigned);
unsigned Style(HWND);
unsigned SetStyle(HWND, unsigned);
// SetWindowLong stores GWL_STYLE/GWL_EXSTYLE without recalculating geometry.
// SetWindowPos with SWP_FRAMECHANGED triggers RelayoutFrame.
unsigned SetStyleWords(HWND, unsigned style, bool extended);
// Recalculate frame, client rectangle and edge metrics without moving the
// window, for SWP_FRAMECHANGED.
void RelayoutFrame(HWND);
// Window flag for modules targeting Windows 4.0 or later. USER uses it to
// enable WM_STYLECHANGING, among other behavior.
bool ModernWindow(HWND);
// GWL_HWNDPARENT selects the parent for WS_CHILD and the owner otherwise.
// Children require sibling-chain updates, so return false and let the caller
// use SetParent.
bool SetOwnerField(HWND, HWND);
unsigned Parameter(HWND);
HWND Parent(HWND);
unsigned ControlID(HWND);
// USER writes a new control id into an existing window record.
bool SetControlID(HWND,unsigned);
bool IsWindow(HWND);
HWND FindWindow(const char *klass,const char *title);
HWND FindWindowEx(HWND parent,HWND after,const char *klass,const char *title);
unsigned GetClassName(HWND,char *,unsigned);
bool IsEdit(HWND);
// Whether a multiline EDIT stands in a dialog; mark sets it.
bool EditInDialog(HWND,bool mark=false);
bool GetEditFormat(HWND,Gdi::RECT&);
bool SetEditFormat(HWND,const Gdi::RECT&,bool repaint);
bool ResetEditFormat(HWND,bool repaint);
bool SetEditTabStops(HWND,const int*,unsigned count);
bool EditCaretInfo(HWND,Gdi::POINT&,unsigned&,bool&);
bool IsButton(HWND);
// STATIC: the image of SS_ICON (USER keeps it at window+5Ch) and its size.
bool IsLabel(HWND);
unsigned LabelImage(HWND);
unsigned SetLabelImage(HWND,unsigned image,int width,int height);
void AdoptButton(HWND);
void AdoptLabel(HWND);
bool AdoptEdit(HWND,const char *title=nullptr);
bool CanvasInfo(HWND,bool&,bool&);
unsigned CommonControl(HWND); // 1 tree view, 2 list view, 3 status bar, 4 listbox, 5 toolbar, 6 combobox, 7 tab, 8 updown, 9 tooltip, 10 scrollbar
bool IsEnabled(HWND);
bool IsVisible(HWND);
HWND Owner(HWND);
unsigned ExtendedStyle(HWND);
void SetExtendedStyle(HWND, unsigned);
unsigned ControlMessage(HWND,unsigned,unsigned,unsigned);
void ClickButton(HWND);
bool ButtonInside(HWND,int,int);
void BeginControlCapture(HWND);
unsigned NextDialogItem(HWND,HWND,bool previous,bool group=false);
bool SystemIconSelected(HWND);                                      // the bar's menu mode selects the system menu
unsigned DialogChildControl(HWND root,HWND current);                 // USER
unsigned DialogNextControl(HWND root,HWND current,unsigned flags);   // USER
Win98Dialog::Node DialogNode(HWND);
using OwnerDraw = void (*)(void *, HWND parent, HWND control, Gdi::HDC, const Gdi::RECT &, unsigned state);
using ControlColor = unsigned (*)(void *, HWND parent, HWND control, Gdi::HDC, unsigned message);
using CommonDraw = void (*)(void *,HWND,Gdi::HDC,const Gdi::RECT &);
void SetCommonDraw(void *,CommonDraw);
void SetControlCallbacks(void *,OwnerDraw,ControlColor);
using GuestPaint = void (*)(void *, HWND);
void SetGuestPaint(void *,GuestPaint);   // WM_PAINT through a program's procedure of a STATIC
bool PaintLabel(HWND,Gdi::HDC);           // the STATIC class procedure's WM_PAINT
bool PaintButton(HWND,Gdi::HDC);          // synchronous BUTTON drawing, with its color callback
bool Rendering();
bool ButtonTracking(HWND,bool tracking);
using CaretDraw=unsigned(*)(void*,HWND,Gdi::HDC);
using CaretDestroy=void(*)(void*,HWND);
void SetCaretProvider(void*,CaretDraw,CaretDestroy);
// Takes the caret of a window off the screen before it is painted (hide) and
// puts it back afterwards; answers whether the window has the caret.
using CaretBracket=bool(*)(void*,HWND,bool hide);
void SetCaretBracket(void*,CaretBracket);
// The caption's system-menu icon: USER asks its caller to raster one icon.
using FrameIcon = bool (*)(void *, Gdi::HDC, int x, int y, unsigned icon, int width, int height);
void SetFrameIcon(void *, FrameIcon);
// The size an icon handle of the window's process names in its header, as
// BUTTON reads it from CURSORICONINFO for BS_ICON.
using IconSizeQuery = bool (*)(void *, unsigned icon, int &width, int &height);
void SetIconSizeQuery(void *, IconSizeQuery);
// Answers whether an icon handle of the window's process is USER's shared
// IDI_APPLICATION, the value [230h] USER compares against.
using DefaultIconQuery = bool (*)(void *, unsigned icon);
void SetDefaultIconQuery(void *, DefaultIconQuery);
using DragIcon = unsigned (*)(void *, HWND);
void SetDragIconProvider(void *, DragIcon);
// WM_CTLCOLORSCROLLBAR. USER sends it to the bar's owner and fills the
// track with the brush that comes back. The native message queue is
// asynchronous, so the owner answers through this provider instead; without
// one the default answer stands and USER's own dither brush is painted.
using ScrollBrush = unsigned (*)(void *, HWND owner, Gdi::HDC, unsigned message);
void SetScrollBrush(void *, ScrollBrush);
void CaretChanged();
bool CaretPending(HWND,const Gdi::RECT&);
bool ShowWindow(HWND, int command, bool defer_geometry = false);
bool MoveWindow(HWND, int x, int y, int width, int height, bool defer_geometry = false);
void FinishWindowPositions();
bool GetClientRect(HWND, Gdi::RECT *);
bool AdjustWindowRect(Gdi::RECT *, unsigned style, unsigned menu, unsigned extended);
bool GetWindowRect(HWND, Gdi::RECT *);
bool ScreenToClient(HWND, Gdi::POINT *);
void CursorPosition(Gdi::POINT *);
bool SetWindowText(HWND, const char *);
unsigned GetWindowText(HWND, char *, unsigned capacity);
unsigned TextLength(HWND);
unsigned CaptionLength(HWND);
unsigned CaptionText(HWND,char*,unsigned);
bool SetCaptionText(HWND,const char*);
HWND SetFocus(HWND);
HWND GetFocus();
using CaptureNotify=void(*)(void*,HWND,HWND);
void SetCaptureNotify(void*,CaptureNotify);
// WinEvents that USER raises itself. The flag
// is USER's ds:2AEh, "a hook exists"; the callback reaches the process whose
// thread runs USER at that moment.
using WinEventCallback=void(*)(void*,unsigned event,HWND,unsigned object,unsigned child,bool window_queue);
void SetWinEventCallback(void*,WinEventCallback);
void SetWinEventListening(bool);
bool WinEventListening();
void RaiseWinEvent(unsigned event,HWND,unsigned object,unsigned child,bool window_queue);
unsigned CaptureOperation(unsigned,HWND=0,unsigned=0);
HWND ThreadCapture(unsigned thread); // the capture of a thread's queue
HWND ProcessCapture(unsigned pid);   // a capture held by a thread of the process
bool TrackMouseLeave(HWND,bool enable);
bool TrackMouse(HWND,unsigned flags,unsigned hover_time);
void QueryMouseTrack(unsigned &flags,HWND &window,unsigned &hover_time);
constexpr unsigned MouseHoverTimerProcedure=0x71000061;
void DispatchMouseHoverTimer(HWND,unsigned serial);
unsigned CaptureMenuState(unsigned&,unsigned&);
HWND GetForegroundWindow();
HWND ForegroundActiveWindow();
// Hears every system colour a mode change moved (index, COLORREF), so the
// system brushes follow it (USER recolours them in place).
void SetSystemColourListener(void (*)(unsigned index, unsigned colorref));
// The idle time of the screen saver and the power state of the display
// (src/x86_screen_saver.cpp): hardware input restarts the idle time
// and turns the display on; 0 on, 1 low power, 2 off.
void NoteActivity(unsigned now);
unsigned IdleSince();
unsigned DisplayPower();
void SetDisplayPower(unsigned level);
bool SaverRequested();
void SetSaverRequested(bool requested);
// WM_SETHOTKEY and WM_GETHOTKEY of the default window procedure: 1 set, 2
// taken by another window, 0 no such window, ~0u a key that cannot be one.
unsigned SetWindowHotKey(HWND, unsigned key);
unsigned WindowHotKey(HWND);
Win98Activation::Window ActivationWindow(HWND);
void WriteActivationWindow(HWND, const Win98Activation::Window &);
// The activation queue of the foreground (USER ds:0AA6). Every other
// thread keeps its own queue below.
Win98Activation::Queue ActivationQueue();
void WriteActivationQueue(const Win98Activation::Queue &);
// Win98 gives every Win32 thread its own input queue. The queue is named by
// the thread id; 0 means there is no foreground queue.
unsigned ForegroundQueue();
Win98Activation::Globals &ActivationGlobals(); // USER's activation globals, one set per session
void SetForegroundQueue(unsigned thread);
Win98Activation::Queue ThreadActivation(unsigned thread);   // the foreground record for the foreground queue
void WriteThreadActivation(unsigned thread, const Win98Activation::Queue &);
void ForgetThreadQueue(unsigned thread);
// Queue flag 10h (queue word 14h): the queue may pass the foreground on once.
bool TakeQueuePass(unsigned thread);
void GrantQueuePass(unsigned thread);
bool QueuePass(unsigned thread);
void KeepQueueFocus(unsigned thread, unsigned hwnd);
unsigned KeptQueueFocus(unsigned thread);
// An activation posted into a queue by a foreground change from outside it.
void PostQueueActivation(unsigned thread, unsigned hwnd);
unsigned TakeQueueActivation(unsigned thread);
bool HasQueueActivation(unsigned thread);
// The foreground goes to a window that is no window of the guest
// (Application::HostActivation); the queue that had it deactivates its window
// when its thread next reads the queue.
void PostQueueLeave(unsigned thread, bool leave);
bool TakeQueueLeave(unsigned thread);
// A mouse activation the input router hands to the queue of the clicked
// window; that queue's thread carries it out when it next reads its queue.
void PostQueueMouseActivation(unsigned thread, unsigned hwnd, bool focus);
bool TakeQueueMouseActivation(unsigned thread, unsigned &hwnd, bool &focus);
// Process flag 4 (Windows 98 sets flags 5 when a process registers): the
// process may take the foreground once. Windows 98 uses it up; Windows 98 clears it
// for every process on a key or button press.
bool TakeStartPermission(unsigned pid);
bool StartPermission(unsigned pid);
// may process pid move the foreground on its own?
bool ForegroundUnlocked(unsigned pid);
void NotifyActivation(HWND);
void RefreshShellActivation();
bool NormalWindowRect(HWND, Gdi::RECT *);
bool SetShowState(HWND, int command, Gdi::RECT &target);
HWND MinimizeInsertAfter(HWND);
void PlacementPoints(HWND, Gdi::POINT &minimum, Gdi::POINT &maximum);
bool RestoresMaximized(HWND);
bool SetNormalWindowRect(HWND, const Gdi::RECT &);
bool EnableWindow(HWND, bool);
HWND GetDlgItem(HWND, unsigned id);
using PointHit=unsigned(*)(void*,HWND,unsigned,unsigned,unsigned);
Win98WindowHit::Window HitWindowState(HWND);
HWND PointWindow(unsigned operation,HWND,int,int,unsigned,void*,PointHit,unsigned task);
HWND ChildWindowFromPoint(HWND, int x, int y);
// Window regions: original USER decisions on native GDI regions.
using RegionPosition=unsigned(*)(void*,HWND,unsigned region,unsigned flags);
unsigned SetWindowRegion(HWND, unsigned region, bool redraw,void *context=nullptr,RegionPosition position=nullptr);
void ApplyWindowRegion(HWND,unsigned region);
unsigned GetWindowRegion(HWND, unsigned region);
unsigned WindowRegion(HWND);
bool WindowRegionContains(HWND, int screen_x, int screen_y);
Gdi::HDC GetDC(HWND);
Gdi::HDC GetDCEx(HWND,unsigned flags);
// USER GetWindowDC and the pieces GetDCEx needs on top of it: the
// window-origin lease of the screen DC with the window's visible region, the
// update rectangles in client coordinates and the screen rectangles the DCX
// clip flags remove.
Gdi::HDC GetWindowDC(HWND);
unsigned UpdateRectangles(HWND, Gdi::RECT *out, unsigned capacity);
unsigned VisibleChildRects(HWND, Gdi::RECT *out, unsigned capacity);
unsigned SiblingsAboveRects(HWND, Gdi::RECT *out, unsigned capacity);
// Returns the live USER window associated with an active USER-owned DC.
// Memory and standalone GDI DCs, released DCs, and stale windows return 0.
HWND WindowFromDC(Gdi::HDC);
HWND DisplayDCWindow(Gdi::HDC); // Live USER DCE; GetDC(NULL) names the desktop.
Gdi::HDC GetUpdateDC(HWND);
// GetDCEx transfers ownership of its screen-coordinate region to the DCE.
bool SetDCRegion(HWND, Gdi::HDC, unsigned, bool intersect);
bool ReleaseDC(HWND, Gdi::HDC);
int ScrollWindowArea(HWND, int dx, int dy, const Gdi::RECT *scroll, const Gdi::RECT *clip, Gdi::RECT *update, bool invalidate, bool erase, unsigned update_region = 0, bool scroll_children = false);
bool InvalidateRect(HWND, const Gdi::RECT *, bool erase);
bool ValidateRect(HWND, const Gdi::RECT *);
// USER: before UpdateWindow paints a window, the parents without
// WS_CLIPCHILDREN that still owe a paint lose the window's rectangle from
// their update region, so they do not erase over it afterwards.
void ClipAncestorUpdates(HWND);
bool AncestorNeedsPaint(HWND);
bool OrderUnchanged(HWND, HWND after);
bool GetUpdateRect(HWND, Gdi::RECT *);
bool TakeFramePaint(HWND);
void InvalidateFrame(HWND);
bool TakeInternalPaint(HWND);
bool NeedsErase(HWND);
void RememberErase(HWND, bool handled);
unsigned EraseToken(HWND);
void Erased(HWND, unsigned token);
void KeepErase(HWND);
unsigned Background(HWND);
using Paint = Win98Paint::Paint32;
Gdi::HDC BeginPaint(HWND, Paint &);
bool EndPaint(HWND, const Paint &);
// An open paint operation in the bound thread's queue.
bool PaintInProgress();
// The rectangle the move/size loop currently outlines, while Win98 runs
// without "show window contents while dragging".
// Nonclient hit test, move/size tracking and scrollbar tracking. The guest
// procedure answers WM_NCHITTEST, WM_SIZING, WM_MOVING and WM_GETMINMAXINFO
// through these providers; the loop runs nested on the guest fiber the way the
// menu tracker does.
using FrameSend = unsigned (*)(void *, HWND, unsigned message, unsigned wparam, unsigned lparam);
using FrameRect = unsigned (*)(void *, HWND, unsigned message, unsigned wparam, Gdi::RECT *);
using FrameLoop = unsigned (*)(void *);
using FrameCommit = void (*)(void *, HWND, int x, int y, int width, int height, unsigned flags);
void SetFrameCallbacks(void *, FrameSend, FrameRect, FrameLoop, FrameCommit);
unsigned FrameHitTest(HWND, int x, int y);   // DefWindowProc's own answer
unsigned HitTest(HWND, int x, int y);        // the window's answer, guest first
bool BeginSizeMove(HWND, unsigned command, bool mouse, int x, int y);
bool BeginScrollTrack(HWND, unsigned vertical, int x, int y);
// The drawn Win98 cursor. It lies over the published frame; the
// provider rasters the original USER.EXE AND/XOR masks at the hotspot.
using CursorDraw = bool (*)(void *, Gdi::HDC, int x, int y, unsigned cursor);
void SetCursorProvider(void *, CursorDraw);
void ShowPointer(bool);        // compose the cursor at all; off by default
bool PointerComposited();
unsigned SetPointerShape(unsigned cursor);
unsigned PointerShape();
int ShowPointerCount(bool show); // the ShowCursor counter of the calling thread's queue
void DropPointerLevel(unsigned process, unsigned thread); // the queue ended; thread 0 = all
// The process whose icon table holds the pointer's shape, and the hand-over
// when that process ends: another process draws the same shape from its own
// table. ForgetPointerOwner is the last guard at DetachProcess.
void *PointerOwner();
void *PointerHeir(void *leaving);
void HandPointer(void *context, unsigned cursor);
void ForgetPointerOwner(void *context);
int PointerCount();
bool PointerVisible();
// Moves the drawn pointer over the published frame, the way the Win98 display
// driver moves it over the screen: it restores the pixels the old position
// covered and draws the shape at the new one. No composition, no guest thread.
void MovePointer();
// A move the clip forces is routed at once, or with moved_x/moved_y given,
// handed back to the caller to queue as input.
bool ClipPointer(const Gdi::RECT *, int *moved_x = nullptr, int *moved_y = nullptr);
void ConfinePointer(int &x, int &y);
bool PointerClip(Gdi::RECT *);
bool PointerClipActive();
bool FrameTrackActive();
HWND SizeMoveWindow();
bool FrameTrackDue();   // a scrollbar repeat is waiting to fire
void FrameTrackTick();
bool FrameTrackMessage(const Message &);
void CancelFrameTrack();
// WM_CANCELMODE in DefWindowProc : only the window that holds
// the capture of a move, size or scrollbar loop ends it.
void CancelFrameTrack(HWND h);
using MetadataTask=Win98MessageMetadata::Task;
using MetadataCurrent=MetadataTask*(*)(void*);
using MetadataClock=unsigned(*)(void*);
void SetMetadataProvider(void*,MetadataCurrent,MetadataClock,MetadataClock timer_clock=nullptr);
unsigned MessageMetadata(unsigned operation,unsigned value=0);
unsigned MessageClock();
bool PostMessage(HWND, unsigned message, unsigned wparam, unsigned lparam);
void PostQuitMessage(unsigned code);
// Original USER decisions on our per-thread queues. The provider answers
// a queue token for a thread that has one and zero for a thread that has not.
// It is always called with the context of the process that is bound now, never
// with a pointer captured at registration: a child process can be released
// while its own registration is still in place.
using ThreadQueue = unsigned (*)(void *, unsigned thread);
void SetThreadQueueProvider(ThreadQueue);
bool PostThreadMessage(unsigned thread, unsigned message, unsigned wparam, unsigned lparam);
// Original USER hot-key list on the native per-thread queues.
// The keyboard path posts WM_HOTKEY and swallows the keystroke on a hit.
bool RegisterHotKey(HWND, unsigned id, unsigned modifiers, unsigned vk);
bool UnregisterHotKey(HWND, unsigned id);
unsigned HotKeyError();      // the last 16-bit thunk parameter-error code
unsigned HotKeyCount();      // live records, for the tests
unsigned HotKeyOwnerQueue(); // USER's DGROUP word at 150h
void FreeTaskHotKeys();
// Original USER decisions on the native update
// regions. The immediate callback carries kind 1 for RDW_UPDATENOW with the
// child flag and kind 2 for RDW_ERASENOW with the original's 0/4/8 mode.
using RedrawImmediate = void (*)(void *, HWND, unsigned kind, unsigned parameter);
bool RedrawWindow(HWND, const Gdi::RECT *, unsigned region, unsigned flags,
                  void * = nullptr, RedrawImmediate = nullptr);
unsigned RedrawError();
unsigned WaitQueueStatus(bool observe=false,unsigned mask=~0u);
void UnobserveQueue();
// The queue kinds a retrieval with this message filter looks at. Only their
// "new since last looked" bits are consumed by the look, so a thread that
// peeks for one private message still wakes from MsgWaitForMultipleObjects
// for a mouse press it has not seen.
unsigned FilterWakeMask(unsigned minimum,unsigned maximum);
bool PeekMessage(Message *, HWND filter, unsigned minimum, unsigned maximum, bool remove, bool paint = true);
unsigned DefaultMessage(HWND, unsigned message, unsigned wparam, unsigned lparam);
void SetRedrawVisible(HWND, bool on);
void SetControlRedraw(HWND, bool on);
void SetModifiers(bool shift, bool control);
unsigned Modifiers();
unsigned GetKeyState(unsigned);
unsigned GetAsyncKeyState(unsigned);
// USER.186 SwapMouseButton : exchanges the swap flag of the input data
// and SM_SWAPBUTTON, answers the previous flag.
unsigned SwapMouseButtons(unsigned value);
unsigned KeyboardState(unsigned char *buffer, bool set);
unsigned EditMessage(HWND, unsigned message, unsigned wparam, unsigned lparam);
void AskEditColors(HWND);
unsigned EditSelection(HWND, unsigned *start, unsigned *end);
bool SetEditSelection(HWND, unsigned start, unsigned end);
bool ReplaceEditSelection(HWND, const char *, unsigned length, bool undo);
using EditNotify = void (*)(void *, HWND parent, unsigned command, HWND control);
void SetEditNotify(void *, EditNotify);
using EditWordBreak = int (*)(void *, unsigned procedure, const char *, unsigned current, unsigned length, unsigned code);
void SetEditWordBreak(void *, EditWordBreak);
unsigned ClipboardLength();
using ClipboardText = unsigned (*)(void *, HWND, bool write, char *, unsigned);
using ClipboardDestroy = void (*)(void *, HWND);
void SetClipboardCallbacks(void *, ClipboardText, ClipboardDestroy);
bool SetEditFont(HWND, Gdi::HFONT);
bool EditModified(HWND);
void SetEditModified(HWND, bool);
HMENU CreateMenu(bool popup=false);
bool IsMenu(HMENU);
bool MenuItemInfo(HMENU,unsigned,unsigned,Win98Menu::Info&,unsigned operation,bool layout=true);
bool ModifyMenu(HMENU,unsigned,unsigned,unsigned,const char*);
unsigned MenuState(HMENU,unsigned,unsigned);
unsigned MenuDefault(HMENU,unsigned,unsigned);
unsigned MenuRadio(HMENU,unsigned,unsigned,unsigned,unsigned);
// How many items one menu can hold. Win98 keeps the item array of a menu as one
// block in the 64-KB USER heap, and an item record there is 46 bytes
// (Win98Menu::Insert divides a byte position by 46); the count is that ceiling,
// not a number of the port.
constexpr unsigned MenuItemCapacity = 65536 / 46;
unsigned MenuContextHelp(HMENU,unsigned,bool);
bool MenuDetails(HMENU,Win98Menu::MenuInfo&,bool);
bool AppendMenu(HMENU, unsigned flags, unsigned id, const char *label, HMENU submenu);
bool AppendMenuEx(HMENU, unsigned flags, unsigned id, const char *label, HMENU submenu);
bool InsertMenu(HMENU, unsigned item, unsigned flags, unsigned id, const char *label);
bool RemoveMenu(HMENU, unsigned item, bool by_position, bool destroy, bool layout=true);
bool DestroyMenu(HMENU);
unsigned MenuItemCount(HMENU);
unsigned MenuItemID(HMENU, unsigned index);
bool SetMenuDefault(HMENU, unsigned item, bool by_position);
HMENU GetMenu(HWND);
bool SetMenu(HWND, HMENU, bool layout = true);
bool DrawMenuBar(HWND, bool layout=true);
int DrawMenuBarTemp(HWND owner, Gdi::HDC dc, Gdi::RECT rect, HMENU menu, Gdi::HFONT font); // USER
HMENU SystemMenuRoot(HWND);
bool SetSystemMenuRoot(HWND, HMENU);
HMENU GetSubMenu(HMENU, unsigned index);
unsigned SetMenuFlags(HMENU, unsigned item, unsigned flags, bool by_position, unsigned mask);
using MenuSend=unsigned(*)(void*,unsigned,unsigned,unsigned,unsigned);
using MenuLoop=unsigned(*)(void*);using MenuThread=unsigned(*)(void*);
void SetMenuCallbacks(void*,MenuSend,MenuLoop,MenuThread);
// Owner-draw menu items. The adapter builds the original MEASUREITEMSTRUCT and
// DRAWITEMSTRUCT in guest memory and delivers them on the owner's thread.
using MenuMeasure=void(*)(void*,unsigned owner,unsigned id,unsigned data,int*cx,int*cy);
using MenuDrawItem=void(*)(void*,unsigned owner,const unsigned*fields);
void SetMenuOwnerCallbacks(void*,MenuMeasure,MenuDrawItem);
bool MeasureMenuObject(HMENU,HWND owner,int bar_width,int&width,int&height);
unsigned MenuCheckMarkSize(); // packed check-mark width and height
unsigned TrackPopup(HMENU,unsigned flags,int x,int y,HWND,const Gdi::RECT*exclude=nullptr);
unsigned FinishPopupTracking(unsigned result, unsigned flags);
unsigned ResumeBarTracking(unsigned result);
bool MenuBarInfo(HWND,int,int,unsigned*);
bool EndMenu();bool PopupActive();unsigned PopupCommand();bool PopupInputMessage(const Message&);
// SC_KEYMENU: the menu mode of the window's bar from the keyboard (USER); character 0 for ALT or F10 alone, ' ' for the system menu.
unsigned KeyMenu(HWND,HMENU system_menu,unsigned character);
unsigned MouseMenu(HWND,int x,int y);
bool BarModeActive();bool MenuBeepRequest();
unsigned MenuKeyFlags(unsigned thread,unsigned clear,unsigned set); // answers the bits before the change
unsigned PopupHoverRequest();bool PopupHoverOpen(unsigned window);
bool PendingPopupInput();bool DispatchPopupInput();
bool MenuItemRect(HWND,HMENU,unsigned,Gdi::RECT*);
int MenuItemFromPoint(HWND,HMENU,int,int);
unsigned PopupWindowForMenu(HMENU);
bool MenuIdleTarget(HWND &owner, HWND &popup);
unsigned GetDoubleClickTime();
void SetDoubleClickTime(unsigned);
// Hardware state is captured immediately; routing may run later on a GUI fiber.
struct InputPacket
{
    unsigned kind = 0, message = 0, key = 0, parameter = 0, character = 0;
    unsigned buttons = 0, changed = 0, milliseconds = 0, extra_info = 0;
    int x = 0, y = 0;
    bool moved = false;
    unsigned host_window = 0;
    unsigned char state[256] = {};
};
void* InputContext(const InputPacket&);
InputPacket CaptureKey(unsigned key, unsigned character, bool down, unsigned scan, bool extended);
InputPacket CaptureMouse(int x, int y, unsigned buttons, unsigned milliseconds);
InputPacket CaptureWheel(int delta, unsigned milliseconds);
// The steps of a trackpad: smaller than a notch, the first of a gesture, and
// the ones the Mac adds after the fingers left.
enum : unsigned { ScrollFine = 1, ScrollBegin = 2, ScrollMomentum = 4 };
InputPacket CaptureScroll(int delta, int sideways, unsigned flags, unsigned milliseconds);
// A window did something with a turn of the wheel (User::RouteWheel).
void NoteWheelActivity(HWND);
HWND WheelTarget();
void SetWheelToPointer(bool on);
bool WheelToPointer();
unsigned InputBlocked();                 // BlockInput, USER: the blocking thread
void SetInputBlocked(unsigned thread);
void NoteQueueRead(unsigned thread, unsigned long long now);
// A button or key message of process `pid` that USER handed to a queue and
// the program has not retrieved yet.
bool HardwareInputUnread(unsigned pid);
bool MouseMovePending();
unsigned long long QueueReadTime(unsigned thread);
// GetMouseMovePoints, USER over the ring of mouse_event.
struct MouseMovePoint { int x, y; unsigned time, extra; };
int MouseMovePoints(const MouseMovePoint &find, MouseMovePoint *out, int capacity);
unsigned PhysicalMouseButtons();
bool PhysicalKeyDown(unsigned key);
// The input router's activation by a press: `focus` false for a press
// outside the client area, which activates the top-level window and leaves
// the focus to the activation; true for a native control that
// takes the focus on its press as the Win98 control does.
using InputFocus = void (*)(void *, HWND, bool focus);
// Asked before a left press is handed to its window (WM_MOUSEACTIVATE, USER): 0 hands the press over and activates, 1 drops the press, 3 hands it
// over without activating.
using InputActivate = unsigned (*)(void *, HWND, unsigned hit, unsigned message);
void RouteInput(const InputPacket &, void *context = nullptr, InputFocus focus = nullptr,
                InputActivate activate = nullptr);
bool InputPacketMatches(const InputPacket &, HWND filter, unsigned minimum, unsigned maximum);
bool HotKeyInput(const InputPacket &);
void Mouse(int x, int y, bool down, bool up, unsigned milliseconds = 0);
// Optional NEO98 extension, independent of the Win98 USER32 export contract.
struct TouchMouse { bool emit = false; int x = 0, y = 0; unsigned buttons = 0; HWND activate = 0; };
// Host probe runs on the UI thread; nullptr means no recognized touchscreen.
void SetTouchscreenProbe(bool (*)());
bool HasTouchscreen();
bool TouchRegister(HWND, unsigned message);
bool TouchUnregister(HWND);
int TouchPeek(HWND, void *events, unsigned capacity);
void TouchConsume(HWND, unsigned count);
TouchMouse TouchInput(unsigned long long device, unsigned long long finger, unsigned phase,
                      int x, int y, unsigned time, int pressure = -1);
void TouchCancelAll(bool forget_contacts = false);
void TouchCancelWindow(HWND);
void TouchDropWindow(HWND);
void TouchForegroundChanged();
void TouchReset();
void TouchPump();
void Keyboard(unsigned virtual_key, unsigned character, bool shift, bool control);
void KeyboardRaw(unsigned virtual_key, unsigned character, bool down, unsigned scan = 0, bool extended = false);
void KeyboardModifiers(unsigned usb_mask);
void MouseButtons(int x, int y, unsigned buttons, unsigned milliseconds = 0);
bool TranslateMessage(const Message *);
// KEYBOARD.DRV's ToAscii for a key message : count as ToAscii
// returns it, negative for a dead key. Uses the thread's key state.
int TranslateKey(unsigned virtual_key, unsigned lparam, unsigned char out[2], const unsigned char *state = nullptr);
void SetKeyboardLanguage(unsigned language);
unsigned KeyboardLanguage();
// Reconcile visible regions, draw USER-owned frames, EDIT, SCROLLBAR and
// desktop background, then publish the host image. Never calls into the
// guest.
bool Render();
unsigned PaintDesktop(unsigned dc);
// Serve native-control paints on the bound thread at its message wait, so
// guest callbacks run on the owning thread.
unsigned ServeControlPaints();
// USER: the WM_QUERYDRAGICON question a window without an icon gets.
// The provider runs on the thread that owns the window and answers an icon
// handle or zero; ServeDragIcons asks each window once and remembers it.
unsigned ServeDragIcons();
bool PendingControlPaint();
// UpdateWindow for a native control: it paints now if it owes a paint.
void UpdateControl(HWND);
const unsigned char *Pixels();
// Optional host scanout. A false read asks whether the resident GPU image
// can be shown; a true read materializes its bytes for USER and screenshots.
using HostScanout = bool (*)(void *, const unsigned char *, bool read);
void SetHostScanoutProvider(void *, HostScanout);
bool DeferScreenRead();
// Pixels dimensions follow the desktop mode, defaulting to 800x600. Rows
// are contiguous 32-bit pixels.
void PixelsSize(int &width, int &height);
// Display surface; see src/user_screen.inc. A provider supplies storage with
// a guest address. Without one, the surface exists only in host memory.
using ScreenAllocate = bool (*)(void *, unsigned bytes, unsigned &guest, unsigned char *&host);
using ScreenRelease = void (*)(void *, unsigned guest, unsigned char *host);
void SetScreenLeaseProvider(void *, ScreenAllocate, ScreenRelease);
bool SetScreenMode(int width, int height, int depth);
bool SetVgaScreenMode(int width, int height);
void ScreenMode(int &width, int &height, int &depth);
// Count screen allocations (mode changes). Older DirectDraw surfaces become
// lost; see src/x86_dx_driver.cpp.
unsigned ScreenModeSerial();
bool ScreenModeSupported(int width, int height, int depth);
// SetScreenMode replaces the surface immediately and retargets window DCs,
// recomputes visible regions and invalidates windows.
void SetHostFrameFollowsMode(bool on); // Match host size to the application display mode.
bool ScreenModeActive();   // An application mode is active; USER still paints visible regions.
// DirectDraw exclusive mode, reported by SetExclusiveMode. It ends when its
// owning process exits.
void SetExclusiveMode(bool enter);
bool ExclusiveMode();
unsigned ExclusiveProcess();   // Exclusive-mode process, or zero.
// Desktop mode comes from HKCC\Display\Settings. Any listed mode can be the
// desktop; the host image follows its size. Other modes belong to
// applications. SetDesktopMode updates the desktop selection without
// switching the active mode itself.
bool DesktopModeSupported(int width, int height, int depth);
bool SetDesktopMode(int width, int height, int depth);
void DesktopMode(int &width, int &height, int &depth);
// Add host-panel dimensions to the mode list at 8, 16, 24 and 32 bits.
// Passing zero dimensions removes them.
bool SetScreenPanel(int width, int height);
void ScreenPanel(int &width, int &height);
// Queries for a host displaying top-level windows separately; see
// src/user_host_view.inc.
unsigned WindowVisibleRects(HWND, Gdi::RECT *out, unsigned capacity);
// Separate complete window images for compositors that place each host
// window independently. Coordinates inside each backing remain guest screen coordinates.
void SetHostWindowSurfaces(bool);
bool HostWindowSurfaces();
const unsigned char *HostWindowPixels(HWND, int &width, int &height, int &stride);
void SetHostPointerWindow(HWND);
HWND HostWindowOwner(HWND);
void SetHostHiddenWindow(HWND); // host-only: omitted from drawing, clipping and hit testing
bool DragOutline(Gdi::RECT *rect, int *edge_width, int *edge_height);
bool PointerDrawn(Gdi::RECT *box);
bool AwaitsPaint(HWND);
unsigned ShownSerial(HWND);
unsigned ScreenModeCount();
bool ScreenModeAt(unsigned index, int &width, int &height, int &depth);
unsigned ScreenGuestAddress();
// Host screen bytes. These match ScreenGuestAddress while the screen lease
// is held.
const unsigned char *ScreenBits();
int ScreenStride();
unsigned ScreenBytes();
const unsigned *ScreenColors();
bool SetScreenColors(unsigned start, unsigned count, const unsigned *colors);
void InstallScreenPaletteSink();
void PublishScreen();
void ScreenChanged();      // Screen contents changed; publish a new frame.
bool ScanoutScreen();      // Scan out the application or exclusive-mode surface.
// The scanout start defaults to screen memory. A DirectDraw HAL flip selects
// the back buffer instead, as a display card changes its start address. See
// src/x86_ddhal.cpp.
void SetScanout(const unsigned char *bits, int stride);
void ResetScanout();
// Restore scanout to the screen surface, copying the last displayed pixels so
// the image remains unchanged.
void SettleScanout();
const unsigned char *ScanoutBits();   // Bytes currently scanned out.
// Driver-owned BGRA image at its native resolution, such as Glide's scaled
// front buffer. While present, the host displays it instead of the standard
// screen surface, which retains application-resolution pixels. Storage
// remains valid until the next call.
void SetOverlay(const unsigned *bits, int width, int height);
void ClearOverlay();
const unsigned *Overlay(int &width, int &height, unsigned &serial);
// Guest display size in host pixels, or zero when no host has reported it.
void SetHostOutput(int width, int height);
void HostOutput(int &width, int &height);
// The display gamma ramp: three tables of 256 WORDs for red, green, and blue,
// supplied by SetDeviceGammaRamp. Apply it when publishing a frame; the
// identity ramp needs no work.
void SetGammaRamp(const unsigned short *ramp);
void GammaRamp(unsigned short *ramp);
bool Dirty();
unsigned FrameSerial();
void DrawingChanged(Gdi::HDC); // Direct drawing updates pixels without requesting scene repaint.
unsigned LiveWindows();
// Menus in use and the size of the table they come from, for diagnostics and
// for tests that watch the menu budget of a whole session.
unsigned LiveMenus();
unsigned MenuCapacity();
unsigned MenuCount();
unsigned PendingPaintWindows(unsigned *first);
}
