// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_send_message.h"
#include <cstdio>
#include <cstring>
struct Environment{unsigned*out;void Wake(){++out[4];}void Dispose(){++out[5];}void CallbackReply(unsigned v){++out[6];out[7]=v;}};
extern "C" void send_message(const unsigned*r,unsigned*out){memset(out,0,8*4);Win98SendMessage::Record record{r[1],r[2],r[3]};auto*p=r[4]?&record:nullptr;Environment e{out};switch(r[0]){case 0:out[0]=Win98SendMessage::CrossTask(r[1],r[2]);break;case 1:out[0]=Win98SendMessage::InSend(p);break;case 2:out[0]=Win98SendMessage::InSendEx(p);break;case 3:out[0]=Win98SendMessage::Reply(e,p,r[5]);break;case 4:Win98SendMessage::Finish(e,record,r[5]);break;}out[1]=record.sender;out[2]=record.result;out[3]=record.flags;}
#ifdef SEND_MESSAGE_CORPUS_MAIN
int main(int argc,char**argv){if(argc!=2)return 1;FILE*f=fopen(argv[1],"rb");if(!f)return 2;unsigned r[6],expected[8],actual[8],n=0;while(fread(r,sizeof r,1,f)){if(fread(expected,sizeof expected,1,f)!=1)return 3;send_message(r,actual);if(memcmp(expected,actual,sizeof actual))return 4;++n;}fclose(f);printf("Send message original corpus PASS %u cases\n",n);}
#endif
