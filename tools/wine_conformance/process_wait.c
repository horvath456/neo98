/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"

START_TEST(process_wait)
{
    HANDLE process[4], event, pair[2];
    unsigned i;
    process[0] = GetCurrentProcess();
    process[1] = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, GetCurrentProcessId());
    process[2] = OpenProcess(SYNCHRONIZE, FALSE, GetCurrentProcessId());
    process[3] = NULL;
    ok(DuplicateHandle(process[0], process[0], process[0], &process[3],
                       0, FALSE, DUPLICATE_SAME_ACCESS), "duplicate self %lu\n", GetLastError());
    event = CreateEventA(NULL, TRUE, TRUE, NULL);
    ok(event != NULL, "event\n");
    for (i = 0; i < 4; ++i)
    {
        DWORD single, multiple, all, reversed, code = 0;
        ok(process[i] != NULL, "process %u\n", i);
        if (!process[i]) continue;
        pair[0] = process[i]; pair[1] = event;
        single = WaitForSingleObject(process[i], 0);
        multiple = WaitForMultipleObjects(2, pair, FALSE, 0);
        all = WaitForMultipleObjects(2, pair, TRUE, 0);
        pair[0] = event; pair[1] = process[i];
        reversed = WaitForMultipleObjects(2, pair, FALSE, 0);
        ok(single == WAIT_TIMEOUT, "self single %u: %lu\n", i, single);
        ok(multiple == WAIT_OBJECT_0 + 1, "self+event %u: %lu\n", i, multiple);
        ok(all == WAIT_TIMEOUT, "self+event all %u: %lu\n", i, all);
        ok(reversed == WAIT_OBJECT_0, "event+self %u: %lu\n", i, reversed);
        ok(GetExitCodeProcess(process[i], &code) && code == STILL_ACTIVE,
           "self exit status %u: %lu\n", i, code);
        trace("PROCESS_WAIT kind=%u single=%lu any=%lu all=%lu reversed=%lu code=%lu\n",
              i, single, multiple, all, reversed, code);
        if (i) CloseHandle(process[i]);
    }
    CloseHandle(event);
}
