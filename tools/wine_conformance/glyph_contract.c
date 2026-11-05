/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
typedef HRESULT (WINAPI *cmap_fn)(HDC,void **,const WCHAR *,int,DWORD,WORD *);
typedef HRESULT (WINAPI *free_fn)(void **);
static unsigned hash(const DWORD *p,unsigned n){unsigned h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;}
START_TEST(glyph_contract)
{
    BITMAPINFO bi;DWORD *pixels;HDC dc=CreateCompatibleDC(NULL);HBITMAP bitmap,oldbitmap;
    HMODULE module=LoadLibraryA("usp10.dll");cmap_fn cmap=module?(cmap_fn)GetProcAddress(module,"ScriptGetCMap"):NULL;
    free_fn release=module?(free_fn)GetProcAddress(module,"ScriptFreeCache"):NULL;
    const WCHAR wide[]={'A','B','C'};const char ansi[]="ABC";RECT r={0,0,128,48};
    const char *faces[]={"Arial","Symbol"};unsigned f,mode;int dx[]={22,19,21};
    ZeroMemory(&bi,sizeof bi);bi.bmiHeader.biSize=40;bi.bmiHeader.biWidth=128;bi.bmiHeader.biHeight=-48;
    bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;
    bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,(void **)&pixels,NULL,0);oldbitmap=SelectObject(dc,bitmap);
    ok(bitmap&&pixels,"create glyph surface\n");if(!bitmap||!pixels)return;
    ok(cmap&&release,"load installed Uniscribe glyph mapping\n");
    for(f=0;f<2;++f){
        HFONT font=CreateFontA(-20,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,f?SYMBOL_CHARSET:ANSI_CHARSET,0,0,0,0,faces[f]);
        HFONT old=SelectObject(dc,font);WORD glyphs[3];void *cache=NULL;HRESULT mapped=E_FAIL;
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(0,0,0));
        ok(font!=NULL,"create font %s\n",faces[f]);
        if(cmap)mapped=cmap(dc,&cache,wide,3,0,glyphs);
        trace("GLYPH_MAP face=%s result=%08lx glyphs=%u,%u,%u\n",faces[f],mapped,glyphs[0],glyphs[1],glyphs[2]);
        for(mode=0;mode<2;++mode){
            unsigned a,w,g;BOOL result;const int *advance=mode?dx:NULL;
            PatBlt(dc,0,0,128,48,WHITENESS);result=ExtTextOutA(dc,8,8,ETO_CLIPPED,&r,ansi,3,advance);a=hash(pixels,128*48);
            ok(result,"ANSI draw %s mode=%u\n",faces[f],mode);
            PatBlt(dc,0,0,128,48,WHITENESS);result=ExtTextOutW(dc,8,8,ETO_CLIPPED,&r,wide,3,advance);w=hash(pixels,128*48);
            ok(result,"Unicode draw %s mode=%u\n",faces[f],mode);
            ok(a==w,"ANSI/Unicode pixels %s mode=%u %08x/%08x\n",faces[f],mode,a,w);
            if(SUCCEEDED(mapped)){
                PatBlt(dc,0,0,128,48,WHITENESS);result=ExtTextOutW(dc,8,8,ETO_CLIPPED|ETO_GLYPH_INDEX,&r,glyphs,3,advance);g=hash(pixels,128*48);
                ok(result,"glyph draw %s mode=%u\n",faces[f],mode);
                ok(g==w,"glyph/Unicode pixels %s mode=%u %08x/%08x\n",faces[f],mode,g,w);
                trace("GLYPH_PIXELS face=%s mode=%u ansi=%08x wide=%08x glyph=%08x\n",faces[f],mode,a,w,g);
            }
        }
        if(release)release(&cache);SelectObject(dc,old);DeleteObject(font);
    }
    SelectObject(dc,oldbitmap);DeleteObject(bitmap);DeleteDC(dc);if(module)FreeLibrary(module);
}
