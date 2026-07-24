// SPDX-License-Identifier: Apache-2.0
//
// hash_test - headless self-test for the MD5/SHA-1/SHA-256/SHA-512 engine (src/hash_algos.h). Pure, no wx.
// Expected digests were generated with Python's hashlib (the reference), NOT typed from memory, and cover
// the padding edge cases (block-boundary lengths) plus a long multi-block message and the
// incremental-update == one-shot property.
//
//   cmake --build build --target hash_test && build/bin/hash_test
//
#include "hash_algos.h"

#include <cstdio>
#include <string>
#include <algorithm>

using namespace wxn::hash;

static int g_fail = 0, g_pass = 0;
static void check(bool ok, const char* what) { std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what); if (ok) ++g_pass; else ++g_fail; }

static std::string H(Algo a, const std::string& m) { return hexDigest(a, m.data(), m.size()); }

// Hash `m` feeding the incremental API in fixed-size chunks, to prove chunk boundaries don't affect output.
static std::string incr(Algo a, const std::string& m, size_t chunk)
{
    const unsigned char* p = reinterpret_cast<const unsigned char*>(m.data());
    const size_t sz = m.size();
    auto feed = [&](auto& h, unsigned char* o, size_t olen) {
        h.init();
        for (size_t i = 0; i < sz; i += chunk) h.update(p + i, std::min(chunk, sz - i));
        h.final(o);
        return detail::toHex(o, olen);
    };
    switch (a) {
        case Algo::Md5:    { detail::Md5    h; unsigned char o[16]; return feed(h, o, 16); }
        case Algo::Sha1:   { detail::Sha1   h; unsigned char o[20]; return feed(h, o, 20); }
        case Algo::Sha256: { detail::Sha256 h; unsigned char o[32]; return feed(h, o, 32); }
        case Algo::Sha512: { detail::Sha512 h; unsigned char o[64]; return feed(h, o, 64); }
    }
    return std::string();
}

struct Vec { const char* name; std::string msg; const char* md5; const char* sha1; const char* sha256; const char* sha512; };

int main()
{
    std::printf("hash_test\n");

    const Vec vecs[] = {
        { "empty", std::string(""),
          "d41d8cd98f00b204e9800998ecf8427e",
          "da39a3ee5e6b4b0d3255bfef95601890afd80709",
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e" },
        { "abc", std::string("abc"),
          "900150983cd24fb0d6963f7d28e17f72",
          "a9993e364706816aba3e25717850c26c9cd0d89d",
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f" },
        { "448bit", std::string("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
          "8215ef0796a20bcaaae116d3876c664a",
          "84983e441c3bd26ebaae4aa1f95129e5e54670f1",
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "204a8fc6dda82f0a0ced7beb8e08a41657c16ef468b228a8279be331a703c33596fd15c13b1b07f9aa1d3bea57789ca031ad85c7a71dd70354ec631238ca3445" },
        { "block64", std::string(64, 'a'),   // exactly one 64-byte block -> padding forces a 2nd block
          "014842d480b571495a4a0363793f7367",
          "0098ba824b5c16427bd7a1122a5a442a25ec644d",
          "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb",
          "01d35c10c6c38c2dcf48f7eebb3235fb5ad74a65ec4cd016e2354c637a8fb49b695ef3c1d6f7ae4cd74d78cc9c9bcac9d4f23a73019998a7f73038a5c9b2dbde" },
        { "block128", std::string(128, 'a'), // exactly one SHA-512 block
          "e510683b3f5ffe4093d021808bc6ff70",
          "ad5b3fdbcb526778c2839d2f151ea753995e26a0",
          "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e",
          "b73d1929aa615934e61a871596b3f3b33359f42b8175602e89f7e06e5f658a243667807ed300314b95cacdd579f3e33abdfbe351909519a846d465c59582f321" },
        { "million_a", std::string(1000000, 'a'),  // long, multi-block, big length field
          "7707d6ae4e027c70eea2a935c2296f21",
          "34aa973cd4c4daa4f61eeb2bdbad27316534016f",
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973ebde0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b" },
    };

    std::printf("[known-answer test vectors (vs Python hashlib)]\n");
    for (const Vec& v : vecs) {
        char lbl[64];
        std::snprintf(lbl, sizeof lbl, "MD5    %s", v.name);    check(H(Algo::Md5, v.msg)    == v.md5, lbl);
        std::snprintf(lbl, sizeof lbl, "SHA1   %s", v.name);    check(H(Algo::Sha1, v.msg)   == v.sha1, lbl);
        std::snprintf(lbl, sizeof lbl, "SHA256 %s", v.name);    check(H(Algo::Sha256, v.msg) == v.sha256, lbl);
        std::snprintf(lbl, sizeof lbl, "SHA512 %s", v.name);    check(H(Algo::Sha512, v.msg) == v.sha512, lbl);
    }

    std::printf("[incremental update == one-shot (odd chunk sizes)]\n");
    const Algo algos[] = { Algo::Md5, Algo::Sha1, Algo::Sha256, Algo::Sha512 };
    const char* an[]   = { "MD5", "SHA1", "SHA256", "SHA512" };
    int badIncr = 0;
    for (const Vec& v : vecs)
        for (int ai = 0; ai < 4; ++ai)
            for (size_t chunk : { (size_t)1, (size_t)7, (size_t)63, (size_t)64, (size_t)65, (size_t)128 })
                if (v.msg.size() && incr(algos[ai], v.msg, chunk) != H(algos[ai], v.msg)) { ++badIncr; (void)an; }
    check(badIncr == 0, "all algos: chunked update reproduces the one-shot digest at every chunk size");

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
