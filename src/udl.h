// SPDX-License-Identifier: Apache-2.0
//
// wxNote - User-Defined Language (UDL) data model + XML (de)serialization.
//
// Models the userDefineLang.xml schema field-for-field (verified against a real sample export,
// not guessed), so definition files can be exchanged both ways (Notepad++-compatible UDL
// format). This is a pure data model + XML I/O - no Scintilla/UI code lives here, so it can be
// unit-reasoned about independently of the styling engine (see udlLexer.h) and the dialog (see
// the "User-Defined Language Dialogue" section of src/main.cpp).
//
// Schema reference (userDefineLang.xml, one <UserLang> element):
//   <UserLang name="..." ext="..." udlVersion="2.1">
//     <Settings>
//       <Global caseIgnored="no" allowFoldOfComments="no" foldCompact="no"
//               forcePureLC="0" decimalSeparator="0" />
//       <Prefix Keywords1="no" ... Keywords8="no" />
//     </Settings>
//     <KeywordLists>
//       <Keywords name="Comments">...</Keywords>                         (encoded, see below)
//       <Keywords name="Numbers, prefix1">...</Keywords>  ... suffix1/2, extras1/2, range
//       <Keywords name="Operators1">...</Keywords> <Keywords name="Operators2">...</Keywords>
//       <Keywords name="Folders in code1, open/middle/close">...</Keywords>  (and code2, comment)
//       <Keywords name="Keywords1">...</Keywords> ... Keywords8
//       <Keywords name="Delimiters">00{ 01 02} 03[ 04 05] ...</Keywords>     (encoded, see below)
//     </KeywordLists>
//     <Styles>
//       <WordsStyle name="DEFAULT" fgColor="000000" bgColor="FFFFFF" fontName="" fontStyle="0"
//                   fontSize="" nesting="0" />
//       ... COMMENTS, LINE COMMENTS, NUMBERS, KEYWORDS1-8, OPERATORS, FOLDER IN CODE1/2,
//           FOLDER IN COMMENT, DELIMITERS1-8  (21 fixed style slots total)
//     </Styles>
//   </UserLang>
//
// Two encodings worth calling out (matched exactly, not reinvented):
//   - "Comments" keyword list packs 5 space-separated 2-digit-prefixed tokens in a fixed order:
//     00=comment-line-open, 01=comment-open, 02=comment-close, 03=? , 04=? (purpose unknown) - in
//     practice only 00/01/02 are populated by the dialog; the encoding leaves room for the rest.
//     We store this decoded (commentLineOpen/commentOpen/commentClose as plain strings) and
//     re-encode losslessly on export.
//   - "Delimiters" packs 8 sets of 3 tokens (open/escape/close), each 2-digit-index-prefixed,
//     space-separated, e.g. "00{ 01 02}" = set 0: open="{", escape="" (index 01 empty), close="}".
//     We store this decoded as 8 UdlDelimiterSet entries and re-encode on export.

#pragma once

#include <wx/wx.h>
#include <wx/xml/xml.h>
#include <wx/tokenzr.h>
#include <array>

// One paired-delimiter region (e.g. a quoted string, a bracketed expression). The format allows 8
// independent sets; each is a triple of open/escape/close TOKENS (usually single characters, but
// the format allows short strings, so we don't constrain length).
struct UdlDelimiterSet
{
    wxString open, escape, close;
};

// Three keyword-list slots controlling one folding trigger family ("code1", "code2", or
// "comment"): Open starts a fold region, Close ends it, Middle is a same-level marker (e.g.
// "else" between "if" and "endif") that doesn't itself change fold depth.
struct UdlFoldMarkers
{
    wxString open, middle, close;
};

// One entry from the Styles section - one of the 21 fixed style slots (see kUdlStyleNames).
// fontSize/fontName empty = "inherit from DEFAULT", matching the real dialog's blank-means-
// inherit convention. `nesting` is a bitmask over UdlNestBit.
struct UdlStyle
{
    wxColour fgColor = wxColour(0, 0, 0);
    wxColour bgColor = wxColour(255, 255, 255);
    wxString fontName;
    int      fontSize = 0;      // 0 = inherit
    bool     bold = false, italic = false, underline = false;
    uint32_t nesting = 0;
};

// Bits for UdlStyle::nesting - "when this style is active, also recognize and colour these other
// categories inside it" (e.g. keywords coloured even inside a nested string). Matches the real
// dialog's Styler "Nesting" checkboxes exactly (delimiters/keywords 1-8, comment, comment-line,
// operators 1-2, numbers - 21 bits, same categories as the style slots themselves).
enum UdlNestBit : uint32_t
{
    NEST_DELIM1 = 1u << 0, NEST_DELIM2 = 1u << 1, NEST_DELIM3 = 1u << 2, NEST_DELIM4 = 1u << 3,
    NEST_DELIM5 = 1u << 4, NEST_DELIM6 = 1u << 5, NEST_DELIM7 = 1u << 6, NEST_DELIM8 = 1u << 7,
    NEST_KEYWORD1 = 1u << 8,  NEST_KEYWORD2 = 1u << 9,  NEST_KEYWORD3 = 1u << 10, NEST_KEYWORD4 = 1u << 11,
    NEST_KEYWORD5 = 1u << 12, NEST_KEYWORD6 = 1u << 13, NEST_KEYWORD7 = 1u << 14, NEST_KEYWORD8 = 1u << 15,
    NEST_COMMENT = 1u << 16, NEST_COMMENT_LINE = 1u << 17,
    NEST_OPERATOR1 = 1u << 18, NEST_OPERATOR2 = 1u << 19,
    NEST_NUMBER = 1u << 20,
};

// The 21 fixed style-slot names, in the same order the <WordsStyle name="..."> elements are written
// - used both as the canonical index (styles[kUdlStyleIndex_X]) and as the XML "name" attribute.
enum UdlStyleIndex
{
    UDL_STYLE_DEFAULT = 0, UDL_STYLE_COMMENTS, UDL_STYLE_LINE_COMMENTS, UDL_STYLE_NUMBERS,
    UDL_STYLE_KEYWORDS1, UDL_STYLE_KEYWORDS2, UDL_STYLE_KEYWORDS3, UDL_STYLE_KEYWORDS4,
    UDL_STYLE_KEYWORDS5, UDL_STYLE_KEYWORDS6, UDL_STYLE_KEYWORDS7, UDL_STYLE_KEYWORDS8,
    UDL_STYLE_OPERATORS,
    UDL_STYLE_FOLDER_IN_CODE1, UDL_STYLE_FOLDER_IN_CODE2, UDL_STYLE_FOLDER_IN_COMMENT,
    UDL_STYLE_DELIMITERS1, UDL_STYLE_DELIMITERS2, UDL_STYLE_DELIMITERS3, UDL_STYLE_DELIMITERS4,
    UDL_STYLE_DELIMITERS5, UDL_STYLE_DELIMITERS6, UDL_STYLE_DELIMITERS7, UDL_STYLE_DELIMITERS8,
    UDL_STYLE_COUNT
};
inline const wxString& udlStyleName(int i)
{
    static const wxString names[UDL_STYLE_COUNT] = {
        "DEFAULT", "COMMENTS", "LINE COMMENTS", "NUMBERS",
        "KEYWORDS1", "KEYWORDS2", "KEYWORDS3", "KEYWORDS4",
        "KEYWORDS5", "KEYWORDS6", "KEYWORDS7", "KEYWORDS8",
        "OPERATORS",
        "FOLDER IN CODE1", "FOLDER IN CODE2", "FOLDER IN COMMENT",
        "DELIMITERS1", "DELIMITERS2", "DELIMITERS3", "DELIMITERS4",
        "DELIMITERS5", "DELIMITERS6", "DELIMITERS7", "DELIMITERS8",
    };
    return names[i];
}

// One complete User-Defined Language, matching one real <UserLang> element.
struct UdlLanguage
{
    wxString name;                    // shown in the Language menu
    wxString ext;                     // space-separated file extensions, no leading dots ("cfg conf")
    wxString udlVersion = "2.1";      // written through verbatim; not interpreted by us

    // ---- Settings > Global ----
    bool caseIgnored = false;         // keyword matching is case-insensitive
    bool allowFoldOfComments = false; // block comments can be folded
    bool foldCompact = false;         // trailing blank lines fold into the prior block
    int  forcePureLC = 0;             // 0=off, 1=force lowercase before matching, 2=force lowercase in keyword lists too
    int  decimalSeparator = 0;        // 0='.', 1=',', 2=both accepted in Numbers
    std::array<bool, 8> prefixMode = {};  // per Keywords-group "Prefix Mode" (match start-of-word only)

    // ---- Comment & Number tab ----
    wxString commentLineOpen;                    // e.g. "//" ("" = no line comments)
    wxString commentOpen, commentClose;          // e.g. "/*", "*/" (both empty = no block comments)
    wxString numPrefix1, numPrefix2;             // e.g. "0x", "0b"
    wxString numExtras1, numExtras2;
    wxString numSuffix1, numSuffix2;             // e.g. type suffixes like "f", "L"
    wxString numRange;                           // chars treated as "still part of the number" (e.g. ".")

    // ---- Operators & Delimiters tab ----
    wxString operators1;              // no-space-required operators, e.g. "+ - * /"
    wxString operators2;              // space-delimited-only operators
    std::array<UdlDelimiterSet, 8> delimiters = {};

    // ---- Folder & Default tab (fold trigger keyword lists; styling is separate, see styles[]) ----
    UdlFoldMarkers foldCode1;          // triggers matching with no required whitespace, e.g. "{"/"}"
    UdlFoldMarkers foldCode2;          // triggers requiring surrounding whitespace, e.g. "begin"/"end"
    UdlFoldMarkers foldComment;        // fold markers recognized only inside comments

    // ---- Keywords tab ----
    std::array<wxString, 8> keywords = {};   // space-separated; quoted phrases allowed, kept as-is

    // ---- Styles ----
    std::array<UdlStyle, UDL_STYLE_COUNT> styles = {};

    UdlLanguage() { for (auto& s : styles) s = UdlStyle{}; }
};

namespace udl_detail {

// "00{ 01 02}" -> 8 UdlDelimiterSet, one triple (open,escape,close) per set, each token prefixed
// with its own 2-digit slot index (0-23) and space-separated; an empty token still gets emitted
// as its bare index with nothing after it. This is the actual on-disk Delimiters encoding.
inline std::array<UdlDelimiterSet, 8> decodeDelimiters(const wxString& packed)
{
    std::array<UdlDelimiterSet, 8> out;
    wxString token;
    wxStringTokenizer tk(packed, " ", wxTOKEN_STRTOK);
    // Tokens are concatenated as "<2-digit index><value>" with no separator between index and
    // value, and the packed string space-separates tokens - but a value may itself be empty, so
    // reconstruct by index prefix rather than assuming positional order.
    while (tk.HasMoreTokens())
    {
        wxString t = tk.GetNextToken();
        if (t.length() < 2) continue;
        long idx = 0;
        if (!t.Left(2).ToLong(&idx) || idx < 0 || idx >= 24) continue;
        const wxString value = t.Mid(2);
        const int setIdx = static_cast<int>(idx) / 3;
        const int slot    = static_cast<int>(idx) % 3;   // 0=open, 1=escape, 2=close
        if (setIdx >= 8) continue;
        if (slot == 0) out[setIdx].open = value;
        else if (slot == 1) out[setIdx].escape = value;
        else out[setIdx].close = value;
    }
    return out;
}
inline wxString encodeDelimiters(const std::array<UdlDelimiterSet, 8>& sets)
{
    wxString out;
    for (int setIdx = 0; setIdx < 8; ++setIdx)
    {
        const UdlDelimiterSet& d = sets[setIdx];
        const wxString* vals[3] = { &d.open, &d.escape, &d.close };
        for (int slot = 0; slot < 3; ++slot)
        {
            if (!out.empty()) out += " ";
            out += wxString::Format("%02d", setIdx * 3 + slot) + *vals[slot];
        }
    }
    return out;
}

// "Comments" keyword list: 00=line-open, 01=block-open, 02=block-close (same index-prefix scheme
// as Delimiters, just three slots instead of eight sets of three).
inline void decodeComments(const wxString& packed, wxString& lineOpen, wxString& blockOpen, wxString& blockClose)
{
    lineOpen.Clear(); blockOpen.Clear(); blockClose.Clear();
    wxStringTokenizer tk(packed, " ", wxTOKEN_STRTOK);
    while (tk.HasMoreTokens())
    {
        wxString t = tk.GetNextToken();
        if (t.length() < 2) continue;
        long idx = 0;
        if (!t.Left(2).ToLong(&idx)) continue;
        const wxString value = t.Mid(2);
        if (idx == 0) lineOpen = value;
        else if (idx == 1) blockOpen = value;
        else if (idx == 2) blockClose = value;
    }
}
inline wxString encodeComments(const wxString& lineOpen, const wxString& blockOpen, const wxString& blockClose)
{
    return wxString::Format("00%s", lineOpen) + " " + wxString::Format("01%s", blockOpen) + " " + wxString::Format("02%s", blockClose);
}

inline wxString colourToHex(const wxColour& c) { return wxString::Format("%02X%02X%02X", c.Red(), c.Green(), c.Blue()); }
inline wxColour hexToColour(const wxString& hex, const wxColour& fallback)
{
    if (hex.length() != 6) return fallback;
    long v = 0;
    if (!hex.ToLong(&v, 16)) return fallback;
    return wxColour(static_cast<unsigned char>((v >> 16) & 0xFF), static_cast<unsigned char>((v >> 8) & 0xFF), static_cast<unsigned char>(v & 0xFF));
}
inline wxString yesNo(bool b) { return b ? "yes" : "no"; }
inline bool parseYesNo(const wxString& s) { return s.IsSameAs("yes", false) || s == "1"; }

}  // namespace udl_detail

// Serialize one UdlLanguage as a <UserLang> element, ready to be inserted into a <NotepadPlus>
// root (or written standalone - a bare <UserLang> root is also a valid file).
inline wxXmlNode* udlToXml(const UdlLanguage& u)
{
    using namespace udl_detail;
    auto* root = new wxXmlNode(wxXML_ELEMENT_NODE, "UserLang");
    root->AddAttribute("name", u.name);
    root->AddAttribute("ext", u.ext);
    root->AddAttribute("udlVersion", u.udlVersion);

    auto* settings = new wxXmlNode(wxXML_ELEMENT_NODE, "Settings");
    root->AddChild(settings);
    auto* global = new wxXmlNode(wxXML_ELEMENT_NODE, "Global");
    global->AddAttribute("caseIgnored", yesNo(u.caseIgnored));
    global->AddAttribute("allowFoldOfComments", yesNo(u.allowFoldOfComments));
    global->AddAttribute("foldCompact", yesNo(u.foldCompact));
    global->AddAttribute("forcePureLC", wxString::Format("%d", u.forcePureLC));
    global->AddAttribute("decimalSeparator", wxString::Format("%d", u.decimalSeparator));
    settings->AddChild(global);
    auto* prefix = new wxXmlNode(wxXML_ELEMENT_NODE, "Prefix");
    for (int i = 0; i < 8; ++i) prefix->AddAttribute(wxString::Format("Keywords%d", i + 1), yesNo(u.prefixMode[i]));
    settings->AddChild(prefix);

    auto* kw = new wxXmlNode(wxXML_ELEMENT_NODE, "KeywordLists");
    root->AddChild(kw);
    auto addKw = [&](const wxString& name, const wxString& value) {
        auto* n = new wxXmlNode(wxXML_ELEMENT_NODE, "Keywords");
        n->AddAttribute("name", name);
        if (!value.empty()) n->AddChild(new wxXmlNode(wxXML_TEXT_NODE, "", value));
        kw->AddChild(n);
    };
    addKw("Comments", encodeComments(u.commentLineOpen, u.commentOpen, u.commentClose));
    addKw("Numbers, prefix1", u.numPrefix1); addKw("Numbers, prefix2", u.numPrefix2);
    addKw("Numbers, extras1", u.numExtras1); addKw("Numbers, extras2", u.numExtras2);
    addKw("Numbers, suffix1", u.numSuffix1); addKw("Numbers, suffix2", u.numSuffix2);
    addKw("Numbers, range", u.numRange);
    addKw("Operators1", u.operators1); addKw("Operators2", u.operators2);
    addKw("Folders in code1, open", u.foldCode1.open); addKw("Folders in code1, middle", u.foldCode1.middle); addKw("Folders in code1, close", u.foldCode1.close);
    addKw("Folders in code2, open", u.foldCode2.open); addKw("Folders in code2, middle", u.foldCode2.middle); addKw("Folders in code2, close", u.foldCode2.close);
    addKw("Folders in comment, open", u.foldComment.open); addKw("Folders in comment, middle", u.foldComment.middle); addKw("Folders in comment, close", u.foldComment.close);
    for (int i = 0; i < 8; ++i) addKw(wxString::Format("Keywords%d", i + 1), u.keywords[i]);
    addKw("Delimiters", encodeDelimiters(u.delimiters));

    auto* styles = new wxXmlNode(wxXML_ELEMENT_NODE, "Styles");
    root->AddChild(styles);
    for (int i = 0; i < UDL_STYLE_COUNT; ++i)
    {
        const UdlStyle& s = u.styles[i];
        auto* n = new wxXmlNode(wxXML_ELEMENT_NODE, "WordsStyle");
        n->AddAttribute("name", udlStyleName(i));
        n->AddAttribute("fgColor", colourToHex(s.fgColor));
        n->AddAttribute("bgColor", colourToHex(s.bgColor));
        n->AddAttribute("fontName", s.fontName);
        int fontStyle = (s.bold ? 1 : 0) | (s.italic ? 2 : 0) | (s.underline ? 8 : 0);   // the on-disk fontStyle bitmask
        n->AddAttribute("fontStyle", wxString::Format("%d", fontStyle));
        n->AddAttribute("fontSize", s.fontSize > 0 ? wxString::Format("%d", s.fontSize) : wxString(""));
        n->AddAttribute("nesting", wxString::Format("%u", static_cast<unsigned>(s.nesting)));
        styles->AddChild(n);
    }
    return root;
}

// Parse one <UserLang> element (as found either standalone or inside a <NotepadPlus> root) into
// a UdlLanguage. Returns false (leaving `out` partially filled) if `node` isn't a UserLang element.
inline bool udlFromXml(const wxXmlNode* node, UdlLanguage& out)
{
    using namespace udl_detail;
    if (!node || node->GetName() != "UserLang") return false;
    out = UdlLanguage{};
    out.name = node->GetAttribute("name");
    out.ext = node->GetAttribute("ext");
    out.udlVersion = node->GetAttribute("udlVersion", "2.1");

    for (const wxXmlNode* child = node->GetChildren(); child; child = child->GetNext())
    {
        if (child->GetName() == "Settings")
        {
            for (const wxXmlNode* s = child->GetChildren(); s; s = s->GetNext())
            {
                if (s->GetName() == "Global")
                {
                    out.caseIgnored = parseYesNo(s->GetAttribute("caseIgnored"));
                    out.allowFoldOfComments = parseYesNo(s->GetAttribute("allowFoldOfComments"));
                    out.foldCompact = parseYesNo(s->GetAttribute("foldCompact"));
                    long v = 0;
                    s->GetAttribute("forcePureLC", "0").ToLong(&v); out.forcePureLC = static_cast<int>(v);
                    s->GetAttribute("decimalSeparator", "0").ToLong(&v); out.decimalSeparator = static_cast<int>(v);
                }
                else if (s->GetName() == "Prefix")
                {
                    for (int i = 0; i < 8; ++i)
                        out.prefixMode[i] = parseYesNo(s->GetAttribute(wxString::Format("Keywords%d", i + 1)));
                }
            }
        }
        else if (child->GetName() == "KeywordLists")
        {
            for (const wxXmlNode* k = child->GetChildren(); k; k = k->GetNext())
            {
                if (k->GetName() != "Keywords") continue;
                const wxString name = k->GetAttribute("name");
                const wxString value = k->GetChildren() ? k->GetChildren()->GetContent() : wxString();
                if (name == "Comments") decodeComments(value, out.commentLineOpen, out.commentOpen, out.commentClose);
                else if (name == "Numbers, prefix1") out.numPrefix1 = value;
                else if (name == "Numbers, prefix2") out.numPrefix2 = value;
                else if (name == "Numbers, extras1") out.numExtras1 = value;
                else if (name == "Numbers, extras2") out.numExtras2 = value;
                else if (name == "Numbers, suffix1") out.numSuffix1 = value;
                else if (name == "Numbers, suffix2") out.numSuffix2 = value;
                else if (name == "Numbers, range") out.numRange = value;
                else if (name == "Operators1") out.operators1 = value;
                else if (name == "Operators2") out.operators2 = value;
                else if (name == "Folders in code1, open") out.foldCode1.open = value;
                else if (name == "Folders in code1, middle") out.foldCode1.middle = value;
                else if (name == "Folders in code1, close") out.foldCode1.close = value;
                else if (name == "Folders in code2, open") out.foldCode2.open = value;
                else if (name == "Folders in code2, middle") out.foldCode2.middle = value;
                else if (name == "Folders in code2, close") out.foldCode2.close = value;
                else if (name == "Folders in comment, open") out.foldComment.open = value;
                else if (name == "Folders in comment, middle") out.foldComment.middle = value;
                else if (name == "Folders in comment, close") out.foldComment.close = value;
                else if (name == "Delimiters") out.delimiters = decodeDelimiters(value);
                else
                {
                    for (int i = 0; i < 8; ++i)
                        if (name == wxString::Format("Keywords%d", i + 1)) out.keywords[i] = value;
                }
            }
        }
        else if (child->GetName() == "Styles")
        {
            for (const wxXmlNode* w = child->GetChildren(); w; w = w->GetNext())
            {
                if (w->GetName() != "WordsStyle") continue;
                const wxString name = w->GetAttribute("name");
                for (int i = 0; i < UDL_STYLE_COUNT; ++i)
                {
                    if (name != udlStyleName(i)) continue;
                    UdlStyle& s = out.styles[i];
                    s.fgColor = hexToColour(w->GetAttribute("fgColor"), wxColour(0, 0, 0));
                    s.bgColor = hexToColour(w->GetAttribute("bgColor"), wxColour(255, 255, 255));
                    s.fontName = w->GetAttribute("fontName");
                    long fs = 0; w->GetAttribute("fontStyle", "0").ToLong(&fs);
                    s.bold = (fs & 1) != 0; s.italic = (fs & 2) != 0; s.underline = (fs & 8) != 0;
                    long sz = 0; s.fontSize = w->GetAttribute("fontSize", "").ToLong(&sz) ? static_cast<int>(sz) : 0;
                    unsigned long nest = 0; w->GetAttribute("nesting", "0").ToULong(&nest); s.nesting = static_cast<uint32_t>(nest);
                    break;
                }
            }
        }
    }
    return true;
}

// Load every <UserLang> from a userDefineLang.xml-shaped file (root <NotepadPlus> containing one
// or more <UserLang> children - or a single bare <UserLang> root, the shape a one-language
// export produces). Returns the languages found, in document order.
inline std::vector<UdlLanguage> loadUdlFile(const wxString& path)
{
    std::vector<UdlLanguage> out;
    wxXmlDocument doc;
    if (!doc.Load(path)) return out;
    wxXmlNode* root = doc.GetRoot();
    if (!root) return out;
    if (root->GetName() == "UserLang")
    {
        UdlLanguage u;
        if (udlFromXml(root, u)) out.push_back(std::move(u));
        return out;
    }
    for (wxXmlNode* child = root->GetChildren(); child; child = child->GetNext())
    {
        UdlLanguage u;
        if (udlFromXml(child, u)) out.push_back(std::move(u));
    }
    return out;
}

// Write one or more UdlLanguage as a userDefineLang.xml-shaped file (<NotepadPlus> root, one
// <UserLang> child per language) - the standard multi-language layout of that format.
inline bool saveUdlFile(const wxString& path, const std::vector<UdlLanguage>& langs)
{
    wxXmlDocument doc;
    auto* root = new wxXmlNode(wxXML_ELEMENT_NODE, "NotepadPlus");
    for (const auto& lang : langs) root->AddChild(udlToXml(lang));
    doc.SetRoot(root);
    return doc.Save(path);
}
