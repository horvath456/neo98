// SPDX-License-Identifier: GPL-2.0-or-later
// Host bridge for the implemented SystemParametersInfo write branches.
// The environment stores its words in a copy of the analyzed USER data segment so
// the comparison can check every byte the original touches.
#include "../src/win98_metrics.h"
#include "../src/win98_minmax.h"
#include "../src/win98_parameters_write.h"
#include <string.h>

namespace
{
using U = unsigned;
struct Trace
{
    U *records, capacity, count;
    void Add(U a, U b = 0, U c = 0, U d = 0)
    {
        if (count + 4 > capacity)
            return;
        records[count++] = a;
        records[count++] = b;
        records[count++] = c;
        records[count++] = d;
    }
};
struct State final : Win98ParametersWrite::Environment, Win98Metrics::Environment
{
    unsigned char *data;
    Trace &trace;
    U profile_result, font_handle;
    State(unsigned char *d, Trace &t, U profile, U font)
        : data(d), trace(t), profile_result(profile), font_handle(font)
    {
    }
    unsigned short Word(U offset) const override
    {
        return (unsigned short)(U(data[offset]) | U(data[offset + 1]) << 8);
    }
    void SetWord(U offset, unsigned short value) override
    {
        data[offset] = (unsigned char)value;
        data[offset + 1] = (unsigned char)(value >> 8);
    }
    unsigned short ExpectedVersion() const override { return Word(0x1200); }
    U PenWindows() override { return 0; }
    int Read16(U offset) const
    {
        const U w = Word(offset);
        return (w & 0x8000) ? int(w) - 65536 : int(w);
    }
    void Rect(U offset, Win98ParametersWrite::Rect &r) const
    {
        r = {Read16(offset), Read16(offset + 2), Read16(offset + 4), Read16(offset + 6)};
    }
    void MonitorRect(Win98ParametersWrite::Rect &r) const override { Rect(0x11b2, r); }
    void WorkArea(Win98ParametersWrite::Rect &r) const override { Rect(0x11ba, r); }
    void SetWorkArea(const Win98ParametersWrite::Rect &r) override
    {
        SetWord(0x11ba, (unsigned short)r.left);
        SetWord(0x11bc, (unsigned short)r.top);
        SetWord(0x11be, (unsigned short)r.right);
        SetWord(0x11c0, (unsigned short)r.bottom);
    }
    void RecomputeMetrics() override { Win98Metrics::Recompute(*this); }
    void RepositionWindows(const Win98ParametersWrite::Rect &previous) override
    {
        trace.Add(1, U(previous.left) & 0xffff, U(previous.top) & 0xffff, U(previous.right) & 0xffff);
    }
    bool WriteProfileInt(U identifier, int value) override
    {
        trace.Add(2, identifier, U(value) & 0xffff);
        return profile_result != 0;
    }
    bool WriteProfileFont(U identifier, const unsigned char *logfont) override
    {
        trace.Add(3, identifier, logfont ? U(logfont[0]) | U(logfont[1]) << 8 : 0,
                  logfont ? U(logfont[8]) | U(logfont[9]) << 8 : 0);
        return profile_result != 0;
    }
    void ApplyNonClientMetrics(const unsigned char *m) override { trace.Add(4, m[4] | U(m[5]) << 8); }
    void ApplyMinimizedMetrics(const unsigned char *m) override { trace.Add(5, m[4] | U(m[5]) << 8); }
    void ApplyIconMetrics(const unsigned char *m) override { trace.Add(6, m[4] | U(m[5]) << 8); }
    U CreateIconTitleFont(const unsigned char *logfont) override
    {
        trace.Add(7, logfont ? U(logfont[0]) | U(logfont[1]) << 8 : 0);
        return font_handle;
    }
    U IconTitleFont() const override { return Word(0x1b0); }
    void SetIconTitleFont(U font) override { SetWord(0x1b0, (unsigned short)font); }
    void DeleteFont(U font) override { trace.Add(8, font); }
    // Matches the oracle's GDI GetObject provider, which writes a zeroed object.
    bool FontObject(U, unsigned char (&logfont)[50]) const override
    {
        memset(logfont, 0, sizeof logfont);
        return true;
    }
    U AppVersion() const override { return Word(0x1202); }
    void RepaintDesktopSetting(U first, U second) override { trace.Add(9, first, second); }
    void RepaintDesktop() override { trace.Add(10); }
    bool SetDeskWallpaper(const Win98ParametersWrite::Parameter &) override { return true; }
    bool SetDeskPattern(const Win98ParametersWrite::Parameter &) override { return true; }
    bool WriteProfileText(U identifier, const char *) override
    {
        trace.Add(11, identifier);
        return profile_result != 0;
    }
    bool SectionString(U identifier, char *out, U size) override
    {
        trace.Add(12, identifier);
        if (size)
            out[0] = 0;
        return false;
    }
    void Broadcast(U action, const char *) override { trace.Add(13, action); }
};
} // namespace

extern "C" int spi_write_apply(unsigned char *data, unsigned action, unsigned uiParam,
                               const unsigned char *parameter, unsigned parameter_size,
                               unsigned sentinel, unsigned flags, unsigned profile_result,
                               unsigned font_handle, unsigned *records, unsigned capacity,
                               unsigned *count)
{
    Trace trace{records, capacity, 0};
    State state(data, trace, profile_result, font_handle);
    Win98ParametersWrite::Parameter p;
    p.data = parameter;
    p.capacity = parameter_size;
    p.sentinel = sentinel;
    p.null = parameter == nullptr;
    const int result = Win98ParametersWrite::Write(state, action, uiParam, p, flags);
    *count = trace.count;
    return result;
}

extern "C" int spi_border_units(unsigned style, unsigned extended, unsigned frame, unsigned extra,
                                unsigned border)
{
    return Win98MinMax::BorderUnits(style, extended, frame != 0, extra != 0, border);
}

// The work-area correction of USER for one top-level window on one
// monitor: in = monitor[4], work[4], caption height, style, the application
// window flag, the reserved word of the monitor, ptMaxSize, ptMaxPosition;
// out = ptMaxSize, ptMaxPosition.
extern "C" void spi_minmax_adjust(const int *in, int *out)
{
    struct E final : Win98MinMax::Environment
    {
        const int *in;
        explicit E(const int *values) : in(values) {}
        unsigned short Word(Win98MinMax::U offset) const override
        {
            return offset == 0x2e6 ? (unsigned short)in[8] : 0;
        }
        void MonitorRect(Win98MinMax::Rect &r) const override { r = {in[0], in[1], in[2], in[3]}; }
        void WorkArea(Win98MinMax::Rect &r) const override { r = {in[4], in[5], in[6], in[7]}; }
        bool MonitorReserved() const override { return in[11] != 0; }
        bool ParentIsDesktop() const override { return true; }
        void ParentClientRect(Win98MinMax::Rect &) const override {}
        Win98MinMax::U Style() const override { return unsigned(in[9]); }
        Win98MinMax::U ExtendedStyle() const override { return 0; }
        bool AppWindow() const override { return in[10] != 0; }
        bool Maximizable() const override { return true; }
        void SetWorkAreaMaximized(bool) override {}
    } e(in);
    Win98MinMax::Info info = {};
    info.max_size = {in[12], in[13]};
    info.max_position = {in[14], in[15]};
    Win98MinMax::Adjust(e, info);
    out[0] = info.max_size.x;
    out[1] = info.max_size.y;
    out[2] = info.max_position.x;
    out[3] = info.max_position.y;
}
