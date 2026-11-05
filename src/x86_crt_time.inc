// SPDX-License-Identifier: GPL-2.0-or-later
// Native process/PTD providers for the recovered MSVCRT time family.
struct Application::CrtTimeEnvironment {
    Application&app;CrtThreads&threads;Win98CrtTime::State&state;
    explicit CrtTimeEnvironment(Application&a):app(a),threads(*a.crt_threads),state(threads.time_state){}
    unsigned Data(){
        if(threads.time_data)return threads.time_data;
        const unsigned p=app.Allocate(768);if(!p){app.Fail("CRT time data allocation failed");return 0;}
        threads.time_data=p;app.Write32(p,28800);app.Write32(p+4,1);app.Write32(p+8,unsigned(-3600));
        app.Write32(p+12,p+32);app.Write32(p+16,p+96);app.Store(p+32,"PST",4);app.Store(p+96,"PDT",4);return p;
    }
    void ReadGlobals(){unsigned p=Data();state.timezone=Win98CrtTime::I(app.Read32(p));state.daylight=Win98CrtTime::I(app.Read32(p+4));state.dstbias=Win98CrtTime::I(app.Read32(p+8));app.Read(app.Read32(p+12),state.standard,64);app.Read(app.Read32(p+16),state.summer,64);}
    void WriteGlobals(){unsigned p=Data();app.Write32(p,unsigned(state.timezone));app.Write32(p+4,unsigned(state.daylight));app.Write32(p+8,unsigned(state.dstbias));
        app.Store(app.Read32(p+12),state.standard,unsigned(strlen(state.standard))+1);app.Store(app.Read32(p+16),state.summer,unsigned(strlen(state.summer))+1);
    }
    char*Environment(){
        // Read the CRT's writable _environ table, not the host environment.
        unsigned table=app.Read32(app.crt_data+32);
        for(unsigned i=0;table&&i<4096;++i){unsigned p=app.Read32(table+i*4);if(!p)return nullptr;char prefix[3];if(!app.Read(p,prefix,3))return nullptr;
            if((prefix[0]=='T'||prefix[0]=='t')&&(prefix[1]=='Z'||prefix[1]=='z')&&prefix[2]=='='){
                unsigned n=0;char c;do{if(n==32768){app.Fail("CRT TZ exceeds native environment limit");return nullptr;}if(!app.Read(p+3+n,&c,1))return nullptr;++n;}while(c);
                char*out=static_cast<char*>(malloc(n));if(!out){app.Fail("CRT TZ snapshot unavailable");return nullptr;}app.Read(p+3,out,n);return out;}}
        if(table)app.Fail("CRT environment table exceeds native limit");return nullptr;
    }
    unsigned Zone(Win98Timezone::Zone&out){unsigned p=Data()+160;const unsigned a[]={p};unsigned result=app.TimezoneApi("GetTimeZoneInformation",a);app.Read(p,&out,sizeof out);return result;}
    void Name(const unsigned short*input,char*out){
        unsigned p=app.Allocate(160);if(!p){app.Fail("CRT timezone name scratch unavailable");out[0]=0;return;}app.Store(p,input,64);app.Write32(p+128,0);
        const unsigned a[]={app.LocaleCodePage(),0x220,p,0xffffffff,p+64,63,0,p+128};unsigned result=app.NlsApi(811,a);
        if(result&&!app.Read32(p+128)){app.Read(p+64,out,result);out[63]=0;}else out[0]=0;app.Free(p);
    }
    void Tzset(bool force){
        ReadGlobals();if(!force&&state.initialized)return;
        app.CrtLock(true,11);app.CrtLock(true,12);state.start.year=state.end.year=-1;state.os_zone=false;
        char*tz=Environment();
        if(tz){
            if(*tz&&(!threads.time_environment||strcmp(tz,threads.time_environment))){
                free(threads.time_environment);threads.time_environment=tz;tz=nullptr;
                if(!Win98CrtTime::ParseTz(state,threads.time_environment))app.Fail("Original CRT TZ parser would read past a short value");
            }
            free(tz);app.CrtLock(false,12);
        }else{
            app.CrtLock(false,12);Win98Timezone::Zone zone;unsigned flag=Zone(zone);
            if(flag!=~0u){Win98CrtTime::Zone(state,zone);Name(zone.standard_name,state.standard);Name(zone.daylight_name,state.summer);}
        }
        WriteGlobals();if(!force)state.initialized=true;app.CrtLock(false,11);
    }
    unsigned Buffer(unsigned offset,unsigned size,unsigned fallback){
        const unsigned ptd=Win98ThreadCrt::GetData(threads,threads.core);if(!ptd)return 0;unsigned result=app.Read32(ptd+offset);
        if(!result){result=app.CrtApi("malloc",&size);app.Write32(ptd+offset,result);}return result?result:Data()+fallback;
    }
    Win98Time::SystemTime Clock(bool local){unsigned p=Data()+384;const unsigned a[]={p};app.TimezoneApi(local?"GetLocalTime":"GetSystemTime",a);Win98Time::SystemTime result;app.Read(p,&result,sizeof result);return result;}
    int Daylight(){Win98Timezone::Zone z;unsigned flag=Zone(z);return flag==~0u?-1:(flag==2&&z.daylight.month&&z.daylight_bias)?1:0;}
};
unsigned Application::CrtTimeExport(const char*name){
    unsigned offset;if(!strcmp(name,"_timezone"))offset=0;else if(!strcmp(name,"_daylight"))offset=4;else if(!strcmp(name,"_dstbias"))offset=8;else if(!strcmp(name,"_tzname"))offset=12;else return 0;
    if(!crt_threads&&!InitializeThreadCrt())return 0;CrtTimeEnvironment e(*this);return e.Data()+offset;
}
unsigned Application::CrtTimeApi(ApiName name,const unsigned*a){
    namespace C=Win98CrtTime;CrtTimeEnvironment e(*this);if(!e.Data())return 0;
    switch (name.id)
    {
    API_CASE("__p__timezone")return e.Data();API_CASE("__p__daylight")return e.Data()+4;API_CASE("__p__dstbias")return e.Data()+8;API_CASE("__p__tzname")return e.Data()+12;
    API_CASE("_tzset"){e.Tzset(true);return 0;}
    API_CASE("time"){
        auto local=e.Clock(true),system=e.Clock(false);auto&old=crt_threads->time_previous;
        if(system.year!=old.year||system.month!=old.month||system.day!=old.day||system.hour!=old.hour||system.minute!=old.minute){crt_threads->time_daylight=e.Daylight();old=system;}
        e.Tzset(false);const unsigned result=unsigned(C::FromLocal(e.state,local,crt_threads->time_daylight));if(a[0])Write32(a[0],result);return result;
    }
    API_CASE("_ftime"){
        e.Tzset(false);auto system=e.Clock(false);Win98Time::Q value;if(!Win98Time::SystemToFile(system,value)){Fail("CRT _ftime clock provider failed");return 0;}
        const unsigned minute=unsigned(value/600000000);if(minute!=crt_threads->time_ftime_minute){crt_threads->time_ftime_minute=minute;crt_threads->time_ftime_daylight=e.Daylight();}
        unsigned seconds=unsigned((value-116444736000000000ull)/10000000);unsigned short fields[]={static_cast<unsigned short>((value/10000)%1000),static_cast<unsigned short>(e.state.timezone/60),static_cast<unsigned short>(crt_threads->time_ftime_daylight)};
        Write32(a[0],seconds);Store(a[0]+4,fields,sizeof fields);return 0;
    }
    default:
        break;
    }
    const bool gmt=API_IS(name,"gmtime"),local=API_IS(name,"localtime"),make=API_IS(name,"mktime"),ct=API_IS(name,"ctime")||API_IS(name,"_wctime"),wide=API_IS(name,"_wasctime")||API_IS(name,"_wctime");
    C::Tm tm{};
    if(make){if(!Read(a[0],&tm,sizeof tm))return ~0u;unsigned result=unsigned(C::Mktime(e.state,tm,true,[&]{e.Tzset(false);}));if(result!=~0u){const unsigned buffer=e.Buffer(0x40,36,512);if(buffer)Store(buffer,&tm,sizeof tm);}Store(a[0],&tm,sizeof tm);return result;}
    if(gmt||local||ct){int value=C::I(Read32(a[0]));if(value<0)return 0;if(!gmt)e.Tzset(false);
        if(!(gmt?C::Gmtime(value,tm):C::Localtime(e.state,value,tm)))return 0;
        unsigned result=e.Buffer(0x40,36,512);if(!result)return 0;Store(result,&tm,sizeof tm);if(!ct)return result;
    }else if(!Read(a[0],&tm,sizeof tm))return 0;
    const unsigned output=e.Buffer(wide?0x3c:0x38,wide?52:26,wide?584:552);if(!output)return 0;
    if(wide){unsigned short text[26];if(!C::Asctime(tm,text)){Fail("Original wide asctime table index is outside its resources");return 0;}Store(output,text,sizeof text);}
    else{char text[26];if(!C::Asctime(tm,text)){Fail("Original asctime table index is outside its resources");return 0;}Store(output,text,sizeof text);}return output;
}

unsigned Application::CrtTimeName(bool daylight){CrtTimeEnvironment e(*this);e.Tzset(false);return Read32(e.Data()+12+(daylight?4:0));}
