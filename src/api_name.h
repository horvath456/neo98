// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Numeric identity of a Windows API name.
// The ID is the 32-bit FNV-1a hash of the exported name. It is computed once:
// when an import is bound, or at compile time for a literal. Dispatch code
// switches on the ID and never compares strings. tools/test_api_ids.py proves
// that no two names known to the sources share an ID; two equal IDs inside one
// switch also fail to compile as duplicate case labels.
constexpr unsigned ApiId(const char *text)
{
    unsigned id = 2166136261u;
    while (*text)
    {
        id ^= (unsigned char)*text++;
        id *= 16777619u;
    }
    return id;
}
// Forces constant evaluation of ApiId for a literal, also without optimisation.
template <unsigned Id> struct ApiConstant
{
    static constexpr unsigned value = Id;
};
#define API_ID(literal) (ApiConstant<ApiId(literal)>::value)
// True when the ApiName variable carries the name given as a literal.
#define API_IS(variable, literal) ((variable).id == API_ID(literal))
// The name stays available for logs, traces and handlers that parse it
// (ordinal numbers, prefixes). It converts to const char * on its own.
struct ApiName
{
    const char *text;
    unsigned id;
    constexpr ApiName(const char *name) : text(name), id(name ? ApiId(name) : 0) {}
    constexpr ApiName(const char *name, unsigned known) : text(name), id(known) {}
    constexpr operator const char *() const { return text; }
};
// A case label of a switch over ApiName::id.
#define API_CASE(literal) case API_ID(literal):
// IDs of a constexpr name table, computed at compile time:
// static constexpr ApiIdTable ids(Exports);
// The entries are either strings or records with a member `name`.
constexpr const char *ApiEntryName(const char *text) { return text; }
template <class Entry> constexpr const char *ApiEntryName(const Entry &entry) { return entry.name; }
template <class Entry, unsigned Count> struct ApiIdTable
{
    unsigned ids[Count];
    constexpr explicit ApiIdTable(const Entry (&table)[Count]) : ids{}
    {
        for (unsigned i = 0; i < Count; ++i)
            ids[i] = ApiId(ApiEntryName(table[i]));
    }
    // Position of the ID in the table, or Count when the table lacks it.
    constexpr unsigned Index(unsigned id) const
    {
        unsigned i = 0;
        while (i < Count && ids[i] != id)
            ++i;
        return i;
    }
    constexpr bool Has(unsigned id) const { return Index(id) < Count; }
};
