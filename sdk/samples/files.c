/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* A console program that works with files and memory: writes a file, reads
   it back, lists the directory it lies in, and uses the heap and VirtualAlloc. */
#include <windows.h>

static HANDLE output;
static void Print(const char *text)
{
    DWORD written;
    WriteFile(output, text, (DWORD)lstrlenA(text), &written, NULL);
}

int main(int argc, char **argv)
{
    char directory[MAX_PATH], path[MAX_PATH], line[400];
    DWORD count;
    output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (argc > 1) lstrcpynA(directory, argv[1], MAX_PATH - 16);
    else GetTempPathA(MAX_PATH - 16, directory);
    if (directory[lstrlenA(directory) - 1] != '\\') lstrcatA(directory, "\\");

    wsprintfA(path, "%sARM64.TXT", directory);
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        wsprintfA(line, "cannot create %s, error %u\r\n", path, GetLastError());
        Print(line);
        return 1;
    }
    for (int i = 1; i <= 3; ++i)
    {
        wsprintfA(line, "line %d\r\n", i);
        WriteFile(file, line, (DWORD)lstrlenA(line), &count, NULL);
    }
    CloseHandle(file);

    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD size = GetFileSize(file, NULL);
    char *text = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size + 1);
    ReadFile(file, text, size, &count, NULL);
    CloseHandle(file);
    wsprintfA(line, "read %u of %u bytes\r\n", count, size);
    Print(line);
    Print(text);
    HeapFree(GetProcessHeap(), 0, text);

    wsprintfA(path, "%sARM64.*", directory);
    WIN32_FIND_DATAA found;
    HANDLE find = FindFirstFileA(path, &found);
    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            wsprintfA(line, "found %s, %u bytes\r\n", found.cFileName, found.nFileSizeLow);
            Print(line);
        } while (FindNextFileA(find, &found));
        FindClose(find);
    }

    /* A megabyte of pages, committed and touched at both ends. */
    unsigned char *pages = (unsigned char *)VirtualAlloc(NULL, 1 << 20, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    pages[0] = 1;
    pages[(1 << 20) - 1] = 2;
    MEMORY_BASIC_INFORMATION info;
    VirtualQuery(pages, &info, sizeof info);
    wsprintfA(line, "pages: %u KiB committed, sum %d\r\n", (unsigned)(info.RegionSize >> 10), pages[0] + pages[(1 << 20) - 1]);
    Print(line);
    VirtualFree(pages, 0, MEM_RELEASE);

    wsprintfA(path, "%sARM64.TXT", directory);
    DeleteFileA(path);
    Print(GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES ? "deleted\r\n" : "still there\r\n");
    return 0;
}
