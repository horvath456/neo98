// SPDX-License-Identifier: GPL-2.0-or-later
// New Win32/HFILE ABI adapter for NEO98's native RAM volume.
#include "x86_app.h"
#include "win98_szdd.h"
#include "win98_volume_information.h"
#include "win98_drive_type.h"
#include "win98_path_family.h"
#include "win98_disk_space.h"
#include "win98_file_open.h"
#include "win98_file_times.h"
#include "win98_time.h"
#include "win98_comm_parse.h"
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace X86
{
namespace
{
unsigned Fold(unsigned c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
bool PathEqual(const char *a,const char *b) {
    while(*a&&Fold(*a)==Fold(*b))++a,++b;
    return *a==*b;
}
unsigned LookupNameError(const char *path,unsigned error)
{
    if(error!=123)return error;
    // Win98's lookup services distinguish invalid path characters from
    // unmatched literal quote/colon names. These are not CreateFile rules.
    if(strpbrk(path,"<>|"))return 161;
    const char *tail=path+(path[0]&&path[1]==':'?2:0);
    if(strchr(path,'"')||strchr(tail,':'))return 2;
    return error;
}
bool Match(const char *pattern, const char *name)
{
    if (!strcmp(pattern, "*.*"))
        pattern = "*";
    // A DOS search ending in its only dot requests an empty extension.
    // FAT's literal directory entries participate in '*.' as well.
    const char *dot = strchr(pattern, '.');
    if (dot && !dot[1] && (strchr(pattern, '*') || strchr(pattern, '?')))
    {
        if (!strcmp(pattern, "*.") && (!strcmp(name, ".") || !strcmp(name, ".."))) return true;
        if (strchr(name, '.')) return false;
        char base[256];const unsigned length=dot-pattern;
        if (length>=sizeof base)return false;
        memcpy(base,pattern,length);base[length]=0;
        return Match(base,name);
    }
    const char *star = nullptr, *retry = nullptr;
    while (*name)
    {
        if (*pattern == '?' || Fold((unsigned char)*pattern) == Fold((unsigned char)*name))
            ++pattern, ++name;
        else if (*pattern == '*')
            star = pattern++, retry = name;
        else if (star)
            pattern = star + 1, name = ++retry;
        else
            return false;
    }
    while (*pattern == '*')
        ++pattern;
    return !*pattern;
}
} // namespace
unsigned Application::NextFile(unsigned slot, unsigned output)
{
    auto &find = file_searches[slot];
    unsigned id;
    if (find.device) { last_error = 18; return 0; }
    // A FAT directory other than the root starts with its "." and ".."
    // entries, and FindFirstFileA returns them like any other: a program
    // that reads a directory it just made finds "." first. The Blitz3D
    // runtime of Wonderland takes a failed search of its new save directory
    // for a missing directory and goes on with a null object.
    while (find.dots < 2)
    {
        const char *dot = find.dots++ == 0 ? "." : "..";
        if (!Match(find.pattern, dot)) continue;
        const auto *self = files.store.Get(find.directory);
        // The directory went with its medium (Files::Store::ChangeMedium).
        if (!self) { last_error = 18; return 0; }
        const auto *node = find.dots == 2 && self->parent != Files::Invalid ? files.store.Get(self->parent) : self;
        unsigned data[80] = {};
        data[0] = 0x10;   // FILE_ATTRIBUTE_DIRECTORY
        memcpy((char *)data + 44, dot, strlen(dot) + 1);
        for(unsigned i=0;i<3;++i){auto stamp=node->times[i];Win98Time::Q value=0;Win98Time::DosService(1,stamp.date,stamp.time,stamp.hundredths,value,EffectiveTimeBias());data[1+i*2]=unsigned(value);data[2+i*2]=unsigned(value>>32);}
        return Store(output, data, sizeof data);
    }
    while (!files.store.EnumerateNext(find.directory, find.index, id))
    {
        const auto *node = files.store.Get(id);
        // A FAT directory search matches the long name or the 8.3 alias.
        if (!Match(find.pattern, node->name) && !(*node->alias && Match(find.pattern, node->alias)))
            continue;
        unsigned data[80] = {};
        data[0] = node->attributes;
        data[8] = node->size;
        memcpy((char *)data + 44, node->name, strlen(node->name) + 1);
        // cAlternateFileName at offset 130h, the field GetShortPathNameA reads.
        if (*node->alias)
            memcpy((char *)data + 304, node->alias, strlen(node->alias) + 1);
        for(unsigned i=0;i<3;++i){auto stamp=node->times[i];Win98Time::Q value=0;Win98Time::DosService(1,stamp.date,stamp.time,stamp.hundredths,value,EffectiveTimeBias());data[1+i*2]=unsigned(value);data[2+i*2]=unsigned(value>>32);}
        // The backend does not synthesize a DOS short-name alias.
        return Store(output, data, sizeof data);
    }
    last_error = 18;
    return 0;
}
unsigned Application::FileApi(ApiName name, const unsigned *a)
{
#define API(n) case API_ID(#n):
    const bool legacy_open = API_IS(name,"_lopen") || API_IS(name,"_lcreat");
    const bool read = API_IS(name,"ReadFile") || API_IS(name,"_lread") || API_IS(name,"_hread");
    const bool write = API_IS(name,"WriteFile") || API_IS(name,"_lwrite") || API_IS(name,"_hwrite");
    // Calls that work on a handle or build their own path.
    switch (name.id)
    {
    API(CloseHandle)
    {
        unsigned e = files.Close(a[0]);
        if (e)
            last_error = e;
        return !e;
    }
    API(_lopen) API(_lcreat) API(CreateFileA)
    {
        unsigned access, share, disposition, attrs, handle_flags=0;
        if (legacy_open)
        {
            bool create = API_IS(name,"_lcreat");
            access = create ? 3 : (a[1] & 3) == 0 ? 1 : (a[1] & 3) == 1 ? 2 : 3;
            static const unsigned shares[] = {3, 0, 1, 2, 3};
            if (!create && ((a[1] & ~0x73u) || (a[1] & 3) == 3 || ((a[1] >> 4) & 7) > 4))
            {
                last_error = 87;
                return Files::Invalid;
            }
            share = create ? 3 : shares[(a[1] >> 4) & 7];
            disposition = create ? 2 : 3;
            attrs = create ? a[1] & 0x27u : 0;
            if(create){
                const char *path=ReadString(a[0]);const unsigned length=strlen(path);
                if(length&&(path[length-1]=='\\'||path[length-1]=='/'))
                {last_error=123;return Files::Invalid;}
            }
        }
        else
        {
            // Local-file opens accept only null or -1 template handles. Other
            // values return ERROR_NOT_SUPPORTED.
            if (a[6] && a[6] != ~0u) { last_error = 50; return Files::Invalid; }
            if(a[3]&&!virtual_memory.Access(a[3],12,false)){last_error=87;return Files::Invalid;}
            struct E {Application&app;unsigned Read32(unsigned p){return app.Read32(p);}}e{*this};
            unsigned short mode=0x1080;
            unsigned error=Win98FileOpen::Parameters(e,a[3],a[1],a[2],mode,handle_flags);
            if(error){last_error=error;return Files::Invalid;}
            access=(mode&15)==2?3:(mode&15)==1?2:1;
            share = a[2];
            disposition = a[4];
            // Accept random/sequential-access, no-buffering and write-through
            // hints. They affect Windows 98 filesystem caching; this provider
            // has no additional cache and writes changes directly to the host
            // file. Local delete-on-close retains the full path and removes
            // the file when its last handle closes.
            if (a[5] & 0x10000000u) mode |= 0x0008;
            if (a[5] & 0x08000000u) mode |= 0x8000;
            if (a[5] & 0x20000000u) mode |= 0x0100;
            if (a[5] & 0x80000000u) mode |= 0x4000;
            // FILE_ATTRIBUTE_NORMAL alone becomes zero. Otherwise keep bits
            // in 0x67, then add FILE_ATTRIBUTE_ARCHIVE.
            attrs = (a[5] == 0x80 ? 0u : a[5] & 0x67u) | 0x20u;
        }
        unsigned handle, status;
        const char *filename = ReadString(a[0]), *leaf = filename;
        for (const char *p=filename; *p; ++p) if (*p=='\\'||*p=='/'||*p==':') leaf=p+1;
        const bool null_device = Fold(leaf[0])=='N' && leaf[0] &&
            Fold(leaf[1])=='U' && leaf[1] && Fold(leaf[2])=='L' &&
            (!leaf[3]||leaf[3]=='.');
        unsigned error = null_device ? files.OpenNull(access, handle) :
            files.Open(filename, access, share, disposition, attrs, handle, status);
        if(null_device)status=0;
        if (getenv("NEO98_TRACE_FILES"))
            fprintf(stderr, "FILE open path=%s access=%u disposition=%u error=%u\n", ReadString(a[0]), access, disposition, error);
        if(!error){
            if(!legacy_open)files.SetMappingAccess(handle,
                (a[1]&0x80000000u?1u:0u)|(a[1]&0x40000000u?2u:0u));
            files.SetInheritable(handle,(handle_flags&0x80000000u)!=0);
            if(!legacy_open&&(a[5]&0x04000000u)){
                char absolute[260];
                if(!files.FullPath(ReadString(a[0]),absolute))files.SetDeleteOnClose(handle,absolute);
            }
            // handle access retains FILE_WRITE_ATTRIBUTES even
            // when the DOS access mapper chooses a read-only file handle.
            if(!legacy_open)files.SetWriteAttributes(handle,(Win98FileOpen::HandleAccess(a[1])&0x100)!=0);
        }
        if (error || disposition == 2 || disposition == 4)
            last_error = error ? error : status;
        if (!error && legacy_open)
        {
            const unsigned legacy = files.LegacyHandle(handle);
            if (legacy == Files::Invalid) { files.Close(handle); last_error = 4; }
            return legacy;
        }
        return error ? Files::Invalid : handle;
    }
    API(_lclose)
    {
        unsigned e = files.Close(a[0]);
        if (e)
            last_error = e;
        return e ? Files::Invalid : 0;
    }
    API(ReadFile) API(_lread) API(_hread) API(WriteFile) API(_lwrite) API(_hwrite)
    {
        const bool legacy = name[0] == '_';
        if (!legacy && a[3] && !virtual_memory.Access(a[3], 4, true))
        { last_error = 87; return 0; }
        if (!legacy && a[3])
            Write32(a[3], 0);
        if (!legacy && (a[4] || !a[3]))
        {
            // Ordinary local Win98 files reject OVERLAPPED with
            // ERROR_INVALID_PARAMETER: KERNEL32 (ReadFile) and
            // (WriteFile). Jet uses that exact error to retry
            // with SetFilePointer and synchronous I/O.
            last_error = 87;
            return 0;
        }
        unsigned size = a[2], done = 0, error = 0;
        // Validate before copying: invalid user buffers are API failures, not
        // fatal faults in the host. Do not reproduce Win98's NULL read leak.
        if (size && !virtual_memory.Access(a[1], size, read))
        { last_error = write ? 1784 : 87; return legacy ? Files::Invalid : 0; }
        unsigned char *buffer = !error && size ? Files::AcquireBuffer(size) : nullptr;
        if (!error && size && !buffer)
            error = 8;
        if (!error && write && size && !Read(a[1], buffer, size))
            error = 998;
        if (!error)
            error = read              ? files.Read(a[0], buffer, size, done)
                    : legacy && !size ? files.Truncate(a[0])
                                      : files.Write(a[0], buffer, size, done);
        if (!error && read && done && !Store(a[1], buffer, done))
            error = 998;
        if (!error && write && log)
        {
            unsigned hash = 2166136261u;
            for (unsigned i = 0; i < done; ++i)
                hash = (hash ^ buffer[i]) * 16777619u;
            char text[128];
            snprintf(text, sizeof text, "file write bytes=%u hash=%08x", done, hash);
            log(text);
        }
        Files::ReleaseBuffer(buffer);
        if (error)
            last_error = error;
        if (!legacy && a[3])
            Write32(a[3], done);
        return legacy ? error ? Files::Invalid : done : !error;
    }
    API(SetFilePointer) API(_llseek)
    {
        bool legacy = name[0] == '_';
        unsigned method = legacy ? a[2] : a[3], position = 0;
        long long distance = (int)a[1];
        if (!legacy && a[2])
            distance = (long long)(int)Read32(a[2]) * 0x100000000LL + a[1];
        unsigned e = files.Seek(a[0], distance, method, position);
        if (!e && !legacy && a[2])
            Write32(a[2], 0);
        if (e || position == 0xffffffff)
            last_error = e;
        return e ? Files::Invalid : position;
    }
    API(GetFileSize)
    {
        unsigned size = 0, e = files.Size(a[0], size);
        if (e)
            last_error = e;
        else if (a[1])
            Write32(a[1], 0);
        return e ? Files::Invalid : size;
    }
    API(GetFileType)
    {
        if (files.Owns(a[0]))
            return files.IsNull(a[0]) ? 2 : 1;
        last_error = 6;
        return 0;
    }
    API(SetEndOfFile)
    {
        unsigned e = files.Truncate(a[0]);
        if (e)
            last_error = e;
        return !e;
    }
    API(FlushFileBuffers)
    {
        // All writes reach the RAM volume synchronously. This is not disk durability.
        if (files.Owns(a[0]))
            return 1;
        last_error = 6;
        return 0;
    }
    API(FindFirstFileA)
    {
        char input[260], directory[260];
        const char *path = ReadString(a[0]);
        if (strlen(path) >= sizeof input || !*path)
        {
            last_error = *path ? 206 : 3;
            return Files::Invalid;
        }
        strcpy(input, path);
        const char last=input[strlen(input)-1];
        if (!strchr(input, '*') && !strchr(input, '?') && last!='\\' && last!='/' && last!=':')
        {
            // Resolve exact names before searching their parent. In Win98,
            // 'name.txt.' finds name.txt, and '.'/'..' return the actual
            // directory name rather than their literal enumeration entries.
            char resolved[260];const unsigned error=files.FullPath(input,resolved);
            if (!error)strcpy(input,resolved);
            else if(error!=120){last_error=LookupNameError(input,error);return Files::Invalid;}
        }
        else
        {
            // Trailing spaces are ignored, but wildcard dots retain their
            // empty-extension meaning ('*.txt.' must not become '*.txt').
            unsigned length=strlen(input);while(length&&input[length-1]==' ')input[--length]=0;
        }
        char *slash = nullptr;
        for (char *p = input; *p; ++p)
            if (*p == '\\' || *p == '/' || *p == ':')
                slash = p;
        const char *pattern = slash ? slash + 1 : input;
        if (strlen(pattern) > 255)
        {
            last_error = 2;
            return Files::Invalid;
        }
        char folder[260];
        unsigned n = slash ? slash - input + 1 : 0;
        memcpy(folder, input, n);
        folder[n] = 0;
        unsigned id, e = files.FullPath(n ? folder : ".", directory);
        if (!e)
            e = files.store.Lookup(directory, id);
        if (!e && !(files.store.Get(id)->attributes & 16))
            e = 3;
        if (e)
        {
            last_error = e == 2 || e == 120 ? 3 : LookupNameError(input,e);
            return Files::Invalid;
        }
        // An existing drive/directory without a search pattern has no
        // matching entry, including a bare drive name such as "C:".
        if (!*pattern) { last_error=2; return Files::Invalid; }
        // An exact ".." resolves to its parent before the FAT lookup. A
        // drive root has no directory entry, unlike a wildcard enumeration
        // of the child's literal ".." record.
        if(!strcmp(pattern,"..")){
            const auto*node=files.store.Get(id);
            const auto*parent=node&&node->parent!=Files::Invalid?files.store.Get(node->parent):nullptr;
            if(!parent||parent->parent==Files::Invalid){last_error=2;return Files::Invalid;}
        }
        for (unsigned i = 0; i < FileSearchCapacity; ++i)
            if (!file_searches[i].used)
            {
                auto &find = file_searches[i];
                find.used = true;
                find.device = Files::IsDeviceName(pattern);
                find.directory = id;
                find.index = 0;
                // The root of a drive has no "." and "..".
                find.dots = files.store.Get(id)->parent == Files::Invalid ? 2 : 0;
                strcpy(find.pattern, pattern);
                if (find.device) {
                    unsigned data[80]{};data[0]=0x20;
                    memcpy((char*)data+44,pattern,strlen(pattern)+1);
                    if (!Store(a[1],data,sizeof data)) {find.used=false;return Files::Invalid;}
                    return 0x30000000|(find.generation<<8)|i;
                }
                if (NextFile(i, a[1]))
                    return 0x30000000 | (find.generation << 8) | i;
                find.used = false;
                last_error = 2;
                return Files::Invalid;
            }
        last_error = 4;
        return Files::Invalid;
    }
    API(FindNextFileA) API(FindClose)
    {
        unsigned slot = a[0] & 255;
        if ((a[0] & 0xff000000) != 0x30000000 || slot >= FileSearchCapacity || !file_searches[slot].used ||
            ((a[0] >> 8) & 65535) != file_searches[slot].generation)
        {
            last_error = 6;
            return 0;
        }
        if (API_IS(name,"FindNextFileA"))
            return NextFile(slot, a[1]);
        file_searches[slot].used = false;
        file_searches[slot].generation = file_searches[slot].generation % 65535 + 1;
        return 1;
    }
    API(GetWindowsDirectoryA) API(GetSystemDirectoryA)
    {
        const char *text = API_IS(name,"GetWindowsDirectoryA") ? "C:\\WINDOWS" : "C:\\WINDOWS\\SYSTEM";
        unsigned n = strlen(text);
        if (a[1] <= n) return n + 1;
        PutString(a[0], text, a[1]);
        return n;
    }
    API(GetFileInformationByHandle) API(GetFileAttributesExA) {
        const bool handle = API_IS(name,"GetFileInformationByHandle");
        unsigned output = a[handle ? 1 : 2];
        if (!handle && !a[0]) { last_error = 3; return 0; }
        if ((!handle && a[1]) || !output || !virtual_memory.Access(output, handle ? 52 : 36, true)) {
            last_error = 87; return 0;
        }
        const Files::Node *node = nullptr;
        unsigned error = 0;
        if (handle) error = files.Information(a[0], node);
        else {
            const char *path = ReadString(a[0]);
            // Original Windows 98 rejects either wildcard before lookup.
            if (strchr(path, '*') || strchr(path, '?')) error = 123;
            char absolute[260]; unsigned id;
            if (!error) error = files.FullPath(path, absolute);
            if (!error) error = files.store.Lookup(absolute, id);
            if (!error) node = files.store.Get(id);
        }
        if (error) { last_error = error; return 0; }
        unsigned data[13] = {};
        data[0] = node->attributes;
        for (unsigned i = 0; i < 3; ++i) {
            auto stamp = node->times[i]; Win98Time::Q value = 0;
            Win98Time::DosService(1, stamp.date, stamp.time, stamp.hundredths, value, EffectiveTimeBias());
            data[1 + i * 2] = unsigned(value); data[2 + i * 2] = unsigned(value >> 32);
        }
        if (handle) {
            data[7] = 0; // Same volatile-volume serial as GetVolumeInformationA.
            data[9] = node->size;
            data[10] = 1; // No hard links in this provider.
            data[11] = unsigned(node->identity >> 32); data[12] = unsigned(node->identity);
        } else data[8] = node->size;
        return Store(output, data, handle ? 52 : 36);
    }
    API(CopyFileA) {
        char source[260], destination[260];
        unsigned error = files.FullPath(ReadString(a[0]), source);
        if (!error) error = files.FullPath(ReadString(a[1]), destination);
        if (!error) error = files.Copy(source, destination, a[2] != 0);
        if (error) last_error = error;
        return !error;
    }
    API(CreateDirectoryExA) {
        char source[260], destination[260]; unsigned id;
        unsigned error = files.FullPath(ReadString(a[0]), source);
        if (!error) error = files.FullPath(ReadString(a[1]), destination);
        if (!error && a[2] && !virtual_memory.Access(a[2], 12, false)) error = 87;
        if (!error) error = files.store.Lookup(source, id);
        if (!error) {
            const unsigned attributes = files.store.Get(id)->attributes;
            // queries attributes, creates the directory and
            // applies attributes. A file is also accepted as a template.
            error = files.store.MakeDirectory(destination);
            if (!error) error = files.store.Attributes(destination, attributes);
        }
        if (error) last_error = error;
        return !error;
    }
    default:
        break;
    }
    // Calls on one path; they share the error handling behind the switch.
    char absolute[260];
    if ((API_IS(name,"CreateDirectoryA") || API_IS(name,"DeleteFileA")) &&
        (!a[0] || !*ReadString(a[0]))) { last_error = 3; return 0; }
    unsigned e = files.FullPath(ReadString(a[0]), absolute);
    if(API_IS(name,"GetFileAttributesA"))e=LookupNameError(ReadString(a[0]),e);
    if (e == 123 && API_IS(name,"GetFileAttributesA") &&
        (strchr(ReadString(a[0]), '*') || strchr(ReadString(a[0]), '?'))) e = 2;
    if (e == 120 && API_IS(name,"DeleteFileA")) e = 5;
    if (e)
    {
        last_error = e;
        return API_IS(name,"GetFileAttributesA") ? Files::Invalid : 0;
    }
    switch (name.id)
    {
    API(GetFileAttributesA)
    {
        if(log&&trace_calls){char line[300];snprintf(line,sizeof line,"FILE_QUERY path=%s",absolute);log(line);}
        unsigned id;
        e = files.store.Lookup(absolute, id);
        if (e)
            last_error = e;
        else if (!(files.store.Get(id)->attributes&16))
        {
            const char *input=ReadString(a[0]);const unsigned length=strlen(input);
            if(length&&(input[length-1]=='\\'||input[length-1]=='/')){last_error=123;return Files::Invalid;}
        }
        return e ? Files::Invalid : files.store.Get(id)->attributes;
    }
    // Win98 normalizes NORMAL and masks NT-only bits
    // before the DOS 7143h call. In particular, 0x2000 is ignored.
    API(SetFileAttributesA) e = files.store.Attributes(absolute, a[1] & 0x67u); break;
    // probes SECURITY_ATTRIBUTES; ignores its contents.
    // InstallShield supplies it when creating its extraction directory.
    API(CreateDirectoryA)
        e = a[1] && !virtual_memory.Access(a[1], 12, false) ? 87 : files.store.MakeDirectory(absolute);
        break;
    API(RemoveDirectoryA)
        // The process's current directory stays valid until it changes away.
        e = PathEqual(absolute, files.current) ? 16 : files.store.Remove(absolute, true); break;
    API(DeleteFileA) e = files.store.Remove(absolute, false); break;
    API(MoveFileA)
    {
        char destination[260];
        e = files.FullPath(ReadString(a[1]), destination);
        if (!e)
            e = files.store.Rename(absolute, destination);
        break;
    }
    default:
        break;
    }
    if (e)
        last_error = e;
    return !e;
#undef API
}
#include "x86_volume_information.inc"
#include "x86_file_times.inc"
unsigned Application::CommParseApi(ApiName name,const unsigned *a)
{
    const bool extended=API_IS(name,"BuildCommDCBAndTimeoutsA");
    if(!a[0]||!virtual_memory.Access(a[1],28,true)||(extended&&a[2]&&!virtual_memory.Access(a[2],20,true))){last_error=87;return 0;}
    unsigned char dcb[28],timeouts[20];Read(a[1],dcb,28);if(extended&&a[2])Read(a[2],timeouts,20);
    const bool result=Win98CommParse::Build(ReadString(a[0]),dcb,extended&&a[2]?timeouts:nullptr);
    Store(a[1],dcb,28);if(result&&extended&&a[2])Store(a[2],timeouts,20);
    if(!result)last_error=87;return result;
}
#include "x86_file_extended.inc"
#include "x86_pipes.inc"
#include "x86_console.inc"
#include "x86_multimedia.inc"
#include "x86_lz.inc"
} // namespace X86
