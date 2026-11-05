/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
START_TEST(console_edges)
{
    HANDLE screen;CONSOLE_SCREEN_BUFFER_INFO info;DWORD mode,count;unsigned variant,pass;
    const char *inputs[]={"abcd","abcd\nf","abcd\rf","ab\tcd"};
    FreeConsole();ok(AllocConsole(),"allocate fresh console\n");
    for(pass=0;pass<2;++pass){
    screen=pass?CreateFileA("CONOUT$",GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,0):
        CreateConsoleScreenBuffer(GENERIC_READ|GENERIC_WRITE,0,NULL,CONSOLE_TEXTMODE_BUFFER,NULL);
    ok(screen!=INVALID_HANDLE_VALUE,"create private screen\n");if(screen==INVALID_HANDLE_VALUE)return;
    GetConsoleScreenBufferInfo(screen,&info);
    for(mode=0;mode<4;++mode)for(variant=0;variant<4;++variant){
        COORD origin={0,0},position={info.dwSize.X-2,0};char text[9]={0};unsigned part;
        FillConsoleOutputCharacterA(screen,'.',info.dwSize.X*3,origin,&count);
        ok(SetConsoleMode(screen,mode),"mode %lu\n",mode);
        SetConsoleCursorPosition(screen,position);
        ok(WriteConsoleA(screen,inputs[variant],lstrlenA(inputs[variant]),&count,NULL),"write %lu/%u\n",mode,variant);
        for(part=0;part<2;++part){
            COORD at={part?0:info.dwSize.X-4,part?1:0};
            ReadConsoleOutputCharacterA(screen,text,8,at,&count);
            trace("CONSOLE_EDGE pass=%u mode=%lu input=%u row=%u bytes=%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x\n",pass,mode,variant,part,
                (BYTE)text[0],(BYTE)text[1],(BYTE)text[2],(BYTE)text[3],(BYTE)text[4],(BYTE)text[5],(BYTE)text[6],(BYTE)text[7]);
        }
        GetConsoleScreenBufferInfo(screen,&info);
        trace("CONSOLE_CURSOR pass=%u mode=%lu input=%u x=%d y=%d\n",pass,mode,variant,info.dwCursorPosition.X,info.dwCursorPosition.Y);
    }
    CloseHandle(screen);
    }
}
