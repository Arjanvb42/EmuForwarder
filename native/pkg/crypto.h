#pragma once
// The cryptography a PS4 package needs, without libraries (runs on the PS4 and on a PC):
// SHA-256, HMAC-SHA256, AES-128 (CBC and XTS, encryption only) and 2048-bit RSA (modular
// exponentiation with Montgomery multiplication).

#include <stdint.h>
#include <string.h>

#include <vector>

namespace pkgcrypto {

// ---- SHA-256 -------------------------------------------------------------------------------
struct Sha256 {
    uint32_t h[8];
    uint8_t buf[64];
    uint64_t length = 0;
    size_t used = 0;

    Sha256() { reset(); }
    void reset()
    {
        static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        memcpy(h, init, sizeof(h));
        length = 0;
        used = 0;
    }
    static uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void block(const uint8_t *p)
    {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = uint32_t(p[4 * i]) << 24 | uint32_t(p[4 * i + 1]) << 16 | uint32_t(p[4 * i + 2]) << 8 | p[4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 = hh + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void update(const void *data, size_t n)
    {
        const uint8_t *p = static_cast<const uint8_t *>(data);
        length += n;
        if (used) {
            const size_t take = n < 64 - used ? n : 64 - used;
            memcpy(buf + used, p, take);
            used += take; p += take; n -= take;
            if (used == 64) { block(buf); used = 0; }
        }
        while (n >= 64) { block(p); p += 64; n -= 64; }
        if (n) { memcpy(buf, p, n); used = n; }
    }
    void finish(uint8_t out[32])
    {
        const uint64_t bits = length * 8;
        const uint8_t one = 0x80, zero = 0;
        update(&one, 1);
        while (used != 56) update(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; ++i) len[i] = uint8_t(bits >> (56 - 8 * i));
        update(len, 8);
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 4; ++j) out[4 * i + j] = uint8_t(h[i] >> (24 - 8 * j));
    }
};

using Hash = std::vector<uint8_t>;

static Hash sha256(const void *data, size_t n)
{
    Sha256 s;
    s.update(data, n);
    Hash out(32);
    s.finish(out.data());
    return out;
}
static Hash sha256(const std::vector<uint8_t> &v) { return sha256(v.data(), v.size()); }

static Hash hmac_sha256(const uint8_t *key, size_t key_len, const void *data, size_t n)
{
    uint8_t k[64] = {0};
    if (key_len > 64) {
        const Hash kh = sha256(key, key_len);
        memcpy(k, kh.data(), 32);
    } else {
        memcpy(k, key, key_len);
    }
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    Sha256 inner;
    inner.update(ipad, 64);
    inner.update(data, n);
    uint8_t ih[32];
    inner.finish(ih);
    Sha256 outer;
    outer.update(opad, 64);
    outer.update(ih, 32);
    Hash out(32);
    outer.finish(out.data());
    return out;
}
static Hash hmac_sha256(const Hash &key, const void *data, size_t n) { return hmac_sha256(key.data(), key.size(), data, n); }

// ---- AES-128 (encryption) ------------------------------------------------------------------
struct Aes128 {
    uint8_t rk[176];
    static const uint8_t *sbox()
    {
        static const uint8_t s[256] = {
            0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
            0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
            0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
            0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
            0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
            0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
            0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
            0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};
        return s;
    }
    explicit Aes128(const uint8_t key[16])
    {
        const uint8_t *s = sbox();
        static const uint8_t rcon[10] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36};
        memcpy(rk, key, 16);
        for (int i = 4; i < 44; ++i) {
            uint8_t t[4];
            memcpy(t, rk + 4 * (i - 1), 4);
            if (i % 4 == 0) {
                const uint8_t u = t[0];
                t[0] = s[t[1]] ^ rcon[i / 4 - 1]; t[1] = s[t[2]]; t[2] = s[t[3]]; t[3] = s[u];
            }
            for (int j = 0; j < 4; ++j) rk[4 * i + j] = rk[4 * (i - 4) + j] ^ t[j];
        }
    }
    static uint8_t xt(uint8_t x) { return uint8_t((x << 1) ^ ((x >> 7) * 0x1b)); }
    void encrypt(uint8_t b[16]) const
    {
        const uint8_t *s = sbox();
        for (int i = 0; i < 16; ++i) b[i] ^= rk[i];
        for (int round = 1; round <= 10; ++round) {
            uint8_t t[16];
            for (int i = 0; i < 16; ++i) t[i] = s[b[i]];
            // shift rows (column-major state)
            uint8_t r[16];
            for (int c = 0; c < 4; ++c)
                for (int row = 0; row < 4; ++row) r[4 * c + row] = t[4 * ((c + row) % 4) + row];
            if (round < 10) {
                for (int c = 0; c < 4; ++c) {
                    uint8_t *col = r + 4 * c;
                    const uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3], all = a0 ^ a1 ^ a2 ^ a3;
                    col[0] ^= all ^ xt(a0 ^ a1); col[1] ^= all ^ xt(a1 ^ a2);
                    col[2] ^= all ^ xt(a2 ^ a3); col[3] ^= all ^ xt(a3 ^ a0);
                }
            }
            for (int i = 0; i < 16; ++i) b[i] = r[i] ^ rk[16 * round + i];
        }
    }
};

// AES-128-CBC encryption in place (length a multiple of 16, no padding)
static void aes_cbc_encrypt(uint8_t *data, size_t n, const uint8_t key[16], const uint8_t iv[16])
{
    Aes128 aes(key);
    uint8_t prev[16];
    memcpy(prev, iv, 16);
    for (size_t off = 0; off + 16 <= n; off += 16) {
        for (int i = 0; i < 16; ++i) data[off + i] ^= prev[i];
        aes.encrypt(data + off);
        memcpy(prev, data + off, 16);
    }
}

// XTS-AES-128 sector encryption, as LibOrbisPkg's XtsBlockTransform
struct Xts {
    Aes128 data, tweak;
    Xts(const uint8_t data_key[16], const uint8_t tweak_key[16]) : data(data_key), tweak(tweak_key) {}
    void encrypt_sector(uint8_t *sector, size_t n, uint64_t number) const
    {
        uint8_t t[16] = {0};
        for (int i = 0; i < 8; ++i) t[i] = uint8_t(number >> (8 * i));
        tweak.encrypt(t);
        for (size_t off = 0; off < n; off += 16) {
            uint8_t x[16];
            for (int i = 0; i < 16; ++i) x[i] = sector[off + i] ^ t[i];
            data.encrypt(x);
            for (int i = 0; i < 16; ++i) sector[off + i] = x[i] ^ t[i];
            int feedback = 0;
            for (int k = 0; k < 16; ++k) {
                const uint8_t tmp = t[k];
                t[k] = uint8_t(2 * t[k] | feedback);
                feedback = (tmp & 0x80) >> 7;
            }
            if (feedback) t[0] ^= 0x87;
        }
    }
};

// ---- RSA-2048 ------------------------------------------------------------------------------
// Numbers are 64 little-endian 32-bit words; input and output are 256 big-endian bytes.
struct Mont {
    static const int N = 64;
    uint32_t n[N], r2[N], one_m[N];
    uint32_t n0inv;

    static void from_be(const uint8_t *be, uint32_t *w)
    {
        for (int i = 0; i < N; ++i)
            w[i] = uint32_t(be[255 - 4 * i]) | uint32_t(be[254 - 4 * i]) << 8 | uint32_t(be[253 - 4 * i]) << 16 |
                   uint32_t(be[252 - 4 * i]) << 24;
    }
    static void to_be(const uint32_t *w, uint8_t *be)
    {
        for (int i = 0; i < N; ++i) {
            be[255 - 4 * i] = uint8_t(w[i]); be[254 - 4 * i] = uint8_t(w[i] >> 8);
            be[253 - 4 * i] = uint8_t(w[i] >> 16); be[252 - 4 * i] = uint8_t(w[i] >> 24);
        }
    }
    static int cmp(const uint32_t *a, const uint32_t *b)
    {
        for (int i = N - 1; i >= 0; --i)
            if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
        return 0;
    }
    static uint32_t sub(uint32_t *a, const uint32_t *b)  // a -= b, returns borrow
    {
        uint64_t borrow = 0;
        for (int i = 0; i < N; ++i) {
            const uint64_t d = uint64_t(a[i]) - b[i] - borrow;
            a[i] = uint32_t(d);
            borrow = (d >> 63) & 1;
        }
        return uint32_t(borrow);
    }
    explicit Mont(const uint8_t modulus_be[256])
    {
        from_be(modulus_be, n);
        // n0inv = -n[0]^-1 mod 2^32 (Newton iteration)
        uint32_t inv = 1;
        for (int i = 0; i < 5; ++i) inv *= 2 - n[0] * inv;
        n0inv = 0u - inv;
        // r2 = 2^(2*2048) mod n, by doubling 1 with reduction
        uint32_t x[N] = {1};
        for (int i = 0; i < 2 * 32 * N; ++i) {
            const uint32_t carry = x[N - 1] >> 31;
            for (int j = N - 1; j > 0; --j) x[j] = (x[j] << 1) | (x[j - 1] >> 31);
            x[0] <<= 1;
            if (carry || cmp(x, n) >= 0) sub(x, n);
        }
        memcpy(r2, x, sizeof(x));
        uint32_t one[N] = {1};
        mul(one, r2, one_m);
    }
    // out = a * b / R mod n (CIOS)
    void mul(const uint32_t *a, const uint32_t *b, uint32_t *out) const
    {
        uint32_t t[N + 2] = {0};
        for (int i = 0; i < N; ++i) {
            uint64_t c = 0;
            for (int j = 0; j < N; ++j) {
                const uint64_t s = uint64_t(t[j]) + uint64_t(a[j]) * b[i] + c;
                t[j] = uint32_t(s);
                c = s >> 32;
            }
            uint64_t s = uint64_t(t[N]) + c;
            t[N] = uint32_t(s);
            t[N + 1] = uint32_t(s >> 32);
            const uint32_t m = t[0] * n0inv;
            c = (uint64_t(t[0]) + uint64_t(m) * n[0]) >> 32;
            for (int j = 1; j < N; ++j) {
                const uint64_t s2 = uint64_t(t[j]) + uint64_t(m) * n[j] + c;
                t[j - 1] = uint32_t(s2);
                c = s2 >> 32;
            }
            s = uint64_t(t[N]) + c;
            t[N - 1] = uint32_t(s);
            t[N] = t[N + 1] + uint32_t(s >> 32);
        }
        if (t[N] || cmp(t, n) >= 0) sub(t, n);
        memcpy(out, t, N * 4);
    }
    // out_be = base_be ^ exp_be mod n (base < n)
    void pow(const uint8_t base_be[256], const uint8_t *exp_be, size_t exp_len, uint8_t out_be[256]) const
    {
        uint32_t base[N], bm[N], acc[N];
        from_be(base_be, base);
        mul(base, r2, bm);
        memcpy(acc, one_m, sizeof(acc));
        bool started = false;
        for (size_t i = 0; i < exp_len; ++i)
            for (int bit = 7; bit >= 0; --bit) {
                if (started) mul(acc, acc, acc);
                if ((exp_be[i] >> bit) & 1) { mul(acc, bm, acc); started = true; }
            }
        uint32_t one[N] = {1}, res[N];
        mul(acc, one, res);
        to_be(res, out_be);
    }
};

// RSA "encryption" with the public exponent 65537
static std::vector<uint8_t> rsa_public(const uint8_t modulus[256], const uint8_t value[256])
{
    static const uint8_t e[3] = {0x01, 0x00, 0x01};
    std::vector<uint8_t> out(256);
    Mont(modulus).pow(value, e, 3, out.data());
    return out;
}

// PKCS#1 v1.5 signature of a SHA-256 hash with a private exponent
static std::vector<uint8_t> rsa_sign_sha256(const uint8_t modulus[256], const uint8_t private_exponent[256],
                                            const uint8_t hash[32])
{
    static const uint8_t prefix[19] = {0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
                                       0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};
    uint8_t em[256];
    em[0] = 0x00;
    em[1] = 0x01;
    const size_t pad_end = 256 - 32 - 19 - 1;
    memset(em + 2, 0xff, pad_end - 2);
    em[pad_end] = 0x00;
    memcpy(em + pad_end + 1, prefix, 19);
    memcpy(em + 256 - 32, hash, 32);
    std::vector<uint8_t> out(256);
    Mont(modulus).pow(em, private_exponent, 256, out.data());
    return out;
}

}  // namespace pkgcrypto
