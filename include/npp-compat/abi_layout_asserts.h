// SPDX-License-Identifier: Apache-2.0
//
// wxNote - plugin ABI compatibility layer: compile-time layout guards.
// Copyright 2026 The wxNote Authors.
//
// A real, compiled Notepad++ plugin DLL was built against the real Notepad++ headers, and it
// reads/writes NppData/FuncItem/tTbData/CommunicationInfo through raw pointers at fixed byte
// offsets - there is no runtime negotiation. If our clean-room redeclaration of one of these
// structs in PluginInterface.h/Docking.h/Notepad_plus_msgs.h ever drifts (a field added,
// removed, reordered, or resized), the failure mode is a silent, wrong-offset memory read in a
// real plugin at runtime - not a build error. This header turns that into a build error instead,
// by asserting each field sits at the offset its own size/alignment implies relative to the
// field before it. That does NOT independently verify our layout matches the *real* upstream N++
// headers (this repo has no access to N++ source to compare against) - it only guarantees the
// layout can no longer change *silently*: touch a struct here, and either the asserts still
// hold (compiles clean) or they don't (compile error naming exactly which field moved).
//
// Included once from packages/npp-bridge/npp_bridge.cpp - the only GPL component in this repo
// that consumes these ABI headers (see docs/PLUGIN_API_PLAN.md §1). Windows-only, like the
// bridge itself: these structs' real-world layout only has to match compiled plugin DLLs on
// Windows, where the plugin-loading bridge actually runs.

#pragma once

#include <cstddef>   // offsetof
#include "PluginInterface.h"    // NppData, FuncItem, ShortcutKey
#include "Docking.h"            // DockedWidgetData / tTbData
#include "Notepad_plus_msgs.h"  // CommunicationInfo

namespace wxn_abi_layout_asserts {

// ---- NppData: three HWNDs, in declaration order, no gaps ------------------------------------
static_assert(offsetof(NppData, _nppHandle) == 0);
static_assert(offsetof(NppData, _scintillaMainHandle)   == sizeof(HWND));
static_assert(offsetof(NppData, _scintillaSecondHandle) == 2 * sizeof(HWND));
static_assert(sizeof(NppData) == 3 * sizeof(HWND));

// ---- FuncItem: fixed-width name buffer, function pointer, id, flag, shortcut pointer ---------
static_assert(offsetof(FuncItem, _itemName) == 0);
static_assert(offsetof(FuncItem, _pFunc) == sizeof(wchar_t) * menuItemSize);
static_assert(offsetof(FuncItem, _cmdID) == offsetof(FuncItem, _pFunc) + sizeof(PFUNCPLUGINCMD));
static_assert(offsetof(FuncItem, _init2Check) == offsetof(FuncItem, _cmdID) + sizeof(int));
// _pShKey is a pointer following a bool, so the compiler may insert alignment padding here -
// assert it lands on a pointer-aligned boundary no earlier than the previous field ends, rather
// than a hardcoded byte offset that would depend on padding rules we shouldn't need to encode by hand.
static_assert(offsetof(FuncItem, _pShKey) >= offsetof(FuncItem, _init2Check) + sizeof(bool));
static_assert(offsetof(FuncItem, _pShKey) % alignof(ShortcutKey*) == 0);
static_assert(sizeof(FuncItem) >= offsetof(FuncItem, _pShKey) + sizeof(ShortcutKey*));

// ---- ShortcutKey: three bools + one byte, no surprises ---------------------------------------
static_assert(offsetof(ShortcutKey, _isCtrl)  == 0);
static_assert(offsetof(ShortcutKey, _isAlt)   == sizeof(bool));
static_assert(offsetof(ShortcutKey, _isShift) == 2 * sizeof(bool));
static_assert(offsetof(ShortcutKey, _key)     == 3 * sizeof(bool));

// ---- DockedWidgetData / tTbData: the dockable-panel registration payload ---------------------
static_assert(offsetof(tTbData, hClient) == 0);
static_assert(offsetof(tTbData, pszName) == sizeof(HWND));
static_assert(offsetof(tTbData, dlgID)   == offsetof(tTbData, pszName) + sizeof(const wchar_t*));
static_assert(offsetof(tTbData, uMask)   == offsetof(tTbData, dlgID) + sizeof(int));
// hIconTab is a pointer following a UINT - may need alignment padding before it.
static_assert(offsetof(tTbData, hIconTab) >= offsetof(tTbData, uMask) + sizeof(UINT));
static_assert(offsetof(tTbData, hIconTab) % alignof(HICON) == 0);
static_assert(offsetof(tTbData, pszAddInfo) == offsetof(tTbData, hIconTab) + sizeof(HICON));
static_assert(offsetof(tTbData, rcFloat)    == offsetof(tTbData, pszAddInfo) + sizeof(const wchar_t*));
static_assert(offsetof(tTbData, iPrevCont)  == offsetof(tTbData, rcFloat) + sizeof(RECT));
// pszModuleName is a pointer following an int - may need alignment padding before it.
static_assert(offsetof(tTbData, pszModuleName) >= offsetof(tTbData, iPrevCont) + sizeof(int));
static_assert(offsetof(tTbData, pszModuleName) % alignof(const wchar_t*) == 0);
static_assert(sizeof(tTbData) >= offsetof(tTbData, pszModuleName) + sizeof(const wchar_t*));

// ---- CommunicationInfo: the NPPM_MSGTOPLUGIN relay payload ------------------------------------
static_assert(offsetof(CommunicationInfo, internalMsg) == 0);
static_assert(offsetof(CommunicationInfo, srcModuleName) >= sizeof(long));
static_assert(offsetof(CommunicationInfo, srcModuleName) % alignof(const wchar_t*) == 0);
static_assert(offsetof(CommunicationInfo, info) == offsetof(CommunicationInfo, srcModuleName) + sizeof(const wchar_t*));

}  // namespace wxn_abi_layout_asserts
