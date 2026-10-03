// SPDX-License-Identifier: Apache-2.0
//
// settings_store_test - headless self-test for src/settings_store.h and src/settings_schema.h: the
// line-level editing of settings.yaml (comments and layout survive, a broken file is never written
// over), the schema's typed reads and sparse writes, and state.yaml's merge of two processes' saves.
// No wx, no files.
//
//   cmake --build build --target settings_store_test && build/bin/settings_store_test
//
#include "settings_schema.h"

#include <cstdio>
#include <set>
#include <string>
#include <vector>

static int g_fail = 0, g_pass = 0;
static void check(bool ok, const char* what) { std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what); if (ok) ++g_pass; else ++g_fail; }
static void checkText(const std::string& got, const std::string& want, const char* what)
{
    check(got == want, what);
    if (got != want) std::printf("        --- wanted:\n%s\n        --- got:\n%s\n", want.c_str(), got.c_str());
}

using namespace wxnsettings;

static void testLineEdits()
{
    std::printf("settings.yaml line edits\n");
    const std::string src =
        "# My settings\n"
        "editor.tabSize: 2   # small\n"
        "editor.wordWrap: true\n"
        "\n"
        "# Looks\n"
        "ui.themeMode: dark\n";
    SettingsFile f;
    check(f.load(src), "loads");
    Settings s(f);
    check(s.getInt("editor.tabSize") == 2 && s.getBool("editor.wordWrap") && s.getChoice("ui.themeMode") == 1, "reads the values");

    check(s.setInt("editor.tabSize", 8), "change tabSize");
    checkText(f.text(),
        "# My settings\n"
        "editor.tabSize: 8   # small\n"
        "editor.wordWrap: true\n"
        "\n"
        "# Looks\n"
        "ui.themeMode: dark\n", "...in place, comment kept");

    check(s.setText("editor.fontFamily", "JetBrains Mono"), "add a new editor.* setting");
    checkText(f.text(),
        "# My settings\n"
        "editor.tabSize: 8   # small\n"
        "editor.wordWrap: true\n"
        "editor.fontFamily: JetBrains Mono\n"
        "\n"
        "# Looks\n"
        "ui.themeMode: dark\n", "...placed after the last editor.* line");

    check(s.setText("print.header", "$(FILE_NAME)"), "add a setting of a new namespace");
    const std::string tail = "ui.themeMode: dark\nprint.header: $(FILE_NAME)\n";
    checkText(f.text().size() >= tail.size() ? f.text().substr(f.text().size() - tail.size()) : f.text(), tail, "...appended at the end");

    check(s.setBool("editor.wordWrap", false), "set wordWrap back to its default");
    check(f.text().find("wordWrap") == std::string::npos, "...removes its line");
    check(f.text().find("editor.tabSize: 8   # small\neditor.fontFamily") != std::string::npos, "...and nothing else");

    check(s.setChoice("ui.themeMode", 2), "change a choice");
    check(f.text().find("ui.themeMode: light\n") != std::string::npos && f.text().find("# Looks\n") != std::string::npos, "...by name, header comment kept");
    check(s.setChoice("ui.themeMode", 0), "choice back to default");
    check(f.text().find("themeMode") == std::string::npos, "...removed");
    check(f.dirty(), "edits mark the file dirty");
    f.markSaved();
    check(!f.dirty(), "markSaved clears it");
    check(s.setInt("editor.tabSize", 8) && !f.dirty(), "writing the value already there changes nothing");
}

static void testValueShapes()
{
    std::printf("value shapes\n");
    SettingsFile f;
    Settings s(f);
    check(f.load(""), "empty text loads");
    check(s.getInt("editor.tabSize") == 4 && s.getText("editor.fontFamily") == "Cascadia Mono", "empty: defaults");
    check(s.setInt("editor.tabSize", 3), "first setting into an empty file");
    checkText(f.text(), "editor.tabSize: 3\n", "...is one line");

    f.load("editor.fontFamily: 'Hack'  # my font\n");
    check(s.setText("editor.fontFamily", "Iosevka Fixed"), "replace a quoted value");
    checkText(f.text(), "editor.fontFamily: Iosevka Fixed  # my font\n", "...quotes dropped where not needed, comment kept");
    check(s.setText("editor.fontFamily", "a: b #c"), "a value that needs quoting");
    check(s.getText("editor.fontFamily") == "a: b #c", "...reads back");
    check(f.text().find("# my font") != std::string::npos, "...comment still there");

    f.load("'editor.tabSize': 2\n\"editor.useTabs\": false\n");
    check(s.getInt("editor.tabSize") == 2 && !s.getBool("editor.useTabs"), "quoted keys read");
    check(s.setInt("editor.tabSize", 6) && s.setBool("editor.useTabs", true), "quoted keys edit");
    checkText(f.text(), "'editor.tabSize': 6\n", "...in place; the default one removed");

    f.load("editor.fontFamily: Cascadia\n  Code\neditor.tabSize: 2\n");
    check(s.getText("editor.fontFamily") == "Cascadia Code", "a plain value folded over two lines reads");
    check(s.setText("editor.fontFamily", "Hack"), "...and is replaced");
    checkText(f.text(), "editor.fontFamily: Hack\neditor.tabSize: 2\n", "...as a block");

    f.load("files.associations:\n  inc: PHP   # old site\n  h: C++\n# keep me\neditor.tabSize: 2\n");
    std::vector<std::pair<std::string, std::string>> m = s.getMap("files.associations");
    check(m.size() == 2 && m[0].first == "inc" && m[0].second == "PHP" && m[1].second == "C++", "map reads in order");
    check(s.setMap("files.associations", { { "inc", "PHP" }, { "tpl", "HTML" } }), "map replaced");
    checkText(f.text(), "files.associations:\n  inc: PHP\n  tpl: HTML\n# keep me\neditor.tabSize: 2\n", "...as a block, the comment after it kept");
    check(s.setMap("files.associations", {}), "empty map");
    checkText(f.text(), "# keep me\neditor.tabSize: 2\n", "...removes the setting");

    f.load("a: 1\r\nb: 2\r\n");
    check(s.setInt("editor.tabSize", 2), "CRLF file");
    checkText(f.text(), "a: 1\r\nb: 2\r\neditor.tabSize: 2\r\n", "...keeps CRLF");

    f.load("editor.tabSize: 2");
    check(s.setBool("editor.wordWrap", true), "no newline at the end");
    checkText(f.text(), "editor.tabSize: 2\neditor.wordWrap: true\n", "...gets one before the new line");

    f.load("---\neditor.tabSize: 2\n");
    check(s.setInt("editor.tabSize", 5), "a leading --- is fine");
    checkText(f.text(), "---\neditor.tabSize: 5\n", "...edited in place");

    f.load("editor.tabSize: 2\neditor.tabSize: 3\n");
    check(s.getInt("editor.tabSize") == 2, "duplicate key: the first counts");
    check(s.setInt("editor.tabSize", 7) && s.getInt("editor.tabSize") == 7, "...and is the one edited");
}

static void testFallbacks()
{
    std::printf("fallbacks and refusals\n");
    SettingsFile f;
    Settings s(f);
    f.load("{editor.tabSize: 2, editor.wordWrap: true}\n");
    check(s.getInt("editor.tabSize") == 2, "flow map file reads");
    check(s.setInt("editor.tabSize", 9), "...an edit falls back to a whole rewrite");
    check(s.getInt("editor.tabSize") == 9 && s.getBool("editor.wordWrap"), "...values all kept");

    check(!f.load("# header\n- a\n- b\n") && !f.error().empty(), "a top-level list is no settings file: reported");
    check(s.getInt("editor.tabSize") == 4, "...reads as the defaults");
    check(!s.setInt("editor.tabSize", 9), "...refuses edits");
    check(f.text() == "# header\n- a\n- b\n", "...and untouched");

    const std::string twoDocs = "editor.tabSize: 2\n---\neditor.wordWrap: true\n";
    check(!f.load(twoDocs) && f.error().find("document") != std::string::npos, "two YAML documents: reported");
    check(!s.setInt("editor.tabSize", 3) && f.text() == twoDocs, "...and left alone, since a rewrite would drop the second");
    check(f.load("editor.tabSize: 2\n---\n"), "an empty document after the first is no second document");

    // A byte-order mark, as some Windows editors save one: the line editor must still find the first key.
    check(f.load("\xEF\xBB\xBF# mine\neditor.tabSize: 2   # keep\neditor.fontFamily: Hack\n"), "a file with a byte-order mark loads");
    check(s.getInt("editor.tabSize") == 2, "...its first key reads");
    check(s.setInt("editor.tabSize", 6), "...and edits");
    checkText(f.text(), "# mine\neditor.tabSize: 6   # keep\neditor.fontFamily: Hack\n", "...in place, comments kept");
    check(s.setInt("editor.tabSize", 4) && f.text() == "# mine\neditor.fontFamily: Hack\n", "...and back to its default, its line goes");
    check(wxnyaml::leadingComments("\xEF\xBB\xBF# head\nkey: 1\n") == "# head\n", "leadingComments: a byte-order mark is no part of the header");

    const std::string broken = "editor.tabSize: [2\neditor.wordWrap: true\n";
    check(!f.load(broken), "broken file does not load");
    check(!f.ok() && !f.error().empty(), "...reports why");
    std::printf("        (%s)\n", f.error().c_str());
    check(s.getInt("editor.tabSize") == 4 && !s.getBool("editor.wordWrap"), "...reads as all defaults");
    check(!s.setInt("editor.tabSize", 3) && !s.setBool("editor.wordWrap", true), "...refuses every edit");
    check(f.text() == broken, "...and keeps the user's text");

    f.load("editor.tabSize: 99\neditor.wordWrap: maybe\nui.themeMode: purple\neditor.caretWidth: x\n");
    check(s.getInt("editor.tabSize") == 4, "out of range: default");
    check(!s.getBool("editor.wordWrap"), "not a bool: default");
    check(s.getChoice("ui.themeMode") == 0, "unknown name: default");
    check(s.getInt("editor.caretWidth") == 1, "not a number: default");

    f.load("ui.themeMode: DARK\neditor.wordWrap: Yes\nsomething.unknown: 5\n");
    check(s.getChoice("ui.themeMode") == 1 && s.getChoiceName("ui.themeMode") == "dark", "choice names in any case");
    check(s.getBool("editor.wordWrap"), "Yes is true");
    check(s.setInt("editor.tabSize", 3) && f.text().find("something.unknown: 5") != std::string::npos, "unknown keys survive edits");
    check(!s.setInt("no.such.setting", 3) && !s.setBool("editor.tabSize", true), "unknown ID or wrong kind: refused");
}

static void testSchema()
{
    std::printf("schema\n");
    bool ok = true;
    std::set<std::string> ids;
    for (const Def& d : schema())
    {
        if (!ids.insert(d.id).second) { std::printf("        duplicate %s\n", d.id); ok = false; }
        if (d.kind == Kind::Int)
        {
            const long long v = std::stoll(d.def);
            if (v < d.lo || v > d.hi) { std::printf("        %s default out of range\n", d.id); ok = false; }
        }
        if (d.kind == Kind::Bool && std::string(d.def) != "true" && std::string(d.def) != "false") { std::printf("        %s bad bool\n", d.id); ok = false; }
        if (d.kind == Kind::Choice)
        {
            bool found = false;
            for (const std::string& n : choiceNames(d)) found = found || n == d.def;
            if (!found) { std::printf("        %s default not a choice\n", d.id); ok = false; }
        }
    }
    check(ok, "every default fits its setting, IDs unique");

    SettingsFile f;
    Settings s(f);
    f.load("");
    check(s.setColor("editor.gutterColor", 0x3E3A3A), "colour");
    unsigned rgb = 0;
    check(s.getColor("editor.gutterColor", rgb) && rgb == 0x3E3A3A, "...reads back");
    check(f.text() == "editor.gutterColor: '#3E3A3A'\n", "...as '#RRGGBB'");
    f.load("editor.gutterColor: e8e8e8\n");
    check(s.getColor("editor.gutterColor", rgb) && rgb == 0xE8E8E8, "colour without # and lower case");
    f.load("");
    check(!s.getColor("editor.gutterColor", rgb), "absent colour: caller's fallback");

    f.load("editor.tabSize: 2\nlanguages:\n  Python:\n    editor.tabSize: 4\n    editor.useTabs: false\n  Makefile:\n    editor.tabSize: 99\n");
    // As the editor reads them: the general value, then the language's own where it has a valid one.
    auto tabFor  = [&](const char* lang) { long long v = s.getInt("editor.tabSize"); s.languageInt("editor.tabSize", lang, v); return v; };
    auto tabsFor = [&](const char* lang) { bool v = s.getBool("editor.useTabs"); s.languageBool("editor.useTabs", lang, v); return v; };
    check(tabFor("Python") == 4 && !tabsFor("Python"), "per-language values");
    check(tabFor("C++") == 2 && tabsFor("C++"), "...other languages get the general one");
    check(tabFor("Makefile") == 2, "...an invalid per-language value falls back");
    check(s.setInt("editor.tabSize", 3) && tabFor("Python") == 4, "editing the general value leaves languages alone");
    check(f.text().find("  Python:\n    editor.tabSize: 4\n") != std::string::npos, "...textually too");
}

// No value at `path` (a container there counts as none, as for every reader).
static bool absent(const StateFile& s, const std::string& path) { return s.getText(path, "") == ""; }

static void testState()
{
    std::printf("state.yaml\n");
    StateFile a;
    check(a.load(""), "empty loads");
    a.setInt("window/x", -20);
    a.setInt("window/width", 1100);
    a.setBool("window/maximized", true);
    a.setText("run/lastCommand", "notepad \"$(FULL_CURRENT_PATH)\"");
    a.setText("note", "123");
    a.setList("recentFiles", { "C:\\a b\\x.txt", "/home/u/y.md" });
    a.setText("recovery/entries/5/title", "new 1");
    a.setText("recovery/entries/12/title", "new 2");
    const std::string text = a.mergeForSave("");
    check(text.find("x: -20") != std::string::npos && text.find("maximized: true") != std::string::npos, "numbers and bools plain");
    check(text.find("note: '123'") != std::string::npos, "text that looks like a number is quoted");

    StateFile b;
    check(b.load(text), "the written text loads");
    check(b.getInt("window/x", 0) == -20 && b.getInt("window/width", 0) == 1100 && b.getBool("window/maximized", false), "values read back");
    check(b.getText("run/lastCommand") == "notepad \"$(FULL_CURRENT_PATH)\"" && b.getText("note") == "123", "text reads back");
    check(b.getList("recentFiles").size() == 2 && b.getList("recentFiles")[0] == "C:\\a b\\x.txt", "list reads back");
    const std::vector<std::string> ids = b.children("recovery/entries");
    check(ids.size() == 2 && ids[0] == "12" && ids[1] == "5", "children");
    check(b.getInt("missing", 7) == 7 && b.getText("window") == "", "fallbacks; a map is no text");

    // Two processes: B saves a recovery entry while A, started earlier, changes its recent files.
    StateFile procA, procB;
    procA.load(text);
    procB.load(text);
    procB.setText("recovery/entries/9/title", "from B");
    const std::string diskAfterB = procB.mergeForSave(text);
    procA.setList("recentFiles", { "only-A.txt" });
    procA.remove("recovery/entries/5");
    const std::string merged = procA.mergeForSave(diskAfterB);
    StateFile m;
    m.load(merged);
    check(m.getList("recentFiles").size() == 1 && m.getList("recentFiles")[0] == "only-A.txt", "merge: A's change wins where A touched");
    check(m.getText("recovery/entries/9/title") == "from B", "merge: B's entry survives A's save");
    check(absent(m, "recovery/entries/5/title") && m.getText("recovery/entries/12/title") == "new 2", "merge: A's removal applies, the rest stays");
    check(procA.getText("recovery/entries/9/title") == "from B", "merge: A now sees B's entry");
    check(procA.dirty(), "merged but not yet written: the changes stay pending, so a failed write is retried");
    const std::string again = procA.mergeForSave(diskAfterB);   // the write failed: the next save merges again
    check(again == merged, "...and the retry produces the same text");
    procA.markSaved();
    check(!procA.dirty(), "written (markSaved): not dirty");

    // A key that changes shape keeps the shape it was given last, in the merge too.
    StateFile shape;
    shape.load("");
    shape.setText("a/b", "x");
    shape.setText("a", "leaf");
    m.load(shape.mergeForSave(""));
    check(m.getText("a") == "leaf" && absent(m, "a/b"), "merge: 'a/b' then 'a' ends as 'a'");
    StateFile shape2;
    shape2.load("");
    shape2.remove("a/b");
    shape2.setText("a", "leaf");
    m.load(shape2.mergeForSave("a:\n  b: old\n"));
    check(m.getText("a") == "leaf", "merge: removing 'a/b', then setting 'a', keeps 'a'");
    // Removing something that is not there must not take another process's value above it.
    StateFile gone;
    gone.load("");
    gone.remove("a/b");
    m.load(gone.mergeForSave("a: other\n"));
    check(m.getText("a") == "other", "merge: removing an absent 'a/b' leaves another process's 'a'");

    // A byte-order mark, as a Windows editor may save one.
    StateFile bom;
    check(bom.load("\xEF\xBB\xBFzoom: 2\n") && bom.getInt("zoom", 0) == 2, "state.yaml with a byte-order mark reads");

    StateFile c;
    c.load("");
    c.setText("a b", "x");
    c.setText("a/x", "y");
    c.setText("a/z/q", "w");
    StateFile disk;
    disk.load("a:\n  x: old\n  zz: keep\n");
    StateFile c2;
    c2.load("");
    c2.setText("a/x", "new");
    m.load(c2.mergeForSave(disk.mergeForSave("a:\n  x: old\n  zz: keep\n")));
    check(m.getText("a/x") == "new" && m.getText("a/zz") == "keep", "merge: sibling keys are separate subtrees");
    m.load(c.mergeForSave(""));
    check(m.getText("a b") == "x" && m.getText("a/x") == "y" && m.getText("a/z/q") == "w", "'a b' sorts between 'a' and 'a/x' without losing either");

    StateFile d;
    d.load("");
    d.setText("a", "leaf");
    d.setText("a/b", "child");
    check(absent(d, "a") && d.getText("a/b") == "child", "a value under a path replaces a value at it");
    d.setText("a", "leaf");
    check(d.getText("a") == "leaf" && absent(d, "a/b"), "...and the other way round");

    StateFile broken;
    check(!broken.load("a: [1\n"), "broken state.yaml does not load");
    broken.setInt("zoom", 2);
    StateFile back;
    back.load(broken.mergeForSave("a: [1\n"));
    check(back.getInt("zoom", 0) == 2, "...and is replaced on the next save");
}

int main()
{
    testLineEdits();
    testValueShapes();
    testFallbacks();
    testSchema();
    testState();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
