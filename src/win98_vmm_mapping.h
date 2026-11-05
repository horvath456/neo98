// SPDX-License-Identifier: GPL-2.0-or-later
// Bounded decisions from Win98 VMM _PageCommit, W16C object 14. This is not
// the complete pager, reservation validator, page-fault path or rollback code.
#pragma once
namespace Win98VmmMapping
{
using U = unsigned;
enum class Action : U
{
    NewPage,
    Reuse,
    Reject
};
struct ExistingResult
{
    Action action;
    U entry;
    int committed_delta;
};
// PR_STATIC reservations require PC_STATIC at commit time.
inline bool StaticAllowed(U reservation_flags, U flags)
{
    return !(reservation_flags & 0x10) || (flags & 0x20000000u);
}
// PC_INCR advances the pager data
// once per processed page, including an already committed page.
inline U PagerIncrement(U flags) { return (flags >> 30) & 1u; }
inline U AdvancePagerData(U data, U flags) { return data + PagerIncrement(flags); }
// entry_base is the allocated descriptor index shifted by 12
// in the surrounding original routine; descriptor allocation stays a provider.
inline U InstallBits(U entry_base, U flags) { return entry_base | (((flags & 0x60000u) | 0x2000000u) >> 16); }
// The caller supplies a page-rejection mask and NewPage/Reject/Reuse
// continuations. A WRITE request adds permission bit 0x2; READ never removes
// existing WRITE. Adjust commit accounting by -1.
inline ExistingResult Existing(U entry, U flags, U reject_mask)
{
    if (!(entry & 0x200))
        return {Action::NewPage, entry, 0};
    if (flags & reject_mask)
        return {Action::Reject, entry, 0};
    if (flags & 0x20000)
        entry |= 2;
    return {Action::Reuse, entry, -1};
}
} // namespace Win98VmmMapping
