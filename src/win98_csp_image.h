// SPDX-License-Identifier: GPL-2.0-or-later
// Validate a cryptographic service provider image before loading it.
#pragma once
#include <string.h>
namespace Win98CspImage
{
typedef unsigned U;
typedef unsigned char B;
constexpr U KeyOffset1 = 0xc3f0, KeyOffset2 = 0xc490, KeyBytes = 0x9c;
constexpr B Rc4Seed[5] = {0xa2, 0x17, 0x9c, 0x98, 0xca};

inline void Rc4(const B *key, U length, B *data, U size)
{
    B s[256];
    for (U i = 0; i < 256; ++i) s[i] = (B)i;
    for (U i = 0, j = 0; i < 256; ++i)
    {
        j = (j + s[i] + key[i % length]) & 255;
        const B t = s[i]; s[i] = s[j]; s[j] = t;
    }
    for (U n = 0, i = 0, j = 0; n < size; ++n)
    {
        i = (i + 1) & 255;
        j = (j + s[i]) & 255;
        const B t = s[i]; s[i] = s[j]; s[j] = t;
        data[n] ^= s[(s[i] + s[j]) & 255];
    }
}

// MD5, RFC 1321.
struct Md5
{
    U state[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    unsigned long long length = 0;
    B buffer[64];
    static U Rotate(U x, U c) { return (x << c) | (x >> (32 - c)); }
    void Block(const B *p)
    {
        static const U k[64] = {
            0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
            0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
            0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
            0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
            0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
            0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
            0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
            0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
        static const U r[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
        U w[16];
        for (U i = 0; i < 16; ++i) w[i] = p[i * 4] | p[i * 4 + 1] << 8 | p[i * 4 + 2] << 16 | (U)p[i * 4 + 3] << 24;
        U a = state[0], b = state[1], c = state[2], d = state[3];
        for (U i = 0; i < 64; ++i)
        {
            U f, g;
            if (i < 16) { f = (b & c) | (~b & d); g = i; }
            else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
            else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) & 15; }
            else { f = c ^ (b | ~d); g = (7 * i) & 15; }
            const U t = d;
            d = c; c = b;
            b = b + Rotate(a + f + k[i] + w[g], r[i]);
            a = t;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    }
    void Update(const B *p, U n)
    {
        U fill = (U)(length & 63);
        length += n;
        while (n)
        {
            U take = 64 - fill < n ? 64 - fill : n;
            memcpy(buffer + fill, p, take);
            fill += take; p += take; n -= take;
            if (fill == 64) { Block(buffer); fill = 0; }
        }
    }
    void Final(B out[16])
    {
        const unsigned long long bits = length * 8;
        const B one = 0x80, zero = 0;
        Update(&one, 1);
        while ((length & 63) != 56) Update(&zero, 1);
        B tail[8];
        for (U i = 0; i < 8; ++i) tail[i] = (B)(bits >> (8 * i));
        Update(tail, 8);
        for (U i = 0; i < 4; ++i)
            for (U j = 0; j < 4; ++j) out[i * 4 + j] = (B)(state[i] >> (8 * j));
    }
};

// Little-endian numbers of up to 2048 bits, 32-bit limbs.
struct Number
{
    static constexpr U Limbs = 66;
    U v[Limbs] = {};
};
inline int Compare(const Number &a, const Number &b)
{
    for (int i = Number::Limbs - 1; i >= 0; --i)
        if (a.v[i] != b.v[i]) return a.v[i] < b.v[i] ? -1 : 1;
    return 0;
}
inline void Subtract(Number &a, const Number &b)
{
    unsigned long long borrow = 0;
    for (U i = 0; i < Number::Limbs; ++i)
    {
        const unsigned long long d = (unsigned long long)a.v[i] - b.v[i] - borrow;
        a.v[i] = (U)d;
        borrow = (d >> 63) & 1;
    }
}
inline void Double(Number &a)
{
    U carry = 0;
    for (U i = 0; i < Number::Limbs; ++i)
    {
        const U next = a.v[i] >> 31;
        a.v[i] = a.v[i] << 1 | carry;
        carry = next;
    }
}
inline void Add(Number &a, const Number &b)
{
    unsigned long long carry = 0;
    for (U i = 0; i < Number::Limbs; ++i)
    {
        const unsigned long long s = (unsigned long long)a.v[i] + b.v[i] + carry;
        a.v[i] = (U)s;
        carry = s >> 32;
    }
}
// a * b mod n by doubling and adding, a and b below n.
inline Number MulMod(const Number &a, const Number &b, const Number &n, U bits)
{
    Number r;
    for (int i = (int)bits - 1; i >= 0; --i)
    {
        Double(r);
        if (Compare(r, n) >= 0) Subtract(r, n);
        if ((b.v[i / 32] >> (i % 32)) & 1)
        {
            Add(r, a);
            if (Compare(r, n) >= 0) Subtract(r, n);
        }
    }
    return r;
}
inline Number PowMod(Number base, U exponent, const Number &n, U bits)
{
    Number result;
    result.v[0] = 1;
    while (Compare(base, n) >= 0) Subtract(base, n);
    for (int i = 31; i >= 0; --i)
    {
        result = MulMod(result, result, n, bits);
        if ((exponent >> i) & 1) result = MulMod(result, base, n, bits);
    }
    return result;
}

// One decrypted BSAFE key against one signature. `key` is the 9Ch bytes out
// of ADVAPI32 before decryption, `variant` the XOR value of the RC4 key.
inline bool Verify(const B *encrypted_key, B variant, const B *signature, U signature_bytes, const B digest[16])
{
    B key[KeyBytes];
    memcpy(key, encrypted_key, KeyBytes);
    B seed[5];
    for (U i = 0; i < 5; ++i) seed[i] = Rc4Seed[i] ^ variant;
    Rc4(seed, 5, key, KeyBytes);
    auto dword = [&](U at) { return key[at] | key[at + 1] << 8 | key[at + 2] << 16 | (U)key[at + 3] << 24; };
    if (memcmp(key, "RSA1", 4)) return false;
    const U keylen = dword(4), bitlen = dword(8), datalen = dword(12), exponent = dword(16);
    if (bitlen > 2048 || keylen > KeyBytes - 20 || datalen + 1 > keylen || bitlen / 8 > keylen) return false;
    const U bytes = bitlen / 8;
    if (signature_bytes < bytes) return false;
    Number n, s;
    memcpy(n.v, key + 20, bytes);
    memcpy(s.v, signature, bytes);
    const Number m = PowMod(s, exponent, n, bitlen);
    B plain[256];
    memcpy(plain, m.v, bytes);
    if (memcmp(plain, digest, 16)) return false;
    if (plain[bytes - 1] != 0 || plain[bytes - 2] != 1) return false;
    return plain[0x10] == 0 && plain[0x11] == 0xff;
}
} // namespace Win98CspImage
