/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Reuse the existing Glide window, binding and rectangle helpers. */
#define mainCRTStartup baseline_mainCRTStartup
#include "glide_fixture.c"
#undef mainCRTStartup
API U CreateFileA(const char *, U, U, void *, U, U, U);
API U WriteFile(U, const void *, U, U *, void *);
API U CloseHandle(U);
#define UTILITY_FUNCTIONS(X) \
 X(U, guTexAllocateMemory, (int,U,int,int,int,int,int,int,int,int,int,int,int,float,int), "_guTexAllocateMemory@60") \
 X(int, guTexChangeAttributes, (U,int,int,int,int,int,int,int,int,int,int,int), "_guTexChangeAttributes@48") \
 X(void, guTexDownloadMipMap, (U,void *,void *), "_guTexDownloadMipMap@12") \
 X(void, guTexDownloadMipMapLevel, (U,int,void **), "_guTexDownloadMipMapLevel@12") \
 X(U, guTexGetCurrentMipMap, (int), "_guTexGetCurrentMipMap@4") \
 X(U *, guTexGetMipMapInfo, (U), "_guTexGetMipMapInfo@4") \
 X(U, guTexMemQueryAvail, (int), "_guTexMemQueryAvail@4") \
 X(void, guTexMemReset, (void), "_guTexMemReset@0") \
 X(void, guTexSource, (U), "_guTexSource@4")
UTILITY_FUNCTIONS(DECLARE)
static U report, row_number;
static void value(U v) { U row[2]={row_number++,v},done; WriteFile(report,row,8,&done,0); }
static void info(U id) { U *p=guTexGetMipMapInfo(id); for(U i=0;i<49;i++)value(p[i]); }
static float as_float(U word) { union {U word;float value;} v;v.word=word;return v.value; }
static U alloc(const U *a) { return guTexAllocateMemory(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],as_float(a[13]),a[14]); }
static const U matrix[4][15]={
 {0,3,32,32,10,1,8,3,3,1,0,1,0,0,0},
 {1,1,32,32,5,0,8,2,0,1,0,1,0,0x3f800000,0},
 {0,2,32,32,1,2,8,4,6,1,0,1,0,0xbf800000,1},
 {1,3,32,32,9,2,8,0,2,1,0,1,0,0x3fc00000,1}
};
static unsigned short rgb_levels[16*16+8*8+4*4+2*2+1];
static B yiq[8*8];
static NccTable utility_ncc;
void mainCRTStartup(void)
{
    U glide=LoadLibraryA("glide2x.dll"),id,i;
    if(!glide)fail(1);
    GLIDE_FUNCTIONS(RESOLVE)
    UTILITY_FUNCTIONS(RESOLVE)
    report=CreateFileA("C:\\GU.BIN",0x40000000,0,0,2,0,0);
    if(report==~0u)ExitProcess(99);
    struct {U style;void *proc;int extra[2];U instance,icon,cursor,brush;const char *menu,*name;} wc={0};
    wc.proc=(void*)window_proc;wc.name="GuFixture";RegisterClassA(&wc);
    U hwnd=CreateWindowExA(0,"GuFixture","Glide utilities",0x90000000,0,0,640,480,0,0,0,0);
    grGlideInit();grSstSelect(0);
    if(!grSstWinOpen(hwnd,7,0,0,0,2,1))fail(3);
    grGammaCorrectionValue(1);grCullMode(0);
    for(i=0;i<4;i++) {
        guTexMemReset();
        value(guTexGetCurrentMipMap(matrix[i][0]));
        id=alloc(matrix[i]);value(id);value(guTexMemQueryAvail(matrix[i][0]));info(id);
        value(guTexChangeAttributes(id,64,16,10,0,7,5,3,0,1,0,1));info(id);
        value(guTexMemQueryAvail(matrix[i][0]));guTexSource(id);
        value(guTexGetCurrentMipMap(matrix[i][0]));
        guTexSource(~0u);value(guTexGetCurrentMipMap(matrix[i][0]));
        value(guTexChangeAttributes(~0u,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1));
    }
    guTexMemReset();
    for(i=0;i<18;i++) {
        value(guTexAllocateMemory(0,3,256,256,10,0,0,0,3,0,0,0,0,0,0));
        value(guTexMemQueryAvail(0));
    }
    guTexMemReset();
    U bad=0;
    for(i=0;i<1024;i++)if(guTexAllocateMemory(0,3,1,1,5,0,8,8,3,0,0,0,0,0,0)!=i)bad++;
    value(bad);value(guTexAllocateMemory(1,3,1,1,5,0,8,8,3,0,0,0,0,0,0));
    value(guTexMemQueryAvail(0));value(guTexMemQueryAvail(1));
    U *stable=guTexGetMipMapInfo(0);
    guTexMemReset();value(stable==guTexGetMipMapInfo(0));value(stable[1]);
    value(guTexGetCurrentMipMap(0));value(guTexMemQueryAvail(0));
    // Full chain upload. The most detailed level is blue, the next yellow.
    for(i=0;i<sizeof rgb_levels/2;i++)rgb_levels[i]=i<256?0x001f:0xffe0;
    id=guTexAllocateMemory(0,3,16,16,10,0,8,4,3,1,1,0,0,0,0);
    guTexDownloadMipMap(id,rgb_levels,0);
    value(guTexGetMipMapInfo(id)[5]==(U)rgb_levels);
    guTexSource(id);plain();decal();grBufferClear(0,0,0);rect(0,0,100,100,0xffffff,255,0,1);
    grBufferSwap(1);unsigned short pixel=0;
    value(grLfbReadRegion(0,50,50,1,1,2,&pixel));value(pixel);
    // A single odd level, followed by an excluded even level. Both advance
    // the input pointer, only the selected level changes texture bytes.
    U odd=guTexAllocateMemory(0,2,8,8,10,0,5,5,3,1,1,0,0,0,0);
    void *data=rgb_levels+256;guTexDownloadMipMapLevel(odd,5,&data);
    value((B*)data-(B*)(rgb_levels+256));
    guTexDownloadMipMapLevel(odd,6,&data);value((B*)data-(B*)(rgb_levels+256));
    guTexSource(odd);grBufferClear(0,0,0);rect(0,0,100,100,0xffffff,255,0,1);grBufferSwap(1);
    value(grLfbReadRegion(0,50,50,1,1,2,&pixel));value(pixel);
    // NCC metadata and binding must reach the sampled YIQ texture.
    for(i=0;i<64;i++)yiq[i]=0xff;
    for(i=0;i<4;i++)utility_ncc.packed[i]=0xffffffff;
    U nccid=guTexAllocateMemory(0,3,8,8,1,0,5,5,3,1,1,0,0,0,0);
    guTexDownloadMipMap(nccid,yiq,&utility_ncc);
    value(guTexGetMipMapInfo(nccid)[37]);
    guTexSource(nccid);grBufferClear(0,0,0);rect(0,0,100,100,0xffffff,255,0,1);grBufferSwap(1);
    value(grLfbReadRegion(0,50,50,1,1,2,&pixel));value(pixel);
    CloseHandle(report);grSstWinClose();ExitProcess(0);
}
