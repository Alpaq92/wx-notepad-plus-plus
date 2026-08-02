// SPDX-License-Identifier: Apache-2.0
//
// wxNote - self-test for the Quick Open workspace crawler (src/file_index.h).
// Copyright 2026 The wxNote Authors.
//
// Builds a real directory tree in the system temp dir and crawls it. Real I/O rather than a mocked
// filesystem is deliberate: the things that break here are wxDir flag semantics (does wxDIR_DIRS also
// return files? does wxDIR_HIDDEN cover dotted names on this platform?), and a mock would encode my
// assumption about those flags instead of testing it.
//
//   cmake --build build --target file_index_test && build/bin/file_index_test

#include "file_index.h"

#include <wx/file.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/init.h>

#include <cstdio>
#include <string>

static int g_pass = 0, g_fail = 0;

static void check(bool ok, const std::string& what)
{
    if (ok) { ++g_pass; std::printf("  ok    %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL  %s\n", what.c_str()); }
}

static bool has(const std::vector<wxString>& v, const wxString& leaf)
{
    for (const wxString& p : v) if (wxFileName(p).GetFullName() == leaf) return true;
    return false;
}

static int indexOf(const std::vector<wxString>& v, const wxString& leaf)
{
    for (size_t i = 0; i < v.size(); ++i) if (wxFileName(v[i]).GetFullName() == leaf) return (int)i;
    return -1;
}

static void touch(const wxString& path)
{
    wxFile f;
    f.Create(path, /*overwrite=*/true);
}

// Recursive teardown - wxRmdir only removes empty directories.
static void rmTree(const wxString& dir)
{
    wxDir d(dir);
    if (!d.IsOpened()) return;
    wxString name;
    std::vector<wxString> subs;
    for (bool more = d.GetFirst(&name, wxEmptyString, wxDIR_FILES | wxDIR_HIDDEN);
         more; more = d.GetNext(&name))
        wxRemoveFile(dir + wxFILE_SEP_PATH + name);
    for (bool more = d.GetFirst(&name, wxEmptyString, wxDIR_DIRS | wxDIR_HIDDEN);
         more; more = d.GetNext(&name))
        subs.push_back(dir + wxFILE_SEP_PATH + name);
    for (const wxString& s : subs) rmTree(s);
    wxRmdir(dir);
}

int main(int argc, char** argv)
{
    wxInitializer init(argc, argv);
    if (!init.IsOk()) { std::printf("wx init failed\n"); return 2; }

    std::printf("== file_index_test ==\n\n");

    // ---- (a) the prune list, as a pure function -------------------------------------------------
    check(wxnPruneDir(".git"),          "prune: .git");
    check(wxnPruneDir("node_modules"),  "prune: node_modules");
    check(wxnPruneDir("build"),         "prune: build");
    check(wxnPruneDir("__pycache__"),   "prune: __pycache__");
    check(!wxnPruneDir("src"),          "prune: src is kept");
    check(!wxnPruneDir("buildsystem"),  "prune: matches whole names only, not prefixes");
    check(!wxnPruneDir("rebuild"),      "prune: matches whole names only, not suffixes");

    check(wxnSkipExt("exe"),            "skipext: exe");
    check(wxnSkipExt("png"),            "skipext: png");
    check(wxnSkipExt("mo"),             "skipext: compiled catalogs");
    check(!wxnSkipExt("cpp"),           "skipext: cpp is kept");
    check(!wxnSkipExt("h"),             "skipext: h is kept");
    check(!wxnSkipExt("po"),            "skipext: po source catalogs are kept");

    // ---- build the tree -------------------------------------------------------------------------
    const wxString base = wxFileName::GetTempDir() + wxFILE_SEP_PATH + "wxn_fidx_test";
    rmTree(base);
    wxMkdir(base);
    wxMkdir(base + wxFILE_SEP_PATH + ".git");
    wxMkdir(base + wxFILE_SEP_PATH + "node_modules");
    wxMkdir(base + wxFILE_SEP_PATH + "node_modules" + wxFILE_SEP_PATH + "pkg");
    wxMkdir(base + wxFILE_SEP_PATH + "src");
    wxMkdir(base + wxFILE_SEP_PATH + "src" + wxFILE_SEP_PATH + "deep");

    touch(base + wxFILE_SEP_PATH + "a.txt");
    touch(base + wxFILE_SEP_PATH + "main.cpp");
    touch(base + wxFILE_SEP_PATH + "logo.png");                                    // skipped extension
    touch(base + wxFILE_SEP_PATH + "README");                                      // no extension at all
    touch(base + wxFILE_SEP_PATH + ".lock");    // a DOTFILE whose name collides with a skipped extension
    touch(base + wxFILE_SEP_PATH + ".gitignore");
    touch(base + wxFILE_SEP_PATH + ".git"        + wxFILE_SEP_PATH + "config");    // pruned
    touch(base + wxFILE_SEP_PATH + "node_modules"+ wxFILE_SEP_PATH + "pkg" + wxFILE_SEP_PATH + "index.js");
    touch(base + wxFILE_SEP_PATH + "src"         + wxFILE_SEP_PATH + "b.cpp");
    touch(base + wxFILE_SEP_PATH + "src" + wxFILE_SEP_PATH + "deep" + wxFILE_SEP_PATH + "c.h");

    // ---- (b) an unbudgeted crawl (no clock injected => never times out) --------------------------
    WxnFileIndex idx;
    idx.start(base);
    const bool more = idx.step(/*budgetMs=*/5);
    check(!more,        "crawl: an unbudgeted step finishes in one call");
    check(idx.done(),   "crawl: done() is set when the queue drains");
    check(!idx.truncated(), "crawl: a small tree is not reported truncated");

    const std::vector<wxString>& f = idx.files();
    check(f.size() == 7, "crawl: exactly the seven indexable files are found");
    check(has(f, "a.txt"),    "crawl: finds a.txt");
    check(has(f, "main.cpp"), "crawl: finds main.cpp");
    check(has(f, "README"),   "crawl: an extensionless file is indexed, not skipped");
    // ".lock" is a NAME, not an extension. Reading the text after the leading dot as one would have
    // matched the skip list ("lock") and quietly dropped the file.
    check(has(f, ".lock"),      "crawl: a dotfile named like a skipped extension is still indexed");
    check(has(f, ".gitignore"), "crawl: ordinary dotfiles are indexed");
    check(has(f, "b.cpp"),    "crawl: descends into src/");
    check(has(f, "c.h"),      "crawl: descends two levels");
    check(!has(f, "logo.png"),"crawl: a skipped extension is excluded");
    check(!has(f, "config"),  "crawl: .git is pruned");
    check(!has(f, "index.js"),"crawl: node_modules is pruned even though .js is indexable");

    // ---- (c) breadth-first: shallow files land before deep ones ----------------------------------
    check(indexOf(f, "a.txt") < indexOf(f, "b.cpp"), "order: root files precede src/ files");
    check(indexOf(f, "b.cpp") < indexOf(f, "c.h"),   "order: src/ files precede src/deep/ files");

    // ---- (d) resumability: forced one-directory-per-step yields the identical result -------------
    // The clock advances by 1 on every read, so with budgetMs 0 the deadline is always already past.
    long long tick = 0;
    WxnFileIndex sliced([&tick] { return tick++; });
    sliced.start(base);
    int calls = 0;
    while (sliced.step(/*budgetMs=*/0)) { if (++calls > 100) break; }
    check(calls > 0 && calls < 100, "sliced: the crawl really was split across several steps");
    check(sliced.done(), "sliced: still reaches done()");
    check(sliced.files() == f, "sliced: a time-sliced crawl finds exactly the same files, in the same order");
    check(!sliced.step(5), "sliced: stepping a finished index is a no-op that stays finished");

    // ---- (e) the depth cap ----------------------------------------------------------------------
    const wxString deep = wxFileName::GetTempDir() + wxFILE_SEP_PATH + "wxn_fidx_deep";
    rmTree(deep);
    wxMkdir(deep);
    wxString cur = deep;
    for (int i = 0; i < WxnFileIndex::kMaxDepth + 3; ++i)
    {
        cur += wxFILE_SEP_PATH + wxString::Format("d%d", i);
        wxMkdir(cur);
        touch(cur + wxFILE_SEP_PATH + wxString::Format("f%d.txt", i));
    }
    WxnFileIndex dix;
    dix.start(deep);
    dix.step(5);
    // Levels 0..kMaxDepth-1 are descended into; the directory AT kMaxDepth is scanned but not expanded.
    check(dix.size() == (size_t)WxnFileIndex::kMaxDepth,
          "depth: the walk stops at kMaxDepth instead of following a runaway chain");
    check(has(dix.files(), "f0.txt"), "depth: the shallow end is still indexed");
    check(!has(dix.files(), wxString::Format("f%d.txt", WxnFileIndex::kMaxDepth + 2)),
          "depth: the deepest files are not");

    // ---- (f) lifecycle --------------------------------------------------------------------------
    dix.clear();
    check(dix.size() == 0 && !dix.done() && dix.root().empty(), "clear: resets every field");
    dix.start("");
    check(!dix.step(5) && dix.size() == 0, "empty root: crawls to completion finding nothing");
    dix.start(base + wxFILE_SEP_PATH + "does_not_exist");
    check(!dix.step(5) && dix.size() == 0, "missing root: no crash, no results");
    dix.start(base + wxFILE_SEP_PATH + "a.txt");
    check(!dix.step(5) && dix.size() == 0, "root that is a file: no crash, no results");

    // restart must not accumulate the previous crawl's results
    dix.start(base);
    dix.step(5);
    const size_t firstRun = dix.size();
    dix.start(base);
    dix.step(5);
    check(dix.size() == firstRun, "restart: start() clears the previous results rather than appending");

    rmTree(base);
    rmTree(deep);

    std::printf("\n%s  (%d passed, %d failed)\n", g_fail ? "FAILED" : "PASSED", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
