// Scintilla messages that are newer than the Scintilla wxWidgets ships.
//
// Plugins compile against third_party/scintilla/include (Notepad++'s current Scintilla 5.x), but the
// editor they talk to is the 5.0 inside wxStyledTextCtrl, which answers an id it does not know with a
// silent 0. Each message here has an exact equivalent in terms of what 5.0 does have, so it is served by
// that instead.
//
// The *FULL messages exist so a document can pass 2 GiB, and wxNote's documents can (they are created with
// SC_DOCUMENTOPTION_TEXT_LARGE). 5.0's own range messages take `long` positions, 32 bits on Windows, so
// forwarding to them would fail on exactly the files FULL is for. These use the messages that take a real
// position instead. SCI_FORMATRANGEFULL is the exception and lives in main.cpp: it needs a wxDC.
#pragma once

#include "Scintilla.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace wxnsci {

// 5.0 has no setter for this flag, but SCI_SETSELECTIONMODE toggles it when the mode does not change.
template <class Send>
void setMoveExtends(bool on, Send& send)
{
    if ((send(SCI_GETMOVEEXTENDSSELECTION, 0, 0) != 0) != on)
        send(SCI_SETSELECTIONMODE, static_cast<uintptr_t>(send(SCI_GETSELECTIONMODE, 0, 0)), 0);
}

// SCI_GETRANGEPOINTER does no bounds checking, so unlike the other cases this one clamps first.
template <class Send>
intptr_t getTextRangeFull(Sci_TextRangeFull* tr, Send& send)
{
    if (!tr || !tr->lpstrText) return 0;
    const intptr_t docLen = send(SCI_GETLENGTH, 0, 0);
    const intptr_t from = std::min<intptr_t>(std::max<intptr_t>(tr->chrg.cpMin, 0), docLen);
    const intptr_t to   = tr->chrg.cpMax < 0 ? docLen : std::min<intptr_t>(tr->chrg.cpMax, docLen);
    const intptr_t len  = std::max<intptr_t>(to - from, 0);
    if (len > 0)
    {
        const intptr_t at = send(SCI_GETRANGEPOINTER, static_cast<uintptr_t>(from), len);
        if (at) std::memcpy(tr->lpstrText, reinterpret_cast<const char*>(at), static_cast<size_t>(len));
    }
    tr->lpstrText[len] = '\0';
    return len;
}

template <class Send>
intptr_t getStyledTextFull(Sci_TextRangeFull* tr, Send& send)
{
    if (!tr || !tr->lpstrText) return 0;
    intptr_t place = 0;
    for (Sci_Position pos = tr->chrg.cpMin; pos < tr->chrg.cpMax; ++pos)
    {
        tr->lpstrText[place++] = static_cast<char>(send(SCI_GETCHARAT, static_cast<uintptr_t>(pos), 0));
        tr->lpstrText[place++] = static_cast<char>(send(SCI_GETSTYLEAT, static_cast<uintptr_t>(pos), 0));
    }
    tr->lpstrText[place] = '\0';
    tr->lpstrText[place + 1] = '\0';
    return place;
}

// Target search is the one search in 5.0 with real positions. Target and flags are put back afterwards:
// SCI_FINDTEXT leaves both alone, and a plugin may be in the middle of a replace loop that relies on them.
template <class Send>
intptr_t findTextFull(uintptr_t flags, Sci_TextToFindFull* ft, Send& send)
{
    if (!ft || !ft->lpstrText) return -1;
    const intptr_t targetStart = send(SCI_GETTARGETSTART, 0, 0);
    const intptr_t targetEnd   = send(SCI_GETTARGETEND, 0, 0);
    const intptr_t oldFlags    = send(SCI_GETSEARCHFLAGS, 0, 0);
    send(SCI_SETSEARCHFLAGS, flags, 0);
    send(SCI_SETTARGETRANGE, static_cast<uintptr_t>(ft->chrg.cpMin), ft->chrg.cpMax);
    const intptr_t found = send(SCI_SEARCHINTARGET, std::strlen(ft->lpstrText), reinterpret_cast<intptr_t>(ft->lpstrText));
    if (found != -1)
    {
        ft->chrgText.cpMin = send(SCI_GETTARGETSTART, 0, 0);
        ft->chrgText.cpMax = send(SCI_GETTARGETEND, 0, 0);
    }
    send(SCI_SETTARGETRANGE, static_cast<uintptr_t>(targetStart), targetEnd);
    send(SCI_SETSEARCHFLAGS, static_cast<uintptr_t>(oldFlags), 0);
    return found;
}

// Serve msg, through send(msg, wParam, lParam) for whatever 5.0 understands directly.
template <class Send>
intptr_t translate(unsigned msg, uintptr_t w, intptr_t l, Send&& send)
{
    switch (msg)
    {
    case SCI_GETTEXTRANGEFULL:
        return getTextRangeFull(reinterpret_cast<Sci_TextRangeFull*>(l), send);
    case SCI_GETSTYLEDTEXTFULL:
        return getStyledTextFull(reinterpret_cast<Sci_TextRangeFull*>(l), send);
    case SCI_FINDTEXTFULL:
        return findTextFull(w, reinterpret_cast<Sci_TextToFindFull*>(l), send);
    case SCI_GETSTYLEINDEXAT:   // SCI_GETSTYLEAT returns a signed char: a style above 127 comes back negative
        return send(SCI_GETSTYLEAT, w, 0) & 0xFF;
    case SCI_REPLACETARGETMINIMAL:   // the same text, without the undo-size saving
        return send(SCI_REPLACETARGET, w, l);
    case SCI_SETMOVEEXTENDSSELECTION:
        setMoveExtends(w != 0, send);
        return 0;
    case SCI_CHANGESELECTIONMODE:    // SCI_SETSELECTIONMODE, without its side effect on the flag above
    {
        const bool extends = send(SCI_GETMOVEEXTENDSSELECTION, 0, 0) != 0;
        send(SCI_SETSELECTIONMODE, w, 0);
        setMoveExtends(extends, send);
        return 0;
    }
    case SCI_CUTALLOWLINE:
        if (!send(SCI_GETSELECTIONEMPTY, 0, 0)) return send(SCI_CUT, 0, 0);
        send(SCI_BEGINUNDOACTION, 0, 0);
        send(SCI_COPYALLOWLINE, 0, 0);
        send(SCI_LINEDELETE, 0, 0);
        send(SCI_ENDUNDOACTION, 0, 0);
        return 0;
    case SCI_SCROLLVERTICAL:         // wParam = document line, lParam = sub-line within it
        return send(SCI_SETFIRSTVISIBLELINE, static_cast<uintptr_t>(send(SCI_VISIBLEFROMDOCLINE, w, 0) + l), 0);
    default:
        return send(msg, w, l);
    }
}

} // namespace wxnsci
