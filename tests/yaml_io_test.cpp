// SPDX-License-Identifier: Apache-2.0
//
// yaml_io_test - headless self-test for src/yaml_io.h, the layer every wxNote file store reads and
// writes through. No wx, no files: everything is parsed from and emitted to strings.
//
//   cmake --build build --target yaml_io_test && build/bin/yaml_io_test
//
#include "yaml_io.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0, g_pass = 0;
static void check(bool ok, const char* what) { std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what); if (ok) ++g_pass; else ++g_fail; }

using namespace wxnyaml;

static void testReading()
{
    std::printf("reading\n");
    Doc d;
    const std::string src =
        "# a comment\n"
        "editor.tabSize: 4\n"
        "editor.wordWrap: yes\n"
        "neg: -12\n"
        "hex: 0x1F\n"
        "big: 9223372036854775807\n"
        "tooBig: 9223372036854775808\n"
        "min: -9223372036854775808\n"
        "real: 1.5e3\n"
        "realBad: 1.5x\n"
        "name: Cascadia Mono\n"
        "quotedNull: 'null'\n"
        "empty: ''\n"
        "nothing:\n"
        "tilde: ~\n"
        "list: [a, 'b c']\n"
        "map: {x: 1}\n";
    check(parse(src, d), "parses");
    const Node r = d.root();
    check(isMap(r), "top node is a map");
    long long i = 0;
    check(getInteger(child(r, "editor.tabSize"), i) && i == 4, "dotted key reads as an integer");
    bool b = false;
    check(getBool(child(r, "editor.wordWrap"), b) && b, "yes reads as true");
    check(getInteger(child(r, "neg"), i) && i == -12, "negative integer");
    check(getInteger(child(r, "hex"), i) && i == 31, "hex integer");
    check(getInteger(child(r, "big"), i) && i == 9223372036854775807LL, "largest long long");
    check(!getInteger(child(r, "tooBig"), i), "past the range does not fit");
    check(getInteger(child(r, "min"), i) && i == (-9223372036854775807LL - 1), "smallest long long");
    double x = 0;
    check(getNumber(child(r, "real"), x) && x == 1500.0, "real number in C spelling");
    check(!getNumber(child(r, "realBad"), x), "trailing junk is no number");
    check(!getInteger(child(r, "real"), i), "a real is no integer");
    check(textOr(child(r, "name"), "") == "Cascadia Mono", "plain text with a space");
    check(textOr(child(r, "quotedNull"), "") == "null", "quoted null is text");
    std::string s = "unset";
    check(getText(child(r, "empty"), s) && s.empty(), "quoted empty is empty text, not absent");
    check(!getText(child(r, "nothing"), s), "empty value reads as absent");
    check(!getText(child(r, "tilde"), s), "~ reads as absent");
    check(!getText(child(r, "missing"), s), "missing key reads as absent");
    check(!getText(child(r, "list"), s), "a list is no text");
    check(isSeq(child(r, "list")) && child(r, "list").num_children() == 2, "flow list");
    check(textOr(child(r, "list")[1], "") == "b c", "quoted list item");
    check(isMap(child(r, "map")), "flow map");
    check(!isMap(child(child(r, "name"), "x")), "child of a scalar is not readable");
    check(integerOr(child(r, "missing"), 7) == 7 && boolOr(child(r, "missing"), true), "fallbacks");
    check(keyOf(child(r, "neg")) == "neg", "keyOf");

    Doc empty;
    check(parse("", empty) && !empty.root().readable(), "empty text: ok, no root");
    check(parse("# only a comment\n", empty) && !isMap(empty.root()), "comment only: ok, no map");

    Doc stream;
    check(parse("---\na: 1\n---\na: 2\n", stream) && integerOr(child(stream.root(), "a"), 0) == 1,
          "explicit documents: the first one counts");
}

static void testErrors()
{
    std::printf("errors\n");
    Doc d;
    check(!parse("a: 1\nb: 2\nc: [3, 4\nd: 5\n", d, "settings.yaml"), "unclosed list fails");
    check(!d.ok() && !d.root().readable(), "failed parse leaves no root");
    check(d.error.find("line 4") != std::string::npos, "error names the line (1-based)");
    std::printf("        (%s)\n", d.error.c_str());
    check(!parse("a: 1\n  b: 2\n", d), "bad indentation fails");
    check(d.error.find("line 2") != std::string::npos, "indentation error on line 2");
    check(parse("a: 1\n", d) && d.ok() && d.error.empty(), "a later good parse clears the error");
    check(!parse("key: \"unterminated\n", d), "unterminated quote fails");
    check(!parse("\t- tab-indented\n", d) || d.ok(), "tab indentation does not crash");
}

static void testAliases()
{
    std::printf("aliases\n");
    Doc d;
    check(parse("a: &x hello\nb: *x\n", d), "anchor and alias parse");
    check(textOr(child(d.root(), "a"), "") == "hello", "the anchored value reads");
    std::string s;
    check(!getText(child(d.root(), "b"), s), "an alias reads as absent");

    // Nine levels of tenfold aliases: a billion "lol"s if anything expanded them.
    std::string bomb = "a: &a [lol, lol, lol, lol, lol, lol, lol, lol, lol, lol]\n";
    for (char c = 'b'; c <= 'j'; ++c)
    {
        const char p = static_cast<char>(c - 1);
        bomb += std::string(1, c) + ": &" + std::string(1, c) + " [";
        for (int k = 0; k < 10; ++k) bomb += std::string(k ? ", *" : "*") + p;
        bomb += "]\n";
    }
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = parse(bomb, d);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    check(ok, "billion-laughs text parses");
    check(ms < 2000, "...without expanding (fast)");
    check(d.tree.size() < 200, "...and without growing the tree");
}

static void testJson()
{
    std::printf("json\n");
    Doc d;
    check(parseJson("{\"s\": \"1\", \"n\": 1, \"r\": -2.5e2, \"t\": true, \"z\": null, \"a\": [1], \"o\": {}}", d), "parses");
    const Node r = d.root();
    check(jsonType(child(r, "s")) == JsonType::String, "quoted 1 is a string");
    check(jsonType(child(r, "n")) == JsonType::Number, "1 is a number");
    check(jsonType(child(r, "r")) == JsonType::Number, "-2.5e2 is a number");
    double x = 0;
    check(getNumber(child(r, "r"), x) && x == -250.0, "number value");
    check(jsonType(child(r, "t")) == JsonType::Bool, "true is a bool");
    check(jsonType(child(r, "z")) == JsonType::Null, "null is null");
    check(jsonType(child(r, "a")) == JsonType::Array, "array");
    check(jsonType(child(r, "o")) == JsonType::Object, "object");
    check(jsonType(child(r, "missing")) == JsonType::None, "missing is none");

    check(parseJson("{\"bmp\": \"\\u0105\", \"pair\": \"\\ud83d\\ude00\", \"lone\": \"x\\ud800y\", \"lowFirst\": \"\\ude00\\ud83d\"}", d),
          "unicode escapes parse");
    check(textOr(child(d.root(), "bmp"), "") == "\xC4\x85", "\\u0105 is UTF-8 a-ogonek");
    check(textOr(child(d.root(), "pair"), "") == "\xF0\x9F\x98\x80", "surrogate pair joins into one 4-byte character");
    check(textOr(child(d.root(), "lone"), "") == "x\xEF\xBF\xBDy", "lone surrogate becomes U+FFFD");
    check(textOr(child(d.root(), "lowFirst"), "") == "\xEF\xBF\xBD\xEF\xBF\xBD", "reversed pair: two U+FFFD");

    check(!parseJson("{\"a\": 1} x", d), "trailing garbage fails");
    check(!parseJson("{\"a\": ", d), "truncated input fails");
    check(!parseJson("", d) || !d.root().readable(), "empty input yields no document");

    std::string deep(5000, '[');
    deep += std::string(5000, ']');
    const bool parsed = parseJson(deep, d);
    check(parsed || !d.ok(), "5000 nested arrays: no crash");
}

static void testWriting()
{
    std::printf("writing\n");
    const std::vector<std::string> tricky = {
        "", " lead", "trail ", "a: b", "#x", "x #y", "- x", "[x", "{x", "'q'", "\"dq\"", "multi\nline",
        "tab\there", "\xC4\x85\xC4\x87", "C:\\path\\file.txt", "~", "null", "Null", "true", "False", "yes",
        "off", "123", "-4", "1.5", ".5", "0x1F", "@x", "`x", "%x", "!x", "&x", "*x", "|x", ">x", "?", ":",
        "-", "---", "...", "a,b", "x]", "ctrl+k ctrl+c", "#0000FF", "FF8000", "bell\x07", "cr\r\nlf"
    };
    ryml::Tree t;
    MutNode root = resetToMap(t);
    MutNode list = addSeq(root, std::string("str") + "ings");          // key from a temporary
    for (const std::string& s : tricky) setText(addItem(list), s);
    setInteger(addKey(root, "int"), -42);
    setBool(addKey(root, "on"), true);
    setBool(addKey(root, "off"), false);
    MutNode rows = addSeq(root, "styles");
    MutNode row = addMapItem(rows);
    setOneLine(row);
    setInteger(addKey(row, "id"), 5);
    setText(addKey(row, "name"), "INSTRUCTION WORD");
    setText(addKey(row, "fg"), "#0000FF");
    setBool(addKey(row, "bold"), true);

    std::string out;
    check(emit(t, out) && !out.empty(), "emits");
    check(out.find("int: -42\n") != std::string::npos, "integer is plain");
    check(out.find("on: true\n") != std::string::npos && out.find("off: false\n") != std::string::npos, "bools are true/false");
    check(out.find("- {id: 5, name: INSTRUCTION WORD, fg: '#0000FF', bold: true}") != std::string::npos, "one-line row with spaces");
    check(out.find("- '123'") != std::string::npos && out.find("- 'true'") != std::string::npos
          && out.find("- 'null'") != std::string::npos && out.find("- ''") != std::string::npos, "typed-looking text is quoted");

    Doc back;
    check(parse(out, back), "emitted text parses");
    const Node strings = child(back.root(), "strings");
    bool same = isSeq(strings) && strings.num_children() == tricky.size();
    size_t k = 0;
    for (Node item : strings.children())
    {
        std::string got;
        if (!getText(item, got) || got != tricky[k])
        {
            std::printf("        mismatch at %zu: wanted [%s]\n", k, tricky[k].c_str());
            same = false;
        }
        ++k;
    }
    check(same, "every string reads back byte for byte");
    long long i = 0;
    check(getInteger(child(back.root(), "int"), i) && i == -42, "integer reads back");
    bool b = false;
    check(getBool(child(back.root(), "on"), b) && b && getBool(child(back.root(), "off"), b) && !b, "bools read back");
    const Node r0 = isSeq(child(back.root(), "styles")) ? child(back.root(), "styles")[0] : Node();
    check(textOr(child(r0, "fg"), "") == "#0000FF" && integerOr(child(r0, "id"), 0) == 5, "row reads back");

    ryml::Tree emptyTree;
    std::string none = "x";
    check(emit(emptyTree, none) && none.empty(), "empty tree emits nothing");
}

int main()
{
    testReading();
    testErrors();
    testAliases();
    testJson();
    testWriting();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
