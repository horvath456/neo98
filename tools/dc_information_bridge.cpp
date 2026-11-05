// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_dc_information.h"
#include <cstring>
using U=unsigned;
extern "C" U dc_caps(const unsigned char *data,U signature,U flags,U index,U win32,U bitmap_result) {
 struct E { const unsigned char *data;U bitmap; U Word(U p){return data[p]|U(data[p+1])<<8;} U BitmapCaps(U){return bitmap;} }e{data,bitmap_result};
 return win32?Win98DcInformation::Caps32(e,signature,flags,index):Win98DcInformation::Caps16(e,signature,index);
}
extern "C" void dc_initial(unsigned char *data) {
 struct E{unsigned char*p;void Write8(U at,U v){p[at]=v;}void Write16(U at,U v){Write8(at,v);Write8(at+1,v>>8);}}e{data};
 Win98DcInformation::InitialAttributes(e);
}
extern "C" U dc_create(unsigned char *memory,const U *a,U *trace) {
 struct E{unsigned char*p;const U*a;U*t;bool valid=true;
  U Buffer(){return 0x1000;}bool Valid(){return valid;}
  U Read8(U at){if(at>=0x2000){valid=false;return 0;}return p[at];}
  void Write8(U at,U value){if(at>=0x2000){valid=false;return;}p[at]=value;}
  U Profile(U device){t[0]=device;return a[5];}
  U Create16(U driver,U device,U output,U mode,bool info){t[1]=driver;t[2]=device;t[3]=output;t[4]=mode;t[5]=info;return a[6];}
 }e{memory,a,trace};
 return Win98DcInformation::Create(e,a[0],a[1],a[2],a[3],a[4]!=0);
}

extern "C" U dc_query(const U *a,U *trace) {
 struct E{const U*a;U*t;
  U Direct(U index){t[0]=1;t[1]=index;return a[1];}
  int Escape(U code,U (&out)[2]){t[0]=2;t[1]=code;out[0]=a[3];out[1]=a[4];return int(a[2]);}
 }e{a,trace};return Win98DcInformation::Query(e,a[0]);
}

extern "C" U dc_drawing(U signature){return Win98DcInformation::DrawingAllowed(signature);}
