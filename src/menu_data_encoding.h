#pragma once
#include "menu_model.h"
#include "menu_labels_encoding.h"
#include "command_ids.h"

// ------------------------------------------------------------- Encoding
// The Encoding menu: the four Unicode/interpretation modes up top, then a "Character sets"
// submenu grouping the legacy single-byte codepages by writing system / region (Arabic, Baltic,
// Celtic, Cyrillic, ...) — the standard charset taxonomy shared by Windows' own codepage lists
// and ICU, alphabetized by region name so a codepage is found by the script it encodes. The
// three-level "Encoding > Character sets > <region> > <codepage>" nesting keeps the ~50 legacy
// codepages off the top level where 90% of users never need them.

// ---- Character sets > region codepage leaves (innermost arrays first)

static const MenuItemDef kEncCharsetArabicItems[] = {
    { MenuItemKind::Normal, kCmdFormatIso88596, &Label::EncCharsetIso88596, "encoding.charsets.arabic.iso88596" },
    { MenuItemKind::Normal, kCmdFormatDos720,    &Label::EncCharsetOem720,   "encoding.charsets.arabic.oem720" },
    { MenuItemKind::Normal, kCmdFormatWin1256,   &Label::EncCharsetWin1256,  "encoding.charsets.arabic.win1256" },
};

static const MenuItemDef kEncCharsetBalticItems[] = {
    { MenuItemKind::Normal, kCmdFormatIso88594,  &Label::EncCharsetIso88594,  "encoding.charsets.baltic.iso88594" },
    { MenuItemKind::Normal, kCmdFormatIso885913, &Label::EncCharsetIso885913, "encoding.charsets.baltic.iso885913" },
    { MenuItemKind::Normal, kCmdFormatDos775,     &Label::EncCharsetOem775,    "encoding.charsets.baltic.oem775" },
    { MenuItemKind::Normal, kCmdFormatWin1257,    &Label::EncCharsetWin1257,   "encoding.charsets.baltic.win1257" },
};

static const MenuItemDef kEncCharsetCelticItems[] = {
    { MenuItemKind::Normal, kCmdFormatIso885914, &Label::EncCharsetIso885914, "encoding.charsets.celtic.iso885914" },
};

static const MenuItemDef kEncCharsetCyrillicItems[] = {
    { MenuItemKind::Normal, kCmdFormatIso88595,     &Label::EncCharsetIso88595,    "encoding.charsets.cyrillic.iso88595" },
    { MenuItemKind::Normal, kCmdFormatKoi8rCyrillic, &Label::EncCharsetKoi8r,       "encoding.charsets.cyrillic.koi8r" },
    { MenuItemKind::Normal, kCmdFormatKoi8uCyrillic, &Label::EncCharsetKoi8u,       "encoding.charsets.cyrillic.koi8u" },
    { MenuItemKind::Normal, kCmdFormatMacCyrillic,   &Label::EncCharsetMacCyrillic, "encoding.charsets.cyrillic.macintosh" },
    { MenuItemKind::Normal, kCmdFormatDos855,        &Label::EncCharsetOem855,      "encoding.charsets.cyrillic.oem855" },
    { MenuItemKind::Normal, kCmdFormatDos866,        &Label::EncCharsetOem866,      "encoding.charsets.cyrillic.oem866" },
    { MenuItemKind::Normal, kCmdFormatWin1251,       &Label::EncCharsetWin1251,     "encoding.charsets.cyrillic.win1251" },
};

static const MenuItemDef kEncCharsetCentralEuropeanItems[] = {
    { MenuItemKind::Normal, kCmdFormatDos852,  &Label::EncCharsetOem852,  "encoding.charsets.centralEuropean.oem852" },
    { MenuItemKind::Normal, kCmdFormatWin1250, &Label::EncCharsetWin1250, "encoding.charsets.centralEuropean.win1250" },
};

static const MenuItemDef kEncCharsetChineseItems[] = {
    { MenuItemKind::Normal, kCmdFormatBig5,    &Label::EncCharsetBig5,   "encoding.charsets.chinese.big5" },
    { MenuItemKind::Normal, kCmdFormatGb2312,  &Label::EncCharsetGb2312, "encoding.charsets.chinese.gb2312" },
};

static const MenuItemDef kEncCharsetEasternEuropeanItems[] = {
    { MenuItemKind::Normal, kCmdFormatIso88592, &Label::EncCharsetIso88592, "encoding.charsets.easternEuropean.iso88592" },
};

static const MenuItemDef kEncCharsetGreekItems[] = {
    { MenuItemKind::Normal, kCmdFormatIso88597, &Label::EncCharsetIso88597, "encoding.charsets.greek.iso88597" },
    { MenuItemKind::Normal, kCmdFormatDos737,    &Label::EncCharsetOem737,   "encoding.charsets.greek.oem737" },
    { MenuItemKind::Normal, kCmdFormatDos869,    &Label::EncCharsetOem869,   "encoding.charsets.greek.oem869" },
    { MenuItemKind::Normal, kCmdFormatWin1253,   &Label::EncCharsetWin1253,  "encoding.charsets.greek.win1253" },
};

static const MenuItemDef kEncCharsetHebrewItems[] = {
    { MenuItemKind::Normal, kCmdFormatIso88598, &Label::EncCharsetIso88598, "encoding.charsets.hebrew.iso88598" },
    { MenuItemKind::Normal, kCmdFormatDos862,    &Label::EncCharsetOem862,   "encoding.charsets.hebrew.oem862" },
    { MenuItemKind::Normal, kCmdFormatWin1255,   &Label::EncCharsetWin1255,  "encoding.charsets.hebrew.win1255" },
};

static const MenuItemDef kEncCharsetJapaneseItems[] = {
    { MenuItemKind::Normal, kCmdFormatShiftJis, &Label::EncCharsetShiftJis, "encoding.charsets.japanese.shiftJis" },
};

static const MenuItemDef kEncCharsetKoreanItems[] = {
    { MenuItemKind::Normal, kCmdFormatKoreanWin, &Label::EncCharsetWin949, "encoding.charsets.korean.win949" },
    { MenuItemKind::Normal, kCmdFormatEucKr,     &Label::EncCharsetEucKr,  "encoding.charsets.korean.eucKr" },
};

static const MenuItemDef kEncCharsetNorthEuropeanItems[] = {
    { MenuItemKind::Normal, kCmdFormatDos861, &Label::EncCharsetOem861, "encoding.charsets.northEuropean.oem861" },
    { MenuItemKind::Normal, kCmdFormatDos865, &Label::EncCharsetOem865, "encoding.charsets.northEuropean.oem865" },
};

static const MenuItemDef kEncCharsetThaiItems[] = {
    { MenuItemKind::Normal, kCmdFormatTis620, &Label::EncCharsetTis620, "encoding.charsets.thai.tis620" },
};

static const MenuItemDef kEncCharsetTurkishItems[] = {
    { MenuItemKind::Normal, kCmdFormatIso88593, &Label::EncCharsetIso88593, "encoding.charsets.turkish.iso88593" },
    { MenuItemKind::Normal, kCmdFormatIso88599, &Label::EncCharsetIso88599, "encoding.charsets.turkish.iso88599" },
    { MenuItemKind::Normal, kCmdFormatDos857,    &Label::EncCharsetOem857,   "encoding.charsets.turkish.oem857" },
    { MenuItemKind::Normal, kCmdFormatWin1254,   &Label::EncCharsetWin1254,  "encoding.charsets.turkish.win1254" },
};

static const MenuItemDef kEncCharsetWesternEuropeanItems[] = {
    { MenuItemKind::Normal, kCmdFormatIso88591,  &Label::EncCharsetIso88591,  "encoding.charsets.westernEuropean.iso88591" },
    { MenuItemKind::Normal, kCmdFormatIso885915, &Label::EncCharsetIso885915, "encoding.charsets.westernEuropean.iso885915" },
    { MenuItemKind::Normal, kCmdFormatDos850,     &Label::EncCharsetOem850,    "encoding.charsets.westernEuropean.oem850" },
    { MenuItemKind::Normal, kCmdFormatDos858,     &Label::EncCharsetOem858,    "encoding.charsets.westernEuropean.oem858" },
    { MenuItemKind::Normal, kCmdFormatDos860,     &Label::EncCharsetOem860,    "encoding.charsets.westernEuropean.oem860" },
    { MenuItemKind::Normal, kCmdFormatDos863,     &Label::EncCharsetOem863,    "encoding.charsets.westernEuropean.oem863" },
    { MenuItemKind::Normal, kCmdFormatDos437,     &Label::EncCharsetOemUs,     "encoding.charsets.westernEuropean.oemUs" },
    { MenuItemKind::Normal, kCmdFormatWin1252,    &Label::EncCharsetWin1252,   "encoding.charsets.westernEuropean.win1252" },
};

static const MenuItemDef kEncCharsetVietnameseItems[] = {
    { MenuItemKind::Normal, kCmdFormatWin1258, &Label::EncCharsetWin1258, "encoding.charsets.vietnamese.win1258" },
};

// ---- Character sets > 15 region submenus (middle level)

static const MenuItemDef kEncCharsetsItems[] = {
    { MenuItemKind::Submenu, 0, &Label::EncCharsetsArabic, "encoding.charsets.arabic",
      kEncCharsetArabicItems, WXSIZEOF(kEncCharsetArabicItems) },
    { MenuItemKind::Submenu, 0, &Label::EncCharsetsBaltic, "encoding.charsets.baltic",
      kEncCharsetBalticItems, WXSIZEOF(kEncCharsetBalticItems) },
    { MenuItemKind::Submenu, 0, &Label::EncCharsetsCeltic, "encoding.charsets.celtic",
      kEncCharsetCelticItems, WXSIZEOF(kEncCharsetCelticItems) },
    { MenuItemKind::Submenu, 0, &Label::EncCharsetsCyrillic, "encoding.charsets.cyrillic",
      kEncCharsetCyrillicItems, WXSIZEOF(kEncCharsetCyrillicItems) },
    { MenuItemKind::Submenu, 0, &Label::EncCharsetsCentralEuropean, "encoding.charsets.centralEuropean",
      kEncCharsetCentralEuropeanItems, WXSIZEOF(kEncCharsetCentralEuropeanItems) },
    { MenuItemKind::Submenu, 0, &Label::EncCharsetsChinese, "encoding.charsets.chinese",
      kEncCharsetChineseItems, WXSIZEOF(kEncCharsetChineseItems) },
    { MenuItemKind::Submenu, 0, &Label::EncCharsetsEasternEuropean, "encoding.charsets.easternEuropean",
      kEncCharsetEasternEuropeanItems, WXSIZEOF(kEncCharsetEasternEuropeanItems) },
    { MenuItemKind::Submenu, 0, &Label::EncCharsetsGreek, "encoding.charsets.greek",
      kEncCharsetGreekItems, WXSIZEOF(kEncCharsetGreekItems) },
    { MenuItemKind::Submenu, 0, &Label::EncCharsetsHebrew, "encoding.charsets.hebrew",
      kEncCharsetHebrewItems, WXSIZEOF(kEncCharsetHebrewItems) },
    { MenuItemKind::Submenu, 0, &Label::EncCharsetsJapanese, "encoding.charsets.japanese",
      kEncCharsetJapaneseItems, WXSIZEOF(kEncCharsetJapaneseItems) },
    { MenuItemKind::Submenu, 0, &Label::EncCharsetsKorean, "encoding.charsets.korean",
      kEncCharsetKoreanItems, WXSIZEOF(kEncCharsetKoreanItems) },
    { MenuItemKind::Submenu, 0, &Label::EncCharsetsNorthEuropean, "encoding.charsets.northEuropean",
      kEncCharsetNorthEuropeanItems, WXSIZEOF(kEncCharsetNorthEuropeanItems) },
    { MenuItemKind::Submenu, 0, &Label::EncCharsetsThai, "encoding.charsets.thai",
      kEncCharsetThaiItems, WXSIZEOF(kEncCharsetThaiItems) },
    { MenuItemKind::Submenu, 0, &Label::EncCharsetsTurkish, "encoding.charsets.turkish",
      kEncCharsetTurkishItems, WXSIZEOF(kEncCharsetTurkishItems) },
    { MenuItemKind::Submenu, 0, &Label::EncCharsetsWesternEuropean, "encoding.charsets.westernEuropean",
      kEncCharsetWesternEuropeanItems, WXSIZEOF(kEncCharsetWesternEuropeanItems) },
    { MenuItemKind::Submenu, 0, &Label::EncCharsetsVietnamese, "encoding.charsets.vietnamese",
      kEncCharsetVietnameseItems, WXSIZEOF(kEncCharsetVietnameseItems) },
};

// ---- Top-level Encoding menu

static const MenuItemDef kEncodingMenuItems[] = {
    { MenuItemKind::Check,  kCmdFormatAnsi,       &Label::EncAnsi,        "encoding.ansi" },
    { MenuItemKind::Check,  kCmdFormatAsUtf8,   &Label::EncAsUtf8,      "encoding.asUtf8" },
    { MenuItemKind::Check,  kCmdFormatUtf8,      &Label::EncUtf8Bom,     "encoding.utf8Bom" },
    { MenuItemKind::Check,  kCmdFormatUtf16be,   &Label::EncUtf16BeBom,  "encoding.utf16BeBom" },
    { MenuItemKind::Check,  kCmdFormatUtf16le,   &Label::EncUtf16LeBom,  "encoding.utf16LeBom" },
    { MenuItemKind::Submenu, 0, &Label::EncCharsets, "encoding.charsets",
      kEncCharsetsItems, WXSIZEOF(kEncCharsetsItems) },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, kCmdFormatConv2Ansi,      &Label::EncConvertToAnsi,        "encoding.convertToAnsi" },
    { MenuItemKind::Normal, kCmdFormatConv2AsUtf8,  &Label::EncConvertToAsUtf8,      "encoding.convertToAsUtf8" },
    { MenuItemKind::Normal, kCmdFormatConv2Utf8,     &Label::EncConvertToUtf8Bom,     "encoding.convertToUtf8Bom" },
    { MenuItemKind::Normal, kCmdFormatConv2Utf16be,  &Label::EncConvertToUtf16BeBom,  "encoding.convertToUtf16BeBom" },
    { MenuItemKind::Normal, kCmdFormatConv2Utf16le,  &Label::EncConvertToUtf16LeBom,  "encoding.convertToUtf16LeBom" },
};

static const MenuDef kEncodingMenu = { "menu.encoding", &Label::MenuEncoding, kEncodingMenuItems, WXSIZEOF(kEncodingMenuItems) };
