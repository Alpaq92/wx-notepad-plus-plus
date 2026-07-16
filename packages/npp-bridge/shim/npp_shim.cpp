// SPDX-License-Identifier: GPL-3.0-or-later
//
// wxNote - Notepad++ plugin cross-platform shim (PART A: libnpp_shim).
// Copyright 2026 The wxNote Authors.
//
// The WHOLE body is compiled only off-Windows: on Windows a recompiled plugin
// uses the OS ::SendMessage, and defining our own would collide with it. The
// #ifndef _WIN32 guard still lets the Windows CI compiler syntax-check the file
// without emitting any of these symbols.

#ifndef _WIN32
#include "npp_shim.h"

// The host dispatch table, handed over once via npp_shim_bind() before the
// plugin issues any SendMessage. Null until then; forwarders no-op if unbound.
static const NppHostBridge* g_host = nullptr;

extern "C" NPP_EXPORT void npp_shim_bind(const NppHostBridge* h) { g_host = h; }

// The plugin author's unchanged ::SendMessage(hwnd, NPPM_*/SCI_*, w, l) calls
// resolve to THIS symbol at link time (there is no Win32 to provide it), so the
// message is routed straight into the host's bridge_dispatch via g_host->send.
extern "C" NPP_EXPORT LRESULT SendMessage(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    return g_host ? g_host->send(g_host->ctx, h, msg, w, l) : 0;
}
extern "C" NPP_EXPORT LRESULT SendMessageW(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    return g_host ? g_host->send(g_host->ctx, h, msg, w, l) : 0;
}

// (MessageBoxW->wxMessageBox, GetPrivateProfileString*, PathFileExists->
//  wxFileExists, clipboard helpers, ... are added here LAZILY only when a target
//  plugin actually fails to link -- SWELL discipline: a link error is a precise,
//  per-plugin porting TODO for the author, not something Phase 1 pre-solves.)
#endif // !_WIN32
