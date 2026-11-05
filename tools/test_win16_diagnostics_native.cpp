/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#define private public
#include "../src/x86_app.h"
#undef private
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
int main(int argc,char** argv)
{
    assert(argc==3);auto exe=Load(argv[1]),font=Load(argv[2]);
    X86::Application app(nullptr);
    assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"DIAG16.EXE"));
    assert(app.Win16Process());auto status=app.Status();
    // A descriptor can outlive its memory. Reading diagnostic bytes from it
    // must not replace the original fault or recursively enter error handling.
    unsigned selector=app.selectors.CreateSegment(0xdead0000,0xfff,true,false);assert(selector);
    char description[256]{};
    app.Win16DescribeAddress(description,sizeof description,selector,0);
    assert(app.Status()==status&&!app.Error()[0]);
    assert(strstr(description,"unowned selector")&&!strstr(description," bytes"));
    app.selectors.Unmap(selector<<16);
    puts("WIN16 DIAGNOSTICS PASS: unmapped instruction bytes preserve process state");
}
