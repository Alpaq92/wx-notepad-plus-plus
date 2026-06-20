// Notepad++ -> wxWidgets  |  Phase-0 de-risking spike
// ---------------------------------------------------------------------------
// Throwaway/experimental. Does NOT touch the Notepad++ application. It exists to
// empirically test the three riskiest bets in docs/WXWIDGETS_MIGRATION_PLAN.md:
//
//   (1) Native Scintilla hosting: the editor below IS the real native Scintilla
//       from our own scintilla static lib (CreateWindowEx(L"Scintilla")), hosted
//       inside a wxFrame -> proves we can keep Scintilla native under wxWidgets
//       instead of adopting wxStyledTextCtrl's bundled engine.
//
//   (2) Plugin-ABI shim mechanism: the wxFrame's real HWND (wxWindow::GetHandle())
//       is subclassed to answer a custom synchronous message -> this is exactly the
//       mechanism a SendMessage(nppData._nppHandle, NPPM_*) shim would use.
//
//   (3) Leaf-dialog fidelity: a "Go to line" dialog reimplemented in wxWidgets
//       that drives the native editor (SCI_GOTOLINE) -> the dialog-migration path.

#include <wx/wx.h>
#include <wx/spinctrl.h>

#include <windows.h>
#include <commctrl.h>   // SetWindowSubclass / DefSubclassProc

#include "Scintilla.h"  // our scintilla lib's public header: SCI_*, Scintilla_RegisterClasses

// A stand-in for an NPPM_* message.
static const UINT WM_SPIKE_PING = WM_APP + 1;

enum SpikeId { ID_GOTO = wxID_HIGHEST + 1, ID_PING };

// (2) The shim: intercept our custom message on the wxFrame's own HWND and answer
// it synchronously, just like a real NPPM_* handler would.
static LRESULT CALLBACK FrameSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                          UINT_PTR /*id*/, DWORD_PTR /*ref*/)
{
    if (msg == WM_SPIKE_PING)
        return static_cast<LRESULT>(wParam) * 2;   // demo semantics: synchronous reply
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// (3) The "Go to line" leaf dialog, in pure wxWidgets.
class GoToLineDialog : public wxDialog
{
public:
    GoToLineDialog(wxWindow* parent, int maxLine, int current)
        : wxDialog(parent, wxID_ANY, "Go to line")
    {
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(new wxStaticText(this, wxID_ANY,
                       wxString::Format("Line number (1 - %d):", maxLine)),
                   0, wxALL, 10);
        m_spin = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                wxDefaultSize, wxSP_ARROW_KEYS, 1, maxLine, current);
        sizer->Add(m_spin, 0, wxALL | wxEXPAND, 10);
        sizer->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALL | wxEXPAND, 10);
        SetSizerAndFit(sizer);
        m_spin->SetFocus();
        m_spin->SetSelection(-1, -1);
    }
    int GetLine() const { return m_spin->GetValue(); }

private:
    wxSpinCtrl* m_spin = nullptr;
};

class SpikeFrame : public wxFrame
{
public:
    SpikeFrame()
        : wxFrame(nullptr, wxID_ANY, "Notepad++ -> wxWidgets  |  Phase-0 spike",
                  wxDefaultPosition, wxSize(960, 640))
    {
        // --- wx chrome around the native editor: menu bar + status bar ---
        auto* menuFile = new wxMenu;
        menuFile->Append(wxID_EXIT);
        auto* menuEdit = new wxMenu;
        menuEdit->Append(ID_GOTO, "&Go to line...\tCtrl-G");
        auto* menuTest = new wxMenu;
        menuTest->Append(ID_PING, "Ping ABI &shim (WM_SPIKE_PING)");
        auto* bar = new wxMenuBar;
        bar->Append(menuFile, "&File");
        bar->Append(menuEdit, "&Edit");
        bar->Append(menuTest, "&Test");
        SetMenuBar(bar);
        CreateStatusBar();
        SetStatusText("Native Scintilla hosted in a wxFrame. Try Edit > Go to line and Test > Ping ABI shim.");

        // --- (1) host the NATIVE Scintilla window inside a wxPanel ---
        m_panel = new wxPanel(this);   // sole child: wxFrame auto-fits it to the client area
        ::Scintilla_RegisterClasses(::GetModuleHandle(nullptr));
        m_sci = ::CreateWindowExW(0, L"Scintilla", L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                  0, 0, 0, 0,
                                  static_cast<HWND>(m_panel->GetHandle()),
                                  reinterpret_cast<HMENU>(1000),
                                  ::GetModuleHandle(nullptr), nullptr);
        if (m_sci)
        {
            sci(SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
            sci(SCI_SETMARGINWIDTHN, 0, 48);
            sci(SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<sptr_t>("Consolas"));
            sci(SCI_STYLESETSIZE, STYLE_DEFAULT, 11);
            sci(SCI_STYLECLEARALL, 0, 0);
            static const char* kSample =
                "// Notepad++ -> wxWidgets Phase-0 spike\r\n"
                "// This editor IS the real native Scintilla (our scintilla lib),\r\n"
                "// created via CreateWindowEx(L\"Scintilla\") and hosted in a wxPanel.\r\n"
                "//\r\n"
                "// Proven here:\r\n"
                "//   1. native Scintilla editing inside a wxFrame\r\n"
                "//   2. the wxFrame HWND answers a custom synchronous message (ABI shim)\r\n"
                "//   3. a wxWidgets 'Go to line' dialog drives the native editor\r\n"
                "\r\n"
                "for (int i = 0; i < 100; ++i)\r\n"
                "{\r\n"
                "    doSomething(i);\r\n"
                "}\r\n";
            sci(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(kSample));
        }

        // keep the native child filling the panel as wx lays things out
        m_panel->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
            if (m_sci)
            {
                const wxSize s = m_panel->GetClientSize();
                ::MoveWindow(m_sci, 0, 0, s.GetWidth(), s.GetHeight(), TRUE);
            }
            e.Skip();
        });

        // --- (2) subclass our own top-level HWND, like the ABI shim would ---
        ::SetWindowSubclass(static_cast<HWND>(GetHandle()), FrameSubclassProc, 1, 0);

        Bind(wxEVT_MENU, &SpikeFrame::OnExit, this, wxID_EXIT);
        Bind(wxEVT_MENU, &SpikeFrame::OnGoTo, this, ID_GOTO);
        Bind(wxEVT_MENU, &SpikeFrame::OnPing, this, ID_PING);
    }

private:
    sptr_t sci(UINT m, uptr_t w, sptr_t l)
    {
        return static_cast<sptr_t>(::SendMessageW(m_sci, m, w, l));
    }

    void OnExit(wxCommandEvent&) { Close(true); }

    void OnGoTo(wxCommandEvent&)
    {
        if (!m_sci) return;
        const int maxLine = static_cast<int>(sci(SCI_GETLINECOUNT, 0, 0));
        const int curPos  = static_cast<int>(sci(SCI_GETCURRENTPOS, 0, 0));
        const int curLine = static_cast<int>(sci(SCI_LINEFROMPOSITION, curPos, 0)) + 1;
        GoToLineDialog dlg(this, maxLine, curLine);
        if (dlg.ShowModal() == wxID_OK)
        {
            sci(SCI_GOTOLINE, dlg.GetLine() - 1, 0);
            ::SetFocus(m_sci);
            SetStatusText(wxString::Format("Go to line: jumped to %d via native SCI_GOTOLINE.", dlg.GetLine()));
        }
    }

    void OnPing(wxCommandEvent&)
    {
        const LRESULT r = ::SendMessageW(static_cast<HWND>(GetHandle()), WM_SPIKE_PING, 21, 0);
        SetStatusText(wxString::Format(
            "ABI shim OK: SendMessage(frameHWND, WM_SPIKE_PING, 21) returned %ld  (this is the NPPM_* mechanism).",
            static_cast<long>(r)));
    }

    wxPanel* m_panel = nullptr;
    HWND m_sci = nullptr;
};

class SpikeApp : public wxApp
{
public:
    bool OnInit() override
    {
        (new SpikeFrame())->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(SpikeApp);
