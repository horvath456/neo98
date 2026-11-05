/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
typedef unsigned short W;
typedef struct { U low, high; } FT;
typedef struct { W year, month, weekday, day, hour, minute, second, milliseconds; } ST;
#define API __declspec(dllimport) __stdcall
API U SystemTimeToFileTime(const ST *, FT *);
API U FileTimeToSystemTime(const FT *, ST *);
API int CompareFileTime(const FT *, const FT *);
API U DosDateTimeToFileTime(W, W, FT *);
API U FileTimeToDosDateTime(const FT *, W *, W *);
API U FileTimeToLocalFileTime(const FT *, FT *);
API U LocalFileTimeToFileTime(const FT *, FT *);
API void SetLastError(U);
API U GetLastError(void);
API void *VirtualAlloc(void *, U, U, U);
API U VirtualFree(void *, U, U);
API U VirtualProtect(void *, U, U, U *);
API void ExitProcess(U);
#ifndef TIME_FAMILY_BIAS_MINUTES
#define TIME_FAMILY_BIAS_MINUTES 420
#endif
/* Compile-time constants only: the fixture has no 64-bit runtime helpers. */
#define BIAS_TICKS ((long long)(TIME_FAMILY_BIAS_MINUTES)*600000000LL)
#define BIAS_LOCAL_OF_PROBE (0x8000000000000000ULL-(unsigned long long)BIAS_TICKS)
#define BIAS_DOS_ZERO (0ULL-(unsigned long long)BIAS_TICKS)
static void check(int okay, U code) { if (!okay) ExitProcess(code); }
static int equal(FT a, FT b) { return a.low == b.low && a.high == b.high; }
void run_time_family_test(void)
{
    ST st = {2000,2,0xffff,29,23,59,59,999}, out;
    FT file, before, result;
    SetLastError(0x9898);
    check(SystemTimeToFileTime(&st,&file) && GetLastError()==0x9898,6500);
    check(FileTimeToSystemTime(&file,&out) && out.year==2000 && out.month==2 && out.day==29 &&
          out.weekday==2 && out.hour==23 && out.minute==59 && out.second==59 && out.milliseconds==999,6501);
    before=file;st.weekday=0;
    check(SystemTimeToFileTime(&st,&result) && equal(result,before),6502);
    for(U field=0;field<7;++field)
    {
        ST bad=st;
        if(field==0)bad.year=1600;
        if(field==1)bad.month=13;
        if(field==2)bad.day=30;
        if(field==3)bad.hour=24;
        if(field==4)bad.minute=60;
        if(field==5)bad.second=60;
        if(field==6)bad.milliseconds=1000;
        result=before;SetLastError(0x9898);
        check(!SystemTimeToFileTime(&bad,&result) && equal(result,before) && GetLastError()==0x9898,6503+field);
    }
    st=(ST){1601,1,0,0,0,0,0,0};
    check(SystemTimeToFileTime(&st,&file) && file.low==0xd5964000 && file.high==0xffffff36,6510);
    st.day=1;
    check(SystemTimeToFileTime(&st,&file) && !file.low && !file.high,6511);
    file=(FT){1,0};
    check(FileTimeToSystemTime(&file,&out) && out.year==1601 && out.month==1 && out.day==1 && !out.milliseconds,6512);
    file=(FT){0xffffffff,0xffffffff};
    check(FileTimeToSystemTime(&file,&out) && out.year==1601 && out.day==1 && !out.milliseconds,6513);
    file=(FT){0,0x80000000};
    check(FileTimeToSystemTime(&file,&out) && out.year==650 && out.month==5 && out.day==10 &&
          out.hour==1190 && out.minute==14 && out.second==41 && out.milliseconds==819,6514);
    before=(FT){0xffffffff,0x7fffffff};
    check(CompareFileTime(&file,&before)==1 && CompareFileTime(&before,&file)==-1 &&
          !CompareFileTime(&file,&file),6515);
    /* The guest runs at the machine's own bias. KERNEL32 fills its timezone
       cache from HKLM\System\CurrentControlSet\Control\TimeZoneInformation,
       so the expectation follows the hive of the Win98 folder: whoever builds
       this file passes its ActiveTimeBias as TIME_FAMILY_BIAS_MINUTES
       (tools/build_common_fixture.py reads it with tools/win98_time_zone.py).
       The reference system installation held W. Europe (-120), the first ISO
       installation Pacific (420), the one since 3 October 2026 W. Europe again.
       Without the macro the bias is 420, which tools/test_time_native.cpp
       configures explicitly for the standalone build.
       Only 6516 and 6521 depend on the bias. The unchanged original KERNEL32
       and IFSMGR bodies under Unicorn give probe minus the tick bias and minus
       the tick bias for them, at 420 and at -120 alike
       (tools/derive_time_family_bias.py --minutes N); every other constant
       from here on is the same at both and was produced there too. */
    /* 6516: KERNEL32 reads the tick bias and subtracts it
       from the probe: at 420 local time is seven hours behind UTC and the
       probe loses 252,000,000,000 ticks. */
    check(FileTimeToLocalFileTime(&file,&result) && result.low==(U)(BIAS_LOCAL_OF_PROBE) &&
          result.high==(U)(BIAS_LOCAL_OF_PROBE>>32),6516);
    /* 6517: KERNEL32 inverts that exactly, including in place. */
    before=file;file=result;
    check(LocalFileTimeToFileTime(&file,&file) && equal(file,before),6517);
    /* 6518: the DOS gateway applies the same bias in both directions, so a real
       DOS date converts to the FILETIME its fields name, bias or not. */
    check(DosDateTimeToFileTime(0x21,0,&file) && file.low==0xe1d58000 && file.high==0x01a8e79f,6518);
    W date=0xcccc,time=0xcccc;
    check(FileTimeToDosDateTime(&file,&date,&time) && date==0x21 && !time,6519);
    check(FileTimeToDosDateTime(&file,&date,&date) && date==0x21,6520);
    /* 6521: zero is the original sentinel and takes the other path through the
       gateway, where the bias does not cancel. It converts to the bias itself. */
    check(DosDateTimeToFileTime(0,0,&file) && file.low==(U)(BIAS_DOS_ZERO) && file.high==(U)(BIAS_DOS_ZERO>>32),6521);
    check(FileTimeToDosDateTime(&file,&date,&time) && !date && !time,6522);
    file=(FT){0xd53e8000,0x019db1de};date=time=0xcccc;SetLastError(0x9898);
    check(!FileTimeToDosDateTime(&file,&date,&time) && date==0xcccc && time==0xcccc && GetLastError()==13,6523);
    /* Original IFSMGR's DOS calendar retains a four-year cycle through 2100. */
    st=(ST){2100,3,0,1,0,0,0,0};
    check(SystemTimeToFileTime(&st,&file) && FileTimeToDosDateTime(&file,&date,&time) && date==0xf05d,6524);
    st=(ST){2107,12,0,31,0,0,0,0};
    check(SystemTimeToFileTime(&st,&file) && FileTimeToDosDateTime(&file,&date,&time) && !date && !time,6525);
    st=(ST){1980,1,0,1,0,0,3,999};
    check(SystemTimeToFileTime(&st,&file) && FileTimeToDosDateTime(&file,&date,&time) && date==0x21 && time==1,6526);
    union { ST st; FT ft; } alias;
    alias.st=(ST){2000,2,0,29,0,0,0,0};
    check(SystemTimeToFileTime(&alias.st,&alias.ft) && alias.ft.low==0xebcc8000 && alias.ft.high==0x01bf8247,6527);
    check(FileTimeToSystemTime(&alias.ft,&alias.st) && alias.st.year==2000 && alias.st.day==29,6528);
    SetLastError(0);check(!SystemTimeToFileTime(0,&file) && GetLastError()==87,6530);
    SetLastError(0);check(!FileTimeToSystemTime(&file,0) && GetLastError()==87,6531);
    SetLastError(0);check(!CompareFileTime(0,&file) && GetLastError()==87,6532);
    SetLastError(0);check(!DosDateTimeToFileTime(0,0,0) && GetLastError()==87,6533);
    SetLastError(0);check(!FileTimeToDosDateTime(&file,0,&time) && GetLastError()==87,6534);
    SetLastError(0);check(!FileTimeToLocalFileTime((FT *)0xfffffffc,&file) && GetLastError()==87,6535);
    SetLastError(0);check(!LocalFileTimeToFileTime(&file,(FT *)0xfffffffc) && GetLastError()==87,6536);
    FT *page=VirtualAlloc(0,8192,0x3000,4);U old=0;
    check(page!=0,6537);page[0]=(FT){0,0};
    check(VirtualProtect(page,4096,2,&old),6538);
    check(FileTimeToSystemTime(page,&out) && !CompareFileTime(page,page),6539);
    SetLastError(0);check(!FileTimeToLocalFileTime(&file,page) && GetLastError()==87,6540);
    check(VirtualProtect(page,4096,4,&old) && VirtualFree((char *)page+4096,4096,0x4000),6541);
    SetLastError(0);check(!FileTimeToLocalFileTime((FT *)((char *)page+4092),&file) && GetLastError()==87,6542);
    check(VirtualFree(page,0,0x8000),6543);
}
#ifdef TIME_FAMILY_STANDALONE
void mainCRTStartup(void) { run_time_family_test(); ExitProcess(0); }
#endif
