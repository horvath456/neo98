// SPDX-License-Identifier: GPL-2.0-or-later
// Executes the public x86 stdcall ABI, including reentrant resource callbacks.
typedef unsigned U;
typedef unsigned short W;
typedef unsigned long long Q;
#define API __declspec(dllimport) __stdcall
void API ExitProcess(U);
U API GetLastError(void);
void API SetLastError(U);
void API RtlFillMemory(void*,U,U);
void API RtlZeroMemory(void*,U);
void API GetSystemTimeAsFileTime(Q*);
void API GetSystemTime(W*);
U API SystemTimeToFileTime(const W*,Q*);
U API CreateFileA(const char*,U,U,void*,U,U,U);
U API CloseHandle(U);
U API WriteFile(U,const void*,U,U*,void*);
U API ReadFile(U,void*,U,U*,void*);
U API SetFileTime(U,const Q*,const Q*,const Q*);
U API GetFileInformationByHandle(U,void*);
U API GetFileAttributesExA(const char*,U,void*);
U API GetFileAttributesA(const char*);
U API SetFileAttributesA(const char*,U);
U API CopyFileA(const char*,const char*,U);
U API CreateDirectoryExA(const char*,const char*,void*);
U API RemoveDirectoryA(const char*);
U API DeleteFileA(const char*);
U API MoveFileA(const char*,const char*);
U API FindResourceA(U,const char*,const char*);
U API FindResourceW(U,const W*,const W*);
U API FindResourceExW(U,const W*,const W*,U);
U API SizeofResource(U,U);
U API LoadResource(U,U);
U API LockResource(U);
U API EnumResourceTypesA(U,void*,U);
U API EnumResourceTypesW(U,void*,U);
U API EnumResourceNamesA(U,const char*,void*,U);
U API EnumResourceNamesW(U,const W*,void*,U);
U API CreateCompatibleDC(U);
U API CreateBitmap(U,U,U,U,void*);
U API CreateSolidBrush(U);
U API CreatePatternBrush(U);
U API SelectObject(U,U);
U API GetCurrentObject(U,U);
U API DeleteObject(U);
U API DeleteDC(U);
U API PatBlt(U,int,int,int,int,U);
U API GetPixel(U,int,int);
U API FrameRect(U,const int*,U);
U API InvertRect(U,const int*);
U API GetBrushOrgEx(U,int*);
U API SetBrushOrgEx(U,int,int,int*);
U API SaveDC(U);
U API RestoreDC(U,int);
U API IntersectClipRect(U,int,int,int,int);
U API SetViewportOrgEx(U,int,int,int*);
#define CHECK(x) do {if(!(x))ExitProcess(__LINE__);}while(0)
#define ID(x) ((const char*)(x))
static unsigned char bytes[6004], readback[6004];
static int equal(const char*a,const char*b){while(*a&&*a==*b)++a,++b;return *a==*b;}
static U types,names,stops;
static U __stdcall name_a(U module,const char*type,char*name,U param){
 CHECK(!module&&param==77);++names;
 U resource=FindResourceA(module,name,type);CHECK(resource&&SizeofResource(module,resource)==4);
 CHECK(*(const W*)LockResource(LoadResource(module,resource))==0x1234);
 if((U)name>=65536){CHECK(equal(name,"ALPHA"));name[0]='!';}
 else CHECK((U)name==17);
 return 1;
}
static U __stdcall type_a(U module,char*type,U param){
 CHECK(!module&&param==91);++types;
 CHECK((U)type==10||((U)type>=65536&&equal(type,"CUSTOM")));
 CHECK(EnumResourceNamesA(module,type,name_a,77));
 return 1;
}
static U __stdcall name_w(U module,const W*type,W*name,U param){
 CHECK(!module&&param==78);++names;
 U resource=FindResourceW(module,name,type);CHECK(resource&&SizeofResource(module,resource)==4);
 CHECK(FindResourceExW(module,type,name,0x409)==resource);
 if((U)name>=65536){CHECK(name[0]=='A'&&name[4]=='A'&&!name[5]);name[0]='!';}
 else CHECK((U)name==17);
 return 1;
}
static U __stdcall type_w(U module,W*type,U param){
 CHECK(!module&&param==92);++types;
 CHECK((U)type==10||((U)type>=65536&&type[0]=='C'&&type[5]=='M'&&!type[6]));
 CHECK(EnumResourceNamesW(module,type,name_w,78));return 1;
}
static U __stdcall stop_type(U module,void*type,U param){(void)module;(void)type;CHECK(param==93);++stops;return 0;}
static U __stdcall stop_name(U module,void*type,void*name,U param){(void)module;(void)type;(void)name;CHECK(param==94);++stops;return 0;}
void mainCRTStartup(void){
 RtlFillMemory(bytes,sizeof bytes,0x155);
 for(U i=0;i<sizeof bytes;++i)CHECK(bytes[i]==0x55);
 RtlZeroMemory(bytes+1,sizeof bytes-2);CHECK(bytes[0]==0x55&&bytes[6003]==0x55);
 for(U i=1;i<6003;++i)CHECK(!bytes[i]);
 RtlFillMemory((void*)1,0,1);RtlZeroMemory((void*)1,0);
 Q before,now,after;W time[8];GetSystemTimeAsFileTime(&before);GetSystemTime(time);CHECK(SystemTimeToFileTime(time,&now));GetSystemTimeAsFileTime(&after);CHECK(before<=now&&now<=after);
 CHECK(EnumResourceTypesA(0,type_a,91)&&types==2&&names==3);
 types=names=0;CHECK(EnumResourceTypesW(0,type_w,92)&&types==2&&names==3);
 CHECK(!EnumResourceTypesA(0,stop_type,93)&&stops==1);
 CHECK(!EnumResourceTypesW(0,stop_type,93)&&stops==2);
 CHECK(!EnumResourceNamesA(0,ID(10),stop_name,94)&&stops==3);
 CHECK(!EnumResourceNamesW(0,(const W*)10,stop_name,94)&&stops==4);
 CHECK(!EnumResourceNamesA(0,ID(123),name_a,77)&&GetLastError()==1813);
 CHECK(!EnumResourceNamesW(0,(const W*)123,name_w,78)&&GetLastError()==1813);
 CHECK(!EnumResourceTypesA(0x123456,stop_type,93)&&GetLastError()==6);
 const W overflow[]={'#','6','5','5','3','6',0};CHECK(!FindResourceW(0,overflow,(const W*)10)&&GetLastError()==87);
 const W invalid[]={'#',0};CHECK(!FindResourceW(0,invalid,(const W*)10)&&GetLastError()==87);
 const W number[]={'#','1','7',0};CHECK(FindResourceW(0,number,(const W*)10)==FindResourceA(0,ID(17),ID(10)));
 CHECK(!FindResourceExW(0,(const W*)10,(const W*)17,0x411)&&GetLastError()==1815);
 U file=CreateFileA("C:\\API-SOURCE",0xc0000000,3,0,1,0,0),done;CHECK(file!=~0u);
 for(U i=0;i<6000;++i)bytes[i]=(unsigned char)(i*17);
 CHECK(WriteFile(file,bytes,6000,&done,0)&&done==6000);
 const W oldtime[8]={1990,2,0,3,4,5,6,0};Q old;CHECK(SystemTimeToFileTime(oldtime,&old));CHECK(SetFileTime(file,&old,&old,&old));
 U info[13],other[13],attrs[9];CHECK(GetFileInformationByHandle(file,info)&&info[9]==6000&&info[10]==1&&info[12]);
 U second=CreateFileA("C:\\API-SOURCE",0x80000000,3,0,3,0,0);CHECK(second!=~0u);
 CHECK(GetFileInformationByHandle(second,other)&&other[12]==info[12]);CHECK(CloseHandle(second));CHECK(CloseHandle(file));
 CHECK(GetFileAttributesExA("C:\\API-SOURCE",0,attrs)&&attrs[8]==6000&&attrs[0]==info[0]);
 for(U i=1;i<7;++i)CHECK(attrs[i]==info[i]);
 CHECK(!GetFileAttributesExA("C:\\API-SOURCE",1,attrs)&&GetLastError()==87);
 CHECK(!GetFileAttributesExA(0,0,attrs)&&GetLastError()==3);
 CHECK(!GetFileAttributesExA("C:\\API-*",0,attrs)&&GetLastError()==123);
 CHECK(!GetFileInformationByHandle(file,other)&&GetLastError()==6);
 CHECK(CopyFileA("C:\\API-SOURCE","C:\\API-COPY",1));
 CHECK(!CopyFileA("C:\\API-SOURCE","C:\\API-COPY",1)&&GetLastError()==80);
 CHECK(!CopyFileA("C:\\API-SOURCE","c:\\api-source",0)&&GetLastError()==80);
 CHECK(GetFileAttributesExA("C:\\API-COPY",0,attrs));CHECK(attrs[5]==info[5]&&attrs[6]==info[6]);CHECK(attrs[1]!=info[1]||attrs[2]!=info[2]);
 file=CreateFileA("C:\\API-COPY",0x80000000,3,0,3,0,0);CHECK(file!=~0u);
 CHECK(ReadFile(file,readback,6004,&done,0)&&done==6000);for(U i=0;i<done;++i)CHECK(readback[i]==bytes[i]);CHECK(CloseHandle(file));
 CHECK(SetFileAttributesA("C:\\API-COPY",2));CHECK(!CopyFileA("C:\\API-SOURCE","C:\\API-COPY",0)&&GetLastError()==5);
 CHECK(SetFileAttributesA("C:\\API-COPY",0));CHECK(CopyFileA("C:\\API-SOURCE","C:\\API-COPY",0));
 file=CreateFileA("C:\\API-SOURCE",0x80000000,0,0,3,0,0);CHECK(file!=~0u);CHECK(!CopyFileA("C:\\API-SOURCE","C:\\API-LOCKED",0)&&GetLastError()==32);CHECK(CloseHandle(file));
 CHECK(SetFileAttributesA("C:\\API-SOURCE",3));CHECK(CreateDirectoryExA("C:\\API-SOURCE","C:\\API-DIR",0));CHECK(GetFileAttributesA("C:\\API-DIR")==19);
 CHECK(!CreateDirectoryExA("C:\\API-SOURCE","C:\\API-DIR",0)&&GetLastError()==183);
 CHECK(CopyFileA("C:\\API-SOURCE","C:\\API-READONLY",1));CHECK(GetFileAttributesA("C:\\API-READONLY")==3);
 CHECK(SetFileAttributesA("C:\\API-DIR",16)&&RemoveDirectoryA("C:\\API-DIR"));
 CHECK(SetFileAttributesA("C:\\API-SOURCE",0));CHECK(MoveFileA("C:\\API-SOURCE","C:\\API-RENAMED"));
 file=CreateFileA("C:\\API-RENAMED",0x80000000,3,0,3,0,0);CHECK(GetFileInformationByHandle(file,other)&&other[12]==info[12]);CHECK(CloseHandle(file));CHECK(DeleteFileA("C:\\API-RENAMED"));
 file=CreateFileA("C:\\API-RENAMED",0x80000000,3,0,1,0,0);CHECK(GetFileInformationByHandle(file,other)&&other[12]!=info[12]);CHECK(CloseHandle(file));
 U dc=CreateCompatibleDC(0),bitmap=CreateBitmap(16,16,1,32,0),brush=CreateSolidBrush(0x0000ff);CHECK(dc&&bitmap&&brush);
 U previous=SelectObject(dc,bitmap),selected=GetCurrentObject(dc,2);CHECK(previous);
 CHECK(PatBlt(dc,0,0,16,16,0x00ff0062));int rect[4]={2,2,10,10};
 CHECK(FrameRect(dc,rect,brush));CHECK(GetCurrentObject(dc,2)==selected);
 CHECK(GetPixel(dc,2,2)==0x0000ff&&GetPixel(dc,9,9)==0x0000ff&&GetPixel(dc,3,3)==0xffffff&&GetPixel(dc,10,10)==0xffffff);
 CHECK(InvertRect(dc,rect));CHECK(GetPixel(dc,2,2)==0xffff00&&GetPixel(dc,3,3)==0);CHECK(InvertRect(dc,rect));CHECK(GetPixel(dc,2,2)==0x0000ff&&GetPixel(dc,3,3)==0xffffff);
 int point[2];CHECK(GetBrushOrgEx(dc,point)&&!point[0]&&!point[1]);CHECK(SetBrushOrgEx(dc,7,-3,0));CHECK(SaveDC(dc));CHECK(SetBrushOrgEx(dc,99,99,0));CHECK(RestoreDC(dc,-1));CHECK(GetBrushOrgEx(dc,point)&&point[0]==7&&point[1]==-3);CHECK(!GetBrushOrgEx(0,point));
 CHECK(SaveDC(dc));CHECK(IntersectClipRect(dc,4,4,7,7));CHECK(InvertRect(dc,rect));CHECK(RestoreDC(dc,-1));CHECK(GetPixel(dc,3,3)==0xffffff&&GetPixel(dc,4,4)==0);
 CHECK(PatBlt(dc,0,0,16,16,0x00ff0062));CHECK(SetViewportOrgEx(dc,2,1,0));CHECK(FrameRect(dc,rect,brush));CHECK(GetPixel(dc,2,2)==0x0000ff&&GetPixel(dc,3,3)==0xffffff);
 CHECK(SetViewportOrgEx(dc,0,0,0));CHECK(SetBrushOrgEx(dc,65535,65536,0));CHECK(GetBrushOrgEx(dc,point)&&point[0]==-1&&!point[1]);
 static unsigned char tile[16]={0xaa,0,0x55,0,0xaa,0,0x55,0,0xaa,0,0x55,0,0xaa,0,0x55,0};
 U mono=CreateBitmap(8,8,1,1,tile),pattern=CreatePatternBrush(mono);CHECK(mono&&pattern);CHECK(DeleteObject(mono));
 CHECK(SelectObject(dc,pattern));CHECK(SetBrushOrgEx(dc,0,0,0));CHECK(PatBlt(dc,0,0,16,16,0x00f00021));
 U a=GetPixel(dc,0,0),b=GetPixel(dc,1,0);CHECK(a!=b);
 CHECK(SetBrushOrgEx(dc,1,0,0));CHECK(PatBlt(dc,0,0,16,16,0x00f00021));CHECK(GetPixel(dc,0,0)==b&&GetPixel(dc,1,0)==a);
 CHECK(SelectObject(dc,selected));CHECK(DeleteObject(pattern));
 CHECK(SelectObject(dc,previous));CHECK(DeleteObject(bitmap)&&DeleteObject(brush)&&DeleteDC(dc));
 ExitProcess(0);
}
