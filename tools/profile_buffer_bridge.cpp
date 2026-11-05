// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_profile_buffer.h"
#include "../src/win98_profile_mutation.h"
struct Wire {unsigned operation,size,position,capacity,missing_key,missing_default,result,flags,missing_section,missing_value,fail,reserves;unsigned char text[2048],section[128],key[128],fallback[256],out[256];};
extern "C" void profile_buffer(Wire*w){using namespace Win98Profile;switch(w->operation){case 0:w->size=w->result=Normalize(w->text,w->size,2048);w->flags|=1;break;case 1:w->result=Section(w->text,w->size,w->section);break;case 2:w->result=Key(w->text,w->size,w->position,w->key);break;case 3:w->result=ValueLength(w->fallback);break;case 4:w->result=String(w->text,w->size,w->section,w->missing_key?nullptr:w->key,w->missing_default?nullptr:w->fallback,w->out,w->capacity);break;case 5:w->result=Number(w->fallback);break;case 6:case 9:{struct Env:MutationEnvironment{Wire&w;Env(Wire&v):w(v){}bool Reserve(Buffer&,int)override{++w.reserves;return !w.fail;}}e(*w);Buffer b{w->text,w->size,2048,w->flags};w->result=w->operation==9?WriteSection(e,b,w->section,w->missing_value?nullptr:w->fallback):WriteString(e,b,w->missing_section?nullptr:w->section,w->missing_key?nullptr:w->key,w->missing_value?nullptr:w->fallback);w->size=b.size;w->flags=b.flags;break;}case 7:w->result=SectionText(w->text,w->size,w->section,w->out,w->capacity);break;case 8:w->result=SectionNames(w->text,w->size,w->out,w->capacity);break;}}

#ifdef PROFILE_REPLAY
#include <cstdio>
#include <cstring>
#include <cassert>
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);Wire value,expected;unsigned count=0;while(fread(&value,sizeof value,1,f)){assert(fread(&expected,sizeof expected,1,f)==1);profile_buffer(&value);assert(!memcmp(&value,&expected,sizeof value));++count;}fclose(f);printf("PROFILE sanitizer original replays PASS %u\n",count);}
#endif
