// SPDX-License-Identifier: GPL-2.0-or-later
#include "imports.h"
#define P(x) ((U)(x))
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
static U ck[5],parent[5],info[18],done,devmode[31];
static char text[64],readback[64],caps[52],riff[38];
static short wave[9];
static U stream;
static volatile U timer_calls;
static void __stdcall timer_proc(U arg,U low,U high){if(arg==0x1234&&high)++timer_calls;}
/* An IO procedure that refuses every open with MMIOERR_INVALIDFILE-1. */
static U refused;
static U __stdcall refusing_proc(U info,U message,U a,U b){if(message==3)++refused;return message==3?0x10b:0;}
void mainCRTStartup(void){
 U timer=CreateWaitableTimerA(0,0,P("native-timer"));CHECK(timer);
 CHECK(WaitForSingleObject(timer,0)==258);
 CHECK(!CreateEventA(0,0,0,P("native-timer"))&&GetLastError()==6);
 CHECK(!SetEvent(timer)&&GetLastError()==6);
 U alias=OpenWaitableTimerA(0x100000,0,P("native-timer"));CHECK(alias);
 long long due=-200000;
 CHECK(!SetWaitableTimer(alias,P(&due),0,0,0,0)&&GetLastError()==5);
 CHECK(SetWaitableTimer(timer,P(&due),0,0,0,0));
 CHECK(WaitForSingleObject(alias,1000)==0&&WaitForSingleObject(timer,0)==258);
 CHECK(SetWaitableTimer(timer,P(&due),10,0,0,0));
 CHECK(WaitForSingleObject(timer,1000)==0&&WaitForSingleObject(timer,1000)==0);
 CHECK(CancelWaitableTimer(timer)&&WaitForSingleObject(timer,30)==258);
 CHECK(SetWaitableTimer(timer,P(&due),0,P(timer_proc),0x1234,0));
 CHECK(SleepEx(1000,1)==0xc0&&timer_calls==1);
 CHECK(SetWaitableTimer(timer,P(&due),0,P(timer_proc),0x1234,0));
 Sleep(50);CHECK(CancelWaitableTimer(timer)&&SleepEx(0,1)==0&&timer_calls==1);
 CHECK(WaitForSingleObject(timer,0)==0);
 CHECK(CloseHandle(alias)&&CloseHandle(timer));
 U manual=CreateWaitableTimerA(0,1,0);due=1;
 CHECK(SetWaitableTimer(manual,P(&due),0,0,0,0));
 CHECK(WaitForSingleObject(manual,0)==0&&WaitForSingleObject(manual,0)==0);
 CHECK(CancelWaitableTimer(manual)&&WaitForSingleObject(manual,0)==0);
 due=-100000;CHECK(SetWaitableTimer(manual,P(&due),0,0,0,0));
 U handles[2]={manual,CreateEventA(0,1,1,0)};
 CHECK(WaitForMultipleObjects(2,P(handles),1,1000)==0);
 CHECK(CloseHandle(handles[1])&&CloseHandle(manual));

 CHECK(MapVirtualKeyA('A',0)==0x1e);
 CHECK(MapVirtualKeyA(0x1e,1)=='A');
 CHECK(MapVirtualKeyA(0x10,0)==0x2a);
 CHECK(MapVirtualKeyA(0x36,1)==0x10);
 CHECK(MapVirtualKeyA('A',2)=='A');
 CHECK(GetKeyNameTextA(0x1e0000,P(text),sizeof text)==1&&text[0]=='A'&&!text[1]);
 CHECK(GetKeyNameTextA(0x1e0000,P(text),1)==0&&!text[0]);
 SetFileApisToOEM();CHECK(!AreFileApisANSI());SetFileApisToANSI();CHECK(AreFileApisANSI());
 CHECK(lstrlen(P("hello"))==5);CHECK(lstrcpy(P(text),P("ab"))==P(text));
 CHECK(lstrcat(P(text),P("cd"))==P(text));CHECK(lstrcmp(P(text),P("abcd"))==0);
 CHECK(lstrcmpi(P("AB"),P("ab"))==0);CHECK(lstrcpyn(P(text),P("abcdef"),3)==P(text)&&text[2]==0);
 U h=mmioOpenA(P("C:\\WINDOWS\\RIFFTEST.WAV"),P(info),0x10000);CHECK(h);
 parent[2]=0x45564157;
 CHECK(mmioDescend(h,P(parent),0,0x20)==0&&parent[3]==8&&parent[1]==30);
 ck[0]=0x61746164;CHECK(mmioDescend(h,P(ck),P(parent),0x10)==0&&ck[1]==5&&ck[3]==32);
 CHECK(mmioRead(h,P(text),64)==6&&text[0]=='h'&&text[4]=='o');
 CHECK(mmioAscend(h,P(ck),0)==0&&mmioSeek(h,0,1)==38);
 CHECK(mmioSeek(h,12,0)==12);ck[0]=0x20746d66;
 CHECK(mmioDescend(h,P(ck),P(parent),0x10)==0&&ck[1]==3&&ck[3]==20);
 CHECK(mmioRead(h,P(text),3)==3&&text[0]=='f');
 CHECK(mmioAscend(h,P(ck),0)==0&&mmioSeek(h,0,1)==24);
 ck[0]=0x1234;CHECK(mmioDescend(h,P(ck),P(parent),0x10)==265);
 CHECK(mmioClose(h,0)==0&&mmioRead(h,P(text),1)==~0u&&mmioClose(h,0)==5);
 CHECK(!mmioOpenA(P("missing.wav"),P(info),0)&&info[3]==257);
 /* The same RIFF as a memory file. szFilename is null, fccIOProc is
    FOURCC_MEM and the caller owns the block; MMIO reads out of it, which is
    how a program plays a wave it already holds (WinPac 2). */
 {
  U i;
  for(i=0;i<38;i++)riff[i]=0;
  riff[0]='R';riff[1]='I';riff[2]='F';riff[3]='F';riff[4]=30;
  riff[8]='W';riff[9]='A';riff[10]='V';riff[11]='E';
  riff[12]='f';riff[13]='m';riff[14]='t';riff[15]=' ';riff[16]=3;
  riff[20]='f';riff[21]='m';riff[22]='t';
  riff[24]='d';riff[25]='a';riff[26]='t';riff[27]='a';riff[28]=5;
  riff[32]='h';riff[33]='e';riff[34]='l';riff[35]='l';riff[36]='o';
  for(i=0;i<18;i++)info[i]=0;
  info[1]=0x204d454d;info[5]=38;info[6]=P(riff);
  U m=mmioOpenA(0,P(info),0);CHECK(m&&info[3]==0);
  parent[2]=0x45564157;
  CHECK(mmioDescend(m,P(parent),0,0x20)==0&&parent[3]==8&&parent[1]==30);
  ck[0]=0x61746164;CHECK(mmioDescend(m,P(ck),P(parent),0x10)==0&&ck[1]==5&&ck[3]==32);
  CHECK(mmioRead(m,P(text),64)==6&&text[0]=='h'&&text[4]=='o');
  CHECK(mmioSeek(m,0,1)==38&&mmioSeek(m,12,0)==12);
  CHECK(mmioClose(m,0)==0&&mmioRead(m,P(text),1)==~0u&&mmioClose(m,0)==5);
  /* Without a block the MEM procedure still opens (WINMM hands
     mmioSetBuffer a null block of size zero and accepts it), and a
     file of zero bytes reads nothing. A program's own IO procedure gets
     MMIOM_OPEN, and its answer is the error the open reports. */
  for(i=0;i<18;i++)info[i]=0;info[1]=0x204d454d;
  m=mmioOpenA(0,P(info),0);CHECK(m&&info[3]==0);
  CHECK(mmioRead(m,P(text),4)==0&&mmioClose(m,0)==0);
  for(i=0;i<18;i++)info[i]=0;info[2]=P(refusing_proc);
  CHECK(!mmioOpenA(0,P(info),0)&&info[3]==0x10b&&refused==1);
 }
 /* WINMM : four bytes, a space for every position past the end of
    the string, and MMIO_TOUPPER (0x10) through USER32!CharUpperA. The first
    character is the low byte. */
 CHECK(mmioStringToFOURCCA(P("RIFF"),0)==0x46464952);
 CHECK(mmioStringToFOURCCA(P("fmt"),0)==0x20746d66);
 CHECK(mmioStringToFOURCCA(P("data"),0)==0x61746164);
 CHECK(mmioStringToFOURCCA(P("wave"),0)==0x65766177);
 CHECK(mmioStringToFOURCCA(P("wave"),0x10)==0x45564157);
 CHECK(mmioStringToFOURCCA(P(""),0)==0x20202020);
 CHECK(mmioStringToFOURCCA(P("abcdefgh"),0)==0x64636261);
 CHECK(mmioStringToFOURCCA(0,0)==0);
 /* WINMM : the MMSYSTEM version, 3.10. : a buffer under
    twelve bytes is MMSYSERR_INVALPARAM, otherwise wType becomes TIME_MS and
    the millisecond field the count the timer VxD keeps. */
 CHECK(mmsystemGetVersion()==0x30a);
 {
  U mmtime[3];mmtime[0]=0x5a5a5a5a;mmtime[1]=0;mmtime[2]=0x5a5a5a5a;
  CHECK(timeGetSystemTime(P(mmtime),12)==0&&mmtime[0]==1);
  U before=mmtime[1];Sleep(30);
  CHECK(timeGetSystemTime(P(mmtime),12)==0&&mmtime[1]>=before+20);
  CHECK(timeGetSystemTime(P(mmtime),11)==11);
  CHECK(timeGetSystemTime(0,12)==11);
 }
 /* The mixer exposes one wave device, so waveOutGetNumDevs returns one
    and device zero answers. */
 CHECK(waveOutGetNumDevs()==1);
 /*  answers a size of zero with zero before it looks at handle or
    buffer, and probes the buffer with IsBadWritePtr for MMSYSERR_INVALPARAM
    when the size is not zero. */
 CHECK(waveOutGetDevCapsA(0,0,0)==0&&waveOutGetDevCapsA(0,0,52)==11);
 /* The device and the mapper both answer; a second device does not exist and
    is MMSYSERR_BADDEVICEID. wMid is 1 for MM_MICROSOFT and szPname at +8 is
    the name of this driver. */
 CHECK(waveOutGetDevCapsA(0,P(caps),52)==0&&caps[0]==1&&caps[8]=='N');
 CHECK(waveOutGetDevCapsA(~0u,P(caps),52)==0&&waveOutGetDevCapsA(1,P(caps),52)==2);
 /* The shared open worker probes the format over 0Eh bytes before
    anything else, so a null format is MMSYSERR_INVALPARAM even for
    WAVE_MAPPER. */
 CHECK(waveOutOpen(0,~0u,0,0,0,0)==11);
 /* Without a handle slot only asks whether the format plays.
    PCM 11.025 kHz, 8 bit, mono is one the mixer converts; a tag that is not
    PCM is WAVERR_BADFORMAT. */
 wave[0]=1;wave[1]=1;*(U*)(wave+2)=11025;*(U*)(wave+4)=11025;wave[6]=1;wave[7]=8;wave[8]=0;
 CHECK(waveOutOpen(0,~0u,P(wave),0,0,0)==0);
 wave[0]=2;CHECK(waveOutOpen(0,~0u,P(wave),0,0,0)==32);wave[0]=1;
 /* A callback kind does not know is MMSYSERR_INVALPARAM. */
 CHECK(waveOutOpen(0,~0u,P(wave),0,0,0x40000)==11);
 /* The device itself: open it, ask what it plays back, close it again. */
 CHECK(waveOutOpen(P(&stream),~0u,P(wave),0,0,0)==0&&stream);
 CHECK(waveOutGetID(stream,P(&done))==0&&done==0);
 CHECK(waveOutClose(stream)==0);
 /* Every other entry of the family tests the handle with the mask 77h first,
    so a null handle is MMSYSERR_INVALHANDLE. */
 CHECK(waveOutGetPosition(0,0,0)==5&&waveOutPrepareHeader(0,0,0)==5&&waveOutUnprepareHeader(0,0,0)==5);
 CHECK(waveOutWrite(0,0,0)==5&&waveOutReset(0)==5&&waveOutClose(0)==5&&waveOutPause(0)==5&&waveOutRestart(0)==5);
 /* MCI_OPEN without a parameter block cannot name a device type, and the
    sentence of that number in the MMSYSTEM string table is "A null parameter
    block was passed to MCI". Before the fourth pass of PLAN.md step 11 there
    was no MCI at all and the answer was MCIERR_CANNOT_LOAD_DRIVER. */
 CHECK(mciSendCommandA(0,0x803,0,0)==297);
 CHECK(mciSendCommandA(0,0x808,0,0)==257);
 devmode[9]=124;devmode[10]=0x5c0000;devmode[26]=32;devmode[27]=800;devmode[28]=600;devmode[30]=60;
 CHECK(ChangeDisplaySettingsA(P(devmode),2)==0);devmode[27]=1024;
 CHECK(ChangeDisplaySettingsA(P(devmode),2)==(U)-2);
 CHECK(ChangeDisplaySettingsA(0,0)==0);
 U pos[2];CHECK(SetCursorPos(140,120)&&GetCursorPos(P(pos))&&pos[0]==140&&pos[1]==120);
 U output=GetStdHandle(-11),input=GetStdHandle(-10);
 CHECK(SetConsoleTextAttribute(output,0x1e));
 CHECK(WriteConsoleA(output,P("Native console> "),16,P(&done),0)&&done==16);
 CHECK(ReadConsoleA(input,P(readback),sizeof readback,P(&done),0));
 CHECK(done==4&&readback[0]=='O'&&readback[1]=='K'&&readback[2]==13&&readback[3]==10);
 CHECK(WriteConsoleA(output,P("PASS"),4,P(&done),0));
 Sleep(100);ExitProcess(0);
}
