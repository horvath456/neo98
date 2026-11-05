// SPDX-License-Identifier: GPL-2.0-or-later
// NotifyWinEvent does nothing when no hooks are installed. Otherwise dispatch
// through the hook list maintained by SetWinEventHook and UnhookWinEvent in
// src/x86_win_event.cpp. The API has no return value.
#pragma once
namespace Win98WinEvent {
// True when USER would enter the notification body.
inline bool Notifies(unsigned installed_hooks){return installed_hooks!=0;}
}
