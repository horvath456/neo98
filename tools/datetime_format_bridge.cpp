// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_datetime_format.h"
#include "../src/win98_datetime_formats.h"
#include <cstring>
#include <cstdio>
#include "datetime_fields.h"
using namespace Win98DateTime;
struct E {const char*Info(U f){return Field(f);}const char*Month(U m,bool a,bool){return Info((a?0x44:0x38)+m-1);}void Era(Output&,const Time&){} };
extern "C" U datetime_format(U date,U locale,U flags,const Time*input,const char*format,U capacity,char*out,U*error){
 if(locale!=0x409||capacity>0x7fffffffu||(!out&&capacity)){*error=87;return 0;}
 if(U e=FormatFlags(date,flags,format!=nullptr)){*error=e;return 0;}
 Time t=*input;if(!(date?ValidDate(t):ValidTime(t))){*error=87;return 0;}
 if(!format)format=!date?Times[0]:(flags&11)==2?LongDates[0]:(flags&11)==8?YearMonths[0]:ShortDates[0];
 E e;char buffer[4096];Output o{buffer,sizeof buffer};U n=date?DateFormat(e,t,format,o):TimeFormat(e,t,flags,format,o);
 if(o.overflow){*error=50;return 0;}if(!capacity)return n;if(capacity<n){*error=122;return 0;}memcpy(out,buffer,n);return n;
}
extern "C" U datetime_enum(U date,U flags,U stop,char*out,U*error){
 if(U err=EnumFlags(date,flags)){*error=err;return 0;}const char*const*list=Times;U n=4;if(date){list=(flags&11)==1?ShortDates:(flags&11)==2?LongDates:YearMonths;n=(flags&11)==1?7:(flags&11)==2?4:1;}
 struct EnumE{char*out;U stop;bool Callback(const char*s,U&r){U n=strlen(s)+1;memcpy(out,s,n);out+=n;r=stop;return true;}}e{out,stop};return Enumerate(e,list,n);
}
#ifdef DATETIME_CORPUS_MAIN
struct Row{U date,locale,flags,custom,capacity,result,error;Time time;char format[256],output[256];};
int main(int argc,char**argv){if(argc!=2)return 2;FILE*f=fopen(argv[1],"rb");if(!f)return 2;Row row;U count=0;while(fread(&row,sizeof row,1,f)==1){char out[256];memset(out,0xcc,sizeof out);U error=0x76543210;U got=datetime_format(row.date,row.locale,row.flags,&row.time,row.custom?row.format:nullptr,row.capacity,out,&error);if(got!=row.result||error!=row.error||memcmp(out,row.output,256)){fprintf(stderr,"row %u failed\n",count);return 1;}++count;}fclose(f);printf("DATETIME ORIGINAL CORPUS %u PASS\n",count);}
#endif
