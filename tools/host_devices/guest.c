/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "guest_win32.h"
#include "../../src/host_devices_abi.h"

/* Tray icons, and the views the host asks for with message 402h. */
enum { TRAY_POWER=1, TRAY_WIFI=2, TRAY_EJECT=3 };
static void close_key(void);
static void close_hidden(void);
enum { SHOW_DEVICES=0, SHOW_POWER=1, SHOW_WIFI=2, SHOW_CAMERA=3, SHOW_CONTROLLERS=4 };
enum { ROW_NETWORK=40, ROW_MEDIUM=22 };

static H instance,window,wifi,key,eject,battery,bridge,bold;
static H power_text,wifi_state,wifi_message,network_list,key_text,key_entry,key_confirm,key_save;
static H media_list,media_info,eject_message,eject_picture;
static AH_Status status;
static TRAY trays[3];
static H icons[3],network_icon,medium_icon,cdrom_icon,eject_icon;
static U icon_keys[3]={~0u,~0u,~0u},added[3],taskbar_created;
static U shown_generation=~0u,native_power,native_power_checked,camera_registered;
static AH_Network networks[AH_MAX_NETWORKS];static U network_count;
static AH_Medium media[AH_MAX_MEDIA];static U media_count;
/* Media this program has already tried to attach. A medium that leaves the
   host list is forgotten, so plugging it in again attaches it again. */
static U handled[AH_MAX_MEDIA];static U handled_count;
static U key_network,stopping,asked_network,asked_generation;
static void ask_key(AH_Network *n);
static char stopped_name[200];
/* The rows a list box shows: host ids, or one id 0 for the empty-list text. */
typedef struct { U ids[AH_MAX_NETWORKS]; U count; } ROWS;
static ROWS network_rows,media_rows;
static H notice,notice_icon,wifi_status,hidden,adapter_box,adapter_label,status_values[6];
static H hidden_name,hidden_secure,hidden_key,hidden_confirm,hidden_save;
static char notice_title[96],notice_text[256];
/* Media present when the program started; only later arrivals open a window. */
static U initial_media[AH_MAX_MEDIA],initial_count,initial_taken,autoplay_id;
static U was_connected=~0u,connected_since,shown_adapters=~0u;
static char converted[1024];

void *memset(void *p,I c,unsigned n){unsigned char *b=p;for(U i=0;i<n;++i)b[i]=(unsigned char)c;return p;}
void *memcpy(void *d,const void *s,unsigned n){for(U i=0;i<n;++i)((unsigned char*)d)[i]=((const unsigned char*)s)[i];return d;}
static U len(S s){U n=0;while(s[n])++n;return n;}
static void cat(char *d,S s){U n=len(d);while(*s)d[n++]=*s++;d[n]=0;}
static void copy(char *d,S s,U capacity){U i=0;for(;i+1<capacity&&s[i];++i)d[i]=s[i];d[i]=0;}
static void number(char *s,U n){char b[16];U k=0;do{b[k++]=(char)('0'+n%10);n/=10;}while(n);U p=len(s);while(k)s[p++]=b[--k];s[p]=0;}
static void two_digits(char *s,U n){char d[3]={(char)('0'+n/10%10),(char)('0'+n%10),0};cat(s,d);}
static S ansi(S text){unsigned short wide[512];I n=MultiByteToWideChar(65001,0,text,-1,wide,512);if(n>0&&WideCharToMultiByte(0,0,wide,-1,converted,sizeof converted,0,0))return converted;return text;}
static void wipe(volatile char *p,U n){for(U i=0;i<n;++i)p[i]=0;}
/* Only touch a control whose text changes, so the one-second refresh does not flicker. */
static void set_text(H h,S text)
{
    char now[512];GetWindowTextA(h,now,sizeof now);
    U i=0;while(i<sizeof now-1&&now[i]&&now[i]==text[i])++i;
    if(now[i]!=text[i]||(i==sizeof now-1&&text[i]))SetWindowTextA(h,text);
}

static H control(H parent,S kind,S title,U style,U extended,I x,I y,I width,I height,U id)
{
    H h=CreateWindowExA(extended,kind,title,0x50000000u|style,x,y,width,height,parent,(H)id,instance,0);
    SendMessageA(h,0x30,(U)GetStockObject(17),1);return h;
}
static H label(H parent,S title,I x,I y,I width,I height,U id){return control(parent,"STATIC",title,0,0,x,y,width,height,id);}
static void button(H parent,S title,I x,I y,I width,U id,U style){control(parent,"BUTTON",title,0x10000|style,0,x,y,width,23,id);}
static H list(H parent,I x,I y,I width,I height,U id,U row)
{
    /* LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT, sunken. */
    H h=control(parent,"LISTBOX","",0x10000|0x200000|0x151,0x200,x,y,width,height,id);
    SendMessageA(h,0x1a0,0,(I)row);return h;
}
/* A fixed dialog frame centred on the screen around a client area of the given size. */
static H frame_window(S klass,S title,I width,I height,H owner)
{
    const U style=0x00c80000u; RECT r={0,0,width,height};AdjustWindowRectEx(&r,style,0,0);
    I w=r.right-r.left,h=r.bottom-r.top,x=(GetSystemMetrics(0)-w)/2,y=(GetSystemMetrics(1)-h)/2;
    return CreateWindowExA(0,klass,title,style,x<0?0:x,y<0?0:y,w,h,owner,0,instance,0);
}
static U selected(H box,U count){I n=SendMessageA(box,0x188,0,0);return n>=0&&(U)n<count?(U)n:~0u;}
static void show(H h){ShowWindow(h,5);SwitchToThisWindow(h,1);}
static void error(U code)
{
    char text[96]="Windows could not complete the request. Error ";number(text,code);cat(text,".");
    H owner=IsWindowVisible(key)?key:IsWindowVisible(wifi)?wifi:IsWindowVisible(eject)?eject:0;
    MessageBoxA(owner,text,"NEO98 Hardware",0x10);
}
static U wifi_action(U what){return (what>=AH_SCAN&&what<=AH_FORGET)||what==AH_CONNECT_HIDDEN||what==AH_SELECT_ADAPTER;}
static U media_action(U what){return what>=AH_ATTACH&&what<=AH_EJECT;}
/* Each window shows the progress and the result of its own requests only. */
static S message_for(U wifi_side)
{
    const U what=status.last_action;
    if(wifi_side?!wifi_action(what):!media_action(what))return "";
    if(status.busy)return "Please wait...";
    if(!status.error&&!status.message[0])return "";
    S text=ansi(status.message);
    if(!status.error&&text[0]=='D'&&text[1]=='o'&&text[2]=='n'&&text[3]=='e'&&text[4]=='.'&&!text[5])return "";
    return text;
}
static U wifi_action(U what);
static U media_action(U what);
static I send(AH_Request *r)
{
    U done=0;I okay=DeviceIoControl(bridge,AH_IOCTL_ACTION,r,sizeof *r,0,0,&done,0);
    wipe(r->password,sizeof r->password);
    if(!okay){error(GetLastError());return 0;}
    if(wifi_action(r->action))set_text(wifi_message,"Please wait...");
    if(media_action(r->action))set_text(eject_message,"Please wait...");
    return 1;
}
static I action(U what,U id,U flags)
{
    AH_Request r;memset(&r,0,sizeof r);r.version=AH_VERSION;r.action=what;r.id=id;r.flags=flags;
    if(what==AH_CONNECT)
    {
        char entered[128];unsigned short wide[128];GetWindowTextA(key_entry,entered,sizeof entered);
        I n=MultiByteToWideChar(0,0,entered,-1,wide,128);
        I okay=n&&WideCharToMultiByte(65001,0,wide,-1,r.password,sizeof r.password,0,0);
        wipe(entered,sizeof entered);wipe((volatile char*)wide,sizeof wide);
        SetWindowTextA(key_entry,"");SetWindowTextA(key_confirm,"");
        if(!okay){error(87);return 0;}
    }
    return send(&r);
}

/* 16x16 pictures, one character per pixel. */
static U colour(char c)
{
    switch(c)
    {
    case 'K':return 0x000000;case 'G':return 0x808080;case 'L':return 0xc0c0c0;case 'W':return 0xffffff;
    case 'T':return 0x008080;case 'C':return 0x00ffff;case 'R':return 0xe00000;case 'B':return 0x000080;
    case 'g':return 0x00b000;case 'N':return 0x006000;case 'Y':return 0xffff00;case 'D':return 0x404040;
    }
    return 0;
}
/* A 16x16 picture: colours 0xRRGGBB and an AND mask, as CreateIcon takes them. */
typedef struct { U pixels[256]; unsigned char mask[32]; } ART;
static void art_clear(ART *a){memset(a->pixels,0,sizeof a->pixels);memset(a->mask,255,sizeof a->mask);}
static void art_set(ART *a,U x,U y,U rgb){a->mask[y*2+x/8]&=(unsigned char)~(0x80>>(x%8));a->pixels[y*16+x]=rgb;}
static void art_erase(ART *a,U x,U y){a->mask[y*2+x/8]|=(unsigned char)(0x80>>(x%8));a->pixels[y*16+x]=0;}
static U art_opaque(const ART *a,U x,U y){return !(a->mask[y*2+x/8]&(0x80>>(x%8)));}
static H art_icon(const ART *a){return CreateIcon(instance,16,16,1,32,a->mask,(const unsigned char*)a->pixels);}
#include "eject_icons.h"
/* The 16x16 image of an icon group in an original Windows file, read from
   its DIB. The file is mapped as data only; nothing in it runs. */
static U original_art(S file,U group,ART *out)
{
    H module=LoadLibraryExA(file,0,2);if(!module)return 0;
    U okay=0;H found=FindResourceA(module,(S)group,(S)14);
    const unsigned char *directory=found?LockResource(LoadResource(module,found)):0;
    I id=directory?LookupIconIdFromDirectoryEx(directory,1,16,16,0):0;
    H image=id?FindResourceA(module,(S)id,(S)3):0;
    const unsigned char *dib=image?LockResource(LoadResource(module,image)):0;
    U bytes=image?SizeofResource(module,image):0;
    if(dib&&bytes>=40)
    {
        const U header=*(const U*)dib;const I width=*(const I*)(dib+4),height=*(const I*)(dib+8)/2;
        const U bits=*(const unsigned short*)(dib+14),used=*(const U*)(dib+32);
        const U colours=bits<=8?(used?used:1u<<bits):0;
        const U stride=((U)width*bits+31)/32*4,mask_stride=((U)width+31)/32*4;
        const unsigned char *palette=dib+header,*xor_bits=palette+colours*4,*and_bits=xor_bits+stride*(U)height;
        if(width==16&&height==16&&(bits==1||bits==4||bits==8||bits==24||bits==32)&&and_bits+mask_stride*16<=dib+bytes)
        {
            art_clear(out);
            for(U y=0;y<16;++y)for(U x=0;x<16;++x)
            {
                const U row=15-y;if(and_bits[row*mask_stride+x/8]&(0x80>>(x%8)))continue;
                const unsigned char *line=xor_bits+row*stride;U rgb;
                if(bits>8){const unsigned char *p=line+x*(bits/8);rgb=(U)p[2]<<16|(U)p[1]<<8|p[0];}
                else
                {
                    U index=bits==8?line[x]:bits==4?(line[x/2]>>(x&1?0:4))&15:(line[x/8]>>(7-x%8))&1;
                    const unsigned char *q=palette+index*4;rgb=(U)q[2]<<16|(U)q[1]<<8|q[0];
                }
                art_set(out,x,y,rgb);
            }
            okay=1;
        }
    }
    FreeLibrary(module);return okay;
}
static H original_icon(S file,U group){ART a;return original_art(file,group,&a)?art_icon(&a):0;}
static H picture(const char rows[16][17])
{
    U pixels[256];unsigned char mask[32];memset(pixels,0,sizeof pixels);memset(mask,255,sizeof mask);
    for(U y=0;y<16;++y)for(U x=0;x<16;++x)if(rows[y][x]!='.')
    {mask[y*2+x/8]&=(unsigned char)~(0x80>>(x%8));pixels[y*16+x]=colour(rows[y][x]);}
    return CreateIcon(instance,16,16,1,32,mask,(const unsigned char*)pixels);
}
static H picture_from(const S rows[16]){char copy_rows[16][17];for(U y=0;y<16;++y)copy(copy_rows[y],rows[y],17);return picture(copy_rows);}
static const S tower[16]={
    "................","..g..........g..",".g..g......g..g.","g..g...DD...g..g",
    "g..g..DLLD..g..g","g..g..DLLD..g..g",".g..g..DD..g..g.","..g....DL....g..",
    ".......DL.......","......DLLD......","......D..D......",".....DL..LD.....",
    ".....D....D.....","....DL....LD....","....DDDDDDDD....","................"};
static const S stick[16]={
    "................",".....N..........","....ggN.........","...ggggN........",
    "..ggggggN.......","....ggN.........","....ggN.........","KKKKKKKKKK......",
    "KLWWWWWWLKKKKKK.","KLLLLLLLLKWLLLK.","KLLBBLLLLKWKLKK.","KLLLLLLLLKWLLLK.",
    "KLGGGGGGGKKKKKK.","KKKKKKKKKK......","................","................"};
static const S disc[16]={
    "................",".....KKKKK......","...KKWWWWLKK....","..KWWWWWLLLGK...",
    "..KWWWWLLLLGK...",".KWWWWKKKLLLGK..",".KWWWKLLLKLLGK..",".KWWWKL.LKLGGK..",
    ".KLLLKLLLKGGGK..",".KLLLLKKKGGGGK..","..KLLLLGGGGGK...","..KLLLGGGGGGK...",
    "...KKGGGGGKK....",".....KKKKK......","................","................"};
static const S monitor[16]={
    "................","................","................","................",
    "................","KKKKKKKK........","KWWWWWGK........","KWTTTTGK........",
    "KWTCTTGK........","KWTTTTGK........","KWTTTTGK........","KGGGGGGK........",
    "KKKKKKKK........","...KGK..........",".KKKKKKK........",".KLLLLLK........"};
/* The tray picture is Dial-Up Networking's connection icon from RNAAPP.EXE:
   110 two computers, 114 the same with a red cross. Four signal bars take
   the place of its cable in the free upper right corner. */
static H wifi_picture(U bars,U cross)
{
    ART art;
    if(original_art("C:\\WINDOWS\\SYSTEM\\RNAAPP.EXE",cross?114:110,&art))
    {
        for(U y=0;y<5;++y)for(U x=9;x<16;++x)if(art_opaque(&art,x,y))art_erase(&art,x,y);
        for(U i=0;i<4;++i)for(U y=3-i;y<5;++y)art_set(&art,9+i*2,y,i<bars?0x00b000:0x808080);
        return art_icon(&art);
    }
    char rows[16][17];for(U y=0;y<16;++y)copy(rows[y],monitor[y],17);
    for(U i=0;i<4;++i){U x=9+i*2,top=10-i*3;for(U y=top;y<16;++y)rows[y][x]=i<bars?'g':'G';}
    if(cross)
    {
        static const unsigned char marks[][2]={{8,0},{9,0},{12,0},{13,0},{9,1},{10,1},{11,1},{12,1},{10,2},{11,2},{9,3},{10,3},{11,3},{12,3},{8,4},{9,4},{12,4},{13,4}};
        for(U i=0;i<sizeof marks/sizeof marks[0];++i)rows[marks[i][1]][marks[i][0]]='R';
    }
    return picture(rows);
}
static U bars_for(U signal){return signal>=75?4:signal>=50?3:signal>=25?2:signal?1:0;}
/* The network list shows five bars, as Windows XP does. */
static U meter_for(U signal){U n=(signal+19)/20;return n>5?5:n;}
static void tray_icon(U which,U key_value,U wanted)
{
    if(!wanted){if(added[which])Shell_NotifyIconA(2,&trays[which]);added[which]=0;return;}
    if(icon_keys[which]!=key_value)
    {
        H next=0;
        if(which==TRAY_WIFI-1)next=wifi_picture(key_value&7,(key_value>>3)&1);
        else if(which==TRAY_EJECT-1)next=art_icon(&eject_tray_art);
        else
        {
            /* The fallback battery: a level bar, red when low, with a bolt on mains power. */
            char rows[16][17];for(U y=0;y<16;++y)copy(rows[y],"................",17);
            U percent=status.power.percent<=100?status.power.percent:0,fill=percent*11/100;
            for(U x=1;x<14;++x){rows[4][x]='D';rows[12][x]='D';}
            for(U y=4;y<13;++y){rows[y][1]='D';rows[y][13]='D';}
            for(U y=6;y<11;++y)rows[y][14]='D';
            for(U y=5;y<12;++y)for(U x=2;x<13;++x)rows[y][x]=x-2<fill?(status.power.flags&4?'R':'g'):'L';
            if(status.power.ac==1)for(U y=1;y<10;++y)rows[y][8-y/3]='Y';
            next=picture(rows);
        }
        if(next){H old=icons[which];icons[which]=next;trays[which].icon=next;icon_keys[which]=key_value;if(old)DestroyIcon(old);}
    }
    if(icons[which])added[which]=Shell_NotifyIconA(added[which]?1:0,&trays[which]);
}
static void tip(U which,S text)
{memset(trays[which].tip,0,sizeof trays[which].tip);for(U i=0;i<63&&text[i];++i)trays[which].tip[i]=text[i];}

static void security_text(char *text,U flags)
{
    if(!(flags&AH_NET_SECURE)){cat(text,"Unsecured wireless network");return;}
    cat(text,"Security-enabled wireless network");
    cat(text,flags&AH_NET_WEP?" (WEP)":flags&AH_NET_ENTERPRISE?" (802.1X)":flags&AH_NET_SAE?" (WPA3)":" (WPA)");
}
static void medium_text(char *text,const AH_Medium *m)
{
    text[0]=0;cat(text,ansi(m->name));
    if(m->letter){char drive[8]=" (X:)";drive[2]=(char)m->letter;cat(text,drive);}
}
static AH_Network *chosen_network(void){U n=selected(network_list,network_count);return n==~0u?0:&networks[n];}
static AH_Medium *chosen_medium(void){U n=selected(media_list,media_count);return n==~0u?0:&media[n];}
static void update_buttons(void)
{
    const U idle=!status.busy;AH_Network *n=chosen_network();AH_Medium *m=chosen_medium();
    const U wifi_ready=idle&&(status.features&AH_WIFI);
    EnableWindow(GetDlgItem(wifi,101),wifi_ready&&status.wifi_on);
    EnableWindow(GetDlgItem(wifi,102),wifi_ready);
    set_text(GetDlgItem(wifi,102),status.wifi_on?"Turn &Off Wireless":"Turn &On Wireless");
    EnableWindow(GetDlgItem(wifi,103),wifi_ready&&n!=0);
    set_text(GetDlgItem(wifi,103),n&&(n->flags&AH_NET_ACTIVE)?"&Disconnect":"&Connect");
    EnableWindow(GetDlgItem(wifi,105),wifi_ready&&n&&(n->flags&AH_NET_SAVED));
    EnableWindow(GetDlgItem(eject,203),idle&&(status.features&AH_MEDIA)&&m!=0);
    set_text(GetDlgItem(eject,203),m&&!m->letter?"&Start":"&Stop");
    char text[320]="";
    if(m)
    {
        cat(text,m->flags&AH_MEDIA_OPTICAL?"CD-ROM drive":m->flags&AH_MEDIA_FLOPPY?"Floppy disk drive":"USB mass storage device");
        if(m->letter){char drive[16]=", drive X:";drive[8]=(char)m->letter;cat(text,drive);cat(text,m->flags&AH_MEDIA_READONLY?", read-only":"");}
        else cat(text,", not in use by Windows");
    }
    set_text(media_info,text);
}
/* Keep the selection on the same item across a new host list. */
static void fill(H box,ROWS *rows,U count,const U *ids)
{
    U same=count==rows->count;for(U i=0;same&&i<count;++i)if(ids[i]!=rows->ids[i])same=0;
    if(same){InvalidateRect(box,0,0);return;}
    I at=SendMessageA(box,0x188,0,0);U chosen=at>=0&&(U)at<rows->count?rows->ids[at]:0;
    SendMessageA(box,0x184,0,0);
    for(U i=0;i<count;++i){SendMessageA(box,0x180,0,(I)"");SendMessageA(box,0x19a,i,(I)ids[i]);if(chosen&&ids[i]==chosen)SendMessageA(box,0x186,i,0);}
    if(!chosen&&count&&ids[0])SendMessageA(box,0x186,0,0);
    rows->count=count;for(U i=0;i<count;++i)rows->ids[i]=ids[i];
}
static void refresh_lists(void)
{
    U ids[AH_MAX_NETWORKS];
    /* The connected network first, the rest by signal strength. */
    network_count=status.wifi_on?status.networks:0;
    for(U i=0;i<network_count;++i)
    {
        AH_Network n=status.network[i];U j=i;
        while(j&&((n.flags&AH_NET_ACTIVE)>(networks[j-1].flags&AH_NET_ACTIVE)||((n.flags&AH_NET_ACTIVE)==(networks[j-1].flags&AH_NET_ACTIVE)&&n.signal>networks[j-1].signal)))
        {networks[j]=networks[j-1];--j;}
        networks[j]=n;
    }
    for(U i=0;i<network_count;++i)ids[i]=networks[i].id;
    ids[0]=network_count?ids[0]:0;fill(network_list,&network_rows,network_count?network_count:1,ids);
    U media_ids[AH_MAX_MEDIA];media_count=status.media;
    for(U i=0;i<media_count;++i){media[i]=status.medium[i];media_ids[i]=media[i].id;}
    media_ids[0]=media_count?media_ids[0]:0;fill(media_list,&media_rows,media_count?media_count:1,media_ids);
}
/* Windows attaches a newly arrived medium as a drive, as the host desktop does. */
static void attach_new_media(void)
{
    U kept=0;
    for(U i=0;i<handled_count;++i){U present=0;for(U j=0;j<status.media;++j)if(status.medium[j].id==handled[i])present=1;if(present)handled[kept++]=handled[i];}
    handled_count=kept;
    if(status.busy||!(status.features&AH_MEDIA))return;
    for(U j=0;j<status.media;++j)
    {
        U known=0;for(U i=0;i<handled_count;++i)if(handled[i]==status.medium[j].id)known=1;
        if(known)continue;
        if(handled_count<AH_MAX_MEDIA)handled[handled_count++]=status.medium[j].id;
        if(status.medium[j].letter)continue;
        AH_Request r;memset(&r,0,sizeof r);r.version=AH_VERSION;r.action=AH_ATTACH;r.id=status.medium[j].id;r.flags=1;
        U done=0;if(!DeviceIoControl(bridge,AH_IOCTL_ACTION,&r,sizeof r,0,0,&done,0))return;
        status.busy=1;
        U initial=0;for(U i=0;i<initial_count;++i)if(initial_media[i]==r.id)initial=1;
        if(!initial)autoplay_id=r.id;
        return;
    }
}
static void draw_bars(H dc,RECT *r,U bars);
/* A note above the notification area, in the tooltip colours. Windows 98
   has no balloons; this takes the place of the ones Windows XP showed. */
static void show_notice(H icon,S title,S text)
{
    copy(notice_title,title,sizeof notice_title);copy(notice_text,text,sizeof notice_text);notice_icon=icon;
    const I width=340,height=64;RECT area={0,0,GetSystemMetrics(0),GetSystemMetrics(1)};
    H tray=FindWindowA("Shell_TrayWnd",0);if(tray)GetWindowRect(tray,&area);
    I x=area.right-width-6,y=(tray?area.top:area.bottom)-height-6;if(x<0)x=0;if(y<0)y=0;
    SetWindowPos(notice,(H)~0u,x,y,width,height,0x10|0x40);InvalidateRect(notice,0,1);SetTimer(notice,3,8000,0);
}
static void paint_notice(H hwnd)
{
    PAINT p;H dc=BeginPaint(hwnd,&p);RECT r;GetClientRect(hwnd,&r);
    H brush=CreateSolidBrush(GetSysColor(24));FillRect(dc,&r,brush);DeleteObject(brush);
    SetBkMode(dc,1);SetTextColor(dc,GetSysColor(23));
    if(notice_icon)DrawIconEx(dc,8,8,notice_icon,16,16,0,0,3);
    H old=SelectObject(dc,bold);RECT title={32,7,r.right-8,23};DrawTextA(dc,notice_title,-1,&title,0x8820);
    SelectObject(dc,GetStockObject(17));RECT body={32,26,r.right-8,r.bottom-4};DrawTextA(dc,notice_text,-1,&body,0x810);
    SelectObject(dc,old);EndPaint(hwnd,&p);
}
static S signal_text(U signal){return signal>=80?"Excellent":signal>=60?"Very Good":signal>=40?"Good":signal>=20?"Low":"Very Low";}
static U active_signal(void){for(U i=0;i<status.networks;++i)if(status.network[i].flags&AH_NET_ACTIVE)return status.network[i].signal;return 0;}
static S empty_text(U id)
{
    if(id==14)return "There are no removable devices attached.";
    if(!(status.features&AH_WIFI))return "Windows cannot find a wireless network adapter.";
    if(!status.wifi_on)return "Wireless networking is turned off.";
    if(status.busy&&status.last_action==AH_SCAN)return "Searching for wireless networks...";
    return "No wireless networks were found in range.";
}
static void update_status_window(void)
{
    char text[64];
    set_text(status_values[0],!status.wifi_on?"Disabled":status.wifi_connected?"Connected":"Not connected");
    set_text(status_values[1],status.wifi_connected?ansi(status.wifi_name):"");
    text[0]=0;
    if(status.wifi_connected)
    {
        U seconds=(GetTickCount()-connected_since)/1000;
        two_digits(text,seconds/3600%100);cat(text,":");two_digits(text,seconds/60%60);cat(text,":");two_digits(text,seconds%60);
    }
    set_text(status_values[2],text);
    text[0]=0;
    if(status.wifi_connected&&status.wifi_bitrate){number(text,status.wifi_bitrate/1000);cat(text,".");number(text,status.wifi_bitrate/100%10);cat(text," Mbps");}
    else if(status.wifi_connected)cat(text,"Unknown");
    set_text(status_values[3],text);
    set_text(status_values[4],status.wifi_connected?signal_text(active_signal()):"");
    text[0]=0;
    if(status.wifi_address)for(U i=0;i<4;++i){if(i)cat(text,".");number(text,status.wifi_address>>(i*8)&255);}
    else if(status.wifi_connected)cat(text,"Not available");
    set_text(status_values[5],text);
    EnableWindow(GetDlgItem(wifi_status,104),!status.busy&&status.wifi_connected);
    RECT bars={215,95,255,118};InvalidateRect(wifi_status,&bars,1);
}
static void paint_status(H hwnd)
{
    PAINT p;H dc=BeginPaint(hwnd,&p);
    if(status.wifi_connected){RECT meter={0,0,250,114};draw_bars(dc,&meter,meter_for(active_signal()));}
    EndPaint(hwnd,&p);
}
static void update_adapters(void)
{
    U key_value=status.adapters|(status.adapter<<8);
    for(U i=0;i<status.adapters&&i<AH_MAX_ADAPTERS;++i)for(U j=0;status.adapter_name[i][j]&&j<64;++j)key_value=key_value*31+(unsigned char)status.adapter_name[i][j];
    if(key_value==shown_adapters)return;shown_adapters=key_value;
    SendMessageA(adapter_box,0x14b,0,0);
    for(U i=0;i<status.adapters&&i<AH_MAX_ADAPTERS;++i)SendMessageA(adapter_box,0x143,0,(I)ansi(status.adapter_name[i]));
    SendMessageA(adapter_box,0x14e,status.adapter,0);
    ShowWindow(adapter_box,status.adapters>1?5:0);ShowWindow(adapter_label,status.adapters>1?5:0);
}
static void refresh(void)
{
    if(!native_power_checked&&FindWindowA("SystemTray_Main",0))
    {
        native_power_checked=1;H module=LoadLibraryA("BATMETER.DLL");
        if(module)
        {
            typedef I (__stdcall *CAPS)(void*);CAPS capabilities=(CAPS)GetProcAddress(module,"BatMeterCapabilities");
            if(capabilities&&capabilities(0)){native_power=1;tray_icon(TRAY_POWER-1,0,0);}
            FreeLibrary(module);
        }
    }
    U done=0;
    if(!DeviceIoControl(bridge,AH_IOCTL_STATUS,0,0,&status,sizeof status,&done,0)||done!=sizeof status||status.version!=AH_VERSION)return;
    if(status.networks>AH_MAX_NETWORKS||status.media>AH_MAX_MEDIA||status.devices>AH_MAX_DEVICES||status.joysticks>AH_MAX_JOYS||status.adapters>AH_MAX_ADAPTERS)return;
    /* Video for Windows finds the host camera as the capture driver
       NEO98CAP.DRV, unless SYSTEM.INI already names another one: the line
       an INF of a capture card writes (VIDCAP.INF), once the host has found
       its camera. */
    if(status.camera_id&&!camera_registered)
    {
        char driver[64];GetPrivateProfileStringA("drivers","msvideo","",driver,sizeof driver,"system.ini");
        if(!driver[0])WritePrivateProfileStringA("drivers","msvideo","neo98cap.drv","system.ini");
        camera_registered=1;
    }
    if(!initial_taken){initial_taken=1;for(U i=0;i<status.media;++i)initial_media[initial_count++]=status.medium[i].id;}
    AH_Power power;if(GetSystemPowerStatus(&power))status.power=power;else power=status.power;
    char text[512]="";
    if(power.flags==128)cat(text,"No battery");
    else if(power.percent==255)cat(text,"Battery level unknown");
    else{number(text,power.percent);cat(text,"% remaining");}
    char line[200]="";copy(line,text,sizeof line);
    cat(text,power.ac==1?(power.flags!=255&&(power.flags&8)?"\r\nPlugged in, charging":"\r\nPlugged in"):power.ac==0?"\r\nRunning on batteries":"");
    if(power.seconds!=~0u)
    {
        char time[32]="";number(time,power.seconds/3600);cat(time,":");two_digits(time,power.seconds/60%60);
        cat(text,"\r\nTime remaining: ");cat(text,time);cat(line," (");cat(line,time);cat(line,")");
    }
    else if(power.ac==1)cat(line,", plugged in");
    /* A tray tip is one line. */
    set_text(power_text,text);tip(TRAY_POWER-1,line);
    tray_icon(TRAY_POWER-1,power.ac|(power.flags<<8)|(power.percent<<16),!native_power&&power.flags!=128&&power.flags!=255);

    U strength=0;for(U i=0;i<status.networks;++i)if(status.network[i].flags&AH_NET_ACTIVE)strength=status.network[i].signal;
    text[0]=0;
    if(!(status.features&AH_WIFI))cat(text,"Wireless networking is not available");
    else if(!status.wifi_on)cat(text,"Wireless networking is turned off");
    else if(status.wifi_connected){cat(text,"Connected to ");cat(text,ansi(status.wifi_name));}
    else cat(text,"Not connected");
    set_text(wifi_state,text);
    char tray_tip[200]="Wireless Network Connection: ";cat(tray_tip,text);tip(TRAY_WIFI-1,tray_tip);
    U bars=status.wifi_connected?bars_for(strength):0;if(status.wifi_connected&&!bars)bars=1;
    tray_icon(TRAY_WIFI-1,bars|(!status.wifi_connected?8:0),(status.features&AH_WIFI)!=0);
    if(!(status.features&AH_WIFI))
    {
        if(IsWindowVisible(key))close_key();
        if(IsWindowVisible(hidden))close_hidden();
        ShowWindow(wifi,0);ShowWindow(wifi_status,0);
    }
    if(was_connected!=status.wifi_connected)
    {
        if(status.wifi_connected)connected_since=GetTickCount();
        if(status.wifi_connected&&was_connected==0)
        {
            char note[200]="";cat(note,ansi(status.wifi_name));cat(note,"\r\nSignal Strength: ");cat(note,signal_text(strength));
            show_notice(icons[TRAY_WIFI-1],"Wireless Network Connection is now connected",note);
        }
        was_connected=status.wifi_connected;
    }
    update_adapters();
    if(IsWindowVisible(wifi_status))update_status_window();

    if(asked_network&&status.generation!=asked_generation&&!status.busy&&status.last_action==AH_CONNECT)
    {
        const U network=asked_network;asked_network=0;
        if(status.error==86)for(U i=0;i<status.networks;++i)if(status.network[i].id==network){ask_key(&status.network[i]);break;}
    }
    set_text(wifi_message,message_for(1));set_text(eject_message,message_for(0));
    if(shown_generation!=status.generation){refresh_lists();shown_generation=status.generation;}
    U letters=0;for(U i=0;i<status.media;++i)if(status.medium[i].letter)++letters;
    tip(TRAY_EJECT-1,"Unplug or Eject Hardware");tray_icon(TRAY_EJECT-1,1,letters!=0);
    /* AutoPlay: a drive that arrived while Windows runs opens its folder. */
    if(autoplay_id&&!status.busy&&status.last_action==AH_ATTACH)
    {
        for(U i=0;i<status.media;++i)if(status.medium[i].id==autoplay_id&&status.medium[i].letter&&!status.error&&!(status.medium[i].flags&AH_MEDIA_OPTICAL))
        {char root[4]="X:\\";root[0]=(char)status.medium[i].letter;ShellExecuteA(0,"open",root,0,0,1);}
        autoplay_id=0;
    }
    attach_new_media();
    update_buttons();
    if(stopping&&!status.busy&&status.last_action==AH_EJECT)
    {
        stopping=0;
        if(!status.error)
        {
            char note[320]="The '";cat(note,stopped_name);cat(note,"' device can now be safely removed from the system.");
            show_notice(icons[TRAY_EJECT-1]?icons[TRAY_EJECT-1]:eject_icon,"Safe To Remove Hardware",note);
        }
    }
}
static void show_power(void)
{
    if(native_power){ShellExecuteA(window,"open","control.exe","powercfg.cpl",0,1);return;}
    refresh();
    I point[2]={0,0};GetCursorPos(point);I sw=GetSystemMetrics(0),sh=GetSystemMetrics(1);
    I x=point[0]-230,y=point[1]-130;if(x<0)x=0;if(y<0)y=0;if(x+240>sw)x=sw-240;if(y+120>sh)y=sh-120;
    SetWindowPos(battery,(H)~0u,x,y,240,120,0x40);SwitchToThisWindow(battery,1);
}
static void show_wifi(void)
{
    refresh();
    if(!IsWindowVisible(wifi)&&!status.busy&&status.wifi_on&&(status.features&AH_WIFI))action(AH_SCAN,0,0);
    show(wifi);SetFocus(network_list);
}
// A click on the tray icon opens the window, the next one closes it again.
// A double click comes as up, double click, up: it opens and stays open.
static void toggle_wifi(U message)
{
    static int after_double;
    if(message==0x203){after_double=1;show_wifi();return;}
    if(after_double){after_double=0;return;}
    if(!IsWindowVisible(wifi)){show_wifi();return;}
    if(IsWindowVisible(key))close_key();
    if(IsWindowVisible(hidden))close_hidden();
    ShowWindow(wifi,0);
}
static void show_devices(void){refresh();show(eject);}
/* The camera is the Windows program Camera (tools/host_devices/camera.c). */
static void show_camera(void){WinExec("C:\\WINDOWS\\CAMERA.EXE",1);}
static void stop_medium(AH_Medium *m)
{
    if(!m)return;
    if(!m->letter){action(AH_ATTACH,m->id,1);return;}
    copy(stopped_name,ansi(m->name),sizeof stopped_name);
    if(action(AH_EJECT,m->id,0))stopping=1;
}
static void connect_network(AH_Network *n)
{
    if(!n)return;
    if(n->flags&AH_NET_ACTIVE){action(AH_DISCONNECT,0,0);return;}
    if((n->flags&AH_NET_SECURE)&&!(n->flags&AH_NET_SAVED))ask_key(n);
    else if(action(AH_CONNECT,n->id,1)){asked_network=n->id;asked_generation=status.generation;}
}
/* The key window for a secured network: also when the host has saved
   settings but cannot hand out the key (ERROR_INVALID_PASSWORD). */
static void ask_key(AH_Network *n)
{
    char text[400]="The network '";cat(text,ansi(n->name));
    cat(text,"' requires a network key. A network key helps prevent unknown intruders from connecting to this network.\r\n\r\nType the key, and then click Connect.");
    SetWindowTextA(key_text,text);SetWindowTextA(key_entry,"");SetWindowTextA(key_confirm,"");
    key_network=n->id;EnableWindow(wifi,0);show(key);SetFocus(key_entry);
}
static void close_key(void){EnableWindow(wifi,1);ShowWindow(key,0);SetWindowTextA(key_entry,"");SetWindowTextA(key_confirm,"");if(IsWindowVisible(wifi))SwitchToThisWindow(wifi,1);}
static void submit_key(void)
{
    char a[128],b[128];GetWindowTextA(key_entry,a,sizeof a);GetWindowTextA(key_confirm,b,sizeof b);
    U same=1;for(U i=0;i<sizeof a&&(a[i]||b[i]);++i)if(a[i]!=b[i]){same=0;break;}
    U empty=!a[0];wipe(a,sizeof a);wipe(b,sizeof b);
    if(empty||!same)
    {
        MessageBoxA(key,empty?"Type the network key.":"The network keys do not match. Type the key again in both boxes.","Wireless Network Connection",0x30);
        SetWindowTextA(key_confirm,"");return;
    }
    U network=key_network;U remember=SendMessageA(key_save,0xf0,0,0)!=0;
    EnableWindow(wifi,1);ShowWindow(key,0);
    action(AH_CONNECT,network,remember);
    if(IsWindowVisible(wifi))SwitchToThisWindow(wifi,1);
}
static void show_wifi_status(void){refresh();update_status_window();show(wifi_status);}
static void show_hidden(void)
{
    SetWindowTextA(hidden_name,"");SetWindowTextA(hidden_key,"");SetWindowTextA(hidden_confirm,"");
    SendMessageA(hidden_secure,0xf1,1,0);EnableWindow(hidden_key,1);EnableWindow(hidden_confirm,1);
    EnableWindow(wifi,0);show(hidden);SetFocus(hidden_name);
}
static void close_hidden(void)
{
    SetWindowTextA(hidden_key,"");SetWindowTextA(hidden_confirm,"");
    EnableWindow(wifi,1);ShowWindow(hidden,0);if(IsWindowVisible(wifi))SwitchToThisWindow(wifi,1);
}
/* ANSI text of an edit control as UTF-8; zero when it does not fit. */
static U utf8_text(H edit,char *out,U capacity)
{
    char entered[128];unsigned short wide[128];GetWindowTextA(edit,entered,sizeof entered);
    I n=MultiByteToWideChar(0,0,entered,-1,wide,128);
    I okay=n&&WideCharToMultiByte(65001,0,wide,-1,out,(I)capacity,0,0);
    wipe(entered,sizeof entered);wipe((volatile char*)wide,sizeof wide);
    return okay>0;
}
static void submit_hidden(void)
{
    AH_Request r;memset(&r,0,sizeof r);r.version=AH_VERSION;r.action=AH_CONNECT_HIDDEN;
    const U secure=SendMessageA(hidden_secure,0xf0,0,0)!=0;
    S problem=0;
    if(!utf8_text(hidden_name,r.ssid,sizeof r.ssid)||!r.ssid[0])problem="Type a network name (SSID) of up to 32 bytes.";
    else if(secure)
    {
        char a[128],b[128];GetWindowTextA(hidden_key,a,sizeof a);GetWindowTextA(hidden_confirm,b,sizeof b);
        U same=1,length=len(a);for(U i=0;i<sizeof a&&(a[i]||b[i]);++i)if(a[i]!=b[i]){same=0;break;}
        wipe(a,sizeof a);wipe(b,sizeof b);
        if(!same)problem="The network keys do not match. Type the key again in both boxes.";
        else if(length<8||length>63)problem="The network key must have 8 to 63 characters.";
        else if(!utf8_text(hidden_key,r.password,sizeof r.password))problem="The network key contains characters Windows cannot send.";
    }
    if(problem){wipe(r.password,sizeof r.password);MessageBoxA(hidden,problem,"Wireless Network Connection",0x30);return;}
    r.flags=(secure?AH_HIDDEN_SECURE:0)|(SendMessageA(hidden_save,0xf0,0,0)?AH_HIDDEN_SAVE:0);
    close_hidden();send(&r);
}
static void popup_menu(U which)
{
    I point[2]={0,0};GetCursorPos(point);H menu=CreatePopupMenu();
    if(which==TRAY_WIFI)
    {
        AppendMenuA(menu,0,505,status.wifi_on?"Turn Wireless &Off":"Turn Wireless &On");
        AppendMenuA(menu,status.wifi_connected?0:1,506,"&Status");
        AppendMenuA(menu,0,502,"View &Available Wireless Networks");
        SetMenuDefaultItem(menu,502,0);
    }
    else if(which==TRAY_EJECT)
    {
        U any=0;
        for(U i=0;i<media_count&&i<AH_MAX_MEDIA;++i)if(media[i].letter)
        {char text[300];copy(text,media[i].flags&AH_MEDIA_OPTICAL?"Eject ":"Safely remove ",sizeof text);char name[240];medium_text(name,&media[i]);cat(text,name);AppendMenuA(menu,0,510+i,text);any=1;}
        if(!any)AppendMenuA(menu,1,0,"No removable devices");
    }
    else if(which==~0u)
    {
        AppendMenuA(menu,0,503,"&Unplug or Eject Hardware");SetMenuDefaultItem(menu,503,0);
    }
    else
    {
        AppendMenuA(menu,0,501,"Open &Power Meter");SetMenuDefaultItem(menu,501,0);
    }
    SetForegroundWindow(window);I id=TrackPopupMenu(menu,0x100|2,point[0],point[1],0,window,0);DestroyMenu(menu);
    PostMessageA(window,0,0,0);
    if(id)PostMessageA(window,0x111,(U)id,0);
}
static void draw_bars(H dc,RECT *r,U bars)
{
    H on=CreateSolidBrush(0x0000b000),off=CreateSolidBrush(0x00ffffff),edge=CreateSolidBrush(0x00808080);
    for(U i=0;i<5;++i)
    {
        I height=4+(I)i*3;RECT bar={r->right-30+(I)i*5,r->bottom-height,r->right-30+(I)i*5+4,r->bottom};
        FillRect(dc,&bar,edge);RECT inside={bar.left+1,bar.top+1,bar.right-1,bar.bottom-1};
        FillRect(dc,&inside,i<bars?on:off);
    }
    DeleteObject(on);DeleteObject(off);DeleteObject(edge);
}
static void draw_item(DRAWITEM *d)
{
    if(d->item==~0u)return;
    if((d->id==10&&!network_count)||(d->id==14&&!media_count))
    {
        H white=CreateSolidBrush(GetSysColor(5));FillRect(d->dc,&d->rect,white);DeleteObject(white);
        SetBkMode(d->dc,1);SetTextColor(d->dc,GetSysColor(17));H font=SelectObject(d->dc,GetStockObject(17));
        DrawTextA(d->dc,empty_text(d->id),-1,&d->rect,0x825);SelectObject(d->dc,font);return;
    }
    const U chosen=(d->state&1)!=0;
    H brush=CreateSolidBrush(GetSysColor(chosen?13:5));FillRect(d->dc,&d->rect,brush);DeleteObject(brush);
    SetBkMode(d->dc,1);SetTextColor(d->dc,GetSysColor(chosen?14:8));
    H old=SelectObject(d->dc,GetStockObject(17));
    if(d->id==10&&d->item<network_count)
    {
        AH_Network *n=&networks[d->item];char text[200]="";
        DrawIconEx(d->dc,d->rect.left+6,d->rect.top+12,network_icon,16,16,0,0,3);
        RECT name={d->rect.left+30,d->rect.top+4,d->rect.right-90,d->rect.top+20};
        SelectObject(d->dc,bold);copy(text,ansi(n->name),sizeof text);DrawTextA(d->dc,text,-1,&name,0x8820);
        RECT state={d->rect.right-110,d->rect.top+4,d->rect.right-6,d->rect.top+20};
        if(n->flags&AH_NET_ACTIVE)DrawTextA(d->dc,"Connected",-1,&state,0x822);
        SelectObject(d->dc,GetStockObject(17));
        if(!(n->flags&AH_NET_ACTIVE)&&(n->flags&AH_NET_SAVED))DrawTextA(d->dc,"Saved",-1,&state,0x822);
        RECT line={d->rect.left+30,d->rect.top+21,d->rect.right-40,d->rect.top+37};text[0]=0;security_text(text,n->flags);
        DrawTextA(d->dc,text,-1,&line,0x8820);
        RECT meter={d->rect.left,d->rect.top,d->rect.right-6,d->rect.bottom-5};draw_bars(d->dc,&meter,meter_for(n->signal));
    }
    else if(d->id==14&&d->item<media_count)
    {
        AH_Medium *m=&media[d->item];char text[240];medium_text(text,m);
        DrawIconEx(d->dc,d->rect.left+4,d->rect.top+3,m->flags&AH_MEDIA_OPTICAL?cdrom_icon:medium_icon,16,16,0,0,3);
        RECT line={d->rect.left+26,d->rect.top,d->rect.right-4,d->rect.bottom};
        if(!m->letter&&!chosen)SetTextColor(d->dc,GetSysColor(17));
        DrawTextA(d->dc,text,-1,&line,0x8824);
    }
    SelectObject(d->dc,old);
    if(d->state&0x10)DrawFocusRect(d->dc,&d->rect);
}
static void command(H hwnd,U id,U code)
{
    if(hwnd==key){if(id==1)submit_key();else if(id==2)close_key();return;}
    if(hwnd==hidden)
    {
        if(id==1)submit_hidden();else if(id==2)close_hidden();
        else if(id==32){U on=SendMessageA(hidden_secure,0xf0,0,0)!=0;EnableWindow(hidden_key,on);EnableWindow(hidden_confirm,on);}
        return;
    }
    if(id==36&&code==1){I chosen=SendMessageA(adapter_box,0x147,0,0);if(chosen>=0&&(U)chosen!=status.adapter&&action(AH_SELECT_ADAPTER,(U)chosen,0))shown_generation=~0u;return;}
    if(id==10&&code==1){update_buttons();return;}
    if(id==14&&code==1){update_buttons();return;}
    if(id==10&&code==2){connect_network(chosen_network());return;}
    if(id==14&&code==2){stop_medium(chosen_medium());return;}
    if(id==101)action(AH_SCAN,0,0);
    else if(id==102||id==505)action(status.wifi_on?AH_WIFI_OFF:AH_WIFI_ON,0,0);
    else if(id==103||(id==1&&hwnd==wifi))connect_network(chosen_network());
    else if(id==105)
    {
        AH_Network *n=chosen_network();if(!n)return;
        char text[300]="Remove the saved settings for '";cat(text,ansi(n->name));cat(text,"'? Windows will ask for the network key the next time you connect.");
        if(MessageBoxA(wifi,text,"Wireless Network Connection",0x24)==6)action(AH_FORGET,n->id,0);
    }
    else if(id==203||(id==1&&hwnd==eject))stop_medium(chosen_medium());
    else if(id>=510&&id<510+AH_MAX_MEDIA&&id-510<media_count)stop_medium(&media[id-510]);
    else if(id==104)action(AH_DISCONNECT,0,0);
    else if(id==106)show_hidden();
    else if(id==506)show_wifi_status();
    else if(id==501)show_power();else if(id==502)show_wifi();else if(id==503)show_devices();else if(id==504)show_camera();
    else if(id==2&&hwnd!=window)ShowWindow(hwnd,0);
}
static I __stdcall proc(H hwnd,U msg,U wp,I lp)
{
    if(msg==taskbar_created){added[0]=added[1]=added[2]=0;icon_keys[0]=icon_keys[1]=icon_keys[2]=~0u;refresh();return 0;}
    if(hwnd==notice)
    {
        if(msg==0xf){paint_notice(hwnd);return 0;}
        if(msg==0x113||msg==0x201){KillTimer(hwnd,3);ShowWindow(hwnd,0);return 0;}
        return DefWindowProcA(hwnd,msg,wp,lp);
    }
    if(hwnd==wifi_status&&msg==0xf){paint_status(hwnd);return 0;}
    if(msg==0x113){refresh();return 0;}
    if(msg==0x2c){U *m=(U*)lp;m[4]=m[1]==10?ROW_NETWORK:ROW_MEDIUM;return 1;}
    if(msg==0x2b){draw_item((DRAWITEM*)lp);return 1;}
    if(msg==0x401)
    {
        if(lp==0x202||lp==0x203)
        {
            if(wp==TRAY_POWER)show_power();
            else if(wp==TRAY_WIFI)toggle_wifi(lp);
            else if(wp==TRAY_EJECT){if(lp==0x203)show_devices();else popup_menu(TRAY_EJECT);}
        }
        else if(lp==0x205)popup_menu(wp==TRAY_EJECT?~0u:wp);
        return 0;
    }
    if(msg==0x402)
    {
        if(wp==SHOW_POWER)show_power();else if(wp==SHOW_WIFI)show_wifi();else if(wp==SHOW_CAMERA)show_camera();
        else if(wp==SHOW_CONTROLLERS)ShellExecuteA(0,"open","control.exe","joy.cpl",0,1);else show_devices();
        return 0;
    }
    if(msg==0x10){if(hwnd==key)close_key();else if(hwnd==hidden)close_hidden();else ShowWindow(hwnd,0);return 0;}
    if(msg==6&&!(wp&65535)&&hwnd==battery){ShowWindow(hwnd,0);return 0;}
    if(msg==2&&hwnd==window)
    {
        for(U i=0;i<3;++i)if(added[i])Shell_NotifyIconA(2,&trays[i]);KillTimer(hwnd,1);PostQuitMessage(0);return 0;
    }
    if(msg==0x111){command(hwnd,wp&65535,wp>>16);return 0;}
    return DefWindowProcA(hwnd,msg,wp,lp);
}
void mainCRTStartup(void)
{
    H mutex=CreateMutexA(0,0,"NEO98HardwareManager");
    if(mutex&&GetLastError()==183){H previous=FindWindowA("NEO98Hardware",0);if(previous)PostMessageA(previous,0x402,SHOW_WIFI,0);ExitProcess(0);}
    instance=GetModuleHandleA(0);bridge=CreateFileA("\\\\.\\NEO98HOST",0,0,0,3,0,0);
    if(bridge==(H)~0u){MessageBoxA(0,"The NEO98 host integration is not available.","NEO98 Hardware",0x10);ExitProcess(1);}
    taskbar_created=RegisterWindowMessageA("TaskbarCreated");
    /* The raster MS Sans Serif has no bold face; Windows 98 ships Tahoma Bold. */
    bold=CreateFontA(-11,0,0,0,700,0,0,0,0,0,0,0,0,"Tahoma");
    /* The Explorer pictures for drives and Dial-Up Networking's connection icon. */
    network_icon=original_icon("C:\\WINDOWS\\SYSTEM\\RNAUI.DLL",101);if(!network_icon)network_icon=picture_from(tower);
    medium_icon=original_icon("C:\\WINDOWS\\SYSTEM\\SHELL32.DLL",8);if(!medium_icon)medium_icon=picture_from(stick);
    cdrom_icon=original_icon("C:\\WINDOWS\\SYSTEM\\SHELL32.DLL",12);if(!cdrom_icon)cdrom_icon=picture_from(disc);
    /* Unplug or Eject Hardware from Microsoft's KB242975 (tools/host_devices/icons). */
    eject_icon=art_icon(&eject_dialog_art);
    CLASS c={0,proc,0,0,instance,0,LoadCursorA(0,(S)32512),(H)16,0,"NEO98Hardware"};RegisterClassA(&c);
    c.name="NEO98Wifi";RegisterClassA(&c);c.name="NEO98WifiKey";RegisterClassA(&c);c.name="NEO98Eject";RegisterClassA(&c);
    c.name="NEO98Battery";RegisterClassA(&c);c.name="NEO98WifiStatus";RegisterClassA(&c);c.name="NEO98WifiHidden";RegisterClassA(&c);
    c.name="NEO98Notice";c.brush=0;RegisterClassA(&c);
    /* The owner of the tray icons. It never shows itself. */
    window=CreateWindowExA(0,"NEO98Hardware","NEO98 Hardware",0x00c80000,0,0,100,100,0,0,instance,0);

    wifi=frame_window("NEO98Wifi","Wireless Network Connection",452,372,0);
    SendMessageA(control(wifi,"STATIC","Choose a wireless network",0,0,12,10,428,16,22),0x30,(U)bold,1);
    label(wifi,"Click an item in the list below to connect to a wireless network in range.",12,28,428,16,23);
    network_list=list(wifi,12,50,428,200,10,ROW_NETWORK);
    adapter_label=label(wifi,"&Adapter:",12,262,52,16,0);
    adapter_box=control(wifi,"COMBOBOX","",0x10000|0x200000|3,0,66,258,180,120,36);
    ShowWindow(adapter_label,0);ShowWindow(adapter_box,0);
    button(wifi,"Add &Hidden Network...",300,256,140,106,0);
    wifi_state=label(wifi,"Reading wireless status...",12,288,428,16,21);
    wifi_message=label(wifi,"",12,306,428,28,17);
    button(wifi,"&Refresh Network List",12,338,140,101,0);button(wifi,"Turn &Off Wireless",158,338,120,102,0);
    button(wifi,"&Forget",284,338,72,105,0);button(wifi,"&Connect",362,338,78,103,1);

    key=frame_window("NEO98WifiKey","Wireless Network Connection",380,212,wifi);
    key_text=label(key,"",12,10,356,70,24);
    label(key,"Network &key:",12,92,130,16,0);
    key_entry=control(key,"EDIT","",0x10000|0x80|0x20,0x200,148,88,220,22,11);SendMessageA(key_entry,0xc5,127,0);
    label(key,"Con&firm network key:",12,122,130,16,0);
    key_confirm=control(key,"EDIT","",0x10000|0x80|0x20,0x200,148,118,220,22,12);SendMessageA(key_confirm,0xc5,127,0);
    key_save=control(key,"BUTTON","&Remember this network",3|0x10000,0,12,148,300,20,13);SendMessageA(key_save,0xf1,1,0);
    button(key,"&Connect",206,178,78,1,1);button(key,"Cancel",290,178,78,2,0);

    hidden=frame_window("NEO98WifiHidden","Add Hidden Network",360,236,wifi);
    label(hidden,"Type the name of a network that does not announce itself. Windows connects to it when it is in range.",12,10,336,30,0);
    label(hidden,"Network &name (SSID):",12,52,128,16,0);
    hidden_name=control(hidden,"EDIT","",0x10000|0x80,0x200,146,48,202,22,31);SendMessageA(hidden_name,0xc5,32,0);
    hidden_secure=control(hidden,"BUTTON","This network requires a network key (&WPA)",3|0x10000,0,12,80,330,20,32);
    label(hidden,"Network &key:",12,112,128,16,0);
    hidden_key=control(hidden,"EDIT","",0x10000|0x80|0x20,0x200,146,108,202,22,33);SendMessageA(hidden_key,0xc5,63,0);
    label(hidden,"Con&firm network key:",12,142,128,16,0);
    hidden_confirm=control(hidden,"EDIT","",0x10000|0x80|0x20,0x200,146,138,202,22,34);SendMessageA(hidden_confirm,0xc5,63,0);
    hidden_save=control(hidden,"BUTTON","&Remember this network",3|0x10000,0,12,170,300,20,35);SendMessageA(hidden_save,0xf1,1,0);
    button(hidden,"&Connect",186,202,78,1,1);button(hidden,"Cancel",270,202,78,2,0);

    wifi_status=frame_window("NEO98WifiStatus","Wireless Network Connection Status",330,234,0);
    control(wifi_status,"BUTTON","Connection",7,0,12,8,306,124,0);
    {
        static const S names[]={"Status:","Network:","Duration:","Speed:","Signal Strength:"};
        for(U i=0;i<5;++i){label(wifi_status,names[i],24,28+18*(I)i,110,16,0);status_values[i]=label(wifi_status,"",140,28+18*(I)i,i==4?72:170,16,40+i);}
    }
    control(wifi_status,"BUTTON","Address",7,0,12,140,306,46,0);
    label(wifi_status,"IP Address:",24,160,110,16,0);status_values[5]=label(wifi_status,"",140,160,170,16,45);
    button(wifi_status,"&Disconnect",12,200,90,104,0);button(wifi_status,"&View Wireless Networks",108,200,140,502,0);button(wifi_status,"Close",254,200,64,2,1);

    notice=CreateWindowExA(0x88,"NEO98Notice","",0x80800000u,0,0,300,64,0,0,instance,0);

    eject=frame_window("NEO98Eject","Unplug or Eject Hardware",392,300,0);
    eject_picture=control(eject,"STATIC","",3,0,14,14,16,16,26);SendMessageA(eject_picture,0x170,(U)eject_icon,0);
    label(eject,"Select the device you want to unplug or eject, and then click Stop. When Windows notifies you that it is safe to do so, unplug the device from your computer.",44,12,336,56,25);
    label(eject,"&Hardware devices:",12,74,368,16,0);
    media_list=list(eject,12,92,368,116,14,ROW_MEDIUM);
    media_info=label(eject,"",12,214,368,16,18);
    eject_message=label(eject,"",12,234,368,28,19);
    button(eject,"&Stop",216,266,78,203,0);button(eject,"Close",302,266,78,2,1);

    battery=CreateWindowExA(0x80,"NEO98Battery","Power Meter",0x00c80000,0,0,240,120,window,0,instance,0);
    power_text=label(battery,"Reading battery status...",14,12,206,60,20);

    for(U i=0;i<3;++i){trays[i].size=sizeof(TRAY);trays[i].hwnd=window;trays[i].id=i+1;trays[i].flags=1|2|4;trays[i].message=0x401;}
    if(FindWindowA("Shell_TrayWnd",0))WinExec("C:\\WINDOWS\\SYSTEM\\SYSTRAY.EXE 8",0);
    refresh();SetTimer(window,1,1000,0);
    MSG m;
    while(GetMessageA(&m,0,0,0)>0)
    {
        H dialog=IsWindowVisible(key)?key:0,active=GetForegroundWindow();
        if(!dialog&&IsWindowVisible(hidden))dialog=hidden;
        if(!dialog&&(active==wifi||active==eject||active==battery||active==wifi_status))dialog=active;
        if(!dialog||!IsDialogMessageA(dialog,&m)){TranslateMessage(&m);DispatchMessageA(&m);}
    }
    CloseHandle(bridge);if(mutex)CloseHandle(mutex);
    for(U i=0;i<3;++i)if(icons[i])DestroyIcon(icons[i]);
    DestroyIcon(network_icon);DestroyIcon(medium_icon);DestroyIcon(cdrom_icon);DestroyIcon(eject_icon);DeleteObject(bold);ExitProcess(0);
}
