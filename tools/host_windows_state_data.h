// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdio>
#include "host_windows_data.h"
#include "../src/x86_state_fields.h"
namespace HostWindowsData {
inline void RegisterStateData() {
    char identity[32];
    snprintf(identity, sizeof identity, "%016llx", (unsigned long long)importIdentity);
    // Increment the schema when an importer changes the meaning of offsets
    // or the ordering of retained strings.
    const std::string prefix = std::string("windows-import/1/") + identity + "/";
    auto visit = [&](const char *name, const void *base, size_t bytes) {
        if (bytes) MachineState::ImportedRegion((prefix + name).c_str(), base, bytes);
    };
    VisitWindowsRuntimeData(visit);
    VisitWindowsRuntimeLocale(visit);
    auto vector = [&](const char *name, const auto &values) {
        visit(name, values.data(), values.size() * sizeof(values[0]));
    };
    vector("messages", OriginalMultimediaMessages);
    vector("locales", OriginalLocaleData);
    vector("calendars", OriginalCalendars);
    vector("eras", OriginalCalendarEras);
    vector("locale-calendars", OriginalLocaleCalendars);
    vector("locale-formats", OriginalLocaleFormats);
    size_t index = 0;
    for (const auto &text : localeStrings)
        visit(("text/" + std::to_string(index++)).c_str(), text.c_str(), text.size() + 1);
    index = 0;
    for (const auto &text : formatStrings)
        visit(("format-text/" + std::to_string(index++)).c_str(), text.c_str(), text.size() + 1);
}
}
