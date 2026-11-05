// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_crt.h"
#include "win98_crt_time.h"
#include "win98_crt_system_clock.h"
#include "win98_crt_time_format.h"
#include "win98_crt_decimal.h"
#include "win98_crt_output.h"
#include "win98_crt_search.h"
#include "win98_crt_set_search.h"
#include "x86_fpu_return.h"
#include "win98_crt_fpu.h"
#include "x86_fpu_integer.h"
#include "x86_app.h"
#include <string.h>
namespace X86
{
bool Application::InitializeFpu(unsigned control)
{
    return Check(X86FpuReturn::InitializeThread(cpu, control));
}
bool Application::ReturnDoubleBits(unsigned long long bits)
{
    const auto error = X86FpuReturn::Push64(cpu, bits);
    if (error == CpuError::Exception) { Fail("Native floating-point return has no empty x87 stack slot"); return false; }
    if (error == CpuError::Argument) { Fail("Native floating-point return cannot deliver signalling-NaN exceptions"); return false; }
    return Check(error);
}
unsigned Application::CrtApi(ApiName name, const unsigned *a)
{
#define CRT(n) API_CASE(n)
    switch (name.id)
    {
#include "x86_crt_fpu.inc"
    API_CASE("strtod") API_CASE("atof")return CrtFloatApi(name,a);
    API_CASE("_errno") API_CASE("__doserrno") API_CASE("_beginthreadex") API_CASE("_endthreadex") API_CASE("_beginthread") API_CASE("_endthread") API_CASE("srand") API_CASE("rand") API_CASE("strtok") API_CASE("__threadid") API_CASE("__threadhandle")
        return CrtThreadApi(name, a);
    API_CASE("setlocale") API_CASE("localeconv") API_CASE("__p__pctype") API_CASE("__p__pwctype") API_CASE("__p___mb_cur_max") API_CASE("_isctype") API_CASE("toupper") API_CASE("tolower")
        return LocaleApi(name, a);
    API_CASE("__dllonexit") API_CASE("_onexit") API_CASE("atexit")
    {
        struct Environment
        {
            Application &app;
            void Lock() { app.CrtLock(true, 13); }
            void Unlock() { app.CrtLock(false, 13); }
            unsigned Read(unsigned p) { return app.Read32(p); }
            void Write(unsigned p, unsigned value) { app.Write32(p, value); }
            unsigned Size(unsigned p) { return app.CrtApi("_msize", &p); }
            unsigned Resize(unsigned p, unsigned size)
            {
                const unsigned args[] = {p, size};
                return app.CrtApi("realloc", args);
            }
        } e{*this};
        if (API_IS(name,"__dllonexit"))
            return Win98Crt::DllOnExit(e, a[0], a[1], a[2]);
        CrtLock(true, 13);
        if (!Read32(crt_data + 24))
        {
            const unsigned size = 16;
            const unsigned table = CrtApi("malloc", &size);
            if (!table)
            {
                CrtLock(false, 13);
                return API_IS(name,"atexit") ? ~0u : 0;
            }
            Write32(crt_data + 24, table);
            Write32(crt_data + 28, table);
        }
        const unsigned result = Win98Crt::OnExit(e, a[0], crt_data + 24, crt_data + 28);
        CrtLock(false, 13);
        return API_IS(name,"atexit") ? (result ? 0 : ~0u) : result;
    }
    CRT("_EH_prolog")
    {
        // Windows 98 CRT exception-frame setup.
        // This naked helper establishes FS:[0] and changes EBP/ESP itself.
        // It does not implement C++ exception dispatch or stack unwinding.
        unsigned sp = Register(Reg::Esp), ret = Read32(sp);
        const auto result = Win98Crt::ExceptionProlog(sp, ret, Register(Reg::Eax),
                                                      Register(Reg::Ebp), Read32(Teb()));
        Store(result.sp, result.frame, sizeof result.frame);
        Write32(Teb(), result.handler);
        Register(Reg::Esp, result.sp);
        Register(Reg::Ebp, result.bp);
        Register(Reg::Eax, result.ax);
        Register(Reg::Eip, result.ip);
        return result.ax;
    }
#include "x86_crt_multibyte.inc"
    CRT("__set_app_type")
    {
        crt_app_type = a[0];
        return 0;
    }
    CRT("__p__fmode") return crt_data + 4;
    CRT("__p__commode") return crt_data + 8;
    CRT("__setusermatherr")
    {
        crt_math_error = a[0];
        return 0;
    }
    API_CASE("__getmainargs") API_CASE("__p___argc") API_CASE("__p___argv") API_CASE("__p__environ") API_CASE("__p__pgmptr") API_CASE("__p___initenv") API_CASE("__p__acmdln")
        return ArgumentsApi(name, a);
    CRT("_initterm")
    {
        if (a[1] < a[0] || (a[1] - a[0]) % 4 || (a[1] - a[0]) > 4096)
        {
            Fail("Invalid CRT initializer range");
            return 0;
        }
        for (unsigned at = a[0]; at < a[1] && state != Failed && state != Exited; at += 4)
        {
            unsigned function = Read32(at);
            if (function)
                Call(function, nullptr, 0);
        }
        return 0;
    }
    API_CASE("_controlfp") API_CASE("_control87")
    {
        const auto result =
            Win98Crt::Control(Register(Reg::Fpcw), a[0], a[1], API_IS(name,"_control87"));
        Register(Reg::Fpcw, result.word);
        return result.value;
    }
    API_CASE("exit") API_CASE("_exit") API_CASE("_cexit") API_CASE("_c_exit")
    {
        // Hold CRT lock 13 throughout exit processing. Guest atexit callbacks
        // may fail or terminate the process; release the lock through a scope
        // guard so another exiting thread cannot wait forever.
        struct CrtExitLock
        {
            Application &app;
            bool held = true;
            explicit CrtExitLock(Application &a) : app(a) { app.CrtLock(true, 13); }
            void Release() { if (held) { held = false; app.CrtLock(false, 13); } }
            ~CrtExitLock() { Release(); }
        } exit_lock{*this};
        if (API_IS(name,"exit") || API_IS(name,"_cexit"))

        {
            struct Environment
            {
                Application &app;
                unsigned Read(unsigned p) { return app.Read32(p); }
                bool Invoke(unsigned f)
                {
                    app.Call(f, nullptr, 0);
                    return app.state != Failed && app.state != Exited;
                }
            } e{*this};
            Win98Crt::Cleanup(e, crt_data + 24, crt_data + 28);
        }
        // Native stdio and MSVCRT pre-terminator/I/O tables are not
        // implemented. Release the lock before the non-returning ExitProcess
        // call.
        exit_lock.Release();
        if ((API_IS(name,"exit") || API_IS(name,"_exit")) && state != Failed && state != Exited)
            Api("ExitProcess", a);
        return 0;

    }
    API_CASE("malloc") API_CASE("calloc") API_CASE("realloc") API_CASE("free") API_CASE("_msize")
    {
        // New CRT allocator adapter. The Win98 small-block heap and new-handler are not implemented.
        struct PreserveError
        {
            unsigned &error, saved;
            bool enabled;
            ~PreserveError()
            {
                if (enabled)
                    error = saved;
            }
        } preserve{last_error, last_error, !API_IS(name,"_msize")};
        const bool release = API_IS(name,"free"), resize = API_IS(name,"realloc"),
                   zero = API_IS(name,"calloc"), size = API_IS(name,"_msize");
        if (release && !a[0])
            return 0;
        if (!crt_heap)
        {
            const unsigned args[] = {0, 0, 0};
            crt_heap = HeapApi("HeapCreate", args);
        }
        if (!crt_heap)
        {
            SetCrtErrno(12);
            return 0;
        }
        if (release || (resize && a[0] && !a[1]))
        {
            const unsigned args[] = {crt_heap, 0, a[0]};
            if (!HeapApi("HeapFree", args))
                Fail("Invalid CRT free");
            return 0;
        }
        if (size)
        {
            const unsigned args[] = {crt_heap, 0, a[0]};
            // Pass HeapSize through unchanged.
            return HeapApi("HeapSize", args);
        }
        if (zero && a[0] && a[1] > ~0u / a[0])
        {
            SetCrtErrno(12);
            return 0;
        }
        const unsigned wanted = zero ? a[0] * a[1] : resize ? a[1] : a[0];
        const unsigned args[] = {crt_heap, zero ? 8u : 0u, resize && a[0] ? a[0] : wanted, wanted};
        const auto result = HeapApi(resize && a[0] ? "HeapReAlloc" : "HeapAlloc", args);
        if (!result)
            SetCrtErrno(12);
        return result;
    }
    API_CASE("memcpy") API_CASE("memmove")
    {
        if (!a[2])
            return a[0];
        // MSVCRT memcpy/memmove do not install KERNEL32's pointer-fault
        // handler. Invalid CRT buffers must still fault in the guest.
        unsigned char buffer[4096];
        const bool forward = a[0] <= a[1];
        for (unsigned done = 0; done < a[2];)
        {
            const unsigned n = a[2] - done < sizeof buffer ? a[2] - done : sizeof buffer;
            const unsigned offset = forward ? done : a[2] - done - n;
            if (!Read(a[1] + offset, buffer, n) || !Store(a[0] + offset, buffer, n)) return a[0];
            done += n;
        }
        return a[0];
    }
    CRT("memcmp")
    {
        if (a[2] && (a[0] > ~0u - (a[2] - 1) || a[1] > ~0u - (a[2] - 1)))
        {
            Fail("Oversized CRT comparison");
            return 0;
        }
        unsigned char left[512], right[512];
        for (unsigned done = 0; done < a[2];)
        {
            const unsigned n = a[2] - done < sizeof left ? a[2] - done : sizeof left;
            if (!Read(a[0] + done, left, n) || !Read(a[1] + done, right, n))
                return 0;
            for (unsigned i = 0; i < n; ++i)
                if (left[i] != right[i])
                    return left[i] < right[i] ? ~0u : 1;
            done += n;
        }
        return 0; // Normalize unequal bytes to -1 or +1.
    }
    CRT("memset")
    {
        // New native service over checked guest memory. No host pointers cross the ABI.
        if (a[2] && a[0] > ~0u - (a[2] - 1))
        {
            Fail("Oversized CRT fill");
            return 0;
        }
        unsigned char buffer[1024];
        memset(buffer, a[1] & 255, sizeof buffer);
        for (unsigned done = 0; done < a[2] && state != Failed && state != Exited;)
        {
            const unsigned n = a[2] - done < sizeof buffer ? a[2] - done : sizeof buffer;
            Store(a[0] + done, buffer, n);
            done += n;
        }
        return a[0];
    }
    CRT("_strdup")
    {
        // Check null input, measure length+1, allocate, then copy.
        if (!a[0])
            return 0;
        const char *text = ReadString(a[0]);
        if (state == Failed)
            return 0;
        const unsigned size = strlen(text) + 1;
        const unsigned result = CrtApi("malloc", &size);
        if (result)
            Store(result, text, size);
        return result;
    }
    API_CASE("strpbrk") API_CASE("strspn") API_CASE("strcspn") API_CASE("_mbspbrk") API_CASE("_mbsspn") API_CASE("_mbscspn") API_CASE("_mbsspnp") {
        struct Reader {
            Application &app;
            bool multibyte;
            unsigned Read8(unsigned p) { unsigned char c=0; app.Read(p,&c,1); return c; }
            bool Multibyte() { return multibyte; }
            bool LeadByte(unsigned c) { return Win98CrtMultibyte::Lead(app.crt_multibyte,c); }
            void Lock(unsigned id) { app.CrtLock(true,id); }
            void Unlock(unsigned id) { app.CrtLock(false,id); }
        } e{*this,name[0]=='_' && crt_multibyte.multibyte!=0};
        bool keep=API_IS(name,"strspn") || API_IS(name,"_mbsspn") || API_IS(name,"_mbsspnp");
        bool pointer=API_IS(name,"strpbrk") || API_IS(name,"_mbspbrk") || API_IS(name,"_mbsspnp");
        return Win98CrtSetSearch::Search(e,a[0],a[1],keep,pointer);
    }
    API_CASE("strchr") API_CASE("strrchr") API_CASE("_mbschr") API_CASE("_mbsrchr")
    {
        struct Search
        {
            Application &app;
            unsigned Read8(unsigned p)
            {
                unsigned char v = 0;
                app.Read(p, &v, 1);
                return v;
            }
            unsigned Read32(unsigned p) { return app.Read32(p); }
            bool Multibyte() { return app.crt_multibyte.multibyte!=0; }
            bool LeadByte(unsigned c) { return Win98CrtMultibyte::Lead(app.crt_multibyte,c); }
            void Lock(unsigned id) { app.CrtLock(true, id); }
            void Unlock(unsigned id) { app.CrtLock(false, id); }
        } e{*this};
        if (API_IS(name,"strchr"))
            return Win98CrtSearch::Find(e, a[0], a[1]);
        if (API_IS(name,"strrchr"))
            return Win98CrtSearch::FindLast(e, a[0], a[1]);
        if (API_IS(name,"_mbschr"))
            return Win98CrtSearch::MultiFind(e, a[0], a[1]);
        return Win98CrtSearch::MultiFindLast(e, a[0], a[1]);
    }
    CRT("_strrev")
    {
        const char *text = ReadString(a[0]);
        char copy[8192];
        unsigned n = strlen(text);
        for (unsigned i = 0; i < n; ++i)
            copy[i] = text[n - 1 - i];
        copy[n] = 0;
        PutString(a[0], copy);
        return a[0];
    }
    default:
        break;
    }
    // Families described by an export table. Their names differ from every
    // case above, so the position of this test does not change the result.
    static constexpr ApiIdTable system_clock(Win98CrtSystemClock::Exports);
    static constexpr ApiIdTable time_format(Win98CrtTimeFormat::Exports);
    static constexpr ApiIdTable time(Win98CrtTime::Exports);
    static constexpr ApiIdTable output(Win98CrtOutput::Exports);
    if (system_clock.Has(name.id)) return CrtSystemClockApi(name, a);
    if (time_format.Has(name.id)) return CrtTimeFormatApi(name, a);
    if (time.Has(name.id)) return CrtTimeApi(name, a);
    if (output.Has(name.id)) return CrtOutputApi(name, a);
    Fail(name);
    return 0;
#undef CRT
}
#include "x86_crt_decimal.inc"
} // namespace X86
