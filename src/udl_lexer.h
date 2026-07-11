// SPDX-License-Identifier: GPL-3.0-or-later
//
// wxNote - User-Defined Language: the live styling/folding engine.
//
// Applies a UdlLanguage (src/udl.h) to a wxStyledTextCtrl via Scintilla's container-lexing mode
// (wxSTC_LEX_CONTAINER): Scintilla fires wxEVT_STC_STYLENEEDED whenever it needs text styled
// beyond what's already been painted, and udlStyleRange()/udlFoldRange() below do the painting
// and fold-level computation by hand - there is no built-in Lexilla lexer for a user-defined
// language, so the app has to be the lexer for these buffers.
//
// Style numbers used on the Scintilla side are UdlStyleIndex values directly (0..UDL_STYLE_COUNT-1,
// well under Scintilla's 256-style ceiling and disjoint from the numbered-lexer style ranges used
// elsewhere, since a UDL buffer never has a Lexilla lexer active at the same time).

#pragma once

#include "udl.h"
#include <wx/stc/stc.h>
#include <vector>

namespace udl_detail {

// A single accent/operator/delimiter "word list" lookup: split a space-separated field into
// tokens once (per style/fold pass, not per character) for fast prefix/exact matching.
inline std::vector<wxString> splitWords(const wxString& packed)
{
    std::vector<wxString> out;
    wxStringTokenizer tk(packed, " \t\r\n", wxTOKEN_STRTOK);
    while (tk.HasMoreTokens()) out.push_back(tk.GetNextToken());
    return out;
}

// True if `text` starting at `pos` matches `token` exactly (case-sensitivity per caseIgnored).
inline bool matchAt(const wxString& text, int pos, const wxString& token, bool caseIgnored)
{
    if (token.empty() || pos + (int)token.length() > (int)text.length()) return false;
    for (size_t i = 0; i < token.length(); ++i)
    {
        wxUniChar a = text[pos + i], b = token[i];
        if (caseIgnored) { a = wxTolower(a); b = wxTolower(b); }
        if (a != b) return false;
    }
    return true;
}

// Longest-match lookup across a small word list (8 delimiter-open tokens, comment markers, etc.)
// - returns the matched token's length, or 0 if none match at this position.
inline int matchAnyAt(const wxString& text, int pos, const std::vector<wxString>& tokens, bool caseIgnored)
{
    int best = 0;
    for (const auto& t : tokens) if ((int)t.length() > best && matchAt(text, pos, t, caseIgnored)) best = (int)t.length();
    return best;
}

inline bool isWordStartChar(wxUniChar c) { return wxIsalpha(c) || c == '_'; }
inline bool isWordChar(wxUniChar c) { return wxIsalnum(c) || c == '_'; }
inline bool isDigitOrRange(wxUniChar c, const wxString& rangeChars)
{
    if (wxIsdigit(c)) return true;
    for (size_t i = 0; i < rangeChars.length(); ++i) if (rangeChars[i] == c) return true;
    return false;
}

// Precomputed, cheap-to-reuse view over a UdlLanguage's word lists, built once per styling pass
// (not per character) since splitWords()/tokenizing every keystroke would be wasteful.
struct UdlTables
{
    std::vector<wxString> keywords[8];
    std::vector<wxString> operators1, operators2;
    std::vector<wxString> delimOpen[8];
    explicit UdlTables(const UdlLanguage& lang)
    {
        for (int i = 0; i < 8; ++i) keywords[i] = splitWords(lang.keywords[i]);
        operators1 = splitWords(lang.operators1);
        operators2 = splitWords(lang.operators2);
        for (int i = 0; i < 8; ++i) if (!lang.delimiters[i].open.empty()) delimOpen[i].push_back(lang.delimiters[i].open);
    }
};

// Which keyword group (0-7) `word` belongs to, honouring per-group Prefix Mode (match if `word`
// STARTS WITH the keyword, rather than exact match) and the language's caseIgnored setting.
// Returns -1 if no group matches.
inline int classifyWord(const wxString& word, const UdlLanguage& lang, const UdlTables& t)
{
    for (int g = 0; g < 8; ++g)
    {
        for (const auto& kw : t.keywords[g])
        {
            if (lang.prefixMode[g])
            {
                if (matchAt(word, 0, kw, lang.caseIgnored)) return g;
            }
            else
            {
                const bool eq = lang.caseIgnored ? word.IsSameAs(kw, false) : (word == kw);
                if (eq) return g;
            }
        }
    }
    return -1;
}

}  // namespace udl_detail

// Style [startPos, endPos) of `stc`'s text according to `lang`. Called from the app's
// wxEVT_STC_STYLENEEDED handler with startPos = stc->GetEndStyled() (rounded back to a line
// start for safety, since some state - "are we in a block comment" - can only be recovered by
// re-scanning from a known-good boundary) and endPos = event.GetPosition().
//
// State carried across the scan: 0 = normal code, 1 = line comment, 2 = block comment,
// 10+n = inside delimiter set n (0-7)'s body, having seen its open token.
inline void udlStyleRange(wxStyledTextCtrl* stc, const UdlLanguage& lang, int startPos, int endPos)
{
    using namespace udl_detail;
    if (!stc || endPos <= startPos) return;
    const UdlTables t(lang);

    // Re-scan from the start of the current line (styling mid-line is unsafe - a token like a
    // keyword or number needs its full width visible to classify correctly). The STATE at that
    // line-start position, though, is NOT necessarily "normal code": a block comment or a
    // delimiter region can span multiple lines, and separate STYLENEEDED calls only cover the
    // newly-needed range each time - a naive "always start at state 0" loses "still inside a
    // block comment" across calls (this only wouldn't show up in a single one-shot Colourise(0,-1)
    // pass, where the whole document is one call and `state` survives as a loop-local variable -
    // it matters for real interactive editing, where Scintilla calls this repeatedly in small
    // chunks). Recover it instead from the style already painted on the character just before
    // this line - that byte was set by a previous, already-completed call, so it's trustworthy.
    const int line = stc->LineFromPosition(startPos);
    int pos = stc->PositionFromLine(line);
    const wxString text = stc->GetTextRange(pos, endPos);

    int state = 0;          // 0 normal, 1 line-comment, 2 block-comment, 10+n delimiter n
    if (pos > 0)
    {
        const int prevStyle = stc->GetStyleAt(pos - 1);
        if (prevStyle == UDL_STYLE_COMMENTS) state = 2;
        else if (prevStyle >= UDL_STYLE_DELIMITERS1 && prevStyle <= UDL_STYLE_DELIMITERS8)
            state = 10 + (prevStyle - UDL_STYLE_DELIMITERS1);
        // UDL_STYLE_LINE_COMMENTS never carries over: a line comment always ends at EOL, and
        // `pos` is itself a line start, so the previous line's line-comment (if any) already
        // closed before this line began.
    }
    stc->StartStyling(pos);
    int i = 0;
    const int n = (int)text.length();
    while (i < n)
    {
        if (state == 1)   // line comment: runs to end of line
        {
            int j = i;
            while (j < n && text[j] != '\n' && text[j] != '\r') ++j;
            stc->SetStyling(j - i, UDL_STYLE_LINE_COMMENTS);
            i = j;
            if (i < n) state = 0;   // newline itself styled as DEFAULT below, then back to code
            continue;
        }
        if (state == 2)   // block comment: runs until commentClose (or comment-fold markers don't break state)
        {
            if (!lang.commentClose.empty() && matchAt(text, i, lang.commentClose, false))
            {
                stc->SetStyling((int)lang.commentClose.length(), UDL_STYLE_COMMENTS);
                i += (int)lang.commentClose.length();
                state = 0;
                continue;
            }
            stc->SetStyling(1, UDL_STYLE_COMMENTS);
            ++i;
            continue;
        }
        if (state >= 10)   // inside a delimiter body
        {
            const int setIdx = state - 10;
            const UdlDelimiterSet& d = lang.delimiters[setIdx];
            if (!d.escape.empty() && matchAt(text, i, d.escape, false))
            {
                const int esc = (int)d.escape.length();
                int consume = esc + 1;   // the escape token plus (at least) the char it protects
                if (i + consume > n) consume = n - i;
                stc->SetStyling(consume, UDL_STYLE_DELIMITERS1 + setIdx);
                i += consume;
                continue;
            }
            if (!d.close.empty() && matchAt(text, i, d.close, false))
            {
                stc->SetStyling((int)d.close.length(), UDL_STYLE_DELIMITERS1 + setIdx);
                i += (int)d.close.length();
                state = 0;
                continue;
            }
            stc->SetStyling(1, UDL_STYLE_DELIMITERS1 + setIdx);
            ++i;
            continue;
        }

        // ---- normal code state: try each trigger, longest/most-specific first ----
        if (!lang.commentLineOpen.empty() && matchAt(text, i, lang.commentLineOpen, false))
        {
            stc->SetStyling((int)lang.commentLineOpen.length(), UDL_STYLE_LINE_COMMENTS);
            i += (int)lang.commentLineOpen.length();
            state = 1;
            continue;
        }
        if (!lang.commentOpen.empty() && matchAt(text, i, lang.commentOpen, false))
        {
            stc->SetStyling((int)lang.commentOpen.length(), UDL_STYLE_COMMENTS);
            i += (int)lang.commentOpen.length();
            state = 2;
            continue;
        }
        bool enteredDelim = false;
        for (int d = 0; d < 8; ++d)
        {
            const wxString& open = lang.delimiters[d].open;
            if (open.empty() || !matchAt(text, i, open, false)) continue;
            stc->SetStyling((int)open.length(), UDL_STYLE_DELIMITERS1 + d);
            i += (int)open.length();
            state = 10 + d;
            enteredDelim = true;
            break;
        }
        if (enteredDelim) continue;
        if (isWordStartChar(text[i]) || (wxIsdigit(text[i])))
        {
            const bool startsAsNumber = wxIsdigit(text[i]) && !isWordStartChar(text[i]);
            int j = i;
            if (startsAsNumber)
            {
                // Optional prefix (e.g. "0x"), digits/range chars, optional suffix/extras.
                int p1 = matchAnyAt(text, j, { lang.numPrefix1 }, false);
                int p2 = matchAnyAt(text, j, { lang.numPrefix2 }, false);
                j += wxMax(p1, p2);
                while (j < n && isDigitOrRange(text[j], lang.numRange)) ++j;
                for (const auto& suf : { lang.numSuffix1, lang.numSuffix2, lang.numExtras1, lang.numExtras2 })
                    if (!suf.empty() && matchAt(text, j, suf, false)) j += (int)suf.length();
                stc->SetStyling(j - i, UDL_STYLE_NUMBERS);
                i = j;
                continue;
            }
            while (j < n && isWordChar(text[j])) ++j;
            const wxString word = text.Mid(i, j - i);
            const int group = classifyWord(word, lang, t);
            stc->SetStyling(j - i, group >= 0 ? (UDL_STYLE_KEYWORDS1 + group) : UDL_STYLE_DEFAULT);
            i = j;
            continue;
        }
        {
            const int op2len = matchAnyAt(text, i, t.operators2, false);
            const int op1len = matchAnyAt(text, i, t.operators1, false);
            const int oplen = wxMax(op1len, op2len);
            if (oplen > 0) { stc->SetStyling(oplen, UDL_STYLE_OPERATORS); i += oplen; continue; }
        }
        stc->SetStyling(1, UDL_STYLE_DEFAULT);
        ++i;
    }
}

// Compute fold levels for [startLine, endLine] per lang's foldCode1/foldCode2/foldComment marker
// lists. Container lexers get no automatic folding - Scintilla just uses whatever SC_FOLDLEVEL*
// values the app last set via SCI_SETFOLDLEVEL, so this must run alongside styling.
inline void udlFoldRange(wxStyledTextCtrl* stc, const UdlLanguage& lang, int startLine, int endLine)
{
    using namespace udl_detail;
    if (!stc) return;
    const std::vector<wxString> code1Open = splitWords(lang.foldCode1.open), code1Mid = splitWords(lang.foldCode1.middle), code1Close = splitWords(lang.foldCode1.close);
    const std::vector<wxString> code2Open = splitWords(lang.foldCode2.open), code2Mid = splitWords(lang.foldCode2.middle), code2Close = splitWords(lang.foldCode2.close);
    const std::vector<wxString> cmtOpen = splitWords(lang.foldComment.open), cmtClose = splitWords(lang.foldComment.close);

    int depth = (startLine > 0) ? (stc->GetFoldLevel(startLine - 1) & SC_FOLDLEVELNUMBERMASK) - SC_FOLDLEVELBASE : 0;
    if (depth < 0) depth = 0;

    for (int line = startLine; line <= endLine && line < stc->GetLineCount(); ++line)
    {
        const wxString text = stc->GetLine(line);
        int opens = 0, closes = 0;
        for (int i = 0; i < (int)text.length();)
        {
            const int style = stc->GetStyleAt(stc->PositionFromLine(line) + i);
            const bool inComment = (style == UDL_STYLE_COMMENTS || style == UDL_STYLE_LINE_COMMENTS);
            const auto& openList  = inComment ? cmtOpen  : code1Open;
            const auto& closeList = inComment ? cmtClose : code1Close;
            int m = matchAnyAt(text, i, openList, false);
            if (!m) m = matchAnyAt(text, i, code2Open, false);
            if (m) { ++opens; i += m; continue; }
            m = matchAnyAt(text, i, closeList, false);
            if (!m) m = matchAnyAt(text, i, code2Close, false);
            if (m) { ++closes; i += m; continue; }
            ++i;
        }
        int lvl = SC_FOLDLEVELBASE + depth;
        if (closes > 0 && opens == 0) lvl = SC_FOLDLEVELBASE + wxMax(0, depth - 1);
        const int nextDepth = wxMax(0, depth + opens - closes);
        const bool isHeader = opens > closes;
        stc->SetFoldLevel(line, lvl | (isHeader ? SC_FOLDLEVELHEADERFLAG : 0));
        depth = nextDepth;
    }
}

// Push a UdlLanguage's Styles table into `stc`'s style definitions and switch it into container-
// lexing mode. Call once when a UDL becomes the active buffer's language; the caller is
// responsible for binding wxEVT_STC_STYLENEEDED to a handler that calls udlStyleRange()+
// udlFoldRange() (this lives in main.cpp since it needs to know which EditorPage/UdlLanguage
// is active, which this header intentionally has no knowledge of).
inline void udlApplyStyles(wxStyledTextCtrl* stc, const UdlLanguage& lang)
{
    if (!stc) return;
    stc->SetLexer(wxSTC_LEX_CONTAINER);
    for (int i = 0; i < UDL_STYLE_COUNT; ++i)
    {
        const UdlStyle& s = lang.styles[i];
        stc->StyleSetForeground(i, s.fgColor);
        stc->StyleSetBackground(i, s.bgColor);
        stc->StyleSetBold(i, s.bold);
        stc->StyleSetItalic(i, s.italic);
        stc->StyleSetUnderline(i, s.underline);
        if (!s.fontName.empty()) stc->StyleSetFaceName(i, s.fontName);
        if (s.fontSize > 0) stc->StyleSetSize(i, s.fontSize);
    }
    stc->SetProperty("fold", "1");
}
