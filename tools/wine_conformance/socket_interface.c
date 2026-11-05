/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <winsock2.h>
#include "wine/test.h"

typedef DWORD (WINAPI *wscontrol_fn)(DWORD, DWORD, void *, DWORD *, void *, DWORD *);

START_TEST(socket_interface)
{
    HMODULE module = LoadLibraryA("wsock32.dll");
    wscontrol_fn control = (wscontrol_fn)GetProcAddress(module, "WsControl");
    DWORD addresses[] = {0x0100007f, 0, 0x04030201, 0};
    const DWORD capacities[] = {0, 1, 15, 16, 20, 22, 32, 128};
    WSADATA data;
    unsigned i, j, k, address_count = 3;
    ok(control != NULL, "WsControl export\n");
    ok(WSAStartup(MAKEWORD(2, 2), &data) == 0, "Winsock startup\n");
    if (!control) return;
    {
        DWORD request[9] = {0x301, 0, 0x200, 0x100, 0x102, 0, 0, 0, 0};
        DWORD table[96], input_size = sizeof(request), size = sizeof(table);
        if (!control(6, 0, request, &input_size, table, &size))
            for (i = 0; i + 24 <= size; i += 24)
                if (table[i / 4] != 0x0100007f) {
                    addresses[3] = table[i / 4]; address_count = 4; break;
                }
    }
    for (i = 0; i < address_count; ++i) for (j = 0; j < 8; ++j) {
        DWORD request[9] = {0x301, 0, 0x200, 0x100, 0x103, 0, 0, 0, 0};
        DWORD output[40], input_size = sizeof(request), size = capacities[j], result;
        request[5] = addresses[i];
        memset(output, 0xa5, sizeof(output));
        result = control(6, 0, request, &input_size, output, &size);
        trace("%s address=%08lx capacity=%lu result=%08lx size=%lu words=%08lx,%08lx,%08lx,%08lx,%08lx,%08lx\n",
              i == 3 ? "SOCKET_PRIMARY" : "SOCKET_INTERFACE", addresses[i], capacities[j], result, size,
              output[0], output[1], output[2], output[3], output[4], output[5]);
        for (k = capacities[j]; k < sizeof(output); ++k)
            if (((BYTE *)output)[k] != 0xa5) break;
        ok(k == sizeof(output), "write beyond capacity %lu at byte %u\n", capacities[j], k);
    }
    WSACleanup();
    FreeLibrary(module);
}
