/* SPDX-License-Identifier: GPL-2.0-or-later */
/* MEDIACTR.EXE, Media Center 98: the catalogue of Kodi on the host as an
   Explorer view. The tree on the left holds the folders, the right side
   shows the folder as large icons with the posters; a double click opens a
   folder or plays a video. The video plays in Kodi on the host, which takes
   the screen until it stops or the user goes back; where the host can, the
   video shows in this window instead (the right side, painted in
   HM_VIDEO_KEY for the host to fill). A dialog of Kodi (a question of an
   add-on, the choice of a profile, a sign-in) comes up here as a Windows
   dialog, and the answer goes back to Kodi. The program talks to the host
   through \\.\NEO98MEDIA (src/host_media_abi.h) and never sees a picture
   of the video. */
#include "../host_devices/guest_win32.h"
#include "../../src/host_media_abi.h"
#define API(dll,ret,name,args) __declspec(dllimport) ret __stdcall name args
API(USER32,H,CreateMenu,(void));
API(USER32,H,LoadIconA,(H,S));
API(USER32,I,MoveWindow,(H,I,I,I,I,I));
API(USER32,H,GetDC,(H));
API(USER32,I,ReleaseDC,(H,H));
API(USER32,I,EnableMenuItem,(H,U,U));
API(USER32,H,GetMenu,(H));
API(USER32,I,DrawIcon,(H,I,I,H));
API(USER32,I,TranslateAcceleratorA,(H,H,MSG*));
API(USER32,H,CreateAcceleratorTableA,(void*,I));
API(USER32,I,FrameRect,(H,RECT*,H));
API(USER32,I,IsIconic,(H));
API(USER32,I,DialogBoxIndirectParamA,(H,const void*,H,PROC,I));
API(USER32,I,EndDialog,(H,I));
API(GDI32,U,SetBkColor,(H,U));
API(USER32,I,ScreenToClient,(H,void*));
API(GDI32,H,CreateCompatibleBitmap,(H,I,I));
API(GDI32,H,CreateCompatibleDC,(H));
API(GDI32,I,DeleteDC,(H));
API(GDI32,H,CreateDIBSection,(H,const void*,U,void**,H,U));
API(SHELL32,H,ExtractIconA,(H,S,U));
API(SHELL32,I,ShellAboutA,(H,S,S,H));
API(COMCTL32,void,InitCommonControls,(void));
API(COMCTL32,H,ImageList_Create,(I,I,U,I,I));
API(COMCTL32,I,ImageList_Add,(H,H,H));
API(COMCTL32,I,ImageList_Remove,(H,I));
API(COMCTL32,I,ImageList_ReplaceIcon,(H,I,H));
API(KERNEL32,void*,VirtualAlloc,(void*,U,U,U));
#undef API

void *memset(void *p,I c,unsigned n){unsigned char *b=p;for(U i=0;i<n;++i)b[i]=(unsigned char)c;return p;}
void *memcpy(void *d,const void *s,unsigned n){for(U i=0;i<n;++i)((unsigned char*)d)[i]=((const unsigned char*)s)[i];return d;}
static U len(S s){U n=0;while(s[n])++n;return n;}
static void copy(char *d,S s,U capacity){U i=0;for(;i+1<capacity&&s[i];++i)d[i]=s[i];d[i]=0;}
static void cat(char *d,S s,U capacity){U n=len(d);copy(d+n,s,capacity>n?capacity-n:0);}
static I same(S a,S b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
static void number(char *s,U n,U capacity){char b[16];U k=0;do{b[k++]=(char)('0'+n%10);n/=10;}while(n);U p=len(s);while(k&&p+1<capacity)s[p++]=b[--k];s[p]=0;}
static void clock_text(char *s,U seconds,U capacity)
{
    number(s,seconds/3600,capacity);cat(s,":",capacity);
    char two[3]={(char)('0'+seconds/60%60/10),(char)('0'+seconds/60%10),0};cat(s,two,capacity);cat(s,":",capacity);
    two[0]=(char)('0'+seconds%60/10);two[1]=(char)('0'+seconds%10);cat(s,two,capacity);
}
/* The host's text (UTF-8) in the ANSI code page. */
static S ansi(S text,char *out,U capacity)
{
    unsigned short wide[1024];
    I n=MultiByteToWideChar(65001,0,text,-1,wide,1024);
    if(n>0&&WideCharToMultiByte(0,0,wide,-1,out,(I)capacity,0,0))return out;
    copy(out,text,capacity);return out;
}

#define TITLE "Media Center 98"
#define ROOT_LABEL "Kodi"
#define ROOT_PATH ""
#define CLASS_NAME "NEO98MediaCenter"
enum { POSTER_W=96, POSTER_H=144, BAR_H=32, MAX_NODES=1024, MAX_ITEMS=1024 };
enum { ID_TREE=10, ID_LIST=11, ID_STATUS=12, ID_MESSAGE=13, ID_RETRY=14, ID_NOW=15,
       ID_PLAYPAUSE=20, ID_STOP=21, ID_BACK30=22, ID_FORWARD30=23, ID_SHOWVIDEO=24, ID_VIDEO=25, ID_SEEK=26, ID_MUTE=27, ID_VOLUME=28 };
enum { MENU_OPEN=100, MENU_EXIT=101, MENU_REFRESH=110, MENU_UP=111, MENU_PLAYPAUSE=120, MENU_STOP=121,
       MENU_BACK30=122, MENU_FORWARD30=123, MENU_SHOWVIDEO=124, MENU_SETUP=130, MENU_ABOUT=131 };
/* Common controls (commctrl.h). */
enum { TVM_INSERTITEMA=0x1100, TVM_EXPAND=0x1102, TVM_GETNEXTITEM=0x110a, TVM_SELECTITEM=0x110b, TVM_SETIMAGELIST=0x1109, TVM_SETITEMA=0x110d,
       LVM_SETIMAGELIST=0x1003, LVM_DELETEALLITEMS=0x1009, LVM_INSERTITEMA=0x1007, LVM_SETITEMA=0x1006, LVM_GETNEXTITEM=0x100c,
       LVM_REDRAWITEMS=0x1015, LVM_SETICONSPACING=0x1035, LVM_ARRANGE=0x1016,
       SB_SETTEXTA=0x401, SB_SETPARTS=0x404 };
#define TVI_ROOT ((H)0xffff0000u)
#define TVI_LAST ((H)0xffff0002u)
typedef struct { H hwnd; U id; I code; } NMHDR;
typedef struct { U mask; H item; U state,state_mask; char *text; I text_max,image,selected_image,children; U param; } TVITEM;
typedef struct { H parent,after; TVITEM item; } TVINSERT;
typedef struct { NMHDR hdr; U action; TVITEM old_item,new_item; I x,y; } NMTREEVIEW;
typedef struct { U mask; I item,sub_item; U state,state_mask; char *text; I text_max,image; U param; } LVITEM;
typedef struct { NMHDR hdr; unsigned short key; U flags; } NMKEY;
typedef struct { NMHDR hdr; I item,sub_item; U new_state,old_state,changed; I x,y; U param; } NMLISTVIEW;
typedef struct { unsigned char virt; unsigned short key,cmd; } ACCEL;

/* A folder of the tree: its place in Kodi, its tree item, its parent, and
   whether its subfolders are in the tree. */
typedef struct { char path[HM_PATH]; H item; U parent,filled; } NODE;
static NODE *nodes;static U node_count;
static H instance,window,tree,list,status_bar,message,retry,now_text,device,posters,folder_icons,icon;
static H bar_buttons[5],seek_bar,mute_button,volume_bar;
/* The volume shown, and the user dragging its slider. */
static U shown_volume=~0u,shown_muted=~0u,volume_dragging;
/* The pictures of the bar's buttons: play, pause, stop, back, forward, full screen. */
enum { GLYPH_PLAY, GLYPH_PAUSE, GLYPH_STOP, GLYPH_BACK, GLYPH_FORWARD, GLYPH_FULL, GLYPH_SPEAKER, GLYPH_MUTED, GLYPHS };
static H glyphs[GLYPHS];static U shown_glyph=~0u;
/* The timeline: its length, the user dragging it, the place asked for and
   until when the position of Kodi does not move it back. */
static U seek_max,seek_dragging,seek_target=~0u,seek_hold;
/* Media Center's own full screen: a window over the whole screen in the key colour. */
static H full;static U full_on;
/* The video area, and what the host last heard of it. Until Kodi shows the
   first picture it shows Windows' download animation (SHDOCVW.DLL, AVI 256)
   and "Opening...". */
static H video,key_brush,face_brush,animation;static U video_shown,video_opening;static I area_sent[5]={-1,-1,-1,-1,-1};
static HM_Status status;static HM_Request request;static HM_Listing *listing;static HM_Picture *picture;
/* The folder on the right: its node, its entries as far as they have come,
   their pictures, and whether a page is still due. */
static U current=~0u,loading,failed;
static HM_Entry *items;static U item_count;
static U picture_state[MAX_ITEMS];
enum { ART_NONE=0, ART_WANTED, ART_DONE };
/* Folders opened in the tree whose subfolders are still being read. */
static U pending_fill[16],pending_count;
static U shown_generation=~0u,shown_player=~0u,shown_screen=~0u,shown_state=~0u;
static U folder_image=0,video_image=1,bar_shown;
static H folder_icon,video_icon;
/* The folder on the right goes on page by page: the place being read, how
   much of it came, the add-on's next page still to read at the end. */
static char fetch_path[HM_PATH],next_path[HM_PATH];static U fetched,continuing;
/* While a video plays: "Now Playing" first in the tree; browsing a folder
   hides the video (the sound goes on) until it is chosen again. */
#define NOW_PARAM 0xfffffffeu
static H now_item;static U browsing;
static char text_buffer[1024];

static I control_io(U code,void *in,U in_size,void *out,U out_size)
{U done=0;return DeviceIoControl(device,code,in,in_size,out,out_size,&done,0);}
static void prepare(U action,S path){memset(&request,0,sizeof request);request.version=HM_VERSION;request.action=action;copy(request.path,path,sizeof request.path);}
static U action(U what,S path,I value)
{
    prepare(what,path);request.value=value;
    return control_io(HM_IOCTL_CONTROL,&request,sizeof request,0,0)?0:GetLastError();
}
static void read_status(void){if(!control_io(HM_IOCTL_STATUS,0,0,&status,sizeof status))memset(&status,0,sizeof status);}
static U browse(S path,U first)
{
    prepare(0,path);request.first=first;
    if(!control_io(HM_IOCTL_BROWSE,&request,sizeof request,listing,sizeof *listing)){listing->state=HM_LIST_FAILED;listing->message[0]=0;listing->count=0;}
    return listing->state;
}

static void set_status(U part,S text){SendMessageA(status_bar,SB_SETTEXTA,part,(I)text);}
/* Only a text that changes is set, so the timer does not make it flicker. */
static void set_text(H h,S text)
{
    char now[512];GetWindowTextA(h,now,sizeof now);
    if(!same(now,text))SetWindowTextA(h,text);
}

/* A box of the poster size with an icon in the middle, for entries without a picture. */
static I placeholder(H source)
{
    H screen=GetDC(0),dc=CreateCompatibleDC(screen);void *bits=0;
    DIB bmi;memset(&bmi,0,sizeof bmi);bmi.size=sizeof bmi;bmi.width=POSTER_W;bmi.height=-POSTER_H;bmi.planes=1;bmi.bits=32;
    H bitmap=CreateDIBSection(screen,&bmi,0,&bits,0,0);
    I index=-1;
    if(bitmap&&dc)
    {
        H old=SelectObject(dc,bitmap);
        RECT r={0,0,POSTER_W,POSTER_H};FillRect(dc,&r,(H)6);
        RECT frame={8,24,POSTER_W-8,POSTER_H-24};FrameRect(dc,&frame,GetStockObject(2));
        if(source)DrawIcon(dc,(POSTER_W-32)/2,(POSTER_H-32)/2,source);
        SelectObject(dc,old);
        index=ImageList_Add(posters,bitmap,0);
    }
    if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);ReleaseDC(0,screen);
    return index;
}
/* The picture of the host as an image of the list. */
static I poster_image(void)
{
    H screen=GetDC(0);void *bits=0;
    DIB bmi;memset(&bmi,0,sizeof bmi);bmi.size=sizeof bmi;bmi.width=(I)picture->width;bmi.height=-(I)picture->height;bmi.planes=1;bmi.bits=32;
    H bitmap=CreateDIBSection(screen,&bmi,0,&bits,0,0);
    I index=-1;
    if(bitmap&&bits){memcpy(bits,picture->pixels,picture->width*picture->height*4);index=ImageList_Add(posters,bitmap,0);DeleteObject(bitmap);}
    ReleaseDC(0,screen);
    return index;
}

static U add_node(S path,S label,U parent,U children)
{
    if(node_count>=MAX_NODES)return ~0u;
    NODE *n=&nodes[node_count];copy(n->path,path,sizeof n->path);n->parent=parent;n->filled=0;
    TVINSERT ins;memset(&ins,0,sizeof ins);
    ins.parent=parent==~0u?TVI_ROOT:nodes[parent].item;ins.after=TVI_LAST;
    ins.item.mask=1|2|4|0x20|0x40;ins.item.text=(char*)label;ins.item.image=0;ins.item.selected_image=1;
    ins.item.children=children;ins.item.param=node_count;
    n->item=(H)SendMessageA(tree,TVM_INSERTITEMA,0,(I)&ins);
    return node_count++;
}
static U find_child(U parent,S path)
{
    for(U i=0;i<node_count;++i)if(nodes[i].parent==parent&&same(nodes[i].path,path))return i;
    return ~0u;
}
/* The subfolders of a node from a listing page. */
static void fill_from_listing(U node)
{
    for(U i=0;i<listing->count;++i)
    {
        const HM_Entry *e=&listing->entry[i];
        if(!(e->flags&HM_ENTRY_FOLDER)||(e->flags&HM_ENTRY_NEXT)||find_child(node,e->path)!=~0u)continue;
        add_node(e->path,ansi(e->label,text_buffer,sizeof text_buffer),node,1);
    }
}
static void no_children(U node)
{
    TVITEM t;memset(&t,0,sizeof t);t.mask=0x40;t.item=nodes[node].item;t.children=0;
    SendMessageA(tree,TVM_SETITEMA,0,(I)&t);
}

static void layout(void)
{
    RECT r;GetClientRect(window,&r);
    SendMessageA(status_bar,0x05,0,0);
    RECT s;GetWindowRect(status_bar,&s);
    const I status_h=s.bottom-s.top,tree_w=r.right/4<160?160:r.right/4;
    I parts[2]={r.right-260>100?r.right-260:100,-1};SendMessageA(status_bar,SB_SETPARTS,2,(I)parts);
    const I playing=(I)bar_shown;
    const I body_h=r.bottom-status_h-(playing?BAR_H:0);
    MoveWindow(tree,0,0,tree_w,body_h,1);
    MoveWindow(list,tree_w+2,0,r.right-tree_w-2,body_h,1);
    MoveWindow(video,tree_w+2,0,r.right-tree_w-2,body_h,1);
    MoveWindow(message,tree_w+24,40,r.right-tree_w-48>0?r.right-tree_w-48:0,140,1);
    MoveWindow(retry,tree_w+24,190,90,23,1);
    /* The buttons, the timeline, the time at the right. */
    I x=6;
    for(U i=0;i<5;++i){MoveWindow(bar_buttons[i],x,body_h+4,26,24,1);x+=i==1||i==3?34:28;}
    /* At the right: the speaker and the volume. */
    MoveWindow(volume_bar,r.right-78,body_h+4,72,24,1);
    MoveWindow(mute_button,r.right-108,body_h+4,26,24,1);
    MoveWindow(now_text,r.right-230,body_h+9,116,16,1);
    MoveWindow(seek_bar,x+2,body_h+4,r.right-236-x>40?r.right-236-x:40,24,1);
}

/* The right side says why there is nothing to show. */
static void explain(S text,U offer_retry)
{
    SetWindowTextA(message,ansi(text,text_buffer,sizeof text_buffer));
    ShowWindow(list,0);ShowWindow(message,5);ShowWindow(retry,offer_retry?5:0);
}
static void show_list(void){ShowWindow(message,0);ShowWindow(retry,0);ShowWindow(list,5);}

static void open_node(U node)
{
    current=node;item_count=0;loading=1;failed=0;
    fetched=0;continuing=0;next_path[0]=0;copy(fetch_path,nodes[node].path,sizeof fetch_path);
    SendMessageA(list,LVM_DELETEALLITEMS,0,0);
    /* The posters of the folder before go: one list of all would fill up. */
    ImageList_Remove(posters,-1);
    folder_image=(U)placeholder((U)folder_icon>1?folder_icon:0);video_image=(U)placeholder((U)video_icon>1?video_icon:0);
    for(U i=0;i<MAX_ITEMS;++i)picture_state[i]=ART_NONE;
    set_status(0,"Loading...");
}

static void add_items(void)
{
    for(U i=0;i<listing->count&&item_count<MAX_ITEMS;++i)
    {
        if(listing->entry[i].flags&HM_ENTRY_NEXT){copy(next_path,listing->entry[i].path,sizeof next_path);continue;}
        items[item_count]=listing->entry[i];
        const HM_Entry *e=&items[item_count];
        LVITEM v;memset(&v,0,sizeof v);v.mask=1|2|4;v.item=(I)item_count;
        v.text=(char*)ansi(e->label,text_buffer,sizeof text_buffer);
        v.image=(I)((e->flags&HM_ENTRY_FOLDER)?folder_image:video_image);v.param=item_count;
        SendMessageA(list,LVM_INSERTITEMA,0,(I)&v);
        picture_state[item_count]=e->art[0]?ART_WANTED:ART_NONE;
        ++item_count;
    }
}

static void objects_text(void)
{
    char text[64]="";number(text,item_count,sizeof text);cat(text," object(s)",sizeof text);set_status(0,text);
}

/* The next page of the folder on the right. */
static void load_current(void)
{
    if(current==~0u||!loading)return;
    const U state=browse(fetch_path,fetched);
    if(state==HM_LIST_LOADING)return;
    if(state==HM_LIST_FAILED)
    {
        loading=0;
        if(continuing){objects_text();return;}   /* the next page failed: what came stays */
        failed=1;
        explain(listing->message[0]?listing->message:"Kodi could not open this folder.",1);
        set_status(0,"");return;
    }
    if(!item_count&&!video_shown)show_list();
    fill_from_listing(current);
    add_items();
    fetched+=listing->count;
    if(fetched>=listing->total||!listing->count||item_count>=MAX_ITEMS)
    {
        loading=0;
        if(continuing){objects_text();return;}
        nodes[current].filled=1;
        U folders=0;for(U i=0;i<item_count;++i)if(items[i].flags&HM_ENTRY_FOLDER)++folders;
        if(!folders)no_children(current);
        else SendMessageA(tree,TVM_EXPAND,2,(I)nodes[current].item);
        if(!item_count){explain(listing->message[0]?listing->message:"This folder is empty.",0);}
        objects_text();
    }
}
/* Scrolled near the end with a next page to come: it is read and added. */
static U near_end(void)
{
    if(!item_count||!IsWindowVisible(list))return 0;
    RECT last={0,0,0,0},client;SendMessageA(list,0x100e,item_count-1,(I)&last);   /* LVM_GETITEMRECT, LVIR_BOUNDS */
    GetClientRect(list,&client);
    return last.top<client.bottom+POSTER_H;
}
static void load_more(void)
{
    if(loading||!next_path[0]||item_count>=MAX_ITEMS||!near_end())return;
    copy(fetch_path,next_path,sizeof fetch_path);next_path[0]=0;
    fetched=0;continuing=1;loading=1;set_status(0,"Loading more...");
}
/* Subfolders of nodes opened in the tree but not shown on the right. */
static void load_pending(void)
{
    if(!pending_count)return;
    const U node=pending_fill[0];
    U first=0;
    for(;;)
    {
        const U state=browse(nodes[node].path,first);
        if(state==HM_LIST_LOADING)return;
        if(state==HM_LIST_READY)fill_from_listing(node);
        if(state!=HM_LIST_READY||!listing->count||first+listing->count>=listing->total)break;
        first+=listing->count;
    }
    nodes[node].filled=1;
    U children=0;for(U i=0;i<node_count;++i)if(nodes[i].parent==node)++children;
    if(!children)no_children(node);
    for(U i=1;i<pending_count;++i)pending_fill[i-1]=pending_fill[i];
    --pending_count;
}
/* A few pictures at a time; the host fetches them in the background. */
static void load_pictures(void)
{
    U asked=0;
    U window_colour=GetSysColor(5),background=(window_colour&255)<<16|(window_colour&0xff00)|(window_colour>>16&255);
    for(U i=0;i<item_count&&asked<8;++i)
    {
        if(picture_state[i]!=ART_WANTED)continue;
        ++asked;
        prepare(0,items[i].art);request.width=POSTER_W;request.height=POSTER_H;request.value=(I)background;
        if(!control_io(HM_IOCTL_PICTURE,&request,sizeof request,picture,sizeof *picture)){picture_state[i]=ART_DONE;continue;}
        if(picture->state==HM_PICTURE_LOADING)continue;
        picture_state[i]=ART_DONE;
        if(picture->state!=HM_PICTURE_READY)continue;
        const I image=poster_image();
        if(image<0)continue;
        LVITEM v;memset(&v,0,sizeof v);v.mask=2;v.item=(I)i;v.image=image;
        SendMessageA(list,LVM_SETITEMA,0,(I)&v);
        SendMessageA(list,LVM_REDRAWITEMS,i,(I)i);
    }
}

static U selected_item(void){I n=SendMessageA(list,LVM_GETNEXTITEM,(U)-1,2);return n>=0&&(U)n<item_count?(U)n:~0u;}
static void select_node(U node){SendMessageA(tree,TVM_SELECTITEM,9,(I)nodes[node].item);}

/* A double click or Enter: a folder opens, a video plays in Kodi. */
static void activate(U index)
{
    if(index==~0u)return;
    const HM_Entry *e=&items[index];
    if(e->flags&HM_ENTRY_FOLDER)
    {
        U node=find_child(current,e->path);
        if(node==~0u)node=add_node(e->path,ansi(e->label,text_buffer,sizeof text_buffer),current,1);
        if(node==~0u)return;
        SendMessageA(tree,TVM_EXPAND,2,(I)nodes[current].item);
        select_node(node);
        return;
    }
    if(!(e->flags&HM_ENTRY_PLAYABLE))return;
    const U error=action(HM_PLAY,e->path,0);
    if(error)MessageBoxA(window,"Kodi is not ready to play videos.",TITLE,0x30);
    else
    {
        char text[HM_LABEL+16]="Opening ";cat(text,ansi(e->label,text_buffer,sizeof text_buffer),sizeof text);set_status(1,text);
        /* The new video shows at once, also when a folder was in front. */
        if(browsing&&now_item)SendMessageA(tree,TVM_SELECTITEM,9,(I)now_item);
        browsing=0;
    }
}

static void enable(U item,U on){EnableMenuItem(GetMenu(window),item,on?0:1);}
/* Where the video area is on the screen and whether it shows, to the host,
   whenever that changes. */
static void report_area(void)
{
    RECT r={0,0,0,0};if(full_on&&full)GetWindowRect(full,&r);else if(video_shown)GetWindowRect(video,&r);
    const I shown=(full_on&&full)||(video_shown&&IsWindowVisible(window)&&!IsIconic(window));
    const I now[5]={r.left,r.top,r.right-r.left,r.bottom-r.top,shown};
    U same_area=1;for(U i=0;i<5;++i)same_area&=now[i]==area_sent[i];
    if(same_area)return;
    for(U i=0;i<5;++i)area_sent[i]=now[i];
    prepare(HM_VIDEO_AREA,"");request.first=(U)(now[0]&0xffff)|(U)now[1]<<16;request.width=(U)now[2];request.height=(U)now[3];request.value=shown;
    control_io(HM_IOCTL_CONTROL,&request,sizeof request,0,0);
}
/* The video in the window: the area takes the place of the list. */
static void show_video(U on)
{
    if(on==video_shown)return;
    video_shown=on;
    if(on){ShowWindow(list,0);ShowWindow(message,0);ShowWindow(retry,0);ShowWindow(video,5);}
    else{ShowWindow(video,0);if(failed)ShowWindow(message,5);else show_list();}
    report_area();
}
/* "Now Playing" first in the tree while a video plays. */
static void now_playing(U playing)
{
    if(playing&&!now_item)
    {
        TVINSERT ins;memset(&ins,0,sizeof ins);
        ins.parent=TVI_ROOT;ins.after=(H)0xffff0001u;   /* TVI_FIRST */
        ins.item.mask=1|2|0x20|4;ins.item.text="Now Playing";ins.item.image=0;ins.item.selected_image=1;ins.item.param=NOW_PARAM;
        now_item=(H)SendMessageA(tree,TVM_INSERTITEMA,0,(I)&ins);
        browsing=0;
        if(now_item)SendMessageA(tree,TVM_SELECTITEM,9,(I)now_item);
    }
    else if(!playing&&now_item)
    {
        H gone=now_item;now_item=0;browsing=0;
        SendMessageA(tree,0x1101,0,(I)gone);   /* TVM_DELETEITEM */
    }
}
/* A 16 x 16 picture of a button in the dialog colour, its sign in black: a
   bitmap of the screen's kind, which BS_BITMAP draws. */
static H glyph(U kind)
{
    static U pixels[256];
    DIB b;memset(&b,0,sizeof b);b.size=sizeof b;b.width=16;b.height=-16;b.planes=1;b.bits=32;
    const U face=GetSysColor(15),back=(face&255)<<16|(face&0xff00)|(face>>16&255);
    U *p=pixels;
    for(I y=0;y<16;++y)for(I x=0;x<16;++x)
    {
        I dy2=2*y-15;if(dy2<0)dy2=-dy2;   /* twice the distance from the middle row */
        I on=0;
        /* Triangles: half heights shrinking towards the point. */
        if(kind==GLYPH_PLAY)on=x>=4&&x<=11&&y>=2&&y<=13&&dy2<=2*((11-x)*6/7)+1;
        else if(kind==GLYPH_PAUSE)on=y>=3&&y<=12&&((x>=4&&x<=6)||(x>=9&&x<=11));
        else if(kind==GLYPH_STOP)on=x>=4&&x<=11&&y>=4&&y<=11;
        else if(kind==GLYPH_BACK)on=y>=3&&y<=12&&((x>=1&&x<=7&&dy2<=2*((x-1)*5/6)+1)||(x>=8&&x<=14&&dy2<=2*((x-8)*5/6)+1));
        else if(kind==GLYPH_FORWARD)on=y>=3&&y<=12&&((x>=1&&x<=7&&dy2<=2*((7-x)*5/6)+1)||(x>=8&&x<=14&&dy2<=2*((14-x)*5/6)+1));
        else if(kind==GLYPH_FULL)on=(x>=2&&x<=13&&y>=3&&y<=12&&(x<=3||x>=12||y<=4||y>=11))||(x>=6&&x<=9&&y>=6&&y<=9);
        else if(kind==GLYPH_SPEAKER||kind==GLYPH_MUTED)
        {
            /* The box and the cone of a speaker; sound waves, or a cross when muted. */
            on=(x>=1&&x<=3&&y>=6&&y<=9)||(x>=4&&x<=8&&dy2<=2*(x-2)+1&&y>=2&&y<=13);
            if(kind==GLYPH_SPEAKER)
            {
                const I dx=x-6,d2=dx*dx*4+dy2*dy2;   /* twice the distance from the cone's tip, squared, times 4 */
                on|=x>=10&&((d2>=4*16&&d2<=4*25)||(d2>=4*49&&d2<=4*64));
            }
            else on|=x>=10&&x<=14&&y>=5&&y<=10&&(x-10==y-5||x-10==10-y);
        }
        p[y*16+x]=on?0:back;
    }
    H screen=GetDC(0),dc=CreateCompatibleDC(screen),bitmap=CreateCompatibleBitmap(screen,16,16);
    if(dc&&bitmap)
    {
        H old=SelectObject(dc,bitmap);
        StretchDIBits(dc,0,0,16,16,0,0,16,16,pixels,&b,0,0xcc0020);
        SelectObject(dc,old);
    }
    if(dc)DeleteDC(dc);ReleaseDC(0,screen);
    return bitmap;
}
static void set_glyph(H button,U kind){SendMessageA(button,0xf7,0,(I)glyphs[kind]);}   /* BM_SETIMAGE, IMAGE_BITMAP */

/* The timeline goes where the user put it; Kodi's position moves it again
   once it has arrived (or after a few seconds). */
static void seek_to(U seconds)
{
    if(seconds==seek_target&&GetTickCount()<seek_hold)return;
    seek_target=seconds;seek_hold=GetTickCount()+4000;
    action(HM_SEEK_TO,"",(I)seconds);
}
static void seek_text(U seconds)
{
    char text[64]="";clock_text(text,seconds,sizeof text);cat(text," / ",sizeof text);clock_text(text,status.total,sizeof text);
    set_text(now_text,text);
}
static void seek_scroll(U code)
{
    if(!seek_max)return;
    U pos=(U)SendMessageA(seek_bar,0x400,0,0);   /* TBM_GETPOS */
    if(code==2||code==3)
    {
        /* A click on the channel goes to the place clicked, not one page. */
        I point[2];GetCursorPos(point);ScreenToClient(seek_bar,point);
        RECT channel;SendMessageA(seek_bar,0x41a,0,(I)&channel);   /* TBM_GETCHANNELRECT */
        const I width=channel.right-channel.left;
        if(width>0)
        {
            I x=point[0]-channel.left;if(x<0)x=0;if(x>width)x=width;
            pos=(U)x*seek_max/(U)width;   /* fits: a few hundred pixels times hours of seconds */
            SendMessageA(seek_bar,0x405,1,(I)pos);   /* TBM_SETPOS */
        }
        seek_to(pos);seek_text(pos);return;
    }
    if(code==5){seek_dragging=1;seek_text(pos);return;}   /* TB_THUMBTRACK */
    if(code==8){seek_dragging=0;seek_to(pos);seek_text(pos);}   /* TB_ENDTRACK */
}

/* The volume follows the slider while it moves; a click on it goes there. */
static void volume_scroll(U code)
{
    U volume=(U)SendMessageA(volume_bar,0x400,0,0);
    if(code==2||code==3)
    {
        I point[2];GetCursorPos(point);ScreenToClient(volume_bar,point);
        RECT channel;SendMessageA(volume_bar,0x41a,0,(I)&channel);
        const I width=channel.right-channel.left;
        if(width>0){I x=point[0]-channel.left;if(x<0)x=0;if(x>width)x=width;volume=(U)x*100/(U)width;SendMessageA(volume_bar,0x405,1,(I)volume);}
    }
    volume_dragging=code==5;
    if(volume!=shown_volume){shown_volume=volume;action(HM_VOLUME,"",(I)volume);}
}

/* Media Center's full screen, only while the video plays in the window:
   Esc or a double click goes back, Space pauses, the arrows go 10 seconds. */
static void report_area(void);
static void set_full(U on)
{
    if(on==full_on)return;
    full_on=on;
    if(on)
    {
        full=CreateWindowExA(8,"NEO98MediaVideo",TITLE,0x90000000u,0,0,GetSystemMetrics(0),GetSystemMetrics(1),window,0,instance,0);
        SetForegroundWindow(full);SetFocus(full);
    }
    else if(full){H gone=full;full=0;DestroyWindow(gone);SetForegroundWindow(window);}
    action(HM_FULLSCREEN,"",(I)on);
    report_area();
}

/* The animation while the video opens, in the middle of the area. */
static void place_animation(void)
{
    RECT r;GetClientRect(video,&r);
    MoveWindow(animation,0,r.bottom/2-60,r.right,80,1);
}
static void show_opening(U on)
{
    if(on==video_opening)return;
    video_opening=on;
    if(on)
    {
        static H shdocvw;if(!shdocvw)shdocvw=LoadLibraryA("SHDOCVW.DLL");
        place_animation();
        if(shdocvw)SendMessageA(animation,0x464,(U)shdocvw,256);   /* ACM_OPENA, plays by itself (ACS_AUTOPLAY) */
        ShowWindow(animation,5);
    }
    else{ShowWindow(animation,0);SendMessageA(animation,0x464,0,0);}
    InvalidateRect(video,0,1);
}
static void update_player(void)
{
    const U playing=status.player!=HM_STOPPED;
    if(playing!=bar_shown)
    {
        bar_shown=playing;ShowWindow(now_text,playing?5:0);ShowWindow(seek_bar,playing?5:0);
        ShowWindow(mute_button,playing?5:0);ShowWindow(volume_bar,playing?5:0);
        for(U i=0;i<5;++i)ShowWindow(bar_buttons[i],playing?5:0);
        layout();
        seek_max=0;seek_target=~0u;
    }
    char text[HM_LABEL+64]="";
    if(playing)
    {
        const U want=status.player==HM_PAUSED?GLYPH_PLAY:GLYPH_PAUSE;
        if(want!=shown_glyph){shown_glyph=want;set_glyph(bar_buttons[0],want);}
        if(status.total!=seek_max){seek_max=status.total;SendMessageA(seek_bar,0x408,1,(I)seek_max);SendMessageA(seek_bar,0x415,0,(I)(seek_max/20+1));}
        EnableWindow(seek_bar,seek_max>0);
        if(status.muted!=shown_muted){shown_muted=status.muted;set_glyph(mute_button,status.muted?GLYPH_MUTED:GLYPH_SPEAKER);}
        if(status.volume!=shown_volume&&!volume_dragging){shown_volume=status.volume;SendMessageA(volume_bar,0x405,1,(I)status.volume);}
        /* Kodi's position, unless the user holds the timeline or a jump is still on its way. */
        const I off=(I)status.time-(I)seek_target;
        if(!seek_dragging&&(GetTickCount()>=seek_hold||(off<3&&off>-3)))
        {
            seek_hold=0;SendMessageA(seek_bar,0x405,1,(I)status.time);
            if(status.total){clock_text(text,status.time,sizeof text);cat(text," / ",sizeof text);clock_text(text,status.total,sizeof text);}
            set_text(now_text,text);text[0]=0;
        }
        cat(text,status.player==HM_PAUSED?"Paused: ":status.player==HM_OPENING?"Opening: ":"Playing: ",sizeof text);
        cat(text,ansi(status.title,text_buffer,sizeof text_buffer),sizeof text);
    }
    else if(status.message[0])copy(text,ansi(status.message,text_buffer,sizeof text_buffer),sizeof text);
    else if(status.state==HM_READY){cat(text,"Connected to ",sizeof text);cat(text,status.kodi,sizeof text);}
    set_status(1,text);
    for(U i=MENU_PLAYPAUSE;i<=MENU_SHOWVIDEO;++i)enable(i,playing);
    now_playing(playing);
    if(!playing||status.screen!=HM_SCREEN_WINDOW||browsing)set_full(0);
    show_video(playing&&status.screen==HM_SCREEN_WINDOW&&!browsing);
    show_opening(video_shown&&status.player==HM_OPENING);
    if(video_opening)InvalidateRect(video,0,1);
}

/* Kodi's state: missing, starting, ready, gone. */
static void update_state(void)
{
    if(status.state==shown_state)return;
    shown_state=status.state;
    if(status.state==HM_NO_KODI||status.state==HM_FAILED){explain(status.message,1);set_status(0,"");return;}
    if(status.state==HM_STARTING||status.state==HM_IDLE){explain("Starting Kodi...",0);set_status(0,"Starting Kodi...");return;}
    if(status.state==HM_READY)
    {
        show_list();
        /* The catalogue is new: read the open folder again. */
        if(current==~0u)current=0;
        open_node(current);
    }
}

static void show_dialog(void);
static void tick(void)
{
    read_status();
    update_state();
    if(status.generation!=shown_generation||status.player!=shown_player||status.screen!=shown_screen)
    {
        shown_generation=status.generation;shown_player=status.player;shown_screen=status.screen;
        update_player();
    }
    report_area();
    if(status.state!=HM_READY)return;
    load_current();
    load_more();
    load_pending();
    load_pictures();
    show_dialog();
}

/* Kodi's dialog as a Windows dialog. The host reads it from Kodi; this
   window closes by itself when Kodi closes the dialog. */
enum { ID_TEXT=200, ID_CHOICES=201, ID_INPUT=202, ID_PROGRESS=203, ID_NO=204, ID_THIRD=205 };
enum { DIALOG_W=380 };
static HM_Dialog *kodi_dialog;static U dialog_open,dialog_done;
static char dialog_text[HM_TEXT],dialog_title[HM_LABEL];
static U dialog_percent;
/* The text of the user (ANSI) for the host (UTF-8). */
static void utf8(S text,char *out,U capacity)
{
    unsigned short wide[HM_PATH];
    I n=MultiByteToWideChar(0,0,text,-1,wide,HM_PATH);
    if(n<=0||!WideCharToMultiByte(65001,0,wide,-1,out,(I)capacity,0,0))copy(out,text,capacity);
    memset(wide,0,sizeof wide);
}
static void answer(U serial,I value,S text)
{
    prepare(HM_ANSWER,"");request.first=serial;request.value=value;
    if(text)utf8(text,request.path,sizeof request.path);
    control_io(HM_IOCTL_CONTROL,&request,sizeof request,0,0);
    memset(request.path,0,sizeof request.path);
}
static U read_dialog(void){if(!control_io(HM_IOCTL_DIALOG,0,0,kodi_dialog,sizeof *kodi_dialog))memset(kodi_dialog,0,sizeof *kodi_dialog);return kodi_dialog->serial;}
static H dialog_child(H dialog,S klass,S title,U style,U extended,U id,I x,I y,I w,I h)
{
    H c=CreateWindowExA(extended,klass,title,0x50000000u|style,x,y,w,h,dialog,(H)id,instance,0);
    SendMessageA(c,0x30,(U)GetStockObject(17),0);return c;
}
/* The height the dialog's text needs at the width of the dialog. */
static I text_height(S text)
{
    if(!text[0])return 0;
    H dc=GetDC(0);H old=SelectObject(dc,GetStockObject(17));
    RECT r={0,0,DIALOG_W-24,0};DrawTextA(dc,text,-1,&r,0x400|0x10|0x800);   /* DT_CALCRECT, DT_WORDBREAK, DT_NOPREFIX */
    SelectObject(dc,old);ReleaseDC(0,dc);
    return r.bottom>320?320:r.bottom;
}
static void dialog_texts(void)
{
    ansi(kodi_dialog->text[0]?kodi_dialog->text:kodi_dialog->kind==HM_DIALOG_INPUT?kodi_dialog->heading:"",
         dialog_text,sizeof dialog_text);
    ansi(kodi_dialog->heading[0]?kodi_dialog->heading:"Kodi",dialog_title,sizeof dialog_title);
}
static I __stdcall dialog_proc(H dialog,U msg,U wp,I lp)
{
    const U kind=kodi_dialog->kind,serial=kodi_dialog->serial;
    switch(msg)
    {
    case 0x110: /* WM_INITDIALOG */
    {
        SetWindowTextA(dialog,dialog_title);
        const I th=text_height(dialog_text);
        I y=12;
        if(th){dialog_child(dialog,"STATIC",dialog_text,0x80,0,ID_TEXT,12,y,DIALOG_W-24,th);y+=th+10;}
        H focus=0;
        if(kind==HM_DIALOG_SELECT)
        {
            H l=dialog_child(dialog,"LISTBOX","",0x10000u|0x200000u|1,0x200,ID_CHOICES,12,y,DIALOG_W-24,140);
            for(U i=0;i<kodi_dialog->count;++i)SendMessageA(l,0x180,0,(I)ansi(kodi_dialog->choice[i],text_buffer,sizeof text_buffer));
            SendMessageA(l,0x186,0,0);focus=l;y+=140+10;
        }
        else if(kind==HM_DIALOG_INPUT)
        {
            U style=0x10000u|0x80u;
            if(kodi_dialog->flags&HM_DIALOG_HIDDEN)style|=0x20u;     /* ES_PASSWORD */
            if(kodi_dialog->flags&HM_DIALOG_NUMBER)style|=0x2000u;   /* ES_NUMBER */
            H e=dialog_child(dialog,"EDIT",ansi(kodi_dialog->input,text_buffer,sizeof text_buffer),style,0x200,ID_INPUT,12,y,DIALOG_W-24,22);
            SendMessageA(e,0xc5,HM_PATH/2,0);SendMessageA(e,0xb1,0,-1);focus=e;y+=22+10;
        }
        else if(kind==HM_DIALOG_PROGRESS)
        {
            H b=dialog_child(dialog,"msctls_progress32","",0x800000u,0,ID_PROGRESS,12,y,DIALOG_W-24,18);
            SendMessageA(b,0x402,kodi_dialog->percent,0);dialog_percent=kodi_dialog->percent;y+=18+10;
        }
        /* The buttons at the right, the first the default. */
        S labels[3]={0,0,0};U ids[3]={0,0,0};U count=0;
        if(kind==HM_DIALOG_OK){labels[0]=kodi_dialog->button[0][0]?kodi_dialog->button[0]:"OK";ids[0]=1;count=1;}
        else if(kind==HM_DIALOG_YESNO)
        {
            labels[0]=kodi_dialog->button[1][0]?kodi_dialog->button[1]:"&Yes";ids[0]=1;
            labels[1]=kodi_dialog->button[0][0]?kodi_dialog->button[0]:"&No";ids[1]=ID_NO;count=2;
            if(kodi_dialog->button[2][0]){labels[2]=kodi_dialog->button[2];ids[2]=ID_THIRD;count=3;}
        }
        else if(kind==HM_DIALOG_SELECT||kind==HM_DIALOG_INPUT){labels[0]="OK";ids[0]=1;labels[1]="Cancel";ids[1]=2;count=2;}
        else if(kodi_dialog->flags&HM_DIALOG_CANCEL){labels[0]="Cancel";ids[0]=2;count=1;}
        y+=4;
        for(U i=0;i<count;++i)
        {
            char label[HM_BUTTON];ansi(labels[i],label,sizeof label);
            H b=dialog_child(dialog,"BUTTON",label,0x10000u|(i==0&&ids[i]==1?1u:0u),0,ids[i],DIALOG_W-12-(I)(count-i)*87+12,y,75,23);
            if(!focus&&i==0)focus=b;
        }
        if(count)y+=23;
        y+=12;
        RECT r={0,0,DIALOG_W,y};AdjustWindowRectEx(&r,0x80c80080u,0,1);
        RECT owner;GetWindowRect(window,&owner);
        const I w=r.right-r.left,h=r.bottom-r.top;
        SetWindowPos(dialog,0,owner.left+(owner.right-owner.left-w)/2,owner.top+(owner.bottom-owner.top-h)/2,w,h,0x14);
        SetTimer(dialog,1,200,0);
        if(focus){SetFocus(focus);return 0;}
        return 1;
    }
    case 0x113: /* WM_TIMER: Kodi closed the dialog, or the progress moved */
    {
        read_status();
        if(status.dialog!=serial){KillTimer(dialog,1);EndDialog(dialog,0);return 1;}
        if(kind==HM_DIALOG_PROGRESS&&read_dialog()==serial)
        {
            dialog_texts();
            char now[HM_TEXT];GetWindowTextA(GetDlgItem(dialog,ID_TEXT),now,sizeof now);
            if(!same(now,dialog_text))SetWindowTextA(GetDlgItem(dialog,ID_TEXT),dialog_text);
            if(kodi_dialog->percent!=dialog_percent){dialog_percent=kodi_dialog->percent;SendMessageA(GetDlgItem(dialog,ID_PROGRESS),0x402,dialog_percent,0);}
        }
        return 1;
    }
    case 0x111: /* WM_COMMAND */
    {
        const U id=wp&65535,code=wp>>16;
        if(id==ID_CHOICES&&code==2){dialog_proc(dialog,0x111,1,0);return 1;}  /* LBN_DBLCLK */
        I value;char text[HM_PATH];text[0]=0;
        if(id==1)
        {
            if(kind==HM_DIALOG_SELECT){value=SendMessageA(GetDlgItem(dialog,ID_CHOICES),0x188,0,0);if(value<0)return 1;}
            else if(kind==HM_DIALOG_INPUT){GetWindowTextA(GetDlgItem(dialog,ID_INPUT),text,sizeof text);value=0;}
            else value=kind==HM_DIALOG_YESNO?1:0;
        }
        else if(id==ID_NO)value=0;
        else if(id==ID_THIRD)value=2;
        else if(id==2)value=HM_ANSWER_CANCEL;
        else return 0;
        KillTimer(dialog,1);
        answer(serial,value,kind==HM_DIALOG_INPUT&&value==0?text:0);
        memset(text,0,sizeof text);
        if(GetDlgItem(dialog,ID_INPUT))SetWindowTextA(GetDlgItem(dialog,ID_INPUT),"");
        EndDialog(dialog,1);
        return 1;
    }
    }
    return 0;
}
/* A new dialog of Kodi: shown until it is answered or Kodi closes it. The
   catalogue goes on loading meanwhile (the window's timer runs). */
static void show_dialog(void)
{
    if(dialog_open||!status.dialog||status.dialog==dialog_done)return;
    if(read_dialog()!=status.dialog||kodi_dialog->kind==HM_DIALOG_NONE)return;
    dialog_open=1;dialog_done=kodi_dialog->serial;
    dialog_texts();
    /* An empty template: DS_MODALFRAME, WS_POPUP, WS_CAPTION, WS_SYSMENU;
       the children come in WM_INITDIALOG. */
    static U dialog_template[6]={0x80c80080u};
    DialogBoxIndirectParamA(instance,dialog_template,window,(PROC)dialog_proc,0);
    dialog_open=0;
}

static void streaming_setup(void)
{
    char text[1400]="";
    read_status();
    if(status.state!=HM_READY){MessageBoxA(window,status.message[0]?ansi(status.message,text_buffer,sizeof text_buffer):"Kodi is not connected.",TITLE,0x40);return;}
    cat(text,"Media Center 98 shows the catalogue of ",sizeof text);cat(text,status.kodi,sizeof text);
    cat(text," on this computer. Videos play in Kodi.\r\n\r\n",sizeof text);
    cat(text,status.features&HM_HAS_NETFLIX?"Netflix add-on: installed\r\n":"Netflix add-on: not installed\r\n",sizeof text);
    cat(text,status.features&HM_HAS_INPUTSTREAM?"InputStream Adaptive: installed\r\n":"InputStream Adaptive: not installed\r\n",sizeof text);
    cat(text,status.features&HM_HAS_HELPER?"InputStream Helper: installed\r\n":"InputStream Helper: not installed\r\n",sizeof text);
    cat(text,status.features&HM_HAS_WIDEVINE?"Widevine: installed\r\n":"Widevine: not installed\r\n",sizeof text);
    cat(text,status.features&HM_HAS_DIALOGS?"Kodi's dialogs in this window: yes\r\n":"Kodi's dialogs in this window: no (NEO98's add-on service.neo98 does not run in Kodi)\r\n",sizeof text);
    if((status.features&15)!=15)
        cat(text,"\r\nNEO98 includes none of these. To watch Netflix, install in Kodi the repository of CastagnaIT "
                 "(github.com/CastagnaIT/plugin.video.netflix) and from it the add-on Netflix. Kodi then offers to install "
                 "InputStream Adaptive and, through InputStream Helper, Widevine. Sign in to Netflix in Kodi.",sizeof text);
    MessageBoxA(window,text,"Streaming Setup",0x40);
}

static void command(U id)
{
    switch(id)
    {
    case MENU_OPEN:activate(selected_item());break;
    case MENU_EXIT:DestroyWindow(window);break;
    case MENU_REFRESH:case ID_RETRY:
        if(status.state==HM_NO_KODI||status.state==HM_FAILED){action(HM_START,"",0);shown_state=~0u;break;}
        action(HM_REFRESH,"",0);
        for(U i=0;i<node_count;++i)nodes[i].filled=0;
        if(current!=~0u){show_list();open_node(current);}
        break;
    case MENU_UP:if(current!=~0u&&nodes[current].parent!=~0u)select_node(nodes[current].parent);break;
    case MENU_PLAYPAUSE:case ID_PLAYPAUSE:action(HM_PAUSE,"",0);break;
    case MENU_STOP:case ID_STOP:action(HM_STOP,"",0);break;
    case ID_MUTE:action(HM_MUTE,"",!status.muted);break;
    case MENU_BACK30:case ID_BACK30:action(HM_SEEK,"",-30);break;
    case MENU_FORWARD30:case ID_FORWARD30:action(HM_SEEK,"",30);break;
    case MENU_SHOWVIDEO:case ID_SHOWVIDEO:
        /* Browsing: back to the video first. */
        if(browsing&&now_item){SendMessageA(tree,TVM_SELECTITEM,9,(I)now_item);break;}
        /* The video in the window: Media Center's own full screen, Kodi stays hidden. */
        if(status.screen==HM_SCREEN_WINDOW||full_on){set_full(!full_on);break;}
        /* Back to the video: Kodi takes the screen and a paused video goes on. */
        action(HM_SHOW_VIDEO,"",0);if(status.player==HM_PAUSED)action(HM_PAUSE,"",0);break;
    case MENU_SETUP:streaming_setup();break;
    case MENU_ABOUT:ShellAboutA(window,TITLE,"Shows the catalogue of Kodi on the host. Videos play in Kodi.",icon);break;
    }
}

static I notify(NMHDR *n)
{
    if(n->hwnd==tree)
    {
        NMTREEVIEW *t=(NMTREEVIEW*)n;
        if(n->code==-402&&t->new_item.param==NOW_PARAM){browsing=0;update_player();return 0;}
        if(n->code==-402&&t->new_item.param<node_count)          /* TVN_SELCHANGEDA */
        {
            /* A folder while a video plays: the folder shows, the video waits under Now Playing. */
            if(now_item&&!browsing){browsing=1;update_player();}
            const U node=t->new_item.param;
            if(node!=current||failed){show_list();open_node(node);}
        }
        else if(n->code==-405&&t->action==2&&t->new_item.param<node_count) /* TVN_ITEMEXPANDINGA */
        {
            const U node=t->new_item.param;
            if(!nodes[node].filled&&node!=current&&pending_count<16)
            {
                U queued=0;for(U i=0;i<pending_count;++i)queued|=pending_fill[i]==node;
                if(!queued)pending_fill[pending_count++]=node;
            }
        }
        return 0;
    }
    if(n->hwnd==list)
    {
        /* LVN_ITEMCHANGED to selected: the host looks up the video ahead. */
        if(n->code==-101)
        {
            const NMLISTVIEW *v=(const NMLISTVIEW*)n;
            if((v->new_state&2)&&!(v->old_state&2)&&v->item>=0&&(U)v->item<item_count&&(items[v->item].flags&HM_ENTRY_PLAYABLE))
                action(HM_PREPARE,items[v->item].path,0);
        }
        if(n->code==-3||n->code==-4)activate(selected_item());   /* NM_DBLCLK, NM_RETURN */
        else if(n->code==-155&&((NMKEY*)n)->key==8)command(MENU_UP);  /* LVN_KEYDOWN, Backspace */
    }
    return 0;
}

static I __stdcall proc(H hwnd,U msg,U wp,I lp)
{
    switch(msg)
    {
    case 0x05:if(window){layout();report_area();}return 0;
    case 0x03:if(window)report_area();return 0;
    case 0x113:tick();return 0;
    case 0x4e:return notify((NMHDR*)lp);
    case 0x111:command(wp&65535);return 0;
    case 0x114:   /* WM_HSCROLL of the timeline or the volume */
        if((H)lp==seek_bar)seek_scroll(wp&65535);
        else if((H)lp==volume_bar)volume_scroll(wp&65535);
        return 0;
    case 0x02:KillTimer(hwnd,1);PostQuitMessage(0);return 0;
    }
    return DefWindowProcA(hwnd,msg,wp,lp);
}

/* The video area: the key colour, which the host fills with Kodi's picture;
   while the video opens, the animation and its title on the dialog colour.
   A double click gives Kodi the whole screen. */
static I __stdcall video_proc(H hwnd,U msg,U wp,I lp)
{
    if(msg==0x0f)
    {
        PAINT p;H dc=BeginPaint(hwnd,&p);RECT r;GetClientRect(hwnd,&r);
        if(video_opening)
        {
            FillRect(dc,&r,face_brush);
            char text[HM_LABEL+32]="Opening ";cat(text,status.title[0]?ansi(status.title,text_buffer,sizeof text_buffer):"the video",sizeof text);cat(text,"...",sizeof text);
            H old=SelectObject(dc,GetStockObject(17));SetBkMode(dc,1);SetTextColor(dc,GetSysColor(18));
            RECT t={12,r.bottom/2+28,r.right-12,r.bottom/2+60};DrawTextA(dc,text,-1,&t,1|0x20|0x800|0x8000);  /* centred, one line, no prefix, ellipsis */
            SelectObject(dc,old);
        }
        else FillRect(dc,&r,key_brush);
        EndPaint(hwnd,&p);return 0;
    }
    if(msg==0x14)return 1;
    if(msg==0x05){if(animation)place_animation();return 0;}
    if(msg==0x138){SetBkColor((H)wp,GetSysColor(15));return (I)face_brush;}   /* WM_CTLCOLORSTATIC of the animation */
    if(msg==0x203){command(MENU_SHOWVIDEO);return 0;}
    if(msg==0x100&&hwnd==full)
    {
        if(wp==0x1b)set_full(0);
        else if(wp==0x26||wp==0x28){I v=(I)status.volume+(wp==0x26?5:-5);action(HM_VOLUME,"",v<0?0:v>100?100:v);}
        else if(wp=='M')action(HM_MUTE,"",!status.muted);
        else if(wp==0x20)action(HM_PAUSE,"",0);
        else if(wp==0x25||wp==0x27)action(HM_SEEK,"",wp==0x25?-10:10);
        return 0;
    }
    return DefWindowProcA(hwnd,msg,wp,lp);
}
static H child(S klass,S title,U style,U extended,U id)
{
    H h=CreateWindowExA(extended,klass,title,0x50000000u|style,0,0,10,10,window,(H)id,instance,0);
    SendMessageA(h,0x30,(U)GetStockObject(17),0);return h;
}

void mainCRTStartup(void)
{
    instance=GetModuleHandleA(0);icon=LoadIconA(instance,(S)1);
    InitCommonControls();
    nodes=VirtualAlloc(0,sizeof(NODE)*MAX_NODES,0x3000,4);items=VirtualAlloc(0,sizeof(HM_Entry)*MAX_ITEMS,0x3000,4);
    listing=VirtualAlloc(0,sizeof *listing,0x3000,4);picture=VirtualAlloc(0,sizeof *picture,0x3000,4);
    kodi_dialog=VirtualAlloc(0,sizeof *kodi_dialog,0x3000,4);
    device=CreateFileA("\\\\.\\NEO98MEDIA",0,0,0,3,0,0);
    if(device==(H)~0u){MessageBoxA(0,"The NEO98 host media integration is not available.",TITLE,0x10);ExitProcess(1);}
    CLASS c={0,proc,0,0,instance,icon,LoadCursorA(0,(S)32512),(H)16,0,CLASS_NAME};RegisterClassA(&c);
    key_brush=CreateSolidBrush((HM_VIDEO_KEY&0xff00)|(HM_VIDEO_KEY>>16&255)|(HM_VIDEO_KEY&255)<<16);
    CLASS vc={8,video_proc,0,0,instance,0,LoadCursorA(0,(S)32512),key_brush,0,"NEO98MediaVideo"};RegisterClassA(&vc);
    H menu=CreateMenu(),file=CreatePopupMenu(),view=CreatePopupMenu(),play=CreatePopupMenu(),help=CreatePopupMenu();
    AppendMenuA(file,0,MENU_OPEN,"&Open\tEnter");AppendMenuA(file,0x800,0,0);AppendMenuA(file,0,MENU_EXIT,"E&xit");
    AppendMenuA(view,0,MENU_UP,"&Up One Level\tBackspace");AppendMenuA(view,0x800,0,0);AppendMenuA(view,0,MENU_REFRESH,"&Refresh\tF5");
    AppendMenuA(play,0,MENU_PLAYPAUSE,"&Play/Pause\tCtrl+P");AppendMenuA(play,0,MENU_STOP,"&Stop\tCtrl+S");AppendMenuA(play,0x800,0,0);
    AppendMenuA(play,0,MENU_BACK30,"&Back 30 Seconds\tCtrl+Left");AppendMenuA(play,0,MENU_FORWARD30,"&Forward 30 Seconds\tCtrl+Right");
    AppendMenuA(play,0x800,0,0);AppendMenuA(play,0,MENU_SHOWVIDEO,"Show &Video\tCtrl+Enter");
    AppendMenuA(help,0,MENU_SETUP,"&Streaming Setup...");AppendMenuA(help,0x800,0,0);AppendMenuA(help,0,MENU_ABOUT,"&About " TITLE);
    AppendMenuA(menu,0x10,(U)file,"&File");AppendMenuA(menu,0x10,(U)view,"&View");AppendMenuA(menu,0x10,(U)play,"&Play");AppendMenuA(menu,0x10,(U)help,"&Help");
    /* F5, Ctrl+P, Ctrl+S, Ctrl+Left, Ctrl+Right, Ctrl+Enter (FVIRTKEY 1, FCONTROL 8). */
    ACCEL keys[6]={{1,0x74,MENU_REFRESH},{9,'P',MENU_PLAYPAUSE},{9,'S',MENU_STOP},{9,0x25,MENU_BACK30},{9,0x27,MENU_FORWARD30},{9,0x0d,MENU_SHOWVIDEO}};
    H accelerators=CreateAcceleratorTableA(keys,6);
    window=CreateWindowExA(0,CLASS_NAME,TITLE,0x00cf0000u|0x02000000u,(I)0x80000000,(I)0x80000000,720,520,0,menu,instance,0);
    tree=child("SysTreeView32","",0x00800000u|1|2|4|0x20,0x200,ID_TREE);
    list=child("SysListView32","",4|8|0x100,0x200,ID_LIST);
    message=child("STATIC","",0,0,ID_MESSAGE);ShowWindow(message,0);
    retry=child("BUTTON","&Retry",0x10000,0,ID_RETRY);ShowWindow(retry,0);
    video=CreateWindowExA(0x200,"NEO98MediaVideo","",0x40000000u,0,0,10,10,window,(H)ID_VIDEO,instance,0);
    face_brush=CreateSolidBrush(GetSysColor(15));
    /* ACS_CENTER, ACS_TRANSPARENT, ACS_AUTOPLAY */
    animation=CreateWindowExA(0,"SysAnimate32","",0x40000000u|1|2|4,0,0,10,10,video,0,instance,0);
    status_bar=child("msctls_statusbar32","",0x100,0,ID_STATUS);
    /* The bar has no window of its own: one under the buttons would paint over them. */
    {
        /* Buttons with pictures (BS_BITMAP): pause, stop, back, forward, full screen. */
        for(U i=0;i<GLYPHS;++i)glyphs[i]=glyph(i);
        const U kinds[5]={GLYPH_PAUSE,GLYPH_STOP,GLYPH_BACK,GLYPH_FORWARD,GLYPH_FULL};
        for(U i=0;i<5;++i){bar_buttons[i]=CreateWindowExA(0,"BUTTON","",0x50010080u,0,0,10,10,window,(H)(ID_PLAYPAUSE+i),instance,0);set_glyph(bar_buttons[i],kinds[i]);}
        shown_glyph=GLYPH_PAUSE;
        /* The timeline (TBS_NOTICKS). */
        seek_bar=CreateWindowExA(0,"msctls_trackbar32","",0x50010010u,0,0,10,10,window,(H)ID_SEEK,instance,0);
        mute_button=CreateWindowExA(0,"BUTTON","",0x50010080u,0,0,10,10,window,(H)ID_MUTE,instance,0);
        volume_bar=CreateWindowExA(0,"msctls_trackbar32","",0x50010010u,0,0,10,10,window,(H)ID_VOLUME,instance,0);
        SendMessageA(volume_bar,0x408,0,100);SendMessageA(volume_bar,0x415,0,10);   /* 0-100, a page of 10 */
        now_text=child("STATIC","",0x82,0,ID_NOW+100);  /* SS_RIGHT, SS_NOPREFIX */
        ShowWindow(now_text,0);ShowWindow(seek_bar,0);ShowWindow(mute_button,0);ShowWindow(volume_bar,0);for(U i=0;i<5;++i)ShowWindow(bar_buttons[i],0);
    }
    /* The pictures: posters, and boxes with the folder and the video icon for
       entries without one; small folder icons in the tree. */
    posters=ImageList_Create(POSTER_W,POSTER_H,0x18,16,16);
    H folder=ExtractIconA(instance,"SHELL32.DLL",3),open=ExtractIconA(instance,"SHELL32.DLL",4);
    H video=ExtractIconA(instance,"C:\\WINDOWS\\MPLAYER.EXE",0);
    if((U)video<=1)video=ExtractIconA(instance,"SHELL32.DLL",2);
    folder_icon=folder;video_icon=video;
    folder_image=(U)placeholder((U)folder>1?folder:0);video_image=(U)placeholder((U)video>1?video:0);
    SendMessageA(list,LVM_SETIMAGELIST,0,(I)posters);
    SendMessageA(list,LVM_SETICONSPACING,0,(POSTER_W+28)|((POSTER_H+40)<<16));
    folder_icons=ImageList_Create(16,16,0x9,2,2);
    ImageList_ReplaceIcon(folder_icons,-1,folder);ImageList_ReplaceIcon(folder_icons,-1,open);
    SendMessageA(tree,TVM_SETIMAGELIST,0,(I)folder_icons);
    add_node(ROOT_PATH,ROOT_LABEL,~0u,1);
    layout();
    ShowWindow(window,5);UpdateWindow(window);
    select_node(0);
    tick();SetTimer(window,1,200,0);
    MSG m;
    while(GetMessageA(&m,0,0,0)>0)
    {
        if(TranslateAcceleratorA(window,accelerators,&m))continue;
        TranslateMessage(&m);DispatchMessageA(&m);
    }
    CloseHandle(device);
    ExitProcess(0);
}
