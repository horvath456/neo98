/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// The Win16 command tail: a program started with a Win32 command line finds
// only the words behind its own name in the program segment prefix.
#define private public
#include "../src/x86_app.h"
#undef private
#include "../src/win16_machine.h"
#include "../src/win98_ne_task.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

static std::vector<unsigned char> Load(const char* path)
{
    FILE* file=fopen(path,"rb");assert(file);
    fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);
    std::vector<unsigned char> bytes(size);
    assert(fread(bytes.data(),1,size,file)==size_t(size));fclose(file);return bytes;
}

static void Check(const std::vector<unsigned char>& exe,const std::vector<unsigned char>& font,
                  const char* command,const char* expected)
{
    X86::Application app(nullptr);
    assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"FIXTURE.EXE",command));
    assert(app.Win16Process());
    unsigned char tail[128]={};
    assert(app.Read(app.win16.state->command_prefix+Win98NeTask::ProgramPrefixCommand,tail,sizeof tail));
    const unsigned length=tail[0];
    assert(length==strlen(expected));
    assert(!memcmp(tail+1,expected,length));
    assert(tail[1+length]==13);
    // The Win32 view of the same start keeps the whole line.
    char whole[128]={};
    assert(app.command_line&&app.Read(app.command_line,whole,sizeof whole-1));
    assert(!strcmp(whole,command?command:"FIXTURE.EXE"));
    printf("WIN16 command tail: %-48s -> \"%s\"\n",command?command:"(none)",expected);
}

int main(int argc,char** argv)
{
    assert(argc==3);auto exe=Load(argv[1]),font=Load(argv[2]);
    Check(exe,font,nullptr,"");
    Check(exe,font,"FIXTURE.EXE","");
    Check(exe,font,"\"D:\\GAMES\\SHANGHAI\\SHANGHAI.EXE\"","");
    Check(exe,font,"D:\\GAMES\\FIX\\FIXTURE.EXE one two","one two");
    Check(exe,font,"\"C:\\Program Files\\Fix\\FIXTURE.EXE\"  /x \"a b\"","/x \"a b\"");
    Check(exe,font,"  FIXTURE.EXE\tC:\\DOCS\\A.TXT","C:\\DOCS\\A.TXT");
    puts("WIN16 command PASS: the prefix holds the words behind the program name");
}
