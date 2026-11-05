// SPDX-License-Identifier: GPL-2.0-or-later
// COMCTL32 5.80: class registration bodies and public initialization exports.
#pragma once
namespace Win98CommonInit {
using U = unsigned;
static_assert(sizeof(U) == 4, "Win98 DWORD width");
struct Class {
    const char *name;
    U procedure, style, window_extra, background, cursor;
};
// Procedure values identify original bodies. The runtime must supply a callable
// native procedure or an explicit unsupported-control trap for each identity.
inline constexpr Class Classes[] = {
    {"ToolbarWindow32",    0xbfe9397b,0x4008,4,16,0x7f00},
    {"ReBarWindow32",      0xbfe9b5ad,0x4008,4,16,0},
    {"tooltips_class32",   0xbfe915d2,0x4808,4,0,0x7f00},
    {"msctls_statusbar32", 0xbfe98f06,0x4009,4,16,0x7f00},
    {"SysListView32",      0xbfead662,0x4008,4,6,0x7f00},
    {"SysHeader32",        0xbfe9d9b8,0x4008,4,16,0},
    {"SysTabControl32",    0xbfe92049,0x400b,4,16,0x7f00},
    {"SysTreeView32",      0xbfea9198,0x4008,4,0,0x7f00},
    {"msctls_trackbar32",  0xbfeb5ded,0x4000,4,16,0x7f00},
    {"msctls_updown32",    0xbfeb5631,0x4003,4,16,0x7f00},
    {"msctls_progress32",  0xbfe9c1a3,0x4003,4,16,0x7f00},
    {"msctls_hotkey32",    0xbfecc575,0x4000,24,0,0},
    {"SysAnimate32",       0xbfeab902,0x4008,4,16,0x7f00},
    {"SysMonthCal32",      0xbfed6c0a,0x4000,4,6,0x7f00},
    {"SysDateTimePick32",  0xbfedc7a8,0x4000,4,6,0x7f00},
    {"ComboBoxEx32",       0xbfe9a21e,0x4000,4,6,0x7f00},
    {"SysIPAddress32",     0xbfece805,0x400b,4,6,0x7f01},
    {"SysPager",           0xbfea7d0f,0x4000,4,16,0x7f00},
    {"NativeFontCtl",      0xbfeb50f3,0x4000,4,16,0x7f00}
};
struct Group {U entry, flags, first; bool boolean_result;};
inline constexpr Group Groups[] = {
    {0xbfeb2d68,4,0,true}, {0xbfeb20da,0x400,1,true},
    {0xbfeb2ddd,0xe,2,false}, {0xbfeb2e4a,4,3,false},
    {0xbfeb2eba,1,4,false}, {0xbfeb2f2c,1,5,false},
    {0xbfeb2f90,8,6,false}, {0xbfeb3002,2,7,false},
    {0xbfeb3071,4,8,false}, {0xbfeb30e2,0x10,9,false},
    {0xbfeb3153,0x20,10,true}, {0xbfeb31c8,0x40,11,true},
    {0xbfeb322d,0x80,12,true}, {0xbfeba8f5,0x100,13,true},
    {0xbfeb2048,0x200,15,false}, {0xbfeb21ee,0x800,16,false},
    {0xbfeb2168,0x1000,17,false}, {0xbfeb1fb9,0x2000,18,false}
};
struct Environment {
    virtual ~Environment() = default;
    virtual bool Read(U address, U &value) = 0;
    virtual bool Write(U address, U value) = 0;
    virtual bool BadWrite(U address, U bytes) = 0;
    virtual U Exists(U instance, U class_index) = 0;
    virtual U LoadCursor(U instance, U resource) = 0;
    virtual U Register(U instance, U class_index, U cursor) = 0;
};
inline U RegisterGroup(Environment &e,U group,U instance) {
    const auto &g=Groups[group];
    const auto &c=Classes[g.first];
    if(e.Exists(instance,g.first))return 1;
    U cursor=c.cursor?e.LoadCursor(0,c.cursor):0;
    U result=e.Register(instance,g.first,cursor)&0xffff;
    // MonthCal and DateTimePick are one group. A second registration failure
    // leaves the first class registered; a subsequent call sees it and exits.
    if(group==13){if(!result)return 0;result=e.Register(instance,g.first+1,cursor)&0xffff;}
    return g.boolean_result ? !!result : result;
}
inline U Initialize(Environment &e,U pointer,U instance) { //
    U value=0;
    if(!pointer||!e.Read(pointer,value)||value!=8)return 0;
    if(!e.Read(pointer+4,value)||(value&0xffff8000u))return 0;
    for(U i=0;i<18;++i){
        // The original reloads the caller's mask after each registration.
        if(!e.Read(pointer+4,value))return 0;
        if((value&Groups[i].flags)&&!RegisterGroup(e,i,instance))return 0;
    }
    return 1;
}
inline U Version(Environment &e,U pointer) { //
    U size=0;
    if(!pointer||e.BadWrite(pointer,20)||!e.Read(pointer,size)||size!=20)
        return 0x80070057;
    if(!e.Write(pointer+4,5)||!e.Write(pointer+8,80)||
       !e.Write(pointer+12,2614)||!e.Write(pointer+16,1))return 0x80070057;
    return 0;
}
// InitCommonControls is a single RET. Its caller's EAX survives.
inline U Legacy(U eax){return eax;}
}
