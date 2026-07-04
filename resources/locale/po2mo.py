#!/usr/bin/env python3
"""Minimal .po -> .mo compiler (GNU gettext binary catalog format).

No msgfmt available on this machine, so this hand-rolls the same scheme
CPython's own Tools/i18n/msgfmt.py uses: entries sorted by msgid, hash-table
size set to 0 (forces the gettext runtime to binary-search the sorted table
instead of hashing).

Usage: python po2mo.py <catalog>.po <catalog>.mo
"""
import sys
import struct
import re


def parse_po(path):
    with open(path, encoding='utf-8') as f:
        text = f.read()

    entries = {}
    # Split on blank lines into stanzas; each stanza has msgid "..." (possibly
    # continued on multiple quoted lines) and msgstr "...".
    stanzas = re.split(r'\n\s*\n', text)
    for stanza in stanzas:
        lines = [l for l in stanza.splitlines() if l.strip() and not l.strip().startswith('#')]
        if not lines:
            continue

        def collect(keyword, lines):
            out = []
            i = 0
            found = False
            while i < len(lines):
                line = lines[i].strip()
                if line.startswith(keyword):
                    found = True
                    rest = line[len(keyword):].strip()
                    out.append(rest)
                    i += 1
                    while i < len(lines) and lines[i].strip().startswith('"'):
                        out.append(lines[i].strip())
                        i += 1
                    break
                i += 1
            if not found:
                return None, i
            joined = ''.join(decode_quoted(p) for p in out)
            return joined, i

        msgid, _ = collect('msgid ', lines)
        msgstr, _ = collect('msgstr ', lines)
        if msgid is None or msgstr is None:
            continue
        entries[msgid] = msgstr
    return entries


def decode_quoted(part):
    part = part.strip()
    if not (part.startswith('"') and part.endswith('"')):
        return ''
    inner = part[1:-1]
    out = []
    i = 0
    while i < len(inner):
        c = inner[i]
        if c == '\\' and i + 1 < len(inner):
            n = inner[i + 1]
            if n == 'n':
                out.append('\n')
            elif n == 't':
                out.append('\t')
            elif n == '\\':
                out.append('\\')
            elif n == '"':
                out.append('"')
            else:
                out.append(n)
            i += 2
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def generate_mo(entries):
    keys = sorted(entries.keys())
    offsets = []
    ids = b''
    strs = b''
    for k in keys:
        msg_id = k.encode('utf-8')
        msg_str = entries[k].encode('utf-8')
        offsets.append((len(ids), len(msg_id), len(strs), len(msg_str)))
        ids += msg_id + b'\0'
        strs += msg_str + b'\0'

    keystart = 7 * 4 + 16 * len(keys)
    valuestart = keystart + len(ids)
    koffsets = []
    voffsets = []
    for o1, l1, o2, l2 in offsets:
        koffsets += [l1, o1 + keystart]
        voffsets += [l2, o2 + valuestart]
    offsets_flat = koffsets + voffsets

    output = struct.pack('Iiiiiii',
                          0x950412de,               # magic
                          0,                         # version
                          len(keys),                 # number of strings
                          7 * 4,                      # offset of key table
                          7 * 4 + len(keys) * 8,      # offset of value table
                          0, 0)                       # hash table size, offset (unused)
    output += struct.pack('i' * len(offsets_flat), *offsets_flat)
    output += ids
    output += strs
    return output


if __name__ == '__main__':
    po_path, mo_path = sys.argv[1], sys.argv[2]
    entries = parse_po(po_path)
    data = generate_mo(entries)
    with open(mo_path, 'wb') as f:
        f.write(data)
    print(f'{po_path} -> {mo_path}: {len(entries)} entries')
