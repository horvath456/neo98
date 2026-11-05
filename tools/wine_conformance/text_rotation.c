/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
START_TEST(text_rotation)
{
    static const int angles[]={0,900,-900,1800},heights[]={10,20,50};
    BITMAPINFO info;HDC dc=CreateCompatibleDC(NULL);HBITMAP bitmap,old;DWORD *pixels;unsigned a,h,align;
    memset(&info,0,sizeof info);info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=200;info.bmiHeader.biHeight=-200;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,(void**)&pixels,NULL,0);ok(bitmap!=NULL,"bitmap\n");if(!bitmap)return;old=SelectObject(dc,bitmap);
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(0,0,0));
    for(h=0;h<3;++h)for(a=0;a<4;++a)for(align=0;align<2;++align){
        LOGFONTA lf;HFONT font,previous;unsigned i,hash=2166136261u,count=0;int left=200,top=200,right=-1,bottom=-1;
        memset(&lf,0,sizeof lf);lf.lfHeight=-heights[h];lf.lfEscapement=angles[a];lf.lfCharSet=DEFAULT_CHARSET;lstrcpyA(lf.lfFaceName,"Marlett");
        font=CreateFontIndirectA(&lf);ok(font!=NULL,"font %d/%d\n",heights[h],angles[a]);if(!font)continue;
        previous=SelectObject(dc,font);for(i=0;i<40000;++i)pixels[i]=0xffffff;
        SetTextAlign(dc,TA_LEFT|(align?TA_BASELINE:TA_TOP));ok(TextOutA(dc,100,100,"gg",2),"draw\n");GdiFlush();
        for(i=0;i<40000;++i){unsigned value=pixels[i]&0xffffff;hash=(hash^value)*16777619u;if(!value){int x=i%200,y=i/200;++count;if(x<left)left=x;if(x>right)right=x;if(y<top)top=y;if(y>bottom)bottom=y;}}
        trace("TEXT_ROTATION height=%d angle=%d baseline=%u pixels=%u box=%d,%d,%d,%d hash=%08x\n",heights[h],angles[a],align,count,left,top,right,bottom,hash);fflush(stdout);
        {
            MAT2 matrix={{0,1},{0,0},{0,0},{0,1}};GLYPHMETRICS gm;TEXTMETRICA tm;
            DWORD bytes=GetGlyphOutlineA(dc,'g',GGO_BITMAP,&gm,0,NULL,&matrix);unsigned glyph_hash=2166136261u;
            BYTE *buffer=bytes!=GDI_ERROR&&bytes<1048576?HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,bytes?bytes:1):NULL;
            ok(buffer!=NULL,"outline buffer\n");
            if(buffer){ok(GetGlyphOutlineA(dc,'g',GGO_BITMAP,&gm,bytes,buffer,&matrix)!=GDI_ERROR,"outline\n");for(i=0;i<bytes;++i)glyph_hash=(glyph_hash^buffer[i])*16777619u;HeapFree(GetProcessHeap(),0,buffer);}
            GetTextMetricsA(dc,&tm);
            trace("TEXT_ROTATION_QUERY height=%d angle=%d baseline=%u bytes=%lu box=%lu,%lu origin=%ld,%ld advance=%d,%d hash=%08x metrics=%ld,%ld,%ld,%ld\n",heights[h],angles[a],align,bytes,gm.gmBlackBoxX,gm.gmBlackBoxY,gm.gmptGlyphOrigin.x,gm.gmptGlyphOrigin.y,gm.gmCellIncX,gm.gmCellIncY,glyph_hash,tm.tmHeight,tm.tmAscent,tm.tmDescent,tm.tmAveCharWidth);fflush(stdout);
        }
        SelectObject(dc,previous);DeleteObject(font);
    }
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
