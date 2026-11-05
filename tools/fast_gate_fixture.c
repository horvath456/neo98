/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Every API of the fast gate positive list, called twice, with the state a
 * caller can observe recorded after the second call: return value, the stack
 * pointer, the four registers the ABI makes the callee preserve, and the last
 * error. The runner tools/test_fast_gate_guest.cpp runs this once with the
 * fast gates on and once with them off and compares the records.
 * */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetTickCount(void);
API U GetCurrentThreadId(void);
API U GetCurrentProcessId(void);
API U GetCurrentThread(void);
API U GetCurrentProcess(void);
API U QueryPerformanceCounter(void *);
API U QueryPerformanceFrequency(void *);
API U GetLastError(void);
API void SetLastError(U);
API U TlsAlloc(void);
API void *TlsGetValue(U);
API U TlsSetValue(U, void *);
API U InterlockedIncrement(long *);
API U InterlockedDecrement(long *);
API U InterlockedExchange(long *, long);
API U InterlockedExchangeAdd(long *, long);
API U InterlockedCompareExchange(long *, long, long);
API U lstrlenA(const char *);
API U lstrlenW(const unsigned short *);
API U lstrcmpA(const char *, const char *);
API U lstrcmpiA(const char *, const char *);
API U IsBadReadPtr(const void *, U);
API U IsBadWritePtr(void *, U);
API U IsBadCodePtr(const void *);
API U IsBadHugeReadPtr(const void *, U);
API U IsBadHugeWritePtr(void *, U);
API U IsBadStringPtrA(const char *, U);
API U IsBadStringPtrW(const unsigned short *, U);
API U IsDBCSLeadByte(unsigned char);
API U IsDBCSLeadByteEx(U, unsigned char);
API void InitializeCriticalSection(void *);
API void EnterCriticalSection(void *);
API void LeaveCriticalSection(void *);
API void *VirtualAlloc(void *, U, U, U);
API void ExitProcess(U);
API U timeGetTime(void);
API char *CharNextA(const char *);
API char *CharPrevA(const char *, const char *);
API U SetRect(void *, int, int, int, int);
API U SetRectEmpty(void *);
API U IsRectEmpty(const void *);
API U PtInRect(const void *, int, int);
API U CopyRect(void *, const void *);
API U OffsetRect(void *, int, int);
API U InflateRect(void *, int, int);
API U EqualRect(const void *, const void *);
API U IsWindow(U);
API U GetPropA(U, const char *);
API U GetSysColor(int);
API U GetSystemMetrics(int);
API U GetCursorPos(void *);
API U GetDC(U);
API U ReleaseDC(U, U);
API U GetBkMode(U);
API U SetBkMode(U, int);
API U GetBkColor(U);
API U SetBkColor(U, U);
API U GetTextColor(U);
API U SetTextColor(U, U);
API U GetROP2(U);
API U SetROP2(U, int);
API U GetMapMode(U);
API U GetStockObject(int);
API U GetTextAlign(U);
API U SetTextAlign(U, U);

/* The runner reads the records from this address, so the fixture needs no way
 * of its own to report them. */
#define RECORDS 0x30000000u
#define FIELDS 8u
static U *records;
static U count;
/* Bit 31 of the identifier says the return value moves with the clock, so the
 * runner compares everything about that record except the value. */
#define VOLATILE 0x80000000u

static void record(U id, U value, U esp, U ebx, U esi, U edi, U ebp, U error)
{
    U *p = records + 1 + count * FIELDS;
    p[0] = id; p[1] = value; p[2] = esp; p[3] = ebx; p[4] = esi; p[5] = edi; p[6] = ebp; p[7] = error;
    records[0] = ++count;
}

/* The sentinels go into the three registers a stdcall callee must give back
 * unchanged. EBP is the frame pointer of this function and is read as it is.
 * The fixture is built without optimisation so that nothing of the compiler
 * stands between the sentinels and the call. */
#define PROBE(id, expr)                                                                            \
    do {                                                                                           \
        U esp0_, esp1_, ebx_, esi_, edi_, ebp_, value_;                                            \
        __asm__ volatile("movl %%esp, %0" : "=r"(esp0_)::"memory");                                \
        __asm__ volatile("movl $0x1b1b1b1b, %%ebx\n\t"                                             \
                         "movl $0x515e515e, %%esi\n\t"                                             \
                         "movl $0xd1d1d1d1, %%edi" ::: "ebx", "esi", "edi", "memory");             \
        value_ = (U)(expr);                                                                        \
        __asm__ volatile("movl %%esp, %0\n\tmovl %%ebx, %1\n\tmovl %%esi, %2\n\t"                  \
                         "movl %%edi, %3\n\tmovl %%ebp, %4"                                        \
                         : "=r"(esp1_), "=r"(ebx_), "=r"(esi_), "=r"(edi_), "=r"(ebp_)::"memory"); \
        record((id), value_, esp0_ - esp1_, ebx_, esi_, edi_, ebp_, GetLastError());               \
    } while (0)

/* Every entry runs twice. The first call binds the import and always takes the
 * ordinary way; the second is the one the fast gate can answer. */
#define TWICE(id, expr)                                                                            \
    do { PROBE((id), (expr)); PROBE((id) + 1u, (expr)); } while (0)

API U GetCPInfo(U, void *);
API U MultiByteToWideChar(U, U, const char *, int, unsigned short *, int);
API U WideCharToMultiByte(U, U, const unsigned short *, int, char *, int, const char *, U *);
API char *lstrcpyA(char *, const char *);
API char *lstrcpynA(char *, const char *, int);
static char output[64];
static unsigned short converted[64];
static U cpinfo[8];
static char text[64] = "Windows 98";
static char other[64] = "WINDOWS 98";
static unsigned short wide[8] = {'W', 'i', 'n', '9', '8', 0, 0, 0};
static long counter = 7;
static int rect[4] = {1, 2, 30, 40}, rect2[4] = {0, 0, 0, 0};
static unsigned char section[32];
static unsigned long long stamp;
static int point[2];

static void run_added_cases(void)
{
    /* Slots are range-checked even when they have not been allocated. */
    TWICE(900, TlsSetValue(79, (void *)0x87654321));
    TWICE(910, (U)TlsGetValue(79));
    TWICE(920, TlsSetValue(80, (void *)1));
    TWICE(930, (U)TlsGetValue(80));
    TWICE(940, TlsSetValue(~0u, (void *)1));
    TWICE(950, (U)TlsGetValue(~0u));
    TWICE(960, TlsSetValue(0x80000000u, (void *)1));
    TWICE(970, (U)TlsGetValue(0x80000000u));
    TWICE(1000, GetCPInfo(1252, cpinfo));
    TWICE(1010, GetCPInfo(0xffffffffu, cpinfo));
    TWICE(1020, MultiByteToWideChar(1252, 0, text, -1, converted, 64));
    TWICE(1030, WideCharToMultiByte(1252, 0, converted, -1, output, 64, 0, 0));
    TWICE(1040, MultiByteToWideChar(1252, 0, text, -1, converted, 1));
    TWICE(1050, MultiByteToWideChar(1252, 0, 0, -1, converted, 64));
    TWICE(1060, (U)lstrcpyA(output, text));
    TWICE(1070, (U)lstrcpynA(output, text, 4));
    TWICE(1080, (U)lstrcpynA(output, text, 0));
    TWICE(1090, (U)lstrcpynA(0, text, 4));
}

void run_fast_gate_test(void)
{
    records = VirtualAlloc((void *)RECORDS, 65536, 0x3000, 4);
    if (records != (U *)RECORDS)
        ExitProcess(7300);
    records[0] = 0;
    U slot = TlsAlloc();
    U dc = GetDC(0);
    if (!dc)
        ExitProcess(7301);
    InitializeCriticalSection(section);

    SetLastError(0x98bad);
    TWICE(VOLATILE | 100, GetTickCount());
    TWICE(VOLATILE | 110, timeGetTime());
    TWICE(120, GetCurrentThreadId());
    TWICE(130, GetCurrentProcessId());
    TWICE(140, GetCurrentThread());
    TWICE(150, GetCurrentProcess());
    TWICE(160, QueryPerformanceCounter(&stamp));
    TWICE(170, QueryPerformanceFrequency(&stamp));
    TWICE(180, GetLastError());
    /* SetLastError returns nothing, so the record's value is the error the
     * next GetLastError in PROBE reads back. */
    TWICE(190, (SetLastError(0x4711), 0u));
    TWICE(200, TlsSetValue(slot, (void *)0x1234));
    TWICE(210, (U)TlsGetValue(slot));

    TWICE(220, InterlockedIncrement(&counter));
    TWICE(230, InterlockedDecrement(&counter));
    TWICE(240, InterlockedExchange(&counter, 21));
    TWICE(250, InterlockedExchangeAdd(&counter, 4));
    TWICE(260, InterlockedCompareExchange(&counter, 9, 25));
    TWICE(270, lstrlenA(text));
    TWICE(280, lstrlenW(wide));
    TWICE(290, lstrcmpA(text, other));
    TWICE(300, lstrcmpiA(text, other));
    TWICE(310, IsBadReadPtr(text, 8));
    TWICE(320, IsBadWritePtr(text, 8));
    TWICE(330, IsBadCodePtr(text));
    TWICE(340, IsBadHugeReadPtr(text, 8));
    TWICE(350, IsBadHugeWritePtr(text, 8));
    TWICE(360, IsBadStringPtrA(text, 64));
    TWICE(370, IsBadStringPtrW(wide, 8));
    TWICE(380, IsBadReadPtr((const void *)4, 8));
    TWICE(390, IsDBCSLeadByte(0x81));
    TWICE(400, IsDBCSLeadByteEx(1252, 0x81));
    TWICE(410, (EnterCriticalSection(section), 0u));
    TWICE(420, (LeaveCriticalSection(section), 0u));
    /* Recursion and release again, so the counters move through more than one
     * state on the uncontended path. */
    EnterCriticalSection(section);
    TWICE(430, (EnterCriticalSection(section), 0u));
    LeaveCriticalSection(section);
    LeaveCriticalSection(section);
    TWICE(440, (LeaveCriticalSection(section), 0u));

    TWICE(500, (U)CharNextA(text));
    TWICE(510, (U)CharPrevA(text, text + 4));
    TWICE(520, SetRect(rect, 1, 2, 30, 40));
    TWICE(530, IsRectEmpty(rect));
    TWICE(540, PtInRect(rect, 5, 6));
    TWICE(550, CopyRect(rect2, rect));
    TWICE(560, OffsetRect(rect, 3, 4));
    TWICE(570, InflateRect(rect, 1, 1));
    TWICE(580, EqualRect(rect, rect2));
    TWICE(590, SetRectEmpty(rect2));
    TWICE(600, IsWindow(0));
    TWICE(610, GetPropA(0, text));
    TWICE(620, GetSysColor(5));
    TWICE(630, GetSystemMetrics(0));
    TWICE(640, GetCursorPos(point));

    TWICE(700, GetBkMode(dc));
    TWICE(710, SetBkMode(dc, 1));
    TWICE(720, GetBkColor(dc));
    TWICE(730, SetBkColor(dc, 0x123456));
    TWICE(740, GetTextColor(dc));
    TWICE(750, SetTextColor(dc, 0x654321));
    TWICE(760, GetROP2(dc));
    TWICE(770, SetROP2(dc, 13));
    TWICE(780, GetMapMode(dc));
    TWICE(790, GetStockObject(7));
    TWICE(800, GetTextAlign(dc));
    TWICE(810, SetTextAlign(dc, 0));

    run_added_cases();
    ReleaseDC(0, dc);
    ExitProcess(0);
}

void mainCRTStartup(void)
{
    run_fast_gate_test();
}
