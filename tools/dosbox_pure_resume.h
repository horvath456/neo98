// SPDX-License-Identifier: GPL-2.0-or-later
// Native DOS continuations. Frames describe values, never addresses of a
// host stack or code. A freshly started machine recreates the same call
// chain; the provider restores its locals before the loaded CPU runs on.
#pragma once
#include "dbp_serialize.h"
#include <cassert>
#include <cstring>
#include <functional>
#include <vector>

struct Neo98ResumeFrame
{
    unsigned tag;
    std::function<void(DBPArchive&, bool)> fields;
    Neo98ResumeFrame(unsigned tag_, std::function<void(DBPArchive&, bool)> fields_);
    ~Neo98ResumeFrame();
    Neo98ResumeFrame(const Neo98ResumeFrame&) = delete;
    Neo98ResumeFrame& operator=(const Neo98ResumeFrame&) = delete;
};
bool Neo98ResumeSafe();

// Forced shutdown ends native guest calls too. Returning from an unfinished
// page fault would let its caller use a page the guest never made present.
struct Neo98DosShutdown {};
template<class Number> struct Neo98ScopeCount
{
    Number &count;
    explicit Neo98ScopeCount(Number &value) : count(value) { ++count; }
    ~Neo98ScopeCount() { --count; }
    Neo98ScopeCount(const Neo98ScopeCount&) = delete;
    Neo98ScopeCount& operator=(const Neo98ScopeCount&) = delete;
};
using Neo98NestedRun = Neo98ScopeCount<int>;
void PAGING_NEO98_Shutdown();
void DOSBOX_NEO98_Shutdown();
