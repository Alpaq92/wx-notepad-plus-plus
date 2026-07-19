// term_view.cpp - owner-drawn cell grid over libvterm (contract and scope notes in term_view.h).

// vterm.h FIRST, before any wx header: wx pulls in <windows.h>, whose rpcndr.h does
// `#define small char`, which would mangle vterm.h's `unsigned int small : 1` bit-field into a
// syntax error. Parsed before the macro exists, the struct is fine (we never touch .small).
#include <vterm.h>

#include "term_view.h"

#include <wx/clipbrd.h>   // Ctrl+Shift+C / Ctrl+Shift+V
#include <wx/dcbuffer.h>  // wxAutoBufferedPaintDC - flicker-free owner-draw
#include <wx/dcclient.h>  // wxClientDC - cell metrics from GetTextExtent
#include <wx/strconv.h>   // wxMBConvUTF32 - paste re-joins MSW's UTF-16 surrogate pairs
#include <wx/utils.h>     // wxBell

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>

namespace {

// ANSI 0-15 per theme (VS Code's dark/light terminal sets - readable on our default backgrounds,
// which match TerminalTab::applyTheme so v1 and v2 terminals look like one feature). 16-255 are
// computed by libvterm itself (6x6x6 cube + gray ramp) and need no theming.
const unsigned char k_ansiDark[16][3] = {
    {  12,  12,  12 }, { 205,  49,  49 }, {  13, 188, 121 }, { 229, 229,  16 },
    {  36, 114, 200 }, { 188,  63, 188 }, {  17, 168, 205 }, { 229, 229, 229 },
    { 102, 102, 102 }, { 241,  76,  76 }, {  35, 209, 139 }, { 245, 245,  67 },
    {  59, 142, 234 }, { 214, 112, 214 }, {  41, 184, 219 }, { 255, 255, 255 },
};
const unsigned char k_ansiLight[16][3] = {
    {   0,   0,   0 }, { 205,  49,  49 }, {   0, 188,   0 }, { 148, 152,   0 },
    {   4,  81, 165 }, { 188,   5, 188 }, {   5, 152, 188 }, {  85,  85,  85 },
    { 102, 102, 102 }, { 205,  49,  49 }, {  20, 206,  20 }, { 181, 186,   0 },
    {   4,  81, 165 }, { 188,   5, 188 }, {   5, 152, 188 }, { 165, 165, 165 },
};

// wxKeyboardState is the common base of wxKeyEvent and wxMouseState, so one helper serves both.
// RawControl deliberately: on macOS wx maps ControlDown() to Cmd, but the terminal's CTRL must be
// the PHYSICAL Ctrl key (Ctrl+C = SIGINT); RawControlDown() is that key there and identical to
// ControlDown() everywhere else.
VTermModifier modsOf(const wxKeyboardState& ks)
{
    int mod = VTERM_MOD_NONE;
    if (ks.ShiftDown())      mod |= VTERM_MOD_SHIFT;
    if (ks.RawControlDown()) mod |= VTERM_MOD_CTRL;
    if (ks.AltDown())        mod |= VTERM_MOD_ALT;
    return (VTermModifier)mod;
}

} // namespace

struct TermView::Impl
{
    static constexpr int kMaxScrollback = 5000;

    // Selection endpoints live in ABSOLUTE line space - line 0 is the oldest scrollback line,
    // sb.size()..sb.size()+rows-1 are the live screen - so a selection survives both scrolling the
    // view and new output pushing lines into scrollback (indices are adjusted when the bounded
    // deque drops its front, see cbSbPush).
    struct AbsPos
    {
        int line = 0, col = 0;
        bool operator==(const AbsPos& o) const { return line == o.line && col == o.col; }
        bool operator<(const AbsPos& o) const { return line != o.line ? line < o.line : col < o.col; }
    };

    TermView*    q   = nullptr;
    VTerm*       vt  = nullptr;
    VTermState*  vst = nullptr;
    VTermScreen* vts = nullptr;

    int cols = 80, rows = 24;   // stays the 80x24 default until the first real wxEVT_SIZE
    int cellW = 8, cellH = 16;

    wxString face;
    int      pt = 10;
    wxFont   font, fontB, fontI, fontBI;

    bool     dark = true;
    wxColour defFg, defBg;

    VTermPos cursor{ 0, 0 };
    bool cursorVisible = true;
    bool focused       = false;
    bool altScreen     = false;
    int  mouseMode     = VTERM_PROP_MOUSE_NONE;   // VTERM_PROP_MOUSE_* the child requested

    std::deque<std::vector<VTermScreenCell>> sb;  // scrollback, oldest line at front
    int sbOffset   = 0;                           // lines scrolled back into sb; 0 = live
    int wheelAccum = 0;                           // sub-notch wheel remainder (hi-res wheels)

    bool   selecting = false, hasSel = false;
    AbsPos selA, selB;

    static const VTermScreenCallbacks kScreenCbs;

    // ---- libvterm -> widget callbacks (all fire inside vterm_* calls we make, on the UI thread) --

    static int cbDamage(VTermRect r, void* user)
    {
        static_cast<Impl*>(user)->refreshCellRect(r);
        return 1;
    }
    static int cbMoveRect(VTermRect dest, VTermRect src, void* user)
    {
        // No blitting: painting always re-reads the (already moved) model, so refreshing the
        // destination area is complete and correct. src needs nothing - vterm damages it separately.
        (void)src;
        static_cast<Impl*>(user)->refreshCellRect(dest);
        return 1;
    }
    static int cbMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user)
    {
        (void)oldpos; (void)visible;   // visibility tracked via VTERM_PROP_CURSORVISIBLE instead
        Impl* im = static_cast<Impl*>(user);
        im->refreshCursorCell();       // erase at the old position...
        im->cursor = pos;
        im->refreshCursorCell();       // ...draw at the new one
        return 1;
    }
    static int cbTermProp(VTermProp prop, VTermValue* val, void* user)
    {
        Impl* im = static_cast<Impl*>(user);
        switch (prop)
        {
        case VTERM_PROP_CURSORVISIBLE:
            im->cursorVisible = val->boolean != 0;
            im->refreshCursorCell();
            break;
        case VTERM_PROP_ALTSCREEN:
            // vim/htop switching in or out: the whole visible content changes, and a selection made
            // on the other screen would highlight unrelated cells - drop both view state bits.
            im->altScreen = val->boolean != 0;
            im->sbOffset  = 0;
            im->hasSel    = false;
            im->q->Refresh();
            break;
        case VTERM_PROP_MOUSE:
            im->mouseMode = val->number;
            break;
        default:
            break;
        }
        return 1;
    }
    static int cbBell(void* user)
    {
        (void)user;
        wxBell();
        return 1;
    }
    static int cbSbPush(int cols, const VTermScreenCell* cells, void* user)
    {
        Impl* im = static_cast<Impl*>(user);
        // Trim trailing default-blank cells before storing: 5000 lines x a wide grid of mostly
        // space cells would pin tens of MB otherwise. Cells with a non-default background survive
        // the trim (a full-width colour bar must repaint from scrollback intact).
        int n = cols;
        while (n > 0)
        {
            const VTermScreenCell& c = cells[n - 1];
            if (c.chars[0] == 0 && !c.attrs.reverse && VTERM_COLOR_IS_DEFAULT_BG(&c.bg)) --n;
            else break;
        }
        im->sb.emplace_back(cells, cells + n);
        if ((int)im->sb.size() > kMaxScrollback)
        {
            im->sb.pop_front();
            // Absolute line indices just shifted by one - drag the selection along so it keeps
            // highlighting the same text (clamped out of existence once it falls off the top).
            if (--im->selA.line < 0) im->selA = { 0, 0 };
            if (--im->selB.line < 0) { im->selB = { 0, 0 }; im->hasSel = false; }
        }
        if (im->sbOffset > 0)
        {
            // The user is reading scrollback: growing the offset by exactly one keeps the viewed
            // text stationary as new lines arrive (same back-relative distance = same content, so
            // no repaint needed unless the clamp bites).
            const int want = im->sbOffset + 1;
            im->sbOffset = std::min(want, (int)im->sb.size());
            if (im->sbOffset != want) im->q->Refresh();
        }
        return 1;
    }
    static int cbSbPop(int cols, VTermScreenCell* cells, void* user)
    {
        Impl* im = static_cast<Impl*>(user);
        if (im->sb.empty()) return 0;
        const std::vector<VTermScreenCell> line = std::move(im->sb.back());
        im->sb.pop_back();
        im->sbOffset = std::min(im->sbOffset, (int)im->sb.size());
        for (int i = 0; i < cols; ++i)
        {
            if (i < (int)line.size()) cells[i] = line[(size_t)i];
            else im->blankCell(cells[i]);   // restore what cbSbPush trimmed
        }
        return 1;
    }
    static int cbSbClear(void* user)
    {
        Impl* im = static_cast<Impl*>(user);
        im->sb.clear();
        im->sbOffset = 0;
        im->hasSel   = false;
        im->q->Refresh();
        return 1;
    }
    static void cbOutput(const char* s, size_t len, void* user)
    {
        Impl* im = static_cast<Impl*>(user);
        if (im->q->onOutput) im->q->onOutput(s, len);
    }

    // ---- geometry ------------------------------------------------------------------------------

    void refreshCellRect(const VTermRect& r)
    {
        // Live rows render sbOffset rows lower while the view is scrolled back; wx clips whatever
        // lands below the client area.
        q->RefreshRect(wxRect(r.start_col * cellW,
                              (r.start_row + sbOffset) * cellH,
                              (r.end_col - r.start_col) * cellW,
                              (r.end_row - r.start_row) * cellH));
    }
    void refreshCursorCell()
    {
        // Two cells wide: the cursor may sit on a double-width glyph.
        q->RefreshRect(wxRect(cursor.col * cellW, (cursor.row + sbOffset) * cellH,
                              2 * cellW, cellH));
    }
    VTermPos gridAt(const wxPoint& p) const   // pixel -> live-grid cell, clamped inside the grid
    {
        VTermPos g;
        g.col = std::max(0, std::min(cols - 1, p.x / std::max(1, cellW)));
        g.row = std::max(0, std::min(rows - 1, p.y / std::max(1, cellH)));
        return g;
    }
    AbsPos absAt(const wxPoint& p) const      // pixel -> absolute (scrollback-aware) cell
    {
        const VTermPos g = gridAt(p);
        return AbsPos{ (int)sb.size() - sbOffset + g.row, g.col };
    }

    // ---- cell access (scrollback + live screen flattened into absolute line space) --------------

    void blankCell(VTermScreenCell& out) const
    {
        std::memset(&out, 0, sizeof(out));
        out.width = 1;
        // The state's defaults carry the DEFAULT_FG/BG type flags, so these blanks retroactively
        // recolour on a theme swap just like real default-coloured cells.
        vterm_state_get_default_colors(vst, &out.fg, &out.bg);
    }
    void cellAtAbs(int line, int col, VTermScreenCell& out) const
    {
        const int sbSize = (int)sb.size();
        if (line < 0 || col < 0) { blankCell(out); return; }
        if (line < sbSize)
        {
            const std::vector<VTermScreenCell>& l = sb[(size_t)line];
            if (col < (int)l.size()) out = l[(size_t)col];
            else blankCell(out);
            return;
        }
        const int liveRow = line - sbSize;
        if (liveRow >= rows || col >= cols) { blankCell(out); return; }
        VTermPos p; p.row = liveRow; p.col = col;
        std::memset(&out, 0, sizeof(out));
        vterm_screen_get_cell(vts, p, &out);
    }
    // [c0, c1] inclusive of one absolute line as text, right-trimmed. Continuation halves of
    // double-width glyphs (chars[0] == -1) contribute nothing; blanks become spaces.
    wxString textRange(int line, int c0, int c1) const
    {
        wxString out;
        VTermScreenCell cell;
        for (int c = std::max(0, c0); c <= c1; )
        {
            cellAtAbs(line, c, cell);
            if (cell.chars[0] == (uint32_t)-1) { ++c; continue; }
            if (cell.chars[0] == 0)
                out += ' ';
            else
                for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i)
                    out << wxUniChar((unsigned int)cell.chars[i]);
            c += cell.width >= 2 ? 2 : 1;
        }
        size_t end = out.length();
        while (end > 0 && out[end - 1] == ' ') --end;
        out.Truncate(end);
        return out;
    }

    // ---- colours / fonts -----------------------------------------------------------------------

    wxColour resolveColor(VTermColor c, bool asBg) const
    {
        // Default-flagged colours resolve against OUR defaults, not the RGB frozen into the cell:
        // cells keep the rgb the defaults had when they were written, so after setDark() the stale
        // value would paint the old theme's colour.
        if (asBg  && VTERM_COLOR_IS_DEFAULT_BG(&c)) return defBg;
        if (!asBg && VTERM_COLOR_IS_DEFAULT_FG(&c)) return defFg;
        vterm_screen_convert_color_to_rgb(vts, &c);   // indexed -> palette lookup (256-safe)
        return wxColour(c.rgb.red, c.rgb.green, c.rgb.blue);
    }
    void applyPalette()
    {
        defFg = dark ? wxColour(220, 220, 220) : wxColour(30, 30, 30);
        defBg = dark ? wxColour(24, 24, 24)    : wxColour(252, 252, 252);
        const unsigned char (*pal)[3] = dark ? k_ansiDark : k_ansiLight;
        for (int i = 0; i < 16; ++i)
        {
            VTermColor c;
            vterm_color_rgb(&c, pal[i][0], pal[i][1], pal[i][2]);
            vterm_state_set_palette_color(vst, i, &c);
        }
        VTermColor f, b;
        vterm_color_rgb(&f, defFg.Red(), defFg.Green(), defFg.Blue());
        vterm_color_rgb(&b, defBg.Red(), defBg.Green(), defBg.Blue());
        // The _screen_ variant also rewrites default-flagged colours already in the buffers.
        vterm_screen_set_default_colors(vts, &f, &b);
        q->SetBackgroundColour(defBg);
    }
    void rebuildFonts()
    {
        auto mk = [this](bool bold, bool ital)
        {
            wxFontInfo fi(pt);
            fi.Family(wxFONTFAMILY_TELETYPE);
            if (!face.empty()) fi.FaceName(face);
#ifdef __WXMSW__
            else fi.FaceName("Consolas");   // TELETYPE alone maps to Courier New on MSW; match TerminalTab
#endif
            if (bold) fi.Bold();
            if (ital) fi.Italic();
            return wxFont(fi);
        };
        font   = mk(false, false);
        fontB  = mk(true,  false);
        fontI  = mk(false, true);
        fontBI = mk(true,  true);
        wxClientDC dc(q);
        dc.SetFont(font);
        const wxSize ext = dc.GetTextExtent("M");   // one reference glyph: monospace assumed (see term_view.h)
        cellW = std::max(1, ext.x);
        cellH = std::max(1, ext.y);
    }
    const wxFont& pickFont(bool bold, bool ital) const
    {
        return bold ? (ital ? fontBI : fontB) : (ital ? fontI : font);
    }

    // ---- grid / resize -------------------------------------------------------------------------

    void updateGrid(const wxSize& client)
    {
        // Pre-layout sizes (0x0 or sub-cell) would collapse the grid to 1x1 and shove 23 blank
        // lines into scrollback before the pane is even shown - keep the 80x24 default instead.
        if (client.x < cellW || client.y < cellH) return;
        const int nc = std::max(1, client.x / cellW);
        const int nr = std::max(1, client.y / cellH);
        if (nc == cols && nr == rows) return;
        cols = nc;
        rows = nr;
        vterm_set_size(vt, rows, cols);   // resizes state+screen; may push/pop scrollback via callbacks
        vterm_screen_flush_damage(vts);
        sbOffset = std::min(sbOffset, (int)sb.size());
        if (q->onResizeRequest) q->onResizeRequest(cols, rows);
        q->Refresh();
    }
    void snapBack()   // any keypress while reading scrollback jumps back to the live screen
    {
        if (sbOffset != 0)
        {
            sbOffset = 0;
            q->Refresh();
        }
    }

    // ---- selection / clipboard -----------------------------------------------------------------

    bool isSelectedView(int viewRow, int col) const
    {
        if (!hasSel) return false;
        AbsPos a = selA, b = selB;
        if (b < a) std::swap(a, b);
        const AbsPos p{ (int)sb.size() - sbOffset + viewRow, col };
        return !(p < a) && !(b < p);   // inclusive: the cell under the mouse is part of the selection
    }
    wxString selectionText() const
    {
        if (!hasSel) return wxString();
        AbsPos a = selA, b = selB;
        if (b < a) std::swap(a, b);
        wxString out;
        for (int line = a.line; line <= b.line; ++line)
        {
            if (line != a.line) out += '\n';
            out += textRange(line,
                             line == a.line ? a.col : 0,
                             line == b.line ? b.col : cols - 1);
        }
        return out;
    }
    void copySelection()
    {
        const wxString s = selectionText();
        if (s.empty()) return;
        wxClipboardLocker lock;
        if (!lock) return;
        wxTheClipboard->SetData(new wxTextDataObject(s));   // clipboard takes ownership
    }
    void paste()
    {
        wxString text;
        {
            wxClipboardLocker lock;
            if (!lock) return;
            wxTextDataObject data;
            if (!wxTheClipboard->GetData(data)) return;
            text = data.GetText();
        }
        if (text.empty()) return;
        snapBack();
        // Through UTF-32 so astral-plane chars survive MSW's UTF-16 wxString (surrogate pairs are
        // re-joined by the conversion; indexing the wxString directly would feed vterm lone halves).
        wxMBConvUTF32 conv;
        const auto buf = text.mb_str(conv);
        const size_t n = buf.length() / 4;
        // Emits the bracketed-paste markers only if the app enabled DECSET 2004; plain input otherwise.
        vterm_keyboard_start_paste(vt);
        for (size_t i = 0; i < n; ++i)
        {
            uint32_t c = 0;
            std::memcpy(&c, buf.data() + 4 * i, 4);
            if (c == '\r' || c == '\n')
            {
                vterm_keyboard_key(vt, VTERM_KEY_ENTER, VTERM_MOD_NONE);
                if (c == '\r' && i + 1 < n)   // CRLF -> ONE Enter, or shells run every line twice
                {
                    uint32_t next = 0;
                    std::memcpy(&next, buf.data() + 4 * (i + 1), 4);
                    if (next == '\n') ++i;
                }
            }
            else if (c == '\t') vterm_keyboard_key(vt, VTERM_KEY_TAB, VTERM_MOD_NONE);
            else if (c >= 32)   vterm_keyboard_unichar(vt, c, VTERM_MOD_NONE);
            // other control bytes are dropped - pasting a BEL/ESC into a shell is never intended
        }
        vterm_keyboard_end_paste(vt);
    }

    // ---- mouse reporting -----------------------------------------------------------------------

    void fwdButton(const wxMouseEvent& e, int button, bool pressed)
    {
        if (mouseMode == VTERM_PROP_MOUSE_NONE) return;
        const VTermPos p = gridAt(e.GetPosition());
        vterm_mouse_move(vt, p.row, p.col, modsOf(e));
        vterm_mouse_button(vt, button, pressed, modsOf(e));
    }

    // ---- painting ------------------------------------------------------------------------------

    void paint(wxDC& dc)
    {
        dc.SetBackground(wxBrush(defBg));
        dc.Clear();
        VTermScreenCell cell;
        for (int r = 0; r < rows; ++r)
        {
            const int y = r * cellH;
            // Run state: consecutive same-attr cells accumulate into one DrawText (per-cell
            // DrawText was an order of magnitude slower on full-screen repaints).
            wxString run;
            int      runX = 0, runCells = 0;
            wxColour runFg, runBg;
            bool     runBold = false, runItal = false, runUl = false, runStrike = false;
            bool     haveRun = false;
            auto flush = [&]()
            {
                if (!haveRun) return;
                const int w = runCells * cellW;
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(wxBrush(runBg));
                dc.DrawRectangle(runX, y, w, cellH);
                dc.SetFont(pickFont(runBold, runItal));
                dc.SetTextForeground(runFg);
                dc.DrawText(run, runX, y);
                if (runUl)   // double/curly underline rendered as single - fidelity is v3 territory
                {
                    dc.SetPen(wxPen(runFg));
                    dc.DrawLine(runX, y + cellH - 2, runX + w, y + cellH - 2);
                }
                if (runStrike)
                {
                    dc.SetPen(wxPen(runFg));
                    dc.DrawLine(runX, y + cellH / 2, runX + w, y + cellH / 2);
                }
                run.clear();
                runCells = 0;
                haveRun  = false;
            };
            for (int c = 0; c < cols; )
            {
                cellAtAbs((int)sb.size() - sbOffset + r, c, cell);
                if (cell.chars[0] == (uint32_t)-1) { ++c; continue; }   // right half of a wide glyph
                const int width = cell.width >= 2 ? 2 : 1;
                wxColour fg = resolveColor(cell.fg, false);
                wxColour bg = resolveColor(cell.bg, true);
                if (cell.attrs.reverse)     std::swap(fg, bg);
                if (isSelectedView(r, c))   std::swap(fg, bg);   // selection = inverted colours
                const bool bold   = cell.attrs.bold != 0;
                const bool ital   = cell.attrs.italic != 0;
                const bool ul     = cell.attrs.underline != VTERM_UNDERLINE_OFF;
                const bool strike = cell.attrs.strike != 0;
                if (haveRun && (fg != runFg || bg != runBg || bold != runBold ||
                                ital != runItal || ul != runUl || strike != runStrike))
                    flush();
                if (!haveRun)
                {
                    runX    = c * cellW;
                    runFg   = fg;   runBg   = bg;
                    runBold = bold; runItal = ital;
                    runUl   = ul;   runStrike = strike;
                    haveRun = true;
                }
                if (cell.attrs.conceal || cell.chars[0] == 0)
                {
                    run += ' ';
                    if (width == 2) run += ' ';
                }
                else
                {
                    for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i)
                        run << wxUniChar((unsigned int)cell.chars[i]);
                }
                runCells += width;
                c += width;
            }
            flush();
        }
        drawCursor(dc);
    }
    void drawCursor(wxDC& dc)
    {
        // Hidden while reading scrollback: the live cursor row isn't on screen, and painting it
        // over historical text pins a phantom block onto unrelated content.
        if (!cursorVisible || sbOffset != 0) return;
        if (cursor.row < 0 || cursor.row >= rows || cursor.col < 0 || cursor.col >= cols) return;
        const int x = cursor.col * cellW;
        const int y = cursor.row * cellH;
        if (!focused)
        {
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.SetPen(wxPen(defFg));
            dc.DrawRectangle(x, y, cellW, cellH);   // hollow outline: "this is where input would go"
            return;
        }
        VTermScreenCell cell;
        cellAtAbs((int)sb.size() + cursor.row, cursor.col, cell);
        wxColour fg = resolveColor(cell.fg, false);
        wxColour bg = resolveColor(cell.bg, true);
        if (cell.attrs.reverse) std::swap(fg, bg);
        const int w = cell.width >= 2 ? 2 * cellW : cellW;
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(fg));   // filled block = the cell with its colours swapped
        dc.DrawRectangle(x, y, w, cellH);
        if (cell.chars[0] && cell.chars[0] != (uint32_t)-1)
        {
            wxString s;
            for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i)
                s << wxUniChar((unsigned int)cell.chars[i]);
            dc.SetFont(pickFont(cell.attrs.bold != 0, cell.attrs.italic != 0));
            dc.SetTextForeground(bg);
            dc.DrawText(s, x, y);
        }
    }
};

// Positional init - field order must match VTermScreenCallbacks in vterm.h exactly. libvterm KEEPS
// this pointer (vterm_screen_set_callbacks stores it, no copy), hence a static, not a ctor local.
const VTermScreenCallbacks TermView::Impl::kScreenCbs = {
    cbDamage,       // damage
    cbMoveRect,     // moverect
    cbMoveCursor,   // movecursor
    cbTermProp,     // settermprop
    cbBell,         // bell
    nullptr,        // resize - the grid is driven from wxEVT_SIZE, never by the child
    cbSbPush,       // sb_pushline
    cbSbPop,        // sb_popline
    cbSbClear,      // sb_clear
};

// ================================================================================================

TermView::TermView(wxWindow* parent, const wxString& fontFace, int fontSize, bool dark)
    // wxWANTS_CHARS: without it MSW's dialog manager eats Tab and Return as focus-navigation /
    // default-button keys before wxEVT_KEY_DOWN ever fires - the same lesson TermFlatButton learned.
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS | wxBORDER_NONE)
    , m(new Impl)
{
    Impl* im = m.get();
    im->q    = this;
    im->face = fontFace;
    im->pt   = fontSize > 0 ? fontSize : 10;
    im->dark = dark;

    SetBackgroundStyle(wxBG_STYLE_PAINT);   // required by wxAutoBufferedPaintDC; also kills erase flicker

    im->vt  = vterm_new(im->rows, im->cols);
    vterm_set_utf8(im->vt, 1);
    im->vst = vterm_obtain_state(im->vt);
    im->vts = vterm_obtain_screen(im->vt);
    vterm_screen_enable_altscreen(im->vts, 1);    // vim/htop live on the alternate screen
    vterm_state_set_bold_highbright(im->vst, 1);  // bold + ANSI 0-7 promotes to 8-15 (common xterm default)
    vterm_screen_set_callbacks(im->vts, &Impl::kScreenCbs, im);
    // SCROLL merge: scrolling output arrives as one moverect + a damage strip instead of per-cell
    // damage - full-speed `dir /s` output was repaint-bound with the default per-cell merge.
    vterm_screen_set_damage_merge(im->vts, VTERM_DAMAGE_SCROLL);
    vterm_output_set_callback(im->vt, &Impl::cbOutput, im);
    im->applyPalette();
    vterm_screen_reset(im->vts, 1);   // libvterm requires a reset before first use
    im->rebuildFonts();

    Bind(wxEVT_PAINT,       &TermView::onPaint,       this);
    Bind(wxEVT_SIZE,        &TermView::onSize,        this);
    Bind(wxEVT_KEY_DOWN,    &TermView::onKeyDown,     this);
    Bind(wxEVT_CHAR,        &TermView::onChar,        this);
    Bind(wxEVT_LEFT_DOWN,   &TermView::onMouseDown,   this);
    Bind(wxEVT_LEFT_UP,     &TermView::onMouseUp,     this);
    Bind(wxEVT_MOTION,      &TermView::onMotion,      this);
    Bind(wxEVT_MOUSEWHEEL,  &TermView::onWheel,       this);
    Bind(wxEVT_SET_FOCUS,   &TermView::onFocusGain,   this);
    Bind(wxEVT_KILL_FOCUS,  &TermView::onFocusLose,   this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, &TermView::onCaptureLost, this);
    // Right/middle buttons exist only for child mouse reporting (vim's mouse=a etc.); with the
    // mouse mode off, fwdButton is a no-op and the events fall through unused.
    Bind(wxEVT_RIGHT_DOWN,  [this](wxMouseEvent& e) { m->fwdButton(e, 3, true);  });
    Bind(wxEVT_RIGHT_UP,    [this](wxMouseEvent& e) { m->fwdButton(e, 3, false); });
    Bind(wxEVT_MIDDLE_DOWN, [this](wxMouseEvent& e) { m->fwdButton(e, 2, true);  });
    Bind(wxEVT_MIDDLE_UP,   [this](wxMouseEvent& e) { m->fwdButton(e, 2, false); });
}

TermView::~TermView()
{
    // Callbacks cannot fire after this: they only ever run inside vterm_* calls we make ourselves.
    if (m && m->vt) vterm_free(m->vt);
}

// ---- public API --------------------------------------------------------------------------------

void TermView::feed(const char* data, size_t len)
{
    vterm_input_write(m->vt, data, len);
    vterm_screen_flush_damage(m->vts);   // deliver the merged damage now, while the model is settled
}

void TermView::setDark(bool dark)
{
    m->dark = dark;
    m->applyPalette();
    Refresh();
}

void TermView::setFont(const wxString& face, int pt)
{
    m->face = face;
    m->pt   = pt > 0 ? pt : 10;
    m->rebuildFonts();
    m->updateGrid(GetClientSize());   // new cell metrics usually mean a new grid -> resize the PTY too
    Refresh();
}

int TermView::cols() const { return m->cols; }
int TermView::rows() const { return m->rows; }

wxString TermView::rowText(int row) const
{
    if (row < 0 || row >= m->rows) return wxString();
    return m->textRange((int)m->sb.size() + row, 0, m->cols - 1);
}

wxString TermView::screenText() const
{
    wxString out;
    for (int r = 0; r < m->rows; ++r)
    {
        if (r) out += '\n';
        out += rowText(r);
    }
    return out;
}

// ---- event handlers ----------------------------------------------------------------------------

void TermView::onPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);   // clipped to the update region by wx - full-grid draw is cheap
    m->paint(dc);
}

void TermView::onSize(wxSizeEvent& e)
{
    m->updateGrid(GetClientSize());
    e.Skip();
}

void TermView::onKeyDown(wxKeyEvent& e)
{
    Impl* im = m.get();
    const int kc = e.GetKeyCode();

    // Copy/paste live on Ctrl+SHIFT+C/V, terminal-style, so that PLAIN Ctrl+C stays free to reach
    // the child as SIGINT (via onChar below) - that is what terminals do.
    if (e.RawControlDown() && e.ShiftDown() && (kc == 'C' || kc == 'V'))
    {
        if (kc == 'C') im->copySelection();
        else           im->paste();
        return;
    }

    VTermKey k = VTERM_KEY_NONE;
    switch (kc)
    {
    case WXK_RETURN: case WXK_NUMPAD_ENTER:       k = VTERM_KEY_ENTER;     break;
    case WXK_TAB:                                 k = VTERM_KEY_TAB;       break;
    case WXK_BACK:                                k = VTERM_KEY_BACKSPACE; break;
    case WXK_ESCAPE:                              k = VTERM_KEY_ESCAPE;    break;
    case WXK_UP:       case WXK_NUMPAD_UP:        k = VTERM_KEY_UP;        break;
    case WXK_DOWN:     case WXK_NUMPAD_DOWN:      k = VTERM_KEY_DOWN;      break;
    case WXK_LEFT:     case WXK_NUMPAD_LEFT:      k = VTERM_KEY_LEFT;      break;
    case WXK_RIGHT:    case WXK_NUMPAD_RIGHT:     k = VTERM_KEY_RIGHT;     break;
    case WXK_INSERT:   case WXK_NUMPAD_INSERT:    k = VTERM_KEY_INS;       break;
    case WXK_DELETE:   case WXK_NUMPAD_DELETE:    k = VTERM_KEY_DEL;       break;
    case WXK_HOME:     case WXK_NUMPAD_HOME:      k = VTERM_KEY_HOME;      break;
    case WXK_END:      case WXK_NUMPAD_END:       k = VTERM_KEY_END;       break;
    case WXK_PAGEUP:   case WXK_NUMPAD_PAGEUP:    k = VTERM_KEY_PAGEUP;    break;
    case WXK_PAGEDOWN: case WXK_NUMPAD_PAGEDOWN:  k = VTERM_KEY_PAGEDOWN;  break;
    default:
        if (kc >= WXK_F1 && kc <= WXK_F12)
            k = (VTermKey)VTERM_KEY_FUNCTION(kc - WXK_F1 + 1);
        break;
    }
    if (k != VTERM_KEY_NONE)
    {
        im->snapBack();
        VTermModifier mod = modsOf(e);
        // Enter/Backspace/Esc must reach the child as their PLAIN bytes even mid-chord: with the
        // modifier kept, libvterm encodes them as CSI-u (ESC[13;2u for Shift+Enter) which ordinary
        // shells print as garbage instead of acting on - typing a command that ends in a capital
        // and pressing Enter with Shift still held simply did not run it. xterm and Windows
        // Terminal send plain \r / 0x7f / ESC for these chords. Shift+Tab must KEEP its modifier
        // (CSI Z backtab is correct), as must cursor/function keys (modified CSI is standard) and
        // Alt (its ESC prefix is meaningful, e.g. Alt+Enter in TUIs).
        if (k == VTERM_KEY_ENTER || k == VTERM_KEY_BACKSPACE)
            mod = (VTermModifier)(mod & ~(VTERM_MOD_SHIFT | VTERM_MOD_CTRL));
        else if (k == VTERM_KEY_ESCAPE)
            mod = (VTermModifier)(mod & ~VTERM_MOD_SHIFT);
        vterm_keyboard_key(im->vt, k, mod);
        return;
    }
    e.Skip();   // printable path: let wx cook it into wxEVT_CHAR
}

void TermView::onChar(wxKeyEvent& e)
{
    Impl* im = m.get();
    const wxChar uc = e.GetUnicodeKey();
    if (uc == WXK_NONE) { e.Skip(); return; }
    const uint32_t cp = (uint32_t)uc;
    VTermModifier mod = modsOf(e);

    if (cp < 32)
    {
        // A control char here means the OS cooked a Ctrl chord for us (Ctrl+C -> 0x03). Hand vterm
        // the BASE character plus the CTRL modifier instead of the raw byte so its own encoding
        // rules apply - this is the path that delivers plain Ctrl+C to the child as SIGINT.
        if (mod & VTERM_MOD_CTRL)
        {
            uint32_t base = 0;
            if (cp >= 1 && cp <= 26)       base = 'a' + cp - 1;
            else if (cp >= 27 && cp <= 31) base = cp + 64;   // Ctrl+[ \ ] ^ _
            if (base)
            {
                im->snapBack();
                vterm_keyboard_unichar(im->vt, base, mod);
            }
        }
        // Enter/Tab/Esc/Backspace never get here - onKeyDown consumed them as VTERM_KEY_*.
        return;
    }

    // AltGr on MSW reports Ctrl+Alt while the codepoint is already the composed char ('ą', '€') -
    // forwarding CTRL|ALT with it would make vterm mangle national-layout typing into escape
    // prefixes. When both arrive together, the chord IS the character: strip them.
    if ((mod & VTERM_MOD_CTRL) && (mod & VTERM_MOD_ALT))
        mod = (VTermModifier)(mod & ~(VTERM_MOD_CTRL | VTERM_MOD_ALT));
    im->snapBack();
    // SHIFT is already baked into the codepoint ('A', '!'); passing it too would double-encode.
    vterm_keyboard_unichar(im->vt, cp, (VTermModifier)(mod & ~VTERM_MOD_SHIFT));
}

void TermView::onMouseDown(wxMouseEvent& e)
{
    SetFocus();
    Impl* im = m.get();
    if (im->mouseMode != VTERM_PROP_MOUSE_NONE && !e.ShiftDown())
    {
        im->fwdButton(e, 1, true);   // the child owns the mouse; Shift bypasses for local selection, xterm-style
        return;
    }
    if (im->hasSel)
    {
        im->hasSel = false;
        Refresh();
    }
    im->selecting = true;
    im->selA = im->selB = im->absAt(e.GetPosition());
    CaptureMouse();   // keep receiving MOTION while the drag leaves the window
}

void TermView::onMouseUp(wxMouseEvent& e)
{
    Impl* im = m.get();
    if (HasCapture()) ReleaseMouse();
    if (im->selecting)
    {
        im->selecting = false;   // hasSel was set by onMotion iff the drag actually moved cells
        return;
    }
    im->fwdButton(e, 1, false);
}

void TermView::onMotion(wxMouseEvent& e)
{
    Impl* im = m.get();
    if (im->selecting)
    {
        const Impl::AbsPos p = im->absAt(e.GetPosition());
        if (!(p == im->selB))
        {
            im->selB   = p;
            im->hasSel = true;
            Refresh();   // simplest correct invalidation; the grid is small and the paint is buffered
        }
        return;
    }
    if (im->mouseMode == VTERM_PROP_MOUSE_MOVE ||
        (im->mouseMode == VTERM_PROP_MOUSE_DRAG && e.Dragging()))
    {
        const VTermPos p = im->gridAt(e.GetPosition());
        vterm_mouse_move(im->vt, p.row, p.col, modsOf(e));
    }
}

void TermView::onWheel(wxMouseEvent& e)
{
    Impl* im = m.get();
    if (e.GetWheelAxis() != wxMOUSE_WHEEL_VERTICAL) return;
    im->wheelAccum += e.GetWheelRotation();
    const int delta   = std::max(1, e.GetWheelDelta());
    const int notches = im->wheelAccum / delta;
    if (notches == 0) return;   // hi-res wheels deliver sub-notch rotations; accumulate
    im->wheelAccum -= notches * delta;
    const int lines = std::max(1, e.GetLinesPerAction());

    if (im->mouseMode != VTERM_PROP_MOUSE_NONE && !e.ShiftDown())
    {
        // The child asked for the mouse: report wheel as buttons 4/5 (how vim's own wheel works).
        const VTermPos p = im->gridAt(e.GetPosition());
        const VTermModifier mod = modsOf(e);
        vterm_mouse_move(im->vt, p.row, p.col, mod);
        for (int i = 0; i < std::abs(notches); ++i)
            vterm_mouse_button(im->vt, notches > 0 ? 4 : 5, true, mod);
        return;
    }
    if (im->altScreen)
    {
        // No scrollback on the alternate screen: convert wheel to arrows - exactly what lets
        // vim/htop (with mouse reporting off) still scroll under the wheel.
        const VTermKey k = notches > 0 ? VTERM_KEY_UP : VTERM_KEY_DOWN;
        for (int i = 0; i < lines * std::abs(notches); ++i)
            vterm_keyboard_key(im->vt, k, VTERM_MOD_NONE);
        return;
    }
    const int want    = im->sbOffset + notches * lines;
    const int clamped = std::max(0, std::min((int)im->sb.size(), want));
    if (clamped != im->sbOffset)
    {
        im->sbOffset = clamped;
        Refresh();
    }
}

void TermView::onFocusGain(wxFocusEvent& e)
{
    m->focused = true;
    vterm_state_focus_in(m->vst);    // emits ESC[I only if the app enabled focus reporting
    m->refreshCursorCell();          // outline -> filled block
    e.Skip();
}

void TermView::onFocusLose(wxFocusEvent& e)
{
    m->focused = false;
    vterm_state_focus_out(m->vst);
    m->refreshCursorCell();          // filled block -> outline
    e.Skip();
}

void TermView::onCaptureLost(wxMouseCaptureLostEvent&)
{
    m->selecting = false;   // wx demands this handler exist whenever CaptureMouse is used
}
