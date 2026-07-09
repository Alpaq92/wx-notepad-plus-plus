#pragma once
#include "menu_model.h"
#include "menu_labels_document.h"
#include "menu_data_encoding.h"   // reuses kEncodingMenuItems verbatim (Encoding nests one level deeper here)
#include "menuCmdID.h"

// ----------------------------------------------------------------- Document
// Phase B reshape: Language and Encoding, formerly two top-level menus, now live as submenus of
// one "Document" menu (see the reshape plan). Both keep their own internal structure completely
// unchanged - Encoding's item array is reused as-is from menu_data_encoding.h.
//
// "slot.language" is a DynamicSlot: Language isn't a static MenuItemDef table (it's generated at
// runtime from nppLangTable via buildLanguageMenu(), same as before Phase B - see
// menu_data_language.h's own header comment for why), so menu_builder.h's buildNppMainMenu()
// resolves this slot immediately after building this menu, exactly like it already did when
// Language was a top-level entry.

static const MenuItemDef kDocumentMenuItems[] = {
    { MenuItemKind::DynamicSlot, 0, nullptr, "slot.language" },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::MenuEncoding, "menu.encoding",
      kEncodingMenuItems, WXSIZEOF(kEncodingMenuItems) },
};

static const MenuDef kDocumentMenu = { "menu.document", &Label::MenuDocument, kDocumentMenuItems, WXSIZEOF(kDocumentMenuItems) };
