// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "win98_time.h"
#include "win98_performance_counter.h"
#include "win98_timezone.h"
#include <string.h>
namespace X86
{
struct Application::TimezoneEnvironment {
    Application &app;
    struct Shared { Win98Timezone::State state; unsigned long long anchor=0,local=0; bool started=false,loaded=false; };
    static Shared &System(){static Shared value;return value;}
    Shared &shared;
    static Shared&Initialized(){auto&shared=System();if(!shared.started){shared.started=true;shared.anchor=Application::HostNow();Win98Time::SystemTime initial={1998,6,4,25,12,0,0,0};Win98Time::SystemToFile(initial,shared.local);}return shared;}
    explicit TimezoneEnvironment(Application&a):app(a),shared(Initialized()) {Load();}
    using U=unsigned;using Q=Win98Time::Q;
    // The wall clock follows the host's clock, not the machine's: after a
    // pause it shows the true time, as a notebook does after its sleep. Only the counters stand.
    Q Local(){const Q value=shared.local+(guest_clock_frozen?0:(Application::HostNow()-shared.anchor)*10);return value-value%100000;}
    U Open(bool create){U key=0;bool made=false;U error=app.registry.Open(0x80000002,"System\\CurrentControlSet\\Control\\TimeZoneInformation",false,key,made);if(error&&create)error=app.registry.Open(0x80000002,"System\\CurrentControlSet\\Control\\TimeZoneInformation",true,key,made);if(error&&create)Error(error);return error?0:key;}
    void Close(U key){app.registry.Close(key);}
    void Delete(U key,const char*name){U node;if(!app.registry.Key(key,node))app.registry.store.DeleteValue(node,name);}
    U Query(U key,const char*name,U&type,void*out,U&size){U node,error=app.registry.Key(key,node);if(error)return error;const Registry::Value*value=nullptr;error=app.registry.store.Query(node,name,value);if(error)return error;type=value->type;U capacity=size;size=value->size;if(size>capacity)return 234;if(size)memcpy(out,value->data,size);return 0;}
    U Set(U key,const char*name,U type,const void*data,U size){U node,error=app.registry.Key(key,node);return error?error:app.registry.store.Set(node,name,type,data,size);}
    void Error(U value){app.last_error=value;}
    void BiasChanged(){} // IFSMGR reads the same native bias through TimeApi.
    void Lock(){}void Unlock(){} // These native provider calls do not yield.
    void Name(const unsigned short*in,char*out){
        U scratch=app.Allocate(100);if(!scratch){app.Fail("Timezone name conversion scratch unavailable");return;}
        app.Store(scratch,in,64);const U args[]={0,0,scratch,32,scratch+64,32,0,0};app.NlsApi(811,args);app.Read(scratch+64,out,32);out[32]=0;app.Free(scratch);
    }
    U SetLocalFields(const Win98Time::SystemTime&time){
        // DOS validates the date and time and stores hundredths. This is a guest clock.
        Q value;if(!Win98Time::SystemToFile(time,value)||!time.day){Error(87);return 0;}
        shared.local=value-value%100000;shared.anchor=Application::HostNow();return 1;
    }
    void SetLocal(Q value){Win98Time::SystemTime time;Win98Time::FileToSystem(value,time);SetLocalFields(time);}
    // The original publishes the persisted timezone once, at KERNEL32 startup:
    // the cached structure, the classification flag and
    // the active bias all come from
    // HKLM\System\CurrentControlSet\Control\TimeZoneInformation. Without this
    // the cache would start empty, and Explorer's first ordinal 99 would
    // classify a daylight change the stored state had already recorded and move
    // the guest clock an hour. A missing or malformed value leaves its field
    // alone, the way the original tolerates an incomplete key.
    // ConfigureTimeZoneBias still overrides everything afterwards.
    //
    void Load(){
        if(shared.loaded)return;
        shared.loaded=true;
        U key=Open(false);if(!key)return;
        auto&s=shared.state;
        auto binary=[&](const char*name,void*out,U bytes){
            U type=0,size=bytes;unsigned char scratch[16];
            if(bytes>sizeof scratch||Query(key,name,type,scratch,size)||type!=3||size!=bytes)return false;
            memcpy(out,scratch,bytes);return true;};
        auto name=[&](const char*value,unsigned short*out){
            // text is zero filled and one byte longer than the capacity, so a
            // payload that carries no trailing NUL still ends as a string.
            U type=0,size=33;char text[34]={};
            if(Query(key,value,type,text,size)||(type!=1&&type!=2)||!size)return false;
            for(U i=0;i<31&&text[i];++i)out[i]=(unsigned short)app.AnsiWide((unsigned char)text[i]);
            return true;};
        binary("Bias",&s.zone.bias,4);binary("StandardBias",&s.zone.standard_bias,4);
        binary("DaylightBias",&s.zone.daylight_bias,4);
        binary("StandardStart",&s.zone.standard,16);binary("DaylightStart",&s.zone.daylight,16);
        name("StandardName",s.zone.standard_name);name("DaylightName",s.zone.daylight_name);
        binary("DaylightFlag",&s.flag,4);
        if(binary("ActiveTimeBias",&s.minutes,4))s.bias=Win98Time::MinutesBias(s.minutes).ticks;
        // The repeated-hour exclusion is persisted beside the zone, so a restart
        // inside that hour still refuses a second adjustment.
        binary("NoChangeStart",&s.start,8);binary("NoChangeEnd",&s.end,8);
        // The battery clock of NEO98 is the Mac's local time (StartGuestClock),
        // which macOS has already moved across every daylight change. A PC's
        // RTC has not, which is what ordinal 99 corrects. Persist the current
        // classification without moving the clock; otherwise the first boot
        // after a change (a C: set up in summer, started in winter) moves the
        // guest clock an hour and RUNDLL announces it with timedate.cpl /m.
        const U flag=Win98Timezone::Classify(*this,s.zone);
        if(flag!=~0u&&flag!=s.flag){s.flag=flag;Set(key,"DaylightFlag",3,&s.flag,4);Win98Timezone::RefreshBias(*this,s,key);}
        Close(key);
    }
};
unsigned Application::TimezoneApi(ApiName name,const unsigned*a){
    namespace Z=Win98Timezone;TimezoneEnvironment e(*this);auto&s=e.shared.state;
    if(API_IS(name,"GetDaylightFlag"))return Z::GetDaylight(s);
    if(API_IS(name,"SetDaylightFlag"))return Z::SetDaylight(s,a[0]);
    if(API_IS(name,"ordinal_99"))return Z::Check(e,s,a[0]);
    unsigned size=strstr(name,"TimeZone")?172:16;
    const bool write=!strncmp(name,"Get",3);
    if(!a[0]||!virtual_memory.Access(a[0],size,write)){last_error=87;return 0;}
    if(API_IS(name,"GetTimeZoneInformation")){Z::Zone out;unsigned result=Z::Get(s,out);Store(a[0],&out,sizeof out);return result;}
    if(API_IS(name,"SetTimeZoneInformation")){Z::Zone in;Read(a[0],&in,sizeof in);return Z::Set(e,s,in);}
    Win98Time::SystemTime time;
    if(write){Win98Time::FileToSystem(e.Local()+(API_IS(name,"GetSystemTime")?s.bias:0),time);Store(a[0],&time,sizeof time);return 0;}
    Read(a[0],&time,sizeof time);
    return API_IS(name,"SetSystemTime")?Z::SetUtc(e,s,time):Z::SetLocal(e,s,time);
}
// Win98 takes its clock from the battery clock at boot and lets it run on;
// the next start sees a later time. The port starts every session at the
// same fixed moment (1998-06-25 12:00) unless the host sets the clock here,
// so without this call the guest time runs backwards between two sessions.
// ASProtect reads that as a turned-back clock and ends Super Collapse with
// ExitProcess(-1) before its first window. Runners keep the fixed
// start for reproducible pictures; the SDL host passes the Mac's local time.
// A runner that needs the same guest time of day at every read (a program
// that seeds its random numbers from the BIOS clock and a test that expects
// one deal) holds the clock at its start; the tick count and the timers run
// on as before. Never set by the host.
bool Application::guest_clock_frozen = false;
void Application::StartGuestClock(const unsigned short fields[8]){
    auto&shared=TimezoneEnvironment::System();
    Win98Time::SystemTime local={fields[0],fields[1],fields[2],fields[3],fields[4],fields[5],fields[6],fields[7]};
    Win98Time::Q value=0;if(!Win98Time::SystemToFile(local,value))return;
    shared.started=true;shared.anchor=Application::HostNow();shared.local=value-value%100000;
}
void Application::ConfigureTimeZoneBias(int minutes){TimezoneEnvironment e(*this);e.shared.state={};e.shared.state.zone.bias=unsigned(minutes);e.shared.state.minutes=unsigned(minutes);e.shared.state.bias=Win98Time::MinutesBias(unsigned(minutes)).ticks;e.shared.state.flag=1;}
unsigned long long Application::CurrentFileTime(){TimezoneEnvironment e(*this);return e.Local()+e.shared.state.bias;}
unsigned Application::EffectiveTimeBias(){TimezoneEnvironment e(*this);return e.shared.state.minutes;}
Files::TimeStamp Application::FileClock(){
 auto&s=TimezoneEnvironment::Initialized();Files::TimeStamp stamp;
 auto utc=s.local+(guest_clock_frozen?0:(HostNow()-s.anchor)*10)+s.state.bias;
 Win98Time::DosService(0,stamp.date,stamp.time,stamp.hundredths,utc,s.state.minutes);return stamp;
}

unsigned Application::TimeApi(ApiName name, const unsigned *a)
{
    using Q = Win98Time::Q;
    if (API_IS(name,"GetSystemTimeAsFileTime")) {
        TimezoneEnvironment e(*this);
        const Q value = e.Local() + e.shared.state.bias;
        Store(a[0], &value, sizeof value);
        return 0;
    }
    if (API_IS(name,"QueryPerformanceCounter") || API_IS(name,"QueryPerformanceFrequency"))
    {
        struct E {
            Application &app;
            bool Probe(unsigned p) { return p && app.virtual_memory.Access(p,8,true); }
            void Error(unsigned value) { app.last_error=value; }
            void Write32(unsigned p,unsigned value) { app.Write32(p,value); }
            unsigned VwinQuery(unsigned p) {
                // Now is CLOCK_MONOTONIC in microseconds on the native host,
                // and the host monotonic clock.
                return Win98Performance::Pack(*this,p,Win98Performance::FromMicroseconds(Application::Now()));
            }
        } e{*this};
        return Win98Performance::Query(e,a[0],API_IS(name,"QueryPerformanceFrequency"));
    }
    const auto bias = Win98Time::MinutesBias(EffectiveTimeBias());
    auto probe = [&](unsigned p, unsigned bytes, bool write) {
        if (!p || !virtual_memory.Access(p, bytes, write))
        {
            last_error = 87;
            return false;
        }
        return true;
    };
    auto file = [&](unsigned p) { return Q(Read32(p)) | (Q(Read32(p + 4)) << 32); };
    auto store_file = [&](unsigned p, Q value) {
        // Local/UTC originals store the high DWORD first. No provider yields.
        Write32(p + 4, unsigned(value >> 32));
        Write32(p, unsigned(value));
    };
    switch (name.id)
    {
    API_CASE("SystemTimeToFileTime")
    {
        if (!probe(a[0], 16, false) || !probe(a[1], 8, true)) return 0;
        Win98Time::SystemTime in;
        if (!Read(a[0], &in, sizeof in)) return 0;
        Q out;
        if (!Win98Time::SystemToFile(in, out)) return 0;
        store_file(a[1], out);
        return 1;
    }
    API_CASE("FileTimeToSystemTime")
    {
        if (!probe(a[0], 8, false) || !probe(a[1], 16, true)) return 0;
        Win98Time::SystemTime out;
        Win98Time::FileToSystem(file(a[0]), out);
        return Store(a[1], &out, sizeof out);
    }
    API_CASE("CompareFileTime")
    {
        if (!probe(a[0], 8, false) || !probe(a[1], 8, false)) return 0;
        return Win98Time::Compare(file(a[0]), file(a[1]));
    }
    API_CASE("FileTimeToLocalFileTime") API_CASE("LocalFileTimeToFileTime")
    {
        if (!probe(a[0], 8, false) || !probe(a[1], 8, true)) return 0;
        const Q value = file(a[0]);
        store_file(a[1], name[0] == 'F' ? Win98Time::ToLocal(value, bias.ticks)
                                      : Win98Time::ToUtc(value, bias.ticks));
        return 1;
    }
    API_CASE("DosDateTimeToFileTime")
    {
        if (!probe(a[2], 8, true)) return 0;
        Q out;
        const unsigned error = Win98Time::DosToFile(a[0], a[1], bias, out);
        if (error) { last_error = error; return 0; }
        store_file(a[2], out);
        return 1;
    }
    API_CASE("FileTimeToDosDateTime")
    {
        if (!probe(a[0], 8, false) || !probe(a[1], 2, true) || !probe(a[2], 2, true)) return 0;
        unsigned date, time;
        const unsigned error = Win98Time::FileToDos(file(a[0]), bias, date, time);
        if (error) { last_error = error; return 0; }
        const unsigned short d = date, t = time;
        // DOS gateway Windows 98 writes time before date, including aliased outputs.
        return Store(a[2], &t, 2) && Store(a[1], &d, 2);
    }
    default:
        break;
    }
    Fail("Unrouted time conversion");
    return 0;
}
} // namespace X86
