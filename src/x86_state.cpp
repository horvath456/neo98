// SPDX-License-Identifier: GPL-2.0-or-later
// The streams of a saved state and the small things its parts share
// (src/x86_state.h). New infrastructure of this
// port.
#include "x86_state.h"
#include "x86_state_fields.h"
#include "x86_state_parts.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__) || defined(__linux__)
#include <sys/mman.h>
#endif
#ifdef __linux__
#include "../tools/linux_state_platform.h"
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#endif
namespace MachineState
{
// ---- Streams over a file of the host ------------------------------------------
namespace
{
struct FileSinkStream final : Sink
{
    FILE *file = nullptr;
    unsigned long long written = 0;
    ~FileSinkStream() override
    {
        if (file)
            fclose(file);
    }
    bool Write(const void *bytes, size_t count) override
    {
        written += count;
        return !count || fwrite(bytes, 1, count, file) == count;
    }
};
struct FileSourceStream final : Source
{
    FILE *file = nullptr;
    ~FileSourceStream() override
    {
        if (file)
            fclose(file);
    }
    bool Read(void *bytes, size_t count) override { return !count || fread(bytes, 1, count, file) == count; }
};
// The streams are objects of the host process (src/state_rules/state.rules).
FileSinkStream sink_stream;
FileSourceStream source_stream;
constexpr size_t Page = 4096;
bool ZeroPage(const unsigned char *page)
{
    uint64_t words[Page / 8];
    memcpy(words, page, Page);
    for (size_t i = 0; i < Page / 8; ++i)
        if (words[i])
            return false;
    return true;
}
} // namespace
Sink *FileSink(const char *path)
{
    FILE *file = fopen(path, "wb");
    if (!file)
        return nullptr;
    setvbuf(file, nullptr, _IOFBF, 1 << 20);
    if (sink_stream.file)
        fclose(sink_stream.file);
    sink_stream.file = file;
    sink_stream.written = 0;
    return &sink_stream;
}
Source *FileSource(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return nullptr;
    setvbuf(file, nullptr, _IOFBF, 1 << 20);
    if (source_stream.file)
        fclose(source_stream.file);
    source_stream.file = file;
    return &source_stream;
}
unsigned long long Written(const Sink *sink)
{
    return static_cast<const FileSinkStream *>(sink)->written;
}
void CloseSink(Sink *sink)
{
    auto *stream = static_cast<FileSinkStream *>(sink);
    if (stream->file)
        fclose(stream->file);
    stream->file = nullptr;
}
void CloseSource(Source *source)
{
    auto *stream = static_cast<FileSourceStream *>(source);
    if (stream->file)
        fclose(stream->file);
    stream->file = nullptr;
}
bool Put(Sink &sink, const void *bytes, size_t count)
{
    return sink.Write(bytes, count);
}
bool Put64(Sink &sink, unsigned long long value)
{
    return sink.Write(&value, 8);
}
bool Get(Source &source, void *bytes, size_t count)
{
    return source.Read(bytes, count);
}
bool Get64(Source &source, unsigned long long &value)
{
    return source.Read(&value, 8);
}
// The bytes of a range without its zero pages: (offset, length, bytes)... and
// an end mark.
bool PutPages(Sink &sink, const unsigned char *base, unsigned long long bytes)
{
    unsigned long long offset = 0;
    while (offset < bytes)
    {
        const unsigned long long step = bytes - offset < Page ? bytes - offset : Page;
        if (step == Page && ZeroPage(base + offset))
        {
            offset += step;
            continue;
        }
        unsigned long long end = offset + step;
        while (end < bytes && bytes - end >= Page && !ZeroPage(base + end))
            end += Page;
        if (!Put64(sink, offset) || !Put64(sink, end - offset) || !Put(sink, base + offset, end - offset))
            return false;
        offset = end;
    }
    return Put64(sink, ~0ull);
}
bool GetPages(Source &source, unsigned char *bytes, unsigned long long count, bool &zero)
{
    unsigned long long offset = 0;
    if (!Get64(source, offset))
        return false;
    zero = offset == ~0ull;
    if (zero)
        return true;
    memset(bytes, 0, count);
    for (;;)
    {
        unsigned long long length = 0;
        if (!Get64(source, length) || offset > count || length > count - offset || !Get(source, bytes + offset, length) ||
            !Get64(source, offset))
            return false;
        if (offset == ~0ull)
            return true;
    }
}
void *ReserveWindow(const char *name, unsigned long long bytes)
{
#if defined(__APPLE__) || defined(__linux__)
    void *reserved = mmap(nullptr, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    if (reserved == MAP_FAILED)
        return nullptr;
    Region(name, reserved, bytes);
    return reserved;
#else
    (void)name;
    (void)bytes;
    return nullptr;
#endif
}
bool BuildIdentity(unsigned char out[16])
{
#if defined(__APPLE__)
    const auto *image = reinterpret_cast<const mach_header_64 *>(_dyld_get_image_header(0));
    const auto *command = reinterpret_cast<const load_command *>(image + 1);
    for (unsigned i = 0; i < image->ncmds; ++i)
    {
        if (command->cmd == LC_UUID)
        {
            memcpy(out, reinterpret_cast<const uuid_command *>(command)->uuid, 16);
            return true;
        }
        command = reinterpret_cast<const load_command *>(reinterpret_cast<const char *>(command) + command->cmdsize);
    }
#endif
#ifdef __linux__
    if (LinuxStatePlatform::Identity(out)) return true;
#endif
    memset(out, 0, 16);
    return false;
}
} // namespace MachineState
