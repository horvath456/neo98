/* SPDX-License-Identifier: GPL-2.0-or-later
 * Own x86 TreeView contract; callbacks can change and veto native operations.
 */
#include "capture_handshake.h"
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
typedef struct
{
    U hwnd, message, wp, lp, time;
    int x, y;
} CaptureMessage;
API U GetMessageA(CaptureMessage *, U, U, U);
API U TranslateMessage(const CaptureMessage *);
API U DispatchMessageA(const CaptureMessage *);
API void ExitProcess(U);
API void Sleep(U);
API U IsWindow(U);
API U RegisterClassA(U *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U,
                      U, U, U);
API U DestroyWindow(U);
API U DefWindowProcA(U, U, U, U);
API U CallWindowProcA(U, U, U, U, U);
API U SendMessageA(U, U, U, U);
API U SetWindowLongA(U, int, U);
API U GetWindowLongA(U, int);
static U tree, first, child, last, notifications, vetoes, subclass_calls,
    base_proc;
static U destroy_on_selection;
static void check(int ok, U code) {
  if (!ok)
    ExitProcess(code);
}
static U insert(U parent, const char *text, U parameter, U children) {
  U data[12] = {parent,  0xffff0002, 1 | 4 | 64, 0, 0,        0,
                (U)text, 0,          0,          0, children, parameter};
  U result = SendMessageA(tree, 0x1100, 0, (U)data);
  check(result, 110);
  return result;
}
static U __stdcall procedure(U hwnd, U message, U wp, U lp) {
  if (message == 0x4e) {
    U *data = (U *)lp;
    ++notifications;
    check(data[0] == tree && data[1] == 98, 111);
    if (data[2] == (U)-405 && data[3] == 2 && data[15] == first && !child)
      child = insert(first, "Deferred child", 77, 0);
    if (data[2] == (U)-401 && data[23] == 77) {
      ++vetoes;
      return 1;
    }
    if(data[2]==(U)-401&&destroy_on_selection){destroy_on_selection=0;check(DestroyWindow(tree),123);return 0;}
    return 0;
  }
  return DefWindowProcA(hwnd, message, wp, lp);
}
static U __stdcall subclass(U hwnd, U message, U wp, U lp) {
  ++subclass_calls;
  return CallWindowProcA(base_proc, hwnd, message, wp, lp);
}
void run_tree_test(U instance) {
  U klass[10] = {0, (U)procedure,           0, 8, instance, 0, 0, 0,
                 0, (U) "NEO98TreeContract"};
  check(RegisterClassA(klass), 100);
  U parent = CreateWindowExA(0, "NEO98TreeContract", "Native TreeView",
                             0x10cf0000, 50, 50, 400, 350, 0, 0, instance, 0);
  check(parent, 101);
  check(!SetWindowLongA(parent, 0, 0x98abcd) &&
            GetWindowLongA(parent, 0) == 0x98abcd,
        102);
  check(!SetWindowLongA(parent, -21, 0x1234) &&
            GetWindowLongA(parent, -21) == 0x1234,
        103);
  tree = CreateWindowExA(0, "SysTreeView32", "", 0x5001001f, 5, 5, 350, 250,
                         parent, 98, instance, 0);
  check(tree, 104);
  base_proc = SetWindowLongA(tree, -4, (U)subclass);
  check(base_proc, 105);
  first = insert(0xffff0000, "Windows 98", 98, 1);
  last = insert(0xffff0000, "Other root", 99, 0);
  check(SendMessageA(tree, 0x110a, 0, 0) == first &&
            SendMessageA(tree, 0x110a, 3, first) == 0,
        106);
  check(SendMessageA(tree, 0x110a, 6, first) == last, 107);
  check(SendMessageA(tree, 0x110b, 9, first), 108);
  check(SendMessageA(tree, 0x1102, 2, first) && child, 109);
  check(SendMessageA(tree, 0x110a, 6, first) == child &&
            SendMessageA(tree, 0x110a, 7, last) == child,
        112);
  check(!SendMessageA(tree, 0x110b, 9, child) && vetoes == 1 &&
            SendMessageA(tree, 0x110a, 9, 0) == first,
        113);
  U item[10] = {4 | 64, child};
  check(SendMessageA(tree, 0x110c, 0, (U)item) && item[9] == 77 && item[8] == 0,
        114);
  int rect[4] = {(int)child};
  check(SendMessageA(tree, 0x1104, 1, (U)rect) && rect[1] > 0 &&
            rect[2] > rect[0],
        115);
  U hit[4] = {(U)(rect[0] + 1), (U)(rect[1] + 1)};
  check(SendMessageA(tree, 0x1111, 0, (U)hit) == child && hit[3] == child, 116);
  SendMessageA(tree,0x201,1,((U)(rect[1]+2)<<16)|(U)(rect[0]+2));
  check(vetoes==2&&SendMessageA(tree,0x110a,9,0)==first,124);
  SendMessageA(tree,0x100,0x28,0);
  check(vetoes==3&&SendMessageA(tree,0x110a,9,0)==first,125);
  NEO98_WAIT_FOR_CAPTURE(CaptureMessage); // The host takes its picture of the finished tree here.
  check(SendMessageA(tree, 0x1102, 1, first) &&
            SendMessageA(tree, 0x110a, 6, first) == last,
        117);
  check(!SendMessageA(tree, 0x1104, 1, (U)(U[4]){child}), 118);
  check(SendMessageA(tree, 0x1101, 0, first) &&
            SendMessageA(tree, 0x1105, 0, 0) == 1,
        119);
  check(!SendMessageA(tree, 0x110c, 0, (U)item) &&
            SendMessageA(tree, 0x110a, 0, 0) == last,
        120);
  check(notifications >= 7 && subclass_calls > 20, 121);
  check(DestroyWindow(parent), 122);
  parent=CreateWindowExA(0,"NEO98TreeContract","Tree lifetime",0x10cf0000,50,50,400,350,0,0,instance,0);
  check(parent,126);
  for(U i=0;i<20;++i){
    tree=CreateWindowExA(0,"SysTreeView32","",0x5001001f,5,5,350,250,parent,98,instance,0);check(tree,127);
    U item=insert(0xffff0000,"Destroyed by callback",1,0);destroy_on_selection=1;
    check(!SendMessageA(tree,0x110b,9,item)&&!IsWindow(tree),128);
  }
  check(DestroyWindow(parent),129);
}
