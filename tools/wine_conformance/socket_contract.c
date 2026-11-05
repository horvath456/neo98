/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <winsock2.h>
#include <windows.h>
#include "wine/test.h"
static void backpressure(void)
{
    SOCKET listener=INVALID_SOCKET,sender=INVALID_SOCKET,receiver=INVALID_SOCKET;
    WSAEVENT event=WSA_INVALID_EVENT;
    struct sockaddr_in address={0};WSANETWORKEVENTS events;
    char buffer[4096];int size=sizeof(address),small=4096,ret,error=0,pattern=1;
    unsigned sent=0,received=0,i;DWORD start;u_long nonblock=1;
    listener=socket(AF_INET,SOCK_STREAM,0);sender=socket(AF_INET,SOCK_STREAM,0);
    ok(listener!=INVALID_SOCKET&&sender!=INVALID_SOCKET,"backpressure sockets\n");
    if(listener==INVALID_SOCKET||sender==INVALID_SOCKET)goto done;
    address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    ret=bind(listener,(struct sockaddr*)&address,sizeof(address));ok(!ret,"backpressure bind\n");if(ret)goto done;
    ret=getsockname(listener,(struct sockaddr*)&address,&size);ok(!ret,"backpressure name\n");if(ret)goto done;
    ret=listen(listener,1);ok(!ret,"backpressure listen\n");if(ret)goto done;
    ret=connect(sender,(struct sockaddr*)&address,sizeof(address));ok(!ret,"backpressure connect\n");if(ret)goto done;
    receiver=accept(listener,NULL,NULL);ok(receiver!=INVALID_SOCKET,"backpressure accept\n");if(receiver==INVALID_SOCKET)goto done;
    ok(!setsockopt(sender,SOL_SOCKET,SO_SNDBUF,(char*)&small,sizeof(small)),"send buffer\n");
    ok(!setsockopt(receiver,SOL_SOCKET,SO_RCVBUF,(char*)&small,sizeof(small)),"receive buffer\n");
    event=WSACreateEvent();ok(event!=WSA_INVALID_EVENT,"write event\n");if(event==WSA_INVALID_EVENT)goto done;
    ret=WSAEventSelect(sender,event,FD_WRITE);ok(!ret,"select write\n");if(ret)goto done;
    ret=WaitForSingleObject(event,5000);ok(ret==WAIT_OBJECT_0,"initial write wait %d\n",ret);
    ret=WSAEnumNetworkEvents(sender,event,&events);ok(!ret,"initial write enum\n");
    ok((events.lNetworkEvents&FD_WRITE)&&!events.iErrorCode[FD_WRITE_BIT],"initial write event\n");
    memset(buffer,0x5a,sizeof(buffer));start=GetTickCount();
    while(sent<16*1024*1024&&GetTickCount()-start<10000){
        ret=send(sender,buffer,sizeof(buffer),0);
        if(ret<0){error=WSAGetLastError();break;}if(!ret)break;sent+=ret;
    }
    trace("SOCKET_CONTRACT backpressure blocked=%u error=%d sent_some=%u\n",error==WSAEWOULDBLOCK,error,sent!=0);
    ok(error==WSAEWOULDBLOCK,"bounded send must reach backpressure: %d\n",error);
    ok(!ioctlsocket(receiver,FIONBIO,&nonblock),"receiver nonblocking\n");
    start=GetTickCount();
    while(received<sent&&GetTickCount()-start<15000){
        ret=recv(receiver,buffer,sizeof(buffer),0);
        if(ret>0){for(i=0;i<(unsigned)ret;++i)if(buffer[i]!=0x5a)pattern=0;received+=ret;}
        else if(ret<0&&WSAGetLastError()==WSAEWOULDBLOCK)Sleep(1);
        else break;
    }
    ret=WaitForSingleObject(event,5000);
    ok(!WSAEnumNetworkEvents(sender,event,&events),"resumed write enum\n");
    trace("SOCKET_CONTRACT resume waited=%u writable=%u error=%d complete=%u pattern=%u\n",ret==WAIT_OBJECT_0,!!(events.lNetworkEvents&FD_WRITE),events.iErrorCode[FD_WRITE_BIT],received==sent,pattern);
    ok(ret==WAIT_OBJECT_0&&(events.lNetworkEvents&FD_WRITE)&&!events.iErrorCode[FD_WRITE_BIT],"resume after blocked send\n");
    ok(sent&&received==sent&&pattern,"all buffered data received intact\n");
done:
    if(event!=WSA_INVALID_EVENT)WSACloseEvent(event);
    if(receiver!=INVALID_SOCKET)closesocket(receiver);
    if(sender!=INVALID_SOCKET)closesocket(sender);
    if(listener!=INVALID_SOCKET)closesocket(listener);
}
START_TEST(socket_contract)
{
    WSADATA data;unsigned type,firstreuse,secondreuse,listening,i;
    const int sizes[]={0,3,4,8};
    int startup=WSAStartup(MAKEWORD(2,2),&data);
    ok(startup==0,"WSAStartup %d\n",startup);if(startup)return;
    for(type=SOCK_STREAM;type<=SOCK_DGRAM;++type){
        SOCKET sock=socket(AF_INET,type,0);
        ok(sock!=INVALID_SOCKET,"socket type %u\n",type);if(sock==INVALID_SOCKET)continue;
        for(i=0;i<4;++i){
            DWORD value[2]={0xdeadbeef,0xabcdef01};int size=sizes[i],ret,error;
            WSASetLastError(0x1234);ret=getsockopt(sock,SOL_SOCKET,SO_MAX_MSG_SIZE,(char*)value,&size);error=ret?WSAGetLastError():0;
            trace("SOCKET_CONTRACT max type=%u input_size=%d ret=%d error=%d size=%d value=%08lx canary=%08lx\n",type,sizes[i],ret,error,size,value[0],value[1]);
        }
        closesocket(sock);
        for(listening=0;listening<(type==SOCK_STREAM?2u:1u);++listening)
        for(firstreuse=0;firstreuse<2;++firstreuse)
        for(secondreuse=0;secondreuse<2;++secondreuse){
            SOCKET a=socket(AF_INET,type,0),b=socket(AF_INET,type,0);
            struct sockaddr_in address={0};int one=1,size=sizeof(address),ret,error,retry=0,retryerror=0;
            ok(a!=INVALID_SOCKET&&b!=INVALID_SOCKET,"create pair\n");
            if(a==INVALID_SOCKET||b==INVALID_SOCKET){if(a!=INVALID_SOCKET)closesocket(a);if(b!=INVALID_SOCKET)closesocket(b);continue;}
            if(firstreuse)ok(!setsockopt(a,SOL_SOCKET,SO_REUSEADDR,(char*)&one,sizeof(one)),"set first reuse\n");
            if(secondreuse)ok(!setsockopt(b,SOL_SOCKET,SO_REUSEADDR,(char*)&one,sizeof(one)),"set second reuse\n");
            address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
            ret=bind(a,(struct sockaddr*)&address,sizeof(address));ok(!ret,"first bind %d\n",WSAGetLastError());
            if(!ret)ok(!getsockname(a,(struct sockaddr*)&address,&size),"first name\n");
            if(listening)ok(!listen(a,1),"first listen\n");
            WSASetLastError(0x1234);ret=bind(b,(struct sockaddr*)&address,sizeof(address));error=ret?WSAGetLastError():0;
            if(ret&&!secondreuse){
                ok(!setsockopt(b,SOL_SOCKET,SO_REUSEADDR,(char*)&one,sizeof(one)),"enable reuse after bind failed\n");
                retry=bind(b,(struct sockaddr*)&address,sizeof(address));retryerror=retry?WSAGetLastError():0;
            }
            trace("SOCKET_CONTRACT bind type=%u listening=%u firstreuse=%u secondreuse=%u ret=%d error=%d retry=%d retryerror=%d\n",type,listening,firstreuse,secondreuse,ret,error,retry,retryerror);
            closesocket(b);closesocket(a);
        }
    }
    backpressure();
    WSACleanup();
}
