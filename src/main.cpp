// wxNotepad++  |  cross-platform main-window shell
// ---------------------------------------------------------------------------
// A wxWidgets reproduction of the Notepad++ UI, pursuing a close match with the
// native (default/light) look while building cross-platform (Windows/Linux/macOS):
//   * wxMenuBar with Notepad++-style menu labels + the project's OWN clean-room
//     IDM_* command ids (include/npp-compat/menuCmdID.h, Apache-2.0)
//   * wxToolBar using the project's own MIT icon set (resources/icons/*.svg)
//   * wxAuiNotebook tab strip + a wxStyledTextCtrl editor (bundles Scintilla + Lexilla)
//   * 6-field status bar, updated live
//   * the application's own icon (src/app_icon_svg.h)
//   * plugins via the core's OWN permissive "Nib" API (include/nib/nib.h); real
//     Notepad++ binary plugins load through the optional GPL packages/npp-bridge -
//     the core itself reproduces NO Notepad++ plugin ABI.
//
// Commands are routed through one onCommand() dispatcher. Editor-backed functions
// (file I/O, clipboard, case/EOL/line ops, comment, find/replace, bookmarks, brace
// match, zoom, wrap/whitespace/guides, full screen) are implemented against
// Scintilla via wxStyledTextCtrl.

#include <wx/wx.h>
#include <wx/notebook.h>
#include <wx/listbook.h>       // wxListbook - the Preferences dialog's left-side page selector
#include <wx/clrpicker.h>      // wxColourPickerCtrl - Style Configurator foreground/background pickers
#include <wx/aui/auibook.h>
#include <wx/aui/aui.h>          // wxAuiManager - dock host for plugin panels (NPPM_DMM*)
#include <wx/stc/stc.h>          // wxStyledTextCtrl - cross-platform editor (Phase 3 port target)
#include <wx/treectrl.h>         // wxTreeCtrl - Function List symbol tree
#include <wx/dirctrl.h>          // wxGenericDirCtrl - Folder as Workspace file browser
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
#include <wx/dir.h>             // wxDir - scan the plugins/ folder + Find-in-Files traversal
#include <wx/textfile.h>        // wxTextFile - read files line-by-line for Find in Files
#include <wx/dirdlg.h>          // wxDirDialog - folder picker for Find in Files

#ifdef WXNPP_HAS_BORDERLESS
#include <type_traits>                  // std::is_base_of - detect the borderless base in NppShellFrameT<FB>
#include <vector>                       // std::vector - accelerator-entry list for installAccelsFromMenuBar
#include <wxbf/borderless_frame.h>      // wxBorderlessFrame - the optional integrated/borderless title bar (Windows + GTK)
#endif

#ifdef __WXMSW__
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <shellapi.h>          // ShellExecuteW / SHFileOperationW - File menu shell commands (Explorer, cmd, Recycle Bin)
#include <bcrypt.h>            // BCrypt* - MD5 / SHA digests for the Tools menu
#pragma comment(lib, "bcrypt.lib")
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
#include <regex>               // std::regex - Function List symbol parsing (per-language rules)
#include <set>
#include <algorithm>
#include <cstdlib>
#include <random>              // std::shuffle - "Randomize Line Order"

#include <wx/clipbrd.h>        // wxClipboard - copy file paths / hashes to the clipboard
#include <wx/utils.h>          // wxExecute / wxLaunchDefaultBrowser / wxLaunchDefaultApplication
#include <wx/choicdlg.h>       // wxGetSingleChoiceIndex - the Window list dialog
#include <wx/textdlg.h>        // wxGetTextFromUser - name a saved macro

#include "Scintilla.h"
#include "ILexer.h"             // Scintilla::ILexer5 (required by Lexilla.h)
#include "Lexilla.h"            // CreateLexer() - syntax highlighting
#include "SciLexer.h"           // SCE_* lexer style numbers
#include <wx/dynlib.h>         // wxDynamicLibrary - portable .dll/.so/.dylib loader (Nib plugin host)
#include <cstring>             // strcmp / memcpy (Nib host)
#include "menuCmdID.h"
#include "app_icon_svg.h"
#include "nib.h"               // our own permissive, cross-platform plugin API (Nib)
#include "npp_menu.h"          // faithful 1:1 Notepad++ main-menu builder

static const int  MARK_BOOKMARK = 2;      // a free Scintilla marker number for bookmarks
static const int  MARK_INDIC    = 9;      // indicator number for "Mark All" highlights (Find dialog)
static const int  SMART_INDIC   = 10;     // indicator number for smart-highlight (double-click a word)
enum { myID_TIMER = 60000, myID_DARKMODE, myID_DOCLIST, myID_CAP_NEW, myID_CAP_CLOSE, myID_FLTIMER };   // fixed ids, above the IDM_* range
static const int myID_DOCLIST_ITEM = 61000;   // base id for the document-list dropdown entries
static const int myID_MACRO_ITEM   = 62100;   // base id for saved-macro entries at the bottom of the Macro menu

// The one persistent editor view (set by the frame), used to release a tab's Document when its
// EditorPage is destroyed - the notebook switches away first, so the doc holds only the buffer ref.
static wxStyledTextCtrl* g_view = nullptr;

// One editor tab: a wxPanel that owns a Scintilla Document (the single shared view swaps to it).
// On-disk text encoding of a buffer; the Scintilla document itself always holds UTF-8.
enum Enc { ENC_UTF8 = 0, ENC_UTF8_BOM, ENC_UTF16_LE, ENC_UTF16_BE, ENC_ANSI, ENC_CHARSET };

// One recorded Scintilla command in a macro (Macro menu: record / playback / run multiple).
struct MacroStep { int msg; uptr_t wparam; sptr_t lparam; bool hasText = false; std::string text; };

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
    bool     langForced = false;           // true once the user picks a language from the Language menu
    wxString forcedLexer;                  // that pick's Lexilla lexer name ("" = forced Normal Text)
    wxString forcedName;                   // that pick's display label for the status bar, e.g. "C++"
    int      encoding = ENC_UTF8;          // on-disk encoding (detected on load, written on save)
    int      codepage = 0;                 // when encoding == ENC_CHARSET: the Windows code page
    wxString encLabel;                     // when encoding == ENC_CHARSET: its status-bar label
};

// A parsed Notepad++ theme (stylers.model.xml / themes/*.xml).
struct StyleDef { int id; int fg; int bg; int fontStyle; wxString name; };   // fg/bg = -1 when unspecified
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

#ifdef __WXMSW__   // ---- the SCI_* bridge: route Scintilla messages sent to the editor HWND into wxStyledTextCtrl ----
// Win32 plugins reach the editor by SendMessage'ing SCI_* to the Scintilla HWND. But wxStyledTextCtrl
// services Scintilla messages only through SendMsg (a direct ScintillaWX call); its HWND's WndProc
// ignores them. Bridge it: subclass the wxSTC HWND and forward Scintilla-range messages (SCI_* live in
// 2000-2999, the lexer messages in 4000-4999) to the real editor via the frame's sci(). (Set by frame.)
static std::function<sptr_t(UINT, WPARAM, LPARAM)> g_sciForward;

// Mirrors the frame's dark state so the editor's non-client painting (below) runs only in dark mode.
static bool g_editorDark = false;

// DarkMode_Explorer darkens the scrollbars but NOT the little "dead corner" square where the horizontal and
// vertical scrollbars meet -- Windows fills it with a light system colour. When both bars are visible, repaint
// that square with the scrollbar background so it blends in. Border-agnostic: the corner is derived from the
// client rect vs the window rect, so any window border/edge is accounted for automatically.
static void paintScrollbarCorner(HWND h)
{
    SCROLLBARINFO vsi = { sizeof(vsi) }, hsi = { sizeof(hsi) };
    if (!::GetScrollBarInfo(h, OBJID_VSCROLL, &vsi) || !::GetScrollBarInfo(h, OBJID_HSCROLL, &hsi)) return;
    if ((vsi.rgstate[0] & STATE_SYSTEM_INVISIBLE) || (hsi.rgstate[0] & STATE_SYSTEM_INVISIBLE)) return;  // need BOTH bars

    RECT wr; ::GetWindowRect(h, &wr);
    RECT cr; ::GetClientRect(h, &cr);
    POINT org = { 0, 0 }; ::ClientToScreen(h, &org);                  // client origin in screen coords
    const LONG left = (org.x - wr.left) + cr.right;                   // client's right edge, in window-DC coords
    const LONG top  = (org.y - wr.top)  + cr.bottom;                  // client's bottom edge, in window-DC coords
    RECT corner = { left, top, left + ::GetSystemMetrics(SM_CXVSCROLL), top + ::GetSystemMetrics(SM_CYHSCROLL) };

    // The DarkMode_Explorer scrollbar track colour - a fixed Windows dark-theme system colour (not one of
    // our editor-theme colours), so it lives here rather than in applyEditorTheme. Cached once: WM_NCPAINT
    // fires on every scroll / resize / focus change.
    static const HBRUSH s_corner = ::CreateSolidBrush(RGB(23, 23, 23));
    HDC dc = ::GetWindowDC(h);
    ::FillRect(dc, &corner, s_corner);
    ::ReleaseDC(h, dc);
}

static LRESULT CALLBACK SciHwndProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR)
{
    if (g_sciForward && ((msg >= 2000 && msg < 3000) || (msg >= 4000 && msg < 5000)))
        return static_cast<LRESULT>(g_sciForward(msg, w, l));
    const LRESULT res = DefSubclassProc(h, msg, w, l);
    if (msg == WM_NCPAINT && g_editorDark)   // overpaint the scrollbar dead-corner once the bars are drawn
        paintScrollbarCorner(h);
    return res;
}
#endif // __WXMSW__ (Win32 plugin message plumbing)

// Set by the frame so the page subclass can raise the editor's own themed right-click menu
// (Scintilla's native popup is suppressed with SC_POPUP_NEVER). Args are screen coords.
static std::function<void(int, int)> g_editorContextMenu;

// Set by the frame: open file(s) dropped onto the editor (Scintilla fires SCN_URIDROPPED).
static std::function<void(const wxString&)> g_openDropped;

// Set by the frame: a zoom change (Ctrl+wheel etc.) in one editor syncs all tabs + persists.
static std::function<void(int)> g_onZoom;

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

// ---- Function List symbol parsing -----------------------------------------------------------------
// Notepad++'s Function List is regex-driven (PowerEditor/installer/functionList/*.xml). The two Qt
// N++-likes (NotepadNext, notepadqq) have NO equivalent feature, so these rules are distilled from
// N++'s XML and simplified to portable std::regex (ECMAScript) - N++'s patterns use PCRE/Boost
// recursion + \K which std::regex/wxRegEx can't run. Each rule = {kind 0=function/1=class, regex,
// name capture-group}. Patterns anchor on (?:^|\n) (start-of-line) and the name's position gives the line.
struct FLRule { int kind; std::regex re; int grp; };

class FLItemData : public wxTreeItemData { public: int line; explicit FLItemData(int l) : line(l) {} };

static const std::vector<FLRule>* flRules(const std::string& lang)
{
    static std::map<std::string, std::vector<FLRule>> tbl;
    static bool built = false;
    if (!built)
    {
        built = true;
        auto add = [&](const char* l, int kind, const char* pat, int grp) {
            try { tbl[l].push_back({ kind, std::regex(pat, std::regex::ECMAScript | std::regex::optimize), grp }); } catch (...) {}
        };
        // C / C++ (class/struct, then free functions & methods)
        add("cpp", 1, R"((?:^|\n)[ \t]*(?:template[ \t]*<[^>]*>[ \t\n]*)?(?:class|struct)[ \t]+(?:[A-Za-z_]\w*[ \t]+)*([A-Za-z_]\w*)\b(?:[ \t]+final)?[ \t\n]*(?::[^{]*)?\{)", 1);
        add("cpp", 0, R"((?:^|\n)[ \t]*(?:(?:inline|static|virtual|explicit|friend|constexpr)[ \t]+)*[A-Za-z_][\w:<>,\*& \t]*?[\*& \t](?!(?:if|while|for|switch|catch|return|sizeof|do|else)\b)(~?[A-Za-z_]\w*)[ \t]*\([^;{}]*\)[ \t\n]*(?:const|noexcept|override|final|[ \t\n])*\{)", 1);
        // Python
        add("python", 1, R"((?:^|\n)[ \t]*class[ \t]+([A-Za-z_]\w*)[ \t]*[\(:])", 1);
        add("python", 0, R"((?:^|\n)[ \t]*(?:async[ \t]+)?def[ \t]+([A-Za-z_]\w*)[ \t]*\()", 1);
        // JavaScript / TypeScript
        add("js", 1, R"((?:^|\n)[ \t]*(?:export[ \t]+)?(?:default[ \t]+)?(?:abstract[ \t]+)?class[ \t]+([A-Za-z_$][\w$]*))", 1);
        add("js", 0, R"((?:^|\n)[ \t]*(?:export[ \t]+)?(?:default[ \t]+)?(?:async[ \t]+)?function\*?[ \t]+([A-Za-z_$][\w$]*)[ \t]*\()", 1);
        add("js", 0, R"((?:^|\n)[ \t]*(?:export[ \t]+)?(?:const|let|var)[ \t]+([A-Za-z_$][\w$]*)[ \t]*=[ \t]*(?:async[ \t]+)?\([^)]*\)[ \t]*=>)", 1);
        // Java
        add("java", 1, R"((?:^|\n)[ \t]*(?:(?:public|private|protected|abstract|final|static|sealed)[ \t]+)*(?:class|interface|enum)[ \t]+([A-Za-z_]\w*))", 1);
        add("java", 0, R"((?:^|\n)[ \t]*(?:(?:public|private|protected|static|final|abstract|synchronized|native|default)[ \t]+)+(?:<[^>]+>[ \t]*)?[A-Za-z_][\w<>\[\], \t.]*?[ \t]([A-Za-z_]\w*)[ \t]*\([^;{=]*\)[ \t\n]*(?:throws[^{]*)?\{)", 1);
        // C#
        add("cs", 1, R"((?:^|\n)[ \t]*(?:(?:public|private|protected|internal|static|sealed|abstract|partial)[ \t]+)*(?:class|struct|interface|enum|record)[ \t]+([A-Za-z_]\w*))", 1);
        add("cs", 0, R"((?:^|\n)[ \t]*(?:(?:public|private|protected|internal|static|virtual|override|sealed|abstract|extern|async|new)[ \t]+)+(?!return|if|else|while|for|switch|using)[A-Za-z_][\w<>\[\], \t.]*?[ \t]([A-Za-z_]\w*)[ \t]*(?:<[^>]+>)?[ \t]*\([^;{]*\)[ \t\n]*(?:where[^{]*)?\{)", 1);
    }
    auto it = tbl.find(lang);
    return it == tbl.end() ? nullptr : &it->second;
}

// Comment + string spans to mask out, so keywords inside comments/strings aren't parsed as symbols.
static const std::regex* flCommentRe(const std::string& lang)
{
    static std::map<std::string, std::regex> tbl;
    static bool built = false;
    if (!built)
    {
        built = true;
        try {
            const char* cfam = R"(/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*')";
            tbl["cpp"] = std::regex(cfam); tbl["js"] = std::regex(cfam); tbl["java"] = std::regex(cfam); tbl["cs"] = std::regex(cfam);
            tbl["python"] = std::regex(R"(#[^\n]*|'''[\s\S]*?'''|"""[\s\S]*?"""|"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*')");
        } catch (...) {}
    }
    auto it = tbl.find(lang);
    return it == tbl.end() ? nullptr : &it->second;
}

// ---- Find in Files --------------------------------------------------------------------------------
struct FifHit { wxString file; int line; wxString text; };
class FifItemData : public wxTreeItemData { public: wxString file; int line; FifItemData(const wxString& f, int l) : file(f), line(l) {} };

// Search every file under `dir` matching `filters` (e.g. "*.cpp;*.h", ';'-separated) for `term`,
// line by line. Collects {file, line, text} hits; fills `searched` with the file count.
static void findInFiles(const wxString& term, const wxString& dir, const wxString& filters,
                        bool matchCase, bool wholeWord, bool useRegex, bool subdirs,
                        std::vector<FifHit>& hits, int& searched)
{
    searched = 0;
    if (term.empty() || !wxDirExists(dir)) return;
    std::set<wxString> files;
    wxArrayString pats = wxSplit(filters.empty() ? wxString("*.*") : filters, ';');
    const int flags = wxDIR_FILES | (subdirs ? wxDIR_DIRS : 0);
    for (wxString pat : pats) { pat.Trim(true).Trim(false); if (pat.empty()) continue;
        wxArrayString f; wxDir::GetAllFiles(dir, &f, pat, flags); for (const auto& x : f) files.insert(x); }
    std::regex re; bool re_ok = false;
    if (useRegex) { try { auto fl = std::regex::ECMAScript; if (!matchCase) fl |= std::regex::icase; re = std::regex(term.ToStdString(), fl); re_ok = true; } catch (...) {} }
    const wxString needle = matchCase ? term : term.Lower();
    auto isWord = [](wxUniChar c) { return wxIsalnum(c) || c == '_'; };
    for (const wxString& path : files)
    {
        wxTextFile tf(path);
        if (!tf.Open()) continue;
        ++searched;
        for (size_t i = 0; i < tf.GetLineCount(); ++i)
        {
            const wxString& line = tf.GetLine(i);
            bool m = false;
            if (useRegex) { if (re_ok) { try { m = std::regex_search(line.ToStdString(), re); } catch (...) {} } }
            else
            {
                const wxString hay = matchCase ? line : line.Lower();
                size_t pos = hay.find(needle);
                while (pos != wxString::npos)
                {
                    if (!wholeWord) { m = true; break; }
                    const bool okB = (pos == 0) || !isWord(line[pos - 1]);
                    const size_t end = pos + needle.length();
                    const bool okA = (end >= line.length()) || !isWord(line[end]);
                    if (okB && okA) { m = true; break; }
                    pos = hay.find(needle, pos + 1);
                }
            }
            if (m) hits.push_back({ path, (int)i + 1, line });
            if (hits.size() > 50000) { tf.Close(); return; }    // safety cap on pathological searches
        }
        tf.Close();
    }
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

// Notepad++-style Column Editor (Alt+C): insert text or an incrementing number down a column /
// rectangular (or multi-) selection. The dialog gathers the choice; the frame applies it per selection.
class ColumnEditorDialog : public wxDialog
{
public:
    ColumnEditorDialog(wxWindow* p, bool dark) : wxDialog(p, wxID_ANY, "Column Editor")
    {
        const wxColour fbg = dark ? wxColour(32, 32, 32) : *wxWHITE, ffg = dark ? wxColour(220, 220, 220) : *wxBLACK;
        auto field = [&](const wxString& v, int w) { auto* t = new wxTextCtrl(this, wxID_ANY, v, wxDefaultPosition, wxSize(w, -1)); if (dark) { t->SetBackgroundColour(fbg); t->SetForegroundColour(ffg); } return t; };
        auto* s = new wxBoxSizer(wxVERTICAL);

        m_radioText = new wxRadioButton(this, wxID_ANY, "Text to Insert", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
        m_textField = field("", 250);
        s->Add(m_radioText, 0, wxLEFT | wxTOP | wxRIGHT, 10);
        s->Add(m_textField, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 26);

        m_radioNum = new wxRadioButton(this, wxID_ANY, "Number to Insert");
        s->Add(m_radioNum, 0, wxLEFT | wxRIGHT, 10);
        auto* nrow = new wxBoxSizer(wxHORIZONTAL);
        nrow->Add(new wxStaticText(this, wxID_ANY, "Initial:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        m_initial = field("1", 60); nrow->Add(m_initial, 0, wxRIGHT, 14);
        nrow->Add(new wxStaticText(this, wxID_ANY, "Increase by:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        m_increase = field("1", 60); nrow->Add(m_increase);
        s->Add(nrow, 0, wxLEFT | wxRIGHT | wxTOP, 26);
        m_leadZero = new wxCheckBox(this, wxID_ANY, "Leading zeros");
        s->Add(m_leadZero, 0, wxLEFT | wxTOP | wxBOTTOM, 26);
        wxString fmts[] = { "Dec", "Hex", "Oct", "Bin" };
        m_format = new wxRadioBox(this, wxID_ANY, "Format", wxDefaultPosition, wxDefaultSize, 4, fmts, 4, wxRA_SPECIFY_COLS);
        s->Add(m_format, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 26);

        s->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALL | wxEXPAND, 10);
        SetSizerAndFit(s);
        m_radioText->SetValue(true);
        m_textField->SetFocus();
    }
    bool     isText()       const { return m_radioText->GetValue(); }
    wxString text()         const { return m_textField->GetValue(); }
    long     initial()      const { long v = 0; m_initial->GetValue().ToLong(&v); return v; }
    long     increase()     const { long v = 0; m_increase->GetValue().ToLong(&v); return v; }
    bool     leadingZeros() const { return m_leadZero->GetValue(); }
    int      base()         const { switch (m_format->GetSelection()) { case 1: return 16; case 2: return 8; case 3: return 2; default: return 10; } }
private:
    wxRadioButton* m_radioText = nullptr; wxRadioButton* m_radioNum = nullptr;
    wxTextCtrl*    m_textField = nullptr; wxTextCtrl* m_initial = nullptr; wxTextCtrl* m_increase = nullptr;
    wxCheckBox*    m_leadZero  = nullptr; wxRadioBox* m_format = nullptr;
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

#ifdef __WXMSW__
// Custom status-bar size-grip. Windows draws the native grip (wxSTB_SIZEGRIP) on a light 3D corner that
// ignores our dark chrome, so we drop the native one (see buildStatusBar) and paint our own dots on the
// matching status-bar background here, forwarding the drag to the frame's bottom-right resize border.
class SizeGripWin : public wxWindow
{
public:
    explicit SizeGripWin(wxWindow* parent)
        : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(16, 16), wxBORDER_NONE)
    {
        SetCursor(wxCursor(wxCURSOR_SIZENWSE));
        Bind(wxEVT_PAINT,     &SizeGripWin::onPaint, this);
        Bind(wxEVT_LEFT_DOWN, &SizeGripWin::onDown,  this);
    }
    void setColours(const wxColour& bg, const wxColour& dot)
    {
        m_bg = bg; m_dot = dot; SetBackgroundColour(bg); Refresh();
    }
private:
    wxColour m_bg{32, 32, 32}, m_dot{112, 112, 112};
    void onPaint(wxPaintEvent&)
    {
        wxPaintDC dc(this);
        dc.SetBackground(wxBrush(m_bg));        // == the status bar's chrome colour: no "different background"
        dc.Clear();
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(m_dot));
        const wxSize s = GetClientSize();
        const int M = 4, P = 4, D = 2;          // margin, pitch, dot size -> classic triangular grip
        for (int j = 0; j <= 2; ++j)
            for (int i = 0; i + j <= 2; ++i)
                dc.DrawRectangle(s.x - M - i * P, s.y - M - j * P, D, D);
    }
    void onDown(wxMouseEvent&)
    {
        wxWindow* top = wxGetTopLevelParent(this);   // hand the drag to the native bottom-right resize border
        ::ReleaseCapture();
        ::SendMessage(static_cast<HWND>(top->GetHandle()), WM_NCLBUTTONDOWN, HTBOTTOMRIGHT, 0);
    }
};
#endif // __WXMSW__

// ============================ Nib plugin host (cross-platform) ============================
// Loads plugins written against our own permissive plugin API (include/nib/nib.h) - no Win32, no
// Notepad++ ABI. The editor bridge rides on g_view's portable SendMsg, so it works on every platform.
struct NibCmd { std::string id, title; NibCommandFn fn; void* user; };
static std::vector<NibCmd>            g_nibCommands;
struct NibLoaded { const NibPluginApi* api; wxDynamicLibrary* lib; };   // a loaded Nib plugin (kept for teardown)
static std::vector<NibLoaded> g_nibLibs;
static const int NIB_CMD_BASE = 63000;   // Nib command menu ids (clear of IDM_*, doc-list 61xxx, N++ plugins 62xxx)

static sptr_t nibSci(UINT m, uptr_t w = 0, sptr_t l = 0)
{ return g_view ? static_cast<sptr_t>(g_view->SendMsg(static_cast<int>(m), static_cast<wxUIntPtr>(w), static_cast<wxIntPtr>(l))) : 0; }

// nib.editor/1
static int64_t nibEdLength(NibHost*)                             { return nibSci(SCI_GETLENGTH); }
static void    nibEdInsert(NibHost*, int64_t pos, const char* t) { if (t) nibSci(SCI_INSERTTEXT, pos < 0 ? nibSci(SCI_GETCURRENTPOS) : static_cast<uptr_t>(pos), reinterpret_cast<sptr_t>(t)); }
static void    nibEdReplSel(NibHost*, const char* t)             { if (t) nibSci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>(t)); }
static int64_t nibEdSelStart(NibHost*)                           { return nibSci(SCI_GETSELECTIONSTART); }
static int64_t nibEdSelEnd(NibHost*)                             { return nibSci(SCI_GETSELECTIONEND); }
static int64_t nibEdGetText(NibHost*, int64_t s, int64_t e, char* b, int64_t bs)
{
    const char* doc = reinterpret_cast<const char*>(nibSci(SCI_GETCHARACTERPOINTER));
    int64_t len = e - s; if (len < 0) len = 0;
    if (b && bs > 0) { int64_t n = (doc && len < bs - 1) ? len : 0; if (n) std::memcpy(b, doc + s, static_cast<size_t>(n)); b[n] = 0; }
    return len;
}
// nib.commands/1
static void nibCmdRegister(NibHost*, const char* id, const char* title, NibCommandFn fn, void* u)
{ g_nibCommands.push_back({ id ? id : "", title ? title : "", fn, u }); }
// nib.events/1
struct NibSub { NibEventKind kind; NibEventFn fn; void* user; };
static std::vector<NibSub> g_nibSubs;
static void nibSubscribe(NibHost*, NibEventKind kind, NibEventFn fn, void* u) { g_nibSubs.push_back({ kind, fn, u }); }
static void nibFireEvent(const NibEvent& ev)   // called by the editor handlers below
{ for (const auto& s : g_nibSubs) if (s.kind == ev.kind && s.fn) s.fn(reinterpret_cast<NibHost*>(g_view), &ev, s.user); }
// nib.panels/1 - the frame installs these (they need m_aui + the frame as parent), matching g_sciForward.
static std::function<void*(const char*, const char*, int)> g_nibCreatePanel;
static std::function<void(void*, const char*, bool)>        g_nibPanelText;   // (panel, utf8, append?)
static std::function<void(void*, bool)>                     g_nibPanelShow;
static NibPanel* nibPanelRegister(NibHost*, const char* id, const char* title, NibDock dock)
{ return g_nibCreatePanel ? reinterpret_cast<NibPanel*>(g_nibCreatePanel(id, title, static_cast<int>(dock))) : nullptr; }
static void nibPanelSetText(NibHost*, NibPanel* p, const char* t) { if (g_nibPanelText) g_nibPanelText(p, t, false); }
static void nibPanelAppend(NibHost*, NibPanel* p, const char* t)  { if (g_nibPanelText) g_nibPanelText(p, t, true);  }
static void nibPanelShow(NibHost*, NibPanel* p, int v)            { if (g_nibPanelShow) g_nibPanelShow(p, v != 0);   }
// nib.documents/1 - the frame installs these (they need the tab list + the active page's path).
static std::function<int()>            g_nibDocCount;
static std::function<int(char*, int)>  g_nibDocActivePath;   // copy the active doc's UTF-8 path -> length, 0 if untitled
static std::function<int(const char*)> g_nibDocOpen;         // open a file by UTF-8 path
static std::function<int()>            g_nibDocSave;         // save the active document
static int nibDocCount(NibHost*)                      { return g_nibDocCount ? g_nibDocCount() : 0; }
static int nibDocActivePath(NibHost*, char* b, int c) { return g_nibDocActivePath ? g_nibDocActivePath(b, c) : 0; }
static int nibDocOpen(NibHost*, const char* p)        { return g_nibDocOpen ? g_nibDocOpen(p) : 0; }
static int nibDocSave(NibHost*)                       { return g_nibDocSave ? g_nibDocSave() : 0; }
#ifdef __WXMSW__
// nib.win32/1 - Windows-only native-handle capability (the GPL npp-bridge uses it to rebuild NppData).
static std::function<void*()> g_nibMainWindow, g_nibEditorMain, g_nibPluginsMenu;
static std::function<void(void*, const char*, int)> g_nibDockNative;   // host a plugin's native HWND in a dock pane
static std::function<void(void*, bool)>             g_nibShowDock;
static void* nibW32Main(NibHost*)   { return g_nibMainWindow   ? g_nibMainWindow()   : nullptr; }
static void* nibW32EdMain(NibHost*) { return g_nibEditorMain   ? g_nibEditorMain()   : nullptr; }
static void* nibW32EdSec(NibHost*)  { return nullptr; }   // split view dropped: the host exposes no secondary editor
static void* nibW32Menu(NibHost*)   { return g_nibPluginsMenu  ? g_nibPluginsMenu()  : nullptr; }
static void  nibW32Dock(NibHost*, void* h, const char* t, int e) { if (g_nibDockNative) g_nibDockNative(h, t, e); }
static void  nibW32ShowDock(NibHost*, void* h, int v)            { if (g_nibShowDock)   g_nibShowDock(h, v != 0); }
static const NibWin32Api g_nibWin32Api = { 1, sizeof(NibWin32Api), nibW32Main, nibW32EdMain, nibW32EdSec, nibW32Menu, nibW32Dock, nibW32ShowDock };
#endif
// nib.host/1
static const char* nibHostName(NibHost*) { return "wxNotepad++"; }
static uint32_t     nibHostAbi(NibHost*) { return NIB_ABI_VERSION; }

static const NibHostApi     g_nibHostApi     = { 1, sizeof(NibHostApi),     nibHostName, nibHostAbi };
static const NibEditorApi   g_nibEditorApi   = { 1, sizeof(NibEditorApi),   nibEdLength, nibEdInsert, nibEdReplSel, nibEdSelStart, nibEdSelEnd, nibEdGetText };
static const NibCommandsApi g_nibCommandsApi = { 1, sizeof(NibCommandsApi), nibCmdRegister };
static const NibEventsApi   g_nibEventsApi   = { 1, sizeof(NibEventsApi),   nibSubscribe };
static const NibPanelsApi   g_nibPanelsApi   = { 1, sizeof(NibPanelsApi),   nibPanelRegister, nibPanelSetText, nibPanelAppend, nibPanelShow };
static const NibDocumentsApi g_nibDocumentsApi = { 1, sizeof(NibDocumentsApi), nibDocCount, nibDocActivePath, nibDocOpen, nibDocSave };

static const void* nibQuery(NibHost*, const char* iface, uint32_t minv)
{
    if (!iface) return nullptr;
    if (minv <= 1 && std::strcmp(iface, NIB_IFACE_HOST)     == 0) return &g_nibHostApi;
    if (minv <= 1 && std::strcmp(iface, NIB_IFACE_EDITOR)   == 0) return &g_nibEditorApi;
    if (minv <= 1 && std::strcmp(iface, NIB_IFACE_DOCUMENTS)== 0) return &g_nibDocumentsApi;
    if (minv <= 1 && std::strcmp(iface, NIB_IFACE_COMMANDS) == 0) return &g_nibCommandsApi;
    if (minv <= 1 && std::strcmp(iface, NIB_IFACE_EVENTS)   == 0) return &g_nibEventsApi;
    if (minv <= 1 && std::strcmp(iface, NIB_IFACE_PANELS)   == 0) return &g_nibPanelsApi;
#ifdef __WXMSW__
    if (minv <= 1 && std::strcmp(iface, NIB_IFACE_WIN32)    == 0) return &g_nibWin32Api;
#endif
    return nullptr;
}
static void nibLog(NibHost*, int, const char* msg) { if (msg) wxLogDebug("[nib] %s", msg); }

// Load Nib plugins from <exe>/nib/ via wxDynamicLibrary (portable: .dll / .so / .dylib).
static void loadNibPlugins()
{
    const wxString dir = wxPathOnly(wxStandardPaths::Get().GetExecutablePath()) + wxFILE_SEP_PATH + "nib";
    if (!wxDirExists(dir)) return;
#if defined(__WXMSW__)
    const wxString pat = "*.dll";
#elif defined(__WXMAC__)
    const wxString pat = "*.dylib";
#else
    const wxString pat = "*.so";
#endif
    wxDir d(dir); wxString f;
    for (bool more = d.GetFirst(&f, pat, wxDIR_FILES); more; more = d.GetNext(&f))
    {
        auto* lib = new wxDynamicLibrary(dir + wxFILE_SEP_PATH + f);
        bool ok = false;
        if (lib->IsLoaded())
        {
            auto entry = reinterpret_cast<NibPluginMainFn>(lib->GetSymbol("nib_plugin_main"));
            if (entry)
            {
                NibBootstrap boot{ NIB_ABI_VERSION, sizeof(NibBootstrap), reinterpret_cast<NibHost*>(g_view), &nibQuery, &nibLog };
                const NibPluginApi* api = entry(&boot);
                if (api && (api->abi_version >> 16) == (NIB_ABI_VERSION >> 16))   // compatible major version
                {
                    if (api->activate) api->activate(reinterpret_cast<NibHost*>(g_view), &nibQuery);
                    g_nibLibs.push_back({ api, lib });
                    ok = true;
                }
            }
        }
        if (!ok) { lib->Unload(); delete lib; }
    }
}

// Tear down Nib plugins in reverse load order: deactivate (the GPL bridge removes its frame subclass), then unload.
static void unloadNibPlugins()
{
    for (auto it = g_nibLibs.rbegin(); it != g_nibLibs.rend(); ++it)
    {
        if (it->api && it->api->deactivate) it->api->deactivate(reinterpret_cast<NibHost*>(g_view));
        if (it->lib) { it->lib->Unload(); delete it->lib; }
    }
    g_nibLibs.clear();
}
// ============================ end Nib plugin host ============================

// The tab pin button, drawn from the project's own icon set (resources/icons/pin.svg) instead of the
// wxAui default pin glyph. Subclasses the default flat tab art and re-skins the pin/unpin bitmaps
// (recoloured to the tab-button colour) whenever the art refreshes its colours.
class PinTabArt : public wxAuiDefaultTabArt
{
public:
    explicit PinTabArt(const wxColour& iconColour) : m_iconColour(iconColour) {}
    void UpdateColoursFromSystem() override { wxAuiDefaultTabArt::UpdateColoursFromSystem(); reskin(); }
    // wxAuiFlatTabArt is non-copyable (private pimpl); clone a fresh one (the app sets no custom art
    // colours, so a fresh instance has the same state) and re-skin its buttons.
    wxAuiTabArt* Clone() override { auto* a = new PinTabArt(m_iconColour); a->UpdateColoursFromSystem(); return a; }
private:
    wxColour m_iconColour;
    wxBitmapBundle iconBundle(const wxString& name) const   // resources/icons/<name>.svg, recoloured to the button colour
    {
        const wxString path = wxPathOnly(wxStandardPaths::Get().GetExecutablePath()) + "\\icons\\" + name + ".svg";
        if (!wxFileExists(path)) return wxBitmapBundle();
        wxFile f(path); wxString svg;
        if (!f.IsOpened() || !f.ReadAll(&svg)) return wxBitmapBundle();
        svg.Replace("currentColor", m_iconColour.GetAsString(wxC2S_HTML_SYNTAX));
        return wxBitmapBundle::FromSVG(svg.utf8_str().data(), wxSize(16, 16));
    }
    void reskin()
    {
        const wxBitmapBundle pin = iconBundle("pin");
        if (pin.IsOk()) { m_activePinBmp = pin; m_activeUnpinBmp = pin; m_disabledPinBmp = pin; m_disabledUnpinBmp = pin; }
        const wxBitmapBundle cls = iconBundle("close-tab");
        if (cls.IsOk()) { m_activeCloseBmp = cls; m_disabledCloseBmp = cls; }
    }
};

// The shell frame is a template on its chrome base so the same ~3300 lines work whether the base is the
// native wxFrame or the borderless wxBorderlessFrame. The base is chosen at startup (restart-to-apply)
// via the two aliases defined just after the class. (Two-phase lookup: MSVC is permissive, but GCC needs
// the inherited wxFrame methods brought into scope - see the `using FB::` block below.)
template <class FB> class NppShellFrameT : public FB
{
public:
    // Bring inherited wxFrame/wxWindow/wxEvtHandler members into scope. MSVC's permissive lookup resolves
    // them through the dependent base FB on its own; GCC/Clang's strict two-phase lookup does not, so an
    // unqualified SetMenuBar(...) etc. would otherwise be "not declared". Real methods only - unused ones
    // are harmless; a missing one is a GCC-only error surfaced by CI.
    using FB::Bind; using FB::Unbind; using FB::Connect;
    using FB::SetMenuBar; using FB::GetMenuBar; using FB::CreateToolBar; using FB::GetToolBar;
    using FB::CreateStatusBar; using FB::GetStatusBar; using FB::SetStatusText; using FB::SetStatusWidths;
    using FB::SetTitle; using FB::GetTitle; using FB::SetIcon; using FB::SetIcons; using FB::SetLabel;
    using FB::Iconize; using FB::IsIconized; using FB::Maximize; using FB::IsMaximized; using FB::Restore;
    using FB::Close; using FB::Destroy; using FB::Show; using FB::Hide; using FB::IsShown;
    using FB::SetFocus; using FB::Refresh; using FB::Update; using FB::SendSizeEvent; using FB::Fit;
    using FB::PopupMenu; using FB::SetSizer; using FB::SetSizerAndFit; using FB::GetSizer; using FB::Layout;
    using FB::SetSize; using FB::GetSize; using FB::SetClientSize; using FB::GetClientSize;
    using FB::GetClientRect; using FB::Move; using FB::Centre; using FB::SetMinSize; using FB::SetMaxSize;
    using FB::SetBackgroundColour; using FB::GetBackgroundColour; using FB::SetForegroundColour;
    using FB::ClientToScreen; using FB::ScreenToClient; using FB::GetPosition; using FB::GetScreenPosition;
    using FB::GetEventHandler; using FB::CaptureMouse; using FB::ReleaseMouse; using FB::HasCapture;
    using FB::ShowFullScreen; using FB::IsFullScreen; using FB::Freeze; using FB::Thaw; using FB::IsFrozen;
    using FB::GetHandle; using FB::SetTransparent; using FB::Raise; using FB::SetDropTarget;
    using FB::SetAcceleratorTable; using FB::GetClientAreaOrigin; using FB::GetParent; using FB::GetFont;
    using FB::SetExtraStyle; using FB::GetContentScaleFactor; using FB::ProcessWindowEvent;

    // True when the chrome base is the borderless frame (integrated top bar), false for native wxFrame.
    // Compile-time, so the borderless-only branches below are `if constexpr` and never instantiated for
    // the native frame (and never reference wxBorderlessFrame symbols on macOS, where the lib is absent).
#ifdef WXNPP_HAS_BORDERLESS
    static constexpr bool kBorderless = std::is_base_of<wxBorderlessFrameBase, FB>::value;
    static constexpr int  kTitleBarH  = 30;   // height (px) of the integrated top bar
#else
    static constexpr bool kBorderless = false;
#endif

    explicit NppShellFrameT(bool dark)
        : FB(nullptr, wxID_ANY, "new 1 - wxNotepad++", wxDefaultPosition, wxSize(1100, 720)),
          m_timer(this, myID_TIMER)
    {
        m_dark = dark;          // chrome darkness is fixed for this process (restart-to-apply)
        loadSettings();         // restore preferences incl. the chosen editor theme (before loadTheme reads m_themeName)
        loadTheme();            // parse the active Notepad++ theme XML for exact colours
        { long z = 0; wxConfigBase::Get()->Read("Zoom", &z, 0L); m_zoom = static_cast<int>(z); }   // restore zoom
        setAppIcon();
        buildEditor();
        buildMenuBar();
        buildToolBar();
        buildStatusBar();

        // The Notepad++ binary-plugin host (NppData, the NPPM_* router, the DLL loader) is NOT in the core:
        // it lives in the optional GPL npp-bridge, which reaches this frame through the nib.win32 capability
        // and serves NPPM_* itself. The core keeps only the generic SciHwndProc editor bridge (below), which
        // is not Notepad++-derived. Confining the N++ ABI to that GPL module is what lets the core relicense.
        // Nib panel host: host-owned, dockable wxAui text panels (cross-platform). Installed before plugins
        // load so a plugin's activate() can register one. The opaque NibPanel handle is a read-only
        // wxStyledTextCtrl* - Scintilla auto-hides its scrollbars when the content fits (no empty scrollbars).
        g_nibCreatePanel = [this](const char* id, const char* title, int dock) -> void* {
            auto* stc = new wxStyledTextCtrl(this, wxID_ANY);
            stc->SetWrapMode(wxSTC_WRAP_WORD);                       // wrap -> never a horizontal scrollbar
            stc->SetUseHorizontalScrollBar(false);
            for (int m = 0; m < 3; ++m) stc->SetMarginWidth(m, 0);  // no line-number / symbol / fold margins
            stc->StyleSetFaceName(wxSTC_STYLE_DEFAULT, "Consolas"); stc->StyleSetSize(wxSTC_STYLE_DEFAULT, 9);
            if (m_dark) { stc->StyleSetBackground(wxSTC_STYLE_DEFAULT, wxColour(30, 30, 30)); stc->StyleSetForeground(wxSTC_STYLE_DEFAULT, wxColour(220, 220, 220)); }
            stc->StyleClearAll();
            stc->SetReadOnly(true);
            wxAuiPaneInfo pi; pi.Name(wxString::FromUTF8(id)).Caption(wxString::FromUTF8(title)).CloseButton(true).MaximizeButton(false);
            switch (dock) { case 1: pi.Left().BestSize(240, 320); break; case 2: pi.Right().BestSize(240, 320); break;
                            case 3: pi.Top().BestSize(320, 150);  break; default: pi.Bottom().BestSize(320, 150); }
            m_aui.AddPane(stc, pi); m_aui.Update();
            return stc;
        };
        g_nibPanelText = [](void* p, const char* utf8, bool append) {
            auto* stc = static_cast<wxStyledTextCtrl*>(p);
            if (!stc || !utf8) return;
            stc->SetReadOnly(false);
            if (append) stc->AppendText(wxString::FromUTF8(utf8)); else stc->SetText(wxString::FromUTF8(utf8));
            stc->GotoPos(stc->GetLength());   // keep the latest line in view (log-style)
            stc->SetReadOnly(true);
        };
        g_nibPanelShow = [this](void* p, bool v) {
            wxAuiPaneInfo& pi = m_aui.GetPane(static_cast<wxWindow*>(p)); if (pi.IsOk()) { pi.Show(v); m_aui.Update(); }
        };
        g_nibDocCount      = [this]() -> int { return m_tabs ? static_cast<int>(m_tabs->GetPageCount()) : 1; };
        g_nibDocActivePath = [this](char* b, int c) -> int {   // active document's full path (UTF-8); 0 if untitled
            EditorPage* p = activePage();
            const std::string u = (p ? p->path : wxString()).utf8_string();
            if (u.empty()) return 0;
            if (b && c > 0) { int n = static_cast<int>(u.size()); if (n > c - 1) n = c - 1; std::memcpy(b, u.data(), static_cast<size_t>(n)); b[n] = 0; }
            return static_cast<int>(u.size());
        };
        g_nibDocOpen = [this](const char* p) -> int { if (!p) return 0; openPath(wxString::FromUTF8(p)); return 1; };
        g_nibDocSave = [this]() -> int { onSave(); return 1; };
#ifdef __WXMSW__   // nib.win32: hand the (optional, GPL) N++ bridge the native handles it needs
        g_nibMainWindow   = [this]() -> void* { return static_cast<HWND>(GetHandle()); };
        g_nibEditorMain   = [this]() -> void* { return m_sci; };
        g_nibPluginsMenu  = [this]() -> void* {           // the Plugins menu HMENU (the GPL bridge maps it to NPPM_GETMENUHANDLE)
            auto* mb = menuBar(); if (!mb) return nullptr;
            const int mi = mb->FindMenu("Plugins");
            return mi == wxNOT_FOUND ? nullptr : reinterpret_cast<void*>(mb->GetMenu(mi)->GetHMenu());
        };
        g_nibDockNative = [this](void* hwndV, const char* title, int edge) {   // host a plugin's native window in a dock pane
            HWND hc = static_cast<HWND>(hwndV);
            if (!hc) return;
            for (const auto& d : m_nibDocks) if (d.hClient == hc) return;   // already docked
            auto* host = new wxPanel(this);
            ::SetParent(hc, static_cast<HWND>(host->GetHandle()));
            host->Bind(wxEVT_SIZE, [host, hc](wxSizeEvent& e) {
                const wxSize s = host->GetClientSize(); ::MoveWindow(hc, 0, 0, s.GetWidth(), s.GetHeight(), TRUE); e.Skip();
            });
            const wxString name = wxString::FromUTF8(title ? title : "Plugin");
            wxAuiPaneInfo pi; pi.Name(name).Caption(name).CloseButton(true).BestSize(340, 220).MinSize(160, 100).Hide();
            switch (edge) { case 1: pi.Left(); break; case 2: pi.Right(); break; case 3: pi.Top(); break; default: pi.Bottom(); break; }
            m_aui.AddPane(host, pi); m_aui.Update();
            m_nibDocks.push_back({ hc, host, name });
        };
        g_nibShowDock = [this](void* hwndV, bool v) {
            HWND hc = static_cast<HWND>(hwndV);
            for (const auto& d : m_nibDocks) if (d.hClient == hc) {
                wxAuiPaneInfo& pi = m_aui.GetPane(d.host);
                if (pi.IsOk()) { pi.Show(v); if (v) ::ShowWindow(d.hClient, SW_SHOW); m_aui.Update(); }
                return;
            }
        };
#endif
        loadNibPlugins();   // cross-platform: plugins written against our own Nib API (include/nib/nib.h)
        if (!g_nibCommands.empty())   // surface registered Nib commands in the Plugins menu
            if (auto* mb = menuBar()) { const int mi = mb->FindMenu("Plugins");
                if (mi != wxNOT_FOUND) { wxMenu* pm = mb->GetMenu(mi);
                    if (pm->GetMenuItemCount() > 0) pm->AppendSeparator();
                    for (size_t i = 0; i < g_nibCommands.size(); ++i)
                        pm->Append(NIB_CMD_BASE + static_cast<int>(i), wxString::FromUTF8(g_nibCommands[i].title)); } }

        Bind(wxEVT_MENU, &NppShellFrameT::onCommand, this);          // one dispatcher for all menu+toolbar ids
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { updateStatus(); updateUiState(); }, myID_TIMER);
        g_editorContextMenu = [this](int sx, int sy) { showEditorContext(sx, sy); };   // editor right-click menu
        g_openDropped = [this](const wxString& s) { openDroppedPaths(s); };            // files dropped on the editor
        g_onZoom = [this](int z) { onZoomChanged(z); };                                // sync + persist zoom
        SetDropTarget(new FileDrop([this](const wxArrayString& fs) { for (const auto& f : fs) openPath(f); }));
        Bind(wxEVT_CLOSE_WINDOW, &NppShellFrameT::onCloseWindow, this);                 // prompt to save on exit
        m_timer.Start(150);
        applyTheme(m_dark);     // style for the mode this process was launched in
        applySettings();        // apply persisted preferences (tab size, wrap, line numbers, toolbar/status visibility)
        macroToolStates();      // sync the Macro menu/toolbar enable states (nothing recorded yet)
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
                                   wxAUI_NB_CLOSE_ON_ALL_TABS | wxAUI_NB_MIDDLE_CLICK_CLOSE |
                                   wxAUI_NB_PIN_ON_ACTIVE_TAB);   // pin button on the active tab
        { auto* art = new PinTabArt(m_dark ? wxColour(222, 226, 230) : wxColour(52, 58, 64));
          art->UpdateColoursFromSystem(); m_tabs->SetArtProvider(art); }   // pin glyph from resources/icons/pin.svg
        m_tabs->Bind(wxEVT_AUINOTEBOOK_PAGE_CLOSE,   &NppShellFrameT::onPageClose,   this);
        m_tabs->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &NppShellFrameT::onPageChanged, this);
        m_tabs->Bind(wxEVT_AUINOTEBOOK_TAB_RIGHT_UP, &NppShellFrameT::onTabContext,  this);
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
        buildDocMap();   // Document Map (minimap) pane - hidden until toggled from the View menu / toolbar
        buildFuncList(); // Function List (symbol tree) pane - hidden until toggled
        buildFifPanel(); // Find-in-Files results pane - hidden until a search runs
    }

    // wxStyledTextCtrl fires wxEVT_STC_* (not Win32 WM_NOTIFY), so the N++ editor behaviours and the
    // plugin beNotified() forwarding bind here instead of in a page subclass.
    void bindEditorEvents()
    {
        m_stc->Bind(wxEVT_STC_CHARADDED,        &NppShellFrameT::onStcCharAdded,   this);
        m_stc->Bind(wxEVT_STC_UPDATEUI,         &NppShellFrameT::onStcUpdateUI,    this);
        m_stc->Bind(wxEVT_STC_DOUBLECLICK,      &NppShellFrameT::onStcDoubleClick, this);
        m_stc->Bind(wxEVT_STC_MARGINCLICK,      &NppShellFrameT::onStcMarginClick, this);
        m_stc->Bind(wxEVT_STC_ZOOM,             &NppShellFrameT::onStcZoom,        this);
        m_stc->Bind(wxEVT_STC_MODIFIED,         &NppShellFrameT::onStcModified,    this);
        m_stc->Bind(wxEVT_STC_MACRORECORD,      &NppShellFrameT::onMacroRecord,    this);   // capture commands while recording a macro
        m_stc->Bind(wxEVT_STC_SAVEPOINTREACHED, [this](wxStyledTextEvent& e) { NibEvent ev{}; ev.kind = NIB_EV_DOCUMENT_SAVED; ev.struct_size = sizeof(NibEvent); nibFireEvent(ev); e.Skip(); });
        m_stc->Bind(wxEVT_CONTEXT_MENU,         &NppShellFrameT::onStcContextMenu, this);
    }
    void onStcCharAdded(wxStyledTextEvent& e)
    {
        const int ch = e.GetKey();
        if ((ch == '\n' || ch == '\r') && m_autoindent) autoIndentOnNewline(m_stc);
        else if (ch == '}')           dedentCloseBrace(m_stc);
        else if (m_autocomplete && (std::isalnum((unsigned char)ch) || ch == '_'))   // auto word/keyword completion after 3+ chars
        {
            const int caret = (int)sci(SCI_GETCURRENTPOS);
            if (caret - (int)sci(SCI_WORDSTARTPOSITION, caret, 1) >= 3) autoComplete(true);
        }
        e.Skip();
    }
    void onStcUpdateUI(wxStyledTextEvent& e)
    {
        updateBraceMatch(m_stc);
        if (g_smartActive && g_smartSci == m_stc && sci(SCI_GETSELECTIONEMPTY)) clearSmart(m_stc);
        updateDocMapViewport();   // keep the minimap's viewport band in sync with scrolling/caret
        if (e.GetUpdated() & SC_UPDATE_SELECTION)   // raise a Nib selection-changed event
        {
            refreshFoldNestedAccent();   // keep the nested fold-square accent tracking the active section
            NibEvent ev{};
            ev.kind = NIB_EV_SELECTION_CHANGED; ev.struct_size = sizeof(NibEvent);
            ev.as.selection.anchor = sci(SCI_GETANCHOR);
            ev.as.selection.caret  = sci(SCI_GETCURRENTPOS);
            nibFireEvent(ev);
        }
        e.Skip();
    }
    void onStcDoubleClick(wxStyledTextEvent& e) { smartHighlight(m_stc); e.Skip(); }
    void onStcMarginClick(wxStyledTextEvent& e)
    {
        if (e.GetMargin() == 1)   // the bookmark margin
        {
            const int line = static_cast<int>(sci(SCI_LINEFROMPOSITION, e.GetPosition()));
            if (sci(SCI_MARKERGET, line) & (1 << MARK_BOOKMARK)) sci(SCI_MARKERDELETE, line, MARK_BOOKMARK);
            else                                                 sci(SCI_MARKERADD, line, MARK_BOOKMARK);
        }
        e.Skip();
    }
    void onStcZoom(wxStyledTextEvent& e)      { if (g_onZoom) g_onZoom(static_cast<int>(sci(SCI_GETZOOM))); e.Skip(); }
    void onStcModified(wxStyledTextEvent& e)
    {
        const int mt = e.GetModificationType();
        if (mt & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))   // raise a Nib text-changed event
        {
            NibEvent ev{};
            ev.kind = NIB_EV_TEXT_CHANGED; ev.struct_size = sizeof(NibEvent);
            ev.as.text.pos     = e.GetPosition();
            ev.as.text.added   = (mt & SC_MOD_INSERTTEXT) ? e.GetLength() : 0;
            ev.as.text.removed = (mt & SC_MOD_DELETETEXT) ? e.GetLength() : 0;
            nibFireEvent(ev);
        }
        if (mt & SC_MOD_CHANGEFOLD) m_lastFoldSection = -2;   // fold structure changed -> re-evaluate nested-square accent on next caret move
        if (m_flTimer) m_flTimer->StartOnce(600);   // debounce Function List re-parse
        e.Skip();
    }
    // (file drops onto the editor are handled by the frame's wxFileDropTarget; wxEVT_STC_URIDROPPED is
    //  deprecated in wx 3.3 - "never generated" - and absent when wx is built without drag-and-drop.)
    void onStcContextMenu(wxContextMenuEvent& e)
    {
        wxPoint p = e.GetPosition();
        if (p == wxDefaultPosition) p = ::wxGetMousePosition();   // keyboard (Shift+F10): use the pointer
        if (g_editorContextMenu) g_editorContextMenu(p.x, p.y);
    }

    // ---- Document Map (minimap) -------------------------------------------------------------------
    // A second wxStyledTextCtrl that SHARES the active document (no copy, live), shrunk to minimap scale
    // in a right-docked pane. The visible viewport is shown with the minimap's OWN selection (selection
    // is per-view, so it doesn't bleed into the main editor); click/drag scrolls the main editor.
    void buildDocMap()
    {
        m_docMap = new wxStyledTextCtrl(this, wxID_ANY);
        for (int i = 0; i < 5; ++i) m_docMap->SetMarginWidth(i, 0);   // no line-number / fold / symbol margins
        m_docMap->SetZoom(-8);                                        // shrink to minimap scale
        m_docMap->SetCaretStyle(wxSTC_CARETSTYLE_INVISIBLE);
        m_docMap->SetCaretLineVisible(false);
        m_docMap->SetUseHorizontalScrollBar(false);
        m_docMap->SetUseVerticalScrollBar(false);
        m_docMap->SetSelEOLFilled(true);                             // viewport band spans the full line width
        m_docMap->SetExtraAscent(-1); m_docMap->SetExtraDescent(-1); // tighten line spacing
        applyDocMapTheme();
        m_docMap->Bind(wxEVT_LEFT_DOWN, &NppShellFrameT::onDocMapClick, this);
        m_docMap->Bind(wxEVT_MOTION,    &NppShellFrameT::onDocMapDrag,  this);
        m_aui.AddPane(m_docMap, wxAuiPaneInfo().Name("docmap").Caption("Document Map")
                          .Right().BestSize(150, 400).MinSize(70, 80).CloseButton(true).Hide());
        m_aui.Update();
    }
    void applyDocMapTheme()
    {
        if (!m_docMap) return;
        const bool d = m_dark;
        m_docMap->StyleSetBackground(wxSTC_STYLE_DEFAULT, d ? wxColour(30, 30, 30)    : wxColour(255, 255, 255));
        m_docMap->StyleSetForeground(wxSTC_STYLE_DEFAULT, d ? wxColour(150, 150, 150) : wxColour(96, 96, 96));
        m_docMap->StyleClearAll();
        m_docMap->SetSelBackground(true, d ? wxColour(95, 115, 150) : wxColour(180, 200, 230));   // viewport box
        m_docMap->SetSelAlpha(90);
        m_docMap->SetBackgroundColour(d ? wxColour(30, 30, 30) : wxColour(255, 255, 255));
    }
    void updateDocMapViewport()
    {
        if (!m_docMap || !m_stc) return;
        wxAuiPaneInfo& pi = m_aui.GetPane(m_docMap);
        if (!pi.IsOk() || !pi.IsShown()) return;
        const int firstVis = m_stc->GetFirstVisibleLine();
        const int firstDoc = m_stc->DocLineFromVisible(firstVis);
        const int lastDoc  = m_stc->DocLineFromVisible(firstVis + m_stc->LinesOnScreen());
        m_docMap->SetSelection(m_docMap->PositionFromLine(firstDoc), m_docMap->PositionFromLine(lastDoc + 1));
        const int mapFirst = m_docMap->GetFirstVisibleLine();
        const int mapLines = m_docMap->LinesOnScreen();
        if (firstDoc < mapFirst || lastDoc > mapFirst + mapLines)   // keep the viewport visible on long docs
            m_docMap->SetFirstVisibleLine(firstDoc > mapLines / 2 ? firstDoc - mapLines / 2 : 0);
    }
    void toggleDocMap()
    {
        if (!m_docMap) return;
        wxAuiPaneInfo& pi = m_aui.GetPane(m_docMap);
        if (!pi.IsOk()) return;
        const bool show = !pi.IsShown();
        pi.Show(show);
        m_aui.Update();
        if (show && activePage()) { m_docMap->SetDocPointer(reinterpret_cast<void*>(activePage()->doc)); applyDocMapTheme(); updateDocMapViewport(); }
    }
    void scrollMainToDocMapY(int y)
    {
        if (!m_docMap || !m_stc) return;
        const int line = m_docMap->LineFromPosition(m_docMap->PositionFromPoint(wxPoint(2, y)));
        const int half = m_stc->LinesOnScreen() / 2;
        m_stc->SetFirstVisibleLine(line > half ? line - half : 0);
        updateDocMapViewport();
    }
    void onDocMapClick(wxMouseEvent& e) { scrollMainToDocMapY(e.GetY()); }
    void onDocMapDrag(wxMouseEvent& e)  { if (e.Dragging() && e.LeftIsDown()) scrollMainToDocMapY(e.GetY()); else e.Skip(); }

    // ---- Function List (per-file symbol tree) -----------------------------------------------------
    void buildFuncList()
    {
        m_funcList = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                    wxTR_HAS_BUTTONS | wxTR_HIDE_ROOT | wxTR_NO_LINES | wxTR_FULL_ROW_HIGHLIGHT | wxBORDER_NONE);
        { wxVector<wxBitmapBundle> imgs; imgs.push_back(icon("func-leaf")); imgs.push_back(icon("func-node")); m_funcList->SetImages(imgs); }   // 0=symbol, 1=group/class (icon set)
        m_funcList->Bind(wxEVT_TREE_ITEM_ACTIVATED, &NppShellFrameT::onFuncListActivate, this);
        m_funcList->Bind(wxEVT_TREE_SEL_CHANGED,     &NppShellFrameT::onFuncListActivate, this);   // single-click jumps too (like N++)
        m_flTimer = new wxTimer(this, myID_FLTIMER);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { parseFuncList(); }, myID_FLTIMER);
        m_aui.AddPane(m_funcList, wxAuiPaneInfo().Name("funclist").Caption("Function List")
                          .Right().BestSize(210, 400).MinSize(110, 80).CloseButton(true).Hide());
        m_aui.Update();
    }
    std::string flLangKey()
    {
        wxString ext; if (auto* p = activePage()) ext = p->path.AfterLast('.').Lower();
        if (ext=="cpp"||ext=="cc"||ext=="cxx"||ext=="c"||ext=="h"||ext=="hpp"||ext=="hxx"||ext=="ino") return "cpp";
        if (ext=="py"||ext=="pyw") return "python";
        if (ext=="js"||ext=="jsx"||ext=="mjs"||ext=="ts"||ext=="tsx") return "js";
        if (ext=="java") return "java";
        if (ext=="cs") return "cs";
        return "";
    }
    void parseFuncList()
    {
        if (!m_funcList) return;
        wxAuiPaneInfo& pi = m_aui.GetPane(m_funcList);
        if (!pi.IsOk() || !pi.IsShown()) return;
        m_funcList->Freeze();
        m_funcList->DeleteAllItems();
        const wxTreeItemId root = m_funcList->AddRoot("");
        const std::string lang = flLangKey();
        const auto* rules = flRules(lang);
        if (rules)
        {
            const wxCharBuffer cb = m_stc->GetTextRaw();          // UTF-8 bytes - offsets line up with Scintilla
            const std::string text(cb.data(), cb.length());
            std::vector<std::pair<size_t, size_t>> zones;          // comment/string spans to skip
            if (const std::regex* cre = flCommentRe(lang))
                for (std::sregex_iterator it(text.begin(), text.end(), *cre), e; it != e; ++it)
                    zones.push_back({ (size_t)it->position(0), (size_t)(it->position(0) + it->length(0)) });
            auto inZone = [&](size_t p) { for (auto& z : zones) if (p >= z.first && p < z.second) return true; return false; };
            struct Sym { wxString name; int kind; size_t pos, end, rangeEnd; };
            std::vector<Sym> syms;
            for (const auto& r : *rules)
                for (std::sregex_iterator it(text.begin(), text.end(), r.re), e; it != e; ++it)
                {
                    const auto& m = *it;
                    if (m.position(r.grp) < 0) continue;
                    const size_t np = (size_t)m.position(r.grp);
                    if (inZone(np)) continue;
                    syms.push_back({ wxString::FromUTF8(m.str(r.grp).c_str()), r.kind, np, (size_t)(m.position(0) + m.length(0)), 0 });
                }
            for (auto& s : syms)                                   // compute each class's range (for nesting methods)
            {
                if (s.kind != 1) { s.rangeEnd = s.end; continue; }
                if (lang == "python")
                {
                    size_t ls = text.rfind('\n', s.pos); ls = (ls == std::string::npos) ? 0 : ls + 1;
                    size_t ind = 0; while (ls + ind < text.size() && (text[ls + ind] == ' ' || text[ls + ind] == '\t')) ind++;
                    size_t i = s.pos; s.rangeEnd = text.size();
                    while ((i = text.find('\n', i)) != std::string::npos)
                    {
                        size_t l = i + 1, k = 0; while (l + k < text.size() && (text[l + k] == ' ' || text[l + k] == '\t')) k++;
                        if (l + k < text.size() && text[l + k] != '\n' && text[l + k] != '\r' && k <= ind) { s.rangeEnd = l; break; }
                        i = l;
                    }
                }
                else
                {
                    int depth = 1; s.rangeEnd = text.size();
                    for (size_t i = s.end; i < text.size(); ++i)
                    {
                        if (inZone(i)) continue;
                        if (text[i] == '{') depth++;
                        else if (text[i] == '}' && --depth == 0) { s.rangeEnd = i; break; }
                    }
                }
            }
            std::sort(syms.begin(), syms.end(), [](const Sym& a, const Sym& b) { return a.pos < b.pos; });
            struct Open { size_t rangeEnd; wxTreeItemId item; };
            std::vector<Open> stack;
            for (auto& s : syms)
            {
                while (!stack.empty() && s.pos >= stack.back().rangeEnd) stack.pop_back();
                const wxTreeItemId parent = stack.empty() ? root : stack.back().item;
                const int line = (int)sci(SCI_LINEFROMPOSITION, (uptr_t)s.pos);
                const wxTreeItemId item = m_funcList->AppendItem(parent, s.name, s.kind == 1 ? 1 : 0, -1, new FLItemData(line));
                if (s.kind == 1) { m_funcList->SetItemBold(item, true); stack.push_back({ s.rangeEnd, item }); }
            }
            m_funcList->ExpandAll();
        }
        m_funcList->Thaw();
    }
    void onFuncListActivate(wxTreeEvent& e)
    {
        if (!m_funcList) return;
        if (auto* d = dynamic_cast<FLItemData*>(m_funcList->GetItemData(e.GetItem())))
        {
            sci(SCI_ENSUREVISIBLE, d->line);
            sci(SCI_GOTOLINE, d->line);
            const int half = (int)sci(SCI_LINESONSCREEN) / 2;
            sci(SCI_SETFIRSTVISIBLELINE, d->line > half ? d->line - half : 0);
            m_stc->SetFocus();
        }
        e.Skip();
    }
    void toggleFuncList()
    {
        if (!m_funcList) return;
        wxAuiPaneInfo& pi = m_aui.GetPane(m_funcList);
        if (!pi.IsOk()) return;
        pi.Show(!pi.IsShown());
        m_aui.Update();
        if (pi.IsShown()) parseFuncList();
    }

    // ---- Document List (dockable list of the open documents; click an entry to switch to it) -------
    wxListBox* m_docList = nullptr;
    void buildDocList()
    {
        m_docList = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_SINGLE | wxBORDER_NONE);
        const int bg = (int)sci(SCI_STYLEGETBACK, STYLE_DEFAULT), fg = (int)sci(SCI_STYLEGETFORE, STYLE_DEFAULT);
        m_docList->SetBackgroundColour(wxColour(bg & 0xFF, (bg >> 8) & 0xFF, (bg >> 16) & 0xFF));   // match the editor (Scintilla colours are BGR)
        m_docList->SetForegroundColour(wxColour(fg & 0xFF, (fg >> 8) & 0xFF, (fg >> 16) & 0xFF));
        m_docList->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& ev) {
            const int s = ev.GetSelection();
            if (m_tabs && s >= 0 && (size_t)s < m_tabs->GetPageCount()) m_tabs->SetSelection((size_t)s);
        });
        m_aui.AddPane(m_docList, wxAuiPaneInfo().Name("doclist").Caption("Document List")
                          .Right().BestSize(210, 400).MinSize(110, 80).CloseButton(true).Hide());
        m_aui.Update();
    }
    void refreshDocList()   // keep the panel's entries + selection in sync with the open tabs (no-op when hidden)
    {
        if (!m_docList) return;
        wxAuiPaneInfo& pi = m_aui.GetPane(m_docList);
        if (!pi.IsOk() || !pi.IsShown()) return;
        m_docList->Freeze();
        m_docList->Clear();
        if (m_tabs) for (size_t i = 0; i < m_tabs->GetPageCount(); ++i) m_docList->Append(m_tabs->GetPageText(i));
        if (m_tabs && m_tabs->GetSelection() != wxNOT_FOUND) m_docList->SetSelection(m_tabs->GetSelection());
        m_docList->Thaw();
    }
    void toggleDocList()
    {
        if (!m_docList) buildDocList();
        wxAuiPaneInfo& pi = m_aui.GetPane(m_docList);
        if (!pi.IsOk()) return;
        pi.Show(!pi.IsShown());
        m_aui.Update();
        if (pi.IsShown()) refreshDocList();
    }

    // ---- Folder as Workspace (a dockable file-system browser rooted at a chosen folder) -----------
    wxGenericDirCtrl* m_fileBrowser = nullptr;
    void toggleFileBrowser()
    {
        if (!m_fileBrowser)
        {
            wxString root = curPath().empty() ? wxGetCwd() : wxFileName(curPath()).GetPath();   // start expanded at the current file's folder
            if (root.empty()) root = wxGetCwd();
            m_fileBrowser = new wxGenericDirCtrl(this, wxID_ANY, root, wxDefaultPosition, wxDefaultSize,
                                                 wxDIRCTRL_SHOW_FILTERS | wxBORDER_NONE);
            if (auto* tree = m_fileBrowser->GetTreeCtrl())   // theme the tree to the editor's colours
            {
                const int bg = (int)sci(SCI_STYLEGETBACK, STYLE_DEFAULT), fg = (int)sci(SCI_STYLEGETFORE, STYLE_DEFAULT);
                tree->SetBackgroundColour(wxColour(bg & 0xFF, (bg >> 8) & 0xFF, (bg >> 16) & 0xFF));
                tree->SetForegroundColour(wxColour(fg & 0xFF, (fg >> 8) & 0xFF, (fg >> 16) & 0xFF));
            }
            m_fileBrowser->Bind(wxEVT_DIRCTRL_FILEACTIVATED, [this](wxTreeEvent&) {
                const wxString f = m_fileBrowser->GetFilePath();
                if (!f.empty() && wxFileExists(f)) openPath(f);   // double-click a file -> open it
            });
            m_aui.AddPane(m_fileBrowser, wxAuiPaneInfo().Name("filebrowser").Caption("Folder as Workspace")
                              .Left().BestSize(240, 500).MinSize(140, 100).CloseButton(true).Hide());
        }
        wxAuiPaneInfo& pi = m_aui.GetPane(m_fileBrowser);
        if (!pi.IsOk()) return;
        pi.Show(!pi.IsShown());
        m_aui.Update();
    }

    // ---- Incremental Search (Ctrl+Alt+I): find-as-you-type bar; Enter = next, Esc = close ---------
    wxPanel*    m_incBar = nullptr;
    wxTextCtrl* m_incField = nullptr;
    int         m_incAnchor = 0;
    void buildIncBar()
    {
        m_incBar = new wxPanel(this);
        if (m_dark) m_incBar->SetBackgroundColour(wxColour(45, 45, 45));
        auto* sz  = new wxBoxSizer(wxHORIZONTAL);
        auto* lbl = new wxStaticText(m_incBar, wxID_ANY, "Find: ");
        if (m_dark) lbl->SetForegroundColour(wxColour(220, 220, 220));
        m_incField = new wxTextCtrl(m_incBar, wxID_ANY, "", wxDefaultPosition, wxSize(260, -1), wxTE_PROCESS_ENTER);
        if (m_dark) { m_incField->SetBackgroundColour(wxColour(30, 30, 30)); m_incField->SetForegroundColour(wxColour(220, 220, 220)); }
        auto* nextB  = new wxButton(m_incBar, wxID_ANY, "Next",  wxDefaultPosition, wxSize(60, -1));
        auto* closeB = new wxButton(m_incBar, wxID_ANY, "Close", wxDefaultPosition, wxSize(60, -1));
        sz->Add(lbl, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
        sz->Add(m_incField, 0, wxALIGN_CENTER_VERTICAL | wxALL, 4);
        sz->Add(nextB,  0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        sz->Add(closeB, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        m_incBar->SetSizer(sz);
        m_incField->Bind(wxEVT_TEXT,       [this](wxCommandEvent&){ incFind(true);  });
        m_incField->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&){ incFind(false); });
        m_incField->Bind(wxEVT_CHAR_HOOK,  [this](wxKeyEvent& e){ if (e.GetKeyCode() == WXK_ESCAPE) hideIncBar(); else e.Skip(); });
        nextB->Bind(wxEVT_BUTTON,  [this](wxCommandEvent&){ incFind(false); m_incField->SetFocus(); });
        closeB->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){ hideIncBar(); });
        m_aui.AddPane(m_incBar, wxAuiPaneInfo().Name("incsearch").Bottom().CaptionVisible(false)
                          .DockFixed(true).Resizable(false).BestSize(-1, 38).MinSize(-1, 34).CloseButton(false).Hide());
        m_aui.Update();
    }
    void incFind(bool fromAnchor)   // search from the original anchor (typing) or from the current match (Next)
    {
        if (!m_incField) return;
        const wxString needle = m_incField->GetValue();
        const wxColour ok = m_dark ? wxColour(220, 220, 220) : *wxBLACK;
        if (needle.empty()) { sci(SCI_SETSEL, m_incAnchor, m_incAnchor); m_incField->SetForegroundColour(ok); m_incField->Refresh(); return; }
        const int start = fromAnchor ? m_incAnchor : (int)sci(SCI_GETSELECTIONEND);
        sci(SCI_SETSEL, start, start);
        sci(SCI_SEARCHANCHOR);
        const wxScopedCharBuffer u = needle.utf8_str();
        sptr_t pos = sci(SCI_SEARCHNEXT, 0, reinterpret_cast<sptr_t>(u.data()));
        if (pos < 0) { sci(SCI_SETSEL, 0, 0); sci(SCI_SEARCHANCHOR); pos = sci(SCI_SEARCHNEXT, 0, reinterpret_cast<sptr_t>(u.data())); }   // wrap to top
        m_incField->SetForegroundColour(pos >= 0 ? ok : *wxRED);   // red field = not found
        m_incField->Refresh();
        if (pos >= 0) sci(SCI_SCROLLCARET);
    }
    void showIncBar()
    {
        if (!m_incBar) buildIncBar();
        m_incAnchor = (int)sci(SCI_GETCURRENTPOS);
        wxAuiPaneInfo& pi = m_aui.GetPane(m_incBar);
        if (!pi.IsOk()) return;
        pi.Show(); m_aui.Update();
        m_incField->ChangeValue(""); m_incField->SetFocus();
    }
    void hideIncBar()
    {
        if (!m_incBar) return;
        wxAuiPaneInfo& pi = m_aui.GetPane(m_incBar);
        if (pi.IsOk() && pi.IsShown()) { pi.Hide(); m_aui.Update(); }
        if (m_stc) m_stc->SetFocus();
    }

    // ---- Column Editor (Alt+C): insert text or an incrementing number down a column selection ------
    static std::string colNum(long v, int base)
    {
        if (base == 10 || v < 0) return std::to_string(v);
        unsigned long u = (unsigned long)v; const char* digits = "0123456789abcdef"; std::string s;
        if (u == 0) return "0";
        while (u) { s += digits[u % (unsigned)base]; u /= (unsigned)base; }
        std::reverse(s.begin(), s.end()); return s;
    }
    void columnEditor()
    {
        ColumnEditorDialog dlg(this, m_dark);
        themeDialog(&dlg);
        if (dlg.ShowModal() != wxID_OK) return;
        const int nsel = (int)sci(SCI_GETSELECTIONS);
        if (nsel <= 0) return;
        struct Seln { int a, b; };
        std::vector<Seln> sels;
        for (int i = 0; i < nsel; ++i) { int a = (int)sci(SCI_GETSELECTIONNSTART, i), b = (int)sci(SCI_GETSELECTIONNEND, i); if (a > b) std::swap(a, b); sels.push_back({ a, b }); }
        std::sort(sels.begin(), sels.end(), [](const Seln& x, const Seln& y) { return x.a < y.a; });   // top-to-bottom for the number order
        std::vector<std::string> ins(sels.size());
        if (dlg.isText())
        {
            const std::string t(dlg.text().utf8_str());
            for (auto& v : ins) v = t;
        }
        else
        {
            const long init = dlg.initial(), inc = dlg.increase(); const int base = dlg.base(); size_t w = 0;
            for (size_t i = 0; i < sels.size(); ++i) { ins[i] = colNum(init + (long)i * inc, base); w = std::max(w, ins[i].size()); }
            if (dlg.leadingZeros()) for (auto& v : ins) if (v.size() < w) v = std::string(w - v.size(), '0') + v;
        }
        sci(SCI_BEGINUNDOACTION);
        for (int i = (int)sels.size() - 1; i >= 0; --i)   // apply bottom-to-top so earlier edits don't shift later offsets
        {
            sci(SCI_SETTARGETRANGE, sels[i].a, sels[i].b);
            sci(SCI_REPLACETARGET, (uptr_t)ins[i].size(), reinterpret_cast<sptr_t>(ins[i].c_str()));
        }
        sci(SCI_ENDUNDOACTION);
    }

    // ---- Find in Files: a search dialog + a docked "Find result" panel (double-click a hit to jump) --
    void buildFifPanel()
    {
        m_fifPanel = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                    wxTR_HAS_BUTTONS | wxTR_HIDE_ROOT | wxTR_FULL_ROW_HIGHLIGHT | wxBORDER_NONE);
        m_fifPanel->Bind(wxEVT_TREE_ITEM_ACTIVATED, &NppShellFrameT::onFifActivate, this);
        m_aui.AddPane(m_fifPanel, wxAuiPaneInfo().Name("findresult").Caption("Find result")
                          .Bottom().BestSize(800, 180).MinSize(150, 70).CloseButton(true).Hide());
        m_aui.Update();
    }
    void onFindInFiles()
    {
        wxDialog dlg(this, wxID_ANY, "Find in Files");
        auto* find = new wxTextCtrl(&dlg, wxID_ANY, selText());
        wxString dir; if (auto* p = activePage()) dir = wxPathOnly(p->path); if (dir.empty()) dir = wxGetCwd();
        auto* dirc = new wxTextCtrl(&dlg, wxID_ANY, dir);
        auto* filt = new wxTextCtrl(&dlg, wxID_ANY, "*.*");
        auto* cc = new wxCheckBox(&dlg, wxID_ANY, "Match case");
        auto* ww = new wxCheckBox(&dlg, wxID_ANY, "Whole word only");
        auto* rx = new wxCheckBox(&dlg, wxID_ANY, "Regular expression");
        auto* sd = new wxCheckBox(&dlg, wxID_ANY, "In all sub-folders"); sd->SetValue(true);
        auto* gs = new wxFlexGridSizer(2, 8, 8); gs->AddGrowableCol(1);
        gs->Add(new wxStaticText(&dlg, wxID_ANY, "Find what:"), 0, wxALIGN_CENTRE_VERTICAL); gs->Add(find, 1, wxEXPAND);
        gs->Add(new wxStaticText(&dlg, wxID_ANY, "Directory:"), 0, wxALIGN_CENTRE_VERTICAL);
        auto* drow = new wxBoxSizer(wxHORIZONTAL); drow->Add(dirc, 1, wxEXPAND);
        auto* browse = new wxButton(&dlg, wxID_ANY, "...", wxDefaultPosition, wxSize(32, -1)); drow->Add(browse, 0, wxLEFT, 4);
        gs->Add(drow, 1, wxEXPAND);
        gs->Add(new wxStaticText(&dlg, wxID_ANY, "Filters:"), 0, wxALIGN_CENTRE_VERTICAL); gs->Add(filt, 1, wxEXPAND);
        browse->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { wxDirDialog dd(&dlg, "Choose folder", dirc->GetValue()); if (dd.ShowModal() == wxID_OK) dirc->SetValue(dd.GetPath()); });
        auto* opt = new wxBoxSizer(wxHORIZONTAL); opt->Add(cc, 0, wxRIGHT, 12); opt->Add(ww, 0, wxRIGHT, 12); opt->Add(rx, 0, wxRIGHT, 12); opt->Add(sd, 0);
        auto* btn = new wxBoxSizer(wxHORIZONTAL); btn->AddStretchSpacer();
        auto* findAll = new wxButton(&dlg, wxID_OK, "Find All"); findAll->SetDefault();   // Enter submits
        btn->Add(findAll, 0, wxRIGHT, 6); btn->Add(new wxButton(&dlg, wxID_CANCEL, "Close"), 0);
        auto* top = new wxBoxSizer(wxVERTICAL);
        top->Add(gs, 0, wxEXPAND | wxALL, 12); top->Add(opt, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12); top->Add(btn, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
        dlg.SetSizerAndFit(top); dlg.SetSize(wxSize(480, dlg.GetSize().GetHeight()));
        find->SetFocus();
        themeDialog(&dlg);
        if (dlg.ShowModal() != wxID_OK || find->GetValue().empty()) return;
        std::vector<FifHit> hits; int searched = 0;
        { wxBusyCursor busy; findInFiles(find->GetValue(), dirc->GetValue(), filt->GetValue(),
                                         cc->IsChecked(), ww->IsChecked(), rx->IsChecked(), sd->IsChecked(), hits, searched); }
        showFifResults(find->GetValue(), hits, searched);
    }
    wxString curWord()   // the identifier under the caret (for $(CURRENT_WORD))
    {
        const int p = (int)sci(SCI_GETCURRENTPOS);
        const int a = (int)sci(SCI_WORDSTARTPOSITION, p, 1), b = (int)sci(SCI_WORDENDPOSITION, p, 1);
        if (b <= a) return "";
        sci(SCI_SETTARGETSTART, a); sci(SCI_SETTARGETEND, b);
        std::string buf((size_t)(b - a) + 1, '\0'); sci(SCI_GETTARGETTEXT, 0, reinterpret_cast<sptr_t>(&buf[0])); buf.resize(b - a);
        return wxString::FromUTF8(buf.c_str(), buf.size());
    }
    wxString substituteRunVars(wxString c)   // expand Notepad++'s $(...) Run variables
    {
        const wxString full = curPath(); wxFileName fn(full);
        c.Replace("$(FULL_CURRENT_PATH)", full);
        c.Replace("$(CURRENT_DIRECTORY)", fn.GetPath());
        c.Replace("$(FILE_NAME)", fn.GetFullName());
        c.Replace("$(NAME_PART)", fn.GetName());
        c.Replace("$(EXT_PART)", fn.GetExt());
        c.Replace("$(CURRENT_WORD)", curWord());
        c.Replace("$(CURRENT_LINE)", wxString::Format("%d", (int)sci(SCI_LINEFROMPOSITION, sci(SCI_GETCURRENTPOS)) + 1));
        c.Replace("$(CURRENT_COLUMN)", wxString::Format("%d", (int)sci(SCI_GETCOLUMN, sci(SCI_GETCURRENTPOS)) + 1));
        c.Replace("$(NPP_DIRECTORY)", wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath());
        c.Replace("$(NPP_FULL_FILE_PATH)", wxStandardPaths::Get().GetExecutablePath());
        return c;
    }
    void onRun()   // Run dialog (F5): enter a command with $(...) variables and launch it
    {
        wxString cmd; wxConfigBase::Get()->Read("RunCommand", &cmd, "");
        wxDialog dlg(this, wxID_ANY, "Run...");
        auto* tc = new wxTextCtrl(&dlg, wxID_ANY, cmd, wxDefaultPosition, wxSize(520, -1));
        auto* browse = new wxButton(&dlg, wxID_ANY, "...", wxDefaultPosition, wxSize(32, -1));
        auto* row = new wxBoxSizer(wxHORIZONTAL); row->Add(tc, 1, wxEXPAND); row->Add(browse, 0, wxLEFT, 4);
        auto* btn = new wxBoxSizer(wxHORIZONTAL); btn->AddStretchSpacer();
        auto* runb = new wxButton(&dlg, wxID_OK, "Run"); runb->SetDefault();
        btn->Add(runb, 0, wxRIGHT, 6); btn->Add(new wxButton(&dlg, wxID_CANCEL, "Cancel"), 0);
        auto* top = new wxBoxSizer(wxVERTICAL);
        top->Add(new wxStaticText(&dlg, wxID_ANY, "The Program to Run   (variables: $(FULL_CURRENT_PATH), $(CURRENT_DIRECTORY),\n$(FILE_NAME), $(CURRENT_WORD), $(CURRENT_LINE) ...)"), 0, wxALL, 12);
        top->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
        top->Add(btn, 0, wxEXPAND | wxALL, 12);
        dlg.SetSizerAndFit(top); dlg.SetSize(wxSize(540, dlg.GetSize().GetHeight()));
        tc->SetFocus(); tc->SetInsertionPointEnd();
        browse->Bind(wxEVT_BUTTON, [&](wxCommandEvent&){ wxFileDialog fd(&dlg, "Select a program to run", "", "", "Programs (*.exe;*.bat;*.cmd)|*.exe;*.bat;*.cmd|All files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST); if (fd.ShowModal() == wxID_OK) tc->SetValue("\"" + fd.GetPath() + "\" \"$(FULL_CURRENT_PATH)\""); });
        themeDialog(&dlg);
        if (dlg.ShowModal() != wxID_OK) return;
        cmd = tc->GetValue().Trim().Trim(false); if (cmd.empty()) return;
        wxConfigBase::Get()->Write("RunCommand", cmd); wxConfigBase::Get()->Flush();
        const wxString full = substituteRunVars(cmd);
        if (wxExecute(full, wxEXEC_ASYNC) == 0) wxMessageBox("Failed to run:\n" + full, "Run", wxOK | wxICON_ERROR, this);
    }

    // ----- sessions (File > Save / Load Session) ------------------------
    // Notepad++-style session XML: <NotepadPlus><Session><mainView><File .../></mainView></Session>.
    // The active buffer also stores caret position, first-visible line, and bookmarks (the structure NotepadNext uses).
    void saveSession()
    {
        if (!m_tabs) return;
        wxFileDialog d(this, "Save Session", "", "session.xml", "Session files (*.xml)|*.xml|All files (*.*)|*.*", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (d.ShowModal() != wxID_OK) return;
        const int active = m_tabs->GetSelection();
        wxXmlDocument doc;
        auto* root = new wxXmlNode(wxXML_ELEMENT_NODE, "NotepadPlus");
        auto* sess = new wxXmlNode(wxXML_ELEMENT_NODE, "Session"); sess->AddAttribute("activeView", "0");
        auto* view = new wxXmlNode(wxXML_ELEMENT_NODE, "mainView");
        int saved = 0, sessActive = 0;
        for (size_t i = 0; i < m_tabs->GetPageCount(); ++i)
        {
            auto* p = static_cast<EditorPage*>(m_tabs->GetPage(i));
            if (p->path.empty()) continue;                       // only real (saved) files go in a session
            auto* f = new wxXmlNode(wxXML_ELEMENT_NODE, "File");
            f->AddAttribute("filename", p->path); f->AddAttribute("lang", p->lang);
            if ((int)i == active)                                // the live view holds caret/scroll/marks for the active tab
            {
                f->AddAttribute("position", wxString::Format("%d", (int)sci(SCI_GETCURRENTPOS)));
                f->AddAttribute("firstVisibleLine", wxString::Format("%d", (int)sci(SCI_GETFIRSTVISIBLELINE)));
                int ln = -1; const int lc = (int)sci(SCI_GETLINECOUNT);
                while ((ln = (int)sci(SCI_MARKERNEXT, ln + 1, 1 << MARK_BOOKMARK)) >= 0 && ln < lc)
                { auto* mk = new wxXmlNode(wxXML_ELEMENT_NODE, "Mark"); mk->AddAttribute("line", wxString::Format("%d", ln)); f->AddChild(mk); }
                sessActive = saved;
            }
            else { f->AddAttribute("position", "0"); f->AddAttribute("firstVisibleLine", "0"); }
            view->AddChild(f); ++saved;
        }
        view->AddAttribute("activeIndex", wxString::Format("%d", sessActive));
        sess->AddChild(view); root->AddChild(sess); doc.SetRoot(root);
        if (doc.Save(d.GetPath())) { setStatus(0, wxString::Format("Session saved - %d file(s)", saved)); m_hint = true; }
    }
    void loadSession()
    {
        wxFileDialog d(this, "Load Session", "", "", "Session files (*.xml)|*.xml|All files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (d.ShowModal() != wxID_OK) return;
        wxXmlDocument doc;
        if (!doc.Load(d.GetPath()) || !doc.GetRoot()) { wxMessageBox("Could not read the session file.", "Load Session", wxOK | wxICON_ERROR, this); return; }
        wxXmlNode* view = nullptr;
        for (wxXmlNode* a = doc.GetRoot()->GetChildren(); a && !view; a = a->GetNext())
            if (a->GetName() == "Session")
                for (wxXmlNode* b = a->GetChildren(); b; b = b->GetNext())
                    if (b->GetName() == "mainView") { view = b; break; }
        if (!view) { wxMessageBox("No session data in this file.", "Load Session", wxOK | wxICON_WARNING, this); return; }
        const int activeIndex = wxAtoi(view->GetAttribute("activeIndex", "0"));
        std::vector<EditorPage*> opened;
        for (wxXmlNode* f = view->GetChildren(); f; f = f->GetNext())
        {
            if (f->GetName() != "File") continue;
            const wxString path = f->GetAttribute("filename");
            if (path.empty() || !wxFileExists(path)) continue;
            openPath(path);                                      // opens it in a new tab and makes it active
            sci(SCI_GOTOPOS, wxAtoi(f->GetAttribute("position", "0")));
            sci(SCI_SETFIRSTVISIBLELINE, wxAtoi(f->GetAttribute("firstVisibleLine", "0")));
            for (wxXmlNode* mk = f->GetChildren(); mk; mk = mk->GetNext())
                if (mk->GetName() == "Mark") sci(SCI_MARKERADD, wxAtoi(mk->GetAttribute("line", "0")), MARK_BOOKMARK);
            opened.push_back(activePage());
        }
        if (activeIndex >= 0 && activeIndex < (int)opened.size() && opened[activeIndex])
        { const int idx = m_tabs->GetPageIndex(opened[activeIndex]); if (idx != wxNOT_FOUND) m_tabs->SetSelection(idx); }
        setStatus(0, wxString::Format("Session loaded - %d file(s)", (int)opened.size())); m_hint = true;
    }

    // ----- auto-completion (word / keyword / path) ----------------------
    // Word completion adapted from NotepadNext (scan the document for words sharing the prefix);
    // keyword completion adapted from notepad-- (merge the language keyword list). Path completion is ours.
    std::string rangeText(int a, int b) { if (b <= a) return {}; sci(SCI_SETTARGETSTART, a); sci(SCI_SETTARGETEND, b); std::string s((size_t)(b - a) + 1, '\0'); sci(SCI_GETTARGETTEXT, 0, reinterpret_cast<sptr_t>(&s[0])); s.resize(b - a); return s; }
    static const char* keywordsForExt(const wxString& ext)
    {
        if (ext == "js" || ext == "jsx" || ext == "ts" || ext == "tsx") return JS_KEYWORDS;
        if (ext == "java") return JAVA_KEYWORDS;
        if (ext == "cs") return CS_KEYWORDS;
        if (ext == "c" || ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "h" || ext == "hpp" || ext == "hxx" || ext == "rc") return CPP_KEYWORDS;
        return nullptr;   // no keyword list -> document-word completion only
    }
    const char* keywordsForActiveLang() { auto* p = activePage(); return p ? keywordsForExt(p->path.AfterLast('.').Lower()) : nullptr; }
    static void collectWords(const std::string& doc, const std::string& prefix, std::set<std::string>& out)
    {
        auto isW = [](unsigned char c){ return std::isalnum(c) || c == '_'; }; const size_t pl = prefix.size();
        for (size_t i = 0; i < doc.size(); )
        {
            if (!isW((unsigned char)doc[i])) { ++i; continue; }
            size_t j = i; while (j < doc.size() && isW((unsigned char)doc[j])) ++j;
            if (j - i > pl && doc.compare(i, pl, prefix) == 0) out.insert(doc.substr(i, j - i));
            i = j;
        }
    }
    void collectKeywords(const std::string& prefix, std::set<std::string>& out)
    {
        const char* kw = keywordsForActiveLang(); if (!kw) return;
        const std::string s(kw); const size_t pl = prefix.size();
        for (size_t i = 0; i < s.size(); )
        {
            if (s[i] == ' ' || s[i] == '\n' || s[i] == '\t') { ++i; continue; }
            size_t j = i; while (j < s.size() && s[j] != ' ' && s[j] != '\n' && s[j] != '\t') ++j;
            if (j - i > pl && s.compare(i, pl, prefix) == 0) out.insert(s.substr(i, j - i));
            i = j;
        }
    }
    void autoComplete(bool withKeywords)
    {
        if (!m_stc) return;
        const int caret = (int)sci(SCI_GETCURRENTPOS), start = (int)sci(SCI_WORDSTARTPOSITION, caret, 1), plen = caret - start;
        if (plen <= 0) { sci(SCI_AUTOCCANCEL); return; }
        const std::string prefix = rangeText(start, caret);
        std::set<std::string> cand; collectWords(getDocUtf8(), prefix, cand);
        if (withKeywords) collectKeywords(prefix, cand);
        cand.erase(prefix);
        if (cand.empty()) { sci(SCI_AUTOCCANCEL); return; }
        std::string list; for (const auto& w : cand) { if (!list.empty()) list += ' '; list += w; }
        sci(SCI_AUTOCSETIGNORECASE, 0); sci(SCI_AUTOCSETSEPARATOR, ' '); sci(SCI_AUTOCSETMAXHEIGHT, 10); sci(SCI_AUTOCSETORDER, SC_ORDER_PERFORMSORT);
        sci(SCI_AUTOCSHOW, plen, reinterpret_cast<sptr_t>(list.c_str()));
    }
    void autoCompletePath()
    {
        if (!m_stc) return;
        const int caret = (int)sci(SCI_GETCURRENTPOS);
        auto stop = [](char c){ return c == '"' || c == '\'' || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '<' || c == '>' || c == '(' || c == ')'; };
        int start = caret; while (start > 0 && !stop((char)sci(SCI_GETCHARAT, start - 1))) --start;
        const std::string frag = rangeText(start, caret); if (frag.empty()) { sci(SCI_AUTOCCANCEL); return; }
        wxString wf = wxString::FromUTF8(frag.data(), frag.size());
        const int sl = std::max(wf.Find('/', true), wf.Find('\\', true));
        wxString dir, partial;
        if (sl == wxNOT_FOUND) { dir = curPath().empty() ? wxGetCwd() : wxFileName(curPath()).GetPath(); partial = wf; }
        else { dir = wf.Left(sl + 1); partial = wf.Mid(sl + 1); }
        std::set<std::string> cand; wxDir d(dir);
        if (d.IsOpened())
        { wxString name; bool more = d.GetFirst(&name, wxEmptyString, wxDIR_FILES | wxDIR_DIRS); while (more) { if (partial.empty() || name.Lower().StartsWith(partial.Lower())) cand.insert(std::string(name.utf8_str())); more = d.GetNext(&name); } }
        if (cand.empty()) { sci(SCI_AUTOCCANCEL); return; }
        std::string list; for (const auto& w : cand) { if (!list.empty()) list += ' '; list += w; }
        sci(SCI_AUTOCSETSEPARATOR, ' '); sci(SCI_AUTOCSETORDER, SC_ORDER_PERFORMSORT);
        sci(SCI_AUTOCSHOW, (int)std::string(partial.utf8_str()).size(), reinterpret_cast<sptr_t>(list.c_str()));
    }
    void showFifResults(const wxString& term, const std::vector<FifHit>& hits, int searched)
    {
        if (!m_fifPanel) return;
        wxAuiPaneInfo& pi = m_aui.GetPane(m_fifPanel);
        if (pi.IsOk()) { pi.Show(); m_aui.Update(); }
        m_fifPanel->Freeze();
        m_fifPanel->DeleteAllItems();
        const wxTreeItemId root = m_fifPanel->AddRoot("");
        std::set<wxString> files; for (const auto& h : hits) files.insert(h.file);
        const wxTreeItemId head = m_fifPanel->AppendItem(root, wxString::Format("Search \"%s\"  (%d hits in %d files of %d searched)",
                                                          term, (int)hits.size(), (int)files.size(), searched));
        m_fifPanel->SetItemBold(head, true);
        wxString cur; wxTreeItemId fnode;
        for (const auto& h : hits)
        {
            if (h.file != cur) { cur = h.file; fnode = m_fifPanel->AppendItem(head, h.file); m_fifPanel->SetItemBold(fnode, true); }
            wxString t = h.text; t.Trim(true).Trim(false);
            m_fifPanel->AppendItem(fnode, wxString::Format("Line %d:  %s", h.line, t), -1, -1, new FifItemData(h.file, h.line));
        }
        m_fifPanel->ExpandAll();
        m_fifPanel->Thaw();
    }
    void onFifActivate(wxTreeEvent& e)
    {
        auto* d = m_fifPanel ? dynamic_cast<FifItemData*>(m_fifPanel->GetItemData(e.GetItem())) : nullptr;
        if (d)
        {
            EditorPage* pg = nullptr;
            for (size_t i = 0; i < m_tabs->GetPageCount(); ++i) { auto* p = static_cast<EditorPage*>(m_tabs->GetPage(i)); if (p->path == d->file) { pg = p; m_tabs->SetSelection(i); break; } }
            if (!pg) openPath(d->file);
            sci(SCI_GOTOLINE, d->line - 1);
            const int half = (int)sci(SCI_LINESONSCREEN) / 2;
            sci(SCI_SETFIRSTVISIBLELINE, (d->line - 1) > half ? (d->line - 1) - half : 0);
            m_stc->SetFocus();
        }
        e.Skip();
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
        mkBtn("M8 4.5 V11.5 M4.5 8 H11.5",               "New",            [this] { doNew(); });   // span 4.5..11.5, matching the "x"
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
        SetTitle(title + " - wxNotepad++");
        updateStatus();
        return page;
    }

    // The bookmark margin marker, drawn from the project's own bookmark icon (resources/icons/bookmark.svg)
    // as an RGBA image so its Open-Color accent shows through, instead of a flat Scintilla marker shape.
    void defineBookmarkMarker()
    {
        static const wxString path = wxPathOnly(wxStandardPaths::Get().GetExecutablePath()) + "\\icons\\bookmark.svg";
        const int sz = 14;   // matches the bookmark margin width
        wxImage img = wxFileExists(path)
            ? wxBitmapBundle::FromSVGFile(path, wxSize(sz, sz)).GetBitmap(wxSize(sz, sz)).ConvertToImage()
            : wxImage();
        if (!img.IsOk()) { sci(SCI_MARKERDEFINE, MARK_BOOKMARK, SC_MARK_BOOKMARK); return; }   // fallback to the built-in shape
        const int w = img.GetWidth(), h = img.GetHeight();
        const unsigned char* rgb = img.GetData();
        const unsigned char* a   = img.HasAlpha() ? img.GetAlpha() : nullptr;
        std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
        for (int i = 0; i < w * h; ++i)
        {
            rgba[i*4+0] = rgb[i*3+0]; rgba[i*4+1] = rgb[i*3+1]; rgba[i*4+2] = rgb[i*3+2];
            rgba[i*4+3] = a ? a[i] : 255;
        }
        sci(SCI_RGBAIMAGESETWIDTH,  w);
        sci(SCI_RGBAIMAGESETHEIGHT, h);
        sci(SCI_MARKERDEFINERGBAIMAGE, MARK_BOOKMARK, reinterpret_cast<sptr_t>(rgba.data()));
    }

    // Per-editor Scintilla configuration: margins, font, options, theme, scrollbars.
    void setupScintilla()
    {
        sci(SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);   // right-justified line numbers
        sci(SCI_SETMARGINWIDTHN, 1, 14);                 // bookmark/symbol margin (like Notepad++)
        sci(SCI_SETMARGINSENSITIVEN, 1, 1);
        defineBookmarkMarker();   // the project's own bookmark icon (resources/icons/bookmark.svg)
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
        g_editorDark = m_dark;   // so SciHwndProc repaints the scrollbar dead-corner to match in dark mode
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
        const int markActive = foldActive.first >= 0 ? foldActive.first : markBack;   // full "Fold active" accent on the active fold's box markers
        const int markers[7] = { SC_MARKNUM_FOLDEROPEN, SC_MARKNUM_FOLDER, SC_MARKNUM_FOLDERSUB, SC_MARKNUM_FOLDERTAIL,
                                 SC_MARKNUM_FOLDEREND, SC_MARKNUM_FOLDEROPENMID, SC_MARKNUM_FOLDERMIDTAIL };
        const int symbols[7] = { SC_MARK_BOXMINUS, SC_MARK_BOXPLUS, SC_MARK_VLINE, SC_MARK_LCORNER,   // box-tree (N++ default)
                                 SC_MARK_BOXPLUSCONNECTED, SC_MARK_BOXMINUSCONNECTED, SC_MARK_TCORNER };
        for (int i = 0; i < 7; ++i)
            sci(SCI_MARKERDEFINE, markers[i], symbols[i]);   // box-tree shapes (defined once; colours live in applyFoldMarkerColours)
        sci(SCI_MARKERENABLEHIGHLIGHT, 1);                   // active fold's top header box + connector lines take the accent
        applyFoldMarkerColours(markFore, markBack, markActive);
        sci(SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_SHOW | SC_AUTOMATICFOLD_CLICK | SC_AUTOMATICFOLD_CHANGE);
        sci(SCI_SETFOLDFLAGS, SC_FOLDFLAG_LINEAFTER_CONTRACTED);
    }
    // --- active-section accent for NESTED fold squares -------------------------------------------
    // Scintilla's fold highlight (SCI_MARKERENABLEHIGHLIGHT) accents the active fold's top header box
    // and its connector lines, but NEVER the nested child header boxes. A static colour therefore
    // can't make those inner squares follow the active/inactive state the lines already follow - the
    // user's "buttons don't follow this logic". So we recolour the nested-box markers on each caret
    // move: accent only while the caret sits in a section that actually contains nested folds, grey
    // otherwise. (Marker colours are global, so this tracks at section granularity - if two sibling
    // sections each nest, entering one tints both their inner squares. That's the one rough edge of
    // doing it without per-line marker colours, which Scintilla doesn't offer.)
    int  m_foldAccent = 0, m_foldGrey = 0;   // resolved from the theme in setupScintilla / applyEditorTheme
    int  m_foldNestedOn = -1;                // cached marker state: -1 unknown, 0 grey, 1 accent (avoids needless repaints)
    int  m_lastFoldSection = -2;             // last outermost-fold start line we evaluated (avoids needless re-scans)
    // Paint the 7 fold-margin markers (grey base + accent-when-active via the highlight) and re-arm the
    // nested-square accent. Shared by setupScintilla (after the shapes are defined) and applyEditorTheme.
    void applyFoldMarkerColours(int markFore, int markBack, int markActive)
    {
        for (int m : { SC_MARKNUM_FOLDEROPEN, SC_MARKNUM_FOLDER, SC_MARKNUM_FOLDERSUB, SC_MARKNUM_FOLDERTAIL, SC_MARKNUM_FOLDEREND, SC_MARKNUM_FOLDEROPENMID, SC_MARKNUM_FOLDERMIDTAIL })
        { sci(SCI_MARKERSETFORE, m, markFore); sci(SCI_MARKERSETBACK, m, markBack); sci(SCI_MARKERSETBACKSELECTED, m, markActive); }
        m_foldAccent = markActive; m_foldGrey = markBack; m_foldNestedOn = -1; m_lastFoldSection = -2;
        refreshFoldNestedAccent();
    }
    void refreshFoldNestedAccent()
    {
        if (!m_foldAccent && !m_foldGrey) return;   // colours not resolved yet
        const int caret = static_cast<int>(sci(SCI_LINEFROMPOSITION, sci(SCI_GETCURRENTPOS)));
        // INNERMOST fold the caret is in (same fold Scintilla highlights for the lines): the fold the
        // caret heads if it's on a header line, otherwise its immediate parent fold.
        const int fold = (sci(SCI_GETFOLDLEVEL, caret) & SC_FOLDLEVELHEADERFLAG)
                       ? caret : static_cast<int>(sci(SCI_GETFOLDPARENT, caret));
        if (fold == m_lastFoldSection) return;      // same fold -> verdict can't have changed
        m_lastFoldSection = fold;
        bool hasChildren = false;                   // does that fold DIRECTLY contain nested child folds?
        if (fold >= 0)
        {
            int last = static_cast<int>(sci(SCI_GETLASTCHILD, fold, -1));
            if (last > fold + 2000) last = fold + 2000;            // bound the scan on pathological folds
            for (int l = fold + 1; l <= last; ++l)
                if (sci(SCI_GETFOLDLEVEL, l) & SC_FOLDLEVELHEADERFLAG) { hasChildren = true; break; }
        }
        const int want = hasChildren ? 1 : 0;
        if (want == m_foldNestedOn) return;
        m_foldNestedOn = want;
        const int back = hasChildren ? m_foldAccent : m_foldGrey;
        sci(SCI_MARKERSETBACK, SC_MARKNUM_FOLDEROPENMID, back);   // BOXMINUSCONNECTED (expanded nested header)
        sci(SCI_MARKERSETBACK, SC_MARKNUM_FOLDEREND,     back);   // BOXPLUSCONNECTED  (collapsed nested header)
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
        m_lastFoldSection = -2; refreshFoldNestedAccent();                  // the nested-square accent marker is global to the view - re-evaluate it for the swapped-in document
        if (m_docMap) { m_docMap->SetDocPointer(reinterpret_cast<void*>(p->doc)); updateDocMapViewport(); }   // minimap follows the active doc
        parseFuncList();                                                    // re-parse symbols for the newly active doc
        refreshDocList();                                                   // keep the Document List panel + its selection in sync
        refreshTab(p);
        updateStatus();
        updateEncodingMenuChecks();   // tick this buffer's encoding in the Encoding menu
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

        wxDialog dlg(this, wxID_ANY, "wxNotepad++");
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
        saveSettings();    // persist any in-session View-menu toggle changes
        m_aui.UnInit();
        unloadNibPlugins();   // deactivate + unload Nib plugins (the GPL bridge removes its frame subclass first)
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
            const wxString t = star + (p->path.empty() ? p->title : p->path) + " - wxNotepad++";
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

    // Menu/tool-bar accessors that work in BOTH chrome modes. Integrated mode has no native menu bar
    // (menus live in m_menuOwner) and its toolbar is an aui pane, not the frame toolbar - so the built-in
    // GetMenuBar()/GetToolBar() return null there. Route all item enable/check/toggle state through these.
    wxMenuBar* menuBar() const
    {
#ifdef WXNPP_HAS_BORDERLESS
        if (m_menuOwner) return m_menuOwner;
#endif
        return GetMenuBar();
    }
    wxToolBar* toolBar() const { return m_toolBarPtr ? m_toolBarPtr : GetToolBar(); }
    void showToolBar(bool show)
    {
        auto* tb = toolBar(); if (!tb) return;
#ifdef WXNPP_HAS_BORDERLESS
        if constexpr (kBorderless) { auto& pi = m_aui.GetPane(tb); if (pi.IsOk()) { pi.Show(show); m_aui.Update(); } return; }
#endif
        tb->Show(show);   // native frame toolbar
    }

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
#ifdef WXNPP_HAS_BORDERLESS
        if constexpr (kBorderless)
        {
            m_menuOwner = mb;                  // keep the wxMenus alive for the title-bar buttons to pop
            installAccelsFromMenuBar(mb);      // keyboard shortcuts still fire without a native menu bar
            buildIntegratedTitleBar(mb);       // VS-style top bar: icon + menu-buttons + window controls
            return;
        }
#endif
        SetMenuBar(mb);
    }

#ifdef WXNPP_HAS_BORDERLESS
    // ---- integrated/borderless top bar (compiled only where wxBorderlessFrame exists; instantiated only
    //      for the borderless frame, i.e. kBorderless - the native frame never reaches these) ------------

    // Borderless hit-testing. We use real child controls for the menu/window buttons and a manual drag
    // (onTitleBarDrag), so the inner area is plain client; the lib still handles the resize borders itself.
    wxWindowPart GetWindowPart(wxPoint) const { return wxWP_CLIENT_AREA; }

    // A wxAcceleratorTable built from every menu item's shortcut, so Ctrl+S / Ctrl+F / ... keep working
    // even though integrated mode never attaches a native wxMenuBar.
    void installAccelsFromMenuBar(wxMenuBar* mb)
    {
        std::vector<wxAcceleratorEntry> accels;
        std::function<void(wxMenu*)> scan = [&](wxMenu* m) {
            for (wxMenuItem* item : m->GetMenuItems())
            {
                if (item->IsSubMenu()) { scan(item->GetSubMenu()); continue; }
                if (wxAcceleratorEntry* a = item->GetAccel())
                {
                    accels.push_back(wxAcceleratorEntry(a->GetFlags(), a->GetKeyCode(), item->GetId()));
                    delete a;
                }
            }
        };
        for (size_t i = 0; i < mb->GetMenuCount(); ++i) scan(mb->GetMenu(i));
        if (!accels.empty()) SetAcceleratorTable(wxAcceleratorTable((int)accels.size(), accels.data()));
    }

    void buildIntegratedTitleBar(wxMenuBar* mb)
    {
        const wxColour barBg = m_dark ? wxColour(45, 45, 48)    : wxColour(240, 240, 240);
        const wxColour barFg = m_dark ? wxColour(220, 220, 220) : wxColour(30, 30, 30);
        m_titleBar = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, kTitleBarH));
        m_titleBar->SetBackgroundColour(barBg);
        m_titleBar->Bind(wxEVT_LEFT_DOWN, &NppShellFrameT::onTitleBarDrag, this);   // drag empty area to move

        auto* sz = new wxBoxSizer(wxHORIZONTAL);

        // App icon (left)
        wxBitmap ico = wxBitmapBundle::FromSVG(APP_ICON_SVG, wxSize(18, 18)).GetBitmap(wxSize(18, 18));
        sz->Add(new wxStaticBitmap(m_titleBar, wxID_ANY, ico), 0, wxALIGN_CENTRE_VERTICAL | wxLEFT | wxRIGHT, 8);

        // Menu-buttons - each pops the same wxMenu the native menu bar would show. Pop on the frame so the
        // resulting command events land on our onCommand dispatcher (which is bound to the frame).
        for (size_t i = 0; i < mb->GetMenuCount(); ++i)
        {
            wxMenu* menu = mb->GetMenu(i);
            auto* b = new wxButton(m_titleBar, wxID_ANY, mb->GetMenuLabelText(i),
                                   wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxBORDER_NONE);
            b->SetBackgroundColour(barBg); b->SetForegroundColour(barFg);
            b->Bind(wxEVT_BUTTON, [this, b, menu](wxCommandEvent&) {
                wxPoint p = this->ScreenToClient(b->GetParent()->ClientToScreen(
                                b->GetPosition() + wxPoint(0, b->GetSize().y)));
                this->PopupMenu(menu, p);
            });
            sz->Add(b, 0, wxALIGN_CENTRE_VERTICAL);
        }

        sz->AddStretchSpacer(1);   // empty middle stays draggable

        // Window controls (right): minimize, maximize/restore, close (close hovers red, VS-style)
        auto ctrl = [&](const wxString& glyph, int which, const wxColour& hot) {
            auto* b = new wxButton(m_titleBar, wxID_ANY, glyph, wxDefaultPosition,
                                   wxSize(46, kTitleBarH), wxBU_EXACTFIT | wxBORDER_NONE);
            b->SetBackgroundColour(barBg); b->SetForegroundColour(barFg);
            b->Bind(wxEVT_BUTTON,       [this, which](wxCommandEvent&) { onWindowControl(which); });
            b->Bind(wxEVT_ENTER_WINDOW, [b, hot](wxMouseEvent& e)   { b->SetBackgroundColour(hot);   b->Refresh(); e.Skip(); });
            b->Bind(wxEVT_LEAVE_WINDOW, [b, barBg](wxMouseEvent& e) { b->SetBackgroundColour(barBg); b->Refresh(); e.Skip(); });
            sz->Add(b, 0, wxEXPAND);
        };
        const wxColour hover = m_dark ? wxColour(63, 63, 70) : wxColour(220, 220, 220);
        ctrl(L"—", 0, hover);                    // minimize  (em dash)
        ctrl(L"☐", 1, hover);                    // maximize  (ballot box)
        ctrl(L"✕", 2, wxColour(232, 17, 35));    // close     (multiplication X, red hover)

        m_titleBar->SetSizer(sz);
        m_aui.AddPane(m_titleBar, wxAuiPaneInfo().Name("titlebar").Top().Layer(2)
            .CaptionVisible(false).PaneBorder(false).Gripper(false).Floatable(false).Movable(false)
            .Dockable(false).DockFixed().Resizable(false).MinSize(-1, kTitleBarH).MaxSize(-1, kTitleBarH));
        m_aui.Update();
    }

    void onTitleBarDrag(wxMouseEvent& ev)
    {
#ifdef __WXMSW__
        ::ReleaseCapture();
        ::SendMessage(GetHandle(), WM_NCLBUTTONDOWN, HTCAPTION, 0);   // let Windows move the borderless window
#else
        ev.Skip();   // TODO: GTK window-move on Linux
#endif
    }

    void onWindowControl(int which)
    {
        if      (which == 0) Iconize(true);
        else if (which == 1) Maximize(!IsMaximized());
        else                 Close();
    }

    wxToolBar* makeIntegratedToolBar()
    {
        return new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxTB_FLAT | wxTB_HORIZONTAL | wxTB_NODIVIDER);
    }
    void dockIntegratedToolBar(wxToolBar* tb)
    {
        // ToolbarPane() must come FIRST: it turns gripper/movable/floatable back ON, so the locking flags
        // have to follow it. Result is a fixed toolbar - no drag gripper, not movable, not floatable.
        m_aui.AddPane(tb, wxAuiPaneInfo().Name("toolbar").ToolbarPane().Top().Layer(1)
            .Gripper(false).Floatable(false).Movable(false).Dockable(false).DockFixed()
            .CaptionVisible(false).PaneBorder(false).Resizable(false));
        m_aui.Update();
    }
#endif // WXNPP_HAS_BORDERLESS

    void addToMRU(const wxString& path)
    {
        m_fileHistory.AddFileToHistory(path);
        auto* c = wxConfigBase::Get(); c->SetPath("/RecentFiles"); m_fileHistory.Save(*c); c->SetPath("/"); c->Flush();
    }

    // ----- toolbar (Notepad++ icon pack, native order) -------------------
    wxBitmapBundle icon(const wxString& name)
    {
        static const wxString dir = wxPathOnly(wxStandardPaths::Get().GetExecutablePath()) + "\\icons\\";
        // Permissive toolbar icons (Tabler x Open Color, MIT). They paint with stroke="currentColor" plus a
        // few hardcoded accent hues (blue file ops, red/green macro, orange bookmark). Resolve currentColor
        // to Open-Color gray-3 on the dark toolbar / gray-8 on light, and collapse the accents to the SAME
        // colour so the toolbar is one colour (no blue-vs-white split).
        const wxString path = dir + name + ".svg";
        if (wxFileExists(path))
        {
            wxFile f(path); wxString svg;
            if (f.IsOpened() && f.ReadAll(&svg))
            {
                const wxString col = m_dark ? "#dee2e6" : "#343a40";
                svg.Replace("currentColor", col);
                for (const char* accent : { "#1971c2", "#e03131", "#2f9e44", "#f08c00", "#e8590c" })
                    svg.Replace(accent, col);
                const wxScopedCharBuffer u = svg.utf8_str();
                return wxBitmapBundle::FromSVG(u.data(), wxSize(16, 16));
            }
            return wxBitmapBundle::FromSVGFile(path, wxSize(16, 16));
        }
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
        // Native frame: the frame's own toolbar (CreateToolBar). Integrated frame: a child wxToolBar we
        // dock as an aui pane just under the title bar (the frame toolbar would otherwise sit above it).
        wxToolBar* tb;
#ifdef WXNPP_HAS_BORDERLESS
        if constexpr (kBorderless) tb = makeIntegratedToolBar();
        else                       tb = CreateToolBar(wxTB_FLAT | wxTB_HORIZONTAL);
#else
        tb = CreateToolBar(wxTB_FLAT | wxTB_HORIZONTAL);
#endif
        m_toolBarPtr = tb;                 // so toolBar() works in both modes (frame toolbar / aui pane)
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
#ifdef WXNPP_HAS_BORDERLESS
        if constexpr (kBorderless) dockIntegratedToolBar(tb);   // dock the child toolbar under the title bar
#endif
    }

    void buildStatusBar()
    {
#ifdef __WXMSW__
        // Drop the native size-grip (wxSTB_SIZEGRIP): Windows draws it on a light 3D corner that ignores
        // our dark chrome. We park our own dark-matching grip (SizeGripWin) in the corner instead.
        auto* sb = CreateStatusBar(7, wxSTB_DEFAULT_STYLE & ~wxSTB_SIZEGRIP);
#else
        auto* sb = CreateStatusBar(7);   // GTK/Cocoa theme their own grip correctly - keep the native one
#endif
        // field 0 (doc type / message area) is proportional so the fields fill the whole bar and
        // the typing-mode field lands flush right, like Notepad++ (no dead slack on the right).
        const int w[7] = { -1, 150, 210, 110, 130, 120, 46 };   // field 5 (encoding) wide enough for "UTF-16 LE BOM"
        sb->SetStatusWidths(7, w);
        const int styles[7] = { wxSB_FLAT, wxSB_FLAT, wxSB_FLAT, wxSB_FLAT, wxSB_FLAT, wxSB_FLAT, wxSB_FLAT };
        sb->SetStatusStyles(7, styles);   // flat fields - no per-field sunken background
#ifdef __WXMSW__
        // our own resize grip, parked in the bottom-right corner and kept there as the bar resizes
        m_grip = new SizeGripWin(sb);
        auto place = [this, sb]() {
            const wxSize ss = sb->GetClientSize();
            const int gw = 16;
            if (m_grip) m_grip->SetSize(ss.x - gw, 0, gw, ss.y);
        };
        sb->Bind(wxEVT_SIZE, [place](wxSizeEvent& e){ e.Skip(); place(); });
        place();
#endif
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
        auto* page = activePage();
        // A manual Language pick forces its lexer directly; otherwise auto-detect from the file extension.
        wxString lexer, themeKey, disp, ext;
        if (page && page->langForced) { lexer = page->forcedLexer; themeKey = page->forcedLexer; disp = page->forcedName; }
        else { ext = path.AfterLast('.').Lower(); const LexMap lm = lexerForExt(ext);
               lexer = lm.lexer ? lm.lexer : ""; themeKey = lm.theme ? lm.theme : ""; disp = langDisplayName(ext); }
        if (page) page->lang = disp;
        const bool hasLexer = !lexer.empty();
        const std::string lx = lexer.ToStdString();
        sci(SCI_SETILEXER, 0, reinterpret_cast<sptr_t>(hasLexer ? CreateLexer(lx.c_str()) : nullptr));
        if (hasLexer)
        {
            sci(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold"), reinterpret_cast<sptr_t>("1"));
            sci(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold.compact"), reinterpret_cast<sptr_t>("0"));
            bool themed = false;
            if (m_theme.loaded)
            {
                auto it = m_theme.lexers.find(themeKey.ToStdString());
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
            if (!themed) { if (lx == "python") stylePythonFallback(); else styleCppFallback(); }
            auto kw = [&](const char* words) { sci(SCI_SETKEYWORDS, 0, reinterpret_cast<sptr_t>(words)); };
            if (lx == "cpp") {   // shared C-family lexer: pick keywords by extension (auto) or picked name (forced)
                const wxString v = (page && page->langForced) ? page->forcedName : ext;
                if      (v=="js"||v=="jsx"||v=="ts"||v=="tsx"||v=="JavaScript"||v=="TypeScript") kw(JS_KEYWORDS);
                else if (v=="java"||v=="Java")                                                   kw(JAVA_KEYWORDS);
                else if (v=="cs"||v=="C#")                                                       kw(CS_KEYWORDS);
                else                                                                             kw(CPP_KEYWORDS);
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
    void setDocTitle(const wxString& name) { if (auto* p = activePage()) { p->title = name; refreshTab(p); } else SetTitle(name + " - wxNotepad++"); }
    void doNew() { addDocument("", nextNewName()); }   // New opens a fresh tab, like Notepad++
    // ===== text encoding: detect on load, encode on save (the Scintilla doc is always UTF-8) =====
    static bool isValidUtf8(const std::string& s)
    {
        for (size_t i = 0; i < s.size(); )
        {
            const unsigned char c = (unsigned char)s[i]; int n;
            if (c < 0x80) n = 0; else if ((c >> 5) == 0x6) n = 1; else if ((c >> 4) == 0xE) n = 2; else if ((c >> 3) == 0x1E) n = 3; else return false;
            for (int k = 1; k <= n; ++k) if (i + k >= s.size() || ((unsigned char)s[i + k] & 0xC0) != 0x80) return false;
            i += n + 1;
        }
        return true;
    }
    static std::string utf16ToUtf8(const std::string& b, bool be)
    {
        std::string out; auto rd = [&](size_t i){ unsigned char a = b[i], c = b[i + 1]; return be ? (unsigned)((a << 8) | c) : (unsigned)((c << 8) | a); };
        for (size_t i = 0; i + 1 < b.size(); )
        {
            unsigned u = rd(i); i += 2; unsigned cp;
            if (u >= 0xD800 && u <= 0xDBFF && i + 1 < b.size()) { unsigned lo = rd(i); i += 2; cp = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00); } else cp = u;
            if (cp < 0x80) out += (char)cp;
            else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
            else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        }
        return out;
    }
    static std::string utf8ToUtf16(const std::string& s, bool be)
    {
        std::string out; auto emit = [&](unsigned u){ unsigned char hi = (u >> 8) & 0xFF, lo = u & 0xFF; if (be) { out += (char)hi; out += (char)lo; } else { out += (char)lo; out += (char)hi; } };
        for (size_t i = 0; i < s.size(); )
        {
            unsigned char c = (unsigned char)s[i]; unsigned cp; int n;
            if (c < 0x80) { cp = c; n = 1; } else if ((c >> 5) == 0x6) { cp = c & 0x1F; n = 2; } else if ((c >> 4) == 0xE) { cp = c & 0x0F; n = 3; } else if ((c >> 3) == 0x1E) { cp = c & 0x07; n = 4; } else { cp = 0xFFFD; n = 1; }
            for (int k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
            i += n;
            if (cp <= 0xFFFF) emit(cp); else { cp -= 0x10000; emit(0xD800 | (cp >> 10)); emit(0xDC00 | (cp & 0x3FF)); }
        }
        return out;
    }
    std::string decodeToUtf8(const std::string& raw, int& enc)
    {
        if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) { enc = ENC_UTF8_BOM; return raw.substr(3); }
        if (raw.size() >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE) { enc = ENC_UTF16_LE; return utf16ToUtf8(raw.substr(2), false); }
        if (raw.size() >= 2 && (unsigned char)raw[0] == 0xFE && (unsigned char)raw[1] == 0xFF) { enc = ENC_UTF16_BE; return utf16ToUtf8(raw.substr(2), true); }
        bool hi = false; for (unsigned char c : raw) if (c >= 0x80) { hi = true; break; }
        if (hi && isValidUtf8(raw)) { enc = ENC_UTF8; return raw; }                 // UTF-8 without BOM
        if (!hi) { enc = ENC_ANSI; return raw; }                                    // pure ASCII -> "ANSI" like Notepad++ (bytes identical)
        enc = ENC_ANSI;                                                             // high bytes, not valid UTF-8 -> system code page
        return std::string(wxString(raw.data(), wxCSConv(wxFONTENCODING_SYSTEM), raw.size()).utf8_str());
    }
    std::string encodeFromUtf8(const std::string& u, int enc)
    {
        switch (enc)
        {
            case ENC_UTF8_BOM: return std::string("\xEF\xBB\xBF", 3) + u;
            case ENC_UTF16_LE: return std::string("\xFF\xFE", 2) + utf8ToUtf16(u, false);
            case ENC_UTF16_BE: return std::string("\xFE\xFF", 2) + utf8ToUtf16(u, true);
            case ENC_ANSI: { wxString s = wxString::FromUTF8(u.data(), u.size()); wxScopedCharBuffer b = s.mb_str(wxCSConv(wxFONTENCODING_SYSTEM)); if (!u.empty() && b.length() == 0) return u; return std::string(b.data(), b.length()); }
            default: return u;   // ENC_UTF8 (no BOM)
        }
    }
    static const char* encName(int enc)
    { switch (enc) { case ENC_UTF8_BOM: return "UTF-8-BOM"; case ENC_UTF16_LE: return "UTF-16 LE BOM"; case ENC_UTF16_BE: return "UTF-16 BE BOM"; case ENC_ANSI: return "ANSI"; default: return "UTF-8"; } }
    std::string readRawBytes(const wxString& path)
    {
        wxFile f(path); if (!f.IsOpened()) return {};
        const wxFileOffset len = f.Length(); if (len <= 0) return {};
        std::string buf(static_cast<size_t>(len), '\0'); f.Read(&buf[0], len); return buf;
    }
#ifdef __WXMSW__
    static std::string cpToUtf8(UINT cp, const std::string& bytes)   // code-page bytes -> UTF-8 (via UTF-16)
    {
        if (bytes.empty()) return {};
        const int wn = MultiByteToWideChar(cp, 0, bytes.data(), (int)bytes.size(), nullptr, 0); if (wn <= 0) return bytes;
        std::wstring w((size_t)wn, L'\0'); MultiByteToWideChar(cp, 0, bytes.data(), (int)bytes.size(), &w[0], wn);
        const int un = WideCharToMultiByte(CP_UTF8, 0, w.data(), wn, nullptr, 0, nullptr, nullptr); if (un <= 0) return bytes;
        std::string out((size_t)un, '\0'); WideCharToMultiByte(CP_UTF8, 0, w.data(), wn, &out[0], un, nullptr, nullptr); return out;
    }
    static std::string utf8ToCp(UINT cp, const std::string& u8)      // UTF-8 -> code-page bytes (via UTF-16)
    {
        if (u8.empty()) return {};
        const int wn = MultiByteToWideChar(CP_UTF8, 0, u8.data(), (int)u8.size(), nullptr, 0); if (wn <= 0) return u8;
        std::wstring w((size_t)wn, L'\0'); MultiByteToWideChar(CP_UTF8, 0, u8.data(), (int)u8.size(), &w[0], wn);
        const int bn = WideCharToMultiByte(cp, 0, w.data(), wn, nullptr, 0, nullptr, nullptr); if (bn <= 0) return u8;
        std::string out((size_t)bn, '\0'); WideCharToMultiByte(cp, 0, w.data(), wn, &out[0], bn, nullptr, nullptr); return out;
    }
#endif
    static int codepageForId(int id)   // Encoding > character-set menu item -> Windows code page (0 = not a charset item)
    {
        switch (id)
        {
            case IDM_FORMAT_WIN_1250: return 1250; case IDM_FORMAT_WIN_1251: return 1251; case IDM_FORMAT_WIN_1252: return 1252;
            case IDM_FORMAT_WIN_1253: return 1253; case IDM_FORMAT_WIN_1254: return 1254; case IDM_FORMAT_WIN_1255: return 1255;
            case IDM_FORMAT_WIN_1256: return 1256; case IDM_FORMAT_WIN_1257: return 1257; case IDM_FORMAT_WIN_1258: return 1258;
            case IDM_FORMAT_ISO_8859_1: return 28591; case IDM_FORMAT_ISO_8859_2: return 28592; case IDM_FORMAT_ISO_8859_3: return 28593;
            case IDM_FORMAT_ISO_8859_4: return 28594; case IDM_FORMAT_ISO_8859_5: return 28595; case IDM_FORMAT_ISO_8859_6: return 28596;
            case IDM_FORMAT_ISO_8859_7: return 28597; case IDM_FORMAT_ISO_8859_8: return 28598; case IDM_FORMAT_ISO_8859_9: return 28599;
            case IDM_FORMAT_ISO_8859_13: return 28603; case IDM_FORMAT_ISO_8859_14: return 28604; case IDM_FORMAT_ISO_8859_15: return 28605;
            case IDM_FORMAT_DOS_437: return 437; case IDM_FORMAT_DOS_720: return 720; case IDM_FORMAT_DOS_737: return 737;
            case IDM_FORMAT_DOS_775: return 775; case IDM_FORMAT_DOS_850: return 850; case IDM_FORMAT_DOS_852: return 852;
            case IDM_FORMAT_DOS_855: return 855; case IDM_FORMAT_DOS_857: return 857; case IDM_FORMAT_DOS_858: return 858;
            case IDM_FORMAT_DOS_860: return 860; case IDM_FORMAT_DOS_861: return 861; case IDM_FORMAT_DOS_862: return 862;
            case IDM_FORMAT_DOS_863: return 863; case IDM_FORMAT_DOS_865: return 865; case IDM_FORMAT_DOS_866: return 866;
            case IDM_FORMAT_DOS_869: return 869;
            case IDM_FORMAT_KOI8R_CYRILLIC: return 20866; case IDM_FORMAT_KOI8U_CYRILLIC: return 21866; case IDM_FORMAT_MAC_CYRILLIC: return 10007;
            case IDM_FORMAT_BIG5: return 950; case IDM_FORMAT_GB2312: return 936; case IDM_FORMAT_SHIFT_JIS: return 932;
            case IDM_FORMAT_EUC_KR: return 51949; case IDM_FORMAT_KOREAN_WIN: return 949; case IDM_FORMAT_TIS_620: return 874;
            default: return 0;
        }
    }
    void interpretCharset(int cp, const wxString& name)   // re-read the file decoded as that code page
    {
#ifdef __WXMSW__
        const wxString p = curPath();
        if (!p.empty())
        {
            if (sci(SCI_GETMODIFY) && wxMessageBox("Re-interpreting the encoding discards unsaved changes. Continue?", "Encoding", wxYES_NO | wxICON_QUESTION, this) != wxYES) return;
            setDocUtf8(cpToUtf8((UINT)cp, readRawBytes(p))); sci(SCI_EMPTYUNDOBUFFER); sci(SCI_SETSAVEPOINT);
        }
        if (auto* pg = activePage()) { pg->encoding = ENC_CHARSET; pg->codepage = cp; pg->encLabel = name; }
        updateStatus(); updateEncodingMenuChecks();
#else
        (void)cp; notImpl(name + " (Windows only)");
#endif
    }
    std::string encodeForPage(const std::string& u, EditorPage* p)   // bytes to write for this buffer's encoding
    {
#ifdef __WXMSW__
        if (p && p->encoding == ENC_CHARSET) return utf8ToCp((UINT)p->codepage, u);
#endif
        return encodeFromUtf8(u, p ? p->encoding : ENC_UTF8);
    }
    wxString encDisplay(EditorPage* p) { return (p && p->encoding == ENC_CHARSET) ? p->encLabel : wxString(encName(p ? p->encoding : ENC_UTF8)); }
    void updateEncodingMenuChecks()   // tick the active encoding in the Encoding menu's interpret-as group
    {
        auto* mb = menuBar(); if (!mb) return;
        const int enc = activePage() ? activePage()->encoding : ENC_UTF8;
        auto chk = [&](int id, bool on){ if (auto* it = mb->FindItem(id)) if (it->IsCheckable()) it->Check(on); };
        chk(IDM_FORMAT_ANSI, enc == ENC_ANSI); chk(IDM_FORMAT_AS_UTF_8, enc == ENC_UTF8); chk(IDM_FORMAT_UTF_8, enc == ENC_UTF8_BOM);
        chk(IDM_FORMAT_UTF_16LE, enc == ENC_UTF16_LE); chk(IDM_FORMAT_UTF_16BE, enc == ENC_UTF16_BE);
    }
    void convertTo(int enc)   // change the on-disk encoding; text is unchanged, written on next save
    {
        if (auto* p = activePage()) p->encoding = enc;
        setStatus(0, wxString("Encoding: ") + encName(enc) + " (save to apply)"); m_hint = true;
        updateStatus(); updateEncodingMenuChecks();
    }
    void interpretAs(int enc)   // re-read the file's bytes decoded as the chosen encoding (Notepad++ "interpret as")
    {
        const wxString p = curPath();
        if (p.empty()) { if (auto* pg = activePage()) pg->encoding = enc; updateStatus(); updateEncodingMenuChecks(); return; }
        if (sci(SCI_GETMODIFY) && wxMessageBox("Re-interpreting the encoding discards unsaved changes. Continue?", "Encoding", wxYES_NO | wxICON_QUESTION, this) != wxYES) return;
        const std::string raw = readRawBytes(p); std::string u;
        switch (enc)
        {
            case ENC_UTF8_BOM: u = (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF) ? raw.substr(3) : raw; break;
            case ENC_UTF16_LE: u = utf16ToUtf8(raw.size() >= 2 && (unsigned char)raw[0] == 0xFF ? raw.substr(2) : raw, false); break;
            case ENC_UTF16_BE: u = utf16ToUtf8(raw.size() >= 2 && (unsigned char)raw[0] == 0xFE ? raw.substr(2) : raw, true); break;
            case ENC_ANSI: u = std::string(wxString(raw.data(), wxCSConv(wxFONTENCODING_SYSTEM), raw.size()).utf8_str()); break;
            default: u = raw; break;   // ENC_UTF8
        }
        setDocUtf8(u); sci(SCI_EMPTYUNDOBUFFER); sci(SCI_SETSAVEPOINT);
        if (auto* pg = activePage()) pg->encoding = enc;
        updateStatus(); updateEncodingMenuChecks();
    }
    void loadFile(const wxString& path)
    {
        const std::string raw = readRawBytes(path);
        int enc = ENC_UTF8;
        const std::string u = decodeToUtf8(raw, enc);
        if (auto* p = activePage()) p->encoding = enc;
        sci(SCI_CLEARALL);
        sci(SCI_ADDTEXT, (int)u.length(), reinterpret_cast<sptr_t>(u.data()));
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
        updateEncodingMenuChecks();
    }
    void onOpen() { wxFileDialog d(this, "Open", "", "", "All files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST); if (d.ShowModal() == wxID_OK) addDocument(d.GetPath(), wxFileNameFromPath(d.GetPath())); }
    void onReload() { if (!m_path.empty()) loadFile(m_path); }
    bool writeFile(const wxString& path)
    {
        const int len = static_cast<int>(sci(SCI_GETLENGTH));
        std::string b(static_cast<size_t>(len) + 1, '\0');
        sci(SCI_GETTEXT, len + 1, reinterpret_cast<sptr_t>(&b[0])); b.resize(len);
        const std::string out = encodeForPage(b, activePage());
        wxFile f(path, wxFile::write);
        if (!f.IsOpened()) return false;
        f.Write(out.data(), out.size()); sci(SCI_SETSAVEPOINT);
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
    void sortLines(int mode, bool desc)   // 0=lexicographic 1=case-insensitive 2=integer 3=length 4=decimal-dot 5=decimal-comma
    {
        auto v = docLines();
        auto lower = [](std::string s){ for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };
        auto dec   = [](std::string s, bool comma){ if (comma) for (char& c : s) if (c == ',') c = '.'; return std::strtod(s.c_str(), nullptr); };
        std::stable_sort(v.begin(), v.end(), [&](const std::string& a, const std::string& b){
            if (mode == 5) return dec(a, true)  < dec(b, true);
            if (mode == 4) return dec(a, false) < dec(b, false);
            if (mode == 3) return a.size() < b.size();
            if (mode == 2) return std::strtoll(a.c_str(), nullptr, 10) < std::strtoll(b.c_str(), nullptr, 10);
            if (mode == 1) return lower(a) < lower(b);
            return a < b;
        });
        if (desc) std::reverse(v.begin(), v.end());
        putLines(v);
    }
    void shuffleLines() { auto v = docLines(); std::mt19937 g{ std::random_device{}() }; std::shuffle(v.begin(), v.end(), g); putLines(v); }
    void transformLines(const std::function<void(std::string&)>& fn) { auto v = docLines(); for (auto& l : v) fn(l); putLines(v); }
    void trimLeading() { transformLines([](std::string& s){ const size_t p = s.find_first_not_of(" \t"); s = (p == std::string::npos) ? std::string() : s.substr(p); }); }
    void trimBoth()    { transformLines([](std::string& s){ const size_t a = s.find_first_not_of(" \t"); const size_t b = s.find_last_not_of(" \t"); s = (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1); }); }
    void eolToSpace()  { std::string b = getDocUtf8(), out; for (char c : b) { if (c == '\r') continue; out += (c == '\n') ? ' ' : c; } setDocUtf8(out); }

    // ===================================================================================
    //  Additional Notepad++ menu commands (clipboard / files / shell / search / view / tools)
    // ===================================================================================
    bool m_onTop = false;                                  // "Always on Top" state
    wxString curPath() const { auto* p = activePage(); return p ? p->path : wxString(); }
    void copyToClip(const wxString& t) { if (!t.empty() && wxTheClipboard->Open()) { wxTheClipboard->SetData(new wxTextDataObject(t)); wxTheClipboard->Close(); } }
    wxString getClipText() { wxString t; if (wxTheClipboard->Open()) { if (wxTheClipboard->IsSupported(wxDF_UNICODETEXT)) { wxTextDataObject d; wxTheClipboard->GetData(d); t = d.GetText(); } wxTheClipboard->Close(); } return t; }
    wxString allOpenPaths(bool namesOnly)
    {
        wxString out;
        if (m_tabs) for (size_t i = 0; i < m_tabs->GetPageCount(); ++i)
        { auto* p = static_cast<EditorPage*>(m_tabs->GetPage(i)); wxString s = p->path.empty() ? p->title : p->path; out += (namesOnly ? wxFileNameFromPath(s) : s) + "\r\n"; }
        return out;
    }

    // ---- open the file's folder / a shell there / the file in another app, and new instances ----
    void revealInFolder()
    {
        const wxString p = curPath();
        if (p.empty()) { notImpl("Open Containing Folder (save the file first)"); return; }
#ifdef __WXMSW__
        const std::wstring arg = L"/select,\"" + p.ToStdWstring() + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
#else
        wxLaunchDefaultApplication(wxFileName(p).GetPath());
#endif
    }
    void openShellHere(bool powershell)
    {
        const wxString dir = curPath().empty() ? wxGetCwd() : wxFileName(curPath()).GetPath();
#ifdef __WXMSW__
        const std::wstring exe = powershell ? L"powershell.exe" : L"cmd.exe";
        ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, dir.ToStdWstring().c_str(), SW_SHOWNORMAL);
#else
        (void)powershell; wxLaunchDefaultApplication(dir);
#endif
    }
    void openInDefaultViewer() { const wxString p = curPath(); if (p.empty()) { notImpl("Open in Default Viewer (save the file first)"); return; } wxLaunchDefaultApplication(p); }
    void openFolder(const wxString& dir)
    {
#ifdef __WXMSW__
        ShellExecuteW(nullptr, L"open", dir.ToStdWstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
        wxLaunchDefaultApplication(dir);
#endif
    }
    void openInBrowser(const wxString& exe)   // exe = firefox/chrome/iexplore, or "" for Edge (Windows)
    {
        const wxString p = curPath();
        const wxString url = p.empty() ? wxString("about:blank") : wxFileName(p).GetFullPath();
#ifdef __WXMSW__
        const wxString cmd = exe.empty() ? ("cmd /c start microsoft-edge:\"" + url + "\"") : (exe + " \"" + url + "\"");
        if (wxExecute(cmd, wxEXEC_ASYNC) <= 0 && !exe.empty()) wxLaunchDefaultBrowser(url);
#else
        (void)exe; wxLaunchDefaultBrowser(url);
#endif
    }
    void newInstance(bool withFile)
    {
        wxString cmd = "\"" + wxStandardPaths::Get().GetExecutablePath() + "\"";
        if (withFile && !curPath().empty()) cmd += " \"" + curPath() + "\"";
        wxExecute(cmd, wxEXEC_ASYNC);
    }

    // ---- file operations ----
    void saveCopyAs()
    {
        wxFileDialog dlg(this, "Save a Copy As", wxFileName(curPath()).GetPath(), wxFileNameFromPath(curPath()), "All files (*.*)|*.*", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg.ShowModal() != wxID_OK) return;
        const std::string body = encodeForPage(getDocUtf8(), activePage());
        wxFile f(dlg.GetPath(), wxFile::write);
        if (f.IsOpened()) { f.Write(body.data(), body.size()); setStatus(0, "Copy saved: " + dlg.GetPath()); }
    }
    void renameFile()
    {
        const wxString p = curPath();
        if (p.empty()) { notImpl("Rename (save the file first)"); return; }
        wxFileDialog dlg(this, "Rename", wxFileName(p).GetPath(), wxFileNameFromPath(p), "All files (*.*)|*.*", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg.ShowModal() != wxID_OK) return;
        if (wxRenameFile(p, dlg.GetPath()))
        { if (auto* ep = activePage()) { ep->path = dlg.GetPath(); ep->title = wxFileNameFromPath(dlg.GetPath()); setLexerForFile(dlg.GetPath()); refreshTab(ep); SetTitle(ep->title + " - wxNotepad++"); } }
    }
    void recycleFile()
    {
        const wxString p = curPath();
        if (p.empty()) { notImpl("Move to Recycle Bin (save the file first)"); return; }
        if (wxMessageBox("Move \"" + wxFileNameFromPath(p) + "\" to the Recycle Bin?", "wxNotepad++", wxYES_NO | wxICON_QUESTION, this) != wxYES) return;
#ifdef __WXMSW__
        std::wstring from = p.ToStdWstring(); from.push_back(L'\0'); from.push_back(L'\0');   // double-NUL terminated list
        SHFILEOPSTRUCTW op{}; op.wFunc = FO_DELETE; op.pFrom = from.c_str(); op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
        if (SHFileOperationW(&op) == 0) closeActive();
#else
        if (wxRemoveFile(p)) closeActive();
#endif
    }
    void closeAllSide(bool toRight)
    {
        const int cur = m_tabs->GetSelection();
        for (int i = (int)m_tabs->GetPageCount() - 1; i >= 0; --i)
            if ((toRight && i > cur) || (!toRight && i < cur)) m_tabs->DeletePage(i);
    }
    void closeAllUnchanged()
    {
        auto* keep = activePage();
        for (int i = (int)m_tabs->GetPageCount() - 1; i >= 0; --i)
        { auto* p = static_cast<EditorPage*>(m_tabs->GetPage(i)); if (p != keep && !p->dirty) m_tabs->DeletePage(i); }
    }

    // ---- read-only ----
    void toggleReadOnly() { const bool ro = sci(SCI_GETREADONLY) != 0; sci(SCI_SETREADONLY, ro ? 0 : 1); setStatus(0, ro ? "Read/Write" : "Read-Only"); }
    void toggleSystemReadOnly()
    {
        const wxString p = curPath(); if (p.empty()) { notImpl("Read-Only Attribute (save the file first)"); return; }
#ifdef __WXMSW__
        const std::wstring w = p.ToStdWstring(); DWORD a = GetFileAttributesW(w.c_str());
        if (a == INVALID_FILE_ATTRIBUTES) return;
        a ^= FILE_ATTRIBUTE_READONLY; SetFileAttributesW(w.c_str(), a);
        const bool ro = (a & FILE_ATTRIBUTE_READONLY) != 0; sci(SCI_SETREADONLY, ro ? 1 : 0);
        setStatus(0, ro ? "File attribute: Read-Only" : "File attribute: Read/Write");
#else
        notImpl("Read-Only Attribute (Windows only)");
#endif
    }

    // ---- comments (C-like): single-line add/remove + stream /* */ ----
    void setLineComment(bool add)
    {
        const int l1 = (int)sci(SCI_LINEFROMPOSITION, sci(SCI_GETSELECTIONSTART));
        const int l2 = (int)sci(SCI_LINEFROMPOSITION, sci(SCI_GETSELECTIONEND));
        sci(SCI_BEGINUNDOACTION);
        for (int l = l1; l <= l2; ++l)
        {
            const int p = (int)sci(SCI_POSITIONFROMLINE, l), ll = (int)sci(SCI_LINELENGTH, l);
            std::string b(static_cast<size_t>(ll) + 1, '\0'); sci(SCI_GETLINE, l, reinterpret_cast<sptr_t>(&b[0])); b.resize(ll);
            const size_t nb = b.find_first_not_of(" \t\r\n"); if (nb == std::string::npos) continue;
            const bool commented = b.compare(nb, 2, "//") == 0;
            if (add && !commented) sci(SCI_INSERTTEXT, p + (int)nb, reinterpret_cast<sptr_t>("//"));
            else if (!add && commented) { sci(SCI_SETTARGETSTART, p + (int)nb); sci(SCI_SETTARGETEND, p + (int)nb + 2); sci(SCI_REPLACETARGET, 0, reinterpret_cast<sptr_t>("")); }
        }
        sci(SCI_ENDUNDOACTION);
    }
    void streamComment(bool add)
    {
        const int a = (int)sci(SCI_GETSELECTIONSTART), b = (int)sci(SCI_GETSELECTIONEND);
        sci(SCI_BEGINUNDOACTION);
        if (add) { sci(SCI_INSERTTEXT, b, reinterpret_cast<sptr_t>("*/")); sci(SCI_INSERTTEXT, a, reinterpret_cast<sptr_t>("/*")); sci(SCI_SETSEL, a, b + 4); }
        else
        {
            std::string s = getSelUtf8(); const size_t i = s.find_first_not_of(" \t\r\n"), j = s.find_last_not_of(" \t\r\n");
            if (i != std::string::npos && j != std::string::npos && j > i + 1 && s.compare(i, 2, "/*") == 0 && s.compare(j - 1, 2, "*/") == 0)
            { s.erase(j - 1, 2); s.erase(i, 2); sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>(s.c_str())); }
        }
        sci(SCI_ENDUNDOACTION);
    }
    void spacesToTabs(bool leadingOnly)
    {
        const int tw = (int)sci(SCI_GETTABWIDTH); if (tw <= 0) return;
        const std::string sp(static_cast<size_t>(tw), ' ');
        transformLines([&](std::string& s){
            if (leadingOnly)
            { size_t n = s.find_first_not_of(' '); if (n == std::string::npos) n = s.size(); std::string lead = s.substr(0, n), rest = s.substr(n); size_t pos; while ((pos = lead.find(sp)) != std::string::npos) lead.replace(pos, sp.size(), "\t"); s = lead + rest; }
            else { size_t pos; while ((pos = s.find(sp)) != std::string::npos) s.replace(pos, sp.size(), "\t"); }
        });
    }
    void removeEmptyLinesBlank() { auto v = docLines(); v.erase(std::remove_if(v.begin(), v.end(), [](const std::string& s){ return s.find_first_not_of(" \t") == std::string::npos; }), v.end()); putLines(v); }
    void redactSelection() { transformSel([](std::string& s){ std::string o; for (size_t i = 0; i < s.size();) { unsigned char c = s[i]; if (c == '\r' || c == '\n') { o += (char)c; ++i; } else { const int n = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0x80) ? 2 : 1; o += "\xE2\x96\x88"; i += n; } } s = o; }); }
    void searchOnInternet() { wxString q = selText().Trim().Trim(false); if (q.empty()) return; q.Replace(" ", "+"); wxLaunchDefaultBrowser("https://duckduckgo.com/?q=" + q); }
    void openSelectedFile() { const wxString f = selText().Trim().Trim(false); if (!f.empty() && wxFileExists(f)) openPath(f); else notImpl("Open File (selection is not an existing path)"); }

    // ---- search helpers: select word + find, volatile/next-found ----
    void findSel(bool fwd, bool selectWord)
    {
        if (selectWord && sci(SCI_GETSELECTIONEMPTY)) { const int p = (int)sci(SCI_GETCURRENTPOS); sci(SCI_SETSEL, sci(SCI_WORDSTARTPOSITION, p, 1), sci(SCI_WORDENDPOSITION, p, 1)); }
        const wxString term = selText();
        if (term.empty()) { findNext(fwd); return; }
        m_lastFind = term;
        FindOpts o; if (m_findDlg) o = m_findDlg->opts(); o.find = term; o.forward = fwd; doFindNext(o);
    }
    void selectBetweenBraces()
    {
        const int p = (int)sci(SCI_GETCURRENTPOS);
        sptr_t open = sci(SCI_BRACEMATCH, p, 0); if (open < 0 && p > 0) open = sci(SCI_BRACEMATCH, p - 1, 0);
        if (open >= 0) { const sptr_t close = sci(SCI_BRACEMATCH, open, 0); if (close > open) sci(SCI_SETSEL, open + 1, close); }
    }

    // ---- bookmarked-line operations ----
    void bookmarkLinesOp(int op)   // 0=copy 1=cut 2=remove-marked 3=remove-unmarked 4=paste-replace 5=inverse
    {
        const int n = (int)sci(SCI_GETLINECOUNT);
        if (op == 5) { sci(SCI_BEGINUNDOACTION); for (int l = 0; l < n; ++l) { if (sci(SCI_MARKERGET, l) & (1 << MARK_BOOKMARK)) sci(SCI_MARKERDELETE, l, MARK_BOOKMARK); else sci(SCI_MARKERADD, l, MARK_BOOKMARK); } sci(SCI_ENDUNDOACTION); return; }
        std::vector<bool> mark(n, false); for (int l = 0; l < n; ++l) mark[l] = (sci(SCI_MARKERGET, l) & (1 << MARK_BOOKMARK)) != 0;
        auto lines = docLines();
        if (op == 0 || op == 1) { wxString out; for (int l = 0; l < (int)lines.size() && l < n; ++l) if (mark[l]) out += wxString::FromUTF8(lines[l].data(), lines[l].size()) + "\r\n"; copyToClip(out); }
        if (op == 1 || op == 2) { std::vector<std::string> keep; for (int l = 0; l < (int)lines.size(); ++l) if (l >= n || !mark[l]) keep.push_back(lines[l]); putLines(keep); sci(SCI_MARKERDELETEALL, MARK_BOOKMARK); }
        else if (op == 3)       { std::vector<std::string> keep; for (int l = 0; l < (int)lines.size(); ++l) if (l < n && mark[l]) keep.push_back(lines[l]); putLines(keep); sci(SCI_MARKERDELETEALL, MARK_BOOKMARK); }
        else if (op == 4)
        {
            wxString clip = getClipText(); std::vector<std::string> repl; wxString cur;
            for (wxUniChar ch : clip) { if (ch == '\n') { repl.push_back(std::string(cur.utf8_str())); cur.clear(); } else if (ch != '\r') cur += ch; }
            if (!cur.empty()) repl.push_back(std::string(cur.utf8_str()));
            size_t ri = 0; std::vector<std::string> outv;
            for (int l = 0; l < (int)lines.size(); ++l) { if (l < n && mark[l]) outv.push_back(ri < repl.size() ? repl[ri++] : std::string()); else outv.push_back(lines[l]); }
            putLines(outv);
        }
    }

    // ---- view ----
    void toggleAlwaysOnTop()
    {
#ifdef __WXMSW__
        m_onTop = !m_onTop;
        SetWindowPos(static_cast<HWND>(GetHandle()), m_onTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        setStatus(0, m_onTop ? "Always on Top: ON" : "Always on Top: OFF");
#else
        notImpl("Always on Top (Windows only)");
#endif
    }
    void toggleWrapSymbol() { sci(SCI_SETWRAPVISUALFLAGS, sci(SCI_GETWRAPVISUALFLAGS) ? SC_WRAPVISUALFLAG_NONE : SC_WRAPVISUALFLAG_END); }
    void hideSelectedLines() { const int a = (int)sci(SCI_LINEFROMPOSITION, sci(SCI_GETSELECTIONSTART)), b = (int)sci(SCI_LINEFROMPOSITION, sci(SCI_GETSELECTIONEND)); sci(SCI_HIDELINES, a, b); }
    // A dark-theme-aware replacement for wxMessageBox(... wxOK ...): the native message box ignores the
    // app theme (white body in dark mode), so info popups must be a themed wxDialog instead.
    void themedInfo(const wxString& msg, const wxString& title)
    {
        wxDialog dlg(this, wxID_ANY, title);
        auto* s = new wxBoxSizer(wxVERTICAL);
        s->Add(new wxStaticText(&dlg, wxID_ANY, msg), 0, wxALL, 16);
        s->Add(dlg.CreateButtonSizer(wxOK), 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 12);
        dlg.SetSizerAndFit(s); dlg.Centre();
        themeDialog(&dlg);
        dlg.ShowModal();
    }
    void showSummary()
    {
        std::string t = getDocUtf8();
        long cp = 0, words = 0; bool inw = false;
        for (unsigned char c : t) { if ((c & 0xC0) != 0x80) ++cp; if (std::isspace(c)) inw = false; else if (!inw) { inw = true; ++words; } }
        const wxString name = curPath().empty() ? (activePage() ? activePage()->title : wxString("(none)")) : curPath();
        themedInfo(wxString::Format("File: %s\n\nCharacters: %ld\nWords: %ld\nLines: %d",
            name, cp, words, (int)sci(SCI_GETLINECOUNT)), "Summary");
    }
    void showWindowsList()
    {
        if (!m_tabs || !m_tabs->GetPageCount()) return;
        wxArrayString items; for (size_t i = 0; i < m_tabs->GetPageCount(); ++i) { auto* p = static_cast<EditorPage*>(m_tabs->GetPage(i)); items.Add(p->path.empty() ? p->title : p->path); }
        const int sel = wxGetSingleChoiceIndex("Activate document:", "Windows", items, m_tabs->GetSelection(), this);
        if (sel >= 0) m_tabs->SetSelection(sel);
    }

    // ---- Tools: MD5 / SHA digests ----
    void hashSelection(const wchar_t* algo, const char* name, bool toClip)
    {
#ifdef __WXMSW__
        std::string data = getSelUtf8(); if (data.empty()) data = getDocUtf8();
        wxString hex; BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE h = nullptr; DWORD len = 0, res = 0;
        if (BCryptOpenAlgorithmProvider(&alg, algo, nullptr, 0) == 0)
        {
            BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&len), sizeof(len), &res, 0);
            if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0)
            {
                BCryptHashData(h, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())), (ULONG)data.size(), 0);
                std::vector<unsigned char> dig(len); BCryptFinishHash(h, dig.data(), len, 0);
                for (unsigned char c : dig) hex += wxString::Format("%02x", c);
                BCryptDestroyHash(h);
            }
            BCryptCloseAlgorithmProvider(alg, 0);
        }
        if (toClip) { copyToClip(hex); setStatus(0, wxString(name) + " copied to clipboard"); }
        else themedInfo(hex, wxString(name) + " digest");
#else
        (void)algo; (void)toClip; notImpl(wxString(name) + " (Windows only)");
#endif
    }
    // Manually force a language on the active buffer (Language menu). ext "" forces Normal Text. The
    // choice sticks across tab switches via EditorPage::langForced (setLexerForFile honours it over the
    // file extension).
    void setForcedLang(const wxString& lexer, const wxString& name)
    {
        auto* p = activePage(); if (!p) return;
        p->langForced = true; p->forcedLexer = lexer; p->forcedName = name;
        setLexerForFile(p->path);
        updateStatus();
        if (m_stc) m_stc->Refresh();
    }

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
    void syncToggle(int id, bool& flag) { flag = !flag; if (menuBar()) menuBar()->Check(id, flag); if (toolBar()) toolBar()->ToggleTool(id, flag); }
    void toggleWrap()  { syncToggle(IDM_VIEW_WRAP, m_wrap); sci(SCI_SETWRAPMODE, m_wrap ? SC_WRAP_WORD : SC_WRAP_NONE); }
    void toggleWs()    { syncToggle(IDM_VIEW_ALL_CHARACTERS, m_ws); sci(SCI_SETVIEWWS, m_ws ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE); sci(SCI_SETVIEWEOL, m_ws ? 1 : 0); }
    void toggleGuides(){ syncToggle(IDM_VIEW_INDENT_GUIDE, m_guides); sci(SCI_SETINDENTATIONGUIDES, m_guides ? SC_IV_LOOKBOTH : SC_IV_NONE); }

    // ----- persisted preferences (Settings > Preferences) ---------------
    void loadSettings()
    {
        auto* c = wxConfigBase::Get();
        c->Read("IntegratedBar", &m_integratedBar, false);   // integrated top bar on/off (also read in OnInit; here for the Preferences checkbox)
        long tw = 4; c->Read("Editing/TabWidth", &tw, 4L); m_tabWidth = (int)tw;
        c->Read("Editing/UseTabs", &m_useTabs, true);
        c->Read("Editing/LineNumbers", &m_lineNumbers, true);
        c->Read("Editing/Wrap", &m_wrap, false);
        c->Read("Editing/Whitespace", &m_ws, false);
        c->Read("Editing/IndentGuides", &m_guides, false);
        c->Read("Editing/WrapSymbol", &m_wrapSymbol, false);
        c->Read("View/Toolbar", &m_showToolbar, true);
        c->Read("View/StatusBar", &m_showStatusbar, true);
        c->Read("Editing/AutoComplete", &m_autocomplete, true);
        c->Read("Editing/CaretLine", &m_caretLine, true);
        c->Read("Editing/AutoIndent", &m_autoindent, true);
        long cw = 1; c->Read("Editing/CaretWidth", &cw, 1L); m_caretWidth = (int)cw;
        long ec = 0; c->Read("Editing/EdgeColumn", &ec, 0L); m_edgeColumn = (int)ec;
        c->Read("Theme", &m_themeName, wxEmptyString);
    }
    void saveSettings()
    {
        auto* c = wxConfigBase::Get();
        c->Write("Editing/TabWidth", (long)m_tabWidth);   c->Write("Editing/UseTabs", m_useTabs);
        c->Write("Editing/LineNumbers", m_lineNumbers);   c->Write("Editing/Wrap", m_wrap);
        c->Write("Editing/Whitespace", m_ws);             c->Write("Editing/IndentGuides", m_guides);
        c->Write("Editing/WrapSymbol", m_wrapSymbol);     c->Write("View/Toolbar", m_showToolbar);
        c->Write("View/StatusBar", m_showStatusbar);      c->Write("Editing/AutoComplete", m_autocomplete);
        c->Write("Editing/CaretLine", m_caretLine);       c->Write("Editing/AutoIndent", m_autoindent);
        c->Write("Editing/CaretWidth", (long)m_caretWidth); c->Write("Editing/EdgeColumn", (long)m_edgeColumn);
        c->Write("Theme", m_themeName);
        c->Flush();
    }
    void applySettings()   // push the current preferences onto the editor view + chrome
    {
        if (m_stc)
        {
            sci(SCI_SETTABWIDTH, m_tabWidth);
            sci(SCI_SETUSETABS, m_useTabs ? 1 : 0);
            sci(SCI_SETWRAPMODE, m_wrap ? SC_WRAP_WORD : SC_WRAP_NONE);
            sci(SCI_SETVIEWWS, m_ws ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
            sci(SCI_SETVIEWEOL, m_ws ? 1 : 0);
            sci(SCI_SETINDENTATIONGUIDES, m_guides ? SC_IV_LOOKBOTH : SC_IV_NONE);
            sci(SCI_SETWRAPVISUALFLAGS, m_wrapSymbol ? SC_WRAPVISUALFLAG_END : SC_WRAPVISUALFLAG_NONE);
            sci(SCI_SETCARETLINEVISIBLE, m_caretLine ? 1 : 0);
            sci(SCI_SETCARETWIDTH, m_caretWidth);
            sci(SCI_SETEDGEMODE, m_edgeColumn > 0 ? EDGE_LINE : EDGE_NONE);
            sci(SCI_SETEDGECOLUMN, m_edgeColumn);
            if (m_lineNumbers) updateLineMargin(); else sci(SCI_SETMARGINWIDTHN, 0, 0);
        }
        if (auto* mb = menuBar()) { mb->Check(IDM_VIEW_WRAP, m_wrap); mb->Check(IDM_VIEW_ALL_CHARACTERS, m_ws); mb->Check(IDM_VIEW_INDENT_GUIDE, m_guides); }
        if (auto* tb = toolBar()) tb->ToggleTool(IDM_VIEW_WRAP, m_wrap);
        showToolBar(m_showToolbar);   // aui-aware: hides the pane in integrated mode, the frame toolbar in native
        if (auto* sb = GetStatusBar()) sb->Show(m_showStatusbar);
        SendSizeEvent();
    }
    void onPreferences()
    {
        // Page layout mirrors Notepad++'s Preferences (General / Editing / Indentation /
        // Auto-Completion / Dark Mode); the labels are Notepad++'s exact wording.
        wxDialog dlg(this, wxID_ANY, "Preferences", wxDefaultPosition, wxSize(560, 440));
        auto* book = new wxListbook(&dlg, wxID_ANY);
        auto pg  = [&](const wxString& name, bool sel = false) { auto* p = new wxPanel(book); book->AddPage(p, name, sel); return p; };
        auto row = [&](wxBoxSizer* s, wxWindow* w) { s->Add(w, 0, wxLEFT | wxRIGHT | wxTOP, 10); };

        // ---- General --------------------------------------------------------------------------
        auto* gen = pg("General", true); auto* gs = new wxBoxSizer(wxVERTICAL);
        auto* cbToolbar = new wxCheckBox(gen, wxID_ANY, "Show toolbar");    cbToolbar->SetValue(m_showToolbar);
        auto* cbStatus  = new wxCheckBox(gen, wxID_ANY, "Show status bar"); cbStatus->SetValue(m_showStatusbar);
        row(gs, cbToolbar); row(gs, cbStatus);
#ifdef WXNPP_HAS_BORDERLESS
        auto* cbIntBar = new wxCheckBox(gen, wxID_ANY, "Show integrated top bar - VS-style icon + menus + window controls (applied on restart)");
        cbIntBar->SetValue(m_integratedBar); row(gs, cbIntBar);
#endif
        gen->SetSizer(gs);

        // ---- Editing --------------------------------------------------------------------------
        auto* ed = pg("Editing"); auto* es = new wxBoxSizer(wxVERTICAL);
        auto* cbLineNum = new wxCheckBox(ed, wxID_ANY, "Display line number");      cbLineNum->SetValue(m_lineNumbers);
        auto* cbGuides  = new wxCheckBox(ed, wxID_ANY, "Show indentation guide");   cbGuides->SetValue(m_guides);
        auto* cbWs      = new wxCheckBox(ed, wxID_ANY, "Show white space and TAB"); cbWs->SetValue(m_ws);
        auto* cbWrapSym = new wxCheckBox(ed, wxID_ANY, "Show wrap symbol");         cbWrapSym->SetValue(m_wrapSymbol);
        auto* cbWrap    = new wxCheckBox(ed, wxID_ANY, "Word wrap");                cbWrap->SetValue(m_wrap);
        auto* cbCaretLn = new wxCheckBox(ed, wxID_ANY, "Highlight current line");   cbCaretLn->SetValue(m_caretLine);
        for (auto* c : { cbLineNum, cbGuides, cbWs, cbWrapSym, cbWrap, cbCaretLn }) row(es, c);
        auto* erow = new wxBoxSizer(wxHORIZONTAL);
        erow->Add(new wxStaticText(ed, wxID_ANY, "Caret width:"), 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 8);
        auto* spCaret = new wxSpinCtrl(ed, wxID_ANY, "", wxDefaultPosition, wxSize(60, -1), wxSP_ARROW_KEYS, 1, 3, m_caretWidth);
        erow->Add(spCaret, 0, wxRIGHT, 24);
        erow->Add(new wxStaticText(ed, wxID_ANY, "Vertical edge at column (0 = off):"), 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 8);
        auto* spEdge = new wxSpinCtrl(ed, wxID_ANY, "", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 300, m_edgeColumn);
        erow->Add(spEdge, 0);
        es->Add(erow, 0, wxALL, 10); ed->SetSizer(es);

        // ---- Indentation ----------------------------------------------------------------------
        auto* ind = pg("Indentation"); auto* is = new wxBoxSizer(wxVERTICAL);
        auto* trow = new wxBoxSizer(wxHORIZONTAL);
        trow->Add(new wxStaticText(ind, wxID_ANY, "Tab size:"), 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 8);
        auto* spTab = new wxSpinCtrl(ind, wxID_ANY, "", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 1, 16, m_tabWidth);
        trow->Add(spTab, 0); is->Add(trow, 0, wxALL, 10);
        auto* cbSpace  = new wxCheckBox(ind, wxID_ANY, "Replace by space");      cbSpace->SetValue(!m_useTabs);
        auto* cbIndent = new wxCheckBox(ind, wxID_ANY, "Auto-indent new lines"); cbIndent->SetValue(m_autoindent);
        row(is, cbSpace); row(is, cbIndent); ind->SetSizer(is);

        // ---- Auto-Completion ------------------------------------------------------------------
        auto* ac = pg("Auto-Completion"); auto* as = new wxBoxSizer(wxVERTICAL);
        auto* cbAuto = new wxCheckBox(ac, wxID_ANY, "Enable auto-completion"); cbAuto->SetValue(m_autocomplete);
        row(as, cbAuto); ac->SetSizer(as);

        // ---- Dark Mode ------------------------------------------------------------------------
        auto* dm = pg("Dark Mode"); auto* ds = new wxBoxSizer(wxVERTICAL);
        auto* cbDark = new wxCheckBox(dm, wxID_ANY, "Enable Dark Mode   (applied on restart)"); cbDark->SetValue(m_dark);
        row(ds, cbDark); dm->SetSizer(ds);

        auto* btn = new wxBoxSizer(wxHORIZONTAL); btn->AddStretchSpacer(); btn->Add(new wxButton(&dlg, wxID_OK, "Close"), 0);
        auto* top = new wxBoxSizer(wxVERTICAL); top->Add(book, 1, wxEXPAND | wxALL, 8); top->Add(btn, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
        dlg.SetSizer(top); themeDialog(&dlg);
        dlg.ShowModal();   // Notepad++ Preferences has no Cancel - changes apply on close
        const bool newDark = cbDark->GetValue();
        m_showToolbar = cbToolbar->GetValue(); m_showStatusbar = cbStatus->GetValue();
        m_tabWidth = spTab->GetValue(); m_useTabs = !cbSpace->GetValue(); m_lineNumbers = cbLineNum->GetValue();
        m_guides = cbGuides->GetValue(); m_ws = cbWs->GetValue(); m_wrapSymbol = cbWrapSym->GetValue(); m_wrap = cbWrap->GetValue(); m_autocomplete = cbAuto->GetValue();
        m_caretLine = cbCaretLn->GetValue(); m_autoindent = cbIndent->GetValue(); m_caretWidth = spCaret->GetValue(); m_edgeColumn = spEdge->GetValue();
        applySettings(); saveSettings();
        bool needRestart = (newDark != m_dark);
#ifdef WXNPP_HAS_BORDERLESS
        if (cbIntBar->GetValue() != m_integratedBar)   // the chrome base is fixed per process - relaunch to switch
        { wxConfigBase::Get()->Write("IntegratedBar", cbIntBar->GetValue()); needRestart = true; }
#endif
        if (needRestart) restartWithTheme(newDark);   // a chrome change (dark mode / integrated bar) needs a relaunch
    }

    // ----- macros (record / playback / run multiple / save) -------------
    static bool macroMsgHasText(int m) { return m == SCI_REPLACESEL || m == SCI_ADDTEXT || m == SCI_INSERTTEXT || m == SCI_APPENDTEXT; }
    void onMacroRecord(wxStyledTextEvent& e)   // Scintilla emits one of these per recordable command while recording
    {
        if (!m_recording) return;
        const int msg = (int)e.GetMessage(); const uptr_t wp = (uptr_t)e.GetWParam(); const sptr_t lp = (sptr_t)e.GetLParam();
        // Merge char-by-char typing into one step (NotepadNext does this): consecutive ReplaceSel concatenate,
        // and a Backspace right after typing trims the last character instead of adding a separate step.
        if (msg == SCI_REPLACESEL && lp && !m_macro.empty() && m_macro.back().msg == SCI_REPLACESEL)
        { m_macro.back().text += reinterpret_cast<const char*>(lp); return; }
        if (msg == SCI_DELETEBACK && !m_macro.empty() && m_macro.back().msg == SCI_REPLACESEL && !m_macro.back().text.empty())
        {
            std::string& t = m_macro.back().text;
            size_t n = 1; while (n < t.size() && ((unsigned char)t[t.size() - n] & 0xC0) == 0x80) ++n;   // drop a whole UTF-8 character
            t.erase(t.size() - n); if (t.empty()) m_macro.pop_back(); return;
        }
        MacroStep s; s.msg = msg; s.wparam = wp; s.lparam = lp;
        if (macroMsgHasText(msg) && lp)   // copy the string payload now (the pointer is transient)
        { const char* t = reinterpret_cast<const char*>(lp); s.text = (msg == SCI_ADDTEXT) ? std::string(t, (size_t)wp) : std::string(t); s.hasText = true; }
        m_macro.push_back(s);
    }
    void macroToolStates()
    {
        const bool has = !m_macro.empty();
        auto* tb = toolBar(); auto* mb = menuBar();
        auto en = [&](int id, bool on){ if (tb) tb->EnableTool(id, on); if (mb) if (auto* it = mb->FindItem(id)) it->Enable(on); };
        en(IDM_MACRO_STARTRECORDINGMACRO, !m_recording);
        en(IDM_MACRO_STOPRECORDINGMACRO, m_recording);
        en(IDM_MACRO_PLAYBACKRECORDEDMACRO, !m_recording && has);
        en(IDM_MACRO_RUNMULTIMACRODLG, !m_recording && has);
        en(IDM_MACRO_SAVECURRENTMACRO, !m_recording && has);
    }
    void startMacroRecord() { if (m_recording) return; m_macro.clear(); m_recording = true; sci(SCI_STARTRECORD); macroToolStates(); setStatus(0, "Recording macro..."); m_hint = true; }
    void stopMacroRecord()  { if (!m_recording) return; sci(SCI_STOPRECORD); m_recording = false; macroToolStates(); setStatus(0, wxString::Format("Macro recorded - %d step(s)", (int)m_macro.size())); m_hint = true; }
    void playSteps(const std::vector<MacroStep>& m) { for (const auto& s : m) sci((UINT)s.msg, s.wparam, s.hasText ? reinterpret_cast<sptr_t>(s.text.c_str()) : s.lparam); }
    void playMacro(const std::vector<MacroStep>& m) { if (m.empty()) return; sci(SCI_BEGINUNDOACTION); playSteps(m); sci(SCI_ENDUNDOACTION); }
    void runMultiple()
    {
        if (m_macro.empty()) return;
        wxDialog dlg(this, wxID_ANY, "Run a Macro Multiple Times");
        auto* rbN   = new wxRadioButton(&dlg, wxID_ANY, "Run", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
        auto* sp    = new wxSpinCtrl(&dlg, wxID_ANY, "", wxDefaultPosition, wxSize(90, -1), wxSP_ARROW_KEYS, 1, 99999, 1);
        auto* rbEof = new wxRadioButton(&dlg, wxID_ANY, "Run until the end of file");
        auto* r1 = new wxBoxSizer(wxHORIZONTAL);
        r1->Add(rbN, 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 6); r1->Add(sp, 0, wxRIGHT, 6);
        r1->Add(new wxStaticText(&dlg, wxID_ANY, "times"), 0, wxALIGN_CENTRE_VERTICAL);
        auto* btn = new wxBoxSizer(wxHORIZONTAL); btn->AddStretchSpacer();
        auto* ok = new wxButton(&dlg, wxID_OK, "Run"); ok->SetDefault();
        btn->Add(ok, 0, wxRIGHT, 6); btn->Add(new wxButton(&dlg, wxID_CANCEL, "Cancel"), 0);
        auto* top = new wxBoxSizer(wxVERTICAL);
        top->Add(r1, 0, wxALL, 12); top->Add(rbEof, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12); top->Add(btn, 0, wxEXPAND | wxALL, 12);
        dlg.SetSizerAndFit(top); themeDialog(&dlg);
        if (dlg.ShowModal() != wxID_OK) return;
        sci(SCI_BEGINUNDOACTION);
        if (rbEof->GetValue())   // replay until the caret stops advancing or reaches the end
        { int guard = 0; while (guard++ < 100000) { const int before = (int)sci(SCI_GETCURRENTPOS); playSteps(m_macro); const int after = (int)sci(SCI_GETCURRENTPOS); if (after == before || after >= (int)sci(SCI_GETLENGTH)) break; } }
        else { const int n = sp->GetValue(); for (int i = 0; i < n; ++i) playSteps(m_macro); }
        sci(SCI_ENDUNDOACTION);
    }
    void appendMacroMenuItems()   // (re)list the saved macros at the bottom of the Macro menu
    {
        auto* mb = menuBar(); if (!mb) return;
        const int mi = mb->FindMenu("Macro"); if (mi == wxNOT_FOUND) return;
        wxMenu* menu = mb->GetMenu(mi);
        for (int id = myID_MACRO_ITEM; id < myID_MACRO_ITEM + 200; ++id) if (auto* it = menu->FindItem(id)) menu->Destroy(it);
        if (!m_savedMacros.empty() && !m_macroSepAdded) { menu->AppendSeparator(); m_macroSepAdded = true; }
        for (size_t i = 0; i < m_savedMacros.size(); ++i) menu->Append(myID_MACRO_ITEM + (int)i, m_savedMacros[i].first);
    }
    void saveMacro()
    {
        if (m_macro.empty()) return;
        const wxString name = wxGetTextFromUser("Macro name:", "Save Current Recorded Macro", "", this);
        if (name.empty()) return;
        for (auto& kv : m_savedMacros) if (kv.first == name) { kv.second = m_macro; setStatus(0, "Macro updated: " + name); m_hint = true; return; }
        m_savedMacros.emplace_back(name, m_macro); appendMacroMenuItems();
        setStatus(0, "Macro saved: " + name); m_hint = true;
    }

    // ----- dark / light theme -------------------------------------------
    // Parse Notepad++'s real theme XML (dark = themes/DarkModeDefault.xml, light = stylers.model.xml)
    // deployed next to the exe, so the editor uses Notepad++'s exact colours.
    wxString themeFilePath(const wxString& name)   // resolve a theme name to its XML on disk
    {
        const wxString dir = wxPathOnly(wxStandardPaths::Get().GetExecutablePath());
        if (name.empty() || name == "Default") return dir + "\\stylers.model.xml";
        return dir + "\\themes\\" + name + ".xml";
    }
    wxArrayString availableThemes()   // "Default" + every themes/*.xml (the 22 Notepad++ themes)
    {
        wxArrayString out; out.Add("Default");
        wxDir d(wxPathOnly(wxStandardPaths::Get().GetExecutablePath()) + "\\themes");
        if (d.IsOpened()) { wxString f; bool more = d.GetFirst(&f, "*.xml", wxDIR_FILES); while (more) { out.Add(f.BeforeLast('.')); more = d.GetNext(&f); } }
        return out;
    }
    void loadTheme()   // load the active theme: an explicit Style-Configurator choice, else the dark/light default
    {
        loadThemeFile(themeFilePath(!m_themeName.empty() ? m_themeName : wxString(m_dark ? "DarkModeDefault" : "Default")));
    }
    void loadThemeFile(const wxString& path)
    {
        m_theme = NppTheme{};   // reset so switching themes fully replaces the previous palette
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
                                           npp_bgr(w->GetAttribute("bgColor")), (int)fs, w->GetAttribute("name") });
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
    void applyThemeSelection(const wxString& name)   // switch the editor theme live (Style Configurator)
    {
        m_themeName = (name == "Default") ? wxString() : name;
        loadTheme();
        applyEditorTheme(m_dark);
        if (auto* p = activePage()) setLexerForFile(p->path);   // re-apply per-token colours for the active doc
        if (m_stc) m_stc->Refresh();
    }
    // ---- Style Configurator colour helpers + write-back to the theme XML ----
    static wxColour bgrToColour(int bgr) { return bgr < 0 ? wxColour(*wxBLACK) : wxColour(bgr & 0xFF, (bgr >> 8) & 0xFF, (bgr >> 16) & 0xFF); }
    static int colourToBgr(const wxColour& c) { return (c.Blue() << 16) | (c.Green() << 8) | c.Red(); }
    static wxString bgrToHex(int bgr) { return wxString::Format("%02X%02X%02X", bgr & 0xFF, (bgr >> 8) & 0xFF, (bgr >> 16) & 0xFF); }
    static void setAttr(wxXmlNode* n, const wxString& a, const wxString& v) { n->DeleteAttribute(a); n->AddAttribute(a, v); }
    void saveThemeToXml(const wxString& path)   // persist the edited m_theme back into its theme XML
    {
        wxXmlDocument doc;
        if (!wxFileExists(path) || !doc.Load(path) || !doc.GetRoot()) return;
        for (wxXmlNode* sec = doc.GetRoot()->GetChildren(); sec; sec = sec->GetNext())
        {
            if (sec->GetName() == "LexerStyles")
                for (wxXmlNode* lt = sec->GetChildren(); lt; lt = lt->GetNext())
                {
                    if (lt->GetName() != "LexerType") continue;
                    auto it = m_theme.lexers.find(lt->GetAttribute("name")); if (it == m_theme.lexers.end()) continue;
                    for (wxXmlNode* w = lt->GetChildren(); w; w = w->GetNext())
                    {
                        if (w->GetName() != "WordsStyle") continue;
                        long id = -1; w->GetAttribute("styleID", "-1").ToLong(&id);
                        for (const StyleDef& s : it->second) if (s.id == (int)id)
                        { if (s.fg >= 0) setAttr(w, "fgColor", bgrToHex(s.fg)); if (s.bg >= 0) setAttr(w, "bgColor", bgrToHex(s.bg)); setAttr(w, "fontStyle", wxString::Format("%d", s.fontStyle)); break; }
                    }
                }
            else if (sec->GetName() == "GlobalStyles")
                for (wxXmlNode* w = sec->GetChildren(); w; w = w->GetNext())
                {
                    if (w->GetName() != "WidgetStyle") continue;
                    auto it = m_theme.global.find(w->GetAttribute("name")); if (it == m_theme.global.end()) continue;
                    if (it->second.first  >= 0) setAttr(w, "fgColor", bgrToHex(it->second.first));
                    if (it->second.second >= 0) setAttr(w, "bgColor", bgrToHex(it->second.second));
                }
        }
        doc.Save(path);
    }
    void onStyleConfig()   // Settings > Style Configurator: theme picker + per-language token style editor (like N++)
    {
        const wxString original = m_themeName;
        wxDialog dlg(this, wxID_ANY, "Style Configurator", wxDefaultPosition, wxSize(680, 440));
        auto* themeRow = new wxBoxSizer(wxHORIZONTAL);
        themeRow->Add(new wxStaticText(&dlg, wxID_ANY, "Select theme:"), 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 8);
        auto* themeCombo = new wxChoice(&dlg, wxID_ANY, wxDefaultPosition, wxSize(220, -1), availableThemes());
        themeCombo->SetStringSelection(m_themeName.empty() ? "Default" : m_themeName);
        themeRow->Add(themeCombo, 0);
        auto* langList  = new wxListBox(&dlg, wxID_ANY, wxDefaultPosition, wxSize(180, 280));
        auto* styleList = new wxListBox(&dlg, wxID_ANY, wxDefaultPosition, wxSize(180, 280));
        auto* fgPick   = new wxColourPickerCtrl(&dlg, wxID_ANY);
        auto* bgPick   = new wxColourPickerCtrl(&dlg, wxID_ANY);
        auto* cbBold   = new wxCheckBox(&dlg, wxID_ANY, "Bold");
        auto* cbItalic = new wxCheckBox(&dlg, wxID_ANY, "Italic");
        auto* eg = new wxFlexGridSizer(2, 8, 10);
        eg->Add(new wxStaticText(&dlg, wxID_ANY, "Foreground colour:"), 0, wxALIGN_CENTRE_VERTICAL); eg->Add(fgPick, 0);
        eg->Add(new wxStaticText(&dlg, wxID_ANY, "Background colour:"), 0, wxALIGN_CENTRE_VERTICAL); eg->Add(bgPick, 0);
        eg->Add(cbBold, 0); eg->Add(cbItalic, 0);
        auto* edBox = new wxStaticBoxSizer(wxVERTICAL, &dlg, "Style settings"); edBox->Add(eg, 0, wxALL, 8);
        auto col = [&](const char* cap, wxWindow* w){ auto* s = new wxBoxSizer(wxVERTICAL); s->Add(new wxStaticText(&dlg, wxID_ANY, cap), 0, wxBOTTOM, 4); s->Add(w, 1, wxEXPAND); return s; };
        auto* mid = new wxBoxSizer(wxHORIZONTAL);
        mid->Add(col("Language:", langList), 0, wxEXPAND | wxRIGHT, 10);
        mid->Add(col("Style:", styleList), 0, wxEXPAND | wxRIGHT, 10);
        mid->Add(edBox, 1, wxEXPAND);
        auto* btn = new wxBoxSizer(wxHORIZONTAL); btn->AddStretchSpacer();
        btn->Add(new wxButton(&dlg, wxID_OK, "Save && Close"), 0, wxRIGHT, 6); btn->Add(new wxButton(&dlg, wxID_CANCEL, "Cancel"), 0);
        auto* top = new wxBoxSizer(wxVERTICAL);
        top->Add(themeRow, 0, wxALL, 12); top->Add(mid, 1, wxEXPAND | wxLEFT | wxRIGHT, 12); top->Add(btn, 0, wxEXPAND | wxALL, 12);
        dlg.SetSizer(top);
        auto fillLangs = [&]{ langList->Clear(); langList->Append("Global Styles"); for (auto& kv : m_theme.lexers) langList->Append(kv.first); };
        auto fillStyles = [&]{
            styleList->Clear(); const wxString lang = langList->GetStringSelection();
            if (lang == "Global Styles") { for (auto& kv : m_theme.global) styleList->Append(kv.first); }
            else { auto it = m_theme.lexers.find(lang); if (it != m_theme.lexers.end()) for (auto& s : it->second) styleList->Append(s.name.empty() ? wxString::Format("Style %d", s.id) : s.name); }
        };
        auto loadStyle = [&]{
            const wxString lang = langList->GetStringSelection(); const int si = styleList->GetSelection(); if (si < 0) return;
            if (lang == "Global Styles") { auto it = m_theme.global.begin(); std::advance(it, si); fgPick->SetColour(bgrToColour(it->second.first)); bgPick->SetColour(it->second.second < 0 ? *wxWHITE : bgrToColour(it->second.second)); cbBold->Disable(); cbItalic->Disable(); }
            else { auto it = m_theme.lexers.find(lang); if (it == m_theme.lexers.end() || si >= (int)it->second.size()) return; const StyleDef& s = it->second[si]; fgPick->SetColour(bgrToColour(s.fg)); bgPick->SetColour(s.bg < 0 ? *wxWHITE : bgrToColour(s.bg)); cbBold->Enable(); cbItalic->Enable(); cbBold->SetValue((s.fontStyle & 1) != 0); cbItalic->SetValue((s.fontStyle & 2) != 0); }
        };
        auto applyEdit = [&]{
            const wxString lang = langList->GetStringSelection(); const int si = styleList->GetSelection(); if (si < 0) return;
            if (lang == "Global Styles") { auto it = m_theme.global.begin(); std::advance(it, si); it->second.first = colourToBgr(fgPick->GetColour()); it->second.second = colourToBgr(bgPick->GetColour()); }
            else { auto it = m_theme.lexers.find(lang); if (it == m_theme.lexers.end() || si >= (int)it->second.size()) return; StyleDef& s = it->second[si]; s.fg = colourToBgr(fgPick->GetColour()); s.bg = colourToBgr(bgPick->GetColour()); s.fontStyle = (cbBold->GetValue() ? 1 : 0) | (cbItalic->GetValue() ? 2 : 0); }
            applyEditorTheme(m_dark); if (auto* p = activePage()) setLexerForFile(p->path); if (m_stc) m_stc->Refresh();
        };
        fillLangs(); langList->SetSelection(0); fillStyles();
        langList->Bind(wxEVT_LISTBOX,  [&](wxCommandEvent&){ fillStyles(); });
        styleList->Bind(wxEVT_LISTBOX, [&](wxCommandEvent&){ loadStyle(); });
        fgPick->Bind(wxEVT_COLOURPICKER_CHANGED, [&](wxColourPickerEvent&){ applyEdit(); });
        bgPick->Bind(wxEVT_COLOURPICKER_CHANGED, [&](wxColourPickerEvent&){ applyEdit(); });
        cbBold->Bind(wxEVT_CHECKBOX,   [&](wxCommandEvent&){ applyEdit(); });
        cbItalic->Bind(wxEVT_CHECKBOX, [&](wxCommandEvent&){ applyEdit(); });
        themeCombo->Bind(wxEVT_CHOICE, [&](wxCommandEvent&){ applyThemeSelection(themeCombo->GetStringSelection()); fillLangs(); langList->SetSelection(0); fillStyles(); });
        themeDialog(&dlg);
        if (dlg.ShowModal() == wxID_OK)
        { saveThemeToXml(themeFilePath(m_themeName.empty() ? wxString(m_dark ? "DarkModeDefault" : "Default") : m_themeName)); saveSettings(); setStatus(0, "Theme styles saved"); m_hint = true; }
        else applyThemeSelection(original.empty() ? "Default" : original);   // Cancel -> reload the original theme from disk
    }
    void importStyleTheme()
    {
        wxFileDialog d(this, "Import style theme(s)", "", "", "Theme files (*.xml)|*.xml", wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
        if (d.ShowModal() != wxID_OK) return;
        wxArrayString paths; d.GetPaths(paths);
        const wxString dir = wxPathOnly(wxStandardPaths::Get().GetExecutablePath()) + "\\themes";
        int n = 0; for (const auto& p : paths) if (wxCopyFile(p, dir + "\\" + wxFileNameFromPath(p))) ++n;
        setStatus(0, wxString::Format("Imported %d theme(s) - choose them in Style Configurator", n)); m_hint = true;
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
            // Fold margin + markers, edge, and highlight indicators - re-applied on every theme switch so the whole
            // editor surface follows the theme like Notepad++ (not just tokens + default background).
            const auto fold = G("Fold"); const auto foldActive = G("Fold active"); const auto foldMargin = G("Fold margin");
            const int fMarginBg = foldMargin.second >= 0 ? foldMargin.second : gutterBg;
            sci(SCI_SETFOLDMARGINCOLOUR, 1, fMarginBg); sci(SCI_SETFOLDMARGINHICOLOUR, 1, fMarginBg);
            const int markFore = fold.second >= 0 ? fold.second : gutterBg;        // N++ swaps Fold fg/bg onto the markers
            const int markBack = fold.first  >= 0 ? fold.first  : 0x808080;
            const int markActive = foldActive.first >= 0 ? foldActive.first : markBack;   // full "Fold active" accent on the box markers
            applyFoldMarkerColours(markFore, markBack, markActive);   // recolour the 7 fold markers + re-arm the nested-square accent
            sci(SCI_SETEDGECOLOUR, dark ? 0x4A4A4A : 0xC8C8C8);   // long-line ruler: a subtle but visible gray (column set in applySettings)
            const auto smart = G("Smart Highlighting");  if (smart.second >= 0) sci(SCI_INDICSETFORE, SMART_INDIC, smart.second);
            const auto findMk = G("Find Mark Style");    if (findMk.second >= 0) sci(SCI_INDICSETFORE, MARK_INDIC, findMk.second);
            const auto bmk = G("Bookmark margin");       if (bmk.second  >= 0) sci(SCI_SETMARGINBACKN, 1, bmk.second);
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
        g_editorDark = dark;
        ::RedrawWindow(m_sci, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);  // refresh the dead-corner now
#endif
        if (menuBar()) menuBar()->Check(myID_DARKMODE, dark);
        const wxColour chromeBg = dark ? wxColour(32, 32, 32) : wxColour(240, 240, 240);   // explicit both ways
        const wxColour chromeFg = dark ? wxColour(220, 220, 220) : wxColour(0, 0, 0);
        if (auto* tb = toolBar()) { tb->SetBackgroundColour(chromeBg); tb->Refresh(); }
        if (auto* sb = GetStatusBar()) { sb->SetBackgroundColour(chromeBg); sb->SetForegroundColour(chromeFg); sb->Refresh(); }
#ifdef __WXMSW__
        if (m_grip) m_grip->setColours(chromeBg, dark ? wxColour(112, 112, 112) : wxColour(150, 150, 150));   // dots blend onto chrome
#endif
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
    void notImpl(const wxString& what) { setStatus(0, what + " - not yet implemented in this build"); m_hint = true; }
    void setStatus(int field, const wxString& text) { SetStatusText(" " + text, field); }  // leading space ~ 4px left margin, like Notepad++
    void showAbout()
    {
        wxDialog dlg(this, wxID_ANY, "About wxNotepad++");
        auto* s = new wxBoxSizer(wxVERTICAL);
        s->Add(new wxStaticBitmap(&dlg, wxID_ANY, wxBitmapBundle::FromSVG(APP_ICON_SVG, wxSize(72, 72))),
               0, wxALIGN_CENTRE | wxTOP, 18);
        s->Add(new wxStaticText(&dlg, wxID_ANY,
                   "wxNotepad++\n\n"
                   "A cross-platform, wxWidgets reimplementation of a Notepad++-style editor:\n"
                   "a native Scintilla editor with dark/light themes and plugin support.\n\n"
                   "Independent project - not affiliated with or endorsed by Notepad++."),
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
        if (!g_nibCommands.empty() && cmd >= NIB_CMD_BASE && cmd < NIB_CMD_BASE + static_cast<int>(g_nibCommands.size()))
        {   // Nib plugin command (cross-platform)
            const NibCmd& nc = g_nibCommands[cmd - NIB_CMD_BASE];
            if (nc.fn) nc.fn(reinterpret_cast<NibHost*>(g_view), &nibQuery, nc.user);
            return;
        }
        if (cmd >= myID_DOCLIST_ITEM && cmd < myID_DOCLIST_ITEM + 1000)   // document-list dropdown entry
        { const size_t n = (size_t)(cmd - myID_DOCLIST_ITEM); if (n < m_tabs->GetPageCount()) m_tabs->SetSelection(n); return; }
        if (const NppLang* L = nppLangFind(cmd)) { setForcedLang(L->lexer, L->name); return; }   // Language menu: force that lexer on the active buffer
        if (cmd >= myID_MACRO_ITEM && cmd < myID_MACRO_ITEM + 200)        // a saved macro from the Macro menu
        { const size_t n = (size_t)(cmd - myID_MACRO_ITEM); if (n < m_savedMacros.size()) playMacro(m_savedMacros[n].second); return; }
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
            case IDM_SEARCH_FINDINFILES: onFindInFiles(); break;
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
            case IDM_VIEW_DOC_MAP: toggleDocMap(); break;
            case IDM_VIEW_FUNC_LIST: toggleFuncList(); break;
            case IDM_VIEW_DOCLIST: toggleDocList(); break;
            case IDM_VIEW_FILEBROWSER: toggleFileBrowser(); break;
            case IDM_SEARCH_FINDINCREMENT: showIncBar(); break;
            case IDM_EDIT_COLUMNMODE: columnEditor(); break;
            case IDM_VIEW_MONITORING: notImpl("File monitoring"); break;
            case IDM_MACRO_STARTRECORDINGMACRO: startMacroRecord(); break;
            case IDM_MACRO_STOPRECORDINGMACRO: stopMacroRecord(); break;
            case IDM_MACRO_PLAYBACKRECORDEDMACRO: playMacro(m_macro); break;
            case IDM_MACRO_RUNMULTIMACRODLG: runMultiple(); break;
            case IDM_MACRO_SAVECURRENTMACRO: saveMacro(); break;
            case IDM_LANG_USER_DLG: notImpl("User-Defined Language dialog"); break;

            // ---- File: shell integration + file ops ----
            case IDM_FILE_SAVECOPYAS: saveCopyAs(); break;
            case IDM_FILE_SAVESESSION: saveSession(); break;
            case IDM_FILE_LOADSESSION: loadSession(); break;
            case IDM_FILE_RENAME: renameFile(); break;
            case IDM_FILE_DELETE: recycleFile(); break;
            case IDM_FILE_OPEN_FOLDER: revealInFolder(); break;
            case IDM_FILE_OPEN_CMD: openShellHere(false); break;
            case IDM_FILE_OPEN_POWERSHELL: openShellHere(true); break;
            case IDM_FILE_OPEN_DEFAULT_VIEWER: openInDefaultViewer(); break;
            case IDM_FILE_CLOSEALL_TOLEFT: closeAllSide(false); break;
            case IDM_FILE_CLOSEALL_TORIGHT: closeAllSide(true); break;
            case IDM_FILE_CLOSEALL_UNCHANGED: closeAllUnchanged(); break;

            // ---- Edit: clipboard paths, case, comments, read-only, sorts, conversions ----
            case IDM_EDIT_FULLPATHTOCLIP: copyToClip(curPath()); break;
            case IDM_EDIT_FILENAMETOCLIP: copyToClip(wxFileNameFromPath(curPath())); break;
            case IDM_EDIT_CURRENTDIRTOCLIP: copyToClip(wxFileName(curPath()).GetPath()); break;
            case IDM_EDIT_COPY_ALL_NAMES: copyToClip(allOpenPaths(true)); break;
            case IDM_EDIT_COPY_ALL_PATHS: copyToClip(allOpenPaths(false)); break;
            case IDM_EDIT_RANDOMCASE: transformSel([](std::string& s){ std::mt19937 g{ std::random_device{}() }; for (char& c : s) if (std::isalpha((unsigned char)c)) c = (char)((g() & 1) ? std::toupper((unsigned char)c) : std::tolower((unsigned char)c)); }); break;
            case IDM_EDIT_PROPERCASE_BLEND: transformSel([](std::string& s){ bool st = true; for (char& c : s){ if (std::isalpha((unsigned char)c)){ if (st) c = (char)std::toupper((unsigned char)c); st = false; } else st = true; } }); break;
            case IDM_EDIT_SENTENCECASE_BLEND: transformSel([](std::string& s){ bool st = true; for (char& c : s){ if (std::isalpha((unsigned char)c)){ if (st) c = (char)std::toupper((unsigned char)c); st = false; } else if (c=='.'||c=='!'||c=='?') st = true; } }); break;
            case IDM_EDIT_STREAM_COMMENT: streamComment(true); break;
            case IDM_EDIT_STREAM_UNCOMMENT: streamComment(false); break;
            case IDM_EDIT_BLOCK_COMMENT_SET: setLineComment(true); break;
            case IDM_EDIT_BLOCK_UNCOMMENT: setLineComment(false); break;
            case IDM_EDIT_TOGGLEREADONLY: toggleReadOnly(); break;
            case IDM_EDIT_TOGGLESYSTEMREADONLY: toggleSystemReadOnly(); break;
            case IDM_EDIT_SORTLINES_LENGTH_ASCENDING: sortLines(3, false); break;
            case IDM_EDIT_SORTLINES_LENGTH_DESCENDING: sortLines(3, true); break;
            case IDM_EDIT_SORTLINES_DECIMALDOT_ASCENDING: sortLines(4, false); break;
            case IDM_EDIT_SORTLINES_DECIMALDOT_DESCENDING: sortLines(4, true); break;
            case IDM_EDIT_SORTLINES_DECIMALCOMMA_ASCENDING: sortLines(5, false); break;
            case IDM_EDIT_SORTLINES_DECIMALCOMMA_DESCENDING: sortLines(5, true); break;
            case IDM_EDIT_SORTLINES_LOCALE_ASCENDING: sortLines(0, false); break;
            case IDM_EDIT_SORTLINES_LOCALE_DESCENDING: sortLines(0, true); break;
            case IDM_EDIT_SORTLINES_RANDOMLY: shuffleLines(); break;
            case IDM_EDIT_REMOVEEMPTYLINESWITHBLANK: removeEmptyLinesBlank(); break;
            case IDM_EDIT_SW2TAB_ALL: spacesToTabs(false); break;
            case IDM_EDIT_SW2TAB_LEADING: spacesToTabs(true); break;
            case IDM_EDIT_OPENSELECTEDFILEFOLDERINEXPLORER: revealInFolder(); break;
            case IDM_EDIT_OPENSELECTEDFILETOEDIT: openSelectedFile(); break;
            case IDM_EDIT_SEARCHONINTERNET: searchOnInternet(); break;
            case IDM_EDIT_REDACT_SELECTION: redactSelection(); break;
            case IDM_EDIT_INSERT_DATETIME_CUSTOMIZED: insertDateTime(true); break;
            case IDM_EDIT_AUTOCOMPLETE: autoComplete(true); break;               // Function/keyword completion (Ctrl+Space)
            case IDM_EDIT_AUTOCOMPLETE_CURRENTFILE: autoComplete(false); break;  // Word completion (document words)
            case IDM_EDIT_AUTOCOMPLETE_PATH: autoCompletePath(); break;          // Path completion

            // ---- Search: select-and-find, results nav, bookmarked lines ----
            case IDM_SEARCH_SETANDFINDNEXT: findSel(true, true); break;
            case IDM_SEARCH_SETANDFINDPREV: findSel(false, true); break;
            case IDM_SEARCH_VOLATILE_FINDNEXT: findSel(true, true); break;
            case IDM_SEARCH_VOLATILE_FINDPREV: findSel(false, true); break;
            case IDM_SEARCH_GOTONEXTFOUND: findNext(true); break;
            case IDM_SEARCH_GOTOPREVFOUND: findNext(false); break;
            case IDM_SEARCH_SELECTMATCHINGBRACES: selectBetweenBraces(); break;
            case IDM_SEARCH_CLEARALLMARKS: clearMarks(); break;
            case IDM_SEARCH_COPYMARKEDLINES: bookmarkLinesOp(0); break;
            case IDM_SEARCH_CUTMARKEDLINES: bookmarkLinesOp(1); break;
            case IDM_SEARCH_DELETEMARKEDLINES: bookmarkLinesOp(2); break;
            case IDM_SEARCH_DELETEUNMARKEDLINES: bookmarkLinesOp(3); break;
            case IDM_SEARCH_PASTEMARKEDLINES: bookmarkLinesOp(4); break;
            case IDM_SEARCH_INVERSEMARKS: bookmarkLinesOp(5); break;

            // ---- View: always-on-top, wrap symbol, hide lines, summary, browsers, new instance ----
            case IDM_VIEW_ALWAYSONTOP: toggleAlwaysOnTop(); break;
            case IDM_VIEW_WRAP_SYMBOL: toggleWrapSymbol(); break;
            case IDM_VIEW_HIDELINES: hideSelectedLines(); break;
            case IDM_VIEW_SUMMARY: showSummary(); break;
            case IDM_VIEW_IN_FIREFOX: openInBrowser("firefox"); break;
            case IDM_VIEW_IN_CHROME: openInBrowser("chrome"); break;
            case IDM_VIEW_IN_EDGE: openInBrowser(""); break;
            case IDM_VIEW_IN_IE: openInBrowser("iexplore"); break;
            case IDM_VIEW_LOAD_IN_NEW_INSTANCE: case IDM_VIEW_GOTO_NEW_INSTANCE: newInstance(true); break;
            case IDM_VIEW_POSTIT: case IDM_VIEW_DISTRACTIONFREE: ShowFullScreen(!IsFullScreen()); break;

            // ---- Tools: MD5 / SHA digests ----
            case IDM_TOOL_MD5_GENERATEINTOCLIPBOARD: hashSelection(L"MD5", "MD5", true); break;
            case IDM_TOOL_SHA1_GENERATEINTOCLIPBOARD: hashSelection(L"SHA1", "SHA-1", true); break;
            case IDM_TOOL_SHA256_GENERATEINTOCLIPBOARD: hashSelection(L"SHA256", "SHA-256", true); break;
            case IDM_TOOL_SHA512_GENERATEINTOCLIPBOARD: hashSelection(L"SHA512", "SHA-512", true); break;
            case IDM_TOOL_MD5_GENERATE: hashSelection(L"MD5", "MD5", false); break;
            case IDM_TOOL_SHA1_GENERATE: hashSelection(L"SHA1", "SHA-1", false); break;
            case IDM_TOOL_SHA256_GENERATE: hashSelection(L"SHA256", "SHA-256", false); break;
            case IDM_TOOL_SHA512_GENERATE: hashSelection(L"SHA512", "SHA-512", false); break;

            // ---- Window / Settings / Language: list, folders, normal text ----
            case IDM_WINDOW_WINDOWS: showWindowsList(); break;
            case IDM_SETTING_PREFERENCE: onPreferences(); break;
            case IDM_LANGSTYLE_CONFIG_DLG: onStyleConfig(); break;
            case IDM_SETTING_IMPORTSTYLETHEMES: importStyleTheme(); break;
            case IDM_SETTING_OPENPLUGINSDIR: openFolder(wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath() + wxFILE_SEP_PATH + "plugins"); break;
            case IDM_LANG_TEXT: setForcedLang("", "Normal text file"); break;   // force Normal Text (a manual pick, like the languages)
            case IDM_LANG_OPENUDLDIR: openFolder(wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath() + wxFILE_SEP_PATH + "userDefineLangs"); break;

            // ---- Help: external links + info ----
            case IDM_HOMESWEETHOME: wxLaunchDefaultBrowser("https://notepad-plus-plus.org/"); break;
            case IDM_PROJECTPAGE: wxLaunchDefaultBrowser("https://github.com/notepad-plus-plus/notepad-plus-plus"); break;
            case IDM_ONLINEDOCUMENT: wxLaunchDefaultBrowser("https://npp-user-manual.org/"); break;
            case IDM_FORUM: wxLaunchDefaultBrowser("https://community.notepad-plus-plus.org/"); break;
            case IDM_LANG_UDLCOLLECTION_PROJECT_SITE: wxLaunchDefaultBrowser("https://github.com/notepad-plus-plus/userDefinedLanguages"); break;
            case IDM_DEBUGINFO: themedInfo(wxString::Format("wxNotepad++ (experimental wxWidgets fork)\n\nwxWidgets %d.%d.%d\n%s\n\nExecutable:\n%s",
                wxMAJOR_VERSION, wxMINOR_VERSION, wxRELEASE_NUMBER, wxGetOsDescription(), wxStandardPaths::Get().GetExecutablePath()), "Debug Info"); break;
            case IDM_CMDLINEARGUMENTS: themedInfo("Usage: wxnpp [files...]\n\nFiles given on the command line are opened in tabs.", "Command Line Arguments"); break;

            case IDM_EXECUTE: onRun(); break;   // Run... (F5)

            // ---- Encoding: interpret-as (re-decode the file) + convert-to (re-encode on save) ----
            case IDM_FORMAT_ANSI: interpretAs(ENC_ANSI); break;
            case IDM_FORMAT_AS_UTF_8: interpretAs(ENC_UTF8); break;
            case IDM_FORMAT_UTF_8: interpretAs(ENC_UTF8_BOM); break;
            case IDM_FORMAT_UTF_16LE: interpretAs(ENC_UTF16_LE); break;
            case IDM_FORMAT_UTF_16BE: interpretAs(ENC_UTF16_BE); break;
            case IDM_FORMAT_CONV2_ANSI: convertTo(ENC_ANSI); break;
            case IDM_FORMAT_CONV2_AS_UTF_8: convertTo(ENC_UTF8); break;
            case IDM_FORMAT_CONV2_UTF_8: convertTo(ENC_UTF8_BOM); break;
            case IDM_FORMAT_CONV2_UTF_16LE: convertTo(ENC_UTF16_LE); break;
            case IDM_FORMAT_CONV2_UTF_16BE: convertTo(ENC_UTF16_BE); break;

            default: {
                const int mid = e.GetId() & 0xFFFF;
                if (const int cp = codepageForId(mid))   // Encoding > character set -> interpret the file as that code page
                {
                    wxString nm; if (auto* mb = menuBar()) if (auto* it = mb->FindItem(mid)) nm = it->GetItemLabelText();
                    interpretCharset(cp, nm); break;
                }
                // Any real menu/toolbar item we don't implement: name it so the click isn't silent.
                wxString lbl;
                if (auto* mb = menuBar()) if (wxMenuItem* it = mb->FindItem(mid)) lbl = it->GetItemLabelText();
                if (lbl.empty()) { e.Skip(); return; }   // not one of ours -> let wx handle it
                notImpl(lbl);
                break;
            }
        }
        updateStatus();
    }

    void updateLineMargin()   // right-justified numbers in a width sized to the digit count
    {
        if (!m_lineNumbers) { sci(SCI_SETMARGINWIDTHN, 0, 0); return; }   // line-number margin disabled in Preferences
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
        setStatus(5, encDisplay(activePage()));
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
        if (auto* tb = toolBar())
        {
            tb->EnableTool(IDM_FILE_SAVE, dirty);   tb->EnableTool(IDM_FILE_SAVEALL, anyDirty);
            tb->EnableTool(IDM_EDIT_UNDO, canUndo); tb->EnableTool(IDM_EDIT_REDO, canRedo);
            tb->EnableTool(IDM_EDIT_CUT, hasSel);   tb->EnableTool(IDM_EDIT_COPY, hasSel);
            tb->EnableTool(IDM_EDIT_PASTE, canPaste);
        }
        if (auto* mb = menuBar())
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
    wxStyledTextCtrl* m_docMap = nullptr;   // Document Map (minimap): a second view sharing the active document
    wxTreeCtrl* m_funcList = nullptr;       // Function List: per-file symbol tree (regex-parsed)
    wxTreeCtrl* m_fifPanel = nullptr;       // Find result: docked Find-in-Files results tree
    wxTimer*    m_flTimer  = nullptr;        // debounce re-parse of the Function List after edits
#ifdef __WXMSW__
    HWND        m_sci  = nullptr;
    SizeGripWin* m_grip = nullptr;          // custom dark-themed status-bar resize grip (native one can't theme)
    struct NibDock { HWND hClient; wxPanel* host; wxString name; };   // a plugin's native window hosted in a dock pane
    std::vector<NibDock> m_nibDocks;        // installed via nib.win32 dock_native (the GPL bridge maps NPPM_DMM* to it)
#endif
    wxAuiManager m_aui;                          // hosts the dockable side/bottom panels (Function List, Doc Map, Find results, Nib panels)
#ifdef WXNPP_HAS_BORDERLESS
    wxPanel*    m_titleBar  = nullptr;            // integrated top bar (icon + menu-buttons + window controls)
    wxMenuBar*  m_menuOwner = nullptr;            // owns the wxMenus the title-bar buttons pop (never attached as a real menu bar)
#endif
    wxToolBar*  m_toolBarPtr = nullptr;          // the toolbar (frame's own in native mode, aui-paned in integrated) - see toolBar()
    bool        m_integratedBar = false;         // setting: show the integrated top bar (restart-to-apply; read in OnInit)
    wxTimer     m_timer;
    wxString    m_path, m_lastFind, m_lastReplace;
    bool        m_wrap = false, m_ws = false, m_guides = false, m_dark = true;
    int         m_tabWidth = 4;                                   // persisted editor preferences (Settings > Preferences)
    bool        m_useTabs = true, m_lineNumbers = true, m_wrapSymbol = false, m_showToolbar = true, m_showStatusbar = true;
    bool        m_autocomplete = true;                            // auto word/keyword completion while typing
    bool        m_caretLine = true, m_autoindent = true;          // highlight the current line; auto-indent new lines
    int         m_caretWidth = 1, m_edgeColumn = 0;               // caret thickness (px); long-line marker column (0 = off)
    wxString    m_themeName;                                      // active editor theme (empty = dark/light default); Style Configurator
    std::vector<MacroStep> m_macro;                               // the current recorded macro
    bool        m_recording = false;
    std::vector<std::pair<wxString, std::vector<MacroStep>>> m_savedMacros;   // named macros (Macro menu, this session)
    bool        m_macroSepAdded = false;
    bool        m_hint = false;   // a "needs full app" message is showing in status field 0
    // cached toolbar/menu enable states (start enabled, matching the freshly-built toolbar)
    bool        m_stSave = true, m_stSaveAll = true, m_stUndo = true, m_stRedo = true, m_stSel = true, m_stPaste = true;
    int         m_newCount = 0;   // counter for "new N" tab titles
    int         m_zoom = 0;       // shared zoom level across all tabs (Ctrl+wheel), persisted
    NppTheme    m_theme;          // exact Notepad++ colours (loaded from theme XML)
};

// Chrome base, chosen at startup (restart-to-apply): native wxFrame, or - when the "integrated top bar"
// option is on and the platform supports it (Windows + Linux/GTK) - the borderless wxBorderlessFrame.
using NppShellFrame = NppShellFrameT<wxFrame>;
#ifdef WXNPP_HAS_BORDERLESS
using NppIntegratedFrame = NppShellFrameT<wxBorderlessFrame>;
#endif

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
#ifdef WXNPP_HAS_BORDERLESS
        // Integrated top bar (Settings > Preferences, restart-to-apply): a borderless frame whose chrome is
        // our own title bar. Only available where the wxBorderlessFrame backend exists (Windows + Linux/GTK).
        bool integrated = false;
        wxConfigBase::Get()->Read("IntegratedBar", &integrated, false);
        if (integrated)
        {
            auto* frame = new NppIntegratedFrame(dark);
            frame->Show(true);
            frame->restoreSession();
            for (int i = 1; i < argc; ++i) frame->openPath(argv[i]);
            return true;
        }
#endif
        auto* frame = new NppShellFrame(dark);
        frame->Show(true);
        frame->restoreSession();                                   // reopen files from a theme-restart
        for (int i = 1; i < argc; ++i) frame->openPath(argv[i]);   // open any files passed on the command line
        return true;
    }
};

wxIMPLEMENT_APP(NppApp);
