#pragma once
// term_backend.h - the PTY process backend for the integrated terminal's v2 upgrade: a REAL terminal
// (ConPTY on Windows 10 1809+, forkpty on POSIX) in place of v1's redirected-pipe console
// (terminal_panel.h), so full-screen TUI apps and shells' native line editing become possible. This
// is only the PROCESS side: it spawns the child on a pty, pushes raw VT bytes both ways, and reports
// resize/exit. Turning those bytes into a cell grid (libvterm) is the renderer's job (term_view.h).
// TerminalTab (terminal_panel.h) wires the two together as its PRIMARY path; the v1 pipe console
// remains only as the fallback when spawn() returns nullptr (Windows < 10 1809, or pty failure).
//
// Threading contract: spawn()/write()/resize()/kill()/running() and DESTRUCTION happen on the UI
// thread. The backend reads child output on its own worker thread(s) but always delivers onData /
// onExit ON THE UI THREAD (marshalled via wxTheApp->CallAfter), so the consumer never needs a lock.
//
// Lifetime (the hazard that always bites): a queued CallAfter can fire AFTER this object is
// destroyed - wx delivers it on the next event-loop pass, and destroying the backend cancels
// nothing already queued. The implementation therefore routes every callback through a
// std::shared_ptr'd state block owned by the backend: the queued lambdas capture only a
// std::weak_ptr (plus a back-pointer the destructor nulls first thing) and quietly do nothing once
// either is gone. The destructor also must not leak the child: it hard-kills the process
// group/tree, unblocks its own blocked reader (ClosePseudoConsole on Windows; SIGKILL + a poll
// timeout on POSIX), and joins the worker threads before returning.

#include <wx/string.h>
#include <functional>
#include <memory>

struct TermBackend
{
    // Spawn `cmd` on a fresh cols x rows pty, working directory `cwd` (empty = inherit ours).
    // Returns nullptr on ANY failure - including Windows < 10 1809, where ConPTY's entry points
    // (resolved dynamically at runtime, never import-linked) don't exist - and the caller falls
    // back to the legacy v1 pipe-console path.
    static std::unique_ptr<TermBackend> spawn(const wxString& cmd, const wxString& cwd,
                                              int cols, int rows);
    virtual ~TermBackend();                       // kills the child tree + joins threads; UI thread only

    virtual void write(const char* data, size_t len) = 0;   // bytes -> child stdin (VT-encoded input; never blocks - queued if the pty won't take them now)
    virtual void resize(int cols, int rows) = 0;            // pty size follows the visible grid
    virtual void kill() = 0;                      // hard-stop child + descendants (v1's wxKILL_CHILDREN semantics)
    virtual bool running() const = 0;

    // Join the kill-sweep/escalation threads kill()/teardown spawned (each can block ~500ms per
    // process, so they run off the UI thread and outlive their backend). Call ONCE at app exit,
    // after the last backend is destroyed and before wx teardown: an abandoned sweep is terminated
    // mid-walk by process exit - orphaning the very descendant tree it exists to reap - and one
    // still running during static destruction touches destroyed wx logging state.
    static void joinKillSweeps();

    // Both invoked ON THE UI THREAD (see the threading contract above). Assign them synchronously
    // right after spawn(), before returning to the event loop: delivery is queued via CallAfter, so
    // chunks the reader picks up in the meantime still arrive in order - but a callback that is
    // still unset when its queued event actually runs is silently dropped, not re-queued.
    std::function<void(const char* data, size_t len)> onData;   // raw VT bytes from the child
    std::function<void(int exitCode)>                 onExit;   // at most once, and only for a real child exit (never for backend destruction)
};
