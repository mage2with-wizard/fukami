#include "disc_extract.h"
#include <cstring>
#include <algorithm>
namespace fukami::disc::detail {
namespace {
struct PortableSha256 {
    uint32_t state[8];
    uint64_t bits;
    uint32_t used;
    unsigned char block[64];
};
static_assert(sizeof(PortableSha256) <= 128);

uint32_t rotr32(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

void sha256Transform(PortableSha256& c)
{
    static constexpr uint32_t k[64] = {
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
        w[i] = (uint32_t(c.block[4 * i]) << 24) | (uint32_t(c.block[4 * i + 1]) << 16) |
               (uint32_t(c.block[4 * i + 2]) << 8) | c.block[4 * i + 3];
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = c.state[0], b = c.state[1], cc = c.state[2], d = c.state[3];
    uint32_t e = c.state[4], f = c.state[5], g = c.state[6], h = c.state[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t t1 = h + (rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
        const uint32_t t2 = (rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
        h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    c.state[0] += a; c.state[1] += b; c.state[2] += cc; c.state[3] += d;
    c.state[4] += e; c.state[5] += f; c.state[6] += g; c.state[7] += h;
}
}  // namespace

Sha256::Sha256()
{
    auto* c = reinterpret_cast<PortableSha256*>(ctx_);
    static constexpr uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::memcpy(c->state, init, sizeof init);
    c->bits = 0;
    c->used = 0;
}

void Sha256::update(const void* data, size_t len)
{
    auto* c = reinterpret_cast<PortableSha256*>(ctx_);
    const auto* p = static_cast<const uint8_t*>(data);
    c->bits += uint64_t(len) * 8u;
    while (len) {
        const size_t n = std::min<size_t>(len, 64u - c->used);
        std::memcpy(c->block + c->used, p, n);
        c->used += uint32_t(n);
        p += n;
        len -= n;
        if (c->used == 64u) {
            sha256Transform(*c);
            c->used = 0;
        }
    }
}

std::string Sha256::finishHex()
{
    auto* c = reinterpret_cast<PortableSha256*>(ctx_);
    const uint64_t bits = c->bits;
    c->block[c->used++] = 0x80;
    if (c->used > 56u) {
        std::memset(c->block + c->used, 0, 64u - c->used);
        sha256Transform(*c);
        c->used = 0;
    }
    std::memset(c->block + c->used, 0, 56u - c->used);
    for (int i = 0; i < 8; ++i)
        c->block[56 + i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
    sha256Transform(*c);
    static const char hex[] = "0123456789abcdef";
    std::string s;
    for (uint32_t word : c->state)
        for (int shift = 28; shift >= 0; shift -= 4)
            s += hex[(word >> shift) & 15];
    return s;
}
}
