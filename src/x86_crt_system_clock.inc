// SPDX-License-Identifier: GPL-2.0-or-later
// Includes the existing time, PTD and guest timezone provider in this TU.
unsigned long long Application::CrtClockValue(){
    CrtTimeEnvironment e(*this);auto system=e.Clock(false);Win98Time::Q stamp=0;
    if(!Win98Time::SystemToFile(system,stamp))Fail("CRT process clock could not read the guest UTC clock");return stamp;
}
unsigned Application::CrtSystemClockApi(ApiName name,const unsigned*a){
    namespace C=Win98CrtSystemClock;
    if(API_IS(name,"difftime")){ReturnDoubleBits(C::Difference(a[0],a[1]));return a[0]-a[1];}
    if(API_IS(name,"clock"))return C::Clock(CrtClockValue(),crt_threads->time_clock_start);
    CrtTimeEnvironment time(*this);Win98CrtTime::Tm tm;if(!Read(a[0],&tm,sizeof tm))return 0;
    struct Environment{
        Application&app;CrtTimeEnvironment&time;unsigned pointer;
        Win98Time::SystemTime Local(){return time.Clock(true);}
        void Normalize(Win98CrtTime::Tm&t){app.Store(pointer,&t,sizeof t);app.CrtTimeApi("mktime",&pointer);app.Read(pointer,&t,sizeof t);}
        bool SetLocal(const Win98Time::SystemTime&c){unsigned p=app.Allocate(16);if(!p){app.Fail("CRT SetLocalTime scratch unavailable");return false;}app.Store(p,&c,16);bool okay=app.TimezoneApi("SetLocalTime",&p)!=0;app.Free(p);return okay;}
        unsigned Error(){return app.last_error;}
    }e{*this,time,a[0]};
    unsigned result=API_IS(name,"_getsystime")?C::Get(e,tm):C::Set(e,tm,a[1]);Store(a[0],&tm,sizeof tm);return result;
}
