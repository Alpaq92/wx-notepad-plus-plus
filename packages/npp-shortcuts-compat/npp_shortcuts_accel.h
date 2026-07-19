// SPDX-License-Identifier: GPL-3.0-or-later
//
// npp-shortcuts-compat - Windows VK -> wxNote portable accelerator-string builder.
// Copyright 2026 The wxNote Authors. See LICENSE (GPL-3.0-or-later).
//
// Notepad++ stores each shortcut as three modifier booleans plus a Windows virtual-key code
// (decimal). wxNote's nib.keymap capability takes a PORTABLE accelerator STRING that the host
// parses with wxAcceleratorEntry::FromString (and re-emits canonically with ToRawString). This
// module bridges the two: it maps a VK to the exact token wx's FromString accepts, then assembles
// "Ctrl+Shift+<token>". It is wx-free (returns a std::string) so the mapping is unit-testable
// standalone; the token spellings were taken directly from wx's wxKeyNames table + ParseAccel
// (src/common/accelcmn.cpp) so every string this produces round-trips through the host.
//
// No RawCtrl is ever emitted: Notepad++ is Windows-only, so its Ctrl is always the physical Control,
// and the host's automatic Ctrl->Cmd remap on macOS is the correct N++-on-Mac behaviour.

#pragma once

#include "npp_shortcuts_parse.h"
#include <string>

namespace nppcompat {

// The wx accelerator token for a Windows virtual-key code, or "" if there is no portable equivalent
// (modifier-only VKs, VK 0, and anything not in the table). Alphanumerics, function keys, navigation,
// numpad and the US-layout OEM punctuation keys are covered (~60 entries).
std::string vkToToken(int vk);

// Build the full portable accelerator string for a keystroke, e.g. "Ctrl+Shift+S", or "" when the VK
// is unmappable (the caller then records it as unmapped and does NOT send it to the host - an empty
// accel would otherwise be read by the host as an UNBIND). Modifier order is Ctrl, Alt, Shift; the
// host re-canonicalises via ToRawString, so order here only has to be one FromString accepts.
std::string buildAccel(const NppKey& key);

}  // namespace nppcompat
