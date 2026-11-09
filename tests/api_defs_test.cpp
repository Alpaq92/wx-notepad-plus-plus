// SPDX-License-Identifier: Apache-2.0
//
// api_defs_test - api/<Language>.yaml (src/api_defs.h): every form the file takes, what a bad entry costs
// (itself only), how a user's file lies over wxNote's, and that what wxnEmitApi writes reads back the same.
//
//   cmake --build build --target api_defs_test && build/bin/api_defs_test
//   build/bin/api_defs_test <file.yaml>...    reads each file instead: what it holds, and its warnings
//
#include "api_defs.h"

#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& what)
{
    std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what.c_str());
    ok ? ++g_pass : ++g_fail;
}
static bool warned(const WxnApi& a, const std::string& part)
{
    for (const std::string& w : a.warnings) if (w.find(part) != std::string::npos) return true;
    return false;
}

int main(int argc, char** argv)
{
    if (argc > 1)
    {
        int bad = 0;
        for (int i = 1; i < argc; ++i)
        {
            std::ifstream f(argv[i], std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            WxnApi a;
            std::string err;
            size_t overloads = 0;
            const bool ok = f && wxnParseApi(ss.str(), a, &err);
            for (const auto& kv : a.functions) overloads += kv.second.size();
            std::printf("%s: %s, %zu functions (%zu overloads), %zu words, %zu warnings\n", argv[i], ok ? "read" : err.c_str(),
                        a.functions.size(), overloads, a.words.size(), a.warnings.size());
            for (const std::string& w : a.warnings) std::printf("    %s\n", w.c_str());
            if (!ok || !a.warnings.empty()) ++bad;
        }
        return bad ? 1 : 0;
    }
    std::printf("api_defs_test\n");
    {
        WxnApi a;
        std::string err;
        const bool ok = wxnParseApi(
            "ignoreCase: true\n"
            "call: {open: '(', close: ')', separator: ',', end: ';'}\n"
            "wordChars: '$'\n"
            "words: [echo, include, 'require once']\n"
            "functions:\n"
            "  strlen: {returns: int, params: [string $string], doc: Returns the length.}\n"
            "  substr:\n"
            "    - {returns: string, params: [string $s, int $offset]}\n"
            "    - {returns: string, params: [string $s, int $offset, '?int $length = null']}\n"
            "  pi:\n"
            "  time: {returns: int}\n", a, &err);
        check(ok && err.empty() && a.warnings.empty(), "a full file reads without a warning");
        check(a.hasIgnoreCase && a.ignoreCase, "ignoreCase");
        check(a.hasCall && a.open == '(' && a.close == ')' && a.separator == ',' && a.end == ';', "the call characters");
        check(a.hasWordChars && a.wordChars == "$", "wordChars");
        check(a.words.size() == 3 && a.words[2] == "require once", "words, a list's entries whole");
        check(a.functions.size() == 4, "every function");
        const auto* s = a.find("SUBSTR");
        check(s && s->size() == 2 && (*s)[1].params.size() == 3 && (*s)[1].params[2] == "?int $length = null", "overloads as a list, found without regard to case");
        const auto* p = a.find("pi");
        check(p && p->size() == 1 && p->front().params.empty() && p->front().returns.empty(), "a name alone is a function with nothing known of it");
        const auto* t = a.find("time");
        check(t && t->front().params.empty() && t->front().returns == "int", "params may be left out");
        check(wxnApiSignature("strlen", a.find("strlen")->front(), a) == "int strlen(string $string)\nReturns the length.", "the call tip text");
        check(wxnApiSignature("substr", (*s)[0], a) == "string substr(string $s, int $offset)", "no doc, no second line");
        std::set<std::string> got;
        a.collect("S", got);
        check(got == std::set<std::string>{ "strlen", "substr" }, "completion with ignoreCase");
        got.clear();
        a.collect("strlen", got);
        check(got.empty(), "the word typed whole is not offered");
    }
    {
        WxnApi a;
        wxnParseApi("functions:\n  Foo: {}\nwords: bar  baz bar\n", a, nullptr);
        check(a.words == std::vector<std::string>{ "bar", "baz" }, "words as one line of text, split, each once");
        std::set<std::string> got;
        a.collect("f", got);
        check(got.empty() && a.find("foo") == nullptr && a.find("Foo"), "case counts without ignoreCase");
        check(!a.hasCall && a.open == '(' && a.separator == ',' && a.end == 0, "the call characters default to C's");
    }
    {
        WxnApi a;
        std::string err;
        check(wxnParseApi("ignoreCase: maybe\ncall: {open: '((' }\nwords: {a: b}\nfunctions:\n  f: [1]\n  g: {returns: int}\n  h: {colour: red}\nmore: 1\n", a, &err) && err.empty(),
              "a file of bad entries still reads");
        check(warned(a, "ignoreCase") && warned(a, "call: open") && warned(a, "words") && warned(a, "function f") && warned(a, "function h") && warned(a, "unknown entry \"more\""),
              "each bad entry is reported");
        check(!a.hasIgnoreCase && !a.hasCall && a.functions.size() == 1 && a.find("g"), "and only the good ones are used");
        check(!wxnParseApi("functions: [\n", a, &err) && !err.empty(), "a file that does not parse is an error");
        check(!wxnParseApi("- a\n- b\n", a, &err), "so is a list");
        check(wxnParseApi("", a, &err) && a.empty(), "an empty file is nothing");
        check(wxnParseApi("\xEF\xBB\xBFwords: [x]\n", a, &err) && a.words.size() == 1, "a BOM is skipped");
    }
    {
        WxnApi base, user;
        wxnParseApi("call: {open: '(', close: ')', separator: ','}\nwords: [a, b]\nfunctions:\n  f: {returns: int}\n  g: {returns: int}\n", base, nullptr);
        wxnParseApi("ignoreCase: true\nwords: [b, c]\nfunctions:\n  g: {returns: long, params: [x]}\n", user, nullptr);
        base.merge(user);
        check(base.ignoreCase && base.hasCall, "the user's settings are laid on, the rest kept");
        check(base.words == std::vector<std::string>{ "a", "b", "c" }, "words added, each once");
        check(base.find("f")->front().returns == "int" && base.find("g")->front().returns == "long", "the user's function replaces wxNote's");
    }
    {
        WxnApi a;
        a.hasIgnoreCase = true; a.ignoreCase = false;
        a.hasCall = true; a.open = '['; a.close = ']'; a.separator = ';'; a.end = 0;
        a.hasWordChars = true; a.wordChars = "#:";
        a.words = { "yes", "null", "1.0" };
        a.functions["f"] = { WxnApiOverload{ "int", { "a: b", "c, d", "[e]", "{f}", "'g'" }, "line one\nline \"two\"" } };
        a.functions["g"] = { WxnApiOverload{}, WxnApiOverload{ "", { "x" }, "" } };
        const std::string yaml = wxnEmitApi(a, "# test\n");
        WxnApi b;
        std::string err;
        check(yaml.rfind("# test\n", 0) == 0, "the header goes first");
        check(wxnParseApi(yaml, b, &err) && err.empty() && b.warnings.empty(), "what is written reads back:\n" + yaml + err);
        check(b.hasIgnoreCase && !b.ignoreCase && b.open == '[' && b.close == ']' && b.separator == ';' && b.end == 0 && b.wordChars == "#:", "settings round-trip");
        check(b.words == a.words, "words that look like other types round-trip");
        const auto* f = b.find("f");
        check(f && f->size() == 1 && f->front().params == a.functions["f"][0].params && f->front().doc == a.functions["f"][0].doc && f->front().returns == "int",
              "params with YAML's own characters, and a doc of two lines, round-trip");
        const auto* g = b.find("g");
        check(g && g->size() == 2 && (*g)[1].params.size() == 1, "overloads round-trip");
    }
    check(wxnApiFileName("C++") == "C++.yaml" && wxnApiFileName("a/b:c") == "a_b_c.yaml", "file names");
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
