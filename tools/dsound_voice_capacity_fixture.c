/* SPDX-License-Identifier: GPL-2.0-or-later */
/* every advertised hardware mixing slot must be playable together. */
#define COBJMACROS
#include <windows.h>
#include <dsound.h>
static HANDLE report;
static DWORD bad;
static void record(DWORD id,DWORD value){DWORD row[2]={id,value},n;WriteFile(report,row,8,&n,0);}
static void check(DWORD id,int okay){record(id,!okay);if(!okay)++bad;}
void mainCRTStartup(void){
 IDirectSound *ds=0;static IDirectSoundBuffer *buffers[256];DSCAPS caps={0};
 report=CreateFileA("C:\\VOICES.BIN",GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);if(report==INVALID_HANDLE_VALUE)ExitProcess(98);
 HMODULE dll=LoadLibraryA("DSOUND.DLL");
 HRESULT (WINAPI *create)(LPCGUID,LPDIRECTSOUND*,LPUNKNOWN)=
  (void*)GetProcAddress(dll,"DirectSoundCreate");
 if(!create){CloseHandle(report);ExitProcess(97);}
 HRESULT hr=create(0,&ds,0);check(1,SUCCEEDED(hr));if(FAILED(hr)){CloseHandle(report);ExitProcess(1);}
 check(2,SUCCEEDED(IDirectSound_SetCooperativeLevel(ds,GetDesktopWindow(),DSSCL_NORMAL)));
 caps.dwSize=sizeof caps;check(3,SUCCEEDED(IDirectSound_GetCaps(ds,&caps)));
 DWORD available=caps.dwFreeHwMixingAllBuffers;record(10000,available);record(10001,caps.dwMaxHwMixingAllBuffers);
 check(4,available<256&&available<=caps.dwMaxHwMixingAllBuffers);if(available>=256)available=0;
 record(10004,caps.dwFlags);
 WAVEFORMATEX wave={WAVE_FORMAT_PCM,1,22050,44100,2,16,0};
 DSBUFFERDESC desc={0};desc.dwSize=sizeof desc;desc.dwFlags=DSBCAPS_STATIC|DSBCAPS_LOCHARDWARE|DSBCAPS_CTRLPAN|DSBCAPS_CTRLVOLUME|DSBCAPS_CTRLFREQUENCY|DSBCAPS_GETCURRENTPOSITION2;desc.dwBufferBytes=4096;desc.lpwfxFormat=&wave;
 for(DWORD i=0;i<available;i++){
  hr=IDirectSound_CreateSoundBuffer(ds,&desc,&buffers[i],0);record(11000+i,(DWORD)hr);check(100+i,SUCCEEDED(hr));
  if(buffers[i]){
   void *p=0;DWORD bytes=0;check(500+i,SUCCEEDED(IDirectSoundBuffer_Lock(buffers[i],0,0,&p,&bytes,0,0,DSBLOCK_ENTIREBUFFER)));
   if(p){for(DWORD j=0;j<bytes;j++)((BYTE*)p)[j]=0;IDirectSoundBuffer_Unlock(buffers[i],p,bytes,0,0);}
   hr=IDirectSoundBuffer_Play(buffers[i],0,0,DSBPLAY_LOOPING);record(12000+i,(DWORD)hr);check(1000+i,SUCCEEDED(hr));
  }
 }
 check(5,SUCCEEDED(IDirectSound_GetCaps(ds,&caps)));check(6,caps.dwFreeHwMixingAllBuffers==0);
 IDirectSoundBuffer *extra=0;hr=IDirectSound_CreateSoundBuffer(ds,&desc,&extra,0);record(10002,(DWORD)hr);check(7,FAILED(hr));if(extra)IDirectSoundBuffer_Release(extra);
 for(DWORD i=0;i<available;i++)if(buffers[i]){IDirectSoundBuffer_Stop(buffers[i]);IDirectSoundBuffer_Release(buffers[i]);}
 check(8,SUCCEEDED(IDirectSound_GetCaps(ds,&caps)));check(9,caps.dwFreeHwMixingAllBuffers==available);
 // Public buffers can still spill into the original DLL's software mixer.
 desc.dwFlags &= ~DSBCAPS_LOCHARDWARE;
 DWORD created=0;
 for(DWORD i=0;i<192;i++){
  buffers[i]=0;hr=IDirectSound_CreateSoundBuffer(ds,&desc,&buffers[i],0);check(2000+i,SUCCEEDED(hr));created+=buffers[i]!=0;
 }
 record(10003,created);
 for(DWORD i=0;i<192;i++)if(buffers[i])IDirectSoundBuffer_Release(buffers[i]);
 IDirectSound_Release(ds);CloseHandle(report);ExitProcess(bad?1:0);
}
