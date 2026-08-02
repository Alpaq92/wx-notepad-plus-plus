// SPDX-License-Identifier: Apache-2.0
//
// wxNote - command ids for features Notepad++ has no equivalent for.
// Copyright 2026 The wxNote Authors.
//
// src/command_ids.h holds the FROZEN IDM_* values inherited from Notepad++, which must never move.
// Everything here is wxNote's own, so it lives in a private range well clear of those.
//
// This header exists so the values are declared exactly ONCE. They used to be an enum in main.cpp
// mirrored by hand in tests/keymap_selftest.cpp - which cannot include main.cpp, because
// terminal_panel.h drags in libvterm and that test is deliberately headless. The values are
// positional, so inserting an id in the middle silently renumbered one copy and not the other.

#pragma once

enum
{
    myID_CMP_FILE = 60220,      // View ▸ Compare ▸ ...
    myID_CMP_CLIP,
    myID_CMP_CLEAR,
    myID_CMP_NEXT,
    myID_CMP_PREV,
    myID_SPELLCHECK,            // View ▸ Spell Check ▸ ...
    myID_SPELL_COMMENTSONLY,
    myID_SPELL_MANAGE,
    myID_MACRO_MANAGE,          // Macro ▸ Manage Saved Macros...
    myID_COMMAND_PALETTE,       // Search ▸ Command Palette...
    myID_QUICK_OPEN,            // Search ▸ Quick Open...
};

// myID_VIEW_TERMINAL (60200) is NOT here: it belongs to terminal_panel.h, which owns the feature.
// The gap between it and 60220 is deliberate headroom for the terminal's own ids.
static_assert(myID_CMP_FILE == 60220 && myID_QUICK_OPEN == 60230,
              "private ids are persisted in user keymaps - renumbering them breaks saved shortcuts");
