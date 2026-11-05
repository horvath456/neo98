/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* each fwrite to one FILE must remain a whole record. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
static FILE *stream;
static HANDLE start;
static DWORD WINAPI Writer(void *argument)
{
    char row[257];
    memset(row, 'A' + (int)(ULONG_PTR)argument, sizeof row - 1);
    row[sizeof row - 1] = '\n';
    WaitForSingleObject(start, INFINITE);
    for (int i = 0; i < 200; ++i)
    {
        if (fwrite(row, 1, sizeof row, stream) != sizeof row) return 1;
        if (i % 7 == 0) Sleep(0);
    }
    return 0;
}
int main(void)
{
    HANDLE threads[4];
    int checks = 0, failures = 0, counts[4] = {0};
    char row[257];
    stream = tmpfile();
    if (!stream) return 1;
    start = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!start) return 2;
    for (int i = 0; i < 4; ++i)
    {
        threads[i] = CreateThread(NULL, 0, Writer, (void *)(ULONG_PTR)i, 0, NULL);
        if (!threads[i]) return 3;
    }
    SetEvent(start);
    for (int i = 0; i < 4; ++i)
    {
        DWORD code = 1;
        WaitForSingleObject(threads[i], INFINITE);
        GetExitCodeThread(threads[i], &code);
        ++checks; failures += code != 0;
        CloseHandle(threads[i]);
    }
    rewind(stream);
    while (fread(row, 1, sizeof row, stream) == sizeof row)
    {
        ++checks;
        int index = row[0] - 'A';
        if (index < 0 || index > 3) { ++failures; continue; }
        ++counts[index];
        int valid = row[256] == '\n';
        for (int i = 0; i < 256; ++i) valid &= row[i] == row[0];
        failures += !valid;
    }
    for (int i = 0; i < 4; ++i) { ++checks; failures += counts[i] != 200; }
    fclose(stream);
    CloseHandle(start);
    printf("STDIO_THREADS checks=%d failures=%d\n", checks, failures);
    return failures;
}
