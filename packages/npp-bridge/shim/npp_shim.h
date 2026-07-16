// SPDX-License-Identifier: GPL-3.0-or-later
//
// wxNote - Notepad++ plugin cross-platform shim (PART A: libnpp_shim).
// Copyright 2026 The wxNote Authors.
//
// This header is linked INTO each recompiled Notepad++ plugin .so/.dylib. It
// declares the host-dispatch vtable (NppHostBridge) that wxNote's npp-bridge
// binds into the plugin, plus the bind entry point the bridge resolves and
// calls after loading the plugin. On Windows the plugin keeps using the OS
// ::SendMessage, so the shim's forwarding definitions are compiled out there
// (see npp_shim.cpp); this header, however, stays type-checked on every OS.

#pragma once
#include "npp_plugin_port.h"     // HWND/LRESULT/UINT/WPARAM/LPARAM + NPP_EXPORT

#ifdef __cplusplus
extern "C" {
#endif

// The host-dispatch vtable the bridge binds into the plugin's shim. (CONTRACT 2)
typedef struct NppHostBridge {
    void*   ctx;                 // opaque host state (plugin never inspects it)
    LRESULT (*send)(void* ctx, HWND handle, UINT msg, WPARAM wParam, LPARAM lParam);
} NppHostBridge;

// Plugin-side entry the bridge resolves + calls once, after dlopen, to hand the
// plugin its host dispatch table. The shim keeps the pointer and routes every
// SendMessage()/SendMessageW() from the plugin's own code through it.
NPP_EXPORT void npp_shim_bind(const NppHostBridge* host);

#ifdef __cplusplus
}
#endif
