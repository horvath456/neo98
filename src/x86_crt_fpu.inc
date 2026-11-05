// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside Application::CrtApi. The original wrappers own the control
// sequence; this adapter supplies the engine registers and its FISTP instruction.
    API_CASE("_ftol") API_CASE("_statusfp") API_CASE("_clearfp") API_CASE("_fpreset")
    {
        struct Environment {
            Application &app;
            unsigned Control() { return app.Register(Reg::Fpcw); }
            void Control(unsigned value) { app.Register(Reg::Fpcw, value); }
            unsigned Status() { return app.Register(Reg::Fpsw); }
            void Wait() {} // Preflight below rejects undeliverable pending exceptions.
            void Clear() { app.Register(Reg::Fpsw, X86FpuInteger::ClearStatus(Status())); }
            unsigned ExceptionPointers() {
                // CRT signal/SEH floating filters do not yet establish the
                // original per-thread _pxcptinfoptrs slot. No active CRT filter.
                return 0;
            }
            unsigned Read(unsigned p) { return app.Read32(p); }
            void Write(unsigned p, unsigned v) { app.Write32(p, v); }
            void Initialize() {
                Control(0x37f);
                app.Register(Reg::Fpsw, 0);
                app.Register(Reg::Fptag, 0xffff);
                const uint64_t zero = 0;
                app.Check(app.cpu->Set(Reg::Fip, zero));
                app.Check(app.cpu->Set(Reg::Fdp, zero));
                app.Register(Reg::Fcs, 0);
                app.Register(Reg::Fds, 0);
                app.Register(Reg::Fop, 0);
            }
            uint64_t IntegerPop() {
                const unsigned status = Status(), top = (status >> 11) & 7;
                const unsigned tags = app.Register(Reg::Fptag);
                uint64_t raw[2] = {};
                app.Check(app.cpu->GetWide(Reg::Fp0 + top, raw));
                const auto result = X86FpuInteger::Truncate(raw[0], unsigned(raw[1]),
                                                          ((tags >> (top * 2)) & 3) == 3);
                app.Register(Reg::Fptag, tags | (3u << (top * 2)));
                app.Register(Reg::Fpsw, X86FpuInteger::PopStatus(status, result.flags));
                return result.bits;
            }
        } e{*this};
        if (API_IS(name,"_fpreset")) { Win98CrtFpu::Reset(e); return 0; }
        if (API_IS(name,"_clearfp")) return Win98CrtFpu::Clear(e);
        if (X86FpuInteger::Pending(e.Control(), e.Status())) {
            Fail("CRT floating-point operation requires pending x87 exception delivery");
            return 0;
        }
        if (API_IS(name,"_statusfp")) return Win98CrtFpu::Status(e);
        // Refuse an unmasked new exception before changing CW, stack or output.
        const unsigned top = (e.Status() >> 11) & 7;
        uint64_t raw[2] = {};
        if (!Check(cpu->GetWide(Reg::Fp0 + top, raw))) return 0;
        const auto planned = X86FpuInteger::Truncate(raw[0], unsigned(raw[1]),
                        ((Register(Reg::Fptag) >> (top * 2)) & 3) == 3);
        if ((planned.flags & ~e.Control()) & 0x3f) {
            Fail("CRT integer conversion requires unmasked x87 exception delivery");
            return 0;
        }
        const uint64_t result = Win98CrtFpu::ToLong(e);
        Register(Reg::Edx, unsigned(result >> 32));
        return unsigned(result);
    }
