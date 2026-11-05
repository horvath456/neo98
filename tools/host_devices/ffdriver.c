/* SPDX-License-Identifier: GPL-2.0-or-later */
/* NEO98FF.DLL, the DirectInput force feedback driver of the host controllers.

   DINPUT.DLL loads the driver an OEM joystick type names under
   OEMForceFeedback\CLSID and talks to it through IDirectInputEffectDriver.
   The runtime registers this one for its "NEO98 Controller" types when SDL
   reports a controller with rumble (src/x86_mm_devices.cpp). Every effect
   becomes rumble of both motors: the strength of a constant, ramp or
   periodic force times the effect's and the device's gain, for as long as
   the effect runs. The host applies it with SDL_JoystickRumble
   (tools/host_devices/service.h). */
#include "../../src/host_devices_abi.h"
typedef unsigned U;typedef int I;typedef void *H;
#define API(ret,name,args) __declspec(dllimport) ret __stdcall name args
API(H,CreateFileA,(const char*,U,U,void*,U,U,H));
API(I,DeviceIoControl,(H,U,void*,U,void*,U,U*,void*));
API(U,GetTickCount,(void));
API(H,LocalAlloc,(U,U));
API(H,LocalFree,(H));

enum { S_OK=0, S_FALSE=1 };
#define E_NOTIMPL ((I)0x80004001)
#define E_NOINTERFACE ((I)0x80004002)
#define E_POINTER ((I)0x80004003)
#define E_OUTOFMEMORY ((I)0x8007000E)
#define CLASS_E_CLASSNOTAVAILABLE ((I)0x80040111)
#define DIERR_DEVICEFULL ((I)0x80040201)
#define DIERR_INVALIDPARAM ((I)0x80070057)
enum { EFFECTS=16 };
#define INFINITE_TIME 0xffffffffu
/* The effect identifiers the registry gives DINPUT (DIEFFECTATTRIBUTES). */
enum { CONSTANT=1, RAMP=2, PERIODIC_FIRST=3, PERIODIC_LAST=7 };

static const unsigned char iid_unknown[16]={0,0,0,0,0,0,0,0,0xC0,0,0,0,0,0,0,0x46};
static const unsigned char iid_class_factory[16]={1,0,0,0,0,0,0,0,0xC0,0,0,0,0,0,0,0x46};
/* {02538130-898F-11D0-9AD0-00A0C9A06E35} */
static const unsigned char iid_effect_driver[16]={0x30,0x81,0x53,0x02,0x8F,0x89,0xD0,0x11,0x9A,0xD0,0x00,0xA0,0xC9,0xA0,0x6E,0x35};
/* {A9809800-4652-4F42-9A41-524D39384646}, CLSID of this driver */
static const unsigned char clsid_driver[16]={0x00,0x98,0x80,0xA9,0x52,0x46,0x42,0x4F,0x9A,0x41,0x52,0x4D,0x39,0x38,0x46,0x46};

static I same(const unsigned char *a,const unsigned char *b){for(U i=0;i<16;++i)if(a[i]!=b[i])return 0;return 1;}
void *memset(void *p,I c,unsigned n){unsigned char *b=p;for(U i=0;i<n;++i)b[i]=(unsigned char)c;return p;}

typedef struct { U used,type,strength,duration,gain,playing,end; } EFFECT;
typedef struct { void *const *vtbl; U refs,joystick,gain,paused; EFFECT effects[EFFECTS]; } DRIVER;
static H bridge;
static U objects;

static I sign(I v){return v<0?-v:v;}
static void rumble(DRIVER *d)
{
    /* The strongest running effect drives both motors. */
    U now=GetTickCount(),strength=0,until=0;
    for(U i=0;i<EFFECTS;++i)
    {
        EFFECT *e=&d->effects[i];
        if(!e->used||!e->playing||d->paused)continue;
        if(e->end!=INFINITE_TIME&&(I)(now-e->end)>=0){e->playing=0;continue;}
        U s=e->strength*e->gain/10000*d->gain/10000; /* at most 10000*10000 */
        if(s>strength){strength=s;until=e->end;}
    }
    if(strength>10000)strength=10000;
    if(!bridge||bridge==(H)~0u)bridge=CreateFileA("\\\\.\\NEO98HOST",0,0,0,3,0,0);
    if(bridge==(H)~0u)return;
    AH_Request r;memset(&r,0,sizeof r);r.version=AH_VERSION;r.action=AH_RUMBLE;r.id=d->joystick;
    r.low=r.high=(unsigned short)(strength*65535/10000);
    U ms=!strength?0:until==INFINITE_TIME?0xffff:until-now;
    r.flags=ms>0xffff?0xffff:ms;
    U done=0;DeviceIoControl(bridge,AH_IOCTL_ACTION,&r,sizeof r,0,0,&done,0);
}

static I __stdcall QueryInterface(DRIVER *d,const unsigned char *riid,void **out)
{
    if(!out)return E_POINTER;
    if(same(riid,iid_unknown)||same(riid,iid_effect_driver)){*out=d;++d->refs;return S_OK;}
    *out=0;return E_NOINTERFACE;
}
static U __stdcall AddRef(DRIVER *d){return ++d->refs;}
static U __stdcall Release(DRIVER *d)
{
    U left=--d->refs;
    if(!left){for(U i=0;i<EFFECTS;++i)d->effects[i].playing=0;rumble(d);LocalFree(d);--objects;}
    return left;
}
/* DeviceID(dwDIVer, dwExternalID, fBegin, dwInternalId, lpReserved): the
   external identifier is the joystick's, the index of the host controller. */
static I __stdcall DeviceID(DRIVER *d,U version,U external,U begin,U internal,void *reserved)
{if(begin)d->joystick=external;return S_OK;}
static I __stdcall GetVersions(DRIVER *d,U *versions)
{
    if(!versions||versions[0]<16)return DIERR_INVALIDPARAM;
    versions[1]=1;versions[2]=1;versions[3]=0x01000000;return S_OK;
}
static I __stdcall Escape(DRIVER *d,U id,void *escape){return E_NOTIMPL;}
static I __stdcall SetGain(DRIVER *d,U id,U gain){d->gain=gain>10000?10000:gain;rumble(d);return S_OK;}
/* DISFFC_RESET 1, STOPALL 2, PAUSE 4, CONTINUE 8, SETACTUATORSON 10h,
   SETACTUATORSOFF 20h. */
static I __stdcall SendForceFeedbackCommand(DRIVER *d,U id,U command)
{
    if(command&(1|2))for(U i=0;i<EFFECTS;++i)d->effects[i].playing=0;
    if(command&1)for(U i=0;i<EFFECTS;++i)d->effects[i].used=0;
    if(command&(4|0x20))d->paused=1;
    if(command&(8|0x10))d->paused=0;
    rumble(d);return S_OK;
}
/* DIDEVICESTATE: dwSize, dwState, dwLoad. DIGFFS_EMPTY 1, STOPPED 2,
   PAUSED 4, ACTUATORSON 10h, POWERON 40h. */
static I __stdcall GetForceFeedbackState(DRIVER *d,U id,U *state)
{
    if(!state||state[0]<12)return DIERR_INVALIDPARAM;
    rumble(d);
    U any=0,running=0;for(U i=0;i<EFFECTS;++i){any|=d->effects[i].used;running|=d->effects[i].used&&d->effects[i].playing;}
    state[1]=0x40|(d->paused?4:0x10)|(running?0:2)|(any?0:1);state[2]=0;return S_OK;
}
static void start(DRIVER *d,EFFECT *e,U count)
{
    e->playing=1;
    e->end=e->duration==INFINITE_TIME||!count||count==INFINITE_TIME?INFINITE_TIME:GetTickCount()+e->duration/1000*count;
}
/* DownloadEffect(dwID, dwEffectID, pdwEffect, peff, dwFlags). DIEFFECT:
   dwDuration +8 (microseconds), dwGain +16, cbTypeSpecificParams +44,
   lpvTypeSpecificParams +48. */
static I __stdcall DownloadEffect(DRIVER *d,U id,U type,U *handle,const U *effect,U flags)
{
    if(!handle||!effect)return DIERR_INVALIDPARAM;
    if(type<CONSTANT||type>PERIODIC_LAST)return DIERR_INVALIDPARAM;
    if(flags&0x80000000u)return S_OK; /* DIEP_NODOWNLOAD */
    EFFECT *e=0;
    if(*handle&&*handle<=EFFECTS&&d->effects[*handle-1].used)e=&d->effects[*handle-1];
    for(U i=0;!e&&i<EFFECTS;++i)if(!d->effects[i].used){e=&d->effects[i];memset(e,0,sizeof *e);e->used=1;e->gain=10000;e->duration=INFINITE_TIME;}
    if(!e)return DIERR_DEVICEFULL;
    e->type=type;
    if(flags&0x1)e->duration=effect[2];
    if(flags&0x4)e->gain=effect[4]>10000?10000:effect[4];
    const I *specific=(const I*)effect[12];
    if((flags&0x100)&&specific&&effect[11]>=4)
    {
        if(type==CONSTANT)e->strength=(U)sign(specific[0]);
        else if(type==RAMP&&effect[11]>=8){I a=sign(specific[0]),b=sign(specific[1]);e->strength=(U)(a>b?a:b);}
        else e->strength=(U)specific[0]; /* DIPERIODIC.dwMagnitude */
        if(e->strength>10000)e->strength=10000;
    }
    *handle=(U)(e-d->effects)+1;
    if(flags&0x20000000u)start(d,e,1); /* DIEP_START */
    rumble(d);return S_OK;
}
static EFFECT *find(DRIVER *d,U handle){return handle&&handle<=EFFECTS&&d->effects[handle-1].used?&d->effects[handle-1]:0;}
static I __stdcall DestroyEffect(DRIVER *d,U id,U handle)
{EFFECT *e=find(d,handle);if(!e)return DIERR_INVALIDPARAM;e->used=e->playing=0;rumble(d);return S_OK;}
/* StartEffect(dwID, dwEffect, dwMode, dwCount); DIES_SOLO 1 stops the others. */
static I __stdcall StartEffect(DRIVER *d,U id,U handle,U mode,U count)
{
    EFFECT *e=find(d,handle);if(!e)return DIERR_INVALIDPARAM;
    if(mode&1)for(U i=0;i<EFFECTS;++i)d->effects[i].playing=0;
    start(d,e,count);rumble(d);return S_OK;
}
static I __stdcall StopEffect(DRIVER *d,U id,U handle)
{EFFECT *e=find(d,handle);if(!e)return DIERR_INVALIDPARAM;e->playing=0;rumble(d);return S_OK;}
/* DEV_STS_EFFECT_RUNNING 1 */
static I __stdcall GetEffectStatus(DRIVER *d,U id,U handle,U *status)
{
    EFFECT *e=find(d,handle);if(!e||!status)return DIERR_INVALIDPARAM;
    rumble(d);*status=e->playing?1:0;return S_OK;
}
static void *const driver_vtbl[]={(void*)QueryInterface,(void*)AddRef,(void*)Release,(void*)DeviceID,(void*)GetVersions,(void*)Escape,
    (void*)SetGain,(void*)SendForceFeedbackCommand,(void*)GetForceFeedbackState,(void*)DownloadEffect,(void*)DestroyEffect,
    (void*)StartEffect,(void*)StopEffect,(void*)GetEffectStatus};

/* The class factory, one static object. */
typedef struct { void *const *vtbl; } FACTORY;
static FACTORY factory;
static I __stdcall FactoryQueryInterface(FACTORY *f,const unsigned char *riid,void **out)
{
    if(!out)return E_POINTER;
    if(same(riid,iid_unknown)||same(riid,iid_class_factory)){*out=f;return S_OK;}
    *out=0;return E_NOINTERFACE;
}
static U __stdcall FactoryAddRef(FACTORY *f){return 2;}
static U __stdcall FactoryRelease(FACTORY *f){return 1;}
static I __stdcall CreateInstance(FACTORY *f,void *outer,const unsigned char *riid,void **out)
{
    if(!out)return E_POINTER;*out=0;
    if(outer)return (I)0x80040110; /* CLASS_E_NOAGGREGATION */
    DRIVER *d=LocalAlloc(0x40,sizeof(DRIVER));if(!d)return E_OUTOFMEMORY;
    d->vtbl=driver_vtbl;d->refs=1;d->gain=10000;++objects;
    I result=QueryInterface(d,riid,out);Release(d);return result;
}
static I __stdcall LockServer(FACTORY *f,I lock){return S_OK;}
static void *const factory_vtbl[]={(void*)FactoryQueryInterface,(void*)FactoryAddRef,(void*)FactoryRelease,(void*)CreateInstance,(void*)LockServer};

I __stdcall DllGetClassObject(const unsigned char *clsid,const unsigned char *riid,void **out)
{
    if(!out)return E_POINTER;*out=0;
    if(!same(clsid,clsid_driver))return CLASS_E_CLASSNOTAVAILABLE;
    factory.vtbl=factory_vtbl;return FactoryQueryInterface(&factory,riid,out);
}
I __stdcall DllCanUnloadNow(void){return objects?S_FALSE:S_OK;}
I __stdcall DllMain(H instance,U reason,void *reserved){return 1;}
