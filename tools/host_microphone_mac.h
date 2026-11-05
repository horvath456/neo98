// SPDX-License-Identifier: GPL-2.0-or-later
// The microphone permission of macOS, tools/host_microphone_mac.mm.
#pragma once
namespace HostMicrophone
{
enum Permission { Granted, Undetermined, Refused };
// What macOS says now, without asking.
Permission Current();
// Shows the question of macOS once and returns at once.
void Ask();
// The answer to Ask, Undetermined while the user has not answered.
Permission Answer();
} // namespace HostMicrophone
