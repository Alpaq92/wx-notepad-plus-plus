#pragma once
// Desktop-entry rewriting, for AppImage self-integration (see WxnShellFrameT's AppImage helpers).
//
// An AppImage installs nothing. It is one executable file the user drops anywhere, so nothing ever
// writes a .desktop into ~/.local/share/applications - which is why the MIME types wxNote declares
// have no effect at all from an AppImage: the desktop never learns the application exists, so it
// cannot offer it in "Open With" or let it be made the default for anything.
//
// The fix is for the running AppImage to install that .desktop itself. The one thing it must not do
// is *invent* the file: the AppImage already carries the real one (installer/linux/wxnote.desktop,
// copied to $APPDIR/wxnote.desktop at build time) with all 84 MimeType entries. Duplicating that list
// in C++ would guarantee the two drift. So integration REWRITES the bundled file, touching only the
// three keys that must become absolute paths - Exec, TryExec and Icon - and copying every other line
// through untouched.
//
// Pure std::string on purpose: no wx, no filesystem, no process. The quoting and the key rewriting
// are where the bugs live (a path with a space produces a .desktop the desktop silently ignores),
// and none of it needs a Linux box to exercise - see tests/desktop_entry_test.cpp.
#include <string>
#include <vector>

// Characters the Desktop Entry spec calls reserved inside an Exec value. Listed for the reader's
// benefit; wxnDesktopQuote does not branch on them (see below).
//   space tab newline " ' \ > < ~ | & ; $ * ? # ( ) `

// Quote one argument for an Exec= value, per the Desktop Entry Specification's "Exec variables".
//
// Always quotes, rather than only when a reserved character is present. Both are spec-legal, and an
// unconditional quote removes the branch most likely to be wrong - the interesting inputs here are
// user-chosen filesystem paths ("~/Apps/wx Note.AppImage"), and a quoting rule that is only exercised
// for some of them is a rule that gets tested for the wrong ones.
inline std::string wxnDesktopQuote(const std::string& arg)
{
    std::string out = "\"";
    for (char c : arg)
    {
        // The spec requires exactly these four to be backslash-escaped inside a quoted argument.
        if (c == '"' || c == '`' || c == '$' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

// The part of an Exec value that follows the program: field codes (%F, %U, ...) and any fixed
// arguments. Returned with no leading space, empty when the value is a bare program.
//
// Understands a quoted program token, because that is what this file's own output looks like - the
// integration is expected to be re-run (every launch checks whether the AppImage moved), so it must
// be able to re-parse a value it wrote itself and not mistake the quoted path for two arguments.
inline std::string wxnDesktopExecArgs(const std::string& execValue)
{
    size_t i = 0;
    while (i < execValue.size() && (execValue[i] == ' ' || execValue[i] == '\t')) ++i;
    if (i < execValue.size() && execValue[i] == '"')
    {
        for (++i; i < execValue.size(); ++i)
        {
            if (execValue[i] == '\\') { ++i; continue; }   // escaped char, never a closing quote
            if (execValue[i] == '"')  { ++i; break; }
        }
    }
    else
    {
        while (i < execValue.size() && execValue[i] != ' ' && execValue[i] != '\t') ++i;
    }
    while (i < execValue.size() && (execValue[i] == ' ' || execValue[i] == '\t')) ++i;
    return execValue.substr(i);
}

// Read one key's value from the [Desktop Entry] group; empty when absent. Used to compare the
// installed entry's TryExec against the AppImage's current path, which is how a moved or renamed
// AppImage is noticed - the alternative is a menu entry that silently stops working.
inline std::string wxnDesktopValue(const std::string& contents, const std::string& key)
{
    bool inEntry = false;
    size_t pos = 0;
    while (pos <= contents.size())
    {
        size_t nl = contents.find('\n', pos);
        if (nl == std::string::npos) nl = contents.size();
        std::string line = contents.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = nl + 1;
        if (!line.empty() && line[0] == '[') { inEntry = (line == "[Desktop Entry]"); continue; }
        if (!inEntry || line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq != std::string::npos && line.substr(0, eq) == key) return line.substr(eq + 1);
        if (nl == contents.size()) break;
    }
    return std::string();
}

// Rewrite a bundled .desktop so it can be installed into ~/.local/share/applications.
//
// Only Exec, TryExec and Icon change; MimeType, Categories, Name, comments and blank lines are copied
// through byte for byte, which is the entire point - the MIME list stays single-sourced in
// installer/linux/wxnote.desktop. Any of the three keys that the bundled file does not have is
// appended to the [Desktop Entry] group rather than dropped.
//
// Only the [Desktop Entry] group is rewritten. A [Desktop Action ...] group would carry its own Exec
// and would need the same treatment; wxNote's entry has no actions, and silently rewriting a group
// this function was never shown is worse than leaving it for whoever adds one.
inline std::string wxnIntegrateDesktopEntry(const std::string& bundled,
                                            const std::string& execPath,
                                            const std::string& iconPath)
{
    const std::string quoted = wxnDesktopQuote(execPath);
    std::vector<std::string> out;
    bool inEntry = false, seenEntry = false;
    bool haveExec = false, haveTry = false, haveIcon = false;
    size_t entryEnd = 0;      // where to append any key the bundled file was missing

    size_t pos = 0;
    const std::string src = bundled;
    while (pos < src.size() || (pos == src.size() && !src.empty() && src.back() == '\n' && false))
    {
        size_t nl = src.find('\n', pos);
        const bool last = (nl == std::string::npos);
        if (last) nl = src.size();
        std::string line = src.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();   // tolerate a CRLF checkout
        pos = nl + 1;

        if (!line.empty() && line[0] == '[')
        {
            inEntry = (line == "[Desktop Entry]");
            if (inEntry) seenEntry = true;
            out.push_back(line);
            if (!inEntry && seenEntry && entryEnd == 0) entryEnd = out.size() - 1;   // group ended here
            if (last) break;
            continue;
        }
        if (inEntry && !line.empty() && line[0] != '#')
        {
            const size_t eq = line.find('=');
            const std::string key = (eq == std::string::npos) ? std::string() : line.substr(0, eq);
            if (key == "Exec")
            {
                // Keep the field codes the bundled entry declared (%F: "accepts several file paths").
                // Rebuilding the value without them would produce a launcher that opens the app but
                // never passes it the file the user double-clicked.
                const std::string args = wxnDesktopExecArgs(line.substr(eq + 1));
                out.push_back("Exec=" + quoted + (args.empty() ? "" : " " + args));
                haveExec = true;
                if (last) break;
                continue;
            }
            if (key == "TryExec") { out.push_back("TryExec=" + execPath); haveTry = true; if (last) break; continue; }
            if (key == "Icon")    { out.push_back("Icon=" + iconPath);    haveIcon = true; if (last) break; continue; }
        }
        out.push_back(line);
        if (last) break;
    }

    if (entryEnd == 0) entryEnd = out.size();   // [Desktop Entry] ran to the end of the file
    // Trailing blank lines belong after the appended keys, not before them: a key written past a blank
    // line still parses, but the file reads as though the group ended and something was tacked on.
    while (entryEnd > 0 && out[entryEnd - 1].empty()) --entryEnd;
    std::vector<std::string> add;
    if (!haveExec) add.push_back("Exec=" + quoted + " %F");
    // TryExec is what makes a stale entry hide itself: the desktop skips a launcher whose TryExec is
    // not an existing executable, so an AppImage the user deleted stops appearing in menus instead of
    // producing a "could not launch" error the next time someone picks it.
    if (!haveTry)  add.push_back("TryExec=" + execPath);
    if (!haveIcon) add.push_back("Icon=" + iconPath);
    out.insert(out.begin() + static_cast<long>(entryEnd), add.begin(), add.end());

    std::string joined;
    for (size_t i = 0; i < out.size(); ++i) { joined += out[i]; joined += '\n'; }
    return joined;
}
