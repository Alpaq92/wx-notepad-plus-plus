// wxNote  |  cross-platform main-window shell
// ---------------------------------------------------------------------------
// A wxWidgets text editor built for a native look on Windows / Linux / macOS:
//   * wxMenuBar built data-driven (menu_model.h / menu_data_*.h) over the
//     application's OWN command-id space (src/command_ids.h)
//   * wxToolBar using the project's own MIT icon set (resources/icons/*.svg)
//   * wxAuiNotebook tab strip + a wxStyledTextCtrl editor (bundles Scintilla + Lexilla)
//   * 6-field status bar, updated live
//   * the application's own icon (src/app_icon_svg.h)
//   * plugins via the core's OWN permissive "Nib" API (include/nib/nib.h); binary
//     plugins written against the legacy Notepad++ ABI load through the optional GPL
//     packages/npp-bridge - the core itself reproduces NO third-party plugin ABI.
//
// Commands are routed through one onCommand() dispatcher. Editor-backed functions
// (file I/O, clipboard, case/EOL/line ops, comment, find/replace, bookmarks, brace
// match, zoom, wrap/whitespace/guides, full screen) are implemented against
// Scintilla via wxStyledTextCtrl.

#include <wx/wx.h>
#include <wx/intl.h>           // wxLocale + the _() gettext macro - UI localization (resources/locale/*)
#include <wx/imagpng.h>        // wxPNGHandler - loading raster PNG icons (the colored toolbar icon set)
#include <wx/notebook.h>
#include <wx/listbook.h>       // wxListbook - the Preferences dialog's left-side page selector
#include <wx/display.h>        // wxDisplay::GetClientArea/GetFromPoint - clamp the initial window to the usable screen (macOS)
#include <wx/listctrl.h>       // wxListView - wxListbook's page list (we widen it so labels don't truncate)
#include <wx/clrpicker.h>      // wxColourPickerCtrl - Style Configurator foreground/background pickers
#include <wx/fontenum.h>       // wxFontEnumerator - list system fonts + validate the chosen editor font is still installed
#include <wx/graphics.h>       // wxGraphicsContext - translucent per-tab colour tint
#include <wx/tglbtn.h>         // wxToggleButton - incremental search bar match-case/whole-word/regex toggles
#include <wx/aui/auibook.h>
#include <wx/aui/aui.h>          // wxAuiManager - dock host for plugin panels
#include <wx/stc/stc.h>          // wxStyledTextCtrl - cross-platform editor (Phase 3 port target)
#include <wx/splitter.h>         // wxSplitterWindow - hosts the two editor views (MAIN | SUB) side by side
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
#include <wx/xml/xml.h>         // wxXmlDocument - load theme XML
#include <wx/datetime.h>        // wxDateTime - insert date/time
#include <wx/dnd.h>             // wxFileDropTarget - drag & drop files to open
#include <wx/dcgraph.h>         // wxGCDC - antialiased drawing (symmetric spinner triangles)
#include <wx/dcbuffer.h>        // wxAutoBufferedPaintDC - flicker-free custom paint (title-bar window-control buttons)
#include <wx/dir.h>             // wxDir - scan the plugins/ folder + Find-in-Files traversal
#include <wx/textfile.h>        // wxTextFile - read files line-by-line for Find in Files
#include <wx/dirdlg.h>          // wxDirDialog - folder picker for Find in Files
#include <wx/print.h>           // wxPrinter/wxPrintout - File > Print
#include <wx/printdlg.h>        // wxPrintDialogData/wxPageSetupDialogData - the Print dialog + page geometry
#include <wx/paper.h>           // wxThePrintPaperDatabase - resolve a concrete default paper size (A4)
#include <wx/cmdline.h>         // wxCmdLineParser - -g/-e/-n/-r switches (see WxnApp::OnInit)
#include <wx/msgout.h>          // wxMessageOutputBest - --version/--help text from a GUI-subsystem exe (see WxnApp::OnInit)
#include <wx/snglinst.h>        // wxSingleInstanceChecker - "reuse an existing window" (Preferences > General)
#include <wx/ipc.h>             // wxServer/wxClient/wxConnection - hand file args to the already-running instance
#include <wx/tokenzr.h>         // wxStringTokenizer - splits the IPC payload back into paths/goto/encoding
#include <cerrno>               // errno/EACCES - detect permission-denied saves (see tryElevatedWrite)

#ifdef WXN_HAS_BORDERLESS
#include <type_traits>                  // std::is_base_of - detect the borderless base in WxnShellFrameT<FB>
#include <vector>                       // std::vector - accelerator-entry list for refreshAccelerators
#include <wxbf/borderless_frame.h>      // wxBorderlessFrame - the optional integrated/borderless title bar (Windows + GTK)
#include <wxbf/window_gripper.h>        // wxWindowGripper - cross-platform window move (MSW + GTK begin_move_drag)
#endif

#ifdef __WXMSW__
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <wx/msw/darkmode.h>   // wxDarkModeSettings - recolour wx's native dark menu bar (see WxnDarkModeSettings)
#include <shellapi.h>          // ShellExecuteW / SHFileOperationW - File menu shell commands (Explorer, cmd, Recycle Bin)
#include <bcrypt.h>            // BCrypt* - MD5 / SHA digests for the Tools menu
#pragma comment(lib, "bcrypt.lib")
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33   // Win11 rounded-corner control
#endif
#ifndef DWMWCP_DONOTROUND
#define DWMWCP_DONOTROUND 1
#endif
#else
#include <unistd.h>          // isatty()/read() - the POSIX side of `wxnote -` (read piped stdin into a buffer)
using UINT = unsigned int;   // Win32 scalar that leaks into the portable sci()/sciSend() message-id params
#endif

#ifdef __WXMAC__
// macOS-native tweak wxWidgets doesn't expose, implemented in src/macos_native.mm (compiled only on
// Apple). Takes the frame's raw NSWindow* (from MacGetTopLevelWindowRef()) as a void* so this header
// stays free of Cocoa types. (The toolbar is a docked non-native child wxToolBar on macOS, so it honours
// SetToolBitmapSize directly - no native NSToolbar icon-pinning shim is needed.)
extern "C" void wxn_HideWindowTitle(void* nsWindow);        // titleVisibility = Hidden (blank native bar)
// Integrated top bar (macOS flavour): transparent title bar + FullSizeContentView + the stock traffic
// lights re-centred in the toolbar row (Electron's WindowButtonsProxy technique - see macos_native.mm).
// Idempotent; re-call after resize/activate/deminiaturize/fullscreen-exit (AppKit snaps buttons back).
// Returns the left inset (px) where toolbar content may start, 0 if unavailable.
extern "C" int  wxn_InlineTrafficLights(void* nsWindow, int rowHeightPx);
extern "C" void wxn_DragWindow(void* nsWindow);             // native window drag from the current mouse-down
#endif

#ifdef __WXGTK__
// GTK-native tweak wxWidgets doesn't expose, implemented in src/gtk_native.cpp (compiled only on GTK).
// Installs a top-priority GtkCssProvider that repaints the app's native GtkScrollbars and their GtkScrolled-
// Window decoration nodes (the editor is wx's own GtkScrolledWindow scrollbar - see gtk_native.cpp - which
// Mint's accented dark theme otherwise paints as a full-height strip/tint down the right edge on an empty
// document) to neutral greys, and also attaches it directly to each scrollbar widget. Takes any of our
// GtkWidget*s (from GetHandle()) to resolve the GdkScreen AND as the tree-walk root; passed as a void* so this
// header stays free of GTK types, and may be null (then only the screen-wide provider is installed).
extern "C" void wxn_InstallDarkScrollbarCss(void* gtkWidgetOrNull, int dark);
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
#include <memory>             // std::unique_ptr - host-owned pending nib.keymap scheme builders
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
#include "command_ids.h"
#include "app_icon_svg.h"

// Set from the project() version by CMake (see CMakeLists.txt); fall back so a stray standalone
// compile still builds. Shown in the About dialog.
#ifndef WXN_VERSION
#define WXN_VERSION "dev"
#endif
#include "nib.h"               // our own permissive, cross-platform plugin API (Nib)
#include "terminal_panel.h"    // View > Show Terminal - bottom multi-tab shell panel (defines myID_VIEW_TERMINAL, used by menu_data_view.h below)
#include "menu_builder.h"      // data-driven menu bar builder (menu_model.h/menu_data_*.h) - replaces the old inline menu construction
#include "keymap_schemes.h"    // bundled read-only keymap presets ("wxNote default" + "Notepad++") - Tier 1 of the shortcut model
#include "shortcut_mapper_dialog.h"  // the Shortcut Mapper dialog + conflict engine (implements kCmdSettingShortcutMapper 48009)
#include "shortcut_labels.h"   // curated Scintilla "Editor commands" tier: seedEditorKeymapDefaults + wx<->SCK translation
#include "scintillua_engine.h" // embedded Lua+LPeg+Scintillua engine (the native language-definition engine)

static const int  MARK_BOOKMARK = 2;      // a free Scintilla marker number for bookmarks
static const int  MARK_INDIC    = 9;      // indicator number for "Mark All" highlights (Find dialog)
static const int  SMART_INDIC   = 10;     // indicator number for smart-highlight (double-click a word)
static const int  MARK_STYLE_BASE = 21;   // "Mark All Ext 1-5" style indicators (21..25) - the 5 mark-style colours
static const unsigned MARK_STYLE_COLOUR[5] = { 0x1F90FF, 0xE0A020, 0x50B050, 0xC060C0, 0x30B0C0 };  // BGR: orange, blue, green, purple, olive
static const int  URL_INDIC = 11;         // clickable-URL underline indicator
enum { myID_TIMER = 60000, myID_DOCLIST, myID_CAP_NEW, myID_CAP_CLOSE, myID_FLTIMER, myID_MONTIMER };   // fixed ids, above the kCmd* range
// UI language selection - shared by the top-level Localization menu and Preferences > General.
// Endonyms are ALWAYS shown in their own language (never translated); index 0 is "System default".
static const int myID_UILANG_FIRST = 60100;   // 10 radio items (myID_UILANG_FIRST + 0..9), clear of the 61xxx/62xxx/63xxx bases
static const int UI_LANG_IDS[] = { wxLANGUAGE_DEFAULT, wxLANGUAGE_ENGLISH, wxLANGUAGE_POLISH,
    wxLANGUAGE_GERMAN, wxLANGUAGE_FRENCH, wxLANGUAGE_SPANISH, wxLANGUAGE_RUSSIAN, wxLANGUAGE_JAPANESE,
    wxLANGUAGE_CHINESE_SIMPLIFIED, wxLANGUAGE_KOREAN };
static const char* const UI_LANG_ENDONYMS[] = { nullptr /*index 0 = "System default", localized at build time*/,
    "English", "Polski", "Deutsch", "Français", "Español", "Русский", "日本語", "简体中文", "한국어" };
static long readUiLang() { long v = wxLANGUAGE_DEFAULT; wxConfigBase::Get()->Read("UILanguage", &v, (long)wxLANGUAGE_DEFAULT); return v; }
static int  uiLangIndex(long lang) { for (int i = 0; i < (int)WXSIZEOF(UI_LANG_IDS); ++i) if (UI_LANG_IDS[i] == lang) return i; return 0; }
static wxString uiLangName(int i) { return i == 0 ? wxString(_("System default")) : wxString::FromUTF8(UI_LANG_ENDONYMS[i]); }
// One source of truth for the EOL-mode display names (status bar, EOL popup, Preferences > New Document).
// Deliberately untranslated in the status bar; menu items wrap them in _() as literals.
static const char* eolName(int mode) { return mode == SC_EOL_LF ? "Unix (LF)" : mode == SC_EOL_CR ? "Macintosh (CR)" : "Windows (CR LF)"; }
// Theme mode (Preferences > General): 0 = follow OS, 1 = Dark, 2 = Light - replaces the old plain
// "DarkMode" bool. Falls back to that legacy key only when it actually EXISTS (users who explicitly
// chose a theme keep that choice). When NEITHER key exists the app follows the OS - that covers fresh
// installs but DELIBERATELY also upgrades that never touched a restart-required preference (ThemeMode
// is only ever written by restartWithTheme): those users never chose Dark, they sat on the old
// accidental default - the previous fallback defaulted the missing legacy key to true, hard-locking
// such installs into Dark and making "system theme detection" look broken on a light OS.
static long readThemeMode()
{
    auto* c = wxConfigBase::Get();
    long mode;
    if (c->Read("ThemeMode", &mode)) return mode;
    bool legacyDark;
    if (c->Read("DarkMode", &legacyDark)) return legacyDark ? 1 : 2;
    return 0;
}
// "System" (themeMode anything but 1/2) must read the OS's app-theme preference directly:
// wxSystemAppearance::IsDark() deliberately does NOT check that (its own docs/source say so) - it
// only reports whether THIS process has already turned dark mode on, which is exactly what we're
// trying to decide here (chicken-and-egg, always false on a fresh light-born process). AreAppsDark()
// reads the actual AppsUseLightTheme registry value, which is what "follow the OS" needs.
static bool resolveDark(long themeMode) { return themeMode == 1 ? true : themeMode == 2 ? false : wxSystemSettings::GetAppearance().AreAppsDark(); }
static const int myID_DOCLIST_ITEM = 61000;   // base id for the document-list dropdown entries
static const int myID_MACRO_ITEM   = 62100;   // base id for saved-macro entries at the bottom of the Macro menu
static const int myID_OPENFOLDER_TOOL_BASE = 60300;   // base id for File > Open Containing Folder's dynamically-detected entries

// The one persistent editor view (set by the frame), used to release a tab's Document when its
// EditorPage is destroyed - the notebook switches away first, so the doc holds only the buffer ref.
static wxStyledTextCtrl* g_view = nullptr;

// One editor tab: a wxPanel that owns a Scintilla Document (the single shared view swaps to it).
// On-disk text encoding of a buffer; the Scintilla document itself always holds UTF-8.
enum Enc { ENC_UTF8 = 0, ENC_UTF8_BOM, ENC_UTF16_LE, ENC_UTF16_BE, ENC_ANSI, ENC_CHARSET };

// -e/--encoding CLI switch: name -> Enc (or -1 if unrecognized, so the caller can ignore it).
static int encodingFromName(const wxString& name)
{
    const wxString n = name.Lower();
    if (n == "ansi") return ENC_ANSI;
    if (n == "utf8" || n == "utf-8") return ENC_UTF8;
    if (n == "utf8bom" || n == "utf-8-bom") return ENC_UTF8_BOM;
    if (n == "utf16le" || n == "utf-16le") return ENC_UTF16_LE;
    if (n == "utf16be" || n == "utf-16be") return ENC_UTF16_BE;
    return -1;
}

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
    wxString lang  = _("Normal text file");   // status-bar language label
    bool     langForced = false;           // true once the user picks a language from the Language menu
    wxString forcedLexer;                  // that pick's Lexilla lexer name ("" = forced Normal Text)
    wxString forcedName;                   // that pick's display label for the status bar, e.g. "C++"
    wxString sciLang;                      // name of a registered Scintillua language when active ("" = none); container-lexed via m_scintillua
    int      encoding = ENC_UTF8;          // on-disk encoding (detected on load, written on save)
    int      codepage = 0;                 // when encoding == ENC_CHARSET: the Windows code page
    wxString encLabel;                     // when encoding == ENC_CHARSET: its status-bar label
    wxColour tabColour;                    // optional per-tab colour (invalid = none) - drawn as a stripe by PinTabArt
    bool     monitored = false;            // View > Monitoring (tail -f): reload on external change, caret to end
    wxLongLong monMtime = 0; wxULongLong monSize = 0;   // last-seen on-disk stats; background tabs catch up by re-comparing on activation
    wxString recoveryId;                   // set once this page's unsaved edits have been backed up (see backupUnsavedChanges); empty = nothing pending
};

// One editor "view" = a tab strip + ONE persistent wxStyledTextCtrl that hops across its pages (the
// MAIN / SUB view halves of the split). The frame keeps two and aliases m_tabs/m_stc/m_sci to whichever is active.
struct ViewPane {
    wxAuiNotebook*    tabs = nullptr;
    wxStyledTextCtrl* stc  = nullptr;
#ifdef __WXMSW__
    HWND              sci  = nullptr;
#endif
};

// A parsed colour theme (stylers.model.xml / themes/*.xml).
struct StyleDef { int id; int fg; int bg; int fontStyle; wxString name; };   // fg/bg = -1 when unspecified
struct WxnTheme
{
    bool loaded = false;
    std::map<wxString, std::pair<int,int>>   global;   // WidgetStyle name -> (fg,bg)
    std::map<wxString, std::vector<StyleDef>> lexers;   // LexerType name  -> WordsStyles
    std::string defaultFont; int defaultSize = 0;
};
// "RRGGBB" (theme XML) -> Scintilla 0xBBGGRR int, or -1 if empty/invalid.
static int npp_bgr(const wxString& rrggbb)
{
    long v = 0;
    if (rrggbb.empty() || !rrggbb.ToLong(&v, 16)) return -1;
    const int r = (int)((v >> 16) & 0xFF), g = (int)((v >> 8) & 0xFF), b = (int)(v & 0xFF);
    return (b << 16) | (g << 8) | r;
}
// Selected-text foreground. Scintilla leaves the selection FOREGROUND unset by default
// (ViewStyle: selColours.fore.isSet == false), so a selected glyph keeps its own syntax colour painted
// on top of the selection fill - and against a pale fill like the light theme's #ADD6FF a mid or light
// token (comment grey, a number) simply disappears. Themes nominally carry a "Selected text colour"
// foreground, but nearly every N++ XML we ship stores an inert sentinel ("000004") or a flat "000000"
// that would paint BLACK on a dark-blue dark-mode selection - so an explicit value is honoured only when
// it genuinely contrasts with the fill, and otherwise black/white is derived from the fill's luminance.
// Gamma-2.0 approximation on purpose: close enough for a pick-one-of-two decision, and needs no <cmath>.
static int selection_fore_bgr(int selBackBgr, int themeForeBgr)
{
    auto lum = [](int c) {                                  // c is Scintilla 0xBBGGRR
        const double r = (c & 0xFF) / 255.0, g = ((c >> 8) & 0xFF) / 255.0, b = ((c >> 16) & 0xFF) / 255.0;
        return 0.2126 * r * r + 0.7152 * g * g + 0.0722 * b * b;
    };
    const double lb = lum(selBackBgr);
    const int autoFore = (lb > 0.30) ? 0x000000 : 0xFFFFFF;   // dark text on a light fill, light on a dark one
    if (themeForeBgr >= 0)
    {
        const double lf = lum(themeForeBgr);
        const double hi = (lf > lb ? lf : lb) + 0.05, lo = (lf > lb ? lb : lf) + 0.05;
        if (hi / lo >= 4.5) return themeForeBgr;              // the theme's own choice is readable - respect it
    }
    return autoFore;
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

// nib.sci/1 - the portable Scintilla passthrough tail. The frame installs g_coreSciCall (it needs
// m_main/m_sub/m_stc); coreSciCall routes a view index (0=main, 1=sub, -1=active) into that editor's
// wxStyledTextCtrl::SendMsg. Both the Windows HWND bridge (SciHwndProc, below) and the cross-platform
// nib.sci vtable funnel through this one function, so every OS exercises the identical routing. Cross-
// platform on purpose - NOT inside __WXMSW__.
static std::function<intptr_t(int, unsigned, uintptr_t, intptr_t)> g_coreSciCall;
static intptr_t coreSciCall(int view, unsigned msg, uintptr_t w, intptr_t l)
{ return g_coreSciCall ? g_coreSciCall(view, msg, w, l) : 0; }

// The two view editors, published so the free-function Win32 SCI bridge (SciHwndProc can't see the
// frame's m_main/m_sub) can turn a subclass ref back into a view index. Set by the frame.
static wxStyledTextCtrl* g_mainStc = nullptr;
static wxStyledTextCtrl* g_subStc  = nullptr;

#ifdef __WXMSW__   // ---- the SCI_* bridge: route Scintilla messages sent to the editor HWND into wxStyledTextCtrl ----
// Win32 plugins reach the editor by SendMessage'ing SCI_* to the Scintilla HWND. But wxStyledTextCtrl
// services Scintilla messages only through SendMsg (a direct ScintillaWX call); its HWND's WndProc
// ignores them. Bridge it: subclass the wxSTC HWND and forward Scintilla-range messages (SCI_* live in
// 2000-2999, the lexer messages in 4000-4999) to the real editor through coreSciCall (above).

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

static LRESULT CALLBACK SciHwndProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR ref)
{
    if ((msg >= 2000 && msg < 3000) || (msg >= 4000 && msg < 5000)) {   // SCI_* + Lexilla range
        // Turn THIS handle's subclass ref (its wxStyledTextCtrl*) into a view index, then hand off to the
        // shared, cross-platform tail so the Windows path exercises the identical routing the Linux path
        // will. ref is always a view editor (set at subclass time), so -1 (active) is only a safety net.
        const auto* stc = reinterpret_cast<const wxStyledTextCtrl*>(ref);
        const int view = (stc == g_subStc) ? 1 : (stc == g_mainStc) ? 0 : -1;
        return static_cast<LRESULT>(coreSciCall(view, msg, w, l));
    }
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

// What one wxnote launch asks a window to open: the command line's positional arguments, already split
// into files (tabs) and folders (workspace-browser roots), plus the goto/encoding switches. Grouped into
// a struct rather than a growing parameter list because it travels three ways - straight from OnInit into
// a freshly built frame, and across the IPC handoff (serialise in OnInit, parse in WxnIpcConnection::OnExec)
// - so every added field would otherwise have to be threaded through four signatures by hand.
struct WxnOpenRequest
{
    wxArrayString paths;                              // existing FILES, absolute; each becomes a tab
    wxArrayString folders;                            // existing DIRECTORIES, absolute; last one roots the browser
    wxArrayInt    lines, cols;                        // parallel to paths: a `file:line[:col]` suffix, or -1
    int gotoLine = -1, gotoCol = -1, forceEnc = -1;   // the launch-wide -g / -e (or +N) switches (-1 = not given)
    bool readOnly = false;                            // -R/-M/--read-only: open THIS launch's files read-only
    bool split    = false;                            // -o/-O/--split: route THIS launch's files into the split view
    wxString findPattern;                             // +/{pattern}: put the caret on the first match in the last-opened file
    bool hasStdin = false;                            // '-': a piped-stdin buffer was captured (never travels over IPC - forces a new instance)
    wxString stdinText;                               // the captured stdin bytes (decoded UTF-8)
    wxString pluginMessage;                           // -pluginMessage=<text>, forwarded over the reuse-window IPC handoff too
};

// Set by the frame: a second wxnote launch handed its file args (+ optional -g/-e) to us over IPC
// (see WxnIpcConnection below) because "reuse an existing window" is active. gotoLine/gotoCol/forceEnc
// are -1 when not requested by the sending launch.
static std::function<void(const WxnOpenRequest&)> g_ipcOpenRequest;

// ---- -w/--wait: block the launching process until the file's tab is closed (git core.editor / $EDITOR) --
// The exe IS the GUI process - there is no separate console-subsystem CLI shim that could block on our
// behalf - so "block the caller" means exactly "don't exit yet", and the caller unblocks on our PROCESS
// EXIT, not on any IPC reply. That is also why --wait forces a NEW instance (see OnInit): the IPC handoff
// below Execute()s and returns immediately, which would unblock git before the tab was even opened.
static wxArrayString g_waitPaths;          // absolute paths still being waited on; emptying it quits the app
static bool          g_waitMode = false;   // --wait was given (also suppresses session restore/save)
// ---- --safe: start with no plugins loaded (guards loadNibPlugins(), which pulls in the N++ bridge too) --
static bool          g_safeMode = false;
// ---- --clean: a pristine launch = --safe (no plugins) PLUS skip session restore AND recovery-backup ----
// restore. Distinct from --safe (which only skips plugins); for bug-repro / catalog-screenshot QA that must
// start from a blank slate. --clean implies --safe, so g_safeMode is also set when this is (see OnInit).
static bool          g_cleanMode = false;
// ---- --locale <lang>: UI language for THIS RUN only -----------------------------------------------
// Deliberately NOT written back to the "UILanguage" config key: Preferences > Localization and the
// Localization radio menu must keep reflecting the user's persisted choice, so that a one-shot
// `wxnote --locale ja README.md` (screenshotting/QA'ing a catalog) doesn't silently repoint the editor.
// -1 = not given / not a language we ship a catalog for; see the raw-argv pre-scan in WxnApp::OnInit.
static int           g_localeOverride = -1;
// ---- -pluginMessage=<text>: a message handed to plugins at startup ---------------------------------
// Notepad++'s -pluginMessage command-line argument: the string is delivered to every loaded plugin once,
// after plugins are up (the GPL npp-bridge forwards it as NPPN_CMDLINEPLUGINMSG). Captured by the raw-argv
// pre-scan in WxnApp::OnInit (also registered with the parser so it shows in --help); empty when not given.
static wxString      g_pluginMessage;

// ---- single-instance "reuse window" IPC (Preferences > General; off by default) ------------------
// A second wxnote process, on finding one already running (wxSingleInstanceChecker), connects here as a
// wxClient and Executes() a small payload instead of opening its own window - one path per line, plus
// \x01-prefixed key lines: GOTO=line,col and ENC=n (-g/-e), DIR=<folder> (a positional directory, which
// must NOT go through openPath() - that returns nullptr for a directory, i.e. the running instance would
// silently swallow `wxnote .`) and FGOTO=idx,line,col (a per-file `file:line[:col]` suffix).
// Standard wx pattern for pairing wxSingleInstanceChecker with wxServer/wxClient; DDE on Windows, a
// loopback TCP port elsewhere.
//
// NOTE the string-literal split in "\x01" "ENC=", "\x01" "DIR=", "\x01" "FGOTO=" - on BOTH the sending and
// the receiving side. A C++ \x escape eats every following HEX DIGIT, so an unsplit "\x01ENC=" is really
// {0x1E,'N','C','='}: four characters, not five, because 'E' is a hex digit. That silently broke -e over
// the IPC handoff (the receiver's Mid(5) walked past the digits, leaving forceEnc at -1). "\x01GOTO="
// escaped it only because 'G' is not a hex digit, which is why that one key always worked.
// Keep the concatenation, and count every Mid() offset from the SPLIT form (5 for \x01XXX=, 7 for FGOTO=).
static const wxChar* const kIpcServiceName = wxT("31415");   // arbitrary but fixed "port" (Unix) / DDE service (Windows)
static const wxChar* const kIpcTopic = wxT("wxnote-open");

class WxnIpcConnection : public wxConnection
{
public:
    // The modern OnExec(topic, wxString) override (not the deprecated raw OnExecute(data,size,format)):
    // wx's DDE client transcodes the UTF-8 payload to UTF-16 for the DDEML wire transfer on Windows, so
    // decoding the raw bytes here as UTF-8 (matching what Execute(wxString) sent) silently mis-parses it -
    // GetTextFromData() (used internally by the base OnExecute(), which this bypasses) is what actually
    // knows to undo that. Overriding OnExec() instead sidesteps the whole issue: wx hands us an already-
    // decoded wxString regardless of which wire format the transport chose.
    bool OnExec(const wxString&, const wxString& payload) override
    {
        WxnOpenRequest req;
        wxStringTokenizer tok(payload, "\n");
        while (tok.HasMoreTokens())
        {
            const wxString line = tok.GetNextToken();
            if (line.StartsWith("\x01GOTO=")) {
                const wxString v = line.Mid(6); long l = -1, c = -1;
                v.BeforeFirst(',').ToLong(&l); if (v.Contains(",")) v.AfterFirst(',').ToLong(&c);
                req.gotoLine = (int)l; req.gotoCol = (int)c;
            } else if (line.StartsWith("\x01" "ENC=")) {
                long e = -1; line.Mid(5).ToLong(&e); req.forceEnc = (int)e;
            } else if (line.StartsWith("\x01" "DIR=")) {
                req.folders.Add(line.Mid(5));          // a positional directory: roots the workspace browser
            } else if (line.StartsWith("\x01" "FGOTO=")) {
                // per-file `file:line[:col]` suffix, as "<index into paths>,<line>,<col>". Without this the
                // suffix would work for a new window and be silently dropped whenever "reuse window" is on.
                const wxString v = line.Mid(7); long i = -1, l = -1, c = -1;
                v.BeforeFirst(',').ToLong(&i);
                const wxString rest = v.AfterFirst(',');
                rest.BeforeFirst(',').ToLong(&l); if (rest.Contains(",")) rest.AfterFirst(',').ToLong(&c);
                while ((long)req.lines.GetCount() <= i) { req.lines.Add(-1); req.cols.Add(-1); }
                if (i >= 0) { req.lines[i] = (int)l; req.cols[i] = (int)c; }
            } else if (line.StartsWith("\x01" "RO=")) {
                req.readOnly = true;                   // -R/-M: open the forwarded file(s) read-only in the reused window too
            } else if (line.StartsWith("\x01" "SPLIT=")) {
                req.split = true;                      // -o/-O/--split: route the forwarded file(s) into the split
            } else if (line.StartsWith("\x01" "FIND=")) {
                // +/{pattern} find-on-open. MUST stay split from \x01 as "\x01" "FIND=": 'F' is a hex digit,
                // so an unsplit "\x01FIND=" is really {0x1F,'I','N','D','='} and Mid(6) would mis-slice it
                // (the exact trap the ENC=/DIR= comment above documents). Count Mid() from the split form: 6.
                req.findPattern = line.Mid(6);
            } else if (line.StartsWith("\x01PLUGINMSG=")) {
                // -pluginMessage=<text>, forwarded so it still fires NPPN_CMDLINEPLUGINMSG in the reused
                // window (see g_ipcOpenRequest) instead of being silently dropped by the handoff. 'P' is
                // not a hex digit so this key is safe unsplit, like GOTO=/RO=/SPLIT= above. NOTE: like
                // FIND=, this does not escape an embedded newline in the message text - the same known
                // corner case as the pattern above (not reachable from a normal shell command line).
                req.pluginMessage = line.Mid(11);
            } else if (!line.empty()) req.paths.Add(line);
        }
        if (g_ipcOpenRequest) g_ipcOpenRequest(req);
        return true;
    }
};
class WxnIpcServer : public wxServer
{
public:
    wxConnectionBase* OnAcceptConnection(const wxString& topic) override
    { return topic == kIpcTopic ? new WxnIpcConnection() : nullptr; }
};

// ---- editor behaviours driven by Scintilla notifications (auto-indent, brace match, smart-hilite) ----
static sptr_t sciSend(wxStyledTextCtrl* s, UINT m, uptr_t w = 0, sptr_t l = 0)
{ return s ? static_cast<sptr_t>(s->SendMsg(static_cast<int>(m), static_cast<wxUIntPtr>(w), static_cast<wxIntPtr>(l))) : 0; }

// Smart-highlight (double-click a word -> box every occurrence) state. Only one editor is visible at
// a time, so a single active/owner pair suffices.
static bool g_smartActive = false;
static wxStyledTextCtrl* g_smartSci = nullptr;

// Auto-indent: on Enter, give the new line the previous line's indentation, plus one level after
// an opening brace/paren/colon.
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
// with its opener.
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

// Highlight the brace pair straddling the caret (red when unmatched).
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

// "Smart Highlighting": box every whole-word, case-sensitive occurrence of the just
// double-clicked word.
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
// A regex-per-language symbol extractor. Each rule below is derived directly from the language's own
// published definition syntax (the "how do you write a function/type definition in this language"
// production), written for portable std::regex (ECMAScript grammar). Each rule = {kind 0=function /
// 1=container(type), regex, name capture-group}. Patterns anchor on (?:^|\n) (start of a line) and the
// captured name's byte position gives the symbol's line. Per-rule derivation notes are inline; the
// shared design constraints are:
//  - match DEFINITIONS, not declarations or calls: a definition introduces a body, so most rules
//    require the opening `{` (or `:` + indent for Python, `=>` for C#'s expression bodies);
//  - a name followed by `(...)` also matches control-flow statements (`if (...)`, `while (...)`) and
//    calls, so function rules carry an explicit keyword-exclusion lookahead;
//  - std::regex has no recursion, so parameter lists are approximated as "no `;`/`{`/`}` (and where
//    noted no `)`) between the parens" - nested-paren default arguments are the accepted false
//    negative of that trade-off.
struct FLRule { int kind; std::regex re; int grp; };

class FLItemData : public wxTreeItemData { public: int line; explicit FLItemData(int l) : line(l) {} };
// Project Panel tree node: a folder (isFile=false) or a file reference (isFile=true, path = full path).
class ProjItemData : public wxTreeItemData { public: bool isFile; wxString path; ProjItemData(bool f, const wxString& p = "") : isFile(f), path(p) {} };

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
        // ---- C / C++ ----
        // Class-key declarations per [class.pre]: `class|struct|union`, optional leading template<>,
        // tolerated attribute/export macro words before the name, optional `final`, optional base-clause,
        // and the body's `{` (requiring the brace is also what excludes forward declarations). `{` may
        // sit on its own line (Allman style) - `[ \t\r\n]*` before it, not just `[ \t]*`.
        add("cpp", 1, R"((?:^|\n)[ \t]*(?:template[ \t]*<[^>]*>[ \t\r\n]*)?(?:class|struct|union)[ \t]+(?:[A-Za-z_]\w*[ \t]+)*([A-Za-z_]\w*)[ \t]*(?:final[ \t]*)?(?::[^{;]*)?[ \t\r\n]*\{)", 1);
        // Function definition: decl-specifiers, then a return type (>=1 type token ending in `*`/`&`/space),
        // then a possibly-qualified name - `ns::Class::fn`, destructor `~fn`, or `operator @` where @ is one
        // of the standard's overloadable operator tokens - then params and the body `{` (trailing
        // const/noexcept/override/final/-> tolerated between `)` and `{`). The lookahead keeps control-flow
        // statements from parsing as "type + name(...)".
        add("cpp", 0, R"((?:^|\n)[ \t]*(?:(?:inline|static|virtual|explicit|friend|constexpr|extern)[ \t]+)*[A-Za-z_][\w:<>,\*& \t]*?[\*& \t](?!(?:if|while|for|switch|catch|return|sizeof|do|else|new|delete|throw)\b)((?:[A-Za-z_]\w*::)*(?:~?[A-Za-z_]\w*|operator[ \t]*(?:\(\)|\[\]|(?:new|delete)(?:\[\])?|[-+*\/%^&|~!=<>,]+)))[ \t]*\([^;{}]*\)[ \t\r\n]*(?:const|noexcept|override|final|->[^;{]*|[ \t\r\n])*\{)", 1);
        // Constructors/destructors have no return type, so the rule above can't see them. Out-of-line
        // ones are unambiguous via the mandatory `Class::` qualifier - but a bare `Class::name(...)  {`
        // shape is indistinguishable by regex from a namespace-qualified CALL with a trailing lambda
        // argument (`std::sort(a, b, [](...) {`), which is common and would otherwise flood the list
        // with phantoms. A backreference (`\1`) requiring the post-`::` name to equal the qualifier
        // itself is what makes this unambiguous: `Foo::Foo(...)  {` can only be a constructor
        // definition in valid C++ (a call can't repeat its own qualifier as its name), never a call.
        add("cpp", 0, R"((?:^|\n)[ \t]*(?:[A-Za-z_]\w*::)*([A-Za-z_]\w*)::\1[ \t]*\([^;{}]*\)[ \t\r\n]*(?::[^{;]*)?\{)", 1);
        // Out-of-line destructor: same backreference trick, name must equal the qualifier with a `~`.
        add("cpp", 0, R"((?:^|\n)[ \t]*(?:[A-Za-z_]\w*::)*([A-Za-z_]\w*)::(~\1)[ \t]*\([^;{}]*\)[ \t\r\n]*\{)", 2);
        add("cpp", 0, R"((?:^|\n)[ \t]*(~[A-Za-z_]\w*)[ \t]*\([ \t]*\)[ \t\r\n]*\{)", 1);
        // ---- Python ----
        // Only two definition forms exist (compound statements `classdef` / `funcdef`): keyword, name,
        // optional PEP 695 type-parameter list `[...]`, then `(` (bases/params) or `:`. `async def` is
        // the one prefix the grammar allows.
        add("python", 1, R"((?:^|\n)[ \t]*class[ \t]+([A-Za-z_]\w*)[ \t]*(?:\[[^\]]*\][ \t]*)?[\(:])", 1);
        add("python", 0, R"((?:^|\n)[ \t]*(?:async[ \t]+)?def[ \t]+([A-Za-z_]\w*)[ \t]*(?:\[[^\]]*\][ \t]*)?\()", 1);
        // ---- JavaScript / TypeScript ----
        // Containers: class declarations (+ TS `interface`), with the export/default/abstract prefixes
        // the grammar permits.
        add("js", 1, R"((?:^|\n)[ \t]*(?:export[ \t]+)?(?:default[ \t]+)?(?:abstract[ \t]+)?(?:class|interface)[ \t]+([A-Za-z_$][\w$]*))", 1);
        // Named function declarations (incl. async and generator `function*`).
        add("js", 0, R"((?:^|\n)[ \t]*(?:export[ \t]+)?(?:default[ \t]+)?(?:async[ \t]+)?function[ \t]*\*?[ \t]*([A-Za-z_$][\w$]*)[ \t]*\()", 1);
        // Bindings initialized with a function value - `= function`, `= (params) =>` (optionally with a
        // TS return annotation before the arrow), or the single-parameter no-paren arrow `= x =>`.
        add("js", 0, R"((?:^|\n)[ \t]*(?:export[ \t]+)?(?:const|let|var)[ \t]+([A-Za-z_$][\w$]*)[ \t]*=[ \t]*(?:async[ \t]+)?(?:function\b|\([^)]*\)[ \t]*(?::[^={]*)?=>|[A-Za-z_$][\w$]*[ \t]*=>))", 1);
        // Class methods: indented `name(params) {`, with the member-modifier prefixes TS allows. The
        // param-content restriction excludes quotes/backtick (rejects the call-plus-string-callback
        // idiom, `describe('x', () => {`) AND parens/braces (rejects the call-plus-bare-callback idiom,
        // `setTimeout(function() {` / `request(opts, function(err, res) {` - both have an inner `(`
        // that would otherwise let the outer call's own closing `)` complete the "params" match). A
        // real method's parameter list never needs a nested call or function expression to parse as
        // one, so this is a one-sided trade-off: default values that call a function (`f(x = g()) {`)
        // become a false negative, same category as the already-documented quoted-default one.
        // (Custom raw-string delimiter: the pattern itself contains the plain )" terminator sequence.)
        add("js", 0, R"FL((?:^|\n)[ \t]+(?:(?:public|private|protected|static|readonly|async|get|set|override)[ \t]+)*(?!(?:if|for|while|switch|catch|function|return|else|do|new|typeof)\b)([A-Za-z_$][\w$]*)[ \t]*\([^(){}"'`]*\)[ \t\r\n]*(?::[^{;]*)?\{)FL", 1);
        // ---- Java ----
        // Type declarations (JLS: normal class, interface, enum, record) with their modifier set.
        add("java", 1, R"((?:^|\n)[ \t]*(?:(?:public|private|protected|abstract|final|static|sealed|non-sealed|strictfp)[ \t]+)*(?:class|interface|enum|record)[ \t]+([A-Za-z_]\w*))", 1);
        // Method definition: optional modifiers (optional, so package-private methods match too),
        // optional generic `<T>`, a return type, the name, params, optional throws, and the body `{`.
        // The return-type token itself must not be `new` (Java has no `new` modifier, so it can only
        // mean this is `new Type() { ... }` - an anonymous-class body, not a method) - without this,
        // `new` gets consumed as if it were the return type and the anonymous class's own type name
        // gets captured as a phantom method.
        add("java", 0, R"((?:^|\n)[ \t]*(?:(?:public|private|protected|static|final|abstract|synchronized|native|default|strictfp)[ \t]+)*(?:<[^>]+>[ \t]*)?(?!new\b)[A-Za-z_][\w<>\[\],. \t]*?[ \t](?!(?:if|while|for|switch|catch|return|new|else|do)\b)([A-Za-z_]\w*)[ \t]*\([^;{}]*\)[ \t\r\n]*(?:throws[^{;]*)?\{)", 1);
        // ---- C# ----
        // Type declarations incl. `record class` / `record struct` forms.
        add("cs", 1, R"((?:^|\n)[ \t]*(?:(?:public|private|protected|internal|static|sealed|abstract|partial|readonly|ref)[ \t]+)*(?:class|struct|interface|enum|record(?:[ \t]+(?:class|struct))?)[ \t]+([A-Za-z_]\w*))", 1);
        // Methods: like Java's rule, plus C#'s expression-bodied member form - the body is either a
        // block `{` or `=>`. Unlike Java, `new` IS a legitimate C# modifier (member-hiding), so it
        // can't be excluded as a return-type start the way Java's rule excludes it - `new Handler() {`
        // (an object-creation expression opening an initializer block) is an accepted residual
        // false-positive of that ambiguity, not fixable by regex alone.
        add("cs", 0, R"((?:^|\n)[ \t]*(?:(?:public|private|protected|internal|static|virtual|override|sealed|abstract|extern|async|new|partial|unsafe)[ \t]+)*[A-Za-z_][\w<>\[\],.? \t]*?[ \t](?!(?:if|while|for|foreach|switch|catch|return|new|else|do|using|lock)\b)([A-Za-z_]\w*)(?:<[^>]+>)?[ \t]*\([^;{}]*\)[ \t\r\n]*(?:where[^{=;]*)?(?:\{|=>))", 1);
        // ---- Go ----
        // The spec puts all declarations at column 0 (FunctionDecl/MethodDecl/TypeDecl are top-level
        // only), which conveniently also excludes function literals. Methods carry a receiver in parens
        // between `func` and the name. Optional `[T any]` type-parameter list (Go 1.18+ generics)
        // between the name and the parameter list / type keyword.
        add("go", 0, R"((?:^|\n)func[ \t]+(?:\([^)]*\)[ \t]*)?([A-Za-z_]\w*)[ \t]*(?:\[[^\]]*\][ \t]*)?\()", 1);
        add("go", 1, R"((?:^|\n)type[ \t]+([A-Za-z_]\w*)(?:\[[^\]]*\])?[ \t]+(?:struct|interface)[ \t]*\{)", 1);
        // ---- Rust ----
        // fn items with their qualifier prefixes (pub(...), async/unsafe/const, extern "abi").
        add("rust", 0, R"((?:^|\n)[ \t]*(?:pub(?:\([^)]*\))?[ \t]+)?(?:(?:async|unsafe|const)[ \t]+)*(?:extern[ \t]+"[^"]*"[ \t]+)?fn[ \t]+([A-Za-z_]\w*))", 1);
        // Type items; unit/tuple structs (`struct Foo;`) match too and are handled as leaf containers
        // by the range scan below (a `;` before any `{` ends the item).
        add("rust", 1, R"((?:^|\n)[ \t]*(?:pub(?:\([^)]*\))?[ \t]+)?(?:struct|enum|trait|union)[ \t]+([A-Za-z_]\w*))", 1);
        // impl blocks group their fns under the implemented type's name (the identifier after `for`,
        // or directly after `impl` for inherent impls). The trait name before `for` can itself carry
        // generic arguments - `impl From<u32> for MyType`, `impl Deref<Target = T> for MyType` - so the
        // character class spanning up to `for` must tolerate `<>`, `=`, and `,`, or the pattern can't
        // find "for" at all past the `<...>` and mis-captures the TRAIT name as if it were inherent.
        add("rust", 1, R"((?:^|\n)[ \t]*impl(?:[ \t]*<[^>]*>)?[ \t]+(?:[\w:&'<>=, \t]+[ \t]for[ \t]+)?([A-Za-z_]\w*))", 1);
        // ---- Lua ----
        // Both spellings of a named function: the `function Name` statement (with `.` field and `:`
        // method sugar) and the `Name = function(...)` assignment form.
        add("lua", 0, R"((?:^|\n)[ \t]*(?:local[ \t]+)?function[ \t]+([A-Za-z_]\w*(?:[.:][A-Za-z_]\w*)*)[ \t]*\()", 1);
        add("lua", 0, R"((?:^|\n)[ \t]*(?:local[ \t]+)?([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*)[ \t]*=[ \t]*function[ \t]*\()", 1);
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
            tbl["cpp"] = std::regex(cfam); tbl["java"] = std::regex(cfam); tbl["cs"] = std::regex(cfam);
            // JS adds template literals (backtick strings can span lines and contain braces, which
            // would otherwise corrupt the container range scan).
            tbl["js"] = std::regex(R"(/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*'|`(?:\\.|[^`\\])*`)");
            tbl["python"] = std::regex(R"(#[^\n]*|'''[\s\S]*?'''|"""[\s\S]*?"""|"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*')");
            // Go adds back-quoted raw string literals.
            tbl["go"] = std::regex(R"(/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*'|`[^`]*`)");
            // Rust: single quotes are lifetimes ('a) far more often than char literals, so only the
            // exactly-one-char literal form is masked - masking 'a...' spans would swallow real code.
            tbl["rust"] = std::regex(R"(/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\])')");
            // Lua: long comments/strings --[=[ ]=] / [=[ ]=] use a backreference to match the same
            // number of `=` signs on both ends; the line-comment alternative must come after the long
            // form so `--[[` prefers the block reading.
            tbl["lua"] = std::regex(R"(--\[(=*)\[[\s\S]*?\]\1\]|--[^\n]*|\[(=*)\[[\s\S]*?\]\2\]|"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*')");
        } catch (...) {}
    }
    auto it = tbl.find(lang);
    return it == tbl.end() ? nullptr : &it->second;
}

// One extracted symbol: byte positions in the scanned UTF-8 text. For containers (kind 1), rangeEnd
// is one past the body's closing brace (or dedent, for Python) so later symbols inside [pos, rangeEnd)
// nest under it in the tree.
struct FLSym { wxString name; int kind; size_t pos, end, rangeEnd; };

// Run flRules(lang) over `text` (UTF-8), mask comment/string zones, and compute container body ranges.
// Free function (rather than living inside the frame's parseFuncList) so it is directly testable
// against plain sample strings without a live editor.
static std::vector<FLSym> flCollect(const std::string& text, const std::string& lang)
{
    std::vector<FLSym> syms;
    const auto* rules = flRules(lang);
    if (!rules) return syms;
    // std::regex's backtracking engine can throw std::regex_error(error_stack/error_complexity) on a
    // sufficiently large or pathological span (observed around a ~300 KB block comment) rather than
    // just running slowly - this is a matcher-implementation limit, not a malformed pattern (the
    // add() lambda's own try/catch only guards construction). Uncaught, it would propagate out of the
    // wxTimer-driven parseFuncList() and take the whole app down over a single oversized file, so
    // every matching pass here is wrapped: on failure the Function List just ends up incomplete for
    // that language/rule instead of crashing.
    std::vector<std::pair<size_t, size_t>> zones;          // comment/string spans to skip
    if (const std::regex* cre = flCommentRe(lang))
    {
        try { for (std::sregex_iterator it(text.begin(), text.end(), *cre), e; it != e; ++it)
            zones.push_back({ (size_t)it->position(0), (size_t)(it->position(0) + it->length(0)) }); } catch (const std::regex_error&) {}
    }
    auto inZone = [&](size_t p) { for (auto& z : zones) if (p >= z.first && p < z.second) return true; return false; };
    for (const auto& r : *rules)
    {
        try {
            for (std::sregex_iterator it(text.begin(), text.end(), r.re), e; it != e; ++it)
            {
                const auto& m = *it;
                if (m.position(r.grp) < 0) continue;
                const size_t np = (size_t)m.position(r.grp);
                if (inZone(np)) continue;
                syms.push_back({ wxString::FromUTF8(m.str(r.grp).c_str()), r.kind, np, (size_t)(m.position(0) + m.length(0)), 0 });
            }
        } catch (const std::regex_error&) {}
    }
    for (auto& s : syms)                                   // compute each container's body range (for nesting)
    {
        if (s.kind != 1) { s.rangeEnd = s.end; continue; }
        if (lang == "python")                              // block extent = lines indented deeper than the class line
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
            // Not every container rule consumes the body's `{` (e.g. `class Name` rules stop at the
            // name so they can share one pattern with brace-on-next-line styles) - so first find the
            // opening brace, starting the depth count THERE. A `;` before any `{` means the match was
            // a declaration-only item (`struct Foo;` in Rust): keep it as a leaf with an empty range.
            size_t open = 0; bool haveOpen = false;
            if (s.end > 0 && text[s.end - 1] == '{') { open = s.end; haveOpen = true; }
            else
                for (size_t i = s.end; i < text.size(); ++i)
                {
                    if (inZone(i)) continue;
                    if (text[i] == '{') { open = i + 1; haveOpen = true; break; }
                    if (text[i] == ';') break;
                }
            if (!haveOpen) { s.rangeEnd = s.end; continue; }
            int depth = 1; s.rangeEnd = text.size();
            for (size_t i = open; i < text.size(); ++i)
            {
                if (inZone(i)) continue;
                if (text[i] == '{') depth++;
                else if (text[i] == '}' && --depth == 0) { s.rangeEnd = i; break; }
            }
        }
    }
    // Two rules can legitimately capture the same name at the same spot (e.g. Java/C#'s
    // `record Point(...)` reads as both a type declaration and a "type + name + parens + brace"
    // method) - sort containers first at equal positions, then drop the duplicate.
    std::sort(syms.begin(), syms.end(), [](const FLSym& a, const FLSym& b)
              { return a.pos != b.pos ? a.pos < b.pos : a.kind > b.kind; });
    syms.erase(std::unique(syms.begin(), syms.end(), [](const FLSym& a, const FLSym& b)
               { return a.pos == b.pos && a.name == b.name; }), syms.end());
    return syms;
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
// A themed numeric up/down field: borderless wxTextCtrl + custom-painted 1px outline and integrated
// ▲/▼ arrows. Replaces wxSpinCtrl everywhere, because the native msctls_updown32 buddy CANNOT be
// dark-themed (SetWindowTheme is a no-op on it) so a native spinner always sticks out in dark mode.
// Extracted from the Go-to-line dialog where this design was first fought out and approved.
class SpinField : public wxPanel
{
public:
    SpinField(wxWindow* p, int minV, int maxV, int value, bool dark, int width)
        : wxPanel(p, wxID_ANY), m_min(minV), m_max(maxV)
    {
        m_fieldBg   = dark ? wxColour(32, 32, 32)    : *wxWHITE;             // == the DarkMode_CFD edit bg
        m_fieldFg   = dark ? wxColour(220, 220, 220) : *wxBLACK;
        m_borderCol = dark ? wxColour(82, 82, 82)    : wxColour(122, 122, 122);

        SetBackgroundColour(m_fieldBg);
        m_text = new wxTextCtrl(this, wxID_ANY, wxString::Format("%d", value), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        m_text->SetBackgroundColour(m_fieldBg); m_text->SetForegroundColour(m_fieldFg);
        const int fh = m_text->GetBestSize().GetHeight();
        auto* fhs = new wxBoxSizer(wxHORIZONTAL);
        fhs->Add(m_text, 1, wxALIGN_CENTRE_VERTICAL | wxLEFT, 6);
        fhs->AddSpacer(kArrowZone);                          // childless zone on the right; arrows are painted here
        SetSizer(fhs);
        SetMinSize(wxSize(width, fh + 6));

        Bind(wxEVT_PAINT, [this](wxPaintEvent&) { paintField(); });
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
            const wxSize sz = GetClientSize();
            if (e.GetX() >= sz.x - kArrowZone) { bump(e.GetY() < sz.y / 2 ? +1 : -1); m_text->SetFocus(); }
            else e.Skip();
        });
    }
    int  GetValue() const { long v = 0; m_text->GetValue().ToLong(&v); if (v < m_min) v = m_min; if (v > m_max) v = m_max; return (int)v; }
    void SetValue(int v)  { if (v < m_min) v = m_min; if (v > m_max) v = m_max; m_text->SetValue(wxString::Format("%d", v)); }
    wxTextCtrl* Text() const { return m_text; }
private:
    static const int kArrowZone = 20;
    void bump(int d) { SetValue(GetValue() + d); m_text->SetInsertionPointEnd(); }
    void paintField()
    {
        // themeDialog's recursive colour pass (run after construction, before ShowModal) repaints every
        // panel in the dialog grey; lazily put the field colour back so the spinner stays one seamless box.
        if (GetBackgroundColour() != m_fieldBg)
        {
            SetBackgroundColour(m_fieldBg);
            m_text->SetBackgroundColour(m_fieldBg); m_text->SetForegroundColour(m_fieldFg); m_text->Refresh();
        }
        wxPaintDC dc(this);
        const wxSize sz = GetClientSize();
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
    wxColour    m_fieldBg, m_fieldFg, m_borderCol;
    int         m_min, m_max;
};

// The status bar's zoom control: SpinField's chrome recipe (borderless wxTextCtrl + a painted 1px
// outline + a childless zone on the right that an arrow is drawn into), but with ONE ▼ that opens a
// preset list instead of a spinner - SpinField's +/-1 bump is meaningless here, because one Scintilla
// zoom step is ~10 percentage points. A native wxComboBox is disqualified for the same reason
// TermListPopup exists: its drop list is drawn by the OS and stays bright white on dark chrome.
// The frame owns the percent<->zoom maths; this widget only shows what it is told and reports intent.
class ZoomField : public wxPanel
{
public:
    std::function<void(int)> onPercentTyped;   // a value was typed and committed (the frame clamps it and pushes the TRUE value back)
    std::function<void()>    onDropdown;       // the ▼ zone was clicked
    std::function<void(int)> onWheelStep;      // wheel over the field: one zoom step up (+1) or down (-1)

    ZoomField(wxWindow* p, bool dark) : wxPanel(p, wxID_ANY)
    {
        pickColours(dark);
        SetBackgroundColour(m_fieldBg);
        m_text = new wxTextCtrl(this, wxID_ANY, "100%", wxDefaultPosition, wxDefaultSize,
                                wxBORDER_NONE | wxTE_PROCESS_ENTER | wxTE_RIGHT);
        m_text->SetBackgroundColour(m_fieldBg); m_text->SetForegroundColour(m_fieldFg);
        auto* fhs = new wxBoxSizer(wxHORIZONTAL);
        fhs->Add(m_text, 1, wxALIGN_CENTRE_VERTICAL | wxLEFT, 4);
        fhs->AddSpacer(kCaretZone);                          // childless zone on the right; the caret is painted here
        SetSizer(fhs);

        Bind(wxEVT_PAINT, [this](wxPaintEvent&) { paintField(); });
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
            const wxSize sz = GetClientSize();
            if (e.GetX() >= sz.x - kCaretZone) { if (onDropdown) onDropdown(); }
            else { m_text->SetFocus(); e.Skip(); }
        });
        m_text->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { commit(); });
        m_text->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { e.Skip(); commit(); });
        m_text->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e) {
            if (e.GetKeyCode() == WXK_ESCAPE) { setPercent(m_shown); return; }   // abandon the edit, back to the truth
            e.Skip();
        });
        auto wheel = [this](wxMouseEvent& e) { if (onWheelStep) onWheelStep(e.GetWheelRotation() > 0 ? +1 : -1); };
        Bind(wxEVT_MOUSEWHEEL, wheel);
        m_text->Bind(wxEVT_MOUSEWHEEL, wheel);
    }

    // The frame pushes the value here; ChangeValue (not SetValue) so a programmatic resync never fires
    // wxEVT_TEXT, and the no-op check keeps the caret put when the shown value hasn't actually moved.
    void setPercent(int p)
    {
        m_shown = p;
        const wxString s = wxString::Format("%d%%", p);
        if (m_text->GetValue() != s) m_text->ChangeValue(s);
    }
    // Live dark/light toggle: SpinField's lazy repaint restore only covers the dialog-theming pass.
    void setColours(bool dark)
    {
        pickColours(dark);
        SetBackgroundColour(m_fieldBg);
        m_text->SetBackgroundColour(m_fieldBg); m_text->SetForegroundColour(m_fieldFg);
        m_text->Refresh(); Refresh();
    }

private:
    static const int kCaretZone = 16;
    void pickColours(bool dark)
    {
        m_fieldBg   = dark ? wxColour(32, 32, 32)    : wxColour(240, 240, 240);   // == the status bar's chrome, so the field reads as part of it
        m_fieldFg   = dark ? wxColour(220, 220, 220) : *wxBLACK;
        m_borderCol = dark ? wxColour(82, 82, 82)    : wxColour(160, 160, 160);
    }
    void commit()
    {
        // Clicking the ▼ moves focus off the edit, so kill-focus fires a commit BEFORE the popup opens -
        // without this guard the field fights itself (commit -> zoom -> setPercent) mid-click.
        if (m_inCommit) return;
        m_inCommit = true;
        wxString s = m_text->GetValue();
        s.Trim(true).Trim(false);
        if (s.EndsWith("%")) { s.RemoveLast(); s.Trim(true); }
        long v = 0;
        // Garbage, empty or nonsense: silently revert. A status-bar field must never pop a dialog.
        if (!s.ToLong(&v) || v <= 0)      setPercent(m_shown);
        else if ((int)v == m_shown)       setPercent(m_shown);      // unchanged - just re-render the canonical "NNN%"
        else if (onPercentTyped)          onPercentTyped((int)v);   // the frame clamps and calls setPercent with the truth
        m_inCommit = false;
    }
    void paintField()
    {
        wxPaintDC dc(this);
        const wxSize sz = GetClientSize();
        dc.SetBrush(*wxTRANSPARENT_BRUSH); dc.SetPen(wxPen(m_borderCol));
        dc.DrawRectangle(0, 0, sz.x, sz.y);                       // field outline (crisp 1px)
        const int ax = sz.x - kCaretZone;
        dc.DrawLine(ax, 3, ax, sz.y - 3);                         // thin separator before the caret
        // Antialiased fill, like SpinField's arrows: plain GDI DrawPolygon drops the bottom edge and the
        // triangle comes out lopsided.
        wxGCDC gdc(dc);
        const int cx = ax + kCaretZone / 2, mid = sz.y / 2;
        gdc.SetPen(*wxTRANSPARENT_PEN); gdc.SetBrush(wxBrush(m_fieldFg));
        const wxPoint dn[3] = { { cx - 3, mid - 2 }, { cx + 3, mid - 2 }, { cx, mid + 2 } };   // ▼
        gdc.DrawPolygon(3, dn);
    }
    wxTextCtrl* m_text = nullptr;
    wxColour    m_fieldBg, m_fieldFg, m_borderCol;
    int         m_shown = 100;      // the last percent the frame set - what Esc/garbage reverts to
    bool        m_inCommit = false;
};

class GoToLineDialog : public wxDialog
{
public:
    GoToLineDialog(wxWindow* p, int maxLine, int cur, bool dark)
        : wxDialog(p, wxID_ANY, _("Go to line"))
    {
        auto* s = new wxBoxSizer(wxVERTICAL);
        s->Add(new wxStaticText(this, wxID_ANY, wxString::Format(_("Line number (1 - %d):"), maxLine)), 0, wxALL, 10);
        m_spin = new SpinField(this, 1, maxLine, cur, dark, 130);
        s->Add(m_spin, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);
        s->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALL | wxEXPAND, 10);
        SetSizerAndFit(s);
        m_spin->Text()->SetFocus(); m_spin->Text()->SelectAll();
    }
    int GetLine() const { return m_spin->GetValue(); }
private:
    SpinField* m_spin = nullptr;
};

// Column Editor (Alt+C): insert text or an incrementing number down a column /
// rectangular (or multi-) selection. The dialog gathers the choice; the frame applies it per selection.
class ColumnEditorDialog : public wxDialog
{
public:
    ColumnEditorDialog(wxWindow* p, bool dark) : wxDialog(p, wxID_ANY, _("Column Editor"))
    {
        const wxColour fbg = dark ? wxColour(32, 32, 32) : *wxWHITE, ffg = dark ? wxColour(220, 220, 220) : *wxBLACK;
        auto field = [&](const wxString& v, int w) { auto* t = new wxTextCtrl(this, wxID_ANY, v, wxDefaultPosition, wxSize(w, -1)); if (dark) { t->SetBackgroundColour(fbg); t->SetForegroundColour(ffg); } return t; };
        auto* s = new wxBoxSizer(wxVERTICAL);

        m_radioText = new wxRadioButton(this, wxID_ANY, _("Text to Insert"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
        m_textField = field("", 250);
        s->Add(m_radioText, 0, wxLEFT | wxTOP | wxRIGHT, 10);
        s->Add(m_textField, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 26);

        m_radioNum = new wxRadioButton(this, wxID_ANY, _("Number to Insert"));
        s->Add(m_radioNum, 0, wxLEFT | wxRIGHT, 10);
        auto* nrow = new wxBoxSizer(wxHORIZONTAL);
        nrow->Add(new wxStaticText(this, wxID_ANY, _("Initial:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        m_initial = field("1", 60); nrow->Add(m_initial, 0, wxRIGHT, 14);
        nrow->Add(new wxStaticText(this, wxID_ANY, _("Increase by:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        m_increase = field("1", 60); nrow->Add(m_increase);
        s->Add(nrow, 0, wxLEFT | wxRIGHT | wxTOP, 26);
        m_leadZero = new wxCheckBox(this, wxID_ANY, _("Leading zeros"));
        s->Add(m_leadZero, 0, wxLEFT | wxTOP | wxBOTTOM, 26);
        wxString fmts[] = { _("Dec"), _("Hex"), _("Oct"), _("Bin") };
        m_format = new wxRadioBox(this, wxID_ANY, _("Format"), wxDefaultPosition, wxDefaultSize, 4, fmts, 4, wxRA_SPECIFY_COLS);
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

// Settings > ... Edit > Search on Internet's "Change Search Engine...".
class SearchEngineDialog : public wxDialog
{
public:
    SearchEngineDialog(wxWindow* p, int current) : wxDialog(p, wxID_ANY, _("Change Search Engine"))
    {
        auto* s = new wxBoxSizer(wxVERTICAL);
        wxString names[] = { "DuckDuckGo", "Google", "Bing", "Yahoo", "Brave Search" };
        m_choice = new wxRadioBox(this, wxID_ANY, _("Search on Internet uses:"), wxDefaultPosition, wxDefaultSize, 5, names, 1, wxRA_SPECIFY_COLS);
        m_choice->SetSelection(current >= 0 && current < 5 ? current : 0);
        s->Add(m_choice, 0, wxALL | wxEXPAND, 12);
        s->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALL | wxEXPAND, 10);
        SetSizerAndFit(s);
    }
    int engine() const { return m_choice->GetSelection(); }
private:
    wxRadioBox* m_choice = nullptr;
};

// Search > "Find characters in range...": selects the next character (by byte value) within [from, to].
class FindCharRangeDialog : public wxDialog
{
public:
    FindCharRangeDialog(wxWindow* p, bool dark) : wxDialog(p, wxID_ANY, _("Find Characters in Range"))
    {
        const wxColour fbg = dark ? wxColour(32, 32, 32) : *wxWHITE, ffg = dark ? wxColour(220, 220, 220) : *wxBLACK;
        auto field = [&](const wxString& v) { auto* t = new wxTextCtrl(this, wxID_ANY, v, wxDefaultPosition, wxSize(80, -1)); if (dark) { t->SetBackgroundColour(fbg); t->SetForegroundColour(ffg); } return t; };
        auto* s = new wxBoxSizer(wxVERTICAL);
        s->Add(new wxStaticText(this, wxID_ANY, _("Finds the next character (from the caret, wrapping)\nwhose code falls within this range.")), 0, wxALL, 10);
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(this, wxID_ANY, _("From:")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 6);
        m_from = field("00"); row->Add(m_from, 0, wxRIGHT, 14);
        row->Add(new wxStaticText(this, wxID_ANY, _("To:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        m_to = field("1F"); row->Add(m_to);
        s->Add(row, 0, wxLEFT | wxRIGHT | wxBOTTOM, 26);
        m_hex = new wxCheckBox(this, wxID_ANY, _("Hexadecimal"));
        m_hex->SetValue(true);
        s->Add(m_hex, 0, wxLEFT | wxBOTTOM, 26);
        s->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALL | wxEXPAND, 10);
        SetSizerAndFit(s);
        m_from->SetFocus();
        if (auto* ok = FindWindow(wxID_OK)) ok->SetLabel(_("Find Next"));
    }
    bool range(int& from, int& to) const
    {
        long a = 0, b = 0; const int base = m_hex->GetValue() ? 16 : 10;
        if (!m_from->GetValue().ToLong(&a, base) || !m_to->GetValue().ToLong(&b, base)) return false;
        from = (int)a; to = (int)b;
        if (from > to) std::swap(from, to);
        return true;
    }
private:
    wxTextCtrl* m_from = nullptr; wxTextCtrl* m_to = nullptr; wxCheckBox* m_hex = nullptr;
};

// Options gathered from the Find/Replace dialog and passed to the editor search engine.
struct FindOpts { wxString find, repl; bool matchCase = false, wholeWord = false, regex = false, wrap = true, forward = true, inSelection = false; };

// A modeless Find dialog: a tabbed control (Find / Replace / Find in Files /
// Mark). It is UI-only: each button calls a std::function the frame
// supplies (so the dialog needn't know about WxnShellFrame). opts() reads the active tab's
// controls; the find/replace text carries across tabs.
class FindReplaceDialog : public wxDialog
{
public:
    enum Tab { TAB_FIND, TAB_REPLACE, TAB_FIF, TAB_MARK };
    std::function<void(const FindOpts&)> findNextCb, countCb, replaceCb, replaceAllCb, markAllCb;
    std::function<void()> clearMarksCb;
    std::function<void(const FindOpts&, const wxString& dir, const wxString& filters, bool replace)> fifCb;   // Find/Replace in Files
    std::function<void(const wxString&)> infoCb;   // hint for actions the wx shell can't do yet

    explicit FindReplaceDialog(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, _("Replace"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    {
        m_nb = new wxNotebook(this, wxID_ANY);
        for (int t = 0; t < 4; ++t) m_pages[t] = buildPage(t);
        m_nb->AddPage(m_pages[TAB_FIND]->panel,    _("Find"));
        m_nb->AddPage(m_pages[TAB_REPLACE]->panel, _("Replace"));
        m_nb->AddPage(m_pages[TAB_FIF]->panel,     _("Find in Files"));
        m_nb->AddPage(m_pages[TAB_MARK]->panel,    _("Mark"));

        auto* top = new wxBoxSizer(wxVERTICAL);
        top->Add(m_nb, 1, wxEXPAND | wxALL, 6);
        m_status = new wxStaticText(this, wxID_ANY, " ");   // result line ("Replaced N occurrences")
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
    // Open the Mark tab with the given text primed into its Find field.
    void showMarkTab(const wxString& findText)
    {
        m_nb->SetSelection(TAB_MARK); SetTitle(tabTitle(TAB_MARK));
        PageCtrls* p = m_pages[TAB_MARK];
        if (!findText.empty() && p->find) p->find->SetValue(findText);
        if (p->find) { p->find->SetFocus(); p->find->SelectAll(); }
    }
    // Show a result message in the dialog (e.g. "Replaced 3 occurrences").
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
        static const wxString n[4] = { _("Find"), _("Replace"), _("Find in Files"), _("Mark") };
        return n[(t >= 0 && t < 4) ? t : 0];
    }

    // Translate Extended-mode escapes (\n \r \t \0 \\ \xHH) to their literal bytes, so a plain
    // (non-regex) search matches them - the "Extended" search mode.
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
        grid->Add(new wxStaticText(panel, wxID_ANY, _("Find what :")), 0, wxALIGN_CENTRE_VERTICAL);
        pc->find = new wxComboBox(panel, wxID_ANY, "", wxDefaultPosition, wxSize(330, -1));
        grid->Add(pc->find, 1, wxEXPAND);
        if (t == TAB_REPLACE || t == TAB_FIF) {
            grid->Add(new wxStaticText(panel, wxID_ANY, _("Replace with :")), 0, wxALIGN_CENTRE_VERTICAL);
            pc->repl = new wxComboBox(panel, wxID_ANY, "", wxDefaultPosition, wxSize(330, -1));
            grid->Add(pc->repl, 1, wxEXPAND);
        }
        if (t == TAB_FIF) {
            grid->Add(new wxStaticText(panel, wxID_ANY, _("Filters :")), 0, wxALIGN_CENTRE_VERTICAL);
            pc->filters = new wxComboBox(panel, wxID_ANY, "*.*", wxDefaultPosition, wxSize(330, -1));
            grid->Add(pc->filters, 1, wxEXPAND);
            grid->Add(new wxStaticText(panel, wxID_ANY, _("Directory :")), 0, wxALIGN_CENTRE_VERTICAL);
            pc->dir = new wxComboBox(panel, wxID_ANY, "", wxDefaultPosition, wxSize(330, -1));
            grid->Add(pc->dir, 1, wxEXPAND);
        }
        s->Add(grid, 0, wxEXPAND | wxALL, 8);

        // ---- options (left) + action buttons (right) ----
        auto* mid = new wxBoxSizer(wxHORIZONTAL);
        auto* opt = new wxBoxSizer(wxVERTICAL);
        pc->word  = new wxCheckBox(panel, wxID_ANY, _("Match &whole word only"));
        pc->caseC = new wxCheckBox(panel, wxID_ANY, _("Match &case"));
        pc->wrap  = new wxCheckBox(panel, wxID_ANY, _("Wra&p around")); pc->wrap->SetValue(true);
        opt->Add(pc->word, 0, wxALL, 3);
        opt->Add(pc->caseC, 0, wxALL, 3);
        if (t == TAB_FIND || t == TAB_REPLACE) {
            pc->backward = new wxCheckBox(panel, wxID_ANY, _("&Backward direction"));
            opt->Add(pc->backward, 0, wxALL, 3);
        }
        opt->Add(pc->wrap, 0, wxALL, 3);
        if (t == TAB_REPLACE || t == TAB_MARK) {
            pc->inSel = new wxCheckBox(panel, wxID_ANY, _("In se&lection"));
            opt->Add(pc->inSel, 0, wxALL, 3);
        }
        const wxString modes[3] = { _("&Normal"), _("E&xtended (\\n, \\r, \\t, \\0, \\x...)"), _("Re&gular expression") };
        pc->mode = new wxRadioBox(panel, wxID_ANY, _("Search Mode"), wxDefaultPosition, wxDefaultSize, 3, modes, 1, wxRA_SPECIFY_COLS);
        opt->Add(pc->mode, 0, wxALL | wxEXPAND, 3);
        mid->Add(opt, 1, wxEXPAND | wxRIGHT, 12);

        auto* col = new wxBoxSizer(wxVERTICAL);
        auto mk = [&](const wxString& lbl, std::function<void()> act) {
            auto* b = new wxButton(panel, wxID_ANY, lbl, wxDefaultPosition, wxSize(150, -1));
            b->Bind(wxEVT_BUTTON, [act](wxCommandEvent&) { act(); });
            col->Add(b, 0, wxEXPAND | wxBOTTOM, 5);
        };
        if (t == TAB_FIND) {
            mk(_("Find Next"), [this] { pushHistory(); if (findNextCb) findNextCb(opts()); });
            mk(_("Count"),     [this] { pushHistory(); if (countCb)    countCb(opts()); });
            mk(_("Find All"),  [this] { pushHistory(); if (markAllCb)  markAllCb(opts()); });
        } else if (t == TAB_REPLACE) {
            mk(_("Find Next"),   [this] { pushHistory(); if (findNextCb)   findNextCb(opts()); });
            mk(_("Replace"),     [this] { pushHistory(); if (replaceCb)    replaceCb(opts()); });
            mk(_("Replace All"), [this] { pushHistory(); if (replaceAllCb) replaceAllCb(opts()); });
        } else if (t == TAB_FIF) {
            mk(_("Find All"),         [this] { pushHistory(); if (fifCb) fifCb(opts(), m_pages[TAB_FIF]->dir->GetValue(), m_pages[TAB_FIF]->filters->GetValue(), false); });
            mk(_("Replace in Files"), [this] { pushHistory(); if (fifCb) fifCb(opts(), m_pages[TAB_FIF]->dir->GetValue(), m_pages[TAB_FIF]->filters->GetValue(), true); });
        } else { // TAB_MARK
            mk(_("Mark All"),    [this] { pushHistory(); if (markAllCb) markAllCb(opts()); });
            mk(_("Clear Marks"), [this] { if (clearMarksCb) clearMarksCb(); });
        }
        mk(_("Close"), [this] { Hide(); });
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
            case TAB_FIF:     if (infoCb)     infoCb(_("Find in Files")); break;
        }
    }
    // Remember the searched text in the combo dropdowns (most-recent first, de-duplicated).
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
static const int NIB_CMD_BASE = 63000;   // Nib command menu ids (clear of kCmd*, doc-list 61xxx, bridge plugins 62xxx)
static const int kSciLangMenuBase = 63500;   // Language-menu ids for registered Scintillua languages (clear of NIB_CMD_BASE)

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
// nib.commands/1 (+v2 invoke_command)
static std::function<void(int)> g_nibInvokeCommand;   // frame installs this (needs onCommand); dispatches a command id
static void nibCmdRegister(NibHost*, const char* id, const char* title, NibCommandFn fn, void* u)
{ g_nibCommands.push_back({ id ? id : "", title ? title : "", fn, u }); }
static void nibInvokeCommand(NibHost*, int id) { if (g_nibInvokeCommand) g_nibInvokeCommand(id); }
// nib.events/1 (+v2). Both versions share one subscriber list; the `v2` flag records which table a
// subscription came through, because NIB_EV_DOCUMENT_SAVED means different things per version: v1 is
// savepoint-derived (fires on undo-to-savepoint too, no payload), v2 fires once per real disk write
// with the saved buffer's id. Emission sites pass a gate so each cohort only hears its own semantics.
struct NibSub { NibEventKind kind; NibEventFn fn; void* user; bool v2; };
static std::vector<NibSub> g_nibSubs;
static void nibSubscribe(NibHost*, NibEventKind kind, NibEventFn fn, void* u)   { g_nibSubs.push_back({ kind, fn, u, false }); }
static void nibSubscribeV2(NibHost*, NibEventKind kind, NibEventFn fn, void* u) { g_nibSubs.push_back({ kind, fn, u, true  }); }
// nib.events/3 set_modified_mask: the union of Scintilla modificationType flags a subscriber wants
// NIB_EV_TEXT_MODIFIED fired for. 0 (the default) means "never fire it" - onStcModified checks this mask
// FIRST, so with no opt-in the high-volume SCN_MODIFIED path costs a single branch and crosses the ABI
// boundary zero times. A plugin arms it through the v3 table (the GPL bridge does so from
// NPPM_ADDSCNMODIFIEDFLAGS); unloadNibPlugins() resets it to 0 when the last plugin goes away.
static uint32_t g_nibModifiedMask = 0;
static void nibSetModifiedMask(NibHost*, uint32_t mask) { g_nibModifiedMask = mask; }
enum NibFireGate { NIB_FIRE_ALL, NIB_FIRE_V1_ONLY, NIB_FIRE_V2_ONLY };
static void nibFireEvent(const NibEvent& ev, NibFireGate gate = NIB_FIRE_ALL)   // called by the editor handlers below
{
    for (const auto& s : g_nibSubs)
        if (s.kind == ev.kind && s.fn && (gate == NIB_FIRE_ALL || (gate == NIB_FIRE_V2_ONLY) == s.v2))
            s.fn(reinterpret_cast<NibHost*>(g_view), &ev, s.user);
}
// Fire a document-scoped event (id = the EditorPage*). The one shape every document event shares.
static void nibFireDocEvent(NibEventKind kind, void* page, NibFireGate gate = NIB_FIRE_ALL)
{
    NibEvent ev{}; ev.kind = kind; ev.struct_size = sizeof(NibEvent);
    ev.as.document.id = reinterpret_cast<intptr_t>(page);
    nibFireEvent(ev, gate);
}
// v4 NIB_EV_CMDLINE_PLUGIN_MSG: deliver the -pluginMessage command-line text to subscribers (the GPL
// npp-bridge maps it to NPPN_CMDLINEPLUGINMSG). The UTF-8 buffer is stack-owned and outlives the
// synchronous fire, matching the "valid only during the callback" contract in nib.h. Also the direct
// test seam the selftest calls (no second process launch).
static void nibFireCmdlinePluginMsg(const wxString& msg)
{
    const wxScopedCharBuffer u8 = msg.utf8_str();
    NibEvent ev{}; ev.kind = NIB_EV_CMDLINE_PLUGIN_MSG; ev.struct_size = sizeof(NibEvent);
    ev.as.cmdline.msg_utf8 = u8.data();
    nibFireEvent(ev);
}
// nib.panels/1 - the frame installs these (they need m_aui + the frame as parent), matching g_coreSciCall.
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
static std::function<intptr_t()>              g_nibDocActiveId;    // v2: stable opaque id of the active document; 0 if none
static std::function<int(intptr_t,char*,int)> g_nibDocPathFromId;  // v2: copy a document id's UTF-8 path -> length, 0 if no such id
static std::function<int()>                   g_nibDocActiveView;  // v3: which view holds the active doc (0=main, 1=sub)
static std::function<int(int,char*,int)>      g_nibDocPathAt;      // v4: UTF-8 path of the open doc at a flat index
// v5: per-view buffer model + per-buffer properties (the frame installs these; each needs the tab list /
// the active view's editor / the page's own fields). Kept NUL-/gate-guarded like every other nib.* seam.
static std::function<int(int)>                    g_nibDocViewCount;      // open docs in a view (0=main, 1=sub)
static std::function<intptr_t(int,int)>           g_nibDocIdAt;           // buffer id at (view, index); 0 if out of range
static std::function<int(intptr_t,int,int*,int*)> g_nibDocPosOf;          // locate an id -> (view, index); 1 if found
static std::function<int(int)>                    g_nibDocIndexOfActive;  // active doc's index in a view; -1 if empty
static std::function<int(int,int)>                g_nibDocActivateAt;     // activate the doc at (view, index)
static std::function<int(intptr_t,int)>           g_nibDocSetLangById;    // force a Language (host cmd id) on a buffer
static std::function<int(intptr_t)>               g_nibDocEncodingGet;    // on-disk encoding enum of a buffer; -1 if none
static std::function<int(intptr_t,int)>           g_nibDocEncodingSet;    // set it (save-to-apply); 1 on success
static std::function<int(intptr_t)>               g_nibDocEolGet;         // EOL mode (SC_EOL_*) of a buffer; -1 if none
static std::function<int(intptr_t,int)>           g_nibDocEolSet;         // set+convert it, marks dirty; 1 on success
static std::function<int(const char*,int)>        g_nibDocSaveActiveAs;   // save active as / a copy of it
static std::function<int(intptr_t)>               g_nibDocSaveById;       // save a (possibly background) buffer by id
static std::function<int()>                       g_nibDocSetDirtyActive; // make the active buffer dirty
static std::function<int(intptr_t,const char*)>   g_nibDocRenameUntitled; // rename an untitled buffer's tab label
static std::function<int(int,int)>                g_nibDocTabColorId;     // tab colour palette slot 0..4, or -1
// Programmatic (no-UI) rename/delete of the active document's on-disk file - the dialog-free cores of
// renameFile()/recycleFile(), so the file-lifecycle events (v4 rename/delete kinds) can be driven without
// a modal file dialog (used by bridge_selftest; also a clean automation seam). Each fires the same
// before/after event pair its menu-driven wrapper does.
static std::function<int(const char*)>            g_nibRenameActive;      // rename active file -> newPath (fires BEFORE_RENAME + RENAMED/CANCEL)
static std::function<int()>                       g_nibRecycleActive;     // delete active file (fires BEFORE_DELETE + DELETED/DELETE_FAILED)
static int nibDocCount(NibHost*)                      { return g_nibDocCount ? g_nibDocCount() : 0; }
static int nibDocActivePath(NibHost*, char* b, int c) { return g_nibDocActivePath ? g_nibDocActivePath(b, c) : 0; }
static int nibDocOpen(NibHost*, const char* p)        { return g_nibDocOpen ? g_nibDocOpen(p) : 0; }
static int nibDocSave(NibHost*)                       { return g_nibDocSave ? g_nibDocSave() : 0; }
static intptr_t nibDocActiveId(NibHost*)                              { return g_nibDocActiveId ? g_nibDocActiveId() : 0; }
static int      nibDocPathFromId(NibHost*, intptr_t id, char* b, int c) { return g_nibDocPathFromId ? g_nibDocPathFromId(id, b, c) : 0; }
static int      nibDocActiveView(NibHost*)                             { return g_nibDocActiveView ? g_nibDocActiveView() : 0; }
static int      nibDocPathAt(NibHost*, int index, char* b, int c)       { return g_nibDocPathAt ? g_nibDocPathAt(index, b, c) : 0; }
static int      nibDocViewCount(NibHost*, int v)                        { return g_nibDocViewCount ? g_nibDocViewCount(v) : 0; }
static intptr_t nibDocIdAt(NibHost*, int v, int i)                      { return g_nibDocIdAt ? g_nibDocIdAt(v, i) : 0; }
static int      nibDocPosOf(NibHost*, intptr_t id, int pv, int* ov, int* oi) { return g_nibDocPosOf ? g_nibDocPosOf(id, pv, ov, oi) : 0; }
static int      nibDocIndexOfActive(NibHost*, int v)                    { return g_nibDocIndexOfActive ? g_nibDocIndexOfActive(v) : -1; }
static int      nibDocActivateAt(NibHost*, int v, int i)                { return g_nibDocActivateAt ? g_nibDocActivateAt(v, i) : 0; }
static int      nibDocSetLangById(NibHost*, intptr_t id, int c)         { return g_nibDocSetLangById ? g_nibDocSetLangById(id, c) : 0; }
static int      nibDocEncodingGet(NibHost*, intptr_t id)               { return g_nibDocEncodingGet ? g_nibDocEncodingGet(id) : -1; }
static int      nibDocEncodingSet(NibHost*, intptr_t id, int e)        { return g_nibDocEncodingSet ? g_nibDocEncodingSet(id, e) : 0; }
static int      nibDocEolGet(NibHost*, intptr_t id)                    { return g_nibDocEolGet ? g_nibDocEolGet(id) : -1; }
static int      nibDocEolSet(NibHost*, intptr_t id, int m)            { return g_nibDocEolSet ? g_nibDocEolSet(id, m) : 0; }
static int      nibDocSaveActiveAs(NibHost*, const char* p, int c)     { return g_nibDocSaveActiveAs ? g_nibDocSaveActiveAs(p, c) : 0; }
static int      nibDocSaveById(NibHost*, intptr_t id)                  { return g_nibDocSaveById ? g_nibDocSaveById(id) : 0; }
static int      nibDocSetDirtyActive(NibHost*)                         { return g_nibDocSetDirtyActive ? g_nibDocSetDirtyActive() : 0; }
static int      nibDocRenameUntitled(NibHost*, intptr_t id, const char* n) { return g_nibDocRenameUntitled ? g_nibDocRenameUntitled(id, n) : 0; }
static int      nibDocTabColorId(NibHost*, int v, int i)               { return g_nibDocTabColorId ? g_nibDocTabColorId(v, i) : -1; }
#ifdef __WXMSW__
// nib.win32/1 - Windows-only native-handle capability (the GPL npp-bridge uses it to rebuild NppData).
static std::function<void*()> g_nibMainWindow, g_nibEditorMain, g_nibEditorSecond, g_nibPluginsMenu;
static std::function<void(void*, const char*, int)> g_nibDockNative;   // host a plugin's native HWND in a dock pane
static std::function<void(void*, bool)>             g_nibShowDock;
static void* nibW32Main(NibHost*)   { return g_nibMainWindow   ? g_nibMainWindow()   : nullptr; }
static void* nibW32EdMain(NibHost*) { return g_nibEditorMain   ? g_nibEditorMain()   : nullptr; }
static void* nibW32EdSec(NibHost*)  { return g_nibEditorSecond ? g_nibEditorSecond() : nullptr; }   // the SUB view's HWND (NULL until split)
static void* nibW32Menu(NibHost*)   { return g_nibPluginsMenu  ? g_nibPluginsMenu()  : nullptr; }
static void  nibW32Dock(NibHost*, void* h, const char* t, int e) { if (g_nibDockNative) g_nibDockNative(h, t, e); }
static void  nibW32ShowDock(NibHost*, void* h, int v)            { if (g_nibShowDock)   g_nibShowDock(h, v != 0); }
static const NibWin32Api g_nibWin32Api = { 1, sizeof(NibWin32Api), nibW32Main, nibW32EdMain, nibW32EdSec, nibW32Menu, nibW32Dock, nibW32ShowDock };
#endif
// nib.langdef/1 - a plugin registers a language by handing the host Scintillua lexer source.
static std::function<int(const char*, const char*, const char*)> g_nibRegisterLanguage;
static int nibLangRegister(NibHost*, const char* name, const char* exts, const char* lua)
{ return (g_nibRegisterLanguage && name && lua) ? g_nibRegisterLanguage(name, exts ? exts : "", lua) : 0; }
static const NibLangDefApi g_nibLangDefApi = { 1, sizeof(NibLangDefApi), nibLangRegister };

// nib.keymap/1 - a plugin contributes keybinding overrides as a named, switchable SCHEME (the optional
// GPL npp-shortcuts-compat plugin uses it to re-add Notepad++ shortcuts after parsing a shortcuts.xml).
// The opaque NibKeymapScheme handed to the plugin IS a host-owned builder that accumulates COPIED strings
// only - never a plugin-side pointer or callback. That is the categorical safety difference from
// nib.events (whose retained raw fn pointers caused the unload UAF/crash - the nib-event-subscriber-uaf +
// nib-plugin-unload-crash memories): a committed scheme is copied into the KeymapStore + persisted to
// disk, and any still-pending builder holds only host memory, so BOTH survive FreeLibrary by construction.
// Menu-command bindings (bind_id / bind_name) resolve to the store's symbolicName space and become a
// KeymapScheme's deltas; editor bindings (bind_editor) resolve a SCI_* id to the curated editor-command
// name of the store's Phase-4 editor tier. The frame installs the six hooks below against its m_keymap.
struct NibKmMenuBind   { wxString sym;  wxString accel; bool unbind = false; bool additional = false; };
struct NibKmEditorBind { wxString name; wxString accel; bool unbind = false; bool additional = false; };
struct NibKeymapBuilder                       // == the opaque NibKeymapScheme* the plugin holds
{
    wxString id, title, parent;
    std::vector<NibKmMenuBind>   menu;        // bind_id / bind_name (id already resolved to a symbolicName)
    std::vector<NibKmEditorBind> editor;      // bind_editor (SCI_* already resolved to an editor-command name)
};
// Uncommitted builders live here, host-owned. commit_scheme / discard_scheme consume one; unloadNibPlugins()
// clears any left pending BEFORE FreeLibrary. (They hold no plugin-side pointers, so this is a leak-guard,
// not a UAF fix - but it keeps the "nothing dangling into an unmapped DLL" invariant total and obvious.)
static std::vector<std::unique_ptr<NibKeymapBuilder>> g_nibKmPendingBuilders;
static void dropNibKmBuilder(NibKeymapBuilder* b)
{
    if (!b) return;
    auto& v = g_nibKmPendingBuilders;
    v.erase(std::remove_if(v.begin(), v.end(),
            [b](const std::unique_ptr<NibKeymapBuilder>& p){ return p.get() == b; }), v.end());
}
// An accelerator string the host will accept: a single-level chord ("ctrl+k ctrl+c" - which
// wxAcceleratorEntry can't round-trip, so accept it verbatim for the store's chord layer) or a spelling
// wxAcceleratorEntry::FromString() parses. Rejecting the unparseable ones here is what lets bind_*()
// return the 0/1 an importer uses to build its "Validate shortcuts.xml" report without any host read API.
static bool nibKmAccelOk(const wxString& accel)
{
    if (accel.empty()) return false;
    if (keySpell::isChord(accel)) return true;
    wxAcceleratorEntry e;
    return e.FromString(accel);
}
// Record one menu-command binding into a pending builder (shared by bind_id after id->symbolicName, and
// bind_name). A NULL/empty accel is an UNBIND (masks the parent/default); otherwise the accel must parse
// or nothing is recorded and 0 is returned. `additional`==0 marks a REPLACE (commit prepends an unbind so
// the scheme delta clears the inherited default before binding - unbind-then-bind); `additional`
// !=0 marks an extra binding (N++ NextKey). Returns 1 on record, 0 on an unparseable accel.
static int nibKmPushMenu(NibKeymapBuilder* b, const wxString& sym, const char* accel, int additional)
{
    if (!b || sym.empty()) return 0;
    const bool unbind = !accel || !*accel;
    wxString a;
    if (!unbind) { a = wxString::FromUTF8(accel); if (!nibKmAccelOk(a)) return 0; }
    b->menu.push_back({ sym, a, unbind, additional != 0 });
    return 1;
}
// The frame installs these against its KeymapStore (m_keymap). begin/discard/commit manage the pending
// builder list; bind_* resolve + validate against the loaded store and push a copied binding into a builder.
static std::function<NibKeymapScheme*(const char*, const char*, const char*)> g_nibKmBegin;
static std::function<int(NibKeymapScheme*, int, const char*, int)>            g_nibKmBindId;
static std::function<int(NibKeymapScheme*, const char*, const char*, int)>    g_nibKmBindName;
static std::function<int(NibKeymapScheme*, int, const char*, int)>            g_nibKmBindEditor;
static std::function<void(NibKeymapScheme*)>                                  g_nibKmDiscard;
static std::function<int(NibKeymapScheme*, int)>                              g_nibKmCommit;
// Thin C trampolines (host/handle/string NUL-guarded) -> the frame hooks. Same shape as nibLangRegister.
static NibKeymapScheme* nibKmBegin(NibHost*, const char* id, const char* title, const char* parent)
{ return g_nibKmBegin ? g_nibKmBegin(id ? id : "", title ? title : "", parent ? parent : "") : nullptr; }
static int nibKmBindId(NibHost*, NibKeymapScheme* s, int cmd_id, const char* accel, int additional)
{ return (g_nibKmBindId && s) ? g_nibKmBindId(s, cmd_id, accel, additional) : 0; }
static int nibKmBindName(NibHost*, NibKeymapScheme* s, const char* name, const char* accel, int additional)
{ return (g_nibKmBindName && s && name) ? g_nibKmBindName(s, name, accel, additional) : 0; }
static int nibKmBindEditor(NibHost*, NibKeymapScheme* s, int sci_command, const char* accel, int additional)
{ return (g_nibKmBindEditor && s) ? g_nibKmBindEditor(s, sci_command, accel, additional) : 0; }
static void nibKmDiscard(NibHost*, NibKeymapScheme* s)
{ if (g_nibKmDiscard && s) g_nibKmDiscard(s); }
static int nibKmCommit(NibHost*, NibKeymapScheme* s, int activate)
{ return (g_nibKmCommit && s) ? g_nibKmCommit(s, activate) : 0; }
// nib.keymap/2 read hook: the effective shortcut bound to a host command id, as portable modifiers + key
// (the frame installs it against its m_keymap; the GPL bridge maps it to the N++ ShortcutKey ABI for
// NPPM_GETSHORTCUTBYCMDID). Read-only, so it stays a plain frame-installed hook like g_nibUiIsDark.
static std::function<int(int, uint32_t*, uint32_t*)> g_nibKmEffectiveShortcut;
static int nibKmEffectiveShortcut(NibHost*, int cmd_id, uint32_t* mods, uint32_t* key)
{ return (g_nibKmEffectiveShortcut && mods && key) ? g_nibKmEffectiveShortcut(cmd_id, mods, key) : 0; }
static const NibKeymapApi g_nibKeymapApi = { 2, sizeof(NibKeymapApi),
    nibKmBegin, nibKmBindId, nibKmBindName, nibKmBindEditor, nibKmDiscard, nibKmCommit,
    nibKmEffectiveShortcut };

// nib.paths/1 - well-known host directories.
static std::function<int(char*, int)> g_nibUserDataDir;
static int nibPathsUserData(NibHost*, char* b, int c) { return g_nibUserDataDir ? g_nibUserDataDir(b, c) : 0; }
static const NibPathsApi g_nibPathsApi = { 1, sizeof(NibPathsApi), nibPathsUserData };

// nib.sci/1 - generic Scintilla passthrough (portable, offered on every OS). Wraps the shared
// coreSciCall tail installed by the frame; the same tail the Win32 HWND bridge uses.
static intptr_t nibSciCall(NibHost*, int view, unsigned msg, uintptr_t w, intptr_t l)
{ return coreSciCall(view, msg, w, l); }
static const NibSciApi g_nibSciApi = { 1, sizeof(NibSciApi), nibSciCall };

// nib.ui/1 - menu checkmarks + dark-mode probe/palette. The frame installs the three hooks (they need
// menuBar()/m_dark/m_theme); the C trampolines below stay NUL-guarded like every other nib.* surface.
static std::function<int(int, bool)>          g_nibUiMenuCheck;
static std::function<int()>                   g_nibUiIsDark;
static std::function<int(NibUiDarkColors*)>   g_nibUiDarkColors;
static int nibUiMenuCheck(NibHost*, int id, int on)        { return g_nibUiMenuCheck ? g_nibUiMenuCheck(id, on != 0) : 0; }
static int nibUiIsDark(NibHost*)                           { return g_nibUiIsDark ? g_nibUiIsDark() : 0; }
static int nibUiDarkColorsFn(NibHost*, NibUiDarkColors* c) { return (g_nibUiDarkColors && c) ? g_nibUiDarkColors(c) : 0; }
// nib.ui/2 additions: host chrome visibility (toolbar/menubar/statusbar/doclist) + editor/UI state
// getters (line-number width policy, auto-indent, macro state, icon set, locale). Frame-installed hooks;
// the trampolines stay NUL-guarded so a late call after teardown degrades to the documented 0 return.
static std::function<int(int)>                g_nibUiChromeGet;
static std::function<int(int, int)>           g_nibUiChromeSet;
static std::function<int()>                   g_nibUiWidthModeGet;
static std::function<int(int)>                g_nibUiWidthModeSet;
static std::function<int()>                   g_nibUiAutoIndent;
static std::function<int()>                   g_nibUiMacroState;
static std::function<int()>                   g_nibUiIconSet;
static std::function<int(char*, int)>         g_nibUiLocaleName;
static int nibUiChromeGet(NibHost*, int which)          { return g_nibUiChromeGet ? g_nibUiChromeGet(which) : 0; }
static int nibUiChromeSet(NibHost*, int which, int vis) { return g_nibUiChromeSet ? g_nibUiChromeSet(which, vis) : 0; }
static int nibUiWidthModeGet(NibHost*)                  { return g_nibUiWidthModeGet ? g_nibUiWidthModeGet() : 0; }
static int nibUiWidthModeSet(NibHost*, int mode)        { return g_nibUiWidthModeSet ? g_nibUiWidthModeSet(mode) : 0; }
static int nibUiAutoIndentOn(NibHost*)                  { return g_nibUiAutoIndent ? g_nibUiAutoIndent() : 0; }
static int nibUiMacroState(NibHost*)                    { return g_nibUiMacroState ? g_nibUiMacroState() : 0; }
static int nibUiIconSetChoice(NibHost*)                 { return g_nibUiIconSet ? g_nibUiIconSet() : 0; }
static int nibUiLocaleName(NibHost*, char* b, int c)    { return g_nibUiLocaleName ? g_nibUiLocaleName(b, c) : 0; }
static const NibUiApi g_nibUiApi = { 2, sizeof(NibUiApi), nibUiMenuCheck, nibUiIsDark, nibUiDarkColorsFn,
    nibUiChromeGet, nibUiChromeSet, nibUiWidthModeGet, nibUiWidthModeSet, nibUiAutoIndentOn, nibUiMacroState,
    nibUiIconSetChoice, nibUiLocaleName };

// nib.session/1 - save/load/enumerate session files BY PATH (the GPL npp-bridge serves the Notepad++
// *SESSION* messages from it). The frame installs the hooks against its own session reader/writer.
static std::function<int(const char*)>                          g_nibSessSaveCurrent;
static std::function<int(const char*, const char* const*, int)> g_nibSessSaveFiles;
static std::function<int(const char*)>                          g_nibSessLoad;
static std::function<int(const char*, int*)>                    g_nibSessFileCount;
static std::function<int(const char*, int, char*, int)>         g_nibSessFileAt;
static int nibSessSaveCurrent(NibHost*, const char* p)          { return (g_nibSessSaveCurrent && p) ? g_nibSessSaveCurrent(p) : 0; }
static int nibSessSaveFiles(NibHost*, const char* p, const char* const* f, int n) { return (g_nibSessSaveFiles && p) ? g_nibSessSaveFiles(p, f, n) : 0; }
static int nibSessLoad(NibHost*, const char* p)                 { return (g_nibSessLoad && p) ? g_nibSessLoad(p) : 0; }
static int nibSessFileCount(NibHost*, const char* p, int* v)    { return (g_nibSessFileCount && p) ? g_nibSessFileCount(p, v) : 0; }
static int nibSessFileAt(NibHost*, const char* p, int i, char* b, int c) { return (g_nibSessFileAt && p) ? g_nibSessFileAt(p, i, b, c) : 0; }
static const NibSessionApi g_nibSessionApi = { 1, sizeof(NibSessionApi),
    nibSessSaveCurrent, nibSessSaveFiles, nibSessLoad, nibSessFileCount, nibSessFileAt };

// nib.lexer/1 - create a Lexilla ILexer by name + count user-registered languages (portable: Lexilla is
// built into the host on every OS, so the GPL bridge can serve NPPM_CREATELEXER for a recompiled plugin).
static std::function<intptr_t(const char*)> g_nibLexerCreate;
static std::function<int()>                 g_nibLexerUserLangCount;
static intptr_t nibLexerCreate(NibHost*, const char* n) { return (g_nibLexerCreate && n) ? g_nibLexerCreate(n) : 0; }
static int nibLexerUserLangCount(NibHost*)              { return g_nibLexerUserLangCount ? g_nibLexerUserLangCount() : 0; }
static const NibLexerApi g_nibLexerApi = { 1, sizeof(NibLexerApi), nibLexerCreate, nibLexerUserLangCount };

// nib.toolbar/1 - plugin toolbar buttons. add_tool copies the RGBA pixels + tooltip immediately (no
// plugin memory retained); the button's command id dispatches through onCommand exactly like a menu
// click (wx-event path - never raw WM_COMMAND, see the & 0xFFFF notes there). g_nibToolbarRemoveAll is
// the teardown the frame installs: unloadNibPlugins()/onCloseWindow run it BEFORE FreeLibrary so no
// button whose command targets a plugin outlives that plugin's mapped image.
static std::function<int(int, const NibToolbarIcon*, const char*)> g_nibToolbarAddTool;
static std::function<void()>                                       g_nibToolbarRemoveAll;
static int nibToolbarAddTool(NibHost*, int cmdId, const NibToolbarIcon* icon, const char* tip)
{ return (g_nibToolbarAddTool && icon) ? g_nibToolbarAddTool(cmdId, icon, tip ? tip : "") : 0; }
static const NibToolbarApi g_nibToolbarApi = { 1, sizeof(NibToolbarApi), nibToolbarAddTool };

// nib.alloc/1 - host-owned dynamic ranges of the shared number spaces. Reserved-number audit (what the
// grants below are provably disjoint from):
//   * command ids - frozen kCmd*/IDM_* menu ids 1001..50011 (src/command_ids.h), wx stock ids (< 6000,
//     incl. wxID_FILE1..9 MRU), myID_TIMER block 60000..60005, UI-language radios 60100..60109,
//     Open-Containing-Folder tools 60300+, doc-list dropdown 61000..61999, saved macros 62100+,
//     Nib plugin commands NIB_CMD_BASE 63000..63499, Scintillua language menu 63500..63999.
//     Pool: 64000..64999 - and < 65536 on purpose, so a granted id survives the 16-bit WM_COMMAND
//     path unwrapped (see MSWWindowProc / onCommand's & 0xFFFF; ids > 32767 arrive sign-wrapped and
//     are recovered by that mask, exactly like the kCmd* range).
//   * markers (Scintilla 0..31) - MARK_BOOKMARK = 2 (main.cpp:160), fold chrome SC_MARKNUM_* 25..31
//     (setupFolding); 21..24 stay clear for Scintilla's change-history markers (SC_MARKNUM_HISTORY_*,
//     inert under the wx-bundled 5.0 runtime but reserved so grants survive a Scintilla upgrade).
//     Pool: 3..20 (0 and 1 also stay host-reserved for future chrome).
//   * indicators - 0..7 belong to lexers (Scintilla's INDICATOR_CONTAINER starts at 8; 8 stays
//     host-reserved as headroom), MARK_INDIC = 9, SMART_INDIC = 10, URL_INDIC = 11, and the five
//     "Mark All Ext" styles MARK_STYLE_BASE 21..25 (main.cpp:161-165). Pools: 12..20, then 26..31 -
//     each grant is contiguous, so a request that doesn't fit the first pool's remainder is served
//     whole from the next.
// Grants are process-lifetime and never recycled (a stale number from an unloaded plugin must never
// be re-granted to another - same reasoning as N++'s allocators).
static const int NIB_ALLOC_CMD_FIRST = 64000, NIB_ALLOC_CMD_LAST = 64999;
static int g_nibAllocNextCmd    = NIB_ALLOC_CMD_FIRST;
static int g_nibAllocNextMarker = 3;                       // marker pool: 3..20
struct NibAllocSink { NibAllocCommandFn fn; void* user; };
static std::vector<NibAllocSink> g_nibAllocSinks;          // raw plugin fn pointers - cleared BEFORE FreeLibrary
static int nibAllocCmdIds(NibHost*, int n, int* first)
{
    if (n <= 0 || !first || g_nibAllocNextCmd + n - 1 > NIB_ALLOC_CMD_LAST) return 0;
    *first = g_nibAllocNextCmd; g_nibAllocNextCmd += n; return 1;
}
static int nibAllocMarkers(NibHost*, int n, int* first)
{
    if (n <= 0 || !first || g_nibAllocNextMarker + n - 1 > 20) return 0;
    *first = g_nibAllocNextMarker; g_nibAllocNextMarker += n; return 1;
}
static int nibAllocIndicators(NibHost*, int n, int* first)
{
    static int next[2] = { 12, 26 };                       // indicator pools: 12..20, 26..31
    static const int hi[2] = { 20, 31 };
    if (n <= 0 || !first) return 0;
    for (int p = 0; p < 2; ++p)
        if (next[p] + n - 1 <= hi[p]) { *first = next[p]; next[p] += n; return 1; }
    return 0;
}
static void nibAllocOnCommand(NibHost*, NibAllocCommandFn fn, void* u)
{ if (fn) g_nibAllocSinks.push_back({ fn, u }); }
static const NibAllocApi g_nibAllocApi = { 1, sizeof(NibAllocApi),
    nibAllocCmdIds, nibAllocMarkers, nibAllocIndicators, nibAllocOnCommand };

// nib.host/1
static const char* nibHostName(NibHost*) { return "wxNote"; }
static uint32_t     nibHostAbi(NibHost*) { return NIB_ABI_VERSION; }

static const NibHostApi     g_nibHostApi     = { 1, sizeof(NibHostApi),     nibHostName, nibHostAbi };
static const NibEditorApi   g_nibEditorApi   = { 1, sizeof(NibEditorApi),   nibEdLength, nibEdInsert, nibEdReplSel, nibEdSelStart, nibEdSelEnd, nibEdGetText };
static const NibCommandsApi g_nibCommandsApi = { 2, sizeof(NibCommandsApi), nibCmdRegister, nibInvokeCommand };
static const NibEventsApi   g_nibEventsApi   = { 1, sizeof(NibEventsApi),   nibSubscribe,   nullptr };            // v1: savepoint-derived DOCUMENT_SAVED
static const NibEventsApi   g_nibEventsApiV2 = { 2, sizeof(NibEventsApi),   nibSubscribeV2, nullptr };            // v2: id-carrying SAVING/SAVED/BEFORE_OPEN
static const NibEventsApi   g_nibEventsApiV3 = { 3, sizeof(NibEventsApi),   nibSubscribeV2, nibSetModifiedMask }; // v3: + modified-mask, language/style/shortcut/shutdown kinds
static const NibEventsApi   g_nibEventsApiV4 = { 4, sizeof(NibEventsApi),   nibSubscribeV2, nibSetModifiedMask }; // v4: + long-tail file lifecycle (rename/delete/order/readonly/before-load) + cmdline plugin message
static const NibPanelsApi   g_nibPanelsApi   = { 1, sizeof(NibPanelsApi),   nibPanelRegister, nibPanelSetText, nibPanelAppend, nibPanelShow };
static const NibDocumentsApi g_nibDocumentsApi = { 5, sizeof(NibDocumentsApi), nibDocCount, nibDocActivePath, nibDocOpen, nibDocSave, nibDocActiveId, nibDocPathFromId, nibDocActiveView, nibDocPathAt,
    nibDocViewCount, nibDocIdAt, nibDocPosOf, nibDocIndexOfActive, nibDocActivateAt, nibDocSetLangById,
    nibDocEncodingGet, nibDocEncodingSet, nibDocEolGet, nibDocEolSet, nibDocSaveActiveAs, nibDocSaveById,
    nibDocSetDirtyActive, nibDocRenameUntitled, nibDocTabColorId };

static const void* nibQuery(NibHost*, const char* iface, uint32_t minv)
{
    if (!iface) return nullptr;
    if (minv <= 1 && std::strcmp(iface, NIB_IFACE_HOST)     == 0) return &g_nibHostApi;
    if (minv <= 1 && std::strcmp(iface, NIB_IFACE_EDITOR)   == 0) return &g_nibEditorApi;
    if (minv <= 5 && std::strcmp(iface, NIB_IFACE_DOCUMENTS)== 0) return &g_nibDocumentsApi;   // grow-in-place: one struct, caller checks ->version
    if (minv <= 1 && std::strcmp(iface, NIB_IFACE_COMMANDS) == 0) return &g_nibCommandsApi;
    if (std::strcmp(iface, NIB_IFACE_EVENTS) == 0)   // version-gated: which table a plugin gets decides
    {                                                // its DOCUMENT_SAVED semantics (see nib.h)
        if (minv <= 1) return &g_nibEventsApi;
        if (minv == 2) return &g_nibEventsApiV2;
        if (minv == 3) return &g_nibEventsApiV3;   // + set_modified_mask + v3 event kinds
        if (minv == 4) return &g_nibEventsApiV4;   // + v4 long-tail file-lifecycle + cmdline plugin message kinds
        return nullptr;
    }
    if (minv <= 1 && std::strcmp(iface, NIB_IFACE_PANELS)   == 0) return &g_nibPanelsApi;
    if (minv <= 1 && std::strcmp(iface, NIB_IFACE_LANGDEF)  == 0) return &g_nibLangDefApi;
    if (minv <= 2 && std::strcmp(iface, NIB_IFACE_KEYMAP)   == 0) return &g_nibKeymapApi;   // grow-in-place: caller checks ->version for the v2 read hook
    if (minv <= 1 && std::strcmp(iface, NIB_IFACE_PATHS)    == 0) return &g_nibPathsApi;
    if (minv <= 1 && std::strcmp(iface, NIB_IFACE_SCI)      == 0) return &g_nibSciApi;   // portable: every OS
    if (minv <= 2 && std::strcmp(iface, NIB_IFACE_UI)       == 0) return &g_nibUiApi;   // grow-in-place: caller checks ->version for the v2 chrome/state getters
    if (minv <= 1 && std::strcmp(iface, NIB_IFACE_TOOLBAR)  == 0) return &g_nibToolbarApi;
    if (minv <= 1 && std::strcmp(iface, NIB_IFACE_ALLOC)    == 0) return &g_nibAllocApi;
    if (minv <= 1 && std::strcmp(iface, NIB_IFACE_SESSION)  == 0) return &g_nibSessionApi;
    if (minv <= 1 && std::strcmp(iface, NIB_IFACE_LEXER)    == 0) return &g_nibLexerApi;
#ifdef __WXMSW__
    if (minv <= 1 && std::strcmp(iface, NIB_IFACE_WIN32)    == 0) return &g_nibWin32Api;
#endif
    return nullptr;
}
static void nibLog(NibHost*, int, const char* msg) { if (msg) wxLogDebug("[nib] %s", msg); }

// Copy a UTF-8 string into a (buf, cap) out-param (NUL-terminated if it fits); returns the byte
// length excluding the NUL. The shared shape of the nib.* "give me a path/dir" callbacks.
static int nibCopyUtf8(const std::string& s, char* b, int c)
{
    if (b && c > 0) { int n = static_cast<int>(s.size()); if (n > c - 1) n = c - 1; std::memcpy(b, s.data(), static_cast<size_t>(n)); b[n] = 0; }
    return static_cast<int>(s.size());
}

// Load Nib plugins from <exe>/nib/ via wxDynamicLibrary (portable: .dll / .so / .dylib).
static void loadNibPlugins()
{
    const wxString dir = wxPathOnly(wxStandardPaths::Get().GetExecutablePath()) + wxFILE_SEP_PATH + "nib";
    if (!wxDirExists(dir)) return;
#if defined(__WXMSW__)
    const std::vector<wxString> pats = { "*.dll" };
#elif defined(__WXMAC__)
    // CMake MODULE libraries are emitted as .so even on macOS (only SHARED libs get .dylib), so
    // accept BOTH - otherwise every bin/nib/*.so (the bridge, udl-compat, ...) silently never loads.
    const std::vector<wxString> pats = { "*.dylib", "*.so" };
#else
    const std::vector<wxString> pats = { "*.so" };
#endif
    wxDir d(dir); wxString f;
    for (const wxString& pat : pats)
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
    // Drop callbacks into the DLLs we're about to unload first: g_nibSubs/g_nibCommands hold raw function
    // pointers (and, for commands, user-data pointers) that live inside each plugin's mapped image. Editor
    // events (text/selection/savepoint/doc-activated) can still fire while the frame's children are torn
    // down after this call returns, and nibFireEvent() would otherwise call straight into unmapped memory.
    g_nibSubs.clear();
    g_nibModifiedMask = 0;     // no subscriber left to arm NIB_EV_TEXT_MODIFIED - restore the perf default (never fire it)
    g_nibCommands.clear();
    g_nibAllocSinks.clear();   // nib.alloc command sinks: raw plugin fn pointers, same hazard class as g_nibSubs
    // Remove any plugin toolbar buttons BEFORE FreeLibrary, so no button whose command id targets a
    // plugin outlives that plugin's mapped image. At app shutdown this hook is already run (and nulled)
    // by onCloseWindow while the toolbar widget still exists - this call covers any non-shutdown unload.
    if (g_nibToolbarRemoveAll) { g_nibToolbarRemoveAll(); g_nibToolbarRemoveAll = nullptr; }
    // Free any still-pending nib.keymap builders (a plugin that began a scheme but never committed/discarded
    // it - e.g. it aborted on a parse error, or is being unloaded mid-import). Unlike g_nibSubs/g_nibCommands
    // above these hold NO plugin-side pointers (only copied strings), so this is a leak-guard, not a UAF fix -
    // but it keeps "nothing referencing a DLL we're about to unmap" total. COMMITTED schemes are deliberately
    // NOT torn down here: they were copied into the KeymapStore + persisted to shortcuts.json, so they are
    // host-owned data that OUTLIVES the plugin - the exact property nib.events lacked.
    g_nibKmPendingBuilders.clear();
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
    // Per-tab colour. With pinned tabs the flat art calls DrawPageTab (NOT the legacy DrawTab), so we
    // hook here: draw the normal tab, then lay a translucent colour wash over its body so the label still reads.
    int DrawPageTab(wxDC& dc, wxWindow* wnd, wxAuiNotebookPage& page, const wxRect& rect) override
    {
        const int extent = wxAuiDefaultTabArt::DrawPageTab(dc, wnd, page, rect);
        auto* ep = static_cast<EditorPage*>(page.window);   // every editor notebook page is an EditorPage
        if (ep && ep->tabColour.IsOk())
        {
            if (wxGraphicsContext* gc = wxGraphicsContext::CreateFromUnknownDC(dc))
            {
                const wxColour& c = ep->tabColour;
                gc->SetBrush(wxBrush(wxColour(c.Red(), c.Green(), c.Blue(), 140)));   // ~55% alpha: clearly coloured, label legible
                gc->SetPen(*wxTRANSPARENT_PEN);
                const int w = (extent > 0 && extent <= rect.width) ? extent : rect.width;
                gc->DrawRectangle(rect.x, rect.y + 2, w, rect.height - 2);   // start just under the active-tab marker - covers the white separator row so no untinted outline shows
                delete gc;
            }
        }
        // Active tab's top-edge marker, in the project's accent green over the system-blue one the base
        // class just drew. Shared with the terminal's tab strip (drawActiveTabMarker, terminal_panel.h)
        // so the two strips can't drift apart - it also carries the `extent`-vs-page.rect and the
        // FromDIP-height fixes, which this copy predates.
        if (page.active) drawActiveTabMarker(dc, wnd, page.rect, rect);
        return extent;
    }
private:
    wxColour m_iconColour;
    wxBitmapBundle iconBundle(const wxString& name, int px, const wxColour& colour) const   // resources/icons/<name>.svg, recoloured to the given colour
    {
        const wxString path = wxPathOnly(wxStandardPaths::Get().GetExecutablePath()) + wxFILE_SEP_PATH + "icons" + wxFILE_SEP_PATH + name + ".svg";
        if (!wxFileExists(path)) return wxBitmapBundle();
        wxFile f(path); wxString svg;
        if (!f.IsOpened() || !f.ReadAll(&svg)) return wxBitmapBundle();
        svg.Replace("currentColor", colour.GetAsString(wxC2S_HTML_SYNTAX));
        return wxBitmapBundle::FromSVG(svg.utf8_str().data(), wxSize(px, px));
    }
    void reskin()
    {
        const wxBitmapBundle pin = iconBundle("pin", 11, m_iconColour);   // small secondary button (close stays 16)
        if (pin.IsOk()) { m_activePinBmp = pin; m_activeUnpinBmp = pin; m_disabledPinBmp = pin; m_disabledUnpinBmp = pin; }
        const wxBitmapBundle cls = iconBundle("close-tab", 12, m_iconColour);   // match the tab bar's (smaller) global close button
        if (cls.IsOk()) { m_activeCloseBmp = cls; m_disabledCloseBmp = cls; }
    }
};

// The integrated title bar's minimize/maximize/close buttons, non-Windows only (see buildIntegratedTitleBar).
// A plain wxButton fights its own native widget on GTK: wx-on-GTK maps wxButton onto a real GtkButton
// rendered by the GTK CSS theme engine, which independently drives its own :hover/:prelight pseudo-class
// repaint on pointer-enter/leave through its own invalidate cycle - racing this app's own explicit
// SetBackgroundColour()+Refresh() call on the same enter/leave events. Two uncoordinated repaint passes on
// the same widget is what produces the "hover highlight fills half the button, then catches up a moment
// later" artifact. Painting the button entirely by hand removes the native GtkButton (and its independent
// repaint cycle) from the picture - there is nothing left for wx's own Refresh() to race against.
class TitleBarBtn : public wxWindow
{
public:
    TitleBarBtn(wxWindow* parent, wxWindowID id, const wxSize& size, const wxBitmapBundle& glyph, const wxColour& bg, const wxColour& hotBg, int glyphPx = 12)
        : wxWindow(parent, id, wxDefaultPosition, size, wxBORDER_NONE), m_glyph(glyph), m_bg(bg), m_hotBg(hotBg), m_glyphPx(glyphPx)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);   // we paint the whole background ourselves - no native erase
        Bind(wxEVT_PAINT, &TitleBarBtn::onPaint, this);
        Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& e) { m_hot = true;  Refresh(); e.Skip(); });
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e) { m_hot = false; Refresh(); e.Skip(); });
        Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) {
            wxCommandEvent evt(wxEVT_BUTTON, GetId());
            evt.SetEventObject(this);
            ProcessWindowEvent(evt);
        });
    }
    void SetGlyph(const wxBitmapBundle& glyph) { m_glyph = glyph; Refresh(); }

private:
    void onPaint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBrush(wxBrush(m_hot ? m_hotBg : m_bg));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(wxPoint(0, 0), GetClientSize());
        const wxBitmap bmp = m_glyph.GetBitmap(FromDIP(wxSize(m_glyphPx, m_glyphPx)));
        const wxSize sz = GetClientSize();
        dc.DrawBitmap(bmp, (sz.x - bmp.GetWidth()) / 2, (sz.y - bmp.GetHeight()) / 2, true);
    }
    wxBitmapBundle m_glyph;
    wxColour m_bg, m_hotBg;
    int m_glyphPx = 12;   // glyph render size (px, DIP-scaled): 12 for window controls, 16 for the tab caption +/v/x
    bool m_hot = false;
};

// File > Print (Ctrl+P) / Print Now: paginate + render the active document through Scintilla's
// FormatRange (wx's cross-platform wrapper around SCI_FORMATRANGE). Follows the pattern from wxWidgets' own
// samples/stc/edit.cpp (EditPrint) - measure page breaks with a dry (non-drawing) pass over the whole
// document, then draw just the one page's range when the print system asks for it.
class SciPrintout : public wxPrintout
{
public:
    // header/footer are templates with $(CURRENT_PRINTING_PAGE)/$(TOTAL_PRINTING_PAGES) still literal - every
    // other macro ($(FULL_PATH) etc.) is resolved by the caller first, since those don't depend on pagination.
    SciPrintout(wxStyledTextCtrl* stc, const wxString& title, const wxPageSetupDialogData& pageSetup,
                const wxString& header = wxString(), const wxString& footer = wxString())
        : wxPrintout(title), m_stc(stc), m_pageSetup(pageSetup), m_header(header), m_footer(footer)
    {
        // Print on clean white paper, not the on-screen editor theme: a dark editor theme would otherwise
        // paint the document lines with its dark background (the "black stripe" behind printed text). A dark
        // theme prints plain black-on-white (its light syntax colours would be invisible on white, and
        // inverting the background only yields a grey stripe, not white); a light theme keeps its syntax
        // colours but forces every background white. SetPrintColourMode only affects SCI_FORMATRANGE, never
        // the on-screen view.
        const wxColour bg = stc->StyleGetBackground(wxSTC_STYLE_DEFAULT);
        const bool darkEditor = (bg.Red() * 299 + bg.Green() * 587 + bg.Blue() * 114) < 128 * 1000;
        stc->SetPrintColourMode(darkEditor ? wxSTC_PRINT_BLACKONWHITE : wxSTC_PRINT_COLOURONWHITE);
    }

    bool OnPrintPage(int page) override
    {
        if (page < 1 || page > (int)m_pageEnds.size()) return false;   // out of the range GetPageInfo/HasPage advertised
        wxDC* dc = GetDC();
        if (!dc) return false;
        scaleDC(dc);
        m_stc->FormatRange(true, page == 1 ? 0 : m_pageEnds[page - 2], m_pageEnds[page - 1], dc, dc, m_printRect, m_pageRect);
        if (!m_header.empty() || !m_footer.empty())
        {
            dc->SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
            dc->SetTextForeground(*wxBLACK);
            const int total = (int)m_pageEnds.size();
            if (!m_header.empty()) dc->DrawText(resolvePageMacros(m_header, page, total), m_printRect.x, m_printRect.y - kBandHeight);
            if (!m_footer.empty()) dc->DrawText(resolvePageMacros(m_footer, page, total), m_printRect.x, m_printRect.y + m_printRect.height);
        }
        return true;
    }

    void GetPageInfo(int* minPage, int* maxPage, int* pageFrom, int* pageTo) override
    {
        *minPage = *maxPage = *pageFrom = *pageTo = 0;
        wxDC* dc = GetDC();
        if (!dc) return;
        scaleDC(dc);

        wxSize ppiScr; GetPPIScreen(&ppiScr.x, &ppiScr.y);
        wxSize page = m_pageSetup.GetPaperSize();
        page.x = wxRound(page.x * ppiScr.x / 25.4);
        page.y = wxRound(page.y * ppiScr.y / 25.4);
        if (m_pageSetup.GetPrintData().GetOrientation() == wxLANDSCAPE) wxSwap(page.x, page.y);
        m_pageRect = wxRect(0, 0, page.x, page.y);

        const wxPoint tl = m_pageSetup.GetMarginTopLeft(), br = m_pageSetup.GetMarginBottomRight();
        const int left = wxRound(tl.x * ppiScr.x / 25.4), top = wxRound(tl.y * ppiScr.y / 25.4);
        const int right = wxRound(br.x * ppiScr.x / 25.4), bottom = wxRound(br.y * ppiScr.y / 25.4);
        int bodyTop = top, bodyBottom = page.y - bottom;
        if (!m_header.empty()) bodyTop += kBandHeight;   // reserve a text-height band just inside each margin
        if (!m_footer.empty()) bodyBottom -= kBandHeight;
        m_printRect = wxRect(left, bodyTop, page.x - left - right, bodyBottom - bodyTop);

        m_pageEnds.clear();
        int pos = 0;
        const int len = m_stc->GetLength();
        while (pos < len)
        {
            const int next = m_stc->FormatRange(false, pos, len, dc, dc, m_printRect, m_pageRect);
            if (next <= pos) break;   // safety: a page that fits nothing would loop forever
            m_pageEnds.push_back(next);
            pos = next;
        }
        *maxPage = (int)m_pageEnds.size();
        *minPage = *maxPage > 0 ? 1 : 0;
        *pageFrom = *minPage; *pageTo = *maxPage;
    }

    bool HasPage(int page) override { return page >= 1 && page <= (int)m_pageEnds.size(); }

private:
    static constexpr int kBandHeight = 24;   // logical px reserved for header/footer text, just inside the page margin
    static wxString resolvePageMacros(const wxString& s, int page, int total)
    {
        wxString r = s;
        r.Replace("$(CURRENT_PRINTING_PAGE)", wxString::Format("%d", page));
        r.Replace("$(TOTAL_PRINTING_PAGES)", wxString::Format("%d", total));
        return r;
    }
    void scaleDC(wxDC* dc)   // map screen-DPI page geometry onto the target DC (printer or preview) so margins/paper size stay physically correct
    {
        wxSize ppiScr; GetPPIScreen(&ppiScr.x, &ppiScr.y);
        if (ppiScr.x == 0) ppiScr.x = ppiScr.y = 96;
        wxSize ppiPrt; GetPPIPrinter(&ppiPrt.x, &ppiPrt.y);
        if (ppiPrt.x == 0) ppiPrt = ppiScr;
        const wxSize dcSize = dc->GetSize();
        wxSize pageSize; GetPageSizePixels(&pageSize.x, &pageSize.y);
        if (pageSize.x <= 0 || pageSize.y <= 0) return;   // guard: a bogus (0,0) page size would divide-by-zero below
        dc->SetUserScale((double)(ppiPrt.x * dcSize.x) / (double)(ppiScr.x * pageSize.x),
                          (double)(ppiPrt.y * dcSize.y) / (double)(ppiScr.y * pageSize.y));
    }

    wxStyledTextCtrl*      m_stc;
    wxPageSetupDialogData  m_pageSetup;
    wxString               m_header, m_footer;
    std::vector<int>       m_pageEnds;   // m_pageEnds[i] = the char position where page i+1 ends (exclusive)
    wxRect                 m_pageRect, m_printRect;
};

// wxNote's Print Preview frame. Same as wxPreviewFrame but its control bar is built WITHOUT the editable
// page field (wxPREVIEW_GOTO): a native single-line edit can't align cleanly among the flat toolbar icons
// (its box and its digit can't both sit on the shared midline, and its dark-theme border can't be stripped),
// so stylePreviewWindow() adds a centred static "cur / total" readout instead. Everything else is stock.
class WxnPreviewFrame : public wxPreviewFrame
{
public:
    WxnPreviewFrame(wxPrintPreviewBase* preview, wxWindow* parent, const wxString& title,
                    const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize)
        : wxPreviewFrame(preview, parent, title, pos, size) {}
    void CreateControlBar() override
    {
        m_controlBar = new wxPreviewControlBar(
            m_printPreview,
            wxPREVIEW_PRINT | wxPREVIEW_FIRST | wxPREVIEW_PREVIOUS | wxPREVIEW_NEXT | wxPREVIEW_LAST | wxPREVIEW_ZOOM,
            this);
        m_controlBar->CreateButtons();
    }
};

// The shell frame is a template on its chrome base so the same ~3300 lines work whether the base is the
// native wxFrame or the borderless wxBorderlessFrame. The base is chosen at startup (restart-to-apply)
// via the two aliases defined just after the class. (Two-phase lookup: MSVC is permissive, but GCC needs
// the inherited wxFrame methods brought into scope - see the `using FB::` block below.)
template <class FB> class WxnShellFrameT : public FB
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
    using FB::SetExtraStyle; using FB::GetContentScaleFactor; using FB::ProcessWindowEvent; using FB::CallAfter;

    // True when the chrome base is the borderless frame (integrated top bar), false for native wxFrame.
    // Compile-time, so the borderless-only branches below are `if constexpr` and never instantiated for
    // the native frame (and never reference wxBorderlessFrame symbols on macOS, where the lib is absent).
#ifdef WXN_HAS_BORDERLESS
    static constexpr bool kBorderless = std::is_base_of<wxBorderlessFrameBase, FB>::value;
    static constexpr int  kTitleBarH  = 30;   // height (px) of the integrated top bar
    // Window-control glyphs drawn (not text): crisp + correctly sized, in a 10x10 box. Restore is the
    // canonical two-overlapping-squares; maximize is a single square.
    static constexpr const char* GLYPH_MIN     = "M1 6 H11";                          // ~10px line, centered (matches native)
    static constexpr const char* GLYPH_MAX     = "M1 1 H11 V11 H1 Z";                 // ~10px square
    static constexpr const char* GLYPH_RESTORE = "M1 4 H8 V11 H1 Z M4 4 V1 H11 V8 H8"; // two overlapping squares
    static constexpr const char* GLYPH_CLOSE   = "M1 1 L11 11 M11 1 L1 11";           // ~10px X
#else
    static constexpr bool kBorderless = false;
#endif

    // Keyboard focus scope, consumed by refreshAccelerators(). Phase 0 of the shortcut work:
    // when focus is inside the terminal panel the frame's
    // accelerator table must NOT carry the terminal-critical keys (Ctrl+C etc.), or MSW's accelerator
    // translation steals them from the focused TermView before it can turn Ctrl+C into a SIGINT. Later
    // phases grow this into KeyScope { Global, Editor, Terminal }; for now Editor = the full table,
    // Terminal = an empty table.
    enum class Scope { Editor, Terminal };

    explicit WxnShellFrameT(bool dark)
        : FB(nullptr, wxID_ANY, "new 1 - wxNote", wxDefaultPosition, wxSize(1100, 720)),
          m_timer(this, myID_TIMER)
    {
#ifdef __WXMAC__
        // Clamp the initial window to the current display's usable area so it can never open larger than the
        // screen. A macOS laptop's usable width (NSScreen visibleFrame - screen minus menu bar and Dock) is
        // ~1016px, narrower than the 1100px default, so the frame would otherwise be created partly off-screen.
        // Nudge to a margin-friendly default first, then shrink-clamp to be safe on any small screen. macOS-only,
        // so Windows/Linux keep the 1100x720 default byte-for-byte. The frame's create-time position is already a
        // concrete on-screen point (wxNonOwnedWindow centres wxDefaultPosition frames), so GetFromPoint() resolves
        // here even though Show() (in OnInit) hasn't run yet; the primary-display fallback is a defensive backstop.
        {
            SetSize(wxSize(1040, 700));
            int dispIdx = wxDisplay::GetFromPoint(GetPosition());
            if (dispIdx == wxNOT_FOUND) dispIdx = 0;
            const wxRect area = wxDisplay(static_cast<unsigned>(dispIdx)).GetClientArea();
            const wxSize cur  = GetSize();
            const int margin  = 24;
            const int w = wxMin(cur.GetWidth(),  area.GetWidth()  - margin);
            const int h = wxMin(cur.GetHeight(), area.GetHeight() - margin);
            if (w < cur.GetWidth() || h < cur.GetHeight()) { SetSize(wxSize(w, h)); Centre(); }
        }
        SetTitle(wxString());   // macOS: clean/blank native title bar (see setWindowTitle) - the constructor seeded a literal for the other platforms
        // SetTitle("") clears the text but the clean look shouldn't hinge on that one call surviving every
        // wx/AppKit title reassertion; hide the title outright. Deferred so the NSWindow peer is realized
        // (Show has run) before we reach for it via MacGetTopLevelWindowRef().
        // this-> is required: MacGetTopLevelWindowRef() lives in the dependent template base (FB -> wxWindowMac)
        // and isn't in this class's `using FB::...` list, so Clang's two-phase lookup rejects the unqualified name.
        // (Also the integrated-top-bar hook: by the time this lambda runs, loadSettings()/buildToolBar()
        // below have populated m_integratedBar and m_macToolbarRowH, and Show() has realized the NSWindow.)
        CallAfter([this]{
            wxn_HideWindowTitle((void*)this->MacGetTopLevelWindowRef());
            if (m_integratedBar && m_macToolbarRowH > 0)
            {
                const int inset = wxn_InlineTrafficLights((void*)this->MacGetTopLevelWindowRef(), m_macToolbarRowH);
                if (inset > 0 && m_macInsetItem && m_toolBarHost)
                { m_macInsetItem->AssignSpacer(inset, -1); m_toolBarHost->Layout(); }   // exact live inset (default was a safe guess)
            }
        });
#endif
        m_dark = dark;          // chrome darkness is fixed for this process (restart-to-apply)
        loadSettings();         // restore preferences incl. the chosen editor theme (before loadTheme reads m_themeName)
        loadTheme();            // parse the active theme XML for exact colours
        { long z = 0; wxConfigBase::Get()->Read("Zoom", &z, 0L); m_zoom = static_cast<int>(z); }   // restore zoom
        // Seed a concrete paper size/orientation so File > Print's page geometry is never (0,0) before the
        // user has ever opened the print dialog - a fresh wxPrintData's paper id doesn't resolve to a size
        // on its own (see wxWidgets samples/stc's identical g_printData setup).
        if (const wxPrintPaperType* paper = wxThePrintPaperDatabase->FindPaperType(wxPAPER_A4))
        { m_printData.SetPaperId(paper->GetId()); m_printData.SetPaperSize(paper->GetSize()); }
        m_printData.SetOrientation(wxPORTRAIT);
        setAppIcon();
        buildEditor();
        buildMenuBar();
        rebuildUserLangMenu();   // populate the Language menu's per-UDL section from what loadAllUdls() found
#ifdef __WXMAC__
        // Host panel for the docked (non-native) macOS toolbar (see buildToolBar). A direct frame child so
        // it can be a wxAui pane; the toolbar is parented to THIS (not the frame) so wxToolBar::Create leaves
        // m_macToolbar null and we get a wx-laid-out toolbar instead of the native NSToolbar. buildEditor()
        // above already ran m_aui.SetManagedWindow(this), so AUI is live for the dock in buildToolBar().
        m_toolBarHost = new wxPanel(this, wxID_ANY);
#endif
#ifdef __WXGTK__
        // Install the GTK CSS (scrollbar/tint palette + toolbar compaction) BEFORE the toolbar exists: in
        // the integrated-top-bar mode dockIntegratedToolBar()'s AddPane snapshots the GtkToolbar's best size
        // ONCE and never re-measures, so the compact button metrics must already be in effect when the
        // toolbar is realized - installing only from applyTheme() (which runs later) left the docked row at
        // the roomy pre-CSS theme height. applyTheme()'s later call reloads the same provider (harmless).
        wxn_InstallDarkScrollbarCss(nullptr, m_dark ? 1 : 0);
#endif
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
            wxAuiPaneInfo pi; pi.Name(wxString::FromUTF8(id)).Caption(wxString::FromUTF8(title)).CloseButton(true).MaximizeButton(false).Hide();   // plugin panels only appear when invoked
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
        g_nibRegisterLanguage = [this](const char* n, const char* e, const char* lua) -> int {
            return registerScintilluaLanguage(wxString::FromUTF8(n), wxString::FromUTF8(e ? e : ""), wxString::FromUTF8(lua ? lua : ""));
        };
        // nib.keymap/1: build/commit plugin keybinding schemes into m_keymap. Installed here (before
        // loadNibPlugins() at the end of this method) so a plugin's activate() can call them, and after
        // buildMenuBar() has seeded+loaded m_keymap (m_keymapReady) so bind_id/bind_name can resolve ids and
        // names against a live store. Data-only across the boundary -> unload-safe (see the nib.keymap block).
        g_nibKmBegin = [this](const char* id, const char* title, const char* parent) -> NibKeymapScheme* {
            if (!id || !*id) return nullptr;                       // a scheme needs a stable id to be committable
            // A bundled preset's id ("wxnote.default") is RESERVED: a plugin scheme may
            // PARENT a preset but never claim its id (registerScheme would refuse the commit anyway -
            // failing here surfaces the mistake at begin time instead of after a whole build).
            if (m_keymap.schemeIsBundled(wxString::FromUTF8(id))) return nullptr;
            auto up = std::make_unique<NibKeymapBuilder>();
            up->id     = wxString::FromUTF8(id);
            up->title  = wxString::FromUTF8(title  ? title  : "");
            up->parent = wxString::FromUTF8(parent ? parent : "");
            NibKeymapBuilder* raw = up.get();
            g_nibKmPendingBuilders.push_back(std::move(up));       // host-owned until commit / discard / unload
            return reinterpret_cast<NibKeymapScheme*>(raw);
        };
        g_nibKmBindId = [this](NibKeymapScheme* h, int cmdId, const char* accel, int additional) -> int {
            NibKeymapBuilder* b = reinterpret_cast<NibKeymapBuilder*>(h);
            if (!b) return 0;
            const EffectiveBinding* eff = m_keymap.effectiveByCmd(cmdId);   // frozen kCmd/IDM -> symbolicName
            if (!eff) return 0;                                   // bind_id MUST reject an unknown id
            return nibKmPushMenu(b, eff->symbolicName, accel, additional);
        };
        g_nibKmBindName = [this](NibKeymapScheme* h, const char* name, const char* accel, int additional) -> int {
            NibKeymapBuilder* b = reinterpret_cast<NibKeymapBuilder*>(h);
            if (!b || !name || !*name) return 0;
            const wxString sym = wxString::FromUTF8(name);
            const int recorded = nibKmPushMenu(b, sym, accel, additional);
            // Report success only for a command this build actually knows; an unknown name is still recorded
            // (forward-compat: resolveAll() retains-but-ignores it) but returns 0 so an importer can flag it.
            return (recorded && m_keymap.effective(sym) != nullptr) ? 1 : 0;
        };
        g_nibKmBindEditor = [this](NibKeymapScheme* h, int sciCmd, const char* accel, int additional) -> int {
            NibKeymapBuilder* b = reinterpret_cast<NibKeymapBuilder*>(h);
            if (!b) return 0;
            // Phase 4's editor tier is keyed by a curated ascii name, not the raw SCI_* id; map id -> name.
            wxString name;
            for (const EditorEffective& e : m_keymap.editorAll())
                if (e.sciCmd == sciCmd) { name = e.name; break; }
            if (name.empty()) return 0;                           // no remappable editor row for this SCI id
            const bool unbind = !accel || !*accel;
            wxString a;
            if (!unbind) { a = wxString::FromUTF8(accel); if (!nibKmAccelOk(a)) return 0; }
            b->editor.push_back({ name, a, unbind, additional != 0 });
            return 1;
        };
        g_nibKmDiscard = [](NibKeymapScheme* h) { dropNibKmBuilder(reinterpret_cast<NibKeymapBuilder*>(h)); };
        g_nibKmCommit = [this](NibKeymapScheme* h, int activate) -> int {
            NibKeymapBuilder* b = reinterpret_cast<NibKeymapBuilder*>(h);
            if (!b) return 0;
            // Menu tier: build a delta-only KeymapScheme. bundled=false so save() serializes it into
            // shortcuts.json and it OUTLIVES the plugin (reloads as a plain user scheme even with the plugin
            // gone). registerScheme replaces any same-id scheme, so a re-import is idempotent.
            KeymapScheme s;
            s.id      = b->id;
            s.name    = b->title.empty() ? b->id : b->title;
            s.parent  = b->parent;             // "" or an unknown id => resolves against the root only
            s.bundled = false;
            for (const NibKmMenuBind& mb : b->menu)
            {
                // A replace (additional==0, non-unbind) is the store's unbind-then-bind convention -
                // one owner, KeymapStore::pushReplace (clears the inherited Tier-0/parent accel first;
                // without it applyDelta ADDS the new key alongside the old one).
                if (!mb.additional && !mb.unbind)
                { KeymapStore::pushReplace(s.deltas, mb.sym, mb.accel); continue; }
                KeymapDelta d;
                d.symbolicName = mb.sym;
                d.scope        = KeyScope::Global;
                d.unbind       = mb.unbind;
                d.key          = mb.unbind ? wxString() : mb.accel;
                s.deltas.push_back(std::move(d));
            }
            // Editor tier: SCHEME-SCOPED editor deltas, stored UNCONDITIONALLY (activate or not) so an
            // inactive commit still persists them and a later activation applies them - exact parity
            // with the menu deltas above (the importer's "select the scheme in the mapper to apply"
            // holds for both tiers). The editor tier resolves ONE accel per command last-wins, so an
            // `additional` bind collapses the same way the old activate-time setEditorBinding loop did.
            for (const NibKmEditorBind& eb : b->editor)
            {
                KeymapDelta d;
                d.symbolicName = eb.name;
                d.scope        = KeyScope::Editor;
                d.unbind       = eb.unbind;
                d.key          = eb.unbind ? wxString() : eb.accel;
                s.editorDeltas.push_back(std::move(d));
            }
            if (!m_keymap.registerScheme(s))   // refused: the id names a bundled read-only preset
            { dropNibKmBuilder(b); return 0; }
            // Re-resolve NOW even without activation: a re-commit may have REPLACED a scheme that is
            // already in the ACTIVE chain, and registerScheme itself never resolves - without this the
            // live accel table/menu labels/editor keymap would serve the stale deltas until restart.
            // Idempotent (the activate path's setActiveScheme resolves again) and a cheap no-op when
            // the committed scheme is not in the active chain.
            m_keymap.resolveAll();
            if (activate) m_keymap.setActiveScheme(s.id);
            m_keymap.save();                   // persist immediately (host-owned on disk) - never on exit
            refreshAccelerators(m_accelScope); // reflect any now-active menu deltas on the bar + frame accel table
            reapplyEditorKeymaps();            // and any now-active editor overrides on the live STCs
            dropNibKmBuilder(b);               // the handle is consumed - free the builder, do not reuse
            return 1;
        };
        // nib.keymap/2 read hook: report the effective (primary, plain-global) shortcut of a command id as
        // portable modifiers + key. Parses the store's own accel spelling with wxAcceleratorEntry so the
        // Ctrl/Alt/Shift + keycode it yields matches exactly what the menu/accel table installs.
        g_nibKmEffectiveShortcut = [this](int cmdId, uint32_t* mods, uint32_t* key) -> int {
            if (!m_keymapReady) return 0;
            const EffectiveBinding* b = m_keymap.effectiveByCmd(cmdId);
            if (!b) return 0;
            const wxString raw = b->primaryRaw();   // first plain, non-chord Global accel; empty if unbound
            if (raw.empty()) return 0;
            wxAcceleratorEntry e;
            if (!e.FromString(raw)) return 0;   // same parse the store/menu feed use (keySpell::canonical)
            const int f = e.GetFlags();
            uint32_t m = 0;
            if (f & (wxACCEL_CTRL | wxACCEL_CMD)) m |= 1;   // fold macOS Cmd into the Ctrl bit (N++ plugins think in Ctrl)
            if (f & wxACCEL_ALT)                  m |= 2;
            if (f & wxACCEL_SHIFT)                m |= 4;
            *mods = m;
            *key  = static_cast<uint32_t>(e.GetKeyCode());   // uppercase ASCII for letters/digits; WXK_* for named keys
            return 1;
        };
        g_nibUserDataDir = [this](char* b, int c) -> int { return nibCopyUtf8(userDataDir().utf8_string(), b, c); };
        g_nibDocCount      = [this]() -> int { const int n = (m_main.tabs ? (int)m_main.tabs->GetPageCount() : 0) + (m_sub.tabs ? (int)m_sub.tabs->GetPageCount() : 0); return n > 0 ? n : 1; };   // across BOTH views
        g_nibDocActivePath = [this](char* b, int c) -> int {   // active document's full path (UTF-8); 0 if untitled
            EditorPage* p = activePage();
            const std::string u = (p ? p->path : wxString()).utf8_string();
            if (u.empty()) return 0;
            return nibCopyUtf8(u, b, c);
        };
        g_nibDocOpen = [this](const char* p) -> int { if (!p) return 0; openPath(wxString::FromUTF8(p)); return 1; };
        g_nibDocSave = [this]() -> int { onSave(); return 1; };
        g_nibDocActiveId   = [this]() -> intptr_t { return reinterpret_cast<intptr_t>(activePage()); };   // the EditorPage* IS the buffer id
        g_nibDocPathFromId = [this](intptr_t id, char* b, int c) -> int {   // resolve a buffer id back to its on-disk path
            for (wxAuiNotebook* nb : { m_main.tabs, m_sub.tabs }) {          // a buffer id can live in EITHER view
                if (!nb) continue;
                for (size_t i = 0; i < nb->GetPageCount(); ++i) {
                    EditorPage* pg = static_cast<EditorPage*>(nb->GetPage(i));
                    if (reinterpret_cast<intptr_t>(pg) != id) continue;
                    return nibCopyUtf8(pg->path.utf8_string(), b, c);
                }
            }
            return 0;
        };
        g_nibDocActiveView = [this]() -> int { return m_active == &m_sub ? 1 : 0; };   // 0 = main, 1 = sub
        g_nibDocPathAt = [this](int index, char* b, int c) -> int {   // v4: path of the open doc at a flat index (main view's tabs, then sub view's - matching g_nibDocCount)
            int i = 0;
            for (wxAuiNotebook* nb : { m_main.tabs, m_sub.tabs }) {
                if (!nb) continue;
                for (size_t k = 0; k < nb->GetPageCount(); ++k, ++i)
                    if (i == index) return nibCopyUtf8(static_cast<EditorPage*>(nb->GetPage(k))->path.utf8_string(), b, c);
            }
            return 0;
        };
        // ---- v5: per-view buffer model + per-buffer properties (see the nib.h field comments) ----------
        // view 0 = MAIN, 1 = SUB; the frame keeps two notebooks (m_main/m_sub). Buffer ids are EditorPage*.
        g_nibDocViewCount = [this](int view) -> int {
            wxAuiNotebook* nb = (view == 1) ? m_sub.tabs : m_main.tabs;
            return nb ? (int)nb->GetPageCount() : 0;
        };
        g_nibDocIdAt = [this](int view, int index) -> intptr_t {
            wxAuiNotebook* nb = (view == 1) ? m_sub.tabs : m_main.tabs;
            if (!nb || index < 0 || index >= (int)nb->GetPageCount()) return 0;
            return reinterpret_cast<intptr_t>(nb->GetPage(index));
        };
        g_nibDocPosOf = [this](intptr_t id, int priorityView, int* outView, int* outIndex) -> int {
            const int order[2] = { priorityView == 1 ? 1 : 0, priorityView == 1 ? 0 : 1 };
            for (int oi = 0; oi < 2; ++oi) {
                const int view = order[oi];
                wxAuiNotebook* nb = (view == 1) ? m_sub.tabs : m_main.tabs;
                if (!nb) continue;
                for (size_t k = 0; k < nb->GetPageCount(); ++k)
                    if (reinterpret_cast<intptr_t>(nb->GetPage(k)) == id) {
                        if (outView) *outView = view;
                        if (outIndex) *outIndex = (int)k;
                        return 1;
                    }
            }
            return 0;   // no open document has that id
        };
        g_nibDocIndexOfActive = [this](int view) -> int {
            wxAuiNotebook* nb = (view == 1) ? m_sub.tabs : m_main.tabs;
            if (!nb || nb->GetPageCount() == 0) return -1;
            const int s = nb->GetSelection();
            return s == wxNOT_FOUND ? -1 : s;
        };
        g_nibDocActivateAt = [this](int view, int index) -> int {
            wxAuiNotebook* nb = (view == 1) ? m_sub.tabs : m_main.tabs;
            if (!nb || index < 0 || index >= (int)nb->GetPageCount()) return 0;
            activatePage(static_cast<EditorPage*>(nb->GetPage(index)));
            return 1;
        };
        g_nibDocSetLangById = [this](intptr_t id, int langCmdId) -> int {
            EditorPage* p = pageFromId(id);
            if (!p) return 0;
            wxString lexer, name; bool ok = false;
            if (langCmdId == kCmdLangText) { name = _("Normal text file"); ok = true; }               // Normal Text ("" lexer)
            else if (const WxnLang* L = wxnLangFind(langCmdId)) { lexer = wxString::FromUTF8(L->lexer); name = wxString::FromUTF8(L->name); ok = true; }
            if (!ok) return 0;
            p->langForced = true; p->forcedLexer = lexer; p->forcedName = name; p->sciLang.clear();
            if (p == activePage()) { setLexerForFile(p->path); updateStatus(); if (m_stc) m_stc->Refresh(); }  // background pages re-lex on activation (activateBuffer honours langForced)
            nibFireDocEvent(NIB_EV_LANGUAGE_CHANGED, p);   // -> NPPN_LANGCHANGED (this buffer's id)
            return 1;
        };
        g_nibDocEncodingGet = [this](intptr_t id) -> int {
            EditorPage* p = pageFromId(id);
            return p ? p->encoding : -1;
        };
        g_nibDocEncodingSet = [this](intptr_t id, int enc) -> int {
            EditorPage* p = pageFromId(id);
            if (!p || enc < ENC_UTF8 || enc > ENC_ANSI) return 0;   // charset(5) needs a codepage - can't set blind
            p->encoding = enc;                                       // save-to-apply (documented divergence from N++'s immediate re-decode)
            if (p == activePage()) { updateEncodingMenuChecks(); updateStatus(); }
            return 1;
        };
        g_nibDocEolGet = [this](intptr_t id) -> int {
            EditorPage* p = pageFromId(id);
            if (!p) return -1;
            int mode = -1;
            peekDoc(p, [&]{ mode = (int)sci(SCI_GETEOLMODE); });     // background buffer: doc-swap peek (caret restored)
            return mode;
        };
        g_nibDocEolSet = [this](intptr_t id, int mode) -> int {
            EditorPage* p = pageFromId(id);
            if (!p || mode < 0 || mode > 2) return 0;
            peekDoc(p, [&]{ sci(SCI_SETEOLMODE, mode); sci(SCI_CONVERTEOLS, mode); });   // convert existing line endings
            if (p == activePage()) { refreshTab(p); updateUiState(); }   // active: its own modified event already fired
            else { p->dirty = true; refreshTabLabel(p); }               // background: the swap was quiet, mark it here
            return 1;
        };
        g_nibDocSaveActiveAs = [this](const char* path, int asCopy) -> int {
            return (path && *path && saveActiveAs(wxString::FromUTF8(path), asCopy != 0)) ? 1 : 0;
        };
        g_nibDocSaveById = [this](intptr_t id) -> int {
            EditorPage* p = pageFromId(id);
            if (!p || p->path.empty()) return 0;
            if (p == activePage()) return writeFile(p->path) ? 1 : 0;
            int rc = 0;
            peekDoc(p, [&]{ rc = writePageToDisk(p) ? 1 : 0; });        // background buffer: write it without activation churn
            if (rc) refreshTabLabel(p);
            return rc;
        };
        g_nibDocSetDirtyActive = [this]() -> int {
            EditorPage* p = activePage();
            if (!p || !m_stc) return 0;
            // Scintilla has no "mark modified" message; adding a container undo action moves the document
            // off its save point (SCI_GETMODIFY becomes true) without changing a byte - the documented way
            // a container forces the dirty state. The tab "*", Save-enable and close-prompt follow from it.
            sci(SCI_ADDUNDOACTION, 0, 0);
            p->dirty = true; refreshTab(p); updateUiState();
            return 1;
        };
        g_nibDocRenameUntitled = [this](intptr_t id, const char* name) -> int {
            EditorPage* p = pageFromId(id);
            if (!p || !name || !*name || !p->path.empty()) return 0;   // has an on-disk path -> not untitled, reject
            const wxString nm = wxString::FromUTF8(name);
            if (nm.find('/') != wxString::npos || nm.find('\\') != wxString::npos) return 0;   // a tab label, not a path
            p->title = nm;
            if (p == activePage()) refreshTab(p);   // refreshTab updates the tab label AND the title bar
            else refreshTabLabel(p);
            return 1;
        };
        g_nibDocTabColorId = [this](int view, int index) -> int {
            wxAuiNotebook* nb = (view == 1) ? m_sub.tabs : m_main.tabs;
            if (!nb || index < 0 || index >= (int)nb->GetPageCount()) return -1;
            EditorPage* p = static_cast<EditorPage*>(nb->GetPage(index));
            if (!p || !p->tabColour.IsOk()) return -1;
            for (int i = 0; i < 5; ++i) if (tabPaletteColour(i) == p->tabColour) return i;   // palette slot [0..4]
            return -1;   // a colour not from the host palette -> no id we can name
        };
        // No-UI rename/delete of the active file: fire the same v4 lifecycle events the menu wrappers do.
        g_nibRenameActive = [this](const char* newPathUtf8) -> int {
            if (!newPathUtf8 || !*newPathUtf8) return 0;
            EditorPage* ep = activePage();
            if (!ep || ep->path.empty()) return 0;
            nibFireDocEvent(NIB_EV_FILE_BEFORE_RENAME, ep);   // -> NPPN_FILEBEFORERENAME (before the operation)
            return renameActiveTo(wxString::FromUTF8(newPathUtf8)) ? 1 : 0;   // fires RENAMED or RENAME_CANCEL
        };
        g_nibRecycleActive = [this]() -> int { return recycleActive() ? 1 : 0; };
#ifdef __WXMSW__   // nib.win32: hand the (optional, GPL) N++ bridge the native handles it needs
        g_nibMainWindow   = [this]() -> void* { return static_cast<HWND>(GetHandle()); };
        g_nibEditorMain   = [this]() -> void* { return m_main.sci; };   // _scintillaMainHandle is always the MAIN view (not the active alias)
        g_nibEditorSecond = [this]() -> void* { return m_sub.sci; };    // _scintillaSecondHandle is the SUB view (valid even before first split)
        g_nibPluginsMenu  = [this]() -> void* {           // the Plugins menu HMENU (the GPL bridge maps it to NPPM_GETMENUHANDLE)
            wxMenu* pm = m_menuRegistry.find("menu.extensions");
            return pm ? reinterpret_cast<void*>(pm->GetHMenu()) : nullptr;
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
        // nib.ui/1: menu checkmarks + dark-mode probe/palette. Installed before loadNibPlugins() so a
        // plugin's activate() can already query them.
        g_nibUiMenuCheck = [this](int id, bool on) -> int {
            wxMenuBar* mb = menuBar(); if (!mb) return 0;
            // Ids can arrive sign-wrapped through the 16-bit WM_COMMAND path (kCmd* > 32767); recover
            // with the same & 0xFFFF mask onCommand uses. Every real menu id is < 65536.
            wxMenuItem* it = mb->FindItem(id & 0xFFFF);
            if (!it || !it->IsCheckable()) return 0;
            it->Check(on);
            return 1;
        };
        g_nibUiIsDark = [this]() -> int { return m_dark ? 1 : 0; };
        g_nibUiDarkColors = [this](NibUiDarkColors* c) -> int {
            if (!c || c->struct_size < sizeof(NibUiDarkColors)) return 0;
            // Scintilla-style 0xBBGGRR (theme XML ints) -> the portable 0xRRGGBB the ABI speaks.
            auto bgrToRgb = [](int bgr, uint32_t fallback) -> uint32_t {
                if (bgr < 0) return fallback;
                return ((static_cast<uint32_t>(bgr) & 0xFF) << 16) | (static_cast<uint32_t>(bgr) & 0xFF00)
                     | ((static_cast<uint32_t>(bgr) >> 16) & 0xFF);
            };
            // The dark palette is the host's fixed dark chrome set (chrome darkness is per-process, so
            // these are the exact colours applyTheme/buildStatusBar paint); pure_background follows the
            // ACTIVE editor theme's default background when this process runs dark, so plugin surfaces
            // sit on the same canvas colour as the editor.
            int themeBg = -1;
            if (m_dark && m_theme.loaded)
            { auto it = m_theme.global.find("Default Style"); if (it != m_theme.global.end()) themeBg = it->second.second; }
            c->version           = 1;
            c->background        = 0x202020;   // chrome bars (32,32,32) - applyTheme's chromeBg
            c->softer_background = 0x2D2D2D;   // raised rows/gutter (45,45,45) - incremental-search bar
            c->hot_background    = 0x3F3F46;   // hover fill (63,63,70) - menu/tab hover
            c->pure_background   = bgrToRgb(themeBg, 0x1E1E1E);   // editor default bg (30,30,30 fallback)
            c->error_background  = 0xB00000;
            c->text              = 0xDCDCDC;   // primary text (220,220,220) - chromeFg
            c->darker_text       = 0xC0C0C0;
            c->disabled_text     = 0x808080;
            c->link_text         = 0x4DA6FF;   // a lighter shade of the URL indicator's link blue
            c->edge              = 0x646464;
            c->hot_edge          = 0x9B9B9B;
            c->disabled_edge     = 0x484848;
            return 1;
        };
        // nib.ui/2: host chrome visibility (which: 0=toolbar, 1=menubar, 2=statusbar, 3=doc list) +
        // editor/UI state getters. chrome_set returns the PREVIOUS visibility so the bridge can report
        // Notepad++'s "old status". The MENUBAR is a portable documented NO-OP (a global macOS menubar
        // can't be hidden, so - identically on every OS, no per-platform detach hack - it always reports
        // shown and hide is ignored); toolbar/statusbar/doclist toggle for real.
        g_nibUiChromeGet = [this](int which) -> int {
            switch (which) {
                case 0: return m_showToolbar ? 1 : 0;
                case 1: return 1;   // menubar: always shown (documented no-op)
                case 2: return m_showStatusbar ? 1 : 0;
                case 3: { if (!m_docList) return 0; wxAuiPaneInfo& pi = m_aui.GetPane(m_docList); return (pi.IsOk() && pi.IsShown()) ? 1 : 0; }
                default: return 0;
            }
        };
        g_nibUiChromeSet = [this](int which, int visible) -> int {
            const bool vis = visible != 0;
            switch (which) {
                case 0: { const int old = m_showToolbar ? 1 : 0; m_showToolbar = vis; showToolBar(vis); return old; }
                case 1: return 1;   // menubar: documented portable no-op - stays shown, nothing detached
                case 2: { const int old = m_showStatusbar ? 1 : 0; m_showStatusbar = vis;
                          if (auto* sb = GetStatusBar()) sb->Show(vis); SendSizeEvent(); return old; }
                case 3: {
                    if (!m_docList) buildDocList();
                    wxAuiPaneInfo& pi = m_aui.GetPane(m_docList);
                    const int old = (pi.IsOk() && pi.IsShown()) ? 1 : 0;
                    if (pi.IsOk()) { pi.Show(vis); m_aui.Update(); if (vis) refreshDocList(); }
                    return old;
                }
                default: return 0;
            }
        };
        g_nibUiWidthModeGet = [this]() -> int { return m_lineNumWidthMode; };
        g_nibUiWidthModeSet = [this](int mode) -> int {
            if (mode != 0 && mode != 1) return 0;
            m_lineNumWidthMode = mode;
            if (m_lineNumbers) updateLineMargin();   // re-size the margin to the new policy
            return 1;
        };
        g_nibUiAutoIndent = [this]() -> int { return m_autoindent ? 1 : 0; };
        g_nibUiMacroState = [this]() -> int {
            // 0 idle, 1 recording, 2 recorded-macro-available (stopped), 3 playing. wxNote has no
            // observable re-entrant playback state, so playback is not reported; a recorded macro waiting
            // to play reports "stopped" (2).
            if (m_recording) return 1;
            return (!m_macro.empty() || !m_savedMacros.empty()) ? 2 : 0;
        };
        g_nibUiIconSet = [this]() -> int { return m_iconStyle; };
        g_nibUiLocaleName = [](char* b, int c) -> int {
            wxLocale* loc = wxGetLocale();                        // the app's active locale (App::m_locale)
            const wxString canon = loc ? loc->GetCanonicalName() : wxString();   // "en" / "pl" / "ja_JP" ...
            return canon.empty() ? 0 : nibCopyUtf8(canon.utf8_string(), b, c);
        };
        // nib.session/1: save/load/enumerate session files by explicit path (factored out of the File >
        // Save/Load Session dialogs into saveSessionToPath/loadSessionFromPath/sessionFileList below).
        g_nibSessSaveCurrent = [this](const char* path) -> int {
            return (path && *path && saveSessionToPath(wxString::FromUTF8(path)) >= 0) ? 1 : 0;
        };
        g_nibSessSaveFiles = [this](const char* path, const char* const* files, int n) -> int {
            if (!path || !*path) return 0;
            std::vector<wxString> list;
            for (int i = 0; i < n; ++i) if (files && files[i]) list.push_back(wxString::FromUTF8(files[i]));
            return saveSessionFilesToPath(wxString::FromUTF8(path), list) ? 1 : 0;
        };
        g_nibSessLoad = [this](const char* path) -> int {
            return (path && *path && loadSessionFromPath(wxString::FromUTF8(path)) >= 0) ? 1 : 0;
        };
        g_nibSessFileCount = [this](const char* path, int* valid) -> int {
            if (valid) *valid = 0;
            if (!path || !*path) return 0;
            bool v = false;
            const int n = sessionFileList(wxString::FromUTF8(path), nullptr, &v);
            if (valid) *valid = v ? 1 : 0;
            return n;
        };
        g_nibSessFileAt = [this](const char* path, int index, char* b, int c) -> int {
            if (!path || !*path || index < 0) return 0;
            std::vector<wxString> files;
            sessionFileList(wxString::FromUTF8(path), &files, nullptr);
            if (index >= (int)files.size()) return 0;
            return nibCopyUtf8(files[index].utf8_string(), b, c);
        };
        // nib.lexer/1: create a Lexilla ILexer by name (portable on every OS) + count registered languages.
        g_nibLexerCreate = [this](const char* name) -> intptr_t {
            if (!name || !*name) return 0;
            return reinterpret_cast<intptr_t>(CreateLexer(name));   // Lexilla; NULL for an unknown lexer name
        };
        g_nibLexerUserLangCount = [this]() -> int { return (int)m_sciLangs.size(); };
        // nib.toolbar/1: plugin toolbar buttons. The pixels/tooltip are copied HERE (nothing plugin-
        // owned is retained); the button dispatches its command id through the frame's normal command
        // path - on MSW that is MSWWindowProc's WM_COMMAND redispatch into onCommand (unsigned 16-bit
        // recovery), elsewhere the wxEVT_TOOL/wxEVT_MENU bindings - never a raw window message.
        g_nibToolbarAddTool = [this](int cmdId, const NibToolbarIcon* ic, const char* tip) -> int {
            wxToolBar* tb = toolBar();
            if (!tb || !ic || ic->struct_size < sizeof(NibToolbarIcon) || !ic->rgba) return 0;
            const int w = static_cast<int>(ic->width), h = static_cast<int>(ic->height);
            if (w <= 0 || h <= 0 || w > 256 || h > 256) return 0;   // sanity: toolbar icons, not photographs
            const int id = cmdId & 0xFFFF;                          // same unsigned-16-bit space onCommand matches on
            if (tb->FindById(id)) return 0;                         // one button per command id
            wxImage img(w, h);
            img.InitAlpha();
            const unsigned char* px = ic->rgba;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x, px += 4)
                { img.SetRGB(x, y, px[0], px[1], px[2]); img.SetAlpha(x, y, px[3]); }
            const int isz = m_toolbarIconSize;
            if (w != isz || h != isz) img.Rescale(isz, isz, wxIMAGE_QUALITY_HIGH);   // host icon size wins
            const wxBitmapBundle bmp = wxBitmapBundle::FromBitmap(wxBitmap(img));
            const wxString tipStr = wxString::FromUTF8(tip ? tip : "");
            tb->AddTool(id, tipStr, bmp, iconDisabled(bmp, isz), wxITEM_NORMAL, tipStr);
            tb->Realize();
            m_nibToolIds.push_back(id);
            return 1;
        };
        g_nibToolbarRemoveAll = [this]() {
            if (wxToolBar* tb = toolBar())
            {
                for (int id : m_nibToolIds) tb->DeleteTool(id);
                if (!m_nibToolIds.empty()) tb->Realize();
            }
            m_nibToolIds.clear();
        };
        if (!g_safeMode)    // --safe: load nothing from <exe>/nib - our own Nib plugins AND the GPL npp-bridge
            loadNibPlugins();   // cross-platform: plugins written against our own Nib API (include/nib/nib.h)
        if (!g_nibCommands.empty())   // surface registered Nib commands in the Plugins menu
            if (wxMenu* pm = m_menuRegistry.find("menu.extensions"))
            {
                if (pm->GetMenuItemCount() > 0) pm->AppendSeparator();
                for (size_t i = 0; i < g_nibCommands.size(); ++i)
                    pm->Append(NIB_CMD_BASE + static_cast<int>(i), wxString::FromUTF8(g_nibCommands[i].title));
            }

        Bind(wxEVT_MENU, &WxnShellFrameT::onCommand, this);          // one dispatcher for all menu+toolbar ids
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { updateStatus(); updateUiState(); }, myID_TIMER);
#ifdef WXN_HAS_BORDERLESS
        // Focus-scoped accelerators (see refreshAccelerators / onChildFocus). Only the borderless frame
        // owns a wxAcceleratorTable, so only it needs the gate: when focus moves into the terminal panel
        // we drop that table so Ctrl+C/D/Z/L/A/E and the arrows reach the focused TermView instead of
        // firing a menu command on the hidden editor. wxChildFocusEvent propagates up to the frame on
        // every descendant focus change - and ONLY on focus changes, not per-event like a global filter -
        // so it covers the dynamically-created terminal tabs for free. The native frame's accelerators
        // come from the attached wxMenuBar (which SetAcceleratorTable can't clear), so it never binds this.
        if constexpr (kBorderless) Bind(wxEVT_CHILD_FOCUS, &WxnShellFrameT::onChildFocus, this);
#endif
        g_editorContextMenu = [this](int sx, int sy) { showEditorContext(sx, sy); };   // editor right-click menu
        g_openDropped = [this](const wxString& s) { openDroppedPaths(s); };            // files dropped on the editor
        g_onZoom = [this](int z) { onZoomChanged(z); };                                // sync + persist zoom
        g_ipcOpenRequest = [this](const WxnOpenRequest& r) {
            for (const auto& d : r.folders) showFileBrowserRooted(d);   // `wxnote .` handed to us; last root wins
            // Shared with the new-window path: files as tabs (or into the split for -o/-O/--split), each
            // file's :line[:col], the launch-wide -g/-e (or +N), -R/-M read-only and +/{pattern}. -w/stdin
            // never reach here (both force a new instance in OnInit), so no wait/stdin handling is needed.
            openRequestFiles(r);
            // -pluginMessage=<text>, handed to us over the IPC payload (PLUGINMSG=) because plugins are
            // already loaded in THIS (reused) window - the new-window path fires the same event from
            // applyRequest() in OnInit; this is the reuse-window twin of that call.
            if (!r.pluginMessage.empty()) nibFireCmdlinePluginMsg(r.pluginMessage);
            Raise(); if (IsIconized()) Iconize(false);   // a background launch handed us files - come to front
        };
        SetDropTarget(new FileDrop([this](const wxArrayString& fs) { for (const auto& f : fs) openPath(f); }));
        Bind(wxEVT_CLOSE_WINDOW, &WxnShellFrameT::onCloseWindow, this);                 // prompt to save on exit
        m_timer.Start(150);
        applyTheme(m_dark);     // style for the mode this process was launched in
        applySettings();        // apply persisted preferences (tab size, wrap, line numbers, toolbar/status visibility)
        macroToolStates();      // sync the Macro menu/toolbar enable states (nothing recorded yet)
        updateStatus();
        updateUiState();
    }

    // Open a file given on the command line (cmdline paths open as tabs). Returns the new
    // page (nullptr if the path didn't exist) so callers can act on it further (e.g. -g/-e, IPC handoff).
    // FILES only, by design: a positional DIRECTORY is routed by the caller (WxnApp::OnInit and the IPC
    // lambda above) to openFolderPath() instead. Teaching openPath() to swallow directories would put them
    // into the `opened` array that feeds enterWaitMode(), and `wxnote -w somedir` would then block forever
    // on a "tab" that can never be closed because it never was one.
    EditorPage* openPath(const wxString& p) { return wxFileExists(p) ? addDocument(p, wxFileNameFromPath(p)) : nullptr; }
    // A positional directory (`wxnote .`, `wxnote C:\src\proj`) roots the workspace browser rather than
    // opening a tab. Public wrapper: showFileBrowserRooted() itself lives in the private section below.
    void openFolderPath(const wxString& p) { showFileBrowserRooted(p); }
    // -w/--wait: the paths the launching process is blocked on. Also force the save prompt ON for this run
    // only (not persisted - saveSettings() skips the AskBeforeClose write in wait mode): m_askBeforeClose
    // defaults to OFF, i.e. a modified buffer is discarded silently - which for a commit message would hand
    // git back an unedited COMMIT_EDITMSG with no warning.
    void enterWaitMode(const wxArrayString& paths)
    { g_waitMode = true; g_waitPaths = paths; m_askBeforeClose = true; }
    // Apply -g (goto line[,col]) / -e (force encoding) to whichever page is active after opening some
    // CLI/IPC-supplied paths - the switches apply to the file just opened, and since addDocument()
    // always activates the newest page, that's simply activePage().
    void applyGotoAndEncoding(int gotoLine, int gotoCol, int forceEnc)
    {
        if (forceEnc >= 0) interpretAs(forceEnc);
        if (gotoLine >= 0)
        {
            const sptr_t pos = gotoCol > 0 ? sci(SCI_FINDCOLUMN, gotoLine - 1, gotoCol - 1) : sci(SCI_POSITIONFROMLINE, gotoLine - 1);
            sci(SCI_GOTOPOS, pos);
        }
        if (m_stc) m_stc->SetFocus();
    }
    // Open the FILES named by a launch or IPC request into THIS frame, honouring -o/-O/--split (first file
    // into MAIN, the rest into the revealed SUB view), each file's own :line[:col], the launch-wide -g/-e
    // (or a +N token folded into gotoLine), -R/-M/--read-only and +/{pattern}. Returns the paths that
    // actually opened (feeds -w wait mode). Folders and piped stdin stay with the CALLER: they differ
    // between the new-window path (WxnApp::OnInit) and the reuse-window path (the g_ipcOpenRequest lambda).
    wxArrayString openRequestFiles(const WxnOpenRequest& req)
    {
        wxArrayString opened;
        std::vector<EditorPage*> openedPages;
        // wxNote's split is exactly TWO views in ONE (always side-by-side) orientation, so -o and -O are
        // identical and there is no third pane / second orientation to honour vim's -oN/-ON counts: N>2 just
        // piles the tail onto SUB. Only split for >=2 files - a lone `-o file` in an empty second pane is pointless.
        const bool doSplit = req.split && req.paths.GetCount() >= 2;
        if (doSplit) ensureSplit();
        for (size_t i = 0; i < req.paths.GetCount(); ++i)
        {
            if (doSplit) setActiveView(i == 0 ? &m_main : &m_sub);    // first file MAIN, everything after it SUB
            if (EditorPage* pg = openPath(req.paths[i]))
            {
                opened.Add(req.paths[i]);
                openedPages.push_back(pg);
                if (i < req.lines.GetCount() && req.lines[i] > 0)
                    applyGotoAndEncoding(req.lines[i], req.cols[i], -1);   // this file's own :line[:col] suffix
            }
        }
        // The launch-wide -g/-e (or +N) goes last, so an explicit -g still wins over a suffix on the final
        // file. Runs while the LAST-opened page is still active (so goto/find land on it, matching wxNote's
        // documented "last file opened" semantics), then we hand focus back to MAIN for the split case.
        if (!req.paths.IsEmpty()) applyGotoAndEncoding(req.gotoLine, req.gotoCol, req.forceEnc);
        if (!req.findPattern.empty() && !openedPages.empty()) findOnOpen(req.findPattern);
        if (req.readOnly) setReadOnlyForPages(openedPages);
        if (doSplit) setActiveView(&m_main);
        return opened;
    }
    // -R/-M/--read-only: set (soft, Scintilla) read-only on ONLY the pages just opened by this launch, not
    // every tab - a reuse-window launch must never freeze the user's already-open buffers. Read-only lives
    // on the Scintilla Document, so we reach each page's doc through the same doc-pointer swap
    // setReadOnlyAllDocs uses and restore the active view's own doc + caret afterwards.
    void setReadOnlyForPages(const std::vector<EditorPage*>& pages)
    {
        if (!m_stc || pages.empty()) return;
        const sptr_t original = sci(SCI_GETDOCPOINTER);
        const int savedAnchor = (int)sci(SCI_GETANCHOR), savedCaret = (int)sci(SCI_GETCURRENTPOS);
        for (EditorPage* p : pages) { if (!p) continue; sci(SCI_SETDOCPOINTER, 0, p->doc); sci(SCI_SETREADONLY, 1); }
        sci(SCI_SETDOCPOINTER, 0, original);
        sci(SCI_SETSEL, savedAnchor, savedCaret);
        setStatus(0, _("Read-Only"));
    }
    // +/{pattern}: put the caret on the first LITERAL match of pattern in the active (last-opened) document.
    // Deliberately a plain search-in-target pass, NOT a regex: vim's +/ is a regex, but wxNote has no CLI
    // regex contract to honour and a case-insensitive literal find is the least-surprising, always-valid
    // behaviour. No match = caret left where the goto/open put it (no error dialog for a CLI convenience).
    void findOnOpen(const wxString& pattern)
    {
        if (!m_stc || pattern.empty()) return;
        const wxScopedCharBuffer pat = pattern.ToUTF8();
        sci(SCI_SETSEARCHFLAGS, 0);                          // 0 = plain text, case-insensitive
        sci(SCI_SETTARGETSTART, 0);
        sci(SCI_SETTARGETEND, sci(SCI_GETLENGTH));
        const sptr_t pos = sci(SCI_SEARCHINTARGET, (uptr_t)pat.length(), reinterpret_cast<sptr_t>(pat.data()));
        if (pos >= 0) { sci(SCI_GOTOPOS, pos); if (m_stc) m_stc->SetFocus(); }
    }
    // '-': drop captured piped stdin into a NEW untitled, MODIFIED buffer titled "(stdin)" so `... | wxnote -`
    // and `git show HEAD:f | wxnote -` land the text in an editor. Like restoreRecoveryBackups, this
    // deliberately does NOT SCI_SETSAVEPOINT - the buffer stays flagged unsaved so closing it prompts to save.
    EditorPage* openStdinBuffer(const wxString& text)
    {
        EditorPage* pg = addDocument(wxString(), _("(stdin)"));
        if (!pg) return nullptr;
        setAllText(text);                                    // NOT a savepoint: leave it dirty/unsaved
        if (m_stc) { sci(SCI_GOTOPOS, 0); m_stc->SetFocus(); }
        return pg;
    }
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

    // Reopen the previous instance's files (Session/File*, gated on Session/Pending) plus any pending
    // recovery backups. The recovery pass deliberately runs even when Pending is false: Pending is only
    // re-set by a CLEAN exit, so gating recovery on it meant the launch right after a CRASH - the one
    // scenario the backups exist for - silently skipped the recovery manifest (the backed-up tabs then
    // "resurrected" a launch later). restoreRecoveryBackups self-guards (empty manifest = no-op), so
    // running it unconditionally costs nothing.
    void restoreSession()
    {
        auto* cfg = wxConfigBase::Get();
        bool pending = false;
        cfg->Read("Session/Pending", &pending, false);
        EditorPage* initial = activePage();        // the startup "new 1"
        EditorPage* activePg = nullptr;
        std::map<wxString, EditorPage*> openedByPath;   // so the recovery pass below can overlay onto these instead of reopening
        if (pending)
        {
            cfg->Write("Session/Pending", false); cfg->Flush();
            long count = 0, active = -1;
            cfg->Read("Session/Count", &count, 0L);
            cfg->Read("Session/Active", &active, -1L);
            for (int i = 0; i < (int)count; ++i)
            {
                wxString path;
                if (!cfg->Read(wxString::Format("Session/File%d", i), &path) || path.empty() || !wxFileExists(path)) continue;
                EditorPage* pg = addDocument(path, wxFileNameFromPath(path));
                openedByPath[path] = pg;
                if (i == (int)active) activePg = pg;
            }
        }
        restoreRecoveryBackups(openedByPath);
        // Drop the redundant startup "new 1" if the restore (session files AND/OR recovery backups) brought
        // in at least one other page. Counting only session files here was the bug behind "relaunching adds a
        // second 'new 1' tab": editing the untitled buffer then quitting without saving backs it up, and the
        // recovery pass restores it as a page ALONGSIDE the fresh startup "new 1" - two "new 1" tabs. The
        // initial buffer is always pristine at this point (restore runs during OnInit, before the user can
        // type), so the empty-path check suffices; the >1 guard (here and again inside CallAfter) means the
        // last remaining tab is never deleted. Deferred via CallAfter because deleting an aui-notebook page
        // this early - before the event loop starts - crashes.
        if (initial && initial->path.empty() && (int)m_tabs->GetPageCount() > 1)
            this->CallAfter([this, initial]() {
                const int idx = m_tabs->GetPageIndex(initial);
                if (idx != wxNOT_FOUND && m_tabs->GetPageCount() > 1) m_tabs->DeletePage(idx);
            });
        if (activePg) { const int idx = m_tabs->GetPageIndex(activePg); if (idx != wxNOT_FOUND) m_tabs->SetSelection(idx); }
    }
    // Reopens anything left in the Recovery/* manifest (see backupUnsavedChanges): unsaved edits that
    // were discarded without prompting (or a crash) and never got explicitly saved since. Runs after
    // the normal Session/File* pass above - alreadyOpen lets it overlay onto a page that pass already
    // reopened instead of opening the same path twice.
    void restoreRecoveryBackups(const std::map<wxString, EditorPage*>& alreadyOpen)
    {
        wxLogNull noLog;   // recovery restore is best-effort - a missing/unreadable backup is skipped, never surfaced as an error
        auto* cfg = wxConfigBase::Get();
        const wxString prevPath = cfg->GetPath();
        cfg->SetPath("/Recovery");
        wxArrayString ids; wxString grp; long grpIdx;
        for (bool more = cfg->GetFirstGroup(grp, grpIdx); more; more = cfg->GetNextGroup(grp, grpIdx)) ids.Add(grp);
        cfg->SetPath(prevPath);
        for (const wxString& id : ids)
        {
            const wxString bak = recoveryDir() + wxFILE_SEP_PATH + id + ".bak";
            if (!wxFileExists(bak)) { cfg->DeleteGroup("Recovery/" + id); continue; }   // stale entry (backup file gone) - drop it
            wxString path, title;
            cfg->Read("Recovery/" + id + "/Path", &path);
            cfg->Read("Recovery/" + id + "/Title", &title);
            wxFile f(bak); wxString content;
            if (f.IsOpened())
            {
                const wxFileOffset len = f.Length();
                std::string b(static_cast<size_t>(len), '\0');
                f.Read(&b[0], static_cast<size_t>(len));
                content = wxString::FromUTF8(b.data(), b.size());
            }
            EditorPage* pg = nullptr;
            auto it = path.empty() ? alreadyOpen.end() : alreadyOpen.find(path);
            if (it != alreadyOpen.end()) pg = it->second;   // already reopened above - just overlay the pending edits onto it
            else pg = addDocument((!path.empty() && wxFileExists(path)) ? path : wxString(),
                                   title.empty() ? wxString(_("Recovered")) : title);
            if (!pg) continue;
            setActiveView(viewOf(pg)); activateBuffer(pg);
            setAllText(content);   // deliberately NOT SCI_SETSAVEPOINT - stays flagged unsaved, like the edits it's replacing
            pg->recoveryId = id;   // so a later Save (or another silent discard) updates/clears the SAME backup instead of orphaning it
            refreshTab(pg);
            nibFireDocEvent(NIB_EV_SNAPSHOT_DIRTY_LOADED, pg);   // -> NPPN_SNAPSHOTDIRTYFILELOADED (restored dirty backup)
        }
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

    // ----- editor + tabs: two views (MAIN | SUB), each one persistent Scintilla per document ------
    void buildEditor()
    {
        // The center pane is a splitter holding both views' tab strips; SUB is hidden until first split.
        m_split = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_NOBORDER);
        m_split->SetSashGravity(0.5);
        m_split->SetMinimumPaneSize(120);
        buildView(m_main, m_split);
        buildView(m_sub,  m_split);
        m_active = &m_main; m_tabs = m_main.tabs; m_stc = m_main.stc;   // MAIN is active to start
        // nib.sci/1 tail: view 0=main, 1=sub, -1=active (m_stc is the active-view alias). Portable.
        g_mainStc = m_main.stc; g_subStc = m_sub.stc;
        g_coreSciCall = [this](int view, unsigned msg, uintptr_t w, intptr_t l) -> intptr_t {
            wxStyledTextCtrl* stc = (view == 0) ? m_main.stc : (view == 1) ? m_sub.stc : m_stc;
            return stc ? static_cast<intptr_t>(stc->SendMsg(static_cast<int>(msg), static_cast<wxUIntPtr>(w), static_cast<wxIntPtr>(l))) : 0;
        };
        // Let plugins invoke a built-in command by id (nib.commands v2 -> the N++ bridge's NPPM_MENUCOMMAND),
        // through the same dispatcher as menu/toolbar clicks so large frozen ids don't wrap in WM_COMMAND.
        g_nibInvokeCommand = [this](int id){ wxCommandEvent ce(wxEVT_MENU, id); onCommand(ce); };
#ifdef __WXMSW__
        m_sci = m_main.sci;
#endif
        g_view = m_main.stc;                // EditorPage::~EditorPage releases its Document through an always-valid view
        m_sub.tabs->Hide();
        m_split->Initialize(m_main.tabs);   // unsplit: only MAIN shows - identical to the old single-view layout
        addDocument("", nextNewName());     // initial "new 1" buffer (lands in the active = MAIN view)
        buildTabCaptionButtons();           // +/v/x buttons at the right end of the active tab strip
        // wxAuiManager: the editor splitter is the center; plugin docking panels attach around it.
        m_aui.SetManagedWindow(this);
        m_aui.AddPane(m_split, wxAuiPaneInfo().CenterPane().PaneBorder(false));
        m_aui.Update();
        buildDocMap();   // Document Map (minimap) pane - hidden until toggled from the View menu / toolbar
        buildFuncList(); // Function List (symbol tree) pane - hidden until toggled
        buildFifPanel(); // Find-in-Files results pane - hidden until a search runs
    }

    // Build one view = a tab strip + ONE persistent wxStyledTextCtrl that hops across its pages.
    // Each tab is a Scintilla Document; switching tabs swaps it via SCI_SETDOCPOINTER,
    // so a handle a plugin cached at setInfo() stays valid. On Windows the native HWND is what plugins target.
    void buildView(ViewPane& v, wxWindow* parent)
    {
        v.tabs = new wxAuiNotebook(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                   wxAUI_NB_TOP | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS |
                                   (m_tabCloseBtn ? wxAUI_NB_CLOSE_ON_ALL_TABS : 0) | wxAUI_NB_MIDDLE_CLICK_CLOSE |
                                   wxAUI_NB_PIN_ON_ACTIVE_TAB);
        { auto* art = new PinTabArt(m_dark ? wxColour(222, 226, 230) : wxColour(52, 58, 64));
          art->UpdateColoursFromSystem(); v.tabs->SetArtProvider(art); }   // pin glyph from resources/icons/pin.svg
        v.tabs->Bind(wxEVT_AUINOTEBOOK_PAGE_CLOSE,   &WxnShellFrameT::onPageClose,   this);
        v.tabs->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &WxnShellFrameT::onPageChanged, this);
        v.tabs->Bind(wxEVT_AUINOTEBOOK_TAB_RIGHT_UP, &WxnShellFrameT::onTabContext,  this);
        // A finished tab drag reorders the strip -> NPPN_DOCORDERCHANGED (the previously-unbound END_DRAG
        // anchor). wxEVT_AUINOTEBOOK_END_DRAG lands even when the tab did not actually move (e.g. a
        // click+release with no net drag) - snapshot the page order at BEGIN_DRAG and only fire when
        // END_DRAG's order differs, so the "fires once per reorder" contract (nib.h / bridge README) holds.
        auto dragOrder = std::make_shared<std::vector<EditorPage*>>();
        v.tabs->Bind(wxEVT_AUINOTEBOOK_BEGIN_DRAG, [dragOrder](wxAuiNotebookEvent& e) {
            wxAuiNotebook* nb = static_cast<wxAuiNotebook*>(e.GetEventObject());
            dragOrder->clear();
            if (nb) for (size_t i = 0; i < nb->GetPageCount(); ++i) dragOrder->push_back(static_cast<EditorPage*>(nb->GetPage(i)));
            e.Skip();
        });
        v.tabs->Bind(wxEVT_AUINOTEBOOK_END_DRAG, [this, dragOrder](wxAuiNotebookEvent& e) {
            wxAuiNotebook* nb = static_cast<wxAuiNotebook*>(e.GetEventObject());
            bool changed = !nb || nb->GetPageCount() != dragOrder->size();
            if (!changed)
                for (size_t i = 0; i < nb->GetPageCount(); ++i)
                    if (static_cast<EditorPage*>(nb->GetPage(i)) != (*dragOrder)[i]) { changed = true; break; }
            if (changed) {
                EditorPage* p = (nb && e.GetSelection() != wxNOT_FOUND) ? static_cast<EditorPage*>(nb->GetPage(e.GetSelection())) : activePage();
                nibFireDocEvent(NIB_EV_DOC_ORDER_CHANGED, p);   // -> NPPN_DOCORDERCHANGED
            }
            e.Skip();
        });
        v.stc = new wxStyledTextCtrl(v.tabs, wxID_ANY);
        v.stc->Hide();                                       // activateBuffer mounts it onto the active page
#ifdef __WXMSW__
        v.sci = static_cast<HWND>(v.stc->GetHandle());       // the native Scintilla HWND (plugins/NPPM use it)
#endif
        // setupScintilla() drives the view through sci()/m_stc, so make THIS view active while we set it up.
        ViewPane* prev = m_active; m_active = &v; m_stc = v.stc; m_tabs = v.tabs;
#ifdef __WXMSW__
        m_sci = v.sci;
#endif
        setupScintilla();                   // margins, markers, styles, options for this view
#ifdef __WXMSW__
        ::SetWindowSubclass(v.sci, SciHwndProc, 3, reinterpret_cast<DWORD_PTR>(v.stc));   // plugin SendMessage(SCI_*) to THIS handle -> THIS view
#endif
        bindViewEvents(v);                  // wxEVT_STC_* -> editor behaviours + plugin beNotified
        if (prev) setActiveView(prev);      // restore the previously-active view (re-point the aliases)
    }

    // wxStyledTextCtrl fires wxEVT_STC_* (not Win32 WM_NOTIFY); bind the editor behaviours + plugin
    // beNotified() forwarding per view, plus a focus-in that makes that view the active one.
    void bindViewEvents(ViewPane& v)
    {
        v.stc->Bind(wxEVT_STC_CHARADDED,        &WxnShellFrameT::onStcCharAdded,   this);
        v.stc->Bind(wxEVT_STC_CALLTIP_CLICK,    &WxnShellFrameT::onCallTipClick,   this);
        v.stc->Bind(wxEVT_STC_INDICATOR_CLICK,  &WxnShellFrameT::onUrlClick,       this);
        v.stc->Bind(wxEVT_STC_UPDATEUI,         &WxnShellFrameT::onStcUpdateUI,    this);
        v.stc->Bind(wxEVT_STC_DOUBLECLICK,      &WxnShellFrameT::onStcDoubleClick, this);
        v.stc->Bind(wxEVT_STC_MARGINCLICK,      &WxnShellFrameT::onStcMarginClick, this);
        v.stc->Bind(wxEVT_STC_ZOOM,             &WxnShellFrameT::onStcZoom,        this);
        v.stc->Bind(wxEVT_STC_MODIFIED,         &WxnShellFrameT::onStcModified,    this);
        v.stc->Bind(wxEVT_STC_MACRORECORD,      &WxnShellFrameT::onMacroRecord,    this);   // capture commands while recording a macro
        v.stc->Bind(wxEVT_STC_STYLENEEDED,      &WxnShellFrameT::onStcStyleNeeded, this);   // container-lexed Scintillua-language buffers only
        // v1 nib.events semantics only: savepoint == "saved" (which also fires on undo/redo landing back
        // on the saved state, and during Save All reports no id at all). v2 subscribers instead get the
        // real, id-carrying NIB_EV_DOCUMENT_SAVED from writeFile/writePageToDisk - one per disk write.
        v.stc->Bind(wxEVT_STC_SAVEPOINTREACHED, [this](wxStyledTextEvent& e) { NibEvent ev{}; ev.kind = NIB_EV_DOCUMENT_SAVED; ev.struct_size = sizeof(NibEvent); nibFireEvent(ev, NIB_FIRE_V1_ONLY); e.Skip(); });
        v.stc->Bind(wxEVT_CONTEXT_MENU,         &WxnShellFrameT::onStcContextMenu, this);
        v.stc->Bind(wxEVT_SET_FOCUS, [this, vp = &v](wxFocusEvent& e) { onViewFocus(vp); e.Skip(); });   // focus -> this view becomes active
    }
    // Auto-insert the matching closer (caret stays between), or skip over an existing one. Returns true if it
    // consumed the character. Brackets + quotes; ASCII only; never inserts right before a word character.
    bool autoInsertPair(int ch)
    {
        if (!m_autoInsertPairs || ch <= 0 || ch > 127) return false;
        static const char opens[] = "([{\"'", closes[] = ")]}\"'";
        const int caret = (int)sci(SCI_GETCURRENTPOS), len = (int)sci(SCI_GETLENGTH);
        const int next  = (caret < len) ? (int)sci(SCI_GETCHARAT, caret) : 0;
        if (std::strchr(closes, ch) && next == ch) { sci(SCI_DELETERANGE, caret, 1); return true; }   // skip over
        if (const char* o = std::strchr(opens, ch))
            if (caret >= len || !(std::isalnum((unsigned char)next) || next == '_'))
            {
                const char buf[2] = { closes[o - opens], 0 };
                sci(SCI_INSERTTEXT, caret, reinterpret_cast<sptr_t>(buf));
                sci(SCI_GOTOPOS, caret);   // keep the caret between the pair
                return true;
            }
        return false;
    }
    void onStcCharAdded(wxStyledTextEvent& e)
    {
        const int ch = e.GetKey();
        if ((ch == '\n' || ch == '\r') && m_autoindent) autoIndentOnNewline(m_stc);
        else
        {
            const bool paired = autoInsertPair(ch);            // insert matching )]}"' or skip over an existing one
            if (ch == '(') funcCallTip();                      // parameter hint for the function being called
            else if (ch == '}') dedentCloseBrace(m_stc);
            else if (!paired && m_autocomplete && (std::isalnum((unsigned char)ch) || ch == '_'))   // word/keyword completion
            {
                const int caret = (int)sci(SCI_GETCURRENTPOS);
                if (caret - (int)sci(SCI_WORDSTARTPOSITION, caret, 1) >= m_autoCompFrom) autoComplete(true);
            }
        }
        e.Skip();
    }
    void onStcUpdateUI(wxStyledTextEvent& e)
    {
        if (m_beginEndSelectActive && !m_selExtendGuard)   // Begin/End Select: re-extend from the sticky anchor to wherever the caret just moved
        {
            m_selExtendGuard = true;
            const int caret = (int)sci(SCI_GETCURRENTPOS);
            if (m_beginEndSelectColumnMode) { sci(SCI_SETSELECTIONMODE, SC_SEL_RECTANGLE); sci(SCI_SETRECTANGULARSELECTIONANCHOR, m_beginEndSelectAnchor); sci(SCI_SETRECTANGULARSELECTIONCARET, caret); }
            else sci(SCI_SETSEL, m_beginEndSelectAnchor, caret);
            m_selExtendGuard = false;
        }
        updateBraceMatch(m_stc);
        callTipCaretMoved();   // keep the parameter hint in sync / dismiss when the caret leaves the call
        markVisibleUrls();     // underline URLs on screen (click to open)
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
        // v3 NIB_EV_TEXT_MODIFIED: the RAW modification, but ONLY when its flags intersect the mask a
        // plugin armed via nib.events set_modified_mask. The mask is 0 by default, so this whole path is
        // a single branch until something opts in - the perf gate for the highest-volume editor signal.
        if (g_nibModifiedMask && (static_cast<uint32_t>(mt) & g_nibModifiedMask))
        {
            NibEvent ev{};
            ev.kind = NIB_EV_TEXT_MODIFIED; ev.struct_size = sizeof(NibEvent);
            ev.as.modified.pos               = e.GetPosition();
            ev.as.modified.length            = e.GetLength();
            ev.as.modified.lines_added       = e.GetLinesAdded();
            ev.as.modified.modification_type = static_cast<uint32_t>(mt);
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
        m_docMap->Bind(wxEVT_LEFT_DOWN, &WxnShellFrameT::onDocMapClick, this);
        m_docMap->Bind(wxEVT_MOTION,    &WxnShellFrameT::onDocMapDrag,  this);
        m_aui.AddPane(m_docMap, wxAuiPaneInfo().Name("docmap").Caption(_("Document Map"))
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
        // Viewport band. The band must stay SELECTION-based (selection is the only per-view visual state;
        // markers/indicators live in the shared document and would bleed into the main editor). It must
        // also be OPAQUE: the translucent-selection path (SetSelAlpha) makes this Scintilla skip painting
        // the selected text at minimap zoom, which blanked the band into an unreadable slab (the original
        // bug). Classic opaque selection always draws the glyphs over the band, so pick a background-near
        // tint and let the text keep its syntax colours.
        m_docMap->SetSelBackground(true, d ? wxColour(52, 64, 82) : wxColour(222, 230, 241));
        m_docMap->SetSelAlpha(wxSTC_ALPHA_NOALPHA);
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

    // ---- Terminal (View > Show Terminal) ----------------------------------------------------------
    // Lazily-built bottom pane hosting TerminalPanel (see terminal_panel.h): multi-tab shells with a
    // per-platform picker. First toggle builds the pane and spawns the default shell in the active
    // document's directory.
    void toggleTerminal()
    {
        if (!m_terminal)
        {
            m_terminal = new TerminalPanel(this, m_dark, m_fontFace);
            m_terminal->cwdProvider = [this]() -> wxString {
                const wxString p = curPath();
                return p.empty() ? wxGetCwd() : wxFileName(p).GetPath();
            };
            // No caption bar: the panel's own toolbar is its header, so a wxAui caption on top would be
            // a second, redundant title row. The caption carried two affordances, and dropping it costs
            // both: the close button (replaced by the panel's own X) and the drag handle. Gripper() would
            // restore the drag, but wxAui draws it as a dotted rail down the pane's left edge, which is
            // exactly the visual clutter removing the caption was meant to get rid of - so the terminal
            // is DELIBERATELY fixed to its dock: resize by the sash, close by its own X, no drag/float.
            // A terminal is a bottom-docked tool, not a panel people rearrange; the caption and the
            // gripper both cost more chrome than that is worth.
            m_terminal->onCloseRequested = [this]{ toggleTerminal(); };
            m_aui.AddPane(m_terminal, wxAuiPaneInfo().Name("terminal").Caption(_("Terminal"))
                              .CaptionVisible(false).Bottom().Layer(1).BestSize(-1, 220)
                              .MinSize(150, 80).CloseButton(false).Hide());
        }
        wxAuiPaneInfo& pi = m_aui.GetPane(m_terminal);
        if (!pi.IsOk()) return;
        const bool show = !pi.IsShown();
        if (show && m_terminal->empty()) m_terminal->addTerminal(-1);
        pi.Show(show);
        m_aui.Update();
        if (auto* mb = menuBar()) mb->Check(myID_VIEW_TERMINAL, show);
        if (show) m_terminal->focusActive();
    }

    // ---- Function List (per-file symbol tree) -----------------------------------------------------
    void buildFuncList()
    {
        m_funcList = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                    wxTR_HAS_BUTTONS | wxTR_HIDE_ROOT | wxTR_NO_LINES | wxTR_FULL_ROW_HIGHLIGHT | wxBORDER_NONE);
        { wxVector<wxBitmapBundle> imgs; imgs.push_back(icon("func-leaf")); imgs.push_back(icon("func-node")); m_funcList->SetImages(imgs); }   // 0=symbol, 1=group/class (icon set)
        m_funcList->Bind(wxEVT_TREE_ITEM_ACTIVATED, &WxnShellFrameT::onFuncListActivate, this);
        m_funcList->Bind(wxEVT_TREE_SEL_CHANGED,     &WxnShellFrameT::onFuncListActivate, this);   // single-click jumps too
        m_flTimer = new wxTimer(this, myID_FLTIMER);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { parseFuncList(); }, myID_FLTIMER);
        m_aui.AddPane(m_funcList, wxAuiPaneInfo().Name("funclist").Caption(_("Function List"))
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
        if (ext=="go") return "go";
        if (ext=="rs") return "rust";
        if (ext=="lua") return "lua";
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
        if (!lang.empty())
        {
            const wxCharBuffer cb = m_stc->GetTextRaw();          // UTF-8 bytes - offsets line up with Scintilla
            const std::string text(cb.data(), cb.length());
            const std::vector<FLSym> syms = flCollect(text, lang);   // extraction + nesting ranges (testable free fn)
            struct Open { size_t rangeEnd; wxTreeItemId item; };
            std::vector<Open> stack;
            for (const auto& s : syms)
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
    // Toggle a docked pane's visibility; returns true when the pane ends up shown (so callers can refresh it).
    bool togglePane(wxWindow* w)
    {
        if (!w) return false;
        wxAuiPaneInfo& pi = m_aui.GetPane(w);
        if (!pi.IsOk()) return false;
        pi.Show(!pi.IsShown());
        m_aui.Update();
        return pi.IsShown();
    }
    void toggleFuncList()
    {
        if (togglePane(m_funcList)) parseFuncList();
    }

    // ---- Document List (dockable list of the open documents; click an entry to switch to it) -------
    // Match a side panel's colours to the editor's default style, so docked panes follow the theme.
    void themeToEditor(wxWindow* w)
    {
        w->SetBackgroundColour(bgrToColour((int)sci(SCI_STYLEGETBACK, STYLE_DEFAULT)));
        w->SetForegroundColour(bgrToColour((int)sci(SCI_STYLEGETFORE, STYLE_DEFAULT)));
    }
    wxListBox* m_docList = nullptr;
    void buildDocList()
    {
        m_docList = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_SINGLE | wxBORDER_NONE);
        themeToEditor(m_docList);
        m_docList->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& ev) {
            const int s = ev.GetSelection();
            if (m_tabs && s >= 0 && (size_t)s < m_tabs->GetPageCount()) m_tabs->SetSelection((size_t)s);
        });
        m_aui.AddPane(m_docList, wxAuiPaneInfo().Name("doclist").Caption(_("Document List"))
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
        if (togglePane(m_docList)) refreshDocList();
    }

    // ---- Clipboard History (a ring of recent cut/copy entries; double-click to paste) ----
    wxListBox* m_clipList = nullptr;
    std::vector<wxString> m_clipHist;   // most-recent-first, capped ring of copied/cut text
    static wxString clipPreview(const wxString& t)   // collapse a (possibly multi-line) entry to a one-line list label
    {
        wxString s = t; s.Replace("\r", " "); s.Replace("\n", " "); s.Replace("\t", " ");
        s.Trim(true).Trim(false);
        if (s.size() > 60) s = s.Left(57) + "...";
        return s.empty() ? wxString("(whitespace)") : s;
    }
    void recordClip()   // push the latest cut/copy to the front of the history (deduped, capped)
    {
        const wxString t = getClipText();
        if (t.empty()) return;
        for (size_t i = 0; i < m_clipHist.size(); ++i) if (m_clipHist[i] == t) { m_clipHist.erase(m_clipHist.begin() + i); break; }
        m_clipHist.insert(m_clipHist.begin(), t);
        if (m_clipHist.size() > 50) m_clipHist.resize(50);
        refreshClipList();
    }
    void buildClipList()
    {
        m_clipList = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_SINGLE | wxBORDER_NONE);
        themeToEditor(m_clipList);
        m_clipList->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent& ev) { pasteClip(ev.GetSelection()); });   // double-click to paste
        m_aui.AddPane(m_clipList, wxAuiPaneInfo().Name("cliphistory").Caption(_("Clipboard History"))
                          .Right().BestSize(210, 400).MinSize(110, 80).CloseButton(true).Hide());
        m_aui.Update();
    }
    void refreshClipList()   // re-fill the panel from the ring (no-op while hidden)
    {
        if (!m_clipList) return;
        wxAuiPaneInfo& pi = m_aui.GetPane(m_clipList);
        if (!pi.IsOk() || !pi.IsShown()) return;
        m_clipList->Freeze();
        m_clipList->Clear();
        for (const wxString& t : m_clipHist) m_clipList->Append(clipPreview(t));
        m_clipList->Thaw();
    }
    void pasteClip(int idx)   // insert a history entry at the caret, replacing any selection
    {
        if (idx < 0 || (size_t)idx >= m_clipHist.size()) return;
        const wxScopedCharBuffer u = m_clipHist[(size_t)idx].utf8_str();
        sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>(u.data()));
        if (m_stc) m_stc->SetFocus();
    }
    void toggleClipHistory()
    {
        if (!m_clipList) buildClipList();
        if (togglePane(m_clipList)) refreshClipList();
    }

    // ---- Character Panel (ASCII insertion panel: 0-255 with value/hex/glyph; double-click inserts) ----
    wxListView* m_charPanel = nullptr;
    static wxString charGlyph(int v)   // display text for a code: control-char mnemonic, else the Latin-1 glyph
    {
        static const char* CTRL[] = { "NUL","SOH","STX","ETX","EOT","ENQ","ACK","BEL","BS","TAB","LF","VT","FF","CR","SO","SI",
            "DLE","DC1","DC2","DC3","DC4","NAK","SYN","ETB","CAN","EM","SUB","ESC","FS","GS","RS","US" };
        if (v < 32) return CTRL[v];
        if (v == 32) return "(space)";
        if (v == 127) return "DEL";
        if (v > 127 && v < 160) return "";                 // C1 control range - no printable glyph
        return wxString(wxUniChar(v));
    }
    void buildCharPanel()
    {
        m_charPanel = new wxListView(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
        themeToEditor(m_charPanel);
        m_charPanel->AppendColumn(_("Value"), wxLIST_FORMAT_RIGHT, 46);
        m_charPanel->AppendColumn(_("Hex"),   wxLIST_FORMAT_RIGHT, 46);
        m_charPanel->AppendColumn(_("Character"), wxLIST_FORMAT_LEFT, 90);
        for (int v = 0; v < 256; ++v)
        {
            const long r = m_charPanel->InsertItem(v, wxString::Format("%d", v));
            m_charPanel->SetItem(r, 1, wxString::Format("%02X", v));
            m_charPanel->SetItem(r, 2, charGlyph(v));
        }
        m_charPanel->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& ev) { insertCharCode((int)ev.GetIndex()); });   // double-click / Enter
        m_aui.AddPane(m_charPanel, wxAuiPaneInfo().Name("charpanel").Caption(_("Character Panel"))
                          .Right().BestSize(200, 400).MinSize(120, 80).CloseButton(true).Hide());
        m_aui.Update();
    }
    void insertCharCode(int v)   // insert the Unicode code point (UTF-8) for value 0-255 at the caret
    {
        if (v < 0 || v > 255) return;
        if (v == 0)   // NUL: wxString treats it as a terminator, so insert the byte directly
        {
            const long pos = (long)sci(SCI_GETSELECTIONSTART);
            sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>(""));               // drop any selection first
            sci(SCI_ADDTEXT, 1, reinterpret_cast<sptr_t>("\0"));
            sci(SCI_GOTOPOS, pos + 1);
        }
        else
        {
            const wxScopedCharBuffer u = wxString(wxUniChar(v)).utf8_str();
            sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>(u.data()));
        }
        if (m_stc) m_stc->SetFocus();
    }
    void toggleCharPanel()
    {
        if (!m_charPanel) buildCharPanel();
        togglePane(m_charPanel);
    }

    // ---- Project Panel (workspace tree: named folders + file refs, saved as .xml) ------
    void buildProjectPanel()
    {
        m_projPanel = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                     wxTR_HAS_BUTTONS | wxTR_FULL_ROW_HIGHLIGHT | wxTR_EDIT_LABELS | wxBORDER_NONE);
        themeToEditor(m_projPanel);
        m_projPanel->Bind(wxEVT_TREE_ITEM_ACTIVATED, &WxnShellFrameT::onProjActivate, this);
        m_projPanel->Bind(wxEVT_TREE_ITEM_MENU,      &WxnShellFrameT::onProjContext,  this);
        m_projPanel->AddRoot("Workspace", -1, -1, new ProjItemData(false));
        m_aui.AddPane(m_projPanel, wxAuiPaneInfo().Name("project").Caption(_("Project"))
                          .Left().BestSize(220, 400).MinSize(120, 80).CloseButton(true).Hide());
        m_aui.Update();
    }
    void toggleProjectPanel()
    {
        if (!m_projPanel) buildProjectPanel();
        togglePane(m_projPanel);
    }
    void onProjActivate(wxTreeEvent& e)   // double-click a file node -> open it (folders fall through to expand/collapse)
    {
        auto* d = dynamic_cast<ProjItemData*>(m_projPanel->GetItemData(e.GetItem()));
        if (d && d->isFile && wxFileExists(d->path)) openPath(d->path); else e.Skip();
    }
    void onProjContext(wxTreeEvent& e)
    {
        const wxTreeItemId item = e.GetItem();
        if (item.IsOk()) m_projPanel->SelectItem(item);
        auto* d = item.IsOk() ? dynamic_cast<ProjItemData*>(m_projPanel->GetItemData(item)) : nullptr;
        const bool isRoot   = item.IsOk() && item == m_projPanel->GetRootItem();
        const bool isFolder = isRoot || (d && !d->isFile);
        wxMenu m;
        if (isFolder) { m.Append(7200, _("Add Files...")); m.Append(7201, _("Add Folder")); }
        if (d && !d->isFile && !isRoot) m.Append(7202, _("Rename"));
        if (d && !isRoot) m.Append(7203, _("Remove"));
        m.AppendSeparator();
        m.Append(7210, _("New Workspace")); m.Append(7211, _("Open Workspace...")); m.Append(7212, _("Save Workspace As..."));
        switch (m_projPanel->GetPopupMenuSelectionFromUser(m))
        {
            case 7200: projAddFiles(item);           break;
            case 7201: projAddFolder(item);          break;
            case 7202: m_projPanel->EditLabel(item); break;
            case 7203: m_projPanel->Delete(item);    break;
            case 7210: projNew();  break;
            case 7211: projOpen(); break;
            case 7212: projSave(); break;
        }
    }
    void projAddFiles(const wxTreeItemId& parent)
    {
        if (!parent.IsOk()) return;
        wxFileDialog dlg(this, _("Add Files"), "", "", _("All files (*.*)|*.*"), wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() != wxID_OK) return;
        wxArrayString paths; dlg.GetPaths(paths);
        for (const auto& p : paths) m_projPanel->AppendItem(parent, wxFileNameFromPath(p), -1, -1, new ProjItemData(true, p));
        m_projPanel->Expand(parent);
    }
    void projAddFolder(const wxTreeItemId& parent)
    {
        if (!parent.IsOk()) return;
        const wxTreeItemId f = m_projPanel->AppendItem(parent, _("New Folder"), -1, -1, new ProjItemData(false));
        m_projPanel->Expand(parent);
        m_projPanel->EditLabel(f);                 // let the user name it right away
    }
    void projNew()
    {
        if (!m_projPanel) return;
        m_projPanel->DeleteAllItems();
        m_projPanel->AddRoot("Workspace", -1, -1, new ProjItemData(false));
        m_projWorkspace.clear();
    }
    void projOpen()
    {
        wxFileDialog dlg(this, _("Open Workspace"), "", "", _("Workspace (*.xml)|*.xml"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() == wxID_OK) loadProjectXml(dlg.GetPath());
    }
    void projSave()
    {
        wxString path = m_projWorkspace;
        if (path.empty())
        {
            wxFileDialog dlg(this, _("Save Workspace As"), "", "workspace.xml", _("Workspace (*.xml)|*.xml"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
            if (dlg.ShowModal() != wxID_OK) return;
            path = dlg.GetPath();
        }
        saveProjectXml(path);
        m_projWorkspace = path;
        setStatus(0, _("Workspace saved")); m_hint = true;
    }
    void loadProjectXml(const wxString& path)
    {
        wxXmlDocument doc;
        if (!doc.Load(path) || !doc.GetRoot()) return;
        wxXmlNode* proj = nullptr;
        for (wxXmlNode* n = doc.GetRoot()->GetChildren(); n; n = n->GetNext()) if (n->GetName() == "Project") { proj = n; break; }
        m_projPanel->DeleteAllItems();
        const wxTreeItemId root = m_projPanel->AddRoot(proj ? proj->GetAttribute("name", "Workspace") : "Workspace", -1, -1, new ProjItemData(false));
        if (proj) projLoadChildren(proj, root);
        m_projPanel->ExpandAll();
        m_projWorkspace = path;
    }
    void projLoadChildren(wxXmlNode* xparent, const wxTreeItemId& tparent)
    {
        for (wxXmlNode* n = xparent->GetChildren(); n; n = n->GetNext())
        {
            if (n->GetName() == "Folder")
            { const wxTreeItemId f = m_projPanel->AppendItem(tparent, n->GetAttribute("name", "Folder"), -1, -1, new ProjItemData(false)); projLoadChildren(n, f); }
            else if (n->GetName() == "File")
            { const wxString p = n->GetAttribute("name"); m_projPanel->AppendItem(tparent, wxFileNameFromPath(p), -1, -1, new ProjItemData(true, p)); }
        }
    }
    void saveProjectXml(const wxString& path)
    {
        wxXmlDocument doc;
        auto* npp = new wxXmlNode(wxXML_ELEMENT_NODE, "wxNote");   // loadProjectXml never checks the root's own name, so old <NotepadPlus>-rooted workspace files still load
        doc.SetRoot(npp);
        auto* proj = new wxXmlNode(wxXML_ELEMENT_NODE, "Project");
        proj->AddAttribute("name", m_projPanel->GetItemText(m_projPanel->GetRootItem()));
        npp->AddChild(proj);
        projSaveChildren(m_projPanel->GetRootItem(), proj);
        doc.Save(path);
    }
    void projSaveChildren(const wxTreeItemId& tparent, wxXmlNode* xparent)
    {
        wxTreeItemIdValue cookie;
        for (wxTreeItemId c = m_projPanel->GetFirstChild(tparent, cookie); c.IsOk(); c = m_projPanel->GetNextChild(tparent, cookie))
        {
            auto* d = dynamic_cast<ProjItemData*>(m_projPanel->GetItemData(c));
            if (d && d->isFile)
            { auto* f = new wxXmlNode(wxXML_ELEMENT_NODE, "File"); f->AddAttribute("name", d->path); xparent->AddChild(f); }
            else
            { auto* fo = new wxXmlNode(wxXML_ELEMENT_NODE, "Folder"); fo->AddAttribute("name", m_projPanel->GetItemText(c)); xparent->AddChild(fo); projSaveChildren(c, fo); }
        }
    }

    // ---- Folder as Workspace (a dockable file-system browser rooted at a chosen folder) -----------
    wxGenericDirCtrl* m_fileBrowser = nullptr;
    // (Re)creates the file browser rooted at `root` - wxGenericDirCtrl has no "change root" API (SetPath only
    // expands/selects within the tree it was constructed with), so switching workspaces means rebuilding it.
    // Reuses the previous wxAuiPaneInfo (position/size) when one exists, so picking a new folder doesn't reset
    // the panel's docked layout.
    // Re-skins wxGenericDirCtrl's shared global icon table (wxTheFileIconsTable) with the app's own Tabler
    // icon set, so the Folder as Workspace tree matches the toolbar instead of wx's native folder/file glyphs.
    // Patches the table's wxImageList in place at the same indices wxGenericDirCtrl already reads from -
    // there's only ever the one dir control, so no per-item SetItemImage bookkeeping is needed.
    void patchFileBrowserIcons()
    {
        static bool patched = false;
        if (patched) return;
        patched = true;
        wxImageList* il = wxTheFileIconsTable ? wxTheFileIconsTable->GetSmallImageList() : nullptr;
        if (!il) return;
        const wxSize sz = il->GetSize();
        il->Replace(wxFileIconsTable::folder,      icon("folder").GetBitmap(sz));
        il->Replace(wxFileIconsTable::folder_open, icon("open").GetBitmap(sz));   // open.svg is already Tabler's folder-open glyph
        il->Replace(wxFileIconsTable::computer,    icon("computer").GetBitmap(sz));
        il->Replace(wxFileIconsTable::drive,       icon("drive").GetBitmap(sz));
        il->Replace(wxFileIconsTable::cdrom,       icon("cdrom").GetBitmap(sz));
        il->Replace(wxFileIconsTable::floppy,      icon("floppy").GetBitmap(sz));
        il->Replace(wxFileIconsTable::removeable,  icon("removable").GetBitmap(sz));
        il->Replace(wxFileIconsTable::file,        icon("file").GetBitmap(sz));
        il->Replace(wxFileIconsTable::executable,  icon("executable").GetBitmap(sz));

        // wxFileIconsTable::GetIconID(extension) caches its result per extension (m_HashTable) before
        // ever consulting the OS's MIME database, so calling it here for a known extension - then
        // overwriting whatever image-list slot it just allocated - pre-empts every later real lookup
        // wxGenericDirCtrl makes for that extension with our own icon instead of an OS one. Skip the
        // Replace when GetIconID falls back to the shared `file` id (extension unrecognized by the OS
        // too): that id is the generic fallback for every other unmatched extension, so overwriting it
        // here would wrongly reskin unrelated file types.
        static const struct { const char* ext; const char* iconName; } kExtIcons[] = {
            // source/markup/config - the primary content of a code-editor workspace
            {"c","filetype-code"},{"h","filetype-code"},{"cpp","filetype-code"},{"cc","filetype-code"},
            {"cxx","filetype-code"},{"hpp","filetype-code"},{"hxx","filetype-code"},{"py","filetype-code"},
            {"java","filetype-code"},{"js","filetype-code"},{"jsx","filetype-code"},{"mjs","filetype-code"},
            {"cjs","filetype-code"},{"ts","filetype-code"},{"tsx","filetype-code"},{"go","filetype-code"},
            {"rs","filetype-code"},{"rb","filetype-code"},{"php","filetype-code"},{"cs","filetype-code"},
            {"swift","filetype-code"},{"kt","filetype-code"},{"kts","filetype-code"},{"sh","filetype-code"},
            {"bash","filetype-code"},{"zsh","filetype-code"},{"pl","filetype-code"},{"lua","filetype-code"},
            {"r","filetype-code"},{"m","filetype-code"},{"mm","filetype-code"},{"scala","filetype-code"},
            {"dart","filetype-code"},{"sql","filetype-code"},{"json","filetype-code"},{"yaml","filetype-code"},
            {"yml","filetype-code"},{"toml","filetype-code"},{"xml","filetype-code"},{"html","filetype-code"},
            {"htm","filetype-code"},{"css","filetype-code"},{"scss","filetype-code"},{"less","filetype-code"},
            {"vue","filetype-code"},{"ps1","filetype-code"},{"bat","filetype-code"},{"cmake","filetype-code"},
            {"ini","filetype-code"},{"cfg","filetype-code"},{"conf","filetype-code"},{"md","filetype-code"},
            {"markdown","filetype-code"},
            // documents
            {"pdf","filetype-document"},{"doc","filetype-document"},{"docx","filetype-document"},
            {"odt","filetype-document"},{"rtf","filetype-document"},{"xls","filetype-document"},
            {"xlsx","filetype-document"},{"csv","filetype-document"},{"ods","filetype-document"},
            {"ppt","filetype-document"},{"pptx","filetype-document"},{"odp","filetype-document"},
            // images
            {"png","filetype-image"},{"jpg","filetype-image"},{"jpeg","filetype-image"},{"gif","filetype-image"},
            {"bmp","filetype-image"},{"ico","filetype-image"},{"svg","filetype-image"},{"webp","filetype-image"},
            {"tiff","filetype-image"},{"tif","filetype-image"},
            // archives
            {"zip","filetype-archive"},{"tar","filetype-archive"},{"gz","filetype-archive"},
            {"tgz","filetype-archive"},{"7z","filetype-archive"},{"rar","filetype-archive"},
            {"bz2","filetype-archive"},{"xz","filetype-archive"},{"iso","filetype-archive"},
            // audio
            {"mp3","filetype-audio"},{"wav","filetype-audio"},{"flac","filetype-audio"},{"ogg","filetype-audio"},
            {"m4a","filetype-audio"},{"aac","filetype-audio"},{"wma","filetype-audio"},
            // video
            {"mp4","filetype-video"},{"avi","filetype-video"},{"mkv","filetype-video"},{"mov","filetype-video"},
            {"webm","filetype-video"},{"flv","filetype-video"},{"wmv","filetype-video"},{"m4v","filetype-video"},
        };
        for (const auto& e : kExtIcons)
        {
            const int id = wxTheFileIconsTable->GetIconID(e.ext);
            if (id != wxFileIconsTable::file) il->Replace(id, icon(e.iconName).GetBitmap(sz));
        }

        // Dotfiles with no "real" extension (.gitattributes, .gitignore, ...) can't go through the
        // kExtIcons/GetIconID mechanism above: GetIconID caches its OWN OS-lookup result per key
        // BEFORE we get a chance to override it, and since these typically have NO OS registration,
        // it caches the SHARED generic `file` id - which the skip-guard above correctly declines to
        // overwrite (doing so would reskin every other unmatched file too). So they get a dedicated
        // image-list slot instead, applied by exact-filename match via applyBrowserIcons() below,
        // entirely bypassing wxGenericDirCtrl's built-in icon resolution for just this curated set.
        m_dotfileIconIdx = il->Add(icon("filetype-code").GetBitmap(sz));

        // Everything above only PRE-EMPTS the OS lookup, which makes it a whitelist: any extension not in
        // kExtIcons that Windows/xdg DOES have registered still gets a live OS icon, because
        // wxGenericDirCtrl calls GetIconID() for it at populate time and allocates a fresh image-list slot
        // we never touched (".txt" was one such hole - hence a Notepad icon on CMakeLists.txt). Rather than
        // keep chasing extensions, add our OWN slots here and assign them per item in applyBrowserIcons(),
        // which bypasses wxGenericDirCtrl's icon resolution entirely for files. Unmatched files get our
        // generic "file" icon, so an OS icon can no longer appear whatever the machine has registered.
        for (const auto& e : kExtIcons)
        {
            auto it = m_iconIdxByName.find(e.iconName);
            if (it == m_iconIdxByName.end())
                it = m_iconIdxByName.emplace(e.iconName, il->Add(icon(e.iconName).GetBitmap(sz))).first;
            m_extToIconIdx[wxString::FromAscii(e.ext)] = it->second;
        }
        m_genericFileIconIdx = il->Add(icon("file").GetBitmap(sz));
    }
    int m_dotfileIconIdx = -1;
    int m_genericFileIconIdx = -1;
    std::map<wxString, int> m_iconIdxByName;   // our icon name -> image-list slot (deduped)
    std::map<wxString, int> m_extToIconIdx;    // lowercased extension -> image-list slot
    // Filenames matched LITERALLY (unlike kExtIcons' AfterLast('.') suffix match), so e.g.
    // "config.env"/"production.env" are correctly left alone - only the exact dotfile gets reskinned.
    static bool isCuratedDotfile(const wxString& name)
    {
        static const wxString kNames[] = {
            ".gitattributes", ".gitignore", ".gitmodules", ".editorconfig", ".dockerignore",
            ".npmignore", ".eslintrc", ".prettierrc", ".babelrc", ".env", ".nvmrc", ".yarnrc",
        };
        for (const wxString& n : kNames) if (name == n) return true;
        return false;
    }
    // Re-images any curated dotfile among `parent`'s direct children. Run once for the file browser's
    // root (already populated synchronously at construction) and again on every EVT_TREE_ITEM_EXPANDED,
    // since wxGenericDirCtrl lazily populates a folder's children only when the user expands it.
    // Our icon slot for a filename: exact-name pass first (dotfiles have no real extension), then a
    // case-insensitive extension match, else our generic file icon. Never returns an OS-derived slot.
    int browserIconFor(const wxString& name) const
    {
        if (isCuratedDotfile(name)) return m_dotfileIconIdx;
        const int dot = name.Find('.', /*fromEnd=*/true);
        if (dot != wxNOT_FOUND && dot != 0)
        {
            const auto it = m_extToIconIdx.find(name.Mid(dot + 1).Lower());
            if (it != m_extToIconIdx.end()) return it->second;
        }
        return m_genericFileIconIdx;   // no extension (LICENSE, NOTICE) or an unmapped one
    }
    // Re-images `parent`'s direct FILE children with our own icons. Folders are left alone - their slots
    // (folder/folder_open) were already replaced wholesale above. Run for the file browser's root (already
    // populated synchronously at construction) and again on every EVT_TREE_ITEM_EXPANDED, since
    // wxGenericDirCtrl populates a folder's children only when the user expands it.
    void applyBrowserIcons(wxTreeCtrl* tree, const wxTreeItemId& parent)
    {
        if (!tree || !parent.IsOk() || m_genericFileIconIdx < 0) return;
        wxTreeItemIdValue cookie;
        for (wxTreeItemId child = tree->GetFirstChild(parent, cookie); child.IsOk(); child = tree->GetNextChild(parent, cookie))
        {
            // wxGenericDirCtrl hangs a wxDirItemData off every item; m_isDir is the reliable way to tell a
            // folder from a file here (the tree's own image would already be the thing we're replacing).
            const auto* d = dynamic_cast<wxDirItemData*>(tree->GetItemData(child));
            if (d && d->m_isDir) continue;
            const int idx = browserIconFor(tree->GetItemText(child));
            if (idx < 0) continue;
            tree->SetItemImage(child, idx, wxTreeItemIcon_Normal);
            tree->SetItemImage(child, idx, wxTreeItemIcon_Selected);
        }
    }
    void createFileBrowser(const wxString& root)
    {
        patchFileBrowserIcons();
        wxAuiPaneInfo paneInfo;
        bool hadPane = false;
        if (m_fileBrowser)
        {
            wxAuiPaneInfo& pi = m_aui.GetPane(m_fileBrowser);
            if (pi.IsOk()) { paneInfo = pi; hadPane = true; }
            m_aui.DetachPane(m_fileBrowser);
            m_fileBrowser->Destroy();
        }
        m_fileBrowser = new wxGenericDirCtrl(this, wxID_ANY, root, wxDefaultPosition, wxDefaultSize,
                                             wxDIRCTRL_SHOW_FILTERS | wxBORDER_NONE);
        if (auto* tree = m_fileBrowser->GetTreeCtrl())
        {
            themeToEditor(tree);   // theme the tree to the editor's colours
            applyBrowserIcons(tree, tree->GetRootItem());   // root's children are already populated synchronously
            tree->Bind(wxEVT_TREE_ITEM_EXPANDED, [this, tree](wxTreeEvent& ev) { applyBrowserIcons(tree, ev.GetItem()); ev.Skip(); });
        }
        m_fileBrowser->Bind(wxEVT_DIRCTRL_FILEACTIVATED, [this](wxTreeEvent&) {
            const wxString f = m_fileBrowser->GetFilePath();
            if (!f.empty() && wxFileExists(f)) openPath(f);   // double-click a file -> open it
        });
        if (hadPane) m_aui.AddPane(m_fileBrowser, paneInfo);
        else m_aui.AddPane(m_fileBrowser, wxAuiPaneInfo().Name("filebrowser").Caption(_("Folder as Workspace"))
                                .Left().BestSize(240, 500).MinSize(140, 100).CloseButton(true).Hide());
    }
    void toggleFileBrowser()
    {
        if (!m_fileBrowser)
        {
            wxString root = curPath().empty() ? wxGetCwd() : wxFileName(curPath()).GetPath();   // start expanded at the current file's folder
            if (root.empty()) root = wxGetCwd();
            createFileBrowser(root);
        }
        togglePane(m_fileBrowser);
    }
    // Open Folder as Workspace / Containing Folder as Workspace / a positional directory on the command
    // line (`wxnote .`, via openFolderPath()): (re)root + reveal
    void showFileBrowserRooted(const wxString& root)
    {
        createFileBrowser(root);
        wxAuiPaneInfo& pi = m_aui.GetPane(m_fileBrowser);
        if (pi.IsOk()) { pi.Show(); m_aui.Update(); }
        setStatus(0, wxString::Format(_("Workspace: %s"), root)); m_hint = true;
    }
    void openFolderAsWorkspace()   // kCmdFileOpenFolderAsWorkspace - pick any folder
    {
        wxDirDialog dlg(this, _("Select Folder as Workspace"), curPath().empty() ? wxString() : wxFileName(curPath()).GetPath());
        if (dlg.ShowModal() == wxID_OK) showFileBrowserRooted(dlg.GetPath());
    }
    void containingFolderAsWorkspace()   // kCmdFileContainingFolderAsWorkspace - the active file's own folder
    {
        // Untitled buffer: root the workspace at the working directory (same fallback as the rest of the
        // Open Containing Folder submenu) rather than nagging about saving first.
        showFileBrowserRooted(curPath().empty() ? wxGetCwd() : wxFileName(curPath()).GetPath());
    }

    // ---- Incremental Search (Ctrl+Alt+I): find-as-you-type bar; Enter = next, Esc = close ---------
    wxPanel*    m_incBar = nullptr;
    wxTextCtrl* m_incField = nullptr;
    wxToggleButton* m_incCaseBtn = nullptr; wxToggleButton* m_incWordBtn = nullptr; wxToggleButton* m_incRegexBtn = nullptr;
    wxStaticText*   m_incCount = nullptr;
    int         m_incAnchor = 0;
    int incFlags() const   // SCFIND_* flags from the incremental bar's three toggle buttons
    {
        int f = 0;
        if (m_incCaseBtn  && m_incCaseBtn->GetValue())  f |= SCFIND_MATCHCASE;
        if (m_incWordBtn  && m_incWordBtn->GetValue())  f |= SCFIND_WHOLEWORD;
        if (m_incRegexBtn && m_incRegexBtn->GetValue()) f |= SCFIND_REGEXP | SCFIND_CXX11REGEX;
        return f;
    }
    void buildIncBar()
    {
        m_incBar = new wxPanel(this);
        if (m_dark) m_incBar->SetBackgroundColour(wxColour(45, 45, 45));
        auto* sz  = new wxBoxSizer(wxHORIZONTAL);
        auto* lbl = new wxStaticText(m_incBar, wxID_ANY, _("Find: "));
        if (m_dark) lbl->SetForegroundColour(wxColour(220, 220, 220));
        m_incField = new wxTextCtrl(m_incBar, wxID_ANY, "", wxDefaultPosition, wxSize(220, -1), wxTE_PROCESS_ENTER);
        if (m_dark) { m_incField->SetBackgroundColour(wxColour(30, 30, 30)); m_incField->SetForegroundColour(wxColour(220, 220, 220)); }
        auto mkToggle = [&](const wxString& cap, const wxString& tip) {
            auto* b = new wxToggleButton(m_incBar, wxID_ANY, cap, wxDefaultPosition, wxSize(34, -1)); b->SetToolTip(tip); return b; };
        m_incCaseBtn  = mkToggle("Aa",  _("Match case"));
        m_incWordBtn  = mkToggle("\\b", _("Match whole word only"));
        m_incRegexBtn = mkToggle(".*",  _("Regular expression"));
        m_incCount = new wxStaticText(m_incBar, wxID_ANY, "", wxDefaultPosition, wxSize(108, -1));
        if (m_dark) m_incCount->SetForegroundColour(wxColour(170, 170, 170));
        auto* nextB  = new wxButton(m_incBar, wxID_ANY, _("Next"),  wxDefaultPosition, wxSize(60, -1));
        auto* closeB = new wxButton(m_incBar, wxID_ANY, _("Close"), wxDefaultPosition, wxSize(60, -1));
        sz->Add(lbl, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
        sz->Add(m_incField, 0, wxALIGN_CENTER_VERTICAL | wxALL, 4);
        sz->Add(m_incCaseBtn,  0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);
        sz->Add(m_incWordBtn,  0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);
        sz->Add(m_incRegexBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        sz->Add(m_incCount, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        sz->Add(nextB,  0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        sz->Add(closeB, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        m_incBar->SetSizer(sz);
        auto reFind = [this](wxCommandEvent&){ incFind(true); m_incField->SetFocus(); };   // a toggle changed -> re-search from the anchor
        m_incField->Bind(wxEVT_TEXT,       [this](wxCommandEvent&){ incFind(true);  });
        m_incField->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&){ incFind(false); });
        m_incField->Bind(wxEVT_CHAR_HOOK,  [this](wxKeyEvent& e){ if (e.GetKeyCode() == WXK_ESCAPE) hideIncBar(); else e.Skip(); });
        m_incCaseBtn->Bind(wxEVT_TOGGLEBUTTON,  reFind);
        m_incWordBtn->Bind(wxEVT_TOGGLEBUTTON,  reFind);
        m_incRegexBtn->Bind(wxEVT_TOGGLEBUTTON, reFind);
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
        if (needle.empty()) { sci(SCI_SETSEL, m_incAnchor, m_incAnchor); m_incField->SetForegroundColour(ok); m_incField->Refresh(); incUpdate(); return; }
        const int flags = incFlags();
        const int start = fromAnchor ? m_incAnchor : (int)sci(SCI_GETSELECTIONEND);
        sci(SCI_SETSEL, start, start);
        sci(SCI_SEARCHANCHOR);
        const wxScopedCharBuffer u = needle.utf8_str();
        sptr_t pos = sci(SCI_SEARCHNEXT, flags, reinterpret_cast<sptr_t>(u.data()));
        if (pos < 0) { sci(SCI_SETSEL, 0, 0); sci(SCI_SEARCHANCHOR); pos = sci(SCI_SEARCHNEXT, flags, reinterpret_cast<sptr_t>(u.data())); }   // wrap to top
        m_incField->SetForegroundColour(pos >= 0 ? ok : *wxRED);   // red field = not found
        m_incField->Refresh();
        if (pos >= 0) sci(SCI_SCROLLCARET);
        incUpdate();   // highlight every match + refresh the "n of m" counter
    }
    void incUpdate()   // highlight all matches (MARK_INDIC) and show "n of m" in the bar's counter
    {
        if (!m_incField || !m_incCount) return;
        clearMarks();
        const wxString needle = m_incField->GetValue();
        if (needle.empty()) { m_incCount->SetLabel(""); return; }
        const wxScopedCharBuffer u = needle.utf8_str();
        const int len = (int)sci(SCI_GETLENGTH);
        const int selStart = (int)sci(SCI_GETSELECTIONSTART);
        const int CAP = 10000;   // bound per-keystroke work so a common letter in a huge file can't stall typing
        sci(SCI_SETSEARCHFLAGS, incFlags());
        sci(SCI_SETINDICATORCURRENT, MARK_INDIC);
        int total = 0, current = 0;
        sci(SCI_SETTARGETSTART, 0); sci(SCI_SETTARGETEND, len);
        while (total < CAP && sci(SCI_SEARCHINTARGET, u.length(), reinterpret_cast<sptr_t>(u.data())) >= 0)
        {
            const int s = (int)sci(SCI_GETTARGETSTART), e = (int)sci(SCI_GETTARGETEND);
            sci(SCI_INDICATORFILLRANGE, s, (e > s) ? e - s : 1);
            ++total;
            if (s == selStart) current = total;
            const int adv = (e > s) ? e : e + 1;   // step past zero-length (regex) matches
            if (adv >= len) break;
            sci(SCI_SETTARGETSTART, adv); sci(SCI_SETTARGETEND, len);
        }
        m_incCount->SetLabel(!total      ? wxString(_("No matches"))
                             : total >= CAP ? wxString::Format(_("%d+ matches"), CAP)
                             : current    ? wxString::Format(_("%d of %d"), current, total)
                                          : wxString::Format(_("%d matches"), total));
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
        clearMarks(); if (m_incCount) m_incCount->SetLabel("");   // drop the highlight-all + counter when the bar closes
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
        m_fifPanel->Bind(wxEVT_TREE_ITEM_ACTIVATED, &WxnShellFrameT::onFifActivate, this);
        m_aui.AddPane(m_fifPanel, wxAuiPaneInfo().Name("findresult").Caption(_("Find result"))
                          .Bottom().BestSize(800, 180).MinSize(150, 70).CloseButton(true).Hide());
        m_aui.Update();
    }
    void onFindInFiles()
    {
        wxDialog dlg(this, wxID_ANY, _("Find in Files"));
        auto* find = new wxTextCtrl(&dlg, wxID_ANY, selText());
        wxString dir; if (auto* p = activePage()) dir = wxPathOnly(p->path); if (dir.empty()) dir = wxGetCwd();
        auto* dirc = new wxTextCtrl(&dlg, wxID_ANY, dir);
        auto* filt = new wxTextCtrl(&dlg, wxID_ANY, "*.*");
        auto* cc = new wxCheckBox(&dlg, wxID_ANY, _("Match case"));
        auto* ww = new wxCheckBox(&dlg, wxID_ANY, _("Whole word only"));
        auto* rx = new wxCheckBox(&dlg, wxID_ANY, _("Regular expression"));
        auto* sd = new wxCheckBox(&dlg, wxID_ANY, _("In all sub-folders")); sd->SetValue(true);
        auto* gs = new wxFlexGridSizer(2, 8, 8); gs->AddGrowableCol(1);
        gs->Add(new wxStaticText(&dlg, wxID_ANY, _("Find what:")), 0, wxALIGN_CENTRE_VERTICAL); gs->Add(find, 1, wxEXPAND);
        gs->Add(new wxStaticText(&dlg, wxID_ANY, _("Directory:")), 0, wxALIGN_CENTRE_VERTICAL);
        auto* drow = new wxBoxSizer(wxHORIZONTAL); drow->Add(dirc, 1, wxEXPAND);
        auto* browse = new wxButton(&dlg, wxID_ANY, "...", wxDefaultPosition, wxSize(32, -1)); drow->Add(browse, 0, wxLEFT, 4);
        gs->Add(drow, 1, wxEXPAND);
        gs->Add(new wxStaticText(&dlg, wxID_ANY, _("Filters:")), 0, wxALIGN_CENTRE_VERTICAL); gs->Add(filt, 1, wxEXPAND);
        browse->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { wxDirDialog dd(&dlg, _("Choose folder"), dirc->GetValue()); if (dd.ShowModal() == wxID_OK) dirc->SetValue(dd.GetPath()); });
        auto* opt = new wxBoxSizer(wxHORIZONTAL); opt->Add(cc, 0, wxRIGHT, 12); opt->Add(ww, 0, wxRIGHT, 12); opt->Add(rx, 0, wxRIGHT, 12); opt->Add(sd, 0);
        auto* btn = new wxBoxSizer(wxHORIZONTAL); btn->AddStretchSpacer();
        auto* findAll = new wxButton(&dlg, wxID_OK, _("Find All")); findAll->SetDefault();   // Enter submits
        btn->Add(findAll, 0, wxRIGHT, 6); btn->Add(new wxButton(&dlg, wxID_CANCEL, _("Close")), 0);
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
    wxString substituteRunVars(wxString c)   // expand the Run dialog's $(...) variables
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
        wxDialog dlg(this, wxID_ANY, _("Run..."));
        auto* tc = new wxTextCtrl(&dlg, wxID_ANY, cmd, wxDefaultPosition, wxSize(520, -1));
        auto* browse = new wxButton(&dlg, wxID_ANY, "...", wxDefaultPosition, wxSize(32, -1));
        auto* row = new wxBoxSizer(wxHORIZONTAL); row->Add(tc, 1, wxEXPAND); row->Add(browse, 0, wxLEFT, 4);
        auto* btn = new wxBoxSizer(wxHORIZONTAL); btn->AddStretchSpacer();
        auto* runb = new wxButton(&dlg, wxID_OK, _("Run")); runb->SetDefault();
        btn->Add(runb, 0, wxRIGHT, 6); btn->Add(new wxButton(&dlg, wxID_CANCEL, _("Cancel")), 0);
        auto* top = new wxBoxSizer(wxVERTICAL);
        top->Add(new wxStaticText(&dlg, wxID_ANY, _("The Program to Run   (variables: $(FULL_CURRENT_PATH), $(CURRENT_DIRECTORY),\n$(FILE_NAME), $(CURRENT_WORD), $(CURRENT_LINE) ...)")), 0, wxALL, 12);
        top->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
        top->Add(btn, 0, wxEXPAND | wxALL, 12);
        dlg.SetSizerAndFit(top); dlg.SetSize(wxSize(540, dlg.GetSize().GetHeight()));
        tc->SetFocus(); tc->SetInsertionPointEnd();
        browse->Bind(wxEVT_BUTTON, [&](wxCommandEvent&){ wxFileDialog fd(&dlg, _("Select a program to run"), "", "", _("Programs (*.exe;*.bat;*.cmd)|*.exe;*.bat;*.cmd|All files (*.*)|*.*"), wxFD_OPEN | wxFD_FILE_MUST_EXIST); if (fd.ShowModal() == wxID_OK) tc->SetValue("\"" + fd.GetPath() + "\" \"$(FULL_CURRENT_PATH)\""); });
        themeDialog(&dlg);
        if (dlg.ShowModal() != wxID_OK) return;
        cmd = tc->GetValue().Trim().Trim(false); if (cmd.empty()) return;
        wxConfigBase::Get()->Write("RunCommand", cmd); wxConfigBase::Get()->Flush();
        const wxString full = substituteRunVars(cmd);
        if (wxExecute(full, wxEXEC_ASYNC) == 0) wxMessageBox(_("Failed to run:\n") + full, _("Run"), wxOK | wxICON_ERROR, this);
    }

    // ----- sessions (File > Save / Load Session + the nib.session/1 by-path surface) ------------------
    // Session XML is written in the portable Notepad++ session shape - root <NotepadPlus>, then
    // <Session activeView><mainView activeIndex><File filename lang position startPos endPos
    // firstVisibleLine><Mark line/></File>...</mainView><subView.../></Session> - so the file both
    // round-trips in wxNote AND parses as a Notepad++ session (loadSessionFromPath never checks the
    // root's own tag name, so older <wxNote>-rooted files still load). Beyond the file list, the active
    // tab persists its editing position: caret, first-visible line (scroll), and bookmark lines.
    // The interactive File > Save/Load Session commands and the by-path nib.session hooks (which the GPL
    // npp-bridge serves the Notepad++ *SESSION* messages from) share the three by-path helpers below.

    // Build a <File> node in the session shape. `pos`/`firstVisibleLine` capture the editing position
    // (0 for a non-active/plain entry); the caller adds any <Mark> children.
    static wxXmlNode* makeSessionFileNode(const wxString& filename, const wxString& lang, int pos, int firstVisibleLine)
    {
        auto* f = new wxXmlNode(wxXML_ELEMENT_NODE, "File");
        f->AddAttribute("firstVisibleLine", wxString::Format("%d", firstVisibleLine));
        f->AddAttribute("startPos", wxString::Format("%d", pos));   // Notepad++'s caret attributes
        f->AddAttribute("endPos",   wxString::Format("%d", pos));
        f->AddAttribute("position", wxString::Format("%d", pos));   // wxNote's own round-trip attribute
        f->AddAttribute("lang",     lang.empty() ? wxString("Normal Text") : lang);
        f->AddAttribute("filename", filename);
        return f;
    }
    // Save the currently-open on-disk documents (both views) as a session file. Returns the number of
    // files written (>= 0), or -1 on write failure. Only the globally-active page carries caret/marks.
    int saveSessionToPath(const wxString& path)
    {
        wxXmlDocument doc;
        auto* root = new wxXmlNode(wxXML_ELEMENT_NODE, "NotepadPlus");
        auto* sess = new wxXmlNode(wxXML_ELEMENT_NODE, "Session");
        sess->AddAttribute("activeView", m_active == &m_sub ? "1" : "0");
        root->AddChild(sess); doc.SetRoot(root);
        EditorPage* activeP = activePage();
        struct ViewSpec { wxAuiNotebook* tabs; const char* tag; };
        const ViewSpec views[2] = { { m_main.tabs, "mainView" }, { m_sub.tabs, "subView" } };
        int total = 0;
        for (const auto& vs : views)
        {
            auto* viewNode = new wxXmlNode(wxXML_ELEMENT_NODE, vs.tag);
            int saved = 0, sessActive = 0;
            if (vs.tabs) for (size_t i = 0; i < vs.tabs->GetPageCount(); ++i)
            {
                auto* p = static_cast<EditorPage*>(vs.tabs->GetPage(i));
                if (!p || p->path.empty()) continue;              // only real (saved) files go in a session
                wxXmlNode* f;
                if (p == activeP) {                               // the live editor holds this page's caret/scroll/marks
                    f = makeSessionFileNode(p->path, p->lang, (int)sci(SCI_GETCURRENTPOS), (int)sci(SCI_GETFIRSTVISIBLELINE));
                    int ln = -1; const int lc = (int)sci(SCI_GETLINECOUNT);
                    while ((ln = (int)sci(SCI_MARKERNEXT, ln + 1, 1 << MARK_BOOKMARK)) >= 0 && ln < lc)
                    { auto* mk = new wxXmlNode(wxXML_ELEMENT_NODE, "Mark"); mk->AddAttribute("line", wxString::Format("%d", ln)); f->AddChild(mk); }
                    sessActive = saved;
                } else {
                    f = makeSessionFileNode(p->path, p->lang, 0, 0);
                }
                viewNode->AddChild(f); ++saved; ++total;
            }
            viewNode->AddAttribute("activeIndex", wxString::Format("%d", sessActive));
            sess->AddChild(viewNode);
        }
        wxLogNull noLog;
        return doc.Save(path) ? total : -1;
    }
    // Save an explicit list of file paths as a session file (no editor state). Returns true on success.
    bool saveSessionFilesToPath(const wxString& path, const std::vector<wxString>& files)
    {
        wxXmlDocument doc;
        auto* root = new wxXmlNode(wxXML_ELEMENT_NODE, "NotepadPlus");
        auto* sess = new wxXmlNode(wxXML_ELEMENT_NODE, "Session"); sess->AddAttribute("activeView", "0");
        root->AddChild(sess); doc.SetRoot(root);
        auto* mainView = new wxXmlNode(wxXML_ELEMENT_NODE, "mainView"); mainView->AddAttribute("activeIndex", "0");
        for (const auto& fp : files) if (!fp.empty()) mainView->AddChild(makeSessionFileNode(fp, wxString(), 0, 0));
        sess->AddChild(mainView);
        auto* subView = new wxXmlNode(wxXML_ELEMENT_NODE, "subView"); subView->AddAttribute("activeIndex", "0");
        sess->AddChild(subView);                                  // Notepad++ always writes both views
        wxLogNull noLog;
        return doc.Save(path);
    }
    // Locate the <Session> node's view children under a session document's root (tolerant of the root
    // tag name, and of a bare <mainView> root). Returns the first view node to iterate from, or nullptr.
    static wxXmlNode* sessionViewScan(wxXmlDocument& doc)
    {
        if (!doc.GetRoot()) return nullptr;
        for (wxXmlNode* a = doc.GetRoot()->GetChildren(); a; a = a->GetNext())
            if (a->GetName() == "Session") return a->GetChildren();
        return doc.GetRoot()->GetChildren();                      // no <Session> wrapper: scan the root directly
    }
    // Enumerate the files a session file lists (across both views, in document order). Returns the count;
    // when `out` is non-null it is filled with the paths; when `valid` is non-null it is set true iff the
    // file parsed as a well-formed session (has at least one <mainView>/<subView> node).
    int sessionFileList(const wxString& path, std::vector<wxString>* out, bool* valid)
    {
        if (valid) *valid = false;
        wxXmlDocument doc;
        { wxLogNull noLog; if (!doc.Load(path) || !doc.GetRoot()) return 0; }
        int count = 0; bool anyView = false;
        for (wxXmlNode* v = sessionViewScan(doc); v; v = v->GetNext())
        {
            if (v->GetName() != "mainView" && v->GetName() != "subView") continue;
            anyView = true;
            for (wxXmlNode* f = v->GetChildren(); f; f = f->GetNext())
            {
                if (f->GetName() != "File") continue;
                const wxString fp = f->GetAttribute("filename");
                if (fp.empty()) continue;
                if (out) out->push_back(fp);
                ++count;
            }
        }
        if (valid) *valid = anyView;
        return count;
    }
    // Open every file listed in a session file (both views open into the active view). Returns the number
    // of files actually opened (>= 0) if the file parsed as a session, or -1 if it is unreadable / not a
    // session. Restores each opened file's caret/scroll/bookmarks.
    int loadSessionFromPath(const wxString& path)
    {
        wxXmlDocument doc;
        { wxLogNull noLog; if (!doc.Load(path) || !doc.GetRoot()) return -1; }
        std::vector<EditorPage*> opened;
        int mainActiveIndex = -1; bool anyView = false;
        for (wxXmlNode* v = sessionViewScan(doc); v; v = v->GetNext())
        {
            if (v->GetName() != "mainView" && v->GetName() != "subView") continue;
            anyView = true;
            const bool isMain = v->GetName() == "mainView";
            const int ai = wxAtoi(v->GetAttribute("activeIndex", "-1"));
            int localIdx = 0;
            for (wxXmlNode* f = v->GetChildren(); f; f = f->GetNext())
            {
                if (f->GetName() != "File") continue;
                const int idxHere = localIdx++;
                const wxString fp = f->GetAttribute("filename");
                if (fp.empty() || !wxFileExists(fp)) continue;
                openPath(fp);                                     // opens it in a new tab and makes it active
                sci(SCI_GOTOPOS, wxAtoi(f->GetAttribute("position", f->GetAttribute("startPos", "0"))));
                sci(SCI_SETFIRSTVISIBLELINE, wxAtoi(f->GetAttribute("firstVisibleLine", "0")));
                for (wxXmlNode* mk = f->GetChildren(); mk; mk = mk->GetNext())
                    if (mk->GetName() == "Mark") sci(SCI_MARKERADD, wxAtoi(mk->GetAttribute("line", "0")), MARK_BOOKMARK);
                if (isMain && idxHere == ai && mainActiveIndex < 0) mainActiveIndex = (int)opened.size();
                opened.push_back(activePage());
            }
        }
        if (!anyView) return -1;
        if (mainActiveIndex >= 0 && mainActiveIndex < (int)opened.size() && opened[mainActiveIndex] && m_tabs)
        { const int idx = m_tabs->GetPageIndex(opened[mainActiveIndex]); if (idx != wxNOT_FOUND) m_tabs->SetSelection(idx); }
        return (int)opened.size();
    }
    void saveSession()
    {
        if (!m_tabs) return;
        wxFileDialog d(this, _("Save Session"), "", "session.xml", _("Session files (*.xml)|*.xml|All files (*.*)|*.*"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (d.ShowModal() != wxID_OK) return;
        const int saved = saveSessionToPath(d.GetPath());
        if (saved >= 0) { setStatus(0, wxString::Format(_("Session saved - %d file(s)"), saved)); m_hint = true; }
    }
    void loadSession()
    {
        wxFileDialog d(this, _("Load Session"), "", "", _("Session files (*.xml)|*.xml|All files (*.*)|*.*"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (d.ShowModal() != wxID_OK) return;
        const int opened = loadSessionFromPath(d.GetPath());
        if (opened < 0) { wxMessageBox(_("Could not read the session file."), _("Load Session"), wxOK | wxICON_ERROR, this); return; }
        setStatus(0, wxString::Format(_("Session loaded - %d file(s)"), opened)); m_hint = true;
    }

    // ----- auto-completion (word / keyword / path) ----------------------
    // Three candidate sources, merged into one sorted SCI_AUTOCSHOW list: words already present in the
    // document that share the typed prefix (a linear scan - the document itself is the best dictionary
    // for its own identifiers), the active language's keyword list, and filesystem paths.
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
    // ----- function parameter call tips ("Function Parameters Hint") ----------------------
    // No API database is loaded, so signatures are harvested from the open document: each distinct
    // "name(...)" (plus any preceding return-type / def token) becomes an overload.
    std::vector<std::string> m_ctSigs; int m_ctIdx = 0; int m_ctOpen = -1;
    std::vector<std::string> callTipSigs(const std::string& name)
    {
        std::vector<std::string> out; std::set<std::string> seen;
        if (name.empty()) return out;
        const std::string doc = getDocUtf8();
        const std::string needle = name + "(";
        for (size_t pos = 0; (pos = doc.find(needle, pos)) != std::string::npos; )
        {
            if (pos && (std::isalnum((unsigned char)doc[pos - 1]) || doc[pos - 1] == '_')) { pos += 1; continue; }   // not a word boundary
            const size_t op = pos + name.size();   // the '('
            int depth = 0; size_t i = op;
            for (; i < doc.size() && i < op + 500; ++i) { const char c = doc[i]; if (c == '(') ++depth; else if (c == ')' && --depth == 0) { ++i; break; } }
            if (depth != 0) { pos = op; continue; }
            size_t s = pos;   // include a preceding return-type / 'def' token
            { size_t k = pos; while (k && (doc[k - 1] == ' ' || doc[k - 1] == '\t')) --k; const size_t we = k; while (k && (std::isalnum((unsigned char)doc[k - 1]) || doc[k - 1] == '_')) --k; if (k < we) s = k; }
            std::string norm; bool sp = false;   // collapse whitespace runs to single spaces
            for (size_t j = s; j < i; ++j) { const char c = doc[j]; if (c == '\n' || c == '\r' || c == '\t' || c == ' ') { if (!norm.empty()) sp = true; } else { if (sp) { norm += ' '; sp = false; } norm += c; } }
            if (norm.size() > 300) norm.resize(300);
            if (!norm.empty() && seen.insert(norm).second) out.push_back(norm);
            pos = i;
            if (out.size() >= 12) break;
        }
        return out;
    }
    int ctPrefixLen() const { return m_ctSigs.size() > 1 ? (int)(std::string("\001 ") + std::to_string(m_ctIdx + 1) + " of " + std::to_string((int)m_ctSigs.size()) + " \002").size() : 0; }
    void ctHighlight()   // box the argument the caret is currently in
    {
        if (m_ctSigs.empty() || m_ctOpen < 0 || !m_stc) return;
        const int caret = (int)sci(SCI_GETCURRENTPOS);
        int d = 0, arg = 0;
        for (int p = m_ctOpen + 1; p < caret; ++p) { const char c = (char)sci(SCI_GETCHARAT, p); if (c == '(' || c == '[') ++d; else if ((c == ']' || c == ')') && d) --d; else if (c == ',' && d == 0) ++arg; }
        const std::string& sig = m_ctSigs[m_ctIdx];
        const size_t lp = sig.find('(');
        if (lp == std::string::npos) return;
        int cur = 0, dd = 0; size_t ps = lp + 1, hs = std::string::npos, he = std::string::npos;
        for (size_t i = lp + 1; i < sig.size(); ++i) { const char c = sig[i]; if (c == '(' || c == '[') ++dd; else if (c == ']') { if (dd) --dd; } else if (c == ')') { if (dd == 0) { if (cur == arg) { hs = ps; he = i; } break; } --dd; } else if (c == ',' && dd == 0) { if (cur == arg) { hs = ps; he = i; break; } ++cur; ps = i + 1; } }
        if (hs == std::string::npos) return;
        while (hs < he && sig[hs] == ' ') ++hs;
        const int pre = ctPrefixLen();
        sci(SCI_CALLTIPSETHLT, pre + (int)hs, pre + (int)he);
    }
    void renderCallTip()
    {
        if (m_ctSigs.empty()) return;
        if (m_ctIdx < 0) m_ctIdx = (int)m_ctSigs.size() - 1; else if (m_ctIdx >= (int)m_ctSigs.size()) m_ctIdx = 0;
        const std::string txt = (m_ctSigs.size() > 1 ? std::string("\001 ") + std::to_string(m_ctIdx + 1) + " of " + std::to_string((int)m_ctSigs.size()) + " \002" : std::string()) + m_ctSigs[m_ctIdx];
        sci(SCI_CALLTIPSHOW, m_ctOpen, reinterpret_cast<sptr_t>(txt.c_str()));
        ctHighlight();
    }
    void funcCallTip()   // menu / '(' trigger: show the signature of the call enclosing the caret
    {
        if (!m_stc) return;
        const int caret = (int)sci(SCI_GETCURRENTPOS);
        int depth = 0, open = -1;
        for (int p = caret - 1; p >= 0 && p > caret - 4000; --p) { const char c = (char)sci(SCI_GETCHARAT, p); if (c == ')') ++depth; else if (c == '(') { if (depth == 0) { open = p; break; } --depth; } }
        if (open < 0) { sci(SCI_CALLTIPCANCEL); m_ctSigs.clear(); return; }
        int ne = open; while (ne && std::isspace((unsigned char)sci(SCI_GETCHARAT, ne - 1))) --ne;
        const int ns = (int)sci(SCI_WORDSTARTPOSITION, ne, 1);
        if (ns >= ne) { sci(SCI_CALLTIPCANCEL); m_ctSigs.clear(); return; }
        m_ctSigs = callTipSigs(rangeText(ns, ne));
        if (m_ctSigs.empty()) { sci(SCI_CALLTIPCANCEL); return; }
        m_ctIdx = 0; m_ctOpen = open; renderCallTip();
    }
    void callTipCaretMoved()   // keep the highlight in sync, dismiss once the caret leaves the call
    {
        if (m_ctSigs.empty() || !m_stc) return;
        if (!sci(SCI_CALLTIPACTIVE)) { m_ctSigs.clear(); return; }
        const int caret = (int)sci(SCI_GETCURRENTPOS);
        if (caret <= m_ctOpen) { sci(SCI_CALLTIPCANCEL); m_ctSigs.clear(); return; }
        int d = 0;
        for (int p = m_ctOpen + 1; p < caret; ++p) { const char c = (char)sci(SCI_GETCHARAT, p); if (c == '(') ++d; else if (c == ')') { if (d == 0) { sci(SCI_CALLTIPCANCEL); m_ctSigs.clear(); return; } --d; } }
        ctHighlight();
    }
    void onCallTipClick(wxStyledTextEvent& e) { const int p = (int)e.GetPosition(); if (p == 1) { --m_ctIdx; renderCallTip(); } else if (p == 2) { ++m_ctIdx; renderCallTip(); } }
    // ----- clickable URLs -----------------------------------------------------------------------------
    static bool urlEnd(char c) { return (unsigned char)c <= ' ' || std::strchr("<>\"{}|\\^`", c) != nullptr; }
    void markVisibleUrls()   // underline http(s)/ftp/www URLs in the on-screen lines
    {
        if (!m_stc) return;
        const int lines = (int)sci(SCI_GETLINECOUNT);
        const int fv = (int)sci(SCI_GETFIRSTVISIBLELINE), n = (int)sci(SCI_LINESONSCREEN);
        int a = (int)sci(SCI_DOCLINEFROMVISIBLE, fv), b = (int)sci(SCI_DOCLINEFROMVISIBLE, fv + n + 1);
        if (a < 0) a = 0; if (b >= lines) b = lines - 1; if (a > b) return;
        const int s = (int)sci(SCI_POSITIONFROMLINE, a), e = (int)sci(SCI_GETLINEENDPOSITION, b);
        if (e <= s || e - s > 200000) return;
        sci(SCI_SETINDICATORCURRENT, URL_INDIC);
        sci(SCI_INDICATORCLEARRANGE, s, e - s);
        const std::string t = rangeText(s, e);
        static const char* const schemes[] = { "https://", "http://", "ftp://", "www." };
        for (size_t i = 0; i < t.size(); )
        {
            size_t sl = 0;
            for (const char* sc : schemes) { const size_t l = std::strlen(sc); if (i + l <= t.size() && t.compare(i, l, sc) == 0 && (i == 0 || (unsigned char)t[i - 1] <= ' ' || std::strchr("(<\"'=", t[i - 1]))) { sl = l; break; } }
            if (!sl) { ++i; continue; }
            size_t j = i + sl;
            while (j < t.size() && !urlEnd(t[j])) ++j;
            while (j > i + sl && std::strchr(".,;:!?)]}'\"", t[j - 1])) --j;   // don't swallow trailing punctuation
            if (j > i + sl) sci(SCI_INDICATORFILLRANGE, s + (int)i, (int)(j - i));
            i = j;
        }
    }
    void onUrlClick(wxStyledTextEvent& e)   // click a URL -> open in the default browser
    {
        const int pos = (int)e.GetPosition();
        if (pos >= 0 && sci(SCI_INDICATORVALUEAT, URL_INDIC, pos))
        {
            const int s = (int)sci(SCI_INDICATORSTART, URL_INDIC, pos), en = (int)sci(SCI_INDICATOREND, URL_INDIC, pos);
            std::string url = rangeText(s, en);
            if (url.compare(0, 4, "www.") == 0) url = "http://" + url;
            if (!url.empty()) wxLaunchDefaultBrowser(wxString::FromUTF8(url.c_str()));
        }
        e.Skip();
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
        const wxTreeItemId head = m_fifPanel->AppendItem(root, wxString::Format(_("Search \"%s\"  (%d hits in %d files of %d searched)"),
                                                          term, (int)hits.size(), (int)files.size(), searched));
        m_fifPanel->SetItemBold(head, true);
        wxString cur; wxTreeItemId fnode;
        for (const auto& h : hits)
        {
            if (h.file != cur) { cur = h.file; fnode = m_fifPanel->AppendItem(head, h.file); m_fifPanel->SetItemBold(fnode, true); }
            wxString t = h.text; t.Trim(true).Trim(false);
            m_fifPanel->AppendItem(fnode, wxString::Format(_("Line %d:  %s"), h.line, t), -1, -1, new FifItemData(h.file, h.line));
        }
        m_fifPanel->ExpandAll();
        m_fifPanel->Thaw();
    }
    void onFifActivate(wxTreeEvent& e)
    {
        if (auto* d = m_fifPanel ? dynamic_cast<FifItemData*>(m_fifPanel->GetItemData(e.GetItem())) : nullptr)
            gotoResult(d->file, d->line - 1);   // shared jump: dedups the tab and centres the line
        e.Skip();
    }

    // The colour wxAuiFlatTabArt actually paints the tab-strip background with: m_bgWindow, which its
    // Data::InitColours derives as wxAuiDimColour(wxSYS_COLOUR_WINDOW, 5) (see the vendored
    // aui/tabart.cpp). The caption bar (m_capBar) is an opaque wxPanel Raise()'d over the strip, so its
    // own background has to be that exact colour or it shows as a distinct block. On Windows the strip's
    // system-derived colour happens to sit close enough to our hardcoded chrome that the seam isn't
    // noticed; on GTK the system WINDOW colour is theme-driven and clearly different, so match the strip
    // by replicating the same formula there. (m_tabs->GetBackgroundColour() is NOT the strip colour -
    // the flat art repaints over it - so it can't be used here.)
    static wxColour tabStripBg()
    {
        const wxColour c = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
        const bool isDarkColour = c.GetLuminance() < 0.5;
        const int ialpha = 100 + (isDarkColour != wxSystemSettings::GetAppearance().IsDark() ? 5 : -5);
        return c.ChangeLightness(ialpha);
    }

    // The tab strip's caption buttons (new / document-list / close), overlaid at its right edge.
    void buildTabCaptionButtons()
    {
        m_capBar = new wxPanel(m_tabs);
#ifdef __WXMSW__
        m_capBar->SetBackgroundColour(m_dark ? wxColour(32, 32, 32) : wxColour(240, 240, 240));
#else
        m_capBar->SetBackgroundColour(tabStripBg());   // match the flat tab-art's actual strip colour on GTK/Cocoa
#endif
        const wxColour capBg = m_capBar->GetBackgroundColour();
#ifdef __WXGTK__
        // On GTK a wxBitmapButton carries the theme's button min-height/padding, so the +/v/x row ends up
        // TALLER than the tab strip and overflows it. TitleBarBtn is a plain custom-painted wxWindow with no
        // native button chrome, so we size it exactly to the strip height and it always fits. (MSW/macOS keep
        // wxBitmapButton, where the fit is fine.)
        // this->FromDIP: FromDIP is a dependent-base (wxWindowBase) member and this is a class template
        // (WxnShellFrameT<FB>), so GCC/Clang two-phase lookup rejects the unqualified call (MSVC is lax).
        int capH = m_tabs->GetTabCtrlHeight(); if (capH < this->FromDIP(18)) capH = this->FromDIP(24);
        capH -= this->FromDIP(2);   // full-strip-height buttons paint over the strip's 1px top separator line; shrink so centering leaves a sliver above and below
        const wxSize capBtnSize(this->FromDIP(24), capH);
        const wxColour capHot = m_dark ? wxColour(62, 62, 62) : wxColour(210, 210, 210);
#endif
        auto* s = new wxBoxSizer(wxHORIZONTAL);
        auto mkBtn = [&](const char* path, const wxString& tip, std::function<void()> act) {
#ifdef __WXGTK__
            wxWindow* b = new TitleBarBtn(m_capBar, wxID_ANY, capBtnSize, captionIcon(path), capBg, capHot, 16);   // 16px glyph so the +/v/x aren't blurry (captionIcon SVGs are drawn for 16px)
#else
            wxWindow* b = new wxBitmapButton(m_capBar, wxID_ANY, captionIcon(path), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxBU_EXACTFIT);
            b->SetBackgroundColour(capBg);
#endif
            b->SetToolTip(tip);
            b->Bind(wxEVT_BUTTON, [act](wxCommandEvent&) { act(); });
            s->Add(b, 0, wxALIGN_CENTRE_VERTICAL | wxLEFT, 2);
        };
        mkBtn("M8 4.5 V11.5 M4.5 8 H11.5",               _("New"),            [this] { doNew(); });   // span 4.5..11.5, matching the "x"
        mkBtn("M3.5 6.5 L8 11 L12.5 6.5",                _("Open documents"), [this] { onDocList(); });
        mkBtn("M4.5 4.5 L11.5 11.5 M11.5 4.5 L4.5 11.5", _("Close current"),  [this] { closeActive(); });
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
    wxString nextNewName() { return wxString::Format(_("new %d"), ++m_newCount); }   // untitled-buffer name, localized ("nowy N" / "neu N" / ...)

    // Create a new document tab (panel + its own native Scintilla) and make it active.
    EditorPage* addDocument(const wxString& path, const wxString& title)
    {
        auto* page = new EditorPage(m_tabs);
        page->Bind(wxEVT_SIZE, [this, page](wxSizeEvent& e) {
            if (m_stc && m_stc->GetParent() == page) m_stc->SetSize(page->GetClientSize());   // only the page hosting the view resizes it
            e.Skip();
        });
        page->doc = sci(SCI_CREATEDOCUMENT, 0, SC_DOCUMENTOPTION_TEXT_LARGE);   // the buffer owns one ref
        page->path = path; page->title = title;
        m_tabs->AddPage(page, title, true);    // selecting it fires PAGE_CHANGED -> activateBuffer
        activateBuffer(page);                  // ensure it (the first AddPage may not fire PAGE_CHANGED); idempotent
        if (!path.empty())
        {
            // v4: a global "a file is about to load from disk" signal (id 0, before the content is read),
            // ahead of the id-carrying BEFORE_OPEN. The N++ bridge maps it to NPPN_FILEBEFORELOAD.
            nibFireDocEvent(NIB_EV_DOCUMENT_BEFORE_LOAD, nullptr);
            // Notify subscribers (nib.events v2 -> the N++ bridge's NPPN_FILEBEFOREOPEN) that this file
            // document exists but its content is about to load. Fired AFTER AddPage on purpose: the page
            // is already enumerable, so the id and its path resolve through nib.documents from inside the
            // callback (the ordering-guarantee sibling of DOCUMENT_CLOSED). Real files only, like OPENED.
            nibFireDocEvent(NIB_EV_DOCUMENT_BEFORE_OPEN, page);
            loadFile(path); addToMRU(path);
        }
        else { sci(SCI_SETSAVEPOINT); setLexerForFile(""); }
        setWindowTitle(title);
        updateStatus();
        // Notify subscribers (e.g. the N++ bridge -> NPPN_FILEOPENED) that a file document just opened.
        // Only for real files, not new empty tabs - matching Notepad++'s NPPN_FILEOPENED semantics.
        if (!path.empty()) nibFireDocEvent(NIB_EV_DOCUMENT_OPENED, page);
        return page;
    }

    // The bookmark margin marker, drawn from the project's own bookmark icon (resources/icons/bookmark.svg)
    // as an RGBA image so its Open-Color accent shows through, instead of a flat Scintilla marker shape.
    void defineBookmarkMarker()
    {
        static const wxString path = wxPathOnly(wxStandardPaths::Get().GetExecutablePath()) + wxFILE_SEP_PATH + "icons" + wxFILE_SEP_PATH + "bookmark.svg";
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

    // Preferences > Editing "Font" (m_fontFace), falling back to JetBrains Mono if the chosen font was
    // uninstalled since it was picked - IsValidFacename also happily reports true for JetBrains Mono
    // itself (privately loaded, but still visible to GDI's font enumeration within this same process).
    wxString effectiveFontFace() const
    {
        if (m_fontFace.empty() || !wxFontEnumerator::IsValidFacename(m_fontFace)) return "Cascadia Mono";
        return m_fontFace;
    }
    // Per-editor Scintilla configuration: margins, font, options, theme, scrollbars.
    void setupScintilla()
    {
        sci(SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);   // right-justified line numbers
        sci(SCI_SETMARGINWIDTHN, 1, 14);                 // bookmark/symbol margin
        sci(SCI_SETMARGINSENSITIVEN, 1, 1);
        defineBookmarkMarker();   // the project's own bookmark icon (resources/icons/bookmark.svg)
        sci(SCI_INDICSETSTYLE, MARK_INDIC, INDIC_ROUNDBOX);   // "Mark All" / found-highlight indicator
        sci(SCI_INDICSETFORE, MARK_INDIC, 0x00C800);
        sci(SCI_INDICSETALPHA, MARK_INDIC, 80);
        sci(SCI_INDICSETUNDER, MARK_INDIC, 1);   // paint the box UNDER the glyphs, not over them
        sci(SCI_INDICSETSTYLE, SMART_INDIC, INDIC_ROUNDBOX);  // smart-highlight (double-click a word)
        sci(SCI_INDICSETFORE, SMART_INDIC, 0x00C800);
        sci(SCI_INDICSETALPHA, SMART_INDIC, 70);
        // Without SETUNDER the translucent roundbox is composited ON TOP of the text; a double-click
        // both selects a word AND smart-highlights it, so the selected token got a green wash over an
        // already low-contrast pale selection - the two effects stacking is what made it unreadable.
        sci(SCI_INDICSETUNDER, SMART_INDIC, 1);
        sci(SCI_INDICSETOUTLINEALPHA, SMART_INDIC, 160);
        for (int i = 0; i < 5; ++i) {   // the 5 "Mark All Ext" styles
            sci(SCI_INDICSETSTYLE, MARK_STYLE_BASE + i, INDIC_ROUNDBOX);
            sci(SCI_INDICSETFORE, MARK_STYLE_BASE + i, MARK_STYLE_COLOUR[i]);
            sci(SCI_INDICSETALPHA, MARK_STYLE_BASE + i, 90);
            sci(SCI_INDICSETOUTLINEALPHA, MARK_STYLE_BASE + i, 180);
        }
        sci(SCI_INDICSETSTYLE, URL_INDIC, INDIC_PLAIN);        // clickable URLs: blue underline, click to open
        sci(SCI_INDICSETFORE, URL_INDIC, 0xCC6600);
        sci(SCI_INDICSETHOVERSTYLE, URL_INDIC, INDIC_PLAIN);
        sci(SCI_INDICSETHOVERFORE, URL_INDIC, 0xEE8822);
        // Default: JetBrains Mono (SIL OFL 1.1, bundled - see resources/fonts/), permissively-licensed
        // and present on every platform we ship, unlike Consolas (Microsoft's, Windows-only, not
        // redistributable). See WxnApp::OnInit's AddFontResourceEx call (Windows-only private
        // registration; Linux/macOS reference the family name directly and fall back to the system
        // default monospace font if it isn't separately installed there). User-overridable via
        // Preferences > Editing "Font"; effectiveFontFace() re-falls-back here if that choice
        // disappears (uninstalled) since it was picked.
        // ToUTF8() can return a buffer that's merely a view into the temporary wxString's own storage
        // (wxScopedCharTypeBuffer's documented "non-owned" mode) - safe only within the single expression
        // that creates it, NOT if split across statements (the temporary wxString would already be gone).
        sci(SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<sptr_t>(effectiveFontFace().ToUTF8().data()));
        sci(SCI_STYLESETSIZE, STYLE_DEFAULT, 11);
        sci(SCI_STYLECLEARALL);
        sci(SCI_SETTABWIDTH, 4);
        sci(SCI_SETSCROLLWIDTH, 1);
        sci(SCI_SETSCROLLWIDTHTRACKING, 1);
        sci(SCI_SETENDATLASTLINE, m_scrollBeyond ? 0 : 1);
        sci(SCI_SETCARETPERIOD, m_caretBlink);
        sci(SCI_USEPOPUP, SC_POPUP_NEVER);   // suppress Scintilla's light popup; we show our own themed menu
        // Column (Alt+drag) + multi-caret (Ctrl+click) selection, typing into all of them.
        sci(SCI_SETMULTIPLESELECTION, m_multiEdit ? 1 : 0);
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
        // Editor-command remaps: apply the store's overrides to THIS freshly-created view. At
        // startup the store isn't loaded yet (buildEditor precedes buildMenuBar), so this is a no-op and
        // reapplyEditorKeymaps() in buildMenuBar does the real apply to main+sub; for any view created after
        // the store is ready this remaps its editor keys on creation. The view starts at Scintilla's stock
        // keymap, so a plain apply (no undo) is correct here.
        if (m_keymapReady && m_stc) applyEditorOpsTo(m_stc, computeEditorOps(m_keymap));
    }
    // Code folding: margin 2 with a box-tree, automatic fold on margin click.
    void setupFolding()
    {
        sci(SCI_SETMARGINTYPEN, 2, SC_MARGIN_SYMBOL);
        sci(SCI_SETMARGINMASKN, 2, SC_MASK_FOLDERS);
        sci(SCI_SETMARGINWIDTHN, 2, 14);
        sci(SCI_SETMARGINSENSITIVEN, 2, 1);
        // Fold colour mapping (theme XML files define their "Fold" entries expecting exactly this):
        //   fold-margin background = "Fold margin" bg / fg
        //   marker FORE = "Fold" *bgColor*, marker BACK = "Fold" *fgColor*  (deliberately SWAPPED)
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
        const int symbols[7] = { SC_MARK_BOXMINUS, SC_MARK_BOXPLUS, SC_MARK_VLINE, SC_MARK_LCORNER,   // box-tree
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

    // Which view owns this page (by tab membership - robust to wxAui's internal page reparenting).
    ViewPane* viewOf(EditorPage* p)
    {
        if (p && m_main.tabs && m_main.tabs->GetPageIndex(p) != wxNOT_FOUND) return &m_main;
        if (p && m_sub.tabs  && m_sub.tabs->GetPageIndex(p)  != wxNOT_FOUND) return &m_sub;
        return nullptr;
    }
    // Repoint the active-view aliases (m_tabs/m_stc/m_sci) so every sci()/m_stc/m_tabs call site follows
    // focus. Returns true if the active view actually changed.
    bool setActiveView(ViewPane* v)
    {
        if (!v || !v->stc || m_active == v) return false;
        m_active = v; m_tabs = v->tabs; m_stc = v->stc;
#ifdef __WXMSW__
        m_sci = v->sci;
#endif
        return true;
    }
    // The view whose tab strip fired this notebook event (MAIN unless the SUB notebook fired).
    ViewPane* viewOfEvent(wxAuiNotebookEvent& e)
    { return dynamic_cast<wxAuiNotebook*>(e.GetEventObject()) == m_sub.tabs ? &m_sub : &m_main; }
    // Total open documents across BOTH views (>= 1 once the editor is built).
    int totalDocs() const
    { return (m_main.tabs ? (int)m_main.tabs->GetPageCount() : 0) + (m_sub.tabs ? (int)m_sub.tabs->GetPageCount() : 0); }
    // Every open document across both views (MAIN then SUB) - for cross-view ops (exit save-prompts).
    std::vector<EditorPage*> allPages()
    {
        std::vector<EditorPage*> v;
        for (wxAuiNotebook* nb : { m_main.tabs, m_sub.tabs })
            if (nb) for (size_t i = 0; i < nb->GetPageCount(); ++i) v.push_back(static_cast<EditorPage*>(nb->GetPage(i)));
        return v;
    }
    // The open page whose EditorPage* IS buffer id `id` (a nib.documents buffer id), across BOTH views;
    // nullptr if no open document has that id. The reverse of active_id/id_at. (allPages() not the m_tabs
    // alias, so a buffer living in the SUB view is found too - the cross-view lifecycle invariant.)
    EditorPage* pageFromId(intptr_t id)
    {
        if (!id) return nullptr;
        for (EditorPage* p : allPages()) if (reinterpret_cast<intptr_t>(p) == id) return p;
        return nullptr;
    }
    // Run `fn` with page p's Scintilla document mounted on the ACTIVE view's editor. If p is already the
    // active page, fn runs directly. Otherwise p's doc is swapped in via a raw doc-pointer swap (NOT
    // activateBuffer - the active view's lexer/status/minimap/tab selection are untouched) with editor
    // events quiet, and the active view's OWN document + caret/scroll are restored afterwards (a doc swap
    // clears the per-view selection - the documented doc-pointer-swap-loses-caret pitfall; the OTHER view
    // is never touched). Mirrors onSaveAll's background-buffer peek. Read-only or content-modifying `fn`.
    template <class Fn>
    void peekDoc(EditorPage* p, Fn&& fn)
    {
        if (!p || !m_stc) return;
        if (p == activePage()) { fn(); return; }
        const sptr_t savedDoc    = sci(SCI_GETDOCPOINTER);
        const int    savedAnchor = static_cast<int>(sci(SCI_GETANCHOR));
        const int    savedCaret  = static_cast<int>(sci(SCI_GETCURRENTPOS));
        const int    savedTop    = static_cast<int>(sci(SCI_GETFIRSTVISIBLELINE));
        sci(SCI_SETMODEVENTMASK, 0);                 // quiet: the active view must not react to the peeked doc
        sci(SCI_SETDOCPOINTER, 0, p->doc);
        fn();
        sci(SCI_SETDOCPOINTER, 0, savedDoc);         // remount the active view's own document...
        sci(SCI_SETSEL, savedAnchor, savedCaret);    // ...and restore its caret/selection (lost by the swap)
        sci(SCI_SETFIRSTVISIBLELINE, savedTop);
        sci(SCI_SETMODEVENTMASK, SC_MODEVENTMASKALL);
    }
    // Save the active document to `path`. asCopy: write a copy WITHOUT rebinding the active buffer to it
    // (its path/title/dirty/lexer are untouched, and no save events fire - it is a copy, not "the save");
    // else Save-As (writeFile rebinds path/title/lexer/MRU and fires the SAVING/SAVED events). Returns
    // false on write failure or if there is no active document.
    bool saveActiveAs(const wxString& path, bool asCopy)
    {
        EditorPage* p = activePage();
        if (!p || !m_stc) return false;
        return asCopy ? writeMountedDoc(p, path) : writeFile(path);
    }
    // Before deleting page `p`, move view `v`'s persistent editor off it (to the notebook, hidden) so the
    // page's destruction can't take the editor with it - matters when p is the view's only remaining page
    // (wxAui has no sibling page to re-home it onto). activateBuffer re-mounts it on the next activation.
    void detachViewEditor(ViewPane* v, EditorPage* p)
    { if (v && v->stc && v->stc->GetParent() == p) { v->stc->Hide(); v->stc->Reparent(v->tabs); } }
    // A view's editor gained focus: make it active and sync the chrome (status bar + minimap) to its doc.
    void onViewFocus(ViewPane* v)
    {
        if (!setActiveView(v)) return;
        if (auto* p = activePage()) {
            if (m_docMap) { m_docMap->SetDocPointer(reinterpret_cast<void*>(p->doc)); updateDocMapViewport(); }
            updateStatus();
            // Zoom lives in each wxStyledTextCtrl's own ViewStyle, so after a split the two views can hold
            // different zooms; without this the field would keep showing the other pane's number.
            syncZoomField();
        }
    }
    void onPageChanged(wxAuiNotebookEvent& e)
    {
        setActiveView(viewOfEvent(e));   // route by the notebook that fired
        if (auto* p = activePage()) { activateBuffer(p); noteMru(p); }
        e.Skip();
    }
    // ---- Most-recently-used tab order (Ctrl+Tab) + Restore Recent Closed File (Ctrl+Shift+T) ------
    std::vector<EditorPage*> m_mru;          // live pages, most-recently-active first
    std::vector<wxString>    m_closedFiles;  // recently-closed file paths (stack; back = most recent)
    void noteMru(EditorPage* p)              // record an activation; keep the list pruned to live pages
    {
        if (!p) return;
        for (size_t i = 0; i < m_mru.size(); ++i) if (m_mru[i] == p) { m_mru.erase(m_mru.begin() + i); break; }
        m_mru.insert(m_mru.begin(), p);
        const std::vector<EditorPage*> live = allPages();
        for (size_t i = m_mru.size(); i-- > 0;)
            if (std::find(live.begin(), live.end(), m_mru[i]) == live.end()) m_mru.erase(m_mru.begin() + i);
    }
    void activatePage(EditorPage* p)         // select p's tab (fires PAGE_CHANGED -> activateBuffer + noteMru)
    {
        ViewPane* v = viewOf(p); if (!v) return;
        setActiveView(v);
        const int i = v->tabs->GetPageIndex(p);
        if (i != wxNOT_FOUND) v->tabs->SetSelection(i);
    }
    void mruSwitch()                         // Ctrl+Tab: jump to the most-recently-used other document
    {
        const std::vector<EditorPage*> live = allPages();
        EditorPage* cur = activePage();
        for (EditorPage* p : m_mru)
            if (p != cur && std::find(live.begin(), live.end(), p) != live.end()) { activatePage(p); return; }
    }
    void moveTab(bool forward)               // View > Tab: shift the active tab one slot within its strip
    {
        if (!m_tabs) return;
        const int n = (int)m_tabs->GetPageCount(), i = m_tabs->GetSelection();
        if (i == wxNOT_FOUND || n < 2) return;
        const int j = forward ? i + 1 : i - 1;
        if (j < 0 || j >= n) return;         // already at the end - nothing to do
        EditorPage* p = static_cast<EditorPage*>(m_tabs->GetPage(i));
        const wxString cap = m_tabs->GetPageText(i);
        const wxBitmap bmp = m_tabs->GetPageBitmap(i);
        m_tabs->RemovePage(i);               // detach the tab (NOT delete - the page + its editor survive)
        m_tabs->InsertPage(j, p, cap, true, bmp);
        activateBuffer(p);
    }
    // Window > Sort By: reorders the active view's tab strip (matches moveTab's detach/reinsert pattern,
    // repeated selection-sort style - tab counts are small enough that O(n^2) is a non-issue).
    enum class TabSortKey { Name, Path, Type, Size, Modified };
    void sortTabs(TabSortKey key, bool ascending)
    {
        if (!m_tabs) return;
        const int n = (int)m_tabs->GetPageCount();
        if (n < 2) return;

        std::vector<EditorPage*> pages;
        for (int i = 0; i < n; ++i) pages.push_back(static_cast<EditorPage*>(m_tabs->GetPage(i)));

        // Content Length needs each page's document length, but only one Scintilla editor is shared across
        // a view's tabs - peek every other document via a raw doc-pointer swap (not activateBuffer's full
        // reparent/lexer/doc-map refresh) and restore whatever was attached before sorting started.
        std::map<EditorPage*, int> lengths;
        if (key == TabSortKey::Size && m_stc)
        {
            const sptr_t original = sci(SCI_GETDOCPOINTER);
            for (EditorPage* p : pages) { sci(SCI_SETDOCPOINTER, 0, p->doc); lengths[p] = (int)sci(SCI_GETLENGTH); }
            sci(SCI_SETDOCPOINTER, 0, original);
        }

        std::stable_sort(pages.begin(), pages.end(), [&](EditorPage* a, EditorPage* b) {
            int cmp = 0;
            switch (key)
            {
                case TabSortKey::Name: cmp = a->title.CmpNoCase(b->title); break;
                case TabSortKey::Path: cmp = a->path.CmpNoCase(b->path); break;
                case TabSortKey::Type:
                    cmp = wxFileName(a->path.empty() ? a->title : a->path).GetExt()
                              .CmpNoCase(wxFileName(b->path.empty() ? b->title : b->path).GetExt());
                    break;
                case TabSortKey::Size: cmp = lengths[a] - lengths[b]; break;
                case TabSortKey::Modified:
                {
                    wxDateTime ta, tb;
                    const bool ha = !a->path.empty() && wxFileName(a->path).GetTimes(nullptr, &ta, nullptr);
                    const bool hb = !b->path.empty() && wxFileName(b->path).GetTimes(nullptr, &tb, nullptr);
                    cmp = !ha && !hb ? 0 : !ha ? -1 : !hb ? 1 : (ta.IsEarlierThan(tb) ? -1 : (tb.IsEarlierThan(ta) ? 1 : 0));
                    break;
                }
            }
            return ascending ? cmp < 0 : cmp > 0;
        });

        EditorPage* keepActive = activePage();
        for (int i = 0; i < n; ++i)
        {
            EditorPage* p = pages[i];
            const int cur = m_tabs->GetPageIndex(p);   // re-queried each pass - prior moves shift everyone else's index
            if (cur == i) continue;
            const wxString cap = m_tabs->GetPageText(cur);
            const wxBitmap bmp = m_tabs->GetPageBitmap(cur);
            m_tabs->RemovePage(cur);
            m_tabs->InsertPage(i, p, cap, false, bmp);
        }
        if (keepActive) { const int idx = m_tabs->GetPageIndex(keepActive); if (idx != wxNOT_FOUND) m_tabs->SetSelection(idx); }
        setStatus(0, _("Tabs sorted")); m_hint = true;
        nibFireDocEvent(NIB_EV_DOC_ORDER_CHANGED, keepActive);   // -> NPPN_DOCORDERCHANGED (the tab order changed)
    }
    enum { kTabColourNone = 7100, kTabColourBase = 7101 };   // local ids for the tab "Apply Colour" submenu
    static wxColour tabPaletteColour(int i)
    { static const wxColour c[] = { wxColour(0xC8,0x3E,0x3E), wxColour(0xCF,0x8A,0x2E), wxColour(0x3F,0x9C,0x42), wxColour(0x37,0x7D,0xC8), wxColour(0x8A,0x4F,0xBE) };
      return (i >= 0 && i < 5) ? c[i] : wxColour(); }
    static wxString tabPaletteName(int i)
    {
        switch (i)   // translated at call time (the context menu is rebuilt on every open); literals stay visible to the msgid extractor
        {
            case 0: return _("Red"); case 1: return _("Orange"); case 2: return _("Green");
            case 3: return _("Blue"); case 4: return _("Purple"); default: return wxString();
        }
    }
    void applyTabColour(int idx)             // idx -1 = clear; 0..4 = palette entry; the tint is drawn by PinTabArt::DrawTab
    { if (auto* p = activePage()) { p->tabColour = (idx < 0) ? wxColour() : tabPaletteColour(idx); if (m_tabs) { m_tabs->Refresh(); m_tabs->Update(); } setStatus(0, idx < 0 ? _("Tab colour cleared") : _("Tab colour applied")); m_hint = true; } }
    void recordClosed(EditorPage* p)         // remember a closed file's path (deduped, capped)
    {
        if (!p || p->path.empty()) return;
        for (size_t i = 0; i < m_closedFiles.size(); ++i) if (m_closedFiles[i] == p->path) { m_closedFiles.erase(m_closedFiles.begin() + i); break; }
        m_closedFiles.push_back(p->path);
        if (g_waitMode) checkWaitDone(p->path);   // -w/--wait: this may have been the last file we were waiting on
        if (m_closedFiles.size() > 30) m_closedFiles.erase(m_closedFiles.begin());
    }
    // -w/--wait: a tab the launcher is blocked on just closed. When the last one goes, quit - that process
    // exit is what unblocks git. CallAfter, not a direct Close(): onPageClose defers the page's real
    // deletion to its own CallAfter, so tearing the frame down synchronously here would run under it.
    // Close() (vetoable, not Close(true)) so any OTHER modified tab still gets its save prompt.
    void checkWaitDone(const wxString& path)
    {
        for (size_t i = 0; i < g_waitPaths.GetCount(); ++i)
            // SameAs() normalises + compares case-insensitively on Windows, but it touches the filesystem,
            // so it says "not equal" for a file deleted while still open (git removes COMMIT_EDITMSG in
            // some flows) - fall back to a plain compare, or that path would never leave g_waitPaths and
            // the launcher would hang forever.
            if (wxFileName(g_waitPaths[i]).SameAs(wxFileName(path))
                || g_waitPaths[i].IsSameAs(path, /*caseSensitive=*/wxFileName::IsCaseSensitive()))
            {
                g_waitPaths.RemoveAt(i);
                if (g_waitPaths.IsEmpty()) this->CallAfter([this] { this->Close(); });
                return;
            }
    }
    void restoreLastClosed()                 // Ctrl+Shift+T: reopen the most-recently-closed file (skip vanished/already-open)
    {
        while (!m_closedFiles.empty())
        {
            const wxString path = m_closedFiles.back();
            m_closedFiles.pop_back();
            if (!wxFileExists(path)) continue;
            for (EditorPage* p : allPages()) if (p->path == path) { activatePage(p); return; }   // already open -> focus it
            openPath(path);
            return;
        }
    }
    // Mount the single view on the active page and swap to its document.
    void activateBuffer(EditorPage* p)
    {
        if (!p) return;
        m_beginEndSelectActive = false;      // a sticky Begin/End Select anchor doesn't carry across documents
        setActiveView(viewOf(p));            // mount on the view whose tab strip owns this page
        if (!m_stc) return;
        if (m_stc->GetParent() == p && sci(SCI_GETDOCPOINTER) == p->doc)
        { m_stc->SetFocus(); return; }                                      // already showing this buffer - nothing to do
        if (m_stc->GetParent() != p) m_stc->Reparent(p);                    // the one view hops onto the active page (wx-tracked)
        m_stc->SetSize(p->GetClientSize());
        m_stc->Show();
        sci(SCI_SETMODEVENTMASK, 0);                                         // quiet during the swap
        sci(SCI_SETDOCPOINTER, 0, p->doc);                                   // the magical switch
        sci(SCI_SETMODEVENTMASK, SC_MODEVENTMASKALL);
        m_path = p->path;
        setLexerForFile(p->path);                                           // re-apply lexer/styling for this doc
        m_lastFoldSection = -2; refreshFoldNestedAccent();                  // the nested-square accent marker is global to the view - re-evaluate it for the swapped-in document
        if (m_docMap) { m_docMap->SetDocPointer(reinterpret_cast<void*>(p->doc)); updateDocMapViewport(); }   // minimap follows the active doc
        parseFuncList();                                                    // re-parse symbols for the newly active doc
        refreshDocList();                                                   // keep the Document List panel + its selection in sync
        refreshTab(p);
        updateStatus();
        updateEncodingMenuChecks();   // tick this buffer's encoding in the Encoding menu
        syncMonitoringUi(p);          // the Monitoring check is per-tab state
        if (p->monitored)   // catch up on external changes that happened while this tab was backgrounded
        {
            wxLongLong mt; wxULongLong sz;
            if (monStat(p->path, mt, sz) && (mt != p->monMtime || sz != p->monSize))
            {
                const wxULongLong oldSize = p->monSize;
                p->monMtime = mt; p->monSize = sz;
                // Same dirty guard as the live poll: NEVER reload over unsaved edits (a silent reload here
                // would also hit SCI_SETSAVEPOINT and defeat the close-time save prompt).
                if (sci(SCI_GETMODIFY)) { p->monitored = false; syncMonitoringUi(p); monStatus(_("Monitoring stopped (document was edited)")); updateMonTimer(); }
                else monReloadTail(p, oldSize);
            }
        }
        m_stc->SetFocus();
        // Notify subscribers (e.g. the N++ bridge -> NPPN_BUFFERACTIVATED) that this document is now active.
        NibEvent ev{}; ev.kind = NIB_EV_DOCUMENT_ACTIVATED; ev.struct_size = sizeof(NibEvent);
        ev.as.document.id = reinterpret_cast<intptr_t>(p); nibFireEvent(ev);
    }
    // ----- user-writable app data ------------------------------------------------------------
    // Per-user, WRITABLE app-data root - per-user User-Defined Languages (userDefineLangs/) and an
    // edited contextMenu.xml live here, NOT next to the exe, which is read-only on an installed build
    // (/opt/wxnote on Linux, Program Files on Windows, inside the .app bundle on macOS). This is the
    // same identity wxConfig persists settings/theme under. Kept a pure path getter (it's called on
    // hot paths like every right-click); callers that WRITE create it first (Mkdir with FULL).
    wxString userDataDir() { return wxStandardPaths::Get().GetUserDataDir(); }

    // ----- unsaved-changes recovery (Preferences > General "Ask before closing unsaved changes", off
    // by default) - when a modified document is discarded WITHOUT prompting, its content is backed up
    // to <userDataDir>/RecoveryBackups/<id>.bak first, so it survives that close (or a later crash/relaunch)
    // instead of being silently lost. The manifest is a set of wxConfig groups keyed by that same id
    // (Recovery/<id>/Path, Recovery/<id>/Title) - independent of Session/File* (which only tracks
    // "what's currently open" and is fully rewritten on every exit); an id is only removed once its
    // content is safely saved to disk for real (see writeFile()). Backups live under userDataDir() (NOT
    // <exe>/): the install dir isn't user-writable, so a write there used to fail with ENOENT (its parent
    // RecoveryBackups dir couldn't be created) and pop a wxLog error dialog.
    wxString recoveryDir() { return userDataDir() + wxFILE_SEP_PATH + "RecoveryBackups"; }
    wxString generateRecoveryId()
    {
        auto* cfg = wxConfigBase::Get();
        long next = 1; cfg->Read("Recovery/NextId", &next, 1L);
        cfg->Write("Recovery/NextId", next + 1);
        return wxString::Format("r%ld", next);
    }
    void backupUnsavedChanges(EditorPage* p)
    {
        if (!p) return;
        // An empty untitled buffer holds no work to recover: backing it up would resurrect an empty "new 1"
        // ghost tab on the next launch (and leak an empty .bak + a stale Recovery/ config group every quit).
        // confirmClose has already activated p, so getAllText() reflects it. Clear any stale backup and bail.
        if (p->path.empty() && getAllText().empty()) { clearRecovery(p); return; }
        if (p->recoveryId.empty()) p->recoveryId = generateRecoveryId();
        const wxString dir = recoveryDir();
        wxLogNull noLog;   // best-effort safety net: a failed backup must never surface a wxLog error dialog
        if (!wxDirExists(dir)) wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        wxFile f(dir + wxFILE_SEP_PATH + p->recoveryId + ".bak", wxFile::write);
        if (!f.IsOpened()) return;   // couldn't write the backup - don't record a manifest entry pointing at a file that isn't there
        const wxScopedCharBuffer u = getAllText().ToUTF8(); f.Write(u.data(), u.length());
        auto* cfg = wxConfigBase::Get();
        cfg->Write("Recovery/" + p->recoveryId + "/Path", p->path);
        cfg->Write("Recovery/" + p->recoveryId + "/Title", p->title);
        cfg->Flush();
    }
    void clearRecovery(EditorPage* p)
    {
        if (!p || p->recoveryId.empty()) return;
        wxConfigBase::Get()->DeleteGroup("Recovery/" + p->recoveryId);
        { wxLogNull noLog; wxRemoveFile(recoveryDir() + wxFILE_SEP_PATH + p->recoveryId + ".bak"); }   // best-effort: the .bak may already be gone
        p->recoveryId.clear();
    }
    // Ask to save a modified document before closing it (Save / Don't Save / Cancel), themed like the
    // rest of the app. Returns true if the caller may close the page, false if the user cancelled.
    // exiting=true (only from onCloseWindow) means this discard is the app quitting with unsaved content
    // still open - back it up so it can be recovered next launch. A deliberate in-session tab close
    // (exiting=false, all other call sites) is a final decision - clear any stale recovery instead, or
    // discarded scratch tabs would resurrect as ghost tabs on every future launch forever.
    bool confirmClose(EditorPage* p, bool exiting = false)
    {
        if (!p) return true;
        setActiveView(viewOf(p));            // make p's view active so sci()/m_path/onSave refer to p (incl. the OTHER split view)
        activateBuffer(p);                   // swap that view to p's document and select its tab
        if (sci(SCI_GETMODIFY) == 0) return true;
        if (!m_askBeforeClose) { if (exiting) backupUnsavedChanges(p); else clearRecovery(p); return true; }   // setting off (the default): discard silently, no prompt
        const wxString name = !p->path.empty() ? p->path : (p->title.empty() ? wxString("new") : p->title);

        wxDialog dlg(this, wxID_ANY, "wxNote");
        auto* s = new wxBoxSizer(wxVERTICAL);
        s->Add(new wxStaticText(&dlg, wxID_ANY, wxString::Format(_("Save file\n%s ?"), name)), 0, wxALL, 16);
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* bSave = new wxButton(&dlg, wxID_YES, _("&Save"));
        auto* bNo   = new wxButton(&dlg, wxID_NO, _("Do&n't Save"));
        auto* bCan  = new wxButton(&dlg, wxID_CANCEL, _("&Cancel"));
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
        clearRecovery(p);   // Don't Save -> discard; an explicit, informed choice needs no safety net
        return true;
    }
    void onPageClose(wxAuiNotebookEvent& e)
    {
        setActiveView(viewOfEvent(e));   // close from the view whose tab fired
        auto* p = static_cast<EditorPage*>(m_tabs->GetPage(e.GetSelection()));
        if (!confirmClose(p)) { e.Veto(); return; }
        recordClosed(p);                                                                     // remember it for Restore Recent Closed File
        if (totalDocs() <= 1)
        {   // Never zero documents: the last page is recycled into a fresh untitled buffer instead of
            // being deleted. For plugins that recycle IS a close (real N++ fires NPPN_FILEBEFORECLOSE/
            // NPPN_FILECLOSED here too, then re-uses the tab for "new 1"), so the pair still fires -
            // BEFORE resetActiveDoc(), while the page's path/id are still resolvable (see nib.h).
            e.Veto();
            nibFireDocEvent(NIB_EV_DOCUMENT_CLOSED, p);
            resetActiveDoc();
            return;
        }
        // Remove the page ourselves (after this event) so the collapse check sees the real count - wxAui's
        // own removal races a CallAfter, which would leave an empty pane behind.
        e.Veto();
        // Notify subscribers (e.g. the N++ bridge -> NPPN_FILEBEFORECLOSE + NPPN_FILECLOSED) BEFORE the
        // page is destroyed, so a plugin can still resolve its path/buffer id from `p` (deletion is
        // deferred to the CallAfter). Every close path fires this with the same guarantee - see nib.h.
        nibFireDocEvent(NIB_EV_DOCUMENT_CLOSED, p);
        this->CallAfter([this, p]{
            if (ViewPane* v = viewOf(p)) {
                const int i = v->tabs->GetPageIndex(p);
                if (i != wxNOT_FOUND) {
                    detachViewEditor(v, p);   // lift the editor off the page so its deletion can't take it (last-page case)
                    v->tabs->DeletePage(i);
                }
            }
            collapseIfEmpty();
        });
    }
    void closeActive()
    {
        if (!confirmClose(activePage())) return;
        recordClosed(activePage());                                                          // remember it for Restore Recent Closed File
        if (totalDocs() <= 1)
        {   // Last document - keep one empty (the buffer-recycle path; see onPageClose): still a
            // close for plugins, so fire the event before the recycle wipes path/title.
            nibFireDocEvent(NIB_EV_DOCUMENT_CLOSED, activePage());
            resetActiveDoc();
            return;
        }
        nibFireDocEvent(NIB_EV_DOCUMENT_CLOSED, activePage());   // before teardown: path/id still resolvable (see nib.h)
        detachViewEditor(m_active, activePage());   // lift the editor off the page before deleting it
        m_tabs->DeletePage(m_tabs->GetSelection());
        collapseIfEmpty();
    }
    void closeAll()
    {
        for (EditorPage* p : allPages())
            if (!confirmClose(p)) return;                  // prompt across BOTH views; cancel aborts
        for (EditorPage* p : allPages()) recordClosed(p);  // all become restorable via Ctrl+Shift+T
        for (EditorPage* p : allPages()) nibFireDocEvent(NIB_EV_DOCUMENT_CLOSED, p);   // before any teardown: path/id still resolvable
        for (wxAuiNotebook* nb : { m_main.tabs, m_sub.tabs })
            if (nb)
            {
                ViewPane* v = (nb == m_sub.tabs) ? &m_sub : &m_main;
                if (nb->GetSelection() != wxNOT_FOUND) detachViewEditor(v, static_cast<EditorPage*>(nb->GetPage(nb->GetSelection())));
                while (nb->GetPageCount() > 0) nb->DeletePage(0);
            }
        setActiveView(&m_main);
        addDocument("", nextNewName());                    // leave one empty doc in MAIN (never zero documents)
        collapseIfEmpty();                                 // unsplit the now-empty SUB
    }
    void resetActiveDoc()
    {
        EditorPage* keep = activePage();
        bool shared = false;                 // is this doc cloned into the other view? then don't wipe the shared buffer
        if (keep) for (EditorPage* o : allPages()) if (o != keep && o->doc == keep->doc) { shared = true; break; }
        if (shared)                          // hand the kept page a fresh empty buffer; the clone keeps the original content
        {
            const sptr_t nd = sci(SCI_CREATEDOCUMENT, 0, SC_DOCUMENTOPTION_TEXT_LARGE);
            sci(SCI_SETDOCPOINTER, 0, nd);   // switch the view to the new doc (releases the shared ref without clearing it)
            if (keep) keep->doc = nd;
        }
        else { sci(SCI_CLEARALL); sci(SCI_EMPTYUNDOBUFFER); }
        sci(SCI_SETSAVEPOINT);
        if (keep) { keep->path.clear(); keep->dirty = false; }
        m_path.clear();
        setLexerForFile("");
        setDocTitle(nextNewName());
    }
    void closeAllBut(EditorPage* keep)
    {
        for (EditorPage* p : allPages())
            if (p != keep && !confirmClose(p)) return;     // prompt across BOTH views; cancel aborts
        for (EditorPage* p : allPages()) if (p != keep) recordClosed(p);
        for (EditorPage* p : allPages()) if (p != keep) nibFireDocEvent(NIB_EV_DOCUMENT_CLOSED, p);   // before teardown
        for (wxAuiNotebook* nb : { m_main.tabs, m_sub.tabs })
            if (nb)
            {
                ViewPane* v = (nb == m_sub.tabs) ? &m_sub : &m_main;
                if (nb->GetSelection() != wxNOT_FOUND)     // protect a view's editor if its active page is being deleted
                { auto* ap = static_cast<EditorPage*>(nb->GetPage(nb->GetSelection())); if (ap != keep) detachViewEditor(v, ap); }
                for (int i = (int)nb->GetPageCount() - 1; i >= 0; --i)
                    if (nb->GetPage(i) != keep) nb->DeletePage(i);
            }
        setActiveView(viewOf(keep));
        collapseIfEmpty();                                 // unsplit whichever view is now empty
    }
    // Same three-pass shape as closeAllBut (confirm all first so a Cancel aborts before anything closes; then
    // record + delete), but keeping every Pinned tab instead of one specific page - so, unlike closeAllBut,
    // it CAN legitimately empty both views if nothing is pinned, hence the "never zero documents" fallback.
    void closeAllButPinned()
    {
        auto unpinned = [](wxAuiNotebook* nb, int i) { return nb->GetPageKind(i) != wxAuiTabKind::Pinned; };
        for (wxAuiNotebook* nb : { m_main.tabs, m_sub.tabs })
            if (nb) for (int i = 0; i < (int)nb->GetPageCount(); ++i)
                if (unpinned(nb, i) && !confirmClose(static_cast<EditorPage*>(nb->GetPage(i)))) return;   // cancel aborts
        for (wxAuiNotebook* nb : { m_main.tabs, m_sub.tabs })
            if (nb) for (int i = 0; i < (int)nb->GetPageCount(); ++i)
                if (unpinned(nb, i)) recordClosed(static_cast<EditorPage*>(nb->GetPage(i)));
        for (wxAuiNotebook* nb : { m_main.tabs, m_sub.tabs })   // before teardown: path/id still resolvable
            if (nb) for (int i = 0; i < (int)nb->GetPageCount(); ++i)
                if (unpinned(nb, i)) nibFireDocEvent(NIB_EV_DOCUMENT_CLOSED, nb->GetPage(i));
        for (wxAuiNotebook* nb : { m_main.tabs, m_sub.tabs })
            if (nb)
            {
                ViewPane* v = (nb == m_sub.tabs) ? &m_sub : &m_main;
                if (nb->GetSelection() != wxNOT_FOUND && unpinned(nb, nb->GetSelection()))   // protect the view's editor if its active page is being deleted
                    detachViewEditor(v, static_cast<EditorPage*>(nb->GetPage(nb->GetSelection())));
                for (int i = (int)nb->GetPageCount() - 1; i >= 0; --i)
                    if (unpinned(nb, i)) nb->DeletePage(i);
            }
        if (totalDocs() == 0) { setActiveView(&m_main); addDocument("", nextNewName()); }   // leave one empty doc (never zero documents)
        collapseIfEmpty();
    }
    // On window close / app exit, prompt for every modified document; a Cancel aborts the close.
    void onCloseWindow(wxCloseEvent& e)
    {
        // A shutdown is being attempted (-> NPPN_BEFORESHUTDOWN). It is not yet committed: if a modified
        // document's save prompt below is cancelled we veto and fire NIB_EV_SHUTDOWN_CANCEL instead, and
        // the frame stays open. The FINAL teardown signal (NPPN_SHUTDOWN) is still the CallAfter-deferred
        // unloadNibPlugins() below - never moved (the plugin-unload-crash fix); this is only the "begin".
        nibFireDocEvent(NIB_EV_SHUTDOWN_BEGIN, nullptr);
        if (e.CanVeto())   // a forced close (e.g. the theme-restart) skips prompts and just exits
            for (EditorPage* p : allPages())   // prompt EVERY modified doc across BOTH views - none lost on exit
                if (!confirmClose(p, /*exiting=*/true)) { nibFireDocEvent(NIB_EV_SHUTDOWN_CANCEL, nullptr); e.Veto(); return; }   // -> NPPN_CANCELSHUTDOWN
        saveSettings();    // persist any in-session View-menu toggle changes
        if (!g_waitMode)   // a --wait window is ephemeral (a commit message is not a session): without this every
                           // `git commit` would overwrite the user's saved tabs with .git/COMMIT_EDITMSG and the
                           // next normal launch would reopen it
            saveSession(wxConfigBase::Get());   // remember the open (saved) files so the next launch reopens them
        wxConfigBase::Get()->Flush();
        if (m_terminal) m_terminal->shutdownAll();   // kill child shells + stop their poll timers before AUI teardown
        // stop + free the owned timers so no WM_TIMER can Notify() into the frame while teardown pumps messages
        for (wxTimer** t : { &m_monTimer, &m_flTimer }) if (*t) { (*t)->Stop(); delete *t; *t = nullptr; }
        m_timer.Stop();
        m_aui.UnInit();
        // Defer the actual unload past this call stack (CallAfter -> next idle event), not a synchronous
        // call here: onCloseWindow runs NESTED inside the native WM_CLOSE dispatch, and for a Nib plugin
        // like the N++ bridge that subclasses this same frame (SetWindowSubclass/bridge_frame_proc),
        // RemoveWindowSubclass called reentrantly from within that same window's message dispatch only
        // DEFERS the removal (documented comctl32 behaviour) rather than completing it immediately. If
        // FreeLibrary then runs (as unloadNibPlugins() does) before the deferred removal finalizes, and
        // e.Skip() goes on to destroy the window SYNCHRONOUSLY in the same nested call - as it does here -
        // the still-technically-attached subclass gets one more WM_DESTROY/WM_NCDESTROY dispatched through
        // it, jumping into the now-unmapped DLL and crashing (0xc0000005, consistently at the same offset -
        // this is what showed up as npp_bridge.dll_unloaded crashes on exit). Running the unload from
        // CallAfter guarantees it happens after the whole WM_CLOSE/WM_DESTROY chain has fully unwound back
        // to the event loop, by which point the subclass is already gone and the window already destroyed.
        // Remove plugin toolbar buttons NOW, while the toolbar widget still exists, and null both hooks:
        // unloadNibPlugins() runs from the CallAfter below - after e.Skip() has already destroyed this
        // frame and its toolbar - so running the removal from there (or serving a late add_tool from a
        // plugin's deferred deactivate) would touch a dead widget. This keeps the contract (buttons gone
        // before FreeLibrary) without moving the deferred unload itself.
        if (g_nibToolbarRemoveAll) { g_nibToolbarRemoveAll(); g_nibToolbarRemoveAll = nullptr; }
        g_nibToolbarAddTool = nullptr;
        // Same teardown discipline for the other frame-capturing hooks a plugin can still reach from
        // its NPPN_SHUTDOWN handler: that notification is delivered from the CallAfter-deferred
        // unloadNibPlugins() below - AFTER e.Skip() has already destroyed this frame - so a plugin
        // sending NPPM_SETMENUITEMCHECK / NPPM_ISDARKMODEENABLED / NPPM_GETDARKMODECOLORS (nib.ui) or
        // NPPM_SETCURRENTLANGTYPE / NPPM_RELOADFILE / NPPM_MENUCOMMAND (invoke_command) at shutdown
        // would otherwise call straight into the destroyed frame / native menu. Null them here, like
        // the toolbar hooks above: every C trampoline is NUL-guarded, so a late call degrades to the
        // documented "capability unavailable" 0 return instead of a use-after-free.
        g_nibUiMenuCheck   = nullptr;
        g_nibUiIsDark      = nullptr;
        g_nibUiDarkColors  = nullptr;
        g_nibInvokeCommand = nullptr;
        g_nibKmEffectiveShortcut = nullptr;   // reads m_keymap (a frame member) - null before the frame dies (NPPM_GETSHORTCUTBYCMDID at shutdown)
        // nib.ui/2 + nib.session/1 + nib.lexer/1 hooks all capture `this` (chrome/state, the session
        // reader/writer, Lexilla) - a plugin can still reach them from its NPPN_SHUTDOWN handler, which is
        // delivered from the CallAfter-deferred unload AFTER this frame is destroyed, so null them here too.
        g_nibUiChromeGet = nullptr;     g_nibUiChromeSet = nullptr;
        g_nibUiWidthModeGet = nullptr;  g_nibUiWidthModeSet = nullptr;
        g_nibUiAutoIndent = nullptr;    g_nibUiMacroState = nullptr;
        g_nibUiIconSet = nullptr;       g_nibUiLocaleName = nullptr;
        g_nibSessSaveCurrent = nullptr; g_nibSessSaveFiles = nullptr;  g_nibSessLoad = nullptr;
        g_nibSessFileCount = nullptr;   g_nibSessFileAt = nullptr;
        g_nibLexerCreate = nullptr;     g_nibLexerUserLangCount = nullptr;
        // nib.documents v5 (per-view buffer model + buffer properties, main.cpp:2608-2721) hooks all
        // capture `this` too, and are reachable the same way from a late NPPN_SHUTDOWN handler - null
        // them here for the identical reason as the block above. Every nibDoc* C trampoline is already
        // NUL-guarded (falls back to its documented "capability unavailable" 0/-1 return).
        g_nibDocViewCount = nullptr;      g_nibDocIdAt = nullptr;         g_nibDocPosOf = nullptr;
        g_nibDocIndexOfActive = nullptr;  g_nibDocActivateAt = nullptr;   g_nibDocSetLangById = nullptr;
        g_nibDocEncodingGet = nullptr;    g_nibDocEncodingSet = nullptr;
        g_nibDocEolGet = nullptr;         g_nibDocEolSet = nullptr;
        g_nibDocSaveActiveAs = nullptr;   g_nibDocSaveById = nullptr;     g_nibDocSetDirtyActive = nullptr;
        g_nibDocRenameUntitled = nullptr; g_nibDocTabColorId = nullptr;
        CallAfter([this] { unloadNibPlugins(); });
        e.Skip();
    }
    // The window title-bar text for a given document part. Blank on macOS - the user prefers a clean
    // native title bar there (the document name is already shown in the tab, and macOS apps commonly
    // leave the title empty); "<doc> - wxNote" on Windows/Linux. All main-frame title updates go
    // through here so the platform choice lives in one place.
    void setWindowTitle(const wxString& docPart)
    {
#ifdef __WXMAC__
        (void)docPart; SetTitle(wxString());   // keep the native bar blank (titleVisibility is also hidden, see ctor)
#else
        const wxString t = docPart + " - wxNote";
        if (GetTitle() != t) SetTitle(t);
#endif
    }

    // Show "*" on the active tab + title bar while the document has unsaved changes.
    void refreshTab(EditorPage* p)
    {
        if (!p || !m_tabs) return;
        const bool dirty = (p == activePage()) ? (sci(SCI_GETMODIFY) != 0) : p->dirty;
        const wxString star = dirty ? "*" : "";
        const wxString lbl = star + p->title;                            // tab label = filename
        const int idx = m_tabs->GetPageIndex(p);
        if (idx != wxNOT_FOUND && m_tabs->GetPageText(idx) != lbl) m_tabs->SetPageText(idx, lbl);
        if (p == activePage())                                           // title bar = full path
            setWindowTitle(star + (p->path.empty() ? p->title : p->path));
    }
    void onTabContext(wxAuiNotebookEvent& e)
    {
        setActiveView(viewOfEvent(e));   // the right-clicked tab's view
        if (e.GetSelection() != wxNOT_FOUND) m_tabs->SetSelection(e.GetSelection());
        wxMenu menu;
        menu.Append(kCmdFileClose, _("Close"));
        menu.Append(kCmdFileCloseallButCurrent, _("Close All BUT This"));
        menu.Append(kCmdFileCloseall, _("Close All"));
        menu.AppendSeparator();
        menu.Append(kCmdFileSave, _("Save"));
        menu.Append(kCmdFileSaveas, _("Save As..."));
        menu.AppendSeparator();
        menu.Append(kCmdViewGotoAnotherView,     _("Move to Other View"));
        menu.Append(kCmdViewCloneToAnotherView, _("Clone to Other View"));
        menu.AppendSeparator();
        // Tab colours in an "Apply Colour" submenu. We read the pick via GetPopupMenuSelectionFromUser (below),
        // which returns the chosen id directly - including submenu items - so it sidesteps the MSW quirk where
        // PopupMenu silently drops submenu-item command events (that was why earlier picks did nothing).
        wxMenu* cm = new wxMenu;
        cm->Append(kTabColourNone, _("None"));
        cm->AppendSeparator();
        for (int k = 0; k < 5; ++k) cm->Append(kTabColourBase + k, tabPaletteName(k));
        menu.AppendSubMenu(cm, _("Apply Colour"));
        const int sel = this->GetPopupMenuSelectionFromUser(menu);
        if (sel == wxID_NONE) return;
        if (sel == kTabColourNone) { applyTabColour(-1); return; }
        if (sel >= kTabColourBase && sel < kTabColourBase + 5) { applyTabColour(sel - kTabColourBase); return; }
        wxCommandEvent ce(wxEVT_MENU, sel); onCommand(ce);   // Close / Save / Move / Clone via the normal dispatcher
    }

    // ----- second view (the MAIN | SUB split) ---------------------------------------------
    // Reveal the other view (splitting if needed) and put the active document into it, side by side.
    // clone=true shares the document (edits sync); clone=false relocates the tab, but falls back to a
    // clone when the source view has only this one tab (so a view never goes empty).
    void openInOtherView(bool clone)
    {
        ViewPane& from = m_active ? *m_active : m_main;
        ViewPane& to   = (&from == &m_main) ? m_sub : m_main;
        EditorPage* p  = activePage();
        if (!p) return;
        const int idx = from.tabs->GetPageIndex(p);
        if (idx == wxNOT_FOUND) return;
        const wxString title = from.tabs->GetPageText(idx);
        ensureSplit();
        if (clone || from.tabs->GetPageCount() <= 1)
        {
            auto* np = new EditorPage(to.tabs);
            np->doc = p->doc;
            sci(SCI_ADDREFDOCUMENT, 0, p->doc);    // share the Scintilla Document (extra ref) - "Clone to Other View"
            np->path = p->path; np->title = p->title; np->lang = p->lang;
            np->langForced = p->langForced; np->forcedLexer = p->forcedLexer; np->forcedName = p->forcedName;
            np->encoding = p->encoding; np->codepage = p->codepage; np->encLabel = p->encLabel;
            to.tabs->AddPage(np, title, true);
            setActiveView(&to); activateBuffer(np);
        }
        else
        {
            detachViewEditor(&from, p);            // lift FROM's editor off p so the move can't strand/destroy it
            from.tabs->RemovePage(idx);            // detach the page without destroying it
            p->Reparent(to.tabs);
            to.tabs->AddPage(p, title, true);
            if (from.tabs->GetPageCount() > 0)     // re-home FROM's editor onto its surviving active page
                activateBuffer(static_cast<EditorPage*>(from.tabs->GetPage(from.tabs->GetSelection())));
            setActiveView(&to); activateBuffer(p); // ...then leave the moved doc active in the other view
        }
    }
    // Split the editor area so both views show; theme the sub view's editor to match on first reveal.
    void ensureSplit()
    {
        if (!m_split || m_split->IsSplit()) return;
        if (!m_subThemed) { ViewPane* prev = m_active; setActiveView(&m_sub); applyEditorTheme(m_dark); if (prev) setActiveView(prev); m_subThemed = true; }
        m_sub.tabs->Show();
        m_split->SplitVertically(m_main.tabs, m_sub.tabs);
    }
    // After a close: if the editor is split and a view emptied, collapse back to one view (the sub
    // view hides when it has no documents). If MAIN emptied, the sub's pages move into it so MAIN stays the
    // always-present primary view (keeps _scintillaMainHandle meaningful).
    void collapseIfEmpty()
    {
        if (!m_split || !m_split->IsSplit()) return;
        const bool mainEmpty = (m_main.tabs->GetPageCount() == 0);
        const bool subEmpty  = (m_sub.tabs->GetPageCount() == 0);
        if (!mainEmpty && !subEmpty) return;
        if (mainEmpty && !subEmpty)
        {
            const int ssel = m_sub.tabs->GetSelection();
            EditorPage* keep = static_cast<EditorPage*>(m_sub.tabs->GetPage(ssel == wxNOT_FOUND ? 0 : ssel));
            detachViewEditor(&m_sub, keep);                  // lift SUB's editor off its page before the pages migrate to MAIN
            while (m_sub.tabs->GetPageCount() > 0)            // consolidate the sub view's pages into main
            {
                auto* pg = static_cast<EditorPage*>(m_sub.tabs->GetPage(0));
                const wxString t = m_sub.tabs->GetPageText(0);
                m_sub.tabs->RemovePage(0);
                pg->Reparent(m_main.tabs);
                m_main.tabs->AddPage(pg, t, false);          // don't churn the selection per page
            }
            const int ki = m_main.tabs->GetPageIndex(keep);
            if (ki != wxNOT_FOUND) m_main.tabs->SetSelection(ki);   // restore the doc that was active in SUB
        }
        m_split->Unsplit(m_sub.tabs);                        // show MAIN only
        m_sub.tabs->Hide();
        m_split->UpdateSize();                               // let the surviving notebook fill the splitter
        setActiveView(&m_main);
        if (auto* p = activePage()) { activateBuffer(p); if (m_stc) m_stc->SetSize(p->GetClientSize()); }
    }

    // Popup (right-click) context menu, user-editable via Settings > Edit Popup ContextMenu ->
    // the per-user contextMenu.xml (see contextMenuFilePath()/loadPopupContextMenu()/editContextMenu()).
    // Item ids are the same kCmd* the main menu uses, so onCommand handles them unchanged; labels are pulled
    // live from the real menu bar entry so they follow the current UI language, and enable state
    // mirrors the editor for the handful of ids that need it (undo/redo/paste/selection-dependent).
    struct PopupMenuEntry { int id = 0; bool separator = false; };
    // The shipped, read-only default (co-located next to the exe by CMake) vs. the per-user override.
    // Loads prefer the per-user copy and fall back to the shipped default; edits/saves only ever touch
    // the per-user copy (see editContextMenu) - the install dir is read-only on an installed build.
    wxString shippedContextMenuFilePath() { return wxPathOnly(wxStandardPaths::Get().GetExecutablePath()) + wxFILE_SEP_PATH + "contextMenu.xml"; }
    wxString contextMenuFilePath()        { return userDataDir() + wxFILE_SEP_PATH + "contextMenu.xml"; }
    std::vector<PopupMenuEntry> loadPopupContextMenu()
    {
        std::vector<PopupMenuEntry> out;
        wxXmlDocument doc;
        wxLogNull noLog;   // a bad hand-edit / unreadable file must not pop an error dialog on right-click
        wxString path = contextMenuFilePath();          // prefer the per-user copy...
        if (!wxFileExists(path)) path = shippedContextMenuFilePath();   // ...else the shipped default
        if (doc.Load(path) && doc.GetRoot())
        {
            for (wxXmlNode* sec = doc.GetRoot()->GetChildren(); sec; sec = sec->GetNext())
            {
                if (sec->GetName() != "ScintillaContextMenu") continue;
                for (wxXmlNode* it = sec->GetChildren(); it; it = it->GetNext())
                {
                    if (it->GetName() != "Item") continue;
                    if (it->GetAttribute("type") == "Separator") { out.push_back({ 0, true }); continue; }
                    long id = 0;
                    if (it->GetAttribute("id", "0").ToLong(&id) && id != 0) out.push_back({ (int)id, false });
                }
            }
        }
        if (!out.empty()) return out;
        // contextMenu.xml missing/unparsable - built-in fallback, kept in sync with the bundled
        // resources/contextMenu.xml default, so a bad hand-edit can't leave the menu empty.
        return { { kCmdEditUndo, false }, { kCmdEditRedo, false }, { 0, true },
                 { kCmdEditCut, false }, { kCmdEditCopy, false }, { kCmdEditPaste, false }, { kCmdEditDelete, false }, { 0, true },
                 { kCmdEditSelectall, false }, { 0, true },
                 { kCmdSearchToggleBookmark, false } };
    }
    // Settings > Edit Popup ContextMenu: open the PER-USER contextMenu.xml in the editor to hand-edit.
    // The shipped default sits in the read-only install dir, so on first edit we seed the per-user copy
    // from it - giving the user a working, commented starting point that saves back to a writable path.
    void editContextMenu()
    {
        const wxString userPath = contextMenuFilePath();
        if (!wxFileExists(userPath))
        {
            wxLogNull noLog;   // best-effort seed: a non-writable data dir must not pop an error dialog
            wxFileName::Mkdir(userDataDir(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
            const wxString shipped = shippedContextMenuFilePath();
            if (wxFileExists(shipped)) wxCopyFile(shipped, userPath, false);
        }
        if (wxFileExists(userPath)) openPath(userPath);
        else setStatus(0, _("Could not create a writable contextMenu.xml to edit."));   // degenerate: seed failed
    }
    void showEditorContext(int screenX, int screenY)
    {
        if (!m_stc) return;
        const bool hasSel = sci(SCI_GETSELECTIONEMPTY) == 0;
        wxMenu menu;
        for (const auto& entry : loadPopupContextMenu())
        {
            if (entry.separator) { if (menu.GetMenuItemCount() > 0) menu.AppendSeparator(); continue; }
            wxMenuItem* src = menuBar() ? menuBar()->FindItem(entry.id) : nullptr;
            if (!src) continue;   // unknown/stale id from a hand-edit - skip rather than show a blank entry
            bool enabled = true;
            switch (entry.id)
            {
                case kCmdEditUndo: enabled = sci(SCI_CANUNDO) != 0; break;
                case kCmdEditRedo: enabled = sci(SCI_CANREDO) != 0; break;
                case kCmdEditPaste: enabled = sci(SCI_CANPASTE) != 0; break;
                case kCmdEditCut: case kCmdEditCopy: case kCmdEditDelete: enabled = hasSel; break;
                default: break;
            }
            menu.Append(entry.id, src->GetItemLabel())->Enable(enabled);
        }
        if (menu.GetMenuItemCount() == 0) return;
        const wxPoint pos = (screenX == -1 && screenY == -1) ? wxDefaultPosition : ScreenToClient(wxPoint(screenX, screenY));
        PopupMenu(&menu, pos);
    }

    // ----- menu bar ------------------------------------------------------
    static void placeholder(wxMenu* m) { wxMenuItem* it = m->Append(wxID_ANY, _("(more items added incrementally)")); it->Enable(false); }

    // Menu/tool-bar accessors that work in BOTH chrome modes. Integrated mode has no native menu bar
    // (menus live in m_menuOwner) and its toolbar is an aui pane, not the frame toolbar - so the built-in
    // GetMenuBar()/GetToolBar() return null there. Route all item enable/check/toggle state through these.
    wxMenuBar* menuBar() const
    {
#ifdef WXN_HAS_BORDERLESS
        if (m_menuOwner) return m_menuOwner;
#endif
        return GetMenuBar();
    }
    wxToolBar* toolBar() const { return m_toolBarPtr ? m_toolBarPtr : GetToolBar(); }
    void showToolBar(bool show)
    {
        auto* tb = toolBar(); if (!tb) return;
#ifdef WXN_HAS_BORDERLESS
        if constexpr (kBorderless) { auto& pi = m_aui.GetPane(tb); if (pi.IsOk()) { pi.Show(show); m_aui.Update(); } return; }
#endif
#ifdef __WXMAC__
        // Docked (non-native) toolbar lives in an AUI pane keyed by the host panel, not the toolbar itself.
        if (m_toolBarHost) { auto& pi = m_aui.GetPane(m_toolBarHost); if (pi.IsOk()) { pi.Show(show); m_aui.Update(); } return; }
#endif
        tb->Show(show);   // native frame toolbar
    }

    // Strip a "\t<accel>" suffix from a menu label while KEEPING its "&" mnemonic. wx's own
    // GetItemLabelText() can't be used for the label rewrite below: it runs wxStripMenuCodes(wxStrip_Menu)
    // which also removes the "&", so it would silently turn "&Save" into "Save" and kill Alt-navigation.
    static wxString stripAccelKeepMnemonic(const wxString& label)
    {
        const int t = label.Find('\t');
        return (t == wxNOT_FOUND) ? label : label.Left(t);
    }

    // Rewrite every store-managed menu item's "\t<accel>" suffix from the KeymapStore's effective set,
    // preserving the mnemonic. This is how a shortcuts.json override (and, later, a scheme switch) shows
    // up in the menu, and how the accel is re-attached from locale-independent DATA after a UI-language
    // switch instead of from the (translated) .po label. Items with no store entry (the runtime Language
    // submenu, Recent Files, plugin/macro commands) are left untouched. Idempotent; runs on every
    // keymap CHANGE (menu build, nib commit, mapper apply) - pure focus-scope flips skip it, since no
    // label depends on the scope. On the NATIVE frame SetItemLabel also rebuilds wx's per-port menu
    // accelerators (MSW UpdateAccel / GTK AccelGroup / macOS keyEquivalent), so the native menubar is
    // served here too - and since menu_builder.h appends items with BARE labels, this rewrite is what
    // attaches the accel suffix (and the native accel) in the first place, before the frame first shows.
    //
    // SECONDARY accels (the dual-default rows - redo Ctrl+Shift+Z, close Ctrl+F4 - and user bare-bind
    // ADDs) are installed here too, via wxMenuItem::AddExtraAccel: the label suffix carries only the
    // primary, and on the native frame that label is the ONLY accel wx derives per item - without the
    // extras the secondaries would be store-resolved yet never fire there (the borderless frame was
    // already covered, its collectFrameAccels table installs ALL accels). Port support: MSW folds
    // GetExtraAccels() into the menubar accel table inside UpdateAccel, GTK adds them to the
    // AccelGroup immediately, macOS >= 10.13 creates hidden keyEquivalent items. Diffed against the
    // installed set so the common nothing-changed rewrite stays cheap, and applied BEFORE
    // SetItemLabel - MSW only consumes extras via SetItemLabel's UpdateAccel. On the borderless frame
    // the bar is never attached, so the extras are inert there (no double dispatch with the frame
    // table); on the native frame they share the existing scope residual (native menubar accels are
    // not terminal-scope gated - see refreshAccelerators).
    void rewriteMenuAccelLabels(wxMenuBar* mb)
    {
        std::function<void(wxMenu*)> scan = [&](wxMenu* m) {
            for (wxMenuItem* item : m->GetMenuItems())
            {
                if (item->IsSubMenu()) { scan(item->GetSubMenu()); continue; }
                const int id = item->GetId();
                if (id == 0 || id == wxID_ANY) continue;
                const EffectiveBinding* b = m_keymap.effectiveByCmd(id);
                if (!b) continue;                          // not a store-managed command - leave as-is
                const wxString accel = b->primaryRaw();    // empty => the command is unbound

                std::vector<wxAcceleratorEntry> extras;    // everything beyond the primary (see above)
                for (const wxString& raw : b->secondaryRaws())
                {
                    wxAcceleratorEntry e;
                    if (e.FromString(raw)) extras.push_back(e);
                }
                const wxVector<wxAcceleratorEntry>& cur = item->GetExtraAccels();
                bool sameExtras = cur.size() == extras.size();
                for (size_t k = 0; sameExtras && k < extras.size(); ++k)
                    sameExtras = cur[k].GetFlags() == extras[k].GetFlags()
                              && cur[k].GetKeyCode() == extras[k].GetKeyCode();
                if (!sameExtras)
                {
                    item->ClearExtraAccels();
                    for (const wxAcceleratorEntry& e : extras) item->AddExtraAccel(e);
                }

                const wxString base  = stripAccelKeepMnemonic(item->GetItemLabel());
                const wxString want  = accel.empty() ? base : (base + "\t" + accel);
                if (want != item->GetItemLabel()) item->SetItemLabel(want);
#ifdef __WXMSW__
                // MSW consumes extras only inside wxMenu::UpdateAccel, which SetItemLabel skips when
                // the label text is unchanged (e.g. a Reassign stealing only a SECONDARY key leaves
                // the primary label intact) - force the update so the stale accel can't keep firing.
                else if (!sameExtras && item->GetMenu()) item->GetMenu()->UpdateAccel(item);
#endif
            }
        };
        for (size_t i = 0; i < mb->GetMenuCount(); ++i) scan(mb->GetMenu(i));
    }

    // Rebuild the frame's wxAcceleratorTable from the current menu, filtered by focus scope. Hoisted out
    // of the old borderless-only installAccelsFromMenuBar() so BOTH frame paths and the focus gate share
    // one entry point, now sourcing every binding from the KeymapStore rather than the label suffix.
    //
    // Scope::Terminal installs an EMPTY table. This is the Phase 0 fix: Edit>Copy is bound Ctrl+C, so with
    // the terminal focused MSW's PreProcessMessage accelerator translation fired that accel and ran
    // SCI_COPY on the hidden editor BEFORE the focused TermView ever saw WM_KEYDOWN - so plain Ctrl+C never
    // reached term_view.cpp's onChar, which is what cooks 0x03 into the child's SIGINT. Emptying the frame
    // table for terminal scope removes the collision; the keystroke reaches TermView and the shell gets
    // its interrupt.
    //
    // The frame's wxAcceleratorTable only exists on the BORDERLESS frame (the native frame's accelerators
    // live on the attached wxMenuBar instead, which SetAcceleratorTable cannot clear - so scoping it there
    // is a no-op and a duplicate frame table risks double-dispatch, notably macOS Cmd keyEquivalents). The
    // label rewrite above, however, runs on BOTH paths. The native-frame terminal-scope residual is
    // left for a later phase.
    // rewriteLabels=false is for the pure focus-scope flips (onChildFocus): menu labels depend only on
    // the store, never on the scope, so a scope-only change re-installs the accel table without the
    // full recursive label walk. Every keymap-CHANGE caller (menu build, nib commit, mapper apply)
    // keeps the default and rewrites.
    void refreshAccelerators(Scope scope = Scope::Editor, bool rewriteLabels = true)
    {
        m_accelScope = scope;
        wxMenuBar* mb = menuBar();
        if (!mb) return;

        if (rewriteLabels && m_keymapReady) rewriteMenuAccelLabels(mb);
#ifdef WXN_HAS_BORDERLESS
        if constexpr (kBorderless)
        {
            std::vector<wxAcceleratorEntry> accels;
            if (scope != Scope::Terminal)   // Terminal scope: leave the list empty (see above)
                collectFrameAccels(mb, accels);
            // Always call SetAcceleratorTable, even with an empty list: swapping in the null table is how
            // terminal scope DROPS the previously-installed full one.
            SetAcceleratorTable(accels.empty() ? wxNullAcceleratorTable
                                               : wxAcceleratorTable((int)accels.size(), accels.data()));
        }
#endif
    }

    // Collect the frame accelerator-table entries for every store-managed menu command's plain (non-chord)
    // global accels. Walks the built menu (not the store) so only commands that actually have a menu item
    // get an accelerator, exactly as before the KeymapStore existed; the accel string itself comes from
    // the store. Runs only with the store seeded: m_keymapReady is set BEFORE every refreshAccelerators
    // call in buildMenuBar (its sole caller's sole caller), so no pre-seed fallback is needed - and
    // effectiveByCmd on an empty store safely returns nullptr anyway.
    void collectFrameAccels(wxMenuBar* mb, std::vector<wxAcceleratorEntry>& out)
    {
        std::function<void(wxMenu*)> scan = [&](wxMenu* m) {
            for (wxMenuItem* item : m->GetMenuItems())
            {
                if (item->IsSubMenu()) { scan(item->GetSubMenu()); continue; }
                const int id = item->GetId();
                if (id == 0 || id == wxID_ANY) continue;
                const EffectiveBinding* b = m_keymap.effectiveByCmd(id);
                if (!b) continue;                          // not a store-managed command (Recent Files, ...)
                for (const EffectiveAccel& a : b->accels)
                {
                    if (a.isChord || a.scope != KeyScope::Global || a.raw.empty()) continue;
                    wxAcceleratorEntry e;
                    if (e.FromString(a.raw)) out.push_back(wxAcceleratorEntry(e.GetFlags(), e.GetKeyCode(), id));
                }
            }
        };
        for (size_t i = 0; i < mb->GetMenuCount(); ++i) scan(mb->GetMenu(i));
    }

    // ----- the Scintilla editor tier ---------------
    // Curated editor commands (src/shortcut_labels.h) are remapped per wxStyledTextCtrl via Scintilla's
    // own CmdKeyAssign/CmdKeyClear, NOT the frame accelerator table - they live in Scintilla's key space
    // and only ever fire with editor focus. This applies to the two PERSISTENT editor STCs (main + sub
    // view); the terminal panel's STC is built in terminal_panel.h and never routed through here, so it is
    // excluded for free. The split view's STC reparents across pages, but Scintilla's keymap is
    // per-instance and set once at creation, so a reparent never loses it.
    //
    // A curated row whose EFFECTIVE binding equals its STOCK Scintilla key is never touched, so the stock
    // keymap - including any macOS-only rows we don't mirror - is left exactly as Scintilla set it for
    // those keys. A row is managed (stock key vacated, effective key assigned) whenever the two diverge:
    // a user/scheme override, OR one of the handful of curated defaults that deliberately differ from
    // stock since the 6-editor consensus adoption (shortcut_labels.h: lineDelete's Ctrl+Shift+K, and the
    // unbound-by-default lineCopy/paragraphUp/paragraphDown/wordPartLeft whose stock chords now belong to
    // menu accelerators). On top of that, the scintKeymap::kStockVacated table adds pure vacates for the
    // stock keys a DEFAULT menu accel displaces without a curated row (Ctrl+D/SCI_SELECTIONDUPLICATE).
    // The whole op computation lives in shortcut_labels.h (computeEditorOps(store)) so the ConflictEngine
    // and the self-test consume the exact same set - see the header's comment. We track the applied set
    // (m_editorLive) so a later reset/rebind can restore the stock binding precisely rather than leaving
    // a stale key live until restart.
    // Apply overrides to one STC. TWO PASSES on purpose: vacate EVERY overridden command's stock default
    // key first, THEN bind every override. Interleaving clear+assign per op corrupts the live keymap when
    // one command's override key equals a later-processed command's stock default key: e.g. rebinding
    // editor.lineCut (stock Ctrl+L) to Ctrl+Shift+T while editor.lineCopy's stock default IS Ctrl+Shift+T.
    // With a single interleaved loop the lineCopy op's CmdKeyClear(Ctrl+Shift+T) would wipe the just-
    // assigned SCI_LINECUT off Ctrl+Shift+T, silently killing lineCut's binding. Clearing all defaults up
    // front means a later clear can never undo an earlier assign onto the same key.
    static void applyEditorOpsTo(wxStyledTextCtrl* stc, const std::vector<EditorOp>& ops)
    {
        if (!stc) return;
        for (const EditorOp& op : ops)
            stc->CmdKeyClear(op.defKey, op.defMods);
        for (const EditorOp& op : ops)
            if (op.hasOverride) stc->CmdKeyAssign(op.ovKey, op.ovMods, op.sciCmd);
    }
    // Undo a previously-applied set: restore each command's stock default, and return the key it had been
    // moved onto to its own stock binding (or clear it if it wasn't a stock key). Leaves the STC back at
    // Scintilla's built-in keymap for every key these ops touched.
    static void undoEditorOpsOn(wxStyledTextCtrl* stc, const std::vector<EditorOp>& ops)
    {
        if (!stc) return;
        for (const EditorOp& op : ops)
        {
            stc->CmdKeyAssign(op.defKey, op.defMods, op.sciCmd);
            if (op.hasOverride)
            {
                const int stock = stockSciCommandFor(op.ovKey, op.ovMods);
                if (stock) stc->CmdKeyAssign(op.ovKey, op.ovMods, stock);
                else       stc->CmdKeyClear(op.ovKey, op.ovMods);
            }
        }
    }
    // Re-apply the editor keymap to both persistent views after the store changes (startup after load, and
    // every mapper OK). Diffs against the last-applied set so reset/rebind restores the stock binding.
    void reapplyEditorKeymaps()
    {
        if (!m_keymapReady) return;
        std::vector<EditorOp> ops = computeEditorOps(m_keymap);
        if (ops.empty() && m_editorLive.empty()) return;   // nothing applied and nothing to undo (defensive:
                                                           // the ops always carry at least the kStockVacated rows)
        wxStyledTextCtrl* stcs[2] = { m_main.stc, m_sub.stc };
        for (wxStyledTextCtrl* stc : stcs)
        {
            undoEditorOpsOn(stc, m_editorLive);
            applyEditorOpsTo(stc, ops);
        }
        m_editorLive = std::move(ops);
    }

    // Which scope a window belongs to: Terminal if it (or an ancestor) is the terminal panel, else Editor.
    Scope scopeOfFocus(wxWindow* w) const
    {
        for (; w; w = w->GetParent())
            if (m_terminal && w == static_cast<wxWindow*>(m_terminal)) return Scope::Terminal;
        return Scope::Editor;
    }

    // Focus moved somewhere inside the frame: pick the accelerator scope for the newly-focused window.
    // wxWindow::FindFocus() is authoritative here (the event object can be an intermediate ancestor as the
    // event bubbles up). Only re-installs when the scope actually flips, so the common editor<->editor and
    // terminal<->terminal focus hops do no work.
    void onChildFocus(wxChildFocusEvent& e)
    {
        e.Skip();
        if (!m_terminal) { if (m_accelScope != Scope::Editor) refreshAccelerators(Scope::Editor, /*rewriteLabels=*/false); return; }
        wxWindow* f = wxWindow::FindFocus();
        if (!f) f = e.GetWindow();
        const Scope want = scopeOfFocus(f);
        if (want != m_accelScope) refreshAccelerators(want, /*rewriteLabels=*/false);   // labels are scope-independent
    }

    void buildMenuBar()
    {
        auto* mb = new wxMenuBar;
        buildWxnMainMenu(mb, m_menuRegistry);   // the full menu tree, built from menu_data_*.h (see menu_builder.h)
        // Localization: pick the UI language straight from the menu bar (radio group; restart-to-apply,
        // same flow as the Preferences > General dropdown). A submenu of Settings, inserted at the
        // "slot.localization" DynamicSlot that Settings' data table reserves - not a separate top-level
        // bar entry, and not recomputed position arithmetic either. (The slot resolves by symbolic name,
        // so it didn't need touching when Settings was split back out of Window.)
        {
            auto [locMenu, insertAt] = m_menuRegistry.slot("slot.localization");
            if (locMenu)
            {
                auto* loc = new wxMenu;
                const int cur = uiLangIndex(readUiLang());
                for (int i = 0; i < (int)WXSIZEOF(UI_LANG_IDS); ++i)
                {
                    wxMenuItem* it = loc->AppendRadioItem(myID_UILANG_FIRST + i, uiLangName(i));
                    if (i == cur) it->Check();
                }
                locMenu->Insert(insertAt, wxID_ANY, _("Locali&zation"), loc);
            }
        }
        // Recent Files (MRU) submenu near the bottom of the File menu, backed by wxFileHistory. Inserted
        // at the "slot.recentFiles" DynamicSlot the File menu's data table (menu_data_file.h) reserves,
        // rather than recomputed position arithmetic.
        {
            auto [fileMenu, insertAt] = m_menuRegistry.slot("slot.recentFiles");
            if (fileMenu)
            {
                auto* recent = new wxMenu;
                fileMenu->Insert(insertAt, wxID_ANY, _("Recent &Files"), recent);
                m_fileHistory.UseMenu(recent);
                auto* c = wxConfigBase::Get(); c->SetPath("/RecentFiles"); m_fileHistory.Load(*c); c->SetPath("/");
            }
        }
        // File > Open Containing Folder's tool cluster (see terminal_panel.h), inserted at the
        // "slot.openContainingFolderTools" DynamicSlot: on Windows the EXTRA shells this machine has
        // beyond the static cmd/PowerShell (pwsh/Cygwin/WSL); on Linux/macOS the WHOLE terminal-app
        // list, since those platforms have no legacy plugin ABI reason to keep a fixed shape.
        {
            auto [ofMenu, insertAt] = m_menuRegistry.slot("slot.openContainingFolderTools");
            if (ofMenu)
            {
#ifdef __WXMSW__
                m_openFolderTools = detectWindowsExtraOpenHereTools();
#elif defined(__WXMAC__)
                m_openFolderTools = detectMacTerminalApps();
#else
                m_openFolderTools = detectLinuxTerminalEmulators();
#endif
                for (size_t i = 0; i < m_openFolderTools.size(); ++i)
                    ofMenu->Insert(insertAt + (int)i, myID_OPENFOLDER_TOOL_BASE + (int)i, m_openFolderTools[i].label);
            }
        }
        // Shortcut store: seed Tier 0 from the menu data's defaultAccel, register the bundled read-only
        // preset (Tier 1: the "wxNote default" identity; Notepad++ keys come only via the optional
        // npp-shortcuts-compat import), then layer shortcuts.json's active-scheme selection + user
        // deltas on top (best-effort/hand-editable). Ordering is load-bearing: schemes must be
        // registered BEFORE load() so a saved activeScheme resolves at startup (an id that no longer
        // exists snaps back to the default inside load()) and load()'s reload cleanup (keep bundled,
        // drop user schemes) preserves the preset.
        // All must happen BEFORE the refreshAccelerators() calls in either frame branch below, since those
        // now build the accel table + rewrite the menu labels from this store.
        seedKeymapDefaults(m_keymap);
        seedEditorKeymapDefaults(m_keymap);   // Tier 0 for the curated Scintilla "Editor commands"
        registerKeymapSchemes(m_keymap);
        m_keymap.load(userDataDir());
        m_keymapReady = true;
        // Apply any editor-command overrides to the two persistent STCs now the store is loaded. buildEditor()
        // created them before this point (store empty then), so the setupScintilla-time apply was a no-op;
        // this is the real startup apply. No-op for a user with no editor overrides.
        reapplyEditorKeymaps();
#ifdef WXN_HAS_BORDERLESS
        if constexpr (kBorderless)
        {
            m_menuOwner = mb;                  // keep the wxMenus alive for the title-bar buttons to pop
            // The bar is never SetMenuBar'd, so a popped menu's commands route to it, not the frame - bind
            // onCommand on the bar itself so menu clicks actually fire (otherwise items appear but do nothing).
            mb->Bind(wxEVT_MENU, &WxnShellFrameT::onCommand, this);
            refreshAccelerators();             // borderless: our frame accel table is the ONLY shortcut source (no native menu bar)
            buildIntegratedTitleBar(mb);       // VS-style top bar: icon + menu-buttons + window controls
            return;
        }
#endif
        SetMenuBar(mb);
        refreshAccelerators();   // shared entry point (no-op on the native frame, where the wxMenuBar owns accels) - see refreshAccelerators
    }

#ifdef WXN_HAS_BORDERLESS
    // ---- integrated/borderless top bar (compiled only where wxBorderlessFrame exists; instantiated only
    //      for the borderless frame, i.e. kBorderless - the native frame never reaches these) ------------

    // Borderless hit-testing. We use real child controls for the menu/window buttons and a manual drag
    // (onTitleBarDrag), so the inner area is plain client; the lib still handles the resize borders itself.
    wxWindowPart GetWindowPart(wxPoint) const { return wxWP_CLIENT_AREA; }

    void buildIntegratedTitleBar(wxMenuBar* mb)
    {
        // Match the toolbar/status/frame chrome (32,32,32) exactly so the whole top is one colour and the
        // window corners have no shade seam against the frame backing.
        const wxColour barBg = m_dark ? wxColour(32, 32, 32)    : wxColour(240, 240, 240);
        const wxColour barFg = m_dark ? wxColour(220, 220, 220) : wxColour(30, 30, 30);
        // wxbf's default 1px window border is light grey (168,168,168) - jarring on dark chrome, and its
        // square corner is exactly what notches against Win11's rounded window. Match it to the bar.
        this->SetBorderColour(barBg);
#ifdef __WXMSW__
        // Win11 rounds top-level windows; the rounded cut-off leaves a small notch at the corner of our
        // square chrome. Square the window (DWMWCP_DONOTROUND) so all four corners are clean.
        // (Tried flipping this to DWMWCP_ROUND to get real Win11 rounding instead - confirmed via
        // pixel-level screenshot inspection, even with an explicit SWP_FRAMECHANGED nudge afterward,
        // that DWM does not actually round this window's corners either way. This borderless frame's
        // underlying window style is almost certainly not eligible for DWM's automatic rounding at
        // all - getting real rounded corners here would need deeper surgery (keeping WS_CAPTION while
        // custom-handling WM_NCCALCSIZE, matching how Windows Terminal/similar apps do it), not a
        // one-line attribute flip. DONOTROUND stays the correct call for this architecture as-is.)
        { DWORD corner = DWMWCP_DONOTROUND;
          ::DwmSetWindowAttribute(static_cast<HWND>(this->GetHandle()), DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner)); }
#endif
        m_titleBar = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, kTitleBarH));
        m_titleBar->SetBackgroundColour(barBg);
        m_titleBar->Bind(wxEVT_LEFT_DOWN, &WxnShellFrameT::onTitleBarDrag, this);     // drag empty area to move
        m_titleBar->Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent&){ onWindowControl(1); });   // double-click = maximize/restore
        this->Bind(wxEVT_SIZE, [this](wxSizeEvent& e){ updateMaxGlyph(); e.Skip(); });       // keep the glyph synced (snap, Win+Up)

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
            // GTK/macOS only: wxBU_EXACTFIT collapses each menu button to its bare text extent, so
            // adjacent labels touch with no gap and read as one crammed string. The air must be INSIDE
            // the button (widen it; the label auto-centres), not a sizer border around it: the hover/
            // pressed highlight paints across the button's own rect, so outside spacing left the
            // highlight hugging the text edge-to-edge with no left/right padding. 8px per side matches
            // the ~"padding: 4px 8px" a native GTK menubar item gets. MSW's native button already
            // reserves internal horizontal margin, so adding more there reads too widely spaced.
            // (The AddStretchSpacer below absorbs the width, so no overflow.)
#ifndef __WXMSW__
            b->SetMinSize(wxSize(b->GetBestSize().x + b->FromDIP(16), -1));
#endif
            sz->Add(b, 0, wxALIGN_CENTRE_VERTICAL);
        }

        sz->AddStretchSpacer(1);   // empty middle stays draggable

        // Window controls (right): minimize, maximize/restore, close (close hovers red, VS-style)
#ifdef __WXMSW__
        // Windows draws its caption buttons from the Segoe MDL2 Assets icon font - use the very same font
        // + glyphs (Chrome{Minimize,Maximize,Restore,Close}) so the controls are pixel-identical to native.
        const wxFont mdl2(wxFontInfo(10).FaceName("Segoe MDL2 Assets"));
#endif
        auto ctrl = [&](wchar_t mdl2Glyph, const char* svgPath, int which, const wxColour& hot) -> wxWindow* {
#ifdef __WXMSW__
            // Windows: native wxButton, pixel-identical to the OS's own caption buttons (see mdl2 above).
            // This path has no hover-repaint issue - MSW's native wxButton doesn't run its own independent
            // theme-driven repaint cycle behind wx's back the way GTK's does (see TitleBarBtn above).
            auto* b = new wxButton(m_titleBar, wxID_ANY, "", wxDefaultPosition,
                                   wxSize(46, kTitleBarH), wxBU_EXACTFIT | wxBORDER_NONE);
            b->SetFont(mdl2); b->SetLabel(wxString(wxUniChar(mdl2Glyph)));
            b->SetForegroundColour(m_dark ? wxColour(240, 240, 240) : wxColour(20, 20, 20));
            (void)svgPath;
            b->SetBackgroundColour(barBg);
            b->Bind(wxEVT_BUTTON,       [this, which](wxCommandEvent&) { onWindowControl(which); });
            b->Bind(wxEVT_ENTER_WINDOW, [b, hot](wxMouseEvent& e)   { b->SetBackgroundColour(hot);   b->Refresh(); e.Skip(); });
            b->Bind(wxEVT_LEAVE_WINDOW, [b, barBg](wxMouseEvent& e) { b->SetBackgroundColour(barBg); b->Refresh(); e.Skip(); });
            sz->Add(b, 0, wxEXPAND);
            return b;
#else
            (void)mdl2Glyph;
            auto* b = new TitleBarBtn(m_titleBar, wxID_ANY, wxSize(46, kTitleBarH), winGlyph(svgPath), barBg, hot);
            b->Bind(wxEVT_BUTTON, [this, which](wxCommandEvent&) { onWindowControl(which); });
            sz->Add(b, 0, wxEXPAND);
            return b;
#endif
        };
        const wxColour hover = m_dark ? wxColour(63, 63, 70) : wxColour(220, 220, 220);
        ctrl(0xE921, GLYPH_MIN, 0, hover);                                                                 // minimize
        m_maxBtn = ctrl(IsMaximized() ? 0xE923 : 0xE922, IsMaximized() ? GLYPH_RESTORE : GLYPH_MAX, 1, hover);  // max/restore
        ctrl(0xE8BB, GLYPH_CLOSE, 2, wxColour(232, 17, 35));                                                // close (red hover)

        m_titleBar->SetSizer(sz);
        m_aui.AddPane(m_titleBar, wxAuiPaneInfo().Name("titlebar").Top().Layer(2)
            .CaptionVisible(false).PaneBorder(false).Gripper(false).Floatable(false).Movable(false)
            .Dockable(false).DockFixed().Resizable(false).MinSize(-1, kTitleBarH).MaxSize(-1, kTitleBarH));
        m_aui.Update();
    }

    void onTitleBarDrag(wxMouseEvent& ev)
    {
        // The lib's gripper moves the window on BOTH platforms (WM_NCLBUTTONDOWN on MSW,
        // gtk_window_begin_move_drag on GTK) - so dragging the bar works on Linux too.
        if (!m_gripper) m_gripper = wxWindowGripper::Create();
        if (!m_gripper || !m_gripper->StartDragMove(this)) ev.Skip();
    }

    // A window-control glyph stroked into a small bitmap (10x10 box), themed to the bar.
    wxBitmapBundle winGlyph(const char* path) const
    {
        const char* col = m_dark ? "#f0f0f0" : "#171717";
        const wxString svg = wxString::Format(
            "<svg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 12 12'>"
            "<path d='%s' fill='none' stroke='%s' stroke-width='1.3' stroke-linecap='square' stroke-linejoin='miter'/></svg>",
            path, col);
        return wxBitmapBundle::FromSVG(svg.utf8_str().data(), wxSize(12, 12));
    }
    void updateMaxGlyph()
    {
        if (!m_maxBtn) return;
#ifdef __WXMSW__
        static_cast<wxButton*>(m_maxBtn)->SetLabel(wxString(wxUniChar(IsMaximized() ? 0xE923 : 0xE922)));   // ChromeRestore / ChromeMaximize
#else
        static_cast<TitleBarBtn*>(m_maxBtn)->SetGlyph(winGlyph(IsMaximized() ? GLYPH_RESTORE : GLYPH_MAX));
#endif
    }
    void onWindowControl(int which)
    {
        if      (which == 0) Iconize(true);
        else if (which == 1) { Maximize(!IsMaximized()); updateMaxGlyph(); }
        else                 Close();
    }

    wxToolBar* makeIntegratedToolBar()
    {
        return new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxTB_FLAT | wxTB_HORIZONTAL | wxTB_NODIVIDER);
    }
    void dockIntegratedToolBar(wxToolBar* tb)
    {
        m_aui.AddPane(tb, toolbarPaneInfo());   // shared fixed-top-toolbar recipe (see toolbarPaneInfo())
        m_aui.Update();
    }
#endif // WXN_HAS_BORDERLESS

    void addToMRU(const wxString& path)
    {
        m_fileHistory.AddFileToHistory(path);
        auto* c = wxConfigBase::Get(); c->SetPath("/RecentFiles"); m_fileHistory.Save(*c); c->SetPath("/"); c->Flush();
    }

    // ----- toolbar (default icon set, fixed button order) -------------------
    // Colored icon sets (Settings > Preferences > General > Toolbar icon style): Solar (Bold Duotone,
    // CC BY 4.0) tinted Open Color green, IconPark (Apache-2.0) tinted Open Color teal-7/lime-5, and
    // Streamline (Core flat, CC BY 4.0) tinted Open Color green-4/teal-8 - see
    // resources/icons-solar|iconpark|streamline/CREDITS.md. Unlike the default set these bake a FIXED
    // colour in at generation time rather than a currentColor token. The packs also need a small THEME-specific
    // runtime retint (below): the single baked colour that reads best on dark chrome isn't always the one
    // that reads best on light chrome (or vice versa), so `iconColored()` does a cheap string substitution
    // on top of the baked file rather than shipping four colour variants per pack.
    wxBitmapBundle iconColored(const wxString& name, const wxString& packDir, int px = 16)
    {
        const wxString path = wxPathOnly(wxStandardPaths::Get().GetExecutablePath()) + wxFILE_SEP_PATH + packDir + wxFILE_SEP_PATH + name + ".svg";
        if (!wxFileExists(path)) return wxBitmapBundle();   // caller falls back to the line-icon SVG for anything the colored set doesn't cover
        if (packDir == "icons-solar" && !m_dark)
        {
            // The baked colours (green-8 fill / green-3 secondary) are tuned for DARK chrome - green-3 is
            // deliberately pale so the duotone's second tone pops against a dark background instead of
            // alpha-blending into it (see the dark-mode duotone fix). On light chrome that same pale
            // secondary reads as washed-out/faint. Darken both tones for light mode specifically.
            wxFile f(path); wxString svg;
            if (f.IsOpened() && f.ReadAll(&svg))
            {
                svg.Replace("#2f9e44", "#2b8a3e");   // Open Color green-9 (primary, darker for light chrome)
                svg.Replace("#8ce99a", "#40c057");   // Open Color green-6 (secondary, darker for light chrome)
                const wxScopedCharBuffer u = svg.utf8_str();
                return wxBitmapBundle::FromSVG(u.data(), wxSize(px, px));
            }
        }
        if (packDir == "icons-iconpark" && m_dark)
        {
            // IconPark's signature black outline stroke (its accent fills are already baked to a fixed
            // teal/lime at generation time - see resources/icons-iconpark/CREDITS.md) reads fine on the
            // light chrome it was designed for, but nearly vanishes on dark chrome. A first dark-mode fix
            // lightened it to a NEUTRAL grey (Open Color gray-4), which fixed visibility but read as "a
            // grey outline with a teal patch" rather than one coherent object (gray-4 has noticeably
            // HIGHER contrast against dark chrome than the teal fill itself). Switched to a lighter shade
            // of the SAME accent (Open Color teal-3) instead - a trial of applying that same teal-3 tint
            // in LIGHT mode too (for hue consistency across themes) was tried and reverted: side by side,
            // the original black outline on light chrome read better than the unified teal-everywhere
            // look, so this stays dark-mode-only. The white highlight stroke needs no change either way.
            wxFile f(path); wxString svg;
            if (f.IsOpened() && f.ReadAll(&svg))
            {
                svg.Replace("stroke=\"#000\"", "stroke=\"#63e6be\"");   // Open Color teal-3
                // Same rule for the pack's stroke-only glyphs (close/undo/redo/word-wrap/doc-list/...):
                // they are drawn purely in teal-7 STROKES with no accent fill, and teal-7 line art on dark
                // chrome has so little luminance contrast that an ENABLED icon read as a disabled one
                // (reported for the toolbar close "x"). All strokes lighten to teal-3 in dark mode; the
                // teal-7 accent FILLS stay untouched, so filled icons keep their saturated look.
                svg.Replace("stroke=\"#0ca678\"", "stroke=\"#63e6be\"");
                const wxScopedCharBuffer u = svg.utf8_str();
                return wxBitmapBundle::FromSVG(u.data(), wxSize(px, px));
            }
        }
        if (packDir == "icons-streamline" && m_dark)
        {
            // Streamline's flat two-paint pair is baked for LIGHT chrome (green-4 fill + teal-8
            // detail, both matching the stock blues' weight - see the pack's CREDITS.md). On dark
            // chrome teal-8 has too little luminance contrast (the same "enabled reads as disabled"
            // failure the IconPark strokes had), so lighten BOTH paints a la Solar: fill to green-3
            // (unchanged) and detail to teal-6 (was teal-4). Teal-4 first-drafted here made the two
            // paints nearly the SAME luminance (~0.70 vs green-3's ~0.81, a ~0.11 gap) - the pair read
            // as visually flat/low-contrast at 16px, losing the fill/detail separation that makes the
            // glyph legible; teal-6 sits further from green-3 (~0.51, a ~0.30 gap matching the light-
            // mode bake's own ~0.29 gap) while staying well clear of the too-dim teal-8 baseline. The
            // bare-hex replace covers fills and the couple of stroked prompt glyphs (terminal) alike;
            // the save-macro badge's #fff stays.
            wxFile f(path); wxString svg;
            if (f.IsOpened() && f.ReadAll(&svg))
            {
                svg.Replace("#099268", "#0ca678");   // Open Color teal-8  -> teal-6
                svg.Replace("#69db7c", "#8ce99a");   // Open Color green-4 -> green-3
                const wxScopedCharBuffer u = svg.utf8_str();
                return wxBitmapBundle::FromSVG(u.data(), wxSize(px, px));
            }
        }
        return wxBitmapBundle::FromSVGFile(path, wxSize(px, px));
    }
    // px is the raster/default bundle size - the toolbar passes m_toolbarIconSize (configurable, restart-to-
    // apply); other callers (Function List tree, file-browser icon table) keep the fixed 16 default.
    wxBitmapBundle icon(const wxString& name, int px = 16)
    {
        // m_iconStyle: 0 = line icons (default), 1 = Solar (green), 2 = IconPark (teal/lime), 3 = Streamline (green/teal)
        if (m_iconStyle == 1) { wxBitmapBundle c = iconColored(name, "icons-solar", px); if (c.IsOk()) return c; }
        else if (m_iconStyle == 2) { wxBitmapBundle c = iconColored(name, "icons-iconpark", px); if (c.IsOk()) return c; }
        else if (m_iconStyle >= 3) { wxBitmapBundle c = iconColored(name, "icons-streamline", px); if (c.IsOk()) return c; }   // 3 also absorbs a stale ToolbarIconStyle=3 ("IconPark Bold", removed) from an older config - it lands on a colored pack either way (loadSettings clamps anything past 3)
        static const wxString dir = wxPathOnly(wxStandardPaths::Get().GetExecutablePath()) + wxFILE_SEP_PATH + "icons" + wxFILE_SEP_PATH;
        // Permissive toolbar icons (Tabler x Open Color, MIT) - monochrome by default, with a meaning-accent
        // on 8 of the 32 (gold New "+", blue Save/Save-All, red Record/Stop, green Playback/-multiple). Neutral
        // parts paint with stroke="currentColor"; the accents are explicit Open-Color hues. Resolve ONLY
        // currentColor to the theme grey (Open-Color gray-3 dark / gray-8 light) and keep the accents, so the
        // toolbar matches the icon set's design (README: "color only where it carries meaning").
        const wxString path = dir + name + ".svg";
        if (wxFileExists(path))
        {
            wxFile f(path); wxString svg;
            if (f.IsOpened() && f.ReadAll(&svg))
            {
                const wxString col = m_dark ? "#dee2e6" : "#343a40";
                svg.Replace("currentColor", col);
                const wxScopedCharBuffer u = svg.utf8_str();
                return wxBitmapBundle::FromSVG(u.data(), wxSize(px, px));
            }
            return wxBitmapBundle::FromSVGFile(path, wxSize(px, px));
        }
        return wxArtProvider::GetBitmapBundle(wxART_QUESTION, wxART_TOOLBAR, wxSize(px, px));
    }
    // A clearly-disabled version of a toolbar icon: fade the alpha channel down to ~30% instead of
    // wxToolBar's automatic "shadow" (wxImage::ConvertToDisabled blends every pixel towards a fixed
    // brightness of 255, i.e. white) - on light chrome that only lightens a dark icon a little (not a
    // strong enough break from the enabled look), and on dark chrome, where icons already paint light-
    // on-dark, blending further towards white barely changes them at all, so disabled and enabled tools
    // can look nearly identical. Scaling alpha instead works the same way regardless of chrome colour or
    // icon hue: the icon becomes see-through, letting the toolbar background show through it.
    wxBitmapBundle iconDisabled(const wxBitmapBundle& enabled, int px = 16)
    {
        wxImage img = enabled.GetBitmap(wxSize(px, px)).ConvertToImage();
        if (!img.HasAlpha()) img.InitAlpha();
        unsigned char* alpha = img.GetAlpha();
        const int n = img.GetWidth() * img.GetHeight();
        for (int i = 0; i < n; ++i) alpha[i] = static_cast<unsigned char>(alpha[i] * 0.3);
        return wxBitmapBundle::FromBitmap(wxBitmap(img));
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
    // Synthesized Print-Preview toolbar glyphs (page-nav chevrons, zoom +/-, and the close X), drawn as clean
    // line icons in the ACTIVE pack's accent so they read clearly and sit with icon()'s print glyph on that
    // toolbar. The packs ship print but no page-nav arrows, and their filled zoom magnifiers turn muddy at
    // 16px, so these are drawn here rather than as SVGs per pack. Solar/Streamline -> their signature green,
    // IconPark -> its teal, Tabler/line -> the themed grey (like icon()'s currentColor resolve).
    wxBitmapBundle previewGlyph(const char* kind, int px = 16)
    {
        // Theme-aware so the glyph reads at the same weight as icon()'s (fixed-colour) print glyph on the
        // themed toolbar: a bright pack accent on the dark bar, the deeper accent on the light bar.
        const wxString col =
            m_iconStyle == 1 ? (m_dark ? "#69db7c" : "#2f9e44") :   // Solar
            m_iconStyle == 2 ? (m_dark ? "#38d9a9" : "#0ca678") :   // IconPark (teal)
            m_iconStyle >= 3 ? (m_dark ? "#69db7c" : "#099268") :   // Streamline
            (m_dark ? "#dee2e6" : "#343a40");                       // Tabler / line: themed grey
        wxString paths;   // separate <path>/<circle> elements (one multi-subpath 'd' can confuse the SVG parser)
        if      (!strcmp(kind, "prev"))    paths = "<path d='M15 6l-6 6l6 6'/>";
        else if (!strcmp(kind, "next"))    paths = "<path d='M9 6l6 6l-6 6'/>";
        else if (!strcmp(kind, "first"))   paths = "<path d='M6 6v12'/><path d='M18 6l-6 6l6 6'/>";
        else if (!strcmp(kind, "last"))    paths = "<path d='M6 6l6 6l-6 6'/><path d='M18 6v12'/>";
        else if (!strcmp(kind, "zoomout")) paths = "<circle cx='10' cy='10' r='6'/><path d='M7 10h6'/><path d='M15 15l4.5 4.5'/>";
        else if (!strcmp(kind, "zoomin"))  paths = "<circle cx='10' cy='10' r='6'/><path d='M7 10h6'/><path d='M10 7v6'/><path d='M15 15l4.5 4.5'/>";
        else /* close */                   paths = "<path d='M6 6l12 12'/><path d='M18 6l-12 12'/>";
        const wxString svg = wxString::Format(
            "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' "
            "fill='none' stroke='%s' stroke-width='2.6' stroke-linecap='round' stroke-linejoin='round'>"
            "%s</svg>", col, paths);
        const wxScopedCharBuffer u = svg.utf8_str();
        return wxBitmapBundle::FromSVG(u.data(), wxSize(px, px));
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

    // Shared wxAuiPaneInfo recipe for a fixed, non-draggable top toolbar pane - used by both the borderless
    // integrated toolbar (dockIntegratedToolBar) and the macOS docked toolbar. ToolbarPane() must come
    // FIRST (it turns gripper/movable/floatable back ON), so the locking flags have to follow it; the
    // result is a fixed toolbar - no drag gripper, not movable, not floatable. (Defined outside the
    // WXN_HAS_BORDERLESS guard so the macOS branch, where that flag is off, can reuse it too.)
    static wxAuiPaneInfo toolbarPaneInfo()
    {
        return wxAuiPaneInfo().Name("toolbar").ToolbarPane().Top().Layer(1)
            .Gripper(false).Floatable(false).Movable(false).Dockable(false).DockFixed()
            .CaptionVisible(false).PaneBorder(false).Resizable(false);
    }

    void buildToolBar()
    {
        // Native frame: the frame's own toolbar (CreateToolBar). Integrated frame: a child wxToolBar we
        // dock as an aui pane just under the title bar (the frame toolbar would otherwise sit above it).
        wxToolBar* tb;
#ifdef WXN_HAS_BORDERLESS
        if constexpr (kBorderless) tb = makeIntegratedToolBar();
        else                       tb = CreateToolBar(wxTB_FLAT | wxTB_HORIZONTAL);
#elif defined(__WXMAC__)
        // macOS has no borderless backend, but we still avoid the frame's native NSToolbar: it inflates
        // icons to a coarse size-mode slot (ignoring SetToolBitmapSize) and turns each AddSeparator into a
        // fat NSToolbarSpaceItem that shoves the first icon ~180px right. Parenting the toolbar to
        // m_toolBarHost (a wxPanel, NOT the frame) makes wxToolBar::Create leave m_macToolbar null, so wx
        // lays the bar out itself with real icon-sized slots + thin separators; we dock it as a top AUI
        // pane below (the frame is wxAuiManager-managed, like the Windows/Linux integrated toolbar).
        tb = new wxToolBar(m_toolBarHost, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxTB_FLAT | wxTB_HORIZONTAL | wxTB_NODIVIDER);
#else
        tb = CreateToolBar(wxTB_FLAT | wxTB_HORIZONTAL);
#endif
        m_toolBarPtr = tb;                 // so toolBar() works in both modes (frame toolbar / aui pane)
        const int isz = m_toolbarIconSize;   // configurable toolbar icon size (Preferences > General, restart-to-apply)
        tb->SetToolBitmapSize(wxSize(isz, isz));
        auto T  = [&](int id, const wxString& svg, const wxString& tip) { const wxBitmapBundle bmp = icon(svg, isz); tb->AddTool(id, tip, bmp, iconDisabled(bmp, isz), wxITEM_NORMAL, tip); };
        auto TC = [&](int id, const wxString& svg, const wxString& tip) { tb->AddCheckTool(id, tip, icon(svg, isz), wxNullBitmap, tip); };

        T(kCmdFileNew, "new", _("New"));           T(kCmdFileOpen, "open", _("Open"));
        T(kCmdFileSave, "save", _("Save"));        T(kCmdFileSaveall, "save-all", _("Save All"));
        T(kCmdFileClose, "close", _("Close"));     T(kCmdFileCloseall, "close-all", _("Close All"));
        T(kCmdFilePrint, "print", _("Print"));
        T(kCmdFilePrintPreview, "print-preview", _("Print Preview"));
        tb->AddSeparator();
        T(kCmdEditCut, "cut", _("Cut"));           T(kCmdEditCopy, "copy", _("Copy"));           T(kCmdEditPaste, "paste", _("Paste"));
        tb->AddSeparator();
        T(kCmdEditUndo, "undo", _("Undo"));        T(kCmdEditRedo, "redo", _("Redo"));
        tb->AddSeparator();
        T(kCmdSearchFind, "find", _("Find"));      T(kCmdSearchReplace, "replace", _("Replace"));
        tb->AddSeparator();
        T(kCmdViewZoomin, "zoom-in", _("Zoom In"));  T(kCmdViewZoomout, "zoom-out", _("Zoom Out"));
        tb->AddSeparator();
        TC(kCmdViewWrap, "word-wrap", _("Word Wrap"));
        TC(kCmdViewAllCharacters, "all-chars", _("Show All Characters"));
        TC(kCmdViewIndentGuide, "indent-guide", _("Show Indent Guide"));
        tb->AddSeparator();
        T(kCmdViewDocMap, "doc-map", _("Document Map"));
        T(kCmdViewDoclist, "doc-list", _("Document List"));
        T(kCmdViewFuncList, "function-list", _("Function List"));
        T(kCmdViewFilebrowser, "folder-as-workspace", _("Folder as Workspace"));
        T(myID_VIEW_TERMINAL, "terminal", _("Show Terminal"));   // sits with the other panel toggles
        tb->AddSeparator();
        TC(kCmdViewMonitoring, "monitoring", _("Monitoring (tail -f)"));
        tb->AddSeparator();
        T(kCmdMacroStartRecordingMacro, "record", _("Start Recording"));
        T(kCmdMacroStopRecordingMacro, "stop-record", _("Stop Recording"));
        T(kCmdMacroPlaybackRecordedMacro, "playback", _("Playback"));
        T(kCmdMacroRunMultiMacroDlg, "playback-multiple", _("Run a Macro Multiple Times"));
        T(kCmdMacroSaveCurrentMacro, "save-macro", _("Save Current Recorded Macro"));
        tb->Realize();
        // Macro idle state (nothing recording, no macro stored): only "Start Recording"
        // is enabled; Stop / Playback / Run-Multiple / Save stay greyed until a macro is being/been recorded.
        tb->EnableTool(kCmdMacroStopRecordingMacro, false);
        tb->EnableTool(kCmdMacroPlaybackRecordedMacro, false);
        tb->EnableTool(kCmdMacroRunMultiMacroDlg, false);
        tb->EnableTool(kCmdMacroSaveCurrentMacro, false);
#ifdef __WXMSW__
        ::SendMessageW(static_cast<HWND>(tb->GetHandle()), TB_SETINDENT, 4, 0);  // small left margin before the first button
#endif
#ifdef WXN_HAS_BORDERLESS
        if constexpr (kBorderless)
        {
            dockIntegratedToolBar(tb);            // dock the child toolbar under the title bar
#ifndef __WXMSW__
            // The aui-docked toolbar's tool clicks don't reach onCommand on their own. On MSW the
            // WM_COMMAND override above already redispatches them; elsewhere there's no WM_COMMAND, so fire
            // the tool from the mouse-up via hit-test. (No double-fire: MSW is excluded, and the toolbar's
            // own wxEVT_TOOL never reaches onCommand, which listens on wxEVT_MENU.)
            tb->Bind(wxEVT_LEFT_UP, [this, tb](wxMouseEvent& ev){
                wxToolBarToolBase* tool = tb->FindToolForPosition(ev.GetX(), ev.GetY());
                if (tool && tool->IsEnabled())
                {
                    if (tool->CanBeToggled()) tb->ToggleTool(tool->GetId(), !tool->IsToggled());   // reflect check-tool state
                    wxCommandEvent ce(wxEVT_MENU, tool->GetId());
                    ce.SetInt(tool->IsToggled() ? 1 : 0);
                    this->onCommand(ce);
                }
                ev.Skip();
            });
#endif
        }
#endif
#ifdef __WXMAC__
        // Dock the non-native child toolbar as a fixed top AUI pane. The frame is wxAuiManager-managed
        // (there is no top-level box sizer), and the toolbar lives inside m_toolBarHost, so the HOST panel
        // is the pane window. Pin the pane height to the host's best height so it never grows/shrinks.
        // Mirrors dockIntegratedToolBar() but for the non-borderless macOS frame.
        {
            auto* hostSizer = new wxBoxSizer(wxHORIZONTAL);
            // Integrated top bar: reserve room at the left for the traffic lights, which the deferred
            // wxn_InlineTrafficLights call (ctor CallAfter) re-centres onto this row. 72px is a safe
            // guess for the stock button cluster; the CallAfter replaces it with the exact live inset.
            if (m_integratedBar) m_macInsetItem = hostSizer->AddSpacer(this->FromDIP(72));
            hostSizer->Add(tb, 1, wxEXPAND);
            m_toolBarHost->SetSizerAndFit(hostSizer);
            const int barH = wxMax(m_toolBarHost->GetBestSize().GetHeight(), isz + 12);   // floor so the pane can't collapse
            m_macToolbarRowH = barH;   // the traffic-light row height (see wxn_InlineTrafficLights)
            // Same fixed-toolbar pane recipe as the integrated bar (toolbarPaneInfo()), plus a pinned height.
            m_aui.AddPane(m_toolBarHost, toolbarPaneInfo().MinSize(-1, barH).MaxSize(-1, barH));
            m_aui.Update();
        }
        // Route tool clicks. The non-native macOS toolbar fires wxEVT_TOOL (== wxEVT_MENU) from each button;
        // bind it on the toolbar and do NOT Skip, so onCommand runs exactly once - the event is caught here
        // before it would propagate on to the frame, so there is no double-fire either way.
        tb->Bind(wxEVT_TOOL, [this](wxCommandEvent& ce){ this->onCommand(ce); });
        if (m_integratedBar)
        {
            // AppKit snaps the repositioned traffic lights back to their stock spot whenever it rebuilds
            // the titlebar layout - reapply after every trigger Electron re-runs its redraw from. wx's
            // shared NSWindowDelegate must NOT be replaced, so hook the equivalent wx events instead.
            auto reapply = [this](wxEvent& ev){ this->reapplyTrafficLights(); ev.Skip(); };
            this->Bind(wxEVT_SIZE,       reapply);
            this->Bind(wxEVT_ACTIVATE,   reapply);
            this->Bind(wxEVT_ICONIZE,    reapply);
            this->Bind(wxEVT_FULLSCREEN, [this](wxFullScreenEvent& ev){ this->CallAfter([this]{ this->reapplyTrafficLights(); }); ev.Skip(); });
            // With the title-bar band reclaimed, dragging must be restarted natively from empty toolbar
            // area (wx views consume the mouse-down before AppKit's frame-drag machinery sees it). A
            // click on a real tool still works: only tool-less spots start a drag.
            tb->Bind(wxEVT_LEFT_DOWN, [this, tb](wxMouseEvent& ev){
                if (tb->FindToolForPosition(ev.GetX(), ev.GetY())) { ev.Skip(); return; }
                wxn_DragWindow((void*)this->MacGetTopLevelWindowRef());
            });
            m_toolBarHost->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&){   // the spacer strip left of the toolbar
                wxn_DragWindow((void*)this->MacGetTopLevelWindowRef());
            });
        }
#endif
    }

#ifdef __WXMAC__
    // Re-centre the traffic lights in the toolbar row (integrated top bar). Idempotent and cheap;
    // called from wxEVT_SIZE/ACTIVATE/ICONIZE/FULLSCREEN because AppKit snaps the buttons back to
    // their stock spot whenever it rebuilds the titlebar layout. No-ops in native fullscreen (the
    // shim guards it) and when the integrated bar is off.
    void reapplyTrafficLights()
    {
        if (!m_integratedBar || m_macToolbarRowH <= 0) return;
        wxn_InlineTrafficLights((void*)this->MacGetTopLevelWindowRef(), m_macToolbarRowH);
    }
#endif

    void buildStatusBar()
    {
#if defined(__WXMSW__) || defined(__WXGTK__)
        // Drop the native size-grip (wxSTB_SIZEGRIP). Windows draws it on a light 3D corner that ignores
        // our dark chrome (we park our own dark SizeGripWin in the corner instead, below). GTK draws it
        // in the desktop theme's accent - e.g. a blue diagonal grip under Linux Mint's dark theme - which
        // stands out sharply against our dark chrome (this was the persistent "blue tint in the bottom-
        // right corner"). The window still resizes from its borders without it.
        auto* sb = CreateStatusBar(8, wxSTB_DEFAULT_STYLE & ~wxSTB_SIZEGRIP);
#else
        auto* sb = CreateStatusBar(8);   // macOS: keep the native grip (Cocoa themes it consistently)
#endif
        const int styles[8] = { wxSB_FLAT, wxSB_FLAT, wxSB_FLAT, wxSB_FLAT, wxSB_FLAT, wxSB_FLAT, wxSB_FLAT, wxSB_FLAT };
        sb->SetStatusStyles(8, styles);   // flat fields - no per-field sunken background
        sb->Bind(wxEVT_LEFT_DCLICK, &WxnShellFrameT::onStatusDClick, this);   // interactive status bar: double-click a field to act
        // Editable zoom combo, parked over field 6 (INS/OVR moved to 7). m_zoom is already restored from
        // config at this point (the ctor reads it before building any chrome), so seed the field directly
        // rather than waiting for a zoom event that only fires when the user changes something.
        m_zoomField = new ZoomField(sb, m_dark);
        m_zoomField->SetToolTip(_("Zoom level"));
        m_zoomField->onDropdown     = [this]{ openZoomList(); };
        m_zoomField->onPercentTyped = [this](int p){ applyZoomPercent(p); };
        // Wheel goes through SCI_ZOOMIN/OUT, not SETZOOM, so the +20/-10 engine clamps apply for free.
        m_zoomField->onWheelStep    = [this](int d){ sci(d > 0 ? SCI_ZOOMIN : SCI_ZOOMOUT); syncZoomField(); };
        m_zoomField->setPercent(percentFromZoom(m_zoom));
#ifdef __WXMSW__
        m_grip = new SizeGripWin(sb);   // our own resize grip, parked in the bottom-right corner
#endif
        // Manually placed children must be repositioned on every bar resize. This hook is deliberately
        // NOT inside the MSW-only grip block any more: on GTK/macOS the zoom field would otherwise never
        // be positioned and would sit at (0,0), on top of the language label.
        sb->Bind(wxEVT_SIZE, [this](wxSizeEvent& e){ e.Skip(); layoutStatusBarChildren(); });
        applyZoomFieldVisibility();   // also sets the field widths (index 6 depends on the setting) and lays the children out
    }

    // Field widths + zoom-field visibility in one place: the two are coupled, because hiding the child
    // window on its own would leave a visibly empty 78px slot between the encoding and INS/OVR fields.
    // Collapsing field 6 to width 0 hands those pixels back to the proportional field 0, so INS/OVR and
    // the resize grip stay flush right. Field indices are untouched either way, so updateStatus's
    // setStatus(7, ...) and onStatusDClick's "case 7" keep working in both states.
    void applyZoomFieldVisibility()
    {
        wxStatusBar* sb = GetStatusBar();
        if (!sb || sb->GetFieldsCount() != 8) return;
        // field 0 (doc type / message area) is proportional so the fields fill the whole bar and
        // the typing-mode field lands flush right (no dead slack on the right).
        // Field 6 carries no text: the ZoomField child window is parked over it (when shown).
        const int w[8] = { -1, 150, 210, 110, 130, 120, m_showZoomField ? 78 : 0, 46 };   // field 5 (encoding) wide enough for "UTF-16 LE BOM"
        sb->SetStatusWidths(8, w);
        if (m_zoomField) m_zoomField->Show(m_showZoomField);
        layoutStatusBarChildren();
        sb->Refresh();   // SetStatusWidths alone doesn't repaint the reflowed separators on MSW
    }

    void layoutStatusBarChildren()
    {
        wxStatusBar* sb = GetStatusBar();
        if (!sb) return;
        wxRect r;
        // Skipped when hidden: field 6's rect is 0px wide then, and SetSize'ing the child to it would
        // also drag it to the collapsed x - pointless work that would have to be undone on re-show.
        if (m_zoomField && m_showZoomField && sb->GetFieldRect(6, r))
            m_zoomField->SetSize(r.x, r.y + 1, r.width, r.height - 2);   // 1px inset so the outline isn't clipped
#ifdef __WXMSW__
        const wxSize ss = sb->GetClientSize();
        const int gw = 16;
        if (m_grip) m_grip->SetSize(ss.x - gw, 0, gw, ss.y);
#endif
    }

    // ----- syntax highlighting (Lexilla + theme colours) -------
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
    // Friendly document-type label for the status bar (e.g. "C++ source file"). Translated at the
    // point of construction: this string's only consumer is EditorPage::lang, which updateStatus()
    // paints into status field 0 (the session XML's "lang" attribute is written but never read back).
    static wxString langDisplayName(const wxString& e)
    {
        if (e=="cpp"||e=="cc"||e=="cxx"||e=="hpp"||e=="hxx") return _("C++ source file");
        if (e=="c"||e=="h")                                  return _("C source file");
        if (e=="cs")                                         return _("C# source file");
        if (e=="java")                                       return _("Java source file");
        if (e=="js"||e=="jsx")                               return _("JavaScript file");
        if (e=="ts"||e=="tsx")                               return _("TypeScript file");
        if (e=="py"||e=="pyw")                               return _("Python file");
        if (e=="json")                                       return _("JSON file");
        if (e=="xml"||e=="svg"||e=="xaml"||e=="xsd"||e=="xsl"||e=="vcxproj") return _("XML file");
        if (e=="css")                                        return _("CSS file");
        if (e=="sql")                                        return _("SQL file");
        if (e=="lua")                                        return _("Lua source file");
        if (e=="sh"||e=="bash")                              return _("Shell script file");
        if (e=="bat"||e=="cmd")                              return _("Batch file");
        if (e=="rb")                                         return _("Ruby source file");
        if (e=="rs")                                         return _("Rust source file");
        if (e=="go")                                         return _("Go source file");
        if (e=="pl"||e=="pm")                                return _("Perl source file");
        if (e=="ps1"||e=="psm1")                             return _("PowerShell file");
        if (e=="ini"||e=="properties"||e=="cfg")            return _("Properties file");
        if (e=="yml"||e=="yaml")                             return _("YAML file");
        return _("Normal text file");
    }
    // Content-based language sniff for documents the extension table can't place (no extension, or an
    // unknown one): shebang interpreters, XML/HTML prologs and JSON shapes. Returns the CANONICAL
    // extension for the existing ext-driven tables (lexerForExt / langDisplayName / keyword pick), so
    // detection has exactly one downstream path; "" = no idea, stay plain text. Only languages the app
    // actually has lexers for are returned (e.g. HTML maps to the XML lexer - tag colouring beats plain).
    static wxString extFromContent(const wxString& head)
    {
        wxString h = head; h.Replace("\r", "\n");
        wxString first = h.BeforeFirst('\n'); first.Trim(true).Trim(false);
        if (first.StartsWith("#!"))
        {
            wxString cmd = first.Mid(2); cmd.Replace("\t", " "); cmd.Trim(true).Trim(false);
            wxArrayString parts = wxSplit(cmd, ' ');
            wxString interp = parts.empty() ? wxString() : parts[0].AfterLast('/');
            if (interp == "env" && parts.size() > 1) interp = parts[1].AfterLast('/');   // "#!/usr/bin/env python3"
            if (interp.StartsWith("python")) return "py";
            if (interp.StartsWith("perl"))   return "pl";
            if (interp.StartsWith("ruby"))   return "rb";
            if (interp.StartsWith("node"))   return "js";
            if (interp.StartsWith("lua"))    return "lua";
            if (interp.StartsWith("pwsh"))   return "ps1";
            return "sh";   // sh/bash/zsh/ksh/dash/fish or an unknown interpreter: shell beats plain text
        }
        wxString t = h; t.Trim(false);
        const wxString tl = t.Lower();
        if (tl.StartsWith("<?xml"))                              return "xml";
        if (tl.StartsWith("<!doctype html") || tl.StartsWith("<html")) return "xml";   // no dedicated HTML lexer wired (yet)
        if (!t.empty() && (t[0] == '{' || t[0] == '['))
            if (t.Contains("\":") || t.Contains("\" :"))         return "json";        // opener + a quoted-key colon = JSON-ish
        return "";
    }
    void setLexerForFile(const wxString& path)
    {
        applyEditorTheme(m_dark);          // reset every style to the theme base (incl. line numbers)
        auto* page = activePage();
        // Registered Scintillua language: auto-detect by extension (unless one is already chosen/forced),
        // then container-lex it via the embedded engine.
        if (page && page->sciLang.empty() && !page->langForced)
        {
            const wxString ext = path.AfterLast('.').Lower();
            for (const auto& l : m_sciLangs)
            {
                bool matched = false;
                wxStringTokenizer tk(l.exts, " ", wxTOKEN_STRTOK);
                while (!ext.empty() && tk.HasMoreTokens()) if (tk.GetNextToken().Lower() == ext) { matched = true; break; }
                if (matched) { page->sciLang = l.name; break; }
            }
        }
        if (page && !page->sciLang.empty())   // Scintillua: container-lexed by the embedded engine
        {
            applyScintilluaStyles(m_stc);
            page->lang = page->sciLang;
            m_stc->Colourise(0, -1);   // fires STYLENEEDED -> scintilluaStyle, which sets every fold level
            return;
        }
        // A manual Language pick forces its lexer directly; otherwise auto-detect from the file extension.
        wxString lexer, themeKey, disp, ext;
        if (page && page->langForced) { lexer = page->forcedLexer; themeKey = page->forcedLexer; disp = page->forcedName; }
        else { ext = path.AfterLast('.').Lower(); LexMap lm = lexerForExt(ext);
               if (!lm.lexer && m_stc && m_stc->GetLength() > 0)
               {   // the extension told us nothing - sniff the buffer head (shebang / prolog / JSON shape)
                   const wxString sniffed = extFromContent(m_stc->GetTextRange(0, wxMin((int)m_stc->GetLength(), 512)));
                   if (!sniffed.empty()) { ext = sniffed; lm = lexerForExt(ext); }
               }
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
                    for (const StyleDef& s : it->second)   // apply the theme's exact per-token colours
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
    void setDocTitle(const wxString& name) { if (auto* p = activePage()) { p->title = name; refreshTab(p); } else setWindowTitle(name); }
    void doNew()   // New opens a fresh tab - honouring the New Document default EOL + language
    {
        addDocument("", nextNewName());
        if (auto* p = activePage()) p->encoding = m_defaultEncoding;
        setEol(m_defaultEol);
        if (m_defaultLangId >= 0) { if (const WxnLang* L = wxnLangFind(m_defaultLangId)) setForcedLang(L->lexer, L->name); }
        updateEncodingMenuChecks();
        updateStatus();   // reflect the default EOL/encoding/language in the status bar
    }
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
        if (!hi) { enc = ENC_UTF8; return raw; }                                    // pure ASCII: report UTF-8 (bytes identical either way)
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
            case kCmdFormatWin1250: return 1250; case kCmdFormatWin1251: return 1251; case kCmdFormatWin1252: return 1252;
            case kCmdFormatWin1253: return 1253; case kCmdFormatWin1254: return 1254; case kCmdFormatWin1255: return 1255;
            case kCmdFormatWin1256: return 1256; case kCmdFormatWin1257: return 1257; case kCmdFormatWin1258: return 1258;
            case kCmdFormatIso88591: return 28591; case kCmdFormatIso88592: return 28592; case kCmdFormatIso88593: return 28593;
            case kCmdFormatIso88594: return 28594; case kCmdFormatIso88595: return 28595; case kCmdFormatIso88596: return 28596;
            case kCmdFormatIso88597: return 28597; case kCmdFormatIso88598: return 28598; case kCmdFormatIso88599: return 28599;
            case kCmdFormatIso885913: return 28603; case kCmdFormatIso885914: return 28604; case kCmdFormatIso885915: return 28605;
            case kCmdFormatDos437: return 437; case kCmdFormatDos720: return 720; case kCmdFormatDos737: return 737;
            case kCmdFormatDos775: return 775; case kCmdFormatDos850: return 850; case kCmdFormatDos852: return 852;
            case kCmdFormatDos855: return 855; case kCmdFormatDos857: return 857; case kCmdFormatDos858: return 858;
            case kCmdFormatDos860: return 860; case kCmdFormatDos861: return 861; case kCmdFormatDos862: return 862;
            case kCmdFormatDos863: return 863; case kCmdFormatDos865: return 865; case kCmdFormatDos866: return 866;
            case kCmdFormatDos869: return 869;
            case kCmdFormatKoi8rCyrillic: return 20866; case kCmdFormatKoi8uCyrillic: return 21866; case kCmdFormatMacCyrillic: return 10007;
            case kCmdFormatBig5: return 950; case kCmdFormatGb2312: return 936; case kCmdFormatShiftJis: return 932;
            case kCmdFormatEucKr: return 51949; case kCmdFormatKoreanWin: return 949; case kCmdFormatTis620: return 874;
            default: return 0;
        }
    }
    void interpretCharset(int cp, const wxString& name)   // re-read the file decoded as that code page
    {
#ifdef __WXMSW__
        const wxString p = curPath();
        if (!p.empty())
        {
            if (sci(SCI_GETMODIFY) && wxMessageBox(_("Re-interpreting the encoding discards unsaved changes. Continue?"), _("Encoding"), wxYES_NO | wxICON_QUESTION, this) != wxYES) return;
            setDocUtf8(cpToUtf8((UINT)cp, readRawBytes(p))); sci(SCI_EMPTYUNDOBUFFER); sci(SCI_SETSAVEPOINT);
        }
        if (auto* pg = activePage()) { pg->encoding = ENC_CHARSET; pg->codepage = cp; pg->encLabel = name; }
        updateStatus(); updateEncodingMenuChecks();
#else
        (void)cp; notImpl(wxString::Format(_("%s (Windows only)"), name));
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
        chk(kCmdFormatAnsi, enc == ENC_ANSI); chk(kCmdFormatAsUtf8, enc == ENC_UTF8); chk(kCmdFormatUtf8, enc == ENC_UTF8_BOM);
        chk(kCmdFormatUtf16le, enc == ENC_UTF16_LE); chk(kCmdFormatUtf16be, enc == ENC_UTF16_BE);
    }
    void convertTo(int enc)   // change the on-disk encoding; text is unchanged, written on next save
    {
        if (auto* p = activePage()) p->encoding = enc;
        setStatus(0, wxString::Format(_("Encoding: %s (save to apply)"), encName(enc))); m_hint = true;
        updateStatus(); updateEncodingMenuChecks();
    }
    void interpretAs(int enc)   // re-read the file's bytes decoded as the chosen encoding ("interpret as")
    {
        const wxString p = curPath();
        if (p.empty()) { if (auto* pg = activePage()) pg->encoding = enc; updateStatus(); updateEncodingMenuChecks(); return; }
        if (sci(SCI_GETMODIFY) && wxMessageBox(_("Re-interpreting the encoding discards unsaved changes. Continue?"), _("Encoding"), wxYES_NO | wxICON_QUESTION, this) != wxYES) return;
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
        // Detect the file's line-ending style and match Scintilla's EOL mode to it, so the
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
    void onOpen() { wxFileDialog d(this, _("Open"), "", "", _("All files (*.*)|*.*"), wxFD_OPEN | wxFD_FILE_MUST_EXIST); if (d.ShowModal() == wxID_OK) addDocument(d.GetPath(), wxFileNameFromPath(d.GetPath())); }
    void onReload() { if (!m_path.empty()) loadFile(m_path); }

    // ---- View > Monitoring (tail -f): poll monitored files for external changes; reload + jump to end ----
    static bool monStat(const wxString& path, wxLongLong& mtime, wxULongLong& size)
    {
        wxLogNull noLog;   // rotation races / transient AV locks must not pop error dialogs from the poll timer
#ifdef __WXMSW__
        WIN32_FILE_ATTRIBUTE_DATA fad{};   // one attributes syscall for existence + mtime + size (per tick, forever)
        if (!::GetFileAttributesExW(path.ToStdWstring().c_str(), GetFileExInfoStandard, &fad)) return false;
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
        mtime = wxLongLong(fad.ftLastWriteTime.dwHighDateTime, fad.ftLastWriteTime.dwLowDateTime);
        size  = wxULongLong(fad.nFileSizeHigh, fad.nFileSizeLow);
#else
        wxFileName fn(path);
        if (!fn.FileExists()) return false;
        const wxDateTime dt = fn.GetModificationTime();
        mtime = dt.IsValid() ? dt.GetValue() : wxLongLong(0);
        size  = fn.GetSize();
#endif
        return true;
    }
    void syncMonitoringUi(EditorPage* p) { setToggleUi(kCmdViewMonitoring, p && p->monitored); }
    void updateMonTimer()   // the one shared 1s poll timer runs only while at least one tab is monitored
    {
        bool any = false; for (EditorPage* q : allPages()) if (q && q->monitored) { any = true; break; }
        if (any && !m_monTimer) { m_monTimer = new wxTimer(this, myID_MONTIMER); Bind(wxEVT_TIMER, &WxnShellFrameT::onMonTimer, this, myID_MONTIMER); }
        if (m_monTimer) { if (any) { if (!m_monTimer->IsRunning()) m_monTimer->Start(1000); } else m_monTimer->Stop(); }
    }
    void monStatus(const wxString& msg) { setStatus(0, msg); m_hint = true; }   // m_hint keeps the 150ms status refresh from wiping it
    // Reload the (active) monitored page and follow the end. When the file merely GREW and the page is
    // plain UTF-8, append just the new bytes instead of re-reading + re-lexing the whole file - the
    // difference between a few KB and the entire log, every second. Any other change -> full reload.
    void monReloadTail(EditorPage* p, wxULongLong oldSize)
    {
        bool appended = false;
        if (p->encoding == ENC_UTF8 && p->monSize > oldSize)
        {
            wxFile f(p->path);
            if (f.IsOpened() && f.Seek(static_cast<wxFileOffset>(oldSize.GetValue())) != wxInvalidOffset)
            {
                const size_t n = static_cast<size_t>((p->monSize - oldSize).GetValue());
                std::string chunk(n, '\0');
                const ssize_t got = f.Read(&chunk[0], n);
                // only take the shortcut when the chunk starts on a UTF-8 boundary (not a continuation byte)
                if (got == static_cast<ssize_t>(n) && (chunk.empty() || (static_cast<unsigned char>(chunk[0]) & 0xC0) != 0x80))
                {
                    sci(SCI_APPENDTEXT, n, reinterpret_cast<sptr_t>(chunk.data()));
                    sci(SCI_SETSAVEPOINT);   // like loadFile: content mirrors disk, keep the doc "clean"
                    appended = true;
                }
            }
        }
        if (!appended) loadFile(p->path);                  // same path as File > Reload from Disk
        sci(SCI_DOCUMENTEND); sci(SCI_SCROLLCARET);        // tail -f: follow the end
    }
    void toggleMonitoring()
    {
        EditorPage* p = activePage();
        if (!p) return;
        if (p->path.empty()) { monStatus(_("Monitoring needs a file on disk - save the document first")); syncMonitoringUi(p); return; }
        if (!p->monitored && sci(SCI_GETMODIFY)) { monStatus(_("Save or discard the unsaved changes before monitoring")); syncMonitoringUi(p); return; }
        p->monitored = !p->monitored;
        if (p->monitored)
        {
            monStat(p->path, p->monMtime, p->monSize);
            sci(SCI_DOCUMENTEND); sci(SCI_SCROLLCARET);   // tail: start watching from the end
            monStatus(_("Monitoring started"));
        }
        else monStatus(_("Monitoring stopped"));
        syncMonitoringUi(p);
        updateMonTimer();
    }
    // Poll only the ACTIVE page: background monitored tabs catch up with one stat-compare on activation
    // (see activateBuffer), so they cost nothing per tick and can never clobber an edited buffer.
    void onMonTimer(wxTimerEvent&)
    {
        EditorPage* p = activePage();
        if (!p || !p->monitored) return;
        wxLongLong mt; wxULongLong sz;
        if (!monStat(p->path, mt, sz)) return;                         // file temporarily gone (rotation) - keep watching
        if (mt == p->monMtime && sz == p->monSize) return;             // unchanged
        const wxULongLong oldSize = p->monSize;
        p->monMtime = mt; p->monSize = sz;
        if (sci(SCI_GETMODIFY)) { p->monitored = false; syncMonitoringUi(p); monStatus(_("Monitoring stopped (document was edited)")); updateMonTimer(); return; }
        monReloadTail(p, oldSize);
    }
#ifdef __WXMSW__
    // The unprivileged process can't write path (access denied) - ask, then relaunch itself elevated
    // (via ShellExecuteExW "runas") with a hidden --elevated-write switch to copy just this one file in.
    // The elevated child runs NO GUI code at all (see WxnApp::OnInit) - it exists only to do that copy.
    bool tryElevatedWrite(const wxString& path, const std::string& data)
    {
        if (wxMessageBox(
                wxString::Format(_("\"%s\" cannot be written with your current permissions.\n\nRelaunch wxnote elevated to save this file?"), path),
                _("Access Denied"), wxYES_NO | wxICON_QUESTION, this) != wxYES)
            return false;

        const wxString tempPath = wxFileName::CreateTempFileName("wxnote_elev_");
        if (tempPath.empty()) return false;
        { wxFile tf(tempPath, wxFile::write); if (!tf.IsOpened()) return false; tf.Write(data.data(), data.size()); }

        const wxString exePath = wxStandardPaths::Get().GetExecutablePath();
        const wxString params = wxString::Format("--elevated-write \"%s\" \"%s\"", tempPath, path);
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"runas";
        sei.lpFile = exePath.wc_str();
        sei.lpParameters = params.wc_str();
        sei.nShow = SW_HIDE;
        if (!::ShellExecuteExW(&sei) || !sei.hProcess) { wxRemoveFile(tempPath); return false; }   // user declined the UAC prompt, or launch failed
        ::WaitForSingleObject(sei.hProcess, 15000);   // give the elevated copy a moment; don't hang the UI forever
        ::CloseHandle(sei.hProcess);
        wxRemoveFile(tempPath);
        return wxFileExists(path);
    }
#endif
    // Encode the CURRENTLY-MOUNTED document for buffer p's encoding and write it to `path`. Returns false
    // only if the file couldn't be opened (after the Windows elevated-write fallback). Shared by writeFile
    // (active buffer) and writePageToDisk (background buffers swapped in by Save All).
    bool writeMountedDoc(EditorPage* p, const wxString& path)
    {
        const int len = static_cast<int>(sci(SCI_GETLENGTH));
        std::string b(static_cast<size_t>(len) + 1, '\0');
        sci(SCI_GETTEXT, len + 1, reinterpret_cast<sptr_t>(&b[0])); b.resize(len);
        const std::string out = encodeForPage(b, p);
        wxFile f(path, wxFile::write);
        if (!f.IsOpened())
        {
#ifdef __WXMSW__
            if (errno != EACCES || !tryElevatedWrite(path, out)) return false;
#else
            return false;
#endif
        }
        else
            f.Write(out.data(), out.size());
        return true;
    }
    bool writeFile(const wxString& path)
    {
        EditorPage* p = activePage();
        // nib.events v2: about to write this buffer (fires whether or not the write then succeeds -
        // NPPN_FILEBEFORESAVE semantics in the bridge). The buffer is mounted, so a subscriber's edits
        // (trim-on-save style) land in exactly the content that is written below.
        nibFireDocEvent(NIB_EV_DOCUMENT_SAVING, p);
        if (!writeMountedDoc(p, path)) return false;
        sci(SCI_SETSAVEPOINT);                        // fires the v1 savepoint-derived DOCUMENT_SAVED
        if (p) { p->path = path; clearRecovery(p); }   // content is safely on disk now - no recovery copy needed
        m_path = path; setDocTitle(wxFileNameFromPath(path)); setLexerForFile(path); addToMRU(path);
        nibFireDocEvent(NIB_EV_DOCUMENT_SAVED, p, NIB_FIRE_V2_ONLY);   // v2: the REAL save signal, with the saved buffer's id
        return true;
    }
    // Save a BACKGROUND buffer (its document must already be swapped into the shared editor by onSaveAll).
    // Unlike writeFile it touches none of the active-view chrome (window title, MRU, lexer, m_path) - only
    // this page's own on-disk copy plus its dirty/recovery bookkeeping.
    bool writePageToDisk(EditorPage* p)
    {
        if (!p || p->path.empty()) return false;
        nibFireDocEvent(NIB_EV_DOCUMENT_SAVING, p);   // v2 - carries THIS page's id, not the active one's
        if (!writeMountedDoc(p, p->path)) return false;
        sci(SCI_SETSAVEPOINT); clearRecovery(p); p->dirty = false;
        nibFireDocEvent(NIB_EV_DOCUMENT_SAVED, p, NIB_FIRE_V2_ONLY);   // v2: right id even for a background Save All write
        return true;
    }
    void onSave() { if (m_path.empty()) onSaveAs(); else writeFile(m_path); }
    void onSaveAs() { wxFileDialog d(this, _("Save As"), "", "new 1.txt", _("All files (*.*)|*.*"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT); if (d.ShowModal() == wxID_OK) writeFile(d.GetPath()); }
    // Repaint page p's tab label (add/remove the unsaved "*") on ITS OWN notebook - works for a page in
    // EITHER split view, unlike refreshTab() which only touches the active view's strip.
    void refreshTabLabel(EditorPage* p)
    {
        ViewPane* v = viewOf(p); if (!p || !v || !v->tabs) return;
        const wxString lbl = (p->dirty ? wxString("*") : wxString()) + p->title;
        const int idx = v->tabs->GetPageIndex(p);
        if (idx != wxNOT_FOUND && v->tabs->GetPageText(idx) != lbl) v->tabs->SetPageText(idx, lbl);
    }
    // File > Save All: write EVERY dirty document across both split views, not just the active one.
    void onSaveAll()
    {
        if (!m_stc) { onSave(); return; }             // no editor mounted yet - fall back to single-doc save
        EditorPage* const active = activePage();
        std::vector<EditorPage*> untitled;            // dirty buffers with no path yet -> Save As, after the sweep

        // (1) The active document is already mounted - save it through the normal path (title/MRU/lexer).
        if (active && sci(SCI_GETMODIFY) != 0)
        {
            if (active->path.empty()) untitled.push_back(active);
            else                      writeFile(active->path);
        }

        // (2) Every OTHER document (both views): peek its buffer in via a raw doc-pointer swap and, if it's
        // dirty, write it - without going through activateBuffer, so the active view's lexer/doc-map/status
        // aren't churned. A doc swap clears the view's per-view selection (see the second-view lifecycle
        // notes / sortTabs' size sort), so snapshot the caret + scroll and restore them afterwards.
        const sptr_t savedDoc    = sci(SCI_GETDOCPOINTER);
        const int    savedAnchor = static_cast<int>(sci(SCI_GETANCHOR));
        const int    savedCaret  = static_cast<int>(sci(SCI_GETCURRENTPOS));
        const int    savedTop    = static_cast<int>(sci(SCI_GETFIRSTVISIBLELINE));
        bool swapped = false;
        for (EditorPage* p : allPages())
        {
            if (!p || p == active) continue;
            sci(SCI_SETDOCPOINTER, 0, p->doc); swapped = true;   // mount this background buffer to inspect/write it
            const bool dirty = sci(SCI_GETMODIFY) != 0;
            p->dirty = dirty;                                    // refresh the cached flag while the buffer is mounted
            if (!dirty) continue;
            if (p->path.empty()) { untitled.push_back(p); continue; }
            if (writePageToDisk(p)) refreshTabLabel(p);   // drop this tab's unsaved "*" (its strip may be the other view)
        }
        if (swapped)                                             // restore the active view's own document + caret/scroll
        {
            sci(SCI_SETDOCPOINTER, 0, savedDoc);
            sci(SCI_SETSEL, savedAnchor, savedCaret);
            sci(SCI_SETFIRSTVISIBLELINE, savedTop);
        }

        // (3) Untitled dirty buffers need a filename - activate each and run Save As (one prompt each).
        for (EditorPage* p : untitled) { activatePage(p); onSaveAs(); }
        if (active && activePage() != active) activatePage(active);   // return to the originally-active tab

        updateStatus(); updateUiState();
    }

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

    // ----- line / blank / sort operations (Edit menu) ----------
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
    //  Additional menu commands (clipboard / files / shell / search / view / tools)
    // ===================================================================================
    bool m_onTop = false;                                  // "Always on Top" state
    wxString curPath() const { auto* p = activePage(); return p ? p->path : wxString(); }
    void copyToClip(const wxString& t) { if (!t.empty() && wxTheClipboard->Open()) { wxTheClipboard->SetData(new wxTextDataObject(t)); wxTheClipboard->Close(); } }
    wxString getClipText() { wxString t; if (wxTheClipboard->Open()) { if (wxTheClipboard->IsSupported(wxDF_UNICODETEXT)) { wxTextDataObject d; wxTheClipboard->GetData(d); t = d.GetText(); } wxTheClipboard->Close(); } return t; }
    // Paste Special: the selection's raw bytes (embedded nulls and all - getSelUtf8() already preserves
    // them via a known length, not strlen) round-tripped through a private registered clipboard format.
    // Plain Win32 SetClipboardData (immediate rendering) instead of wx's wxDataObject/OLE clipboard path -
    // the latter uses delayed rendering for custom formats and a same-process write-then-read-back never
    // saw the format as available (IsSupported() false immediately after our own SetData()). Also writes
    // CF_UNICODETEXT in the same session, so pasting into another app still gets ordinary text.
#ifdef __WXMSW__
    static UINT cfBinary() { static UINT cf = ::RegisterClipboardFormatW(L"wxNoteBinary"); return cf; }
#endif
    void copyCutBinary(bool cut)
    {
#ifdef __WXMSW__
        const std::string data = getSelUtf8();
        if (data.empty()) return;
        if (::OpenClipboard(GetHwnd()))
        {
            ::EmptyClipboard();
            const std::wstring w = wxString::FromUTF8(data.data(), data.size()).ToStdWstring();
            if (HGLOBAL ht = ::GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t)))
            {
                if (void* p = ::GlobalLock(ht)) { memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t)); ::GlobalUnlock(ht); }
                ::SetClipboardData(CF_UNICODETEXT, ht);
            }
            if (HGLOBAL hb = ::GlobalAlloc(GMEM_MOVEABLE, sizeof(uint32_t) + data.size()))   // [length][raw bytes] - exact round-trip regardless of GlobalAlloc's own size rounding
            {
                if (auto* p = static_cast<unsigned char*>(::GlobalLock(hb)))
                {
                    const uint32_t n = (uint32_t)data.size();
                    memcpy(p, &n, sizeof(n));
                    memcpy(p + sizeof(n), data.data(), data.size());
                    ::GlobalUnlock(hb);
                }
                ::SetClipboardData(cfBinary(), hb);
            }
            ::CloseClipboard();
        }
        if (cut) sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>(""));
#else
        (void)cut; notImpl(_("Binary Clipboard (Windows only)"));
#endif
    }
    void pasteBinary()
    {
#ifdef __WXMSW__
        if (!::IsClipboardFormatAvailable(cfBinary())) { notImpl(_("Paste Binary Content (clipboard has no matching content)")); return; }
        if (!::OpenClipboard(GetHwnd())) return;
        if (HANDLE h = ::GetClipboardData(cfBinary()))
        {
            if (const auto* p = static_cast<const unsigned char*>(::GlobalLock(h)))
            {
                uint32_t n = 0; memcpy(&n, p, sizeof(n));
                sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>(""));
                sci(SCI_ADDTEXT, (uptr_t)n, reinterpret_cast<sptr_t>(p + sizeof(n)));
                ::GlobalUnlock(h);
            }
        }
        ::CloseClipboard();
#else
        notImpl(_("Paste Binary Content (Windows only)"));
#endif
    }
    // Paste HTML/RTF Content: the raw markup source apps register when copying rich content ("HTML Format" /
    // "Rich Text Format" are real Windows clipboard format NAMES, e.g. what a browser or Word puts there) -
    // inserted as literal text, not rendered. Both are conventionally null-terminated, unlike our own binary format.
    void pasteRichFormat(const wxString& formatName)
    {
#ifdef __WXMSW__
        wxDataFormat fmt(formatName);
        if (!wxTheClipboard->Open()) return;
        if (wxTheClipboard->IsSupported(fmt))
        {
            wxCustomDataObject obj(fmt);
            wxTheClipboard->GetData(obj);
            const char* p = static_cast<const char*>(obj.GetData());
            const size_t n = strnlen(p, obj.GetSize());
            sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>(""));
            sci(SCI_ADDTEXT, (uptr_t)n, reinterpret_cast<sptr_t>(p));
        }
        else wxMessageBox(_("Clipboard has no matching content."), _("Paste"), wxOK | wxICON_INFORMATION, this);
        wxTheClipboard->Close();
#else
        notImpl(wxString::Format(_("Paste %s (Windows only)"), formatName));
#endif
    }
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
        // Untitled buffers have no containing folder - fall back to the working directory, exactly like
        // the cmd/PowerShell/Terminal entries in the same submenu, instead of a dead disabled item.
        const wxString p = curPath();
        const wxString dir = p.empty() ? wxGetCwd() : wxFileName(p).GetPath();
        // Nothing to select: just open the folder, through the shared cascade in launchFolder().
        if (p.empty()) { openFolder(dir); return; }
#ifdef __WXMSW__
        const std::wstring arg = L"/select,\"" + p.ToStdWstring() + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
#elif defined(__WXMAC__)
        wxExecute(wxString::Format("open -R \"%s\"", p), wxEXEC_ASYNC);   // reveal (select) the file in Finder
#else
        openFolder(dir);   // no portable "select this file" on Linux - open the containing folder instead
#endif
    }
#ifdef __WXMSW__
    // Windows only: the two ABI-frozen static entries (cmd/PowerShell, original command ids - see
    // menu_data_file.h). Everything genuinely dynamic (pwsh/Cygwin/WSL, and the whole Linux/macOS
    // terminal-app list) goes through openHereToolAt() below instead.
    void openShellHere(bool powershell)
    {
        const wxString dir = curPath().empty() ? wxGetCwd() : wxFileName(curPath()).GetPath();
        const std::wstring exe = powershell ? L"powershell.exe" : L"cmd.exe";
        ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, dir.ToStdWstring().c_str(), SW_SHOWNORMAL);
    }
#endif
    // File > Open Containing Folder's dynamically-detected entries (see terminal_panel.h's
    // detect*OpenHereTools/TerminalEmulators/TerminalApps, populated in buildMenuBar()). `launch` is
    // interpreted per platform: a full command line on Windows, a bare executable on Linux, an app name
    // for `open -a` on macOS - matching what each detector produces.
    void openHereToolAt(int idx)
    {
        if (idx < 0 || idx >= (int)m_openFolderTools.size()) return;
        const wxString dir = curPath().empty() ? wxGetCwd() : wxFileName(curPath()).GetPath();
        const wxString& launch = m_openFolderTools[(size_t)idx].launch;
#ifdef __WXMSW__
        wxExecuteEnv env; env.cwd = dir; wxGetEnvMap(&env.env); env.env["CHERE_INVOKING"] = "1";   // Cygwin --login keeps cwd instead of jumping to ~
        wxExecute(launch, wxEXEC_ASYNC, nullptr, &env);
#elif defined(__WXMAC__)
        wxExecute(wxString::Format("open -a \"%s\" \"%s\"", launch, dir), wxEXEC_ASYNC);
#else
        wxExecuteEnv env; env.cwd = dir;
        wxLogNull noLog;
        wxExecute(launch, wxEXEC_ASYNC, nullptr, &env);
#endif
    }
    void openInDefaultViewer() { const wxString p = curPath(); if (p.empty()) { notImpl(_("Open in Default Viewer (save the file first)")); return; } wxLaunchDefaultApplication(p); }
    // THE one way to open a directory in the desktop's file manager - revealInFolder() below routes
    // here too, so the per-platform cascade lives in exactly one place. Each branch is load-bearing:
    // on Linux, wxLaunchDefaultApplication routes through gnome/gvfs paths that are flaky for
    // DIRECTORIES on some distros (reported "doesn't work" on Mint), so xdg-open - what every desktop's
    // file-manager association actually answers to - has to be tried first.
    //
    // A false return is trustworthy; a true one is not a promise. On Linux/macOS the helper is spawned
    // async and reports success as soon as it starts, whatever it does afterwards.
    bool launchFolder(const wxString& dir)
    {
#ifdef __WXMSW__
        // ShellExecuteW's HINSTANCE is a fake handle: any value <= 32 is an error code, not success.
        return reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", dir.ToStdWstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#elif defined(__WXMAC__)
        wxLogNull noLog;
        return wxExecute(wxString::Format("open \"%s\"", dir), wxEXEC_ASYNC) > 0;
#else
        wxLogNull noLog;
        if (wxExecute(wxString::Format("xdg-open \"%s\"", dir), wxEXEC_ASYNC) > 0) return true;
        return wxLaunchDefaultApplication(dir);
#endif
    }
    // Opening a path that doesn't exist otherwise fails SILENTLY - the menu item just looks dead, which
    // is exactly how a missing plugins/ dir presented before that caller learned to create it on demand.
    // m_hint keeps the message on the status bar; without it the 150ms status timer wipes it instantly.
    void openFolder(const wxString& dir)
    {
        if (!launchFolder(dir)) { setStatus(0, wxString::Format(_("Could not open folder: %s"), dir)); m_hint = true; }
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
        wxFileDialog dlg(this, _("Save a Copy As"), wxFileName(curPath()).GetPath(), wxFileNameFromPath(curPath()), _("All files (*.*)|*.*"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg.ShowModal() != wxID_OK) return;
        const std::string body = encodeForPage(getDocUtf8(), activePage());
        wxFile f(dlg.GetPath(), wxFile::write);
        if (f.IsOpened()) { f.Write(body.data(), body.size()); setStatus(0, wxString::Format(_("Copy saved: %s"), dlg.GetPath())); }
    }
    // The rename operation itself (no dialog): fires NPPN_FILERENAMED on success or NPPN_FILERENAMECANCEL
    // on failure, via the nib.events v4 kinds. Callers fire NIB_EV_FILE_BEFORE_RENAME BEFORE calling this,
    // so the before/renamed(or cancel) pair brackets the attempt. Returns success.
    bool renameActiveTo(const wxString& newPath)
    {
        EditorPage* ep = activePage();
        const wxString old = ep ? ep->path : wxString();
        if (!ep || old.empty() || newPath.empty()) return false;
        if (!wxRenameFile(old, newPath)) { nibFireDocEvent(NIB_EV_FILE_RENAME_CANCEL, ep); return false; }   // -> NPPN_FILERENAMECANCEL
        ep->path = newPath; ep->title = wxFileNameFromPath(newPath); setLexerForFile(newPath); refreshTab(ep); setWindowTitle(ep->title);
        nibFireDocEvent(NIB_EV_FILE_RENAMED, ep);   // -> NPPN_FILERENAMED (this buffer's id)
        return true;
    }
    void renameFile()
    {
        EditorPage* ep = activePage();
        const wxString p = curPath();
        if (p.empty()) { notImpl(_("Rename (save the file first)")); return; }
        nibFireDocEvent(NIB_EV_FILE_BEFORE_RENAME, ep);   // -> NPPN_FILEBEFORERENAME (before the rename UI)
        wxFileDialog dlg(this, _("Rename"), wxFileName(p).GetPath(), wxFileNameFromPath(p), _("All files (*.*)|*.*"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg.ShowModal() != wxID_OK) { nibFireDocEvent(NIB_EV_FILE_RENAME_CANCEL, ep); return; }   // user cancelled -> NPPN_FILERENAMECANCEL
        renameActiveTo(dlg.GetPath());
    }
    // The delete operation itself (no confirm dialog): fires NIB_EV_FILE_BEFORE_DELETE, deletes the active
    // document's on-disk file, then NIB_EV_FILE_DELETED (success, fired while the buffer id is still
    // resolvable) or NIB_EV_FILE_DELETE_FAILED. On success the tab is closed. Returns success.
    bool recycleActive()
    {
        EditorPage* ep = activePage();
        const wxString p = curPath();
        if (!ep || p.empty()) return false;
        nibFireDocEvent(NIB_EV_FILE_BEFORE_DELETE, ep);   // -> NPPN_FILEBEFOREDELETE
#ifdef __WXMSW__
        std::wstring from = p.ToStdWstring(); from.push_back(L'\0'); from.push_back(L'\0');   // double-NUL terminated list
        SHFILEOPSTRUCTW op{}; op.wFunc = FO_DELETE; op.pFrom = from.c_str(); op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
        const bool ok = (SHFileOperationW(&op) == 0);
#else
        const bool ok = wxRemoveFile(p);
#endif
        if (ok) { nibFireDocEvent(NIB_EV_FILE_DELETED, ep); closeActive(); }   // -> NPPN_FILEDELETED (id still valid), then close the tab
        else    { nibFireDocEvent(NIB_EV_FILE_DELETE_FAILED, ep); }             // -> NPPN_FILEDELETEFAILED
        return ok;
    }
    void recycleFile()
    {
        const wxString p = curPath();
        if (p.empty()) { notImpl(_("Move to Recycle Bin (save the file first)")); return; }
        if (wxMessageBox(wxString::Format(_("Move \"%s\" to the Recycle Bin?"), wxFileNameFromPath(p)), "wxNote", wxYES_NO | wxICON_QUESTION, this) != wxYES) return;
        recycleActive();
    }
    void closeAllSide(bool toRight)
    {
        const int cur = m_tabs->GetSelection();
        for (int i = (int)m_tabs->GetPageCount() - 1; i >= 0; --i)
            if ((toRight && i > cur) || (!toRight && i < cur))
            {
                nibFireDocEvent(NIB_EV_DOCUMENT_CLOSED, m_tabs->GetPage(i));   // before teardown: path/id still resolvable
                m_tabs->DeletePage(i);
            }
    }
    void closeAllUnchanged()
    {
        auto* keep = activePage();
        for (int i = (int)m_tabs->GetPageCount() - 1; i >= 0; --i)
        {
            auto* p = static_cast<EditorPage*>(m_tabs->GetPage(i));
            if (p == keep || p->dirty) continue;
            nibFireDocEvent(NIB_EV_DOCUMENT_CLOSED, p);   // before teardown: path/id still resolvable
            m_tabs->DeletePage(i);
        }
    }

    // ---- read-only ----
    void toggleReadOnly() { const bool ro = sci(SCI_GETREADONLY) != 0; sci(SCI_SETREADONLY, ro ? 0 : 1); setStatus(0, ro ? _("Read/Write") : _("Read-Only")); nibFireDocEvent(NIB_EV_READONLY_CHANGED, activePage()); }   // -> NPPN_READONLYCHANGED (this buffer's id)
    void toggleSystemReadOnly()
    {
        const wxString p = curPath(); if (p.empty()) { notImpl(_("Read-Only Attribute (save the file first)")); return; }
#ifdef __WXMSW__
        const std::wstring w = p.ToStdWstring(); DWORD a = GetFileAttributesW(w.c_str());
        if (a == INVALID_FILE_ATTRIBUTES) return;
        a ^= FILE_ATTRIBUTE_READONLY; SetFileAttributesW(w.c_str(), a);
        const bool ro = (a & FILE_ATTRIBUTE_READONLY) != 0; sci(SCI_SETREADONLY, ro ? 1 : 0);
        setStatus(0, ro ? _("File attribute: Read-Only") : _("File attribute: Read/Write"));
        nibFireDocEvent(NIB_EV_READONLY_CHANGED, activePage());   // -> NPPN_READONLYCHANGED
#else
        notImpl(_("Read-Only Attribute (Windows only)"));
#endif
    }
    // Only one Scintilla view is shared across a tab strip's pages (see buildView) - peek every other
    // document via a raw doc-pointer swap (same technique sortTabs uses for TabSortKey::Size) and restore
    // whatever was attached before. Read-only lives on the Document itself, so it sticks after the swap back.
    void setReadOnlyAllDocs(bool ro)
    {
        if (!m_stc) return;
        const sptr_t original = sci(SCI_GETDOCPOINTER);
        const int savedAnchor = (int)sci(SCI_GETANCHOR), savedCaret = (int)sci(SCI_GETCURRENTPOS);   // caret/selection are per-VIEW, not per-document - SETDOCPOINTER below won't preserve them on its own
        for (EditorPage* p : allPages()) { sci(SCI_SETDOCPOINTER, 0, p->doc); sci(SCI_SETREADONLY, ro ? 1 : 0); }
        sci(SCI_SETDOCPOINTER, 0, original);
        sci(SCI_SETSEL, savedAnchor, savedCaret);
        setStatus(0, ro ? _("Read-Only for All Documents") : _("Read/Write for All Documents"));
    }
    // Edit > Begin/End Select: a sticky selection anchor for keyboard/mouse-driven selection without
    // holding Shift - re-extended from onStcUpdateUI on every caret move until toggled off again.
    void toggleBeginEndSelect(bool columnMode)
    {
        m_beginEndSelectActive = !m_beginEndSelectActive;
        if (m_beginEndSelectActive)
        {
            m_beginEndSelectColumnMode = columnMode;
            m_beginEndSelectAnchor = (int)sci(SCI_GETCURRENTPOS);
            setStatus(0, columnMode ? _("Begin/End Select (Column Mode): move the caret, then repeat to finish")
                                     : _("Begin/End Select: move the caret, then repeat to finish"));
        }
        else { sci(SCI_SETSELECTIONMODE, SC_SEL_STREAM); setStatus(0, _("Select finished")); }   // leave no lingering rectangle mode behind
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
    void searchOnInternet()
    {
        wxString q = selText().Trim().Trim(false); if (q.empty()) return; q.Replace(" ", "+");
        static const char* urls[] = { "https://duckduckgo.com/?q=", "https://www.google.com/search?q=", "https://www.bing.com/search?q=", "https://search.yahoo.com/search?p=", "https://search.brave.com/search?q=" };
        wxLaunchDefaultBrowser(urls[m_searchEngine >= 0 && m_searchEngine < 5 ? m_searchEngine : 0] + q);
    }
    void changeSearchEngine()
    {
        SearchEngineDialog dlg(this, m_searchEngine);
        themeDialog(&dlg);
        if (dlg.ShowModal() != wxID_OK) return;
        m_searchEngine = dlg.engine();
        saveSettings();
    }
    void findCharInRange()
    {
        FindCharRangeDialog dlg(this, m_dark);
        themeDialog(&dlg);
        if (dlg.ShowModal() != wxID_OK) return;
        int from = 0, to = 0;
        if (!dlg.range(from, to)) { wxMessageBox(_("Enter valid character codes."), _("Find Characters in Range"), wxOK | wxICON_WARNING, this); return; }
        const int len = (int)sci(SCI_GETLENGTH);
        if (len <= 0) return;
        const int start = (int)sci(SCI_GETCURRENTPOS);
        for (int steps = 1; steps <= len; ++steps)
        {
            const int pos = (start + steps) % len;
            const int ch = (int)sci(SCI_GETCHARAT, pos) & 0xFF;
            if (ch >= from && ch <= to) { sci(SCI_SETSEL, pos, pos + 1); sci(SCI_SCROLLCARET); setStatus(0, wxString::Format(_("Found character 0x%02X at position %d"), ch, pos)); return; }
        }
        setStatus(0, _("No character in that range found"));
    }
    void openSelectedFile() { const wxString f = selText().Trim().Trim(false); if (!f.empty() && wxFileExists(f)) openPath(f); else notImpl(_("Open File (selection is not an existing path)")); }

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
        setStatus(0, m_onTop ? _("Always on Top: ON") : _("Always on Top: OFF"));
#else
        notImpl(_("Always on Top (Windows only)"));
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
        themedInfo(wxString::Format(_("File: %s\n\nCharacters: %ld\nWords: %ld\nLines: %d"),
            name, cp, words, (int)sci(SCI_GETLINECOUNT)), _("Summary"));
    }
    void showWindowsList()
    {
        if (!m_tabs || !m_tabs->GetPageCount()) return;
        wxArrayString items; for (size_t i = 0; i < m_tabs->GetPageCount(); ++i) { auto* p = static_cast<EditorPage*>(m_tabs->GetPage(i)); items.Add(p->path.empty() ? p->title : p->path); }
        const int sel = wxGetSingleChoiceIndex(_("Activate document:"), _("Windows"), items, m_tabs->GetSelection(), this);
        if (sel >= 0) m_tabs->SetSelection(sel);
    }

    // ---- Tools: MD5 / SHA digests ----
#ifdef __WXMSW__
    // Hex digest of a raw byte buffer via CNG (BCrypt) - shared by hashSelection (selection/doc text) and
    // hashFiles (raw file bytes, so non-UTF8 files hash correctly instead of being re-encoded first).
    wxString hashBytes(const wchar_t* algo, const void* data, size_t size)
    {
        wxString hex; BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE h = nullptr; DWORD len = 0, res = 0;
        if (BCryptOpenAlgorithmProvider(&alg, algo, nullptr, 0) == 0)
        {
            BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&len), sizeof(len), &res, 0);
            if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0)
            {
                BCryptHashData(h, reinterpret_cast<PUCHAR>(const_cast<void*>(data)), (ULONG)size, 0);
                std::vector<unsigned char> dig(len); BCryptFinishHash(h, dig.data(), len, 0);
                for (unsigned char c : dig) hex += wxString::Format("%02x", c);
                BCryptDestroyHash(h);
            }
            BCryptCloseAlgorithmProvider(alg, 0);
        }
        return hex;
    }
#endif
    void hashSelection(const wchar_t* algo, const char* name, bool toClip)
    {
#ifdef __WXMSW__
        std::string data = getSelUtf8(); if (data.empty()) data = getDocUtf8();
        const wxString hex = hashBytes(algo, data.data(), data.size());
        if (toClip) { copyToClip(hex); setStatus(0, wxString::Format(_("%s copied to clipboard"), name)); }
        else themedInfo(hex, wxString::Format(_("%s digest"), name));
#else
        (void)algo; (void)toClip; notImpl(wxString::Format(_("%s (Windows only)"), name));
#endif
    }
    // Tools > <algo> > Generate from files...: hash each picked file's raw bytes (not its
    // wxSTC/UTF-8 text, so this matches externally-computed digests for any file, text or binary).
    void hashFiles(const wchar_t* algo, const char* name)
    {
#ifdef __WXMSW__
        wxFileDialog dlg(this, wxString::Format(_("%s - generate from files"), name), wxEmptyString, wxEmptyString,
                          _("All files (*.*)|*.*"), wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() != wxID_OK) return;
        wxArrayString paths; dlg.GetPaths(paths);
        wxString out;
        for (const wxString& p : paths)
        {
            wxFile f(p);
            if (!f.IsOpened()) { out += wxString::Format(_("%s:  (could not open)"), wxFileNameFromPath(p)) + "\n"; continue; }
            const wxFileOffset sz = f.Length();
            std::string buf(static_cast<size_t>(sz > 0 ? sz : 0), '\0');
            if (sz > 0) f.Read(&buf[0], (size_t)sz);
            out += wxFileNameFromPath(p) + ":  " + hashBytes(algo, buf.data(), buf.size()) + "\n";
        }
        themedInfo(out, wxString::Format(_("%s digest"), name));
#else
        (void)algo; notImpl(wxString::Format(_("%s (Windows only)"), name));
#endif
    }
    // Manually force a language on the active buffer (Language menu). ext "" forces Normal Text. The
    // choice sticks across tab switches via EditorPage::langForced (setLexerForFile honours it over the
    // file extension).
    void setForcedLang(const wxString& lexer, const wxString& name)
    {
        auto* p = activePage(); if (!p) return;
        p->langForced = true; p->forcedLexer = lexer; p->forcedName = name; p->sciLang.clear();
        setLexerForFile(p->path);
        updateStatus();
        if (m_stc) m_stc->Refresh();
        nibFireDocEvent(NIB_EV_LANGUAGE_CHANGED, p);   // -> NPPN_LANGCHANGED (Language-menu pick on the active buffer)
    }
    // Rebuild the Language menu's dynamic section: one entry per registered Scintillua language
    // (from nib.langdef, e.g. the udl-compat plugin). Called at startup and whenever a language is
    // registered, so the menu never needs a restart to catch up.
    void rebuildUserLangMenu()
    {
        auto* mb = menuBar(); if (!mb) return;
        wxMenu* lang = nullptr;
        mb->FindItem(kCmdLangText, &lang);   // kCmdLangText ("Normal text file") lives directly on the top-level Language menu
        if (!lang) return;
        for (size_t i = 0; i < m_sciLangs.size(); ++i)
            if (wxMenuItem* it = lang->FindItem(kSciLangMenuBase + (int)i)) lang->Destroy(it);
        const size_t insertPos = lang->GetMenuItemCount();
        for (size_t i = 0; i < m_sciLangs.size(); ++i)
            lang->Insert(insertPos + i, kSciLangMenuBase + (int)i, m_sciLangs[i].name);
    }

    // ----- Scintillua: wxNote's native language engine (embedded Lua+LPeg+Scintillua) --------------
    scintillua::Engine* scintilluaEngine()
    {
        if (!m_scintillua)
        {
            const wxString lexerDir = wxPathOnly(wxStandardPaths::Get().GetExecutablePath()) + wxFILE_SEP_PATH + "lexers";
            const wxString writable = userDataDir() + wxFILE_SEP_PATH + "scintillua-lexers";
            { wxLogNull noLog; if (!wxDirExists(writable)) wxFileName::Mkdir(writable, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL); }
            m_scintillua = std::make_unique<scintillua::Engine>(
                std::string(lexerDir.utf8_string()), std::string(writable.utf8_string()));
            if (!m_scintillua->ok())
                wxLogDebug("[scintillua] engine unavailable: %s", wxString::FromUTF8(m_scintillua->lastError()));
        }
        return m_scintillua.get();
    }
    // Register (or replace) a Scintillua language from lexer Lua source; 1 on success (drives nib.langdef).
    int registerScintilluaLanguage(const wxString& name, const wxString& exts, const wxString& lua)
    {
        auto* eng = scintilluaEngine();
        if (!eng || !eng->ok() || name.empty()) return 0;
        if (!eng->registerLexer(std::string(name.utf8_string()), std::string(lua.utf8_string())))
        { wxLogDebug("[scintillua] register '%s' failed: %s", name, wxString::FromUTF8(eng->lastError())); return 0; }
        bool replaced = false;
        for (auto& l : m_sciLangs) if (l.name == name) { l.exts = exts; replaced = true; break; }
        if (!replaced && (int)m_sciLangs.size() < 400) m_sciLangs.push_back({name, exts});
        rebuildUserLangMenu();
        return 1;
    }
    // Scintillua tag name -> container-lexer style number (kept < 32, disjoint from UDL's own 0..20 use is fine:
    // a buffer is never both UDL- and Scintillua-lexed). 0=default/whitespace, 1=keyword, 2=string, 3=comment,
    // 4=number, 5=operator, 6..12=keyword2..8, 13=preprocessor, 14=type/function, 15=constant.
    static int sciTagToStyle(const std::string& tag)
    {
        if (tag.rfind("keyword", 0) == 0)
        {
            if (tag.size() == 8 && tag[7] >= '2' && tag[7] <= '8') return 6 + (tag[7] - '2');
            return 1;
        }
        if (tag.rfind("string", 0) == 0 || tag == "character")   return 2;
        if (tag.rfind("comment", 0) == 0)                        return 3;
        if (tag.rfind("number", 0) == 0)                         return 4;   // number, number.float, ...
        if (tag.rfind("operator", 0) == 0)                       return 5;
        if (tag.rfind("preprocessor", 0) == 0)                   return 13;
        // Prefix-match so Scintillua's dot-namespaced sub-tags land on the right style, not default:
        // function.builtin / function.method / type.builtin / class.* / constant.builtin / variable.*
        if (tag.rfind("type", 0) == 0 || tag.rfind("function", 0) == 0 || tag.rfind("class", 0) == 0)
            return 14;
        if (tag.rfind("constant", 0) == 0 || tag.rfind("variable", 0) == 0 || tag == "label")
            return 15;
        if (tag == "regex")                                      return 2;   // regex literals -> string colour
        if (tag == "foldkw")                                     return 1;   // udl-compat word fold markers (if/end) -> keyword
        if (tag == "foldsym")                                    return 5;   // udl-compat symbol fold markers ({ }) -> operator
        return 0;   // whitespace.*, default, identifier, and anything unmapped
    }
    // Colour the container styles a Scintillua-lexed buffer uses. A small fixed palette (light + dark),
    // pending a proper themable "genericLangDef" block.
    void applyScintilluaStyles(wxStyledTextCtrl* stc)
    {
        if (!stc) return;
        stc->SetLexer(wxSTC_LEX_CONTAINER);
        struct Sty { int id; unsigned lightFg, darkFg; bool bold, italic; };
        static const Sty P[] = {
            { 1, 0x0000FF, 0x569CD6, true,  false },   // keyword
            { 2, 0x008000, 0xCE9178, false, false },   // string
            { 3, 0x808080, 0x6A9955, false, true  },   // comment
            { 4, 0x098658, 0xB5CEA8, false, false },   // number
            { 5, 0x000000, 0xD4D4D4, false, false },   // operator
            {13, 0xAF00DB, 0xC586C0, false, false },   // preprocessor
            {14, 0x267F99, 0x4EC9B0, false, false },   // type/function/class
            {15, 0x0070C1, 0x9CDCFE, false, false },   // constant/variable
        };
        auto rgb = [](unsigned c){ return wxColour((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF); };
        auto set = [&](int id, unsigned light, unsigned dark, bool bold, bool italic) {
            stc->StyleSetForeground(id, rgb(m_dark ? dark : light));
            stc->StyleSetBold(id, bold);
            stc->StyleSetItalic(id, italic);
        };
        for (const Sty& s : P) set(s.id, s.lightFg, s.darkFg, s.bold, s.italic);
        // keyword2..8 (styles 6..12): distinct hues so a UDL's separate keyword groups are visually
        // told apart (Notepad++ colours each group; until we carry the UDL's own per-slot colours we
        // pick a sensible fixed palette instead of painting them all the keyword1 blue).
        static const Sty KW[] = {
            { 6, 0x267F99, 0x4EC9B0, true, false },   // keyword2 - teal
            { 7, 0xAF00DB, 0xC586C0, true, false },   // keyword3 - purple
            { 8, 0xB55305, 0xCE9178, true, false },   // keyword4 - orange
            { 9, 0xC72D6E, 0xD16D9E, true, false },   // keyword5 - magenta
            {10, 0x098658, 0x6A9955, true, false },   // keyword6 - green
            {11, 0x0070C1, 0x9CDCFE, true, false },   // keyword7 - light blue
            {12, 0x8B6D00, 0xD7BA7D, true, false },   // keyword8 - gold
        };
        for (const Sty& s : KW) set(s.id, s.lightFg, s.darkFg, s.bold, s.italic);
    }
    // Lex the whole buffer with the named Scintillua lexer and apply the styles (called from STYLENEEDED).
    void scintilluaStyle(wxStyledTextCtrl* stc, const wxString& langName)
    {
        auto* eng = scintilluaEngine();
        if (!stc || !eng || !eng->ok()) return;
        const int len = static_cast<int>(stc->GetLength());
        const char* buf = stc->GetRangePointer(0, len);   // zero-copy view into Scintilla's buffer (valid until the doc changes; we only read it here)
        if (!buf) return;
        std::vector<int> folds;
        const auto toks = eng->lexAndFold(std::string(langName.utf8_string()), buf,
                                          static_cast<size_t>(len), &folds);
        int prev = 0;
        stc->StartStyling(0);
        for (const auto& t : toks)
        {
            int end = t.endPos - 1;   // Scintillua positions are 1-based and point one past the token
            if (end > len) end = len;
            if (end > prev) { stc->SetStyling(end - prev, sciTagToStyle(t.tag)); prev = end; }
        }
        if (prev < len) stc->SetStyling(len - prev, 0);   // any trailing bytes -> default
        // Fold levels from Scintillua's lexer:fold() (same single lex pass). Set EVERY line each pass so
        // the result is idempotent across re-lexes (tab switch, Preferences apply, theme toggle): re-
        // setting a line to the level it already has is a no-op in Scintilla, so the user's collapsed/
        // expanded fold state survives. Each fold value is already a Scintilla bitmask (base/header/white
        // flags). A grammar with no fold points returns an empty `folds`; flatten to base then, which
        // also clears any stale fold headers a previous Lexilla lexer left on this buffer.
        // Known limitation: Scintillua's fold() splits lines on \r?\n, so a lone-CR (classic-Mac) file is
        // seen as one line and does not fold (degrades to no folding; highlighting is unaffected).
        const int nLines = static_cast<int>(stc->GetLineCount());
        if (!folds.empty())
        {
            const int m = wxMin(static_cast<int>(folds.size()), nLines);
            for (int line = 0; line < m; ++line) stc->SetFoldLevel(line, folds[line]);
        }
        else
        {
            for (int line = 0; line < nLines; ++line) stc->SetFoldLevel(line, wxSTC_FOLDLEVELBASE);
        }
    }
    // Put the active buffer under a registered Scintillua language (Language-menu pick). Mirrors
    // route through setLexerForFile so tab switches re-apply it.
    void applyScintilluaToActiveBuffer(const wxString& name)
    {
        auto* p = activePage(); if (!p || name.empty()) return;
        p->langForced = true; p->sciLang = name;
        setLexerForFile(p->path);
        updateStatus();
        if (m_stc) m_stc->Refresh();
        nibFireDocEvent(NIB_EV_LANGUAGE_CHANGED, p);   // -> NPPN_LANGCHANGED (a registered Scintillua language pick)
    }

    // ----- search engine (drives the Find/Replace dialog) ----------------
    // Report a search outcome both in the main status bar and, if it is open, inside the Find dialog
    // (the result line at the bottom of the dialog).
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
            findResult(wxString::Format(_("Found: %s"), o.find));
            return true;
        }
        findResult(wxString::Format(_("Can't find: %s"), o.find));
        return false;
    }
    // Search range: the whole document, or just the current selection when "In selection" is set.
    void searchBounds(const FindOpts& o, int& start, int& end)
    {
        if (o.inSelection) { start = static_cast<int>(sci(SCI_GETSELECTIONSTART)); end = static_cast<int>(sci(SCI_GETSELECTIONEND)); }
        else               { start = 0; end = static_cast<int>(sci(SCI_GETLENGTH)); }
    }
    // The multi-select search term: the current selection, or the word under the caret (which it selects).
    // Returns the UTF-8 bytes, empty if there's nothing to match.
    std::string multiSelTerm()
    {
        const int mi = static_cast<int>(sci(SCI_GETMAINSELECTION));   // robust when there are already multiple selections
        int a = static_cast<int>(sci(SCI_GETSELECTIONNSTART, mi)), b = static_cast<int>(sci(SCI_GETSELECTIONNEND, mi));
        if (a > b) { const int t = a; a = b; b = t; }
        if (a == b)
        {
            const int pos = static_cast<int>(sci(SCI_GETSELECTIONNCARET, mi));
            a = static_cast<int>(sci(SCI_WORDSTARTPOSITION, pos, 1));
            b = static_cast<int>(sci(SCI_WORDENDPOSITION,   pos, 1));
            if (a == b) return {};                       // caret not on a word
            sci(SCI_SETSEL, a, b);
        }
        if (b - a <= 0 || b - a > 4096) return {};
        return m_stc->GetTextRange(a, b).utf8_string();   // UTF-8 bytes of the main selection
    }
    // "Multi-select All": box every occurrence of the term as its own selection so typing edits
    // them all at once (multi-cursor). flags picks MATCHCASE / WHOLEWORD per the menu variant.
    void multiSelectAll(int flags)
    {
        if (!m_stc) return;
        const std::string term = multiSelTerm();
        if (term.empty()) return;
        sci(SCI_SETMULTIPLESELECTION, 1);            // these commands need multi-selection even if it's off in Preferences
        const int caret = static_cast<int>(sci(SCI_GETCURRENTPOS)), len = static_cast<int>(sci(SCI_GETLENGTH));
        sci(SCI_SETSEARCHFLAGS, flags);
        std::vector<std::pair<int, int>> hits;
        for (int start = 0; start < len; )
        {
            sci(SCI_SETTARGETSTART, start); sci(SCI_SETTARGETEND, len);
            if (sci(SCI_SEARCHINTARGET, term.size(), reinterpret_cast<sptr_t>(term.c_str())) < 0) break;
            const int ms = static_cast<int>(sci(SCI_GETTARGETSTART)), me = static_cast<int>(sci(SCI_GETTARGETEND));
            if (me <= ms) { start = ms + 1; continue; }   // zero-length guard
            hits.emplace_back(ms, me);
            start = me;
        }
        if (hits.empty()) return;
        sci(SCI_CLEARSELECTIONS);
        int mainIdx = 0;
        for (size_t i = 0; i < hits.size(); ++i)
        {
            if (i == 0) sci(SCI_SETSELECTION, hits[i].second, hits[i].first);   // (caret, anchor)
            else        sci(SCI_ADDSELECTION, hits[i].second, hits[i].first);
            if (hits[i].first <= caret && caret <= hits[i].second) mainIdx = static_cast<int>(i);   // keep the caret's occurrence as the main selection
        }
        sci(SCI_SETMAINSELECTION, mainIdx);
        sci(SCI_SCROLLCARET);
    }
    // Does any current selection overlap [a,b)?
    bool rangeSelected(int a, int b)
    {
        const int n = static_cast<int>(sci(SCI_GETSELECTIONS));
        for (int i = 0; i < n; ++i)
        {
            int s = static_cast<int>(sci(SCI_GETSELECTIONNSTART, i)), e = static_cast<int>(sci(SCI_GETSELECTIONNEND, i));
            if (s > e) { const int t = s; s = e; e = t; }
            if (a < e && s < b) return true;
        }
        return false;
    }
    // "Multi-select Next" (VS Code's Ctrl+D): add the next occurrence of the current term as an
    // extra selection and make it the main one. Wraps around; skips occurrences already selected.
    void multiSelectNext(int flags)
    {
        if (!m_stc) return;
        const std::string term = multiSelTerm();
        if (term.empty()) return;
        sci(SCI_SETMULTIPLESELECTION, 1);
        sci(SCI_SETSEARCHFLAGS, flags);
        const int len = static_cast<int>(sci(SCI_GETLENGTH));
        std::vector<std::pair<int, int>> hits;
        for (int start = 0; start < len; )
        {
            sci(SCI_SETTARGETSTART, start); sci(SCI_SETTARGETEND, len);
            if (sci(SCI_SEARCHINTARGET, term.size(), reinterpret_cast<sptr_t>(term.c_str())) < 0) break;
            const int ms = static_cast<int>(sci(SCI_GETTARGETSTART)), me = static_cast<int>(sci(SCI_GETTARGETEND));
            if (me <= ms) { start = ms + 1; continue; }
            hits.emplace_back(ms, me);
            start = me;
        }
        if (hits.empty()) return;
        const int mi = static_cast<int>(sci(SCI_GETMAINSELECTION));
        const int from = static_cast<int>(sci(SCI_GETSELECTIONNEND, mi));   // look after the main selection first
        int pick = -1;
        for (size_t i = 0; i < hits.size(); ++i) if (hits[i].first >= from && !rangeSelected(hits[i].first, hits[i].second)) { pick = static_cast<int>(i); break; }
        if (pick < 0) for (size_t i = 0; i < hits.size(); ++i) if (!rangeSelected(hits[i].first, hits[i].second)) { pick = static_cast<int>(i); break; }   // wrap around
        if (pick < 0) return;   // every occurrence is already selected
        sci(SCI_ADDSELECTION, hits[pick].second, hits[pick].first);
        sci(SCI_SETMAINSELECTION, static_cast<int>(sci(SCI_GETSELECTIONS)) - 1);
        sci(SCI_SCROLLCARET);
    }
    // Drop the most recently added multi-selection (undo a Multi-select Next).
    void multiSelectUndo()
    {
        const int n = static_cast<int>(sci(SCI_GETSELECTIONS));
        if (n <= 1) return;
        sci(SCI_DROPSELECTIONN, static_cast<uptr_t>(n - 1));
        sci(SCI_SETMAINSELECTION, static_cast<int>(sci(SCI_GETSELECTIONS)) - 1);
        sci(SCI_SCROLLCARET);
    }
    // "Skip this occurrence" (as in VS Code): jump to the next occurrence without keeping the current
    // one (add next, then drop the old main).
    void multiSelectSkip(int flags)
    {
        if (!m_stc) return;
        const int oldMain = static_cast<int>(sci(SCI_GETMAINSELECTION));
        const int before  = static_cast<int>(sci(SCI_GETSELECTIONS));
        multiSelectNext(flags);
        if (static_cast<int>(sci(SCI_GETSELECTIONS)) > before)   // a next occurrence was added -> drop the skipped one
            sci(SCI_DROPSELECTIONN, static_cast<uptr_t>(oldMain));
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
    // Multi-style "Mark": highlight the current word/selection in one of 5 persistent colours.
    // all=true marks every occurrence, all=false just the current one; returns the number marked.
    int markExt(int style, bool all)
    {
        if (style < 0 || style > 4 || !m_stc) return 0;
        int a = static_cast<int>(sci(SCI_GETSELECTIONSTART)), b = static_cast<int>(sci(SCI_GETSELECTIONEND));
        bool wholeWord = false;
        if (a == b) { const int c = static_cast<int>(sci(SCI_GETCURRENTPOS)); a = static_cast<int>(sci(SCI_WORDSTARTPOSITION, c, 1)); b = static_cast<int>(sci(SCI_WORDENDPOSITION, c, 1)); wholeWord = true; }
        if (b <= a || b - a > 512) return 0;
        sci(SCI_SETTARGETSTART, a); sci(SCI_SETTARGETEND, b);
        std::string term(static_cast<size_t>(b - a) + 1, '\0'); sci(SCI_GETTARGETTEXT, 0, reinterpret_cast<sptr_t>(&term[0])); term.resize(b - a);
        const int indic = MARK_STYLE_BASE + style;
        sci(SCI_SETINDICATORCURRENT, indic);
        sci(SCI_INDICATORCLEARRANGE, 0, sci(SCI_GETLENGTH));   // each style re-marks fresh
        if (!all) { sci(SCI_INDICATORFILLRANGE, a, b - a); return 1; }
        sci(SCI_SETSEARCHFLAGS, SCFIND_MATCHCASE | (wholeWord ? SCFIND_WHOLEWORD : 0));
        const int len = static_cast<int>(sci(SCI_GETLENGTH));
        int start = 0, count = 0;
        while (start < len)
        {
            sci(SCI_SETTARGETSTART, start); sci(SCI_SETTARGETEND, len);
            if (sci(SCI_SEARCHINTARGET, term.size(), reinterpret_cast<sptr_t>(term.c_str())) < 0) break;
            const int ms = static_cast<int>(sci(SCI_GETTARGETSTART)), me = static_cast<int>(sci(SCI_GETTARGETEND));
            if (me <= ms) { start = ms + 1; continue; }
            sci(SCI_INDICATORFILLRANGE, ms, me - ms);
            start = me; ++count;
        }
        return count;
    }
    void unmarkExt(int style) { if (style >= 0 && style <= 4 && m_stc) { sci(SCI_SETINDICATORCURRENT, MARK_STYLE_BASE + style); sci(SCI_INDICATORCLEARRANGE, 0, sci(SCI_GETLENGTH)); } }
    void clearAllMarks() { clearMarks(); for (int i = 0; i < 5; ++i) unmarkExt(i); }   // Search > Clear all marks
    // Next / Previous marker: jump between the starts of all marked ranges (MARK_INDIC + the 5 styles).
    void jumpMark(bool fwd)
    {
        if (!m_stc) return;
        const int len = static_cast<int>(sci(SCI_GETLENGTH));
        std::vector<int> starts;
        auto collect = [&](int ind) {
            int p = 0;
            while (p < len) { const bool on = sci(SCI_INDICATORVALUEAT, ind, p) != 0; const int nxt = static_cast<int>(sci(SCI_INDICATOREND, ind, p)); if (nxt <= p) break; if (on) starts.push_back(p); p = nxt; }
        };
        collect(MARK_INDIC); for (int i = 0; i < 5; ++i) collect(MARK_STYLE_BASE + i);
        if (starts.empty()) { findResult(_("No marks to jump to")); return; }
        std::sort(starts.begin(), starts.end()); starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
        const int cur = static_cast<int>(sci(SCI_GETCURRENTPOS));
        int target = -1;
        if (fwd) { for (int s : starts) if (s > cur) { target = s; break; } if (target < 0) target = starts.front(); }
        else     { for (auto it = starts.rbegin(); it != starts.rend(); ++it) if (*it < cur) { target = *it; break; } if (target < 0) target = starts.back(); }
        sci(SCI_GOTOPOS, target); sci(SCI_SCROLLCARET);
    }
    // Search > Copy Styled Text: concatenate every range marked under the given indicator(s) - one per
    // line, in document order - onto the clipboard. Shares jumpMark's on/off run-walk over SCI_INDICATORVALUEAT.
    void copyMarkedToClipboard(const std::vector<int>& indicators)
    {
        if (!m_stc) return;
        const int len = static_cast<int>(sci(SCI_GETLENGTH));
        std::vector<std::pair<int, int>> ranges;
        for (int ind : indicators)
        {
            int p = 0;
            while (p < len)
            {
                const bool on = sci(SCI_INDICATORVALUEAT, ind, p) != 0;
                const int nxt = static_cast<int>(sci(SCI_INDICATOREND, ind, p));
                if (nxt <= p) break;
                if (on) ranges.push_back({ p, nxt });
                p = nxt;
            }
        }
        if (ranges.empty()) { setStatus(0, _("No marked text to copy")); m_hint = true; return; }
        std::sort(ranges.begin(), ranges.end());
        wxString out;
        for (const auto& r : ranges) { out += wxString::FromUTF8(rangeText(r.first, r.second)); out += "\n"; }
        copyToClip(out);
        setStatus(0, wxString::Format(_("Copied %d marked range(s)"), (int)ranges.size())); m_hint = true;
    }
    void onMarkDlg() { auto* d = ensureFindDlg(); d->showMarkTab(selText()); themeDialog(d); d->Show(); d->Raise(); }
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
    // ----- Find in Files: search / replace across files on disk, hits in a docked results panel --------
    void ensureFifScratch() { if (!m_fifScratch) { m_fifScratch = new wxStyledTextCtrl(this, wxID_ANY); m_fifScratch->Hide(); } }
    void ensureFindResultsPanel()
    {
        if (m_findResults) return;
        m_findResults = new wxStyledTextCtrl(this, wxID_ANY);
        m_findResults->SetReadOnly(true);
        for (int mg = 0; mg < 3; ++mg) m_findResults->SetMarginWidth(mg, 0);
        m_findResults->StyleSetBackground(wxSTC_STYLE_DEFAULT, m_dark ? wxColour(30, 30, 30) : *wxWHITE);
        m_findResults->StyleSetForeground(wxSTC_STYLE_DEFAULT, m_dark ? wxColour(220, 220, 220) : wxColour(30, 30, 30));
        m_findResults->StyleClearAll();
        m_findResults->Bind(wxEVT_STC_DOUBLECLICK, [this](wxStyledTextEvent&) {
            const int ln = static_cast<int>(m_findResults->GetCurrentLine());
            if (ln >= 0 && ln < static_cast<int>(m_fifJump.size()) && !m_fifJump[ln].first.empty()) gotoResult(m_fifJump[ln].first, m_fifJump[ln].second);
        });
        m_findResults->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e) {   // Enter on a result line jumps to it too
            if (e.GetKeyCode() == WXK_RETURN || e.GetKeyCode() == WXK_NUMPAD_ENTER) {
                const int ln = static_cast<int>(m_findResults->GetCurrentLine());
                if (ln >= 0 && ln < static_cast<int>(m_fifJump.size()) && !m_fifJump[ln].first.empty()) gotoResult(m_fifJump[ln].first, m_fifJump[ln].second);
            } else e.Skip();
        });
        m_aui.AddPane(m_findResults, wxAuiPaneInfo().Name("findresults").Caption(_("Search results"))
                      .Bottom().Layer(1).BestSize(wxSize(-1, 200)).CloseButton(true).MaximizeButton(false));
        m_aui.Update();
    }
    void doFindInFiles(const FindOpts& o, wxString dir, wxString filters, bool replace)
    {
        if (o.find.empty()) { findResult(_("Find in Files: nothing to search for")); return; }
        if (dir.empty()) { auto* p = activePage(); dir = (p && !p->path.empty()) ? wxPathOnly(p->path) : wxGetCwd(); }
        if (!wxDirExists(dir)) { findResult(wxString::Format(_("Find in Files: no such directory - %s"), dir)); return; }
        filters.Trim(true).Trim(false); if (filters.empty()) filters = "*.*";

        wxArrayString files;
        for (wxString pat : wxSplit(filters, ';')) { pat.Trim(true).Trim(false); if (!pat.empty()) wxDir::GetAllFiles(dir, &files, pat, wxDIR_FILES); }
        { std::set<wxString> seen; wxArrayString uniq; for (auto& f : files) if (seen.insert(f).second) uniq.Add(f); files = uniq; }

        ensureFifScratch();
        ensureFindResultsPanel();
        m_findResults->SetReadOnly(false); m_findResults->ClearAll(); m_fifJump.clear();
        auto out = [&](const wxString& txt, const wxString& file, int line) { m_findResults->AppendText(txt + "\n"); m_fifJump.push_back({ file, line }); };
        out(wxString::Format(_("Search \"%s\" in %s  (%s)"), o.find, dir, filters), "", -1);

        int hits = 0, hitFiles = 0, repl = 0, replFiles = 0;
        for (const wxString& file : files)
        {
            wxString content;
            { wxFile fh(file); if (!fh.IsOpened() || !fh.ReadAll(&content)) continue; }
            m_fifScratch->SetReadOnly(false); m_fifScratch->SetText(content);
            m_fifScratch->SetSearchFlags(static_cast<int>(searchFlags(o)));
            const int len = m_fifScratch->GetLength();
            if (replace)
            {
                int n = 0, end = len; m_fifScratch->SetTargetStart(0); m_fifScratch->SetTargetEnd(end);
                while (m_fifScratch->SearchInTarget(o.find) >= 0)
                {
                    const int s = m_fifScratch->GetTargetStart(), e = m_fifScratch->GetTargetEnd();
                    if (o.regex) m_fifScratch->ReplaceTargetRE(o.repl); else m_fifScratch->ReplaceTarget(o.repl);
                    int ne = m_fifScratch->GetTargetEnd(); end += (ne - e); if (e == s) ++ne;
                    m_fifScratch->SetTargetStart(ne); m_fifScratch->SetTargetEnd(end); ++n;
                }
                if (n > 0) { wxFile w(file, wxFile::write); if (w.IsOpened()) w.Write(m_fifScratch->GetText()); ++replFiles; repl += n; out(wxString::Format(_("  %s  (%d replaced)"), file, n), file, 0); }
            }
            else
            {
                int pos = 0, n = 0;
                while (pos <= len)
                {
                    m_fifScratch->SetTargetStart(pos); m_fifScratch->SetTargetEnd(len);
                    if (m_fifScratch->SearchInTarget(o.find) < 0) break;
                    const int s = m_fifScratch->GetTargetStart(), e = m_fifScratch->GetTargetEnd();
                    const int line = m_fifScratch->LineFromPosition(s);
                    if (n == 0) out(wxString::Format("  %s", file), file, 0);
                    wxString lt = m_fifScratch->GetTextRange(m_fifScratch->PositionFromLine(line), m_fifScratch->GetLineEndPosition(line));
                    out(wxString::Format(_("    Line %d:  %s"), line + 1, lt.Trim(true).Trim(false)), file, line);
                    ++n; ++hits; pos = (e > s) ? e : e + 1;
                }
                if (n > 0) ++hitFiles;
            }
        }
        const wxString summary = replace
            ? wxString::Format(_("Replace in Files: %d occurrence(s) in %d file(s)"), repl, replFiles)
            : wxString::Format(_("Find in Files: %d hit(s) in %d / %d file(s)"), hits, hitFiles, static_cast<int>(files.size()));
        out("  === " + summary + " ===", "", -1);
        m_findResults->SetReadOnly(true);
        wxAuiPaneInfo& pi = m_aui.GetPane(m_findResults); pi.Show(); m_aui.Update();
        findResult(summary);
    }
    // Jump to a search-result location without stacking duplicate tabs: activate the file if it is
    // already open, otherwise open it, then centre the 0-based line (like onFifActivate).
    void gotoResult(const wxString& path, int line0)
    {
        if (path.empty()) return;
        const wxFileName target(path);   // normalise: paths from the launch arg vs a directory walk can differ in case/slash
        bool open = false;
        for (EditorPage* p : allPages()) if (!p->path.empty() && wxFileName(p->path).SameAs(target)) { activatePage(p); open = true; break; }
        if (!open) openPath(path);
        sci(SCI_GOTOLINE, line0);
        const int half = static_cast<int>(sci(SCI_LINESONSCREEN)) / 2;
        sci(SCI_SETFIRSTVISIBLELINE, line0 > half ? line0 - half : 0);
        if (m_stc) m_stc->SetFocus();
    }
    // F4 / Shift+F4 step the visible search-results panel (Next/Previous Search Result).
    // Two panels can hold results - the Ctrl+Shift+F tree (m_fifPanel, 1-based) and the Find-dialog STC
    // (m_findResults, 0-based) - so try each in turn; fall back to in-document Find Next when neither has any.
    bool stepTreeResult(bool fwd)
    {
        if (!m_fifPanel) return false;
        wxAuiPaneInfo& pi = m_aui.GetPane(m_fifPanel);
        if (!pi.IsOk() || !pi.IsShown()) return false;
        std::vector<wxTreeItemId> leaves;   // only the "Line N:" hits carry FifItemData
        for (wxTreeItemId it = m_fifPanel->GetFirstVisibleItem(); it.IsOk(); it = m_fifPanel->GetNextVisible(it))
            if (dynamic_cast<FifItemData*>(m_fifPanel->GetItemData(it))) leaves.push_back(it);
        if (leaves.empty()) return false;
        const wxTreeItemId sel = m_fifPanel->GetSelection();
        int idx = -1; for (int i = 0; i < static_cast<int>(leaves.size()); ++i) if (leaves[i] == sel) { idx = i; break; }
        idx = (idx < 0) ? (fwd ? 0 : static_cast<int>(leaves.size()) - 1) : idx + (fwd ? 1 : -1);
        if (idx < 0) idx = static_cast<int>(leaves.size()) - 1; else if (idx >= static_cast<int>(leaves.size())) idx = 0;   // wrap
        m_fifPanel->SelectItem(leaves[idx]); m_fifPanel->EnsureVisible(leaves[idx]);
        if (auto* d = dynamic_cast<FifItemData*>(m_fifPanel->GetItemData(leaves[idx]))) gotoResult(d->file, d->line - 1);
        return true;
    }
    bool stepStcResult(bool fwd)
    {
        if (!m_findResults || m_fifJump.empty()) return false;
        wxAuiPaneInfo& pi = m_aui.GetPane(m_findResults);
        if (!pi.IsOk() || !pi.IsShown()) return false;
        const int n = static_cast<int>(m_fifJump.size());
        int cur = static_cast<int>(m_findResults->GetCurrentLine()); if (cur < 0) cur = 0; else if (cur >= n) cur = n - 1;
        for (int i = 0; i < n; ++i)   // walk at most once around the list looking for a jumpable line
        {
            cur += fwd ? 1 : -1; if (cur < 0) cur = n - 1; else if (cur >= n) cur = 0;   // wrap
            if (!m_fifJump[cur].first.empty())
            {
                m_findResults->GotoLine(cur);
                m_findResults->SetSelection(m_findResults->PositionFromLine(cur), m_findResults->GetLineEndPosition(cur));
                gotoResult(m_fifJump[cur].first, m_fifJump[cur].second);
                return true;
            }
        }
        return false;
    }
    void stepFoundResult(bool fwd)
    {
        if (stepTreeResult(fwd)) return;
        if (stepStcResult(fwd)) return;
        findNext(fwd);   // nothing in either results panel -> behave like Find Next
    }
    // F7: toggle focus between the editor and whichever search-results panel is showing.
    void focusFoundResults()
    {
        wxWindow* panel = nullptr;
        if (m_fifPanel)    { wxAuiPaneInfo& pi = m_aui.GetPane(m_fifPanel);    if (pi.IsOk() && pi.IsShown()) panel = m_fifPanel; }
        if (!panel && m_findResults) { wxAuiPaneInfo& pi = m_aui.GetPane(m_findResults); if (pi.IsOk() && pi.IsShown()) panel = m_findResults; }
        if (!panel) { findResult(_("No search results yet - run Find in Files or Find All first")); return; }
        if (panel->HasFocus()) { if (m_stc) m_stc->SetFocus(); } else panel->SetFocus();
    }
    FindReplaceDialog* ensureFindDlg()
    {
        if (!m_findDlg)
        {
            m_findDlg = new FindReplaceDialog(this);
            m_findDlg->findNextCb   = [this](const FindOpts& o) { m_lastFind = o.find; m_lastReplace = o.repl; doFindNext(o); };
            m_findDlg->countCb      = [this](const FindOpts& o) { findResult(wxString::Format(_("Count: %d match(es)"), doCount(o))); };
            m_findDlg->replaceCb    = [this](const FindOpts& o) { m_lastFind = o.find; m_lastReplace = o.repl; doReplaceOne(o); };
            m_findDlg->replaceAllCb = [this](const FindOpts& o) { findResult(wxString::Format(_("Replaced %d occurrence(s)"), doReplaceAll(o))); };
            m_findDlg->markAllCb    = [this](const FindOpts& o) { findResult(wxString::Format(_("Marked %d match(es)"), doMarkAll(o))); };
            m_findDlg->clearMarksCb = [this]() { clearMarks(); };
            m_findDlg->fifCb        = [this](const FindOpts& o, const wxString& dir, const wxString& filters, bool replace) { m_lastFind = o.find; m_lastReplace = o.repl; doFindInFiles(o, dir, filters, replace); };
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
    void setToggleUi(int id, bool on) { if (menuBar()) menuBar()->Check(id, on); if (toolBar()) toolBar()->ToggleTool(id, on); }   // keep menu + toolbar checks in step
    void syncToggle(int id, bool& flag) { flag = !flag; setToggleUi(id, flag); }
    void toggleWrap()  { syncToggle(kCmdViewWrap, m_wrap); sci(SCI_SETWRAPMODE, m_wrap ? SC_WRAP_WORD : SC_WRAP_NONE); }
    void toggleWs()    { syncToggle(kCmdViewAllCharacters, m_ws); if (menuBar()) menuBar()->Check(kCmdViewNpc, m_ws); sci(SCI_SETVIEWWS, m_ws ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE); sci(SCI_SETVIEWEOL, m_ws ? 1 : 0); }
    void toggleGuides(){ syncToggle(kCmdViewIndentGuide, m_guides); sci(SCI_SETINDENTATIONGUIDES, m_guides ? SC_IV_LOOKBOTH : SC_IV_NONE); }

    // ----- persisted preferences (Settings > Preferences) ---------------
    static size_t recentMaxFromConfig()   // Recent Files max, read straight from config so the m_fileHistory member init can use it
    {
        long m = 10; wxConfigBase::Get()->Read("RecentFiles/Max", &m, 10L);
        return (size_t)(m < 1 ? 1 : (m > 50 ? 50 : m));
    }
    void loadSettings()
    {
        auto* c = wxConfigBase::Get();
        c->Read("IntegratedBar", &m_integratedBar, false);
        m_themeMode = (int)readThemeMode();   // also resolved in OnInit (before the frame/config exist as members here)
        c->Read("AskBeforeClose", &m_askBeforeClose, false);
        c->Read("FullscreenAutohideToolbar", &m_fsAutohideToolbar, false);
        c->Read("ReuseInstance", &m_reuseInstance, false);
        c->Read("Editing/CustomGutterColour", &m_customGutterColor, false);
        // Default swatch (only shown/used before the user has ever picked a colour themselves) matches
        // the current theme's tone instead of a fixed light gray - a bright swatch read as a jarring
        // full inversion against a dark theme rather than a subtle "this margin stands out" accent.
        const long gcDefault = m_dark ? 0x3E3A3AL : 0xE8E8E8L;
        long gc = gcDefault; c->Read("Editing/GutterColourValue", &gc, gcDefault); m_gutterColorValue = gc;
        long is = 0; c->Read("ToolbarIconStyle", &is, 0L);
        m_iconStyle = (is < 0 || is > 3) ? 2 : (int)is;   // 0 = line icons, 1 = Solar, 2 = IconPark, 3 = Streamline; clamp anything out of range into IconPark so the Preferences combo's SetSelection stays valid (a stale "3 = IconPark Bold" (removed) value now lands on Streamline - still a colored pack)
        long tis = 16; c->Read("ToolbarIconSize", &tis, 16L);
        m_toolbarIconSize = (tis < 12 || tis > 64) ? 16 : (int)tis;   // px; clamp garbage/out-of-range back to the 16 default
        long mr = 10; c->Read("RecentFiles/Max", &mr, 10L); m_maxRecent = (int)mr;
        c->Read("TabBar/CloseButton", &m_tabCloseBtn, true);   // integrated top bar on/off (also read in OnInit; here for the Preferences checkbox)
        long tw = 4; c->Read("Editing/TabWidth", &tw, 4L); m_tabWidth = (int)tw;
        c->Read("Editing/UseTabs", &m_useTabs, true);
        c->Read("Editing/LineNumbers", &m_lineNumbers, true);
        c->Read("Editing/Wrap", &m_wrap, false);
        c->Read("Editing/Whitespace", &m_ws, false);
        c->Read("Editing/IndentGuides", &m_guides, true);
        c->Read("Editing/WrapSymbol", &m_wrapSymbol, false);
        c->Read("View/Toolbar", &m_showToolbar, true);
        c->Read("View/StatusBar", &m_showStatusbar, true);
        c->Read("View/ZoomField", &m_showZoomField, false);
        c->Read("Editing/AutoComplete", &m_autocomplete, true);
        c->Read("Editing/CaretLine", &m_caretLine, true);
        c->Read("Editing/AutoIndent", &m_autoindent, true);
        long cw = 1; c->Read("Editing/CaretWidth", &cw, 1L); m_caretWidth = (int)cw;
        long ec = 0; c->Read("Editing/EdgeColumn", &ec, 0L); m_edgeColumn = (int)ec;
        long de = SC_EOL_CRLF; c->Read("NewDoc/Eol", &de, (long)SC_EOL_CRLF); m_defaultEol = (int)de;
        long dl = -1; c->Read("NewDoc/Lang", &dl, -1L); m_defaultLangId = (int)dl;
        long den = ENC_UTF8; c->Read("NewDoc/Encoding", &den, (long)ENC_UTF8); m_defaultEncoding = (int)den;
        long cbk = 500; c->Read("Editing/CaretBlink", &cbk, 500L); m_caretBlink = (int)cbk;
        c->Read("Editing/ScrollBeyond", &m_scrollBeyond, false);
        c->Read("Editing/MultiEdit", &m_multiEdit, true);
        long acf = 3; c->Read("AutoComplete/FromChar", &acf, 3L); m_autoCompFrom = (int)acf;
        c->Read("AutoComplete/InsertPairs", &m_autoInsertPairs, false);
        c->Read("Theme", &m_themeName, wxEmptyString);
        c->Read("Print/Header", &m_printHeader, wxEmptyString);
        c->Read("Print/Footer", &m_printFooter, wxEmptyString);
        long se = 0; c->Read("Editing/SearchEngine", &se, 0L); m_searchEngine = (int)se;
        c->Read("Editing/FontFace", &m_fontFace, "Cascadia Mono");
    }
    void saveSettings()
    {
        auto* c = wxConfigBase::Get();
        c->Write("Editing/TabWidth", (long)m_tabWidth);   c->Write("Editing/UseTabs", m_useTabs);
        c->Write("Editing/LineNumbers", m_lineNumbers);   c->Write("Editing/Wrap", m_wrap);
        c->Write("Editing/Whitespace", m_ws);             c->Write("Editing/IndentGuides", m_guides);
        c->Write("Editing/WrapSymbol", m_wrapSymbol);     c->Write("View/Toolbar", m_showToolbar);
        c->Write("View/StatusBar", m_showStatusbar);      c->Write("Editing/AutoComplete", m_autocomplete);
        c->Write("View/ZoomField", m_showZoomField);
        c->Write("Editing/CaretLine", m_caretLine);       c->Write("Editing/AutoIndent", m_autoindent);
        c->Write("Editing/CaretWidth", (long)m_caretWidth); c->Write("Editing/EdgeColumn", (long)m_edgeColumn);
        c->Write("NewDoc/Eol", (long)m_defaultEol);         c->Write("NewDoc/Lang", (long)m_defaultLangId);
        c->Write("NewDoc/Encoding", (long)m_defaultEncoding);
        c->Write("RecentFiles/Max", (long)m_maxRecent);     c->Write("TabBar/CloseButton", m_tabCloseBtn);
        c->Write("Editing/CaretBlink", (long)m_caretBlink); c->Write("Editing/ScrollBeyond", m_scrollBeyond);
        c->Write("Editing/MultiEdit", m_multiEdit);
        c->Write("AutoComplete/FromChar", (long)m_autoCompFrom); c->Write("AutoComplete/InsertPairs", m_autoInsertPairs);
        c->Write("Theme", m_themeName);
        c->Write("Print/Header", m_printHeader); c->Write("Print/Footer", m_printFooter);
        c->Write("Editing/SearchEngine", (long)m_searchEngine);
        if (!g_waitMode)   // -w/--wait force-enables the prompt for that run only (enterWaitMode); saveSettings()
                           // runs on every close, so writing it here would silently turn the user's preference on
            c->Write("AskBeforeClose", m_askBeforeClose);
        c->Write("FullscreenAutohideToolbar", m_fsAutohideToolbar);
        c->Write("Editing/CustomGutterColour", m_customGutterColor);
        c->Write("Editing/GutterColourValue", m_gutterColorValue);
        c->Write("Editing/FontFace", m_fontFace);
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
            sci(SCI_SETCARETPERIOD, m_caretBlink);
            sci(SCI_SETENDATLASTLINE, m_scrollBeyond ? 0 : 1);
            sci(SCI_SETMULTIPLESELECTION, m_multiEdit ? 1 : 0);
            if (m_lineNumbers) updateLineMargin(); else sci(SCI_SETMARGINWIDTHN, 0, 0);
        }
        if (auto* mb = menuBar()) { mb->Check(kCmdViewWrap, m_wrap); mb->Check(kCmdViewAllCharacters, m_ws); mb->Check(kCmdViewNpc, m_ws); mb->Check(kCmdViewIndentGuide, m_guides); }
        if (auto* tb = toolBar()) tb->ToggleTool(kCmdViewWrap, m_wrap);
        showToolBar(m_showToolbar);   // aui-aware: hides the pane in integrated mode, the frame toolbar in native
        if (auto* sb = GetStatusBar()) sb->Show(m_showStatusbar);
        applyZoomFieldVisibility();   // live: collapses/restores field 6 without a restart
        SendSizeEvent();
    }
    // Shortcut Mapper (kCmdSettingShortcutMapper == 48009): a searchable grid over every menu command's
    // effective binding, with rebind/clear/reset, a scheme picker, and the live conflict engine. A pure
    // view over m_keymap: edits write shortcuts.json immediately and re-apply through refreshAccelerators()
    // (via the apply callback) so a rebind shows on the menu and fires without a restart. The dialog owns no
    // persistent state, so a fresh instance per invocation is fine (mirrors onPreferences()).
    void showShortcutMapper()
    {
        wxMenuBar* mb = menuBar();
        if (!mb || !m_keymapReady) { notImpl(_("Shortcut Mapper")); return; }
        ShortcutMapperDialog dlg(this, m_keymap, mb, m_dark,
                                 [this](wxWindow* w){ themeDialog(w); },
                                 [this]{ refreshAccelerators(m_accelScope); reapplyEditorKeymaps();
                                         nibFireDocEvent(NIB_EV_SHORTCUTS_CHANGED, nullptr); });   // -> NPPN_SHORTCUTREMAPPED
        dlg.ShowModal();
    }

    void onPreferences()
    {
        // Page layout: General / Editing / Indentation / Auto-Completion / Dark Mode
        // (the labels' wording is frozen - the .po catalogs key on these exact strings).
        wxDialog dlg(this, wxID_ANY, _("Preferences"), wxDefaultPosition, wxSize(620, 440));
        // wxListbook's default style (wxLB_DEFAULT == wxBK_DEFAULT == 0) is resolved by wx to a *horizontal
        // top strip* on macOS but a *left column* on Win/Linux (wx/src/generic/listbkg.cpp Create():
        // #ifdef __WXMAC__ style |= wxBK_TOP; #else style |= wxBK_LEFT). That top strip squashed the page
        // area and shoved the Close button on macOS. Pass wxLB_LEFT (== wxBK_LEFT) so the nav list is a left
        // column on every platform. On Win/Linux the default already resolved to wxBK_LEFT, so the final
        // style is byte-for-byte identical there (no change); only macOS flips top -> left, which is the fix.
        auto* book = new wxListbook(&dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLB_LEFT);
        auto pg  = [&](const wxString& name, bool sel = false) { auto* p = new wxPanel(book); book->AddPage(p, name, sel); return p; };
        auto row = [&](wxBoxSizer* s, wxWindow* w) { s->Add(w, 0, wxLEFT | wxRIGHT | wxTOP, 10); };

        // ---- General --------------------------------------------------------------------------
        auto* gen = pg(_("General"), true); auto* gs = new wxBoxSizer(wxVERTICAL);
        auto* cbToolbar = new wxCheckBox(gen, wxID_ANY, _("Show toolbar"));    cbToolbar->SetValue(m_showToolbar);
        auto* cbStatus  = new wxCheckBox(gen, wxID_ANY, _("Show status bar")); cbStatus->SetValue(m_showStatusbar);
        // Off by default: the editable zoom combo in the status bar. Zooming itself (Ctrl+wheel,
        // View > Zoom In/Out) is unaffected by this - it only hides the readout/entry control.
        auto* cbZoomField = new wxCheckBox(gen, wxID_ANY, _("Show zoom control in status bar"));
        cbZoomField->SetValue(m_showZoomField);
        row(gs, cbToolbar); row(gs, cbStatus); row(gs, cbZoomField);
        // Off by default: closing a modified document just discards it silently
        // rather than blocking on a Save/Don't Save/Cancel prompt every time.
        auto* cbAskClose = new wxCheckBox(gen, wxID_ANY, _("Ask before closing unsaved changes"));
        cbAskClose->SetValue(m_askBeforeClose); row(gs, cbAskClose);
        // Off by default: the toolbar stays visible in full screen. On: full screen hides it (macOS-style).
        auto* cbFsToolbar = new wxCheckBox(gen, wxID_ANY, _("Auto-hide toolbar in full screen"));
        cbFsToolbar->SetValue(m_fsAutohideToolbar); row(gs, cbFsToolbar);
        // Off by default: each launch opens its own window, like today. When on, a second launch hands
        // its file args to the first window over IPC and exits instead of opening a new one (-n/-r on
        // the command line override this per-launch either way).
        auto* cbReuseInstance = new wxCheckBox(gen, wxID_ANY, _("Reuse an existing window"));
        cbReuseInstance->SetValue(m_reuseInstance); row(gs, cbReuseInstance);
#if defined(WXN_HAS_BORDERLESS) || defined(__WXMAC__)
        auto* cbIntBar = new wxCheckBox(gen, wxID_ANY, _("Show integrated top bar"));
        cbIntBar->SetValue(m_integratedBar); row(gs, cbIntBar);
#endif
        // Localization: pick the UI language (restart-to-apply, like dark mode). Same shared table
        // (UI_LANG_IDS/UI_LANG_ENDONYMS) as the top-level Localization menu.
        const long curUi = readUiLang();
        wxArrayString uiNames;
        for (int i = 0; i < (int)WXSIZEOF(UI_LANG_ENDONYMS); ++i) uiNames.Add(uiLangName(i));
        auto* chUiLang = new wxChoice(gen, wxID_ANY, wxDefaultPosition, wxDefaultSize, uiNames);
        chUiLang->SetSelection(uiLangIndex(curUi));
        // Toolbar icon style (restart-to-apply, like Localization above): the default line-icon set
        // (theme-adaptive) vs. three fixed-colour sets, Solar, IconPark and Streamline (see
        // iconColored()). (A separate "IconPark Bold" stroke-width variant existed briefly - dropped
        // after feedback that dark mode made it indistinguishable from IconPark; its old saved index 3
        // now belongs to Streamline.)
        wxArrayString iconStyleNames;
        iconStyleNames.Add(_("Tabler icons (line)")); iconStyleNames.Add(_("Solar icons (green)"));
        iconStyleNames.Add(_("IconPark icons (teal/lime)")); iconStyleNames.Add(_("Streamline icons (green/teal)"));
        auto* chIconStyle = new wxChoice(gen, wxID_ANY, wxDefaultPosition, wxDefaultSize, iconStyleNames);
        chIconStyle->SetSelection(m_iconStyle);
        // Toolbar icon size (restart-to-apply, like the icon style above): the icons are SVG, so they stay
        // crisp at any size. Presets keep it simple; the "20 px" labels are digits + a universal unit, so
        // they need no translation (only the "Toolbar icon size:" field label does).
        static const int kIconSizes[] = { 16, 20, 24, 32 };
        wxArrayString iconSizeNames;
        for (int s : kIconSizes) iconSizeNames.Add(wxString::Format("%d px", s));
        auto* chIconSize = new wxChoice(gen, wxID_ANY, wxDefaultPosition, wxDefaultSize, iconSizeNames);
        { int sel = 0; for (int i = 0; i < (int)WXSIZEOF(kIconSizes); ++i) if (kIconSizes[i] == m_toolbarIconSize) sel = i; chIconSize->SetSelection(sel); }
        // Theme (restart-to-apply, like the two above): follow the OS's own light/dark setting, or pin
        // one explicitly. Was a standalone "Dark Mode" checkbox page; folded in here since it's the same
        // kind of restart-required, general-appearance choice as Localization/Toolbar icon style.
        wxArrayString themeNames;
        themeNames.Add(_("System")); themeNames.Add(_("Dark")); themeNames.Add(_("Light"));
        auto* chTheme = new wxChoice(gen, wxID_ANY, wxDefaultPosition, wxDefaultSize, themeNames);
        chTheme->SetSelection(m_themeMode);
        // A 2-column grid (not independent row sizers) so all combo boxes line up under each other
        // regardless of their label's length - "Localization:" and "Toolbar icon style:" differ enough in
        // width that separate row sizers left the combos visibly staggered. wxEXPAND on the control
        // column additionally stretches each wxChoice to the column's full (uniform) width - without it,
        // a FlexGridSizer still gives both cells the same width, but a narrower control (e.g. "Polski")
        // just sits left-aligned inside its cell rather than filling it, so the two still looked
        // different widths even once their left edges lined up.
        auto* locGrid = new wxFlexGridSizer(4, 2, 8, 8);
        locGrid->Add(new wxStaticText(gen, wxID_ANY, _("Localization:")), 0, wxALIGN_CENTRE_VERTICAL);
        locGrid->Add(chUiLang, 0, wxEXPAND | wxALIGN_CENTRE_VERTICAL);
        locGrid->Add(new wxStaticText(gen, wxID_ANY, _("Toolbar icon style:")), 0, wxALIGN_CENTRE_VERTICAL);
        locGrid->Add(chIconStyle, 0, wxEXPAND | wxALIGN_CENTRE_VERTICAL);
        locGrid->Add(new wxStaticText(gen, wxID_ANY, _("Toolbar icon size:")), 0, wxALIGN_CENTRE_VERTICAL);
        locGrid->Add(chIconSize, 0, wxEXPAND | wxALIGN_CENTRE_VERTICAL);
        locGrid->Add(new wxStaticText(gen, wxID_ANY, _("Theme:")), 0, wxALIGN_CENTRE_VERTICAL);
        locGrid->Add(chTheme, 0, wxEXPAND | wxALIGN_CENTRE_VERTICAL);
        gs->Add(locGrid, 0, wxLEFT | wxRIGHT | wxTOP, 10);
        gen->SetSizer(gs);

        // ---- Editing --------------------------------------------------------------------------
        auto* ed = pg(_("Editing")); auto* es = new wxBoxSizer(wxVERTICAL);
        // Font: the bundled font first (JetBrains Mono, our default), then every system font below the
        // divider. Falls back to JetBrains Mono at render time (effectiveFontFace()) if the chosen one is
        // later uninstalled. The bundled set is listed explicitly rather than relying on the enumeration
        // below, so it stays pinned at the top whatever the system font list contains.
        auto* frow = new wxBoxSizer(wxHORIZONTAL);
        frow->Add(new wxStaticText(ed, wxID_ANY, _("Font:")), 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 8);
        // wxChoice has no native separator item, so a plain-text divider line stands in for one - harmless
        // even if somehow selected: it's not a real face name, so effectiveFontFace() falls back to
        // JetBrains Mono exactly as it would for any other uninstalled/invalid font.
        const wxString kFontSep = wxString(wxUniChar(0x2500), 20);
        const wxArrayString kBundledFonts = [] {
            wxArrayString a; a.Add("Cascadia Mono"); a.Add("JetBrains Mono"); return a; }();
        wxArrayString fontNames = kBundledFonts; fontNames.Add(kFontSep);
        { wxArrayString sysFonts = wxFontEnumerator::GetFacenames(); sysFonts.Sort();
          for (const wxString& f : sysFonts) if (kBundledFonts.Index(f) == wxNOT_FOUND) fontNames.Add(f); }
        auto* chFont = new wxChoice(ed, wxID_ANY, wxDefaultPosition, wxDefaultSize, fontNames);
        { int sel = fontNames.Index(m_fontFace); chFont->SetSelection(sel != wxNOT_FOUND ? sel : 0); }
        frow->Add(chFont, 1, wxALIGN_CENTRE_VERTICAL);
        es->Add(frow, 0, wxEXPAND | wxALL, 10);
        auto* cbLineNum = new wxCheckBox(ed, wxID_ANY, _("Display line number"));      cbLineNum->SetValue(m_lineNumbers);
        auto* cbGuides  = new wxCheckBox(ed, wxID_ANY, _("Show indentation guide"));   cbGuides->SetValue(m_guides);
        auto* cbWs      = new wxCheckBox(ed, wxID_ANY, _("Show white space and TAB")); cbWs->SetValue(m_ws);
        auto* cbWrapSym = new wxCheckBox(ed, wxID_ANY, _("Show wrap symbol"));         cbWrapSym->SetValue(m_wrapSymbol);
        auto* cbWrap    = new wxCheckBox(ed, wxID_ANY, _("Word wrap"));                cbWrap->SetValue(m_wrap);
        auto* cbCaretLn = new wxCheckBox(ed, wxID_ANY, _("Highlight current line"));   cbCaretLn->SetValue(m_caretLine);
        auto* cbScroll  = new wxCheckBox(ed, wxID_ANY, _("Enable scrolling beyond last line"));      cbScroll->SetValue(m_scrollBeyond);
        auto* cbMulti   = new wxCheckBox(ed, wxID_ANY, _("Enable multi-editing (multi-selection)")); cbMulti->SetValue(m_multiEdit);
        for (auto* c : { cbLineNum, cbGuides, cbWs, cbWrapSym, cbWrap, cbCaretLn, cbScroll, cbMulti }) row(es, c);
        auto* erow = new wxBoxSizer(wxHORIZONTAL);
        erow->Add(new wxStaticText(ed, wxID_ANY, _("Caret width:")), 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 8);
        auto* spCaret = new SpinField(ed, 1, 3, m_caretWidth, m_dark, 60);
        erow->Add(spCaret, 0, wxRIGHT, 24);
        erow->Add(new wxStaticText(ed, wxID_ANY, _("Vertical edge at column (0 = off):")), 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 8);
        auto* spEdge = new SpinField(ed, 0, 300, m_edgeColumn, m_dark, 70);
        erow->Add(spEdge, 0);
        es->Add(erow, 0, wxALL, 10);
        auto* brow = new wxBoxSizer(wxHORIZONTAL);
        brow->Add(new wxStaticText(ed, wxID_ANY, _("Caret blink rate (ms, 0 = steady):")), 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 8);
        auto* spBlink = new SpinField(ed, 0, 2000, m_caretBlink, m_dark, 80);
        brow->Add(spBlink, 0);
        es->Add(brow, 0, wxLEFT | wxRIGHT | wxTOP, 10);
        // Optional distinct colour for the line-number/bookmark/fold "gutter" strip on the left edge
        // of the editor - off by default (follows the active theme, as before).
        auto* grow = new wxBoxSizer(wxHORIZONTAL);
        auto* cbGutter = new wxCheckBox(ed, wxID_ANY, _("Use a custom line-number margin colour"));
        cbGutter->SetValue(m_customGutterColor);
        auto* gutterPick = new wxColourPickerCtrl(ed, wxID_ANY, bgrToColour((int)m_gutterColorValue));
        gutterPick->Enable(m_customGutterColor);
        cbGutter->Bind(wxEVT_CHECKBOX, [gutterPick](wxCommandEvent& e) { gutterPick->Enable(e.IsChecked()); });
        grow->Add(cbGutter, 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 10); grow->Add(gutterPick, 0);
        es->Add(grow, 0, wxALL, 10);
        ed->SetSizer(es);


        // ---- Indentation ----------------------------------------------------------------------
        auto* ind = pg(_("Indentation")); auto* is = new wxBoxSizer(wxVERTICAL);
        auto* trow = new wxBoxSizer(wxHORIZONTAL);
        trow->Add(new wxStaticText(ind, wxID_ANY, _("Tab size:")), 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 8);
        auto* spTab = new SpinField(ind, 1, 16, m_tabWidth, m_dark, 70);
        trow->Add(spTab, 0); is->Add(trow, 0, wxALL, 10);
        auto* cbSpace  = new wxCheckBox(ind, wxID_ANY, _("Replace by space"));      cbSpace->SetValue(!m_useTabs);
        auto* cbIndent = new wxCheckBox(ind, wxID_ANY, _("Auto-indent new lines")); cbIndent->SetValue(m_autoindent);
        row(is, cbSpace); row(is, cbIndent); ind->SetSizer(is);

        // ---- Auto-Completion ------------------------------------------------------------------
        auto* ac = pg(_("Auto-Completion")); auto* as = new wxBoxSizer(wxVERTICAL);
        auto* cbAuto = new wxCheckBox(ac, wxID_ANY, _("Enable auto-completion on each input")); cbAuto->SetValue(m_autocomplete);
        row(as, cbAuto);
        auto* acrow = new wxBoxSizer(wxHORIZONTAL);
        acrow->Add(new wxStaticText(ac, wxID_ANY, _("From the")), 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 6);
        auto* spFrom = new SpinField(ac, 1, 10, m_autoCompFrom, m_dark, 60);
        acrow->Add(spFrom, 0, wxRIGHT, 6);
        acrow->Add(new wxStaticText(ac, wxID_ANY, _("th character")), 0, wxALIGN_CENTRE_VERTICAL);
        as->Add(acrow, 0, wxALL, 10);
        auto* cbPairs = new wxCheckBox(ac, wxID_ANY, _("Auto-insert matching brackets and quotes")); cbPairs->SetValue(m_autoInsertPairs);
        row(as, cbPairs); ac->SetSizer(as);

        // ---- New Document ---------------------------------------------------------------------
        auto* nd = pg(_("New Document")); auto* nds = new wxBoxSizer(wxVERTICAL);
        // Reuses the same 3 msgids as the Edit > EOL Conversion menu (kCmdFormatTodos/TOUNIX/TOMAC) -
        // eolName() itself stays untranslated (the status bar's EOL indicator is deliberately
        // English-always), so translate it here rather than in the shared helper. Calling
        // wxGetTranslation() directly (not the _() macro) because _() requires a compile-time string
        // literal for its static extraction check - eolName() returns one at runtime, not a literal.
        const wxString eolChoices[3] = { wxGetTranslation(eolName(SC_EOL_CRLF)), wxGetTranslation(eolName(SC_EOL_LF)), wxGetTranslation(eolName(SC_EOL_CR)) };
        auto* rbEol = new wxRadioBox(nd, wxID_ANY, _("Format (Line ending)"), wxDefaultPosition, wxDefaultSize,
                                     3, eolChoices, 1, wxRA_SPECIFY_COLS);
        rbEol->SetSelection(m_defaultEol == SC_EOL_LF ? 1 : (m_defaultEol == SC_EOL_CR ? 2 : 0));
        nds->Add(rbEol, 0, wxALL, 10);
        const wxString encChoices[5] = { _("UTF-8"), _("UTF-8 with BOM"), _("UTF-16 LE"), _("UTF-16 BE"), _("ANSI") };
        auto* rbEnc = new wxRadioBox(nd, wxID_ANY, _("Encoding"), wxDefaultPosition, wxDefaultSize,
                                     5, encChoices, 1, wxRA_SPECIFY_COLS);
        rbEnc->SetSelection((m_defaultEncoding >= 0 && m_defaultEncoding <= 4) ? m_defaultEncoding : 0);
        nds->Add(rbEnc, 0, wxALL, 10);
        auto* lrow = new wxBoxSizer(wxHORIZONTAL);
        lrow->Add(new wxStaticText(nd, wxID_ANY, _("Default language:")), 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 8);
        auto* chLang = new wxChoice(nd, wxID_ANY);
        chLang->Append(_("Normal Text"));
        { size_t ln; const WxnLang* lt = wxnLangTable(ln); int sel = 0;
          for (size_t i = 0; i < ln; ++i) { chLang->Append(lt[i].name); if (lt[i].id == m_defaultLangId) sel = (int)i + 1; }
          chLang->SetSelection(sel); }
        lrow->Add(chLang, 0, wxALIGN_CENTRE_VERTICAL);
        nds->Add(lrow, 0, wxALL, 10); nd->SetSizer(nds);

        // ---- Tab Bar --------------------------------------------------------------------------
        auto* tbp = pg(_("Tab Bar")); auto* tbs = new wxBoxSizer(wxVERTICAL);
        auto* cbTabClose = new wxCheckBox(tbp, wxID_ANY, _("Show close button on each tab   (applied on restart)"));
        cbTabClose->SetValue(m_tabCloseBtn); row(tbs, cbTabClose); tbp->SetSizer(tbs);

        // ---- Recent Files History -------------------------------------------------------------
        auto* rf = pg(_("Recent Files History")); auto* rfs = new wxBoxSizer(wxVERTICAL);
        auto* rfrow = new wxBoxSizer(wxHORIZONTAL);
        rfrow->Add(new wxStaticText(rf, wxID_ANY, _("Max number of entries (applied on restart):")), 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 8);
        auto* spMaxRec = new SpinField(rf, 1, 50, m_maxRecent, m_dark, 70);
        rfrow->Add(spMaxRec, 0); rfs->Add(rfrow, 0, wxALL, 10); rf->SetSizer(rfs);

        // ---- Print ------------------------------------------------------------------------
        auto* pr = pg(_("Print")); auto* prs = new wxBoxSizer(wxVERTICAL);
        prs->Add(new wxStaticText(pr, wxID_ANY, _("Header:")), 0, wxLEFT | wxRIGHT | wxTOP, 10);
        auto* txHeader = new wxTextCtrl(pr, wxID_ANY, m_printHeader);
        prs->Add(txHeader, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);
        prs->Add(new wxStaticText(pr, wxID_ANY, _("Footer:")), 0, wxLEFT | wxRIGHT | wxTOP, 10);
        auto* txFooter = new wxTextCtrl(pr, wxID_ANY, m_printFooter);
        prs->Add(txFooter, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);
        // The $(...) macro names are the literal tokens resolvePageMacros() substitutes - translations
        // localise the surrounding prose only and must repeat the tokens verbatim.
        auto* prHint = new wxStaticText(pr, wxID_ANY,
            _("Leave blank for none. Macros: $(FULL_PATH) $(FILE_NAME) $(DATE) $(TIME)\n$(CURRENT_PRINTING_PAGE) $(TOTAL_PRINTING_PAGES)"));
        prHint->SetForegroundColour(m_dark ? wxColour(150, 150, 150) : wxColour(110, 110, 110));
        prs->Add(prHint, 0, wxALL, 10);
        pr->SetSizer(prs);

        // wxListbook leaves its single-column page list too narrow, so the longer labels truncate
        // ("Auto-Comp...", "Recent Files ..."). Grow the column to the widest label - which feeds the list's
        // best width, so the whole selector pane widens to fit - and pin a matching min size.
        //
        // wxListbook::GetListCtrlFlags() only picks wxLC_REPORT (a real single-column, left-aligned
        // list) on MSW; GTK/Cocoa fall back to wxLC_LIST mode (an iconless cell-grid layout) purely
        // because of a #ifdef __WXMSW__ guard in wx's own source, not because report mode looks wrong
        // there. GTK's native rendering of that LC_LIST cell layout visually centers each item instead
        // of hugging the left edge, so force the same REPORT style wx already uses on Windows on every
        // platform for a consistent, left-aligned nav list.
        if (auto* lv = book->GetListView())
        {
            if (!lv->InReportView())
            {
                lv->SetWindowStyleFlag(wxLC_REPORT | wxLC_NO_HEADER);
                lv->InsertColumn(0, wxS("Pages"));
            }
            int maxw = 0;
            for (size_t i = 0; i < book->GetPageCount(); ++i)
            { int tw = 0, th = 0; lv->GetTextExtent(book->GetPageText(i), &tw, &th); if (tw > maxw) maxw = tw; }
            if (lv->GetColumnCount() > 0) lv->SetColumnWidth(0, maxw + lv->FromDIP(28));
            // Pin BOTH min and max width, not just min: with wxLC_NO_HEADER set (above),
            // wxListCtrlBase::DoGetBestClientSize() ignores the real column width and returns an
            // arbitrary 50*charWidth (~250px, see the vendored src/common/listctrlcmn.cpp). wxBookCtrlBase
            // sizes the nav list from that best width, so on GTK the list grabbed ~250px+ (while only
            // painting ~180px of items), pushing the page content ~230px to the right and clipping the
            // checkbox labels/combos. GetBestSize() clamps to MaxSize (wincmn.cpp), so capping max at the
            // intended width fixes it. No-op on Windows (its listbook is already report-mode/sized right).
            const wxSize listSize(maxw + lv->FromDIP(36), -1);
            lv->SetMinSize(listSize);
            lv->SetMaxSize(listSize);
            lv->InvalidateBestSize();
        }

        auto* btn = new wxBoxSizer(wxHORIZONTAL); btn->AddStretchSpacer(); btn->Add(new wxButton(&dlg, wxID_OK, _("Close")), 0);
        auto* top = new wxBoxSizer(wxVERTICAL); top->Add(book, 1, wxEXPAND | wxALL, 8); top->Add(btn, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
        // Layout() explicitly, not just SetSizer: this dialog is opened at a fixed size with no
        // post-show resize, and on GTK SetSizer alone doesn't lay children out until an actual size
        // event - so the wxDefaultSize checkboxes/choices would render at their unlaid-out construction
        // geometry (clipped labels, collapsed combos, dead space). MSW reflows on the initial show, so
        // this is a no-op there. Layout() (not Fit/SetSizerAndFit) keeps the deliberate 620x440 size.
        dlg.SetSizer(top); dlg.Layout(); themeDialog(&dlg);
        // The nav list's row selection is native/generic list-control highlighting. Override it ONLY where
        // the platform actually gets it wrong, and deliberately leave it alone otherwise:
        //   - macOS can render the selected row's fill as plain white regardless of dark mode (instead of
        //     the system highlight every other control here uses natively), so it always needs driving.
        //   - In dark mode every platform needs the rows recoloured, since the native list is light.
        //   - In LIGHT mode on Win/GTK we must NOT touch it. Assigning per-item colours gives the items
        //     wxItemAttrs, which flips wxMSW's list control into its custom-draw path (HandleItemPaint,
        //     src/msw/listctrl.cpp): that path paints a SELECTED row with the legacy
        //     COLOR_HIGHLIGHT/COLOR_HIGHLIGHTTEXT pair, ignoring whatever we set, instead of letting
        //     Windows 11 draw its themed selection. On Win11 light that legacy pair is white-on-pale-blue,
        //     which left the selected page ("General") almost unreadable. Setting nothing keeps the native
        //     themed selection (light fill, dark text) - and in light mode our colours were a no-op anyway,
        //     since normBg/normFg below are just the list's own colours.
#ifdef __WXMAC__
        const bool driveNavColours = true;
#else
        const bool driveNavColours = m_dark;
#endif
        if (auto* lv = driveNavColours ? book->GetListView() : nullptr)
        {
            const wxColour hiBg = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);
            const wxColour hiFg = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT);
            const wxColour normBg = m_dark ? wxColour(45, 45, 45) : lv->GetBackgroundColour();
            const wxColour normFg = m_dark ? wxColour(220, 220, 220) : lv->GetForegroundColour();
            auto restyle = [=](int sel) {
                for (int i = 0; i < (int)book->GetPageCount(); ++i) {
                    const bool on = (i == sel);
                    lv->SetItemBackgroundColour(i, on ? hiBg : normBg);
                    lv->SetItemTextColour(i, on ? hiFg : normFg);
                }
            };
            restyle(book->GetSelection());
            book->Bind(wxEVT_LISTBOOK_PAGE_CHANGED, [=](wxBookCtrlEvent& e) { restyle(e.GetSelection()); e.Skip(); });
        }
        dlg.ShowModal();   // Preferences has no Cancel - changes apply on close
        const int newThemeMode = chTheme->GetSelection();
        m_showToolbar = cbToolbar->GetValue(); m_showStatusbar = cbStatus->GetValue();
        m_showZoomField = cbZoomField->GetValue();
        m_askBeforeClose = cbAskClose->GetValue();
        m_fsAutohideToolbar = cbFsToolbar->GetValue();
        m_tabWidth = spTab->GetValue(); m_useTabs = !cbSpace->GetValue(); m_lineNumbers = cbLineNum->GetValue();
        m_guides = cbGuides->GetValue(); m_ws = cbWs->GetValue(); m_wrapSymbol = cbWrapSym->GetValue(); m_wrap = cbWrap->GetValue(); m_autocomplete = cbAuto->GetValue();
        m_caretLine = cbCaretLn->GetValue(); m_autoindent = cbIndent->GetValue(); m_caretWidth = spCaret->GetValue(); m_edgeColumn = spEdge->GetValue();
        m_scrollBeyond = cbScroll->GetValue(); m_multiEdit = cbMulti->GetValue(); m_caretBlink = spBlink->GetValue();
        m_autoCompFrom = spFrom->GetValue(); m_autoInsertPairs = cbPairs->GetValue();
        m_defaultEol = (rbEol->GetSelection() == 1) ? SC_EOL_LF : (rbEol->GetSelection() == 2) ? SC_EOL_CR : SC_EOL_CRLF;
        m_defaultEncoding = rbEnc->GetSelection();   // index maps directly to the Enc enum (UTF8=0 .. ANSI=4)
        { int s = chLang->GetSelection(); if (s <= 0) m_defaultLangId = -1;
          else { size_t ln; const WxnLang* lt = wxnLangTable(ln); m_defaultLangId = (s - 1 < (int)ln) ? lt[s - 1].id : -1; } }
        const bool tabRecentChanged = (cbTabClose->GetValue() != m_tabCloseBtn) || (spMaxRec->GetValue() != m_maxRecent);
        m_tabCloseBtn = cbTabClose->GetValue(); m_maxRecent = spMaxRec->GetValue();
        m_printHeader = txHeader->GetValue(); m_printFooter = txFooter->GetValue();
        m_customGutterColor = cbGutter->GetValue(); m_gutterColorValue = colourToBgr(gutterPick->GetColour());
        { const wxString sel = chFont->GetStringSelection(); if (sel != kFontSep) m_fontFace = sel; }
        applySettings(); saveSettings();
        // Gutter-colour override takes effect immediately, no restart needed - but applyEditorTheme's
        // StyleClearAll wipes the lexer's per-token colours, so re-run the full lexer pass (which itself
        // starts with applyEditorTheme) instead of the bare theme reset: closing Preferences used to
        // leave the active document highlight-less until the next tab switch.
        if (auto* p = activePage()) setLexerForFile(p->path); else applyEditorTheme(m_dark);
        // Chrome settings fixed per process (locale/toolbar-toggle/icon-set all need a relaunch to take effect).
        // The config writes ride restartWithTheme's commit callback, which only runs once the save-prompt loop
        // has actually confirmed the restart - writing them here unconditionally would apply the new value
        // even if the user then cancels a "save changes?" prompt during the attempted restart.
        const int newUi = UI_LANG_IDS[chUiLang->GetSelection()];
#if defined(WXN_HAS_BORDERLESS) || defined(__WXMAC__)
        const bool newIntBar = cbIntBar->GetValue();
#endif
        const int newIconStyle = chIconStyle->GetSelection();
        const int iconSel = chIconSize->GetSelection();   // always 0..3 (a selection is always set); guard the index anyway
        const int newIconSize = kIconSizes[iconSel < 0 ? 0 : iconSel];
        const bool newReuseInstance = cbReuseInstance->GetValue();
        bool needRestart = (newThemeMode != m_themeMode) || tabRecentChanged || (newUi != curUi) || (newIconStyle != m_iconStyle)
                         || (newIconSize != m_toolbarIconSize) || (newReuseInstance != m_reuseInstance);
#if defined(WXN_HAS_BORDERLESS) || defined(__WXMAC__)
        needRestart = needRestart || (newIntBar != m_integratedBar);
#endif
        if (needRestart)
            restartWithTheme([=] {
                m_themeMode = newThemeMode;
                auto* cfg = wxConfigBase::Get();
                if (newUi != curUi) cfg->Write("UILanguage", (long)newUi);
#if defined(WXN_HAS_BORDERLESS) || defined(__WXMAC__)
                if (newIntBar != m_integratedBar) cfg->Write("IntegratedBar", newIntBar);
#endif
                if (newIconStyle != m_iconStyle) cfg->Write("ToolbarIconStyle", (long)newIconStyle);
                if (newIconSize != m_toolbarIconSize) cfg->Write("ToolbarIconSize", (long)newIconSize);
                if (newReuseInstance != m_reuseInstance) cfg->Write("ReuseInstance", newReuseInstance);
            });
    }

    // ----- macros (record / playback / run multiple / save) -------------
    static bool macroMsgHasText(int m) { return m == SCI_REPLACESEL || m == SCI_ADDTEXT || m == SCI_INSERTTEXT || m == SCI_APPENDTEXT; }
    void onMacroRecord(wxStyledTextEvent& e)   // Scintilla emits one of these per recordable command while recording
    {
        if (!m_recording) return;
        const int msg = (int)e.GetMessage(); const uptr_t wp = (uptr_t)e.GetWParam(); const sptr_t lp = (sptr_t)e.GetLParam();
        // Merge char-by-char typing into one step - raw SCI_MACRORECORD traffic is one ReplaceSel per
        // keystroke, which would balloon every macro and replay slowly: consecutive ReplaceSel
        // concatenate, and a Backspace right after typing trims the last character instead of adding
        // a separate step.
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
    // Fires only for a buffer whose lexer is wxSTC_LEX_CONTAINER - a Scintillua-lexed language;
    // a real Lexilla lexer (SCI_SETILEXER) styles itself and never triggers this.
    void onStcStyleNeeded(wxStyledTextEvent& e)
    {
        wxStyledTextCtrl* stc = wxDynamicCast(e.GetEventObject(), wxStyledTextCtrl);
        if (!stc) { e.Skip(); return; }
        // Style the document shown in the VIEW that fired, NOT the globally-active view: in a split,
        // the non-focused pane also fires STYLENEEDED and must be lexed with ITS OWN page's language.
        ViewPane* v = (stc == m_sub.stc) ? &m_sub : &m_main;
        wxAuiNotebook* nb = v->tabs;
        const int sel = nb ? nb->GetSelection() : wxNOT_FOUND;
        auto* p = (sel == wxNOT_FOUND) ? nullptr : static_cast<EditorPage*>(nb->GetPage(sel));
        if (!p) { e.Skip(); return; }
        if (!p->sciLang.empty()) { scintilluaStyle(stc, p->sciLang); return; }   // Scintillua-lexed buffer
        e.Skip();
    }
    void macroToolStates()
    {
        const bool has = !m_macro.empty();
        auto* tb = toolBar(); auto* mb = menuBar();
        auto en = [&](int id, bool on){ if (tb) tb->EnableTool(id, on); if (mb) if (auto* it = mb->FindItem(id)) it->Enable(on); };
        en(kCmdMacroStartRecordingMacro, !m_recording);
        en(kCmdMacroStopRecordingMacro, m_recording);
        en(kCmdMacroPlaybackRecordedMacro, !m_recording && has);
        en(kCmdMacroRunMultiMacroDlg, !m_recording && has);
        en(kCmdMacroSaveCurrentMacro, !m_recording && has);
    }
    void startMacroRecord() { if (m_recording) return; m_macro.clear(); m_recording = true; sci(SCI_STARTRECORD); macroToolStates(); setStatus(0, _("Recording macro...")); m_hint = true; }
    void stopMacroRecord()  { if (!m_recording) return; sci(SCI_STOPRECORD); m_recording = false; macroToolStates(); setStatus(0, wxString::Format(_("Macro recorded - %d step(s)"), (int)m_macro.size())); m_hint = true; }
    void playSteps(const std::vector<MacroStep>& m) { for (const auto& s : m) sci((UINT)s.msg, s.wparam, s.hasText ? reinterpret_cast<sptr_t>(s.text.c_str()) : s.lparam); }
    void playMacro(const std::vector<MacroStep>& m) { if (m.empty()) return; sci(SCI_BEGINUNDOACTION); playSteps(m); sci(SCI_ENDUNDOACTION); }
    void runMultiple()
    {
        if (m_macro.empty()) return;
        wxDialog dlg(this, wxID_ANY, _("Run a Macro Multiple Times"));
        auto* rbN   = new wxRadioButton(&dlg, wxID_ANY, _("Run"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
        auto* sp    = new SpinField(&dlg, 1, 99999, 1, m_dark, 90);
        auto* rbEof = new wxRadioButton(&dlg, wxID_ANY, _("Run until the end of file"));
        auto* r1 = new wxBoxSizer(wxHORIZONTAL);
        r1->Add(rbN, 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 6); r1->Add(sp, 0, wxRIGHT, 6);
        r1->Add(new wxStaticText(&dlg, wxID_ANY, _("times")), 0, wxALIGN_CENTRE_VERTICAL);
        auto* btn = new wxBoxSizer(wxHORIZONTAL); btn->AddStretchSpacer();
        auto* ok = new wxButton(&dlg, wxID_OK, _("Run")); ok->SetDefault();
        btn->Add(ok, 0, wxRIGHT, 6); btn->Add(new wxButton(&dlg, wxID_CANCEL, _("Cancel")), 0);
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
        wxMenu* menu = m_menuRegistry.find("menu.macro"); if (!menu) return;
        for (int id = myID_MACRO_ITEM; id < myID_MACRO_ITEM + 200; ++id) if (auto* it = menu->FindItem(id)) menu->Destroy(it);
        if (!m_savedMacros.empty() && !m_macroSepAdded) { menu->AppendSeparator(); m_macroSepAdded = true; }
        for (size_t i = 0; i < m_savedMacros.size(); ++i) menu->Append(myID_MACRO_ITEM + (int)i, m_savedMacros[i].first);
    }
    void saveMacro()
    {
        if (m_macro.empty()) return;
        const wxString name = wxGetTextFromUser(_("Macro name:"), _("Save Current Recorded Macro"), "", this);
        if (name.empty()) return;
        for (auto& kv : m_savedMacros) if (kv.first == name) { kv.second = m_macro; setStatus(0, wxString::Format(_("Macro updated: %s"), name)); m_hint = true; return; }
        m_savedMacros.emplace_back(name, m_macro); appendMacroMenuItems();
        setStatus(0, wxString::Format(_("Macro saved: %s"), name)); m_hint = true;
    }

    // ----- dark / light theme -------------------------------------------
    // Parse the theme XML (dark = themes/DarkModeDefault.xml, light = stylers.model.xml)
    // deployed next to the exe. The schema is a Notepad++-compatible format, so a third-party
    // theme file in that format works here unmodified - but the two shipped-by-default files are
    // wxNote's own regenerated, permissively-licensed replacements, not copied theme data (see
    // resources/themes/DarkModeDefault.xml's header and NOTICE for the license/provenance detail).
    wxString themeFilePath(const wxString& name)   // resolve a theme name to its XML on disk
    {
        const wxString dir = wxPathOnly(wxStandardPaths::Get().GetExecutablePath());
        if (name.empty() || name == "Default") return dir + wxFILE_SEP_PATH + "stylers.model.xml";
        return dir + wxFILE_SEP_PATH + "themes" + wxFILE_SEP_PATH + name + ".xml";
    }
    // The per-user folder where the optional GPL udl-compat plugin reads Notepad++
    // userDefineLang.xml files (see packages/udl-compat); kCmdLangOpenudldir opens it.
    wxString udlDir() { return userDataDir() + wxFILE_SEP_PATH + "userDefineLangs"; }
    wxArrayString availableThemes()   // "Default" + every themes/*.xml (the 22 bundled themes)
    {
        wxArrayString out; out.Add("Default");
        wxDir d(wxPathOnly(wxStandardPaths::Get().GetExecutablePath()) + wxFILE_SEP_PATH + "themes");
        if (d.IsOpened()) { wxString f; bool more = d.GetFirst(&f, "*.xml", wxDIR_FILES); while (more) { out.Add(f.BeforeLast('.')); more = d.GetNext(&f); } }
        return out;
    }
    void loadTheme()   // load the active theme: an explicit Style-Configurator choice, else the dark/light default
    {
        loadThemeFile(themeFilePath(!m_themeName.empty() ? m_themeName : wxString(m_dark ? "DarkModeDefault" : "Default")));
    }
    void loadThemeFile(const wxString& path)
    {
        m_theme = WxnTheme{};   // reset so switching themes fully replaces the previous palette
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
        nibFireDocEvent(NIB_EV_STYLE_UPDATED, activePage());   // -> NPPN_WORDSTYLESUPDATED (editor styles re-applied)
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
    void onStyleConfig()   // Settings > Style Configurator: theme picker + per-language token style editor
    {
        const wxString original = m_themeName;
        const wxString kGlobalStyles = _("Global Styles");   // both displayed AND compared-against below - must be the same translated value
        wxDialog dlg(this, wxID_ANY, _("Style Configurator"), wxDefaultPosition, wxSize(680, 440));
        auto* themeRow = new wxBoxSizer(wxHORIZONTAL);
        themeRow->Add(new wxStaticText(&dlg, wxID_ANY, _("Select theme:")), 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 8);
        auto* themeCombo = new wxChoice(&dlg, wxID_ANY, wxDefaultPosition, wxSize(220, -1), availableThemes());
        themeCombo->SetStringSelection(m_themeName.empty() ? "Default" : m_themeName);
        themeRow->Add(themeCombo, 0);
        auto* langList  = new wxListBox(&dlg, wxID_ANY, wxDefaultPosition, wxSize(180, 280));
        auto* styleList = new wxListBox(&dlg, wxID_ANY, wxDefaultPosition, wxSize(180, 280));
        auto* fgPick   = new wxColourPickerCtrl(&dlg, wxID_ANY);
        auto* bgPick   = new wxColourPickerCtrl(&dlg, wxID_ANY);
        auto* cbBold   = new wxCheckBox(&dlg, wxID_ANY, _("Bold"));
        auto* cbItalic = new wxCheckBox(&dlg, wxID_ANY, _("Italic"));
        auto* eg = new wxFlexGridSizer(2, 8, 10);
        eg->Add(new wxStaticText(&dlg, wxID_ANY, _("Foreground colour:")), 0, wxALIGN_CENTRE_VERTICAL); eg->Add(fgPick, 0);
        eg->Add(new wxStaticText(&dlg, wxID_ANY, _("Background colour:")), 0, wxALIGN_CENTRE_VERTICAL); eg->Add(bgPick, 0);
        eg->Add(cbBold, 0); eg->Add(cbItalic, 0);
        auto* edBox = new wxStaticBoxSizer(wxVERTICAL, &dlg, _("Style settings")); edBox->Add(eg, 0, wxALL, 8);
        auto col = [&](const wxString& cap, wxWindow* w){ auto* s = new wxBoxSizer(wxVERTICAL); s->Add(new wxStaticText(&dlg, wxID_ANY, cap), 0, wxBOTTOM, 4); s->Add(w, 1, wxEXPAND); return s; };
        auto* mid = new wxBoxSizer(wxHORIZONTAL);
        mid->Add(col(_("Language:"), langList), 0, wxEXPAND | wxRIGHT, 10);
        mid->Add(col(_("Style:"), styleList), 0, wxEXPAND | wxRIGHT, 10);
        mid->Add(edBox, 1, wxEXPAND);
        auto* btn = new wxBoxSizer(wxHORIZONTAL); btn->AddStretchSpacer();
        btn->Add(new wxButton(&dlg, wxID_OK, _("Save && Close")), 0, wxRIGHT, 6); btn->Add(new wxButton(&dlg, wxID_CANCEL, _("Cancel")), 0);
        auto* top = new wxBoxSizer(wxVERTICAL);
        top->Add(themeRow, 0, wxALL, 12); top->Add(mid, 1, wxEXPAND | wxLEFT | wxRIGHT, 12); top->Add(btn, 0, wxEXPAND | wxALL, 12);
        dlg.SetSizer(top);
        auto fillLangs = [&]{ langList->Clear(); langList->Append(kGlobalStyles); for (auto& kv : m_theme.lexers) langList->Append(kv.first); };
        auto fillStyles = [&]{
            styleList->Clear(); const wxString lang = langList->GetStringSelection();
            if (lang == kGlobalStyles) { for (auto& kv : m_theme.global) styleList->Append(kv.first); }
            else { auto it = m_theme.lexers.find(lang); if (it != m_theme.lexers.end()) for (auto& s : it->second) styleList->Append(s.name.empty() ? wxString::Format(_("Style %d"), s.id) : s.name); }
        };
        auto loadStyle = [&]{
            const wxString lang = langList->GetStringSelection(); const int si = styleList->GetSelection(); if (si < 0) return;
            if (lang == kGlobalStyles) { auto it = m_theme.global.begin(); std::advance(it, si); fgPick->SetColour(bgrToColour(it->second.first)); bgPick->SetColour(it->second.second < 0 ? *wxWHITE : bgrToColour(it->second.second)); cbBold->Disable(); cbItalic->Disable(); }
            else { auto it = m_theme.lexers.find(lang); if (it == m_theme.lexers.end() || si >= (int)it->second.size()) return; const StyleDef& s = it->second[si]; fgPick->SetColour(bgrToColour(s.fg)); bgPick->SetColour(s.bg < 0 ? *wxWHITE : bgrToColour(s.bg)); cbBold->Enable(); cbItalic->Enable(); cbBold->SetValue((s.fontStyle & 1) != 0); cbItalic->SetValue((s.fontStyle & 2) != 0); }
        };
        auto applyEdit = [&]{
            const wxString lang = langList->GetStringSelection(); const int si = styleList->GetSelection(); if (si < 0) return;
            if (lang == kGlobalStyles) { auto it = m_theme.global.begin(); std::advance(it, si); it->second.first = colourToBgr(fgPick->GetColour()); it->second.second = colourToBgr(bgPick->GetColour()); }
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
        { saveThemeToXml(themeFilePath(m_themeName.empty() ? wxString(m_dark ? "DarkModeDefault" : "Default") : m_themeName)); saveSettings(); setStatus(0, _("Theme styles saved")); m_hint = true; }
        else applyThemeSelection(original.empty() ? "Default" : original);   // Cancel -> reload the original theme from disk
    }
    void importStyleTheme()
    {
        wxFileDialog d(this, _("Import style theme(s)"), "", "", _("Theme files (*.xml)|*.xml"), wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
        if (d.ShowModal() != wxID_OK) return;
        wxArrayString paths; d.GetPaths(paths);
        const wxString dir = wxPathOnly(wxStandardPaths::Get().GetExecutablePath()) + wxFILE_SEP_PATH + "themes";
        int n = 0; for (const auto& p : paths) if (wxCopyFile(p, dir + wxFILE_SEP_PATH + wxFileNameFromPath(p))) ++n;
        setStatus(0, wxString::Format(_("Imported %d theme(s) - choose them in Style Configurator"), n)); m_hint = true;
    }
    // Settings > Import > Import plugin(s)...: on Windows, .dll can be EITHER a real Notepad++-ABI plugin
    // (the GPL npp-bridge scans <exe>/plugins/<Name>/<Name>.dll - see packages/npp-bridge) or one of our own
    // Nib-native plugins (<exe>/nib/*.dll - see loadNibPlugins). Both share the .dll extension on Windows, so
    // each picked file is probed for the nib_plugin_main export and routed to the matching loader's folder.
    // Off Windows there's no ABI bridge (it's Win32-only), so the only format is Nib-native (.so / .dylib).
    void importPlugin()
    {
#if defined(__WXMSW__)
        wxFileDialog d(this, _("Import plugin(s)"), "", "", _("Plugin files (*.dll)|*.dll"), wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
#elif defined(__WXMAC__)
        wxFileDialog d(this, _("Import plugin(s)"), "", "", _("Plugin files (*.dylib)|*.dylib"), wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
#else
        wxFileDialog d(this, _("Import plugin(s)"), "", "", _("Plugin files (*.so)|*.so"), wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
#endif
        if (d.ShowModal() != wxID_OK) return;
        wxArrayString paths; d.GetPaths(paths);
        const wxString exeDir = wxPathOnly(wxStandardPaths::Get().GetExecutablePath());
        int n = 0;
#if defined(__WXMSW__)
        const wxString pluginsBase = exeDir + wxFILE_SEP_PATH + "plugins", nibDir = exeDir + wxFILE_SEP_PATH + "nib";
        for (const auto& p : paths)
        {
            wxDynamicLibrary probe(p);   // peek exports to route to the right loader - never call activate/entry here
            const bool isNib = probe.IsLoaded() && probe.HasSymbol("nib_plugin_main");
            if (probe.IsLoaded()) probe.Unload();
            if (isNib)
            {
                if (!wxDirExists(nibDir)) wxFileName::Mkdir(nibDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
                if (wxCopyFile(p, nibDir + wxFILE_SEP_PATH + wxFileNameFromPath(p))) ++n;
            }
            else   // default: a real Notepad++-ABI plugin - the bridge requires <Name>\<Name>.dll exactly
            {
                const wxString name = wxFileName(p).GetName();
                const wxString dir  = pluginsBase + wxFILE_SEP_PATH + name;
                if (!wxDirExists(dir)) wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
                if (wxCopyFile(p, dir + wxFILE_SEP_PATH + name + ".dll")) ++n;
            }
        }
#else
        const wxString nibDir = exeDir + wxFILE_SEP_PATH + "nib";
        if (!wxDirExists(nibDir)) wxFileName::Mkdir(nibDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        for (const auto& p : paths) if (wxCopyFile(p, nibDir + wxFILE_SEP_PATH + wxFileNameFromPath(p))) ++n;
#endif
        if (n == 0) return;
        if (wxMessageBox(wxString::Format(_("%d plugin(s) imported. Restart wxNote now to load them?"), n),
                          "wxNote", wxYES_NO | wxICON_QUESTION, this) == wxYES)
            restartWithTheme();
        else
        { setStatus(0, wxString::Format(_("Imported %d plugin(s) - restart to load them"), n)); m_hint = true; }
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
            // The user's Preferences > Editing "Font" choice always wins over whatever font a loaded
            // theme XML declares - a theme should only ever affect colours here, not silently override
            // an explicit font preference.
            // See setupScintilla()'s identical call for why this stays a single expression (ToUTF8()'s
            // buffer can be a non-owned view into the temporary wxString - splitting across statements
            // would dangle).
            sci(SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<sptr_t>(effectiveFontFace().ToUTF8().data()));
            if (m_theme.defaultSize > 0)      sci(SCI_STYLESETSIZE, STYLE_DEFAULT, m_theme.defaultSize);
            sci(SCI_STYLECLEARALL);                                  // propagate default to every style
            const auto ln = G("Line number margin");
            // Preferences > Editing "custom gutter colour" wins over both the theme and the built-in
            // default, for every piece of the gutter (line numbers, bookmark, fold) so it stays one tone.
            const int gutterBg = m_customGutterColor ? (int)m_gutterColorValue
                                : (ln.second >= 0 ? ln.second : (dark ? 0x2D2D2D : 0xE0E0E0));
            sci(SCI_STYLESETFORE, STYLE_LINENUMBER, ln.first >= 0 ? ln.first : 0x808080);
            sci(SCI_STYLESETBACK, STYLE_LINENUMBER, gutterBg);
            sci(SCI_SETMARGINBACKN, 1, gutterBg);   // bookmark margin matches line-number margin (one-tone gutter)
            const auto ig = G("Indent guideline style"); if (ig.first >= 0) sci(SCI_STYLESETFORE, STYLE_INDENTGUIDE, ig.first);
            const auto car = G("Caret colour");          sci(SCI_SETCARETFORE, car.first >= 0 ? car.first : (dark ? 0xFFFFFF : 0x000000));
            sci(SCI_SETCARETLINEVISIBLE, 1);
            const auto cur = G("Current line background colour"); sci(SCI_SETCARETLINEBACK, cur.second >= 0 ? cur.second : (dark ? 0x2A2A2A : 0xF6F6F6));
            // VS Code's own proven light/dark selection colours (#ADD6FF / #264F78, BGR-encoded for
            // Scintilla) - the flat gray this replaced had too little contrast against the editor
            // background to read clearly as a selection at a glance.
            const auto sel = G("Selected text colour");  sci(SCI_SETSELBACK, 1, sel.second >= 0 ? sel.second : (dark ? 0x784F26 : 0xFFD6AD));
            // ...and a matching FOREGROUND, or selected glyphs keep their syntax colour and vanish into a
            // pale fill (see selection_fore_bgr - the theme's own fgColor is used only if it contrasts).
            { const int sb = sel.second >= 0 ? sel.second : (dark ? 0x784F26 : 0xFFD6AD);
              sci(SCI_SETSELFORE, 1, selection_fore_bgr(sb, sel.first)); }
            const auto wsp = G("White space symbol");    sci(SCI_SETWHITESPACEFORE, 1, wsp.first >= 0 ? wsp.first : (dark ? 0x606060 : 0xB0B0B0));
            // Fold margin + markers, edge, and highlight indicators - re-applied on every theme switch so the whole
            // editor surface follows the theme (not just tokens + default background).
            const auto fold = G("Fold"); const auto foldActive = G("Fold active"); const auto foldMargin = G("Fold margin");
            const int fMarginBg = m_customGutterColor ? gutterBg : (foldMargin.second >= 0 ? foldMargin.second : gutterBg);
            sci(SCI_SETFOLDMARGINCOLOUR, 1, fMarginBg); sci(SCI_SETFOLDMARGINHICOLOUR, 1, fMarginBg);
            const int markFore = m_customGutterColor ? gutterBg : (fold.second >= 0 ? fold.second : gutterBg);        // the theme's Fold fg/bg map onto the markers SWAPPED (see setupFolding)
            const int markBack = fold.first  >= 0 ? fold.first  : 0x808080;
            const int markActive = foldActive.first >= 0 ? foldActive.first : markBack;   // full "Fold active" accent on the box markers
            applyFoldMarkerColours(markFore, markBack, markActive);   // recolour the 7 fold markers + re-arm the nested-square accent
            sci(SCI_SETEDGECOLOUR, dark ? 0x4A4A4A : 0xC8C8C8);   // long-line ruler: a subtle but visible gray (column set in applySettings)
            const auto smart = G("Smart Highlighting");  if (smart.second >= 0) sci(SCI_INDICSETFORE, SMART_INDIC, smart.second);
            const auto findMk = G("Find Mark Style");    if (findMk.second >= 0) sci(SCI_INDICSETFORE, MARK_INDIC, findMk.second);
            const auto bmk = G("Bookmark margin");       if (!m_customGutterColor && bmk.second >= 0) sci(SCI_SETMARGINBACKN, 1, bmk.second);
            applyBraceStyles(dark);
            return;
        }
        // fallback: built-in palette when no theme XML is present
        const int bg = dark ? 0x1E1E1E : 0xFFFFFF, fg = dark ? 0xDCDCDC : 0x000000;
        sci(SCI_STYLESETBACK, STYLE_DEFAULT, bg);
        sci(SCI_STYLESETFORE, STYLE_DEFAULT, fg);
        sci(SCI_STYLECLEARALL);
        const int fallbackGutterBg = m_customGutterColor ? (int)m_gutterColorValue : (dark ? 0x2D2D2D : 0xE0E0E0);
        sci(SCI_STYLESETBACK, STYLE_LINENUMBER, fallbackGutterBg);
        sci(SCI_STYLESETFORE, STYLE_LINENUMBER, 0x808080);
        sci(SCI_SETMARGINBACKN, 1, fallbackGutterBg);
        sci(SCI_SETCARETFORE, dark ? 0xFFFFFF : 0x000000);
        sci(SCI_SETCARETLINEVISIBLE, 1);
        sci(SCI_SETCARETLINEBACK, dark ? 0x2A2A2A : 0xF6F6F6);
        sci(SCI_SETSELBACK, 1, dark ? 0x784F26 : 0xFFD6AD);   // VS Code's selection colours (see the other SETSELBACK call)
        sci(SCI_SETSELFORE, 1, selection_fore_bgr(dark ? 0x784F26 : 0xFFD6AD, -1));   // keep selected text readable
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
#ifdef __WXGTK__
        // GTK analogue of the MSW DarkMode_Explorer scrollbar theming above: neutral-theme the editor's native
        // GtkScrollbar so Mint's accent gradient stops painting a full-height strip down the right edge on an
        // empty document. Runs at startup (applyTheme @ startup) and on every live dark/light toggle.
        wxn_InstallDarkScrollbarCss(m_stc ? (void*)m_stc->GetHandle() : nullptr, dark ? 1 : 0);
#endif
        const wxColour chromeBg = dark ? wxColour(32, 32, 32) : wxColour(240, 240, 240);   // explicit both ways
        const wxColour chromeFg = dark ? wxColour(220, 220, 220) : wxColour(0, 0, 0);
        if (auto* tb = toolBar()) { tb->SetBackgroundColour(chromeBg); tb->Refresh(); }
        if (auto* sb = GetStatusBar()) { sb->SetBackgroundColour(chromeBg); sb->SetForegroundColour(chromeFg); sb->Refresh(); }
#ifdef __WXMSW__
        if (m_grip) m_grip->setColours(chromeBg, dark ? wxColour(112, 112, 112) : wxColour(150, 150, 150));   // dots blend onto chrome
#endif
        // Outside the ifdef - the zoom combo exists on every platform. Its percent grid can move too: a
        // theme change rewrites STYLE_DEFAULT's size, which is the base the percentage is measured against.
        if (m_zoomField) { m_zoomField->setColours(dark); syncZoomField(); }
        if (m_tabs) { m_tabs->SetBackgroundColour(chromeBg); m_tabs->Refresh(); }
        // The integrated toolbar is an aui pane that doesn't span the full width; paint the dock
        // background (the strip to the right of the icons) the same chrome colour so there's no grey gap.
        if (auto* art = m_aui.GetArtProvider())
        {
            art->SetColour(wxAUI_DOCKART_BACKGROUND_COLOUR, chromeBg);
            // Pane captions ("Document Map", "Function List", ...): flat chrome-coloured bars matching the
            // tab strip instead of the stock grey gradient - same colour family as the tabs, same height as
            // the tab strip so the docked panels' headers line up with the tab bar row next to them.
            art->SetMetric(wxAUI_DOCKART_GRADIENT_TYPE, wxAUI_GRADIENT_NONE);
            if (m_tabs) art->SetMetric(wxAUI_DOCKART_CAPTION_SIZE, m_tabs->GetTabCtrlHeight());
            art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_COLOUR,          chromeBg);
            art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_GRADIENT_COLOUR, chromeBg);
            art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_COLOUR,            chromeBg);
            art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_GRADIENT_COLOUR,   chromeBg);
            art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_TEXT_COLOUR, dark ? wxColour(220, 220, 220) : wxColour(40, 40, 40));
            art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_TEXT_COLOUR,   dark ? wxColour(220, 220, 220) : wxColour(40, 40, 40));
            art->SetColour(wxAUI_DOCKART_BORDER_COLOUR, chromeBg);
            art->SetColour(wxAUI_DOCKART_SASH_COLOUR,   chromeBg);
            m_aui.Update();
        }
        this->SetBackgroundColour(chromeBg);   // frame backing shows through the Win11 rounded corners - chrome, not black
    }

    // ShowFullScreen() alone leaves the toolbar looking two-toned: the AUI dock-art background strip
    // beside the (non-full-width) toolbar pane and the toolbar's own background are both painted by
    // applyTheme(), but that only ever runs at startup / an explicit theme switch - never on a
    // fullscreen transition, which changes the frame's width (window chrome/borders removed or
    // restored) without re-triggering either paint. Re-running applyTheme() after every toggle keeps
    // both in sync regardless of direction.
    void toggleFullScreen()
    {
        const bool goingFull = !IsFullScreen();
        // Keep the toolbar visible in full screen by default (Preferences > General "Auto-hide toolbar
        // in full screen", off by default); only strip it when the user opts in. This is deliberately
        // one consistent behaviour on every platform - wxFULLSCREEN_ALL (the old unconditional call)
        // always included wxFULLSCREEN_NOTOOLBAR, i.e. hid the toolbar everywhere; now it stays unless
        // opted in. The style arg only applies when entering; leaving full screen restores everything.
        long style = wxFULLSCREEN_ALL;
        if (!m_fsAutohideToolbar) style &= ~wxFULLSCREEN_NOTOOLBAR;
        ShowFullScreen(goingFull, style);
        // wxFULLSCREEN_NOTOOLBAR only acts on the frame's own GetToolBar(); in integrated/borderless
        // mode the toolbar is an AUI pane wx's flag never touches, so honour the preference by hand
        // there. Entering: hide only if auto-hide is on; leaving: restore to the persistent "Show
        // toolbar" preference. (Compiled out on macOS, where WXN_HAS_BORDERLESS is off and the native
        // path above already handles it.)
#ifdef WXN_HAS_BORDERLESS
        if constexpr (kBorderless)
            showToolBar(goingFull ? (m_showToolbar && !m_fsAutohideToolbar) : m_showToolbar);
#endif
#ifdef __WXMAC__
        // macOS toolbar is now an AUI pane too (not the frame's GetToolBar()), so wxFULLSCREEN_NOTOOLBAR
        // doesn't touch it - honour the auto-hide preference by hand, same as the borderless branch above.
        showToolBar(goingFull ? (m_showToolbar && !m_fsAutohideToolbar) : m_showToolbar);
#endif
        applyTheme(m_dark);
    }

    // wxWidgets refuses to re-theme live: wxApp::SetAppearance() returns CannotChange once a
    // top-level window exists, and MSWEnableDarkMode is startup-only, so the native menu bar and
    // toolbar bake in their theme at creation (mixing them yields "a horrible mix of light and
    // dark mode elements", per wx's own source). We therefore persist the choice and relaunch:
    // each process is born in ONE consistent mode (light skips MSWEnableDarkMode entirely).
    void restartWithTheme(const std::function<void()>& commit = {})
    {
        // Close(true) below is a FORCED close (no veto), so run the save prompts up front -
        // otherwise unsaved edits would be silently discarded. Cancel aborts the restart. This process
        // is about to exit (to relaunch), same as onCloseWindow, so any silent discard here is backed up too.
        for (EditorPage* p : allPages())
            if (!confirmClose(p, /*exiting=*/true)) return;
        if (commit) commit();   // config writes that must NOT land when the user cancels above (e.g. the new UI language)
        auto* cfg = wxConfigBase::Get();
        cfg->Write("ThemeMode", (long)m_themeMode);   // m_themeMode is updated inside commit() when it actually changed
        saveSession(cfg);                     // remember open files so the relaunch restores them
        cfg->Flush();
        this->Hide();   // hide the current window before relaunching so the two processes' windows don't briefly overlap on screen
        wxExecute("\"" + wxStandardPaths::Get().GetExecutablePath() + "\"", wxEXEC_ASYNC);
        Close(true);
    }
    void saveSession(wxConfigBase* cfg)
    {
        int count = 0, active = -1;
        EditorPage* activeP = activePage();
        for (EditorPage* p : allPages())                       // BOTH views, so the sub view's files aren't dropped at exit
        {
            if (!p || p->path.empty()) continue;               // only saved files can be restored
            if (p == activeP) active = count;
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
            const bool editLike = wxDynamicCast(x, wxComboBox) != nullptr || wxDynamicCast(x, wxTextCtrl) != nullptr
#ifdef __WXMAC__
                // A native NSButton push button can't take a custom fill - wx falls back to a non-native
                // owner-drawn render that leaves a stray rectangular border around the rounded bezel.
                // Leave it fully native; the stock bezel is legible in both light and dark dialogs.
                || wxDynamicCast(x, wxButton) != nullptr
#endif
                ;
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
    void notImpl(const wxString& what) { setStatus(0, wxString::Format(_("%s - not yet implemented in this build"), what)); m_hint = true; }
    // Resolves the File > Print header/footer macros that don't depend on the current page (path/date/time);
    // $(CURRENT_PRINTING_PAGE)/$(TOTAL_PRINTING_PAGES) are left literal for SciPrintout to fill in per-page.
    wxString resolvePrintMacros(const wxString& tmpl) const
    {
        if (tmpl.empty()) return tmpl;
        wxString s = tmpl;
        auto* page = activePage();
        const wxString path = page ? page->path : wxString();
        const wxString name = path.empty() ? (page ? page->title : wxString()) : wxFileNameFromPath(path);
        const wxDateTime now = wxDateTime::Now();
        s.Replace("$(FULL_PATH)", path.empty() ? name : path);
        s.Replace("$(FILE_NAME)", name);
        s.Replace("$(DATE)", now.FormatDate());
        s.Replace("$(TIME)", now.FormatTime());
        return s;
    }
    // File > Print... (prompt=true, shows the Print dialog) / Print Now (prompt=false, uses the last settings).
    // Re-entrancy guarded: wxPrinter::Print() pumps a nested modal loop, and a second kCmdFilePrint delivered
    // while that loop is running (e.g. a held/repeated Ctrl+P) would race two SciPrintout jobs over the same
    // wxStyledTextCtrl/DC.
    void doPrint(bool prompt)
    {
        if (m_printing || !m_stc) return;
        m_printing = true;
        wxPrintDialogData pdd(m_printData);
        wxPrinter printer(&pdd);
        wxPageSetupDialogData pageSetup(m_printData);
        auto* page = activePage();
        const wxString title = (page && !page->title.empty()) ? page->title : wxString("Untitled");
        SciPrintout printout(m_stc, title, pageSetup, resolvePrintMacros(m_printHeader), resolvePrintMacros(m_printFooter));
        const bool ok = printer.Print(this, &printout, prompt);
        m_printing = false;
        if (!ok)
        {
            if (wxPrinter::GetLastError() == wxPRINTER_ERROR)
                themedInfo(_("There was a problem printing.\nPerhaps your current printer is not set up correctly."), _("Print"));
            return;
        }
        m_printData = printer.GetPrintDialogData().GetPrintData();
        setStatus(0, wxString::Format(_("Printed %s"), title)); m_hint = true;
    }
    // File > Print Preview... - wxNote's own cross-platform preview (wxPrintPreview + wxPreviewFrame),
    // reusing the same SciPrintout as doPrint(). This is deliberately NOT the Windows print dialog's own
    // preview pane: that pane is only populated by the Windows-only modern print API (XPS/WinRT), which the
    // portable GDI printing path can't feed - so the dialog shows "no preview", and this window is the
    // portable answer. The preview OWNS its two heap printouts; SciPrintout copies the page-setup by value
    // and only borrows the editor pointer (which outlives this modeless frame), so nothing dangles.
    void doPrintPreview()
    {
        if (m_printing || !m_stc) return;
        wxPrintDialogData pdd(m_printData);
        wxPageSetupDialogData pageSetup(m_printData);
        auto* page = activePage();
        const wxString title = (page && !page->title.empty()) ? page->title : wxString("Untitled");
        const wxString hdr = resolvePrintMacros(m_printHeader), ftr = resolvePrintMacros(m_printFooter);
        auto* preview = new wxPrintPreview(new SciPrintout(m_stc, title, pageSetup, hdr, ftr),
                                           new SciPrintout(m_stc, title, pageSetup, hdr, ftr), &pdd);
        if (!preview->IsOk())
        {
            delete preview;
            // Reuse the already-translated Print error - the actionable part (printer not set up) is
            // identical, so this adds no new untranslated string.
            themedInfo(_("There was a problem printing.\nPerhaps your current printer is not set up correctly."), _("Print Preview"));
            return;
        }
        auto* frame = new WxnPreviewFrame(preview, this, _("Print Preview"), wxDefaultPosition, wxSize(820, 720));
        frame->Centre(wxBOTH);
        frame->Initialize();   // builds the control bar + canvas (its stock wxArtProvider buttons now exist)
        themeDialog(frame);    // theme the title bar + toolbar strip first; the pass below then overrides the
                               // controls themeDialog would otherwise leave mis-styled (raised buttons, dark canvas)
        stylePreviewWindow(frame, preview);
        frame->Show();
        frame->Raise();   // integrated/borderless parent can leave a new top-level frame behind it
    }
    // Make the stock wxPreviewFrame read like the rest of wxNote: a flat icon toolbar matching the main
    // window, and a paper viewport that stays neutral regardless of the app theme (a printed sheet is white -
    // tinting its backdrop near-black, as themeDialog's blanket recolour does, looks wrong). Runs AFTER
    // themeDialog so these per-control overrides win.
    void stylePreviewWindow(wxWindow* frame, wxPrintPreviewBase* preview)
    {
        // Control-bar buttons: give them wxNote's icons and make them FLAT (no raised button face, background
        // blended into the strip) so only the icon shows, like the main window's wxToolBar. print -> the active
        // pack's icon(); page-nav/zoom -> previewGlyph() (clean line glyphs; the pack's own filled zoom
        // magnifiers turn muddy at 16px). The stock buttons are keyed by wxID_PREVIEW_*
        // (wxPreviewControlBar::CreateButtons); flatness must come from wxBORDER_NONE at CONSTRUCTION - flipping
        // the style post-hoc recreates the HWND and dangles the sizer/handlers (crash). So each is swapped for a
        // fresh borderless button of the same id via wxSizer::Replace: the base bar's id-keyed
        // EVT_BUTTON/EVT_UPDATE_UI handlers still fire (events bubble to the control bar), and the base never
        // dereferences the swapped buttons, so nothing is left dangling. All get one uniform size so they line
        // up vertically across the row.
        static const struct { int id; const char* name; bool synth; } kBtns[] = {
            { wxID_PREVIEW_PRINT,    "print",   false }, { wxID_PREVIEW_FIRST,    "first",   true },
            { wxID_PREVIEW_PREVIOUS, "prev",    true  }, { wxID_PREVIEW_NEXT,     "next",    true },
            { wxID_PREVIEW_LAST,     "last",    true  }, { wxID_PREVIEW_ZOOM_OUT, "zoomout", true },
            { wxID_PREVIEW_ZOOM_IN,  "zoomin",  true  },
        };
        const wxSize btnSize = FromDIP(wxSize(26, 24));   // uniform -> all icons share a baseline and align
        wxWindow* controlBar = nullptr;
        for (const auto& b : kBtns)
            if (wxWindow* old = frame->FindWindow(b.id))
            {
                controlBar = old->GetParent();
                const wxBitmapBundle bmp = b.synth ? previewGlyph(b.name, 16) : icon(b.name, 16);
                auto* flat = new wxBitmapButton(controlBar, b.id, bmp, wxDefaultPosition, btnSize, wxBORDER_NONE);
                flat->SetToolTip(old->GetToolTipText());
                flat->SetBackgroundColour(controlBar->GetBackgroundColour());
                if (wxSizer* sz = controlBar->GetSizer()) sz->Replace(old, flat);
                old->Destroy();
            }
        // Drop the toolbar's Close button: the window's own title-bar close (X) already does this, so a second
        // one is redundant. Detach from the sizer, then destroy it.
        if (wxWindow* closeBtn = frame->FindWindow(wxID_PREVIEW_CLOSE))
        {
            if (!controlBar) controlBar = closeBtn->GetParent();
            if (wxSizer* sz = closeBtn->GetParent()->GetSizer()) sz->Detach(closeBtn);
            closeBtn->Destroy();
        }
        if (wxSizer* sz = controlBar ? controlBar->GetSizer() : nullptr)
        {
            // wxNote's page indicator: the classical "cur / total" readout between the prev/next chevrons, but
            // as ONE static label - drawn text has no native box, so it sits exactly on the icons' midline (the
            // stock editable field never could, which is why WxnPreviewFrame omits it). Kept in sync as the
            // arrows move and once pagination settles (GetMaxPage isn't final until the first render).
            if (preview)
            {
                auto* pageText = new wxStaticText(controlBar, wxID_ANY, "1 / 1");
                if (m_dark)   // created after themeDialog's recolour pass, so theme it by hand
                { pageText->SetForegroundColour(wxColour(220, 220, 220)); pageText->SetBackgroundColour(controlBar->GetBackgroundColour()); }
                pageText->Bind(wxEVT_UPDATE_UI, [pageText, preview](wxUpdateUIEvent&) {
                    const wxString s = wxString::Format("%d / %d", wxMax(1, preview->GetCurrentPage()), wxMax(1, preview->GetMaxPage()));
                    if (pageText->GetLabel() != s) { pageText->SetLabel(s); if (wxWindow* p = pageText->GetParent()) p->Layout(); }
                });
                // Slot it where the stock field sat: just before the "next page" chevron.
                int at = -1;
                for (size_t i = 0; i < sz->GetItemCount(); ++i)
                    if (wxSizerItem* it = sz->GetItem(i); it && it->IsWindow() && it->GetWindow()->GetId() == wxID_PREVIEW_NEXT) { at = static_cast<int>(i); break; }
                // A default-width gap on the LEFT only: every stock button already carries its own left
                // border, so the "next" chevron supplies the right-hand gap - an own wxRIGHT border on top
                // of that doubled it (the readout sat noticeably farther from ">" than from "<").
                const wxSizerFlags fl = wxSizerFlags().Border(wxLEFT, wxSizerFlags::GetDefaultBorder()).Center();
                if (at >= 0) sz->Insert(static_cast<size_t>(at), pageText, fl); else sz->Add(pageText, fl);
            }
            // Everything on one centred baseline, with no top/bottom border throwing an item off the midline.
            for (size_t i = 0; i < sz->GetItemCount(); ++i)
                if (wxSizerItem* it = sz->GetItem(i); it && it->IsWindow())
                    it->SetFlag((it->GetFlag() & ~(wxALIGN_TOP | wxALIGN_BOTTOM | wxEXPAND | wxTOP | wxBOTTOM)) | wxALIGN_CENTER_VERTICAL);
            sz->PrependSpacer(FromDIP(6));   // small left indent before the print icon, like the main toolbar
            // Fixed, roomier bar height: taller than every item so they ALL centre on one shared midline (the
            // page field no longer rides off-centre from the icons), with vertical breathing room top & bottom.
            controlBar->SetMinSize(wxSize(-1, FromDIP(40)));
            controlBar->Layout();
            // Suppress wxPreviewControlBar::OnPaint's hard black separator line along the bar's bottom edge -
            // it sits right under the flat icons and reads as a stray dark line. Just fill the strip with its
            // own colour (not calling Skip() stops the stock painter from drawing the line).
            controlBar->Bind(wxEVT_PAINT, [](wxPaintEvent& ev) {
                wxWindow* w = static_cast<wxWindow*>(ev.GetEventObject());
                wxPaintDC dc(w);
                dc.SetBackground(wxBrush(w->GetBackgroundColour()));
                dc.Clear();
            });
        }
        // Paper viewport: an Open-Color grey backdrop so the white sheet + its drop shadow read well - a light
        // grey (gray-5) in light mode and a deeper grey (gray-7) in dark mode (not the near-black the blanket
        // theme gave, which looked wrong). Re-pinned on an OS colour-scheme flip while the (modeless) preview
        // is open, since wxPreviewCanvas's own EVT_SYS_COLOUR_CHANGED handler would otherwise repaint it with a
        // system colour.
        const wxColour canvasBg = m_dark ? wxColour(0x49, 0x50, 0x57)    // Open Color gray-7
                                         : wxColour(0xad, 0xb5, 0xbd);   // Open Color gray-5
        for (wxWindow* c : frame->GetChildren())
            if (auto* canvas = wxDynamicCast(c, wxPreviewCanvas))
            {
                canvas->SetBackgroundColour(canvasBg);
                canvas->Bind(wxEVT_SYS_COLOUR_CHANGED, [canvas, canvasBg](wxSysColourChangedEvent& e)
                             { canvas->SetBackgroundColour(canvasBg); canvas->Refresh(); e.Skip(); });
                canvas->Refresh();
                break;
            }
        frame->Layout();   // re-flow the frame so the control bar picks up its new (taller) height
    }
    void setStatus(int field, const wxString& text) { SetStatusText(" " + text, field); }  // leading space ~ 4px left margin
    // Interactive status bar: double-click a field to act on it.
    void onStatusDClick(wxMouseEvent& e)
    {
        wxStatusBar* sb = GetStatusBar();
        if (!sb) { e.Skip(); return; }
        int field = -1;
        for (int i = 0; i < (int)sb->GetFieldsCount(); ++i)
        { wxRect r; if (sb->GetFieldRect(i, r) && r.Contains(e.GetPosition())) { field = i; break; } }
        switch (field)
        {
            case 2: onGoTo(); break;                                       // Ln:Col:Pos -> Go To Line
            case 4: showEolMenu(); break;                                  // line-ending -> convert popup
            case 5: showEncodingMenu(); break;                             // encoding -> re-interpret / convert popup
            // no case 6 (zoom): the ZoomField child eats its own mouse events, so nothing reaches the bar there
            case 7: sci(SCI_EDITTOGGLEOVERTYPE); updateStatus(); break;    // INS/OVR -> toggle typing mode
            default: e.Skip(); break;
        }
    }
    void showEolMenu()   // small popup at the cursor: convert the document's line endings
    {
        wxMenu m;
        m.Append(kCmdFormatTodos,  _("Windows (CR LF)"));
        m.Append(kCmdFormatTounix, _("Unix (LF)"));
        m.Append(kCmdFormatTomac,  _("Macintosh (CR)"));
        switch (this->GetPopupMenuSelectionFromUser(m))
        {
            case kCmdFormatTodos:  setEol(SC_EOL_CRLF); break;
            case kCmdFormatTounix: setEol(SC_EOL_LF);   break;
            case kCmdFormatTomac:  setEol(SC_EOL_CR);   break;
        }
    }
    void showEncodingMenu()   // status-bar encoding field: re-interpret (fix a misread file) or convert the encoding
    {
        wxMenu m;
        m.Append(kCmdFormatAnsi,      _("Encode as ANSI"));
        m.Append(kCmdFormatAsUtf8,  _("Encode as UTF-8"));
        m.Append(kCmdFormatUtf8,     _("Encode as UTF-8 BOM"));
        m.Append(kCmdFormatUtf16le,  _("Encode as UTF-16 LE BOM"));
        m.Append(kCmdFormatUtf16be,  _("Encode as UTF-16 BE BOM"));
        m.AppendSeparator();
        m.Append(kCmdFormatConv2Ansi,     _("Convert to ANSI"));
        m.Append(kCmdFormatConv2AsUtf8, _("Convert to UTF-8"));
        m.Append(kCmdFormatConv2Utf8,    _("Convert to UTF-8 BOM"));
        m.Append(kCmdFormatConv2Utf16le, _("Convert to UTF-16 LE BOM"));
        m.Append(kCmdFormatConv2Utf16be, _("Convert to UTF-16 BE BOM"));
        switch (this->GetPopupMenuSelectionFromUser(m))
        {
            case kCmdFormatAnsi:      interpretAs(ENC_ANSI);     break;
            case kCmdFormatAsUtf8:  interpretAs(ENC_UTF8);     break;
            case kCmdFormatUtf8:     interpretAs(ENC_UTF8_BOM); break;
            case kCmdFormatUtf16le:  interpretAs(ENC_UTF16_LE); break;
            case kCmdFormatUtf16be:  interpretAs(ENC_UTF16_BE); break;
            case kCmdFormatConv2Ansi:     convertTo(ENC_ANSI);     break;
            case kCmdFormatConv2AsUtf8: convertTo(ENC_UTF8);     break;
            case kCmdFormatConv2Utf8:    convertTo(ENC_UTF8_BOM); break;
            case kCmdFormatConv2Utf16le: convertTo(ENC_UTF16_LE); break;
            case kCmdFormatConv2Utf16be: convertTo(ENC_UTF16_BE); break;
        }
    }
    void showAbout()
    {
        wxDialog dlg(this, wxID_ANY, _("About wxNote"));
        auto* s = new wxBoxSizer(wxVERTICAL);
        s->Add(new wxStaticBitmap(&dlg, wxID_ANY, wxBitmapBundle::FromSVG(APP_ICON_SVG, wxSize(72, 72))),
               0, wxALIGN_CENTRE | wxTOP, 18);
        // Header: app name + version ("wxNote vX.Y.Z"). Both are proper nouns / the CMake project()
        // version, so no translatable string is needed and the version never drifts from a hardcoded
        // copy. Bold + a touch larger so it reads as the title.
        auto* title = new wxStaticText(&dlg, wxID_ANY, wxString::Format("wxNote v%s", WXN_VERSION));
        { wxFont f = title->GetFont(); f.MakeBold(); f.SetPointSize(f.GetPointSize() + 3); title->SetFont(f); }
        s->Add(title, 0, wxALIGN_CENTRE | wxTOP, 12);
        s->Add(new wxStaticText(&dlg, wxID_ANY,
                   _("A fast, cross-platform text editor built on wxWidgets:\n"
                     "a native Scintilla editor with dark/light themes and plugin support.")),
               0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 16);
        s->Add(dlg.CreateButtonSizer(wxOK), 0, wxALL | wxALIGN_RIGHT, 10);
        dlg.SetSizerAndFit(s);
        themeDialog(&dlg);
        dlg.ShowModal();
    }

#ifdef __WXMSW__
    // wxToolBar::MSWCommand sign-extends the 16-bit WM_COMMAND id to a signed short, so command ids
    // above 32767 (e.g. kCmdFileNew = 41001) wrap negative and its FindById() can't match the tool - the
    // click is silently dropped (this also breaks the native frame toolbar, not just the aui one). Catch a
    // toolbar's WM_COMMAND here and redispatch it through onCommand with the correct, unsigned id.
    WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override
    {
        if (message == WM_COMMAND && lParam && m_toolBarPtr &&
            (void*)lParam == (void*)m_toolBarPtr->GetHandle())
        {
            wxCommandEvent ce(wxEVT_MENU, LOWORD(wParam));
            onCommand(ce);
            return 0;
        }
        // Recolour the toolbar's hover/hot-track highlight from the Windows theme default (a pale system-
        // accent blue, independent of anything we choose - confirmed by disabling all our own icon/colour
        // code and still seeing it) to an Open Color green, via NM_CUSTOMDRAW. TBCDRF_HILITEHOTTRACK tells
        // comctl32 to draw the hot button with a solid clrHighlightHotTrack fill instead of the themed hot
        // appearance - themed (UxTheme) toolbars otherwise ignore clrHighlightHotTrack entirely.
        if (message == WM_NOTIFY && lParam && m_toolBarPtr)
        {
            auto* hdr = reinterpret_cast<NMHDR*>(lParam);
            if (hdr->code == NM_CUSTOMDRAW && hdr->hwndFrom == (HWND)m_toolBarPtr->GetHandle())
            {
                auto* cd = reinterpret_cast<NMTBCUSTOMDRAW*>(lParam);
                switch (cd->nmcd.dwDrawStage)
                {
                    case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
                    case CDDS_ITEMPREPAINT:
                        // Pale green-2 reads as a soft highlight on the light toolbar but glares against
                        // a dark one - dark mode uses the same Open Color family's deep end (green-9)
                        // instead, so the hover cue stays a recognizable "green" without the mismatch.
                        cd->clrHighlightHotTrack = m_dark ? RGB(0x2b, 0x8a, 0x3e) : RGB(0xb2, 0xf2, 0xbb);
                        return CDRF_DODEFAULT | TBCDRF_HILITEHOTTRACK;
                }
            }
        }
        return FB::MSWWindowProc(message, wParam, lParam);
    }
#endif
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
        if (cmd >= NIB_ALLOC_CMD_FIRST && cmd < g_nibAllocNextCmd)
        {   // a nib.alloc/1 dynamic command id (toolbar button / invoke_command / plugin menu entry).
            // Broadcast to every registered sink - each filters on the id, mirroring how N++ relays a
            // dynamic WM_COMMAND to every plugin. This IS the wx-event dispatch path the alloc contract
            // promises (`cmd` is already the & 0xFFFF-recovered unsigned id) - never raw WM_COMMAND.
            for (const NibAllocSink& s : g_nibAllocSinks)
                if (s.fn) s.fn(reinterpret_cast<NibHost*>(g_view), cmd, s.user);
            return;
        }
        if (cmd >= myID_UILANG_FIRST && cmd < myID_UILANG_FIRST + (int)WXSIZEOF(UI_LANG_IDS))   // Localization menu: switch the UI language (restart-to-apply)
        {
            const long newUi = UI_LANG_IDS[cmd - myID_UILANG_FIRST];
            if (newUi != readUiLang())   // the language write rides the commit callback: a cancelled restart must not switch the config
                restartWithTheme([newUi] { wxConfigBase::Get()->Write("UILanguage", newUi); });
            return;
        }
        if (cmd >= myID_DOCLIST_ITEM && cmd < myID_DOCLIST_ITEM + 1000)   // document-list dropdown entry
        { const size_t n = (size_t)(cmd - myID_DOCLIST_ITEM); if (n < m_tabs->GetPageCount()) m_tabs->SetSelection(n); return; }
        if (const WxnLang* L = wxnLangFind(cmd)) { setForcedLang(L->lexer, L->name); return; }   // Language menu: force that lexer on the active buffer
        if (cmd >= kSciLangMenuBase && cmd < kSciLangMenuBase + (int)m_sciLangs.size())   // Language menu: a registered Scintillua language
        { applyScintilluaToActiveBuffer(m_sciLangs[cmd - kSciLangMenuBase].name); return; }
        if (cmd >= myID_MACRO_ITEM && cmd < myID_MACRO_ITEM + 200)        // a saved macro from the Macro menu
        { const size_t n = (size_t)(cmd - myID_MACRO_ITEM); if (n < m_savedMacros.size()) playMacro(m_savedMacros[n].second); return; }
        if (cmd >= myID_OPENFOLDER_TOOL_BASE && cmd < myID_OPENFOLDER_TOOL_BASE + (int)m_openFolderTools.size())
        { openHereToolAt(cmd - myID_OPENFOLDER_TOOL_BASE); return; }   // a dynamically-detected File > Open Containing Folder entry
        // Win32 WM_COMMAND carries only a 16-bit id and wx sign-extends it, so command ids
        // above 32767 (all of the kCmd* range) arrive negative. Read it as unsigned 16-bit
        // (LOWORD(wParam) semantics).
        switch (e.GetId() & 0xFFFF)
        {
            case kCmdFileNew: doNew(); break;
            case kCmdFileOpen: onOpen(); break;
            case kCmdFileReload: onReload(); break;
            case kCmdFileSave: onSave(); break;
            case kCmdFileSaveall: onSaveAll(); break;
            case kCmdFileSaveas: onSaveAs(); break;
            case kCmdFileClose: closeActive(); break;
            case kCmdFileRestoreLastClosedFile: restoreLastClosed(); break;
            case kCmdFileCloseall: closeAll(); break;
            case kCmdFileCloseallButCurrent: closeAllBut(activePage()); break;
            case kCmdFileExit: Close(true); break;

            case kCmdViewGotoAnotherView:     openInOtherView(false); break;   // move the active doc to the other view
            case kCmdViewCloneToAnotherView: openInOtherView(true);  break;   // clone it there (shared document)

            case kCmdEditUndo: sci(SCI_UNDO); break;
            case kCmdEditRedo: sci(SCI_REDO); break;
            case kCmdEditCut: sci(SCI_CUT); recordClip(); break;
            case kCmdEditCopy: sci(SCI_COPY); recordClip(); break;
            case kCmdEditPaste: sci(SCI_PASTE); break;
            case kCmdEditDelete: sci(SCI_CLEAR); break;
            case kCmdEditSelectall: sci(SCI_SELECTALL); break;
            case kCmdEditMultiSelectAll:                   multiSelectAll(0); break;
            case kCmdEditMultiSelectAllMatchCase:          multiSelectAll(SCFIND_MATCHCASE); break;
            case kCmdEditMultiSelectAllWholeWord:          multiSelectAll(SCFIND_WHOLEWORD); break;
            case kCmdEditMultiSelectAllMatchCaseWholeWord: multiSelectAll(SCFIND_MATCHCASE | SCFIND_WHOLEWORD); break;
            case kCmdEditMultiSelectNext:                   multiSelectNext(0); break;
            case kCmdEditMultiSelectNextMatchCase:          multiSelectNext(SCFIND_MATCHCASE); break;
            case kCmdEditMultiSelectNextWholeWord:          multiSelectNext(SCFIND_WHOLEWORD); break;
            case kCmdEditMultiSelectNextMatchCaseWholeWord: multiSelectNext(SCFIND_MATCHCASE | SCFIND_WHOLEWORD); break;
            case kCmdEditMultiSelectUndo:                   multiSelectUndo(); break;
            case kCmdEditMultiSelectSkip:                  multiSelectSkip(0); break;
            case kCmdEditUppercase: sci(SCI_UPPERCASE); break;
            case kCmdEditLowercase: sci(SCI_LOWERCASE); break;
            case kCmdEditInvertcase: transformSel([](std::string& s){ for (char& c : s) c = (char)(std::isupper((unsigned char)c) ? std::tolower((unsigned char)c) : std::toupper((unsigned char)c)); }); break;
            case kCmdEditPropercaseForce: transformSel([](std::string& s){ bool st = true; for (char& c : s){ if (std::isalpha((unsigned char)c)){ c = (char)(st ? std::toupper((unsigned char)c) : std::tolower((unsigned char)c)); st = false; } else st = true; } }); break;
            case kCmdEditSentenceCaseForce: transformSel([](std::string& s){ bool st = true; for (char& c : s){ if (std::isalpha((unsigned char)c)){ c = (char)(st ? std::toupper((unsigned char)c) : std::tolower((unsigned char)c)); st = false; } else if (c=='.'||c=='!'||c=='?') st = true; } }); break;
            case kCmdEditDupLine: sci(SCI_LINEDUPLICATE); break;
            case kCmdEditLineUp: sci(SCI_MOVESELECTEDLINESUP); break;
            case kCmdEditLineDown: sci(SCI_MOVESELECTEDLINESDOWN); break;
            case kCmdEditRemoveEmptyLines: removeEmptyLines(); break;
            case kCmdEditTrimTrailing: trimTrailing(); break;
            case kCmdEditTrimLineHead: trimLeading(); break;
            case kCmdEditTrimBoth: trimBoth(); break;
            case kCmdEditEol2ws: eolToSpace(); break;
            case kCmdEditTrimall: trimBoth(); eolToSpace(); break;
            case kCmdEditTab2sw: tabsToSpaces(); break;
            case kCmdEditInsTab: sci(SCI_TAB); break;
            case kCmdEditRmvTab: sci(SCI_BACKTAB); break;
            case kCmdEditJoinLines: linesJoin(); break;
            case kCmdEditSplitLines: linesSplit(); break;
            case kCmdEditBlankLineAboveCurrent: blankLine(false); break;
            case kCmdEditBlankLineBelowCurrent: blankLine(true); break;
            case kCmdEditRemoveAnyDupLines: removeDuplicateLines(false); break;
            case kCmdEditRemoveConsecutiveDupLines: removeDuplicateLines(true); break;
            case kCmdEditSortlinesReverseOrder: reverseLines(); break;
            case kCmdEditSortlinesLexicographicAscending: sortLines(0, false); break;
            case kCmdEditSortlinesLexicographicDescending: sortLines(0, true); break;
            case kCmdEditSortlinesLexicoCaseInsensAscending: sortLines(1, false); break;
            case kCmdEditSortlinesLexicoCaseInsensDescending: sortLines(1, true); break;
            case kCmdEditSortlinesIntegerAscending: sortLines(2, false); break;
            case kCmdEditSortlinesIntegerDescending: sortLines(2, true); break;
            case kCmdEditInsertDatetimeShort: insertDateTime(false); break;
            case kCmdEditInsertDatetimeLong: insertDateTime(true); break;
            case kCmdEditBlockComment: toggleLineComment(); break;
            case kCmdFormatTodos: setEol(SC_EOL_CRLF); break;
            case kCmdFormatTounix: setEol(SC_EOL_LF); break;
            case kCmdFormatTomac: setEol(SC_EOL_CR); break;

            case kCmdSearchFind: onFind(); break;
            case kCmdSearchFindinfiles: onFindInFiles(); break;
            case kCmdSearchFindnext: findNext(true); break;
            case kCmdSearchFindprev: findNext(false); break;
            case kCmdSearchReplace: onReplace(); break;
            case kCmdSearchGotoline: onGoTo(); break;
            case kCmdSearchGotoMatchingBrace: gotoMatchingBrace(); break;
            case kCmdSearchToggleBookmark: toggleBookmark(); break;
            case kCmdSearchNextBookmark: gotoBookmark(true); break;
            case kCmdSearchPrevBookmark: gotoBookmark(false); break;
            case kCmdSearchClearBookmarks: sci(SCI_MARKERDELETEALL, MARK_BOOKMARK); break;
            // Change History: SCI_SETCHANGEHISTORY was added in upstream Scintilla 5.3.0; wx vendors its
            // own Scintilla fork (github.com/wxWidgets/scintilla, "wx" branch) at 5.0.0 - confirmed still
            // true as of the latest wx release (3.3.2, 2026-03) by checking that fork's pinned commit
            // directly, not just our own currently-built 3.3.1. This isn't a "bump our pinned version"
            // fix - there is currently no wxWidgets release whose vendored Scintilla is new enough, so
            // there's nothing to bump TO. Revisit if/when wx's own Scintilla fork catches up upstream.
            case kCmdSearchChangedNext:
            case kCmdSearchChangedPrev:
            case kCmdSearchClearChangeHistory: notImpl(_("Change History (needs a newer Scintilla than this wx build carries)")); break;

            case kCmdViewZoomin: sci(SCI_ZOOMIN); break;
            case kCmdViewZoomout: sci(SCI_ZOOMOUT); break;
            case kCmdViewZoomrestore: sci(SCI_SETZOOM, 0); break;
            case kCmdViewWrap: toggleWrap(); break;
            case kCmdViewAllCharacters: toggleWs(); break;
            case kCmdViewIndentGuide: toggleGuides(); break;
            case kCmdViewTabSpace: toggleWs(); break;            // "Show Space and Tab"
            case kCmdViewEol: sci(SCI_SETVIEWEOL, sci(SCI_GETVIEWEOL) ? 0 : 1); break;
            case kCmdViewNpc: toggleWs(); break;   // "Show Non-Printing Characters" == show whitespace + EOL
            case kCmdViewNpcCcunieol: { const bool on = sci(SCI_GETLINEENDTYPESALLOWED) == 0; sci(SCI_SETLINEENDTYPESALLOWED, on ? SC_LINE_END_TYPE_UNICODE : 0); if (menuBar()) menuBar()->Check(kCmdViewNpcCcunieol, on); break; }
            case kCmdViewGotoStart: sci(SCI_DOCUMENTSTART); break;
            case kCmdViewGotoEnd:   sci(SCI_DOCUMENTEND);   break;
            case kCmdViewTabColour1: applyTabColour(0); break;
            case kCmdViewTabColour2: applyTabColour(1); break;
            case kCmdViewTabColour3: applyTabColour(2); break;
            case kCmdViewTabColour4: applyTabColour(3); break;
            case kCmdViewTabColour5: applyTabColour(4); break;
            case kCmdViewTabColourNone: applyTabColour(-1); break;
            case kCmdEditRtl: if (m_stc) m_stc->SetLayoutDirection(wxLayout_RightToLeft); break;
            case kCmdEditLtr: if (m_stc) m_stc->SetLayoutDirection(wxLayout_LeftToRight); break;
            case kCmdViewTabNext: mruSwitch(); break;   // Ctrl+Tab -> most-recently-used switch
            case kCmdViewTabPrev: m_tabs->AdvanceSelection(false); break;
            case kCmdViewTabMoveforward:  moveTab(true);  break;
            case kCmdViewTabMoveBackward: moveTab(false); break;
            // (tab "Apply Colour" picks are dispatched straight to applyTabColour() in onTabContext)
            case kCmdViewTabStart: if (m_tabs->GetPageCount()) m_tabs->SetSelection(0); break;
            case kCmdViewTabEnd:   if (m_tabs->GetPageCount()) m_tabs->SetSelection(m_tabs->GetPageCount() - 1); break;
            case kCmdViewTab1: case kCmdViewTab2: case kCmdViewTab3: case kCmdViewTab4: case kCmdViewTab5:
            case kCmdViewTab6: case kCmdViewTab7: case kCmdViewTab8: case kCmdViewTab9:
                { const size_t n = (size_t)((e.GetId() & 0xFFFF) - kCmdViewTab1); if (n < m_tabs->GetPageCount()) m_tabs->SetSelection(n); break; }
            case myID_VIEW_TERMINAL: toggleTerminal(); break;      // View > Show Terminal
            case myID_CAP_NEW: doNew(); break;                      // "+" caption button
            case myID_CAP_CLOSE: closeActive(); break;             // "x" caption button
            case myID_DOCLIST: onDocList(); break;                 // "v" caption dropdown
            case kCmdViewFullScreenToggle: toggleFullScreen(); break;
            case kCmdViewFoldall: sci(SCI_FOLDALL, SC_FOLDACTION_CONTRACT); break;
            case kCmdViewUnfoldall: sci(SCI_FOLDALL, SC_FOLDACTION_EXPAND); break;
            case kCmdViewFoldCurrent: foldCurrent(true); break;
            case kCmdViewUnfoldCurrent: foldCurrent(false); break;
            case kCmdViewFold1: case kCmdViewFold2: case kCmdViewFold3: case kCmdViewFold4:
            case kCmdViewFold5: case kCmdViewFold6: case kCmdViewFold7: case kCmdViewFold8:
                foldToLevel((e.GetId() & 0xFFFF) - kCmdViewFold1 + 1, true); break;
            case kCmdViewUnfold1: case kCmdViewUnfold2: case kCmdViewUnfold3: case kCmdViewUnfold4:
            case kCmdViewUnfold5: case kCmdViewUnfold6: case kCmdViewUnfold7: case kCmdViewUnfold8:
                foldToLevel((e.GetId() & 0xFFFF) - kCmdViewUnfold1 + 1, false); break;

            case wxID_ABOUT: case kCmdAboutBase: showAbout(); break;

            case kCmdFilePrint: doPrint(true); break;
            case kCmdFilePrintPreview: doPrintPreview(); break;
            case kCmdFilePrintnow: doPrint(false); break;
            case kCmdViewDocMap: toggleDocMap(); break;
            case kCmdViewFuncList: toggleFuncList(); break;
            case kCmdViewDoclist: toggleDocList(); break;
            case kCmdViewProjectPanel1:
            case kCmdViewProjectPanel2:
            case kCmdViewProjectPanel3: toggleProjectPanel(); break;
            case kCmdEditClipboardHistoryPanel: toggleClipHistory(); break;
            case kCmdEditCharPanel: toggleCharPanel(); break;
            case kCmdViewFilebrowser: toggleFileBrowser(); break;
            case kCmdSearchFindIncrement: showIncBar(); break;
            case kCmdEditColumnmode: columnEditor(); break;
            case kCmdEditColumnModeTip:
                wxMessageBox(_("Column (rectangular) selection:\n\n- Alt + drag the mouse\n- Alt + Shift + Arrow keys\n- Alt + Shift + Click\n\nTyping or pasting applies to every line of the block at once."),
                             _("Column Mode"), wxOK | wxICON_INFORMATION, this);
                break;
            case kCmdViewMonitoring: toggleMonitoring(); break;
            case kCmdMacroStartRecordingMacro: startMacroRecord(); break;
            case kCmdMacroStopRecordingMacro: stopMacroRecord(); break;
            case kCmdMacroPlaybackRecordedMacro: playMacro(m_macro); break;
            case kCmdMacroRunMultiMacroDlg: runMultiple(); break;
            case kCmdMacroSaveCurrentMacro: saveMacro(); break;

            // ---- File: shell integration + file ops ----
            case kCmdFileSavecopyas: saveCopyAs(); break;
            case kCmdFileSavesession: saveSession(); break;
            case kCmdFileLoadsession: loadSession(); break;
            case kCmdFileRename: renameFile(); break;
            case kCmdFileDelete: recycleFile(); break;
            case kCmdFileOpenFolder: revealInFolder(); break;
#ifdef __WXMSW__
            case kCmdFileOpenCmd: openShellHere(false); break;
            case kCmdFileOpenPowershell: openShellHere(true); break;
#endif
            case kCmdFileOpenDefaultViewer: openInDefaultViewer(); break;
            case kCmdFileOpenFolderAsWorkspace: openFolderAsWorkspace(); break;
            case kCmdFileContainingFolderAsWorkspace: containingFolderAsWorkspace(); break;
            case kCmdFileCloseallButPinned: closeAllButPinned(); break;
            case kCmdFileCloseallToleft: closeAllSide(false); break;
            case kCmdFileCloseallToright: closeAllSide(true); break;
            case kCmdFileCloseallUnchanged: closeAllUnchanged(); break;

            // ---- Edit: clipboard paths, case, comments, read-only, sorts, conversions ----
            case kCmdEditFullPathToClip: copyToClip(curPath()); break;
            case kCmdEditFileNameToClip: copyToClip(wxFileNameFromPath(curPath())); break;
            case kCmdEditCurrentDirToClip: copyToClip(wxFileName(curPath()).GetPath()); break;
            case kCmdEditCopyAllNames: copyToClip(allOpenPaths(true)); break;
            case kCmdEditCopyAllPaths: copyToClip(allOpenPaths(false)); break;
            case kCmdEditCopyBinary: copyCutBinary(false); break;
            case kCmdEditCutBinary: copyCutBinary(true); break;
            case kCmdEditPasteBinary: pasteBinary(); break;
            case kCmdEditPasteAsHtml: pasteRichFormat("HTML Format"); break;
            case kCmdEditPasteAsRtf: pasteRichFormat("Rich Text Format"); break;
            case kCmdEditRandomcase: transformSel([](std::string& s){ std::mt19937 g{ std::random_device{}() }; for (char& c : s) if (std::isalpha((unsigned char)c)) c = (char)((g() & 1) ? std::toupper((unsigned char)c) : std::tolower((unsigned char)c)); }); break;
            case kCmdEditPropercaseBlend: transformSel([](std::string& s){ bool st = true; for (char& c : s){ if (std::isalpha((unsigned char)c)){ if (st) c = (char)std::toupper((unsigned char)c); st = false; } else st = true; } }); break;
            case kCmdEditSentenceCaseBlend: transformSel([](std::string& s){ bool st = true; for (char& c : s){ if (std::isalpha((unsigned char)c)){ if (st) c = (char)std::toupper((unsigned char)c); st = false; } else if (c=='.'||c=='!'||c=='?') st = true; } }); break;
            case kCmdEditStreamComment: streamComment(true); break;
            case kCmdEditStreamUncomment: streamComment(false); break;
            case kCmdEditBlockCommentSet: setLineComment(true); break;
            case kCmdEditBlockUncomment: setLineComment(false); break;
            case kCmdEditToggleReadOnly: toggleReadOnly(); break;
            case kCmdEditToggleSystemReadOnly: toggleSystemReadOnly(); break;
            case kCmdEditSetReadOnlyForAllDocs: setReadOnlyAllDocs(true); break;
            case kCmdEditClearReadOnlyForAllDocs: setReadOnlyAllDocs(false); break;
            case kCmdEditBeginEndSelect: toggleBeginEndSelect(false); break;
            case kCmdEditBeginEndSelectColumnmode: toggleBeginEndSelect(true); break;
            case kCmdEditSortlinesLengthAscending: sortLines(3, false); break;
            case kCmdEditSortlinesLengthDescending: sortLines(3, true); break;
            case kCmdEditSortlinesDecimaldotAscending: sortLines(4, false); break;
            case kCmdEditSortlinesDecimaldotDescending: sortLines(4, true); break;
            case kCmdEditSortlinesDecimalCommaAscending: sortLines(5, false); break;
            case kCmdEditSortlinesDecimalCommaDescending: sortLines(5, true); break;
            case kCmdEditSortlinesLocaleAscending: sortLines(0, false); break;
            case kCmdEditSortlinesLocaleDescending: sortLines(0, true); break;
            case kCmdEditSortlinesRandomly: shuffleLines(); break;
            case kCmdEditRemoveEmptyLinesWithBlank: removeEmptyLinesBlank(); break;
            case kCmdEditSw2tabAll: spacesToTabs(false); break;
            case kCmdEditSw2tabLeading: spacesToTabs(true); break;
            case kCmdEditOpenSelectedFileFolderInExplorer: revealInFolder(); break;
            case kCmdEditOpenSelectedFileToEdit: openSelectedFile(); break;
            case kCmdEditSearchOnInternet: searchOnInternet(); break;
            case kCmdEditChangeSearchEngine: changeSearchEngine(); break;
            case kCmdEditRedactSelection: redactSelection(); break;
            case kCmdEditInsertDatetimeCustomized: insertDateTime(true); break;
            case kCmdEditAutoComplete: autoComplete(true); break;               // Function/keyword completion (Ctrl+Space)
            case kCmdEditFunccalltip: funcCallTip(); break;
            case kCmdEditFunccalltipNext:     if (!m_ctSigs.empty()) { ++m_ctIdx; renderCallTip(); } else funcCallTip(); break;
            case kCmdEditFunccalltipPrevious: if (!m_ctSigs.empty()) { --m_ctIdx; renderCallTip(); } else funcCallTip(); break;
            case kCmdEditAutoCompleteCurrentfile: autoComplete(false); break;  // Word completion (document words)
            case kCmdEditAutoCompletePath: autoCompletePath(); break;          // Path completion

            // ---- Search: select-and-find, results nav, bookmarked lines ----
            case kCmdSearchSetAndFindNext: findSel(true, true); break;
            case kCmdSearchSetAndFindPrev: findSel(false, true); break;
            case kCmdSearchVolatileFindnext: findSel(true, true); break;
            case kCmdSearchVolatileFindprev: findSel(false, true); break;
            case kCmdSearchGotoNextFound: stepFoundResult(true); break;
            case kCmdSearchGotoPrevFound: stepFoundResult(false); break;
            case kCmdFocusOnFoundResults: focusFoundResults(); break;
            case kCmdSearchSelectMatchingBraces: selectBetweenBraces(); break;
            case kCmdSearchFindCharInRange: findCharInRange(); break;
            case kCmdSearchMark: onMarkDlg(); break;
            case kCmdSearchMarkallext1: findResult(wxString::Format(_("Marked %d - style 1"), markExt(0, true))); break;
            case kCmdSearchMarkallext2: findResult(wxString::Format(_("Marked %d - style 2"), markExt(1, true))); break;
            case kCmdSearchMarkallext3: findResult(wxString::Format(_("Marked %d - style 3"), markExt(2, true))); break;
            case kCmdSearchMarkallext4: findResult(wxString::Format(_("Marked %d - style 4"), markExt(3, true))); break;
            case kCmdSearchMarkallext5: findResult(wxString::Format(_("Marked %d - style 5"), markExt(4, true))); break;
            case kCmdSearchMarkoneext1: markExt(0, false); break;
            case kCmdSearchMarkoneext2: markExt(1, false); break;
            case kCmdSearchMarkoneext3: markExt(2, false); break;
            case kCmdSearchMarkoneext4: markExt(3, false); break;
            case kCmdSearchMarkoneext5: markExt(4, false); break;
            case kCmdSearchUnmarkAllExt1: unmarkExt(0); break;
            case kCmdSearchUnmarkAllExt2: unmarkExt(1); break;
            case kCmdSearchUnmarkAllExt3: unmarkExt(2); break;
            case kCmdSearchUnmarkAllExt4: unmarkExt(3); break;
            case kCmdSearchUnmarkAllExt5: unmarkExt(4); break;
            case kCmdSearchClearAllMarks: clearAllMarks(); break;
            case kCmdSearchGoNextMarker1: case kCmdSearchGoNextMarker2: case kCmdSearchGoNextMarker3:
            case kCmdSearchGoNextMarker4: case kCmdSearchGoNextMarker5: case kCmdSearchGoNextMarkerDef: jumpMark(true); break;
            case kCmdSearchGoPrevMarker1: case kCmdSearchGoPrevMarker2: case kCmdSearchGoPrevMarker3:
            case kCmdSearchGoPrevMarker4: case kCmdSearchGoPrevMarker5: case kCmdSearchGoPrevMarkerDef: jumpMark(false); break;
            case kCmdSearchStyle1ToClip: copyMarkedToClipboard({ MARK_STYLE_BASE + 0 }); break;
            case kCmdSearchStyle2ToClip: copyMarkedToClipboard({ MARK_STYLE_BASE + 1 }); break;
            case kCmdSearchStyle3ToClip: copyMarkedToClipboard({ MARK_STYLE_BASE + 2 }); break;
            case kCmdSearchStyle4ToClip: copyMarkedToClipboard({ MARK_STYLE_BASE + 3 }); break;
            case kCmdSearchStyle5ToClip: copyMarkedToClipboard({ MARK_STYLE_BASE + 4 }); break;
            case kCmdSearchAllStylesToClip: copyMarkedToClipboard({ MARK_STYLE_BASE, MARK_STYLE_BASE + 1, MARK_STYLE_BASE + 2, MARK_STYLE_BASE + 3, MARK_STYLE_BASE + 4 }); break;
            case kCmdSearchMarkedToClip: copyMarkedToClipboard({ MARK_INDIC }); break;
            case kCmdSearchCopyMarkedLines: bookmarkLinesOp(0); break;
            case kCmdSearchCutMarkedLines: bookmarkLinesOp(1); break;
            case kCmdSearchDeleteMarkedLines: bookmarkLinesOp(2); break;
            case kCmdSearchDeleteUnmarkedLines: bookmarkLinesOp(3); break;
            case kCmdSearchPasteMarkedLines: bookmarkLinesOp(4); break;
            case kCmdSearchInverseMarks: bookmarkLinesOp(5); break;

            // ---- View: always-on-top, wrap symbol, hide lines, summary, browsers, new instance ----
            case kCmdViewAlwaysontop: toggleAlwaysOnTop(); break;
            case kCmdViewWrapSymbol: toggleWrapSymbol(); break;
            case kCmdViewHidelines: hideSelectedLines(); break;
            case kCmdViewSummary: showSummary(); break;
            case kCmdViewInFirefox: openInBrowser("firefox"); break;
            case kCmdViewInChrome: openInBrowser("chrome"); break;
            case kCmdViewInEdge: openInBrowser(""); break;
            case kCmdViewInIe: openInBrowser("iexplore"); break;
            case kCmdViewLoadInNewInstance: case kCmdViewGotoNewInstance: newInstance(true); break;
            case kCmdViewPostit: case kCmdViewDistractionFree: toggleFullScreen(); break;

            // ---- Tools: MD5 / SHA digests ----
            case kCmdToolMd5GenerateIntoClipboard: hashSelection(L"MD5", "MD5", true); break;
            case kCmdToolSha1GenerateIntoClipboard: hashSelection(L"SHA1", "SHA-1", true); break;
            case kCmdToolSha256GenerateIntoClipboard: hashSelection(L"SHA256", "SHA-256", true); break;
            case kCmdToolSha512GenerateIntoClipboard: hashSelection(L"SHA512", "SHA-512", true); break;
            case kCmdToolMd5Generate: hashSelection(L"MD5", "MD5", false); break;
            case kCmdToolSha1Generate: hashSelection(L"SHA1", "SHA-1", false); break;
            case kCmdToolSha256Generate: hashSelection(L"SHA256", "SHA-256", false); break;
            case kCmdToolSha512Generate: hashSelection(L"SHA512", "SHA-512", false); break;
            case kCmdToolMd5GenerateFromFile: hashFiles(L"MD5", "MD5"); break;
            case kCmdToolSha1GenerateFromFile: hashFiles(L"SHA1", "SHA-1"); break;
            case kCmdToolSha256GenerateFromFile: hashFiles(L"SHA256", "SHA-256"); break;
            case kCmdToolSha512GenerateFromFile: hashFiles(L"SHA512", "SHA-512"); break;

            // ---- Window / Settings / Language: list, folders, normal text ----
            case kCmdWindowWindows: showWindowsList(); break;
            case kCmdWindowSortFnAsc: sortTabs(TabSortKey::Name, true); break;
            case kCmdWindowSortFnDsc: sortTabs(TabSortKey::Name, false); break;
            case kCmdWindowSortFpAsc: sortTabs(TabSortKey::Path, true); break;
            case kCmdWindowSortFpDsc: sortTabs(TabSortKey::Path, false); break;
            case kCmdWindowSortFtAsc: sortTabs(TabSortKey::Type, true); break;
            case kCmdWindowSortFtDsc: sortTabs(TabSortKey::Type, false); break;
            case kCmdWindowSortFsAsc: sortTabs(TabSortKey::Size, true); break;
            case kCmdWindowSortFsDsc: sortTabs(TabSortKey::Size, false); break;
            case kCmdWindowSortFdAsc: sortTabs(TabSortKey::Modified, true); break;
            case kCmdWindowSortFdDsc: sortTabs(TabSortKey::Modified, false); break;
            case kCmdSettingPreference: onPreferences(); break;
            case kCmdSettingShortcutMapper: showShortcutMapper(); break;   // 48009: the Shortcut Mapper dialog
            case kCmdLangstyleConfigDlg: onStyleConfig(); break;
            case kCmdSettingImportPlugin: importPlugin(); break;
            case kCmdSettingImportStyleThemes: importStyleTheme(); break;
            case kCmdSettingOpenPluginsDir: {
                // "Open plugins folder" must always land on a real directory - opening a path that
                // doesn't exist just silently does nothing. Create <exe>/plugins on demand; if the exe
                // dir isn't writable (installed build) fall back to the always-present <exe>/nib (where
                // cross-platform Nib plugins live), then the exe dir itself as a last resort.
                const wxString exeDir = wxPathOnly(wxStandardPaths::Get().GetExecutablePath());
#ifdef __WXMSW__
                wxString dir = exeDir + wxFILE_SEP_PATH + "plugins";                 // where npp-bridge loads N++ plugin DLLs
#else
                wxString dir = userDataDir() + wxFILE_SEP_PATH + "plugins";          // where the POSIX npp-bridge loads plugins
#endif
                { wxLogNull noLog; if (!wxDirExists(dir)) wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL); }
                if (!wxDirExists(dir)) dir = exeDir + wxFILE_SEP_PATH + "nib";       // fallback: the always-present Nib plugin dir
                if (!wxDirExists(dir)) dir = exeDir;
                openFolder(dir);
                break;
            }
            case kCmdSettingEditContextMenu: editContextMenu(); break;
            case kCmdLangText: setForcedLang("", _("Normal text file")); break;   // force Normal Text (a manual pick, like the languages)
            case kCmdLangOpenudldir: { wxLogNull noLog; wxFileName::Mkdir(udlDir(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL); openFolder(udlDir()); break; }   // create-then-open the per-user dir

            // ---- Help: external links + info ----
            case kCmdHomeSweetHome: wxLaunchDefaultBrowser("https://github.com/Alpaq92/wx-notepad-plus-plus"); break;
            case kCmdProjectpage: case kCmdUpdateNpp: wxLaunchDefaultBrowser("https://github.com/Alpaq92/wx-notepad-plus-plus/releases"); break;
            case kCmdOnlineDocument: wxLaunchDefaultBrowser("https://alpaq92.github.io/wx-notepad-plus-plus/docs/"); break;   // the user manual on the project site (repo docs/ holds developer notes, not a manual)
            case kCmdForum: wxLaunchDefaultBrowser("https://github.com/Alpaq92/wx-notepad-plus-plus/issues"); break;
            case kCmdDebuginfo: themedInfo(wxString::Format(_("wxNote (experimental)\n\nwxWidgets %d.%d.%d\n%s\n\nExecutable:\n%s"),
                wxMAJOR_VERSION, wxMINOR_VERSION, wxRELEASE_NUMBER, wxGetOsDescription(), wxStandardPaths::Get().GetExecutablePath()), _("Debug Info")); break;
            case kCmdCmdLineArguments: themedInfo(_("Usage: wxnote [options] [files...]\n\nFiles:\n  file                     open in a tab\n  folder                   open as a workspace\n  file:line[:col]          open at a position\n  +N, +N,col               open the last file at line N (and column)\n  +/text                   put the caret on the first match of 'text'\n  -                        read piped input into a new untitled buffer\n\nOptions:\n  -g, --goto <line[,col]>  go to this line (and column) in the last file opened\n  -e, --encoding <name>    force encoding: ansi|utf8|utf8bom|utf16le|utf16be\n  -R, -M, --read-only      open the given file(s) read-only\n  -o, -O, --split          open the given file(s) in a split view\n  -n, --new-instance       always open a new window\n  -r, --reuse-instance     reuse an already-running window\n  -w, --wait               wait for the file to be closed before returning\n  --safe                   start without loading any plugins\n  --clean                  like --safe, plus skip session and recovery restore\n  --locale <lang>          UI language for this run (e.g. pl, de, ja)\n  -v, --version            print the version and exit\n  -h, --help               show this help message"), _("Command Line Arguments")); break;

            case kCmdExecuteBase: onRun(); break;   // Run... (F5)

            case kCmdExecuteValidateShortcutsXml: {   // 49001: "Validate shortcuts.xml"
                // Stay N++-agnostic: the core knows only that SOME plugin may offer a well-known
                // "import shortcuts" command. If one registered "host.shortcuts.import", forward to it;
                // otherwise fall through to notImpl. The optional GPL npp-shortcuts-compat plugin
                // registers that id and does the whole parse+report+import - the core learns nothing
                // about the shortcuts.xml format (generic-indirection option).
                const NibCmd* imp = nullptr;
                for (const NibCmd& nc : g_nibCommands) if (nc.id == "host.shortcuts.import") { imp = &nc; break; }
                if (imp && imp->fn) imp->fn(reinterpret_cast<NibHost*>(g_view), &nibQuery, imp->user);
                else {
                    wxString lbl;
                    if (auto* mb = menuBar()) if (auto* it = mb->FindItem(kCmdExecuteValidateShortcutsXml)) lbl = it->GetItemLabelText();
                    notImpl(lbl);
                }
                break;
            }

            // ---- Encoding: interpret-as (re-decode the file) + convert-to (re-encode on save) ----
            case kCmdFormatAnsi: interpretAs(ENC_ANSI); break;
            case kCmdFormatAsUtf8: interpretAs(ENC_UTF8); break;
            case kCmdFormatUtf8: interpretAs(ENC_UTF8_BOM); break;
            case kCmdFormatUtf16le: interpretAs(ENC_UTF16_LE); break;
            case kCmdFormatUtf16be: interpretAs(ENC_UTF16_BE); break;
            case kCmdFormatConv2Ansi: convertTo(ENC_ANSI); break;
            case kCmdFormatConv2AsUtf8: convertTo(ENC_UTF8); break;
            case kCmdFormatConv2Utf8: convertTo(ENC_UTF8_BOM); break;
            case kCmdFormatConv2Utf16le: convertTo(ENC_UTF16_LE); break;
            case kCmdFormatConv2Utf16be: convertTo(ENC_UTF16_BE); break;

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
        int digits;
        if (m_lineNumWidthMode == 1) {           // CONSTANT policy: a fixed width, independent of line count
            digits = 4;
        } else {                                  // DYNAMIC policy (default): sized to the current line count
            const int lines = static_cast<int>(sci(SCI_GETLINECOUNT));
            digits = 1; for (int n = lines; n >= 10; n /= 10) ++digits;
            if (digits < 2) digits = 2;
        }
        const std::string nines(static_cast<size_t>(digits), '9');
        const int w = static_cast<int>(sci(SCI_TEXTWIDTH, STYLE_LINENUMBER, reinterpret_cast<sptr_t>(nines.c_str())));
        sci(SCI_SETMARGINWIDTHN, 0, w + 10);   // small left pad + a little right pad; right-justified, flush to text
    }
    // A zoom change (Ctrl+wheel / Zoom In-Out) is persisted. With one shared view, zoom is inherently
    // the same for every tab, so there's nothing to mirror (unlike the old per-tab-HWND model).
    void onZoomChanged(int z)
    {
        if (z != m_zoom)
        {
            m_zoom = z;
            updateLineMargin();
            wxConfigBase::Get()->Write("Zoom", static_cast<long>(z)); wxConfigBase::Get()->Flush();
        }
        // Outside the early-out on purpose: typing "103%" at base 10 rounds to the zoom the editor is
        // already at, so no SCN_ZOOM ever fires - without this the field would keep showing a percentage
        // the editor never adopted.
        syncZoomField();
    }
    // ----- status-bar zoom combo ------------------------------------------
    // Scintilla's zoom is a POINT-SIZE DELTA on the default style, not a percentage, so the percent grid
    // is quantised by the base size - and the base is dynamic (11 raw, but every bundled theme declares
    // 10). Always read it at call time; SCI_STYLEGETSIZE returns the UNZOOMED size, so it is safe to ask
    // while zoomed.
    // (non-const throughout: sci() is the editor gateway and isn't const)
    int zoomBase() { const int b = static_cast<int>(sci(SCI_STYLEGETSIZE, STYLE_DEFAULT)); return b > 0 ? b : 11; }
    // Scintilla clamps the RENDERED size at 2pt without touching zoomLevel, so below 2-base the reported
    // zoom stops matching what's on screen and a percent readout would lie.
    int zoomMin() { return wxMax(-10, 2 - zoomBase()); }
    int percentFromZoom(int z)
    { const int b = zoomBase(), s = wxMax(1, b + z); return (100 * s + b / 2) / b; }
    // CRITICAL clamp at +20: SCI_SETZOOM does NOT clamp, but SCI_ZOOMIN only fires 'if (zoomLevel < 20)',
    // so parking the zoom above 20 kills Ctrl+wheel-up and View > Zoom In permanently while zoom-out
    // still works - a confusing half-dead state with no other guard against it.
    int zoomFromPercent(int p)
    {
        const int b = zoomBase();
        const int z = (b * wxMax(1, p) + 50) / 100 - b;
        return wxMax(zoomMin(), wxMin(kZoomMax, z));
    }
    // The canonical presets, re-labelled with the percentage they ACTUALLY produce at this base size and
    // de-duplicated (at base 11 the top entries collapse onto the +20 ceiling). Returning (percent, zoom)
    // pairs means the pick handler can't disagree with the label the user clicked.
    std::vector<std::pair<int, int>> zoomPresets()
    {
        std::vector<std::pair<int, int>> out;
        for (int p : { 50, 75, 100, 125, 150, 175, 200, 250, 300 })
        {
            const int z = zoomFromPercent(p);
            if (!out.empty() && out.back().second == z) continue;
            out.push_back({ percentFromZoom(z), z });
        }
        return out;
    }
    void syncZoomField()
    { if (m_zoomField) m_zoomField->setPercent(percentFromZoom(static_cast<int>(sci(SCI_GETZOOM)))); }
    void applyZoomPercent(int p)
    {
        sci(SCI_SETZOOM, zoomFromPercent(p));
        syncZoomField();   // covers the case where the clamped/rounded zoom equals the current one (no SCN_ZOOM)
    }
    // The preset list, opening UPWARD out of the status bar. This replicates TerminalPanel::openList's
    // toggle/reopen protocol rather than trusting wx to auto-dismiss: a real-click test showed dismissal
    // is unreliable, so a second click on the caret would stack a second popup instead of closing the first.
    void openZoomList()
    {
        if (m_zoomPopup) { m_zoomPopup->closeNow(); return; }   // same anchor -> the caret toggles the list
        // Debounces the mouse race only: wx dismisses on the re-click, then delivers that same click to us.
        if ((wxGetUTCTimeMillis() - m_zoomPopupClosedAt).GetValue() < 250) return;
        const std::vector<std::pair<int, int>> presets = zoomPresets();
        const int live = static_cast<int>(sci(SCI_GETZOOM));
        std::vector<wxString> items; int current = -1;
        for (size_t i = 0; i < presets.size(); ++i)
        {
            items.push_back(wxString::Format("%d%%", presets[i].first));   // a number, not a translatable string
            if (presets[i].second == live) current = static_cast<int>(i);
        }
        auto* pop = new TermListPopup(this, items, current, m_dark, [this, presets](int i) {
            if (i >= 0 && i < static_cast<int>(presets.size())) { sci(SCI_SETZOOM, presets[i].second); syncZoomField(); }
        });
        pop->onClosed = [this]{
            m_zoomPopup = nullptr;
            m_zoomPopupClosedAt = wxGetUTCTimeMillis();
            if (m_stc) m_stc->SetFocus();   // wxPU_CONTAINS_CONTROLS takes real focus; hand it back or the next keystroke goes nowhere
        };
        m_zoomPopup = pop;
        // Right-aligned: the popup's minimum width (FromDIP(130)) is wider than the 78px field, so a
        // left-aligned list would hang off the window's right edge.
        pop->showAbove(m_zoomField, true);
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
        // translate the formats once, not on every 150ms tick (the UI language is fixed per process)
        static const wxString fmtLen(_("length : %d    lines : %d")), fmtPos(_("Ln : %d    Col : %d    Pos : %d")), fmtSel(_("Sel : %d | %d"));
        if (!m_hint) setStatus(0, activePage() ? activePage()->lang : _("Normal text file"));   // language label; hint persists until next command
        setStatus(1, wxString::Format(fmtLen, len, nl));
        setStatus(2, wxString::Format(fmtPos, line, col, pos + 1));
        setStatus(3, wxString::Format(fmtSel, sel, selLines));
        setStatus(4, eolName(eol));
        setStatus(5, encDisplay(activePage()));
        // Nothing is written to field 6 - the ZoomField window covers it, and text underneath would show
        // through wherever the child doesn't fill the field rect exactly (GTK insets differ from MSW).
        // The zoom field is deliberately NOT resynced here either: this runs on the 150ms timer and would
        // stomp characters mid-typing.
        setStatus(7, sci(SCI_GETOVERTYPE) ? "OVR" : "INS");   // typing mode, toggled by the Insert key
    }

    // Gray out toolbar buttons and menu items that don't apply right now (Save when clean, Undo/Redo
    // when there's no history, Cut/Copy/Delete with no selection, Paste with an empty clipboard).
    // Cached so we only touch the UI when a state actually flips.
    void updateUiState()
    {
        if (!m_stc) return;
        const bool dirty    = sci(SCI_GETMODIFY) != 0;
        const bool canUndo  = sci(SCI_CANUNDO) != 0;
        const bool canRedo  = sci(SCI_CANREDO) != 0;
        const bool hasSel   = sci(SCI_GETSELECTIONEMPTY) == 0;
        const bool canPaste = sci(SCI_CANPASTE) != 0;
        const bool hasPath  = activePage() && !activePage()->path.empty();   // untitled buffers have no containing folder / disk copy
        if (auto* ap = activePage()) ap->dirty = dirty;   // keep the active tab's cached flag current (others use their last-active value)
        bool anyDirty = dirty;
        for (wxAuiNotebook* nb : { m_main.tabs, m_sub.tabs })   // Save-All reflects modified docs in BOTH views
            if (nb)
                for (size_t i = 0; i < nb->GetPageCount() && !anyDirty; ++i)
                {
                    auto* p = static_cast<EditorPage*>(nb->GetPage(i));
                    if (p && p->dirty) anyDirty = true;
                }
        if (dirty == m_stSave && anyDirty == m_stSaveAll && canUndo == m_stUndo && canRedo == m_stRedo &&
            hasSel == m_stSel && canPaste == m_stPaste && hasPath == m_stHasPath)
            return;   // nothing changed
        m_stSave = dirty; m_stSaveAll = anyDirty; m_stUndo = canUndo; m_stRedo = canRedo;
        m_stSel = hasSel; m_stPaste = canPaste; m_stHasPath = hasPath;
        if (auto* tb = toolBar())
        {
            tb->EnableTool(kCmdFileSave, dirty);   tb->EnableTool(kCmdFileSaveall, anyDirty);
            tb->EnableTool(kCmdEditUndo, canUndo); tb->EnableTool(kCmdEditRedo, canRedo);
            tb->EnableTool(kCmdEditCut, hasSel);   tb->EnableTool(kCmdEditCopy, hasSel);
            tb->EnableTool(kCmdEditPaste, canPaste);
        }
        if (auto* mb = menuBar())
        {
            mb->Enable(kCmdFileSave, dirty);   mb->Enable(kCmdFileSaveall, anyDirty);
            mb->Enable(kCmdEditUndo, canUndo); mb->Enable(kCmdEditRedo, canRedo);
            mb->Enable(kCmdEditCut, hasSel);   mb->Enable(kCmdEditCopy, hasSel);
            mb->Enable(kCmdEditPaste, canPaste); mb->Enable(kCmdEditDelete, hasSel);
            // Items that TRULY need the document to exist on disk: nothing to reload, nothing a default
            // viewer could open - grey them out instead of a dead click. The Open
            // Containing Folder submenu stays fully enabled: its entries all fall back to the working
            // directory for untitled buffers.
            mb->Enable(kCmdFileOpenDefaultViewer, hasPath);
            mb->Enable(kCmdFileReload, hasPath);
        }
    }

    // ---- two editor views (the MAIN / SUB split; ViewPane defined above the frame) -----
    // m_tabs/m_stc/m_sci are ALIASES to the *active* view, so the ~140 sci()/m_stc/m_tabs sites follow focus.
    ViewPane          m_main, m_sub;            // the two views (sub is empty + hidden until first split)
    ViewPane*         m_active = nullptr;       // -> &m_main or &m_sub
    wxSplitterWindow* m_split  = nullptr;       // center pane: [ main | sub ]
    bool              m_subThemed = false;      // sub view's editor gets the theme on first reveal
    wxAuiNotebook* m_tabs = nullptr;            // alias -> m_active->tabs
    wxPanel*       m_capBar = nullptr;   // +/v/x caption buttons on the tab strip
    FindReplaceDialog* m_findDlg = nullptr;   // modeless Find/Replace dialog
    TerminalPanel*     m_terminal = nullptr;  // View > Show Terminal - bottom multi-tab shell panel (lazy)
    Scope              m_accelScope = Scope::Editor;   // current frame accel-table scope (see refreshAccelerators/onChildFocus)
    KeymapStore        m_keymap;          // effective shortcut set (defaults + shortcuts.json); drives refreshAccelerators (keymap_store.h)
    bool               m_keymapReady = false;   // true once seeded+loaded in buildMenuBar; gates every store read (label rewrite, editor ops)
    std::vector<EditorOp> m_editorLive;   // editor-command overrides currently applied to the persistent STCs; diffed on re-apply so reset restores stock keys
    std::vector<OpenHereTool> m_openFolderTools;   // File > Open Containing Folder's dynamically-detected entries (see terminal_panel.h)
    wxStyledTextCtrl*  m_findResults = nullptr;   // Find-in-Files results panel (docked, bottom)
    wxStyledTextCtrl*  m_fifScratch  = nullptr;   // hidden scratch buffer for searching file contents
    std::vector<std::pair<wxString, int>> m_fifJump;   // results panel line -> (file, 0-based editor line)
    wxFileHistory      m_fileHistory{ recentMaxFromConfig() };   // Recent Files (MRU); max read from config at construction (restart-to-apply)
    MenuRegistry       m_menuRegistry;   // symbolicName -> wxMenu*/bar position/DynamicSlot, built once in buildMenuBar() (see menu_builder.h)
    wxStyledTextCtrl* m_stc = nullptr;   // the cross-platform editor view; its native HWND on Windows == m_sci
    wxStyledTextCtrl* m_docMap = nullptr;   // Document Map (minimap): a second view sharing the active document
    wxTreeCtrl* m_funcList = nullptr;       // Function List: per-file symbol tree (regex-parsed)
    wxTreeCtrl* m_projPanel = nullptr;      // Project Panel: workspace tree (named folders + file refs)
    wxString    m_projWorkspace;            // the loaded/saved workspace .xml path ("" = unsaved)
    wxTreeCtrl* m_fifPanel = nullptr;       // Find result: docked Find-in-Files results tree
    wxTimer*    m_flTimer  = nullptr;        // debounce re-parse of the Function List after edits
    wxTimer*    m_monTimer = nullptr;        // View > Monitoring (tail -f): 1s poll while any tab is monitored
    ZoomField*  m_zoomField = nullptr;       // editable zoom combo parked over status-bar field 6
    TermListPopup* m_zoomPopup = nullptr;    // its live preset list, if open (the caret toggles on it)
    wxLongLong  m_zoomPopupClosedAt = 0;     // reopen guard: wx delivers the dismissing click to us afterwards
    static const int kZoomMax = 20;          // Scintilla's own SCI_ZOOMIN ceiling - see zoomFromPercent
#ifdef __WXMSW__
    HWND        m_sci  = nullptr;
    SizeGripWin* m_grip = nullptr;          // custom dark-themed status-bar resize grip (native one can't theme)
    struct NibDock { HWND hClient; wxPanel* host; wxString name; };   // a plugin's native window hosted in a dock pane
    std::vector<NibDock> m_nibDocks;        // installed via nib.win32 dock_native (the GPL bridge maps NPPM_DMM* to it)
#endif
    wxAuiManager m_aui;                          // hosts the dockable side/bottom panels (Function List, Doc Map, Find results, Nib panels)
#ifdef WXN_HAS_BORDERLESS
    wxPanel*    m_titleBar  = nullptr;            // integrated top bar (icon + menu-buttons + window controls)
    wxMenuBar*  m_menuOwner = nullptr;            // owns the wxMenus the title-bar buttons pop (never attached as a real menu bar)
    wxWindow*   m_maxBtn    = nullptr;            // the maximize/restore button - a wxButton (MSW) or TitleBarBtn (else); its glyph tracks IsMaximized()
    wxWindowGripper* m_gripper = nullptr;         // lib helper for cross-platform window move (drag the bar)
#endif
    wxToolBar*  m_toolBarPtr = nullptr;          // the toolbar (frame's own in native mode, aui-paned in integrated) - see toolBar()
    std::vector<int> m_nibToolIds;               // plugin toolbar buttons (nib.toolbar/1) - torn down before plugin unload
    wxPanel*    m_toolBarHost = nullptr;          // macOS: host panel for the docked non-native child toolbar (see buildToolBar)
#ifdef __WXMAC__
    wxSizerItem* m_macInsetItem = nullptr;        // integrated bar: leading spacer reserving the traffic-light cluster's width
    int          m_macToolbarRowH = 0;            // integrated bar: toolbar row height = the re-centred traffic lights' band height
#endif
    bool        m_integratedBar = false;         // setting: show the integrated top bar (restart-to-apply; read in OnInit)
    int         m_iconStyle = 0;                 // toolbar icon style: 0 = line icons (default), 1 = Solar, 2 = IconPark, 3 = Streamline (restart-to-apply)
    int         m_toolbarIconSize = 16;          // toolbar icon size in px (16/20/24/32, default 16; restart-to-apply)
    wxTimer     m_timer;
    wxString    m_path, m_lastFind, m_lastReplace;
    bool        m_wrap = false, m_ws = false, m_guides = true, m_dark = true;   // guides default ON
    int         m_themeMode = 1;   // Preferences > General "Theme": 0 = System, 1 = Dark, 2 = Light (restart-to-apply)
    bool        m_askBeforeClose = false;   // Preferences > General "Ask before closing unsaved changes" (off by default)
    bool        m_fsAutohideToolbar = false; // Preferences > General "Auto-hide toolbar in full screen" (off by default: toolbar stays)
    bool        m_reuseInstance = false;    // Preferences > General "Reuse an existing window" (restart-to-apply; read in OnInit)
    bool        m_customGutterColor = false;   // Preferences > Editing "Use a custom line-number margin colour"
    long        m_gutterColorValue = 0xE0E0E0;   // Scintilla BGR-packed int (see bgrToColour/colourToBgr); only used when m_customGutterColor
    wxString    m_fontFace = "Cascadia Mono";    // Preferences > Editing "Font"; effectiveFontFace() falls back to this if it's no longer installed
    int         m_tabWidth = 4;                                   // persisted editor preferences (Settings > Preferences)
    bool        m_useTabs = true, m_lineNumbers = true, m_wrapSymbol = false, m_showToolbar = true, m_showStatusbar = true;
    int         m_lineNumWidthMode = 0;   // line-number margin width policy (nib.ui/NPPM_*LINENUMBERWIDTHMODE): 0 = dynamic, 1 = constant
    // Off by default: the zoom combo is a power-user affordance, and Ctrl+wheel / View > Zoom work
    // without it. When off, status-bar field 6 is collapsed to width 0 so no empty slot is left behind.
    bool        m_showZoomField = false;
    bool        m_autocomplete = true;                            // auto word/keyword completion while typing
    bool        m_caretLine = true, m_autoindent = true;          // highlight the current line; auto-indent new lines
    int         m_caretWidth = 1, m_edgeColumn = 0;               // caret thickness (px); long-line marker column (0 = off)
    int         m_defaultEol = SC_EOL_CRLF, m_defaultLangId = -1; // New Document: default line-ending + language (kCmdLang*; -1 = Normal Text)
    int         m_defaultEncoding = ENC_UTF8;                     // New Document: default on-disk encoding (Enc enum)
    int         m_maxRecent = 10;                                // Recent Files: max entries (restart-to-apply; see recentMaxFromConfig)
    bool        m_tabCloseBtn = true;                            // Tab Bar: a close button on each tab (restart-to-apply)
    int         m_caretBlink = 500;                              // caret blink rate (ms; 0 = steady)
    bool        m_scrollBeyond = false, m_multiEdit = true;      // scroll past the last line; multi-selection / multi-caret editing
    int         m_autoCompFrom = 3;                              // auto-completion triggers from the Nth typed character
    bool        m_autoInsertPairs = false;                       // auto-insert matching brackets/quotes while typing
    wxString    m_themeName;                                      // active editor theme (empty = dark/light default); Style Configurator
    std::unique_ptr<scintillua::Engine>        m_scintillua;     // native language engine (embedded Lua+LPeg+Scintillua), lazy-created
    struct SciLang { wxString name, exts; };                     // exts = space-separated file extensions
    std::vector<SciLang>                       m_sciLangs;       // languages registered via nib.langdef
    wxPrintData m_printData;                                      // File > Print: remembers the chosen printer/paper for the session
    bool        m_printing = false;                               // re-entrancy guard around wxPrinter::Print()'s nested modal loop
    wxString    m_printHeader, m_printFooter;                     // Preferences > Print: optional header/footer text (macros resolved at print time)
    std::vector<MacroStep> m_macro;                               // the current recorded macro
    bool        m_recording = false;
    std::vector<std::pair<wxString, std::vector<MacroStep>>> m_savedMacros;   // named macros (Macro menu, this session)
    bool        m_macroSepAdded = false;
    bool        m_hint = false;   // a "needs full app" message is showing in status field 0
    // cached toolbar/menu enable states (start enabled, matching the freshly-built toolbar)
    bool        m_stSave = true, m_stSaveAll = true, m_stUndo = true, m_stRedo = true, m_stSel = true, m_stPaste = true, m_stHasPath = true;
    int         m_newCount = 0;   // counter for "new N" tab titles
    int         m_zoom = 0;       // shared zoom level across all tabs (Ctrl+wheel), persisted
    WxnTheme    m_theme;          // theme colours (loaded from theme XML)
    int         m_searchEngine = 0;                               // Edit > Change Search Engine: 0=DuckDuckGo 1=Google 2=Bing 3=Yahoo
    bool        m_beginEndSelectActive = false, m_beginEndSelectColumnMode = false, m_selExtendGuard = false;
    int         m_beginEndSelectAnchor = -1;                      // Edit > Begin/End Select: the sticky selection anchor
};

// Chrome base, chosen at startup (restart-to-apply): native wxFrame, or - when the "integrated top bar"
// option is on and the platform supports it (Windows + Linux/GTK) - the borderless wxBorderlessFrame.
using WxnShellFrame = WxnShellFrameT<wxFrame>;
#ifdef WXN_HAS_BORDERLESS
using WxnIntegratedFrame = WxnShellFrameT<wxBorderlessFrame>;
#endif

#ifdef __WXMSW__
// wx 3.3 already dark-draws the native menu bar itself (src/msw/darkmode.cpp, via the undocumented UAH
// WM_UAHDRAWMENU/DRAWMENUITEM messages) - so the bar was never the light native one. Its stock dark is
// wxSYS_COLOUR_MENU == (43,43,43), while every other piece of our chrome - caption, toolbar, tab strip,
// status bar - is (32,32,32). Those 11 levels are exactly the lighter band the user sees across the menu
// row. Overriding just the MENU colours here fixes it without touching any Win32 message handling and
// without giving up native menu behaviour (Alt/F10 activation, Alt+mnemonics, arrow traversal, MSAA).
// Only GetMenuColour is overridden - GetColour()/GetBorderPen() keep wx's defaults, so nothing else moves.
class WxnDarkModeSettings : public wxDarkModeSettings
{
public:
    wxColour GetMenuColour(wxMenuColour which) override
    {
        switch (which)
        {
            case wxMenuColour::StandardBg: return wxColour(32, 32, 32);      // == applyTheme's chromeBg
            case wxMenuColour::StandardFg: return wxColour(220, 220, 220);   // == chromeFg (not wx's pure white)
            case wxMenuColour::DisabledFg: return wxColour(109, 109, 109);   // wx's own 0x6d6d6d, still legible on 0x202020
            case wxMenuColour::HotBg:      return wxColour(51, 51, 51);      // subtle hover lift off the darker bar
        }
        return wxDarkModeSettings::GetMenuColour(which);
    }
};
#endif

// `file:line[:col]` on a positional argument - the format every compiler, linter, grep and stack trace
// already emits ("src/main.cpp:9201:9"), so an error location can be pasted straight onto the command
// line. Peels the suffix off `path` and reports it through line/col (left untouched when there is none).
//
// Three guards, each for a concrete way this mangles a real filename:
//  - if `path` exists exactly as given, nothing is stripped at all - a file literally named "notes:12"
//    opens as itself rather than as "notes" at line 12;
//  - otherwise the suffix is only accepted once the REMAINDER exists on disk, so a nonexistent
//    "report:2024" is still passed through whole (and reported missing) instead of becoming "report";
//  - the remainder is never allowed to shrink to a bare drive spec. On Windows the drive colon is the
//    common case, not the exception: without this "C:" would parse as drive "C" plus a line number, and
//    the two-suffix pass would chew "C:\x" down the same way.
static void stripLineColSuffix(wxString& path, int& line, int& col)
{
    if (path.empty() || wxFileExists(path) || wxDirExists(path)) return;
    auto isDriveSpec = [](const wxString& s) { return s.length() == 2 && s[1] == ':' && wxIsalpha(s[0]); };

    wxString head = path;
    long num[2] = { 0, 0 };                    // num[0] = innermost (col when two are present), num[1] = line
    for (int n = 0; n < 2; ++n)                // at most two: ":line" then ":line:col"
    {
        const size_t p = head.rfind(':');
        if (p == wxString::npos || p + 1 >= head.length()) return;
        const wxString tail = head.Mid(p + 1), rest = head.Left(p);
        if (tail.find_first_not_of("0123456789") != wxString::npos) return;   // not a numeric suffix
        if (rest.empty() || isDriveSpec(rest)) return;
        if (!tail.ToLong(&num[n]) || num[n] <= 0) return;                     // ":0" is not a line number
        head = rest;
        if (wxFileExists(head) || wxDirExists(head))
        {
            path = head;
            line = (int)num[n];                                               // the last number peeled is the line
            if (n == 1) col = (int)num[0];                                    // ...and with two, the first was the column
            return;
        }
    }
}

// `wxnote -`: read piped/redirected standard input into memory so OnInit can drop it into a new untitled
// buffer. Returns false (a NO-OP, never a block) whenever stdin is NOT a pipe or a redirected file. That
// gate is load-bearing on Windows: this is a GUI-subsystem exe (add_executable(wxnote WIN32 ...)) with NO
// console attached from a bare `cmd`/PowerShell/Explorer launch, so blindly ReadFile()-ing the input handle
// would hang forever waiting on a console line the user can never type. A real pipe from git/sh
// (`... | wxnote -`) or a redirect (`wxnote - < file`) DOES connect a readable handle - which is exactly
// what FILE_TYPE_PIPE / FILE_TYPE_DISK (POSIX: !isatty) admits, and a console (FILE_TYPE_CHAR / a tty) does not.
static bool readPipedStdin(wxString& out)
{
    // Bound the capture. An editor never needs gigabytes, and an unbounded slurp here is a real hazard:
    // `yes | wxnote -` or a multi-GB `wxnote - < huge.bin` would otherwise buffer without limit (and twice -
    // once as raw bytes, again as the decoded wxString) and, since this runs synchronously in OnInit BEFORE
    // any window is shown, that either exhausts memory or freezes the GUI before it appears. We stop after
    // this many bytes and open what we captured. (A pipe that stays open while trickling little data - e.g.
    // `tail -f log | wxnote -` - is a different failure this cap can't cover; that needs the read moved onto
    // a background thread after the window is up, which is out of scope for these startup-path fixes.)
    static const size_t kMaxStdinBytes = 64u * 1024u * 1024u;
    std::string buf; char chunk[4096];
#ifdef __WXMSW__
    const HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h == nullptr || h == INVALID_HANDLE_VALUE) return false;
    const DWORD ft = GetFileType(h);
    if (ft != FILE_TYPE_PIPE && ft != FILE_TYPE_DISK) return false;   // console/tty or nothing attached - never block
    DWORD n = 0;
    while (buf.size() < kMaxStdinBytes)
    {
        const DWORD want = (DWORD)std::min<size_t>(sizeof(chunk), kMaxStdinBytes - buf.size());
        if (!ReadFile(h, chunk, want, &n, nullptr) || n == 0) break;
        buf.append(chunk, n);
    }
#else
    if (isatty(0)) return false;                                       // interactive terminal - never block
    ssize_t n;
    while (buf.size() < kMaxStdinBytes)
    {
        const size_t want = std::min<size_t>(sizeof(chunk), kMaxStdinBytes - buf.size());
        if ((n = read(0, chunk, want)) <= 0) break;
        buf.append(chunk, (size_t)n);
    }
#endif
    // Decode as UTF-8, but DON'T silently drop the buffer when it isn't valid UTF-8: wxString::FromUTF8
    // returns an EMPTY string the instant ANY byte is invalid - it discards the whole buffer, it does not
    // substitute - so a cp1252 apostrophe (0x92), a latin-1 stream, or a raw UTF-16 blob (leading 0xFF 0xFE)
    // would otherwise yield an empty '(stdin)' tab with every byte of content gone and no error. Fall back
    // to the local 8-bit encoding, then to latin-1 (which maps all 256 byte values and never fails), so the
    // bytes always survive - a garbled-but-present buffer the user can fix via the encoding menu beats a
    // blank one that ate their piped input.
    out = wxString::FromUTF8(buf.data(), buf.size());
    if (out.empty() && !buf.empty()) out = wxString(buf.data(), wxConvLocal, buf.size());
    if (out.empty() && !buf.empty()) out = wxString(buf.data(), wxConvISO8859_1, buf.size());
    return true;
}

class WxnApp : public wxApp
{
public:
    bool OnInit() override
    {
#ifdef __WXMSW__
        // Hidden UAC-elevation helper mode (see writeFile()): a normal, unprivileged wxnote process that hit
        // access-denied writes its buffer to a temp file, then relaunches itself elevated with this switch
        // to do just the copy - no GUI, no locale/theme setup, nothing else ever runs elevated.
        if (argc >= 4 && wxString(argv[1]) == "--elevated-write")
        {
            wxCopyFile(argv[2], argv[3], true);
            return false;   // wx skips the main loop and exits immediately
        }
#endif
        SetAppName("wxNote");             // the identity wxConfig + GetUserDataDir() key everything under

        // ---- --locale pre-scan: raw argv, before the parser exists -------------------------------------
        // Deliberately runs AFTER the --elevated-write block above: that helper must never have its argv
        // reinterpreted by anything.
        //
        // Why a hand-rolled scan instead of just reading parser.Found("locale") below: every help string
        // handed to wxCmdLineParser is _()-wrapped, and Parse() renders the usage text from them. The
        // locale used to be Init()ed hundreds of lines further down, next to the frame construction, so
        // `wxnote --help` printed ENGLISH usage even for a user whose Preferences > Localization is Polish
        // - the catalog simply wasn't installed yet. Resolving --locale here and doing the Init below,
        // still ABOVE the parser, fixes --help for everyone, not just --locale users.
        for (int i = 1; i < argc; ++i)
        {
            wxString arg(argv[i]), val;
            if (arg == "--locale") { if (i + 1 >= argc) break; val = argv[i + 1]; }
            else if (!arg.StartsWith("--locale=", &val)) continue;
            if (val.empty()) break;
            // Accept the language only if we actually ship a catalog for it. wxLocale::FindLanguageInfo()
            // happily resolves e.g. "sv", and Init()ing that would leave every string English while still
            // switching the process's number/date formatting - a stranger result than ignoring the flag.
            if (const wxLanguageInfo* li = wxLocale::FindLanguageInfo(val))
                for (int k = 0; k < (int)WXSIZEOF(UI_LANG_IDS); ++k)
                    if (UI_LANG_IDS[k] == li->Language) { g_localeOverride = li->Language; break; }
            break;                        // first --locale wins; an unshipped one falls back silently
        }

        // ---- -pluginMessage=<text> pre-scan: raw argv (same reasons as --locale) -----------------------
        // Notepad++'s form is single-dash `-pluginMessage="..."`; also accept the wx-idiomatic `--pluginMessage`
        // and a following-arg spelling. Captured here (not the parser) so the exact N++ single-dash long form
        // is honoured regardless of wxCmdLineParser's dash handling; the value is delivered to plugins after
        // they load (see the fire in the frame-construction block below). Registered with the parser too, for
        // --help. The string may hold arbitrary text (a plugin's private protocol), so it is copied verbatim.
        for (int i = 1; i < argc; ++i)
        {
            wxString arg(argv[i]), val;
            if (arg == "-pluginMessage" || arg == "--pluginMessage") { if (i + 1 >= argc) break; g_pluginMessage = argv[i + 1]; break; }
            if (arg.StartsWith("-pluginMessage=", &val) || arg.StartsWith("--pluginMessage=", &val)) { g_pluginMessage = val; break; }
        }

        // ---- positional-token pre-scan: +N / +N,col, +/{pattern} and a lone '-' -----------------------
        // These are vim/less-style POSITIONAL syntax, not switches: '+' is not in SetSwitchChars("-"), and a
        // bare '-' is (per wxCmdLineParser) a parameter, so none of them collide with any -x flag. wx would
        // just funnel them into GetParam() as nonexistent "files"; we consume them HERE (raw argv, like the
        // --locale scan) and skip them in the param loop below. Kept out of the parser because '+' and '-'
        // aren't option syntax it can model.
        //
        // A POSIX '--' ends option/token processing: everything after it is a literal filename, so the one
        // standard escape for a file whose name looks like a token - `wxnote -- +5.txt`, `wxnote -- -` -
        // works. Without this the pre-scan would eat those '+5.txt'/'-' argv entries and the param loop would
        // skip them, so such a file could never be opened at all. wx strips '--' from GetParam() (and treats
        // ALL following args as parameters, in order), so the post-'--' args are exactly the trailing suffix
        // of the param list; we count them here (excluding any further '--', which wx also discards) and mark
        // that suffix "literal" in the param loop below.
        int  plusLine = -1, plusCol = -1;   // +N / +N,col -> fed into the SAME goto path as -g (an explicit -g wins)
        wxString plusFindPattern;           // +/{pattern} -> caret on the first match in the last-opened file
        bool stdinRequested = false;        // '-' -> read piped stdin into a new untitled buffer
        bool afterDoubleDash = false;       // seen a bare '--' -> stop consuming tokens, everything after is a file
        int  postDoubleDashCount = 0;       // non-'--' argv entries after the first '--' == trailing literal params
        for (int i = 1; i < argc; ++i)
        {
            const wxString a(argv[i]);
            if (a == "--") { afterDoubleDash = true; continue; }   // the separator itself is not a file (wx drops it too)
            if (afterDoubleDash) { ++postDoubleDashCount; continue; }   // a literal filename - never a +N/'-'/+/ token
            if (a == "-") { stdinRequested = true; }
            else if (a.StartsWith("+/")) { if (plusFindPattern.empty()) plusFindPattern = a.Mid(2); }   // first +/pat wins
            else if (a.length() > 1 && a[0] == '+' && a[1] >= '0' && a[1] <= '9')
            {
                long l = -1, c = -1;
                const wxString body = a.Mid(1);
                body.BeforeFirst(',').ToLong(&l);
                if (body.Contains(",")) body.AfterFirst(',').ToLong(&c);
                plusLine = (int)l; plusCol = (int)c;   // last +N wins (mirrors wxNote's "last file opened" goto semantics)
            }
        }

        // UI localization (gettext): every _()-wrapped string looks itself up in resources/locale/<lang>/
        // LC_MESSAGES/wxn.mo next to the exe. The language is --locale for this run if it named a shipped
        // catalog, else the user's Preferences > General > Localization choice (wxLANGUAGE_DEFAULT = follow
        // the OS); if no catalog exists for it (e.g. English, which has none), AddCatalog finds nothing and
        // _() falls back to returning its English argument.
        //
        // Must stay ABOVE the parser: see the pre-scan comment. Note this also makes readUiLang() the first
        // wxConfigBase::Get() touch in the process, which used to be the "ReuseInstance" read further down.
        { wxLogNull noWarn;                                     // a chosen language whose OS locale isn't installed still loads our catalog; hush the C-locale warning
          m_locale.Init(g_localeOverride >= 0 ? g_localeOverride : (int)readUiLang()); }   // ignore failure: wx installs the chosen wxTranslations even then, and a second Init() would assert (wx forbids re-Init)
        m_locale.AddCatalogLookupPathPrefix(wxPathOnly(wxStandardPaths::Get().GetExecutablePath()) + wxFILE_SEP_PATH + "locale");
        m_locale.AddCatalog("wxn");

        // ---- command line: -g/--goto, -e/--encoding, -n/--new-instance, -r/--reuse-instance, file... ----
        wxCmdLineParser parser(argc, argv);
        parser.SetSwitchChars("-");                              // don't also accept "/" (ambiguous with paths)
        parser.AddOption("g", "goto", _("open at line[,col]"), wxCMD_LINE_VAL_STRING);
        parser.AddOption("e", "encoding", _("force encoding: ansi|utf8|utf8bom|utf16le|utf16be"), wxCMD_LINE_VAL_STRING);
        // -R/-M/--read-only: open THIS launch's files read-only. -M is a second short form of the same soft
        // read-only (nvim's -M "disallow modifications"); wx has no multi-alias switch, so it's its own entry
        // OR'd in below. nvim's -m (writable buffer, blocked save) has no clean analogue here and is skipped.
        parser.AddSwitch("R", "read-only", _("open the given file(s) read-only"));
        parser.AddSwitch("M", wxString(), _("open the given file(s) read-only"));   // alias for -R (same soft read-only)
        // -o/-O/--split: route THIS launch's files into the split view (see openRequestFiles for the two-view,
        // single-orientation ceiling that makes -o and -O identical).
        parser.AddSwitch("o", "split", _("open the given file(s) in a split view"));
        parser.AddSwitch("O", wxString(), _("open the given file(s) in a split view"));   // alias for -o (one orientation, so identical)
        parser.AddSwitch("n", "new-instance", _("always open a new window"));
        parser.AddSwitch("r", "reuse-instance", _("reuse an already-running window"));
        parser.AddSwitch("w", "wait", _("wait for the file to be closed before returning"));
        parser.AddLongSwitch("safe", _("start without loading any plugins"));   // no short form
        // --clean: --safe (no plugins) PLUS skip session and recovery-backup restore - a pristine launch for
        // bug-repro / catalog screenshots. Distinct from --safe; gates g_cleanMode alongside g_safeMode below.
        parser.AddLongSwitch("clean", _("like --safe, plus skip session and recovery restore"));
        // Registered so it shows up in --help and doesn't trip "unknown option"; the VALUE is read by the
        // raw-argv pre-scan far above, because the catalog has to be installed before this parser is built.
        parser.AddOption("", "locale", _("UI language for this run (e.g. pl, de, ja)"), wxCMD_LINE_VAL_STRING);
        // Registered so it appears in --help and doesn't trip "unknown option"; the VALUE is read by the
        // raw-argv pre-scan above (to honour Notepad++'s single-dash -pluginMessage spelling exactly).
        parser.AddOption("", "pluginMessage", _("message delivered to plugins at startup"), wxCMD_LINE_VAL_STRING);
        parser.AddSwitch("v", "version", _("print the version and exit"));
        // Registered explicitly: without it -h is just an unknown option, so the usage text is preceded by an
        // error. wxCMD_LINE_OPTION_HELP makes Parse() print the usage itself and return -1, which the
        // `parser.Parse() != 0` line below already treats as "done, don't open a window".
        parser.AddSwitch("h", "help", _("show this help message"), wxCMD_LINE_OPTION_HELP);
        parser.AddParam(_("file-or-folder"), wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_MULTIPLE | wxCMD_LINE_PARAM_OPTIONAL);
        if (parser.Parse() != 0) return false;                   // --help/bad args: wx already showed usage

        if (parser.Found("v"))
        {
            // A GUI-subsystem exe (add_executable(wxnote WIN32 ...)) has NO console of its own, so a plain
            // printf() here is written into the void when launched from cmd/PowerShell. wxMessageOutputBest
            // is the portable fix: on MSW it goes through wxGUIAppTraits::CanUseStderr(), which does the
            // AttachConsole(ATTACH_PARENT_PROCESS) + freopen(CONOUT$) dance and writes into the invoking
            // console; only with no parent console at all (Explorer / the Run box) does it fall back to a
            // message box. On GTK/macOS it is plain stderr. Nothing is constructed yet - no window appears.
            wxMessageOutputBest().Printf("wxNote %s", WXN_VERSION);
            std::exit(0);   // NOT `return false`: wxEntry turns that into exit code -1 (255), which breaks
                            // any script doing `wxnote --version && ...`
        }
        g_cleanMode = parser.Found("clean");
        g_safeMode  = parser.Found("safe") || g_cleanMode;   // --clean implies --safe (no plugins), and also skips session/recovery restore

        // Positional arguments split two ways: an existing DIRECTORY roots the workspace browser
        // (`wxnote .`, matching `code .` / `subl .`), everything else is opened as a tab. Until this split
        // existed a directory was simply dropped - openPath() is wxFileExists()-gated, so `wxnote .` was a
        // silent no-op. Mixing works: `wxnote . main.cpp` roots the browser AND opens the file.
        WxnOpenRequest req;
        // Post-'--' params are the trailing suffix of the param list (see the pre-scan): treat them as
        // literal filenames and never run the token-skip heuristic on them, so `wxnote -- +5.txt` opens the
        // file. Guard against a count larger than the param list (multiple '--' etc.) by clamping to 0.
        const size_t paramCount = parser.GetParamCount();
        const size_t literalStart = ((size_t)postDoubleDashCount >= paramCount) ? 0 : paramCount - (size_t)postDoubleDashCount;
        for (size_t i = 0; i < paramCount; ++i)
        {
            wxString raw = parser.GetParam(i);
            // Skip the positional tokens the pre-scan above already consumed - wx delivers them here as
            // "parameters", but they are vim/less syntax, not files: '-' (stdin), '+N'/'+N,col' (goto) and
            // '+/pattern' (find-on-open). The guard is deliberately narrow (only '+' followed by '/' or a
            // digit) so a real file literally named e.g. "+notes" is still opened. Params after a '--' are
            // literal by definition, so the skip is bypassed for them.
            if (i < literalStart)
            {
                if (raw == "-") continue;
                if (raw.length() > 1 && raw[0] == '+' && (raw[1] == '/' || (raw[1] >= '0' && raw[1] <= '9'))) continue;
            }
            int pLine = -1, pCol = -1;
            stripLineColSuffix(raw, pLine, pCol);                 // `main.cpp:100:9` -> main.cpp + 100,9
            wxFileName fn(raw);
            fn.MakeAbsolute();                                    // an IPC handoff runs in a different process's cwd
            const wxString full = fn.GetFullPath();
            // Folders stay OUT of req.paths: that array is what feeds enterWaitMode(), and -w must only
            // ever wait on real tabs (see openPath()'s comment).
            if (wxDirExists(full)) req.folders.Add(full);
            else { req.paths.Add(full); req.lines.Add(pLine); req.cols.Add(pCol); }
        }
        wxString gotoArg, encArg;
        if (parser.Found("g", &gotoArg))
        {
            long l = -1, c = -1;
            gotoArg.BeforeFirst(',').ToLong(&l);
            if (gotoArg.Contains(",")) gotoArg.AfterFirst(',').ToLong(&c);
            req.gotoLine = (int)l; req.gotoCol = (int)c;
        }
        else if (plusLine >= 0) { req.gotoLine = plusLine; req.gotoCol = plusCol; }   // +N goto, but an explicit -g wins
        if (parser.Found("e", &encArg)) req.forceEnc = encodingFromName(encArg);
        req.readOnly    = parser.Found("R") || parser.Found("M");   // -R / -M
        req.split       = parser.Found("o") || parser.Found("O");   // -o / -O / --split
        req.findPattern = plusFindPattern;                          // +/{pattern}
        // '-': read piped stdin now (a NO-OP off a console - see readPipedStdin). Only when it actually
        // captured a stream do we flag it; an empty pipe still opens an empty "(stdin)" buffer, matching
        // `echo -n | wxnote -`. Piped content has no path to forward, so it forces a NEW instance below.
        if (stdinRequested) { wxString s; if (readPipedStdin(s)) { req.hasStdin = true; req.stdinText = s; } }

        // --wait must never take the IPC handoff path below: that process Execute()s and exits immediately,
        // so the caller (git) would unblock before the tab even opened and commit the untouched template.
        // Forcing a new instance makes "block until closed" reduce to "this process stays alive", with no
        // reply channel, no proxy event loop, and no way to hang if the other instance dies. Piped stdin
        // forces a new instance for the same reason: its buffer can't be serialised as a path over IPC.
        // --safe/--clean (g_safeMode covers both) likewise MUST open our own window: their whole purpose is a
        // pristine launch with no plugins (and, for --clean, no session/recovery restore). Handing off to an
        // already-running instance - the default when "Reuse an existing window" is on - would drop the file
        // into a process that already loaded plugins and restored session, i.e. the exact opposite of what
        // the flags ask, silently. So they force new, and an explicit -r can't drag them back into a reuse.
        const bool wait = parser.Found("w");
        const bool forceNew = parser.Found("n") || wait || req.hasStdin || g_safeMode;
        const bool forceReuse = parser.Found("r") && !wait && !req.hasStdin && !g_safeMode;

        // ---- single-instance "reuse window" handoff (Preferences > General; off by default) -----------
        bool reuseSetting = false;
        wxConfigBase::Get()->Read("ReuseInstance", &reuseSetting, false);
        bool startIpcServer = false;
        if (forceReuse || (reuseSetting && !forceNew))
        {
            m_checker = new wxSingleInstanceChecker(wxString::Format("wxnote-%s", wxGetUserId()));
            if (m_checker->IsAnotherRunning())
            {
                wxLogNull noWarn;                                 // a refused/failed connection is handled below
                wxClient client;
                wxConnectionBase* conn = client.MakeConnection(wxEmptyString, kIpcServiceName, kIpcTopic);
                if (conn)
                {
                    wxString payload;
                    for (const auto& f : req.paths) payload += f + "\n";
                    for (const auto& d : req.folders) payload += "\x01" "DIR=" + d + "\n";
                    for (size_t i = 0; i < req.lines.GetCount(); ++i)
                        if (req.lines[i] > 0) payload += wxString::Format("\x01" "FGOTO=%d,%d,%d\n", (int)i, req.lines[i], req.cols[i]);
                    if (req.gotoLine >= 0) payload += wxString::Format("\x01GOTO=%d,%d\n", req.gotoLine, req.gotoCol);
                    if (req.forceEnc >= 0) payload += wxString::Format("\x01" "ENC=%d\n", req.forceEnc);
                    // -R/-M, -o/-O/--split and +/{pattern} must ride along too, or they'd silently work for a
                    // NEW window but be dropped whenever "reuse window" is on (the same class of bug the FGOTO
                    // key was added to fix). "\x01" "FIND=" MUST stay split: 'F' is a hex digit (see OnExec).
                    if (req.readOnly) payload += "\x01" "RO=1\n";
                    if (req.split)    payload += "\x01" "SPLIT=1\n";
                    if (!req.findPattern.empty()) payload += "\x01" "FIND=" + req.findPattern + "\n";
                    // -pluginMessage=<text>: without this it would silently work for a NEW window but be
                    // dropped whenever "reuse window" is on - the exact bug class the comment above
                    // documents for RO/SPLIT/FIND. g_pluginMessage is captured by the raw-argv pre-scan
                    // above (this process never reaches applyRequest(), which is where a new window fires it).
                    if (!g_pluginMessage.empty()) payload += "\x01PLUGINMSG=" + g_pluginMessage + "\n";
                    conn->Execute(payload);
                    conn->Disconnect();
                    delete conn;
                    return false;                                 // handed off to the running instance; done
                }
                // the lock holder isn't answering IPC (e.g. crashed mid-startup) - fall through and open
                // our own window rather than getting stuck; don't start a second server under this name.
            }
            else
            {
                startIpcServer = true;                            // we're first; later launches will find us
            }
        }

        wxImage::AddHandler(new wxPNGHandler());                // needed to load raster icons (iconColored) - SVG icons go through NanoSVG instead and don't need this
        // Register the bundled code font (SIL OFL 1.1 - see resources/fonts/CREDITS.md) so it is rendered
        // identically on every platform, rather than naming a system font: Consolas is Microsoft's
        // (Windows-only, not ours to redistribute), and naming a font that isn't installed silently falls
        // back to an arbitrary system default.
        //
        // wxFont::AddPrivateFont registers for THIS PROCESS ONLY on every port we ship - MSW, GTK (via
        // fontconfig) and macOS (via CoreText) - so no installer or admin rights are needed and nothing is
        // added to the user's system-wide font list. This replaced a raw ::AddFontResourceExW, which was
        // inside an #ifdef __WXMSW__ and therefore left Linux and macOS silently falling back to whatever
        // the system picked for "JetBrains Mono" - i.e. the bundled default was shipped but never used on
        // two of the three platforms. wxLogNull: a font that fails to register just isn't offered, which
        // effectiveFontFace()/the picker already handle; it must not raise a dialog at startup.
        //
        // wxUSE_PRIVATE_FONTS guard: the API is a wx BUILD OPTION, not a given - our Linux CI's static
        // wxGTK compiles with it OFF, where wxFont::AddPrivateFont does not exist at all (0.9.5 CI broke
        // on exactly this). Without it the bundled faces simply aren't registered and the picker's
        // fallback chain applies - same behaviour the pre-AddPrivateFont code had on those platforms.
#if wxUSE_PRIVATE_FONTS
        { wxLogNull noPopup;
          const wxString fontDir = wxPathOnly(wxStandardPaths::Get().GetExecutablePath()) + wxFILE_SEP_PATH + "fonts" + wxFILE_SEP_PATH;
          // Cascadia MONO, not Cascadia Code: Mono is the ligature-free cut, which is the right one here
          // because Scintilla draws through GDI on Windows (no SetTechnology call anywhere in src/) and GDI
          // does no OpenType shaping - so Code's programming ligatures would never render anyway, and Mono
          // is the smaller, honest choice. Regular and Bold both report GDI family "Cascadia Mono" (name
          // ID 1) with subfamilies "Regular"/"Bold", so they pair as one family and bold syntax styles get
          // the REAL Bold face rather than a GDI-synthesised one (which measures ~11% wider and pulls bold
          // tokens out of column alignment - the trap a separate-family weight cut like SemiBold falls into).
          for (const char* f : { "JetBrainsMono-Regular.ttf", "JetBrainsMono-Bold.ttf",
                                 "CascadiaMono-Regular.ttf",  "CascadiaMono-Bold.ttf" })
              wxFont::AddPrivateFont(fontDir + f); }
#endif
        // (UI localization used to be Init()ed here; it moved above the command-line parser so that the
        //  parser's own _()-wrapped help strings - i.e. `wxnote --help` - are translated too.)
        const bool dark = resolveDark(readThemeMode());
#ifdef __WXMSW__
        if (dark)
            // The settings object recolours wx's native dark menu bar to our chrome (32,32,32) so it stops
            // reading as a lighter band between the caption and the toolbar. wx TAKES OWNERSHIP of the
            // pointer (wxApp::MSWEnableDarkMode -> wxDarkModeModule::SetSettings), so it must be heap-
            // allocated and must not be deleted here.
            MSWEnableDarkMode(DarkMode_Always, new WxnDarkModeSettings());   // native dark chrome ONLY in dark mode; light
                                                               // mode never enables it, so it stays fully native-light.
                                                               // MSW-only API; other platforms theme via wx below.
#endif
        // Everything the command line asks of a newly built window, in one place. A generic lambda because
        // WxnShellFrame and WxnIntegratedFrame are two instantiations of the same template with no common
        // base to call through - and because the two construction blocks below are otherwise near-identical
        // twins, where anything added to only one silently does nothing whenever the other is in use
        // (Preferences > Integrated bar decides which, so a miss is invisible on the developer's own setup).
        auto applyRequest = [&](auto* frame) {
            for (const auto& d : req.folders) frame->openFolderPath(d);   // last folder wins: the browser is single-root
            // Files (as tabs or into the split), per-file :line[:col], the launch-wide -g/-e (or +N),
            // -R/-M read-only and +/{pattern} - all shared with the reuse-window path (openRequestFiles).
            const wxArrayString opened = frame->openRequestFiles(req);
            if (req.hasStdin) frame->openStdinBuffer(req.stdinText);       // '-': piped input into a new "(stdin)" buffer
            if (wait) frame->enterWaitMode(opened);   // only wait on tabs that actually opened, or we'd never return
            // -pluginMessage=<text>: hand it to plugins now that they are loaded and the startup files are in
            // (the GPL npp-bridge forwards it as NPPN_CMDLINEPLUGINMSG). --safe (no plugins) makes this inert.
            if (!g_pluginMessage.empty()) nibFireCmdlinePluginMsg(g_pluginMessage);
        };
#ifdef WXN_HAS_BORDERLESS
        // Integrated top bar (Settings > Preferences, restart-to-apply): a borderless frame whose chrome is
        // our own title bar. Only available where the wxBorderlessFrame backend exists (Windows + Linux/GTK).
        bool integrated = false;
        wxConfigBase::Get()->Read("IntegratedBar", &integrated, false);
        if (integrated)
        {
            auto* frame = new WxnIntegratedFrame(dark);
            frame->Show(true);
            if (!wait && !g_cleanMode) frame->restoreSession();   // --clean: no session AND no recovery restore (restoreSession does both)
            if (startIpcServer) { m_ipcServer = new WxnIpcServer(); m_ipcServer->Create(kIpcServiceName); }
            applyRequest(frame);
            return true;
        }
#endif
        auto* frame = new WxnShellFrame(dark);
        frame->Show(true);
        if (!wait && !g_cleanMode) frame->restoreSession();   // reopen files from a theme-restart. -w: a dedicated
                                                // one-file window; leave Session/Pending set so the next real launch
                                                // still restores the user's tabs. --clean: skip session AND recovery
                                                // restore entirely (restoreSession drives both) for a pristine launch
        if (startIpcServer) { m_ipcServer = new WxnIpcServer(); m_ipcServer->Create(kIpcServiceName); }
        applyRequest(frame);
        return true;
    }
    int OnExit() override
    {
        // No font teardown here: the bundled fonts go through wxFont::AddPrivateFont (see OnInit), which
        // has no removal counterpart - the registrations are process-private and die with the process on
        // every platform. The old ::RemoveFontResourceExW pair this replaced was Windows-only anyway.
        delete m_ipcServer;
        delete m_checker;
        // The terminal backends' kill-sweep threads (descendant tree-kill / SIGKILL escalation)
        // must be joined here, after every backend dtor has run and before wx teardown: abandoned
        // at process exit they die mid-walk (re-orphaning the child tree they exist to reap) or
        // race destroyed wx logging state. See term_backend.cpp's g_sweepThreads.
        TermBackend::joinKillSweeps();
        return wxApp::OnExit();
    }

    // wxWidgets compiles its assertions into RELEASE builds too: wxDEBUG_LEVEL defaults to 1
    // regardless of NDEBUG (see wx/debug.h) and our CMake leaves wxBUILD_DEBUG_LEVEL at "Default",
    // so it is never overridden. That means wxMenuItemBase's ctor runs, live on users' machines,
    //     menucmn.cpp:257  wxASSERT_MSG((itemid >= 0 && itemid < SHRT_MAX) || wxID_AUTO range,
    //                                   "invalid itemid value")
    // and - because the default handler ends in wxAppTraitsBase::ShowAssertDialog/DoShowAssertDialog -
    // pops a modal "wxWidgets Debug Alert" MessageBox (plus an OutputDebugString line) EVERY time we
    // append a menu item whose id is >= 32767 (SHRT_MAX). wxNote does that deliberately: these are
    // frozen, ABI-compatibility command ranges that intentionally live above 32767 -
    //     doc-list dropdown   myID_DOCLIST_ITEM   61000+   (core: fires when the tab list is opened)
    //     saved macros        myID_MACRO_ITEM     62100+
    //     Nib plugin cmds     NIB_CMD_BASE        63000+   (appended at startup if a plugin registers)
    //     Scintillua langs    kSciLangMenuBase    63500+   (appended at startup, e.g. via udl-compat)
    //     nib.alloc pool      NIB_ALLOC_CMD_FIRST 64000+
    // The concern the assert actually guards against - MSW truncating ids to 16 bits in WM_COMMAND -
    // is already handled elsewhere: onCommand recovers the real id with `& 0xFFFF`, and native
    // menu/toolbar clicks are redispatched through MSWWindowProc (see the "npp-ids-break-wx-wm-command"
    // design note). The ids must NOT be renumbered (they are frozen for plugin ABI compatibility), and
    // wx offers no sanctioned way to register an out-of-16-bit-range id on a menu item, so we silence
    // exactly THIS one assertion and defer every other wx assertion to the base handler unchanged -
    // in both debug and release - keeping the genuine safety net (and its dialog) fully intact.
    //
    // "invalid itemid value" is used at exactly one site in the whole wxWidgets tree (menucmn.cpp),
    // so matching on the message is precise: it can only ever be our deliberate high menu ids.
    void OnAssertFailure(const wxChar* file, int line, const wxChar* func,
                         const wxChar* cond, const wxChar* msg) override
    {
        if (msg && wxString(msg) == wxS("invalid itemid value"))
            return;   // deliberate frozen command id above SHRT_MAX - not a bug; swallow the false positive
        wxApp::OnAssertFailure(file, line, func, cond, msg);
    }

private:
    wxLocale m_locale;   // must outlive OnInit() - destroying it would revert the process's locale
    wxSingleInstanceChecker* m_checker = nullptr;   // held for the app's lifetime while "reuse window" is active
    WxnIpcServer* m_ipcServer = nullptr;            // ditto - accepts handoffs from later wxnote launches
};

wxIMPLEMENT_APP(WxnApp);
