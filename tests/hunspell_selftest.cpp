// SPDX-License-Identifier: Apache-2.0
// =====================================================================
// hunspell_selftest.cpp - headless verification of the vendored libhunspell + the bundled SCOWL en_US
// dictionary. No wx and no display, so it runs on every CI arch (including the Windows/MSVC runner),
// which is how the spell-check engine is verified without a Linux machine.
//
//   cmake --build build --target hunspell_selftest && build/bin/hunspell_selftest
//
// DICT_DIR (the resources/dictionaries source dir) is injected by CMake.
// =====================================================================
#include <hunspell/hunspell.hxx>
#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

int main() {
    const std::string dir = DICT_DIR;               // ASCII CMake source path
    const std::string aff = dir + "/en_US.aff";
    const std::string dic = dir + "/en_US.dic";

    Hunspell h(aff.c_str(), dic.c_str());

    // The dictionary must have loaded (a fresh instance over a missing dict "spells" everything wrong).
    check(std::string(h.get_dict_encoding()) == "UTF-8", "en_US.aff declares SET UTF-8");
    check(h.spell("hello"),  "correct word 'hello' accepted");
    check(h.spell("editor"), "correct word 'editor' accepted");
    check(h.spell("colour") || h.spell("color"), "at least one English 'colo(u)r' spelling accepted");
    check(!h.spell("teh"),   "misspelling 'teh' rejected");
    check(!h.spell("jumpd"), "misspelling 'jumpd' rejected");

    // suggest() must return candidates, and the obvious fix for 'teh' should be among them.
    std::vector<std::string> sug = h.suggest("teh");
    bool hasThe = false;
    for (const std::string& s : sug) if (s == "the") hasThe = true;
    check(!sug.empty(), "suggest('teh') returns candidates");
    check(hasThe,       "suggest('teh') includes 'the'");

    // Runtime add() must make a previously-unknown word spell correctly (session dictionary).
    check(!h.spell("wxnote"),    "'wxnote' unknown before add()");
    check(h.add("wxnote") == 0,  "add('wxnote') succeeds");
    check(h.spell("wxnote"),     "'wxnote' accepted after add()");

    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
