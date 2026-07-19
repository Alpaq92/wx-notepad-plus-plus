// term_backend.cpp - PTY process backend behind the integrated terminal's v2 upgrade (contract and
// threading/lifetime rules in term_backend.h). Windows: ConPTY, with the three entry points resolved
// via GetProcAddress so the binary still STARTS on Windows < 10 1809 (an import would fail at load
// time); there spawn() returns nullptr and the caller keeps the v1 pipe console. POSIX: forkpty -
// this path is compiled only by CI (the dev machine is Windows-only), so it deliberately sticks to
// textbook calls with no cleverness to get wrong.

#include "term_backend.h"

#include <wx/app.h>     // wxTheApp->CallAfter - the UI-thread marshalling the contract promises
#include <wx/log.h>     // wxLogNull - silence wxKill's error report on the kill-sweep thread (see killNow)
#include <wx/utils.h>   // wxKill(wxKILL_CHILDREN) - the same tree-kill the v1 pipe console uses

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

TermBackend::~TermBackend() = default;

namespace {

// Shared between the backend object and the CallAfter lambdas its worker threads queue. THE lifetime
// hazard of this file: a chunk marshalled with CallAfter is delivered on a LATER event-loop pass, and
// destroying the backend cancels nothing already queued - so a lambda that captured the backend (or
// its owner window) would run on a dangling pointer whenever a tab closes with output still in
// flight. The UI-thread lambdas therefore capture only a std::weak_ptr of this block and do nothing
// once it has expired (backend destroyed -> threads joined -> last shared_ptr gone). The `owner`
// back-pointer covers the narrower window where the state block is still alive but destruction has
// begun: every destructor nulls it first thing, on the UI thread, so a lambda running between "dtor
// started" and "state freed" also no-ops. (This presumes backends are destroyed on the UI thread -
// true here, the owner is a wxWindow - since only that serialises the dtor against queued lambdas.)
struct BackendState
{
    TermBackend*      owner = nullptr;    // nulled by the dtor before anything else
    std::atomic<bool> running{ true };    // flipped by the exit watcher; running() reads it
    std::atomic<bool> stop{ false };      // destruction in progress: workers drain, deliver nothing
    // POSIX only (unused on MSW, where kills go through a HELD process handle and are immune to
    // pid reuse): serialises the reader's waitpid() against kill()/dtor signalling. A successful
    // waitpid is the instant the kernel may recycle the pid, so a killpg() racing the reap could
    // land on a stranger's fresh process group. Under this mutex - the reap flips `running` in the
    // same critical section - a killer sees either "not yet reaped" (the pid is still ours, alive
    // or zombie, safe to signal) or running==false (skip the signal entirely).
    std::mutex reapMutex;
};

void postData(const std::shared_ptr<BackendState>& st, const char* data, size_t len)
{
    std::string chunk(data, len);   // the reader reuses its buffer immediately - the queued lambda needs its own copy
    std::weak_ptr<BackendState> wk = st;
    if (wxApp* app = wxTheApp)      // null during app teardown - nowhere left to deliver to anyway
        app->CallAfter([wk, chunk = std::move(chunk)]()
        {
            const std::shared_ptr<BackendState> s = wk.lock();
            if (s && s->owner && s->owner->onData)
                s->owner->onData(chunk.data(), chunk.size());
        });
}

void postExit(const std::shared_ptr<BackendState>& st, int code)
{
    std::weak_ptr<BackendState> wk = st;
    if (wxApp* app = wxTheApp)
        app->CallAfter([wk, code]()
        {
            const std::shared_ptr<BackendState> s = wk.lock();
            if (s && s->owner && s->owner->onExit)
                s->owner->onExit(code);
        });
}

// Kill-sweep bookkeeping. The descendant tree-kill (MSW killNow) and the SIGKILL escalation
// (POSIX kill) each run on their own thread because they block up to ~500ms per process - but a
// plain .detach() meant app exit could abandon them mid-walk: process exit terminates the sweep
// before it reaches the grandchildren (re-orphaning the exact `npm run dev` tree it exists to
// reap), and one still alive during CRT static destruction touches destroyed wx logging state
// (wxKill logs via wxLogSysError; wxLogNull toggles per-thread TLS). Threads register here and the
// app joins them ONCE at exit (WxnApp::OnExit / the selftest's OnExit) - after the last backend
// dtor, before wx teardown - via TermBackend::joinKillSweeps().
std::mutex               g_sweepMutex;
std::vector<std::thread> g_sweepThreads;

void registerSweep(std::thread t)
{
    std::lock_guard<std::mutex> lk(g_sweepMutex);
    g_sweepThreads.push_back(std::move(t));
}

} // namespace

void TermBackend::joinKillSweeps()
{
    std::vector<std::thread> sweeps;
    {
        std::lock_guard<std::mutex> lk(g_sweepMutex);
        sweeps.swap(g_sweepThreads);
    }
    for (std::thread& t : sweeps)
        if (t.joinable()) t.join();
}

#ifdef __WXMSW__
// ===================================================================================== Windows ====
#include <windows.h>

namespace {

// ConPTY's exports exist only on Windows 10 1809+. HPCON itself is typedef'd only by 1809+ SDKs, so
// use our own void* alias - it IS a void* underneath - to stay buildable against older kits too.
typedef void* WXN_HPCON;
typedef HRESULT (WINAPI* PFN_CreatePseudoConsole)(COORD, HANDLE, HANDLE, DWORD, WXN_HPCON*);
typedef HRESULT (WINAPI* PFN_ResizePseudoConsole)(WXN_HPCON, COORD);
typedef void    (WINAPI* PFN_ClosePseudoConsole)(WXN_HPCON);

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
    // Value 22 from the 1809 SDK's processthreadsapi.h (ProcThreadAttributePseudoConsole), for SDKs
    // that predate the define - the attribute is consumed by the OS at runtime, not by the SDK.
    #define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE ProcThreadAttributeValue(22, FALSE, TRUE, FALSE)
#endif

struct ConPtyApi
{
    PFN_CreatePseudoConsole create = nullptr;
    PFN_ResizePseudoConsole resize = nullptr;
    PFN_ClosePseudoConsole  close  = nullptr;
    bool ok() const { return create && resize && close; }
};

const ConPtyApi& conPtyApi()
{
    static const ConPtyApi api = []
    {
        ConPtyApi a;
        // GetModuleHandle, not LoadLibrary: kernel32 is mapped into every Win32 process for its
        // whole lifetime, so there is no refcount to hold and nothing to free.
        if (HMODULE k32 = ::GetModuleHandleW(L"kernel32.dll"))
        {
            a.create = reinterpret_cast<PFN_CreatePseudoConsole>(
                           reinterpret_cast<void*>(::GetProcAddress(k32, "CreatePseudoConsole")));
            a.resize = reinterpret_cast<PFN_ResizePseudoConsole>(
                           reinterpret_cast<void*>(::GetProcAddress(k32, "ResizePseudoConsole")));
            a.close  = reinterpret_cast<PFN_ClosePseudoConsole>(
                           reinterpret_cast<void*>(::GetProcAddress(k32, "ClosePseudoConsole")));
        }
        return a;
    }();
    return api;
}

// COORD fields are SHORTs; a renderer bug handing us 0 or a giant grid must not become an integer
// wrap that ConPTY misreads as a nonsense size (CreatePseudoConsole rejects 0 outright).
SHORT clampDim(int v) { return (SHORT)(v < 1 ? 1 : (v > 32767 ? 32767 : v)); }

class ConPtyBackend final : public TermBackend
{
public:
    ConPtyBackend(WXN_HPCON hPC, HANDLE hInWrite, HANDLE hOutRead,
                  LPPROC_THREAD_ATTRIBUTE_LIST attrList, const PROCESS_INFORMATION& pi)
        : m_state(std::make_shared<BackendState>()), m_hPC(hPC), m_hInWrite(hInWrite),
          m_hOutRead(hOutRead), m_attrList(attrList), m_pi(pi)
    {
        m_state->owner = this;

        // Reader: parks in ReadFile until the destructor's ClosePseudoConsole makes conhost exit
        // and break the pipe (nothing else EOFs a ConPTY output pipe - see the waiter below). After
        // the stop flag it keeps DRAINING, just delivering nothing: ClosePseudoConsole flushes
        // pending output and can BLOCK on a full pipe, so a reader that quit at the flag would
        // deadlock the destructor against a chatty child.
        m_reader = std::thread([st = m_state, hOut = hOutRead]
        {
            char buf[4096];
            for (;;)
            {
                DWORD n = 0;
                if (!::ReadFile(hOut, buf, sizeof(buf), &n, nullptr) || n == 0)
                    break;   // broken pipe: conhost is gone (ClosePseudoConsole ran)
                if (!st->stop.load()) postData(st, buf, n);
            }
        });

        // Exit watcher: a ConPTY output pipe does NOT hit EOF when the child exits - conhost holds
        // the write end until ClosePseudoConsole - so unlike the POSIX path the reader cannot
        // detect exit. A second, trivial thread waits on the process handle instead (the pattern
        // Windows Terminal itself uses).
        m_waiter = std::thread([st = m_state, hProc = pi.hProcess]
        {
            ::WaitForSingleObject(hProc, INFINITE);
            DWORD code = 0;
            ::GetExitCodeProcess(hProc, &code);
            st->running.store(false);
            if (!st->stop.load()) postExit(st, (int)code);
        });

        // Writer: WriteFile on the input pipe BLOCKS once the pipe fills - a child that stops
        // reading stdin (`pause`, a long compile) plus a paste bigger than the pipe buffer used to
        // park the UI thread inside write(): no repaint, no WM_CLOSE, the tab unclosable until the
        // child read. The blocking call therefore lives here, and write() only queues + wakes.
        // Teardown unblocks a stuck WriteFile the same way the reader is unblocked: the dtor's
        // ClosePseudoConsole makes conhost exit, which releases its end of the input pipe and
        // fails the WriteFile out. (Capturing `this` is safe: the dtor joins this thread before
        // any member is destroyed.)
        m_writer = std::thread([this]
        {
            for (;;)
            {
                std::string chunk;
                {
                    std::unique_lock<std::mutex> lk(m_wMutex);
                    m_wCv.wait(lk, [this]{ return m_wQuit || !m_wBuf.empty(); });
                    if (m_wQuit) return;   // teardown kills the child anyway - pending input is moot
                    chunk.swap(m_wBuf);
                }
                const char* p    = chunk.data();
                size_t      left = chunk.size();
                while (left > 0)
                {
                    const DWORD want = (DWORD)(left > 0x0FFFFFFFu ? 0x0FFFFFFFu : left);
                    DWORD n = 0;
                    if (!::WriteFile(m_hInWrite, p, want, &n, nullptr) || n == 0)
                        return;   // pipe broken - the child died; drop the input, onExit is on its way
                    p    += n;
                    left -= n;
                }
            }
        });
    }

    ~ConPtyBackend() override
    {
        m_state->stop.store(true);
        m_state->owner = nullptr;   // queued CallAfters no-op from here on (see BackendState)
        if (m_state->running.load())
            killNow();              // don't leak the child; also wakes the waiter immediately
        // Tell the writer to quit. If it is parked on the condvar this alone releases it; if it is
        // parked in a blocked WriteFile, the ClosePseudoConsole below breaks the pipe under it.
        {
            std::lock_guard<std::mutex> lk(m_wMutex);
            m_wQuit = true;
        }
        m_wCv.notify_all();
        // ClosePseudoConsole BEFORE the final handle cleanup, per Microsoft's EchoCon sample order.
        // This is also what unblocks the reader: conhost exits, the output pipe breaks, ReadFile
        // returns. It must run while the reader is still draining (see the ctor's deadlock note).
        conPtyApi().close(m_hPC);
        if (m_reader.joinable()) m_reader.join();
        if (m_waiter.joinable()) m_waiter.join();
        if (m_writer.joinable()) m_writer.join();
        // Now nothing can touch the handles: process info + attribute list, then the pipes last.
        ::CloseHandle(m_pi.hThread);
        ::CloseHandle(m_pi.hProcess);
        ::DeleteProcThreadAttributeList(m_attrList);
        ::HeapFree(::GetProcessHeap(), 0, m_attrList);
        ::CloseHandle(m_hInWrite);
        ::CloseHandle(m_hOutRead);
    }

    void write(const char* data, size_t len) override
    {
        // Queue-and-wake only, never WriteFile: this runs on the UI thread, and a full input pipe
        // (child not reading stdin) turns WriteFile into an indefinite block - which froze the
        // whole app, tab close and exit included, until the child deigned to read. The writer
        // thread (see the ctor) absorbs that block instead.
        if (len == 0) return;
        {
            std::lock_guard<std::mutex> lk(m_wMutex);
            m_wBuf.append(data, len);
        }
        m_wCv.notify_one();
    }

    void resize(int cols, int rows) override
    {
        COORD sz;
        sz.X = clampDim(cols);
        sz.Y = clampDim(rows);
        conPtyApi().resize(m_hPC, sz);
    }

    void kill() override
    {
        if (m_state->running.load()) killNow();
    }

    bool running() const override { return m_state->running.load(); }

private:
    void killNow()
    {
        // Two-pronged, matching the v1 console's shutdown(): wxKill's recursive wxKILL_CHILDREN
        // sweep catches the DESCENDANTS (an `npm run dev` must not outlive its tab as an orphan),
        // but on a detached thread - it blocks up to 500ms per live process in the tree, which
        // froze the UI when run inline (terminal_panel.h tells that story). TerminateProcess on the
        // direct child stays INLINE so the exit watcher wakes right now and the destructor's joins
        // never sit out the tree walk. Exit code 9 = wxSIGKILL, the code wxKill's own
        // TerminateProcess reports, so the observed exit code doesn't depend on which call wins.
        const long pid = (long)m_pi.dwProcessId;
        // The sweep targets pids, and the dtor closes our process handle milliseconds after this
        // returns - once conhost's reference is gone too, the dead child's pid is free for reuse
        // while wxKill is still OpenProcess()ing it (up to ~500ms), so a recycled pid could get an
        // unrelated process TerminateProcess'd. Holding a duplicated handle across the sweep pins
        // the process OBJECT, and Windows never recycles a pid while its object lives - the root
        // pid stays ours for the sweep's whole walk. Registered, not detached: see g_sweepThreads.
        HANDLE pin = nullptr;
        ::DuplicateHandle(::GetCurrentProcess(), m_pi.hProcess, ::GetCurrentProcess(),
                          &pin, 0, FALSE, DUPLICATE_SAME_ACCESS);
        registerSweep(std::thread([pid, pin]
        {
            {
                // The inline TerminateProcess below usually wins this race, so wxKill's sweep finds
                // the direct child already dead and its own TerminateProcess fails with
                // ERROR_ACCESS_DENIED - which wxKill reports via wxLogSysError even when handed a
                // result pointer (msw/utils.cpp). In the GUI app that surfaced as a modal "Failed
                // to kill process N (error 5)" dialog BLOCKING app exit whenever a terminal tab was
                // open at close time. wxLogNull is per-thread off the main thread
                // (wxLog::EnableLogging routes to EnableThreadLogging), so it silences exactly this
                // sweep and no other logging.
                wxLogNull noLog;
                wxKill(pid, wxSIGKILL, nullptr, wxKILL_CHILDREN);
            }
            if (pin) ::CloseHandle(pin);
        }));
        ::TerminateProcess(m_pi.hProcess, 9);
    }

    std::shared_ptr<BackendState> m_state;
    WXN_HPCON                     m_hPC;
    HANDLE                        m_hInWrite;   // our write end -> child stdin
    HANDLE                        m_hOutRead;   // our read end <- child stdout+stderr (merged by conhost)
    LPPROC_THREAD_ATTRIBUTE_LIST  m_attrList;   // freed in the dtor, sample-style (safe either way post-CreateProcess)
    PROCESS_INFORMATION           m_pi;
    std::thread                   m_reader, m_waiter, m_writer;
    std::mutex                    m_wMutex;         // guards m_wBuf/m_wQuit (UI thread vs writer thread)
    std::condition_variable       m_wCv;
    std::string                   m_wBuf;           // outbound bytes the writer has not pushed yet
    bool                          m_wQuit = false;  // dtor tells the writer to stop taking work
};

} // namespace

std::unique_ptr<TermBackend> TermBackend::spawn(const wxString& cmd, const wxString& cwd,
                                                int cols, int rows)
{
    const ConPtyApi& api = conPtyApi();
    if (!api.ok() || cmd.empty())
        return nullptr;   // Windows < 10 1809 - caller falls back to the v1 pipe console

    // Pipes exactly as the canonical sample's CreatePseudoConsoleAndPipes: conhost gets the read
    // end of "in" and the write end of "out"; we keep the opposite two.
    HANDLE hPtyIn = nullptr, hInWrite = nullptr, hOutRead = nullptr, hPtyOut = nullptr;
    if (!::CreatePipe(&hPtyIn, &hInWrite, nullptr, 0))
        return nullptr;
    if (!::CreatePipe(&hOutRead, &hPtyOut, nullptr, 0))
    {
        ::CloseHandle(hPtyIn);
        ::CloseHandle(hInWrite);
        return nullptr;
    }

    COORD size;
    size.X = clampDim(cols);
    size.Y = clampDim(rows);
    WXN_HPCON hPC = nullptr;
    if (FAILED(api.create(size, hPtyIn, hPtyOut, 0, &hPC)))
    {
        ::CloseHandle(hPtyIn);
        ::CloseHandle(hPtyOut);
        ::CloseHandle(hInWrite);
        ::CloseHandle(hOutRead);
        return nullptr;
    }

    // Attribute-list sizing per the sample: the first call fails BY DESIGN
    // (ERROR_INSUFFICIENT_BUFFER) and reports the byte count to allocate.
    SIZE_T attrSize = 0;
    ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    auto attrList = (LPPROC_THREAD_ATTRIBUTE_LIST)::HeapAlloc(::GetProcessHeap(), 0, attrSize);
    bool attrInited = attrList && ::InitializeProcThreadAttributeList(attrList, 1, 0, &attrSize);
    const bool attrOk = attrInited &&
        ::UpdateProcThreadAttribute(attrList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                    hPC, sizeof(hPC), nullptr, nullptr);
    if (!attrOk)
    {
        if (attrInited) ::DeleteProcThreadAttributeList(attrList);
        if (attrList)   ::HeapFree(::GetProcessHeap(), 0, attrList);
        api.close(hPC);
        ::CloseHandle(hPtyIn);
        ::CloseHandle(hPtyOut);
        ::CloseHandle(hInWrite);
        ::CloseHandle(hOutRead);
        return nullptr;
    }

    STARTUPINFOEXW si;
    ZeroMemory(&si, sizeof(si));
    si.StartupInfo.cb = sizeof(si);   // sizeof(STARTUPINFOEX), not STARTUPINFO - the extended struct is what carries the attribute list
    si.lpAttributeList = attrList;
    // STARTF_USESTDHANDLES with explicit NULLs: when the SPAWNING process itself owns a console
    // (terminal_selftest is a console app; wxnote run from a shell can be too), the child binds its
    // std handles to that REAL console instead of the pseudoconsole - observed directly: cmd.exe's
    // prompt printed into the selftest's own console window while the pty delivered only conhost's
    // 116-byte init/title stream, and input written to the pty was never read. The pseudoconsole
    // attribute alone does NOT prevent that inheritance. NULL std handles force the console
    // subsystem to re-bind them to the attached pseudoconsole (the same pattern neovim's
    // pty_process_win.c uses); with no console in the parent this is a no-op, so the GUI app is
    // unaffected either way.
    si.StartupInfo.dwFlags    = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput  = nullptr;
    si.StartupInfo.hStdOutput = nullptr;
    si.StartupInfo.hStdError  = nullptr;

    std::wstring cmdBuf = cmd.ToStdWstring();   // CreateProcessW may WRITE into lpCommandLine - never pass c_str()
    const std::wstring cwdBuf = cwd.ToStdWstring();

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    // bInheritHandles FALSE, per the sample: the child's stdio arrives through the pseudoconsole
    // attribute (conhost owns the pipe ends), not through classic handle inheritance.
    if (!::CreateProcessW(nullptr, &cmdBuf[0], nullptr, nullptr, FALSE,
                          EXTENDED_STARTUPINFO_PRESENT, nullptr,
                          cwdBuf.empty() ? nullptr : cwdBuf.c_str(),
                          &si.StartupInfo, &pi))
    {
        ::DeleteProcThreadAttributeList(attrList);
        ::HeapFree(::GetProcessHeap(), 0, attrList);
        api.close(hPC);
        ::CloseHandle(hPtyIn);
        ::CloseHandle(hPtyOut);
        ::CloseHandle(hInWrite);
        ::CloseHandle(hOutRead);
        return nullptr;
    }

    // The pty-side pipe ends now belong to conhost (CreatePseudoConsole dup'ed them); closing our
    // copies here - after CreateProcess, in the parent - is what lets the pipes actually break
    // (and the reader unblock) once ClosePseudoConsole shuts conhost down.
    ::CloseHandle(hPtyIn);
    ::CloseHandle(hPtyOut);

    return std::unique_ptr<TermBackend>(new ConPtyBackend(hPC, hInWrite, hOutRead, attrList, pi));
}

#else
// ======================================================================================= POSIX ====
#ifdef __APPLE__
    #include <util.h>       // forkpty lives here on macOS/BSD
#else
    #include <pty.h>        // forkpty on Linux (glibc < 2.34: link libutil; see CMakeLists.txt)
#endif
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

extern char** environ;   // POSIX guarantees it exists, but no header is required to declare it

namespace {

class ForkPtyBackend final : public TermBackend
{
public:
    ForkPtyBackend(int master, pid_t pid, int keepSlave)
        : m_state(std::make_shared<BackendState>()), m_master(master), m_pid(pid),
          m_keepSlave(keepSlave)
    {
        m_state->owner = this;

        // One worker does everything here: drains the master both ways and reaps the child.
        // poll() with a timeout rather than a bare blocking read(), deliberately: close()ing an fd
        // does NOT unblock a read() already parked on it (the classic POSIX trap), so a blocking
        // reader could park the destructor's join() forever if some escaped descendant (a setsid'd
        // daemon) kept the slave open after the group was SIGKILLed. The timeout bounds that: the
        // stop flag is rechecked every 250ms no matter what the pty does.
        //
        // Exit detection is waitpid(WNOHANG) on the quiet tick, NOT master-EOF: EOF arrives only
        // when EVERY slave fd is closed, so `sleep 300 &` + `exit` would keep the pty open and the
        // shell's death would never be noticed - no exit banner, running() true forever, the shell
        // a zombie (the MSW twin has a process-handle waiter for exactly this; EOF/EIO still ends
        // the loop early where it does happen). It is also what makes the macOS parent-held slave
        // fd workable at all (see spawn) - with it, EOF never arrives by design.
        // (Capturing `this` is safe: the dtor joins this thread before any member is destroyed.)
        m_reader = std::thread([this, st = m_state, fd = master, pid]
        {
            char buf[4096];
            int  status = 0;
            bool reaped = false;
            for (;;)
            {
                if (st->stop.load()) break;
                struct pollfd pfd;
                pfd.fd = fd; pfd.events = POLLIN; pfd.revents = 0;
                {
                    // Ask for POLLOUT only while outbound bytes are queued: an idle pty is almost
                    // always writable, so polling for it unconditionally would busy-spin.
                    std::lock_guard<std::mutex> lk(m_outMutex);
                    if (!m_outBuf.empty()) pfd.events |= POLLOUT;
                }
                const int pr = ::poll(&pfd, 1, 250);
                if (pr < 0) { if (errno == EINTR) continue; break; }
                if (pr == 0)
                {
                    // Quiet tick: probe for child exit. WNOHANG under reapMutex, so nothing ever
                    // blocks while it is held and the reap + running flip are atomic against
                    // kill()/dtor signalling (see BackendState::reapMutex).
                    std::lock_guard<std::mutex> lk(st->reapMutex);
                    pid_t r;
                    do { r = ::waitpid(pid, &status, WNOHANG); } while (r < 0 && errno == EINTR);
                    if (r == pid)
                    {
                        reaped = true;
                        st->running.store(false);
                        break;   // the final drain below still collects queued output
                    }
                    continue;
                }
                if (pfd.revents & POLLOUT)
                {
                    // Push the queued input write() could not place inline. The fd is O_NONBLOCK,
                    // so a short/EAGAIN write just leaves the remainder for the next round.
                    std::lock_guard<std::mutex> lk(m_outMutex);
                    if (!m_outBuf.empty())
                    {
                        const ssize_t n = ::write(fd, m_outBuf.data(), m_outBuf.size());
                        if (n > 0)
                            m_outBuf.erase(0, (size_t)n);
                        else if (n < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
                            m_outBuf.clear();   // EIO: child gone - drop, exit reporting is on its way
                    }
                }
                if (pfd.revents & POLLIN)
                {
                    const ssize_t n = ::read(fd, buf, sizeof(buf));
                    // EAGAIN can follow POLLIN on the (now non-blocking) fd - treat it like EINTR,
                    // not like EOF, or a spurious wakeup would end the session early.
                    if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
                    if (n <= 0) break;   // EOF/EIO: every slave fd is gone
                    if (!st->stop.load()) postData(st, buf, (size_t)n);
                    // POLLHUP often arrives TOGETHER with POLLIN; keep reading until read() itself
                    // says EOF, or the child's final burst of output would be dropped.
                    continue;
                }
                if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) break;
            }
            if (reaped)
            {
                // Reaped while the pty is still open: one zero-timeout drain so the shell's last
                // output (logout message, tail of a fast script) lands on screen before the banner.
                for (;;)
                {
                    struct pollfd d;
                    d.fd = fd; d.events = POLLIN; d.revents = 0;
                    if (::poll(&d, 1, 0) <= 0 || !(d.revents & POLLIN)) break;
                    const ssize_t n = ::read(fd, buf, sizeof(buf));
                    if (n <= 0) break;
                    if (!st->stop.load()) postData(st, buf, (size_t)n);
                }
            }
            else
            {
                // EOF/stop path. Deliberately NOT a blocking waitpid: EOF only proves the slave
                // fds are gone, not that the child exited (a daemonising child can close its stdio
                // and live on), and blocking while holding reapMutex would deadlock the dtor's
                // killpg - while blocking WITHOUT the lock reopens the reap-vs-signal race. So:
                // WNOHANG probes, each atomic-with-flip under the lock, 10ms apart. Every normal
                // path reaps on the first probe (EOF = group exited; stop = the dtor SIGKILLed
                // before setting the flag).
                for (;;)
                {
                    bool done = false;
                    {
                        std::lock_guard<std::mutex> lk(st->reapMutex);
                        pid_t r;
                        do { r = ::waitpid(pid, &status, WNOHANG); } while (r < 0 && errno == EINTR);
                        if (r == pid) { reaped = true; st->running.store(false); done = true; }
                        else if (r < 0) done = true;   // ECHILD/other: nothing to reap (nobody else reaps this pid)
                    }
                    if (done) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
            if (!reaped)
            {
                // waitpid errored out: nothing else will ever flip running(), so do it here.
                std::lock_guard<std::mutex> lk(st->reapMutex);
                st->running.store(false);
            }
            // 128+sig: the shell convention. An unreaped exit reports -1 ("unknown") instead of
            // decoding a status waitpid never wrote - the old code turned an EINTR'd waitpid into
            // a fabricated "exit code 0" by decoding the untouched zero.
            const int code = !reaped              ? -1
                           : WIFEXITED(status)    ? WEXITSTATUS(status)
                           : WIFSIGNALED(status)  ? 128 + WTERMSIG(status)
                                                  : -1;
            if (!st->stop.load()) postExit(st, code);
        });
    }

    ~ForkPtyBackend() override
    {
        m_state->owner = nullptr;   // queued CallAfters no-op from here on (see BackendState)
        {
            // Signal BEFORE setting stop, and under reapMutex. The lock closes the reap-vs-signal
            // race (a pid freed by the reader's waitpid microseconds ago could already be a
            // stranger's fresh process group - see BackendState::reapMutex); the ordering
            // guarantees the reader's stop-path reap probe only ever waits on a child that has
            // already been SIGKILLed, so the join below returns promptly.
            std::lock_guard<std::mutex> lk(m_state->reapMutex);
            if (m_state->running.load())
                ::killpg(m_pid, SIGKILL);   // teardown is immediate - matches the v1 shutdown()'s straight wxSIGKILL, no HUP grace
        }
        m_state->stop.store(true);
        if (m_reader.joinable()) m_reader.join();
        ::close(m_master);   // AFTER the join: close() wouldn't have unblocked the reader anyway (see the ctor)
        if (m_keepSlave >= 0) ::close(m_keepSlave);   // macOS: release the parent-held slave (see spawn)
    }

    void write(const char* data, size_t len) override
    {
        // Never a blocking write on the UI thread: a foreground child that stops reading stdin
        // plus a paste larger than the kernel tty input queue (~4KB) used to park the old retry
        // loop here indefinitely - no repaint, no tab close, no app exit. The master is O_NONBLOCK
        // (see spawn): push what the pty will take inline (zero-latency typing), queue the rest
        // for the reader thread's POLLOUT drain.
        if (len == 0) return;
        std::lock_guard<std::mutex> lk(m_outMutex);
        if (m_outBuf.empty())   // never write inline PAST queued bytes - that would reorder input
        {
            while (len > 0)
            {
                const ssize_t n = ::write(m_master, data, len);
                if (n > 0) { data += n; len -= (size_t)n; continue; }
                if (n < 0 && errno == EINTR) continue;
                break;   // EAGAIN: input queue full - queue the rest; EIO: child gone - the queued copy is moot but harmless
            }
            if (len == 0) return;
        }
        m_outBuf.append(data, len);
    }

    void resize(int cols, int rows) override
    {
        struct winsize ws;
        ws.ws_col    = (unsigned short)(cols < 1 ? 1 : (cols > 65535 ? 65535 : cols));
        ws.ws_row    = (unsigned short)(rows < 1 ? 1 : (rows > 65535 ? 65535 : rows));
        ws.ws_xpixel = 0;
        ws.ws_ypixel = 0;
        // TIOCSWINSZ on the master is the whole job - the kernel raises SIGWINCH in the pty's
        // foreground process group itself; sending one by hand would double-signal.
        ::ioctl(m_master, TIOCSWINSZ, &ws);
    }

    void kill() override
    {
        const pid_t pid = m_pid;
        {
            // Under reapMutex: the signal must not race the reader's reap - a reaped pid is free
            // for kernel reuse, and a killpg aimed at it could hit a stranger's fresh process
            // group (see BackendState::reapMutex).
            std::lock_guard<std::mutex> lk(m_state->reapMutex);
            if (!m_state->running.load()) return;
            ::killpg(pid, SIGHUP);   // what a closing terminal window sends - lets shells save history
        }
        // Escalate after a grace period (500ms - the same wait wxKill's MSW tree-kill grants each
        // process), giving the whole group the hard stop wxKILL_CHILDREN gave the v1 console.
        // Registered, not detached: app exit joins it (joinKillSweeps), so the escalation can
        // neither be abandoned mid-kill nor run during static teardown. Re-checked against
        // `running` UNDER THE REAP LOCK: if the HUP was honoured and the reader already reaped,
        // the pid number may have been RECYCLED by the OS by now - SIGKILLing it blind could hit a
        // stranger's process group.
        registerSweep(std::thread([pid, wk = std::weak_ptr<BackendState>(m_state)]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            const std::shared_ptr<BackendState> st = wk.lock();
            if (!st) return;
            std::lock_guard<std::mutex> lk(st->reapMutex);
            if (st->running.load())
                ::killpg(pid, SIGKILL);
        }));
    }

    bool running() const override { return m_state->running.load(); }

private:
    std::shared_ptr<BackendState> m_state;
    int         m_master;
    pid_t       m_pid;
    int         m_keepSlave;    // macOS: parent-held slave fd; -1 elsewhere (or if the open failed)
    std::mutex  m_outMutex;     // guards m_outBuf (UI-thread write() vs reader-thread drain)
    std::string m_outBuf;       // outbound bytes the pty would not accept inline yet
    std::thread m_reader;
};

} // namespace

std::unique_ptr<TermBackend> TermBackend::spawn(const wxString& cmd, const wxString& cwd,
                                                int cols, int rows)
{
    if (cmd.empty())
        return nullptr;

    struct winsize ws;
    ws.ws_col    = (unsigned short)(cols < 1 ? 1 : (cols > 65535 ? 65535 : cols));
    ws.ws_row    = (unsigned short)(rows < 1 ? 1 : (rows > 65535 ? 65535 : rows));
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    // EVERYTHING the child needs - byte buffers, argv, envp - is materialised BEFORE forkpty: the
    // child of a multithreaded fork may only rely on async-signal-safe calls, and another thread
    // could hold the heap (or libc environ) lock at fork time, deadlocking any child-side
    // allocation forever. That rules out setenv (grows/copies environ via malloc) and execl
    // (varargs marshalling; not on POSIX's async-signal-safe list) as much as it rules out
    // wxString conversion - so TERM is spliced into a parent-built envp and handed to execve.
    const wxScopedCharBuffer cmdU = cmd.utf8_str();
    const wxScopedCharBuffer cwdU = cwd.utf8_str();

    std::vector<std::string> envStore;
    bool sawTerm = false;
    for (char** e = environ; e && *e; ++e)
    {
        if (std::strncmp(*e, "TERM=", 5) == 0) { envStore.push_back("TERM=xterm-256color"); sawTerm = true; }
        else                                     envStore.push_back(*e);
    }
    if (!sawTerm) envStore.push_back("TERM=xterm-256color");
    std::vector<char*> envp;
    envp.reserve(envStore.size() + 1);
    for (std::string& s : envStore) envp.push_back(&s[0]);
    envp.push_back(nullptr);

    // /bin/sh -c, not a hand-rolled argv split: the v1 console ran shells through wxExecute,
    // whose own word-splitting sh reproduces - so a TermShell cmd carrying arguments keeps
    // working, and a bare "/bin/bash" simply gets exec'd by sh.
    char shName[] = "sh", shDashC[] = "-c";
    char* argv[4];
    argv[0] = shName;
    argv[1] = shDashC;
    argv[2] = const_cast<char*>(cmdU.data());
    argv[3] = nullptr;

    int master = -1;
    const pid_t pid = ::forkpty(&master, nullptr, nullptr, &ws);
    if (pid < 0)
        return nullptr;
    if (pid == 0)
    {
        // Child. forkpty already made it a session leader with the slave as its controlling tty,
        // so its process group id == its pid and killpg(pid) reaches the whole group - the
        // equivalent of the wxEXEC_MAKE_GROUP_LEADER + wxKILL_CHILDREN pairing the v1 console
        // uses. Async-signal-safe calls ONLY from here to exec (see the pre-fork note above):
        // signal, chdir, execve, _exit all qualify.
        ::signal(SIGPIPE, SIG_DFL);   // IGNORED dispositions survive exec, and wx ignores SIGPIPE for its sockets - a shell inheriting that breaks `cmd | head` semantics
        if (cwdU.length() > 0)
        {
            if (::chdir(cwdU.data()) != 0)
            { /* deliberate: a vanished cwd shouldn't abort the spawn - the shell starts in its default dir instead */ }
        }
        ::execve("/bin/sh", argv, envp.data());
        ::_exit(127);   // exec failed; 127 = "command not found", the shell convention
    }

    // Parent. CLOEXEC first: forkpty has no O_CLOEXEC variant, and an inherited master leaks into
    // every LATER exec'd child - each new tab's shell, every wxExecute tool - which then pins the
    // pty pair open (no EOF/SIGHUP-on-close for anything still holding the old slave) for that
    // child's whole life. The fork-to-fcntl window is unavoidable but textbook.
    ::fcntl(master, F_SETFD, FD_CLOEXEC);
    // Non-blocking: write()'s inline fast path runs on the UI thread and must never park there
    // when the pty input queue is full (the reader thread drains the overflow via POLLOUT).
    const int fl = ::fcntl(master, F_GETFL, 0);
    ::fcntl(master, F_SETFL, fl | O_NONBLOCK);

    int keepSlave = -1;
#ifdef __APPLE__
    // Darwin (unlike Linux) DISCARDS output still queued in the pty when the child exits and the
    // last slave fd closes (Apple dev forums #663632; pexpect #662 works around it the same way),
    // truncating the shell's final burst. Holding one slave open in the parent keeps the queue
    // alive until the reader drains it; exit detection never depended on the EOF this suppresses
    // (see the reader's WNOHANG probe). Best-effort: on failure macOS just keeps Linux semantics.
    if (const char* sn = ::ptsname(master))
        keepSlave = ::open(sn, O_RDWR | O_NOCTTY | O_CLOEXEC);   // CLOEXEC for the same leak reason as the master
#endif

    return std::unique_ptr<TermBackend>(new ForkPtyBackend(master, pid, keepSlave));
}

#endif // __WXMSW__ / POSIX
