// Notepad++ -> wxWidgets  |  main-window shell prototype
// ---------------------------------------------------------------------------
// A wxWidgets reproduction of the Notepad++ main window, pursuing a 1:1 match
// with the NATIVE (default/light) UI:
//   * wxMenuBar with Notepad++'s real menu labels + IDM_* command ids (from
//     PowerEditor/src/Notepad_plus.rc)
//   * wxToolBar using Notepad++'s icon pack (spike/icons/*.svg) in native order
//   * native tab strip + the real native Scintilla editor
//   * 6-field status bar, updated live
//   * the application icon (spike/wxNotepad++.svg)
//   * the wxFrame HWND still services real NPPM_* plugin messages (ABI shim)
//
// Commands are routed through one onCommand() dispatcher. Editor-backed functions
// (file I/O, clipboard, case/EOL/line ops, comment, find/replace, bookmarks,
// brace match, zoom, wrap/whitespace/guides, full screen) are implemented against
// Scintilla. Subsystem-only items (dockable panels, macros, preferences, plugins)
// report status -- they need the full app, not the shell.

#include <wx/wx.h>
#include <wx/notebook.h>
#include <wx/aui/auibook.h>
#include <wx/aui/aui.h>          // wxAuiManager - dock host for plugin panels (NPPM_DMM*)
#include <wx/stc/stc.h>          // wxStyledTextCtrl - cross-platform editor (Phase 3 port target)
#include <wx/spinctrl.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/bmpbndl.h>
#include <wx/iconbndl.h>
#include <wx/artprov.h>
#include <wx/bmpbuttn.h>
#include <wx/combobox.h>
#include <wx/radiobox.h>
#include <wx/config.h>
#include <wx/filehistory.h>     // wxFileHistory - Recent Files (MRU)
#include <wx/xml/xml.h>         // wxXmlDocument - load Notepad++ theme XML
#include <wx/datetime.h>        // wxDateTime - insert date/time
#include <wx/dnd.h>             // wxFileDropTarget - drag & drop files to open
#include <wx/dcgraph.h>         // wxGCDC - antialiased drawing (symmetric spinner triangles)
#include <wx/dir.h>             // wxDir - scan the plugins/ folder

#ifdef __WXMSW__
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#else
using UINT = unsigned int;   // Win32 scalar that leaks into the portable sci()/sciSend() message-id params
#endif

#include <string>
#include <functional>
#include <cctype>
#include <map>
#include <vector>
#include <set>
#include <algorithm>
#include <cstdlib>

#include "Scintilla.h"
#include "ILexer.h"             // Scintilla::ILexer5 (required by Lexilla.h)
#include "Lexilla.h"            // CreateLexer() - syntax highlighting
#include "SciLexer.h"           // SCE_* lexer style numbers
#ifdef __WXMSW__
#include "Notepad_plus_msgs.h" // NPPM_* plugin message ids (Win32, HWND-based)
#include "PluginInterface.h"   // real plugin ABI: NppData, FuncItem, setInfo/getFuncsArray/beNotified typedefs
#include "Docking.h"           // tTbData - plugin docking-panel registration (NPPM_DMMREGASDCKDLG)
#endif
#include "menuCmdID.h"
#include "app_icon_svg.h"
#include "npp_menu.h"          // faithful 1:1 Notepad++ main-menu builder

#ifdef __WXMSW__
static const UINT WM_SPIKE_PLUGINTEST = WM_APP + 2;
#endif
static const int  MARK_BOOKMARK = 2;      // a free Scintilla marker number for bookmarks
static const int  MARK_INDIC    = 9;      // indicator number for "Mark All" highlights (Find dialog)
static const int  SMART_INDIC   = 10;     // indicator number for smart-highlight (double-click a word)
enum { myID_TIMER = 60000, myID_DARKMODE, myID_DOCLIST, myID_CAP_NEW, myID_CAP_CLOSE };   // fixed ids, above the IDM_* range
static const int myID_DOCLIST_ITEM = 61000;   // base id for the document-list dropdown entries

// The one persistent editor view (set by the frame), used to release a tab's Document when its
// EditorPage is destroyed - the notebook switches away first, so the doc holds only the buffer ref.
static wxStyledTextCtrl* g_view = nullptr;

// One editor tab: a wxPanel that owns a Scintilla Document (the single shared view swaps to it).
class EditorPage : public wxPanel
{
public:
    explicit EditorPage(wxWindow* parent) : wxPanel(parent) {}
    ~EditorPage() override { if (doc && g_view) g_view->SendMsg(SCI_RELEASEDOCUMENT, 0, static_cast<wxIntPtr>(doc)); }
    sptr_t   doc   = 0;        // this tab's Scintilla Document (the one persistent view swaps between them)
    bool     dirty = false;    // unsaved-changes flag (live SCI_GETMODIFY for the active tab; cached here otherwise)
    wxString path;
    wxString title;            // tab/window title without the unsaved "*" marker
    wxString lang  = "Normal text file";   // status-bar language label (like Notepad++)
};

// A parsed Notepad++ theme (stylers.model.xml / themes/*.xml).
struct StyleDef { int id; int fg; int bg; int fontStyle; };   // fg/bg = -1 when unspecified
struct NppTheme
{
    bool loaded = false;
    std::map<wxString, std::pair<int,int>>   global;   // WidgetStyle name -> (fg,bg)
    std::map<wxString, std::vector<StyleDef>> lexers;   // LexerType name  -> WordsStyles
    std::string defaultFont; int defaultSize = 0;
};
// "RRGGBB" (Notepad++ XML) -> Scintilla 0xBBGGRR int, or -1 if empty/invalid.
static int npp_bgr(const wxString& rrggbb)
{
    long v = 0;
    if (rrggbb.empty() || !rrggbb.ToLong(&v, 16)) return -1;
    const int r = (int)((v >> 16) & 0xFF), g = (int)((v >> 8) & 0xFF), b = (int)(v & 0xFF);
    return (b << 16) | (g << 8) | r;
}
// Keyword lists for the languages we ship words for (others still colour comments/strings/numbers).
static const char CPP_KEYWORDS[] =
    "alignas alignof and auto bool break case catch char char8_t char16_t char32_t class const "
    "consteval constexpr constinit continue decltype default delete do double dynamic_cast else "
    "enum explicit export extern false float for friend goto if inline int long mutable namespace "
    "new noexcept nullptr operator or private protected public register reinterpret_cast return "
    "short signed sizeof static static_assert static_cast struct switch template this thread_local "
    "throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while";
static const char JS_KEYWORDS[] =   // JavaScript / TypeScript (still the C++ lexer)
    "abstract any as async await boolean break case catch class const continue debugger declare "
    "default delete do else enum export extends false finally for from function get if implements "
    "import in instanceof interface is keyof let module namespace never new null number object of "
    "package private protected public readonly return set static string super switch symbol this "
    "throw true try type typeof undefined var void while with yield";
static const char JAVA_KEYWORDS[] =
    "abstract assert boolean break byte case catch char class const continue default do double else "
    "enum extends final finally float for goto if implements import instanceof int interface long "
    "native new package private protected public record return sealed short static strictfp super "
    "switch synchronized this throw throws transient try var void volatile while true false null yield";
static const char CS_KEYWORDS[] =
    "abstract as async await base bool break byte case catch char checked class const continue decimal "
    "default delegate do double dynamic else enum event explicit extern false finally fixed float for "
    "foreach goto if implicit in int interface internal is lock long nameof namespace new null object "
    "operator out override params private protected public readonly ref return sbyte sealed short sizeof "
    "stackalloc static string struct switch this throw true try typeof uint ulong unchecked unsafe ushort "
    "using var virtual void volatile when where while yield";
static const char CSS_KEYWORDS[] =   // common properties (CSS lexer keyword list 0)
    "align-content align-items align-self animation background background-color background-image "
    "background-position background-repeat background-size border border-bottom border-color "
    "border-left border-radius border-right border-style border-top border-width bottom box-shadow "
    "box-sizing clear color content cursor display flex flex-basis flex-direction flex-grow flex-shrink "
    "flex-wrap float font font-family font-size font-style font-weight gap grid grid-template-columns "
    "grid-template-rows height justify-content left letter-spacing line-height list-style margin "
    "margin-bottom margin-left margin-right margin-top max-height max-width min-height min-width opacity "
    "outline overflow padding padding-bottom padding-left padding-right padding-top position right "
    "text-align text-decoration text-transform top transform transition vertical-align visibility "
    "white-space width word-spacing z-index";
static const char BATCH_KEYWORDS[] =
    "if else for in do goto call exit set echo setlocal endlocal shift cd chdir md mkdir rd rmdir del "
    "erase copy xcopy move ren rename type cls pause rem start exist not errorlevel defined equ neq "
    "lss leq gtr geq";
static const char PERL_KEYWORDS[] =
    "if elsif else unless while until for foreach do sub return my our local use require package and or "
    "not eq ne lt gt le ge cmp print printf say sprintf chomp chop split join push pop shift unshift "
    "splice reverse sort map grep keys values each defined exists delete wantarray ref bless die warn "
    "last next redo qw scalar";
static const char RUBY_KEYWORDS[] =
    "alias and begin break case class def defined? do else elsif end ensure false for if in module next "
    "nil not or redo rescue retry return self super then true undef unless until when while yield require "
    "require_relative include extend attr_accessor attr_reader attr_writer puts print raise lambda proc new";
static const char PS_KEYWORDS[] =   // PowerShell
    "begin break catch continue data do dynamicparam else elseif end exit filter finally for foreach from "
    "function if in param process return switch throw trap try until while class enum using namespace";
static const char CSS_PSEUDO[] =   // pseudo-classes (CSS lexer keyword list 1) so :hover etc. aren't flagged red
    "active checked default disabled empty enabled first first-child first-of-type focus focus-within "
    "hover in-range invalid last-child last-of-type link not nth-child nth-last-child nth-last-of-type "
    "nth-of-type only-child only-of-type optional out-of-range read-only read-write required root target "
    "valid visited";
static const char JSON_KEYWORDS[] = "true false null";
static const char PY_KEYWORDS[] =
    "and as assert async await break class continue def del elif else except finally for from "
    "global if import in is lambda nonlocal not or pass raise return try while with yield True False None";
static const char SQL_KEYWORDS[] =
    "add all alter and as asc between by case check column create database default delete desc distinct "
    "drop else end exists foreign from full group having in index inner insert into is join key left "
    "like limit not null on or order outer primary references right select set table then top union "
    "unique update values view where";
static const char LUA_KEYWORDS[] =
    "and break do else elseif end false for function goto if in local nil not or repeat return then true until while";
static const char BASH_KEYWORDS[] =
    "if then else elif fi case esac for select while until do done in function time coproc echo cd export local read return test";
static const char GO_KEYWORDS[] =
    "break case chan const continue default defer else fallthrough for func go goto if import interface map package range "
    "return select struct switch type var bool byte rune string int int8 int16 int32 int64 uint float32 float64 true false nil iota";
static const char RUST_KEYWORDS[] =
    "as async await break const continue crate dyn else enum extern false fn for if impl in let loop match mod move mut pub "
    "ref return self Self static struct super trait true type unsafe use where while bool char str u8 u32 u64 i32 i64 usize Vec String Option Result";

#ifdef __WXMSW__   // ---- Win32 plugin message plumbing (NPPM_* routing + the SCI_* bridge) ----
// Set by the frame: answer NPPM_*/RUNCOMMAND_USER plugin messages with real document/app info.
static std::function<bool(UINT, WPARAM, LPARAM, LRESULT&)> g_nppm;

static LRESULT CALLBACK FrameSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                          UINT_PTR, DWORD_PTR dwRefData)
{
    if (msg == WM_SPIKE_PLUGINTEST)
    {
        int view = -1;
        ::SendMessageW(hWnd, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&view));
        return (static_cast<LRESULT>(view + 1) << 16) |
               (::SendMessageW(reinterpret_cast<HWND>(dwRefData), SCI_GETLENGTH, 0, 0) & 0xFFFF);
    }
    if (g_nppm && msg >= (WM_USER + 1000))   // NPPM_ (WM_USER+1000) and RUNCOMMAND_USER (WM_USER+3000) ranges
    {
        LRESULT out = 0;
        if (g_nppm(msg, wParam, lParam, out)) return out;
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// Win32 plugins reach the editor by SendMessage'ing SCI_* to the Scintilla HWND. But wxStyledTextCtrl
// services Scintilla messages only through SendMsg (a direct ScintillaWX call); its HWND's WndProc
// ignores them. Bridge it: subclass the wxSTC HWND and forward Scintilla-range messages (SCI_* live in
// 2000-2999, the lexer messages in 4000-4999) to the real editor via the frame's sci(). (Set by frame.)
static std::function<sptr_t(UINT, WPARAM, LPARAM)> g_sciForward;
static LRESULT CALLBACK SciHwndProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR)
{
    if (g_sciForward && ((msg >= 2000 && msg < 3000) || (msg >= 4000 && msg < 5000)))
        return static_cast<LRESULT>(g_sciForward(msg, w, l));
    return DefSubclassProc(h, msg, w, l);
}
#endif // __WXMSW__ (Win32 plugin message plumbing)

// Set by the frame so the page subclass can raise the editor's own themed right-click menu
// (Scintilla's native popup is suppressed with SC_POPUP_NEVER). Args are screen coords.
static std::function<void(int, int)> g_editorContextMenu;

// Set by the frame: open file(s) dropped onto the editor (Scintilla fires SCN_URIDROPPED).
static std::function<void(const wxString&)> g_openDropped;

// Set by the frame: a zoom change (Ctrl+wheel etc.) in one editor syncs all tabs + persists.
static std::function<void(int)> g_onZoom;

// Set by the frame: forward every Scintilla SCN_* notification to loaded plugins' beNotified().
static std::function<void(SCNotification*)> g_pluginNotify;

// ---- editor behaviours driven by Scintilla notifications (auto-indent, brace match, smart-hilite) ----
static sptr_t sciSend(wxStyledTextCtrl* s, UINT m, uptr_t w = 0, sptr_t l = 0)
{ return s ? static_cast<sptr_t>(s->SendMsg(static_cast<int>(m), static_cast<wxUIntPtr>(w), static_cast<wxIntPtr>(l))) : 0; }

// Smart-highlight (double-click a word -> box every occurrence) state. Only one editor is visible at
// a time, so a single active/owner pair suffices.
static bool g_smartActive = false;
static wxStyledTextCtrl* g_smartSci = nullptr;

// On Enter, give the new line the previous line's indentation, plus one level after an opening
// brace/paren/colon - matches Notepad++'s auto-indent.
static void autoIndentOnNewline(wxStyledTextCtrl* sci)
{
    const int line = static_cast<int>(sciSend(sci, SCI_LINEFROMPOSITION, sciSend(sci, SCI_GETCURRENTPOS)));
    if (line <= 0) return;
    const int prev = line - 1;
    int indent = static_cast<int>(sciSend(sci, SCI_GETLINEINDENTATION, prev));
    int p = static_cast<int>(sciSend(sci, SCI_GETLINEENDPOSITION, prev));
    while (p > 0) { const char c = static_cast<char>(sciSend(sci, SCI_GETCHARAT, p - 1)); if (c == ' ' || c == '\t') { --p; continue; } break; }
    const char last = p > 0 ? static_cast<char>(sciSend(sci, SCI_GETCHARAT, p - 1)) : 0;
    if (last == '{' || last == '(' || last == ':') { const int w = static_cast<int>(sciSend(sci, SCI_GETTABWIDTH)); indent += (w > 0 ? w : 4); }
    sciSend(sci, SCI_SETLINEINDENTATION, line, indent);
    sciSend(sci, SCI_GOTOPOS, sciSend(sci, SCI_GETLINEINDENTPOSITION, line));
}

// When '}' is typed on an otherwise-blank line, dedent that line one level so the brace lines up
// with its opener (like Notepad++).
static void dedentCloseBrace(wxStyledTextCtrl* sci)
{
    const int pos = static_cast<int>(sciSend(sci, SCI_GETCURRENTPOS));
    const int line = static_cast<int>(sciSend(sci, SCI_LINEFROMPOSITION, pos));
    const int lineStart = static_cast<int>(sciSend(sci, SCI_POSITIONFROMLINE, line));
    for (int i = lineStart; i < pos - 1; ++i) { const char c = static_cast<char>(sciSend(sci, SCI_GETCHARAT, i)); if (c != ' ' && c != '\t') return; }
    const int w = static_cast<int>(sciSend(sci, SCI_GETTABWIDTH)); const int step = w > 0 ? w : 4;
    int indent = static_cast<int>(sciSend(sci, SCI_GETLINEINDENTATION, line));
    indent = indent >= step ? indent - step : 0;
    sciSend(sci, SCI_SETLINEINDENTATION, line, indent);
    sciSend(sci, SCI_GOTOPOS, sciSend(sci, SCI_GETLINEENDPOSITION, line));
}

// Highlight the brace pair straddling the caret (red when unmatched), like Notepad++.
static void updateBraceMatch(wxStyledTextCtrl* sci)
{
    const int pos = static_cast<int>(sciSend(sci, SCI_GETCURRENTPOS));
    auto isBrace = [](char c) { return c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}'; };
    int brace = -1;
    if (pos > 0) { const char b = static_cast<char>(sciSend(sci, SCI_GETCHARAT, pos - 1)); if (isBrace(b)) brace = pos - 1; }
    if (brace < 0) { const char a = static_cast<char>(sciSend(sci, SCI_GETCHARAT, pos)); if (isBrace(a)) brace = pos; }
    if (brace < 0) { sciSend(sci, SCI_BRACEHIGHLIGHT, static_cast<uptr_t>(-1), -1); return; }
    const sptr_t match = sciSend(sci, SCI_BRACEMATCH, brace, 0);
    if (match >= 0) sciSend(sci, SCI_BRACEHIGHLIGHT, brace, match);
    else            sciSend(sci, SCI_BRACEBADLIGHT, brace);
}

static void clearSmart(wxStyledTextCtrl* sci)
{
    sciSend(sci, SCI_SETINDICATORCURRENT, SMART_INDIC);
    sciSend(sci, SCI_INDICATORCLEARRANGE, 0, sciSend(sci, SCI_GETLENGTH));
    g_smartActive = false; g_smartSci = nullptr;
}

// Box every whole-word, case-sensitive occurrence of the just double-clicked word (Notepad++'s
// "Smart Highlighting").
static void smartHighlight(wxStyledTextCtrl* sci)
{
    clearSmart(sci);
    const int a = static_cast<int>(sciSend(sci, SCI_GETSELECTIONSTART));
    const int b = static_cast<int>(sciSend(sci, SCI_GETSELECTIONEND));
    const int wlen = b - a;
    if (wlen <= 0 || wlen > 128) return;
    std::string word(static_cast<size_t>(wlen) + 1, '\0');
    sciSend(sci, SCI_GETSELTEXT, 0, reinterpret_cast<sptr_t>(&word[0]));
    word.resize(wlen);
    const int len = static_cast<int>(sciSend(sci, SCI_GETLENGTH));
    sciSend(sci, SCI_SETSEARCHFLAGS, SCFIND_MATCHCASE | SCFIND_WHOLEWORD);
    sciSend(sci, SCI_SETINDICATORCURRENT, SMART_INDIC);
    int start = 0, count = 0;
    while (start < len)
    {
        sciSend(sci, SCI_SETTARGETSTART, start);
        sciSend(sci, SCI_SETTARGETEND, len);
        if (sciSend(sci, SCI_SEARCHINTARGET, word.size(), reinterpret_cast<sptr_t>(word.c_str())) < 0) break;
        const int ms = static_cast<int>(sciSend(sci, SCI_GETTARGETSTART));
        const int me = static_cast<int>(sciSend(sci, SCI_GETTARGETEND));
        if (me <= ms) { start = ms + 1; continue; }
        sciSend(sci, SCI_INDICATORFILLRANGE, ms, me - ms);
        start = me; ++count;
    }
    if (count > 0) { g_smartActive = true; g_smartSci = sci; }
}

// NOTE: SCN_* notifications used to arrive here as Win32 WM_NOTIFY via a subclass on the page panel.
// wxStyledTextCtrl (ScintillaWX) does NOT send WM_NOTIFY - it fires wxEVT_STC_* events instead - so the
// editor behaviours (auto-indent / brace match / smart-hilite / bookmark margin / zoom / context menu)
// and plugin beNotified forwarding now live in the frame's onStc* handlers bound to m_stc.

// Apply the dark/light visual style to native child controls (combo edit+dropdown, edit fields,
// scrollbars) so they match a themed dialog instead of standing out. Edit/combo controls need the
// "DarkMode_CFD" visual style (the common-file-dialog one) to darken their *edit background* --
// "DarkMode_Explorer" only darkens the dropdown arrow/scrollbar and leaves the field white.
#ifdef __WXMSW__
static BOOL CALLBACK themeChildProc(HWND h, LPARAM dark)
{
    wchar_t cls[64] = {};
    ::GetClassNameW(h, cls, 63);
    const bool editLike = (::lstrcmpW(cls, L"ComboBox") == 0 || ::lstrcmpW(cls, L"Edit") == 0 || ::lstrcmpW(cls, L"ComboLBox") == 0);
    if (dark) ::SetWindowTheme(h, editLike ? L"DarkMode_CFD" : L"DarkMode_Explorer", nullptr);
    else      ::SetWindowTheme(h, L"Explorer", nullptr);
    return TRUE;
}
#endif

// Themed monochrome glyph bitmap (light on dark, dark on light) for small flat buttons.
static wxBitmapBundle glyphIcon(const char* pathData, bool dark, int w = 16, int h = 16, bool filled = false)
{
    const char* col = dark ? "#dcdcdc" : "#404040";
    const wxString svg = filled
        ? wxString::Format("<svg xmlns='http://www.w3.org/2000/svg' width='%d' height='%d' viewBox='0 0 %d %d'>"
                           "<path d='%s' fill='%s'/></svg>", w, h, w, h, pathData, col)
        : wxString::Format("<svg xmlns='http://www.w3.org/2000/svg' width='%d' height='%d' viewBox='0 0 %d %d'>"
                           "<path d='%s' fill='none' stroke='%s' stroke-width='1.5' stroke-linecap='round' stroke-linejoin='round'/></svg>",
                           w, h, w, h, pathData, col);
    const wxScopedCharBuffer u = svg.utf8_str();
    return wxBitmapBundle::FromSVG(u.data(), wxSize(w, h));
}

// "Go to line" with a spinner whose up/down arrows are integrated INTO the field (borderless), like a
// styled NumericUpDown. The native wxSpinCtrl up/down (msctls_updown32) can't be dark-themed, so the
// arrows are drawn directly on a bordered panel that also hosts a borderless edit - one seamless field.
class GoToLineDialog : public wxDialog
{
public:
    GoToLineDialog(wxWindow* p, int maxLine, int cur, bool dark)
        : wxDialog(p, wxID_ANY, "Go to line"), m_max(maxLine)
    {
        m_fieldBg   = dark ? wxColour(32, 32, 32)    : *wxWHITE;             // == the DarkMode_CFD edit bg
        m_fieldFg   = dark ? wxColour(220, 220, 220) : *wxBLACK;
        m_borderCol = dark ? wxColour(82, 82, 82)    : wxColour(122, 122, 122);

        auto* s = new wxBoxSizer(wxVERTICAL);
        s->Add(new wxStaticText(this, wxID_ANY, wxString::Format("Line number (1 - %d):", maxLine)), 0, wxALL, 10);

        m_fieldPanel = new wxPanel(this, wxID_ANY);
        m_fieldPanel->SetBackgroundColour(m_fieldBg);
        m_text = new wxTextCtrl(m_fieldPanel, wxID_ANY, wxString::Format("%d", cur), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        m_text->SetBackgroundColour(m_fieldBg); m_text->SetForegroundColour(m_fieldFg);
        const int fh = m_text->GetBestSize().GetHeight();
        auto* fhs = new wxBoxSizer(wxHORIZONTAL);
        fhs->Add(m_text, 1, wxALIGN_CENTRE_VERTICAL | wxLEFT, 6);
        fhs->AddSpacer(kArrowZone);                          // childless zone on the right; arrows are painted here
        m_fieldPanel->SetSizer(fhs);
        m_fieldPanel->SetMinSize(wxSize(130, fh + 6));

        m_fieldPanel->Bind(wxEVT_PAINT, [this](wxPaintEvent&) { paintField(); });
        m_fieldPanel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
            const wxSize sz = m_fieldPanel->GetClientSize();
            if (e.GetX() >= sz.x - kArrowZone) { bump(e.GetY() < sz.y / 2 ? +1 : -1); m_text->SetFocus(); }
            else e.Skip();
        });

        s->Add(m_fieldPanel, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);
        s->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALL | wxEXPAND, 10);
        SetSizerAndFit(s);
        m_text->SetFocus(); m_text->SelectAll();

        // themeDialog (run by the frame before ShowModal) repaints panels in the dialog's grey; put the
        // field colour back so the spinner stays one seamless box.
        Bind(wxEVT_SHOW, [this](wxShowEvent& e) {
            if (e.IsShown()) { m_fieldPanel->SetBackgroundColour(m_fieldBg); m_text->SetBackgroundColour(m_fieldBg); m_text->SetForegroundColour(m_fieldFg); m_fieldPanel->Refresh(); }
            e.Skip();
        });
    }
    int GetLine() const { long v = 0; m_text->GetValue().ToLong(&v); if (v < 1) v = 1; if (v > m_max) v = m_max; return (int)v; }
private:
    static const int kArrowZone = 20;
    void bump(int d) { int v = GetLine() + d; if (v < 1) v = 1; if (v > m_max) v = m_max; m_text->SetValue(wxString::Format("%d", v)); m_text->SetInsertionPointEnd(); }
    void paintField()
    {
        wxPaintDC dc(m_fieldPanel);
        const wxSize sz = m_fieldPanel->GetClientSize();
        dc.SetBrush(*wxTRANSPARENT_BRUSH); dc.SetPen(wxPen(m_borderCol));
        dc.DrawRectangle(0, 0, sz.x, sz.y);                       // field outline (crisp 1px)
        const int ax = sz.x - kArrowZone;
        dc.DrawLine(ax, 3, ax, sz.y - 3);                         // thin separator before the arrows
        // Antialiased fill so the up/down triangles are symmetric (plain GDI DrawPolygon drops the
        // bottom edge, which shrinks the up-triangle's base but not the down-triangle's apex).
        wxGCDC gdc(dc);
        const int cx = ax + kArrowZone / 2, mid = sz.y / 2;
        gdc.SetPen(*wxTRANSPARENT_PEN); gdc.SetBrush(wxBrush(m_fieldFg));
        const wxPoint up[3] = { { cx - 3, mid - 2 }, { cx + 3, mid - 2 }, { cx, mid - 5 } };   // ▲
        const wxPoint dn[3] = { { cx - 3, mid + 2 }, { cx + 3, mid + 2 }, { cx, mid + 5 } };   // ▼
        gdc.DrawPolygon(3, up); gdc.DrawPolygon(3, dn);
    }
    wxTextCtrl* m_text = nullptr;
    wxPanel*    m_fieldPanel = nullptr;
    wxColour    m_fieldBg, m_fieldFg, m_borderCol;
    int         m_max;
};

// Options gathered from the Find/Replace dialog and passed to the editor search engine.
struct FindOpts { wxString find, repl; bool matchCase = false, wholeWord = false, regex = false, wrap = true, forward = true, inSelection = false; };

// A modeless Notepad++-style Find dialog: a tabbed control (Find / Replace / Find in Files /
// Mark), exactly like the real app. It is UI-only: each button calls a std::function the frame
// supplies (so the dialog needn't know about NppShellFrame). opts() reads the active tab's
// controls; the find/replace text carries across tabs, like Notepad++.
class FindReplaceDialog : public wxDialog
{
public:
    enum Tab { TAB_FIND, TAB_REPLACE, TAB_FIF, TAB_MARK };
    std::function<void(const FindOpts&)> findNextCb, countCb, replaceCb, replaceAllCb, markAllCb;
    std::function<void()> clearMarksCb;
    std::function<void(const wxString&)> infoCb;   // hint for actions the wx shell can't do yet (Find in Files)

    explicit FindReplaceDialog(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, "Replace", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    {
        m_nb = new wxNotebook(this, wxID_ANY);
        for (int t = 0; t < 4; ++t) m_pages[t] = buildPage(t);
        m_nb->AddPage(m_pages[TAB_FIND]->panel,    "Find");
        m_nb->AddPage(m_pages[TAB_REPLACE]->panel, "Replace");
        m_nb->AddPage(m_pages[TAB_FIF]->panel,     "Find in Files");
        m_nb->AddPage(m_pages[TAB_MARK]->panel,    "Mark");

        auto* top = new wxBoxSizer(wxVERTICAL);
        top->Add(m_nb, 1, wxEXPAND | wxALL, 6);
        m_status = new wxStaticText(this, wxID_ANY, " ");   // result line ("Replaced N occurrences"), like N++
        top->Add(m_status, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
        SetSizerAndFit(top);
        m_nb->SetSelection(TAB_REPLACE);
        SetTitle(tabTitle(TAB_REPLACE));

        m_nb->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& e) {
            carryText(e.GetOldSelection(), e.GetSelection());   // keep the typed text when switching tabs
            SetTitle(tabTitle(e.GetSelection()));
            e.Skip();
        });
        Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& k) {
            if      (k.GetKeyCode() == WXK_RETURN) primaryAction();
            else if (k.GetKeyCode() == WXK_ESCAPE) Hide();
            else k.Skip();
        });
    }

    FindOpts opts() const
    {
        const PageCtrls* p = m_pages[m_nb->GetSelection()];
        FindOpts o;
        o.find      = p->find ? p->find->GetValue() : wxString();
        o.repl      = p->repl ? p->repl->GetValue() : wxString();
        o.matchCase = p->caseC && p->caseC->IsChecked();
        o.wholeWord = p->word  && p->word->IsChecked();
        o.wrap      = p->wrap  && p->wrap->IsChecked();
        o.forward   = !(p->backward && p->backward->IsChecked());
        const int mode = p->mode ? p->mode->GetSelection() : 0;   // 0 Normal, 1 Extended, 2 Regex
        o.regex = (mode == 2);
        o.inSelection = p->inSel && p->inSel->IsChecked();
        if (mode == 1) { unescapeExtended(o.find); unescapeExtended(o.repl); }   // search the literal bytes
        return o;
    }
    void prime(const wxString& findText, bool focusReplace)
    {
        const int t = focusReplace ? TAB_REPLACE : TAB_FIND;
        m_nb->SetSelection(t);
        SetTitle(tabTitle(t));
        PageCtrls* p = m_pages[t];
        if (!findText.empty() && p->find) p->find->SetValue(findText);
        if (focusReplace && p->repl) p->repl->SetFocus();
        else if (p->find) { p->find->SetFocus(); p->find->SelectAll(); }
    }
    // Show a result message in the dialog (e.g. "Replaced 3 occurrences"), like Notepad++.
    void setResult(const wxString& msg) { if (m_status) m_status->SetLabel(msg); }

private:
    struct PageCtrls {
        wxPanel*    panel = nullptr;
        wxComboBox *find = nullptr, *repl = nullptr, *filters = nullptr, *dir = nullptr;
        wxCheckBox *word = nullptr, *caseC = nullptr, *wrap = nullptr, *backward = nullptr, *inSel = nullptr;
        wxRadioBox *mode = nullptr;   // Search Mode: Normal / Extended / Regular expression
    };

    static const wxString& tabTitle(int t)
    {
        static const wxString n[4] = { "Find", "Replace", "Find in Files", "Mark" };
        return n[(t >= 0 && t < 4) ? t : 0];
    }

    // Translate Extended-mode escapes (\n \r \t \0 \\ \xHH) to their literal bytes, so a plain
    // (non-regex) search matches them - exactly Notepad++'s "Extended" search mode.
    static void unescapeExtended(wxString& s)
    {
        wxString out;
        for (size_t i = 0; i < s.length(); ++i)
        {
            const wxChar c = s[i];
            if (c != '\\' || i + 1 >= s.length()) { out += c; continue; }
            const wxChar n = s[++i];
            switch (n)
            {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case '0': out += static_cast<wxChar>(0); break;
                case '\\': out += '\\'; break;
                case 'x': case 'X': {
                    auto isHex = [](wxChar h){ return (h>='0'&&h<='9')||(h>='a'&&h<='f')||(h>='A'&&h<='F'); };
                    wxString hex;
                    while (hex.length() < 2 && i + 1 < s.length() && isHex(s[i + 1])) hex += s[++i];
                    long v = 0;
                    if (!hex.empty() && hex.ToLong(&v, 16)) out += static_cast<wxChar>(v);
                    else { out += '\\'; out += n; }
                    break;
                }
                default: out += '\\'; out += n; break;
            }
        }
        s = out;
    }

    PageCtrls* buildPage(int t)
    {
        auto* pc = new PageCtrls();
        auto* panel = new wxPanel(m_nb);
        pc->panel = panel;
        auto* s = new wxBoxSizer(wxVERTICAL);

        // ---- text fields (Find what / Replace with / FiF filters+directory) ----
        auto* grid = new wxFlexGridSizer(2, 6, 8);
        grid->AddGrowableCol(1);
        grid->Add(new wxStaticText(panel, wxID_ANY, "Find what :"), 0, wxALIGN_CENTRE_VERTICAL);
        pc->find = new wxComboBox(panel, wxID_ANY, "", wxDefaultPosition, wxSize(330, -1));
        grid->Add(pc->find, 1, wxEXPAND);
        if (t == TAB_REPLACE || t == TAB_FIF) {
            grid->Add(new wxStaticText(panel, wxID_ANY, "Replace with :"), 0, wxALIGN_CENTRE_VERTICAL);
            pc->repl = new wxComboBox(panel, wxID_ANY, "", wxDefaultPosition, wxSize(330, -1));
            grid->Add(pc->repl, 1, wxEXPAND);
        }
        if (t == TAB_FIF) {
            grid->Add(new wxStaticText(panel, wxID_ANY, "Filters :"), 0, wxALIGN_CENTRE_VERTICAL);
            pc->filters = new wxComboBox(panel, wxID_ANY, "*.*", wxDefaultPosition, wxSize(330, -1));
            grid->Add(pc->filters, 1, wxEXPAND);
            grid->Add(new wxStaticText(panel, wxID_ANY, "Directory :"), 0, wxALIGN_CENTRE_VERTICAL);
            pc->dir = new wxComboBox(panel, wxID_ANY, "", wxDefaultPosition, wxSize(330, -1));
            grid->Add(pc->dir, 1, wxEXPAND);
        }
        s->Add(grid, 0, wxEXPAND | wxALL, 8);

        // ---- options (left) + action buttons (right) ----
        auto* mid = new wxBoxSizer(wxHORIZONTAL);
        auto* opt = new wxBoxSizer(wxVERTICAL);
        pc->word  = new wxCheckBox(panel, wxID_ANY, "Match &whole word only");
        pc->caseC = new wxCheckBox(panel, wxID_ANY, "Match &case");
        pc->wrap  = new wxCheckBox(panel, wxID_ANY, "Wra&p around"); pc->wrap->SetValue(true);
        opt->Add(pc->word, 0, wxALL, 3);
        opt->Add(pc->caseC, 0, wxALL, 3);
        if (t == TAB_FIND || t == TAB_REPLACE) {
            pc->backward = new wxCheckBox(panel, wxID_ANY, "&Backward direction");
            opt->Add(pc->backward, 0, wxALL, 3);
        }
        opt->Add(pc->wrap, 0, wxALL, 3);
        if (t == TAB_REPLACE || t == TAB_MARK) {
            pc->inSel = new wxCheckBox(panel, wxID_ANY, "In se&lection");
            opt->Add(pc->inSel, 0, wxALL, 3);
        }
        const wxString modes[3] = { "&Normal", "E&xtended (\\n, \\r, \\t, \\0, \\x...)", "Re&gular expression" };
        pc->mode = new wxRadioBox(panel, wxID_ANY, "Search Mode", wxDefaultPosition, wxDefaultSize, 3, modes, 1, wxRA_SPECIFY_COLS);
        opt->Add(pc->mode, 0, wxALL | wxEXPAND, 3);
        mid->Add(opt, 1, wxEXPAND | wxRIGHT, 12);

        auto* col = new wxBoxSizer(wxVERTICAL);
        auto mk = [&](const wxString& lbl, std::function<void()> act) {
            auto* b = new wxButton(panel, wxID_ANY, lbl, wxDefaultPosition, wxSize(150, -1));
            b->Bind(wxEVT_BUTTON, [act](wxCommandEvent&) { act(); });
            col->Add(b, 0, wxEXPAND | wxBOTTOM, 5);
        };
        if (t == TAB_FIND) {
            mk("Find Next", [this] { pushHistory(); if (findNextCb) findNextCb(opts()); });
            mk("Count",     [this] { pushHistory(); if (countCb)    countCb(opts()); });
            mk("Find All",  [this] { pushHistory(); if (markAllCb)  markAllCb(opts()); });
        } else if (t == TAB_REPLACE) {
            mk("Find Next",   [this] { pushHistory(); if (findNextCb)   findNextCb(opts()); });
            mk("Replace",     [this] { pushHistory(); if (replaceCb)    replaceCb(opts()); });
            mk("Replace All", [this] { pushHistory(); if (replaceAllCb) replaceAllCb(opts()); });
        } else if (t == TAB_FIF) {
            mk("Find All",         [this] { if (infoCb) infoCb("Find in Files"); });
            mk("Replace in Files", [this] { if (infoCb) infoCb("Replace in Files"); });
        } else { // TAB_MARK
            mk("Mark All",    [this] { pushHistory(); if (markAllCb) markAllCb(opts()); });
            mk("Clear Marks", [this] { if (clearMarksCb) clearMarksCb(); });
        }
        mk("Close", [this] { Hide(); });
        mid->Add(col, 0, wxALIGN_TOP);
        s->Add(mid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        panel->SetSizerAndFit(s);
        return pc;
    }

    // Run the active tab's default (Enter-key) action.
    void primaryAction()
    {
        switch (m_nb->GetSelection()) {
            case TAB_FIND:
            case TAB_REPLACE: pushHistory(); if (findNextCb) findNextCb(opts()); break;
            case TAB_MARK:    pushHistory(); if (markAllCb)  markAllCb(opts());  break;
            case TAB_FIF:     if (infoCb)     infoCb("Find in Files"); break;
        }
    }
    // Remember the searched text in the combo dropdowns (most-recent first, de-duplicated), like N++.
    void pushHistory()
    {
        PageCtrls* a = m_pages[m_nb->GetSelection()];
        if (a->find && !a->find->GetValue().empty()) addToCombo(a->find, a->find->GetValue());
        if (a->repl && !a->repl->GetValue().empty()) addToCombo(a->repl, a->repl->GetValue());
    }
    static void addToCombo(wxComboBox* c, const wxString& s)
    {
        const int idx = c->FindString(s, true);
        if (idx != wxNOT_FOUND) c->Delete(idx);
        c->Insert(s, 0);
        while (c->GetCount() > 20) c->Delete(c->GetCount() - 1);
        c->SetValue(s);   // Insert/Delete clears the edit field; restore the typed text
    }
    // Carry the find/replace text from the previous tab to the one now shown.
    void carryText(int oldIdx, int newIdx)
    {
        if (oldIdx < 0 || newIdx < 0 || oldIdx >= 4 || newIdx >= 4) return;
        const PageCtrls* a = m_pages[oldIdx];
        PageCtrls* b = m_pages[newIdx];
        if (a->find && b->find && !a->find->GetValue().empty()) b->find->SetValue(a->find->GetValue());
        if (a->repl && b->repl && !a->repl->GetValue().empty()) b->repl->SetValue(a->repl->GetValue());
    }

    wxNotebook*   m_nb = nullptr;
    wxStaticText* m_status = nullptr;
    PageCtrls*    m_pages[4] = {};
};

// Accept files dropped onto the frame's non-editor areas (tab strip, margins). Drops over the editor
// itself are handled by Scintilla's SCN_URIDROPPED. The callback opens each dropped file.
class FileDrop : public wxFileDropTarget
{
public:
    explicit FileDrop(std::function<void(const wxArrayString&)> cb) : m_cb(std::move(cb)) {}
    bool OnDropFiles(wxCoord, wxCoord, const wxArrayString& files) override { if (m_cb) m_cb(files); return true; }
private:
    std::function<void(const wxArrayString&)> m_cb;
};

class NppShellFrame : public wxFrame
{
public:
    explicit NppShellFrame(bool dark)
        : wxFrame(nullptr, wxID_ANY, "new 1 - Notepad++", wxDefaultPosition, wxSize(1100, 720)),
          m_timer(this, myID_TIMER)
    {
        m_dark = dark;          // theme is fixed for this process (restart-to-apply)
        loadTheme();            // parse Notepad++'s stylers.model.xml / DarkModeDefault.xml for exact colours
        { long z = 0; wxConfigBase::Get()->Read("Zoom", &z, 0L); m_zoom = static_cast<int>(z); }   // restore zoom
        setAppIcon();
        buildEditor();
        buildMenuBar();
        buildToolBar();
        buildStatusBar();

#ifdef __WXMSW__
        m_npp = { static_cast<HWND>(GetHandle()), m_sci, nullptr };
        ::SetWindowSubclass(static_cast<HWND>(GetHandle()), FrameSubclassProc, 1, reinterpret_cast<DWORD_PTR>(m_sci));
        g_nppm = [this](UINT m, WPARAM w, LPARAM l, LRESULT& o) { return handleNppm(m, w, l, o); };   // serve plugin NPPM_* messages
        loadPlugins();   // load Win32 plugins from plugins/, hand them our HWNDs, build the Plugins menu
#endif

        Bind(wxEVT_MENU, &NppShellFrame::onCommand, this);          // one dispatcher for all menu+toolbar ids
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { updateStatus(); updateUiState(); }, myID_TIMER);
        g_editorContextMenu = [this](int sx, int sy) { showEditorContext(sx, sy); };   // editor right-click menu
        g_openDropped = [this](const wxString& s) { openDroppedPaths(s); };            // files dropped on the editor
        g_onZoom = [this](int z) { onZoomChanged(z); };                                // sync + persist zoom
        SetDropTarget(new FileDrop([this](const wxArrayString& fs) { for (const auto& f : fs) openPath(f); }));
        Bind(wxEVT_CLOSE_WINDOW, &NppShellFrame::onCloseWindow, this);                 // prompt to save on exit
        m_timer.Start(150);
        applyTheme(m_dark);     // style for the mode this process was launched in
        updateStatus();
        updateUiState();
    }

    // Open a file given on the command line (Notepad++ opens cmdline paths as tabs).
    void openPath(const wxString& p) { if (wxFileExists(p)) addDocument(p, wxFileNameFromPath(p)); }
    // Open file(s) from a Scintilla SCN_URIDROPPED payload (newline-separated; may be file:// URIs).
    void openDroppedPaths(const wxString& data)
    {
        wxString cur;
        auto flush = [&] {
            wxString f = cur; cur.clear();
            f.Trim(true).Trim(false);
            if      (f.StartsWith("file:///")) f = f.Mid(8);
            else if (f.StartsWith("file://"))  f = f.Mid(7);
            f.Replace("%20", " ");
            if (!f.empty()) openPath(f);
        };
        for (wxUniChar c : data) { if (c == '\n' || c == '\r') flush(); else cur += c; }
        flush();
    }

    // Reopen the files saved by the previous (theme-restart) instance, once.
    void restoreSession()
    {
        auto* cfg = wxConfigBase::Get();
        bool pending = false;
        if (!cfg->Read("Session/Pending", &pending, false) || !pending) return;
        cfg->Write("Session/Pending", false); cfg->Flush();
        long count = 0, active = -1;
        cfg->Read("Session/Count", &count, 0L);
        cfg->Read("Session/Active", &active, -1L);
        EditorPage* initial = activePage();        // the startup "new 1"
        EditorPage* activePg = nullptr;
        int restored = 0;
        for (int i = 0; i < (int)count; ++i)
        {
            wxString path;
            if (!cfg->Read(wxString::Format("Session/File%d", i), &path) || path.empty() || !wxFileExists(path)) continue;
            EditorPage* pg = addDocument(path, wxFileNameFromPath(path));
            if (i == (int)active) activePg = pg;
            ++restored;
        }
        if (restored > 0 && initial && initial->path.empty() && (int)m_tabs->GetPageCount() > restored)
        {
            const int idx = m_tabs->GetPageIndex(initial);          // drop the redundant empty "new 1"
            if (idx != wxNOT_FOUND) m_tabs->DeletePage(idx);
        }
        if (activePg) { const int idx = m_tabs->GetPageIndex(activePg); if (idx != wxNOT_FOUND) m_tabs->SetSelection(idx); }
    }

private:
    // ----- Scintilla helpers --------------------------------------------
    // Editor message gateway - routes through wxSTC's portable SendMsg (was SendMessageW to the raw HWND).
    sptr_t sci(UINT m, uptr_t w = 0, sptr_t l = 0)
    { return m_stc ? static_cast<sptr_t>(m_stc->SendMsg(static_cast<int>(m), static_cast<wxUIntPtr>(w), static_cast<wxIntPtr>(l))) : 0; }

    wxString getAllText()
    {
        const int len = static_cast<int>(sci(SCI_GETLENGTH));
        std::string b(static_cast<size_t>(len) + 1, '\0');
        sci(SCI_GETTEXT, len + 1, reinterpret_cast<sptr_t>(&b[0])); b.resize(len);
        return wxString::FromUTF8(b.data(), b.size());
    }
    void setAllText(const wxString& s)
    {
        const wxScopedCharBuffer u = s.ToUTF8();
        sci(SCI_BEGINUNDOACTION);
        sci(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(u.data()));
        sci(SCI_ENDUNDOACTION);
    }
    std::string getSelUtf8()
    {
        const int a = static_cast<int>(sci(SCI_GETSELECTIONSTART)), b = static_cast<int>(sci(SCI_GETSELECTIONEND));
        if (b <= a) return {};
        std::string s(static_cast<size_t>(b - a) + 1, '\0');
        sci(SCI_GETSELTEXT, 0, reinterpret_cast<sptr_t>(&s[0])); s.resize(b - a);
        return s;
    }
    void transformSel(const std::function<void(std::string&)>& fn)
    {
        std::string s = getSelUtf8();
        if (s.empty()) return;
        fn(s);
        sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>(s.c_str()));
    }

    // ----- application icon ---------------------------------------------
    void setAppIcon()
    {
        wxBitmapBundle bb = wxBitmapBundle::FromSVG(APP_ICON_SVG, wxSize(256, 256));
        wxIconBundle icons;
        for (int sz : { 16, 20, 24, 32, 48, 64, 128, 256 })
        {
            const wxBitmap bmp = bb.GetBitmap(wxSize(sz, sz));
            if (!bmp.IsOk()) continue;
            wxIcon ic; ic.CopyFromBitmap(bmp); icons.AddIcon(ic);
        }
        if (icons.GetIconCount() > 0) SetIcons(icons);
    }

    // ----- editor + tabs (one native Scintilla per document) -------------
    void buildEditor()
    {
        m_tabs = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                   wxAUI_NB_TOP | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS |
                                   wxAUI_NB_CLOSE_ON_ALL_TABS | wxAUI_NB_MIDDLE_CLICK_CLOSE);
        m_tabs->Bind(wxEVT_AUINOTEBOOK_PAGE_CLOSE,   &NppShellFrame::onPageClose,   this);
        m_tabs->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &NppShellFrame::onPageChanged, this);
        m_tabs->Bind(wxEVT_AUINOTEBOOK_TAB_RIGHT_UP, &NppShellFrame::onTabContext,  this);
        // ONE persistent editor view (wxStyledTextCtrl, like N++'s ScintillaEditView). Each tab is a
        // Scintilla Document; switching tabs swaps the document behind this view via SCI_SETDOCPOINTER,
        // so the editor handle a plugin cached at setInfo() stays valid across tabs. wxSTC bundles its
        // own Scintilla+Lexilla (cross-platform); on Windows its native HWND is what plugins target.
        m_stc = new wxStyledTextCtrl(m_tabs, wxID_ANY);
        m_stc->Hide();                                       // activateBuffer mounts it onto the active page
#ifdef __WXMSW__
        m_sci = static_cast<HWND>(m_stc->GetHandle());       // the native Scintilla HWND (plugins/NPPM use it)
#endif
        g_view = m_stc;                     // so EditorPage::~EditorPage can release its Document (portable)
        setupScintilla();                   // view-level setup once (margins, markers, styles, options)
#ifdef __WXMSW__
        ::SetWindowSubclass(m_sci, SciHwndProc, 3, 0);   // bridge plugin SendMessage(SCI_*) -> wxSTC SendMsg
        g_sciForward = [this](UINT m, WPARAM w, LPARAM l) { return sci(m, static_cast<uptr_t>(w), static_cast<sptr_t>(l)); };
#endif
        bindEditorEvents();                 // wxEVT_STC_* -> editor behaviours + plugin beNotified (was PageSubclassProc)
        addDocument("", nextNewName());     // initial "new 1" buffer
        buildTabCaptionButtons();           // +/v/x buttons at the right end of the tab strip
        // wxAuiManager: the tab area is the center; plugin docking panels attach around it.
        m_aui.SetManagedWindow(this);
        m_aui.AddPane(m_tabs, wxAuiPaneInfo().CenterPane().PaneBorder(false));
        m_aui.Update();
    }

    // wxStyledTextCtrl fires wxEVT_STC_* (not Win32 WM_NOTIFY), so the N++ editor behaviours and the
    // plugin beNotified() forwarding bind here instead of in a page subclass.
    void bindEditorEvents()
    {
        m_stc->Bind(wxEVT_STC_CHARADDED,        &NppShellFrame::onStcCharAdded,   this);
        m_stc->Bind(wxEVT_STC_UPDATEUI,         &NppShellFrame::onStcUpdateUI,    this);
        m_stc->Bind(wxEVT_STC_DOUBLECLICK,      &NppShellFrame::onStcDoubleClick, this);
        m_stc->Bind(wxEVT_STC_MARGINCLICK,      &NppShellFrame::onStcMarginClick, this);
        m_stc->Bind(wxEVT_STC_ZOOM,             &NppShellFrame::onStcZoom,        this);
        m_stc->Bind(wxEVT_STC_MODIFIED,         &NppShellFrame::onStcModified,    this);
        m_stc->Bind(wxEVT_STC_URIDROPPED,       &NppShellFrame::onStcUriDropped,  this);
        m_stc->Bind(wxEVT_STC_SAVEPOINTREACHED, [this](wxStyledTextEvent& e) { forwardScn(SCN_SAVEPOINTREACHED, e); e.Skip(); });
        m_stc->Bind(wxEVT_STC_SAVEPOINTLEFT,    [this](wxStyledTextEvent& e) { forwardScn(SCN_SAVEPOINTLEFT, e);    e.Skip(); });
        m_stc->Bind(wxEVT_CONTEXT_MENU,         &NppShellFrame::onStcContextMenu, this);
    }
    // Rebuild a Win32 SCNotification from the wx event and hand it to every plugin's beNotified().
    void forwardScn(int code, wxStyledTextEvent& e)
    {
#ifdef __WXMSW__
        if (!g_pluginNotify) return;
        SCNotification scn{};
        scn.nmhdr.hwndFrom = m_sci;
        scn.nmhdr.idFrom = 0;
        scn.nmhdr.code = static_cast<unsigned int>(code);
        scn.position = e.GetPosition();
        scn.ch = e.GetKey();
        scn.modificationType = e.GetModificationType();
        scn.length = e.GetLength();
        scn.linesAdded = e.GetLinesAdded();
        scn.line = e.GetLine();
        scn.margin = e.GetMargin();
        const std::string text = e.GetString().utf8_string();
        if (!text.empty()) scn.text = text.c_str();
        g_pluginNotify(&scn);
#else
        (void)code; (void)e;
#endif
    }
    void onStcCharAdded(wxStyledTextEvent& e)
    {
        const int ch = e.GetKey();
        if (ch == '\n' || ch == '\r') autoIndentOnNewline(m_stc);
        else if (ch == '}')           dedentCloseBrace(m_stc);
        forwardScn(SCN_CHARADDED, e);
        e.Skip();
    }
    void onStcUpdateUI(wxStyledTextEvent& e)
    {
        updateBraceMatch(m_stc);
        if (g_smartActive && g_smartSci == m_stc && sci(SCI_GETSELECTIONEMPTY)) clearSmart(m_stc);
        forwardScn(SCN_UPDATEUI, e);
        e.Skip();
    }
    void onStcDoubleClick(wxStyledTextEvent& e) { smartHighlight(m_stc); forwardScn(SCN_DOUBLECLICK, e); e.Skip(); }
    void onStcMarginClick(wxStyledTextEvent& e)
    {
        if (e.GetMargin() == 1)   // the bookmark margin
        {
            const int line = static_cast<int>(sci(SCI_LINEFROMPOSITION, e.GetPosition()));
            if (sci(SCI_MARKERGET, line) & (1 << MARK_BOOKMARK)) sci(SCI_MARKERDELETE, line, MARK_BOOKMARK);
            else                                                 sci(SCI_MARKERADD, line, MARK_BOOKMARK);
        }
        forwardScn(SCN_MARGINCLICK, e);
        e.Skip();
    }
    void onStcZoom(wxStyledTextEvent& e)      { if (g_onZoom) g_onZoom(static_cast<int>(sci(SCI_GETZOOM))); e.Skip(); }
    void onStcModified(wxStyledTextEvent& e)  { forwardScn(SCN_MODIFIED, e); e.Skip(); }
    void onStcUriDropped(wxStyledTextEvent& e){ if (g_openDropped && !e.GetString().empty()) g_openDropped(e.GetString()); e.Skip(); }
    void onStcContextMenu(wxContextMenuEvent& e)
    {
        wxPoint p = e.GetPosition();
        if (p == wxDefaultPosition) p = ::wxGetMousePosition();   // keyboard (Shift+F10): use the pointer
        if (g_editorContextMenu) g_editorContextMenu(p.x, p.y);
    }

    // Notepad++'s caption buttons (new / document-list / close), overlaid at the right of the tab strip.
    void buildTabCaptionButtons()
    {
        m_capBar = new wxPanel(m_tabs);
        m_capBar->SetBackgroundColour(m_dark ? wxColour(32, 32, 32) : wxColour(240, 240, 240));
        auto* s = new wxBoxSizer(wxHORIZONTAL);
        auto mkBtn = [&](const char* path, const wxString& tip, std::function<void()> act) {
            auto* b = new wxBitmapButton(m_capBar, wxID_ANY, captionIcon(path), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxBU_EXACTFIT);
            b->SetBackgroundColour(m_capBar->GetBackgroundColour());
            b->SetToolTip(tip);
            b->Bind(wxEVT_BUTTON, [act](wxCommandEvent&) { act(); });
            s->Add(b, 0, wxALIGN_CENTRE_VERTICAL | wxLEFT, 2);
        };
        mkBtn("M8 5 V11 M5 8 H11",                       "New",            [this] { doNew(); });
        mkBtn("M3.5 6.5 L8 11 L12.5 6.5",                "Open documents", [this] { onDocList(); });
        mkBtn("M4.5 4.5 L11.5 11.5 M11.5 4.5 L4.5 11.5", "Close current",  [this] { closeActive(); });
        m_capBar->SetSizerAndFit(s);
        m_tabs->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { positionCapBar(); e.Skip(); });
        positionCapBar();
    }
    void positionCapBar()
    {
        if (!m_capBar || !m_tabs) return;
        const wxSize sz = m_tabs->GetClientSize();
        const wxSize bs = m_capBar->GetBestSize();
        int y = (m_tabs->GetTabCtrlHeight() - bs.GetHeight()) / 2; if (y < 0) y = 0;
        m_capBar->SetSize(sz.GetWidth() - bs.GetWidth() - 6, y, bs.GetWidth(), bs.GetHeight());
        m_capBar->Raise();
    }

    EditorPage* activePage() const
    {
        const int s = m_tabs ? m_tabs->GetSelection() : wxNOT_FOUND;
        return s == wxNOT_FOUND ? nullptr : static_cast<EditorPage*>(m_tabs->GetPage(s));
    }
    wxString nextNewName() { return wxString::Format("new %d", ++m_newCount); }

    // Create a new document tab (panel + its own native Scintilla) and make it active.
    EditorPage* addDocument(const wxString& path, const wxString& title)
    {
        auto* page = new EditorPage(m_tabs);
        page->Bind(wxEVT_SIZE, [this, page](wxSizeEvent& e) {
            if (m_stc && m_stc->GetParent() == page) m_stc->SetSize(page->GetClientSize());   // only the page hosting the view resizes it
            e.Skip();
        });
        page->doc = sci(SCI_CREATEDOCUMENT, 0, SC_DOCUMENTOPTION_TEXT_LARGE);   // the buffer owns one ref (N++ newEmptyDocument)
        page->path = path; page->title = title;
        m_tabs->AddPage(page, title, true);    // selecting it fires PAGE_CHANGED -> activateBuffer
        activateBuffer(page);                  // ensure it (the first AddPage may not fire PAGE_CHANGED); idempotent
        if (!path.empty()) { loadFile(path); addToMRU(path); } else { sci(SCI_SETSAVEPOINT); setLexerForFile(""); }
        SetTitle(title + " - Notepad++");
        updateStatus();
        return page;
    }

    // Per-editor Scintilla configuration: margins, font, options, theme, scrollbars.
    void setupScintilla()
    {
        sci(SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);   // right-justified line numbers
        sci(SCI_SETMARGINWIDTHN, 1, 14);                 // bookmark/symbol margin (like Notepad++)
        sci(SCI_SETMARGINSENSITIVEN, 1, 1);
        sci(SCI_MARKERDEFINE, MARK_BOOKMARK, SC_MARK_BOOKMARK);
        sci(SCI_MARKERSETFORE, MARK_BOOKMARK, 0x707000);   // teal outline (BGR)
        sci(SCI_MARKERSETBACK, MARK_BOOKMARK, 0xC0C000);   // cyan fill   (BGR)
        sci(SCI_INDICSETSTYLE, MARK_INDIC, INDIC_ROUNDBOX);   // "Mark All" / found-highlight indicator
        sci(SCI_INDICSETFORE, MARK_INDIC, 0x00C800);
        sci(SCI_INDICSETALPHA, MARK_INDIC, 80);
        sci(SCI_INDICSETSTYLE, SMART_INDIC, INDIC_ROUNDBOX);  // smart-highlight (double-click a word)
        sci(SCI_INDICSETFORE, SMART_INDIC, 0x00C800);
        sci(SCI_INDICSETALPHA, SMART_INDIC, 70);
        sci(SCI_INDICSETOUTLINEALPHA, SMART_INDIC, 160);
        sci(SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<sptr_t>("Consolas"));
        sci(SCI_STYLESETSIZE, STYLE_DEFAULT, 11);
        sci(SCI_STYLECLEARALL);
        sci(SCI_SETTABWIDTH, 4);
        sci(SCI_SETSCROLLWIDTH, 1);
        sci(SCI_SETSCROLLWIDTHTRACKING, 1);
        sci(SCI_SETENDATLASTLINE, 1);
        sci(SCI_USEPOPUP, SC_POPUP_NEVER);   // suppress Scintilla's light popup; we show our own themed menu
        // Column (Alt+drag) + multi-caret (Ctrl+click) selection, typing into all - exactly Notepad++'s setup.
        sci(SCI_SETMULTIPLESELECTION, 1);
        sci(SCI_SETADDITIONALSELECTIONTYPING, 1);
        sci(SCI_SETVIRTUALSPACEOPTIONS, SCVS_RECTANGULARSELECTION);
        sci(SCI_SETMULTIPASTE, SC_MULTIPASTE_EACH);
        applyEditorTheme(m_dark);
        setupFolding();
#ifdef __WXMSW__
        ::SetWindowTheme(m_sci, m_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);   // dark/light scrollbars
#endif
        sci(SCI_SETZOOM, m_zoom);   // new tabs inherit the current (persisted) zoom level
        updateLineMargin();
    }
    // Code folding: margin 2 with a box-tree, automatic fold on margin click (like Notepad++).
    void setupFolding()
    {
        sci(SCI_SETMARGINTYPEN, 2, SC_MARGIN_SYMBOL);
        sci(SCI_SETMARGINMASKN, 2, SC_MASK_FOLDERS);
        sci(SCI_SETMARGINWIDTHN, 2, 14);
        sci(SCI_SETMARGINSENSITIVEN, 2, 1);
        // Exactly as Notepad++ (ScintillaEditView::getFoldColor + defineMarker):
        //   fold-margin background = "Fold margin" bg / fg
        //   marker FORE = "Fold" *bgColor*, marker BACK = "Fold" *fgColor*  (Notepad++ SWAPS them)
        //   marker BACKSELECTED = "Fold active" fg, plus SCI_MARKERENABLEHIGHLIGHT.
        auto G = [&](const char* n) -> std::pair<int,int> {
            auto it = m_theme.global.find(n); return it == m_theme.global.end() ? std::make_pair(-1,-1) : it->second; };
        const int defBg = (int)sci(SCI_STYLEGETBACK, STYLE_DEFAULT);
        const auto lnm = G("Line number margin"), fold = G("Fold"), foldActive = G("Fold active");
        const int gutterBg = lnm.second >= 0 ? lnm.second : defBg;                // one-tone gutter: match the line-number margin
        sci(SCI_SETFOLDMARGINCOLOUR, 1, gutterBg);
        sci(SCI_SETFOLDMARGINHICOLOUR, 1, gutterBg);                              // COLOUR == HICOLOUR -> no checkerboard
        const int markFore   = fold.second >= 0 ? fold.second : gutterBg;                    // = "Fold" bg  (swap)
        const int markBack   = fold.first  >= 0 ? fold.first  : 0x808080;                    // = "Fold" fg  (swap)
        const int markActive = foldActive.first >= 0 ? foldActive.first : markBack;          // = "Fold active" fg
        const int markers[7] = { SC_MARKNUM_FOLDEROPEN, SC_MARKNUM_FOLDER, SC_MARKNUM_FOLDERSUB, SC_MARKNUM_FOLDERTAIL,
                                 SC_MARKNUM_FOLDEREND, SC_MARKNUM_FOLDEROPENMID, SC_MARKNUM_FOLDERMIDTAIL };
        const int symbols[7] = { SC_MARK_BOXMINUS, SC_MARK_BOXPLUS, SC_MARK_VLINE, SC_MARK_LCORNER,   // box-tree (N++ default)
                                 SC_MARK_BOXPLUSCONNECTED, SC_MARK_BOXMINUSCONNECTED, SC_MARK_TCORNER };
        for (int i = 0; i < 7; ++i)
        {
            sci(SCI_MARKERDEFINE, markers[i], symbols[i]);
            sci(SCI_MARKERSETFORE, markers[i], markFore);
            sci(SCI_MARKERSETBACK, markers[i], markBack);
            sci(SCI_MARKERSETBACKSELECTED, markers[i], markActive);
        }
        sci(SCI_MARKERENABLEHIGHLIGHT, 1);
        sci(SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_SHOW | SC_AUTOMATICFOLD_CLICK | SC_AUTOMATICFOLD_CHANGE);
        sci(SCI_SETFOLDFLAGS, SC_FOLDFLAG_LINEAFTER_CONTRACTED);
    }
    void foldCurrent(bool contract)
    {
        const int ln  = (int)sci(SCI_LINEFROMPOSITION, sci(SCI_GETCURRENTPOS));
        const int lvl = (int)sci(SCI_GETFOLDLEVEL, ln);
        const int hdr = (lvl & SC_FOLDLEVELHEADERFLAG) ? ln : (int)sci(SCI_GETFOLDPARENT, ln);
        if (hdr < 0) return;
        if ((sci(SCI_GETFOLDEXPANDED, hdr) != 0) == contract) sci(SCI_TOGGLEFOLD, hdr);
    }
    void foldToLevel(int n, bool contract)   // n = 1..8 relative depth
    {
        const int target = SC_FOLDLEVELBASE + (n - 1);
        const int lines = (int)sci(SCI_GETLINECOUNT);
        for (int l = 0; l < lines; ++l)
        {
            const int lvl = (int)sci(SCI_GETFOLDLEVEL, l);
            if (!(lvl & SC_FOLDLEVELHEADERFLAG)) continue;
            if ((lvl & SC_FOLDLEVELNUMBERMASK) != target) continue;
            if ((sci(SCI_GETFOLDEXPANDED, l) != 0) == contract) sci(SCI_TOGGLEFOLD, l);
        }
    }

    void onPageChanged(wxAuiNotebookEvent& e)
    {
        if (auto* p = activePage()) activateBuffer(p);
        e.Skip();
    }
    // Mount the single view on the active page and swap to its document - Notepad++'s activateBuffer.
    void activateBuffer(EditorPage* p)
    {
        if (!p || !m_stc) return;
        if (m_stc->GetParent() == p && sci(SCI_GETDOCPOINTER) == p->doc)
        { m_stc->SetFocus(); return; }                                      // already showing this buffer - nothing to do
        if (m_stc->GetParent() != p) m_stc->Reparent(p);                    // the one view hops onto the active page (wx-tracked)
        m_stc->SetSize(p->GetClientSize());
        m_stc->Show();
        sci(SCI_SETMODEVENTMASK, 0);                                         // quiet during the swap
        sci(SCI_SETDOCPOINTER, 0, p->doc);                                   // the magical switch
        sci(SCI_SETMODEVENTMASK, SC_MODEVENTMASKALL);
        m_path = p->path;
        setLexerForFile(p->path);                                           // re-apply lexer/styling for this doc (N++ defineDocType)
        refreshTab(p);
        updateStatus();
        m_stc->SetFocus();
    }
    // Ask to save a modified document before closing it (Save / Don't Save / Cancel), themed like the
    // rest of the app. Returns true if the caller may close the page, false if the user cancelled.
    bool confirmClose(EditorPage* p)
    {
        if (!p) return true;
        const bool dirty = (p == activePage()) ? (sci(SCI_GETMODIFY) != 0) : p->dirty;
        if (!dirty) return true;
        const int idx = m_tabs->GetPageIndex(p);
        if (idx != wxNOT_FOUND && idx != m_tabs->GetSelection()) m_tabs->SetSelection(idx);   // show the doc in question
        const wxString name = !p->path.empty() ? p->path : (p->title.empty() ? wxString("new") : p->title);

        wxDialog dlg(this, wxID_ANY, "Notepad++");
        auto* s = new wxBoxSizer(wxVERTICAL);
        s->Add(new wxStaticText(&dlg, wxID_ANY, "Save file\n" + name + " ?"), 0, wxALL, 16);
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* bSave = new wxButton(&dlg, wxID_YES, "&Save");
        auto* bNo   = new wxButton(&dlg, wxID_NO, "Do&n't Save");
        auto* bCan  = new wxButton(&dlg, wxID_CANCEL, "&Cancel");
        row->Add(bSave, 0, wxRIGHT, 6); row->Add(bNo, 0, wxRIGHT, 6); row->Add(bCan, 0);
        s->Add(row, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 12);
        bSave->Bind(wxEVT_BUTTON, [&dlg](wxCommandEvent&) { dlg.EndModal(wxID_YES); });
        bNo->Bind(wxEVT_BUTTON,   [&dlg](wxCommandEvent&) { dlg.EndModal(wxID_NO); });
        bCan->Bind(wxEVT_BUTTON,  [&dlg](wxCommandEvent&) { dlg.EndModal(wxID_CANCEL); });
        bSave->SetDefault();
        dlg.SetSizerAndFit(s); dlg.CentreOnParent();
        themeDialog(&dlg);
        const int r = dlg.ShowModal();
        if (r == wxID_CANCEL) return false;
        if (r == wxID_YES) { onSave(); return sci(SCI_GETMODIFY) == 0; }   // p is active now (SetSelection above); false if Save As cancelled
        return true;   // Don't Save -> discard
    }
    void onPageClose(wxAuiNotebookEvent& e)
    {
        if (!confirmClose(static_cast<EditorPage*>(m_tabs->GetPage(e.GetSelection())))) { e.Veto(); return; }
        if (m_tabs->GetPageCount() <= 1) { e.Veto(); resetActiveDoc(); }   // keep one document open, like Notepad++
    }
    void closeActive()
    {
        if (!confirmClose(activePage())) return;
        if (m_tabs->GetPageCount() <= 1) resetActiveDoc();
        else m_tabs->DeletePage(m_tabs->GetSelection());
    }
    void closeAll()
    {
        for (size_t i = 0; i < m_tabs->GetPageCount(); ++i)
            if (!confirmClose(static_cast<EditorPage*>(m_tabs->GetPage(i)))) return;   // cancelled -> abort
        m_tabs->Freeze();
        while (m_tabs->GetPageCount() > 1) m_tabs->DeletePage(0);
        resetActiveDoc();
        m_tabs->Thaw();
    }
    void resetActiveDoc()
    {
        sci(SCI_CLEARALL); sci(SCI_EMPTYUNDOBUFFER); sci(SCI_SETSAVEPOINT);
        if (auto* p = activePage()) { p->path.clear(); p->dirty = false; }
        m_path.clear();
        setLexerForFile("");
        setDocTitle(nextNewName());
    }
    void closeAllBut(EditorPage* keep)
    {
        for (size_t i = 0; i < m_tabs->GetPageCount(); ++i)
        {
            auto* p = static_cast<EditorPage*>(m_tabs->GetPage(i));
            if (p != keep && !confirmClose(p)) return;   // cancelled -> abort
        }
        m_tabs->Freeze();
        for (int i = (int)m_tabs->GetPageCount() - 1; i >= 0; --i)
            if (m_tabs->GetPage(i) != keep) m_tabs->DeletePage(i);
        m_tabs->Thaw();
    }
    // On window close / app exit, prompt for every modified document; a Cancel aborts the close.
    void onCloseWindow(wxCloseEvent& e)
    {
        if (e.CanVeto())   // a forced close (e.g. the theme-restart) skips prompts and just exits
            for (size_t i = 0; i < m_tabs->GetPageCount(); ++i)
                if (!confirmClose(static_cast<EditorPage*>(m_tabs->GetPage(i)))) { e.Veto(); return; }
        m_aui.UnInit();
        e.Skip();
    }
    // Show "*" on the active tab + title bar while the document has unsaved changes (like Notepad++).
    void refreshTab(EditorPage* p)
    {
        if (!p || !m_tabs) return;
        const bool dirty = (p == activePage()) ? (sci(SCI_GETMODIFY) != 0) : p->dirty;
        const wxString star = dirty ? "*" : "";
        const wxString lbl = star + p->title;                            // tab label = filename
        const int idx = m_tabs->GetPageIndex(p);
        if (idx != wxNOT_FOUND && m_tabs->GetPageText(idx) != lbl) m_tabs->SetPageText(idx, lbl);
        if (p == activePage())                                           // title bar = full path, like Notepad++
        {
            const wxString t = star + (p->path.empty() ? p->title : p->path) + " - Notepad++";
            if (GetTitle() != t) SetTitle(t);
        }
    }
    void onTabContext(wxAuiNotebookEvent& e)
    {
        if (e.GetSelection() != wxNOT_FOUND) m_tabs->SetSelection(e.GetSelection());
        wxMenu menu;
        menu.Append(IDM_FILE_CLOSE, "Close");
        menu.Append(IDM_FILE_CLOSEALL_BUT_CURRENT, "Close All BUT This");
        menu.Append(IDM_FILE_CLOSEALL, "Close All");
        menu.AppendSeparator();
        menu.Append(IDM_FILE_SAVE, "Save");
        menu.Append(IDM_FILE_SAVEAS, "Save As...");
        PopupMenu(&menu);
    }

    // Editor right-click menu (Notepad++'s default editor context menu, themed). Item ids are the
    // same IDM_* the main menu uses, so onCommand handles them; enable state mirrors the editor.
    void showEditorContext(int screenX, int screenY)
    {
        if (!m_stc) return;
        const bool hasSel = sci(SCI_GETSELECTIONEMPTY) == 0;
        wxMenu menu;
        auto add = [&](int id, const wxString& label, bool enabled) { menu.Append(id, label)->Enable(enabled); };
        add(IDM_EDIT_UNDO, "Undo\tCtrl+Z", sci(SCI_CANUNDO) != 0);
        add(IDM_EDIT_REDO, "Redo\tCtrl+Y", sci(SCI_CANREDO) != 0);
        menu.AppendSeparator();
        add(IDM_EDIT_CUT,    "Cut\tCtrl+X",  hasSel);
        add(IDM_EDIT_COPY,   "Copy\tCtrl+C", hasSel);
        add(IDM_EDIT_PASTE,  "Paste\tCtrl+V", sci(SCI_CANPASTE) != 0);
        add(IDM_EDIT_DELETE, "Delete\tDel",  hasSel);
        menu.AppendSeparator();
        add(IDM_EDIT_SELECTALL, "Select All\tCtrl+A", true);
        menu.AppendSeparator();
        add(IDM_SEARCH_TOGGLE_BOOKMARK, "Toggle Bookmark", true);
        const wxPoint pos = (screenX == -1 && screenY == -1) ? wxDefaultPosition : ScreenToClient(wxPoint(screenX, screenY));
        PopupMenu(&menu, pos);
    }

    // ----- menu bar ------------------------------------------------------
    static void placeholder(wxMenu* m) { wxMenuItem* it = m->Append(wxID_ANY, "(more items added incrementally)"); it->Enable(false); }

    void buildMenuBar()
    {
        auto* mb = new wxMenuBar;
        buildNppMainMenu(mb, myID_DARKMODE);   // full 1:1 Notepad++ menu tree (see spike/npp_menu.h)
        // Recent Files (MRU) submenu near the bottom of the File menu, backed by wxFileHistory.
        if (wxMenu* fileMenu = mb->GetMenu(0))
        {
            auto* recent = new wxMenu;
            const size_t n = fileMenu->GetMenuItemCount();
            fileMenu->Insert(n > 0 ? n - 1 : 0, wxID_ANY, "Recent &Files", recent);
            m_fileHistory.UseMenu(recent);
            auto* c = wxConfigBase::Get(); c->SetPath("/RecentFiles"); m_fileHistory.Load(*c); c->SetPath("/");
        }
        SetMenuBar(mb);
    }
    void addToMRU(const wxString& path)
    {
        m_fileHistory.AddFileToHistory(path);
        auto* c = wxConfigBase::Get(); c->SetPath("/RecentFiles"); m_fileHistory.Save(*c); c->SetPath("/"); c->Flush();
    }

    // ----- Win32 plugin host (real Notepad++ plugin ABI; Windows build) --------------------------
#ifdef __WXMSW__
    // Load each plugins/<Name>/<Name>.dll, hand it our HWNDs via setInfo(), and surface its
    // getFuncsArray() commands under the Plugins menu - exactly like Notepad++'s PluginsManager.
    void loadPlugins()
    {
        const wxString dir = wxPathOnly(wxStandardPaths::Get().GetExecutablePath()) + "\\plugins";
        if (!wxDirExists(dir)) return;
        wxDir d(dir);
        wxString sub;
        for (bool more = d.GetFirst(&sub, "", wxDIR_DIRS); more; more = d.GetNext(&sub))
        {
            const wxString dll = dir + "\\" + sub + "\\" + sub + ".dll";
            if (wxFileExists(dll)) loadOnePlugin(dll);
        }
        if (!m_plugins.empty())
        {
            g_pluginNotify = [this](SCNotification* scn) { for (auto& p : m_plugins) if (p.beNotified) p.beNotified(scn); };
            buildPluginsMenu();
            setStatus(0, wxString::Format("%zu plugin(s) loaded", m_plugins.size()));
        }
    }
    void loadOnePlugin(const wxString& path)
    {
        HMODULE lib = ::LoadLibraryW(path.wc_str());
        if (!lib) return;
        LoadedPlugin p;
        p.lib         = lib;
        p.setInfo     = reinterpret_cast<PFUNCSETINFO>(::GetProcAddress(lib, "setInfo"));
        p.getFuncs    = reinterpret_cast<PFUNCGETFUNCSARRAY>(::GetProcAddress(lib, "getFuncsArray"));
        p.beNotified  = reinterpret_cast<PBENOTIFIED>(::GetProcAddress(lib, "beNotified"));
        p.messageProc = reinterpret_cast<PMESSAGEPROC>(::GetProcAddress(lib, "messageProc"));
        auto getName  = reinterpret_cast<PFUNCGETNAME>(::GetProcAddress(lib, "getName"));
        if (!p.setInfo || !p.getFuncs || !getName) { ::FreeLibrary(lib); return; }   // not a real plugin
        p.name = getName();
        p.setInfo(m_npp);                 // hand the plugin the frame + Scintilla HWNDs
        p.funcs = p.getFuncs(&p.count);   // its menu commands
        m_plugins.push_back(p);
    }
    void buildPluginsMenu()
    {
        wxMenuBar* mb = GetMenuBar();
        if (!mb) return;
        const int mi = mb->FindMenu("Plugins");
        if (mi == wxNOT_FOUND) return;
        wxMenu* menu = mb->GetMenu(mi);
        while (menu->GetMenuItemCount() > 0) menu->Destroy(menu->FindItemByPosition(0));   // drop placeholders
        for (size_t pi = 0; pi < m_plugins.size(); ++pi)
        {
            LoadedPlugin& p = m_plugins[pi];
            auto* sub = new wxMenu;
            for (int fi = 0; fi < p.count; ++fi)
            {
                const wxString name(p.funcs[fi]._itemName);
                if (name.empty()) sub->AppendSeparator();
                else sub->Append(PLUGIN_CMD_BASE + static_cast<int>(pi) * 100 + fi, name);
            }
            menu->AppendSubMenu(sub, p.name);
        }
    }
    // Where plugins read/write their own config (Notepad++ uses plugins\Config\).
    wxString pluginsConfigDir()
    {
        const wxString d = wxPathOnly(wxStandardPaths::Get().GetExecutablePath()) + "\\plugins\\Config";
        if (!wxDirExists(d)) wxFileName::Mkdir(d, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        return d;
    }
    wxString currentWord()
    {
        if (!m_stc) return wxString();
        const int pos = static_cast<int>(sci(SCI_GETCURRENTPOS));
        const int s = static_cast<int>(sci(SCI_WORDSTARTPOSITION, pos, 1));
        const int e = static_cast<int>(sci(SCI_WORDENDPOSITION, pos, 1));
        if (e <= s) return wxString();
        std::string b;
        for (int i = s; i < e; ++i) b += static_cast<char>(sci(SCI_GETCHARAT, i));   // a word is short; avoids Sci_TextRange
        return wxString::FromUTF8(b.data(), b.size());
    }
    wxString currentLineStr()
    {
        if (!m_stc) return wxString();
        const int line = static_cast<int>(sci(SCI_LINEFROMPOSITION, sci(SCI_GETCURRENTPOS)));
        const int len = static_cast<int>(sci(SCI_GETLINE, line, 0));
        if (len <= 0) return wxString();
        std::string b(static_cast<size_t>(len) + 1, '\0');
        sci(SCI_GETLINE, line, reinterpret_cast<sptr_t>(&b[0])); b.resize(len);
        return wxString::FromUTF8(b.data(), b.size());
    }
    EditorPage* findPageByPath(const wchar_t* path)
    {
        if (!path || !m_tabs) return nullptr;
        const wxString want(path);
        for (size_t i = 0; i < m_tabs->GetPageCount(); ++i)
        {
            auto* pg = static_cast<EditorPage*>(m_tabs->GetPage(i));
            if (pg && pg->path == want) return pg;
        }
        return nullptr;
    }
    LRESULT switchToFile(const wchar_t* path)
    {
        EditorPage* pg = findPageByPath(path);
        if (!pg) return FALSE;
        m_tabs->SetSelection(m_tabs->GetPageIndex(pg));
        return TRUE;
    }
    LRESULT reloadFile(const wchar_t* path)
    {
        EditorPage* pg = findPageByPath(path);
        if (!pg) return FALSE;
        m_tabs->SetSelection(m_tabs->GetPageIndex(pg));
        loadFile(wxString(path));
        return TRUE;
    }
    LRESULT pathFromBufferId(WPARAM bufId, wchar_t* buf)   // bufferID is the EditorPage* (see NPPM_GETCURRENTBUFFERID)
    {
        auto* target = reinterpret_cast<EditorPage*>(bufId);
        for (size_t i = 0; m_tabs && i < m_tabs->GetPageCount(); ++i)
        {
            auto* pg = static_cast<EditorPage*>(m_tabs->GetPage(i));
            if (pg == target) { if (buf) wxStrlcpy(buf, pg->path.wc_str(), MAX_PATH); return static_cast<LRESULT>(pg->path.length()); }
        }
        return -1;
    }
    HMENU pluginsMenuHandle()   // plugins use NPPM_GETMENUHANDLE(NPPPLUGINMENU) to add submenu items at runtime
    {
        auto* mb = GetMenuBar();
        if (!mb) return nullptr;
        const int mi = mb->FindMenu("Plugins");
        return mi == wxNOT_FOUND ? nullptr : reinterpret_cast<HMENU>(mb->GetMenu(mi)->GetHMenu());
    }
    // NPPM_DMMREGASDCKDLG: host the plugin's own dialog HWND in a wxAuiManager dock pane. The default
    // edge comes from the uMask high bits (DWS_DF_CONT_*/DWS_DF_FLOATING). Starts hidden (DMMSHOW shows it).
    LRESULT registerDockDialog(tTbData* d)
    {
        if (!d || !d->hClient) return FALSE;
        HWND hc = d->hClient;
        auto* host = new wxPanel(this);
        ::SetParent(hc, static_cast<HWND>(host->GetHandle()));
        host->Bind(wxEVT_SIZE, [host, hc](wxSizeEvent& e) {
            const wxSize s = host->GetClientSize(); ::MoveWindow(hc, 0, 0, s.GetWidth(), s.GetHeight(), TRUE); e.Skip();
        });
        const wxString name(d->pszName ? d->pszName : L"Plugin");
        wxAuiPaneInfo pi; pi.Name(name).Caption(name).CloseButton(true).BestSize(340, 220).MinSize(160, 100).Hide();
        if (d->uMask & DWS_DF_FLOATING) pi.Float();
        else switch ((d->uMask >> 28) & 0x0F)
        {
            case CONT_LEFT:  pi.Left();  break;
            case CONT_RIGHT: pi.Right(); break;
            case CONT_TOP:   pi.Top();   break;
            default:         pi.Bottom();break;
        }
        m_aui.AddPane(host, pi);
        m_aui.Update();
        m_docks.push_back({ hc, host, name });
        return TRUE;
    }
    LRESULT showDock(HWND hDlg, bool show)
    {
        for (auto& dk : m_docks)
            if (dk.hClient == hDlg)
            {
                wxAuiPaneInfo& pi = m_aui.GetPane(dk.host);
                if (pi.IsOk()) { pi.Show(show); if (show) ::ShowWindow(dk.hClient, SW_SHOW); m_aui.Update(); }
                return TRUE;
            }
        return FALSE;
    }
    // Answer the common NPPM_*/RUNCOMMAND_USER plugin messages from real state. Returns true (with
    // 'out' set) when handled; unhandled messages fall through to DefSubclassProc in the subclass proc.
    bool handleNppm(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& out)
    {
        EditorPage* p = activePage();
        const wxString path = p ? p->path : wxString();
        const wxFileName fn(path);
        auto put = [&](const wxString& s) -> LRESULT {     // copy a string into the plugin's (strLen, wchar_t*) buffer
            wchar_t* buf = reinterpret_cast<wchar_t*>(lParam);
            if (!buf || wParam == 0) return static_cast<LRESULT>(s.length());
            wxStrlcpy(buf, s.wc_str(), static_cast<size_t>(wParam));
            return TRUE;
        };
        switch (msg)
        {
            case NPPM_GETNPPVERSION:        out = MAKELONG(96, 8); return true;
            case NPPM_GETCURRENTSCINTILLA:  if (lParam) *reinterpret_cast<int*>(lParam) = 0; out = TRUE; return true;
            case NPPM_GETNBOPENFILES:       out = static_cast<LRESULT>(m_tabs ? m_tabs->GetPageCount() : 1); return true;
            case NPPM_GETCURRENTBUFFERID:   out = reinterpret_cast<LRESULT>(p); return true;   // EditorPage* = stable per-doc id
            case NPPM_GETFULLCURRENTPATH:   out = put(path); return true;
            case NPPM_GETCURRENTDIRECTORY:  out = put(fn.GetPath()); return true;
            case NPPM_GETFILENAME:          out = put(fn.GetFullName()); return true;
            case NPPM_GETNAMEPART:          out = put(fn.GetName()); return true;
            case NPPM_GETEXTPART:           out = put(fn.GetExt()); return true;
            case NPPM_GETNPPDIRECTORY:      out = put(wxPathOnly(wxStandardPaths::Get().GetExecutablePath())); return true;
            case NPPM_GETNPPFULLFILEPATH:   out = put(wxStandardPaths::Get().GetExecutablePath()); return true;
            case NPPM_GETPLUGINSCONFIGDIR:  out = put(pluginsConfigDir()); return true;
            case NPPM_GETCURRENTWORD:       out = put(currentWord()); return true;
            case NPPM_GETCURRENTLINESTR:    out = put(currentLineStr()); return true;
            case NPPM_GETCURRENTLANGTYPE:   if (lParam) *reinterpret_cast<int*>(lParam) = 0; out = TRUE; return true;   // L_TEXT (minimal)
            case NPPM_MENUCOMMAND:          ::SendMessageW(static_cast<HWND>(GetHandle()), WM_COMMAND, static_cast<WPARAM>(lParam), 0); out = TRUE; return true;
            case NPPM_SETSTATUSBAR:         { auto* t = reinterpret_cast<const wchar_t*>(lParam); if (t) setStatus(0, wxString(t)); out = TRUE; return true; }
            case NPPM_DOOPEN:               if (lParam) openPath(wxString(reinterpret_cast<const wchar_t*>(lParam))); out = TRUE; return true;
            case NPPM_SAVECURRENTFILE:      onSave(); out = TRUE; return true;
            case NPPM_MAKECURRENTBUFFERDIRTY: out = TRUE; return true;   // best-effort: Scintilla has no force-dirty API
            case NPPM_ACTIVATEDOC:          if (m_tabs && static_cast<size_t>(lParam) < m_tabs->GetPageCount()) m_tabs->SetSelection(static_cast<size_t>(lParam)); out = TRUE; return true;
            case NPPM_SWITCHTOFILE:         out = switchToFile(reinterpret_cast<const wchar_t*>(lParam)); return true;
            case NPPM_RELOADFILE:           out = reloadFile(reinterpret_cast<const wchar_t*>(lParam)); return true;
            case NPPM_GETFULLPATHFROMBUFFERID: out = pathFromBufferId(wParam, reinterpret_cast<wchar_t*>(lParam)); return true;
            case NPPM_GETMENUHANDLE:        out = reinterpret_cast<LRESULT>(pluginsMenuHandle()); return true;
            case NPPM_DMMREGASDCKDLG:       out = registerDockDialog(reinterpret_cast<tTbData*>(lParam)); return true;
            case NPPM_DMMSHOW:              out = showDock(reinterpret_cast<HWND>(lParam), true); return true;
            case NPPM_DMMHIDE:              out = showDock(reinterpret_cast<HWND>(lParam), false); return true;
            case NPPM_DMMUPDATEDISPINFO:    if (lParam) ::InvalidateRect(reinterpret_cast<HWND>(lParam), nullptr, TRUE); out = TRUE; return true;
            case RUNCOMMAND_USER + CURRENT_LINE:   out = m_sci ? sci(SCI_LINEFROMPOSITION, sci(SCI_GETCURRENTPOS)) : 0; return true;
            case RUNCOMMAND_USER + CURRENT_COLUMN: out = m_sci ? sci(SCI_GETCOLUMN, sci(SCI_GETCURRENTPOS)) : 0; return true;
        }
        return false;
    }
#endif // __WXMSW__ (Win32 plugin host)

    // ----- toolbar (Notepad++ icon pack, native order) -------------------
    wxBitmapBundle icon(const wxString& name)
    {
        static const wxString dir = wxPathOnly(wxStandardPaths::Get().GetExecutablePath()) + "\\icons\\";
        const wxString path = dir + name + ".svg";
        if (wxFileExists(path)) return wxBitmapBundle::FromSVGFile(path, wxSize(16, 16));
        return wxArtProvider::GetBitmapBundle(wxART_QUESTION, wxART_TOOLBAR, wxSize(16, 16));
    }
    // Small monochrome caption-button glyph (the +/v/x), themed to the current mode.
    wxBitmapBundle captionIcon(const char* pathData)
    {
        const char* col = m_dark ? "#dcdcdc" : "#404040";
        const wxString svg = wxString::Format(
            "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'>"
            "<path d='%s' fill='none' stroke='%s' stroke-width='1.6' stroke-linecap='round' stroke-linejoin='round'/></svg>",
            pathData, col);
        const wxScopedCharBuffer u = svg.utf8_str();
        return wxBitmapBundle::FromSVG(u.data(), wxSize(16, 16));
    }
    // The "v" caption dropdown: list the open documents and switch to the chosen one.
    void onDocList()
    {
        wxMenu menu;
        const int sel = m_tabs->GetSelection();
        for (size_t i = 0; i < m_tabs->GetPageCount(); ++i)
        {
            wxMenuItem* it = menu.AppendRadioItem((int)(myID_DOCLIST_ITEM + i), m_tabs->GetPageText(i));
            if ((int)i == sel) it->Check(true);
        }
        PopupMenu(&menu);
    }

    void buildToolBar()
    {
        auto* tb = CreateToolBar(wxTB_FLAT | wxTB_HORIZONTAL);
        tb->SetToolBitmapSize(wxSize(16, 16));
        auto T  = [&](int id, const wxString& svg, const wxString& tip) { tb->AddTool(id, tip, icon(svg), tip); };
        auto TC = [&](int id, const wxString& svg, const wxString& tip) { tb->AddCheckTool(id, tip, icon(svg), wxNullBitmap, tip); };

        T(IDM_FILE_NEW, "new", "New");           T(IDM_FILE_OPEN, "open", "Open");
        T(IDM_FILE_SAVE, "save", "Save");        T(IDM_FILE_SAVEALL, "save-all", "Save All");
        T(IDM_FILE_CLOSE, "close", "Close");     T(IDM_FILE_CLOSEALL, "close-all", "Close All");
        T(IDM_FILE_PRINT, "print", "Print");
        tb->AddSeparator();
        T(IDM_EDIT_CUT, "cut", "Cut");           T(IDM_EDIT_COPY, "copy", "Copy");           T(IDM_EDIT_PASTE, "paste", "Paste");
        tb->AddSeparator();
        T(IDM_EDIT_UNDO, "undo", "Undo");        T(IDM_EDIT_REDO, "redo", "Redo");
        tb->AddSeparator();
        T(IDM_SEARCH_FIND, "find", "Find");      T(IDM_SEARCH_REPLACE, "replace", "Replace");
        tb->AddSeparator();
        T(IDM_VIEW_ZOOMIN, "zoom-in", "Zoom In");  T(IDM_VIEW_ZOOMOUT, "zoom-out", "Zoom Out");
        tb->AddSeparator();
        TC(IDM_VIEW_SYNSCROLLV, "sync-vertical", "Synchronize Vertical Scrolling");
        TC(IDM_VIEW_SYNSCROLLH, "sync-horizontal", "Synchronize Horizontal Scrolling");
        tb->AddSeparator();
        TC(IDM_VIEW_WRAP, "word-wrap", "Word Wrap");
        TC(IDM_VIEW_ALL_CHARACTERS, "all-chars", "Show All Characters");
        TC(IDM_VIEW_INDENT_GUIDE, "indent-guide", "Show Indent Guide");
        tb->AddSeparator();
        T(IDM_LANG_USER_DLG, "udl-dlg", "User-Defined Language Dialogue");
        T(IDM_VIEW_DOC_MAP, "doc-map", "Document Map");
        T(IDM_VIEW_DOCLIST, "doc-list", "Document List");
        T(IDM_VIEW_FUNC_LIST, "function-list", "Function List");
        T(IDM_VIEW_FILEBROWSER, "folder-as-workspace", "Folder as Workspace");
        tb->AddSeparator();
        TC(IDM_VIEW_MONITORING, "monitoring", "Monitoring (tail -f)");
        tb->AddSeparator();
        T(IDM_MACRO_STARTRECORDINGMACRO, "record", "Start Recording");
        T(IDM_MACRO_STOPRECORDINGMACRO, "stop-record", "Stop Recording");
        T(IDM_MACRO_PLAYBACKRECORDEDMACRO, "playback", "Playback");
        T(IDM_MACRO_RUNMULTIMACRODLG, "playback-multiple", "Run a Macro Multiple Times");
        T(IDM_MACRO_SAVECURRENTMACRO, "save-macro", "Save Current Recorded Macro");
        tb->Realize();
        // Macro idle state (nothing recording, no macro stored) - matches Notepad++: only "Start Recording"
        // is enabled; Stop / Playback / Run-Multiple / Save stay greyed until a macro is being/been recorded.
        tb->EnableTool(IDM_MACRO_STOPRECORDINGMACRO, false);
        tb->EnableTool(IDM_MACRO_PLAYBACKRECORDEDMACRO, false);
        tb->EnableTool(IDM_MACRO_RUNMULTIMACRODLG, false);
        tb->EnableTool(IDM_MACRO_SAVECURRENTMACRO, false);
#ifdef __WXMSW__
        ::SendMessageW(static_cast<HWND>(tb->GetHandle()), TB_SETINDENT, 4, 0);  // small left margin before the first button
#endif
    }

    void buildStatusBar()
    {
        auto* sb = CreateStatusBar(7);
        // field 0 (doc type / message area) is proportional so the fields fill the whole bar and
        // the typing-mode field lands flush right, like Notepad++ (no dead slack on the right).
        const int w[7] = { -1, 150, 210, 110, 130, 80, 46 };
        sb->SetStatusWidths(7, w);
        const int styles[7] = { wxSB_FLAT, wxSB_FLAT, wxSB_FLAT, wxSB_FLAT, wxSB_FLAT, wxSB_FLAT, wxSB_FLAT };
        sb->SetStatusStyles(7, styles);   // flat fields - no per-field sunken background
    }

    // ----- syntax highlighting (Lexilla + Notepad++ theme colours) -------
    struct LexMap { const char* lexer; const char* theme; };   // Lexilla lexer name, theme LexerType name
    static LexMap lexerForExt(const wxString& e)
    {
        auto m = [](const char* l, const char* t){ return LexMap{ l, t }; };
        if (e=="c"||e=="cpp"||e=="cc"||e=="cxx"||e=="h"||e=="hpp"||e=="hxx"||e=="cs"||
            e=="java"||e=="js"||e=="jsx"||e=="ts"||e=="tsx"||e=="rc")          return m("cpp", "cpp");
        if (e=="py"||e=="pyw")                                                return m("python", "python");
        if (e=="json")                                                       return m("json", "json");
        if (e=="sql")                                                        return m("sql", "sql");
        if (e=="css")                                                        return m("css", "css");
        if (e=="lua")                                                        return m("lua", "lua");
        if (e=="sh"||e=="bash")                                              return m("bash", "bash");
        if (e=="bat"||e=="cmd")                                              return m("batch", "batch");
        if (e=="pl"||e=="pm")                                                return m("perl", "perl");
        if (e=="rb")                                                         return m("ruby", "ruby");
        if (e=="rs")                                                         return m("rust", "rust");
        if (e=="go")                                                         return m("go", "go");
        if (e=="ps1"||e=="psm1")                                             return m("powershell", "powershell");
        if (e=="ini"||e=="properties"||e=="cfg")                            return m("props", "props");
        if (e=="yml"||e=="yaml")                                             return m("yaml", "yaml");
        if (e=="xml"||e=="svg"||e=="xaml"||e=="xsd"||e=="xsl"||e=="vcxproj") return m("xml", "xml");
        return m(nullptr, nullptr);   // plain text
    }
    // Friendly document-type label for the status bar (like Notepad++'s "C++ source file").
    static wxString langDisplayName(const wxString& e)
    {
        if (e=="cpp"||e=="cc"||e=="cxx"||e=="hpp"||e=="hxx") return "C++ source file";
        if (e=="c"||e=="h")                                  return "C source file";
        if (e=="cs")                                         return "C# source file";
        if (e=="java")                                       return "Java source file";
        if (e=="js"||e=="jsx")                               return "JavaScript file";
        if (e=="ts"||e=="tsx")                               return "TypeScript file";
        if (e=="py"||e=="pyw")                               return "Python file";
        if (e=="json")                                       return "JSON file";
        if (e=="xml"||e=="svg"||e=="xaml"||e=="xsd"||e=="xsl"||e=="vcxproj") return "XML file";
        if (e=="css")                                        return "CSS file";
        if (e=="sql")                                        return "SQL file";
        if (e=="lua")                                        return "Lua source file";
        if (e=="sh"||e=="bash")                              return "Shell script file";
        if (e=="bat"||e=="cmd")                              return "Batch file";
        if (e=="rb")                                         return "Ruby source file";
        if (e=="rs")                                         return "Rust source file";
        if (e=="go")                                         return "Go source file";
        if (e=="pl"||e=="pm")                                return "Perl source file";
        if (e=="ps1"||e=="psm1")                             return "PowerShell file";
        if (e=="ini"||e=="properties"||e=="cfg")            return "Properties file";
        if (e=="yml"||e=="yaml")                             return "YAML file";
        return "Normal text file";
    }
    void setLexerForFile(const wxString& path)
    {
        applyEditorTheme(m_dark);          // reset every style to the theme base (incl. line numbers)
        const wxString ext = path.AfterLast('.').Lower();
        if (auto* p = activePage()) p->lang = langDisplayName(ext);
        const LexMap lm = lexerForExt(ext);
        sci(SCI_SETILEXER, 0, reinterpret_cast<sptr_t>(lm.lexer ? CreateLexer(lm.lexer) : nullptr));
        if (lm.lexer)
        {
            sci(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold"), reinterpret_cast<sptr_t>("1"));
            sci(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold.compact"), reinterpret_cast<sptr_t>("0"));
            bool themed = false;
            if (m_theme.loaded)
            {
                auto it = m_theme.lexers.find(lm.theme);
                if (it != m_theme.lexers.end())
                {
                    for (const StyleDef& s : it->second)   // apply Notepad++'s exact per-token colours
                    {
                        if (s.id < 0) continue;
                        if (s.fg >= 0) sci(SCI_STYLESETFORE, s.id, s.fg);
                        if (s.bg >= 0) sci(SCI_STYLESETBACK, s.id, s.bg);
                        sci(SCI_STYLESETBOLD,   s.id, (s.fontStyle & 1) ? 1 : 0);
                        sci(SCI_STYLESETITALIC, s.id, (s.fontStyle & 2) ? 1 : 0);
                    }
                    themed = true;
                }
            }
            if (!themed) { if (std::string(lm.lexer) == "python") stylePythonFallback(); else styleCppFallback(); }
            const std::string lx(lm.lexer);
            auto kw = [&](const char* words) { sci(SCI_SETKEYWORDS, 0, reinterpret_cast<sptr_t>(words)); };
            if (lx == "cpp") {   // the C-family lexer is shared; pick keywords by file extension
                if      (ext == "js" || ext == "jsx" || ext == "ts" || ext == "tsx") kw(JS_KEYWORDS);
                else if (ext == "java")                                              kw(JAVA_KEYWORDS);
                else if (ext == "cs")                                                kw(CS_KEYWORDS);
                else                                                                 kw(CPP_KEYWORDS);
            }
            else if (lx == "python")     kw(PY_KEYWORDS);
            else if (lx == "sql")        kw(SQL_KEYWORDS);
            else if (lx == "lua")        kw(LUA_KEYWORDS);
            else if (lx == "bash")       kw(BASH_KEYWORDS);
            else if (lx == "go")         kw(GO_KEYWORDS);
            else if (lx == "rust")       kw(RUST_KEYWORDS);
            else if (lx == "css")      { kw(CSS_KEYWORDS); sci(SCI_SETKEYWORDS, 1, reinterpret_cast<sptr_t>(CSS_PSEUDO)); }
            else if (lx == "batch")      kw(BATCH_KEYWORDS);
            else if (lx == "perl")       kw(PERL_KEYWORDS);
            else if (lx == "ruby")       kw(RUBY_KEYWORDS);
            else if (lx == "powershell") kw(PS_KEYWORDS);
            else if (lx == "json")       kw(JSON_KEYWORDS);
        }
        sci(SCI_COLOURISE, 0, -1);
    }
    // Built-in colour fallback, used only if the theme XML can't be loaded.
    void styleCppFallback()
    {
        const int cm=m_dark?0x55996A:0x008000, kw=m_dark?0xD69C56:0xFF0000, st=m_dark?0x7891CE:0x1515A3,
                  nu=m_dark?0xA8CEB5:0x588609, pp=m_dark?0xC086C5:0x800080;
        auto S=[&](int s,int f){ sci(SCI_STYLESETFORE,s,f); };
        S(SCE_C_COMMENT,cm);S(SCE_C_COMMENTLINE,cm);S(SCE_C_COMMENTDOC,cm);S(SCE_C_COMMENTLINEDOC,cm);
        S(SCE_C_NUMBER,nu);S(SCE_C_WORD,kw);S(SCE_C_WORD2,kw);
        S(SCE_C_STRING,st);S(SCE_C_CHARACTER,st);S(SCE_C_VERBATIM,st);S(SCE_C_STRINGRAW,st);S(SCE_C_PREPROCESSOR,pp);
    }
    void stylePythonFallback()
    {
        const int cm=m_dark?0x55996A:0x008000, kw=m_dark?0xD69C56:0xFF0000, st=m_dark?0x7891CE:0x1515A3, nu=m_dark?0xA8CEB5:0x588609;
        auto S=[&](int s,int f){ sci(SCI_STYLESETFORE,s,f); };
        S(SCE_P_COMMENTLINE,cm);S(SCE_P_COMMENTBLOCK,cm);S(SCE_P_NUMBER,nu);
        S(SCE_P_STRING,st);S(SCE_P_CHARACTER,st);S(SCE_P_TRIPLE,st);S(SCE_P_TRIPLEDOUBLE,st);
        S(SCE_P_WORD,kw);S(SCE_P_DEFNAME,kw);S(SCE_P_CLASSNAME,kw);
    }
    // ----- file actions --------------------------------------------------
    void setDocTitle(const wxString& name) { if (auto* p = activePage()) { p->title = name; refreshTab(p); } else SetTitle(name + " - Notepad++"); }
    void doNew() { addDocument("", nextNewName()); }   // New opens a fresh tab, like Notepad++
    void loadFile(const wxString& path)
    {
        wxFile f(path);
        if (!f.IsOpened()) return;
        wxString c; f.ReadAll(&c);
        const wxScopedCharBuffer u = c.ToUTF8();
        sci(SCI_CLEARALL);
        sci(SCI_ADDTEXT, u.length(), reinterpret_cast<sptr_t>(u.data()));
        // Detect the file's line-ending style and match Scintilla's EOL mode to it (like Notepad++), so the
        // status bar reports the real format (Unix/Windows/Mac) instead of the Windows default. Mode only -
        // the content's actual bytes are preserved (no SCI_CONVERTEOLS) until the user explicitly converts.
        {
            int crlf = 0, lf = 0, cr = 0;
            const char* d = u.data(); const size_t n = u.length();
            for (size_t i = 0; i < n; ++i)
            {
                if (d[i] == '\r') { if (i + 1 < n && d[i + 1] == '\n') { ++crlf; ++i; } else ++cr; }
                else if (d[i] == '\n') ++lf;
            }
            if (crlf || lf || cr)   // leave the default mode for a file with no line breaks at all
                sci(SCI_SETEOLMODE, (crlf >= lf && crlf >= cr) ? SC_EOL_CRLF : (lf >= cr ? SC_EOL_LF : SC_EOL_CR));
        }
        sci(SCI_EMPTYUNDOBUFFER); sci(SCI_GOTOPOS, 0); sci(SCI_SETSAVEPOINT);
        if (auto* p = activePage()) p->path = path;
        m_path = path; setDocTitle(wxFileNameFromPath(path));
        setLexerForFile(path);
    }
    void onOpen() { wxFileDialog d(this, "Open", "", "", "All files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST); if (d.ShowModal() == wxID_OK) addDocument(d.GetPath(), wxFileNameFromPath(d.GetPath())); }
    void onReload() { if (!m_path.empty()) loadFile(m_path); }
    bool writeFile(const wxString& path)
    {
        const int len = static_cast<int>(sci(SCI_GETLENGTH));
        std::string b(static_cast<size_t>(len) + 1, '\0');
        sci(SCI_GETTEXT, len + 1, reinterpret_cast<sptr_t>(&b[0])); b.resize(len);
        wxFile f(path, wxFile::write);
        if (!f.IsOpened()) return false;
        f.Write(b.data(), b.size()); sci(SCI_SETSAVEPOINT);
        if (auto* p = activePage()) p->path = path;
        m_path = path; setDocTitle(wxFileNameFromPath(path)); setLexerForFile(path); addToMRU(path); return true;
    }
    void onSave() { if (m_path.empty()) onSaveAs(); else writeFile(m_path); }
    void onSaveAs() { wxFileDialog d(this, "Save As", "", "new 1.txt", "All files (*.*)|*.*", wxFD_SAVE | wxFD_OVERWRITE_PROMPT); if (d.ShowModal() == wxID_OK) writeFile(d.GetPath()); }

    // ----- edit actions --------------------------------------------------
    std::string getDocUtf8()
    {
        const int len = static_cast<int>(sci(SCI_GETLENGTH));
        std::string b(static_cast<size_t>(len) + 1, '\0');
        sci(SCI_GETTEXT, len + 1, reinterpret_cast<sptr_t>(&b[0])); b.resize(len); return b;
    }
    void setDocUtf8(const std::string& s) { sci(SCI_BEGINUNDOACTION); sci(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(s.c_str())); sci(SCI_ENDUNDOACTION); }
    void removeEmptyLines()
    {
        const std::string b = getDocUtf8(); std::string out; size_t i = 0;
        while (i < b.size())
        {
            const size_t nl = b.find('\n', i);
            std::string line = (nl == std::string::npos) ? b.substr(i) : b.substr(i, nl - i);
            std::string core = line; if (!core.empty() && core.back() == '\r') core.pop_back();
            if (!core.empty()) { out += line; if (nl != std::string::npos) out += '\n'; }
            if (nl == std::string::npos) break; i = nl + 1;
        }
        setDocUtf8(out);
    }
    void trimTrailing()
    {
        const std::string b = getDocUtf8(); std::string out; size_t i = 0;
        for (;;)
        {
            const size_t nl = b.find('\n', i);
            std::string line = (nl == std::string::npos) ? b.substr(i) : b.substr(i, nl - i);
            const bool cr = !line.empty() && line.back() == '\r'; if (cr) line.pop_back();
            const size_t last = line.find_last_not_of(" \t"); line = (last == std::string::npos) ? std::string() : line.substr(0, last + 1);
            if (cr) line += '\r';
            out += line;
            if (nl == std::string::npos) break;
            out += '\n'; i = nl + 1;
        }
        setDocUtf8(out);
    }
    void tabsToSpaces()
    {
        const int tw = static_cast<int>(sci(SCI_GETTABWIDTH)); wxString sp(' ', tw <= 0 ? 4 : tw);
        wxString t = getAllText(); t.Replace("\t", sp); setAllText(t);
    }
    void toggleLineComment()   // simple "//" toggle over the selected lines (C-like)
    {
        const int l1 = static_cast<int>(sci(SCI_LINEFROMPOSITION, sci(SCI_GETSELECTIONSTART)));
        const int l2 = static_cast<int>(sci(SCI_LINEFROMPOSITION, sci(SCI_GETSELECTIONEND)));
        sci(SCI_BEGINUNDOACTION);
        for (int l = l1; l <= l2; ++l)
        {
            const int p  = static_cast<int>(sci(SCI_POSITIONFROMLINE, l));
            const int ll = static_cast<int>(sci(SCI_LINELENGTH, l));
            std::string b(static_cast<size_t>(ll) + 1, '\0');
            sci(SCI_GETLINE, l, reinterpret_cast<sptr_t>(&b[0])); b.resize(ll);
            const size_t nb = b.find_first_not_of(" \t\r\n");
            if (nb == std::string::npos) continue;                       // blank line: skip
            if (b.compare(nb, 2, "//") == 0)                             // already commented -> uncomment
            { sci(SCI_SETTARGETSTART, p + static_cast<int>(nb)); sci(SCI_SETTARGETEND, p + static_cast<int>(nb) + 2); sci(SCI_REPLACETARGET, 0, reinterpret_cast<sptr_t>("")); }
            else                                                         // comment (preserve indentation)
            { sci(SCI_INSERTTEXT, p + static_cast<int>(nb), reinterpret_cast<sptr_t>("//")); }
        }
        sci(SCI_ENDUNDOACTION);
    }
    void setEol(int mode) { sci(SCI_SETEOLMODE, mode); sci(SCI_CONVERTEOLS, mode); }

    // ----- line / blank / sort operations (Notepad++ Edit menu) ----------
    const char* eolStr() { const int m = (int)sci(SCI_GETEOLMODE); return m == SC_EOL_CR ? "\r" : m == SC_EOL_LF ? "\n" : "\r\n"; }
    void linesJoin()  { sci(SCI_TARGETFROMSELECTION); sci(SCI_LINESJOIN); }
    void linesSplit() { sci(SCI_TARGETFROMSELECTION); sci(SCI_LINESSPLIT, 0); }
    void blankLine(bool below)
    {
        const int line = (int)sci(SCI_LINEFROMPOSITION, sci(SCI_GETCURRENTPOS));
        const int pos  = below ? (int)sci(SCI_GETLINEENDPOSITION, line) : (int)sci(SCI_POSITIONFROMLINE, line);
        sci(SCI_INSERTTEXT, pos, reinterpret_cast<sptr_t>(eolStr()));
    }
    void insertDateTime(bool longFmt)
    {
        const wxString s = wxDateTime::Now().Format(longFmt ? "%A, %B %d, %Y %H:%M:%S" : "%Y-%m-%d %H:%M");
        const wxScopedCharBuffer u = s.ToUTF8();
        sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>(u.data()));
    }
    std::vector<std::string> docLines()   // doc split into lines, EOLs stripped
    {
        const std::string b = getDocUtf8(); std::vector<std::string> v; size_t i = 0;
        for (;;) {
            const size_t nl = b.find('\n', i);
            std::string ln = (nl == std::string::npos) ? b.substr(i) : b.substr(i, nl - i);
            if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            v.push_back(ln);
            if (nl == std::string::npos) break; i = nl + 1;
        }
        return v;
    }
    void putLines(const std::vector<std::string>& v)
    {
        const std::string eol = eolStr(); std::string out;
        for (size_t i = 0; i < v.size(); ++i) { out += v[i]; if (i + 1 < v.size()) out += eol; }
        setDocUtf8(out);
    }
    void removeDuplicateLines(bool consecutiveOnly)
    {
        std::vector<std::string> v = docLines(), out;
        if (consecutiveOnly) { for (auto& l : v) if (out.empty() || out.back() != l) out.push_back(l); }
        else { std::set<std::string> seen; for (auto& l : v) if (seen.insert(l).second) out.push_back(l); }
        putLines(out);
    }
    void reverseLines() { auto v = docLines(); std::reverse(v.begin(), v.end()); putLines(v); }
    void sortLines(int mode, bool desc)   // 0 = lexicographic, 1 = case-insensitive, 2 = integer
    {
        auto v = docLines();
        auto lower = [](std::string s){ for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };
        std::stable_sort(v.begin(), v.end(), [&](const std::string& a, const std::string& b){
            if (mode == 2) return std::strtoll(a.c_str(), nullptr, 10) < std::strtoll(b.c_str(), nullptr, 10);
            if (mode == 1) return lower(a) < lower(b);
            return a < b;
        });
        if (desc) std::reverse(v.begin(), v.end());
        putLines(v);
    }
    void transformLines(const std::function<void(std::string&)>& fn) { auto v = docLines(); for (auto& l : v) fn(l); putLines(v); }
    void trimLeading() { transformLines([](std::string& s){ const size_t p = s.find_first_not_of(" \t"); s = (p == std::string::npos) ? std::string() : s.substr(p); }); }
    void trimBoth()    { transformLines([](std::string& s){ const size_t a = s.find_first_not_of(" \t"); const size_t b = s.find_last_not_of(" \t"); s = (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1); }); }
    void eolToSpace()  { std::string b = getDocUtf8(), out; for (char c : b) { if (c == '\r') continue; out += (c == '\n') ? ' ' : c; } setDocUtf8(out); }

    // ----- search engine (drives the Find/Replace dialog) ----------------
    // Report a search outcome both in the main status bar and, if it is open, inside the Find dialog
    // (Notepad++ shows the result line at the bottom of the dialog).
    void findResult(const wxString& msg) { setStatus(0, msg); if (m_findDlg && m_findDlg->IsShown()) m_findDlg->setResult(msg); }
    int searchFlags(const FindOpts& o) const
    {
        int f = 0;
        if (o.matchCase) f |= SCFIND_MATCHCASE;
        if (o.wholeWord) f |= SCFIND_WHOLEWORD;
        if (o.regex)     f |= SCFIND_REGEXP | SCFIND_CXX11REGEX;
        return f;
    }
    bool doFindNext(const FindOpts& o)
    {
        if (o.find.empty()) return false;
        const wxScopedCharBuffer n = o.find.ToUTF8();
        sci(SCI_SETSEARCHFLAGS, searchFlags(o));
        const int len = static_cast<int>(sci(SCI_GETLENGTH));
        const int cur = static_cast<int>(o.forward ? sci(SCI_GETCURRENTPOS) : sci(SCI_GETSELECTIONSTART));
        sci(SCI_SETTARGETSTART, cur);
        sci(SCI_SETTARGETEND, o.forward ? len : 0);
        sptr_t f = sci(SCI_SEARCHINTARGET, n.length(), reinterpret_cast<sptr_t>(n.data()));
        if (f < 0 && o.wrap)
        {
            sci(SCI_SETTARGETSTART, o.forward ? 0 : len);
            sci(SCI_SETTARGETEND, o.forward ? len : 0);
            f = sci(SCI_SEARCHINTARGET, n.length(), reinterpret_cast<sptr_t>(n.data()));
        }
        if (f >= 0)
        {
            sci(SCI_SETSEL, sci(SCI_GETTARGETSTART), sci(SCI_GETTARGETEND));
            sci(SCI_SCROLLCARET); m_stc->SetFocus();
            findResult(wxString::Format("Found: %s", o.find));
            return true;
        }
        findResult(wxString::Format("Can't find: %s", o.find));
        return false;
    }
    // Search range: the whole document, or just the current selection when "In selection" is set.
    void searchBounds(const FindOpts& o, int& start, int& end)
    {
        if (o.inSelection) { start = static_cast<int>(sci(SCI_GETSELECTIONSTART)); end = static_cast<int>(sci(SCI_GETSELECTIONEND)); }
        else               { start = 0; end = static_cast<int>(sci(SCI_GETLENGTH)); }
    }
    int doCount(const FindOpts& o)
    {
        if (o.find.empty()) return 0;
        const wxScopedCharBuffer n = o.find.ToUTF8();
        int start, end; searchBounds(o, start, end);
        sci(SCI_SETSEARCHFLAGS, searchFlags(o));
        int count = 0; sci(SCI_SETTARGETSTART, start); sci(SCI_SETTARGETEND, end);
        while (sci(SCI_SEARCHINTARGET, n.length(), reinterpret_cast<sptr_t>(n.data())) >= 0)
        {
            int e = static_cast<int>(sci(SCI_GETTARGETEND));
            if (e == static_cast<int>(sci(SCI_GETTARGETSTART))) ++e;   // zero-length match guard
            sci(SCI_SETTARGETSTART, e); sci(SCI_SETTARGETEND, end); ++count;
        }
        return count;
    }
    bool doReplaceOne(const FindOpts& o)
    {
        const wxScopedCharBuffer n = o.find.ToUTF8(), r = o.repl.ToUTF8();
        sci(SCI_SETSEARCHFLAGS, searchFlags(o));
        const int ss = static_cast<int>(sci(SCI_GETSELECTIONSTART)), se = static_cast<int>(sci(SCI_GETSELECTIONEND));
        sci(SCI_SETTARGETSTART, ss); sci(SCI_SETTARGETEND, se);
        const bool selMatches = !o.find.empty()
            && sci(SCI_SEARCHINTARGET, n.length(), reinterpret_cast<sptr_t>(n.data())) == ss
            && static_cast<int>(sci(SCI_GETTARGETEND)) == se;
        if (selMatches)
        {
            if (o.regex) sci(SCI_REPLACETARGETRE, r.length(), reinterpret_cast<sptr_t>(r.data()));
            else         sci(SCI_REPLACETARGET,   r.length(), reinterpret_cast<sptr_t>(r.data()));
            sci(SCI_SETSEL, sci(SCI_GETTARGETEND), sci(SCI_GETTARGETEND));
        }
        return doFindNext(o);
    }
    int doReplaceAll(const FindOpts& o)
    {
        if (o.find.empty()) return 0;
        const wxScopedCharBuffer n = o.find.ToUTF8(), r = o.repl.ToUTF8();
        int start, end; searchBounds(o, start, end);
        sci(SCI_SETSEARCHFLAGS, searchFlags(o));
        sci(SCI_BEGINUNDOACTION);
        int count = 0; sci(SCI_SETTARGETSTART, start); sci(SCI_SETTARGETEND, end);
        while (sci(SCI_SEARCHINTARGET, n.length(), reinterpret_cast<sptr_t>(n.data())) >= 0)
        {
            const int ms = static_cast<int>(sci(SCI_GETTARGETSTART)), me = static_cast<int>(sci(SCI_GETTARGETEND));
            if (o.regex) sci(SCI_REPLACETARGETRE, r.length(), reinterpret_cast<sptr_t>(r.data()));
            else         sci(SCI_REPLACETARGET,   r.length(), reinterpret_cast<sptr_t>(r.data()));
            int ns = static_cast<int>(sci(SCI_GETTARGETEND));
            end += (ns - me);                    // the bound grows/shrinks with the replacement
            if (me == ms) ++ns;                  // zero-length match guard
            sci(SCI_SETTARGETSTART, ns); sci(SCI_SETTARGETEND, end); ++count;
        }
        sci(SCI_ENDUNDOACTION);
        if (o.inSelection && count > 0) sci(SCI_SETSEL, start, end);   // keep the (resized) selection
        return count;
    }
    void clearMarks() { sci(SCI_SETINDICATORCURRENT, MARK_INDIC); sci(SCI_INDICATORCLEARRANGE, 0, sci(SCI_GETLENGTH)); }
    int doMarkAll(const FindOpts& o)
    {
        clearMarks();
        if (o.find.empty()) return 0;
        const wxScopedCharBuffer n = o.find.ToUTF8();
        int start, end; searchBounds(o, start, end);
        sci(SCI_SETSEARCHFLAGS, searchFlags(o));
        sci(SCI_SETINDICATORCURRENT, MARK_INDIC);
        int count = 0; sci(SCI_SETTARGETSTART, start); sci(SCI_SETTARGETEND, end);
        while (sci(SCI_SEARCHINTARGET, n.length(), reinterpret_cast<sptr_t>(n.data())) >= 0)
        {
            int s = static_cast<int>(sci(SCI_GETTARGETSTART)), e = static_cast<int>(sci(SCI_GETTARGETEND));
            if (e <= s) e = s + 1;
            sci(SCI_INDICATORFILLRANGE, s, e - s);
            sci(SCI_SETTARGETSTART, e); sci(SCI_SETTARGETEND, end); ++count;
        }
        return count;
    }
    wxString selText() { const std::string s = getSelUtf8(); return wxString::FromUTF8(s.data(), s.size()); }
    FindReplaceDialog* ensureFindDlg()
    {
        if (!m_findDlg)
        {
            m_findDlg = new FindReplaceDialog(this);
            m_findDlg->findNextCb   = [this](const FindOpts& o) { m_lastFind = o.find; m_lastReplace = o.repl; doFindNext(o); };
            m_findDlg->countCb      = [this](const FindOpts& o) { findResult(wxString::Format("Count: %d match(es)", doCount(o))); };
            m_findDlg->replaceCb    = [this](const FindOpts& o) { m_lastFind = o.find; m_lastReplace = o.repl; doReplaceOne(o); };
            m_findDlg->replaceAllCb = [this](const FindOpts& o) { findResult(wxString::Format("Replaced %d occurrence(s)", doReplaceAll(o))); };
            m_findDlg->markAllCb    = [this](const FindOpts& o) { findResult(wxString::Format("Marked %d match(es)", doMarkAll(o))); };
            m_findDlg->clearMarksCb = [this]() { clearMarks(); };
            m_findDlg->infoCb       = [this](const wxString& what) { notImpl(what); };
        }
        return m_findDlg;
    }
    void onFind()    { auto* d = ensureFindDlg(); d->prime(selText(), false); themeDialog(d); d->Show(); d->Raise(); }
    void onReplace() { auto* d = ensureFindDlg(); d->prime(selText(), true);  themeDialog(d); d->Show(); d->Raise(); }
    void findNext(bool fwd)
    {
        FindOpts o;
        if (m_findDlg) o = m_findDlg->opts();
        if (o.find.empty()) o.find = m_lastFind;
        if (o.find.empty()) { onFind(); return; }
        o.forward = fwd; doFindNext(o);
    }
    void onGoTo() { GoToLineDialog d(this, static_cast<int>(sci(SCI_GETLINECOUNT)), static_cast<int>(sci(SCI_LINEFROMPOSITION, sci(SCI_GETCURRENTPOS))) + 1, m_dark); themeDialog(&d); if (d.ShowModal() == wxID_OK) { sci(SCI_GOTOLINE, d.GetLine() - 1); m_stc->SetFocus(); } }
    void gotoMatchingBrace() { const int p = static_cast<int>(sci(SCI_GETCURRENTPOS)); sptr_t m = sci(SCI_BRACEMATCH, p, 0); if (m < 0 && p > 0) m = sci(SCI_BRACEMATCH, p - 1, 0); if (m >= 0) sci(SCI_GOTOPOS, m); }
    void toggleBookmark() { const int l = static_cast<int>(sci(SCI_LINEFROMPOSITION, sci(SCI_GETCURRENTPOS))); if (sci(SCI_MARKERGET, l) & (1 << MARK_BOOKMARK)) sci(SCI_MARKERDELETE, l, MARK_BOOKMARK); else sci(SCI_MARKERADD, l, MARK_BOOKMARK); }
    void gotoBookmark(bool fwd)
    {
        int l = static_cast<int>(sci(SCI_LINEFROMPOSITION, sci(SCI_GETCURRENTPOS)));
        sptr_t t = fwd ? sci(SCI_MARKERNEXT, l + 1, 1 << MARK_BOOKMARK) : sci(SCI_MARKERPREVIOUS, l - 1, 1 << MARK_BOOKMARK);
        if (t < 0) t = fwd ? sci(SCI_MARKERNEXT, 0, 1 << MARK_BOOKMARK) : sci(SCI_MARKERPREVIOUS, sci(SCI_GETLINECOUNT), 1 << MARK_BOOKMARK);
        if (t >= 0) sci(SCI_GOTOLINE, t);
    }

    // ----- view toggles --------------------------------------------------
    void syncToggle(int id, bool& flag) { flag = !flag; if (GetMenuBar()) GetMenuBar()->Check(id, flag); if (GetToolBar()) GetToolBar()->ToggleTool(id, flag); }
    void toggleWrap()  { syncToggle(IDM_VIEW_WRAP, m_wrap); sci(SCI_SETWRAPMODE, m_wrap ? SC_WRAP_WORD : SC_WRAP_NONE); }
    void toggleWs()    { syncToggle(IDM_VIEW_ALL_CHARACTERS, m_ws); sci(SCI_SETVIEWWS, m_ws ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE); sci(SCI_SETVIEWEOL, m_ws ? 1 : 0); }
    void toggleGuides(){ syncToggle(IDM_VIEW_INDENT_GUIDE, m_guides); sci(SCI_SETINDENTATIONGUIDES, m_guides ? SC_IV_LOOKBOTH : SC_IV_NONE); }

    // ----- dark / light theme -------------------------------------------
    // Parse Notepad++'s real theme XML (dark = themes/DarkModeDefault.xml, light = stylers.model.xml)
    // deployed next to the exe, so the editor uses Notepad++'s exact colours.
    void loadTheme()
    {
        const wxString dir  = wxPathOnly(wxStandardPaths::Get().GetExecutablePath());
        const wxString path = m_dark ? dir + "\\themes\\DarkModeDefault.xml" : dir + "\\stylers.model.xml";
        wxXmlDocument doc;
        if (!wxFileExists(path) || !doc.Load(path) || !doc.GetRoot()) return;   // fall back to built-in palette
        for (wxXmlNode* sec = doc.GetRoot()->GetChildren(); sec; sec = sec->GetNext())
        {
            if (sec->GetName() == "LexerStyles")
            {
                for (wxXmlNode* lt = sec->GetChildren(); lt; lt = lt->GetNext())
                {
                    if (lt->GetName() != "LexerType") continue;
                    std::vector<StyleDef> styles;
                    for (wxXmlNode* w = lt->GetChildren(); w; w = w->GetNext())
                    {
                        if (w->GetName() != "WordsStyle") continue;
                        long id = -1, fs = 0;
                        w->GetAttribute("styleID", "-1").ToLong(&id);
                        w->GetAttribute("fontStyle", "0").ToLong(&fs);
                        styles.push_back({ (int)id, npp_bgr(w->GetAttribute("fgColor")),
                                           npp_bgr(w->GetAttribute("bgColor")), (int)fs });
                    }
                    m_theme.lexers[lt->GetAttribute("name")] = std::move(styles);
                }
            }
            else if (sec->GetName() == "GlobalStyles")
            {
                for (wxXmlNode* w = sec->GetChildren(); w; w = w->GetNext())
                {
                    if (w->GetName() != "WidgetStyle") continue;
                    const wxString nm = w->GetAttribute("name");
                    m_theme.global[nm] = { npp_bgr(w->GetAttribute("fgColor")), npp_bgr(w->GetAttribute("bgColor")) };
                    if (nm == "Default Style")
                    {
                        m_theme.defaultFont = w->GetAttribute("fontName").ToStdString();
                        long sz = 0; if (w->GetAttribute("fontSize", "0").ToLong(&sz)) m_theme.defaultSize = (int)sz;
                    }
                }
            }
        }
        m_theme.loaded = true;
    }
    void applyEditorTheme(bool dark)
    {
        if (m_theme.loaded)
        {
            auto G = [&](const char* n) -> std::pair<int,int> {
                auto it = m_theme.global.find(n); return it == m_theme.global.end() ? std::make_pair(-1,-1) : it->second; };
            const auto def = G("Default Style");
            sci(SCI_STYLESETBACK, STYLE_DEFAULT, def.second >= 0 ? def.second : (dark ? 0x1E1E1E : 0xFFFFFF));
            sci(SCI_STYLESETFORE, STYLE_DEFAULT, def.first  >= 0 ? def.first  : (dark ? 0xDCDCDC : 0x000000));
            if (!m_theme.defaultFont.empty()) sci(SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<sptr_t>(m_theme.defaultFont.c_str()));
            if (m_theme.defaultSize > 0)      sci(SCI_STYLESETSIZE, STYLE_DEFAULT, m_theme.defaultSize);
            sci(SCI_STYLECLEARALL);                                  // propagate default to every style
            const auto ln = G("Line number margin");
            const int gutterBg = ln.second >= 0 ? ln.second : (dark ? 0x2D2D2D : 0xE0E0E0);
            sci(SCI_STYLESETFORE, STYLE_LINENUMBER, ln.first >= 0 ? ln.first : 0x808080);
            sci(SCI_STYLESETBACK, STYLE_LINENUMBER, gutterBg);
            sci(SCI_SETMARGINBACKN, 1, gutterBg);   // bookmark margin matches line-number margin (one-tone gutter)
            const auto ig = G("Indent guideline style"); if (ig.first >= 0) sci(SCI_STYLESETFORE, STYLE_INDENTGUIDE, ig.first);
            const auto car = G("Caret colour");          sci(SCI_SETCARETFORE, car.first >= 0 ? car.first : (dark ? 0xFFFFFF : 0x000000));
            sci(SCI_SETCARETLINEVISIBLE, 1);
            const auto cur = G("Current line background colour"); sci(SCI_SETCARETLINEBACK, cur.second >= 0 ? cur.second : (dark ? 0x2A2A2A : 0xF6F6F6));
            const auto sel = G("Selected text colour");  sci(SCI_SETSELBACK, 1, sel.second >= 0 ? sel.second : (dark ? 0x515151 : 0xC8C8C8));
            const auto wsp = G("White space symbol");    sci(SCI_SETWHITESPACEFORE, 1, wsp.first >= 0 ? wsp.first : (dark ? 0x606060 : 0xB0B0B0));
            applyBraceStyles(dark);
            return;
        }
        // fallback: built-in palette when no theme XML is present
        const int bg = dark ? 0x1E1E1E : 0xFFFFFF, fg = dark ? 0xDCDCDC : 0x000000;
        sci(SCI_STYLESETBACK, STYLE_DEFAULT, bg);
        sci(SCI_STYLESETFORE, STYLE_DEFAULT, fg);
        sci(SCI_STYLECLEARALL);
        sci(SCI_STYLESETBACK, STYLE_LINENUMBER, dark ? 0x2D2D2D : 0xE0E0E0);
        sci(SCI_STYLESETFORE, STYLE_LINENUMBER, 0x808080);
        sci(SCI_SETMARGINBACKN, 1, dark ? 0x2D2D2D : 0xE0E0E0);
        sci(SCI_SETCARETFORE, dark ? 0xFFFFFF : 0x000000);
        sci(SCI_SETCARETLINEVISIBLE, 1);
        sci(SCI_SETCARETLINEBACK, dark ? 0x2A2A2A : 0xF6F6F6);
        sci(SCI_SETSELBACK, 1, dark ? 0x515151 : 0xC8C8C8);
        sci(SCI_SETWHITESPACEFORE, 1, dark ? 0x606060 : 0xB0B0B0);
        applyBraceStyles(dark);
    }
    // Matched-brace pair (green, bold) and unmatched brace (red) - reapplied after every STYLECLEARALL.
    void applyBraceStyles(bool dark)
    {
        sci(SCI_STYLESETFORE, STYLE_BRACELIGHT, dark ? 0x4DC44D : 0x008000);   // BGR green
        sci(SCI_STYLESETBOLD, STYLE_BRACELIGHT, 1);
        sci(SCI_STYLESETFORE, STYLE_BRACEBAD, 0x2222CC);                       // BGR red
        sci(SCI_STYLESETBOLD, STYLE_BRACEBAD, 1);
    }
    void setTitleBarDark(bool dark)
    {
#ifdef __WXMSW__
        BOOL v = dark ? TRUE : FALSE;
        ::DwmSetWindowAttribute(static_cast<HWND>(GetHandle()), DWMWA_USE_IMMERSIVE_DARK_MODE, &v, sizeof(v));
#else
        (void)dark;
#endif
    }
    void applyTheme(bool dark)
    {
        m_dark = dark;
        applyEditorTheme(dark);
        setTitleBarDark(dark);
#ifdef __WXMSW__
        ::SetWindowTheme(m_sci, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);  // dark/light native scrollbars
#endif
        if (GetMenuBar()) GetMenuBar()->Check(myID_DARKMODE, dark);
        const wxColour chromeBg = dark ? wxColour(32, 32, 32) : wxColour(240, 240, 240);   // explicit both ways
        const wxColour chromeFg = dark ? wxColour(220, 220, 220) : wxColour(0, 0, 0);
        if (auto* tb = GetToolBar()) { tb->SetBackgroundColour(chromeBg); tb->Refresh(); }
        if (auto* sb = GetStatusBar()) { sb->SetBackgroundColour(chromeBg); sb->SetForegroundColour(chromeFg); sb->Refresh(); }
        if (m_tabs) { m_tabs->SetBackgroundColour(chromeBg); m_tabs->Refresh(); }
    }

    // wxWidgets refuses to re-theme live: wxApp::SetAppearance() returns CannotChange once a
    // top-level window exists, and MSWEnableDarkMode is startup-only, so the native menu bar and
    // toolbar bake in their theme at creation (mixing them yields "a horrible mix of light and
    // dark mode elements", per wx's own source). We therefore persist the choice and relaunch:
    // each process is born in ONE consistent mode (light skips MSWEnableDarkMode entirely).
    void restartWithTheme(bool dark)
    {
        auto* cfg = wxConfigBase::Get();
        cfg->Write("DarkMode", dark);
        saveSession(cfg);                     // remember open files so the relaunch restores them
        cfg->Flush();
#ifdef __WXMSW__
        ::RemoveWindowSubclass(static_cast<HWND>(GetHandle()), FrameSubclassProc, 1);
#endif
        wxExecute("\"" + wxStandardPaths::Get().GetExecutablePath() + "\"", wxEXEC_ASYNC);
        Close(true);
    }
    void saveSession(wxConfigBase* cfg)
    {
        int count = 0, active = -1;
        const int sel = m_tabs->GetSelection();
        for (size_t i = 0; i < m_tabs->GetPageCount(); ++i)
        {
            auto* p = static_cast<EditorPage*>(m_tabs->GetPage(i));
            if (!p || p->path.empty()) continue;               // only saved files can be restored
            if ((int)i == sel) active = count;
            cfg->Write(wxString::Format("Session/File%d", count), p->path);
            ++count;
        }
        cfg->Write("Session/Count", (long)count);
        cfg->Write("Session/Active", (long)active);
        cfg->Write("Session/Pending", true);
    }

    // wxMessageBox uses a native dialog that ignores the app theme; child dialogs also need
    // explicit colours. Apply the current dark/light theme (incl. dark title bar) to a sub-window.
    void themeDialog(wxWindow* w)
    {
        if (!w) return;
#ifdef __WXMSW__
        BOOL v = m_dark ? TRUE : FALSE;
        ::DwmSetWindowAttribute(static_cast<HWND>(w->GetHandle()), DWMWA_USE_IMMERSIVE_DARK_MODE, &v, sizeof(v));
#endif
        std::function<void(wxWindow*)> rec = [&](wxWindow* x) {
            // Leave edit/combo controls to the native DarkMode_CFD theme (applied below); forcing a
            // custom background on them fights the theme and leaves the edit field white.
            const bool editLike = wxDynamicCast(x, wxComboBox) != nullptr || wxDynamicCast(x, wxTextCtrl) != nullptr;
            if (!editLike) {
                if (m_dark) { x->SetBackgroundColour(wxColour(45, 45, 45)); x->SetForegroundColour(wxColour(220, 220, 220)); }
                else        { x->SetBackgroundColour(wxNullColour);          x->SetForegroundColour(wxNullColour); }
            }
            for (wxWindow* c : x->GetChildren()) rec(c);
        };
        rec(w);
#ifdef __WXMSW__
        ::EnumChildWindows(static_cast<HWND>(w->GetHandle()), themeChildProc, m_dark ? 1 : 0);   // theme native sub-controls (spin/combo/scrollbar)
#endif
        w->Refresh();
    }
    void notImpl(const wxString& what) { setStatus(0, what + " - needs the full Notepad++ app (not in the wx shell prototype)"); m_hint = true; }
    void setStatus(int field, const wxString& text) { SetStatusText(" " + text, field); }  // leading space ~ 4px left margin, like Notepad++
    void showAbout()
    {
        wxDialog dlg(this, wxID_ANY, "About");
        auto* s = new wxBoxSizer(wxVERTICAL);
        s->Add(new wxStaticText(&dlg, wxID_ANY,
                   "Notepad++ -> wxWidgets main-window shell.\n\n"
                   "Native Scintilla editor + Notepad++ menu/toolbar (real IDM_* ids and icon\n"
                   "pack), editor-backed commands fully wired, and a dark/light theme."),
               0, wxALL, 16);
        s->Add(dlg.CreateButtonSizer(wxOK), 0, wxALL | wxALIGN_RIGHT, 10);
        dlg.SetSizerAndFit(s);
        themeDialog(&dlg);
        dlg.ShowModal();
    }

    // ----- the one dispatcher -------------------------------------------
    void onCommand(wxCommandEvent& e)
    {
        if (!m_stc) { e.Skip(); return; }
        m_hint = false;   // a fresh command clears any lingering "needs full app" hint
        const int cmd = e.GetId() & 0xFFFF;
        if (e.GetId() >= wxID_FILE1 && e.GetId() <= wxID_FILE9)           // Recent Files (MRU) entry
        {
            const int i = e.GetId() - wxID_FILE1;
            const wxString f = m_fileHistory.GetHistoryFile(i);
            if (wxFileExists(f)) openPath(f);
            else { m_fileHistory.RemoveFileFromHistory(i); auto* c = wxConfigBase::Get(); c->SetPath("/RecentFiles"); m_fileHistory.Save(*c); c->SetPath("/"); }
            return;
        }
#ifdef __WXMSW__
        if (!m_plugins.empty() && cmd >= PLUGIN_CMD_BASE &&
            cmd < PLUGIN_CMD_BASE + static_cast<int>(m_plugins.size()) * 100)   // plugin menu command (use cmd: 62000 sign-extends negative)
        {
            const int rel = cmd - PLUGIN_CMD_BASE;
            const size_t pi = rel / 100; const int fi = rel % 100;
            if (pi < m_plugins.size() && fi < m_plugins[pi].count && m_plugins[pi].funcs[fi]._pFunc)
                m_plugins[pi].funcs[fi]._pFunc();
            return;
        }
#endif
        if (cmd >= myID_DOCLIST_ITEM && cmd < myID_DOCLIST_ITEM + 1000)   // document-list dropdown entry
        { const size_t n = (size_t)(cmd - myID_DOCLIST_ITEM); if (n < m_tabs->GetPageCount()) m_tabs->SetSelection(n); return; }
        // Win32 WM_COMMAND carries only a 16-bit id and wx sign-extends it, so command ids
        // above 32767 (all of Notepad++'s IDM_*) arrive negative. Read it as unsigned 16-bit,
        // exactly as Notepad++ does with LOWORD(wParam).
        switch (e.GetId() & 0xFFFF)
        {
            case IDM_FILE_NEW: doNew(); break;
            case IDM_FILE_OPEN: onOpen(); break;
            case IDM_FILE_RELOAD: onReload(); break;
            case IDM_FILE_SAVE: case IDM_FILE_SAVEALL: onSave(); break;
            case IDM_FILE_SAVEAS: onSaveAs(); break;
            case IDM_FILE_CLOSE: closeActive(); break;
            case IDM_FILE_CLOSEALL: closeAll(); break;
            case IDM_FILE_CLOSEALL_BUT_CURRENT: closeAllBut(activePage()); break;
            case IDM_FILE_EXIT: Close(true); break;

            case IDM_EDIT_UNDO: sci(SCI_UNDO); break;
            case IDM_EDIT_REDO: sci(SCI_REDO); break;
            case IDM_EDIT_CUT: sci(SCI_CUT); break;
            case IDM_EDIT_COPY: sci(SCI_COPY); break;
            case IDM_EDIT_PASTE: sci(SCI_PASTE); break;
            case IDM_EDIT_DELETE: sci(SCI_CLEAR); break;
            case IDM_EDIT_SELECTALL: sci(SCI_SELECTALL); break;
            case IDM_EDIT_UPPERCASE: sci(SCI_UPPERCASE); break;
            case IDM_EDIT_LOWERCASE: sci(SCI_LOWERCASE); break;
            case IDM_EDIT_INVERTCASE: transformSel([](std::string& s){ for (char& c : s) c = (char)(std::isupper((unsigned char)c) ? std::tolower((unsigned char)c) : std::toupper((unsigned char)c)); }); break;
            case IDM_EDIT_PROPERCASE_FORCE: transformSel([](std::string& s){ bool st = true; for (char& c : s){ if (std::isalpha((unsigned char)c)){ c = (char)(st ? std::toupper((unsigned char)c) : std::tolower((unsigned char)c)); st = false; } else st = true; } }); break;
            case IDM_EDIT_SENTENCECASE_FORCE: transformSel([](std::string& s){ bool st = true; for (char& c : s){ if (std::isalpha((unsigned char)c)){ c = (char)(st ? std::toupper((unsigned char)c) : std::tolower((unsigned char)c)); st = false; } else if (c=='.'||c=='!'||c=='?') st = true; } }); break;
            case IDM_EDIT_DUP_LINE: sci(SCI_LINEDUPLICATE); break;
            case IDM_EDIT_LINE_UP: sci(SCI_MOVESELECTEDLINESUP); break;
            case IDM_EDIT_LINE_DOWN: sci(SCI_MOVESELECTEDLINESDOWN); break;
            case IDM_EDIT_REMOVEEMPTYLINES: removeEmptyLines(); break;
            case IDM_EDIT_TRIMTRAILING: trimTrailing(); break;
            case IDM_EDIT_TRIMLINEHEAD: trimLeading(); break;
            case IDM_EDIT_TRIM_BOTH: trimBoth(); break;
            case IDM_EDIT_EOL2WS: eolToSpace(); break;
            case IDM_EDIT_TRIMALL: trimBoth(); eolToSpace(); break;
            case IDM_EDIT_TAB2SW: tabsToSpaces(); break;
            case IDM_EDIT_INS_TAB: sci(SCI_TAB); break;
            case IDM_EDIT_RMV_TAB: sci(SCI_BACKTAB); break;
            case IDM_EDIT_JOIN_LINES: linesJoin(); break;
            case IDM_EDIT_SPLIT_LINES: linesSplit(); break;
            case IDM_EDIT_BLANKLINEABOVECURRENT: blankLine(false); break;
            case IDM_EDIT_BLANKLINEBELOWCURRENT: blankLine(true); break;
            case IDM_EDIT_REMOVE_ANY_DUP_LINES: removeDuplicateLines(false); break;
            case IDM_EDIT_REMOVE_CONSECUTIVE_DUP_LINES: removeDuplicateLines(true); break;
            case IDM_EDIT_SORTLINES_REVERSE_ORDER: reverseLines(); break;
            case IDM_EDIT_SORTLINES_LEXICOGRAPHIC_ASCENDING: sortLines(0, false); break;
            case IDM_EDIT_SORTLINES_LEXICOGRAPHIC_DESCENDING: sortLines(0, true); break;
            case IDM_EDIT_SORTLINES_LEXICO_CASE_INSENS_ASCENDING: sortLines(1, false); break;
            case IDM_EDIT_SORTLINES_LEXICO_CASE_INSENS_DESCENDING: sortLines(1, true); break;
            case IDM_EDIT_SORTLINES_INTEGER_ASCENDING: sortLines(2, false); break;
            case IDM_EDIT_SORTLINES_INTEGER_DESCENDING: sortLines(2, true); break;
            case IDM_EDIT_INSERT_DATETIME_SHORT: insertDateTime(false); break;
            case IDM_EDIT_INSERT_DATETIME_LONG: insertDateTime(true); break;
            case IDM_EDIT_BLOCK_COMMENT: toggleLineComment(); break;
            case IDM_FORMAT_TODOS: setEol(SC_EOL_CRLF); break;
            case IDM_FORMAT_TOUNIX: setEol(SC_EOL_LF); break;
            case IDM_FORMAT_TOMAC: setEol(SC_EOL_CR); break;

            case IDM_SEARCH_FIND: onFind(); break;
            case IDM_SEARCH_FINDNEXT: findNext(true); break;
            case IDM_SEARCH_FINDPREV: findNext(false); break;
            case IDM_SEARCH_REPLACE: onReplace(); break;
            case IDM_SEARCH_GOTOLINE: onGoTo(); break;
            case IDM_SEARCH_GOTOMATCHINGBRACE: gotoMatchingBrace(); break;
            case IDM_SEARCH_TOGGLE_BOOKMARK: toggleBookmark(); break;
            case IDM_SEARCH_NEXT_BOOKMARK: gotoBookmark(true); break;
            case IDM_SEARCH_PREV_BOOKMARK: gotoBookmark(false); break;
            case IDM_SEARCH_CLEAR_BOOKMARKS: sci(SCI_MARKERDELETEALL, MARK_BOOKMARK); break;

            case IDM_VIEW_ZOOMIN: sci(SCI_ZOOMIN); break;
            case IDM_VIEW_ZOOMOUT: sci(SCI_ZOOMOUT); break;
            case IDM_VIEW_ZOOMRESTORE: sci(SCI_SETZOOM, 0); break;
            case IDM_VIEW_WRAP: toggleWrap(); break;
            case IDM_VIEW_ALL_CHARACTERS: toggleWs(); break;
            case IDM_VIEW_INDENT_GUIDE: toggleGuides(); break;
            case IDM_VIEW_TAB_SPACE: toggleWs(); break;            // "Show Space and Tab"
            case IDM_VIEW_EOL: sci(SCI_SETVIEWEOL, sci(SCI_GETVIEWEOL) ? 0 : 1); break;
            case IDM_VIEW_TAB_NEXT: m_tabs->AdvanceSelection(true); break;
            case IDM_VIEW_TAB_PREV: m_tabs->AdvanceSelection(false); break;
            case IDM_VIEW_TAB_START: if (m_tabs->GetPageCount()) m_tabs->SetSelection(0); break;
            case IDM_VIEW_TAB_END:   if (m_tabs->GetPageCount()) m_tabs->SetSelection(m_tabs->GetPageCount() - 1); break;
            case IDM_VIEW_TAB1: case IDM_VIEW_TAB2: case IDM_VIEW_TAB3: case IDM_VIEW_TAB4: case IDM_VIEW_TAB5:
            case IDM_VIEW_TAB6: case IDM_VIEW_TAB7: case IDM_VIEW_TAB8: case IDM_VIEW_TAB9:
                { const size_t n = (size_t)((e.GetId() & 0xFFFF) - IDM_VIEW_TAB1); if (n < m_tabs->GetPageCount()) m_tabs->SetSelection(n); break; }
            case myID_DARKMODE: restartWithTheme(!m_dark); break;   // relaunch in the other mode (wx can't switch live)
            case myID_CAP_NEW: doNew(); break;                      // "+" caption button
            case myID_CAP_CLOSE: closeActive(); break;             // "x" caption button
            case myID_DOCLIST: onDocList(); break;                 // "v" caption dropdown
            case IDM_VIEW_FULLSCREENTOGGLE: ShowFullScreen(!IsFullScreen()); break;
            case IDM_VIEW_FOLDALL: sci(SCI_FOLDALL, SC_FOLDACTION_CONTRACT); break;
            case IDM_VIEW_UNFOLDALL: sci(SCI_FOLDALL, SC_FOLDACTION_EXPAND); break;
            case IDM_VIEW_FOLD_CURRENT: foldCurrent(true); break;
            case IDM_VIEW_UNFOLD_CURRENT: foldCurrent(false); break;
            case IDM_VIEW_FOLD_1: case IDM_VIEW_FOLD_2: case IDM_VIEW_FOLD_3: case IDM_VIEW_FOLD_4:
            case IDM_VIEW_FOLD_5: case IDM_VIEW_FOLD_6: case IDM_VIEW_FOLD_7: case IDM_VIEW_FOLD_8:
                foldToLevel((e.GetId() & 0xFFFF) - IDM_VIEW_FOLD_1 + 1, true); break;
            case IDM_VIEW_UNFOLD_1: case IDM_VIEW_UNFOLD_2: case IDM_VIEW_UNFOLD_3: case IDM_VIEW_UNFOLD_4:
            case IDM_VIEW_UNFOLD_5: case IDM_VIEW_UNFOLD_6: case IDM_VIEW_UNFOLD_7: case IDM_VIEW_UNFOLD_8:
                foldToLevel((e.GetId() & 0xFFFF) - IDM_VIEW_UNFOLD_1 + 1, false); break;

            case wxID_ABOUT: case IDM_ABOUT: showAbout(); break;

            case IDM_FILE_PRINT: notImpl("Print"); break;
            case IDM_VIEW_DOC_MAP: notImpl("Document Map panel"); break;
            case IDM_VIEW_FUNC_LIST: notImpl("Function List panel"); break;
            case IDM_VIEW_DOCLIST: notImpl("Document List panel"); break;
            case IDM_VIEW_FILEBROWSER: notImpl("Folder as Workspace panel"); break;
            case IDM_VIEW_MONITORING: notImpl("File monitoring"); break;
            case IDM_VIEW_SYNSCROLLV: case IDM_VIEW_SYNSCROLLH: notImpl("Split-view sync scrolling"); break;
            case IDM_MACRO_STARTRECORDINGMACRO: case IDM_MACRO_STOPRECORDINGMACRO:
            case IDM_MACRO_PLAYBACKRECORDEDMACRO: case IDM_MACRO_RUNMULTIMACRODLG:
            case IDM_MACRO_SAVECURRENTMACRO: notImpl("Macros"); break;
            case IDM_LANG_USER_DLG: notImpl("User-Defined Language dialog"); break;
            default: {
                // Any real menu/toolbar item we don't implement: name it so the click isn't silent.
                const int mid = e.GetId() & 0xFFFF;
                wxString lbl;
                if (auto* mb = GetMenuBar()) if (wxMenuItem* it = mb->FindItem(mid)) lbl = it->GetItemLabelText();
                if (lbl.empty()) { e.Skip(); return; }   // not one of ours -> let wx handle it
                notImpl(lbl);
                break;
            }
        }
        updateStatus();
    }

    void updateLineMargin()   // right-justified numbers in a width sized to the digit count
    {
        const int lines = static_cast<int>(sci(SCI_GETLINECOUNT));
        int digits = 1; for (int n = lines; n >= 10; n /= 10) ++digits;
        if (digits < 2) digits = 2;
        const std::string nines(static_cast<size_t>(digits), '9');
        const int w = static_cast<int>(sci(SCI_TEXTWIDTH, STYLE_LINENUMBER, reinterpret_cast<sptr_t>(nines.c_str())));
        sci(SCI_SETMARGINWIDTHN, 0, w + 10);   // small left pad + a little right pad; right-justified, flush to text
    }
    // A zoom change (Ctrl+wheel / Zoom In-Out) is persisted. With one shared view, zoom is inherently
    // the same for every tab, so there's nothing to mirror (unlike the old per-tab-HWND model).
    void onZoomChanged(int z)
    {
        if (z == m_zoom) return;
        m_zoom = z;
        updateLineMargin();
        wxConfigBase::Get()->Write("Zoom", static_cast<long>(z)); wxConfigBase::Get()->Flush();
    }
    void updateStatus()
    {
        if (!m_stc) return;
        updateLineMargin();
        refreshTab(activePage());        // keep the unsaved "*" marker live
        if (!GetStatusBar()) return;     // status bar not built yet (initial document creation)
        const int len = static_cast<int>(sci(SCI_GETLENGTH)), nl = static_cast<int>(sci(SCI_GETLINECOUNT));
        const int pos = static_cast<int>(sci(SCI_GETCURRENTPOS));
        const int line = static_cast<int>(sci(SCI_LINEFROMPOSITION, pos)) + 1, col = static_cast<int>(sci(SCI_GETCOLUMN, pos)) + 1;
        const int selA = static_cast<int>(sci(SCI_GETSELECTIONSTART)), selB = static_cast<int>(sci(SCI_GETSELECTIONEND));
        const int sel = selB - selA;
        const int selLines = sel > 0 ? static_cast<int>(sci(SCI_LINEFROMPOSITION, selB)) - static_cast<int>(sci(SCI_LINEFROMPOSITION, selA)) + 1 : 0;
        const int eol = static_cast<int>(sci(SCI_GETEOLMODE));
        if (!m_hint) setStatus(0, activePage() ? activePage()->lang : "Normal text file");   // language label; hint persists until next command
        setStatus(1, wxString::Format("length : %d    lines : %d", len, nl));
        setStatus(2, wxString::Format("Ln : %d    Col : %d    Pos : %d", line, col, pos + 1));
        setStatus(3, wxString::Format("Sel : %d | %d", sel, selLines));
        setStatus(4, eol == SC_EOL_CRLF ? "Windows (CR LF)" : eol == SC_EOL_LF ? "Unix (LF)" : "Macintosh (CR)");
        setStatus(5, "UTF-8");
        setStatus(6, sci(SCI_GETOVERTYPE) ? "OVR" : "INS");   // typing mode, toggled by the Insert key
    }

    // Gray out toolbar buttons and menu items that don't apply right now (Save when clean, Undo/Redo
    // when there's no history, Cut/Copy/Delete with no selection, Paste with an empty clipboard) -
    // exactly like Notepad++. Cached so we only touch the UI when a state actually flips.
    void updateUiState()
    {
        if (!m_stc) return;
        const bool dirty    = sci(SCI_GETMODIFY) != 0;
        const bool canUndo  = sci(SCI_CANUNDO) != 0;
        const bool canRedo  = sci(SCI_CANREDO) != 0;
        const bool hasSel   = sci(SCI_GETSELECTIONEMPTY) == 0;
        const bool canPaste = sci(SCI_CANPASTE) != 0;
        if (auto* ap = activePage()) ap->dirty = dirty;   // keep the active tab's cached flag current (others use their last-active value)
        bool anyDirty = dirty;
        if (m_tabs)
            for (size_t i = 0; i < m_tabs->GetPageCount() && !anyDirty; ++i)
            {
                auto* p = static_cast<EditorPage*>(m_tabs->GetPage(i));
                if (p && p->dirty) anyDirty = true;
            }
        if (dirty == m_stSave && anyDirty == m_stSaveAll && canUndo == m_stUndo && canRedo == m_stRedo &&
            hasSel == m_stSel && canPaste == m_stPaste)
            return;   // nothing changed
        m_stSave = dirty; m_stSaveAll = anyDirty; m_stUndo = canUndo; m_stRedo = canRedo;
        m_stSel = hasSel; m_stPaste = canPaste;
        if (auto* tb = GetToolBar())
        {
            tb->EnableTool(IDM_FILE_SAVE, dirty);   tb->EnableTool(IDM_FILE_SAVEALL, anyDirty);
            tb->EnableTool(IDM_EDIT_UNDO, canUndo); tb->EnableTool(IDM_EDIT_REDO, canRedo);
            tb->EnableTool(IDM_EDIT_CUT, hasSel);   tb->EnableTool(IDM_EDIT_COPY, hasSel);
            tb->EnableTool(IDM_EDIT_PASTE, canPaste);
        }
        if (auto* mb = GetMenuBar())
        {
            mb->Enable(IDM_FILE_SAVE, dirty);   mb->Enable(IDM_FILE_SAVEALL, anyDirty);
            mb->Enable(IDM_EDIT_UNDO, canUndo); mb->Enable(IDM_EDIT_REDO, canRedo);
            mb->Enable(IDM_EDIT_CUT, hasSel);   mb->Enable(IDM_EDIT_COPY, hasSel);
            mb->Enable(IDM_EDIT_PASTE, canPaste); mb->Enable(IDM_EDIT_DELETE, hasSel);
        }
    }

    wxAuiNotebook* m_tabs = nullptr;
    wxPanel*       m_capBar = nullptr;   // +/v/x caption buttons on the tab strip
    FindReplaceDialog* m_findDlg = nullptr;   // modeless Find/Replace dialog
    wxFileHistory      m_fileHistory;         // Recent Files (MRU), persisted in wxConfig
    wxStyledTextCtrl* m_stc = nullptr;   // the cross-platform editor view; its native HWND on Windows == m_sci
#ifdef __WXMSW__
    HWND        m_sci  = nullptr;
    NppData     m_npp{};
    // ----- loaded Win32 plugins -----
    struct LoadedPlugin {
        HMODULE             lib = nullptr;
        std::wstring        name;
        PFUNCSETINFO        setInfo = nullptr;
        PFUNCGETFUNCSARRAY  getFuncs = nullptr;
        PBENOTIFIED         beNotified = nullptr;
        PMESSAGEPROC        messageProc = nullptr;
        FuncItem*           funcs = nullptr;
        int                 count = 0;
    };
    std::vector<LoadedPlugin> m_plugins;
#endif
    wxAuiManager m_aui;                          // hosts plugin docking panels around the editor
#ifdef __WXMSW__
    struct PluginDock { HWND hClient; wxPanel* host; wxString name; };
    std::vector<PluginDock> m_docks;
    static const int PLUGIN_CMD_BASE = 62000;   // plugin menu command ids: above doc-list (61xxx), clear of all IDM_* (40000-~50000)
#endif
    wxTimer     m_timer;
    wxString    m_path, m_lastFind, m_lastReplace;
    bool        m_wrap = false, m_ws = false, m_guides = false, m_dark = true;
    bool        m_hint = false;   // a "needs full app" message is showing in status field 0
    // cached toolbar/menu enable states (start enabled, matching the freshly-built toolbar)
    bool        m_stSave = true, m_stSaveAll = true, m_stUndo = true, m_stRedo = true, m_stSel = true, m_stPaste = true;
    int         m_newCount = 0;   // counter for "new N" tab titles
    int         m_zoom = 0;       // shared zoom level across all tabs (Ctrl+wheel), persisted
    NppTheme    m_theme;          // exact Notepad++ colours (loaded from theme XML)
};

class NppApp : public wxApp
{
public:
    bool OnInit() override
    {
        SetAppName("wxNotepadPlusPlus_spike");                  // stable key for the persisted theme choice
        bool dark = true;                                      // default: dark, matching Notepad++
        wxConfigBase::Get()->Read("DarkMode", &dark, true);
#ifdef __WXMSW__
        if (dark)
            MSWEnableDarkMode(DarkMode_Always);                // native dark chrome ONLY in dark mode; light
                                                               // mode never enables it, so it stays fully native-light.
                                                               // MSW-only API; other platforms theme via wx below.
#endif
        auto* frame = new NppShellFrame(dark);
        frame->Show(true);
        frame->restoreSession();                                   // reopen files from a theme-restart
        for (int i = 1; i < argc; ++i) frame->openPath(argv[i]);   // open any files passed on the command line
        return true;
    }
};

wxIMPLEMENT_APP(NppApp);
