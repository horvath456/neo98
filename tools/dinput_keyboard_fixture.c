/* SPDX-License-Identifier: GPL-2.0-or-later */
/* original DINPUT keyboard in background mode, simultaneous mouse,
   buffered/remapped scan codes, key state and device lifetime. */
#pragma clang diagnostic ignored "-Wdeprecated-non-prototype"
#define mainCRTStartup mouseFixtureUnused
#include "dinput_vxd_fixture.c"
#undef mainCRTStartup
static const GUID sys_keyboard = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF,0xC7,0x44,0x45,0x53,0x54,0,0}};
static struct { const GUID *guid; U offset, type, flags; } keys[256];
static struct { U size, object_size, flags, data_size, count; void *objects; } keyboard_format = {24,16,2,256,256,keys};
void mainCRTStartup(void)
{
    U cls[10]={0,(U)proc,0,0,0,0,0,0,0,(U)"DinputKeyboard98"}, msg[7];
    void *di=0,*kbd=0,*mouse=0;
    U window, i, round, pressed=0, released=0, right_down=0, right_up=0, held=0, last_sequence=0;
    struct {U size, header, object, how, data;} buffer={20,16,0,0,32};
    struct {U offset,data,time,sequence;} entries[32];
    for(i=0;i<256;++i){keys[i].offset=255-i;keys[i].type=0x8000000c|(i<<8);}
    check(RegisterClassA(cls)!=0,10);
    window=CreateWindowExA(0,"DinputKeyboard98","keyboard",0x10cf0000u,0,0,320,240,0,0,0,0);
    check(window!=0,11);
    check(DirectInputCreateA(GetModuleHandleA(0),0x700,&di,0)==0,12);
    check(CALL(di,3,&sys_keyboard,&kbd,0)==0,13);
    check(CALL(kbd,11,&keyboard_format)==0,14);
    check(CALL(kbd,13,window,2|8)==0,15);
    check(CALL(kbd,6,(U)1,&buffer)==0,16);
    check(CALL(kbd,7)==0,17);
    check(CALL(di,3,&sys_mouse,&mouse,0)==0,30);
    check(CALL(mouse,11,&format)==0,31);
    check(CALL(mouse,13,window,2|8)==0,32);
    check(CALL(mouse,6,(U)1,&buffer)==0,33);
    check(CALL(mouse,7)==0,34);
    for(round=0;round<600 && !(released&&right_up);++round){
        U count=32;
        unsigned char state[256];
        while(PeekMessageA(msg,0,0,0,1))DispatchMessageA(msg);
        check(CALL(kbd,10,16,entries,&count,0)==0,18);
        for(i=0;i<count;++i){
            check(entries[i].sequence>last_sequence,19);last_sequence=entries[i].sequence;
            if(entries[i].offset==255-0x1e){if(entries[i].data&0x80)++pressed;else ++released;}
            else if(entries[i].offset==255-0x9d){if(entries[i].data&0x80)++right_down;else ++right_up;}
            else check(0,20);
        }
        check(CALL(kbd,9,256,state)==0,21);
        if(state[255-0x1e]&0x80)held=1;
        Sleep(20);
    }
    check(pressed==1&&released==1&&held,22);
    check(right_down==1&&right_up==1,23);
    {U count=32;check(CALL(mouse,10,16,entries,&count,0)==0&&count==0,35);}
    check(CALL(kbd,8)==0,24);
    check(CALL(kbd,7)==0,25);
    check(CALL(kbd,8)==0,26);
    CALL(kbd,2);CALL(mouse,8);CALL(mouse,2);CALL(di,2);
    ExitProcess(0);
}
