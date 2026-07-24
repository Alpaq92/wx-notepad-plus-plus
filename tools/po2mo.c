// SPDX-License-Identifier: Apache-2.0
//
// po2mo - minimal .po -> .mo compiler (GNU gettext binary catalog format). A C port of the former
// resources/locale/po2mo.py, so the project's only translation-compile step needs no Python interpreter
// (a C/C++ compiler is already a hard prerequisite for building wxNote; Python otherwise isn't used by the
// build, CI, or the shipped binary). Output is BYTE-IDENTICAL to the Python version (and to CPython's own
// Tools/i18n/msgfmt.py scheme): entries sorted by msgid, hash-table size 0 (the gettext runtime then
// binary-searches the sorted table). All integers are written little-endian explicitly, so the output is
// correct regardless of host byte order.
//
// It implements exactly the .po subset the catalogs use: msgid/msgstr with quoted continuation lines and
// the \n \t \\ \" escapes; comment (#) lines skipped; blank-line-separated stanzas; last duplicate wins.
// No plurals (msgid_plural), no contexts (msgctxt) - the same limits the Python version had.
//
//   Build: part of the CMake project as an optional dev target -> cmake --build build --target po2mo
//   (or standalone: cc -O2 -o po2mo tools/po2mo.c)
//   Usage: po2mo <catalog>.po <catalog>.mo
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// ---- growable byte buffer ---------------------------------------------------------------------------
typedef struct { char* p; size_t len, cap; } Buf;
static void buf_put(Buf* b, char c) {
    if (b->len + 1 > b->cap) { b->cap = b->cap ? b->cap * 2 : 64; b->p = (char*)realloc(b->p, b->cap); }
    b->p[b->len++] = c;
}

// ---- one catalog entry ------------------------------------------------------------------------------
typedef struct { char* id; size_t idlen; char* str; size_t strlen_; } Entry;

static Entry* g_ent = NULL;
static size_t g_n = 0, g_cap = 0;

// Insert (or replace, last-wins) an entry. Takes ownership of id/str (malloc'd).
static void put_entry(char* id, size_t idlen, char* str, size_t strlen_) {
    for (size_t i = 0; i < g_n; ++i)
        if (g_ent[i].idlen == idlen && memcmp(g_ent[i].id, id, idlen) == 0) {
            free(g_ent[i].str); free(id);
            g_ent[i].str = str; g_ent[i].strlen_ = strlen_;
            return;
        }
    if (g_n == g_cap) { g_cap = g_cap ? g_cap * 2 : 128; g_ent = (Entry*)realloc(g_ent, g_cap * sizeof(Entry)); }
    g_ent[g_n].id = id; g_ent[g_n].idlen = idlen; g_ent[g_n].str = str; g_ent[g_n].strlen_ = strlen_;
    ++g_n;
}

static int cmp_entry(const void* a, const void* b) {
    const Entry* x = (const Entry*)a; const Entry* y = (const Entry*)b;
    const size_t n = x->idlen < y->idlen ? x->idlen : y->idlen;
    // Compare as unsigned bytes (UTF-8 byte order == Unicode code-point order == Python's sorted()).
    for (size_t i = 0; i < n; ++i) {
        unsigned char cx = (unsigned char)x->id[i], cy = (unsigned char)y->id[i];
        if (cx != cy) return cx < cy ? -1 : 1;
    }
    if (x->idlen != y->idlen) return x->idlen < y->idlen ? -1 : 1;
    return 0;
}

// ---- .po parsing ------------------------------------------------------------------------------------
// Trim leading whitespace; return pointer into s.
static const char* lstrip(const char* s) { while (*s == ' ' || *s == '\t') ++s; return s; }

// Decode one quoted part (e.g. "abc\n") into the growable buffer. Non-quoted parts contribute nothing.
static void decode_quoted(const char* part, Buf* out) {
    const char* s = lstrip(part);
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r')) --n;   // rstrip
    if (n < 2 || s[0] != '"' || s[n-1] != '"') return;                          // not a "..." part
    for (size_t i = 1; i + 1 < n; ++i) {
        char c = s[i];
        if (c == '\\' && i + 2 < n) {
            char nx = s[i+1];
            if (nx == 'n')       buf_put(out, '\n');
            else if (nx == 't')  buf_put(out, '\t');
            else if (nx == '\\') buf_put(out, '\\');
            else if (nx == '"')  buf_put(out, '"');
            else                 buf_put(out, nx);   // unknown escape: drop the backslash, keep the char
            ++i;
        } else {
            buf_put(out, c);
        }
    }
}

int main(int argc, char** argv) {
    if (argc != 3) { fprintf(stderr, "usage: po2mo <catalog>.po <catalog>.mo\n"); return 2; }

    FILE* f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "po2mo: cannot open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long fsz = ftell(f); fseek(f, 0, SEEK_SET);
    char* text = (char*)malloc((size_t)fsz + 1);
    size_t rd = fread(text, 1, (size_t)fsz, f); text[rd] = '\0'; fclose(f);

    // Walk lines. A blank (whitespace-only) line ends a stanza. Within a stanza we collect the msgid and
    // msgstr keyword values (each possibly continued over following quoted lines), skipping # comments.
    // We process a stanza as soon as it closes.
    char** lines = NULL; size_t nlines = 0, capl = 0;   // filtered lines of the current stanza

    #define ADD_LINE(ptr) do { if (nlines==capl){capl=capl?capl*2:32; lines=(char**)realloc(lines,capl*sizeof(char*));} lines[nlines++]=(ptr); } while(0)

    // collect(keyword): first filtered line whose lstrip starts with keyword, plus following quoted lines.
    // Returns a malloc'd decoded string via *out/*outlen, or leaves found=0.
    // (implemented inline below)

    // Split text into NUL-terminated lines in place (strip trailing \r).
    char** all = NULL; size_t nall = 0, capa = 0;
    {
        char* p = text;
        while (1) {
            char* nl = strchr(p, '\n');
            if (nl) *nl = '\0';
            size_t l = strlen(p);
            if (l && p[l-1] == '\r') p[l-1] = '\0';
            if (nall == capa) { capa = capa ? capa*2 : 256; all = (char**)realloc(all, capa*sizeof(char*)); }
            all[nall++] = p;
            if (!nl) break;
            p = nl + 1;
        }
    }

    for (size_t li = 0; li <= nall; ++li) {
        int blank = (li == nall);   // sentinel: end-of-file closes the final stanza
        if (!blank) {
            const char* s = lstrip(all[li]);
            blank = (*s == '\0');
        }
        if (!blank) {
            const char* s = lstrip(all[li]);
            if (*s == '#') continue;   // skip comment lines
            ADD_LINE(all[li]);
            continue;
        }
        // stanza boundary: process what we have
        if (nlines) {
            char* idbuf = NULL; size_t idlen = 0; int have_id = 0;
            char* strbuf = NULL; size_t strlen_ = 0; int have_str = 0;
            for (int pass = 0; pass < 2; ++pass) {
                const char* kw = pass == 0 ? "msgid " : "msgstr ";
                size_t kwl = strlen(kw);
                for (size_t i = 0; i < nlines; ++i) {
                    const char* s = lstrip(lines[i]);
                    if (strncmp(s, kw, kwl) == 0) {
                        Buf b = {0,0,0};
                        decode_quoted(s + kwl, &b);
                        for (size_t j = i + 1; j < nlines; ++j) {
                            const char* c = lstrip(lines[j]);
                            if (*c != '"') break;
                            decode_quoted(c, &b);
                        }
                        if (pass == 0) { idbuf = b.p; idlen = b.len; have_id = 1; }
                        else           { strbuf = b.p; strlen_ = b.len; have_str = 1; }
                        break;
                    }
                }
            }
            if (have_id && have_str) put_entry(idbuf, idlen, strbuf, strlen_);
            else { free(idbuf); free(strbuf); }
        }
        nlines = 0;
    }

    qsort(g_ent, g_n, sizeof(Entry), cmp_entry);

    // Build the two string blobs (each entry's bytes followed by a NUL).
    Buf ids = {0,0,0}, strs = {0,0,0};
    size_t* idoff = (size_t*)malloc(g_n * sizeof(size_t));
    size_t* stroff = (size_t*)malloc(g_n * sizeof(size_t));
    for (size_t i = 0; i < g_n; ++i) {
        idoff[i] = ids.len;
        for (size_t k = 0; k < g_ent[i].idlen; ++k) buf_put(&ids, g_ent[i].id[k]);
        buf_put(&ids, '\0');
        stroff[i] = strs.len;
        for (size_t k = 0; k < g_ent[i].strlen_; ++k) buf_put(&strs, g_ent[i].str[k]);
        buf_put(&strs, '\0');
    }

    const uint32_t count = (uint32_t)g_n;
    const uint32_t keystart = 7u * 4u + 16u * count;          // start of the ids blob in the file
    const uint32_t valuestart = keystart + (uint32_t)ids.len; // start of the strs blob

    FILE* o = fopen(argv[2], "wb");
    if (!o) { fprintf(stderr, "po2mo: cannot write %s\n", argv[2]); return 1; }
    // Little-endian u32 writer.
    #define PUT32(v) do { uint32_t _v=(uint32_t)(v); unsigned char _b[4]={(unsigned char)_v,(unsigned char)(_v>>8),(unsigned char)(_v>>16),(unsigned char)(_v>>24)}; fwrite(_b,1,4,o); } while(0)

    PUT32(0x950412deu);          // magic
    PUT32(0);                    // version
    PUT32(count);                // number of strings
    PUT32(7u * 4u);              // offset of key table
    PUT32(7u * 4u + count * 8u); // offset of value table
    PUT32(0);                    // hash table size (0 -> binary search)
    PUT32(0);                    // hash table offset
    for (size_t i = 0; i < g_n; ++i) { PUT32(g_ent[i].idlen);  PUT32(keystart + idoff[i]); }
    for (size_t i = 0; i < g_n; ++i) { PUT32(g_ent[i].strlen_); PUT32(valuestart + stroff[i]); }
    fwrite(ids.p, 1, ids.len, o);
    fwrite(strs.p, 1, strs.len, o);
    fclose(o);

    printf("%s -> %s: %u entries\n", argv[1], argv[2], count);
    return 0;
}
