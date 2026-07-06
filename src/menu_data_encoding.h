#pragma once
#include "menu_model.h"
#include "menu_labels_encoding.h"
#include "menuCmdID.h"

// ------------------------------------------------------------- Encoding
// Mechanical, zero-behavior-change port of the Encoding menu from the old
// inline buildNppMainMenu() (see src/npp_menu.h). Same items, same order,
// same IDM_* ids, same labels, same nesting (including the 3-level-deep
// "Character sets" tree: Encoding > Character sets > [15 regions] > [codepages]).

// ---- Character sets > region codepage leaves (innermost arrays first)

static const MenuItemDef kEncCharsetArabicItems[] = {
    { MenuItemKind::Normal, IDM_FORMAT_ISO_8859_6, &Label::EncCharsetIso88596, "encoding.charsets.arabic.iso88596" },
    { MenuItemKind::Normal, IDM_FORMAT_DOS_720,    &Label::EncCharsetOem720,   "encoding.charsets.arabic.oem720" },
    { MenuItemKind::Normal, IDM_FORMAT_WIN_1256,   &Label::EncCharsetWin1256,  "encoding.charsets.arabic.win1256" },
};

static const MenuItemDef kEncCharsetBalticItems[] = {
    { MenuItemKind::Normal, IDM_FORMAT_ISO_8859_4,  &Label::EncCharsetIso88594,  "encoding.charsets.baltic.iso88594" },
    { MenuItemKind::Normal, IDM_FORMAT_ISO_8859_13, &Label::EncCharsetIso885913, "encoding.charsets.baltic.iso885913" },
    { MenuItemKind::Normal, IDM_FORMAT_DOS_775,     &Label::EncCharsetOem775,    "encoding.charsets.baltic.oem775" },
    { MenuItemKind::Normal, IDM_FORMAT_WIN_1257,    &Label::EncCharsetWin1257,   "encoding.charsets.baltic.win1257" },
};

static const MenuItemDef kEncCharsetCelticItems[] = {
    { MenuItemKind::Normal, IDM_FORMAT_ISO_8859_14, &Label::EncCharsetIso885914, "encoding.charsets.celtic.iso885914" },
};

static const MenuItemDef kEncCharsetCyrillicItems[] = {
    { MenuItemKind::Normal, IDM_FORMAT_ISO_8859_5,     &Label::EncCharsetIso88595,    "encoding.charsets.cyrillic.iso88595" },
    { MenuItemKind::Normal, IDM_FORMAT_KOI8R_CYRILLIC, &Label::EncCharsetKoi8r,       "encoding.charsets.cyrillic.koi8r" },
    { MenuItemKind::Normal, IDM_FORMAT_KOI8U_CYRILLIC, &Label::EncCharsetKoi8u,       "encoding.charsets.cyrillic.koi8u" },
    { MenuItemKind::Normal, IDM_FORMAT_MAC_CYRILLIC,   &Label::EncCharsetMacCyrillic, "encoding.charsets.cyrillic.macintosh" },
    { MenuItemKind::Normal, IDM_FORMAT_DOS_855,        &Label::EncCharsetOem855,      "encoding.charsets.cyrillic.oem855" },
    { MenuItemKind::Normal, IDM_FORMAT_DOS_866,        &Label::EncCharsetOem866,      "encoding.charsets.cyrillic.oem866" },
    { MenuItemKind::Normal, IDM_FORMAT_WIN_1251,       &Label::EncCharsetWin1251,     "encoding.charsets.cyrillic.win1251" },
};

static const MenuItemDef kEncCharsetCentralEuropeanItems[] = {
    { MenuItemKind::Normal, IDM_FORMAT_DOS_852,  &Label::EncCharsetOem852,  "encoding.charsets.centralEuropean.oem852" },
    { MenuItemKind::Normal, IDM_FORMAT_WIN_1250, &Label::EncCharsetWin1250, "encoding.charsets.centralEuropean.win1250" },
};

static const MenuItemDef kEncCharsetChineseItems[] = {
    { MenuItemKind::Normal, IDM_FORMAT_BIG5,    &Label::EncCharsetBig5,   "encoding.charsets.chinese.big5" },
    { MenuItemKind::Normal, IDM_FORMAT_GB2312,  &Label::EncCharsetGb2312, "encoding.charsets.chinese.gb2312" },
};

static const MenuItemDef kEncCharsetEasternEuropeanItems[] = {
    { MenuItemKind::Normal, IDM_FORMAT_ISO_8859_2, &Label::EncCharsetIso88592, "encoding.charsets.easternEuropean.iso88592" },
};

static const MenuItemDef kEncCharsetGreekItems[] = {
    { MenuItemKind::Normal, IDM_FORMAT_ISO_8859_7, &Label::EncCharsetIso88597, "encoding.charsets.greek.iso88597" },
    { MenuItemKind::Normal, IDM_FORMAT_DOS_737,    &Label::EncCharsetOem737,   "encoding.charsets.greek.oem737" },
    { MenuItemKind::Normal, IDM_FORMAT_DOS_869,    &Label::EncCharsetOem869,   "encoding.charsets.greek.oem869" },
    { MenuItemKind::Normal, IDM_FORMAT_WIN_1253,   &Label::EncCharsetWin1253,  "encoding.charsets.greek.win1253" },
};

static const MenuItemDef kEncCharsetHebrewItems[] = {
    { MenuItemKind::Normal, IDM_FORMAT_ISO_8859_8, &Label::EncCharsetIso88598, "encoding.charsets.hebrew.iso88598" },
    { MenuItemKind::Normal, IDM_FORMAT_DOS_862,    &Label::EncCharsetOem862,   "encoding.charsets.hebrew.oem862" },
    { MenuItemKind::Normal, IDM_FORMAT_WIN_1255,   &Label::EncCharsetWin1255,  "encoding.charsets.hebrew.win1255" },
};

static const MenuItemDef kEncCharsetJapaneseItems[] = {
    { MenuItemKind::Normal, IDM_FORMAT_SHIFT_JIS, &Label::EncCharsetShiftJis, "encoding.charsets.japanese.shiftJis" },
};

static const MenuItemDef kEncCharsetKoreanItems[] = {
    { MenuItemKind::Normal, IDM_FORMAT_KOREAN_WIN, &Label::EncCharsetWin949, "encoding.charsets.korean.win949" },
    { MenuItemKind::Normal, IDM_FORMAT_EUC_KR,     &Label::EncCharsetEucKr,  "encoding.charsets.korean.eucKr" },
};

static const MenuItemDef kEncCharsetNorthEuropeanItems[] = {
    { MenuItemKind::Normal, IDM_FORMAT_DOS_861, &Label::EncCharsetOem861, "encoding.charsets.northEuropean.oem861" },
    { MenuItemKind::Normal, IDM_FORMAT_DOS_865, &Label::EncCharsetOem865, "encoding.charsets.northEuropean.oem865" },
};

static const MenuItemDef kEncCharsetThaiItems[] = {
    { MenuItemKind::Normal, IDM_FORMAT_TIS_620, &Label::EncCharsetTis620, "encoding.charsets.thai.tis620" },
};

static const MenuItemDef kEncCharsetTurkishItems[] = {
    { MenuItemKind::Normal, IDM_FORMAT_ISO_8859_3, &Label::EncCharsetIso88593, "encoding.charsets.turkish.iso88593" },
    { MenuItemKind::Normal, IDM_FORMAT_ISO_8859_9, &Label::EncCharsetIso88599, "encoding.charsets.turkish.iso88599" },
    { MenuItemKind::Normal, IDM_FORMAT_DOS_857,    &Label::EncCharsetOem857,   "encoding.charsets.turkish.oem857" },
    { MenuItemKind::Normal, IDM_FORMAT_WIN_1254,   &Label::EncCharsetWin1254,  "encoding.charsets.turkish.win1254" },
};

static const MenuItemDef kEncCharsetWesternEuropeanItems[] = {
    { MenuItemKind::Normal, IDM_FORMAT_ISO_8859_1,  &Label::EncCharsetIso88591,  "encoding.charsets.westernEuropean.iso88591" },
    { MenuItemKind::Normal, IDM_FORMAT_ISO_8859_15, &Label::EncCharsetIso885915, "encoding.charsets.westernEuropean.iso885915" },
    { MenuItemKind::Normal, IDM_FORMAT_DOS_850,     &Label::EncCharsetOem850,    "encoding.charsets.westernEuropean.oem850" },
    { MenuItemKind::Normal, IDM_FORMAT_DOS_858,     &Label::EncCharsetOem858,    "encoding.charsets.westernEuropean.oem858" },
    { MenuItemKind::Normal, IDM_FORMAT_DOS_860,     &Label::EncCharsetOem860,    "encoding.charsets.westernEuropean.oem860" },
    { MenuItemKind::Normal, IDM_FORMAT_DOS_863,     &Label::EncCharsetOem863,    "encoding.charsets.westernEuropean.oem863" },
    { MenuItemKind::Normal, IDM_FORMAT_DOS_437,     &Label::EncCharsetOemUs,     "encoding.charsets.westernEuropean.oemUs" },
    { MenuItemKind::Normal, IDM_FORMAT_WIN_1252,    &Label::EncCharsetWin1252,   "encoding.charsets.westernEuropean.win1252" },
};

static const MenuItemDef kEncCharsetVietnameseItems[] = {
    { MenuItemKind::Normal, IDM_FORMAT_WIN_1258, &Label::EncCharsetWin1258, "encoding.charsets.vietnamese.win1258" },
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
    { MenuItemKind::Check,  IDM_FORMAT_ANSI,       &Label::EncAnsi,        "encoding.ansi" },
    { MenuItemKind::Check,  IDM_FORMAT_AS_UTF_8,   &Label::EncAsUtf8,      "encoding.asUtf8" },
    { MenuItemKind::Check,  IDM_FORMAT_UTF_8,      &Label::EncUtf8Bom,     "encoding.utf8Bom" },
    { MenuItemKind::Check,  IDM_FORMAT_UTF_16BE,   &Label::EncUtf16BeBom,  "encoding.utf16BeBom" },
    { MenuItemKind::Check,  IDM_FORMAT_UTF_16LE,   &Label::EncUtf16LeBom,  "encoding.utf16LeBom" },
    { MenuItemKind::Submenu, 0, &Label::EncCharsets, "encoding.charsets",
      kEncCharsetsItems, WXSIZEOF(kEncCharsetsItems) },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_FORMAT_CONV2_ANSI,      &Label::EncConvertToAnsi,        "encoding.convertToAnsi" },
    { MenuItemKind::Normal, IDM_FORMAT_CONV2_AS_UTF_8,  &Label::EncConvertToAsUtf8,      "encoding.convertToAsUtf8" },
    { MenuItemKind::Normal, IDM_FORMAT_CONV2_UTF_8,     &Label::EncConvertToUtf8Bom,     "encoding.convertToUtf8Bom" },
    { MenuItemKind::Normal, IDM_FORMAT_CONV2_UTF_16BE,  &Label::EncConvertToUtf16BeBom,  "encoding.convertToUtf16BeBom" },
    { MenuItemKind::Normal, IDM_FORMAT_CONV2_UTF_16LE,  &Label::EncConvertToUtf16LeBom,  "encoding.convertToUtf16LeBom" },
};

static const MenuDef kEncodingMenu = { "menu.encoding", &Label::MenuEncoding, kEncodingMenuItems, WXSIZEOF(kEncodingMenuItems) };
