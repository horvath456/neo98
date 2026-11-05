/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A published front buffer must survive writes to GL_BACK after SwapBuffers. */
typedef unsigned U;
int _fltused;
#define API __declspec(dllimport) __stdcall
API U GetModuleHandleA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U,const char *,const char *,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);
API U GetDC(U); API int ShowCursor(int); API U SetWindowTextA(U,const char *);
API U ChoosePixelFormat(U,const void *); API U SetPixelFormat(U,U,const void *);
API U SwapBuffers(U); API U wglCreateContext(U); API U wglMakeCurrent(U,U);
API void glClearColor(float,float,float,float); API void glClear(U);
API void glFlush(void); API void glFinish(void); API void glReadBuffer(U);
API void glReadPixels(int,int,int,int,U,U,void *);
API void Sleep(U); API void ExitProcess(U); API void OutputDebugStringA(const char *);

void mainCRTStartup(void)
{
    U instance=GetModuleHandleA(0),wc[10]={0,(U)DefWindowProcA,0,0,instance,0,0,0,0,(U)"GLSwapCopy"};
    unsigned char pfd[40]={40,0,1,0,0x25,0,0,0,0,32};
    unsigned char pixel[4]={0};
    RegisterClassA(wc);
    U h=CreateWindowExA(0,"GLSwapCopy","Starting",0x90000000,0,0,640,480,0,0,instance,0);
    U dc=GetDC(h),format=ChoosePixelFormat(dc,pfd);
    if (!format || !SetPixelFormat(dc,format,pfd) || !wglMakeCurrent(dc,wglCreateContext(dc))) ExitProcess(1);
    ShowCursor(0);
    glClearColor(0,1,0,1);glClear(0x4000);SwapBuffers(dc);
    glReadBuffer(0x404);glReadPixels(20,20,1,1,0x1908,0x1401,pixel);
    OutputDebugStringA(pixel[0]==0 && pixel[1]==255 && pixel[2]==0 ? "SWAP front green PASS\n" : "SWAP front green FAIL\n");
    glReadBuffer(0x405);glReadPixels(20,20,1,1,0x1908,0x1401,pixel);
    OutputDebugStringA(pixel[0]==0 && pixel[1]==255 && pixel[2]==0 ? "SWAP back retained PASS\n" : "SWAP back retained FAIL\n");
    SetWindowTextA(h,"Published green");Sleep(500);
    glClearColor(1,0,0,1);glClear(0x4000);glFlush();
    SetWindowTextA(h,"Back red only");Sleep(2500);
    glFinish();SetWindowTextA(h,"Finished back red");Sleep(2500);
    SwapBuffers(dc);SetWindowTextA(h,"Published red");Sleep(2500);
    ExitProcess(0);
}
