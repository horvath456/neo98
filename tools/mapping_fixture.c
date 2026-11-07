/* SPDX-License-Identifier: GPL-2.0-or-later
 * Own x86 contracts for anonymous Win98 KERNEL32 mapping objects. The original
 * process shares one object reservation: repeated offsets return the same
 * address with separate view references. This is not a multiprocess test.
 */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U CreateFileMappingA(U, const void *, U, U, U, const char *);
API U OpenFileMappingA(U, U, const char *);
API U MapViewOfFile(U, U, U, U, U);
API U MapViewOfFileEx(U, U, U, U, U, void *);
API U UnmapViewOfFile(const void *);
API U CloseHandle(U);
API U GetLastError(void);
API void SetLastError(U);
API U VirtualQuery(const void *, void *, U);
API U VirtualFree(void *, U, U);
API U VirtualAlloc(void *, U, U, U);
API U CreateEventA(const void *, U, U, const char *);
API U OpenEventA(U, U, const char *);
API U CreateMutexA(const void *, U, const char *);
API U OpenMutexA(U, U, const char *);
API U CreateSemaphoreA(const void *, int, int, const char *);
API U OpenSemaphoreA(U, U, const char *);
API U CreateThread(U, U, U(__stdcall *)(U), U, U, U *);
API U WaitForSingleObject(U, U);
API U GetExitCodeThread(U, U *);
API void ExitProcess(U);
API U CreateFileA(const char *, U, U, const void *, U, U, U);
API U WriteFile(U, const void *, U, U *, void *);
API U ReadFile(U, void *, U, U *, void *);
API U SetFilePointer(U, int, int *, U);
API U GetFileSize(U, U *);
API U FlushViewOfFile(const void *, U);
API U DeleteFileA(const char *);

typedef struct
{
    U base, allocation, allocation_protection, size, state, protection, type;
} Info;
enum
{
    PAGE_READONLY = 2,
    PAGE_READWRITE = 4,
    FILE_MAP_WRITE = 2,
    FILE_MAP_READ = 4,
    FILE_MAP_ALL_ACCESS = 0xf001f,
    MEM_COMMIT = 0x1000,
    MEM_RESERVE = 0x2000,
    MEM_FREE = 0x10000,
    MEM_MAPPED = 0x40000
};
static const char worker_name[] = "neo98-mapping-worker";

static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static void mapped(U address, U allocation, U protection, U code)
{
    Info info;
    check(VirtualQuery((void *)address, &info, sizeof info) == sizeof info && info.allocation == allocation &&
              info.state == MEM_COMMIT && info.type == MEM_MAPPED && info.protection == protection,
          code);
}
static void released(U address, U code)
{
    Info info;
    /* Neighbouring free regions can coalesce; their exact size is not an ABI. */
    check(VirtualQuery((void *)address, &info, sizeof info) == sizeof info && info.state == MEM_FREE &&
              !info.allocation && !info.type,
          code);
}

static void partial_view(void);

static void shared_lifetime(void)
{
    const char name[] = "neo98-mapping-lifetime";
    U security[] = {12, 0, 1};
    SetLastError(98);
    U first = CreateFileMappingA(~0u, security, PAGE_READWRITE, 0, 1024, name);
    check(first && !GetLastError(), 3900);
    U base = MapViewOfFile(first, FILE_MAP_WRITE, 0, 0, 0);
    /* Auto placement comes from the shared PageReserve provider. The mapping
     * body requires aligned offsets, but does not guarantee a 64-KiB base. */
    check(base && !(base & 4095), 3901);
    mapped(base, base, PAGE_READWRITE, 3902);
    volatile U *bytes = (void *)base;
    for (U i = 0; i < 256; ++i)
        check(!bytes[i], 3903);
    bytes[0] = 0x9898abcd;
    bytes[255] = 0x11223344;
    U second = MapViewOfFile(first, FILE_MAP_WRITE, 0, 0, 1024);
    check(second == base && ((volatile U *)second)[255] == 0x11223344, 3904);
    /* Existing-name lookup precedes validation of the new size/protection. */
    U same = CreateFileMappingA(~0u, 0, PAGE_READONLY, 123, 0, name);
    check(same && same != first && GetLastError() == 183, 3905);
    U opened = OpenFileMappingA(0, 1, name);
    check(opened && opened != first && opened != same, 3906);
    /* This Win98 Open body ignores desiredAccess; object protection governs Map. */
    U third = MapViewOfFile(opened, FILE_MAP_WRITE, 0, 0, 1);
    check(third == base && ((volatile U *)third)[0] == 0x9898abcd, 3907);
    ((volatile U *)third)[1] = 0xfeed1234;
    check(bytes[1] == 0xfeed1234, 3908);
    check(CloseHandle(first), 3909);
    check(!MapViewOfFile(first, FILE_MAP_WRITE, 0, 0, 0) && GetLastError() == 6, 3910);
    check(CloseHandle(same) && CloseHandle(opened), 3911);
    mapped(base, base, PAGE_READWRITE, 3912);
    check(bytes[0] == 0x9898abcd && bytes[255] == 0x11223344, 3913);
    /* Live views retain both the object and its name after the last handle closes. */
    U retained = OpenFileMappingA(FILE_MAP_READ, 0, name);
    check(retained && !CreateEventA(0, 0, 0, name) && GetLastError() == 6, 3914);
    check(CloseHandle(retained), 3915);
    check(!UnmapViewOfFile((void *)(base + 4)) && GetLastError() == 487, 3916);
    check(UnmapViewOfFile((void *)base), 3917);
    mapped(base, base, PAGE_READWRITE, 3918);
    check(UnmapViewOfFile((void *)base) && bytes[1] == 0xfeed1234, 3919);
    mapped(base, base, PAGE_READWRITE, 3920);
    check(UnmapViewOfFile((void *)base), 3921);
    released(base, 3922);
    check(!UnmapViewOfFile((void *)base) && GetLastError() == 487, 3923);
    check(!OpenFileMappingA(FILE_MAP_READ, 0, name) && GetLastError() == 123, 3924);
    U replacement = CreateFileMappingA(~0u, 0, PAGE_READWRITE, 0, 1024, name);
    check(replacement && replacement != first && !GetLastError(), 3925);
    U fresh = MapViewOfFile(replacement, FILE_MAP_WRITE, 0, 0, 0);
    check(fresh && !*(volatile U *)fresh && !((volatile U *)fresh)[255], 3926);
    check(UnmapViewOfFile((void *)fresh), 3927);
    /* Removing the last view does not retire a still-open mapping object. */
    mapped(fresh, fresh, PAGE_READWRITE, 3928);
    U again = MapViewOfFile(replacement, FILE_MAP_WRITE, 0, 0, 0);
    check(again == fresh, 3929);
    *(volatile U *)again = 0x55aa;
    check(UnmapViewOfFile((void *)again), 3930);
    U persisted = MapViewOfFile(replacement, FILE_MAP_WRITE, 0, 0, 0);
    check(persisted == fresh && *(volatile U *)persisted == 0x55aa, 3931);
    check(UnmapViewOfFile((void *)persisted) && CloseHandle(replacement), 3932);
    released(fresh, 3933);
    partial_view();
}

/* A view shorter than its object. MapViewOfFile commits only the length the
 * caller asks for, so an object of 11.140 bytes mapped with length 1.140 has
 * one committed page and not three. Unmapping such a view must succeed and
 * must not touch the pages behind it. jesterrun0.dll does exactly this.
 * */
static void partial_view(void)
{
    const char name[] = "neo98-mapping-partial";
    U object = CreateFileMappingA(~0u, 0, PAGE_READWRITE, 0, 11140, name);
    check(object && !GetLastError(), 3960);
    U base = MapViewOfFile(object, FILE_MAP_WRITE, 0, 0, 1140);
    check(base && !(base & 4095), 3961);
    volatile U *bytes = (void *)base;
    bytes[0] = 0x4711;
    bytes[284] = 0x1234;
    /* The page behind the committed one is reserved and not committed. */
    Info info;
    check(VirtualQuery((void *)(base + 8192), &info, sizeof info) == sizeof info &&
              info.state == MEM_RESERVE,
          3962);
    U opened = OpenFileMappingA(0, 0, name);
    check(opened && opened != object, 3963);
    U again = MapViewOfFile(opened, FILE_MAP_WRITE, 0, 0, 1144);
    check(again == base && ((volatile U *)again)[0] == 0x4711, 3964);
    check(UnmapViewOfFile((void *)again), 3965);
    check(UnmapViewOfFile((void *)base), 3966);
    check(CloseHandle(opened) && CloseHandle(object), 3967);
    released(base, 3968);
}

static void ranges_and_ex(void)
{
    U h = CreateFileMappingA(~0u, 0, PAGE_READWRITE, 0, 0x21001, 0);
    check(h != 0, 3934);
    check(!MapViewOfFile(h, FILE_MAP_WRITE, 0, 1, 0) && GetLastError() == 87, 3935);
    check(!MapViewOfFile(0, FILE_MAP_WRITE, 0, 1, 0) && GetLastError() == 87, 3936);
    check(!MapViewOfFile(h, FILE_MAP_WRITE, 1, 0, 1) && GetLastError() == 8, 3937);
    check(!MapViewOfFile(h, FILE_MAP_WRITE, 0, 0x30000, 0) && GetLastError() == 8, 3938);
    check(!MapViewOfFile(h, FILE_MAP_WRITE, 0, 0, 0x21002) && GetLastError() == 8, 3939);
    check(!MapViewOfFile(h, FILE_MAP_WRITE, 0, 0x20000, 0x1002) && GetLastError() == 8, 3940);
    check(!MapViewOfFile(h, FILE_MAP_WRITE, 0, 0x7fc00000, 0) && GetLastError() == 8, 3941);
    check(!MapViewOfFile(h, FILE_MAP_WRITE, 0, 0, 0x7fc00000) && GetLastError() == 8, 3942);
    check(!MapViewOfFileEx(h, FILE_MAP_WRITE, 0, 0, 4096, (void *)0x70000000) && GetLastError() == 487, 3943);
    check(!MapViewOfFileEx(h, FILE_MAP_WRITE, 0, 0, 4096, (void *)0xc0000000) && GetLastError() == 487, 3944);
    U base = MapViewOfFileEx(h, FILE_MAP_WRITE, 0, 0, 4096, (void *)0x90030000);
    check(base == 0x90030000, 3945);
    mapped(base, base, PAGE_READWRITE, 3946);
    *(volatile U *)base = 0x11119898;
    U middle = MapViewOfFile(h, FILE_MAP_WRITE, 0, 0x10000, 0);
    check(middle == base + 0x10000, 3947);
    mapped(middle, base, PAGE_READWRITE, 3948);
    volatile unsigned char *p = (void *)middle;
    check(!p[0] && !p[0x11000], 3949);
    p[0] = 0x5a;
    p[0x11000] = 0xa5;
    U tail = MapViewOfFileEx(h, FILE_MAP_WRITE, 0, 0x20000, 0x1001, (void *)(base + 0x20000));
    check(tail == base + 0x20000 && ((volatile unsigned char *)tail)[0x1000] == 0xa5, 3950);
    U all = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    check(all == base && ((volatile unsigned char *)all)[0x10000] == 0x5a &&
              ((volatile unsigned char *)all)[0x21000] == 0xa5,
          3951);
    check(!MapViewOfFileEx(h, FILE_MAP_WRITE, 0, 0, 1, (void *)(base + 0x10000)) && GetLastError() == 487,
          3952);
    check(!UnmapViewOfFile((void *)(middle + 4096)) && GetLastError() == 487, 3953);
    check(UnmapViewOfFile((void *)middle) && UnmapViewOfFile((void *)tail), 3954);
    check(UnmapViewOfFile((void *)all) && UnmapViewOfFile((void *)base), 3955);
    mapped(base, base, PAGE_READWRITE, 3956);
    check(CloseHandle(h), 3957);
    released(base, 3958);
    released(base + 0x20000, 3959);
}

static void protection_and_ownership(void)
{
    U read = CreateFileMappingA(~0u, 0, PAGE_READONLY, 0, 4096, 0);
    check(read != 0, 3960);
    check(!MapViewOfFile(read, FILE_MAP_WRITE, 0, 0, 0) && GetLastError() == 87, 3961);
    check(!MapViewOfFile(read, FILE_MAP_ALL_ACCESS, 0, 0, 0) && GetLastError() == 87, 3962);
    U base = MapViewOfFile(read, FILE_MAP_READ, 0, 0, 0);
    check(base && !*(volatile U *)base, 3963);
    mapped(base, base, PAGE_READONLY, 3964);
    check(CloseHandle(read), 3965);
    mapped(base, base, PAGE_READONLY, 3966);
    check(UnmapViewOfFile((void *)base), 3967);
    released(base, 3968);

    U writable = CreateFileMappingA(~0u, 0, PAGE_READWRITE, 0, 4096, 0);
    check(writable != 0, 3969);
    base = MapViewOfFile(writable, FILE_MAP_READ, 0, 0, 0);
    check(base && !*(volatile U *)base, 3970);
    mapped(base, base, PAGE_READONLY, 3971);
    /* Mapping ownership is an ARM backend rule: VirtualAlloc/VirtualFree may
     * not consume an object's reservation or commit over its protected pages. */
    check(!VirtualFree((void *)base, 0, 0x8000), 3972);
    check(!VirtualFree((void *)base, 4096, 0x4000), 3973);
    check(!VirtualAlloc((void *)base, 4096, MEM_COMMIT, PAGE_READWRITE), 3974);
    mapped(base, base, PAGE_READONLY, 3975);
    check(!*(volatile U *)base && UnmapViewOfFile((void *)base) && CloseHandle(writable), 3976);
    released(base, 3977);
}

static void namespace_and_errors(void)
{
    const char name[] = "neo98-mapping-namespace";
    U h = CreateFileMappingA(~0u, 0, PAGE_READWRITE, 0, 1024, name);
    check(h != 0, 3978);
    check(!CreateEventA(0, 0, 0, name) && GetLastError() == 6, 3979);
    check(!OpenEventA(0x1f0003, 0, name) && GetLastError() == 6, 3980);
    check(!CreateMutexA(0, 0, name) && GetLastError() == 6, 3981);
    check(!OpenMutexA(0x1f0001, 0, name) && GetLastError() == 6, 3982);
    check(!CreateSemaphoreA(0, 0, 1, name) && GetLastError() == 6, 3983);
    check(!OpenSemaphoreA(0x1f0003, 0, name) && GetLastError() == 6, 3984);
    check(CloseHandle(h), 3985);
    for (U kind = 0; kind < 3; ++kind)
    {
        U object = kind == 0   ? CreateEventA(0, 0, 0, name)
                   : kind == 1 ? CreateMutexA(0, 0, name)
                               : CreateSemaphoreA(0, 0, 1, name);
        check(object != 0, 3986 + kind * 4);
        check(!CreateFileMappingA(~0u, 0, PAGE_READWRITE, 0, 1024, name) && GetLastError() == 6,
              3987 + kind * 4);
        check(!OpenFileMappingA(FILE_MAP_READ, 0, name) && GetLastError() == 6, 3988 + kind * 4);
        check(!MapViewOfFile(object, FILE_MAP_WRITE, 0, 0, 0) && GetLastError() == 6 && CloseHandle(object),
              3989 + kind * 4);
    }
    check(!OpenFileMappingA(FILE_MAP_READ, 0, name) && GetLastError() == 123, 3998);
    check(!MapViewOfFile(0, FILE_MAP_WRITE, 0, 0, 0) && GetLastError() == 6, 3999);
    check(!MapViewOfFile(~0u, FILE_MAP_WRITE, 0, 0, 0) && GetLastError() == 6, 4000);
    check(!UnmapViewOfFile(0) && GetLastError() == 487, 4001);
    check(!CreateFileMappingA(~0u, 0, PAGE_READWRITE, 1, 1024, 0) && GetLastError() == 8, 4002);
    check(!CreateFileMappingA(~0u, 0, PAGE_READWRITE, 0, 0, name) && GetLastError() == 87, 4003);
    check(!OpenFileMappingA(FILE_MAP_READ, 0, name) && GetLastError() == 123, 4004);
    U security[] = {8, 0, 1};
    check(!CreateFileMappingA(~0u, security, PAGE_READWRITE, 0, 1024, 0) && GetLastError() == 87, 4005);
}

static U __stdcall shared_worker(U expected_base)
{
    U h = OpenFileMappingA(0, 0, worker_name);
    check(h != 0, 4006);
    U base = MapViewOfFile(h, FILE_MAP_WRITE, 0, 0, 0);
    check(base == expected_base, 4007);
    volatile U *data = (void *)base;
    check(data[0] == 0x12349898 && !data[1023], 4008);
    data[1] = data[0] ^ 0xa5a5a5a5;
    data[1023] = 0x76543210;
    check(CloseHandle(h) && UnmapViewOfFile((void *)base), 4009);
    return 98;
}

static void worker_and_reuse(void)
{
    U h = CreateFileMappingA(~0u, 0, PAGE_READWRITE, 0, 4096, worker_name);
    check(h != 0, 4010);
    U base = MapViewOfFile(h, FILE_MAP_WRITE, 0, 0, 0);
    check(base != 0, 4011);
    *(volatile U *)base = 0x12349898;
    check(CloseHandle(h), 4012);
    U thread = CreateThread(0, 0, shared_worker, base, 0, 0), result = 0;
    check(thread && WaitForSingleObject(thread, 10000) == 0 && GetExitCodeThread(thread, &result) &&
              result == 98,
          4013);
    check(((volatile U *)base)[1] == (0x12349898u ^ 0xa5a5a5a5u) && ((volatile U *)base)[1023] == 0x76543210,
          4014);
    check(CloseHandle(thread) && UnmapViewOfFile((void *)base), 4015);
    released(base, 4016);
    check(!OpenFileMappingA(FILE_MAP_READ, 0, worker_name) && GetLastError() == 123, 4017);

    /* More than the original three-slot growth increment, alive simultaneously. */
    U handles[7], views[7];
    for (U i = 0; i < 7; ++i)
    {
        handles[i] = CreateFileMappingA(~0u, 0, PAGE_READWRITE, 0, 4096, 0);
        check(handles[i] != 0, 4018);
        views[i] = MapViewOfFile(handles[i], FILE_MAP_WRITE, 0, 0, 0);
        check(views[i] && !*(volatile U *)views[i], 4019);
        *(volatile U *)views[i] = 0x98000000 + i;
        for (U j = 0; j < i; ++j)
            check(views[i] != views[j] && *(volatile U *)views[j] == 0x98000000 + j, 4020);
    }
    for (U i = 0; i < 7; ++i)
    {
        check(CloseHandle(handles[i]) && UnmapViewOfFile((void *)views[i]), 4021);
        released(views[i], 4022);
    }
    for (U i = 0; i < 140; ++i)
    {
        U next = CreateFileMappingA(~0u, 0, PAGE_READWRITE, 0, 1024, "neo98-mapping-reuse");
        check(next && !GetLastError(), 4023);
        U view = MapViewOfFile(next, FILE_MAP_WRITE, 0, 0, 0);
        check(view && !*(volatile U *)view, 4024);
        *(volatile U *)view = i + 1;
        check(UnmapViewOfFile((void *)view) && CloseHandle(next), 4025);
        released(view, 4026);
    }
}

static void native_capacity_contract(void)
{
    /* These are explicit ARM provider limits, not original Windows maxima. */
    const char object_name[] = "neo98-mapping-object-capacity";
    const char failed_name[] = "neo98-mapping-capacity-failure";
    U objects[64];
    for (U i = 0; i < 64; ++i)
    {
        objects[i] = CreateFileMappingA(~0u, 0, PAGE_READWRITE, 0, 4096, i ? 0 : object_name);
        check(objects[i] && !GetLastError(), 4030);
    }
    check(!CreateFileMappingA(~0u, 0, PAGE_READWRITE, 0, 4096, failed_name) && GetLastError() == 8, 4031);
    check(!OpenFileMappingA(0, 0, failed_name) && GetLastError() == 123, 4032);
    U existing = CreateFileMappingA(~0u, 0, PAGE_READONLY, 12, 0, object_name);
    check(existing && GetLastError() == 183, 4033);
    U opened = OpenFileMappingA(0, 0, object_name);
    check(opened && opened != existing, 4034);
    check(CloseHandle(existing) && CloseHandle(opened), 4035);
    for (U i = 0; i < 64; ++i)
        check(CloseHandle(objects[i]), 4036);
    check(!OpenFileMappingA(0, 0, object_name) && GetLastError() == 123, 4037);
    U h = CreateFileMappingA(~0u, 0, PAGE_READWRITE, 0, 4096, failed_name);
    check(h && !GetLastError(), 4038);
    check(CloseHandle(h), 4039);

    const char view_name[] = "neo98-mapping-view-capacity";
    h = CreateFileMappingA(~0u, 0, PAGE_READWRITE, 0, 4096, view_name);
    check(h != 0, 4040);
    U base = 0;
    for (U i = 0; i < 512; ++i)
    {
        U view = MapViewOfFile(h, FILE_MAP_WRITE, 0, 0, 0);
        if (!i)
        {
            base = view;
            check(base && !*(volatile U *)base, 4041);
            *(volatile U *)base = 0x9898cafe;
        }
        check(view == base && *(volatile U *)view == 0x9898cafe, 4042);
    }
    check(!MapViewOfFile(h, FILE_MAP_WRITE, 0, 0, 0) && GetLastError() == 8 &&
              *(volatile U *)base == 0x9898cafe,
          4043);
    mapped(base, base, PAGE_READWRITE, 4044);
    check(CloseHandle(h), 4045);
    check(!CreateEventA(0, 0, 0, view_name) && GetLastError() == 6, 4046);
    opened = OpenFileMappingA(0, 0, view_name);
    check(opened && CloseHandle(opened), 4047);
    for (U i = 0; i < 511; ++i)
        check(UnmapViewOfFile((void *)base) && *(volatile U *)base == 0x9898cafe, 4048);
    mapped(base, base, PAGE_READWRITE, 4049);
    check(UnmapViewOfFile((void *)base), 4050);
    released(base, 4051);
    check(!OpenFileMappingA(0, 0, view_name) && GetLastError() == 123, 4052);
    check(!UnmapViewOfFile((void *)base) && GetLastError() == 487, 4053);
    h = CreateFileMappingA(~0u, 0, PAGE_READWRITE, 0, 4096, view_name);
    U fresh = h ? MapViewOfFile(h, FILE_MAP_WRITE, 0, 0, 0) : 0;
    check(h && fresh && !*(volatile U *)fresh, 4054);
    check(UnmapViewOfFile((void *)fresh) && CloseHandle(h), 4055);
    released(fresh, 4056);

    /* Other COMMON tests may retain kernel handles. Exhaust only the slots
     * currently available, and close only handles created by this fixture. */
    const char handle_name[] = "neo98-mapping-handle-capacity";
    const char handle_failure[] = "neo98-mapping-handle-failure";
    /* The shared handle table can grow; this bound must still require
     * actual exhaustion rather than assuming the former 128-slot size. */
    static U handles[4096];
    handles[0] = CreateFileMappingA(~0u, 0, PAGE_READWRITE, 0, 4096, handle_name);
    check(handles[0] != 0, 4057);
    U count = 1;
    for (; count < 4096; ++count)
    {
        U next = OpenFileMappingA(0, 0, handle_name);
        if (!next)
        {
            check(GetLastError() == 8, 4058);
            break;
        }
        handles[count] = next;
    }
    check(count >= 1 && count < 4096, 4059);
    check(!OpenFileMappingA(0, 0, handle_name) && GetLastError() == 8, 4060);
    check(!CreateFileMappingA(~0u, 0, PAGE_READWRITE, 0, 4096, handle_failure) && GetLastError() == 8, 4061);
    check(!OpenFileMappingA(0, 0, handle_failure) && GetLastError() == 123, 4062);
    base = MapViewOfFile(handles[0], FILE_MAP_WRITE, 0, 0, 0);
    check(base != 0, 4063);
    *(volatile U *)base = 0x1234abcd;
    check(CloseHandle(handles[--count]), 4064);
    check(!OpenFileMappingA(0, 0, handle_failure) && GetLastError() == 123, 4065);
    fresh = CreateFileMappingA(~0u, 0, PAGE_READWRITE, 0, 4096, handle_failure);
    check(fresh && !GetLastError(), 4066);
    check(CloseHandle(fresh), 4067);
    for (U i = 0; i < count; ++i)
        check(CloseHandle(handles[i]), 4068);
    mapped(base, base, PAGE_READWRITE, 4069);
    check(*(volatile U *)base == 0x1234abcd, 4070);
    opened = OpenFileMappingA(0, 0, handle_name);
    check(opened && CloseHandle(opened), 4071);
    check(UnmapViewOfFile((void *)base), 4072);
    released(base, 4073);
    check(!OpenFileMappingA(0, 0, handle_name) && GetLastError() == 123, 4074);
    check(!MapViewOfFile(handles[0], FILE_MAP_WRITE, 0, 0, 0) && GetLastError() == 6, 4075);
    check(!OpenFileMappingA(0, 0, handle_failure) && GetLastError() == 123, 4076);
}

static void shared_page_rights(void)
{
    U h = CreateFileMappingA(~0u, 0, PAGE_READWRITE, 0, 4096, 0);
    check(h != 0, 4080);
    U read = MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);
    check(read && !*(volatile U *)read, 4081);
    mapped(read, read, PAGE_READONLY, 4082);
    U write = MapViewOfFile(h, FILE_MAP_WRITE, 0, 0, 0);
    check(write == read, 4083);
    mapped(write, write, PAGE_READWRITE, 4084);
    *(volatile U *)write = 0x9988;
    check(*(volatile U *)read == 0x9988, 4085);
    U next = MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);
    check(next == read, 4086);
    *(volatile U *)write = 0xaabb;
    mapped(next, next, PAGE_READWRITE, 4087);
    check(*(volatile U *)next == 0xaabb && CloseHandle(h), 4088);
    check(UnmapViewOfFile((void *)read) && UnmapViewOfFile((void *)write) &&
              UnmapViewOfFile((void *)next), 4089);
    released(read, 4090);
}

/* A file backed mapping with write access. The bytes a view changes go
   into the file at FlushViewOfFile, at UnmapViewOfFile and when the last handle
   of the object closes, the file grows to the mapping size, two views of one
   object in one process are one block, and a PAGE_READONLY object refuses a
   writing view. */
static void file_backed(void)
{
    static const char path[] = "C:\\MAPFILE.BIN";
    U file, mapping, view, second, read_mapping, read_view, done = 0, size = 0, i;
    unsigned char scratch[32];
    unsigned char *bytes;
    DeleteFileA(path);
    file = CreateFileA(path, 0xc0000000u, 3, 0, 2, 128, 0);
    check(file && file != 0xffffffffu, 4091);
    for (i = 0; i < 16; ++i)
        scratch[i] = (unsigned char)(i + 1);
    check(WriteFile(file, scratch, 16, &done, 0) && done == 16, 4092);
    /* No size given: the object is as long as the file. */
    mapping = CreateFileMappingA(file, 0, PAGE_READWRITE, 0, 0, 0);
    check(mapping != 0, 4093);
    view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    check(view != 0, 4094);
    bytes = (unsigned char *)view;
    for (i = 0; i < 16; ++i)
        check(bytes[i] == (unsigned char)(i + 1), 4097);
    /* A second view of the same object in the same process is the same block. */
    second = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    check(second == view, 4099);
    bytes[0] = 0xaa;
    check(((unsigned char *)second)[0] == 0xaa, 4027);
    check(FlushViewOfFile((const void *)view, 16), 4028);
    /* The flush alone must already be visible through an ordinary read. */
    check(SetFilePointer(file, 0, 0, 0) == 0, 4029);
    check(ReadFile(file, scratch, 16, &done, 0) && done == 16 && scratch[0] == 0xaa, 4077);
    bytes[15] = 0x55;
    check(UnmapViewOfFile((const void *)second), 4078);
    check(UnmapViewOfFile((const void *)view), 4079);
    check(CloseHandle(mapping), 4098);
    check(SetFilePointer(file, 0, 0, 0) == 0, 4120);
    check(ReadFile(file, scratch, 16, &done, 0) && done == 16 && scratch[0] == 0xaa && scratch[15] == 0x55,
          4121);
    /* A mapping longer than the file grows the file. */
    mapping = CreateFileMappingA(file, 0, PAGE_READWRITE, 0, 4096, 0);
    check(mapping != 0, 4122);
    check(GetFileSize(file, 0) == 4096, 4123);
    view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    check(view != 0, 4124);
    bytes = (unsigned char *)view;
    check(bytes[0] == 0xaa && bytes[15] == 0x55 && !bytes[2048], 4125);
    bytes[2048] = 0x7e;
    check(UnmapViewOfFile((const void *)view) && CloseHandle(mapping), 4126);
    check(SetFilePointer(file, 2048, 0, 0) == 2048, 4127);
    check(ReadFile(file, scratch, 1, &done, 0) && done == 1 && scratch[0] == 0x7e, 4128);
    /* A read-only object hands out no writing view, and its pages carry no
       write right. */
    read_mapping = CreateFileMappingA(file, 0, PAGE_READONLY, 0, 0, 0);
    check(read_mapping != 0, 4129);
    SetLastError(0);
    check(!MapViewOfFile(read_mapping, FILE_MAP_WRITE, 0, 0, 0) && GetLastError() == 87, 4130);
    read_view = MapViewOfFile(read_mapping, FILE_MAP_READ, 0, 0, 0);
    check(read_view != 0, 4131);
    mapped(read_view, read_view, PAGE_READONLY, 4132);
    check(((const unsigned char *)read_view)[0] == 0xaa, 4133);
    check(UnmapViewOfFile((const void *)read_view) && CloseHandle(read_mapping), 4134);
    /* SEC_COMMIT on a file is what a file mapping is anyway; Need for Speed
       II SE maps its data with PAGE_READONLY | SEC_COMMIT. */
    read_mapping = CreateFileMappingA(file, 0, PAGE_READONLY | 0x08000000u, 0, 0, 0);
    check(read_mapping != 0, 4190);
    read_view = MapViewOfFile(read_mapping, FILE_MAP_READ, 0, 0, 0);
    check(read_view != 0 && ((const unsigned char *)read_view)[0] == 0xaa, 4191);
    check(UnmapViewOfFile((const void *)read_view) && CloseHandle(read_mapping), 4192);
    /* Closing the file first leaves the mapping usable: the object keeps its
       own reference, the way Win98 does. */
    mapping = CreateFileMappingA(file, 0, PAGE_READWRITE, 0, 0, 0);
    check(mapping != 0, 4135);
    check(CloseHandle(file), 4136);
    view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    check(view != 0, 4137);
    ((unsigned char *)view)[1] = 0x39;
    check(UnmapViewOfFile((const void *)view) && CloseHandle(mapping), 4138);
    file = CreateFileA(path, 0x80000000u, 3, 0, 3, 128, 0);
    check(file && file != 0xffffffffu, 4139);
    check(ReadFile(file, scratch, 16, &done, 0) && done == 16 && scratch[1] == 0x39, 4140);
    check(CloseHandle(file) && DeleteFileA(path), 4141);
    (void)size;
}

/* SEC_RESERVE, the way QUARTZ.DLL builds its shared filter graph data: a
   named section of the paging file whose view only reserves, and
   VirtualAlloc(MEM_COMMIT) on the view commits the pages. */
static void reserve_section(void)
{
    Info info;
    const U mapping = CreateFileMappingA(0xffffffffu, 0, PAGE_READWRITE | 0x04000000u, 0, 0xe000, "NEO98 SEC_RESERVE");
    check(mapping != 0, 4190);
    const U view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    check(view != 0, 4191);
    check(VirtualQuery((const void *)(view + 0x2000), &info, sizeof info) == sizeof info && info.state == MEM_RESERVE, 4192);
    check(VirtualAlloc((void *)(view + 0x2000), 0x1000, MEM_COMMIT, PAGE_READWRITE) == view + 0x2000, 4193);
    ((volatile unsigned char *)view)[0x2000] = 0x5a;
    check(((volatile unsigned char *)view)[0x2000] == 0x5a, 4194);
    check(VirtualQuery((const void *)(view + 0x2000), &info, sizeof info) == sizeof info && info.state == MEM_COMMIT, 4195);
    check(UnmapViewOfFile((const void *)view) && CloseHandle(mapping), 4196);
}

void run_mapping_test(void)
{
    shared_lifetime();
    ranges_and_ex();
    protection_and_ownership();
    namespace_and_errors();
    worker_and_reuse();
    native_capacity_contract();
    shared_page_rights();
    file_backed();
    reserve_section();
}
