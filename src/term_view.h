#pragma once
// term_view.h - the RENDERER half of the integrated terminal's v2 upgrade: an owner-drawn cell grid
// over libvterm. term_backend.h owns the child process/pty and pushes raw VT bytes; this widget turns
// those bytes into a screen model (feed -> vterm_input_write), paints the cell grid itself, and turns
// the user's keyboard/mouse back into VT bytes (onOutput -> backend->write). Termin
//
// Threading: everything here is UI-thread only. TermBackend already marshals onData onto the UI
// thread, so feed() runs there; libvterm is not thread-safe and no locking is done (or needed).
//
// Font: the grid assumes a MONOSPACE face. Cell metrics come from one reference glyph ("M") and every
// cell is painted on that fixed pitch, so a proportional face will overlap/gap - callers pass the
// terminal's mono face (Consolas fallback on MSW, wxFONTFAMILY_TELETYPE elsewhere). IME composition
// and complex-script/double-width glyph SHAPING are explicitly out of scope for v2: double-width
// CELLS are honoured (the grid advances two columns), but pretty CJK/emoji rendering is not.
//
// libvterm stays a .cpp-only detail (pimpl): vterm.h must be included before <windows.h> (whose
// rpcndr.h does `#define small char`, mangling vterm.h's `small : 1` bit-field), and keeping it out
// of this header means includers like terminal_panel.h never have to care about that ordering.

#include <wx/window.h>
#include <wx/string.h>
#include <functional>
#include <memory>

class TermView : public wxWindow
{
public:
    TermView(wxWindow* parent, const wxString& fontFace, int fontSize, bool dark);
    ~TermView() override;

    // Raw VT bytes from the child (backend onData) -> vterm. UI thread only.
    void feed(const char* data, size_t len);

    // Swap the palette (default fg/bg + the 16 ANSI colours) and repaint. Cells that referenced the
    // palette by index or by "default" flag recolour retroactively - scrollback included.
    void setDark(bool dark);

    // Recompute cell metrics from the new face/size; the grid (cols x rows) follows on the next
    // repaint and onResizeRequest fires if it changed.
    void setFont(const wxString& face, int pt);

    // vterm's answers to keyboard/mouse/queries - wire straight to backend->write. Assign right
    // after construction: anything typed before it is set is silently dropped.
    std::function<void(const char*, size_t)> onOutput;
    // The widget was resized to a new grid; the caller resizes the PTY to match (backend->resize).
    std::function<void(int cols, int rows)>  onResizeRequest;

    int cols() const;
    int rows() const;

    // FOR TESTS: the LIVE visible screen as text (scrollback and any wheel-back view offset are
    // deliberately ignored so assertions stay deterministic). Rows are right-trimmed.
    wxString rowText(int row) const;
    wxString screenText() const;   // all rows joined with '\n'

private:
    struct Impl;
    std::unique_ptr<Impl> m;

    void onPaint(wxPaintEvent&);
    void onSize(wxSizeEvent&);
    void onKeyDown(wxKeyEvent&);
    void onChar(wxKeyEvent&);
    void onMouseDown(wxMouseEvent&);
    void onMouseUp(wxMouseEvent&);
    void onMotion(wxMouseEvent&);
    void onWheel(wxMouseEvent&);
    void onFocusGain(wxFocusEvent&);
    void onFocusLose(wxFocusEvent&);
    void onCaptureLost(wxMouseCaptureLostEvent&);
};
