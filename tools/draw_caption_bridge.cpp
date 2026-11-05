// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_draw_caption.h"
#include <cstring>
using namespace Win98Caption;
struct E{U*out,n=0;int width=0,height=12;bool alive=true,eligible=true,sysmenu=true;U chosen=77;U text=0xabcdef,bk=0x123456,font=99,mode=2;void T(U a){out[1+n++]=a;}void R(Rect r){T(r.left);T(r.top);T(r.right);T(r.bottom);}
 void Caption(U w,char*p,U size,bool direct){T(1);T(w);T(size);T(direct);strcpy(p,"Caption");}
 bool ValidWindow(U w){T(2);T(w);return alive;}bool EligibleIcon(U w){T(3);T(w);return eligible;}bool SystemMenu(U){return sysmenu;}U SmallIcon(U w,bool d){T(4);T(w);T(d);return chosen;}
 U BkMode(U,U v){T(5);T(v);U old=mode;mode=v;return old;}U TextColor(U,U v){T(6);T(v);U old=text;text=v;return old;}U BkColor(U,U v){T(7);T(v);U old=bk;bk=v;return old;}U Select(U,U v){T(8);T(v);U old=font;font=v;return old;}int Height(U){T(9);return height;}
 U Length(const char*s){T(10);T(strlen(s));return strlen(s);}int Extent(const char*s,U n){T(11);T(n);return s[0]=='.'?3*int(n):width?width:int(n)*7;}
 bool CachedIcon(U,Rect r,U icon,U flags){T(12);R(r);T(icon);T(flags);return false;}bool CachedGradient(U,Rect r,U a){T(13);R(r);T(a);return false;}
 void Fill(U,Rect r,U brush){T(14);R(r);T(brush);}void DrawIcon(U,int x,int y,U icon,int w,int h){T(15);T(x);T(y);T(icon);T(w);T(h);}void Stripe(U,Rect r,U c){T(16);R(r);T(c);}void Ellipsis(U,const char*,U n,Rect r,U flags,int margin){T(17);T(n);R(r);T(flags);T(margin);}void Output(U,int x,int y,Rect r,const char*,U n){T(18);T(x);T(y);R(r);T(n);}void Limit(){T(19);}
};
extern "C" U caption(const U*v,U*out){E e{out};e.width=v[9];e.alive=v[10];e.eligible=v[11];e.sysmenu=v[12];e.chosen=v[13];State s;for(U i=0;i<29;++i){s.colors[i]=0x10101*(i+1);s.brushes[i]=200+i;}s.dither=300;s.font=50;s.smallfont=51;s.height=12;s.smallheight=10;s.depth=v[14];if(v[15])s.colors[20]=s.colors[0];Rect r{int(v[1]),int(v[2]),int(v[3]),int(v[4])};Args a{v[5],123,v[6],v[7],v[0],v[8]?"Caption":nullptr};U result=Draw(e,s,a,r);out[0]=e.n;out[1000]=r.left;out[1001]=r.top;out[1002]=r.right;out[1003]=r.bottom;return result;}
extern "C" void gradient(const U*v,U*out){E e{out};Strips(e,123,{int(v[0]),int(v[1]),int(v[2]),int(v[3])},v[4],v[5],v[6]);out[0]=e.n;}
extern "C" U fit(const U*v,U*out){E e{out};U n=v[0];bool r=EllipsisFit(e,"Caption",n,int(v[1]),int(v[2]));out[0]=e.n;out[1000]=n;return r;}

extern "C" void caption_public(const U*v,U*out){auto p=Public(v,v[7]);memcpy(out,&p,sizeof p);}
extern "C" U end_ellipsis(const U*v,U*out){E e{out};char text[256]="Caption";U length=7;bool result=EndEllipsis(e,text,length,256,int(v[0]),int(v[1]));out[0]=e.n;out[1000]=length;memcpy(out+1001,text,length+1);return result;}
#ifdef DRAW_CAPTION_SANITIZE
#include <cassert>
#include <initializer_list>
int main(){U out[2048]={};for(U flags:{0u,9u,13u,0x19u,0x29u,0x201cu,0x4008u}){U v[]={flags,3,4,103,22,0,52,77,1,0,1,1,1,77,32,0};caption(v,out);}U gradient_values[]={0,0,157,18,2,0xff,0xabcdef};gradient(gradient_values,out);for(U width=0;width<50;++width){U v[]={width,0};end_ellipsis(v,out);}return 0;}
#endif
