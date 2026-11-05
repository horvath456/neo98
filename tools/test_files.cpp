// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/files.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using namespace Files;
int main()
{
    Store store;
    // The store makes only the folders Win98 has (C:\WINDOWS, SYSTEM and
    // TEMP below it); this contract works in two folders of its own.
    {
        U id;
        assert(!store.Lookup("C:\\WINDOWS\\TEMP", id) && store.Lookup("C:\\TEMP", id) == 2 &&
               store.Lookup("C:\\Documents", id) == 2);
    }
    assert(!store.MakeDirectory("C:\\Documents") && !store.MakeDirectory("C:\\TEMP"));
    Session a(store), b(store);
    char path[260], data[256] = {};
    assert(!a.FullPath("C:/Documents/one/.././two. ", path));
    assert(!strcmp(path, "C:\\Documents\\two"));
    assert(!a.SetDirectory("C:\\Documents"));
    assert(!a.FullPath("..\\..\\TEMP\\a", path) && !strcmp(path, "C:\\TEMP\\a"));
    assert(!a.FullPath("C:file.txt", path) && !strcmp(path, "C:\\Documents\\file.txt"));
    assert(a.FullPath("D:\\x", path) == 15);
    assert(a.FullPath("C:\\AUX.txt", path) == 120);
    assert(a.FullPath("C:\\bad*name", path) == 123);
    assert(a.SetDirectory("C:\\missing") == 2);
    U h, h2, status, done, pos, size;
    {
        // Win98 permits CREATE_ALWAYS with read access, but that must neither
        // bypass a read-only target nor grant write rights to a duplicate.
        assert(!a.Open("read-create.bin",1,3,2,0,h,status));
        assert(a.MappingAccess(h)==1);
        assert(a.Write(h,"x",1,done)==5);
        assert(!a.Close(h));
        assert(!store.Attributes("C:\\Documents\\read-create.bin",1));
        assert(a.Open("read-create.bin",1,3,2,0,h,status)==5);
        assert(!a.Open("map-access.bin",3,3,2,0,h,status));
        assert(!a.Duplicate(h,b,1,false,false,h2));
        assert(b.MappingAccess(h2)==1);
        assert(!a.Close(h)&&!b.Close(h2));
        assert(!a.OpenNull(3,h)&&a.IsNull(h));
        assert(!a.Write(h,"abc",3,done)&&done==3);
        assert(!a.Read(h,data,sizeof data,done)&&done==0);
        assert(!a.Close(h)&&!a.Owns(h));
    }
    {
        U native;
        assert(!a.Open("legacy.bin", 3, 3, 2, 0, native, status));
        const U legacy = a.LegacyHandle(native);
        assert(legacy >= 5 && legacy < 256 && a.LegacyHandle(native) == legacy);
        assert(!a.Write(legacy, "HFILE", 5, done) && done == 5);
        assert(!a.Seek(native, 0, 0, pos));
        assert(!a.Read(legacy, data, 5, done) && done == 5 && !memcmp(data, "HFILE", 5));
        assert(a.SetInheritable(legacy, true));
        Session child(store);
        assert(!child.Inherit(a) && child.Owns(legacy));
        assert(!a.Close(native) && !a.Owns(legacy) && a.Close(legacy) == 6);
        assert(!child.Seek(legacy, 0, 0, pos));
        assert(!child.Read(legacy, data, 5, done) && done == 5 && !memcmp(data, "HFILE", 5));
        assert(!child.Close(legacy));
    }
    {
        U original,alias,other;
        assert(!a.Open("duplicate.bin",3,3,2,0,original,status));
        assert(!a.Write(original,"abcdef",6,done));
        assert(!a.Seek(original,0,0,pos));
        assert(!a.Duplicate(original,a,0,true,false,alias));
        assert(!a.Duplicate(original,b,1,false,true,other));
        assert(b.IsInheritable(other)&&b.HasInheritableHandles());
        Session child(store);
        assert(!child.Inherit(b)&&child.Owns(other));
        assert(!a.Read(alias,data,2,done)&&done==2&&!memcmp(data,"ab",2));
        assert(!b.Read(other,data,2,done)&&done==2&&!memcmp(data,"cd",2));
        assert(b.Write(other,"x",1,done)==5);
        assert(!a.Close(original)&&!a.Close(alias));
        assert(!b.Read(other,data,2,done)&&done==2&&!memcmp(data,"ef",2));
        assert(!b.Close(other)&&!b.HasInheritableHandles());
        assert(child.Owns(other));
        assert(!child.Seek(other,0,0,pos));
        assert(!child.Read(other,data,6,done)&&done==6&&!memcmp(data,"abcdef",6));
        assert(!child.Close(other)&&!child.Owns(other));
    }
    assert(!a.Open("hello.txt", 3, 1, 1, 0, h, status));
    assert(!a.Write(h, "hello", 5, done) && done == 5);
    assert(b.Size(h, size) == 6);
    assert(b.Open("C:\\Documents\\HELLO.TXT", 2, 3, 3, 0, h2, status) == 32);
    assert(b.Open("C:\\Documents\\HELLO.TXT", 1, 1, 3, 0, h2, status) == 32);
    assert(!b.Open("C:\\Documents\\HELLO.TXT", 1, 3, 3, 0, h2, status));
    assert(!b.Read(h2, data, sizeof data, done) && done == 5 && !memcmp(data, "hello", 5));
    assert(!b.Read(h2, data, sizeof data, done) && done == 0);
    assert(b.Write(h2, "x", 1, done) == 5 && done == 0);
    assert(store.Remove("C:\\Documents\\hello.txt", false) == 32);
    assert(!b.Close(h2) && b.Close(h2) == 6);
    assert(a.Seek(h, -6, 2, pos) == 131);
    assert(!a.Seek(h, 10, 0, pos) && pos == 10);
    assert(!a.Write(h, "!", 1, done));
    assert(!a.Seek(h, 0, 0, pos));
    memset(data, 0xcc, sizeof data);
    assert(!a.Read(h, data, 256, done) && done == 11 && !memcmp(data, "hello\0\0\0\0\0!", 11));
    assert(!a.Seek(h, 2, 0, pos) && !a.Truncate(h));
    assert(!a.Size(h, size) && size == 2);
    assert(!a.Seek(h, 0xffffffffLL, 0, pos) && pos == 0xffffffff);
    assert(a.Write(h, "xx", 2, done) == 112 && done == 0);
    assert(a.Truncate(h) == 112 && !a.Size(h, size) && size == 2);
    assert(!a.Write(h, nullptr, 0, done));
    assert(!a.Close(h));
    assert(!store.Attributes("C:\\Documents\\hello.txt", 1));
    assert(a.Open("hello.txt", 2, 3, 3, 0, h, status) == 5);
    assert(store.Remove("C:\\Documents\\hello.txt", false) == 5);
    assert(!store.Attributes("C:\\Documents\\hello.txt", 128));
    assert(!a.Open("hello.txt", 3, 3, 4, 0, h, status) && status == 183);
    assert(!a.Size(h, size) && size == 2 && !a.Close(h));
    assert(!a.Open("hello.txt", 3, 3, 2, 0, h, status) && status == 183);
    assert(!a.Size(h, size) && size == 0 && !a.Close(h));
    assert(!store.Rename("C:\\Documents\\hello.txt", "C:\\TEMP\\renamed.txt"));
    assert(a.Open("hello.txt", 1, 3, 3, 0, h, status) == 2);
    assert(!store.Remove("C:\\TEMP\\renamed.txt", false));
    assert(!store.MakeDirectory("C:\\TEMP\\folder"));
    assert(!store.Seed("C:\\TEMP\\folder\\child", "x", 1));
    assert(store.Remove("C:\\TEMP\\folder", true) == 145);
    assert(store.Rename("C:\\TEMP", "C:\\TEMP\\folder\\recursive") == 5);
    assert(!store.Remove("C:\\TEMP\\folder\\child", false));
    assert(!store.Remove("C:\\TEMP\\folder", true));
    for (U i = 0; i < 300; ++i)
    {
        assert(!a.Open("reuse", 3, 0, 2, 0, h, status));
        U stale = h;
        assert(!a.Close(h));
        assert(!a.Open("reuse", 3, 0, 3, 0, h, status));
        assert(h != stale && a.Close(stale) == 6);
        assert(!a.Close(h));
    }
    {
        Session temporary(store);
        assert(!temporary.Open("C:\\TEMP\\release", 3, 0, 2, 0, h, status));
    }
    assert(!b.Open("C:\\TEMP\\release", 3, 0, 3, 0, h, status));
    {
        Store volume;
        assert(!volume.MakeDirectory("C:\\TEMP"));
        Session reader(volume), writer(volume);
        const char *source = "C:\\TEMP\\source", *destination = "C:\\TEMP\\copy";
        assert(!volume.Seed(source, "abc", 3));
        assert(!writer.Open(source, 1, 0, 3, 0, h, status));
        assert(reader.Copy(source, destination, false) == 32);
        const Node *node = nullptr;
        assert(reader.Information(h, node) == 6 && !node);
        assert(!writer.Information(h, node) && node->size == 3);
        assert(!writer.Close(h));
        assert(!reader.Copy(source, destination, true));
        assert(!writer.Open(destination, 1, 3, 3, 0, h, status));
        assert(reader.Copy(source, destination, false) == 32);
        assert(!writer.Read(h, data, sizeof data, done) && done == 3 && !memcmp(data, "abc", 3));
        assert(!writer.Close(h));
        assert(!volume.Seed("C:\\TEMP\\empty", nullptr, 0));
        assert(!reader.Copy("C:\\TEMP\\empty", destination, false));
        U id; assert(!volume.Lookup(destination, id) && !volume.Get(id)->size);
    }
    {
        // Disk full must delete a failed destination and release both handles;
        // the same operation succeeds after space becomes available.
        Store volume;
        assert(!volume.MakeDirectory("C:\\TEMP"));
        Session copy(volume);
        constexpr U chunk = 8 * 1024 * 1024;
        auto *payload = new unsigned char[chunk];
        memset(payload, 0x5a, chunk);
        for (const char *name : {"C:\\TEMP\\a", "C:\\TEMP\\b", "C:\\TEMP\\c", "C:\\TEMP\\d"})
            assert(!volume.Seed(name, payload, chunk));
        delete[] payload;
        assert(copy.Copy("C:\\TEMP\\a", "C:\\TEMP\\copy", true) == 112);
        U id; assert(volume.Lookup("C:\\TEMP\\copy", id) == 2);
        assert(!volume.Remove("C:\\TEMP\\d", false));
        assert(!copy.Copy("C:\\TEMP\\a", "C:\\TEMP\\copy", true));
        assert(!volume.Lookup("C:\\TEMP\\copy", id));
        const auto *node = volume.Get(id);
        assert(node->size == chunk && node->data[0] == 0x5a && node->data[chunk - 1] == 0x5a);
    }
    {
        Store volume;
        assert(!volume.MakeDirectory("C:\\enum-delete"));
        for (const char *name : {"C:\\enum-delete\\a", "C:\\enum-delete\\b", "C:\\enum-delete\\c"})
            assert(!volume.Seed(name, nullptr, 0));
        U directory, cursor=0, node, visited=0;
        assert(!volume.Lookup("C:\\enum-delete",directory));
        while (!volume.EnumerateNext(directory,cursor,node)) {
            char name[260];assert(volume.Path(node,name));
            assert(!volume.Remove(name,false));++visited;
        }
        assert(visited==3);
        assert(!volume.Remove("C:\\enum-delete",true));
    }
    puts("NATIVE FILES PASS sharing, session ownership, EOF, sparse extension, seek, truncation, readonly, "
         "directories, rename, stale handles, cleanup");
}
