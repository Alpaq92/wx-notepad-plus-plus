#!/usr/bin/env python3
"""Strip the moved "\\t<accel>" suffixes out of the gettext catalogs.

Phase 1 of the shortcut work moved every menu accelerator OUT of the translated menu-label strings and
into locale-independent data (MenuItemDef.defaultAccel). The C++ source labels are now bare
(`_("&Save")` instead of `_("&Save\\tCtrl+S")`), so the existing catalog msgids (`"&Save\\tCtrl+S"`) no
longer match and those menu items would fall back to English until the catalogs are updated.

This script performs that update WITHOUT hand-editing 8 catalogs x ~79 msgids (the exact hazard this
migration removes - a stray manual edit that changes a binding). It:

  * Derives the affected {original_msgid -> new_msgid, accel} set from the CURRENT source (the stripped
    label + its data-row defaultAccel reconstruct the original msgid), so it is self-checking and needs
    no hard-coded list. ONLY those exact msgids are touched - note one unrelated msgid also contains a
    TAB (an escape-sequence help string) and must be left alone, so "strip any msgid with a tab" is wrong.
  * Strips the "\\t<accel>" from BOTH the msgid and the msgstr of each matched stanza, in every catalog.
  * De-duplicates the collisions this creates, in BOTH orderings: two stripped stanzas collapsing to the
    same bare id (the Fold/Unfold level items "1".."8" both collapse to the bare number), AND a stripped
    stanza colliding with a pre-existing bare msgid elsewhere in the catalog (e.g. the "&Save" dialog
    button vs. the stripped "&Save\\tCtrl+S" menu label), whichever of the two appears first. The
    surviving translations must be identical before a duplicate is dropped.
  * Refuses to merge two entries whose translations differ (a real collision).

Default is a DRY RUN (report only). Pass --apply to rewrite the .po files, then --mo to also regenerate
the .mo binaries (via the sibling po2mo.py) and mirror them to the xx_XX locale dirs.

Ownership note: resources/locale/* is co-maintained with the concurrent CLI-flags workflow. Coordinate
before --apply so a regeneration from either side doesn't clobber the other.

Usage:
    python scripts/migrate_po_strip_accels.py            # dry run: report what would change
    python scripts/migrate_po_strip_accels.py --apply    # rewrite the .po catalogs
    python scripts/migrate_po_strip_accels.py --apply --mo  # also regenerate + mirror the .mo files
"""
import os
import re
import sys
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "src")
LOCALE = os.path.join(ROOT, "resources", "locale")
LANGS = ["de", "es", "fr", "ja", "ko", "pl", "ru", "zh"]
REGION = {"de": "de_DE", "es": "es_ES", "fr": "fr_FR", "ja": "ja_JP",
          "ko": "ko_KR", "pl": "pl_PL", "ru": "ru_RU", "zh": "zh_CN"}
TAB = chr(92) + "t"   # the two source characters backslash + t (how a tab is normally written in .po)
LITTAB = chr(9)       # a literal TAB byte. Catalogs are inconsistent: 7 langs + the .pot use the "\t"
                      # escape, but zh stores most accel separators as a raw tab byte instead. Both
                      # compile to the same .mo key, so the matcher/stripper below accepts either form.

LABEL_FILES = ["menu_labels_file.h", "menu_labels_edit.h", "menu_labels_view.h",
               "menu_labels_search.h", "menu_labels_help.h", "menu_labels_run.h"]
DATA_FILES = ["menu_data_file.h", "menu_data_edit.h", "menu_data_selection.h", "menu_data_view.h",
              "menu_data_search.h", "menu_data_help.h", "menu_data_run.h"]

LAB_RE = re.compile(r'inline const wxString\s+(\w+)\s*\(\s*\)\s*\{\s*return\s+_\("([^"]*)"\);')
DATA_RE = re.compile(r'&Label::(\w+)\b[^\n]*?,\s*nullptr,\s*0,\s*false,\s*"([^"]*)"\s*\}')


def build_accel_map():
    """Return dict original_msgid -> new_msgid from the current (already-stripped) source + data rows."""
    labels = {}
    for f in LABEL_FILES:
        txt = open(os.path.join(SRC, f), encoding="utf-8", newline="").read()
        for m in LAB_RE.finditer(txt):
            labels[m.group(1)] = m.group(2)
    accels = {}
    for f in DATA_FILES:
        txt = open(os.path.join(SRC, f), encoding="utf-8", newline="").read()
        for m in DATA_RE.finditer(txt):
            accels[m.group(1)] = m.group(2)
    mapping = {}   # original msgid -> new msgid
    for func, accel in accels.items():
        if func not in labels:
            sys.exit("ERROR: data row references &Label::%s but no such label found" % func)
        new_id = labels[func]
        orig_id = new_id + TAB + accel
        mapping[orig_id] = new_id
    return mapping


# ---- tiny .po stanza model (preserves comment lines + blank-line separation) -----------------------
def unquote(line_body):
    return line_body  # we keep .po strings in their raw escaped form; only the \t suffix is manipulated


def split_stanzas(text):
    # Keep the raw text of each stanza (comment lines + msgid/msgstr) so formatting round-trips exactly.
    return re.split(r'\n\s*\n', text)


def stanza_msgid(stanza):
    m = re.search(r'^msgid "((?:[^"\\]|\\.)*)"\s*$', stanza, re.M)
    return m.group(1) if m else None


def first_tab_index(s):
    """Index of the earliest tab separator in a raw .po string, either the "\\t" escape or a raw tab."""
    cands = [i for i in (s.find(TAB), s.find(LITTAB)) if i != -1]
    return min(cands) if cands else -1


def strip_at_tab(s):
    i = first_tab_index(s)
    return s[:i] if i != -1 else s


def norm_msgid(s):
    """Normalize a raw msgid to the escaped form so it can be matched against the derived map keys."""
    return s.replace(LITTAB, TAB)


def process_po(path, mapping):
    text = open(path, encoding="utf-8", newline="").read()
    nl = "\r\n" if "\r\n" in text else "\n"
    # normalize to \n for processing, restore at write
    body = text.replace("\r\n", "\n")
    stanzas = body.split("\n\n")
    seen = {}          # new_msgid -> msgstr (for dedup / collision detection)
    out = []
    changed = 0
    dropped = 0
    collisions = []
    for st in stanzas:
        mid = stanza_msgid(st)
        if mid is not None and norm_msgid(mid) in mapping:
            new_id = mapping[norm_msgid(mid)]
            # rewrite the msgid line to the bare (accel-less) label
            st2 = re.sub(r'^msgid "(?:[^"\\]|\\.)*"\s*$', 'msgid "%s"' % new_id, st, count=1, flags=re.M)
            # strip from the first tab (either "\t" escape or a raw tab byte) to end of the msgstr line

            def strip_msgstr(m):
                return 'msgstr "%s"' % strip_at_tab(m.group(1))
            st2 = re.sub(r'^msgstr "((?:[^"\\]|\\.)*)"\s*$', strip_msgstr, st2, count=1, flags=re.M)
            changed += 1
            new_str = stanza_msgstr(st2)
            if new_id in seen:
                # duplicate produced by stripping (e.g. Fold vs Unfold "1"): keep first if identical
                if seen[new_id] != new_str:
                    collisions.append((new_id, seen[new_id], new_str))
                dropped += 1
                continue   # drop the duplicate stanza
            seen[new_id] = new_str
            out.append(st2)
        else:
            # A stanza we don't touch must still dedup SYMMETRICALLY against `seen`: a
            # pre-existing bare msgid (e.g. a dialog-button "&Save") that appears LATER in
            # the file than a stanza that just stripped down to the same msgid would
            # otherwise be appended unconditionally, shipping a duplicate msgid definition
            # (invalid gettext, rejected by msgfmt --check). Drop the later twin when the
            # translations match; report a collision (-> abort) when they differ. The
            # header stanza (msgid "") is exempt - it can never collide with a label.
            if mid:
                this_str = stanza_msgstr(st)
                if mid in seen:
                    if seen[mid] != this_str:
                        collisions.append((mid, seen[mid], this_str))
                    dropped += 1
                    continue   # drop the duplicate untouched stanza; keep the earlier one
                seen[mid] = this_str
            out.append(st)
    new_body = "\n\n".join(out)
    return text, new_body.replace("\n", nl), changed, dropped, collisions


def stanza_msgstr(stanza):
    m = re.search(r'^msgstr "((?:[^"\\]|\\.)*)"\s*$', stanza, re.M)
    return m.group(1) if m else None


def main():
    apply = "--apply" in sys.argv
    do_mo = "--mo" in sys.argv
    mapping = build_accel_map()
    print("Derived %d accel msgids to strip from the catalogs.\n" % len(mapping))

    targets = [(os.path.join(LOCALE, "wxn.pot"), None)]
    for lang in LANGS:
        targets.append((os.path.join(LOCALE, lang, "LC_MESSAGES", "wxn.po"), lang))

    any_collision = False
    for path, lang in targets:
        if not os.path.exists(path):
            print("  (skip missing %s)" % path); continue
        orig, new_text, changed, dropped, collisions = process_po(path, mapping)
        tag = "pot" if lang is None else lang
        note = "%3d stripped, %d duplicate stanza(s) merged" % (changed, dropped)
        if collisions:
            any_collision = True
            note += "  !! %d TRANSLATION COLLISION(S)" % len(collisions)
            for cid, a, b in collisions:
                note += "\n        msgid %r: %r vs %r" % (cid, a, b)
        print("  %-6s %s" % (tag, note))
        if apply and not collisions:
            open(path, "w", encoding="utf-8", newline="").write(new_text)

    if any_collision:
        sys.exit("\nABORT: differing translations collided after stripping - resolve by hand; nothing written for those files.")

    if apply and do_mo:
        sys.path.insert(0, LOCALE)
        import po2mo
        for lang in LANGS:
            po = os.path.join(LOCALE, lang, "LC_MESSAGES", "wxn.po")
            mo = os.path.join(LOCALE, lang, "LC_MESSAGES", "wxn.mo")
            entries = po2mo.parse_po(po)
            open(mo, "wb").write(po2mo.generate_mo(entries))
            region_mo = os.path.join(LOCALE, REGION[lang], "LC_MESSAGES", "wxn.mo")
            os.makedirs(os.path.dirname(region_mo), exist_ok=True)
            shutil.copyfile(mo, region_mo)
            print("  regenerated %s and mirrored to %s" % (mo, region_mo))

    if not apply:
        print("\nDRY RUN - no files written. Re-run with --apply (and --mo to regenerate binaries).")


if __name__ == "__main__":
    main()
