#pragma once
#include "menu_model.h"
#include "menu_labels_document.h"
#include "menu_data_encoding.h"   // reuses kEncodingMenuItems verbatim (Encoding nests one level deeper here)
#include "command_ids.h"

// ----------------------------------------------------------------- Document
// Phase B reshape: Language and Encoding, formerly two top-level menus, now live as submenus of
// one "Document" menu (see the reshape plan). Both keep their own internal structure completely
// unchanged - Encoding's item array is reused as-is from menu_data_encoding.h.
//
// "slot.language" is a DynamicSlot: Language isn't a static MenuItemDef table (it's generated at
// runtime from wxnLangTable via buildLanguageMenu(), same as before Phase B - see
// menu_data_language.h's own header comment for why), so menu_builder.h's buildWxnMainMenu()
// resolves this slot immediately after building this menu, exactly like it already did when
// Language was a top-level entry.

// Compare (side-by-side diff) and Spell Check live here (moved from the View menu) - both act on the
// current document's content. The mnemonic-path ids ("view.compare.*" / "view.spell.*") are kept
// unchanged so any saved shortcut bindings survive the move. The Dictionary radio list
// (myID_SPELL_DICT_BASE + i) is appended at runtime by rebuildSpellDictMenu() after these static items,
// which finds the submenu by myID_SPELLCHECK regardless of its parent menu.
static const MenuItemDef kDocumentCompareItems[] = {
    { MenuItemKind::Normal, myID_CMP_FILE,  &Label::DocumentCompareFile,      "view.compare.file" },
    { MenuItemKind::Normal, myID_CMP_CLIP,  &Label::DocumentCompareClipboard, "view.compare.clipboard" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, myID_CMP_NEXT,  &Label::DocumentCompareNext,      "view.compare.next" },
    { MenuItemKind::Normal, myID_CMP_PREV,  &Label::DocumentComparePrev,      "view.compare.prev" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, myID_CMP_CLEAR, &Label::DocumentCompareClear,     "view.compare.clear" },
};
static const MenuItemDef kDocumentSpellItems[] = {
    { MenuItemKind::Check,  myID_SPELLCHECK,         &Label::DocumentSpellEnable,       "view.spell.enable" },
    { MenuItemKind::Check,  myID_SPELL_COMMENTSONLY, &Label::DocumentSpellCommentsOnly, "view.spell.commentsOnly" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, myID_SPELL_MANAGE,       &Label::DocumentSpellManage,       "view.spell.manage" },
    { MenuItemKind::Separator },
};

static const MenuItemDef kDocumentMenuItems[] = {
    { MenuItemKind::DynamicSlot, 0, nullptr, "slot.language" },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::MenuEncoding, "menu.encoding",
      kEncodingMenuItems, WXSIZEOF(kEncodingMenuItems) },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::DocumentCompare, "view.compare",
      kDocumentCompareItems, WXSIZEOF(kDocumentCompareItems) },
    { MenuItemKind::Submenu, 0, &Label::DocumentSpellCheck, "view.spellCheck",
      kDocumentSpellItems, WXSIZEOF(kDocumentSpellItems) },
};

static const MenuDef kDocumentMenu = { "menu.document", &Label::MenuDocument, kDocumentMenuItems, WXSIZEOF(kDocumentMenuItems) };
