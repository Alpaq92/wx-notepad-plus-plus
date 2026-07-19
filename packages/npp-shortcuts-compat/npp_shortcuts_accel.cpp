// SPDX-License-Identifier: GPL-3.0-or-later
//
// npp-shortcuts-compat - Windows VK -> wxNote portable accelerator-string builder.
// Copyright 2026 The wxNote Authors. See LICENSE (GPL-3.0-or-later).

#include "npp_shortcuts_accel.h"

namespace nppcompat {

std::string vkToToken(int vk)
{
    // Digits 0-9 (VK 0x30-0x39) and letters A-Z (VK 0x41-0x5A): the VK IS the ASCII code, so wx's
    // single-character path parses "5" / "A" directly.
    if (vk >= 0x30 && vk <= 0x39) return std::string(1, (char)vk);
    if (vk >= 0x41 && vk <= 0x5A) return std::string(1, (char)vk);

    // Function keys F1..F24 (VK 0x70-0x87) -> wx's "F<n>" numbered-key form.
    if (vk >= 0x70 && vk <= 0x87) return "F" + std::to_string(vk - 0x70 + 1);

    // Numpad digits KP_0..KP_9 (VK 0x60-0x69) -> wx's "KP_<n>" numbered-key form.
    if (vk >= 0x60 && vk <= 0x69) return "KP_" + std::to_string(vk - 0x60);

    switch (vk) {
        // Navigation
        case 0x25: return "Left";
        case 0x26: return "Up";
        case 0x27: return "Right";
        case 0x28: return "Down";
        case 0x21: return "PageUp";      // VK_PRIOR
        case 0x22: return "PageDown";    // VK_NEXT
        case 0x23: return "End";
        case 0x24: return "Home";
        case 0x2D: return "Insert";
        case 0x2E: return "Delete";

        // Editing / whitespace
        case 0x08: return "Back";        // VK_BACK  (wx: "Back"/"Backspace")
        case 0x09: return "Tab";
        case 0x0D: return "Enter";       // VK_RETURN (wx: "Enter"/"Return")
        case 0x1B: return "Esc";         // wx: "Esc"/"Escape"
        case 0x20: return "Space";

        // Numpad operators (wx wxKeyNames spellings)
        case 0x6A: return "KP_Multiply";
        case 0x6B: return "KP_Add";
        case 0x6C: return "KP_Separator";
        case 0x6D: return "KP_Subtract";
        case 0x6E: return "KP_Decimal";
        case 0x6F: return "KP_Divide";

        // OEM punctuation (US layout). wx parses each as a single literal character, and re-emits the
        // same character via ToRawString, so these round-trip. The base (unshifted) character is used;
        // an accompanying Shift flag is preserved as a modifier exactly as Notepad++ records it.
        case 0xBA: return ";";           // VK_OEM_1
        case 0xBB: return "=";           // VK_OEM_PLUS   (base key is '=')
        case 0xBC: return ",";           // VK_OEM_COMMA
        case 0xBD: return "-";           // VK_OEM_MINUS
        case 0xBE: return ".";           // VK_OEM_PERIOD
        case 0xBF: return "/";           // VK_OEM_2
        case 0xC0: return "`";           // VK_OEM_3
        case 0xDB: return "[";           // VK_OEM_4
        case 0xDC: return "\\";          // VK_OEM_5
        case 0xDD: return "]";           // VK_OEM_6
        case 0xDE: return "'";           // VK_OEM_7

        default: return "";              // modifier-only VKs, VK 0, or anything layout-dependent: unmapped
    }
}

std::string buildAccel(const NppKey& key)
{
    const std::string token = vkToToken(key.vk);
    if (token.empty()) return "";        // unmappable -> caller records "unmapped" and skips (never binds "")
    std::string s;
    if (key.ctrl)  s += "Ctrl+";
    if (key.alt)   s += "Alt+";
    if (key.shift) s += "Shift+";
    s += token;
    return s;
}

}  // namespace nppcompat
