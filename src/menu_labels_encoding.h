#pragma once
#include <wx/intl.h>

namespace Label
{
    inline const wxString MenuEncoding() { return _("E&ncoding"); }

    inline const wxString EncAnsi() { return _("ANSI"); }
    inline const wxString EncAsUtf8() { return _("UTF-8"); }
    inline const wxString EncUtf8Bom() { return _("UTF-8-BOM"); }
    inline const wxString EncUtf16BeBom() { return _("UTF-16 BE BOM"); }
    inline const wxString EncUtf16LeBom() { return _("UTF-16 LE BOM"); }

    inline const wxString EncCharsets() { return _("Character sets"); }

    // ---- Arabic
    inline const wxString EncCharsetsArabic() { return _("Arabic"); }
    inline const wxString EncCharsetIso88596() { return _("ISO 8859-6"); }
    inline const wxString EncCharsetOem720() { return _("OEM 720"); }
    inline const wxString EncCharsetWin1256() { return _("Windows-1256"); }

    // ---- Baltic
    inline const wxString EncCharsetsBaltic() { return _("Baltic"); }
    inline const wxString EncCharsetIso88594() { return _("ISO 8859-4"); }
    inline const wxString EncCharsetIso885913() { return _("ISO 8859-13"); }
    inline const wxString EncCharsetOem775() { return _("OEM 775"); }
    inline const wxString EncCharsetWin1257() { return _("Windows-1257"); }

    // ---- Celtic
    inline const wxString EncCharsetsCeltic() { return _("Celtic"); }
    inline const wxString EncCharsetIso885914() { return _("ISO 8859-14"); }

    // ---- Cyrillic
    inline const wxString EncCharsetsCyrillic() { return _("Cyrillic"); }
    inline const wxString EncCharsetIso88595() { return _("ISO 8859-5"); }
    inline const wxString EncCharsetKoi8r() { return _("KOI8-R"); }
    inline const wxString EncCharsetKoi8u() { return _("KOI8-U"); }
    inline const wxString EncCharsetMacCyrillic() { return _("Macintosh"); }
    inline const wxString EncCharsetOem855() { return _("OEM 855"); }
    inline const wxString EncCharsetOem866() { return _("OEM 866"); }
    inline const wxString EncCharsetWin1251() { return _("Windows-1251"); }

    // ---- Central European
    inline const wxString EncCharsetsCentralEuropean() { return _("Central European"); }
    inline const wxString EncCharsetOem852() { return _("OEM 852"); }
    inline const wxString EncCharsetWin1250() { return _("Windows-1250"); }

    // ---- Chinese
    inline const wxString EncCharsetsChinese() { return _("Chinese"); }
    inline const wxString EncCharsetBig5() { return _("Big5 (Traditional)"); }
    inline const wxString EncCharsetGb2312() { return _("GB2312 (Simplified)"); }

    // ---- Eastern European
    inline const wxString EncCharsetsEasternEuropean() { return _("Eastern European"); }
    inline const wxString EncCharsetIso88592() { return _("ISO 8859-2"); }

    // ---- Greek
    inline const wxString EncCharsetsGreek() { return _("Greek"); }
    inline const wxString EncCharsetIso88597() { return _("ISO 8859-7"); }
    inline const wxString EncCharsetOem737() { return _("OEM 737"); }
    inline const wxString EncCharsetOem869() { return _("OEM 869"); }
    inline const wxString EncCharsetWin1253() { return _("Windows-1253"); }

    // ---- Hebrew
    inline const wxString EncCharsetsHebrew() { return _("Hebrew"); }
    inline const wxString EncCharsetIso88598() { return _("ISO 8859-8"); }
    inline const wxString EncCharsetOem862() { return _("OEM 862"); }
    inline const wxString EncCharsetWin1255() { return _("Windows-1255"); }

    // ---- Japanese
    inline const wxString EncCharsetsJapanese() { return _("Japanese"); }
    inline const wxString EncCharsetShiftJis() { return _("Shift-JIS"); }

    // ---- Korean
    inline const wxString EncCharsetsKorean() { return _("Korean"); }
    inline const wxString EncCharsetWin949() { return _("Windows 949"); }
    inline const wxString EncCharsetEucKr() { return _("EUC-KR"); }

    // ---- North European
    inline const wxString EncCharsetsNorthEuropean() { return _("North European"); }
    inline const wxString EncCharsetOem861() { return _("OEM 861 : Icelandic"); }
    inline const wxString EncCharsetOem865() { return _("OEM 865 : Nordic"); }

    // ---- Thai
    inline const wxString EncCharsetsThai() { return _("Thai"); }
    inline const wxString EncCharsetTis620() { return _("TIS-620"); }

    // ---- Turkish
    inline const wxString EncCharsetsTurkish() { return _("Turkish"); }
    inline const wxString EncCharsetIso88593() { return _("ISO 8859-3"); }
    inline const wxString EncCharsetIso88599() { return _("ISO 8859-9"); }
    inline const wxString EncCharsetOem857() { return _("OEM 857"); }
    inline const wxString EncCharsetWin1254() { return _("Windows-1254"); }

    // ---- Western European
    inline const wxString EncCharsetsWesternEuropean() { return _("Western European"); }
    inline const wxString EncCharsetIso88591() { return _("ISO 8859-1"); }
    inline const wxString EncCharsetIso885915() { return _("ISO 8859-15"); }
    inline const wxString EncCharsetOem850() { return _("OEM 850"); }
    inline const wxString EncCharsetOem858() { return _("OEM 858"); }
    inline const wxString EncCharsetOem860() { return _("OEM 860 : Portuguese"); }
    inline const wxString EncCharsetOem863() { return _("OEM 863 : French"); }
    inline const wxString EncCharsetOemUs() { return _("OEM-US"); }
    inline const wxString EncCharsetWin1252() { return _("Windows-1252"); }

    // ---- Vietnamese
    inline const wxString EncCharsetsVietnamese() { return _("Vietnamese"); }
    inline const wxString EncCharsetWin1258() { return _("Windows-1258"); }

    // ---- Convert to...
    inline const wxString EncConvertToAnsi() { return _("Convert to ANSI"); }
    inline const wxString EncConvertToAsUtf8() { return _("Convert to UTF-8"); }
    inline const wxString EncConvertToUtf8Bom() { return _("Convert to UTF-8-BOM"); }
    inline const wxString EncConvertToUtf16BeBom() { return _("Convert to UTF-16 BE BOM"); }
    inline const wxString EncConvertToUtf16LeBom() { return _("Convert to UTF-16 LE BOM"); }
}
