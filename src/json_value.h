// SPDX-License-Identifier: Apache-2.0
//
// wxNote - the minimal JSON value type and recursive-descent reader (the parse side only).
// Copyright 2026 The wxNote Authors.
//
// WHY THIS EXISTS. No JSON library is vendored: every schema the app reads is tiny, so a small
// hand-rolled reader beats a dependency. This pair grew up as private nested structs inside
// KeymapStore (src/keymap_store.h, the shortcuts.json loader) and is lifted out UNCHANGED in
// behavior so the next JSON-reading store reuses this copy instead of growing a second one that
// drifts. The matching hand WRITER stayed behind in keymap_store.h - it is schema-specific.
//
// THE CONTRACT, verbatim from the keymap_store.h origin. Untrusted input: every failure degrades to
// "ignore and keep resolving", never a throw/crash (a bad hand-edit must not brick startup).
// JsonParser(text).parse(out) answers false on any malformed input - including input nested past
// kMaxDepth - and the caller falls back to its own defaults. No exception escapes this header.
//
// This header is deliberately free of wx - and of <cctype>/<cstdlib>; see number()/toNumber() - so
// it is unit-testable against plain strings and usable below the UI layer. The one wx convenience
// the nested original carried, Json::wxstr(), is dropped: a caller converts a Str node at the call
// site with wxString::FromUTF8(v.str.c_str()), which is exactly what that member did.
//
// Used by: src/keymap_store.h (shortcuts.json), which aliases Json/JsonParser back into its private
// section so its parsing code reads exactly as it did before the lift.

#pragma once

#include <string>
#include <utility>   // std::pair - Json::obj holds insertion-ordered (key, value) rows, not a map
#include <vector>

namespace wxnjson {

struct Json
{
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false; double num = 0; std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;   // insertion-ordered
    const Json* member(const char* k) const
    {
        if (type != Obj) return nullptr;
        for (const auto& kv : obj) if (kv.first == k) return &kv.second;
        return nullptr;
    }
};

struct JsonParser
{
    const std::string& s; size_t i = 0; bool ok = true;
    // Nesting bound: object()/array() recurse back through value(), so without a cap a file of a
    // few thousand nested brackets overflows the stack - an UNCATCHABLE crash that would break the
    // "a bad hand-edit must not brick startup" contract above (the parse must FAIL, load must fall
    // back to defaults). Siblings share a level (the counter tracks true nesting only), so wide-
    // but-shallow input is unaffected; the real schema nests ~4 levels, 64 is generous. Mirrors
    // keymap_store.h's activeSchemeChain() parent-cycle guard.
    static constexpr int kMaxDepth = 64;
    int depth = 0;
    explicit JsonParser(const std::string& src) : s(src) {}
    void ws() { while (i < s.size() && (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r')) ++i; }
    bool parse(Json& out) { ws(); bool r = value(out); ws(); return r && ok; }
    bool value(Json& v)
    {
        ws();
        if (i >= s.size()) return fail();
        if (depth >= kMaxDepth) return fail();   // too deeply nested: give up cleanly (see kMaxDepth)
        ++depth;
        const bool r = valueDispatch(v);
        --depth;
        return r;
    }
    bool valueDispatch(Json& v)
    {
        char c = s[i];
        if (c == '{') return object(v);
        if (c == '[') return array(v);
        if (c == '"') { v.type = Json::Str; return str(v.str); }
        if (c == 't' || c == 'f') return boolean(v);
        if (c == 'n') { if (s.compare(i,4,"null")==0){ i+=4; v.type=Json::Null; return true;} return fail(); }
        return number(v);
    }
    bool object(Json& v)
    {
        v.type = Json::Obj; ++i; ws();
        if (i < s.size() && s[i]=='}') { ++i; return true; }
        for (;;)
        {
            ws(); if (i>=s.size()||s[i]!='"') return fail();
            std::string key; if (!str(key)) return false;
            ws(); if (i>=s.size()||s[i]!=':') return fail(); ++i;
            Json child; if (!value(child)) return false;
            v.obj.emplace_back(std::move(key), std::move(child));
            ws(); if (i>=s.size()) return fail();
            if (s[i]==',') { ++i; continue; }
            if (s[i]=='}') { ++i; return true; }
            return fail();
        }
    }
    bool array(Json& v)
    {
        v.type = Json::Arr; ++i; ws();
        if (i < s.size() && s[i]==']') { ++i; return true; }
        for (;;)
        {
            Json child; if (!value(child)) return false;
            v.arr.push_back(std::move(child));
            ws(); if (i>=s.size()) return fail();
            if (s[i]==',') { ++i; continue; }
            if (s[i]==']') { ++i; return true; }
            return fail();
        }
    }
    bool str(std::string& out)
    {
        if (i>=s.size()||s[i]!='"') return fail(); ++i;
        while (i < s.size())
        {
            char c = s[i++];
            if (c=='"') return true;
            if (c=='\\')
            {
                if (i>=s.size()) return fail();
                char e = s[i++];
                switch (e)
                {
                    case '"': out+='"'; break;   case '\\': out+='\\'; break; case '/': out+='/'; break;
                    case 'b': out+='\b'; break;  case 'f': out+='\f'; break;  case 'n': out+='\n'; break;
                    case 'r': out+='\r'; break;  case 't': out+='\t'; break;
                    case 'u':
                    {
                        unsigned cp;
                        if (!hex4(cp)) return fail();
                        // Surrogate handling. JSON escapes astral code points as a \uD8xx\uDCxx
                        // PAIR; encoding the halves separately produces CESU-8, which is not valid
                        // UTF-8 - wxString::FromUTF8 answers EMPTY for it, losing the whole field
                        // instead of one character. Combine pairs; map a LONE half - and U+0000,
                        // which silently truncates every downstream c_str() consumer - to U+FFFD.
                        if (cp >= 0xD800 && cp <= 0xDBFF &&
                            i + 1 < s.size() && s[i] == '\\' && s[i+1] == 'u')
                        {
                            i += 2;
                            unsigned lo;
                            if (!hex4(lo)) return fail();
                            if (lo >= 0xDC00 && lo <= 0xDFFF)
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            else
                            {
                                appendUtf8(out, 0xFFFD);   // the orphaned high half
                                cp = (lo == 0 || (lo >= 0xD800 && lo <= 0xDFFF)) ? 0xFFFD : lo;
                            }
                        }
                        else if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
                        if (cp == 0) cp = 0xFFFD;
                        appendUtf8(out, cp);
                        break;
                    }
                    default: return fail();
                }
            }
            else out += c;
        }
        return fail();
    }
    bool boolean(Json& v)
    {
        if (s.compare(i,4,"true")==0){ i+=4; v.type=Json::Bool; v.b=true; return true; }
        if (s.compare(i,5,"false")==0){ i+=5; v.type=Json::Bool; v.b=false; return true; }
        return fail();
    }
    bool number(Json& v)
    {
        // '0'..'9' spelled out instead of isdigit(): byte-identical (the C standard pins isdigit to
        // exactly the ten decimal digits in every locale) and it keeps <cctype> out of this header.
        size_t start = i;
        while (i<s.size() && ((s[i]>='0'&&s[i]<='9')||s[i]=='-'||s[i]=='+'||s[i]=='.'||s[i]=='e'||s[i]=='E')) ++i;
        if (i==start) return fail();
        v.type = Json::Num; v.num = toNumber(s.substr(start, i-start));
        return true;
    }
    bool hex4(unsigned& cp)
    {
        if (i + 4 > s.size()) return false;
        cp = 0;
        for (int k = 0; k < 4; ++k)
        {
            char h = s[i++]; cp <<= 4;
            if (h>='0'&&h<='9') cp |= h-'0'; else if (h>='a'&&h<='f') cp |= h-'a'+10;
            else if (h>='A'&&h<='F') cp |= h-'A'+10; else return false;
        }
        return true;
    }
    static void appendUtf8(std::string& out, unsigned cp)
    {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0|(cp>>6)); out += (char)(0x80|(cp&0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0|(cp>>12)); out += (char)(0x80|((cp>>6)&0x3F)); out += (char)(0x80|(cp&0x3F)); }
        else { out += (char)(0xF0|(cp>>18)); out += (char)(0x80|((cp>>12)&0x3F)); out += (char)(0x80|((cp>>6)&0x3F)); out += (char)(0x80|(cp&0x3F)); }
    }
    // The atof() stand-in that keeps <cstdlib> out of this header. Same grammar strtod applies in
    // the "C" locale over the characters number() collects ([0-9+-.eE]): optional sign, a digit run
    // with an optional fraction, then an exponent that only counts when at least one digit follows
    // it - longest-valid-prefix, like strtod ("1e+" is 1, "1.2.3" is 1.2, "--5" is 0) - and no
    // significand digit at all is atof's no-conversion result, 0. Deliberately locale-INDEPENDENT
    // where atof was not: a comma-decimal LC_NUMERIC can no longer misread "1.5" - JSON's spelling
    // is fixed, so the fixed grammar is the intended read. Digits accumulate raw; the decimal shift
    // is applied in <=22-decade chunks, because 10^k is exact in a double up to k=22, so each chunk
    // is ONE correctly-rounded multiply/divide - bit-identical to strtod for everything the real
    // schemas carry (small integers like "version"). Absurd magnitudes saturate to inf/0, never a
    // throw.
    static double toNumber(const std::string& t)
    {
        size_t p = 0; const size_t n = t.size();
        bool neg = false;
        if (p < n && (t[p]=='+' || t[p]=='-')) { neg = (t[p]=='-'); ++p; }
        double v = 0; bool any = false; long long frac = 0;
        while (p < n && t[p]>='0' && t[p]<='9') { v = v*10 + (t[p]-'0'); ++p; any = true; }
        if (p < n && t[p]=='.')
        {
            ++p;
            while (p < n && t[p]>='0' && t[p]<='9') { v = v*10 + (t[p]-'0'); ++frac; ++p; any = true; }
        }
        if (!any) return 0;                        // no digits at all: no conversion, like atof
        long long ex = 0;
        if (p < n && (t[p]=='e' || t[p]=='E'))
        {
            size_t q = p + 1; bool eneg = false, eany = false;
            if (q < n && (t[q]=='+' || t[q]=='-')) { eneg = (t[q]=='-'); ++q; }
            while (q < n && t[q]>='0' && t[q]<='9')
            { if (ex < 100000000) ex = ex*10 + (t[q]-'0'); ++q; eany = true; }   // clamped far past
            if (!eany) ex = 0;                     // double's range (the loop below saturates anyway);
            else if (eneg) ex = -ex;               // a digitless exponent is not consumed - strtod
        }                                          // backtracks to the bare significand
        long long scale = ex - frac;               // the decimal shift still owed to the digit run
        for (int guard = 0; scale != 0 && guard < 60; ++guard)   // 60 chunks > 1300 decades: walks
        {                                                        // past all of double; the rest are
            long long step = scale < 0 ? -scale : scale;         // inf/0 no-ops
            if (step > 22) step = 22;
            double p10 = 1; for (long long k = 0; k < step; ++k) p10 *= 10;   // exact for k <= 22
            if (scale > 0) { v *= p10; scale -= step; }
            else           { v /= p10; scale += step; }
        }
        return neg ? -v : v;
    }
    bool fail() { ok = false; return false; }
};

} // namespace wxnjson
