// SPDX-License-Identifier: Apache-2.0
//
// wxNote - budgeted workspace file crawler for Quick Open.
// Copyright 2026 The wxNote Authors.
//
// WHY NOT wxDir::GetAllFiles: it cannot prune. Pointed at a real project it walks .git/, node_modules/
// and build/ in full - hundreds of thousands of entries the user will never open - and returns only
// when it has finished.
//
// WHY NOT wxDir::Traverse: it is recursive and uninterruptible, so it cannot be sliced. A crawl that
// takes two seconds would be two seconds of frozen UI.
//
// WHY NO THREAD: there is not one thread anywhere in src/main.cpp, and Quick Open is not the feature
// that should introduce the first - a background crawler would need a lock around every result and
// would have to survive the workspace changing mid-walk. Instead the walk is an explicit work queue
// that does a few milliseconds at a time from a timer tick, which is single-threaded, cancellable and
// trivially correct.

#pragma once

#include <wx/dir.h>
#include <wx/log.h>
#include <wx/string.h>

#include <functional>
#include <utility>
#include <vector>

// Directories never worth indexing: VCS metadata, dependency trees and build output. All are large,
// machine-generated, and nothing in them is what a user means when they type a filename.
inline bool wxnPruneDir(const wxString& lowerName)
{
    // char, not wxChar: wxChar is wchar_t on MSW, so narrow literals would not convert. wxString
    // compares against const char* directly, which keeps the tables readable on every platform.
    static const char* const kPruned[] = {
        ".git", ".hg", ".svn", ".bzr", "cvs",
        "node_modules", "bower_components", "vendor", ".venv", "venv", "__pycache__",
        ".mypy_cache", ".pytest_cache", ".tox", ".gradle", ".idea", ".vs", ".vscode",
        "build", "cmake-build-debug", "cmake-build-release", "out", "dist", "target",
        "bin", "obj", ".cache", ".next", ".nuxt", ".terraform", "deps", "_deps",
    };
    for (const char* p : kPruned) if (lowerName == p) return true;
    return false;
}

// Binary and generated files that would only ever be noise in the picker.
inline bool wxnSkipExt(const wxString& lowerExt)
{
    static const char* const kSkipped[] = {
        "exe", "dll", "so", "dylib", "a", "lib", "o", "obj", "pdb", "ilk", "exp", "res",
        "zip", "gz", "bz2", "xz", "7z", "rar", "tar", "jar", "war",
        "png", "jpg", "jpeg", "gif", "bmp", "ico", "webp", "tiff", "psd",
        "mp3", "mp4", "avi", "mkv", "mov", "wav", "flac", "ogg",
        "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx",
        "ttf", "otf", "woff", "woff2", "eot",
        "mo", "pyc", "class", "bin", "dat", "db", "sqlite", "lock",
    };
    for (const char* p : kSkipped) if (lowerExt == p) return true;
    return false;
}

// A BREADTH-FIRST walk held as an explicit queue so it can be stopped between any two directories.
//
// Breadth-first is load-bearing, not incidental: combined with "files before subdirectories" in
// scanOne it means a crawl that is still running has already found everything shallow. Depth-first
// would spend the first tick at the bottom of one arbitrary branch, so a picker opened mid-crawl in a
// deep tree would list none of the files next to the root - the ones most likely to be wanted.
class WxnFileIndex
{
public:
    static constexpr size_t kMaxFiles = 50000;   // beyond this the picker is not the right tool anyway
    static constexpr int    kMaxDepth = 24;      // no real source tree is deeper than this
    // Directories QUEUED, which kMaxFiles does not bound: a junction or symlink pointing at an ancestor
    // makes the walk branch on every level, and since those branches rediscover files that are already
    // indexed they need not add a single new file - so the file cap never trips while the queue grows
    // exponentially toward 2^kMaxDepth entries. wxDIR_NO_FOLLOW below stops the common case; this stops
    // the rest (Windows junctions are not symlinks, and a bind mount is neither).
    static constexpr size_t kMaxDirs  = 100000;

    // now() is injected so this header needs no clock and the crawl is deterministically testable.
    explicit WxnFileIndex(std::function<long long()> now = nullptr) : m_now(std::move(now)) {}

    void start(const wxString& root)
    {
        clear();
        m_root = root;
        if (!root.empty()) m_queue.emplace_back(root, 0);
    }

    void clear()
    {
        m_root.clear();
        m_queue.clear();
        m_head = 0;
        m_files.clear();
        m_truncated = false;
        m_done = false;
    }

    // Consumes up to budgetMs of wall clock. Returns true if there is more to do, so the caller can
    // stop its timer on false. Always finishes the directory it is in, so the queue stays consistent.
    bool step(int budgetMs)
    {
        if (m_done) return false;
        const long long deadline = m_now ? m_now() + budgetMs : 0;
        wxLogNull noPopup;   // an unreadable directory must not raise a modal log dialog mid-crawl

        while (m_head < m_queue.size())
        {
            const wxString dirPath = m_queue[m_head].first;   // by value: scanOne grows m_queue
            const int      depth   = m_queue[m_head].second;
            ++m_head;
            scanOne(dirPath, depth);
            if (m_files.size() >= kMaxFiles) { m_truncated = true; break; }
            if (m_now && m_now() >= deadline) return true;   // out of budget: resume on the next tick
        }
        m_queue.clear();
        m_head = 0;
        m_done = true;
        return false;
    }

    const std::vector<wxString>& files() const { return m_files; }
    const wxString& root() const { return m_root; }
    bool truncated() const { return m_truncated; }
    bool done() const { return m_done; }
    size_t size() const { return m_files.size(); }

private:
    void scanOne(const wxString& dirPath, int depth)
    {
        wxDir dir(dirPath);
        if (!dir.IsOpened()) return;
        wxString name;
        // Built once per entry into a reused buffer. `dirPath + sep + name` allocates an intermediate
        // string for the first +, and this runs up to kMaxFiles times plus once per directory.
        wxString full;
        auto join = [&] { full = dirPath; full += wxFILE_SEP_PATH; full += name; return full; };

        // Files first, so a shallow crawl that runs out of budget still has the most relevant results.
        for (bool more = dir.GetFirst(&name, wxEmptyString, wxDIR_FILES | wxDIR_HIDDEN);
             more; more = dir.GetNext(&name))
        {
            if (m_files.size() >= kMaxFiles) return;
            // AfterLast, not wxFileName: `name` is a bare filename, so a full volume/dir/name/ext parse
            // is wasted work. Two shapes must NOT be read as extensions:
            //   "Makefile"   - no dot at all; AfterLast('.') would return the whole string
            //   ".lock"      - a dotfile, whose ONLY dot is at index 0; the name is "lock", not the
            //                  extension, and "lock" is on the skip list, so it would vanish silently
            const int dot = name.Find('.', /*fromEnd=*/true);
            if (dot > 0)
            {
                const wxString ext = name.Mid(static_cast<size_t>(dot) + 1).Lower();
                if (!ext.empty() && wxnSkipExt(ext)) continue;
            }
            m_files.push_back(join());
        }
        if (depth >= kMaxDepth) return;
        // wxDIR_NO_FOLLOW: a symlinked directory is listed but never descended into, so an ordinary
        // `ln -s .. loop` cannot send the walk back up its own tree.
        for (bool more = dir.GetFirst(&name, wxEmptyString, wxDIR_DIRS | wxDIR_HIDDEN | wxDIR_NO_FOLLOW);
             more; more = dir.GetNext(&name))
        {
            if (m_queue.size() >= kMaxDirs) { m_truncated = true; return; }
            if (wxnPruneDir(name.Lower())) continue;
            m_queue.emplace_back(join(), depth + 1);
        }
    }

    std::function<long long()>              m_now;
    wxString                                m_root;
    std::vector<std::pair<wxString, int>>   m_queue;     // (directory, depth); FIFO via m_head
    size_t                                  m_head = 0;
    std::vector<wxString>                   m_files;
    bool                                    m_truncated = false;
    bool                                    m_done = false;
};
