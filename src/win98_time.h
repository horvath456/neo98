// SPDX-License-Identifier: GPL-2.0-or-later
// Original KERNEL32 calendar and IFSMGR DOS-time cores.
#pragma once
namespace Win98Time
{
using U = unsigned;
using W = unsigned short;
using Q = unsigned long long;
struct SystemTime { W year, month, weekday, day, hour, minute, second, milliseconds; };
static_assert(sizeof(SystemTime) == 16, "Original SYSTEMTIME layout");
struct Bias { Q ticks = 0; U minutes = 0; };
inline int SignedWord(U value) { value &= 65535; return value < 32768 ? int(value) : int(value) - 65536; }
inline U ExtendWord(U value) { return U(SignedWord(value)); }
inline long long SignedDword(U value) { return value < 0x80000000u ? value : (long long)value - 0x100000000ll; }
inline Q SignedProduct(U a, U b) { return Q(SignedDword(a) * SignedDword(b)); }
inline Bias MinutesBias(U minutes) { return {SignedProduct(minutes, 600000000), minutes}; }
inline bool Leap(U year) { return !(year % 400) || ((year % 100) && !(year & 3)); }
// KERNEL32/. The byte day-to-month tables and
// contain the inverse of these exact month starts.
inline constexpr W MonthStarts[2][13] = {
    {0,31,59,90,120,151,181,212,243,273,304,334,365},
    {0,31,60,91,121,152,182,213,244,274,305,335,366}
};

// High half of the original four-limb product. Using DWORD limbs
// avoids an ARM compiler-runtime dependency for 128-bit multiplication.
inline Q ProductHigh(Q a, Q b)
{
    const Q a0 = U(a), a1 = a >> 32, b0 = U(b), b1 = b >> 32;
    const Q low = a0 * b0;
    const Q middle = a1 * b0 + (low >> 32);
    const Q carry = a0 * b1 + U(middle);
    return a1 * b1 + (middle >> 32) + (carry >> 32);
}
inline Q MagicDivide(Q value, Q factor, U shift)
{
    const bool negative = (value >> 63) != 0;
    const Q magnitude = negative ? Q(0) - value : value;
    const Q quotient = ProductHigh(magnitude, factor) >> shift;
    return negative ? Q(0) - quotient : quotient;
}
// These are the original reciprocal constants and shifts.
inline void Split(Q time, U &days, U &milliseconds)
{
    const Q ms = MagicDivide(time, 0xd1b71758e219652cull, 13);
    days = U(MagicDivide(ms, 0xc6d750ebfa67b90eull, 26));
    milliseconds = U(ms - SignedProduct(days, 86400000));
}
// including signed DWORD day multiplication and low-QWORD wrap.
inline Q Compose(U days, U milliseconds)
{
    return (SignedProduct(days, 86400000) + milliseconds) * 10000;
}
// original 400/100/4/1-year cycle arithmetic.
inline U Years(U days)
{
    U cycles = days / 146097;
    days -= cycles * 146097;
    const U centuries = (days * 100 + 75) / 3652425;
    days -= centuries * 36524;
    cycles = (centuries + cycles * 4) * 25;
    const U fours = days / 1461;
    cycles += fours;
    days -= fours * 1461;
    return ((days * 100 + 75) / 36525) + cycles * 4;
}
// plus public field reorder. It always succeeds,
// even for FILETIMEs whose signed/WORD truncations produce unusual fields.
inline void FileToSystem(Q time, SystemTime &out)
{
    U days, ms;
    Split(time, days, ms);
    const W weekday = W((days + 1) % 7);
    const U years = Years(days);
    const U ordinal = days - years * 365 - (years >> 2) - years / 400 + years / 100;
    const bool leap = Leap(years + 1);
    const auto &starts = MonthStarts[leap];
    const U index = ordinal % (leap ? 366 : 365);
    U month = 0;
    while (month < 11 && index >= starts[month + 1]) ++month;
    const U seconds = ms / 1000, minutes = seconds / 60;
    out = {W(years + 1601), W(month + 1), weekday, W(ordinal - starts[month] + 1),
           W(minutes / 60), W(minutes % 60), W(seconds % 60), W(ms % 1000)};
}
// plus public field reorder. The source mixes
// sign extension and unsigned comparisons. In particular, day zero passes its
// sole signed upper-bound test. Weekday is never read by this conversion.
inline bool SystemToFile(const SystemTime &in, Q &out)
{
    const U year = ExtendWord(in.year), month = ExtendWord(in.month) - 1;
    const U day = ExtendWord(in.day) - 1;
    const U hour = ExtendWord(in.hour), minute = ExtendWord(in.minute);
    const U second = ExtendWord(in.second), ms = ExtendWord(in.milliseconds);
    if (year < 1601 || month > 11) return false;
    const auto &starts = MonthStarts[Leap(year)];
    if (SignedWord(day) >= int(starts[month + 1] - starts[month]) ||
        hour > 23 || minute > 59 || second > 59 || ms > 999) return false;
    const U previous = year - 1601;
    U days = (previous >> 2) - previous / 100 + year * 365 + previous / 400 - 0x8eaad;
    days += MonthStarts[Leap(year - 1600)][month] + day;
    out = Compose(days, ((hour * 60 + minute) * 60 + second) * 1000 + ms);
    return true;
}
// Windows 98 compares both DWORDs unsigned.
inline U Compare(Q a, Q b) { return a == b ? 0 : a < b ? 0xffffffffu : 1; }
// Arithmetic wraps at QWORD limits.
inline Q ToLocal(Q time, Q bias) { return time - bias; }
inline Q ToUtc(Q time, Q bias) { return time + bias; }

// IFSMGR object1:6324. Packed DOS month values 13..15 are not rejected; the
// original reads the adjacent WORDs 365,22,12, retained here for the full domain.
inline constexpr W DosMonthStarts[16] = {0,0,31,59,90,120,151,181,212,243,273,304,334,365,22,12};
// IFSMGR object4:42FE..43AC. Arithmetic is the original wrapping DWORD path;
// its leap-year rule uses four-year cycles throughout the packed DOS range.
inline U DosToUnix(U packed, U bias_minutes)
{
    if (!packed) return 0;
    const U date = packed >> 16, time = packed & 65535, year = date >> 9;
    const U month_bits = date & 0x1e0;
    U days = year * 365 + year / 4 + DosMonthStarts[month_bits >> 5] + (date & 31);
    if (!(date & 0x600) && month_bits <= 0x40) --days;
    const U halves = (((time >> 11) * 60 + ((time >> 5) & 63)) * 30) + (time & 31);
    return ((bias_minutes + (days + 0xe44) * 1440) * 30 + halves) * 2;
}
// IFSMGR object4:41E0..42FD. Zero Unix seconds is the original DOS-zero sentinel.
inline U UnixToDos(U seconds, U bias_minutes)
{
    if (!seconds) return 0;
    const U halves = (seconds - bias_minutes * 60 - 0x12cea600) >> 1;
    const U day_halves = halves % 43200, minutes = day_halves / 30;
    const W time = W((day_halves % 30) | ((minutes % 60) << 5) | ((minutes / 60) << 11));
    U days = halves / 43200;
    W year = W((days / 1461) * 4);
    days %= 1461;
    if (days == 59) return (U(W((year << 9) + 0x5d)) << 16) | time;
    if (days > 59) --days;
    year = W(year + days / 365);
    days %= 365;
    U month = 1;
    while (month < 12 && DosMonthStarts[month + 1] <= days) ++month;
    const W date = W((((year << 4) + month) << 5) + days - DosMonthStarts[month] + 1);
    return (U(date) << 16) | time;
}
// IFSMGR object1:3309..332B and object4:FFBD..FFEB. The reverse returns both
// Unix seconds and subsecond ticks; out-of-range or zero inputs return zeroes.
inline Q UnixToFile(U seconds)
{
    return seconds ? Q(seconds) * 10000000 + 0x019db1ded53e8000ull : 0;
}
inline U FileToUnix(Q time, U &fraction)
{
    fraction = 0;
    if (!time || time < 0x019db1ded53e8000ull) return 0;
    const Q delta = time - 0x019db1ded53e8000ull;
    if ((delta >> 32) >= 10000000) return 0;
    fraction = U(delta % 10000000);
    return U(delta / 10000000);
}
// IFSMGR object4:FFF3..1003C. Bias negation wraps as a DWORD before IMUL.
inline bool DosRange(Q time, U bias_minutes)
{
    if (!time) return true;
    const Q local = time + SignedProduct(U(0) - bias_minutes, 600000000);
    return local >= 0x01a8e79fe1d58000ull && local <= 0x023868b866d29300ull;
}
// Complete IFSMGR DOS71A7 conversion body, object4:9111..922C. The request's
// pointer mapping is a provider boundary. Return is its error WORD at +1A.
inline U DosService(U mode, U &date, U &time, U &hundredths, Q &file, U bias_minutes)
{
    mode &= 255;
    if (mode == 0)
    {
        if (!DosRange(file, bias_minutes)) return 13;
        U fraction;
        const U seconds = FileToUnix(file, fraction), packed = UnixToDos(seconds, bias_minutes);
        date = packed >> 16;
        time = packed & 65535;
        hundredths = W(fraction / 100000 + ((seconds & 1) ? 100 : 0)) & 255;
        return 0;
    }
    if (mode == 1)
    {
        file = UnixToFile(DosToUnix(((date & 65535) << 16) | (time & 65535), bias_minutes));
        file += (hundredths & 255) * 100000;
        return 0;
    }
    return 1;
}
// Both original caches are explicit
// so the oracle can verify the complete chain, including inconsistent state.
inline U DosToFile(U date, U time, const Bias &bias, Q &file)
{
    U hundredths = 0;
    const U error = DosService(1, date, time, hundredths, file, bias.minutes);
    if (!error) file = ToLocal(file, bias.ticks);
    return error;
}
inline U FileToDos(Q file, const Bias &bias, U &date, U &time)
{
    file = ToUtc(file, bias.ticks);
    U hundredths = 0;
    return DosService(0, date, time, hundredths, file, bias.minutes);
}
} // namespace Win98Time
