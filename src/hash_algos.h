// SPDX-License-Identifier: Apache-2.0
#pragma once
// =====================================================================
// hash_algos.h - self-contained, dependency-free MD5 / SHA-1 / SHA-256 / SHA-512 for the Tools menu's
// digest generators. Pure <cstdint>/<string> - NO wx, NO OS crypto - so it is one code path on every
// platform (Windows included; the old Win32 BCrypt/CNG fork is gone) and links into the headless
// hash_test with no dependencies.
//
// WHY HAND-ROLLED: these are non-security checksum/verification tools whose output is a FROZEN standard
// digest, exhaustively pinnable to the published NIST/RFC test vectors (see tests/hash_test.cpp). A
// self-authored impl keeps the permissive-future core free of any third-party provenance (same reasoning
// as src/diff_myers.h) and is byte-identical to any conformant library by construction.
//
// All word assembly is BYTE-WISE (never a reinterpret_cast over the buffer), so the code is endian-safe:
// correct on little-endian x86/arm64 AND big-endian, and on riscv64. Message length is tracked as a
// 64-bit byte count (128-bit bit-length for SHA-512), so files > 4 GB hash correctly - unlike the old
// BCrypt path whose ULONG length capped at 4 GB.
// =====================================================================
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>

namespace wxn { namespace hash {

enum class Algo { Md5, Sha1, Sha256, Sha512 };

namespace detail {

inline uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
inline uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint64_t rotr64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

// ---- MD5 (little-endian) ----------------------------------------------------------------------------
struct Md5 {
    uint32_t s[4]; uint64_t len = 0; unsigned char buf[64]; size_t n = 0;
    void init() { s[0]=0x67452301; s[1]=0xefcdab89; s[2]=0x98badcfe; s[3]=0x10325476; len=0; n=0; }
    static const uint32_t K[64];
    void block(const unsigned char* p) {
        static const int SH[64] = {
            7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
            5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
            4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
            6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21 };
        uint32_t M[16];
        for (int i = 0; i < 16; ++i)
            M[i] = (uint32_t)p[4*i] | ((uint32_t)p[4*i+1]<<8) | ((uint32_t)p[4*i+2]<<16) | ((uint32_t)p[4*i+3]<<24);
        uint32_t a=s[0], b=s[1], c=s[2], d=s[3];
        for (int i = 0; i < 64; ++i) {
            uint32_t f; int g;
            if (i < 16)      { f = (b & c) | (~b & d);        g = i; }
            else if (i < 32) { f = (d & b) | (~d & c);        g = (5*i + 1) & 15; }
            else if (i < 48) { f = b ^ c ^ d;                 g = (3*i + 5) & 15; }
            else             { f = c ^ (b | ~d);              g = (7*i) & 15; }
            f += a + K[i] + M[g];
            a = d; d = c; c = b; b += rotl32(f, SH[i]);
        }
        s[0]+=a; s[1]+=b; s[2]+=c; s[3]+=d;
    }
    void update(const unsigned char* p, size_t sz) {
        len += sz;
        while (sz) {
            size_t take = 64 - n; if (take > sz) take = sz;
            std::memcpy(buf + n, p, take); n += take; p += take; sz -= take;
            if (n == 64) { block(buf); n = 0; }
        }
    }
    void final(unsigned char out[16]) {
        uint64_t bits = len * 8;
        unsigned char pad = 0x80; update(&pad, 1);
        unsigned char z = 0; while (n != 56) update(&z, 1);
        unsigned char lb[8]; for (int i = 0; i < 8; ++i) lb[i] = (unsigned char)(bits >> (8*i));  // little-endian
        update(lb, 8);
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) out[4*i+j] = (unsigned char)(s[i] >> (8*j));
    }
};

// ---- SHA-1 / SHA-256 shared 64-byte, big-endian framing ---------------------------------------------
struct Sha1 {
    uint32_t s[5]; uint64_t len = 0; unsigned char buf[64]; size_t n = 0;
    void init() { s[0]=0x67452301; s[1]=0xEFCDAB89; s[2]=0x98BADCFE; s[3]=0x10325476; s[4]=0xC3D2E1F0; len=0; n=0; }
    void block(const unsigned char* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = ((uint32_t)p[4*i]<<24) | ((uint32_t)p[4*i+1]<<16) | ((uint32_t)p[4*i+2]<<8) | (uint32_t)p[4*i+3];
        for (int i = 16; i < 80; ++i) w[i] = rotl32(w[i-3]^w[i-8]^w[i-14]^w[i-16], 1);
        uint32_t a=s[0],b=s[1],c=s[2],d=s[3],e=s[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);            k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;                     k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d);   k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                     k = 0xCA62C1D6; }
            uint32_t t = rotl32(a,5) + f + e + k + w[i];
            e = d; d = c; c = rotl32(b,30); b = a; a = t;
        }
        s[0]+=a; s[1]+=b; s[2]+=c; s[3]+=d; s[4]+=e;
    }
    void update(const unsigned char* p, size_t sz) {
        len += sz;
        while (sz) { size_t take = 64 - n; if (take > sz) take = sz;
            std::memcpy(buf + n, p, take); n += take; p += take; sz -= take; if (n == 64) { block(buf); n = 0; } }
    }
    void final(unsigned char out[20]) {
        uint64_t bits = len * 8;
        unsigned char pad = 0x80; update(&pad, 1);
        unsigned char z = 0; while (n != 56) update(&z, 1);
        unsigned char lb[8]; for (int i = 0; i < 8; ++i) lb[i] = (unsigned char)(bits >> (8*(7-i)));  // big-endian
        update(lb, 8);
        for (int i = 0; i < 5; ++i) for (int j = 0; j < 4; ++j) out[4*i+j] = (unsigned char)(s[i] >> (8*(3-j)));
    }
};

struct Sha256 {
    uint32_t s[8]; uint64_t len = 0; unsigned char buf[64]; size_t n = 0;
    static const uint32_t K[64];
    void init() { s[0]=0x6a09e667; s[1]=0xbb67ae85; s[2]=0x3c6ef372; s[3]=0xa54ff53a;
                  s[4]=0x510e527f; s[5]=0x9b05688c; s[6]=0x1f83d9ab; s[7]=0x5be0cd19; len=0; n=0; }
    void block(const unsigned char* p) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = ((uint32_t)p[4*i]<<24) | ((uint32_t)p[4*i+1]<<16) | ((uint32_t)p[4*i+2]<<8) | (uint32_t)p[4*i+3];
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr32(w[i-15],7) ^ rotr32(w[i-15],18) ^ (w[i-15] >> 3);
            uint32_t s1 = rotr32(w[i-2],17) ^ rotr32(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=s[0],b=s[1],c=s[2],d=s[3],e=s[4],f=s[5],g=s[6],h=s[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr32(e,6) ^ rotr32(e,11) ^ rotr32(e,25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = h + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr32(a,2) ^ rotr32(a,13) ^ rotr32(a,22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        s[0]+=a; s[1]+=b; s[2]+=c; s[3]+=d; s[4]+=e; s[5]+=f; s[6]+=g; s[7]+=h;
    }
    void update(const unsigned char* p, size_t sz) {
        len += sz;
        while (sz) { size_t take = 64 - n; if (take > sz) take = sz;
            std::memcpy(buf + n, p, take); n += take; p += take; sz -= take; if (n == 64) { block(buf); n = 0; } }
    }
    void final(unsigned char out[32]) {
        uint64_t bits = len * 8;
        unsigned char pad = 0x80; update(&pad, 1);
        unsigned char z = 0; while (n != 56) update(&z, 1);
        unsigned char lb[8]; for (int i = 0; i < 8; ++i) lb[i] = (unsigned char)(bits >> (8*(7-i)));
        update(lb, 8);
        for (int i = 0; i < 8; ++i) for (int j = 0; j < 4; ++j) out[4*i+j] = (unsigned char)(s[i] >> (8*(3-j)));
    }
};

// ---- SHA-512 (128-byte block, 64-bit words, 128-bit length) -----------------------------------------
struct Sha512 {
    uint64_t s[8]; uint64_t len = 0; unsigned char buf[128]; size_t n = 0;
    static const uint64_t K[80];
    void init() {
        s[0]=0x6a09e667f3bcc908ULL; s[1]=0xbb67ae8584caa73bULL; s[2]=0x3c6ef372fe94f82bULL; s[3]=0xa54ff53a5f1d36f1ULL;
        s[4]=0x510e527fade682d1ULL; s[5]=0x9b05688c2b3e6c1fULL; s[6]=0x1f83d9abfb41bd6bULL; s[7]=0x5be0cd19137e2179ULL;
        len=0; n=0;
    }
    void block(const unsigned char* p) {
        uint64_t w[80];
        for (int i = 0; i < 16; ++i) {
            uint64_t v = 0; for (int j = 0; j < 8; ++j) v = (v << 8) | p[8*i+j];
            w[i] = v;
        }
        for (int i = 16; i < 80; ++i) {
            uint64_t s0 = rotr64(w[i-15],1) ^ rotr64(w[i-15],8) ^ (w[i-15] >> 7);
            uint64_t s1 = rotr64(w[i-2],19) ^ rotr64(w[i-2],61) ^ (w[i-2] >> 6);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint64_t a=s[0],b=s[1],c=s[2],d=s[3],e=s[4],f=s[5],g=s[6],h=s[7];
        for (int i = 0; i < 80; ++i) {
            uint64_t S1 = rotr64(e,14) ^ rotr64(e,18) ^ rotr64(e,41);
            uint64_t ch = (e & f) ^ (~e & g);
            uint64_t t1 = h + S1 + ch + K[i] + w[i];
            uint64_t S0 = rotr64(a,28) ^ rotr64(a,34) ^ rotr64(a,39);
            uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint64_t t2 = S0 + maj;
            h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        s[0]+=a; s[1]+=b; s[2]+=c; s[3]+=d; s[4]+=e; s[5]+=f; s[6]+=g; s[7]+=h;
    }
    void update(const unsigned char* p, size_t sz) {
        len += sz;
        while (sz) { size_t take = 128 - n; if (take > sz) take = sz;
            std::memcpy(buf + n, p, take); n += take; p += take; sz -= take; if (n == 128) { block(buf); n = 0; } }
    }
    void final(unsigned char out[64]) {
        uint64_t bitsLo = len << 3;           // low 64 bits of the 128-bit bit-length
        uint64_t bitsHi = len >> 61;          // high 64 bits (nonzero only past 2^61 bytes)
        unsigned char pad = 0x80; update(&pad, 1);
        unsigned char z = 0; while (n != 112) update(&z, 1);
        unsigned char lb[16];
        for (int i = 0; i < 8; ++i) lb[i]   = (unsigned char)(bitsHi >> (8*(7-i)));
        for (int i = 0; i < 8; ++i) lb[8+i] = (unsigned char)(bitsLo >> (8*(7-i)));
        update(lb, 16);
        for (int i = 0; i < 8; ++i) for (int j = 0; j < 8; ++j) out[8*i+j] = (unsigned char)(s[i] >> (8*(7-j)));
    }
};

inline std::string toHex(const unsigned char* d, size_t n) {
    static const char* H = "0123456789abcdef";
    std::string s; s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) { s.push_back(H[d[i] >> 4]); s.push_back(H[d[i] & 0xF]); }
    return s;
}

} // namespace detail

// One-shot lowercase-hex digest of a byte buffer.
inline std::string hexDigest(Algo a, const void* data, size_t len) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    switch (a) {
        case Algo::Md5:    { detail::Md5    h; h.init(); h.update(p, len); unsigned char o[16]; h.final(o); return detail::toHex(o, 16); }
        case Algo::Sha1:   { detail::Sha1   h; h.init(); h.update(p, len); unsigned char o[20]; h.final(o); return detail::toHex(o, 20); }
        case Algo::Sha256: { detail::Sha256 h; h.init(); h.update(p, len); unsigned char o[32]; h.final(o); return detail::toHex(o, 32); }
        case Algo::Sha512: { detail::Sha512 h; h.init(); h.update(p, len); unsigned char o[64]; h.final(o); return detail::toHex(o, 64); }
    }
    return std::string();
}

// K-tables (generated from the algorithm definitions; sin() for MD5, prime cube-roots for SHA-2).
inline const uint32_t detail::Md5::K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };

inline const uint32_t detail::Sha256::K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

inline const uint64_t detail::Sha512::K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL };

}} // namespace wxn::hash
