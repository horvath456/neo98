/* SPDX-License-Identifier: GPL-2.0-or-later */
/* execute the unchanged demo texture-creation function. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U VirtualProtect(void *, U, U, U *);
API void ExitProcess(U);
#include "descent-dispatch-original.h"

/* Keep the original function and its absolute globals inside this PE image,
   ahead of the process heap. The original EXE is not loaded or modified. */
volatile unsigned char original_address_space[0x1000000];
static U bitmaps[4], lightmaps[4], bumpmaps[4], table[7], object;
static U upload_type, upload_slot, format_size, create_calls;
static U dimensions(void) { return 128; }
static U format(void) { return 0; }
static U __stdcall create(U self, const U *description, U *output, U outer)
{
    (void)self; (void)outer;
    ++create_calls;
    format_size = description[18];
    *output = 0x12345678;
    return 0;
}
static void upload(U handle, U type, U slot, U fresh)
{
    (void)handle; (void)fresh;
    upload_type = type; upload_slot = slot;
}
static void jump(U address, void *provider)
{
    unsigned char *p = (unsigned char *)address;
    p[0] = 0xe9;
    *(U *)(p + 1) = (U)provider - address - 5;
}
void mainCRTStartup(void)
{
    original_address_space[0] = 0;
    U old;
    if (!VirtualProtect((void *)0x4e2000, 0x2000, 0x40, &old) ||
        !VirtualProtect((void *)0x517000, 0x2000, 0x40, &old)) ExitProcess(1);
    for (U i = 0; i < sizeof original_dispatch; ++i)
        ((unsigned char *)0x517f00)[i] = original_dispatch[i];
    jump(0x4e3b8a, dimensions); jump(0x4e3b9b, dimensions);
    jump(0x4e20e5, dimensions); jump(0x4e216a, dimensions);
    jump(0x4e34d8, format); jump(0x5179d0, upload);
    table[6] = (U)create; object = (U)table;
    *(U *)0x1302f24 = (U)&object;
    *(U *)0x12fd4b8 = (U)bitmaps;
    *(U *)0x13015a0 = (U)lightmaps;
    *(U *)0x1302f50 = (U)bumpmaps;
    *(U *)0x12fde28 = 32; /* ordinary texture pixel format size */
    typedef U (*Dispatch)(U, U);
    for (U type = 0; type < 3; ++type)
        for (U slot = 0; slot < 4; slot += 3)
        {
            for (U i = 0; i < 4; ++i)
                bitmaps[i] = lightmaps[i] = bumpmaps[i] = 0xcccccccc;
            *(unsigned char *)(0x109a276 + slot * 20) = 128;
            create_calls = 0; upload_type = upload_slot = ~0u;
            U result = ((Dispatch)0x517f00)(slot, type);
            if (result != slot || create_calls != 1 || upload_slot != slot) ExitProcess(2);
            if (!type)
            {
                if (bitmaps[slot] != 0x12345678 || lightmaps[slot] != 0xcccccccc || upload_type) ExitProcess(3);
            }
            else if (type == 1)
            {
                if (lightmaps[slot] != 0x12345678 || bitmaps[slot] != 0xcccccccc || upload_type != 1) ExitProcess(4);
            }
            else
            {
                /* Original defect: a bump map writes the lightmap table,
                   then uploads as a bitmap from the untouched bitmap table. */
                if (lightmaps[slot] != 0x12345678 || bitmaps[slot] != 0xcccccccc ||
                    bumpmaps[slot] != 0xcccccccc || upload_type || format_size) ExitProcess(5);
            }
        }
    ExitProcess(0);
}
