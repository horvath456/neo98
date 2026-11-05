// SPDX-License-Identifier: GPL-2.0-or-later
// Existing LC_TIME strings/LC_CTYPE tables and checked guest-memory providers.
unsigned Application::CrtTimeFormatApi(ApiName name,const unsigned*a){
    namespace F=Win98CrtTimeFormat;
    if(name[0]=='_'&&!API_IS(name,"_Strftime")){
        unsigned scratch=Allocate(16);if(!scratch){Fail("CRT local clock scratch unavailable");return 0;}const unsigned args[]={scratch};TimezoneApi("GetLocalTime",args);Win98Time::SystemTime clock;Read(scratch,&clock,16);Free(scratch);
        bool wide=name[1]=='w',date=strstr(name,"date")!=nullptr;
        if(wide){unsigned short text[9];F::Short(clock,date,text);return Store(a[0],text,sizeof text)?a[0]:0;}
        char text[9];F::Short(clock,date,text);return Store(a[0],text,sizeof text)?a[0]:0;
    }
    if(!crt_locale)crt_locale=new CrtLocale(*this);if(!crt_locale)return 0;
    struct Memory {
        Application&app;CrtLocale&locale;unsigned custom=0,zone[2]={},tm_pointer=0;Win98CrtTime::Tm*tm_value=nullptr;bool tm_loaded=false;
        bool Good(){return app.state!=Failed&&app.state!=Exited;}
        unsigned Read8(unsigned p){unsigned char c=0;app.Read(p,&c,1);return c;}
        unsigned Read16(unsigned p){unsigned short c=0;app.Read(p,&c,2);return c;}
        void Write8(unsigned p,unsigned c){unsigned char b=c;if(Good())app.Store(p,&b,1);}
        void Write16(unsigned p,unsigned c){unsigned short b=c;if(Good())app.Store(p,&b,2);}
        bool LoadTm(){if(!tm_loaded){if(!tm_pointer){app.Fail("Original CRT time formatter dereferences a null tm");return false;}tm_loaded=app.Read(tm_pointer,tm_value,sizeof *tm_value);}return tm_loaded;}
        void BadIndex(){app.Fail("Original CRT time-format table index is outside its resources");}
        unsigned Label(unsigned index,unsigned offset){
            if(index>=43||(!custom&&offset>=128)){BadIndex();return 0;}
            if(custom){if(offset>=32768){app.Fail("Custom CRT time label exceeds native bound");return 0;}return Read8(app.Read32(custom+index*4)+offset);}
            return static_cast<unsigned char>(locale.data.time[index][offset]);
        }
        bool Lead(unsigned c){return (locale.data.ctype[(c&255)+1]&0x8000)!=0;}
        unsigned Zone(bool daylight,unsigned offset){if(!offset)zone[daylight]=app.CrtTimeName(daylight);return zone[daylight]?Read8(zone[daylight]+offset):0;}
        unsigned Allocate(unsigned bytes){return app.CrtApi("malloc",&bytes);}
        void Free(unsigned p){app.CrtApi("free",&p);}
        unsigned WideLength(unsigned p){unsigned n=0;while(Good()&&Read16(p+n*2)){if(++n>=524288){app.Fail("CRT wide time format exceeds native bound");break;}}return n;}
        const Win98Nls::Table*Table(){
            static const Win98Nls::Table tables[]={{OriginalCodePage1252,sizeof OriginalCodePage1252},{OriginalCodePage437,sizeof OriginalCodePage437},{OriginalCodePage850,sizeof OriginalCodePage850}};
            unsigned page=locale.locale.code_page,index=page==1252?0:page==437?1:page==850?2:3;
            if(index==3||!tables[index].Valid()){app.Fail("CRT time-format multibyte codepage provider is not ported");return nullptr;}return &tables[index];
        }
        unsigned NarrowBuffer(unsigned output,unsigned input,unsigned count){
            unsigned i=0;for(;i<count&&Good();++i){unsigned c=Read16(input+i*2),byte=c;
                if(!locale.locale.code_page){if(c>255){app.SetCrtErrno(42);return ~0u;}}
                else{auto*t=Table();if(!t)return ~0u;byte=t->Narrow(c);unsigned info=t->Offset(1);if(byte==(t->Half(info+2)&255)&&c!=t->Half(info+6)){app.SetCrtErrno(42);return ~0u;}}
                Write8(output+i,byte);if(!c)return i;
            }return i;
        }
        unsigned WideBuffer(unsigned output,unsigned input,unsigned count){
            unsigned i=0;for(;i<count&&Good();++i){unsigned byte=Read8(input+i),c=byte;
                if(locale.locale.code_page){auto*t=Table();if(!t)return ~0u;c=t->Wide(byte);}Write16(output+i*2,c);if(!byte)return i;
            }return i;
        }
        unsigned Format(unsigned output,unsigned capacity,unsigned format,unsigned input){
            // Capacity zero reaches no format or tm read in the original.
            if(!capacity)return 0;Win98CrtTime::Tm tm{};tm_pointer=input;tm_value=&tm;tm_loaded=false;
            F::Formatter<Memory> f{*this,tm,output,capacity,capacity};return f.Run(format);
        }
    }memory{*this,*crt_locale,API_IS(name,"_Strftime")?a[4]:0};
    // Existing locale initialization owns these strings and the original lock19.
    CrtLock(true,19);unsigned result;
    if(API_IS(name,"wcsftime")){
        if(a[1]>1048576){Fail("CRT wide time output exceeds native bound");result=0;}
        else result=F::Wide(memory,a[0],a[1],a[2],a[3]);
    }else result=memory.Format(a[0],a[1],a[2],a[3]);
    CrtLock(false,19);return result;
}
