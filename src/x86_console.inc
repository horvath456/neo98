// SPDX-License-Identifier: GPL-2.0-or-later
#include "gdi.h"
#ifdef NEO98_NO_UNICORN
namespace {
Application::ConsoleOutput host_console_output = nullptr;
const Application *host_console_ended[64] = {};
bool HostConsoleEnded(const Application *app)
{ for (auto *entry : host_console_ended) if (entry == app) return true; return false; }
}
void Application::SetConsoleOutput(ConsoleOutput output) { host_console_output = output; }
#else
void Application::SetConsoleOutput(ConsoleOutput) {}
#endif
// Native console buffers. No CONAGENT, DOS VM or 16-bit console service.
struct Application::ConsoleState {
    unsigned references=1;
    unsigned *pixels=nullptr;
    unsigned long long frame_hash=0;
    ~ConsoleState(){delete[] pixels;}
    struct Cell { unsigned short character=32, attribute=7; };
    struct Screen {
        Cell *cells=nullptr;
        unsigned width=80,height=25,x=0,y=0,attribute=7,mode=3,cursor_size=25;
        // The original console writes through the DOS output path. Its tab
        // column survives SetConsoleCursorPosition; extra screen buffers use
        // their actual cursor and honor the wrap flag (console_clip probe).
        unsigned output_column=0;
        bool primary=false;
        bool cursor_visible=true;
        short window[4]={0,0,79,24};
        ~Screen(){delete[] cells;}
        bool Resize(unsigned w,unsigned h){
            if(!w||!h||w>32767||h>32767||(unsigned long long)w*h>1048576)return false;
            auto *p=new Cell[w*h];if(!p)return false;
            for(unsigned row=0;row<h&&row<height;++row)
                for(unsigned col=0;cells&&col<w&&col<width;++col)p[row*w+col]=cells[row*width+col];
            delete[] cells;cells=p;width=w;height=h;
            if(x>=w)x=w-1;if(y>=h)y=h-1;return true;
        }
        void Put(unsigned char ch){
            if(mode&1){
                if(ch==7)return;
                if(ch==8){if(x)--x;if(output_column)--output_column;return;}
                if(ch==13){x=output_column=0;return;}
                if(ch==10){x=output_column=0;++y;goto scroll;}
                if(ch==9){unsigned n=8-(primary?output_column:x)%8;while(n--)Put(' ');return;}
            }
            cells[y*width+x]={(unsigned short)ch,(unsigned short)attribute};
            ++output_column;
            if(++x==width){if((mode&2)||(primary&&(mode&1))){x=0;++y;}else x=width-1;}
          scroll:
            if(y==height){memmove(cells,cells+width,(height-1)*width*sizeof(Cell));
                for(unsigned i=(height-1)*width;i<height*width;++i)cells[i]={32,(unsigned short)attribute};y=height-1;}
        }
    } screens[16];
    struct Handle {unsigned generation=1,kind=0,screen=0,access=0,references=0;bool inherit=false;} handles[64];
    unsigned input_mode=7,active=0,input_count=0;
    unsigned char input[256][20]{};
    bool pending_lf=false;
    char title[1024]{};
    unsigned NewHandle(unsigned kind,unsigned screen,unsigned access){
        static unsigned next_generation=1;
        for(unsigned i=0;i<64;++i)if(!handles[i].kind){auto &h=handles[i];h.generation=next_generation;next_generation=next_generation%65535+1;h.kind=kind;h.screen=screen;h.access=access;h.references=1;h.inherit=false;return 0x62000000|(h.generation<<8)|(i*4+3);}
        return ~0u;
    }
    Handle *Find(unsigned value){unsigned slot=(value&255)>>2;
        if((value&0xff000003)!=0x62000003||slot>=64)return nullptr;
        auto &h=handles[slot];return h.kind&&h.generation==((value>>8)&65535)?&h:nullptr;}
};

bool Application::OwnsConsole(unsigned h) const {
    return console&&console_handles[(h&255)>>2]==h&&console->Find(h);
}
unsigned Application::OpenConsoleFile(const unsigned *a){
    if(!console){last_error=6;return ~0u;}
    if(a[4]!=3||a[6]||(a[1]&~0xc0000000u)||(a[2]&~3u)) {last_error=87;return ~0u;}
    bool inherit=false;
    if(a[3]){
        if(!virtual_memory.Access(a[3],12,false)||Read32(a[3])!=12){last_error=87;return ~0u;}
        inherit=Read32(a[3]+8)!=0;
    }
    const char *path=ReadString(a[0]);
    const bool input=Fold(path[3])=='I';
    const unsigned h=console->NewHandle(input?1:2,console->active,a[1]);
    if(h==~0u){last_error=8;return h;}
    console_handles[(h&255)>>2]=h;console->Find(h)->inherit=inherit;return h;
}
bool Application::InheritConsole(const Application &source,bool inherit,unsigned flags){
    if(flags&0x10)return true; // CREATE_NEW_CONSOLE already allocated by Start.
    if(flags&8){FreeConsoleState();memset(standard_handles,0,sizeof standard_handles);return true;}
    if(!source.console)return true;
    FreeConsoleState();console=source.console;++console->references;
    // Only the ignore flag is inherited. Handler addresses belong to the
    // registering process, even when its child shares the screen buffer.
    console_ignore_ctrl_c=source.console_ignore_ctrl_c;
    if(inherit)for(unsigned value:source.console_handles)if(value){
        auto *h=console->Find(value);
        if(h&&h->inherit){console_handles[(value&255)>>2]=value;++h->references;}
    }
    for(unsigned i=0;i<3;++i){
        const unsigned value=source.standard_handles[i];
        if(!source.OwnsConsole(value)){standard_handles[i]=0;continue;}
        if(OwnsConsole(value)){standard_handles[i]=value;continue;}
        const auto *h=console->Find(value);
        const unsigned copy=console->NewHandle(h->kind,h->screen,h->access);
        if(copy==~0u){last_error=8;return false;}
        console_handles[(copy&255)>>2]=copy;standard_handles[i]=copy;
    }
    return true;
}
// One row of the active screen buffer as text, trailing blanks cut; false
// without a console or past the last row. For runners and tests.
bool Application::ConsoleText(unsigned row,char *out,unsigned size) const{
    if(!console||!out||!size)return false;
    const auto &screen=console->screens[console->active];
    if(!screen.cells||row>=screen.height)return false;
    unsigned n=0;
    for(unsigned col=0;col<screen.width&&n+1<size;++col)out[n++]=(char)screen.cells[row*screen.width+col].character;
    while(n&&out[n-1]==' ')--n;
    out[n]=0;return true;
}
void Application::FreeConsoleState(){
#ifdef NEO98_NO_UNICORN
    for (auto *&entry : host_console_ended) if (entry == this) entry = nullptr;
#endif
    if(console){
        for(unsigned value:console_handles)if(value){const unsigned close[]={value};ConsoleApi("CloseHandle",close);}
        if(!--console->references)delete console;
    }
    memset(console_handles,0,sizeof console_handles);
    console_ctrl_count=0;console_ignore_ctrl_c=false;
    console=nullptr;console_generation=console_generation%65535+1;
}
unsigned Application::ConsoleInputSpace() const { return console ? 256 - console->input_count : 256 - input_count; }
unsigned Application::ConsoleCodePage() { InitializeNls(); return system_locale.oem; }
void Application::EndConsoleInput() {
#ifdef NEO98_NO_UNICORN
    for (auto *&entry : host_console_ended) if (!entry || entry == this) { entry = this; break; }
#endif
}
bool Application::ConsoleReady() const {return !console||console->input_count!=0||console->pending_lf
#ifdef NEO98_NO_UNICORN
    ||HostConsoleEnded(this)
#endif
    ;}
unsigned Application::ConsoleApi(ApiName name,const unsigned *a){
    const auto bad=[&](unsigned e){last_error=e;return 0u;};
    const auto new_handle=[&](unsigned kind,unsigned screen,unsigned access){
        const unsigned value=console->NewHandle(kind,screen,access);
        if(value!=~0u)console_handles[(value&255)>>2]=value;
        return value;
    };
    const auto output=[&](unsigned p,unsigned size){return virtual_memory.Access(p,size,true);};
    if(API_IS(name,"GetStdHandle")||API_IS(name,"SetStdHandle")){
        if(a[0]<0xfffffff4u||a[0]>0xfffffff6u){last_error=name[0]=='S'?1:87;return name[0]=='S'?0:~0u;}
        const unsigned index=0xfffffff6u-a[0];
        if(name[0]=='S'){
            if(a[1]!=~0u&&!files.Owns(a[1])&&!OwnsPipe(a[1])&&!OwnsConsole(a[1])){last_error=6;return 0;}
            standard_handles[index]=a[1];return 1;
        }
        return standard_handles[index];
    }
    if(API_IS(name,"DuplicateHandle")){
        auto*h=OwnsConsole(a[1])?console->Find(a[1]):nullptr;if(!h)return bad(6);
        if(!output(a[3],4)||a[6]&~3u)return bad(87);
        unsigned access=(a[6]&2)?h->access:a[4];if(access&~h->access)return bad(5);
        unsigned result=new_handle(h->kind,h->screen,access);if(result==~0u)return bad(8);
        console->Find(result)->inherit=a[5]!=0;Write32(a[3],result);if(a[6]&1){unsigned close[]={a[1]};ConsoleApi("CloseHandle",close);}return 1;
    }
    if(API_IS(name,"AllocConsole")){
        if(console)return bad(5);
        console=new ConsoleState;if(!console)return bad(8);
        if(!console->screens[0].Resize(80,25)){FreeConsoleState();return bad(8);}
        console->screens[0].primary=true;
        for(auto &h:console->handles)h.generation=console_generation;
        strcpy(console->title,program_name);
        standard_handles[0]=new_handle(1,0,0xc0000000);
        standard_handles[1]=new_handle(2,0,0xc0000000);
        standard_handles[2]=new_handle(2,0,0xc0000000);
        return 1;
    }
    if(!console)return bad((API_IS(name,"GetConsoleCP")||API_IS(name,"GetConsoleOutputCP"))?1:6);
    auto &c=*console;
    switch (name.id)
    {
    API_CASE("FreeConsole"){FreeConsoleState();for(auto &h:standard_handles)if((h&0xff000000)==0x62000000)h=0;return 1;}
    API_CASE("GetConsoleCP") API_CASE("GetConsoleOutputCP"){InitializeNls();return system_locale.oem;}
    API_CASE("GetConsoleTitleA"){
        if(!a[1])return 0;if(!output(a[0],a[1]))return bad(87);
        unsigned n=strlen(c.title);if(n>=a[1])n=a[1]-1;
        Store(a[0],c.title,n);const unsigned char zero=0;Store(a[0]+n,&zero,1);return n;
    }
    API_CASE("SetConsoleTitleA"){const char *title=ReadString(a[0]);if(strlen(title)>=sizeof c.title)return bad(87);strcpy(c.title,title);return 1;}
    API_CASE("GetNumberOfConsoleMouseButtons"){if(!output(a[0],4))return bad(87);Write32(a[0],3);return 1;}
    API_CASE("SetConsoleCtrlHandler"){
        if(!a[0]){console_ignore_ctrl_c=a[1]!=0;return 1;}
        if(a[1]){if(console_ctrl_count==32)return bad(8);console_ctrl_handlers[console_ctrl_count++]=a[0];return 1;}
        for(unsigned i=console_ctrl_count;i;--i)if(console_ctrl_handlers[i-1]==a[0]){memmove(console_ctrl_handlers+i-1,console_ctrl_handlers+i,(console_ctrl_count-i)*4);--console_ctrl_count;return 1;}
        return bad(87);
    }
    API_CASE("GenerateConsoleCtrlEvent"){
        if(a[0]>1)return bad(87);if(a[1]&&a[1]!=process_id)return bad(87);
        if(!a[0]&&console_ignore_ctrl_c)return 1;
        unsigned handlers[32], count=console_ctrl_count;memcpy(handlers,console_ctrl_handlers,count*4);
        for(unsigned i=count;i;--i){if(Call(handlers[i-1],a,1))return 1;if(state==Exited||state==Failed)return 1;}
        exit_code=0xc000013a;state=Exited;return 1;
    }
    API_CASE("CreateConsoleScreenBuffer"){
        if(a[3]!=1||a[4]||(a[0]&~0xc0000000u)||(a[1]&~3u)) {last_error=87;return ~0u;}
        for(unsigned i=1;i<16;++i)if(!c.screens[i].cells){
            c.screens[i].x=c.screens[i].y=0;c.screens[i].attribute=7;c.screens[i].mode=3;
            c.screens[i].cursor_size=25;c.screens[i].cursor_visible=true;
            const short initial_window[]={0,0,79,24};memcpy(c.screens[i].window,initial_window,8);
            if(!c.screens[i].Resize(80,25)){last_error=8;return ~0u;}
            unsigned h=new_handle(2,i,a[0]);if(h==~0u){delete[] c.screens[i].cells;c.screens[i].cells=nullptr;last_error=8;}return h;
        }last_error=8;return ~0u;
    }
    default:
        break;
    }
    auto *h=OwnsConsole(a[0])?c.Find(a[0]):nullptr;if(!h)return bad(6);
    if(API_IS(name,"CloseHandle")){
        const unsigned screen=h->screen,kind=h->kind;
        console_handles[(a[0]&255)>>2]=0;
        if(--h->references)return 1;
        h->kind=0;
        bool used=false;for(const auto &other:c.handles)if(other.kind==2&&other.screen==screen)used=true;
        if(kind==2&&!used&&c.active!=screen){delete[] c.screens[screen].cells;c.screens[screen].cells=nullptr;}return 1;
    }
    if(API_IS(name,"GetFileType"))return 2;
    bool reading=!strncmp(name,"Read",4)||!strncmp(name,"Get",3)||!strncmp(name,"Peek",4);
    if(!(h->access&(reading?0x80000000u:0x40000000u)))return bad(5);
    if(API_IS(name,"GetConsoleMode")){if(!output(a[1],4))return bad(87);Write32(a[1],h->kind==1?c.input_mode:c.screens[h->screen].mode);return 1;}
    if(API_IS(name,"SetConsoleMode")){if(a[1]&~(h->kind==1?31u:3u))return bad(87);(h->kind==1?c.input_mode:c.screens[h->screen].mode)=a[1];return 1;}
    if(h->kind==1){
        if(API_IS(name,"FlushConsoleInputBuffer")){c.input_count=0;c.pending_lf=false;return 1;}
        if(API_IS(name,"GetNumberOfConsoleInputEvents")){if(!output(a[1],4))return bad(87);Write32(a[1],c.input_count);return 1;}
        if(API_IS(name,"WriteConsoleInputA")){
            if(a[2]>256-c.input_count)return bad(8);
            if(!virtual_memory.Access(a[1],a[2]*20,false)||!output(a[3],4))return bad(87);
            Read(a[1],c.input[c.input_count],a[2]*20);c.input_count+=a[2];Write32(a[3],a[2]);return 1;
        }
        const bool peek=API_IS(name,"PeekConsoleInputA"),records=peek||API_IS(name,"ReadConsoleInputA");
        const bool text=API_IS(name,"ReadConsoleA")||API_IS(name,"ReadFile");
        if(!records&&!text)return bad(6);
        if(!output(a[3],4)||a[2]>0x100000||!output(a[1],a[2]*(records?20:1)))return bad(87);
        Write32(a[3],0);if(!a[2])return 1;
        unsigned done=0;
        for(;;){
            if(records){
                unsigned n=c.input_count<a[2]?c.input_count:a[2];
                if(n||peek){Store(a[1],c.input,n*20);if(!peek){memmove(c.input,c.input+n,(c.input_count-n)*20);c.input_count-=n;}Write32(a[3],n);return 1;}
            }else {
                if(c.pending_lf){const unsigned char lf=10;Store(a[1]+done++,&lf,1);c.pending_lf=false;Write32(a[3],done);return 1;}
                while(c.input_count&&done<a[2]){
                    auto *record=c.input[0];unsigned repeat=record[8]|unsigned(record[9])<<8;
                    const unsigned char ch=record[14];
                    if(record[0]!=1||!record[4]||!ch||!repeat){memmove(c.input,c.input+1,(--c.input_count)*20);continue;}
                    if(--repeat){record[8]=repeat;record[9]=repeat>>8;}
                    else memmove(c.input,c.input+1,(--c.input_count)*20);
                    if(ch==8&&(c.input_mode&2)){
                        if(done){--done;if(c.input_mode&4){auto &screen=c.screens[c.active];screen.Put(8);screen.Put(' ');screen.Put(8);}}continue;
                    }
                    Store(a[1]+done++,&ch,1);if(c.input_mode&4)c.screens[c.active].Put(ch);
                    if(ch==13&&(c.input_mode&2)){
                        const unsigned char lf=10;if(c.input_mode&4)c.screens[c.active].Put(lf);
                        if(done<a[2])Store(a[1]+done++,&lf,1);else c.pending_lf=true;
                        Write32(a[3],done);return 1;
                    }
                }
            }
            if(text&&done&&(!(c.input_mode&2)||done==a[2])){Write32(a[3],done);return 1;}
#ifdef NEO98_NO_UNICORN
            if (HostConsoleEnded(this) && !c.input_count) { Write32(a[3], done); return 1; }
#endif
            threads[current_thread].block=9;Suspend(Idle);
            if(state==Exited||state==Failed||console!=&c)return bad(995);
        }
    }
    auto &s=c.screens[h->screen];
    switch (name.id)
    {
    API_CASE("SetConsoleActiveScreenBuffer"){
        const unsigned previous=c.active;c.active=h->screen;bool used=false;
        for(const auto &other:c.handles)if(other.kind==2&&other.screen==previous)used=true;
        if(previous!=c.active&&!used){delete[] c.screens[previous].cells;c.screens[previous].cells=nullptr;}return 1;
    }
    API_CASE("GetLargestConsoleWindowSize")return 80|(25u<<16);
    API_CASE("GetConsoleScreenBufferInfo"){
        unsigned short info[]={static_cast<unsigned short>(s.width),static_cast<unsigned short>(s.height),static_cast<unsigned short>(s.x),static_cast<unsigned short>(s.y),static_cast<unsigned short>(s.attribute),static_cast<unsigned short>(s.window[0]),static_cast<unsigned short>(s.window[1]),static_cast<unsigned short>(s.window[2]),static_cast<unsigned short>(s.window[3]),80,25};
        if(!output(a[1],sizeof info))return bad(87);return Store(a[1],info,sizeof info);
    }
    API_CASE("GetConsoleCursorInfo"){unsigned data[]={s.cursor_size,s.cursor_visible?1u:0};if(!output(a[1],8))return bad(87);return Store(a[1],data,8);}
    API_CASE("SetConsoleCursorInfo"){if(!virtual_memory.Access(a[1],8,false))return bad(87);unsigned size=Read32(a[1]);if(!size||size>100)return bad(87);s.cursor_size=size;s.cursor_visible=Read32(a[1]+4)!=0;return 1;}
    API_CASE("SetConsoleTextAttribute"){s.attribute=a[1]&65535;return 1;}
    default:
        break;
    }
    const int x=static_cast<short>(a[1]),y=static_cast<short>(a[1]>>16);
    switch (name.id)
    {
    API_CASE("SetConsoleCursorPosition"){if(x<0||y<0||unsigned(x)>=s.width||unsigned(y)>=s.height)return bad(87);s.x=x;s.y=y;return 1;}
    API_CASE("SetConsoleScreenBufferSize"){if(x<=s.window[2]||y<=s.window[3])return bad(87);return s.Resize(x,y)?1:bad(87);}
    API_CASE("SetConsoleWindowInfo"){
        short rect[4];if(!virtual_memory.Access(a[2],8,false)||!Read(a[2],rect,8))return bad(87);
        if(!a[1])for(unsigned i=0;i<4;++i)rect[i]+=s.window[i];
        if(rect[0]<0||rect[1]<0||rect[2]<rect[0]||rect[3]<rect[1]||unsigned(rect[2])>=s.width||unsigned(rect[3])>=s.height)return bad(87);
        memcpy(s.window,rect,8);return 1;
    }
    API_CASE("WriteConsoleA") API_CASE("WriteFile"){
        if(a[2]>0x100000||!virtual_memory.Access(a[1],a[2],false)||(a[3]&&!output(a[3],4)))return bad(87);
        unsigned char buffer[1024];for(unsigned i=0;i<a[2];){unsigned n=a[2]-i;if(n>sizeof buffer)n=sizeof buffer;Read(a[1]+i,buffer,n);for(unsigned j=0;j<n;++j)s.Put(buffer[j]);
            if(log&&trace_calls){char line[2070];strcpy(line,"CONSOLE_WRITE data=");const char*hex="0123456789abcdef";unsigned offset=unsigned(strlen(line));for(unsigned j=0;j<n;++j){line[offset++]=hex[buffer[j]>>4];line[offset++]=hex[buffer[j]&15];}line[offset]=0;log(line);}
#ifdef NEO98_NO_UNICORN
            if(host_console_output) { InitializeNls(); host_console_output(buffer,n,a[0]==standard_handles[2],system_locale.oem); }
#endif
            i+=n;}
        if(a[3])Write32(a[3],a[2]);return 1;
    }
    API_CASE("ReadConsoleOutputA") API_CASE("WriteConsoleOutputA"){
        short rect[4];const int bw=static_cast<short>(a[2]),bh=static_cast<short>(a[2]>>16),bx=static_cast<short>(a[3]),by=static_cast<short>(a[3]>>16);
        if(bw<=0||bh<=0||bx<0||by<0||!output(a[4],8)||!Read(a[4],rect,8))return bad(87);
        if((unsigned long long)bw*bh>1048576||!virtual_memory.Access(a[1],unsigned(bw*bh)*4,reading))return bad(87);
        int left=rect[0]<0?0:rect[0],top=rect[1]<0?0:rect[1],right=rect[2],bottom=rect[3];
        if(right>=int(s.width))right=s.width-1;if(bottom>=int(s.height))bottom=s.height-1;
        if(right>rect[0]+bw-bx-1)right=rect[0]+bw-bx-1;if(bottom>rect[1]+bh-by-1)bottom=rect[1]+bh-by-1;
        for(int row=top;row<=bottom;++row)for(int col=left;col<=right;++col){unsigned p=a[1]+((by+row-rect[1])*bw+bx+col-rect[0])*4;auto &cell=s.cells[row*s.width+col];if(reading)Store(p,&cell,4);else Read(p,&cell,4);}
        short clipped[]={static_cast<short>(left),static_cast<short>(top),static_cast<short>(right),static_cast<short>(bottom)};Store(a[4],clipped,8);return 1;
    }
    API_CASE("ScrollConsoleScreenBufferA"){
        short src[4],clip[4]={0,0,static_cast<short>(s.width-1),static_cast<short>(s.height-1)};ConsoleState::Cell fill;
        if(!virtual_memory.Access(a[1],8,false)||!Read(a[1],src,8)||(a[2]&&(!virtual_memory.Access(a[2],8,false)||!Read(a[2],clip,8)))||!virtual_memory.Access(a[4],4,false)||!Read(a[4],&fill,4))return bad(87);
        const int dx=static_cast<short>(a[3])-src[0],dy=static_cast<short>(a[3]>>16)-src[1];
        auto *copy=new ConsoleState::Cell[s.width*s.height];if(!copy)return bad(8);memcpy(copy,s.cells,s.width*s.height*4);
        for(int row=0;row<int(s.height);++row)for(int col=0;col<int(s.width);++col){if(col<clip[0]||col>clip[2]||row<clip[1]||row>clip[3])continue;
            int ox=col-dx,oy=row-dy;bool target=ox>=src[0]&&ox<=src[2]&&oy>=src[1]&&oy<=src[3]&&ox>=0&&oy>=0&&ox<int(s.width)&&oy<int(s.height);
            if(target)s.cells[row*s.width+col]=copy[oy*s.width+ox];else if(col>=src[0]&&col<=src[2]&&row>=src[1]&&row<=src[3])s.cells[row*s.width+col]=fill;
        }delete[] copy;return 1;
    }
    default:
        break;
    }
    // Linear cell/attribute transfers share clipping and validation.
    const bool attribute=strstr(name,"Attribute")!=nullptr,fill=!strncmp(name,"Fill",4);
    if(!fill&&strncmp(name,"ReadConsoleOutput",17)&&strncmp(name,"WriteConsoleOutput",18))return bad(6);
    int cx=static_cast<short>(a[3]),cy=static_cast<short>(a[3]>>16);
    if(cx<0||cy<0||unsigned(cx)>=s.width||unsigned(cy)>=s.height||!output(a[4],4))return bad(87);
    unsigned start=cy*s.width+cx,n=a[2],available=s.width*s.height-start;if(n>available)n=available;
    if(!fill&&!virtual_memory.Access(a[1],n*(attribute?2:1),reading))return bad(87);
    for(unsigned i=0;i<n;++i){auto &cell=s.cells[start+i];unsigned short value=static_cast<unsigned short>(a[1]);
        if(reading){value=attribute?cell.attribute:cell.character;Store(a[1]+i*(attribute?2:1),&value,attribute?2:1);}
        else{if(!fill){value=0;Read(a[1]+i*(attribute?2:1),&value,attribute?2:1);}if(attribute)cell.attribute=value;else cell.character=value&255;}
    }Write32(a[4],n);return 1;
}

void Application::ConsoleKey(unsigned key,unsigned character,bool down,unsigned scan,bool extended){
    if(!console||console->input_count==256||state==Exited||state==Failed)return;
    unsigned char record[20]={};record[0]=1;record[4]=down;record[8]=1;
    record[10]=key;record[11]=key>>8;record[12]=scan;record[13]=scan>>8;
    if(!character){if(key==13)character=13;else if(key==8)character=8;else if(key==9)character=9;}
    if(character<256)record[14]=character;
    unsigned modifiers=(extended?256u:0u);
    if(User::PhysicalKeyDown(0xa5))modifiers|=1;
    if(User::PhysicalKeyDown(0xa4))modifiers|=2;
    if(User::PhysicalKeyDown(0xa3))modifiers|=4;
    if(User::PhysicalKeyDown(0xa2))modifiers|=8;
    if(User::PhysicalKeyDown(0xa0)||User::PhysicalKeyDown(0xa1))modifiers|=16;
    memcpy(record+16,&modifiers,4);
    memcpy(console->input[console->input_count++],record,20);
}
unsigned long long Application::ConsoleFrameHash() const { return console ? console->frame_hash : 0; }
const unsigned *Application::ConsolePixels(){
    // A display in low power or off shows black until the next input.
    if(User::DisplayPower()){static unsigned *black=nullptr;static unsigned held=0;int w=0,h=0;User::PixelsSize(w,h);const unsigned need=unsigned(w)*unsigned(h)>1024u*768u?unsigned(w)*unsigned(h):1024u*768u;if(need>held){delete[] black;black=new unsigned[need]{};held=need;}return black;} // any host frame size (User::PixelsSize)
    // A console executable may also own a GUI (NFS II uses DirectDraw).
    // Follow the same focus rule as RawKey, and let an exclusive display
    // take over even before its window has received keyboard focus.
    if(!console || User::WindowProcess(User::GetFocus()) || User::ScreenModeActive() || User::ExclusiveMode())return nullptr;
    // Console executables can also open GUI windows (for example SDL 1.2).
    // Let USER display the visible foreground window instead of covering it
    // with the console's text buffer; retain the text view when it closes.
    const auto foreground=User::GetForegroundWindow();
    if(foreground&&foreground!=User::GetDesktopWindow()&&User::IsVisible(foreground))return nullptr;
    auto &c=*console;auto &s=c.screens[c.active];if(!s.cells)return nullptr;
    // Hash just the visible cells. Text rendering only runs after a change.
    const unsigned cols=(s.window[2]-s.window[0]+1)<100?unsigned(s.window[2]-s.window[0]+1):100;
    const unsigned rows=(s.window[3]-s.window[1]+1)<37?unsigned(s.window[3]-s.window[1]+1):37;
    unsigned long long hash=1469598103934665603ull;
    const auto mix=[&](unsigned value){hash=(hash^value)*1099511628211ull;};
    mix(cols);mix(rows);mix(s.x);mix(s.y);mix(s.cursor_size);mix(s.cursor_visible);
    mix(s.window[0]);mix(s.window[1]);
    for(unsigned row=0;row<rows;++row)for(unsigned col=0;col<cols;++col){
        auto cell=s.cells[(row+s.window[1])*s.width+col+s.window[0]];mix(cell.character|(unsigned(cell.attribute)<<16));}
    if(c.pixels&&hash==c.frame_hash)return c.pixels;
    if(!c.pixels)c.pixels=new unsigned[800*600];if(!c.pixels)return nullptr;
    memset(c.pixels,0,800*600*4);
    DibEng::Bitmap bitmap{reinterpret_cast<unsigned char*>(c.pixels),800,600,3200,32};
    auto dc=Gdi::CreateSurfaceDC(bitmap);if(!dc)return nullptr;
    Gdi::SelectObject(dc,fixed);Gdi::SetBkMode(dc,2);
    // Console attributes encode BGR components, while COLORREF uses RGB bytes.
    const auto color=[](unsigned v){
        static const unsigned colors[]={0,0x800000,0x008000,0x808000,0x000080,0x800080,0x008080,0xc0c0c0,
                                        0x808080,0xff0000,0x00ff00,0xffff00,0x0000ff,0xff00ff,0x00ffff,0xffffff};
        return colors[v&15];};
    for(unsigned row=0;row<rows;++row)for(unsigned col=0;col<cols;++col){
        const auto cell=s.cells[(row+s.window[1])*s.width+col+s.window[0]];
        Gdi::SetTextColor(dc,color(cell.attribute&15));Gdi::SetBkColor(dc,color((cell.attribute>>4)&15));
        Gdi::RECT rect{int(col*8),int(row*16),int(col*8+8),int(row*16+16)};
        char ch=char(cell.character);Gdi::ExtTextOutA(dc,rect.left,rect.top,6,&rect,&ch,1,nullptr);
    }
    if(s.cursor_visible&&s.x>=unsigned(s.window[0])&&s.y>=unsigned(s.window[1])&&
       s.x<unsigned(s.window[0])+cols&&s.y<unsigned(s.window[1])+rows){
        int x=(s.x-s.window[0])*8,y=(s.y-s.window[1])*16;
        int height=(s.cursor_size*16+99)/100;
        Gdi::RECT rect{x,y+16-height,x+8,y+16};Gdi::InvertRect(dc,&rect);
    }
    Gdi::DeleteDC(dc);c.frame_hash=hash;return c.pixels;
}
