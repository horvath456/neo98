/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U CreateBitmap(int,int,U,U,const void*);API U GetBitmapBits(U,U,void*);API U BitBlt(U,int,int,int,int,U,int,int,U);
API void ExitProcess(U); API U CreateCompatibleDC(U);API U DeleteDC(U);API U DeleteObject(U);API U SelectObject(U,U);API U CreateDIBSection(U,const void*,U,void**,U,U);API U CreateSolidBrush(U);API U CreatePen(U,int,U);API U PatBlt(U,int,int,int,int,U);API U FillRect(U,const void*,U);API U MoveToEx(U,int,int,void*);API U LineTo(U,int,int);API U GetPixel(U,int,int);API U SetPixel(U,int,int,U);API U SaveDC(U);API U RestoreDC(U,int);API U IntersectClipRect(U,int,int,int,int);
static void check(int v,U n){if(!v)ExitProcess(n);}
static U pixel(const unsigned char*p,U depth,U x,U y){U stride=((16*depth+31)/32)*4;p+=y*stride;if(depth==1)return(p[x/8]>>(7-x%8))&1;if(depth==4)return(p[x/2]>>((1-x%2)*4))&15;p+=x*(depth/8);U v=0;for(U i=0;i<depth/8;++i)v|=(U)p[i]<<(i*8);return v;}
void run_device_color_test(void){U dc=CreateCompatibleDC(0),white=CreateSolidBrush(0xffffff),black=CreateSolidBrush(0),pen=CreatePen(0,1,0);check(dc&&white&&black&&pen,14700);
 for(U n=0;n<7;++n){U depth=n==0?1:n==1?4:n==2?8:n==3||n==6?16:n==4?24:32;U header[14]={40,16,(U)-8,(depth<<16)|1,0,0,0,0,0,0,0,0xffffff};if(depth<=8)header[8]=2;if(n==6){header[4]=3;header[10]=0xf800;header[11]=0x07e0;header[12]=0x001f;}
  void*raw=0;U bitmap=CreateDIBSection(0,header,0,&raw,0,0),old=SelectObject(dc,bitmap),ob=SelectObject(dc,white),op=SelectObject(dc,pen);check(bitmap&&raw&&old&&ob&&op,14701);
  check(PatBlt(dc,0,0,16,8,0xf00021),14702);U expected=depth<=8?1:depth==16?(n==6?0xffff:0x7fff):0xffffff;for(U y=0;y<8;++y)for(U x=0;x<16;++x)check(pixel(raw,depth,x,y)==expected,14703);
  check(MoveToEx(dc,1,2,0)&&LineTo(dc,7,2),14704);for(U x=0;x<16;++x)check(pixel(raw,depth,x,2)==(x>=1&&x<7?0:expected),14705);check(SetPixel(dc,7,2,0)==0&&pixel(raw,depth,7,2)==0,14706);
  check(SaveDC(dc)&&IntersectClipRect(dc,2,4,4,6),14707);int r[]={0,0,16,8};check(FillRect(dc,r,black),14708);check(RestoreDC(dc,-1),14709);for(U y=3;y<8;++y)for(U x=0;x<16;++x)check(pixel(raw,depth,x,y)==(y>=4&&y<6&&x>=2&&x<4?0:expected),14710);
  if(depth>=16){U red=CreateSolidBrush(0xff);check(red&&FillRect(dc,r,red),14711);U physical=depth==16?(n==6?0xf800:0x7c00):0xff0000;check(pixel(raw,depth,9,7)==physical&&GetPixel(dc,9,7)==0xff,14712);check(DeleteObject(red),14713);}
  SelectObject(dc,ob);SelectObject(dc,op);SelectObject(dc,old);check(DeleteObject(bitmap),14714);
 }
 // Win98 rejects a colour DDB whose depth differs from the display's 32 bpp.
 unsigned char source[]={0x12,0x34,0x56,0x78},result[4]={0};
 U src=CreateBitmap(4,2,1,4,source),dst=CreateBitmap(4,2,1,4,0),other=CreateCompatibleDC(0);
 check(src&&dst&&other,14716);
 check(!SelectObject(dc,src)&&!SelectObject(other,dst),14717);
 check(GetBitmapBits(src,4,result)==4,14718);
 for(U i=0;i<4;++i)check(result[i]==source[i],14719);
 check(DeleteObject(src)&&DeleteObject(dst),14720);
 // Four-bit DIB sections are selectable independently of the device depth.
 // Distinct palette entries make a wrong index conversion visible in BitBlt.
 U indexed[26]={40,4,(U)-2,(4<<16)|1,0,0,0,0,16,0};
 for(U i=0;i<16;++i)indexed[10+i]=i*0x111111;
 unsigned char *from=0,*to=0;
 src=CreateDIBSection(0,indexed,0,(void**)&from,0,0);
 dst=CreateDIBSection(0,indexed,0,(void**)&to,0,0);
 check(src&&dst&&from&&to,14721);
 for(U i=0;i<8;++i){from[i]=0;to[i]=0;}
 from[0]=source[0];from[1]=source[1];from[4]=source[2];from[5]=source[3];
 U os=SelectObject(dc,src),od=SelectObject(other,dst);
 check(os&&od&&BitBlt(other,0,0,4,2,dc,0,0,0xcc0020),14722);
 for(U y=0;y<2;++y)for(U x=0;x<2;++x)check(to[y*4+x]==source[y*2+x],14723);
 SelectObject(dc,os);SelectObject(other,od);
 check(DeleteObject(src)&&DeleteObject(dst)&&DeleteDC(other),14724);
 check(DeleteObject(white)&&DeleteObject(black)&&DeleteObject(pen)&&DeleteDC(dc),14715);
}
#ifdef DEVICE_COLOR_STANDALONE
void mainCRTStartup(void){run_device_color_test();ExitProcess(0);}
#endif
