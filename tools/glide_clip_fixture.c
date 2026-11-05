/* SPDX-License-Identifier: GPL-2.0-or-later */
#define mainCRTStartup baseline_mainCRTStartup
#include "glide_fixture.c"
#undef mainCRTStartup
#include "clip_reference.h"
API U CreateFileA(const char *, U, U, void *, U, U, U);
API U WriteFile(U, const void *, U, U *, void *);
API U CloseHandle(U);
static void (__stdcall *guDrawTriangleWithClip)(const GrVertex *,const GrVertex *,const GrVertex *);
static unsigned short actual[192*144], expected[192*144];
void mainCRTStartup(void)
{
    U glide=LoadLibraryA("glide2x.dll"),done;
    if(!glide)fail(1);
    GLIDE_FUNCTIONS(RESOLVE)
    guDrawTriangleWithClip=(void*)GetProcAddress(glide,"_guDrawTriangleWithClip@12");
    if(!guDrawTriangleWithClip)fail(2);
    U report=CreateFileA("C:\\CLIP.BIN",0x40000000,0,0,2,0,0);
    if(report==~0u)fail(3);
    struct {U style;void *proc;int extra[2];U instance,icon,cursor,brush;const char *menu,*name;} wc={0};
    wc.proc=(void*)window_proc;wc.name="ClipFixture";RegisterClassA(&wc);
    U hwnd=CreateWindowExA(0,"ClipFixture","Glide clipping",0x90000000,0,0,640,480,0,0,0,0);
    grGlideInit();grSstSelect(0);
    if(!grSstWinOpen(hwnd,7,0,0,0,2,1))fail(4);
    grGammaCorrectionValue(1);grCullMode(0);plain();
    grClipWindow(32,24,160,120);
    for(U c=0;c<CLIP_CASES;c++) {
        grBufferClear(0,0,0);
        guDrawTriangleWithClip(&inputs[c][0],&inputs[c][1],&inputs[c][2]);
        grBufferSwap(1);
        if(!grLfbReadRegion(0,0,0,192,144,384,actual))fail(5);
        grBufferClear(0,0,0);
        for(U i=1;i+1<counts[c];i++)grDrawTriangle(&outputs[c][0],&outputs[c][i],&outputs[c][i+1]);
        grBufferSwap(1);
        if(!grLfbReadRegion(0,0,0,192,144,384,expected))fail(6);
        U row[3]={c,0,0};
        for(U i=0;i<192*144;i++){if(actual[i]!=expected[i])row[1]++;if(actual[i])row[2]++;}
        WriteFile(report,row,sizeof row,&done,0);
    }
    CloseHandle(report);grSstWinClose();ExitProcess(0);
}
